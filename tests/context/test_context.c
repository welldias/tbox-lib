#include <tbox/context.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static tbox_context *open_cstr(const char *html, const char *css, tbox_font_face *font) {
    return tbox_context_open(html, strlen(html), css, strlen(css), font);
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
    capture->node        = NULL;
}

static void record_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    click_capture *capture = (click_capture *)userdata;
    capture->call_count++;
    capture->node = node;
}

/* Exercises tbox_context_document: a handler that mutates the clicked
 * node's `class` attribute via tbox_html_node_set_attribute, the same
 * pattern the v1 vertical slice (ARCHITECTURE.md's "Fatia vertical v1")
 * uses to swap a CSS class on click. */
static void toggle_class_handler(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_html_document *document = tbox_context_document(ctx);
    tbox_html_node_set_attribute(document, node, tbox_string_view_make("class", 5), tbox_string_view_make("box on", 6));
}

int tbox_test_context_run(void) {
    int failures = 0;

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");
    if (font_data == NULL) {
        return failures + 1;
    }

    tbox_font_face *font = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT_MSG(font != NULL, "failed to load embedded font face");
    if (font == NULL) {
        free(font_data);
        return failures + 1;
    }

    /* 1: a <div> styled with background-color, run end to end, produces a
     * display list with the expected FILL_RECT (rect == border_box,
     * color == the declared background-color). */
    {
        tbox_context *ctx = open_cstr(
            "<div>x</div>",
            "div { width: 200px; height: 100px; background-color: rgb(10, 20, 30); }",
            font);
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
        tbox_context *ctx = open_cstr("<div><h1>Hello</h1><p>world</p></div>", "", font);
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
        tbox_context *ctx = open_cstr("<div>x</div>", "div { height: 50px; }", font);
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
        tbox_context *ctx = open_cstr(
            "<div><div>a</div></div>",
            "div div { width: 50px; height: 50px; }",
            font);
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
        tbox_context *ctx = open_cstr("<div>x</div>", "", font);
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
        tbox_context *ctx = open_cstr("   ", "", font);
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
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT_MSG(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture), "tbox_context_on_click must succeed for a well-formed selector");

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
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture));

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
        tbox_context *ctx = open_cstr(
            "<div class=\"outer\"><div class=\"inner\">x</div></div>",
            ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }",
            font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".outer", strlen(".outer"), record_click, &capture));

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            /* (10, 10) sits inside the inner div's box -- the hit-tested
             * node is the inner div, which does NOT itself match ".outer". */
            bool dispatched = tbox_context_dispatch_click(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT_MSG(dispatched, "a click on the inner div must still dispatch via the ancestor walk");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 1, "the .outer handler must fire exactly once");
            if (capture.call_count == 1) {
                const tbox_html_node *doc_root = tbox_html_document_root(tbox_context_document(ctx));
                const tbox_html_node *outer     = (doc_root != NULL) ? doc_root->first_child : NULL;
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
        tbox_context *ctx = open_cstr(
            "<div id=\"target\" class=\"box\">x</div>",
            "#target { width: 60px; height: 60px; }",
            font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture id_capture;
            click_capture class_capture;
            click_capture_reset(&id_capture);
            click_capture_reset(&class_capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "#target", strlen("#target"), record_click, &id_capture));
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".box", strlen(".box"), record_click, &class_capture));

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
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);

            /* ">" alone is a leading combinator with no simple selector
             * before it -- a syntax error (same example
             * tests/css_selector/test_*.c already uses for
             * tbox_css_selector_compile). */
            bool registered = tbox_context_on_click(ctx, ">", strlen(">"), record_click, &capture);
            TBOX_TEST_ASSERT_MSG(!registered, "a selector syntax error must make tbox_context_on_click return false");

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
        tbox_context *ctx = open_cstr("<button>Click</button>", "button { width: 100px; height: 40px; }", font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "button", strlen("button"), record_click, &capture));

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
        tbox_context *ctx = open_cstr(
            "<div class=\"box off\">x</div>",
            ".off { width: 40px; height: 40px; background-color: rgb(0, 0, 0); }"
            ".on  { width: 40px; height: 40px; background-color: rgb(255, 0, 0); }",
            font);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, ".off", strlen(".off"), toggle_class_handler, NULL));

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

    tbox_font_face_destroy(font);
    free(font_data);

    return failures;
}
