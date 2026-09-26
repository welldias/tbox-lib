#include <tbox/context.h>
#include <tbox/image.h>
#include <tbox/output.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "context/tbox_context_hit_test.h"
#include "output/tbox_key_repeat.h"
#include "output/tbox_pointer_click.h"
#include "test_support.h"

/* NOVO v12 (Tarefa 3): forward declaration for tbox_ua_style_generate_css,
 * deliberately given external linkage in tbox_context.c (see that
 * function's doc comment) so this file can drive it directly with the
 * REAL production tbox_ua_style_config -- not declared in <tbox/context.h>
 * (it stays an implementation detail, same reasoning
 * tbox_context_hit_test.h documents for tbox_context_hit_test_box, just
 * without a shared header since this task's file scope is only
 * tbox_context.c + this file). */
bool tbox_ua_style_generate_css(tbox_ua_style_config config, char *buffer, size_t buffer_size);

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
    return tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
}

static const tbox_layout_box *find_layout_box(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node) return box;
        const tbox_layout_box *child = find_layout_box(box->first_child, node);
        if (child != NULL) return child;
    }
    return NULL;
}

/* Finds a point, scanning the viewport row by row, where the hit test lands
 * on `node`'s own box. Form controls are inline-blocks, so a control can
 * sit anywhere along its line, not just at a fixed x. */
static bool hit_point_for(const tbox_context *ctx, const tbox_html_node *node, double width, double height,
                          double *out_x, double *out_y) {
    for (int y = 0; y < (int)height; y++) {
        for (int x = 0; x < (int)width; x++) {
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, (double)x, (double)y);
            if (hit != NULL && hit->node == node) {
                *out_x = (double)x;
                *out_y = (double)y;
                return true;
            }
        }
    }
    return false;
}

static const tbox_layout_box *find_box_by_tag(const tbox_layout_box *box, const char *tag) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT &&
            string_view_equal_cstr(box->node->element.tag_name, tag)) return box;
        const tbox_layout_box *child = find_box_by_tag(box->first_child, tag);
        if (child != NULL) return child;
    }
    return NULL;
}

static const tbox_layout_box *find_context_box(const tbox_context *ctx, const tbox_html_node *node) {
    const tbox_layout_box *root = tbox_context_hit_test(ctx, 10.0, 10.0);
    while (root != NULL && root->parent != NULL) root = root->parent;
    return find_layout_box(root, node);
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

static void record_input(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    (void)node;
    (void)value;
    (*(int *)userdata)++;
}

typedef struct color_capture {
    int count;
    char value[8];
    const tbox_html_node *node;
} color_capture;

static void record_color_input(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    color_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 7) memcpy(capture->value, value.data, 7);
    capture->value[7] = '\0';
}

typedef struct date_capture {
    int count;
    char value[11];
    const tbox_html_node *node;
} date_capture;

typedef struct month_capture {
    int count;
    char value[8];
    const tbox_html_node *node;
} month_capture;

typedef struct submit_capture {
    int count;
    const tbox_html_node *form;
    const tbox_html_node *submitter;
} submit_capture;

static void record_submit(tbox_context *ctx, tbox_html_node *form,
                          tbox_html_node *submitter, void *userdata) {
    (void)ctx;
    submit_capture *capture = userdata;
    capture->count++;
    capture->form = form;
    capture->submitter = submitter;
}

static void record_month_input(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    month_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 7) memcpy(capture->value, value.data, 7);
    capture->value[7] = '\0';
}

static void record_date_input(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    date_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 10) memcpy(capture->value, value.data, 10);
    capture->value[10] = '\0';
}

typedef struct datetime_capture {
    int count;
    char value[17];
    const tbox_html_node *node;
} datetime_capture;

static void record_datetime_input(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    datetime_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 16) memcpy(capture->value, value.data, 16);
    capture->value[16] = '\0';
}

typedef struct checkbox_capture {
    int count;
    bool checked;
    const tbox_html_node *node;
} checkbox_capture;

static bool record_checkbox_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    checkbox_capture *capture = userdata;
    capture->count++;
    capture->checked = tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
    capture->node = node;
    return true;
}

typedef struct select_capture {
    int count;
    char value[64];
} select_capture;

static void record_select(tbox_context *ctx, tbox_html_node *node, tbox_string_view value, void *userdata) {
    (void)ctx;
    (void)node;
    select_capture *capture = userdata;
    capture->count++;
    size_t length = value.size < sizeof(capture->value) - 1 ? value.size : sizeof(capture->value) - 1;
    if (length > 0) memcpy(capture->value, value.data, length);
    capture->value[length] = '\0';
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

/* NOVO v13 (Tarefa 5): shared setup for the 8 new UA-stylesheet tests below
 * (<i>/<em>/<small>/<mark>/<del>/<ins>/<sub>/<sup>) -- same two-call resolve
 * pattern test 37 below already established for <pre>/font-family: generate
 * the REAL production UA CSS via tbox_ua_style_generate_css, parse it, and
 * cascade+resolve the single top-level element against it with no author
 * CSS and no parent style. Deliberately has NO TBOX_TEST_ASSERT/
 * TBOX_TEST_ASSERT_MSG calls of its own -- those macros increment a local
 * variable literally named `failures` (see test_support.h), so they only
 * work inlined directly in tbox_test_context_run itself, not in a helper it
 * calls. Callers check
 * the returned bool and assert on `*out_style`. `html` must be a single
 * top-level element, e.g. "<i>x</i>" -- the document root's first child,
 * same assumption test 37 makes explicit. Returns false (leaving
 * `*out_style` untouched) if any setup step fails. */
static bool resolve_first_child_style(const char *html, tbox_style *out_style) {
    char ua_css_text[4096];
    if (!tbox_ua_style_generate_css(tbox_ua_style_config_default(), ua_css_text, sizeof(ua_css_text))) {
        return false;
    }

    tbox_html_document *doc = tbox_html_parse(html, strlen(html));
    if (doc == NULL) {
        return false;
    }

    const tbox_html_node *node = tbox_html_document_root(doc)->first_child;
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) {
        tbox_html_document_destroy(doc);
        return false;
    }

    tbox_css_stylesheet *ua_sheet = tbox_css_parse(ua_css_text, strlen(ua_css_text));
    if (ua_sheet == NULL) {
        tbox_html_document_destroy(doc);
        return false;
    }

    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(ua_sheet, node);
    *out_style                       = tbox_style_resolve(node, NULL, &computed);

    tbox_css_computed_style_destroy(&computed);
    tbox_css_stylesheet_destroy(ua_sheet);
    tbox_html_document_destroy(doc);

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
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL);
    TBOX_TEST_ASSERT_MSG(fonts != NULL, "failed to create font face cache");
    if (fonts == NULL) {
        free(font_data);
        return failures + 1;
    }

    /* 1: a <div> styled with background-color, run end to end, produces a
     * display list with the expected FILL_RECT (rect == border_box,
     * color == the declared background-color). */
    {
        /* NOVO v14: uses an EMPTY <div> -- with the fixed-text-tag debt
         * fixed, a loose "x" here would now trigger its own anonymous
         * text box (a second, TEXT_RUN paint op), which is irrelevant to
         * this test's actual point (one background FILL_RECT). */
        tbox_context *ctx = open_cstr("<div></div>", "div { width: 200px; height: 100px; background-color: rgb(10, 20, 30); }", fonts);
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
        /* NOVO v14: the inner <div> is EMPTY -- a loose "a" would now
         * trigger its own anonymous text box, which the hit-test at
         * (10, 10) would find INSTEAD of the inner div itself (node ==
         * NULL, breaking this test's "inner->node != NULL" premise);
         * irrelevant to this test's actual point (hit-test structure). */
        tbox_context *ctx = open_cstr("<div><div></div></div>", "div div { width: 50px; height: 50px; }", fonts);
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
     * once, with the clicked node itself.
     *
     * NOVO v14: the <button> is EMPTY -- a loose "Click" label would now
     * build its own anonymous text box covering the button's content area;
     * tbox_context_hit_test would then find that anonymous box (node ==
     * NULL) instead of the button itself, and tbox_context_dispatch_click
     * bails out early on box->node == NULL (see src/context/tbox_context.c)
     * -- correctly out of scope for this Layout Tree task (dispatch
     * bubbling through an anonymous box is Context layer work), but
     * irrelevant to what THIS test actually checks (dispatch landing on the
     * button itself). */
    {
        tbox_context *ctx = open_cstr("<button></button>", "button { width: 100px; height: 40px; }", fonts);
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
                if (capture.node != NULL) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(capture.node->element.tag_name, "button"), "the handler must receive the <button> node that was actually clicked");
                }
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
     * the node->parent walk.
     *
     * NOVO v14: the inner <div> is EMPTY -- a loose "x" would now build its
     * own anonymous text box, which the hit-test at (10, 10) would find
     * INSTEAD of the inner div (node == NULL, on which
     * tbox_context_dispatch_click bails out early -- see
     * src/context/tbox_context.c); dispatching THROUGH an anonymous box is
     * out of scope for this Layout Tree task and irrelevant to what this
     * test actually checks (the ancestor walk from a real inner node). */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\"></div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
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
     * order.
     *
     * NOVO v14: the <div> is EMPTY -- a loose "x" would now build its own
     * anonymous text box, which the hit-test at (10, 10) would find INSTEAD
     * of the div itself (node == NULL, on which dispatch bails out early --
     * out of scope for this Layout Tree task, irrelevant to this test's
     * actual point: both handlers firing on the same matched node). */
    {
        tbox_context *ctx = open_cstr("<div id=\"target\" class=\"box\"></div>", "#target { width: 60px; height: 60px; }", fonts);
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
     * new class's declaration).
     *
     * NOVO v14: the <div> is EMPTY -- a loose "x" would now build its own
     * anonymous text box, adding a second (TEXT_RUN) paint op and stealing
     * the click dispatch (node == NULL) from the div itself, both
     * irrelevant to this test's actual point (background swap on click). */
    {
        tbox_context *ctx = open_cstr("<div class=\"box off\"></div>",
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

        tbox_context *default_ctx = tbox_context_open_with_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, NULL, default_config);
        tbox_context *custom_ctx  = tbox_context_open_with_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, NULL, custom_config);
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
        tbox_context *default_ctx = tbox_context_open_with_config(html, strlen(html), "", 0, fonts, NULL, default_config);
        tbox_context *custom_ctx  = tbox_context_open_with_config(html, strlen(html), "", 0, fonts, NULL, custom_config);
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
     * actually changed.
     *
     * NOVO v14: the <div> is EMPTY -- a loose "x" would now build its own
     * anonymous text box, which tbox_context_hit_test would find INSTEAD of
     * the div (node == NULL, so tbox_context_update_hover's `new_hovered =
     * box->node` would stay NULL -- indistinguishable from "nothing
     * hovered", breaking this test's actual point: hovering the div). */
    {
        tbox_context *ctx = open_cstr("<div></div>", "div { width: 100px; height: 100px; }", fonts);
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
     * it is a no-op (no change).
     *
     * NOVO v14: the <div> is EMPTY, same rationale as test 19 above -- a
     * loose "x" would steal the hover target as an anonymous (node == NULL)
     * box, breaking the div-hover premise this test actually checks. */
    {
        tbox_context *ctx = open_cstr("<div></div>", "div { width: 100px; height: 100px; }", fonts);
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
     * normal color.
     *
     * NOVO v14: the <div> is EMPTY -- a loose "x" would now build its own
     * anonymous text box, both adding a second (TEXT_RUN) paint op AND
     * stealing the hover target as an anonymous (node == NULL) box, both
     * irrelevant to this test's actual point (:hover reaching the cascade). */
    {
        tbox_context *ctx = open_cstr("<div class=\"box\"></div>",
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
     * bubbling order (checked via a shared log both handlers append to).
     *
     * NOVO v14: the inner <div> is EMPTY -- a loose "x" would now build its
     * own anonymous text box, which the hit-test at (10, 10) would find
     * INSTEAD of the inner div (node == NULL, on which dispatch bails out
     * early -- out of scope for this Layout Tree task, irrelevant to this
     * test's actual point: bubbling order from a real inner node). */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\"></div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
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
     * ancestor's otherwise-matching handler from firing at all.
     *
     * NOVO v14: the inner <div> is EMPTY, same rationale as test 22 above. */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\"></div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
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
     * point no longer fires the unbound handler.
     *
     * NOVO v14: the <button> is EMPTY, same rationale as test 7 above -- a
     * loose "Click" label would steal the dispatch target as an anonymous
     * (node == NULL) box. */
    {
        tbox_context *ctx = open_cstr("<button></button>", "button { width: 100px; height: 40px; }", fonts);
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
     * count entirely attributable to div.algo.
     *
     * NOVO v14: div.algo itself is EMPTY -- a loose "x" would now build its
     * own anonymous text box (a second, TEXT_RUN paint op), irrelevant to
     * this test's actual point (which stylesheet source wins the cascade). */
    {
        tbox_context *ctx = open_cstr(
            "<div><style>.algo{background-color:blue;}</style><div class=\"algo\"></div></div>", "", fonts);
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
        /* NOVO v14: div.algo is EMPTY, same rationale as test 30 above. */
        tbox_context *ctx = open_cstr(
            "<div><style>.algo{background-color:blue;}</style><div class=\"algo\"></div></div>",
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
        /* NOVO v14: div.algo is EMPTY, same rationale as test 30 above. */
        tbox_context *ctx = open_cstr(
            "<div class=\"algo\"></div>", ".algo{background-color:green;}", fonts);
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
     * stop at (or drop) any block after the first one found.
     *
     * NOVO v14: both div.a/div.b are EMPTY -- loose "x"/"y" would each now
     * build their own anonymous text box (extra TEXT_RUN paint ops),
     * irrelevant to this test's actual point (both <style> blocks applying). */
    {
        tbox_context *ctx = open_cstr(
            "<div>"
            "<style>.a{background-color:blue;}</style>"
            "<div class=\"a\"></div>"
            "<style>.b{background-color:green;}</style>"
            "<div class=\"b\"></div>"
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

    /* 37: NOVO v12 (Tarefa 3) -- a <pre> with NO author CSS at all resolves
     * font-family "monospace" purely from the UA stylesheet ("pre {
     * display: block; font-family: monospace; }", added to
     * tbox_ua_style_generate_css's template this task). Deliberately does
     * NOT go through tbox_context_open/run_frame -- tbox_context is opaque
     * (no public accessor for its internal tbox_style_table) and the
     * font-family CHOSEN never affects layout geometry, so there is no
     * black-box way to observe it through the display list/hit-test
     * surface every other UA-stylesheet test above uses. Instead, this
     * calls the REAL tbox_ua_style_generate_css directly (see the forward
     * declaration above) to get the actual production UA CSS text, parses
     * it, and resolves the <pre> node's tbox_style against it the same
     * two-call way tests/style/test_style.c's resolve_node() does -- this
     * is what actually proves the orchestration wiring (not a hand-copied
     * "pre { font-family: monospace; }" string, which would test nothing
     * about tbox_context.c). */
    {
        char ua_css_text[4096];
        bool generated = tbox_ua_style_generate_css(tbox_ua_style_config_default(), ua_css_text, sizeof(ua_css_text));
        TBOX_TEST_ASSERT_MSG(generated, "tbox_ua_style_generate_css must succeed with the default config");
        if (generated) {
            tbox_html_document *doc = tbox_html_parse("<pre>x</pre>", strlen("<pre>x</pre>"));
            TBOX_TEST_ASSERT_MSG(doc != NULL, "tbox_html_parse must succeed for <pre>x</pre>");
            if (doc != NULL) {
                const tbox_html_node *pre = tbox_html_document_root(doc)->first_child;
                TBOX_TEST_ASSERT_MSG(pre != NULL && string_view_equal_cstr(pre->element.tag_name, "pre"), "test setup assumption: the document root's first child is the <pre> element");
                if (pre != NULL) {
                    tbox_css_stylesheet *ua_sheet = tbox_css_parse(ua_css_text, strlen(ua_css_text));
                    TBOX_TEST_ASSERT_MSG(ua_sheet != NULL, "tbox_css_parse must succeed for the generated UA CSS text");
                    if (ua_sheet != NULL) {
                        tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(ua_sheet, pre);
                        tbox_style style                 = tbox_style_resolve(pre, NULL, &computed);
                        TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "monospace") == 0, "a <pre> with no author CSS must resolve font-family \"monospace\" from the UA stylesheet alone");

                        tbox_css_computed_style_destroy(&computed);
                        tbox_css_stylesheet_destroy(ua_sheet);
                    }
                }

                tbox_html_document_destroy(doc);
            }
        }
    }

    /* 38: NOVO v13 (Tarefa 5) -- <i>/<em> resolve font_italic == true purely
     * from the UA stylesheet ("i, em { display: inline; font-style:
     * italic; }"), no author CSS involved. */
    {
        tbox_style i_style, em_style;
        bool i_ok  = resolve_first_child_style("<i>x</i>", &i_style);
        bool em_ok = resolve_first_child_style("<em>x</em>", &em_style);
        TBOX_TEST_ASSERT_MSG(i_ok && em_ok, "resolve_first_child_style must succeed for <i>/<em>");
        if (i_ok) {
            TBOX_TEST_ASSERT_MSG(i_style.font_italic == true, "an <i> with no author CSS must resolve font_italic from the UA stylesheet alone");
        }
        if (em_ok) {
            TBOX_TEST_ASSERT_MSG(em_style.font_italic == true, "an <em> with no author CSS must resolve font_italic from the UA stylesheet alone");
        }
    }

    /* 39: NOVO v13 (Tarefa 5) -- <small> resolves a font_size 80% of the
     * inherited (here: default 16px, no parent) font-size purely from the
     * UA stylesheet ("small { display: inline; font-size: 80%; }"). The
     * expected value mirrors tbox_style_resolve_font_size's own percent
     * formula (parent_font_size * value / 100.0) exactly, so the
     * comparison is bit-exact rather than an approximation. */
    {
        tbox_style small_style;
        bool ok = resolve_first_child_style("<small>x</small>", &small_style);
        TBOX_TEST_ASSERT_MSG(ok, "resolve_first_child_style must succeed for <small>");
        if (ok) {
            TBOX_TEST_ASSERT_MSG(small_style.font_size == 16.0 * 80.0 / 100.0, "a <small> with no author CSS must resolve font-size to 80% of the inherited font-size from the UA stylesheet alone");
        }
    }

    /* 40: NOVO v13 (Tarefa 5) -- <mark> resolves an opaque yellow
     * background_color (255, 255, 0, 255) purely from the UA stylesheet
     * ("mark { display: inline; background-color: yellow; }"), proving the
     * named color "yellow" reaches the cascade -- same named-color
     * inspection pattern as test 35 above for <hr>/"gray". */
    {
        tbox_style mark_style;
        bool ok = resolve_first_child_style("<mark>x</mark>", &mark_style);
        TBOX_TEST_ASSERT_MSG(ok, "resolve_first_child_style must succeed for <mark>");
        if (ok) {
            TBOX_TEST_ASSERT_MSG(mark_style.background_color.r == 255 && mark_style.background_color.g == 255 && mark_style.background_color.b == 0 && mark_style.background_color.a == 255, "a <mark> with no author CSS must resolve an opaque yellow (255, 255, 0, 255) background-color from the UA stylesheet alone");
        }
    }

    /* 41: NOVO v13 (Tarefa 5) -- <del>/<ins> resolve text_decoration
     * LINE_THROUGH/UNDERLINE purely from the UA stylesheet ("del {
     * display: inline; text-decoration: line-through; }" / "ins { display:
     * inline; text-decoration: underline; }"). */
    {
        tbox_style del_style, ins_style;
        bool del_ok = resolve_first_child_style("<del>x</del>", &del_style);
        bool ins_ok = resolve_first_child_style("<ins>x</ins>", &ins_style);
        TBOX_TEST_ASSERT_MSG(del_ok && ins_ok, "resolve_first_child_style must succeed for <del>/<ins>");
        if (del_ok) {
            TBOX_TEST_ASSERT_MSG(del_style.text_decoration == TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH, "a <del> with no author CSS must resolve text_decoration LINE_THROUGH from the UA stylesheet alone");
        }
        if (ins_ok) {
            TBOX_TEST_ASSERT_MSG(ins_style.text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE, "an <ins> with no author CSS must resolve text_decoration UNDERLINE from the UA stylesheet alone");
        }
    }

    /* 42: NOVO v13 (Tarefa 5) -- <sub>/<sup> resolve vertical_align SUB/
     * SUPER AND a font_size 75% of the inherited font-size, both purely
     * from the UA stylesheet ("sub { display: inline; font-size: 75%;
     * vertical-align: sub; }" / "sup { ...; vertical-align: super; }") --
     * same bit-exact percent-formula comparison as test 39 above. */
    {
        tbox_style sub_style, sup_style;
        bool sub_ok = resolve_first_child_style("<sub>x</sub>", &sub_style);
        bool sup_ok = resolve_first_child_style("<sup>x</sup>", &sup_style);
        TBOX_TEST_ASSERT_MSG(sub_ok && sup_ok, "resolve_first_child_style must succeed for <sub>/<sup>");
        if (sub_ok) {
            TBOX_TEST_ASSERT_MSG(sub_style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_SUB, "a <sub> with no author CSS must resolve vertical_align SUB from the UA stylesheet alone");
            TBOX_TEST_ASSERT_MSG(sub_style.font_size == 16.0 * 75.0 / 100.0, "a <sub> with no author CSS must resolve font-size to 75% of the inherited font-size from the UA stylesheet alone");
        }
        if (sup_ok) {
            TBOX_TEST_ASSERT_MSG(sup_style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_SUPER, "a <sup> with no author CSS must resolve vertical_align SUPER from the UA stylesheet alone");
            TBOX_TEST_ASSERT_MSG(sup_style.font_size == 16.0 * 75.0 / 100.0, "a <sup> with no author CSS must resolve font-size to 75% of the inherited font-size from the UA stylesheet alone");
        }
    }

    /* Keyboard focus is computed in the backend-independent context. Tab
     * skips disabled and hidden controls, wraps, and exposes :focus to CSS.
     * Enter activates the focused button through the existing click API. */
    {
        tbox_context *ctx = open_cstr(
            "<div><button id='one'>One</button><button disabled>Skip</button>"
            "<button style='display:none'>Hidden</button><button id='two'>Two</button></div>",
            "button:focus { background-color: rgb(255, 0, 0); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            bool button_text_painted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN) {
                    button_text_painted = true;
                }
            }
            TBOX_TEST_ASSERT_MSG(button_text_painted, "button labels must produce text paint operations");
            const tbox_html_node *root = tbox_html_document_root(tbox_context_document(ctx));
            const tbox_html_node *one = root->first_child->first_child;
            const tbox_html_node *two = one->next_sibling->next_sibling->next_sibling;
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "#one", 4, record_click, &capture) >= 0);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            bool focus_painted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 255 &&
                    list.items[i].color.g == 0 && list.items[i].color.b == 0) {
                    focus_painted = true;
                }
            }
            TBOX_TEST_ASSERT_MSG(focus_painted, ":focus must reach CSS painting");
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, false, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.call_count == 1 && capture.node == one);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.call_count == 2);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            double two_x, two_y;
            bool clicked_two = hit_point_for(ctx, two, 320.0, 200.0, &two_x, &two_y);
            if (clicked_two) {
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, two_x, two_y));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            }
            TBOX_TEST_ASSERT_MSG(clicked_two, "pointer click must focus a rendered button without a handler");
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_html_node_remove((tbox_html_node *)one);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            tbox_context_close(ctx);
        }
    }

    /* Tab and Shift+Tab reveal controls in a scrollable list. Manual wheel
     * scrolling after focus stays in place until focus changes again. */
    {
        tbox_context *ctx = open_cstr(
            "<div id='list'><button>A</button><button>B</button><button>C</button><button>D</button></div>",
            "#list { width: 100px; height: 50px; overflow-y: auto; } "
            "button { width: 70px; margin: 0px; padding: 0px; border: 0px solid black; } "
            "button:focus { border: 0px solid black; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            const tbox_html_node *first = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            for (int i = 0; i < 3; i++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
                const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
                while (root_box != NULL && root_box->parent != NULL) root_box = root_box->parent;
                const tbox_layout_box *focus_box = find_layout_box(root_box, tbox_context_focused_node(ctx));
                TBOX_TEST_ASSERT(focus_box != NULL && focus_box->parent != NULL);
                if (focus_box != NULL && focus_box->parent != NULL) {
                    tbox_rect viewport = focus_box->parent->padding_box;
                    TBOX_TEST_ASSERT(focus_box->border_box.y >= viewport.y - 0.01);
                    TBOX_TEST_ASSERT(focus_box->border_box.y + focus_box->border_box.height <=
                        viewport.y + viewport.height + 0.01);
                }
                if (i == 1) TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            }
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            for (int i = 0; i < 3; i++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, true, false}));
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
                const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
                while (root_box != NULL && root_box->parent != NULL) root_box = root_box->parent;
                const tbox_layout_box *focus_box = find_layout_box(root_box, tbox_context_focused_node(ctx));
                TBOX_TEST_ASSERT(focus_box != NULL && focus_box->parent != NULL);
                if (focus_box != NULL && focus_box->parent != NULL) {
                    tbox_rect viewport = focus_box->parent->padding_box;
                    TBOX_TEST_ASSERT(focus_box->border_box.y >= viewport.y - 0.01);
                    TBOX_TEST_ASSERT(focus_box->border_box.y + focus_box->border_box.height <=
                        viewport.y + viewport.height + 0.01);
                }
            }
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == first);
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            tbox_context_close(ctx);
        }
    }

    /* A single select displays its chosen label while exposing the option
     * value. Keyboard and mouse choices skip disabled options. */
    {
        tbox_context *ctx = open_cstr(
            "<div><select id='mode'><option value='a'>Alpha</option>"
            "<option value='b' label='Bee' selected>Beta</option>"
            "<option value='x' disabled>Unavailable</option>"
            "<option>Gamma</option></select><button>Next</button></div>",
            "select { width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *button = select->next_sibling;
            const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
            while (root_box != NULL && root_box->parent != NULL) root_box = root_box->parent;
            const tbox_layout_box *select_box = find_layout_box(root_box, select);
            TBOX_TEST_ASSERT(select_box != NULL && select_box->text_run_count == 1);
            if (select_box != NULL && select_box->text_run_count == 1)
                TBOX_TEST_ASSERT(string_view_equal_cstr(select_box->text_runs[0].text, "Bee"));
            TBOX_TEST_ASSERT(find_layout_box(root_box, select->first_child) == NULL);
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "b"));
            select_capture capture = {0};
            tbox_context_on_select(ctx, record_select, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == select);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "Gamma"));
            TBOX_TEST_ASSERT(capture.count == 1 && strcmp(capture.value, "Gamma") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(capture.count == 2);
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ESCAPE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 2);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 3 && strcmp(capture.value, "Gamma") == 0);
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "Gamma"));
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
            while (root_box != NULL && root_box->parent != NULL) root_box = root_box->parent;
            select_box = find_layout_box(root_box, select);
            TBOX_TEST_ASSERT(select_box != NULL);
            if (select_box != NULL) {
                tbox_context_dispatch_click(ctx, select_box->border_box.x + 5.0,
                    select_box->border_box.y + select_box->border_box.height / 2.0);
                tbox_context_run_frame(ctx, 160.0, 220.0, &list);
                tbox_rect popup = {0};
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 105 &&
                        op->color.g == 112 && op->color.b == 122) popup = op->rect;
                }
                TBOX_TEST_ASSERT(popup.height > 0.0);
                if (popup.height > 0.0) {
                    double row_height = popup.height / 4.0;
                    tbox_context_dispatch_click(ctx, popup.x + 10.0, popup.y + row_height * 2.5);
                    TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "Gamma"));
                    TBOX_TEST_ASSERT(capture.count == 3);
                    tbox_context_dispatch_click(ctx, popup.x + 10.0, popup.y + row_height * 0.5);
                    TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "a"));
                    TBOX_TEST_ASSERT(capture.count == 4 && strcmp(capture.value, "a") == 0);
                }
            }
            TBOX_TEST_ASSERT(tbox_context_select_set_value(ctx, select, tbox_string_view_make("b", 1)));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(capture.count == 4);
            TBOX_TEST_ASSERT(!tbox_context_select_set_value(ctx, select, tbox_string_view_make("missing", 7)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == button);
            tbox_context_close(ctx);
        }
    }

    /* Longer menus show a bounded number of rows. The wheel changes the
     * visible slice, and End reveals the last option before confirmation. */
    {
        tbox_context *ctx = open_cstr(
            "<select><option value='1'>One</option><option value='2'>Two</option>"
            "<option value='3'>Three</option><option value='4'>Four</option>"
            "<option value='5'>Five</option><option value='6'>Six</option>"
            "<option value='7'>Seven</option><option value='8'>Eight</option></select>",
            "select { width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 140.0, 240.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            tbox_context_run_frame(ctx, 140.0, 240.0, &list);
            tbox_rect popup = {0};
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 105 &&
                    op->color.g == 112 && op->color.b == 122) popup = op->rect;
            }
            TBOX_TEST_ASSERT(popup.height > 0.0 && popup.height < 200.0);
            if (popup.height > 0.0) {
                TBOX_TEST_ASSERT(tbox_context_scroll(ctx, popup.x + 10.0, popup.y + 10.0, 30.0));
                tbox_context_run_frame(ctx, 140.0, 240.0, &list);
                bool saw_two_first = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind == TBOX_PAINT_TEXT_RUN && op->rect.y >= popup.y &&
                        op->rect.y < popup.y + 40.0 && string_view_equal_cstr(op->text, "Two"))
                        saw_two_first = true;
                }
                TBOX_TEST_ASSERT(saw_two_first);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_END, true, false, false}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "1"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "8"));
            tbox_context_close(ctx);
        }
    }

    /* An input button paints its value, shares button activation, and cannot
     * be edited. A disabled button neither takes focus nor dispatches clicks. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='action' type='BUTTON' value='Run'>"
            "<input id='empty' type='button'>"
            "<input id='blank' type='button' value=''>"
            "<input id='disabled' type='button' value='Off' disabled></div>",
            "input { width: 60px; } input:focus { background-color: rgb(255, 0, 0); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *action = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *empty = action->next_sibling;
            const tbox_html_node *blank = empty->next_sibling;
            const tbox_html_node *disabled = blank->next_sibling;
            bool painted_run = false, painted_off = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_TEXT_RUN) continue;
                if (string_view_equal_cstr(op->text, "Run"))
                    painted_run = op->has_clip && op->clip.width == 60.0;
                if (string_view_equal_cstr(op->text, "Off")) painted_off = true;
                TBOX_TEST_ASSERT(!string_view_equal_cstr(op->text, "Button"));
            }
            TBOX_TEST_ASSERT(painted_run && painted_off);

            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "input", 5, record_click, &capture) >= 0);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool focus_painted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                    list.items[i].color.r == 255 && list.items[i].color.g == 0 && list.items[i].color.b == 0)
                    focus_painted = true;
            }
            TBOX_TEST_ASSERT(focus_painted);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(changes == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.call_count == 2 && capture.node == action);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == empty);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == blank);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);

            double hx, hy;
            bool clicked_action = hit_point_for(ctx, action, 320.0, 180.0, &hx, &hy);
            if (clicked_action) TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, hx, hy));
            bool clicked_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &hx, &hy);
            if (clicked_disabled) TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, hx, hy));
            TBOX_TEST_ASSERT(clicked_action && clicked_disabled);
            TBOX_TEST_ASSERT(capture.call_count == 3 && capture.node == action);
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);

            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)action,
                                         tbox_string_view_make("value", 5), tbox_string_view_make("Go", 2));
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool painted_go = false, painted_old = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind != TBOX_PAINT_TEXT_RUN) continue;
                if (string_view_equal_cstr(list.items[i].text, "Go")) painted_go = true;
                if (string_view_equal_cstr(list.items[i].text, "Run")) painted_old = true;
            }
            TBOX_TEST_ASSERT(painted_go && !painted_old);
            tbox_context_close(ctx);
        }
    }

    /* Image inputs render decoded pixels at their requested size and use the
     * button click path. A failed image load shows alt text. */
    {
        tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(images != NULL);
        if (images != NULL) {
            const char *html =
                "<div><input id='image-action' type='IMAGE' src='yellow.png' alt='Send' width='32' height='20'>"
                "<input id='broken' type='image' src='missing.png' alt='Fallback'>"
                "<input id='disabled' type='image' src='yellow.png' width='24' height='16' disabled>"
                "<input id='scaled' type='image' src='yellow.png' width='30'></div>";
            const char *css = "#scaled { width: 40px; }";
            tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, images);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                const tbox_html_node *action = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
                const tbox_html_node *broken = action->next_sibling;
                const tbox_html_node *disabled = broken->next_sibling;
                const tbox_html_node *scaled = disabled->next_sibling;
                const tbox_layout_box *box = find_context_box(ctx, action);
                TBOX_TEST_ASSERT(box != NULL);
                if (box != NULL) {
                    TBOX_TEST_ASSERT(box->content_box.width == 32.0 && box->content_box.height == 20.0);
                    bool painted_image = false;
                    for (size_t i = 0; i < list.count; i++)
                        if (list.items[i].kind == TBOX_PAINT_IMAGE &&
                            list.items[i].rect.x == box->content_box.x &&
                            list.items[i].rect.y == box->content_box.y &&
                            list.items[i].rect.width == 32.0 && list.items[i].rect.height == 20.0)
                            painted_image = true;
                    TBOX_TEST_ASSERT(painted_image);
                }
                bool painted_alt = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                        string_view_equal_cstr(list.items[i].text, "Fallback")) painted_alt = true;
                TBOX_TEST_ASSERT(painted_alt);
                const tbox_layout_box *scaled_box = find_context_box(ctx, scaled);
                TBOX_TEST_ASSERT(scaled_box != NULL);
                if (scaled_box != NULL)
                    TBOX_TEST_ASSERT(scaled_box->content_box.width == 40.0 &&
                                     scaled_box->content_box.height == 40.0);

                click_capture capture;
                click_capture_reset(&capture);
                TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "#image-action", 13, record_click, &capture) >= 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
                TBOX_TEST_ASSERT(capture.call_count == 2 && capture.node == action);
                TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
                if (box != NULL)
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
                TBOX_TEST_ASSERT(capture.call_count == 3);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == broken);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == scaled);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
                const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
                TBOX_TEST_ASSERT(disabled_box != NULL);
                if (disabled_box != NULL)
                    TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 2.0,
                        disabled_box->border_box.y + 2.0));
                TBOX_TEST_ASSERT(capture.call_count == 3);
                tbox_context_close(ctx);
            }
            tbox_image_cache_destroy(images);
        }
    }

    /* Checkbox state is the presence of the checked attribute. User toggles
     * update it before click handlers run, and repaint the indicator. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='off' type='CHECKBOX'><input id='on' type='checkbox' checked>"
            "<input id='disabled' type='checkbox' checked disabled><button>Next</button></div>",
            "input:checked { background-color: yellow; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *off = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *on = off->next_sibling;
            const tbox_html_node *disabled = on->next_sibling;
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(on, tbox_string_view_make("checked", 7)) != NULL);

            tbox_rect off_content = {0};
            double hx, hy;
            bool found_off = hit_point_for(ctx, off, 320.0, 180.0, &hx, &hy);
            if (found_off) off_content = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &hx, &hy);
            TBOX_TEST_ASSERT(found_off && found_disabled);
            TBOX_TEST_ASSERT(off_content.width == 12.0 && off_content.height == 12.0);

            checkbox_capture capture = {0};
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "input[type=checkbox]", 20,
                                                   record_checkbox_click, &capture) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == off);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == off && capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool painted_check = false, painted_checked_background = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                if (op->rect.x >= off_content.x && op->rect.x < off_content.x + off_content.width &&
                    op->rect.y >= off_content.y && op->rect.y < off_content.y + off_content.height &&
                    op->rect.width == 1.5 && op->rect.height == 1.5) painted_check = true;
                if (op->color.r == 255 && op->color.g == 255 && op->color.b == 0 &&
                    op->rect.x <= off_content.x && op->rect.y <= off_content.y &&
                    op->rect.x + op->rect.width >= off_content.x + off_content.width &&
                    op->rect.y + op->rect.height >= off_content.y + off_content.height)
                    painted_checked_background = true;
            }
            TBOX_TEST_ASSERT(painted_check && painted_checked_background);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 2 && !capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) == NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            painted_check = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->rect.x >= off_content.x &&
                    op->rect.x < off_content.x + off_content.width && op->rect.y >= off_content.y &&
                    op->rect.y < off_content.y + off_content.height &&
                    op->rect.width == 1.5 && op->rect.height == 1.5) painted_check = true;
            }
            TBOX_TEST_ASSERT(!painted_check);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == on);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 3 && capture.node == on && !capture.checked);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == disabled->next_sibling);

            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            double cx, cy;
            bool clicked_off = hit_point_for(ctx, off, 320.0, 180.0, &cx, &cy);
            if (clicked_off) TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, cx, cy));
            bool clicked_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &cx, &cy);
            if (clicked_disabled) TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, cx, cy));
            TBOX_TEST_ASSERT(clicked_off && clicked_disabled);
            TBOX_TEST_ASSERT(capture.count == 4 && capture.node == off && capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(disabled, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_close(ctx);
        }
    }

    /* Radio selection is exclusive within each named group. The selected
     * control paints a centered dot and matches :checked. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='a' type='RADIO' name='choice'>"
            "<input id='b' type='radio' name='choice' checked>"
            "<input id='c' type='radio' name='other' checked>"
            "<input id='off' type='radio' name='choice' disabled></div>",
            "input:checked { background-color: yellow; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *a = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *b = a->next_sibling;
            const tbox_html_node *c = b->next_sibling;
            const tbox_html_node *disabled = c->next_sibling;
            const tbox_layout_box *a_box = find_context_box(ctx, a);
            const tbox_layout_box *b_box = find_context_box(ctx, b);
            TBOX_TEST_ASSERT(a_box != NULL && b_box != NULL);
            if (a_box != NULL) TBOX_TEST_ASSERT(a_box->content_box.width == 12.0 && a_box->content_box.height == 12.0);
            bool painted_dot = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                    list.items[i].radius == 3.0) painted_dot = true;
            TBOX_TEST_ASSERT(painted_dot);
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "input[type=radio]", 17, record_click, &capture) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == a);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.call_count == 1 && capture.node == a);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(c, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == b);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_close(ctx);
        }
    }

    /* Range inputs normalize to a step, paint a track and thumb, and update
     * by keyboard, click, and pointer drag. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='level' type='RANGE' min='10' max='30' step='5' value='17'>"
            "<input id='default-range' type='range'>"
            "<input type='range' disabled value='40'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            const tbox_html_node *level = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *default_range = level->next_sibling;
            const tbox_html_node *disabled = default_range->next_sibling;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "15"));
            value = tbox_html_node_get_attribute(default_range, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "50"));
            const tbox_layout_box *box = find_context_box(ctx, level);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) TBOX_TEST_ASSERT(box->content_box.width == 160.0 && box->content_box.height == 20.0);
            bool painted_thumb = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].radius == 7.0)
                    painted_thumb = true;
            TBOX_TEST_ASSERT(painted_thumb);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == level);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "20"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, false, false}));
            value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "10"));
            if (box != NULL) {
                double x = box->content_box.x + box->content_box.width / 2.0;
                double y = box->content_box.y + box->content_box.height / 2.0;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "20"));
                TBOX_TEST_ASSERT(tbox_context_range_drag(ctx, box->content_box.x + box->content_box.width, y));
                value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "30"));
                tbox_context_range_release(ctx);
                TBOX_TEST_ASSERT(!tbox_context_range_drag(ctx, x, y));
            }
            TBOX_TEST_ASSERT(changes == 4);
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->content_box.x + 8.0, disabled_box->content_box.y + 8.0));
            TBOX_TEST_ASSERT(changes == 4);
            tbox_context_close(ctx);
        }
    }

    /* A reset input restores the original state of controls in its form,
     * including retained select state, without changing another form. */
    {
        tbox_context *ctx = open_cstr(
            "<div><form><input id='name' value='start'><input id='check' type='checkbox' checked>"
            "<input id='radio-a' type='radio' name='group' checked>"
            "<input id='radio-b' type='radio' name='group'>"
            "<input id='slider' type='range' value='20'>"
            "<textarea>Initial</textarea><select><option value='one' selected>One</option>"
            "<option value='two'>Two</option></select><input id='reset' type='RESET'></form>"
            "<form><input id='other' value='keep'></form></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 500.0, &list);
            const tbox_html_node *form = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *name = form->first_child;
            const tbox_html_node *check = name->next_sibling;
            const tbox_html_node *radio_a = check->next_sibling;
            const tbox_html_node *radio_b = radio_a->next_sibling;
            const tbox_html_node *slider = radio_b->next_sibling;
            const tbox_html_node *textarea = slider->next_sibling;
            const tbox_html_node *select = textarea->next_sibling;
            const tbox_html_node *reset = select->next_sibling;
            const tbox_html_node *other = form->next_sibling->first_child;
            bool label_painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Reset")) label_painted = true;
            TBOX_TEST_ASSERT(label_painted);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)name,
                tbox_string_view_make("value", 5), tbox_string_view_make("changed", 7));
            tbox_html_node_remove_attribute((tbox_html_node *)check, tbox_string_view_make("checked", 7));
            tbox_html_node_remove_attribute((tbox_html_node *)radio_a, tbox_string_view_make("checked", 7));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)radio_b,
                tbox_string_view_make("checked", 7), tbox_string_view_make(NULL, 0));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)slider,
                tbox_string_view_make("value", 5), tbox_string_view_make("80", 2));
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)textarea,
                tbox_string_view_make("Changed", 7));
            TBOX_TEST_ASSERT(tbox_context_select_set_value(ctx, select, tbox_string_view_make("two", 3)));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)other,
                tbox_string_view_make("value", 5), tbox_string_view_make("outside", 7));
            tbox_context_run_frame(ctx, 320.0, 500.0, &list);
            const tbox_layout_box *reset_box = find_context_box(ctx, reset);
            TBOX_TEST_ASSERT(reset_box != NULL);
            if (reset_box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx,
                    reset_box->border_box.x + 5.0, reset_box->border_box.y + 5.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(name, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "start"));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(check, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(radio_a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(radio_b, tbox_string_view_make("checked", 7)) == NULL);
            value = tbox_html_node_get_attribute(slider, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "20"));
            TBOX_TEST_ASSERT(textarea->first_child != NULL &&
                string_view_equal_cstr(textarea->first_child->text.text, "Initial"));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_select_value(ctx, select), "one"));
            value = tbox_html_node_get_attribute(other, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "outside"));
            tbox_context_close(ctx);
        }
    }

    /* Submit inputs expose a form callback by click or keyboard; Enter in a
     * single-line field submits the form without an explicit submitter. */
    {
        tbox_context *ctx = open_cstr(
            "<div><form><input id='query' type='search' value='abc'>"
            "<input id='send' type='SUBMIT'><input id='off' type='submit' disabled></form>"
            "<input id='outside' type='submit'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            const tbox_html_node *form = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *query = form->first_child;
            const tbox_html_node *send = query->next_sibling;
            const tbox_html_node *disabled = send->next_sibling;
            bool painted_submit = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Submit")) painted_submit = true;
            TBOX_TEST_ASSERT(painted_submit);
            submit_capture capture = {0};
            tbox_context_on_submit(ctx, record_submit, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == query);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.form == form && capture.submitter == NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == send);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 2 && capture.submitter == send);
            const tbox_layout_box *send_box = find_context_box(ctx, send);
            TBOX_TEST_ASSERT(send_box != NULL);
            if (send_box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx,
                    send_box->border_box.x + 5.0, send_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(capture.count == 3 && capture.submitter == send);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(capture.count == 3);
            tbox_context_close(ctx);
        }
    }

    /* Search inputs edit like text fields and expose a pointer/Escape clear. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='query' type='SEARCH' value='find me'>"
            "<input type='search' value='off' disabled></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 150.0, &list);
            const tbox_html_node *query = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *box = find_context_box(ctx, query);
            TBOX_TEST_ASSERT(box != NULL);
            bool painted_clear = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                    list.items[i].color.r == 245 && list.items[i].rect.width == 16.0)
                    painted_clear = true;
            TBOX_TEST_ASSERT(painted_clear);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == query);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("new query", 9)));
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx,
                    box->content_box.x + box->content_box.width - 8.0,
                    box->content_box.y + box->content_box.height / 2.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(query, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && value->value.size == 0 && changes == 2);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("again", 5)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ESCAPE, true, false, false}));
            value = tbox_html_node_get_attribute(query, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && value->value.size == 0 && changes == 4);
            tbox_context_close(ctx);
        }
    }

    /* Tel uses ordinary text editing; URL validation checks absolute URLs. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input type='tel' value='+55 85 1234'><input type='url' value='https://example.org/a' required>"
            "<input type='url' value='relative/path'><input type='url' value='mailto:a@example.org'>"
            "<input type='url' required></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 500.0, 200.0, &list);
            const tbox_html_node *tel = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *url = tel->next_sibling;
            TBOX_TEST_ASSERT(tbox_context_url_valid(url));
            TBOX_TEST_ASSERT(!tbox_context_url_valid(url->next_sibling));
            TBOX_TEST_ASSERT(tbox_context_url_valid(url->next_sibling->next_sibling));
            TBOX_TEST_ASSERT(!tbox_context_url_valid(url->next_sibling->next_sibling->next_sibling));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == tel);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("#", 1)));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(tel, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "+55 85 1234#"));
            tbox_context_close(ctx);
        }
    }

    /* Time values are normalized; the picker enforces bounds and exposes
     * hours/minutes through keyboard controls. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input type='time' value='09:30' min='09:00' max='11:00'>"
            "<input type='time' value='25:99'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            const tbox_html_node *input = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *invalid = tbox_html_node_get_attribute(input->next_sibling,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid != NULL && invalid->value.size == 0);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == input);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "10:31") && changes == 1);
            tbox_context_close(ctx);
        }
    }

    /* ISO week stepping crosses calendar years without skipping week 53. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input type='week' value='2020-W53' min='2020-W52' max='2021-W02'>"
            "<input type='week' value='2021-W53'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 250.0, &list);
            const tbox_html_node *input = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *invalid = tbox_html_node_get_attribute(input->next_sibling,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid != NULL && invalid->value.size == 0);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == input);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2021-W01") && changes == 1);
            tbox_context_close(ctx);
        }
    }

    /* A color input paints its #RRGGBB value and edits all three channels
     * through its popup. Disabled controls do not open or take focus. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='chosen' type='COLOR' value='#123456'>"
            "<input id='invalid' type='color' value='wrong'>"
            "<input id='disabled' type='color' disabled></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            const tbox_html_node *chosen = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *invalid = chosen->next_sibling;
            const tbox_html_node *disabled = invalid->next_sibling;
            tbox_rect chosen_rect = {0}, invalid_rect = {0};
            double hx, hy;
            bool found_chosen = hit_point_for(ctx, chosen, 240.0, 220.0, &hx, &hy);
            if (found_chosen) chosen_rect = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_invalid = hit_point_for(ctx, invalid, 240.0, 220.0, &hx, &hy);
            if (found_invalid) invalid_rect = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_disabled = hit_point_for(ctx, disabled, 240.0, 220.0, &hx, &hy);
            TBOX_TEST_ASSERT(found_chosen && found_invalid && found_disabled);
            TBOX_TEST_ASSERT(chosen_rect.width == 48.0 && chosen_rect.height == 24.0);
            bool painted_chosen = false, painted_invalid = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                if (op->rect.x == chosen_rect.x && op->rect.y == chosen_rect.y &&
                    op->rect.width == chosen_rect.width && op->rect.height == chosen_rect.height)
                    painted_chosen = op->color.r == 0x12 && op->color.g == 0x34 && op->color.b == 0x56;
                if (op->rect.x == invalid_rect.x && op->rect.y == invalid_rect.y &&
                    op->rect.width == invalid_rect.width && op->rect.height == invalid_rect.height)
                    painted_invalid = op->color.r == 0 && op->color.g == 0 && op->color.b == 0;
            }
            TBOX_TEST_ASSERT(painted_chosen && painted_invalid);

            color_capture capture = {0};
            tbox_context_on_input(ctx, record_color_input, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == chosen);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == chosen && strcmp(capture.value, "#133456") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, true, false}));
            TBOX_TEST_ASSERT(strcmp(capture.value, "#1d3456") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, false, false}));
            TBOX_TEST_ASSERT(strcmp(capture.value, "#1d0056") == 0);
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            const tbox_layout_box *chosen_box = NULL;
            for (int y = 0; y < 220 && chosen_box == NULL; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node == chosen) chosen_box = hit;
            }
            TBOX_TEST_ASSERT(chosen_box != NULL);
            if (chosen_box != NULL) {
                double popup_x = chosen_box->border_box.x;
                double popup_y = chosen_box->border_box.y + chosen_box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_color_drag(ctx, popup_x + 210.0, popup_y + 73.0));
                TBOX_TEST_ASSERT(strcmp(capture.value, "#1d00ff") == 0);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ESCAPE, true, false, false}));
            TBOX_TEST_ASSERT(!tbox_context_color_drag(ctx, 100.0, 100.0));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == invalid);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == chosen);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)chosen,
                tbox_string_view_make("value", 5), tbox_string_view_make("#abcdef", 7));
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            bool painted_programmatic = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 0xab &&
                    op->color.g == 0xcd && op->color.b == 0xef) painted_programmatic = true;
            }
            TBOX_TEST_ASSERT(painted_programmatic);
            chosen_box = NULL;
            for (int y = 0; y < 220 && chosen_box == NULL; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node == chosen) chosen_box = hit;
            }
            TBOX_TEST_ASSERT(chosen_box != NULL);
            if (chosen_box != NULL) {
                double x = chosen_box->border_box.x + 5.0;
                double y = chosen_box->border_box.y + 5.0;
                int previous_changes = capture.count;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                TBOX_TEST_ASSERT(capture.count == previous_changes);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                TBOX_TEST_ASSERT(!tbox_context_color_drag(ctx, x + 210.0, y + 20.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                tbox_context_run_frame(ctx, 240.0, 220.0, &list);
                chosen_box = NULL;
                for (int scan_y = 0; scan_y < 220 && chosen_box == NULL; scan_y++) {
                    const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)scan_y);
                    if (hit != NULL && hit->node == chosen) chosen_box = hit;
                }
                TBOX_TEST_ASSERT(chosen_box != NULL);
                if (chosen_box != NULL) {
                    double popup_x = chosen_box->border_box.x;
                    double popup_y = chosen_box->border_box.y + chosen_box->border_box.height;
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 210.0, popup_y + 20.0));
                    TBOX_TEST_ASSERT(capture.count == previous_changes + 1);
                    TBOX_TEST_ASSERT(strcmp(capture.value, "#ffcdef") == 0);
                }
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx,
                    (tbox_key_event){TBOX_KEY_ESCAPE, true, false, false}));
            }
            double dx, dy;
            bool clicked_disabled = hit_point_for(ctx, disabled, 240.0, 220.0, &dx, &dy);
            if (clicked_disabled) TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, dx, dy));
            TBOX_TEST_ASSERT(clicked_disabled && tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Date inputs expose a calendar and commit only valid ISO dates within
     * min/max. Navigation crosses leap days and mouse cells use the same value. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='date' type='DATE' value='2024-02-28' min='2024-02-29' max='2024-03-02'>"
            "<input type='date' disabled><button>Next</button>"
            "<input type='date' value='2023-02-29'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_html_node *date = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled = date->next_sibling;
            const tbox_html_node *invalid = disabled->next_sibling->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, date);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) TBOX_TEST_ASSERT(box->content_box.width == 120.0);
            bool painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "2024-02-28")) painted = true;
            TBOX_TEST_ASSERT(painted);
            date_capture capture = {0};
            tbox_context_on_input(ctx, record_date_input, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == date);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("2025", 4)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == date &&
                strcmp(capture.value, "2024-03-01") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == disabled->next_sibling);
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            box = find_context_box(ctx, date);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                double x = box->border_box.x + 5.0;
                double y = box->border_box.y + 5.0;
                double popup_x = box->border_box.x;
                double popup_y = box->border_box.y + box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 29.0, popup_y + 94.0));
                TBOX_TEST_ASSERT(capture.count == 1);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 209.0, popup_y + 70.0));
                TBOX_TEST_ASSERT(capture.count == 2 && strcmp(capture.value, "2024-03-02") == 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 19.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 149.0, popup_y + 166.0));
                TBOX_TEST_ASSERT(capture.count == 3 && strcmp(capture.value, "2024-02-29") == 0);
            }
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Month inputs keep YYYY-MM values and choose from a twelve-month grid. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='month' type='MONTH' value='2024-02' min='2024-03' max='2024-05'>"
            "<input type='month' disabled><input type='month' value='2024-13'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_html_node *month = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled = month->next_sibling;
            const tbox_html_node *invalid = disabled->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, month);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) TBOX_TEST_ASSERT(box->content_box.width == 100.0);
            bool painted_value = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "2024-02")) painted_value = true;
            TBOX_TEST_ASSERT(painted_value);
            month_capture capture = {0};
            tbox_context_on_input(ctx, record_month_input, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == month);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("2025", 4)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            bool painted_march = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Mar")) painted_march = true;
            TBOX_TEST_ASSERT(painted_march);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == month &&
                strcmp(capture.value, "2024-03") == 0);
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            box = find_context_box(ctx, month);
            if (box != NULL) {
                double popup_x = box->border_box.x;
                double popup_y = box->border_box.y + box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0,
                    box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 76.0, popup_y + 50.0));
                TBOX_TEST_ASSERT(capture.count == 1);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 88.0));
                TBOX_TEST_ASSERT(capture.count == 2 && strcmp(capture.value, "2024-05") == 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0,
                    box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 19.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 88.0));
                TBOX_TEST_ASSERT(capture.count == 2);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 214.0, popup_y + 19.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 88.0));
                TBOX_TEST_ASSERT(capture.count == 2);
            }
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            tbox_context_close(ctx);
        }
    }

    /* Datetime-local combines calendar selection and hour/minute changes.
     * Boundary times are checked before confirmation. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='when' type='DATETIME-LOCAL' value='2024-02-29T12:30' "
            "min='2024-02-29T12:00' max='2024-03-01T18:00'>"
            "<input type='datetime-local' value='2024-02-30T10:00'>"
            "<input type='datetime-local' disabled></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 360.0, 440.0, &list);
            const tbox_html_node *when = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *invalid = when->next_sibling;
            const tbox_html_node *disabled = invalid->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, when);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) TBOX_TEST_ASSERT(box->content_box.width == 175.0);
            bool painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "2024-02-29T12:30")) painted = true;
            TBOX_TEST_ASSERT(painted);

            datetime_capture capture = {0};
            tbox_context_on_input(ctx, record_datetime_input, &capture);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == when);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_SPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == when &&
                strcmp(capture.value, "2024-03-01T13:31") == 0);

            tbox_context_run_frame(ctx, 360.0, 440.0, &list);
            box = find_context_box(ctx, when);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                double popup_x = box->border_box.x;
                double popup_y = box->border_box.y + box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 5.0, box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_datetime_drag(ctx, popup_x + 180.0, popup_y + 228.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 200.0, popup_y + 284.0));
                TBOX_TEST_ASSERT(capture.count == 1);
                TBOX_TEST_ASSERT(tbox_context_datetime_drag(ctx, popup_x + 40.0, popup_y + 228.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 180.0, popup_y + 254.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 200.0, popup_y + 284.0));
                TBOX_TEST_ASSERT(capture.count == 2 && strcmp(capture.value, "2024-03-01T00:59") == 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 5.0, box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 19.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 149.0, popup_y + 166.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 200.0, popup_y + 284.0));
                TBOX_TEST_ASSERT(capture.count == 2);
                TBOX_TEST_ASSERT(tbox_context_datetime_drag(ctx, popup_x + 113.0, popup_y + 228.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 200.0, popup_y + 284.0));
                TBOX_TEST_ASSERT(capture.count == 3 && strcmp(capture.value, "2024-02-29T12:59") == 0);
            }
            TBOX_TEST_ASSERT(!tbox_context_datetime_drag(ctx, 100.0, 100.0));
            tbox_context_run_frame(ctx, 360.0, 440.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Email inputs edit like text controls and expose current validity,
     * including required and comma-separated multiple addresses. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='mail' type='EMAIL' value='a@example.com' required>"
            "<input id='multi' type='email' multiple value='a@b, c.d+tag@example.org'>"
            "<input id='empty' type='email'><input id='bad' type='email' value='x@-bad'>"
            "<input id='disabled' type='email' value='off@example.com' disabled></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            const tbox_html_node *mail = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *multi = mail->next_sibling;
            const tbox_html_node *empty = multi->next_sibling;
            const tbox_html_node *bad = empty->next_sibling;
            const tbox_html_node *disabled = bad->next_sibling;
            TBOX_TEST_ASSERT(tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(tbox_context_email_valid(multi));
            TBOX_TEST_ASSERT(tbox_context_email_valid(empty));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(bad));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(NULL));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(mail->parent));
            bool painted_mail = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "a@example.com")) painted_mail = true;
            TBOX_TEST_ASSERT(painted_mail);

            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            click_capture clicks;
            click_capture_reset(&clicks);
            TBOX_TEST_ASSERT(tbox_context_on_click(ctx, "input[type=email]", 17,
                record_click, &clicks) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == mail);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make(".net", 4)));
            TBOX_TEST_ASSERT(changes == 1 && tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("\n", 1)));
            TBOX_TEST_ASSERT(changes == 1);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("bad@@example", 12)));
            TBOX_TEST_ASSERT(changes == 2 && !tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("new@example.com", 15)));
            TBOX_TEST_ASSERT(changes == 3 && tbox_context_email_valid(mail));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)mail,
                tbox_string_view_make("value", 5), tbox_string_view_make("", 0));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(mail));
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == multi);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)multi,
                tbox_string_view_make("value", 5), tbox_string_view_make("a@b, nope", 9));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(multi));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)multi,
                tbox_string_view_make("value", 5), tbox_string_view_make("a@b, x@y", 8));
            TBOX_TEST_ASSERT(tbox_context_email_valid(multi));
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(clicks.call_count == 0);
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Number inputs accept numeric editing, expose constraint validity, and
     * step on Up/Down without exceeding min or max. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='quantity' type='NUMBER' value='1.5' min='1' max='3' step='0.5'>"
            "<input type='number' disabled value='2'>"
            "<input id='required-number' type='number' required min='2'>"
            "<input id='any-step' type='number' value='1.25' step='any'>"
            "<input id='invalid-number' type='number' value='word'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 300.0, &list);
            const tbox_html_node *quantity = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled = quantity->next_sibling;
            const tbox_html_node *required = disabled->next_sibling;
            const tbox_html_node *any = required->next_sibling;
            const tbox_html_node *invalid = any->next_sibling;
            const tbox_layout_box *box = find_context_box(ctx, quantity);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) TBOX_TEST_ASSERT(box->content_box.width == 120.0);
            TBOX_TEST_ASSERT(tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(required));
            TBOX_TEST_ASSERT(tbox_context_number_valid(any));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(NULL));
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid,
                tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == quantity);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("2.25", 4)));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("abc", 3)));
            TBOX_TEST_ASSERT(changes == 1);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(quantity, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2.5"));
            TBOX_TEST_ASSERT(tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
            TBOX_TEST_ASSERT(changes == 4);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("1e2", 3)));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == required);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_UP, true, false, false}));
            value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2"));
            TBOX_TEST_ASSERT(tbox_context_number_valid(required));
            tbox_context_run_frame(ctx, 320.0, 300.0, &list);
            const tbox_layout_box *required_box = find_context_box(ctx, required);
            TBOX_TEST_ASSERT(required_box != NULL);
            if (required_box != NULL) {
                double spinner_x = required_box->content_box.x + required_box->content_box.width - 8.0;
                double spinner_y = required_box->content_box.y + required_box->content_box.height / 4.0;
                bool painted_spinner = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                        list.items[i].color.r == 238 &&
                        list.items[i].rect.x == required_box->content_box.x + required_box->content_box.width - 16.0)
                        painted_spinner = true;
                TBOX_TEST_ASSERT(painted_spinner);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, spinner_x, spinner_y));
                value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "3"));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, spinner_x,
                    required_box->content_box.y + required_box->content_box.height * 0.75));
                value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2"));
            }
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                    disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            tbox_context_close(ctx);
        }
    }

    /* Password values remain in the DOM while rendering one mask glyph per
     * Unicode character; selected secrets are not exposed to the clipboard. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='secret' type='PASSWORD' value='séc'>"
            "<input type='password' disabled value='off'><button>Next</button></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            const tbox_html_node *secret = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "séc"));
            bool painted_secret = false, painted_mask = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind != TBOX_PAINT_TEXT_RUN) continue;
                if (string_view_equal_cstr(list.items[i].text, "séc")) painted_secret = true;
                if (string_view_equal_cstr(list.items[i].text, "•••") ||
                    string_view_equal_cstr(list.items[i].text, "***")) painted_mask = true;
            }
            TBOX_TEST_ASSERT(!painted_secret && painted_mask);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == secret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_selected_text(ctx).size == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "Z"));
            TBOX_TEST_ASSERT(changes == 1);
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            painted_secret = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Z")) painted_secret = true;
            TBOX_TEST_ASSERT(!painted_secret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == secret->next_sibling->next_sibling);
            tbox_context_close(ctx);
        }
    }

    /* File inputs ignore HTML-provided paths, browse the local directory only
     * after activation, and expose the user-selected path separately. */
    {
        char original_directory[4096];
        bool have_directory = getcwd(original_directory, sizeof(original_directory)) != NULL;
        TBOX_TEST_ASSERT(have_directory);
        bool entered_assets = have_directory && chdir(TBOX_TEST_ASSETS_DIR) == 0;
        TBOX_TEST_ASSERT(entered_assets);
        if (entered_assets) {
            tbox_context *ctx = open_cstr(
                "<div><input id='upload' type='FILE' value='/etc/passwd'>"
                "<input id='off' type='file' disabled></div>", "", fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                const tbox_html_node *upload = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
                const tbox_html_node *disabled = upload->next_sibling;
                const tbox_html_attribute *initial = tbox_html_node_get_attribute(upload,
                    tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(initial != NULL && initial->value.size == 0);
                TBOX_TEST_ASSERT(tbox_context_file_path(ctx, upload).size == 0);
                int changes = 0;
                tbox_context_on_input(ctx, record_input, &changes);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == upload);
                TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("/tmp/file", 9)));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_END, true, false, false}));
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool showed_file = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                        string_view_equal_cstr(list.items[i].text, "yellow.png")) showed_file = true;
                TBOX_TEST_ASSERT(showed_file);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
                TBOX_TEST_ASSERT(changes == 1);
                const tbox_html_attribute *selected = tbox_html_node_get_attribute(upload,
                    tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(selected != NULL && string_view_equal_cstr(selected->value, "yellow.png"));
                char expected_path[4096];
                int expected_length = snprintf(expected_path, sizeof(expected_path), "%s/yellow.png", TBOX_TEST_ASSETS_DIR);
                TBOX_TEST_ASSERT(expected_length > 0 && (size_t)expected_length < sizeof(expected_path));
                TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_file_path(ctx, upload), expected_path));

                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool painted_selection = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                        string_view_equal_cstr(list.items[i].text, "yellow.png")) painted_selection = true;
                TBOX_TEST_ASSERT(painted_selection);
                const tbox_layout_box *box = find_context_box(ctx, upload);
                TBOX_TEST_ASSERT(box != NULL);
                if (box != NULL) {
                    double popup_x = box->border_box.x;
                    double popup_y = box->border_box.y + box->border_box.height;
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 5.0, box->border_box.y + 5.0));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_BACKSPACE, true, false, false}));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DOWN, true, false, false}));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
                    TBOX_TEST_ASSERT(tbox_context_scroll(ctx, popup_x + 20.0, popup_y + 100.0, 1.0));
                    TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, popup_x + 20.0, popup_y + 100.0));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_END, true, false, false}));
                    tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 222.0));
                    TBOX_TEST_ASSERT(changes == 2);
                }
                tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)upload,
                    tbox_string_view_make("value", 5), tbox_string_view_make("", 0));
                TBOX_TEST_ASSERT(tbox_context_file_path(ctx, upload).size == 0);
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool painted_empty = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                        string_view_equal_cstr(list.items[i].text, "No file chosen")) painted_empty = true;
                TBOX_TEST_ASSERT(painted_empty);
                const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
                TBOX_TEST_ASSERT(disabled_box != NULL);
                if (disabled_box != NULL)
                    TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx,
                        disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
                tbox_context_close(ctx);
            }
            TBOX_TEST_ASSERT(chdir(original_directory) == 0);
        }
    }

    /* Hidden inputs keep their DOM value, but have no layout box or focus,
     * even when author CSS tries to make them visible. */
    {
        tbox_context *ctx = open_cstr(
            "<div><button id='first'>First</button>"
            "<input id='secret' type='HIDDEN' value='token'>"
            "<button id='second'>Second</button>"
            "<p>Before <input id='inline-secret' type='hidden' value='inline'> after</p></div>",
            "#secret { display: block; width: 300px; height: 100px; } "
            "#inline-secret { display: inline; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_html_node *container = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *first = container->first_child;
            const tbox_html_node *secret = first->next_sibling;
            const tbox_html_node *second = secret->next_sibling;
            const tbox_html_node *inline_secret = second->next_sibling->first_child->next_sibling;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "token"));
            TBOX_TEST_ASSERT(find_context_box(ctx, secret) == NULL);
            TBOX_TEST_ASSERT(find_context_box(ctx, inline_secret) == NULL);
            const tbox_layout_box *first_box = find_context_box(ctx, first);
            const tbox_layout_box *second_box = find_context_box(ctx, second);
            TBOX_TEST_ASSERT(first_box != NULL && second_box != NULL);
            if (first_box != NULL && second_box != NULL)
                /* The hidden input takes no room: the inline-block buttons touch. */
                TBOX_TEST_ASSERT(second_box->margin_box.y == first_box->margin_box.y &&
                    second_box->margin_box.x == first_box->margin_box.x + first_box->margin_box.width);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == first);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == second);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)secret,
                tbox_string_view_make("value", 5), tbox_string_view_make("updated", 7));
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "updated"));
            TBOX_TEST_ASSERT(find_context_box(ctx, secret) == NULL);
            tbox_context_close(ctx);
        }

        ctx = open_cstr("<input type='hidden' value='root-token'>",
                        "input { display: block; width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            TBOX_TEST_ASSERT(list.count == 0);
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 10.0, 10.0) == NULL);
            const tbox_html_node *secret = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "root-token"));
            tbox_context_close(ctx);
        }
    }

    /* Text inputs share keyboard focus with buttons. Editing operates on
     * UTF-8 boundaries and updates both the DOM value and rendered text. */
    {
        tbox_context *ctx = open_cstr(
            "<div><input id='name' value='ab'><input disabled><button>OK</button></div>",
            "input { width: 180px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            const tbox_html_node *field = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == field);
            /* Navigation through an initial HTML value must persist across
             * frames even before the first text insertion. */
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_LEFT, true, false, false}));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, false, false}));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_END, true, false, false}));
            TBOX_TEST_ASSERT(changes == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("é", 2)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_LEFT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_BACKSPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DELETE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            TBOX_TEST_ASSERT(changes == 5);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "Zab"));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("\xff", 1)));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            bool painted_value = false, painted_caret = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Zab")) painted_value = true;
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                    list.items[i].rect.width == 1.0 &&
                    list.items[i].rect.height > 5.0) painted_caret = true;
            }
            TBOX_TEST_ASSERT(painted_value && painted_caret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) != field);
            bool clicked_field = false;
            for (int y = 0; y < 180 && !clicked_field; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node == field) {
                    tbox_context_dispatch_click(ctx, hit->content_box.x + 0.1, hit->content_box.y + 1.0);
                    clicked_field = true;
                }
            }
            TBOX_TEST_ASSERT(clicked_field && tbox_context_focused_node(ctx) == field);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Q", 1)));
            value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "QZab"));
            tbox_context_close(ctx);
        }
    }

    /* Selection operates on UTF-8 boundaries and replaces or deletes the
     * selected range with a single input notification per edit. */
    {
        tbox_context *ctx = open_cstr("<input value='aébc'>", "input { width: 60px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 80.0, &list);
            int changes = 0;
            tbox_context_on_input(ctx, record_input, &changes);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, true, false}));
            tbox_context_run_frame(ctx, 200.0, 80.0, &list);
            bool highlighted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT &&
                    list.items[i].color.r == 130 && list.items[i].rect.width > 0.0)
                    highlighted = true;
            }
            TBOX_TEST_ASSERT(highlighted);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_RIGHT, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_BACKSPACE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_END, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_LEFT, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_DELETE, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_HOME, true, true, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_LEFT, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Q", 1)));
            const tbox_html_node *field = tbox_context_focused_node(ctx);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "QZ"));
            TBOX_TEST_ASSERT(changes == 4);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("!", 1)));
            value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "!"));
            TBOX_TEST_ASSERT(changes == 5);
            tbox_context_close(ctx);
        }
    }

    /* Input text is clipped even before focus. Once focused, its origin
     * scrolls left to keep the caret visible, while the clip stays fixed. */
    {
        tbox_context *ctx = open_cstr("<input value='abcdefghijklmnopqrstuvwxyz'>",
            "input { width: 40px; border: 1px solid black; padding: 2px; background-color: white; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 80.0, &list);
            const tbox_paint_op *text_op = NULL;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN) text_op = &list.items[i];
            }
            TBOX_TEST_ASSERT(text_op != NULL && text_op->has_clip && text_op->clip.width == 40.0);
            tbox_rect input_clip = text_op != NULL ? text_op->clip : (tbox_rect){0};
            uint32_t pixels[300 * 80];
            for (size_t i = 0; i < 300 * 80; i++) pixels[i] = 0xffffffffu;
            tbox_raster_display_list(pixels, 300, 80, &list);
            if (text_op != NULL) {
                int outside_x = (int)(text_op->clip.x + text_op->clip.width + 5.0);
                for (int y = (int)text_op->clip.y; y < (int)(text_op->clip.y + text_op->clip.height); y++)
                    TBOX_TEST_ASSERT(pixels[y * 300 + outside_x] == 0xffffffffu);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            tbox_context_run_frame(ctx, 300.0, 80.0, &list);
            bool scrolled = false, caret_inside = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_TEXT_RUN && op->has_clip)
                    scrolled = op->rect.x < op->clip.x;
                if (op->kind == TBOX_PAINT_FILL_RECT && op->rect.width == 1.0 &&
                    text_op != NULL && op->rect.x >= input_clip.x &&
                    op->rect.x < input_clip.x + input_clip.width)
                    caret_inside = true;
            }
            TBOX_TEST_ASSERT(scrolled && caret_inside);
            for (size_t i = 0; i < 300 * 80; i++) pixels[i] = 0xffffffffu;
            tbox_raster_display_list(pixels, 300, 80, &list);
            int outside_x = (int)(input_clip.x + input_clip.width + 5.0);
            for (int y = (int)input_clip.y; y < (int)(input_clip.y + input_clip.height); y++)
                TBOX_TEST_ASSERT(pixels[y * 300 + outside_x] == 0xffffffffu);
            tbox_context_dispatch_click(ctx, input_clip.x + 2.0, input_clip.y + 2.0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("!", 1)));
            const tbox_html_node *field = tbox_context_focused_node(ctx);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && value->value.size == 27 && value->value.data[0] == 'a');
            tbox_context_close(ctx);
        }
    }

    /* Mouse drag preserves its click anchor across UTF-8 characters. The
     * selected bytes can be copied before cut and inserted again on paste. */
    {
        tbox_context *ctx = open_cstr("<input value='aébc'>", "input { width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 80.0, &list);
            const tbox_layout_box *box = NULL;
            for (int y = 0; y < 80 && box == NULL; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node != NULL &&
                    string_view_equal_cstr(hit->node->element.tag_name, "input")) box = hit;
            }
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                tbox_context_dispatch_click(ctx, box->content_box.x + 70.0, box->content_box.y + 2.0);
                TBOX_TEST_ASSERT(tbox_context_drag_select(ctx, box->content_box.x - 20.0));
                tbox_string_view selected = tbox_context_selected_text(ctx);
                TBOX_TEST_ASSERT(string_view_equal_cstr(selected, "aébc"));
                char copied[6];
                if (selected.size == 5) memcpy(copied, selected.data, 5);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx,
                    (tbox_key_event){TBOX_KEY_DELETE, true, false, false}));
                const tbox_html_node *field = tbox_context_focused_node(ctx);
                const tbox_html_attribute *value = tbox_html_node_get_attribute(field,
                    tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && value->value.size == 0);
                if (selected.size == 5)
                    TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make(copied, 5)));
                value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "aébc"));
            }
            tbox_context_close(ctx);
        }
    }

    TBOX_TEST_ASSERT(tbox_pointer_is_double_click(1200, 1000, 12.0, 9.0, 10.0, 10.0));
    TBOX_TEST_ASSERT(!tbox_pointer_is_double_click(1500, 1000, 12.0, 9.0, 10.0, 10.0));
    TBOX_TEST_ASSERT(!tbox_pointer_is_double_click(1200, 1000, 20.0, 9.0, 10.0, 10.0));

    /* Monotonic scheduler: initial delay, configured rate, disabled repeat,
     * and bounded catch-up after a slow frame. */
    {
        uint64_t next = 400000000u;
        TBOX_TEST_ASSERT(tbox_key_repeat_due(399000000u, &next, 20) == 0);
        TBOX_TEST_ASSERT(tbox_key_repeat_due(400000000u, &next, 20) == 1);
        TBOX_TEST_ASSERT(tbox_key_repeat_due(449000000u, &next, 20) == 0);
        TBOX_TEST_ASSERT(tbox_key_repeat_due(450000000u, &next, 20) == 1);
        TBOX_TEST_ASSERT(tbox_key_repeat_due(1000000000u, &next, 0) == 0);
        TBOX_TEST_ASSERT(tbox_key_repeat_due(1000000000u, &next, 20) == 8);
        TBOX_TEST_ASSERT(next > 1000000000u);
    }

    /* A fixed-height list scrolls, clips painting, and blocks clicks on
     * rows that lie outside its visible padding box. */
    {
        const char *html = "<div id='list'><div id='one'></div><div id='two'></div><div id='three'></div></div>";
        const char *css = "#list { width: 60px; height: 40px; overflow-y: auto; } "
                          "#one, #two, #three { height: 30px; } "
                          "#one { background-color: red; } #two { background-color: green; } "
                          "#three { background-color: blue; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            uint32_t pixels[80 * 100];
            for (size_t i = 0; i < 80 * 100; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFFFF0000u);
            TBOX_TEST_ASSERT(pixels[45 * 80 + 10] == 0xFFFFFFFFu);
            TBOX_TEST_ASSERT(pixels[5 * 80 + 55] == 0xFF646E7Au);
            TBOX_TEST_ASSERT(pixels[35 * 80 + 55] == 0xFFDCE0E6u);
            const tbox_layout_box *hidden = tbox_context_hit_test(ctx, 10.0, 65.0);
            TBOX_TEST_ASSERT(hidden == NULL);
            TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, 10.0, 10.0));
            TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, 55.0, 5.0));
            TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, 5.0, 100.0));
            tbox_context_scrollbar_release(ctx);
            TBOX_TEST_ASSERT(!tbox_context_scrollbar_drag(ctx, 5.0, 0.0));
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            for (size_t i = 0; i < 80 * 100; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFF0000FFu);
            TBOX_TEST_ASSERT(pixels[30 * 80 + 55] == 0xFF646E7Au);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, 55.0, 35.0));
            TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, 10.0, 10.0));
            tbox_context_scrollbar_release(ctx);
            TBOX_TEST_ASSERT(!tbox_context_scrollbar_drag(ctx, 10.0, 10.0));
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            for (size_t i = 0; i < 80 * 100; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFF008000u);
            TBOX_TEST_ASSERT(pixels[5 * 80 + 55] == 0xFFDCE0E6u);
            TBOX_TEST_ASSERT(pixels[25 * 80 + 55] == 0xFF646E7Au);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, 100.0));
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, 100.0));
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            for (size_t i = 0; i < 80 * 100; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFF0000FFu);
            TBOX_TEST_ASSERT(pixels[45 * 80 + 10] == 0xFFFFFFFFu);
            const tbox_layout_box *visible = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(visible != NULL && visible->node != NULL &&
                tbox_html_node_get_attribute(visible->node, tbox_string_view_make("id", 2)) != NULL &&
                string_view_equal_cstr(tbox_html_node_get_attribute(visible->node,
                    tbox_string_view_make("id", 2))->value, "three"));
            tbox_context_close(ctx);
        }
        ctx = open_cstr("<div id='list'><div id='one'></div></div>",
            "#list { width: 60px; height: 40px; overflow-y: auto; } "
            "#one { height: 30px; background-color: red; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, 55.0, 5.0));
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, 10.0));
            tbox_context_close(ctx);
        }
    }

    /* Hidden overflow shares auto's visual and hit-test clip, but never
     * creates a scrollbar or consumes wheel scrolling. */
    {
        tbox_context *ctx = open_cstr(
            "<div id='clip'><div id='first'></div><div id='outside'></div></div>",
            "#clip { width: 60px; height: 30px; overflow-y: hidden; }"
            "#first { height: 30px; background-color: red; }"
            "#outside { height: 30px; background-color: blue; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            uint32_t pixels[80 * 80];
            for (size_t i = 0; i < 80 * 80; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_context_run_frame(ctx, 80.0, 80.0, &list);
            tbox_raster_display_list(pixels, 80, 80, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFFFF0000u);
            TBOX_TEST_ASSERT(pixels[45 * 80 + 10] == 0xFFFFFFFFu);
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 10.0, 45.0) == NULL);
            TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, 55.0, 5.0));
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, 20.0));
            tbox_context_close(ctx);
        }
    }

    /* Textarea keeps its raw initial text, sizes from rows/cols, and edits
     * multiple lines inside a clipped, scrollable content box. */
    {
        tbox_context *ctx = open_cstr("<textarea rows='2' cols='8'>ab\ncd</textarea>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_html_node *node = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_TAB, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == node);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_ENTER, true, false, false}));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(string_view_equal_cstr(node->first_child->text.text, "ab\ncd\nX"));
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(hit != NULL && hit->node == node);
            if (hit != NULL) {
                const tbox_font_face *face = tbox_font_face_cache_get(fonts,
                    tbox_string_view_make(hit->style->font_family, strlen(hit->style->font_family)),
                    hit->style->font_weight_bold, hit->style->font_italic, hit->style->font_size);
                TBOX_TEST_ASSERT(face != NULL);
                if (face != NULL) {
                    double expected_width = 8.0 * tbox_font_measure_text(face, tbox_string_view_make("0", 1));
                    double expected_height = 2.0 * tbox_font_face_line_height(face);
                    double width_error = hit->content_box.width - expected_width;
                    double height_error = hit->content_box.height - expected_height;
                    TBOX_TEST_ASSERT(width_error > -0.01 && width_error < 0.01);
                    TBOX_TEST_ASSERT(height_error > -0.01 && height_error < 0.01);
                }
                TBOX_TEST_ASSERT(hit->text_run_count == 3);
                TBOX_TEST_ASSERT(hit->scroll_content_height > hit->content_box.height);
                TBOX_TEST_ASSERT(tbox_context_scroll(ctx, hit->content_box.x + 2.0,
                    hit->content_box.y + 2.0, -100.0));
                TBOX_TEST_ASSERT(tbox_context_drag_select_at(ctx, hit->content_box.x,
                    hit->content_box.y));
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, (tbox_key_event){TBOX_KEY_A, true, false, true}));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_selected_text(ctx), "ab\ncd\nX"));
            tbox_context_close(ctx);
        }
        ctx = open_cstr("<textarea rows='5' cols='40'>A</textarea>",
            "textarea { width: 90px; height: 35px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_layout_box *box = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(box != NULL && box->content_box.width == 90.0 &&
                box->content_box.height == 35.0);
            tbox_context_close(ctx);
        }
    }

    /* A decorative overlay lets clicks reach the box behind it, while a
     * descendant that explicitly restores auto can still receive clicks. */
    {
        tbox_style overlay_style = {0};
        overlay_style.pointer_events_none = true;
        tbox_style child_style = {0};
        tbox_layout_box root_box = {0}, behind = {0}, overlay = {0}, child = {0};
        root_box.border_box = (tbox_rect){0, 0, 100, 100};
        behind.border_box = (tbox_rect){0, 0, 100, 100};
        overlay.border_box = (tbox_rect){0, 0, 100, 100};
        overlay.style = &overlay_style;
        child.border_box = (tbox_rect){10, 10, 20, 20};
        child.style = &child_style;
        root_box.first_child = &behind;
        root_box.last_child = &overlay;
        behind.next_sibling = &overlay;
        overlay.first_child = overlay.last_child = &child;
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 50, 50) == &behind);
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 20, 20) == &child);
        overlay_style.pointer_events_none = false;
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 50, 50) == &overlay);
    }

    {
        tbox_context *ctx = open_cstr(
            "<div><button>Under</button><span id='overlay'>Tint</span></div>",
            "div { width: 120px; height: 45px; position: relative; }"
            "button { width: 100px; height: 40px; }"
            "#overlay { display: block; position: absolute; top: 0; left: 0;"
            " width: 100px; height: 40px; pointer-events: none; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 100.0, &list);
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(hit != NULL && hit->node != NULL &&
                string_view_equal_cstr(hit->node->element.tag_name, "button"));
            tbox_context_close(ctx);
        }
    }

    /* NOVO v15: a point inside an inline-block hits its own box (it is a
     * child of the text box around it), and the box sits inside the line. */
    {
        tbox_context *ctx = open_cstr("<p>ab <span>cd</span></p>",
            "span { display: inline-block; width: 60px; height: 30px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_layout_box *root = tbox_context_hit_test(ctx, 5.0, 20.0); /* inside the <p> */
            while (root != NULL && root->parent != NULL) root = root->parent;
            const tbox_layout_box *p = find_box_by_tag(root, "p");
            const tbox_layout_box *span = p != NULL ? p->first_child : NULL;
            TBOX_TEST_ASSERT(span != NULL && span->node != NULL);
            if (span != NULL) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, span->border_box.x + 30.0,
                                                                   span->border_box.y + 15.0);
                TBOX_TEST_ASSERT(hit == span || (hit != NULL && hit->node == NULL && hit->parent == span));
                TBOX_TEST_ASSERT(span->border_box.x > p->content_box.x);
            }
            tbox_context_close(ctx);
        }
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);

    return failures;
}
