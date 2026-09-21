#include <tbox/context.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "context/tbox_context_hit_test.h"
#include "test_support.h"

/* This is the first true end-to-end integration test in the codebase: it
 * drives the whole compute pipeline (parse -> cascade+style -> layout ->
 * render) through the one public tbox_context API, so it leans toward a
 * handful of realistic scenarios rather than exhaustive per-layer
 * permutations -- those already live in tests/style/, tests/layout/,
 * tests/render/. */

/* Mirrors tests/font/test_font.c's / tests/layout/test_layout.c's /
 * tests/render/test_render.c's read_file() helper: reads the vendored
 * LiberationSans-Regular.ttf into a malloc'd buffer so the embedded font
 * backend (no Fontconfig dependency) can load it. */
static char *read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)size);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)size;
    return buffer;
}

#ifndef TBOX_TEST_LIBERATION_SANS_PATH
#define TBOX_TEST_LIBERATION_SANS_PATH "external/liberation-sans/LiberationSans-Regular.ttf"
#endif

static bool string_view_equal_cstr(tbox_string_view view, const char *cstr) {
    size_t len = strlen(cstr);
    return view.size == len && (len == 0 || memcmp(view.data, cstr, len) == 0);
}

static tbox_context *open_cstr(const char *html, const char *css, tbox_font_face_cache *fonts) {
    return tbox_context_open(html, strlen(html), css, strlen(css), fonts);
}

/* Shared by every tbox_context_on_click test below: a click_capture is
 * handed in as `userdata` and a handler fills it in so the test can inspect
 * what fired (or that nothing did) after tbox_context_dispatch_click
 * returns. Handlers must be plain function pointers (tbox_context_click_handler),
 * so state cannot be a closure -- this is the same shape record_click's own
 * doc comment on tbox_context_click_handler describes. */
typedef struct click_capture {
    int call_count;
    const tbox_html_node *node;
} click_capture;

static void click_capture_reset(click_capture *capture) {
    capture->call_count = 0;
    capture->node       = NULL;
}

/* NOVO v3: tbox_context_click_handler now returns bool (true = keep
 * propagating). record_click always lets propagation continue, unless the
 * caller wants otherwise -- see record_click_stop below for
 * stopPropagation tests. */
static bool record_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    click_capture *capture = (click_capture *)userdata;
    capture->call_count++;
    capture->node = node;
    return true;
}

/* NOVO v3: same as record_click, but returns false (stopPropagation) -- for
 * tests proving a handler can prevent a farther ancestor's otherwise-
 * matching handler from firing. */
static bool record_click_stop(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    click_capture *capture = (click_capture *)userdata;
    capture->call_count++;
    capture->node = node;
    return false;
}

/* NOVO v3: shared by the bubbling-order tests -- each handler appends its
 * own tag (e.g. "inner"/"outer") to a fixed-size log so the test can verify
 * not just THAT both fired, but the ORDER they fired in (nearest ancestor
 * first). userdata is a bubble_log*; which tag a given registration appends
 * is baked into the handler function itself (bubble_log_append_inner/
 * _outer below), since a plain function pointer can't close over which tag
 * to use. */
typedef struct bubble_log {
    const char *entries[4];
    int count;
} bubble_log;

static void bubble_log_reset(bubble_log *log) {
    log->count = 0;
}

static void bubble_log_append(bubble_log *log, const char *tag) {
    if (log->count < 4) {
        log->entries[log->count] = tag;
    }
    log->count++;
}

static bool bubble_log_inner_handler(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    (void)node;
    bubble_log_append((bubble_log *)userdata, "inner");
    return true;
}

static bool bubble_log_outer_handler(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    (void)node;
    bubble_log_append((bubble_log *)userdata, "outer");
    return true;
}

/* Exercises tbox_context_document: a handler that mutates the clicked
 * node's `class` attribute via tbox_html_node_set_attribute, the same
 * pattern the v1 vertical slice (ARCHITECTURE.md's "Fatia vertical v1")
 * uses to swap a CSS class on click. */
static bool toggle_class_handler(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_html_document *document = tbox_context_document(ctx);
    tbox_html_node_set_attribute(document, node, tbox_string_view_make("class", 5), tbox_string_view_make("box on", 6));
    return true;
}

int tbox_test_context_run(void) {
    int failures = 0;

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");
    if (font_data == NULL) {
        return failures + 1;
    }

    /* NOVO v2: tbox_context_open now takes a tbox_font_face_cache, not a
     * single tbox_font_face -- same regular-bytes-for-both-slots
     * placeholder tbox_app_create uses (see src/app/tbox_app.c), fine here
     * since none of these tests exercise bold text specifically. */
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size);
    TBOX_TEST_ASSERT_MSG(fonts != NULL, "failed to create font face cache");
    if (fonts == NULL) {
        free(font_data);
        return failures + 1;
    }

    /* 1: a <div> styled with background-color, run end to end, produces a
     * display list with the expected FILL_RECT (rect == border_box,
     * color == the declared background-color). */
    {
        tbox_context *ctx = open_cstr("<div>x</div>", "div { width: 200px; height: 100px; background-color: rgb(10, 20, 30); }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed for well-formed HTML+CSS");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 1, "a lone styled div must produce exactly one paint op");
            if (list.count == 1) {
                TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
                TBOX_TEST_ASSERT(list.items[0].color.r == 10 && list.items[0].color.g == 20 && list.items[0].color.b == 30 && list.items[0].color.a == 255);
                TBOX_TEST_ASSERT(list.items[0].rect.width == 200.0 && list.items[0].rect.height == 100.0);
            }

            tbox_context_close(ctx);
        }
    }

    /* 2: a heading + paragraph produce the expected TEXT_RUN ops -- this
     * confirms the whole text pipeline (text_content ->
     * collapse_whitespace -> measure -> layout -> render) works glued
     * together end to end, not just per-layer. */
    {
        tbox_context *ctx = open_cstr("<div><h1>Hello</h1><p>world</p></div>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 2, "a heading + paragraph must produce exactly two TEXT_RUN ops");
            if (list.count == 2) {
                TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_TEXT_RUN);
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[0].text, "Hello"), "h1's TEXT_RUN text must be \"Hello\"");
                TBOX_TEST_ASSERT(list.items[1].kind == TBOX_PAINT_TEXT_RUN);
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[1].text, "world"), "p's TEXT_RUN text must be \"world\"");
            }

            tbox_context_close(ctx);
        }
    }

    /* 3: calling tbox_context_run_frame twice in a row (simulating a
     * resize) succeeds both times and produces correct, independent
     * results -- exercises the frame_arena reset actually invalidating
     * the first frame's data (box widths must track the new viewport,
     * not leftover/corrupted values from the first). */
    {
        tbox_context *ctx = open_cstr("<div>x</div>", "div { height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list first_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &first_list);
            const tbox_layout_box *first_root = tbox_context_hit_test(ctx, 1.0, 1.0);
            TBOX_TEST_ASSERT_MSG(first_root != NULL, "point inside the first frame's root box must hit");
            if (first_root != NULL) {
                TBOX_TEST_ASSERT(first_root->border_box.width == 800.0);
            }

            tbox_display_list second_list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &second_list);
            const tbox_layout_box *second_root = tbox_context_hit_test(ctx, 1.0, 1.0);
            TBOX_TEST_ASSERT_MSG(second_root != NULL, "point inside the second frame's root box must hit");
            if (second_root != NULL) {
                TBOX_TEST_ASSERT_MSG(second_root->border_box.width == 400.0, "second frame's box must reflect the NEW viewport, not stale data from the first");
            }

            tbox_context_close(ctx);
        }
    }

    /* 4: tbox_context_hit_test after a run_frame -- a point inside a
     * specific box's border_box returns that box (checked via its
     * `node`'s tag name), a point clearly outside any box's area returns
     * NULL. */
    {
        tbox_context *ctx = open_cstr("<div><div>a</div></div>", "div div { width: 50px; height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            /* The inner div sits at (0,0)-(50,50): a point inside it must
             * hit the inner box (the deepest match), not the outer one. */
            const tbox_layout_box *inner = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(inner != NULL, "a point inside the inner div must hit something");
            if (inner != NULL) {
                TBOX_TEST_ASSERT(inner->node != NULL);
                TBOX_TEST_ASSERT_MSG(inner->first_child == NULL, "the deepest match for a point inside the inner div must be the inner (leaf) box, not the outer one");
            }

            /* Clearly outside any box's area (the outer div's auto-height
             * shrinks to the inner div's 50px). */
            const tbox_layout_box *outside = tbox_context_hit_test(ctx, 5.0, 500.0);
            TBOX_TEST_ASSERT_MSG(outside == NULL, "a point below every box must miss");

            tbox_context_close(ctx);
        }
    }

    /* 5: tbox_context_hit_test before any run_frame call returns NULL --
     * must not crash on a NULL internal layout root. */
    {
        tbox_context *ctx = open_cstr("<div>x</div>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(hit == NULL, "hit_test before any run_frame must return NULL, not crash");

            tbox_context_close(ctx);
        }
    }

    /* 6: an empty/minimal HTML document doesn't crash run_frame and
     * produces a sensible (empty) display list. */
    {
        tbox_context *ctx = open_cstr("   ", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed even for a whitespace-only document");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            TBOX_TEST_ASSERT_MSG(list.count == 0, "a document with nothing to lay out must produce an empty display list");
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 1.0, 1.0) == NULL);

            tbox_context_close(ctx);
        }
    }

    /* 7: a click inside a matching element's box fires its handler exactly
     * once, with the clicked node itself. */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT_MSG(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture) >= 0, "tbox_context_on_click must succeed for a well-formed selector");

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "a click inside the button's box must dispatch");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 1, "the handler must fire exactly once");
            if (capture.call_count == 1) {
                TBOX_TEST_ASSERT_MSG(capture.node != NULL && capture.node->type == TBOX_HTML_NODE_ELEMENT, "the handler must receive an ELEMENT node");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(capture.node->element.tag_name, "button"), "the handler must receive the <button> node that was actually clicked");
            }

            tbox_context_close(ctx);
        }
    }

    /* 8: a click outside every box fires nothing. */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 500.0, 500.0);
            TBOX_TEST_ASSERT_MSG(!dispatched, "a click outside every box must not dispatch");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 0, "no handler may fire for a click outside every box");

            tbox_context_close(ctx);
        }
    }

    /* 9: two nested elements where only the OUTER one matches the
     * registered selector -- a click on the inner element must still fire
     * the handler, on the outer element (nearest matching ancestor), via
     * the node->parent walk. */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".outer", strlen(".outer"), record_click, &capture) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            /* (10, 10) sits inside the inner div's box -- the hit-tested
             * node is the inner div, which does NOT itself match ".outer". */
            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "a click on the inner div must still dispatch via the ancestor walk");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 1, "the .outer handler must fire exactly once");
            if (capture.call_count == 1) {
                const tbox_html_node *doc_root = tbox_html_document_root(tbox_context_document(ctx));
                const tbox_html_node *outer    = (doc_root != NULL) ? doc_root->first_child : NULL;
                TBOX_TEST_ASSERT_MSG(outer != NULL && string_view_equal_cstr(outer->element.tag_name, "div"), "test setup assumption: the document root's first child is the outer div");
                TBOX_TEST_ASSERT_MSG(capture.node == outer, "the handler must receive the OUTER node (nearest matching ancestor), not the inner node that was actually clicked");
            }

            tbox_context_close(ctx);
        }
    }

    /* 10: two handlers registered with different selectors, both applicable
     * to the same node, both fire on a single click -- and in registration
     * order. */
    {
        tbox_context *ctx = open_cstr("<div id=\"target\" class=\"box\">x</div>", "#target { width: 60px; height: 60px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture id_capture;
            click_capture class_capture;
            click_capture_reset(&id_capture);
            click_capture_reset(&class_capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "#target", strlen("#target"), record_click, &id_capture) >= 0);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".box", strlen(".box"), record_click, &class_capture) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "a click on the target div must dispatch");
            TBOX_TEST_ASSERT_MSG(id_capture.call_count == 1, "the #target handler must fire");
            TBOX_TEST_ASSERT_MSG(class_capture.call_count == 1, "the .box handler must ALSO fire -- both registrations match the same clicked node");
            TBOX_TEST_ASSERT_MSG(id_capture.node == class_capture.node, "both handlers must receive the same (target) node");

            tbox_context_close(ctx);
        }
    }

    /* 11: a syntax error in the selector makes tbox_context_on_click return
     * false and register nothing (a later dispatch must not invoke it). */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);

            /* ">" alone is a leading combinator with no simple selector
             * before it -- a syntax error (same example
             * tests/css_selector/test_*.c already uses for
             * tbox_css_selector_compile). */
            int registered = tbox_context_on_click(ctx, ">", strlen(">"), record_click, &capture);
            TBOX_TEST_ASSERT_MSG(registered == -1, "a selector syntax error must make tbox_context_on_click return -1");

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(!dispatched, "nothing was registered, so nothing may dispatch");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 0, "a handler whose registration failed must never fire");

            tbox_context_close(ctx);
        }
    }

    /* 12: tbox_context_dispatch_click before any tbox_context_run_frame
     * returns false without crashing -- same guard as
     * tbox_context_hit_test. */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture) >= 0);

            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(!dispatched, "dispatch before any run_frame must return false, not crash");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 0, "no handler may fire before there is any layout to hit-test against");

            tbox_context_close(ctx);
        }
    }

    /* 13: tbox_context_document gives a handler's body a document it can
     * pass to tbox_html_node_set_attribute -- mirrors the v1 vertical
     * slice's ".off"/".on" class-swap-on-click scenario end to end (click
     * mutates the attribute, the NEXT run_frame's display list reflects the
     * new class's declaration). */
    {
        tbox_context *ctx = open_cstr("<div class=\"box off\">x</div>",
            ".off { width: 40px; height: 40px; background-color: rgb(0, 0, 0); }"
            ".on  { width: 40px; height: 40px; background-color: rgb(255, 0, 0); }",
            fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".off", strlen(".off"), toggle_class_handler, NULL) >= 0);

            tbox_display_list before_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &before_list);
            TBOX_TEST_ASSERT_MSG(before_list.count == 1, "the .off box must paint one FILL_RECT before the click");
            if (before_list.count == 1) {
                TBOX_TEST_ASSERT_MSG(before_list.items[0].color.r == 0 && before_list.items[0].color.g == 0 && before_list.items[0].color.b == 0, "the box must start with .off's black background");
            }

            bool dispatched = tbox_context_dispatch_click(ctx, 5.0, 5.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "the click must dispatch to the .off handler");

            tbox_display_list after_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &after_list);
            TBOX_TEST_ASSERT_MSG(after_list.count == 1, "the (now) .on box must still paint one FILL_RECT after the click");
            if (after_list.count == 1) {
                TBOX_TEST_ASSERT_MSG(after_list.items[0].color.r == 255 && after_list.items[0].color.g == 0 && after_list.items[0].color.b == 0, "after the click, tbox_html_node_set_attribute must have swapped the class so the box now paints .on's red background");
            }

            tbox_context_close(ctx);
        }
    }

    /* NOVO v2 (Tarefa 4): end-to-end proof that the user-agent stylesheet
     * (tbox_ua_style_config_default(), generated internally by
     * tbox_context_open) actually reaches the cascade -- an <h1> with NO
     * author CSS whatsoever must resolve to a visibly larger font than an
     * equivalent <p>, purely from UA declarations. Both boxes sit at the
     * document root (a single top-level tag), so each one's own margin-top
     * (UA default, known via tbox_ua_style_config_default()) offsets its
     * border_box away from (0,0) -- hit-testing well inside that offset,
     * rather than at (0,0), is what makes this reliable regardless of the
     * exact margin values. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        tbox_context *h1_ctx = open_cstr("<h1>oi</h1>", "", fonts);
        tbox_context *p_ctx  = open_cstr("<p>oi</p>", "", fonts);
        TBOX_TEST_ASSERT_MSG(h1_ctx != NULL && p_ctx != NULL, "tbox_context_open must succeed for both documents");
        if (h1_ctx != NULL && p_ctx != NULL) {
            tbox_display_list h1_list, p_list;
            tbox_context_run_frame(h1_ctx, 800.0, 600.0, &h1_list);
            tbox_context_run_frame(p_ctx, 800.0, 600.0, &p_list);

            const tbox_layout_box *h1_box = tbox_context_hit_test(h1_ctx, 5.0, default_config.margin.heading_px[0] + 2.0);
            const tbox_layout_box *p_box  = tbox_context_hit_test(p_ctx, 5.0, default_config.margin.paragraph_px + 2.0);
            TBOX_TEST_ASSERT_MSG(h1_box != NULL && p_box != NULL, "hit-testing just past each box's own UA margin-top must land inside its border_box");
            if (h1_box != NULL && p_box != NULL) {
                TBOX_TEST_ASSERT_MSG(h1_box->text_run_count > 0 && p_box->text_run_count > 0, "both <h1>oi</h1> and <p>oi</p> must produce a text run");
                if (h1_box->text_run_count > 0 && p_box->text_run_count > 0) {
                    double h1_line_height = tbox_font_face_line_height(h1_box->text_runs[0].font);
                    double p_line_height  = tbox_font_face_line_height(p_box->text_runs[0].font);
                    TBOX_TEST_ASSERT_MSG(h1_line_height > p_line_height, "the UA stylesheet's h1 { font-size: 2em } must resolve to a taller line than p's inherited 1em, with no author CSS involved");
                }
            }

            tbox_context_close(h1_ctx);
            tbox_context_close(p_ctx);
        }
    }

    /* 15: author CSS wins over the user-agent stylesheet -- h1 { font-size:
     * 10px } (author) must produce a SMALLER heading than the UA default's
     * 2em (32px at the 16px root default), exercising real origin priority
     * end to end with two genuine stylesheets (not the synthetic 2-source
     * unit test in tests/style/test_style.c). */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        tbox_context *default_ctx  = open_cstr("<h1>oi</h1>", "", fonts);
        tbox_context *overridden_ctx = open_cstr("<h1>oi</h1>", "h1 { font-size: 10px; }", fonts);
        TBOX_TEST_ASSERT_MSG(default_ctx != NULL && overridden_ctx != NULL, "tbox_context_open must succeed for both documents");
        if (default_ctx != NULL && overridden_ctx != NULL) {
            tbox_display_list default_list, overridden_list;
            tbox_context_run_frame(default_ctx, 800.0, 600.0, &default_list);
            tbox_context_run_frame(overridden_ctx, 800.0, 600.0, &overridden_list);

            /* Author CSS declares no margin, so the UA h1 margin-top (21px
             * default) still applies to both documents -- same hit-test
             * offset as test 14 above. */
            double hit_y = default_config.margin.heading_px[0] + 2.0;
            const tbox_layout_box *default_box    = tbox_context_hit_test(default_ctx, 5.0, hit_y);
            const tbox_layout_box *overridden_box = tbox_context_hit_test(overridden_ctx, 5.0, hit_y);
            TBOX_TEST_ASSERT_MSG(default_box != NULL && overridden_box != NULL, "hit-testing just past the UA margin-top must land inside both boxes");
            if (default_box != NULL && overridden_box != NULL) {
                TBOX_TEST_ASSERT_MSG(default_box->text_run_count > 0 && overridden_box->text_run_count > 0, "both documents must produce a text run for their <h1>");
                if (default_box->text_run_count > 0 && overridden_box->text_run_count > 0) {
                    double default_line_height    = tbox_font_face_line_height(default_box->text_runs[0].font);
                    double overridden_line_height = tbox_font_face_line_height(overridden_box->text_runs[0].font);
                    TBOX_TEST_ASSERT_MSG(overridden_line_height < default_line_height, "author CSS's h1 { font-size: 10px } must beat the UA stylesheet's 2em default -- author outranks user-agent");
                }
            }

            tbox_context_close(default_ctx);
            tbox_context_close(overridden_ctx);
        }
    }

    /* 16: tbox_context_open_with_config with a custom
     * config.font.heading_em[0] resolves a measurably LARGER <h1> than the
     * default config -- proves the config struct's fields genuinely drive
     * the generated UA stylesheet text, not just tbox_ua_style_config_default()'s
     * own baked-in values. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();
        tbox_ua_style_config custom_config  = default_config;
        custom_config.font.heading_em[0]    = 5.0;

        tbox_context *default_ctx = tbox_context_open_with_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, default_config);
        tbox_context *custom_ctx  = tbox_context_open_with_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, custom_config);
        TBOX_TEST_ASSERT_MSG(default_ctx != NULL && custom_ctx != NULL, "tbox_context_open_with_config must succeed for both configs");
        if (default_ctx != NULL && custom_ctx != NULL) {
            tbox_display_list default_list, custom_list;
            tbox_context_run_frame(default_ctx, 800.0, 600.0, &default_list);
            tbox_context_run_frame(custom_ctx, 800.0, 600.0, &custom_list);

            /* Neither config changes margin, so both still offset by the
             * same UA heading margin-top. */
            double hit_y = default_config.margin.heading_px[0] + 2.0;
            const tbox_layout_box *default_box = tbox_context_hit_test(default_ctx, 5.0, hit_y);
            const tbox_layout_box *custom_box   = tbox_context_hit_test(custom_ctx, 5.0, hit_y);
            TBOX_TEST_ASSERT_MSG(default_box != NULL && custom_box != NULL, "hit-testing just past the UA margin-top must land inside both boxes");
            if (default_box != NULL && custom_box != NULL) {
                TBOX_TEST_ASSERT_MSG(default_box->text_run_count > 0 && custom_box->text_run_count > 0, "both documents must produce a text run for their <h1>");
                if (default_box->text_run_count > 0 && custom_box->text_run_count > 0) {
                    double default_line_height = tbox_font_face_line_height(default_box->text_runs[0].font);
                    double custom_line_height  = tbox_font_face_line_height(custom_box->text_runs[0].font);
                    TBOX_TEST_ASSERT_MSG(custom_line_height > default_line_height, "config.font.heading_em[0] == 5.0 must resolve to a taller h1 line than the default config's 2.0");
                }
            }

            tbox_context_close(default_ctx);
            tbox_context_close(custom_ctx);
        }
    }

    /* 17: a <p> with no author CSS has a non-zero UA-default margin
     * reflected between its margin_box and content_box (UA: p { margin:
     * 16px 0 }). */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        tbox_context *ctx = open_cstr("<p>oi</p>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            const tbox_layout_box *box = tbox_context_hit_test(ctx, 5.0, default_config.margin.paragraph_px + 2.0);
            TBOX_TEST_ASSERT_MSG(box != NULL, "hit-testing just past the UA margin-top must land inside the p's border_box");
            if (box != NULL) {
                TBOX_TEST_ASSERT_MSG(box->margin_box.height > box->content_box.height, "the UA p margin must make margin_box strictly taller than content_box");
                TBOX_TEST_ASSERT_MSG(box->content_box.y - box->margin_box.y == default_config.margin.paragraph_px, "content_box must sit exactly one UA paragraph margin-top below margin_box's top edge");
            }

            tbox_context_close(ctx);
        }
    }

    /* 18: tbox_context_open_with_config with a custom config.font.base_px
     * resolves a measurably LARGER <p> line height than the default config
     * -- proves base_px is actually wired into the generated UA
     * stylesheet's `body { font-size: ... }` declaration, not inert (a
     * bug found and fixed after the task that introduced the config
     * struct: base_px used to be stored but never emitted). <p> itself
     * declares no font-size of its own, so its resolved font-size is
     * purely inherited from body's -- the most direct possible proof that
     * base_px reaches the cascade. Deliberately wrapped in an explicit
     * <body> (unlike test 17's bare "<p>oi</p>"): the UA `body { font-size:
     * ... }` rule can only match a document that actually HAS a <body>
     * element -- a root <p> with no parent at all falls back to the Style
     * layer's own hardcoded 16px regardless of base_px, which is the exact
     * pre-existing caveat documented on tbox_ua_style_font_config::base_px
     * in <tbox/context.h>. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();
        tbox_ua_style_config custom_config  = default_config;
        custom_config.font.base_px          = 32.0;

        const char *html = "<body><p>oi</p></body>";
        tbox_context *default_ctx = tbox_context_open_with_config(html, strlen(html), "", 0, fonts, default_config);
        tbox_context *custom_ctx  = tbox_context_open_with_config(html, strlen(html), "", 0, fonts, custom_config);
        TBOX_TEST_ASSERT_MSG(default_ctx != NULL && custom_ctx != NULL, "tbox_context_open_with_config must succeed for both configs");
        if (default_ctx != NULL && custom_ctx != NULL) {
            tbox_display_list default_list, custom_list;
            tbox_context_run_frame(default_ctx, 800.0, 600.0, &default_list);
            tbox_context_run_frame(custom_ctx, 800.0, 600.0, &custom_list);

            /* body's own UA margin (8px, ALL FOUR sides -- single-value
             * shorthand, unlike h1-h6/p's two-value "Npx 0px") plus the
             * <p>'s UA margin-top (16px) stack before the <p>'s content
             * starts, on BOTH axes for body's margin. hit_x must clear
             * body's left margin (8px), not just >= 0 -- neither margin
             * depends on font_size, so both offsets are identical for both
             * configs. */
            double hit_x                       = default_config.margin.body_px + 2.0;
            double hit_y                       = default_config.margin.body_px + default_config.margin.paragraph_px + 2.0;
            const tbox_layout_box *default_box = tbox_context_hit_test(default_ctx, hit_x, hit_y);
            const tbox_layout_box *custom_box  = tbox_context_hit_test(custom_ctx, hit_x, hit_y);
            TBOX_TEST_ASSERT_MSG(default_box != NULL && custom_box != NULL, "hit-testing just past body's UA margin (both axes) and the p's UA margin-top must land inside both boxes");
            if (default_box != NULL && custom_box != NULL) {
                TBOX_TEST_ASSERT_MSG(default_box->text_run_count > 0 && custom_box->text_run_count > 0, "both documents must produce a text run for their <p>");
                if (default_box->text_run_count > 0 && custom_box->text_run_count > 0) {
                    double default_line_height = tbox_font_face_line_height(default_box->text_runs[0].font);
                    double custom_line_height  = tbox_font_face_line_height(custom_box->text_runs[0].font);
                    TBOX_TEST_ASSERT_MSG(custom_line_height > default_line_height, "config.font.base_px == 32 must resolve to a taller p line than the default config's 16 -- base_px must not be inert");
                }
            }

            tbox_context_close(default_ctx);
            tbox_context_close(custom_ctx);
        }
    }

    /* 19: tbox_context_update_hover changes hover state when the point
     * moves onto an element's box, and returns true only when something
     * actually changed. */
    {
        tbox_context *ctx = open_cstr("<div>x</div>", "div { width: 100px; height: 100px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool changed = tbox_context_update_hover(ctx, true, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(changed, "hovering a point inside the div's box for the first time must change the hover state");

            bool changed_again = tbox_context_update_hover(ctx, true, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(!changed_again, "calling update_hover again with the SAME position must report no change");

            tbox_context_close(ctx);
        }
    }

    /* 20: has_position == false un-hovers a previously hovered element
     * (and reports that as a change); with nothing hovered to begin with,
     * it is a no-op (no change). */
    {
        tbox_context *ctx = open_cstr("<div>x</div>", "div { width: 100px; height: 100px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(!tbox_context_update_hover(ctx, false, 0.0, 0.0), "has_position == false with nothing hovered to begin with must report no change");

            TBOX_TEST_ASSERT_MSG(tbox_context_update_hover(ctx, true, 10.0, 10.0), "hovering the div must change the hover state");
            TBOX_TEST_ASSERT_MSG(tbox_context_update_hover(ctx, false, 0.0, 0.0), "has_position == false (pointer left the window) must un-hover a previously hovered element, reporting a change");
            TBOX_TEST_ASSERT_MSG(!tbox_context_update_hover(ctx, false, 0.0, 0.0), "calling again with has_position == false must report no further change");

            tbox_context_close(ctx);
        }
    }

    /* 21: end-to-end proof that :hover reaches the cascade -- a
     * .box:hover author rule resolved via tbox_context_run_frame AFTER
     * update_hover points at the element produces the hover color in the
     * resulting FILL_RECT; not hovering (or un-hovering) produces the
     * normal color. */
    {
        tbox_context *ctx = open_cstr("<div class=\"box\">x</div>",
            ".box { width: 100px; height: 100px; background-color: rgb(0, 0, 0); }"
            ".box:hover { background-color: rgb(255, 0, 0); }",
            fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list before_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &before_list);
            TBOX_TEST_ASSERT_MSG(before_list.count == 1, "the box must paint one FILL_RECT before any hover");
            if (before_list.count == 1) {
                TBOX_TEST_ASSERT_MSG(before_list.items[0].color.r == 0 && before_list.items[0].color.g == 0 && before_list.items[0].color.b == 0, "with nothing hovered, the box must paint its normal (non-hover) background");
            }

            TBOX_TEST_ASSERT_MSG(tbox_context_update_hover(ctx, true, 10.0, 10.0), "hovering the box must change the hover state");

            tbox_display_list hover_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &hover_list);
            TBOX_TEST_ASSERT_MSG(hover_list.count == 1, "the box must still paint one FILL_RECT while hovered");
            if (hover_list.count == 1) {
                TBOX_TEST_ASSERT_MSG(hover_list.items[0].color.r == 255 && hover_list.items[0].color.g == 0 && hover_list.items[0].color.b == 0, ".box:hover's background-color must win while the box is hovered");
            }

            TBOX_TEST_ASSERT_MSG(tbox_context_update_hover(ctx, false, 0.0, 0.0), "un-hovering must change the hover state");

            tbox_display_list after_list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &after_list);
            TBOX_TEST_ASSERT_MSG(after_list.count == 1, "the box must still paint one FILL_RECT after un-hovering");
            if (after_list.count == 1) {
                TBOX_TEST_ASSERT_MSG(after_list.items[0].color.r == 0 && after_list.items[0].color.g == 0 && after_list.items[0].color.b == 0, "after un-hovering, the box must paint its normal background again");
            }

            tbox_context_close(ctx);
        }
    }

    /* 22: a click on a nested node with handlers registered on TWO
     * different ancestors fires both, nearest ancestor first -- real
     * bubbling order (checked via a shared log both handlers append to). */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            bubble_log log;
            bubble_log_reset(&log);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".inner", strlen(".inner"), bubble_log_inner_handler, &log) >= 0);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".outer", strlen(".outer"), bubble_log_outer_handler, &log) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            /* (10, 10) sits inside the inner div's box. */
            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "a click on the inner div must dispatch");
            TBOX_TEST_ASSERT_MSG(log.count == 2, "both the .inner and .outer handlers must fire");
            if (log.count == 2) {
                TBOX_TEST_ASSERT_MSG(strcmp(log.entries[0], "inner") == 0, "the .inner handler (nearest ancestor) must fire FIRST");
                TBOX_TEST_ASSERT_MSG(strcmp(log.entries[1], "outer") == 0, "the .outer handler (farther ancestor) must fire SECOND -- real bubbling order");
            }

            tbox_context_close(ctx);
        }
    }

    /* 23: a handler returning false (stopPropagation) prevents a farther
     * ancestor's otherwise-matching handler from firing at all. */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture inner_capture;
            click_capture outer_capture;
            click_capture_reset(&inner_capture);
            click_capture_reset(&outer_capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".inner", strlen(".inner"), record_click_stop, &inner_capture) >= 0);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".outer", strlen(".outer"), record_click, &outer_capture) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "the .inner handler firing must count as a dispatch");
            TBOX_TEST_ASSERT_MSG(inner_capture.call_count == 1, "the .inner handler must fire exactly once");
            TBOX_TEST_ASSERT_MSG(outer_capture.call_count == 0, "the .outer handler must NOT fire -- the .inner handler's false return must stop propagation before the outer ancestor is even tested");

            tbox_context_close(ctx);
        }
    }

    /* 24: tbox_context_unbind_click followed by a new dispatch at the same
     * point no longer fires the unbound handler. */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            int binding = tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture);
            TBOX_TEST_ASSERT_MSG(binding >= 0, "tbox_context_on_click must succeed for a well-formed selector");

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched_before = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched_before, "the handler must fire once before it is unbound");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 1, "the handler must have fired exactly once before unbind");

            TBOX_TEST_ASSERT_MSG(tbox_context_unbind_click(ctx, binding), "unbinding a currently-active registration must succeed");

            bool dispatched_after = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(!dispatched_after, "no handler may fire once the only registration has been unbound");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 1, "the unbound handler must not fire again on a later dispatch");

            tbox_context_close(ctx);
        }
    }

    /* 25: tbox_context_unbind_click with an invalid or already-removed
     * handle returns false without crashing. */
    {
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            TBOX_TEST_ASSERT_MSG(!tbox_context_unbind_click(ctx, 12345), "unbinding a handle that was never registered must return false, not crash");

            click_capture capture;
            click_capture_reset(&capture);
            int binding = tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture);
            TBOX_TEST_ASSERT_MSG(binding >= 0, "tbox_context_on_click must succeed for a well-formed selector");

            TBOX_TEST_ASSERT_MSG(tbox_context_unbind_click(ctx, binding), "the first unbind of a real registration must succeed");
            TBOX_TEST_ASSERT_MSG(!tbox_context_unbind_click(ctx, binding), "unbinding the SAME handle a second time must return false, not crash");

            tbox_context_close(ctx);
        }
    }

    /* 26: v5's Orchestration hit-test fix -- tbox_context_hit_test_box
     * called directly against a hand-built tbox_layout_box tree (no HTML/
     * CSS, no Style/Layout pipeline needed: this is about the hit-test
     * algorithm itself, see ARCHITECTURE.md's "Orchestration -- correção
     * de hit-test pra caixas fora de fluxo"). Two artificially overlapping
     * SIBLINGS: a point in the overlap must resolve to the LATER sibling
     * in `next_sibling` order (the one painted on top, since the project
     * has no stacking context/z-index and always paints in document
     * order), not the first one that happens to match. */
    {
        tbox_layout_box root_box;
        memset(&root_box, 0, sizeof(root_box));
        root_box.border_box = (tbox_rect){0.0, 0.0, 100.0, 100.0};

        tbox_layout_box sibling_a;
        memset(&sibling_a, 0, sizeof(sibling_a));
        sibling_a.parent     = &root_box;
        sibling_a.border_box = (tbox_rect){0.0, 0.0, 50.0, 50.0};

        tbox_layout_box sibling_b;
        memset(&sibling_b, 0, sizeof(sibling_b));
        sibling_b.parent     = &root_box;
        /* Deliberately overlapping sibling_a in [10,50) x [10,50). */
        sibling_b.border_box = (tbox_rect){10.0, 10.0, 50.0, 50.0};

        root_box.first_child      = &sibling_a;
        root_box.last_child       = &sibling_b;
        sibling_a.next_sibling    = &sibling_b;

        const tbox_layout_box *overlap_hit = tbox_context_hit_test_box(&root_box, 20.0, 20.0);
        TBOX_TEST_ASSERT_MSG(overlap_hit == &sibling_b, "a point in the overlap of two siblings must resolve to the LATER sibling in next_sibling order (painted on top), not the first match found by recursion");

        /* Sanity: a point inside sibling_a's exclusive area (not covered by
         * sibling_b) must still resolve to sibling_a -- the "last match
         * wins" rule only matters when more than one candidate matches. */
        const tbox_layout_box *exclusive_hit = tbox_context_hit_test_box(&root_box, 5.0, 5.0);
        TBOX_TEST_ASSERT_MSG(exclusive_hit == &sibling_a, "a point matching only sibling_a must still resolve to sibling_a");
    }

    /* 27: a child positioned entirely OUTSIDE its own DOM parent's
     * border_box (as an out-of-flow `position: absolute`/`fixed` box can
     * be, from v5 on) is still found -- proof that the hit-test no longer
     * returns early just because the parent's border_box misses the
     * point. */
    {
        tbox_layout_box parent_box;
        memset(&parent_box, 0, sizeof(parent_box));
        parent_box.border_box = (tbox_rect){0.0, 0.0, 50.0, 50.0};

        tbox_layout_box escaped_child;
        memset(&escaped_child, 0, sizeof(escaped_child));
        escaped_child.parent     = &parent_box;
        /* Fully outside parent_box's border_box ([0,50) x [0,50)). */
        escaped_child.border_box = (tbox_rect){200.0, 200.0, 30.0, 30.0};

        parent_box.first_child = &escaped_child;
        parent_box.last_child  = &escaped_child;

        /* A point that only falls inside the escaped child, not the parent. */
        const tbox_layout_box *escaped_hit = tbox_context_hit_test_box(&parent_box, 210.0, 210.0);
        TBOX_TEST_ASSERT_MSG(escaped_hit == &escaped_child, "a child positioned outside its own parent's border_box must still be found by a point that falls only inside the child");

        /* A point inside the parent only (the child doesn't match) must
         * still fall back to the parent itself. */
        const tbox_layout_box *parent_hit = tbox_context_hit_test_box(&parent_box, 5.0, 5.0);
        TBOX_TEST_ASSERT_MSG(parent_hit == &parent_box, "a point inside the parent but outside every child must still resolve to the parent itself");

        /* A point inside neither must miss entirely. */
        const tbox_layout_box *miss = tbox_context_hit_test_box(&parent_box, 1000.0, 1000.0);
        TBOX_TEST_ASSERT_MSG(miss == NULL, "a point inside neither the parent nor any child must return NULL");
    }

    /* 28: NOVO v8 -- a <ul><li>x</li></ul> with no author CSS gets a
     * vertical margin (UA: "ul, ol { margin: %gpx 0px; }", config.margin.
     * list_px) AND a horizontal indentation (UA: "ul, ol { ... padding:
     * 0px 0px 0px %gpx; }", config.list_padding_left_px) by default, same
     * as every real browser. Hit-testing at a point that clears the <ul>'s
     * own margin-top but stays INSIDE its padding-left gutter (x well below
     * list_padding_left_px) must land on the <ul>'s own border_box, not the
     * nested <li>'s -- the <li>'s border_box only starts at the <ul>'s
     * content_box (i.e. past the padding-left), same reasoning test 17
     * below uses for <p>'s margin, one level of nesting deeper. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        TBOX_TEST_ASSERT_MSG(default_config.list_padding_left_px > 5.0, "test setup assumption: x = 5.0 must fall inside the padding-left gutter (outside the nested <li>'s own box) for this hit-test to land on the <ul>");

        tbox_context *ctx = open_cstr("<ul><li>x</li></ul>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            const tbox_layout_box *ul_box = tbox_context_hit_test(ctx, 5.0, default_config.margin.list_px + 2.0);
            TBOX_TEST_ASSERT_MSG(ul_box != NULL, "hit-testing just past the UA margin-top, inside the padding-left gutter, must land inside the <ul>'s own border_box");
            if (ul_box != NULL) {
                TBOX_TEST_ASSERT_MSG(ul_box->node != NULL && string_view_equal_cstr(ul_box->node->element.tag_name, "ul"), "test setup assumption: the point picked (inside the padding-left gutter) must hit the <ul> box itself, not the nested <li>");
                TBOX_TEST_ASSERT_MSG(ul_box->content_box.y - ul_box->margin_box.y == default_config.margin.list_px, "the <ul>'s content_box must sit exactly one UA list margin-top below its margin_box's top edge");
                TBOX_TEST_ASSERT_MSG(ul_box->content_box.x - ul_box->border_box.x == default_config.list_padding_left_px, "the <ul>'s content_box must be indented from its border_box by exactly the UA list padding-left");
            }

            tbox_context_close(ctx);
        }
    }

    /* 29: regression -- a <p>/<h1> in isolation (no <ul>/<ol> anywhere)
     * still resolve to exactly the same UA margin as before the v8
     * ul/ol/li template lines and TBOX_UA_STYLE_CSS_BUFFER_SIZE bump were
     * added -- proves growing the template/buffer didn't perturb the
     * elements that already existed. Same assertions test 17 above already
     * makes for <p> alone; repeated here (plus <h1>) explicitly as a
     * regression check tied to this task's template/buffer change. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        tbox_context *p_ctx  = open_cstr("<p>oi</p>", "", fonts);
        tbox_context *h1_ctx = open_cstr("<h1>oi</h1>", "", fonts);
        TBOX_TEST_ASSERT_MSG(p_ctx != NULL && h1_ctx != NULL, "tbox_context_open must succeed for both documents");
        if (p_ctx != NULL && h1_ctx != NULL) {
            tbox_display_list p_list, h1_list;
            tbox_context_run_frame(p_ctx, 800.0, 600.0, &p_list);
            tbox_context_run_frame(h1_ctx, 800.0, 600.0, &h1_list);

            const tbox_layout_box *p_box  = tbox_context_hit_test(p_ctx, 5.0, default_config.margin.paragraph_px + 2.0);
            const tbox_layout_box *h1_box = tbox_context_hit_test(h1_ctx, 5.0, default_config.margin.heading_px[0] + 2.0);
            TBOX_TEST_ASSERT_MSG(p_box != NULL && h1_box != NULL, "hit-testing just past each box's own UA margin-top must land inside its border_box");
            if (p_box != NULL && h1_box != NULL) {
                TBOX_TEST_ASSERT_MSG(p_box->content_box.y - p_box->margin_box.y == default_config.margin.paragraph_px, "the <p>'s UA margin-top must be unchanged by the v8 template/buffer change");
                TBOX_TEST_ASSERT_MSG(h1_box->content_box.y - h1_box->margin_box.y == default_config.margin.heading_px[0], "the <h1>'s UA margin-top must be unchanged by the v8 template/buffer change");
                TBOX_TEST_ASSERT_MSG(p_box->content_box.x == p_box->border_box.x, "the <p>'s UA stylesheet declares no padding -- content_box and border_box must still align horizontally, unaffected by <ul>/<ol>'s new padding-left rule");
                TBOX_TEST_ASSERT_MSG(h1_box->content_box.x == h1_box->border_box.x, "the <h1>'s UA stylesheet declares no padding -- content_box and border_box must still align horizontally, unaffected by <ul>/<ol>'s new padding-left rule");
            }

            tbox_context_close(p_ctx);
            tbox_context_close(h1_ctx);
        }
    }

    /* 30: NOVO v9 -- an embedded <style>.algo{background-color:blue;}</style>
     * (no external CSS at all -- `css` is "") must reach the cascade: a
     * <div class="algo"> resolves the FILL_RECT blue, proving <style>
     * content, until now inert text (same treatment as <script>), now
     * participates in the cascade.
     *
     * Wrapped in an outer <div> (rather than two top-level siblings):
     * tbox_layout_build only ever lays out the FIRST top-level element under
     * the document root (a pre-existing v0 limitation, unrelated to this
     * task -- see ARCHITECTURE.md's v9 "Escopo" note about a <style> inside
     * a never-drawn <head>). A bare <style>.algo{...}</style><div
     * class="algo">...</div> at the top level would make <style> itself the
     * one element Layout builds, and the following <div> would never be laid
     * out at all. Nesting both inside one wrapper sidesteps that: the
     * wrapper is the sole top-level element, and <style> (not one of Layout
     * Tree's fixed text-tag list -- h1-h6/p/li -- so its raw text content is
     * never collected as words) paints nothing of its own, leaving the FILL_RECT
     * count entirely attributable to div.algo. */
    {
        tbox_context *ctx = open_cstr(
            "<div><style>.algo{background-color:blue;}</style><div class=\"algo\">x</div></div>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed for a document with an embedded <style>");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 1, "the .algo div must paint exactly one FILL_RECT");
            if (list.count == 1) {
                TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 0 && list.items[0].color.g == 0 && list.items[0].color.b == 255, "an internal <style> rule with no external CSS at all must resolve -- .algo must paint blue");
            }

            tbox_context_close(ctx);
        }
    }

    /* 31: the same document, but now with an external stylesheet declaring
     * .algo with the SAME specificity (a single class) but a DIFFERENT
     * color -- the internal <style> must win the tie against the external
     * CSS (decision documented in ARCHITECTURE.md's v9 "Escopo": internal
     * is placed LAST in tbox_context_run_frame's sources array). */
    {
        tbox_context *ctx = open_cstr(
            "<div><style>.algo{background-color:blue;}</style><div class=\"algo\">x</div></div>",
            ".algo{background-color:green;}", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 1, "the .algo div must paint exactly one FILL_RECT");
            if (list.count == 1) {
                TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 0 && list.items[0].color.g == 0 && list.items[0].color.b == 255, "at equal (class) specificity, the internal <style> must win the tie against the external CSS -- .algo must still paint blue, not green");
            }

            tbox_context_close(ctx);
        }
    }

    /* 32: regression -- a document with NO <style> element embedded at all
     * still resolves the external CSS exactly as before (no behavior
     * change for documents that don't use the new feature). */
    {
        tbox_context *ctx = open_cstr(
            "<div class=\"algo\">x</div>", ".algo{background-color:green;}", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 1, "the .algo div must paint exactly one FILL_RECT");
            if (list.count == 1) {
                TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 0 && list.items[0].color.g == 128 && list.items[0].color.b == 0, "a document with no embedded <style> at all must resolve the external CSS unchanged -- .algo must paint green");
            }

            tbox_context_close(ctx);
        }
    }

    /* 33: two SEPARATE <style> blocks, at different positions in the
     * document, both apply -- proof the concatenation in
     * tbox_context_collect_style_elements walks the WHOLE tree and doesn't
     * stop at (or drop) any block after the first one found. */
    {
        tbox_context *ctx = open_cstr(
            "<div>"
            "<style>.a{background-color:blue;}</style>"
            "<div class=\"a\">x</div>"
            "<style>.b{background-color:green;}</style>"
            "<div class=\"b\">y</div>"
            "</div>",
            "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 2, "both divs must paint their own FILL_RECT");
            if (list.count == 2) {
                TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 0 && list.items[0].color.g == 0 && list.items[0].color.b == 255, "the FIRST <style> block's rule (.a -> blue) must apply to the first div");
                TBOX_TEST_ASSERT_MSG(list.items[1].color.r == 0 && list.items[1].color.g == 128 && list.items[1].color.b == 0, "the SECOND <style> block's rule (.b -> green), found later in the tree, must also apply -- not dropped by the concatenation");
            }

            tbox_context_close(ctx);
        }
    }

    /* 34: NOVO v11 -- a <hr> alone (no author CSS) gets an explicit height
     * (UA: "hr { ... height: %gpx; ... }", config.hr_height_px) AND a
     * vertical margin (UA: "hr { ... margin: %gpx 0px; }", config.margin.
     * hr_px) by default -- same shape of assertion test 17 already makes
     * for <p>'s UA margin, plus a content_box.height check since <hr>,
     * unlike <p>, gets an explicit UA height rather than one derived from
     * its content. Hit-testing at margin.hr_px + 1.0 deliberately stays
     * WELL inside the 2px-tall content_box (1.0 < hr_height_px's default
     * of 2.0), not just past the margin-top, so the point cannot
     * accidentally land past the box entirely. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        TBOX_TEST_ASSERT_MSG(default_config.hr_height_px > 1.0, "test setup assumption: hit_y = margin.hr_px + 1.0 must fall inside the <hr>'s own content_box (height > 1.0)");

        tbox_context *ctx = open_cstr("<hr>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            const tbox_layout_box *hr_box = tbox_context_hit_test(ctx, 5.0, default_config.margin.hr_px + 1.0);
            TBOX_TEST_ASSERT_MSG(hr_box != NULL, "hit-testing just past the UA margin-top, still inside the 2px content_box, must land inside the <hr>'s own border_box");
            if (hr_box != NULL) {
                TBOX_TEST_ASSERT_MSG(hr_box->node != NULL && string_view_equal_cstr(hr_box->node->element.tag_name, "hr"), "test setup assumption: the point picked must hit the <hr> box itself");
                TBOX_TEST_ASSERT_MSG(hr_box->content_box.height == default_config.hr_height_px, "the <hr>'s content_box height must equal the UA default hr_height_px -- proves the explicit `height` declaration reached layout");
                TBOX_TEST_ASSERT_MSG(hr_box->content_box.y - hr_box->margin_box.y == default_config.margin.hr_px, "the <hr>'s content_box must sit exactly one UA hr margin-top below its margin_box's top edge");
            }

            tbox_context_close(ctx);
        }
    }

    /* 35: NOVO v11 -- a <hr> alone resolves a Gray (0x80, 0x80, 0x80)
     * background-color by default (UA: "hr { ... background-color: gray;
     * ... }"), proving the named color "gray" reaches the cascade with no
     * changes to the Style layer/CSS Cascade -- same FILL_RECT inspection
     * pattern as test 1 above. */
    {
        tbox_context *ctx = open_cstr("<hr>", "", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            TBOX_TEST_ASSERT_MSG(list.count == 1, "the <hr> must paint exactly one FILL_RECT");
            if (list.count == 1) {
                TBOX_TEST_ASSERT_MSG(list.items[0].kind == TBOX_PAINT_FILL_RECT, "the <hr>'s paint op must be a FILL_RECT");
                TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 0x80 && list.items[0].color.g == 0x80 && list.items[0].color.b == 0x80 && list.items[0].color.a == 0xFF, "the UA stylesheet's \"background-color: gray\" must resolve to Gray (0x80, 0x80, 0x80)");
            }

            tbox_context_close(ctx);
        }
    }

    /* 36: regression -- a <p>/<h1> in isolation (no <hr> anywhere) still
     * resolve to exactly the same UA margin as before the v11 hr template
     * line and TBOX_UA_STYLE_CSS_BUFFER_SIZE recheck were added -- same
     * shape of check as test 29's v8 regression, tied to this task's
     * template change instead. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();

        tbox_context *p_ctx  = open_cstr("<p>oi</p>", "", fonts);
        tbox_context *h1_ctx = open_cstr("<h1>oi</h1>", "", fonts);
        TBOX_TEST_ASSERT_MSG(p_ctx != NULL && h1_ctx != NULL, "tbox_context_open must succeed for both documents");
        if (p_ctx != NULL && h1_ctx != NULL) {
            tbox_display_list p_list, h1_list;
            tbox_context_run_frame(p_ctx, 800.0, 600.0, &p_list);
            tbox_context_run_frame(h1_ctx, 800.0, 600.0, &h1_list);

            const tbox_layout_box *p_box  = tbox_context_hit_test(p_ctx, 5.0, default_config.margin.paragraph_px + 2.0);
            const tbox_layout_box *h1_box = tbox_context_hit_test(h1_ctx, 5.0, default_config.margin.heading_px[0] + 2.0);
            TBOX_TEST_ASSERT_MSG(p_box != NULL && h1_box != NULL, "hit-testing just past each box's own UA margin-top must land inside its border_box");
            if (p_box != NULL && h1_box != NULL) {
                TBOX_TEST_ASSERT_MSG(p_box->content_box.y - p_box->margin_box.y == default_config.margin.paragraph_px, "the <p>'s UA margin-top must be unchanged by the v11 hr template/buffer change");
                TBOX_TEST_ASSERT_MSG(h1_box->content_box.y - h1_box->margin_box.y == default_config.margin.heading_px[0], "the <h1>'s UA margin-top must be unchanged by the v11 hr template/buffer change");
            }

            tbox_context_close(p_ctx);
            tbox_context_close(h1_ctx);
        }
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);

    return failures;
}
