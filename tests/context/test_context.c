#include <tbox/context.h>
#include <tbox/css_selector.h>
#include <tbox/image.h>
#include <tbox/output.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "base/tbox_arena.h"
#include "context/tbox_context_hit_test.h"
#include "context/tbox_context_internal.h"
#include "context/tbox_ua_style.h"
#include "output/tbox_key_repeat.h"
#include "output/tbox_pointer_click.h"
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
    return tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
}

static const tbox_layout_box *find_layout_box(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node)
            return box;
        const tbox_layout_box *child = find_layout_box(box->first_child, node);
        if (child != NULL)
            return child;
    }
    return NULL;
}

/* Finds a point, scanning the viewport row by row, where the hit test lands
 * on `node`'s own box. Form controls are inline-blocks, so a control can
 * sit anywhere along its line, not just at a fixed x. */
static bool hit_point_for(const tbox_context *ctx, const tbox_html_node *node, double width, double height, double *out_x, double *out_y) {
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
        if (box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT && string_view_equal_cstr(box->node->element.tag_name, tag))
            return box;
        const tbox_layout_box *child = find_box_by_tag(box->first_child, tag);
        if (child != NULL)
            return child;
    }
    return NULL;
}

static const tbox_layout_box *find_context_box(const tbox_context *ctx, const tbox_html_node *node) {
    const tbox_layout_box *root = tbox_context_hit_test(ctx, 10.0, 10.0);
    while (root != NULL && root->parent != NULL)
        root = root->parent;
    return find_layout_box(root, node);
}

/* Shared by every tbox_context_bind_click test below: a click_capture is
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

/* tbox_context_click_handler now returns bool (true = keep
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

/* same as record_click, but returns false (stopPropagation) -- for
 * tests proving a handler can prevent a farther ancestor's otherwise-
 * matching handler from firing. */
static bool record_click_stop(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    click_capture *capture = (click_capture *)userdata;
    capture->call_count++;
    capture->node = node;
    return false;
}

/* tbox_destroy_notify bookkeeping: how often a probe was released and
 * called, and what its handler saw right after removing itself. */
typedef struct release_probe {
    int released;
    int calls;
    int released_seen_in_handler;
    int binding;
} release_probe;

static void probe_release(void *userdata) {
    ((release_probe *)userdata)->released++;
}

static bool probe_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    (void)node;
    ((release_probe *)userdata)->calls++;
    return true;
}

static bool probe_click_unbind_self(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)node;
    release_probe *probe = userdata;
    probe->calls++;
    tbox_context_unbind_click(ctx, probe->binding);
    probe->released_seen_in_handler = probe->released; /* still usable here */
    return true;
}

static void probe_input_replace_self(tbox_context *ctx, tbox_html_node *node, const char *value, size_t value_length, void *userdata) {
    (void)node;
    (void)value;
    (void)value_length;
    release_probe *probe = userdata;
    probe->calls++;
    tbox_context_set_input_handler(ctx, NULL, NULL, NULL);
    probe->released_seen_in_handler = probe->released;
}

static void probe_select(tbox_context *ctx, tbox_html_node *node, const char *value, size_t value_length, void *userdata) {
    (void)ctx;
    (void)node;
    (void)value;
    (void)value_length;
    ((release_probe *)userdata)->calls++;
}

static void probe_submit(tbox_context *ctx, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    (void)ctx;
    (void)form;
    (void)submitter;
    ((release_probe *)userdata)->calls++;
}

/* The user-agent config tests: options carrying just that config. */
static tbox_context *open_with_ua_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_ua_style_config config) {
    tbox_context_options options = tbox_context_options_default();
    options.ua_style             = config;
    return tbox_context_open_with_options(html, html_length, css, css_length, fonts, images, &options);
}

static void record_input(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
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

static void record_color_input(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
    (void)ctx;
    color_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 7)
        memcpy(capture->value, value.data, 7);
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

static void record_submit(tbox_context *ctx, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    (void)ctx;
    submit_capture *capture = userdata;
    capture->count++;
    capture->form      = form;
    capture->submitter = submitter;
}

static void record_month_input(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
    (void)ctx;
    month_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 7)
        memcpy(capture->value, value.data, 7);
    capture->value[7] = '\0';
}

static void record_date_input(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
    (void)ctx;
    date_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 10)
        memcpy(capture->value, value.data, 10);
    capture->value[10] = '\0';
}

typedef struct datetime_capture {
    int count;
    char value[17];
    const tbox_html_node *node;
} datetime_capture;

static void record_datetime_input(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
    (void)ctx;
    datetime_capture *capture = userdata;
    capture->count++;
    capture->node = node;
    if (value.size == 16)
        memcpy(capture->value, value.data, 16);
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
    capture->node    = node;
    return true;
}

typedef struct select_capture {
    int count;
    char value[64];
} select_capture;

static void record_select(tbox_context *ctx, tbox_html_node *node, const char *value_data, size_t value_length, void *userdata) {
    tbox_string_view value = tbox_string_view_make(value_data, value_length);
    (void)ctx;
    (void)node;
    select_capture *capture = userdata;
    capture->count++;
    size_t length = value.size < sizeof(capture->value) - 1 ? value.size : sizeof(capture->value) - 1;
    if (length > 0)
        memcpy(capture->value, value.data, length);
    capture->value[length] = '\0';
}

/* shared by the bubbling-order tests -- each handler appends its
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
 * pattern the v1 vertical slice (docs/dev/historico.md's "Fatia vertical v1")
 * uses to swap a CSS class on click. */
static bool toggle_class_handler(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_html_document *document = tbox_context_document(ctx);
    tbox_html_node_set_attribute(document, node, tbox_string_view_make("class", 5), tbox_string_view_make("box on", 6));
    return true;
}

/* Shared setup for UA stylesheet tests: build the production typed UA sheet,
 * then cascade and resolve the single top-level element. Has NO TBOX_TEST_ASSERT/
 * TBOX_TEST_ASSERT_MSG calls of its own -- those macros increment a local
 * variable literally named `failures` (see test_support.h), so they only
 * work inlined directly in tbox_test_context_run itself, not in a helper it
 * calls. Callers check
 * the returned bool and assert on `*out_style`. `html` must be a single
 * top-level element, e.g. "<i>x</i>" -- the document root's first child,
 * same assumption test 37 makes explicit. Returns false (leaving
 * `*out_style` untouched) if any setup step fails. */
static bool resolve_first_child_style_with_config(const char *html, tbox_ua_style_config config, tbox_style *out_style) {
    tbox_html_document *doc = tbox_html_parse(html, strlen(html));
    if (doc == NULL) {
        return false;
    }

    const tbox_html_node *node = tbox_html_document_root(doc)->first_child;
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) {
        tbox_html_document_destroy(doc);
        return false;
    }

    tbox_css_stylesheet *ua_sheet = tbox_ua_stylesheet_create(config);
    if (ua_sheet == NULL) {
        tbox_html_document_destroy(doc);
        return false;
    }

    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(ua_sheet, node);
    /* Keeps the style's out-of-line groups alive for the caller's checks
     * until the process exits (still reachable, not a leak). */
    static tbox_arena style_arena;
    static bool style_arena_ready;
    if (!style_arena_ready) {
        style_arena       = tbox_arena_create(0);
        style_arena_ready = true;
    }
    *out_style = tbox_style_resolve(&style_arena, node, NULL, &computed);

    tbox_css_computed_style_destroy(&computed);
    tbox_css_stylesheet_destroy(ua_sheet);
    tbox_html_document_destroy(doc);

    return true;
}

static bool resolve_first_child_style(const char *html, tbox_style *out_style) {
    return resolve_first_child_style_with_config(html, tbox_ua_style_config_default(), out_style);
}

int tbox_test_svg_run(tbox_font_face_cache *fonts);
int tbox_test_placeholder_run(tbox_font_face_cache *fonts);
int tbox_test_controls_api_run(tbox_font_face_cache *fonts);
int tbox_test_inline_positioned_run(tbox_font_face_cache *fonts);
int tbox_test_focus_visible_run(tbox_font_face_cache *fonts);

int tbox_test_context_run(void) {
    int failures = 0;

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");
    if (font_data == NULL) {
        return failures + 1;
    }

    /* tbox_context_open now takes a tbox_font_face_cache, not a
     * single tbox_font_face -- same regular-bytes-for-both-slots
     * placeholder tbox_app_create uses (see src/app/tbox_app.c), fine here
     * since none of these tests exercise bold text specifically. */
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
    TBOX_TEST_ASSERT_MSG(fonts != NULL, "failed to create font face cache");
    if (fonts == NULL) {
        free(font_data);
        return failures + 1;
    }

    failures += tbox_test_svg_run(fonts);
    failures += tbox_test_placeholder_run(fonts);
    failures += tbox_test_controls_api_run(fonts);
    failures += tbox_test_inline_positioned_run(fonts);
    failures += tbox_test_focus_visible_run(fonts);

    /* 1: a <div> styled with background-color, run end to end, produces a
     * display list with the expected FILL_RECT (rect == border_box,
     * color == the declared background-color). */
    {
        /* uses an EMPTY <div> -- with the fixed-text-tag debt
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
        /* the inner <div> is EMPTY -- a loose "a" would now
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
     * the <button> is EMPTY -- a loose "Click" label would now
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
            TBOX_TEST_ASSERT_MSG(tbox_context_bind_click(ctx, "button", strlen("button"), record_click, &capture, NULL) >= 0, "tbox_context_bind_click must succeed for a well-formed selector");

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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "button", strlen("button"), record_click, &capture, NULL) >= 0);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);

            bool dispatched = tbox_context_dispatch_click(ctx, 500.0, 500.0);
            TBOX_TEST_ASSERT_MSG(!dispatched, "a click outside every box must not dispatch");
            TBOX_TEST_ASSERT_MSG(capture.call_count == 0, "no handler may fire for a click outside every box");

            tbox_context_close(ctx);
        }
    }

    /* tbox_destroy_notify: released exactly once -- at unbind, at
     * replacement, at close -- never for a failed registration, and only
     * after a handler that removes itself has returned. */
    {
        tbox_context *ctx = open_cstr("<button></button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            release_probe unbound = { 0 }, kept = { 0 }, self = { 0 }, failed = { 0 };
            release_probe first_input = { 0 }, second_input = { 0 }, select = { 0 }, submit = { 0 };
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "[", 1, probe_click, &failed, probe_release) == -1);
            int unbound_id = tbox_context_bind_click(ctx, "button", 6, probe_click, &unbound, probe_release);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "button", 6, probe_click, &kept, probe_release) >= 0);
            self.binding = tbox_context_bind_click(ctx, "button", 6, probe_click_unbind_self, &self, probe_release);
            TBOX_TEST_ASSERT(unbound_id >= 0 && self.binding >= 0);

            TBOX_TEST_ASSERT(tbox_context_unbind_click(ctx, unbound_id));
            TBOX_TEST_ASSERT(!tbox_context_unbind_click(ctx, unbound_id));
            TBOX_TEST_ASSERT(unbound.released == 1);

            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, 10.0, 10.0));
            TBOX_TEST_ASSERT(self.calls == 1 && self.released_seen_in_handler == 0 && self.released == 1);
            TBOX_TEST_ASSERT(kept.calls == 1 && kept.released == 0 && unbound.calls == 0);

            tbox_context_set_input_handler(ctx, record_input, &first_input, probe_release);
            tbox_context_set_input_handler(ctx, record_input, &second_input, probe_release);
            TBOX_TEST_ASSERT(first_input.released == 1 && second_input.released == 0);
            tbox_context_set_input_handler(ctx, NULL, NULL, NULL);
            TBOX_TEST_ASSERT(second_input.released == 1);
            tbox_context_set_select_handler(ctx, probe_select, &select, probe_release);
            tbox_context_set_submit_handler(ctx, probe_submit, &submit, probe_release);

            tbox_context_close(ctx);
            TBOX_TEST_ASSERT(kept.released == 1 && select.released == 1 && submit.released == 1);
            TBOX_TEST_ASSERT(unbound.released == 1 && self.released == 1 && failed.released == 0);
            TBOX_TEST_ASSERT(first_input.released == 1 && second_input.released == 1);
        }
    }
    {
        tbox_context *ctx = open_cstr("<input>", "input { width: 100px; height: 20px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            release_probe probe = { 0 };
            tbox_context_set_input_handler(ctx, probe_input_replace_self, &probe, probe_release);
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            tbox_context_dispatch_click(ctx, 5.0, 5.0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            TBOX_TEST_ASSERT(probe.calls == 1 && probe.released_seen_in_handler == 0 && probe.released == 1);
            tbox_context_close(ctx);
            TBOX_TEST_ASSERT(probe.released == 1);
        }
    }

    /* 9: two nested elements where only the OUTER one matches the
     * registered selector -- a click on the inner element must still fire
     * the handler, on the outer element (nearest matching ancestor), via
     * the node->parent walk.
     *
     * the inner <div> is EMPTY -- a loose "x" would now build its
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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".outer", strlen(".outer"), record_click, &capture, NULL) >= 0);

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
     * the <div> is EMPTY -- a loose "x" would now build its own
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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "#target", strlen("#target"), record_click, &id_capture, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".box", strlen(".box"), record_click, &class_capture, NULL) >= 0);

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

    /* 11: a syntax error in the selector makes tbox_context_bind_click return
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
            int registered = tbox_context_bind_click(ctx, ">", strlen(">"), record_click, &capture, NULL);
            TBOX_TEST_ASSERT_MSG(registered == -1, "a selector syntax error must make tbox_context_bind_click return -1");

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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "button", strlen("button"), record_click, &capture, NULL) >= 0);

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
     * the <div> is EMPTY -- a loose "x" would now build its own
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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".off", strlen(".off"), toggle_class_handler, NULL, NULL) >= 0);

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

    /*  (Tarefa 4): end-to-end proof that the user-agent stylesheet
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

        tbox_context *default_ctx    = open_cstr("<h1>oi</h1>", "", fonts);
        tbox_context *overridden_ctx = open_cstr("<h1>oi</h1>", "h1 { font-size: 10px; }", fonts);
        TBOX_TEST_ASSERT_MSG(default_ctx != NULL && overridden_ctx != NULL, "tbox_context_open must succeed for both documents");
        if (default_ctx != NULL && overridden_ctx != NULL) {
            tbox_display_list default_list, overridden_list;
            tbox_context_run_frame(default_ctx, 800.0, 600.0, &default_list);
            tbox_context_run_frame(overridden_ctx, 800.0, 600.0, &overridden_list);

            /* Author CSS declares no margin, so the UA h1 margin-top (21px
             * default) still applies to both documents -- same hit-test
             * offset as test 14 above. */
            double hit_y                          = default_config.margin.heading_px[0] + 2.0;
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
     * the built UA stylesheet, not just tbox_ua_style_config_default()'s
     * own baked-in values. */
    {
        tbox_ua_style_config default_config = tbox_ua_style_config_default();
        tbox_ua_style_config custom_config  = default_config;
        custom_config.font.heading_em[0]    = 5.0;

        tbox_context *default_ctx = open_with_ua_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, NULL, default_config);
        tbox_context *custom_ctx  = open_with_ua_config("<h1>oi</h1>", strlen("<h1>oi</h1>"), "", 0, fonts, NULL, custom_config);
        TBOX_TEST_ASSERT_MSG(default_ctx != NULL && custom_ctx != NULL, "tbox_context_open_with_config must succeed for both configs");
        if (default_ctx != NULL && custom_ctx != NULL) {
            tbox_display_list default_list, custom_list;
            tbox_context_run_frame(default_ctx, 800.0, 600.0, &default_list);
            tbox_context_run_frame(custom_ctx, 800.0, 600.0, &custom_list);

            /* Neither config changes margin, so both still offset by the
             * same UA heading margin-top. */
            double hit_y                       = default_config.margin.heading_px[0] + 2.0;
            const tbox_layout_box *default_box = tbox_context_hit_test(default_ctx, 5.0, hit_y);
            const tbox_layout_box *custom_box  = tbox_context_hit_test(custom_ctx, 5.0, hit_y);
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

        const char *html          = "<body><p>oi</p></body>";
        tbox_context *default_ctx = open_with_ua_config(html, strlen(html), "", 0, fonts, NULL, default_config);
        tbox_context *custom_ctx  = open_with_ua_config(html, strlen(html), "", 0, fonts, NULL, custom_config);
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
     * the <div> is EMPTY -- a loose "x" would now build its own
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
     * the <div> is EMPTY, same rationale as test 19 above -- a
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
     * the <div> is EMPTY -- a loose "x" would now build its own
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
     * the inner <div> is EMPTY -- a loose "x" would now build its
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
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".inner", strlen(".inner"), bubble_log_inner_handler, &log, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".outer", strlen(".outer"), bubble_log_outer_handler, &log, NULL) >= 0);

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
     * the inner <div> is EMPTY, same rationale as test 22 above. */
    {
        tbox_context *ctx = open_cstr("<div class=\"outer\"><div class=\"inner\"></div></div>", ".outer { width: 100px; height: 100px; } .inner { width: 50px; height: 50px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture inner_capture;
            click_capture outer_capture;
            click_capture_reset(&inner_capture);
            click_capture_reset(&outer_capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".inner", strlen(".inner"), record_click_stop, &inner_capture, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, ".outer", strlen(".outer"), record_click, &outer_capture, NULL) >= 0);

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
     * the <button> is EMPTY, same rationale as test 7 above -- a
     * loose "Click" label would steal the dispatch target as an anonymous
     * (node == NULL) box. */
    {
        tbox_context *ctx = open_cstr("<button></button>", "button { width: 100px; height: 40px; }", fonts);
        TBOX_TEST_ASSERT_MSG(ctx != NULL, "tbox_context_open must succeed");
        if (ctx != NULL) {
            click_capture capture;
            click_capture_reset(&capture);
            int binding = tbox_context_bind_click(ctx, "button", strlen("button"), record_click, &capture, NULL);
            TBOX_TEST_ASSERT_MSG(binding >= 0, "tbox_context_bind_click must succeed for a well-formed selector");

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
            int binding = tbox_context_bind_click(ctx, "button", strlen("button"), record_click, &capture, NULL);
            TBOX_TEST_ASSERT_MSG(binding >= 0, "tbox_context_bind_click must succeed for a well-formed selector");

            TBOX_TEST_ASSERT_MSG(tbox_context_unbind_click(ctx, binding), "the first unbind of a real registration must succeed");
            TBOX_TEST_ASSERT_MSG(!tbox_context_unbind_click(ctx, binding), "unbinding the SAME handle a second time must return false, not crash");

            tbox_context_close(ctx);
        }
    }

    /* 26: v5's Orchestration hit-test fix -- tbox_context_hit_test_box
     * called directly against a hand-built tbox_layout_box tree (no HTML/
     * CSS, no Style/Layout pipeline needed: this is about the hit-test
     * algorithm itself, see docs/dev/historico.md's "Orchestration -- correção
     * de hit-test pra caixas fora de fluxo"). Two artificially overlapping
     * SIBLINGS: a point in the overlap must resolve to the LATER sibling
     * in `next_sibling` order (the one painted on top, since the project
     * has no stacking context/z-index and always paints in document
     * order), not the first one that happens to match. */
    {
        tbox_layout_box root_box;
        memset(&root_box, 0, sizeof(root_box));
        root_box.border_box = (tbox_rect){ 0.0, 0.0, 100.0, 100.0 };

        tbox_layout_box sibling_a;
        memset(&sibling_a, 0, sizeof(sibling_a));
        sibling_a.parent     = &root_box;
        sibling_a.border_box = (tbox_rect){ 0.0, 0.0, 50.0, 50.0 };

        tbox_layout_box sibling_b;
        memset(&sibling_b, 0, sizeof(sibling_b));
        sibling_b.parent = &root_box;
        /* Deliberately overlapping sibling_a in [10,50) x [10,50). */
        sibling_b.border_box = (tbox_rect){ 10.0, 10.0, 50.0, 50.0 };

        root_box.first_child   = &sibling_a;
        root_box.last_child    = &sibling_b;
        sibling_a.next_sibling = &sibling_b;

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
        parent_box.border_box = (tbox_rect){ 0.0, 0.0, 50.0, 50.0 };

        tbox_layout_box escaped_child;
        memset(&escaped_child, 0, sizeof(escaped_child));
        escaped_child.parent = &parent_box;
        /* Fully outside parent_box's border_box ([0,50) x [0,50)). */
        escaped_child.border_box = (tbox_rect){ 200.0, 200.0, 30.0, 30.0 };

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

    /* 28: a <ul><li>x</li></ul> with no author CSS gets a
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

    /* 29: regression -- adding list rules must not change the UA margins
     * of isolated <p> and <h1> elements. */
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

    /* 30: an embedded <style>.algo{background-color:blue;}</style>
     * (no external CSS at all -- `css` is "") must reach the cascade: a
     * <div class="algo"> resolves the FILL_RECT blue, proving <style>
     * content, until now inert text (same treatment as <script>), now
     * participates in the cascade.
     *
     * Wrapped in an outer <div> (rather than two top-level siblings):
     * tbox_layout_build only ever lays out the FIRST top-level element under
     * the document root (a pre-existing v0 limitation, unrelated to this
     * task -- see docs/dev/historico.md's v9 "Escopo" note about a <style> inside
     * a never-drawn <head>). A bare <style>.algo{...}</style><div
     * class="algo">...</div> at the top level would make <style> itself the
     * one element Layout builds, and the following <div> would never be laid
     * out at all. Nesting both inside one wrapper sidesteps that: the
     * wrapper is the sole top-level element, and <style> (not one of Layout
     * Tree's fixed text-tag list -- h1-h6/p/li -- so its raw text content is
     * never collected as words) paints nothing of its own, leaving the FILL_RECT
     * count entirely attributable to div.algo.
     *
     * div.algo itself is EMPTY -- a loose "x" would now build its
     * own anonymous text box (a second, TEXT_RUN paint op), irrelevant to
     * this test's actual point (which stylesheet source wins the cascade). */
    {
        tbox_context *ctx = open_cstr("<div><style>.algo{background-color:blue;}</style><div class=\"algo\"></div></div>", "", fonts);
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
     * CSS (decision documented in docs/dev/historico.md's v9 "Escopo": internal
     * is placed LAST in tbox_context_run_frame's sources array). */
    {
        /* div.algo is EMPTY, same rationale as test 30 above. */
        tbox_context *ctx = open_cstr("<div><style>.algo{background-color:blue;}</style><div class=\"algo\"></div></div>", ".algo{background-color:green;}", fonts);
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
        /* div.algo is EMPTY, same rationale as test 30 above. */
        tbox_context *ctx = open_cstr("<div class=\"algo\"></div>", ".algo{background-color:green;}", fonts);
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
     * both div.a/div.b are EMPTY -- loose "x"/"y" would each now
     * build their own anonymous text box (extra TEXT_RUN paint ops),
     * irrelevant to this test's actual point (both <style> blocks applying). */
    {
        tbox_context *ctx = open_cstr("<div>"
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

    /* 34:  -- a <hr> alone (no author CSS) gets an explicit height
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

    /* 35:  -- a <hr> alone resolves a Gray (0x80, 0x80, 0x80)
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

    /* 36: regression -- adding the <hr> rule must not change the UA
     * margins of isolated <p> and <h1> elements. */
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

    /* 37: <pre> gets monospace from the production UA stylesheet. */
    {
        tbox_style style;
        bool resolved = resolve_first_child_style("<pre>x</pre>", &style);
        TBOX_TEST_ASSERT_MSG(resolved, "resolve_first_child_style must succeed for <pre>");
        if (resolved)
            TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "monospace") == 0, "a <pre> with no author CSS must resolve monospace from the UA stylesheet");
    }

    /* Attribute and focus selectors must still participate in the cascade. */
    {
        tbox_html_document *doc           = tbox_html_parse("<input type=checkbox>", strlen("<input type=checkbox>"));
        tbox_css_stylesheet *ua_sheet     = tbox_ua_stylesheet_create(tbox_ua_style_config_default());
        tbox_css_stylesheet *author_sheet = tbox_css_parse("input { width: 30px; }", strlen("input { width: 30px; }"));
        TBOX_TEST_ASSERT_MSG(doc != NULL && ua_sheet != NULL && author_sheet != NULL, "UA selector test setup must succeed");
        if (doc != NULL && ua_sheet != NULL && author_sheet != NULL) {
            const tbox_html_node *input                = tbox_html_document_root(doc)->first_child;
            tbox_css_computed_style computed           = tbox_css_cascade_resolve_stylesheet(ua_sheet, input);
            const tbox_css_resolved_declaration *width = tbox_css_computed_style_find(&computed, tbox_string_view_make("width", 5));
            TBOX_TEST_ASSERT_MSG(width != NULL && string_view_equal_cstr(width->value, "12px"), "checkbox width must override the generic UA input width");
            tbox_css_computed_style_destroy(&computed);

            tbox_css_selector_set_focus_context(input);
            computed                                    = tbox_css_cascade_resolve_stylesheet(ua_sheet, input);
            /* Focus is an outline over the 1px border: the border (and so
             * the layout) stays the unfocused one. */
            const tbox_css_resolved_declaration *border  = tbox_css_computed_style_find(&computed, tbox_string_view_make("border", 6));
            const tbox_css_resolved_declaration *outline = tbox_css_computed_style_find(&computed, tbox_string_view_make("outline", 7));
            const tbox_css_resolved_declaration *offset  = tbox_css_computed_style_find(&computed, tbox_string_view_make("outline-offset", 14));
            TBOX_TEST_ASSERT_MSG(border != NULL && string_view_equal_cstr(border->value, "1px solid #808080"), "focus must keep the generic UA border");
            TBOX_TEST_ASSERT_MSG(outline != NULL && string_view_equal_cstr(outline->value, "2px solid #808080"), "focused input must get the UA focus ring, in the UA border's gray");
            TBOX_TEST_ASSERT_MSG(offset != NULL && string_view_equal_cstr(offset->value, "-1px"), "the focus ring covers the 1px border");
            tbox_css_computed_style_destroy(&computed);
            tbox_css_selector_set_focus_context(NULL);

            tbox_css_cascade_source sources[] = {
                { ua_sheet,     TBOX_CSS_ORIGIN_USER_AGENT },
                { author_sheet, TBOX_CSS_ORIGIN_AUTHOR     },
            };
            computed = tbox_css_cascade_resolve(sources, 2, input);
            width    = tbox_css_computed_style_find(&computed, tbox_string_view_make("width", 5));
            TBOX_TEST_ASSERT_MSG(width != NULL && string_view_equal_cstr(width->value, "30px"), "author width must override the UA attribute selector");
            tbox_css_computed_style_destroy(&computed);
        }
        tbox_css_stylesheet_destroy(author_sheet);
        tbox_css_stylesheet_destroy(ua_sheet);
        tbox_html_document_destroy(doc);
    }

    /* Custom values reach the rules for headings, lists and horizontal rules. */
    {
        tbox_ua_style_config custom = tbox_ua_style_config_default();
        custom.font.heading_em[5]   = 1.25;
        custom.margin.heading_px[5] = 37.0;
        custom.list_padding_left_px = 53.0;
        custom.hr_height_px         = 5.0;
        custom.margin.hr_px         = 11.0;
        tbox_style h6_style, ul_style, hr_style;
        bool h6_ok = resolve_first_child_style_with_config("<h6>x</h6>", custom, &h6_style);
        bool ul_ok = resolve_first_child_style_with_config("<ul></ul>", custom, &ul_style);
        bool hr_ok = resolve_first_child_style_with_config("<hr>", custom, &hr_style);
        TBOX_TEST_ASSERT_MSG(h6_ok && ul_ok && hr_ok, "custom UA styles must resolve");
        if (h6_ok) {
            TBOX_TEST_ASSERT_MSG(h6_style.font_size == 20.0 && h6_style.margin[0].value == 37.0, "h6 must use configured font scale and margin");
        }
        if (ul_ok) {
            TBOX_TEST_ASSERT_MSG(ul_style.padding[3].value == 53.0, "ul must use configured left padding");
        }
        if (hr_ok) {
            TBOX_TEST_ASSERT_MSG(hr_style.height.value == 5.0 && hr_style.margin[0].value == 11.0, "hr must use configured height and margin");
        }
    }

    /* 38:  (Tarefa 5) -- <i>/<em> resolve font_italic == true purely
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

    /* 39:  (Tarefa 5) -- <small> resolves a font_size 80% of the
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

    /* 40:  (Tarefa 5) -- <mark> resolves an opaque yellow
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

    /* 41:  (Tarefa 5) -- <del>/<ins> resolve text_decoration
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

    /* 42:  (Tarefa 5) -- <sub>/<sup> resolve vertical_align SUB/
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
        tbox_context *ctx = open_cstr("<div><button id='one'>One</button><button disabled>Skip</button>"
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
            const tbox_html_node *one  = root->first_child->first_child;
            const tbox_html_node *two  = one->next_sibling->next_sibling->next_sibling;
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "#one", 4, record_click, &capture, NULL) >= 0);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            bool focus_painted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 255 && list.items[i].color.g == 0 && list.items[i].color.b == 0) {
                    focus_painted = true;
                }
            }
            TBOX_TEST_ASSERT_MSG(focus_painted, ":focus must reach CSS painting");
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, false, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.call_count == 1 && capture.node == one);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.call_count == 2);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            double two_x, two_y;
            bool clicked_two = hit_point_for(ctx, two, 320.0, 200.0, &two_x, &two_y);
            if (clicked_two) {
                /* Removing keyboard focus visibility also requires a frame. */
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, two_x, two_y));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            }
            TBOX_TEST_ASSERT_MSG(clicked_two, "pointer click must focus a rendered button without a handler");
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == one);
            tbox_html_node_remove((tbox_html_node *)one);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == two);
            tbox_context_close(ctx);
        }
    }

    /* Tab and Shift+Tab reveal controls in a scrollable list. Manual wheel
     * scrolling after focus stays in place until focus changes again. */
    {
        tbox_context *ctx = open_cstr("<div id='list'><button>A</button><button>B</button><button>C</button><button>D</button></div>",
            "#list { width: 100px; height: 50px; overflow-y: auto; } "
            "button { width: 70px; margin: 0px; padding: 0px; border: 0px solid black; } "
            "button:focus { border: 0px solid black; }",
            fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            const tbox_html_node *first = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            for (int i = 0; i < 3; i++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
                const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
                while (root_box != NULL && root_box->parent != NULL)
                    root_box = root_box->parent;
                const tbox_layout_box *focus_box = find_layout_box(root_box, tbox_context_focused_node(ctx));
                TBOX_TEST_ASSERT(focus_box != NULL && focus_box->parent != NULL);
                if (focus_box != NULL && focus_box->parent != NULL) {
                    tbox_rect viewport = focus_box->parent->padding_box;
                    TBOX_TEST_ASSERT(focus_box->border_box.y >= viewport.y - 0.01);
                    TBOX_TEST_ASSERT(focus_box->border_box.y + focus_box->border_box.height <= viewport.y + viewport.height + 0.01);
                }
                if (i == 1)
                    TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            }
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            for (int i = 0; i < 3; i++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, true, false }));
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
                const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
                while (root_box != NULL && root_box->parent != NULL)
                    root_box = root_box->parent;
                const tbox_layout_box *focus_box = find_layout_box(root_box, tbox_context_focused_node(ctx));
                TBOX_TEST_ASSERT(focus_box != NULL && focus_box->parent != NULL);
                if (focus_box != NULL && focus_box->parent != NULL) {
                    tbox_rect viewport = focus_box->parent->padding_box;
                    TBOX_TEST_ASSERT(focus_box->border_box.y >= viewport.y - 0.01);
                    TBOX_TEST_ASSERT(focus_box->border_box.y + focus_box->border_box.height <= viewport.y + viewport.height + 0.01);
                }
            }
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == first);
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, -100.0));
            tbox_context_close(ctx);
        }
    }

    /* scroll-margin on the focused control and scroll-padding on the list
     * keep a gap when focus reveals it. */
    {
        tbox_context *ctx = open_cstr("<div id='list'><button>A</button><button>B</button><button>C</button><button>D</button></div>",
            "#list { width: 100px; height: 50px; overflow-y: auto; scroll-padding-block-end: 5px; } "
            "button { display: block; width: 70px; height: 20px; margin: 0px; padding: 0px; border: 0px solid black; scroll-margin: 0 0 10px; } "
            "button:focus { border: 0px solid black; }",
            fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            for (int i = 0; i < 3; i++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            }
            const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
            while (root_box != NULL && root_box->parent != NULL)
                root_box = root_box->parent;
            const tbox_layout_box *focus_box = find_layout_box(root_box, tbox_context_focused_node(ctx));
            TBOX_TEST_ASSERT(focus_box != NULL && focus_box->parent != NULL);
            if (focus_box != NULL && focus_box->parent != NULL) {
                tbox_rect viewport = focus_box->parent->padding_box;
                double bottom      = focus_box->border_box.y + focus_box->border_box.height;
                TBOX_TEST_ASSERT(fabs(bottom + 10.0 + 5.0 - (viewport.y + viewport.height)) < 0.01);
            }
            tbox_context_close(ctx);
        }
    }

    /* scroll-behavior: smooth -- revealing the focused control eases the
     * list over 0.3s of animation time (positions unchanged on the frame
     * that starts it, part-way in the middle, exact at the end) and keeps
     * animations_active up meanwhile; `auto` scrolls at once. */
    for (int smooth = 0; smooth < 2; smooth++) {
        char css[320];
        snprintf(css, sizeof(css),
            "#list { width: 100px; height: 50px; overflow-y: auto; scroll-behavior: %s; } "
            "button { display: block; width: 70px; height: 20px; margin: 0px; padding: 0px; border: 0px solid black; } "
            "button:focus { border: 0px solid black; }", smooth ? "smooth" : "auto");
        tbox_context *ctx = open_cstr("<div id='list'><button>A</button><button>B</button><button>C</button><button>D</button></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_set_animation_time(ctx, 1.0);
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            for (int i = 0; i < 4; i++)
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            const tbox_layout_box *list_box = NULL;
            double tops[3];
            const double times[3] = { 1.0, 1.15, 1.5 };
            for (int f = 0; f < 3; f++) {
                tbox_context_set_animation_time(ctx, times[f]);
                tbox_context_run_frame(ctx, 120.0, 100.0, &list);
                const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
                while (root_box != NULL && root_box->parent != NULL)
                    root_box = root_box->parent;
                list_box = find_layout_box(root_box, tbox_html_document_root(tbox_context_document(ctx))->first_child);
                tops[f]  = list_box != NULL && list_box->first_child != NULL ? list_box->first_child->border_box.y - list_box->content_box.y : 0.0;
                if (f == 1)
                    TBOX_TEST_ASSERT(tbox_context_animations_active(ctx) == (smooth != 0));
            }
            if (smooth) {
                TBOX_TEST_ASSERT(fabs(tops[0]) < 0.01);                        /* starts from where it was */
                TBOX_TEST_ASSERT(tops[1] < -0.5 && tops[1] > -29.5);           /* part-way */
                TBOX_TEST_ASSERT(fabs(tops[2] + 30.0) < 0.01);                 /* D's bottom at the list's */
                TBOX_TEST_ASSERT(!tbox_context_animations_active(ctx));
            } else {
                TBOX_TEST_ASSERT(fabs(tops[0] + 30.0) < 0.01 && fabs(tops[1] + 30.0) < 0.01);
            }
            tbox_context_close(ctx);
        }
    }

    /* @starting-style: an element fades in from its starting style when it
     * first appears and again when it leaves display: none; the rule never
     * applies on its own. */
    {
        tbox_context *ctx = open_cstr("<div><div id='box'>x</div></div>",
            "#box { opacity: 1; transition: opacity 1s linear; } #box.hidden { display: none; }"
            "@starting-style { #box { opacity: 0; } }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_html_node *box = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const double times[4] = { 1.0, 1.5, 2.1, 3.0 }, expected[4] = { 0.0, 0.5, 1.0, 1.0 };
            for (int round = 0; round < 2; round++) {
                if (round == 1) {
                    /* hide, then show again */
                    tbox_html_node_set_attribute(tbox_context_document(ctx), box, tbox_string_view_make("class", 5), tbox_string_view_make("hidden", 6));
                    tbox_context_set_animation_time(ctx, 5.0);
                    tbox_context_run_frame(ctx, 200.0, 100.0, &list);
                    tbox_html_node_remove_attribute(box, tbox_string_view_make("class", 5));
                }
                for (int f = 0; f < 4; f++) {
                    tbox_context_set_animation_time(ctx, times[f] + round * 10.0);
                    tbox_context_run_frame(ctx, 200.0, 100.0, &list);
                    const tbox_layout_box *layout_box = find_context_box(ctx, box);
                    TBOX_TEST_ASSERT(layout_box != NULL && fabs(layout_box->style->opacity - expected[f]) < 0.02);
                }
                TBOX_TEST_ASSERT(!tbox_context_animations_active(ctx));
            }
            tbox_context_close(ctx);
        }
    }

    /* A click or hover on a ::after box lands on its element's box, never
     * on the synthetic node (it lives only for one frame), across frames. */
    {
        tbox_context *ctx = open_cstr("<div id='host'><p id='badge'>x</p></div>",
            "#badge::after { content: \"novo\"; display: inline-block; width: 60px; height: 20px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            const tbox_html_node *p = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            const tbox_layout_box *box   = find_context_box(ctx, p);
            const tbox_layout_box *after = box != NULL ? box->first_child : NULL;
            TBOX_TEST_ASSERT(after != NULL && after->style != NULL && after->style->generated == 2);
            if (after != NULL) {
                double x = after->border_box.x + 5.0, y = after->border_box.y + 5.0;
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, x, y);
                TBOX_TEST_ASSERT(hit != NULL && hit->node == p);
                tbox_context_update_hover(ctx, true, x, y);
                tbox_context_run_frame(ctx, 300.0, 100.0, &list);
                tbox_context_run_frame(ctx, 300.0, 100.0, &list);
                hit = tbox_context_hit_test(ctx, x, y);
                TBOX_TEST_ASSERT(hit != NULL && hit->node == p);
            }
            tbox_context_close(ctx);
        }
    }

    /* overscroll-behavior: contain stops a wheel scroll at the inner
     * list's limit from chaining to the outer one; auto chains. */
    for (int contain = 0; contain < 2; contain++) {
        char css[256];
        snprintf(css, sizeof(css),
            "#outer { width: 100px; height: 60px; overflow-y: auto; } "
            "#inner { height: 40px; overflow-y: auto; overscroll-behavior: %s; } "
            ".tall { height: 100px; }", contain ? "contain" : "auto");
        tbox_context *ctx = open_cstr("<div id='outer'><div id='inner'><div class='tall'></div></div><div class='tall'></div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, 200.0)); /* inner to its end */
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, 20.0) == !contain);
            tbox_context_close(ctx);
        }
    }

    /* A single select displays its chosen label while exposing the option
     * value. Keyboard and mouse choices skip disabled options. */
    {
        tbox_context *ctx = open_cstr("<div><select id='mode'><option value='a'>Alpha</option>"
                                      "<option value='b' label='Bee' selected>Beta</option>"
                                      "<option value='x' disabled>Unavailable</option>"
                                      "<option>Gamma</option></select><button>Next</button></div>",
            "select { width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            const tbox_html_node *select    = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *button    = select->next_sibling;
            const tbox_layout_box *root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
            while (root_box != NULL && root_box->parent != NULL)
                root_box = root_box->parent;
            const tbox_layout_box *select_box = find_layout_box(root_box, select);
            TBOX_TEST_ASSERT(select_box != NULL && select_box->text_run_count == 1);
            if (select_box != NULL && select_box->text_run_count == 1)
                TBOX_TEST_ASSERT(string_view_equal_cstr(select_box->text_runs[0].text, "Bee"));
            TBOX_TEST_ASSERT(find_layout_box(root_box, select->first_child) == NULL);
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "b"));
            select_capture capture = { 0 };
            tbox_context_set_select_handler(ctx, record_select, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == select);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "Gamma"));
            TBOX_TEST_ASSERT(capture.count == 1 && strcmp(capture.value, "Gamma") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(capture.count == 2);
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ESCAPE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 2);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 3 && strcmp(capture.value, "Gamma") == 0);
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "Gamma"));
            tbox_context_run_frame(ctx, 160.0, 220.0, &list);
            root_box = tbox_context_hit_test(ctx, 10.0, 10.0);
            while (root_box != NULL && root_box->parent != NULL)
                root_box = root_box->parent;
            select_box = find_layout_box(root_box, select);
            TBOX_TEST_ASSERT(select_box != NULL);
            if (select_box != NULL) {
                tbox_context_dispatch_click(ctx, select_box->border_box.x + 5.0, select_box->border_box.y + select_box->border_box.height / 2.0);
                tbox_context_run_frame(ctx, 160.0, 220.0, &list);
                tbox_rect popup = { 0 };
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 105 && op->color.g == 112 && op->color.b == 122)
                        popup = op->rect;
                }
                TBOX_TEST_ASSERT(popup.height > 0.0);
                if (popup.height > 0.0) {
                    double row_height = popup.height / 4.0;
                    tbox_context_dispatch_click(ctx, popup.x + 10.0, popup.y + row_height * 2.5);
                    TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "Gamma"));
                    TBOX_TEST_ASSERT(capture.count == 3);
                    tbox_context_dispatch_click(ctx, popup.x + 10.0, popup.y + row_height * 0.5);
                    TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "a"));
                    TBOX_TEST_ASSERT(capture.count == 4 && strcmp(capture.value, "a") == 0);
                }
            }
            TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, (tbox_html_node *)select, tbox_string_view_make("b", 1)));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "b"));
            TBOX_TEST_ASSERT(capture.count == 4);
            TBOX_TEST_ASSERT(!tbox_context_control_set_value(ctx, (tbox_html_node *)select, tbox_string_view_make("missing", 7)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == button);
            tbox_context_close(ctx);
        }
    }

    /* Longer menus show a bounded number of rows. The wheel changes the
     * visible slice, and End reveals the last option before confirmation. */
    {
        tbox_context *ctx = open_cstr("<select><option value='1'>One</option><option value='2'>Two</option>"
                                      "<option value='3'>Three</option><option value='4'>Four</option>"
                                      "<option value='5'>Five</option><option value='6'>Six</option>"
                                      "<option value='7'>Seven</option><option value='8'>Eight</option></select>",
            "select { width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 140.0, 240.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 140.0, 240.0, &list);
            tbox_rect popup = { 0 };
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 105 && op->color.g == 112 && op->color.b == 122)
                    popup = op->rect;
            }
            TBOX_TEST_ASSERT(popup.height > 0.0 && popup.height < 200.0);
            if (popup.height > 0.0) {
                TBOX_TEST_ASSERT(tbox_context_scroll(ctx, popup.x + 10.0, popup.y + 10.0, 30.0));
                tbox_context_run_frame(ctx, 140.0, 240.0, &list);
                bool saw_two_first = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind == TBOX_PAINT_TEXT_RUN && op->rect.y >= popup.y && op->rect.y < popup.y + 40.0 && string_view_equal_cstr(op->text, "Two"))
                        saw_two_first = true;
                }
                TBOX_TEST_ASSERT(saw_two_first);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "1"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "8"));
            tbox_context_close(ctx);
        }
    }

    /* optgroup labels occupy popup rows; nested options remain selectable,
     * while a disabled group blocks both API selection and keyboard travel. */
    {
        const char *html = "<select><option value='a'>Avulso</option>"
            "<optgroup label='Bloqueado' disabled><option value='b'>B</option></optgroup>"
            "<optgroup label='Grupo'><option value='c'>C</option><option value='d'>D</option></optgroup></select>";
        tbox_context *ctx = open_cstr(html, "select { width: 140px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 250.0, 300.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "a"));
            TBOX_TEST_ASSERT(!tbox_context_control_set_value(ctx, (tbox_html_node *)select, tbox_string_view_make("b", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "c"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 250.0, 300.0, &list);
            bool group_label = false, disabled_label = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_TEXT_RUN) continue;
                if (string_view_equal_cstr(op->text, "Grupo")) group_label = true;
                if (string_view_equal_cstr(op->text, "Bloqueado")) disabled_label = true;
            }
            TBOX_TEST_ASSERT(group_label && disabled_label);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "d"));
            tbox_context_close(ctx);
        }
    }

    /* An input button paints its value, shares button activation, and cannot
     * be edited. A disabled button neither takes focus nor dispatches clicks. */
    {
        tbox_context *ctx = open_cstr("<div><input id='action' type='BUTTON' value='Run'>"
                                      "<input id='empty' type='button'>"
                                      "<input id='blank' type='button' value=''>"
                                      "<input id='disabled' type='button' value='Off' disabled></div>",
            "input { width: 60px; } input:focus { background-color: rgb(255, 0, 0); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *action   = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *empty    = action->next_sibling;
            const tbox_html_node *blank    = empty->next_sibling;
            const tbox_html_node *disabled = blank->next_sibling;
            bool painted_run = false, painted_off = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_TEXT_RUN)
                    continue;
                if (string_view_equal_cstr(op->text, "Run"))
                    painted_run = op->has_clip && op->clip.width == 60.0;
                if (string_view_equal_cstr(op->text, "Off"))
                    painted_off = true;
                TBOX_TEST_ASSERT(!string_view_equal_cstr(op->text, "Button"));
            }
            TBOX_TEST_ASSERT(painted_run && painted_off);

            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "input", 5, record_click, &capture, NULL) >= 0);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool focus_painted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 255 && list.items[i].color.g == 0 && list.items[i].color.b == 0)
                    focus_painted = true;
            }
            TBOX_TEST_ASSERT(focus_painted);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(changes == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.call_count == 2 && capture.node == action);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == empty);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == blank);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);

            double hx, hy;
            bool clicked_action = hit_point_for(ctx, action, 320.0, 180.0, &hx, &hy);
            if (clicked_action)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, hx, hy));
            bool clicked_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &hx, &hy);
            if (clicked_disabled)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, hx, hy));
            TBOX_TEST_ASSERT(clicked_action && clicked_disabled);
            TBOX_TEST_ASSERT(capture.call_count == 3 && capture.node == action);
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);

            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)action, tbox_string_view_make("value", 5), tbox_string_view_make("Go", 2));
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool painted_go = false, painted_old = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind != TBOX_PAINT_TEXT_RUN)
                    continue;
                if (string_view_equal_cstr(list.items[i].text, "Go"))
                    painted_go = true;
                if (string_view_equal_cstr(list.items[i].text, "Run"))
                    painted_old = true;
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
            const char *html  = "<div><input id='image-action' type='IMAGE' src='yellow.png' alt='Send' width='32' height='20'>"
                                "<input id='broken' type='image' src='missing.png' alt='Fallback'>"
                                "<input id='disabled' type='image' src='yellow.png' width='24' height='16' disabled>"
                                "<input id='scaled' type='image' src='yellow.png' width='30'></div>";
            const char *css   = "#scaled { width: 40px; }";
            tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, images);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                const tbox_html_node *action   = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
                const tbox_html_node *broken   = action->next_sibling;
                const tbox_html_node *disabled = broken->next_sibling;
                const tbox_html_node *scaled   = disabled->next_sibling;
                const tbox_layout_box *box     = find_context_box(ctx, action);
                TBOX_TEST_ASSERT(box != NULL);
                if (box != NULL) {
                    TBOX_TEST_ASSERT(box->content_box.width == 32.0 && box->content_box.height == 20.0);
                    bool painted_image = false;
                    for (size_t i = 0; i < list.count; i++)
                        if (list.items[i].kind == TBOX_PAINT_IMAGE && list.items[i].rect.x == box->content_box.x && list.items[i].rect.y == box->content_box.y && list.items[i].rect.width == 32.0 && list.items[i].rect.height == 20.0)
                            painted_image = true;
                    TBOX_TEST_ASSERT(painted_image);
                }
                bool painted_alt = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Fallback"))
                        painted_alt = true;
                TBOX_TEST_ASSERT(painted_alt);
                const tbox_layout_box *scaled_box = find_context_box(ctx, scaled);
                TBOX_TEST_ASSERT(scaled_box != NULL);
                if (scaled_box != NULL)
                    TBOX_TEST_ASSERT(scaled_box->content_box.width == 40.0 && scaled_box->content_box.height == 40.0);

                click_capture capture;
                click_capture_reset(&capture);
                TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "#image-action", 13, record_click, &capture, NULL) >= 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
                TBOX_TEST_ASSERT(capture.call_count == 2 && capture.node == action);
                TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
                if (box != NULL)
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
                TBOX_TEST_ASSERT(capture.call_count == 3);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == broken);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == scaled);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == action);
                const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
                TBOX_TEST_ASSERT(disabled_box != NULL);
                if (disabled_box != NULL)
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 2.0, disabled_box->border_box.y + 2.0));
                TBOX_TEST_ASSERT(capture.call_count == 3);
                tbox_context_close(ctx);
            }
            tbox_image_cache_destroy(images);
        }
    }

    /* Checkbox state is the presence of the checked attribute. User toggles
     * update it before click handlers run, and repaint the indicator. */
    {
        tbox_context *ctx = open_cstr("<div><input id='off' type='CHECKBOX'><input id='on' type='checkbox' checked>"
                                      "<input id='disabled' type='checkbox' checked disabled><button>Next</button></div>",
            "input:checked { background-color: yellow; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *off      = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *on       = off->next_sibling;
            const tbox_html_node *disabled = on->next_sibling;
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(on, tbox_string_view_make("checked", 7)) != NULL);

            tbox_rect off_content = { 0 };
            double hx, hy;
            bool found_off = hit_point_for(ctx, off, 320.0, 180.0, &hx, &hy);
            if (found_off)
                off_content = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &hx, &hy);
            TBOX_TEST_ASSERT(found_off && found_disabled);
            TBOX_TEST_ASSERT(off_content.width == 12.0 && off_content.height == 12.0);

            checkbox_capture capture = { 0 };
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "input[type=checkbox]", 20, record_checkbox_click, &capture, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == off);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == off && capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            bool painted_check = false, painted_checked_background = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_FILL_RECT)
                    continue;
                if (op->rect.x >= off_content.x && op->rect.x < off_content.x + off_content.width && op->rect.y >= off_content.y && op->rect.y < off_content.y + off_content.height && op->rect.width == 1.5 && op->rect.height == 1.5)
                    painted_check = true;
                if (op->color.r == 255 && op->color.g == 255 && op->color.b == 0 && op->rect.x <= off_content.x && op->rect.y <= off_content.y && op->rect.x + op->rect.width >= off_content.x + off_content.width && op->rect.y + op->rect.height >= off_content.y + off_content.height)
                    painted_checked_background = true;
            }
            TBOX_TEST_ASSERT(painted_check && painted_checked_background);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 2 && !capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(off, tbox_string_view_make("checked", 7)) == NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            painted_check = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->rect.x >= off_content.x && op->rect.x < off_content.x + off_content.width && op->rect.y >= off_content.y && op->rect.y < off_content.y + off_content.height && op->rect.width == 1.5 && op->rect.height == 1.5)
                    painted_check = true;
            }
            TBOX_TEST_ASSERT(!painted_check);

            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == on);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 3 && capture.node == on && !capture.checked);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == disabled->next_sibling);

            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            double cx, cy;
            bool clicked_off = hit_point_for(ctx, off, 320.0, 180.0, &cx, &cy);
            if (clicked_off)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, cx, cy));
            bool clicked_disabled = hit_point_for(ctx, disabled, 320.0, 180.0, &cx, &cy);
            if (clicked_disabled)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, cx, cy));
            TBOX_TEST_ASSERT(clicked_off && clicked_disabled);
            TBOX_TEST_ASSERT(capture.count == 4 && capture.node == off && capture.checked);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(disabled, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_close(ctx);
        }
    }

    /* Radio selection is exclusive within each named group. The selected
     * control paints a centered dot and matches :checked. */
    {
        tbox_context *ctx = open_cstr("<div><input id='a' type='RADIO' name='choice'>"
                                      "<input id='b' type='radio' name='choice' checked>"
                                      "<input id='c' type='radio' name='other' checked>"
                                      "<input id='off' type='radio' name='choice' disabled></div>",
            "input:checked { background-color: yellow; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_html_node *a        = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *b        = a->next_sibling;
            const tbox_html_node *c        = b->next_sibling;
            const tbox_html_node *disabled = c->next_sibling;
            const tbox_layout_box *a_box   = find_context_box(ctx, a);
            const tbox_layout_box *b_box   = find_context_box(ctx, b);
            TBOX_TEST_ASSERT(a_box != NULL && b_box != NULL);
            if (a_box != NULL)
                TBOX_TEST_ASSERT(a_box->content_box.width == 12.0 && a_box->content_box.height == 12.0);
            bool painted_dot = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].radius == 3.0)
                    painted_dot = true;
            TBOX_TEST_ASSERT(painted_dot);
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "input[type=radio]", 17, record_click, &capture, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == a);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.call_count == 1 && capture.node == a);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(c, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == b);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(a, tbox_string_view_make("checked", 7)) == NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_run_frame(ctx, 160.0, 180.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(b, tbox_string_view_make("checked", 7)) != NULL);
            tbox_context_close(ctx);
        }
    }

    /* Range inputs normalize to a step, paint a track and thumb, and update
     * by keyboard, click, and pointer drag. */
    {
        tbox_context *ctx = open_cstr("<div><input id='level' type='RANGE' min='10' max='30' step='5' value='17'>"
                                      "<input id='default-range' type='range'>"
                                      "<input type='range' disabled value='40'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            const tbox_html_node *level         = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *default_range = level->next_sibling;
            const tbox_html_node *disabled      = default_range->next_sibling;
            const tbox_html_attribute *value    = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "15"));
            value = tbox_html_node_get_attribute(default_range, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "50"));
            const tbox_layout_box *box = find_context_box(ctx, level);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(box->content_box.width == 160.0 && box->content_box.height == 20.0);
            bool painted_thumb = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].radius == 7.0)
                    painted_thumb = true;
            TBOX_TEST_ASSERT(painted_thumb);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == level);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            value = tbox_html_node_get_attribute(level, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "20"));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
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
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->content_box.x + 8.0, disabled_box->content_box.y + 8.0));
            TBOX_TEST_ASSERT(changes == 4);
            tbox_context_close(ctx);
        }
    }

    /* A reset input restores the original state of controls in its form,
     * including retained select state, without changing another form. */
    {
        tbox_context *ctx = open_cstr("<div><form><input id='name' value='start'><input id='check' type='checkbox' checked>"
                                      "<input id='radio-a' type='radio' name='group' checked>"
                                      "<input id='radio-b' type='radio' name='group'>"
                                      "<input id='slider' type='range' value='20'>"
                                      "<textarea>Initial</textarea><select><option value='one' selected>One</option>"
                                      "<option value='two'>Two</option></select><input id='reset' type='RESET'></form>"
                                      "<form><input id='other' value='keep'></form></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 500.0, &list);
            const tbox_html_node *form     = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *name     = form->first_child;
            const tbox_html_node *check    = name->next_sibling;
            const tbox_html_node *radio_a  = check->next_sibling;
            const tbox_html_node *radio_b  = radio_a->next_sibling;
            const tbox_html_node *slider   = radio_b->next_sibling;
            const tbox_html_node *textarea = slider->next_sibling;
            const tbox_html_node *select   = textarea->next_sibling;
            const tbox_html_node *reset    = select->next_sibling;
            const tbox_html_node *other    = form->next_sibling->first_child;
            bool label_painted             = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Reset"))
                    label_painted = true;
            TBOX_TEST_ASSERT(label_painted);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)name, tbox_string_view_make("value", 5), tbox_string_view_make("changed", 7));
            tbox_html_node_remove_attribute((tbox_html_node *)check, tbox_string_view_make("checked", 7));
            tbox_html_node_remove_attribute((tbox_html_node *)radio_a, tbox_string_view_make("checked", 7));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)radio_b, tbox_string_view_make("checked", 7), tbox_string_view_make(NULL, 0));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)slider, tbox_string_view_make("value", 5), tbox_string_view_make("80", 2));
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)textarea, tbox_string_view_make("Changed", 7));
            TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, (tbox_html_node *)select, tbox_string_view_make("two", 3)));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)other, tbox_string_view_make("value", 5), tbox_string_view_make("outside", 7));
            tbox_context_run_frame(ctx, 320.0, 500.0, &list);
            const tbox_layout_box *reset_box = find_context_box(ctx, reset);
            TBOX_TEST_ASSERT(reset_box != NULL);
            if (reset_box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, reset_box->border_box.x + 5.0, reset_box->border_box.y + 5.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(name, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "start"));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(check, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(radio_a, tbox_string_view_make("checked", 7)) != NULL);
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(radio_b, tbox_string_view_make("checked", 7)) == NULL);
            value = tbox_html_node_get_attribute(slider, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "20"));
            TBOX_TEST_ASSERT(textarea->first_child != NULL && string_view_equal_cstr(textarea->first_child->text.text, "Initial"));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "one"));
            value = tbox_html_node_get_attribute(other, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "outside"));
            tbox_context_close(ctx);
        }
    }

    /* Submit inputs expose a form callback by click or keyboard; Enter in a
     * single-line field submits the form without an explicit submitter. */
    {
        tbox_context *ctx = open_cstr("<div><form><input id='query' type='search' value='abc'>"
                                      "<input id='send' type='SUBMIT'><input id='off' type='submit' disabled></form>"
                                      "<input id='outside' type='submit'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            const tbox_html_node *form     = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *query    = form->first_child;
            const tbox_html_node *send     = query->next_sibling;
            const tbox_html_node *disabled = send->next_sibling;
            bool painted_submit            = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Submit"))
                    painted_submit = true;
            TBOX_TEST_ASSERT(painted_submit);
            submit_capture capture = { 0 };
            tbox_context_set_submit_handler(ctx, record_submit, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == query);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.form == form && capture.submitter == NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == send);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 2 && capture.submitter == send);
            const tbox_layout_box *send_box = find_context_box(ctx, send);
            TBOX_TEST_ASSERT(send_box != NULL);
            if (send_box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, send_box->border_box.x + 5.0, send_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(capture.count == 3 && capture.submitter == send);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(capture.count == 3);
            tbox_context_close(ctx);
        }
    }

    /* A <button> submits by default (also with an unknown type, and when the
     * click lands on its content), resets with type=reset and only clicks
     * with type=button. */
    {
        tbox_context *ctx = open_cstr("<div><form><input id='name' value='initial'>"
                                      "<button id='go'><b>Go</b></button><button id='odd' type='bogus'>Odd</button>"
                                      "<button id='plain' type='BUTTON'>Plain</button><button id='undo' type='reset'>Undo</button>"
                                      "<button id='off' disabled>Off</button></form><button id='outside'>Out</button></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 480.0, 200.0, &list);
            const tbox_html_node *form    = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_html_node *name          = form->first_child;
            const tbox_html_node *go      = name->next_sibling;
            const tbox_html_node *odd     = go->next_sibling;
            const tbox_html_node *plain   = odd->next_sibling;
            const tbox_html_node *undo    = plain->next_sibling;
            const tbox_html_node *off     = undo->next_sibling;
            const tbox_html_node *outside = form->next_sibling;
            submit_capture capture = { 0 };
            tbox_context_set_submit_handler(ctx, record_submit, &capture, NULL);
            const tbox_layout_box *box = find_context_box(ctx, go);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + box->border_box.width / 2.0,
                    box->border_box.y + box->border_box.height / 2.0));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.form == form && capture.submitter == go);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == odd);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 2 && capture.submitter == odd);
            box = find_context_box(ctx, plain);
            if (box != NULL)
                tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0);
            box = find_context_box(ctx, off);
            if (box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            box = find_context_box(ctx, outside);
            if (box != NULL)
                tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0);
            TBOX_TEST_ASSERT(capture.count == 2);
            tbox_html_node_set_attribute(tbox_context_document(ctx), name, tbox_string_view_make("value", 5),
                tbox_string_view_make("changed", 7));
            tbox_context_run_frame(ctx, 480.0, 200.0, &list);
            box = find_context_box(ctx, undo);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(name, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "initial"));
            TBOX_TEST_ASSERT(capture.count == 2);
            tbox_context_close(ctx);
        }
    }

    /* Disabled controls look disabled (UA sheet): gray text, faint border,
     * half-alpha white background; toggling `disabled` on an ancestor
     * fieldset restyles the control on the next frame. */
    {
        tbox_context *ctx = open_cstr("<div><fieldset><input id='n' type='number' value='4'></fieldset></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 120.0, &list);
            tbox_html_node *fieldset    = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *input = fieldset->first_child;
            const tbox_layout_box *box  = find_context_box(ctx, input);
            TBOX_TEST_ASSERT(box != NULL && box->style->color.r == 0 && box->style->background_color.a == 255 &&
                             box->style->border_color.r == 128);
            tbox_html_node_set_attribute(tbox_context_document(ctx), fieldset, tbox_string_view_make("disabled", 8),
                tbox_string_view_make(NULL, 0));
            tbox_context_run_frame(ctx, 320.0, 120.0, &list);
            box = find_context_box(ctx, input);
            TBOX_TEST_ASSERT(box != NULL && box->style->color.r == 109 && box->style->background_color.a == 128 &&
                             box->style->border_color.r == 220);
            tbox_html_node_remove_attribute(fieldset, tbox_string_view_make("disabled", 8));
            tbox_context_run_frame(ctx, 320.0, 120.0, &list);
            box = find_context_box(ctx, input);
            TBOX_TEST_ASSERT(box != NULL && box->style->color.r == 0 && box->style->background_color.a == 255);
            tbox_context_close(ctx);
        }
    }

    /* Controls inside a disabled fieldset are disabled: not focusable, a
     * checkbox does not toggle and a submit button does not submit. */
    {
        tbox_context *ctx = open_cstr("<div><form><fieldset disabled><legend><input id='free'></legend>"
                                      "<input id='box' type='checkbox'><button id='go'>Go</button></fieldset>"
                                      "<input id='after'></form></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 480.0, 200.0, &list);
            const tbox_html_node *fieldset = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child->first_child;
            const tbox_html_node *free     = fieldset->first_child->first_child;
            const tbox_html_node *box_node = fieldset->first_child->next_sibling;
            const tbox_html_node *go       = box_node->next_sibling;
            const tbox_html_node *after    = fieldset->next_sibling;
            submit_capture capture = { 0 };
            tbox_context_set_submit_handler(ctx, record_submit, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_control_disabled(box_node) && tbox_context_control_disabled(go) &&
                             !tbox_context_control_disabled(free));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == free);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == after);
            const tbox_layout_box *box = find_context_box(ctx, box_node);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            TBOX_TEST_ASSERT(!tbox_html_node_has_attribute(box_node, tbox_string_view_make("checked", 7)));
            box = find_context_box(ctx, go);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            TBOX_TEST_ASSERT(capture.count == 0 && tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* The form attribute moves a control to the <form> with that id; an id
     * that is missing or not a form leaves it without a form. */
    {
        tbox_context *ctx = open_cstr("<div><form id='f'><input id='inner' value='a'><input id='lost' form='nope'>"
                                      "<input type='radio' name='r' checked></form>"
                                      "<form id='g'><input id='moved' form='f' value='b'><button id='go' form='f'>Go</button>"
                                      "<button id='undo' type='reset' form='f'>Undo</button>"
                                      "<input type='radio' name='r' form='f'><input type='radio' name='r' checked></form></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 640.0, 200.0, &list);
            const tbox_html_node *f   = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_html_node *inner     = f->first_child;
            const tbox_html_node *lost = inner->next_sibling;
            const tbox_html_node *radio_f = lost->next_sibling;
            const tbox_html_node *g   = f->next_sibling;
            tbox_html_node *moved     = g->first_child;
            const tbox_html_node *go  = moved->next_sibling;
            const tbox_html_node *undo = go->next_sibling;
            const tbox_html_node *radio_moved = undo->next_sibling;
            const tbox_html_node *radio_g = radio_moved->next_sibling;
            submit_capture capture = { 0 };
            tbox_context_set_submit_handler(ctx, record_submit, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, moved));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.form == f && capture.submitter == NULL);
            TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, (tbox_html_node *)lost));
            tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false });
            TBOX_TEST_ASSERT(capture.count == 1);
            const tbox_layout_box *box = find_context_box(ctx, go);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            TBOX_TEST_ASSERT(capture.count == 2 && capture.form == f && capture.submitter == go);
            /* Checking the moved radio unchecks its peer in f, not the one in g. */
            box = find_context_box(ctx, radio_moved);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            TBOX_TEST_ASSERT(!tbox_html_node_has_attribute(radio_f, tbox_string_view_make("checked", 7)) &&
                             tbox_html_node_has_attribute(radio_g, tbox_string_view_make("checked", 7)));
            tbox_html_node_set_attribute(tbox_context_document(ctx), inner, tbox_string_view_make("value", 5),
                tbox_string_view_make("x", 1));
            tbox_html_node_set_attribute(tbox_context_document(ctx), moved, tbox_string_view_make("value", 5),
                tbox_string_view_make("y", 1));
            tbox_context_run_frame(ctx, 640.0, 200.0, &list);
            box = find_context_box(ctx, undo);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 2.0, box->border_box.y + 2.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(inner, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "a"));
            value = tbox_html_node_get_attribute(moved, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "b"));
            TBOX_TEST_ASSERT(tbox_html_node_has_attribute(radio_f, tbox_string_view_make("checked", 7)) &&
                             !tbox_html_node_has_attribute(radio_moved, tbox_string_view_make("checked", 7)));
            tbox_context_close(ctx);
        }
    }

    /* Search inputs edit like text fields and expose a pointer/Escape clear. */
    {
        tbox_context *ctx = open_cstr("<div><input id='query' type='SEARCH' value='find me'>"
                                      "<input type='search' value='off' disabled></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 150.0, &list);
            const tbox_html_node *query = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *box  = find_context_box(ctx, query);
            TBOX_TEST_ASSERT(box != NULL);
            bool painted_clear = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 245 && list.items[i].rect.width == 16.0)
                    painted_clear = true;
            TBOX_TEST_ASSERT(painted_clear);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == query);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("new query", 9)));
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->content_box.x + box->content_box.width - 8.0, box->content_box.y + box->content_box.height / 2.0));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(query, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && value->value.size == 0 && changes == 2);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("again", 5)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ESCAPE, true, false, false }));
            value = tbox_html_node_get_attribute(query, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && value->value.size == 0 && changes == 4);
            tbox_context_close(ctx);
        }
    }

    /* Tel uses ordinary text editing; URL validation checks absolute URLs. */
    {
        tbox_context *ctx = open_cstr("<div><input type='tel' value='+55 85 1234'><input type='url' value='https://example.org/a' required>"
                                      "<input type='url' value='relative/path'><input type='url' value='mailto:a@example.org'>"
                                      "<input type='url' required></div>",
            "", fonts);
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
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
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
        tbox_context *ctx = open_cstr("<div><input type='time' value='09:30' min='09:00' max='11:00'>"
                                      "<input type='time' value='25:99'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            const tbox_html_node *input        = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *invalid = tbox_html_node_get_attribute(input->next_sibling, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid != NULL && invalid->value.size == 0);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == input);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "10:31") && changes == 1);
            tbox_context_close(ctx);
        }
    }

    /* ISO week stepping crosses calendar years without skipping week 53. */
    {
        tbox_context *ctx = open_cstr("<div><input type='week' value='2020-W53' min='2020-W52' max='2021-W02'>"
                                      "<input type='week' value='2021-W53'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 250.0, &list);
            const tbox_html_node *input        = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *invalid = tbox_html_node_get_attribute(input->next_sibling, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid != NULL && invalid->value.size == 0);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == input);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2021-W01") && changes == 1);
            tbox_context_close(ctx);
        }
    }

    /* A color input paints its #RRGGBB value and edits all three channels
     * through its popup. Disabled controls do not open or take focus. */
    {
        tbox_context *ctx = open_cstr("<div><input id='chosen' type='COLOR' value='#123456'>"
                                      "<input id='invalid' type='color' value='wrong'>"
                                      "<input id='disabled' type='color' disabled></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            const tbox_html_node *chosen   = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *invalid  = chosen->next_sibling;
            const tbox_html_node *disabled = invalid->next_sibling;
            tbox_rect chosen_rect = { 0 }, invalid_rect = { 0 };
            double hx, hy;
            bool found_chosen = hit_point_for(ctx, chosen, 240.0, 220.0, &hx, &hy);
            if (found_chosen)
                chosen_rect = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_invalid = hit_point_for(ctx, invalid, 240.0, 220.0, &hx, &hy);
            if (found_invalid)
                invalid_rect = tbox_context_hit_test(ctx, hx, hy)->content_box;
            bool found_disabled = hit_point_for(ctx, disabled, 240.0, 220.0, &hx, &hy);
            TBOX_TEST_ASSERT(found_chosen && found_invalid && found_disabled);
            TBOX_TEST_ASSERT(chosen_rect.width == 48.0 && chosen_rect.height == 24.0);
            bool painted_chosen = false, painted_invalid = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_FILL_RECT)
                    continue;
                if (op->rect.x == chosen_rect.x && op->rect.y == chosen_rect.y && op->rect.width == chosen_rect.width && op->rect.height == chosen_rect.height)
                    painted_chosen = op->color.r == 0x12 && op->color.g == 0x34 && op->color.b == 0x56;
                if (op->rect.x == invalid_rect.x && op->rect.y == invalid_rect.y && op->rect.width == invalid_rect.width && op->rect.height == invalid_rect.height)
                    painted_invalid = op->color.r == 0 && op->color.g == 0 && op->color.b == 0;
            }
            TBOX_TEST_ASSERT(painted_chosen && painted_invalid);

            color_capture capture = { 0 };
            tbox_context_set_input_handler(ctx, record_color_input, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == chosen);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == chosen && strcmp(capture.value, "#133456") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, true, false }));
            TBOX_TEST_ASSERT(strcmp(capture.value, "#1d3456") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
            TBOX_TEST_ASSERT(strcmp(capture.value, "#1d0056") == 0);
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            const tbox_layout_box *chosen_box = NULL;
            for (int y = 0; y < 220 && chosen_box == NULL; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node == chosen)
                    chosen_box = hit;
            }
            TBOX_TEST_ASSERT(chosen_box != NULL);
            if (chosen_box != NULL) {
                double popup_x = chosen_box->border_box.x;
                double popup_y = chosen_box->border_box.y + chosen_box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_color_drag(ctx, popup_x + 210.0, popup_y + 73.0));
                TBOX_TEST_ASSERT(strcmp(capture.value, "#1d00ff") == 0);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ESCAPE, true, false, false }));
            TBOX_TEST_ASSERT(!tbox_context_color_drag(ctx, 100.0, 100.0));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == invalid);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == chosen);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)chosen, tbox_string_view_make("value", 5), tbox_string_view_make("#abcdef", 7));
            tbox_context_run_frame(ctx, 240.0, 220.0, &list);
            bool painted_programmatic = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 0xab && op->color.g == 0xcd && op->color.b == 0xef)
                    painted_programmatic = true;
            }
            TBOX_TEST_ASSERT(painted_programmatic);
            chosen_box = NULL;
            for (int y = 0; y < 220 && chosen_box == NULL; y++) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 12.0, (double)y);
                if (hit != NULL && hit->node == chosen)
                    chosen_box = hit;
            }
            TBOX_TEST_ASSERT(chosen_box != NULL);
            if (chosen_box != NULL) {
                double x             = chosen_box->border_box.x + 5.0;
                double y             = chosen_box->border_box.y + 5.0;
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
                    if (hit != NULL && hit->node == chosen)
                        chosen_box = hit;
                }
                TBOX_TEST_ASSERT(chosen_box != NULL);
                if (chosen_box != NULL) {
                    double popup_x = chosen_box->border_box.x;
                    double popup_y = chosen_box->border_box.y + chosen_box->border_box.height;
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 210.0, popup_y + 20.0));
                    TBOX_TEST_ASSERT(capture.count == previous_changes + 1);
                    TBOX_TEST_ASSERT(strcmp(capture.value, "#ffcdef") == 0);
                }
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ESCAPE, true, false, false }));
            }
            double dx, dy;
            bool clicked_disabled = hit_point_for(ctx, disabled, 240.0, 220.0, &dx, &dy);
            if (clicked_disabled)
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, dx, dy));
            TBOX_TEST_ASSERT(clicked_disabled && tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Date inputs expose a calendar and commit only valid ISO dates within
     * min/max. Navigation crosses leap days and mouse cells use the same value. */
    {
        tbox_context *ctx = open_cstr("<div><input id='date' type='DATE' value='2024-02-28' min='2024-02-29' max='2024-03-02'>"
                                      "<input type='date' disabled><button>Next</button>"
                                      "<input type='date' value='2023-02-29'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_html_node *date               = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled           = date->next_sibling;
            const tbox_html_node *invalid            = disabled->next_sibling->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, date);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(box->content_box.width == 120.0);
            bool painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "2024-02-28"))
                    painted = true;
            TBOX_TEST_ASSERT(painted);
            date_capture capture = { 0 };
            tbox_context_set_input_handler(ctx, record_date_input, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == date);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("2025", 4)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == date && strcmp(capture.value, "2024-03-01") == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == disabled->next_sibling);
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            box = find_context_box(ctx, date);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                double x       = box->border_box.x + 5.0;
                double y       = box->border_box.y + 5.0;
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
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Month inputs keep YYYY-MM values and choose from a twelve-month grid. */
    {
        tbox_context *ctx = open_cstr("<div><input id='month' type='MONTH' value='2024-02' min='2024-03' max='2024-05'>"
                                      "<input type='month' disabled><input type='month' value='2024-13'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            const tbox_html_node *month              = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled           = month->next_sibling;
            const tbox_html_node *invalid            = disabled->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, month);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(box->content_box.width == 100.0);
            bool painted_value = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "2024-02"))
                    painted_value = true;
            TBOX_TEST_ASSERT(painted_value);
            month_capture capture = { 0 };
            tbox_context_set_input_handler(ctx, record_month_input, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == month);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("2025", 4)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            bool painted_march = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Mar"))
                    painted_march = true;
            TBOX_TEST_ASSERT(painted_march);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == month && strcmp(capture.value, "2024-03") == 0);
            tbox_context_run_frame(ctx, 320.0, 340.0, &list);
            box = find_context_box(ctx, month);
            if (box != NULL) {
                double popup_x = box->border_box.x;
                double popup_y = box->border_box.y + box->border_box.height;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0, box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 76.0, popup_y + 50.0));
                TBOX_TEST_ASSERT(capture.count == 1);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 88.0));
                TBOX_TEST_ASSERT(capture.count == 2 && strcmp(capture.value, "2024-05") == 0);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0, box->border_box.y + 5.0));
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
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            tbox_context_close(ctx);
        }
    }

    /* Datetime-local combines calendar selection and hour/minute changes.
     * Boundary times are checked before confirmation. */
    {
        tbox_context *ctx = open_cstr("<div><input id='when' type='DATETIME-LOCAL' value='2024-02-29T12:30' "
                                      "min='2024-02-29T12:00' max='2024-03-01T18:00'>"
                                      "<input type='datetime-local' value='2024-02-30T10:00'>"
                                      "<input type='datetime-local' disabled></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 360.0, 440.0, &list);
            const tbox_html_node *when               = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *invalid            = when->next_sibling;
            const tbox_html_node *disabled           = invalid->next_sibling;
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            const tbox_layout_box *box = find_context_box(ctx, when);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(box->content_box.width == 175.0);
            bool painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "2024-02-29T12:30"))
                    painted = true;
            TBOX_TEST_ASSERT(painted);

            datetime_capture capture = { 0 };
            tbox_context_set_input_handler(ctx, record_datetime_input, &capture, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == when);
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_SPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(capture.count == 1 && capture.node == when && strcmp(capture.value, "2024-03-01T13:31") == 0);

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
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Email inputs edit like text controls and expose current validity,
     * including required and comma-separated multiple addresses. */
    {
        tbox_context *ctx = open_cstr("<div><input id='mail' type='EMAIL' value='a@example.com' required>"
                                      "<input id='multi' type='email' multiple value='a@b, c.d+tag@example.org'>"
                                      "<input id='empty' type='email'><input id='bad' type='email' value='x@-bad'>"
                                      "<input id='disabled' type='email' value='off@example.com' disabled></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            const tbox_html_node *mail     = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *multi    = mail->next_sibling;
            const tbox_html_node *empty    = multi->next_sibling;
            const tbox_html_node *bad      = empty->next_sibling;
            const tbox_html_node *disabled = bad->next_sibling;
            TBOX_TEST_ASSERT(tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(tbox_context_email_valid(multi));
            TBOX_TEST_ASSERT(tbox_context_email_valid(empty));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(bad));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(NULL));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(mail->parent));
            bool painted_mail = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "a@example.com"))
                    painted_mail = true;
            TBOX_TEST_ASSERT(painted_mail);

            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            click_capture clicks;
            click_capture_reset(&clicks);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "input[type=email]", 17, record_click, &clicks, NULL) >= 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == mail);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make(".net", 4)));
            TBOX_TEST_ASSERT(changes == 1 && tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("\n", 1)));
            TBOX_TEST_ASSERT(changes == 1);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("bad@@example", 12)));
            TBOX_TEST_ASSERT(changes == 2 && !tbox_context_email_valid(mail));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("new@example.com", 15)));
            TBOX_TEST_ASSERT(changes == 3 && tbox_context_email_valid(mail));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)mail, tbox_string_view_make("value", 5), tbox_string_view_make("", 0));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(mail));
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == multi);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)multi, tbox_string_view_make("value", 5), tbox_string_view_make("a@b, nope", 9));
            TBOX_TEST_ASSERT(!tbox_context_email_valid(multi));
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)multi, tbox_string_view_make("value", 5), tbox_string_view_make("a@b, x@y", 8));
            TBOX_TEST_ASSERT(tbox_context_email_valid(multi));
            tbox_context_run_frame(ctx, 320.0, 320.0, &list);
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            TBOX_TEST_ASSERT(clicks.call_count == 0);
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Number inputs accept numeric editing, expose constraint validity, and
     * step on Up/Down without exceeding min or max. */
    {
        tbox_context *ctx = open_cstr("<div><input id='quantity' type='NUMBER' value='1.5' min='1' max='3' step='0.5'>"
                                      "<input type='number' disabled value='2'>"
                                      "<input id='required-number' type='number' required min='2'>"
                                      "<input id='any-step' type='number' value='1.25' step='any'>"
                                      "<input id='invalid-number' type='number' value='word'></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 300.0, &list);
            const tbox_html_node *quantity = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *disabled = quantity->next_sibling;
            const tbox_html_node *required = disabled->next_sibling;
            const tbox_html_node *any      = required->next_sibling;
            const tbox_html_node *invalid  = any->next_sibling;
            const tbox_layout_box *box     = find_context_box(ctx, quantity);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL)
                TBOX_TEST_ASSERT(box->content_box.width == 120.0);
            TBOX_TEST_ASSERT(tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(required));
            TBOX_TEST_ASSERT(tbox_context_number_valid(any));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(NULL));
            const tbox_html_attribute *invalid_value = tbox_html_node_get_attribute(invalid, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(invalid_value != NULL && invalid_value->value.size == 0);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == quantity);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("2.25", 4)));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("abc", 3)));
            TBOX_TEST_ASSERT(changes == 1);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(quantity, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2.5"));
            TBOX_TEST_ASSERT(tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
            TBOX_TEST_ASSERT(changes == 4);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("1e2", 3)));
            TBOX_TEST_ASSERT(!tbox_context_number_valid(quantity));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == required);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
            value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2"));
            TBOX_TEST_ASSERT(tbox_context_number_valid(required));
            tbox_context_run_frame(ctx, 320.0, 300.0, &list);
            const tbox_layout_box *required_box = find_context_box(ctx, required);
            TBOX_TEST_ASSERT(required_box != NULL);
            if (required_box != NULL) {
                double spinner_x     = required_box->content_box.x + required_box->content_box.width - 8.0;
                double spinner_y     = required_box->content_box.y + required_box->content_box.height / 4.0;
                bool painted_spinner = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 238 && list.items[i].rect.x == required_box->content_box.x + required_box->content_box.width - 15.0) /* inside the 1px border-left */
                        painted_spinner = true;
                TBOX_TEST_ASSERT(painted_spinner);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, spinner_x, spinner_y));
                value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "3"));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, spinner_x, required_box->content_box.y + required_box->content_box.height * 0.75));
                value = tbox_html_node_get_attribute(required, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2"));
            }
            const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
            TBOX_TEST_ASSERT(disabled_box != NULL);
            if (disabled_box != NULL)
                TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
            tbox_context_close(ctx);
        }
    }

    /* Password values remain in the DOM while rendering one mask glyph per
     * Unicode character; selected secrets are not exposed to the clipboard. */
    {
        tbox_context *ctx = open_cstr("<div><input id='secret' type='PASSWORD' value='séc'>"
                                      "<input type='password' disabled value='off'><button>Next</button></div>",
            "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            const tbox_html_node *secret     = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "séc"));
            bool painted_secret = false, painted_mask = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind != TBOX_PAINT_TEXT_RUN)
                    continue;
                if (string_view_equal_cstr(list.items[i].text, "séc"))
                    painted_secret = true;
                if (string_view_equal_cstr(list.items[i].text, "•••") || string_view_equal_cstr(list.items[i].text, "***"))
                    painted_mask = true;
            }
            TBOX_TEST_ASSERT(!painted_secret && painted_mask);
            int changes = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == secret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_selected_text(ctx).size == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "Z"));
            TBOX_TEST_ASSERT(changes == 1);
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            painted_secret = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Z"))
                    painted_secret = true;
            TBOX_TEST_ASSERT(!painted_secret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
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
            tbox_context *ctx = open_cstr("<div><input id='upload' type='FILE' value='/etc/passwd'>"
                                          "<input id='off' type='file' disabled></div>",
                "", fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                const tbox_html_node *upload       = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
                const tbox_html_node *disabled     = upload->next_sibling;
                const tbox_html_attribute *initial = tbox_html_node_get_attribute(upload, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(initial != NULL && initial->value.size == 0);
                TBOX_TEST_ASSERT(tbox_context_file_path(ctx, upload).size == 0);
                int changes = 0;
                tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == upload);
                TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("/tmp/file", 9)));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool showed_file = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "yellow.png"))
                        showed_file = true;
                TBOX_TEST_ASSERT(showed_file);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                TBOX_TEST_ASSERT(changes == 1);
                const tbox_html_attribute *selected = tbox_html_node_get_attribute(upload, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(selected != NULL && string_view_equal_cstr(selected->value, "yellow.png"));
                char expected_path[4096];
                int expected_length = snprintf(expected_path, sizeof(expected_path), "%s/yellow.png", TBOX_TEST_ASSETS_DIR);
                TBOX_TEST_ASSERT(expected_length > 0 && (size_t)expected_length < sizeof(expected_path));
                TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_file_path(ctx, upload), expected_path));

                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool painted_selection = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "yellow.png"))
                        painted_selection = true;
                TBOX_TEST_ASSERT(painted_selection);
                const tbox_layout_box *box = find_context_box(ctx, upload);
                TBOX_TEST_ASSERT(box != NULL);
                if (box != NULL) {
                    double popup_x = box->border_box.x;
                    double popup_y = box->border_box.y + box->border_box.height;
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 5.0, box->border_box.y + 5.0));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_BACKSPACE, true, false, false }));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false }));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                    TBOX_TEST_ASSERT(tbox_context_scroll(ctx, popup_x + 20.0, popup_y + 100.0, 1.0));
                    TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, popup_x + 20.0, popup_y + 100.0));
                    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
                    tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup_x + 20.0, popup_y + 222.0));
                    TBOX_TEST_ASSERT(changes == 2);
                }
                tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)upload, tbox_string_view_make("value", 5), tbox_string_view_make("", 0));
                TBOX_TEST_ASSERT(tbox_context_file_path(ctx, upload).size == 0);
                tbox_context_run_frame(ctx, 380.0, 360.0, &list);
                bool painted_empty = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "No file chosen"))
                        painted_empty = true;
                TBOX_TEST_ASSERT(painted_empty);
                const tbox_layout_box *disabled_box = find_context_box(ctx, disabled);
                TBOX_TEST_ASSERT(disabled_box != NULL);
                if (disabled_box != NULL)
                    TBOX_TEST_ASSERT(!tbox_context_dispatch_click(ctx, disabled_box->border_box.x + 5.0, disabled_box->border_box.y + 5.0));
                TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
                tbox_context_close(ctx);
            }
            TBOX_TEST_ASSERT(chdir(original_directory) == 0);
        }
    }

    /* Hidden inputs keep their DOM value, but have no layout box or focus,
     * even when author CSS tries to make them visible. */
    {
        tbox_context *ctx = open_cstr("<div><button id='first'>First</button>"
                                      "<input id='secret' type='HIDDEN' value='token'>"
                                      "<button id='second'>Second</button>"
                                      "<p>Before <input id='inline-secret' type='hidden' value='inline'> after</p></div>",
            "#secret { display: block; width: 300px; height: 100px; } "
            "#inline-secret { display: inline; }",
            fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_html_node *container     = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *first         = container->first_child;
            const tbox_html_node *secret        = first->next_sibling;
            const tbox_html_node *second        = secret->next_sibling;
            const tbox_html_node *inline_secret = second->next_sibling->first_child->next_sibling;
            const tbox_html_attribute *value    = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "token"));
            TBOX_TEST_ASSERT(find_context_box(ctx, secret) == NULL);
            TBOX_TEST_ASSERT(find_context_box(ctx, inline_secret) == NULL);
            const tbox_layout_box *first_box  = find_context_box(ctx, first);
            const tbox_layout_box *second_box = find_context_box(ctx, second);
            TBOX_TEST_ASSERT(first_box != NULL && second_box != NULL);
            if (first_box != NULL && second_box != NULL)
                /* The hidden input takes no room: the inline-block buttons touch. */
                TBOX_TEST_ASSERT(second_box->margin_box.y == first_box->margin_box.y && second_box->margin_box.x == first_box->margin_box.x + first_box->margin_box.width);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == first);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == second);
            tbox_html_node_set_attribute(tbox_context_document(ctx), (tbox_html_node *)secret, tbox_string_view_make("value", 5), tbox_string_view_make("updated", 7));
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "updated"));
            TBOX_TEST_ASSERT(find_context_box(ctx, secret) == NULL);
            tbox_context_close(ctx);
        }

        ctx = open_cstr("<input type='hidden' value='root-token'>", "input { display: block; width: 100px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 200.0, &list);
            TBOX_TEST_ASSERT(list.count == 0);
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 10.0, 10.0) == NULL);
            const tbox_html_node *secret     = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_attribute *value = tbox_html_node_get_attribute(secret, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "root-token"));
            tbox_context_close(ctx);
        }
    }

    /* A malformed byte in the initial value arrives as U+FFFD: the caret
     * steps over it as one character and Delete removes it whole. */
    {
        tbox_context *ctx = open_cstr("<input value='a\xff" "b'>", "input { width: 180px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            const tbox_html_node *field = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == field);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DELETE, true, false, false }));
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "ab"));
            tbox_context_close(ctx);
        }
    }

    /* Text inputs share keyboard focus with buttons. Editing operates on
     * UTF-8 boundaries and updates both the DOM value and rendered text. */
    {
        tbox_context *ctx = open_cstr("<div><input id='name' value='ab'><input disabled><button>OK</button></div>", "input { width: 180px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            const tbox_html_node *field = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            int changes                 = 0;
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == field);
            /* Navigation through an initial HTML value must persist across
             * frames even before the first text insertion. */
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_LEFT, true, false, false }));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
            TBOX_TEST_ASSERT(changes == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("é", 2)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_LEFT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_BACKSPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DELETE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            TBOX_TEST_ASSERT(changes == 5);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "Zab"));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_text(ctx, tbox_string_view_make("\xff", 1)));
            tbox_context_run_frame(ctx, 320.0, 180.0, &list);
            bool painted_value = false, painted_caret = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(list.items[i].text, "Zab"))
                    painted_value = true;
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].rect.width == 1.0 && list.items[i].rect.height > 5.0)
                    painted_caret = true;
            }
            TBOX_TEST_ASSERT(painted_value && painted_caret);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
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
            tbox_context_set_input_handler(ctx, record_input, &changes, NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, true, false }));
            tbox_context_run_frame(ctx, 200.0, 80.0, &list);
            bool highlighted = false;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 130 && list.items[i].rect.width > 0.0)
                    highlighted = true;
            }
            TBOX_TEST_ASSERT(highlighted);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Z", 1)));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_RIGHT, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_BACKSPACE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_END, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_LEFT, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DELETE, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_HOME, true, true, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_LEFT, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("Q", 1)));
            const tbox_html_node *field      = tbox_context_focused_node(ctx);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "QZ"));
            TBOX_TEST_ASSERT(changes == 4);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(!tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
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
        tbox_context *ctx = open_cstr("<input value='abcdefghijklmnopqrstuvwxyz'>", "input { width: 40px; border: 1px solid black; padding: 2px; background-color: white; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 80.0, &list);
            const tbox_paint_op *text_op = NULL;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN)
                    text_op = &list.items[i];
            }
            TBOX_TEST_ASSERT(text_op != NULL && text_op->has_clip && text_op->clip.width == 40.0);
            tbox_rect input_clip = text_op != NULL ? text_op->clip : (tbox_rect){ 0 };
            uint32_t pixels[300 * 80];
            for (size_t i = 0; i < 300 * 80; i++)
                pixels[i] = 0xffffffffu;
            tbox_raster_display_list(pixels, 300, 80, &list);
            if (text_op != NULL) {
                int outside_x = (int)(text_op->clip.x + text_op->clip.width + 5.0);
                for (int y = (int)text_op->clip.y; y < (int)(text_op->clip.y + text_op->clip.height); y++)
                    TBOX_TEST_ASSERT(pixels[y * 300 + outside_x] == 0xffffffffu);
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            tbox_context_run_frame(ctx, 300.0, 80.0, &list);
            bool scrolled = false, caret_inside = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_TEXT_RUN && op->has_clip)
                    scrolled = op->rect.x < op->clip.x;
                if (op->kind == TBOX_PAINT_FILL_RECT && op->rect.width == 1.0 && text_op != NULL && op->rect.x >= input_clip.x && op->rect.x < input_clip.x + input_clip.width)
                    caret_inside = true;
            }
            TBOX_TEST_ASSERT(scrolled && caret_inside);
            for (size_t i = 0; i < 300 * 80; i++)
                pixels[i] = 0xffffffffu;
            tbox_raster_display_list(pixels, 300, 80, &list);
            int outside_x = (int)(input_clip.x + input_clip.width + 5.0);
            for (int y = (int)input_clip.y; y < (int)(input_clip.y + input_clip.height); y++)
                TBOX_TEST_ASSERT(pixels[y * 300 + outside_x] == 0xffffffffu);
            tbox_context_dispatch_click(ctx, input_clip.x + 2.0, input_clip.y + 2.0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("!", 1)));
            const tbox_html_node *field      = tbox_context_focused_node(ctx);
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
                if (hit != NULL && hit->node != NULL && string_view_equal_cstr(hit->node->element.tag_name, "input"))
                    box = hit;
            }
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                tbox_context_dispatch_click(ctx, box->content_box.x + 70.0, box->content_box.y + 2.0);
                TBOX_TEST_ASSERT(tbox_context_drag_select(ctx, box->content_box.x - 20.0));
                tbox_string_view selected = tbox_context_selected_text(ctx);
                TBOX_TEST_ASSERT(string_view_equal_cstr(selected, "aébc"));
                char copied[6];
                if (selected.size == 5)
                    memcpy(copied, selected.data, 5);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DELETE, true, false, false }));
                const tbox_html_node *field      = tbox_context_focused_node(ctx);
                const tbox_html_attribute *value = tbox_html_node_get_attribute(field, tbox_string_view_make("value", 5));
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
        const char *html  = "<div id='list'><div id='one'></div><div id='two'></div><div id='three'></div></div>";
        /* The classic bar (10px, opaque track) pinned by the standard
         * properties: these checks are about scrolling, not the default look. */
        const char *css   = "#list { width: 60px; height: 40px; overflow-y: auto; scrollbar-width: auto; scrollbar-color: #646e7a #dce0e6; } "
                            "#one, #two, #three { height: 30px; } "
                            "#one { background-color: red; } #two { background-color: green; } "
                            "#three { background-color: blue; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            uint32_t pixels[80 * 100];
            for (size_t i = 0; i < 80 * 100; i++)
                pixels[i] = 0xFFFFFFFFu;
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
            for (size_t i = 0; i < 80 * 100; i++)
                pixels[i] = 0xFFFFFFFFu;
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
            for (size_t i = 0; i < 80 * 100; i++)
                pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFF008000u);
            TBOX_TEST_ASSERT(pixels[5 * 80 + 55] == 0xFFDCE0E6u);
            TBOX_TEST_ASSERT(pixels[25 * 80 + 55] == 0xFF646E7Au);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 10.0, 100.0));
            TBOX_TEST_ASSERT(!tbox_context_scroll(ctx, 10.0, 10.0, 100.0));
            tbox_context_run_frame(ctx, 80.0, 100.0, &list);
            for (size_t i = 0; i < 80 * 100; i++)
                pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 100, &list);
            TBOX_TEST_ASSERT(pixels[10 * 80 + 10] == 0xFF0000FFu);
            TBOX_TEST_ASSERT(pixels[45 * 80 + 10] == 0xFFFFFFFFu);
            const tbox_layout_box *visible = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(visible != NULL && visible->node != NULL && tbox_html_node_get_attribute(visible->node, tbox_string_view_make("id", 2)) != NULL && string_view_equal_cstr(tbox_html_node_get_attribute(visible->node, tbox_string_view_make("id", 2))->value, "three"));
            tbox_context_close(ctx);
        }
        ctx = open_cstr("<div id='list'><div id='one'></div></div>",
            "#list { width: 60px; height: 40px; overflow-y: auto; } "
            "#one { height: 30px; background-color: red; }",
            fonts);
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
        tbox_context *ctx = open_cstr("<div id='clip'><div id='first'></div><div id='outside'></div></div>",
            "#clip { width: 60px; height: 30px; overflow-y: hidden; }"
            "#first { height: 30px; background-color: red; }"
            "#outside { height: 30px; background-color: blue; }",
            fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            uint32_t pixels[80 * 80];
            for (size_t i = 0; i < 80 * 80; i++)
                pixels[i] = 0xFFFFFFFFu;
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
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == node);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("X", 1)));
            TBOX_TEST_ASSERT(string_view_equal_cstr(node->first_child->text.text, "ab\ncd\nX"));
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(hit != NULL && hit->node == node);
            if (hit != NULL) {
                const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_make(hit->style->font_family, strlen(hit->style->font_family)), hit->style->font_weight_bold, hit->style->font_italic, hit->style->font_size);
                TBOX_TEST_ASSERT(face != NULL);
                if (face != NULL) {
                    double expected_width  = 8.0 * tbox_font_measure_text(face, tbox_string_view_make("0", 1));
                    double expected_height = 2.0 * tbox_font_face_line_height(face);
                    double width_error     = hit->content_box.width - expected_width;
                    double height_error    = hit->content_box.height - expected_height;
                    TBOX_TEST_ASSERT(width_error > -0.01 && width_error < 0.01);
                    TBOX_TEST_ASSERT(height_error > -0.01 && height_error < 0.01);
                }
                TBOX_TEST_ASSERT(hit->text_run_count == 3);
                TBOX_TEST_ASSERT(hit->scroll_content_height > hit->content_box.height);
                TBOX_TEST_ASSERT(tbox_context_scroll(ctx, hit->content_box.x + 2.0, hit->content_box.y + 2.0, -100.0));
                TBOX_TEST_ASSERT(tbox_context_drag_select_at(ctx, hit->content_box.x, hit->content_box.y));
            }
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_selected_text(ctx), "ab\ncd\nX"));
            tbox_context_close(ctx);
        }
        ctx = open_cstr("<textarea rows='5' cols='40'>A</textarea>", "textarea { width: 90px; height: 35px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_layout_box *box = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(box != NULL && box->content_box.width == 90.0 && box->content_box.height == 35.0);
            tbox_context_close(ctx);
        }
    }

    /* Control parts paint from the document's pseudo-elements: sizes,
     * backgrounds, borders and colors, with pointer input following the
     * new geometry (thumb span, step-control width). */
    {
        const char *html = "<div><input type='range' value='50'><progress value='50' max='100'></progress>"
            "<meter min='0' max='100' value='90' low='20' high='80'></meter><select><option>A</option></select>"
            "<input type='number' value='1'><input type='search' value='x'><input type='file'>"
            "<input type='radio' checked><input type='color' value='#00ff00'></div>";
        const char *css = "input[type=range]::slider-thumb { width: 20px; background-color: rgb(1, 2, 3); }"
            "progress::slider-track { border: 2px solid rgb(4, 5, 6); } progress::slider-fill { background-color: rgb(7, 8, 9); }"
            "meter::slider-fill { background-color: rgb(10, 11, 12); }"
            "select::picker-icon { color: rgb(13, 14, 15); }"
            "input[type=number]::step-control { width: 24px; background-color: rgb(16, 17, 18); }"
            "input[type=search]::clear-icon { color: rgb(19, 20, 21); }"
            "input[type=file]::file-selector-button { width: 50px; background-color: rgb(22, 23, 24); }"
            "input[type=radio]::checkmark { color: rgb(25, 26, 27); }"
            "input[type=color]::color-swatch { border: 3px solid rgb(28, 29, 30); }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 900.0, 300.0, &list);
            tbox_html_node *root = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *range = root->first_child, *progress = range->next_sibling;
            const tbox_html_node *number = progress->next_sibling->next_sibling->next_sibling;
            const tbox_layout_box *range_box = find_context_box(ctx, range), *progress_box = find_context_box(ctx, progress);
            const tbox_layout_box *number_box = find_context_box(ctx, number);
            TBOX_TEST_ASSERT(range_box != NULL && progress_box != NULL && number_box != NULL);
            bool thumb = false, track = false, fill = false, meter = false, arrow = false, column = false;
            bool clear = false, button = false, dot = false, swatch = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                tbox_css_rgba c = op->color;
                if (op->kind == TBOX_PAINT_FILL_RING) {
                    if (c.r == 28 && c.g == 29 && c.b == 30) swatch = true;
                    continue;
                }
                if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                if (c.r == 1 && c.g == 2 && c.b == 3 && op->rect.width == 20.0) thumb = true;
                if (c.r == 4 && c.g == 5 && c.b == 6) track = true;
                if (c.r == 7 && c.g == 8 && c.b == 9 && progress_box != NULL && op->rect.x == progress_box->content_box.x + 2.0 &&
                    op->rect.width == (progress_box->content_box.width - 4.0) * 0.5)
                    fill = true;
                if (c.r == 10 && c.g == 11 && c.b == 12) meter = true;
                if (c.r == 13 && c.g == 14 && c.b == 15) arrow = true;
                if (c.r == 16 && c.g == 17 && c.b == 18 && op->rect.width == 23.0) column = true; /* inside the 1px border-left */
                if (c.r == 19 && c.g == 20 && c.b == 21) clear = true;
                if (c.r == 22 && c.g == 23 && c.b == 24 && op->rect.width == 48.0) button = true; /* inside the 1px border */
                if (c.r == 25 && c.g == 26 && c.b == 27) dot = true;
            }
            TBOX_TEST_ASSERT(thumb && track && fill && meter && arrow && column);
            TBOX_TEST_ASSERT(clear && button && dot && swatch);
            /* The thumb's center travels a span inset by half its width. */
            if (range_box != NULL) {
                double y = range_box->content_box.y + range_box->content_box.height / 2.0;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, range_box->content_box.x + 10.0, y));
                const tbox_html_attribute *value = tbox_html_node_get_attribute(range, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "0"));
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, range_box->content_box.x + range_box->content_box.width - 10.0, y));
                value = tbox_html_node_get_attribute(range, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "100"));
            }
            /* A click 20px from the right edge is inside the 24px column. */
            if (number_box != NULL) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, number_box->content_box.x + number_box->content_box.width - 20.0,
                    number_box->content_box.y + 2.0));
                const tbox_html_attribute *value = tbox_html_node_get_attribute(number, tbox_string_view_make("value", 5));
                TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2"));
            }
            tbox_context_close(ctx);
        }
    }

    /* Focus is a ring (outline), not a thicker border: focusing a control
     * moves neither it, its content, nor what follows it. */
    {
        const char *html = "<div><input value='a'><span>t</span><button>b</button><span>t</span>"
            "<select><option>o</option></select><span>t</span><textarea>x</textarea><span>t</span>"
            "<input type='checkbox'><span>t</span></div>";
        tbox_context *ctx = open_cstr(html, "span { display: inline-block; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 900.0, 300.0, &list);
            const tbox_html_node *root = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            tbox_rect before[10], content[10];
            size_t count = 0;
            for (const tbox_html_node *node = root->first_child; node != NULL && count < 10; node = node->next_sibling, count++) {
                const tbox_layout_box *box = find_context_box(ctx, node);
                TBOX_TEST_ASSERT(box != NULL);
                before[count]  = box != NULL ? box->border_box : (tbox_rect){0};
                content[count] = box != NULL ? box->content_box : (tbox_rect){0};
            }
            for (size_t control = 0; control < count; control += 2) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                tbox_context_run_frame(ctx, 900.0, 300.0, &list);
                size_t i = 0;
                for (const tbox_html_node *node = root->first_child; node != NULL && i < count; node = node->next_sibling, i++) {
                    const tbox_layout_box *box = find_context_box(ctx, node);
                    if (i == control) TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == node);
                    TBOX_TEST_ASSERT(box != NULL && memcmp(&box->border_box, &before[i], sizeof(tbox_rect)) == 0 &&
                                     memcmp(&box->content_box, &content[i], sizeof(tbox_rect)) == 0);
                }
            }
            tbox_context_close(ctx);
        }
    }

    /* The open select list follows the standard customizable-select CSS:
     * ::picker(select) is its box, rows take their <option>/<optgroup>
     * style, the highlighted option matches :hover (moved by keys and by
     * the pointer), the chosen one :checked (not the `selected`
     * attribute), and options in a disabled optgroup match :disabled. */
    {
        const char *html = "<div><select><option>Um</option><option selected>Dois</option>"
            "<optgroup label='G' disabled><option>Tres</option></optgroup><option>Quatro</option></select></div>";
        const char *css = "select::picker( select ) { border: 3px solid rgb(1, 2, 3); background-color: rgb(4, 5, 6); }"
            "option { padding: 10px 20px; }"
            "option:checked { background-color: rgb(7, 8, 9); }"
            "option:hover { background-color: rgb(10, 11, 12); color: rgb(13, 14, 15); }"
            "option:disabled { color: rgb(16, 17, 18); }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 400.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *one = select->first_child, *two = one->next_sibling;
            const tbox_layout_box *box = find_context_box(ctx, select);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                double x = box->border_box.x + 10.0;
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                /* Opening highlights the chosen option: Dois is both. */
                double picker_y = -1.0, row_height = 0.0, checked_y = -1.0, hover_y = -1.0;
                bool picker_bg = false, hover_text = false, disabled_text = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    tbox_css_rgba c = op->color;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 1 && c.g == 2 && c.b == 3) picker_y = op->rect.y;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 4 && c.g == 5 && c.b == 6) picker_bg = true;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 7 && c.g == 8 && c.b == 9) checked_y = op->rect.y;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 10 && c.g == 11 && c.b == 12) { hover_y = op->rect.y; row_height = op->rect.height; }
                    if (op->kind == TBOX_PAINT_TEXT_RUN && c.r == 13 && c.g == 14 && c.b == 15 && string_view_equal_cstr(op->text, "Dois")) hover_text = true;
                    if (op->kind == TBOX_PAINT_TEXT_RUN && c.r == 16 && c.g == 17 && c.b == 18 && string_view_equal_cstr(op->text, "Tres")) disabled_text = true;
                }
                TBOX_TEST_ASSERT(picker_y >= 0.0 && picker_bg && hover_text && disabled_text);
                TBOX_TEST_ASSERT(checked_y < 0.0); /* option:hover comes later and wins on Dois */
                /* Row height: the font's line height plus 10px + 10px of padding.
                 * (The frame above rebuilt the boxes: look the select up again.) */
                box = find_context_box(ctx, select);
                TBOX_TEST_ASSERT(box != NULL);
                const tbox_font_face *face = box == NULL ? NULL :
                    tbox_font_face_cache_get(fonts, tbox_string_view_make(box->style->font_family, strlen(box->style->font_family)),
                                             box->style->font_weight_bold, box->style->font_italic, box->style->font_size);
                TBOX_TEST_ASSERT(face != NULL && hover_y >= 0.0 && row_height == tbox_font_face_line_height(face) + 20.0);
                /* The pointer over the first row highlights Um; Dois keeps :checked. */
                TBOX_TEST_ASSERT(tbox_context_update_hover(ctx, true, x, picker_y + row_height / 2.0));
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                bool one_hover = false, two_checked = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    tbox_css_rgba c = op->color;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 10 && c.g == 11 && op->rect.y == picker_y) one_hover = true;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 7 && c.g == 8 && op->rect.y == picker_y + row_height) two_checked = true;
                }
                TBOX_TEST_ASSERT(one_hover && two_checked);
                /* Choosing Um moves :checked to it; the `selected` attribute stays on Dois. */
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, picker_y + row_height / 2.0));
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "Um"));
                TBOX_TEST_ASSERT(one->element.selectedness && !two->element.selectedness);
                TBOX_TEST_ASSERT(tbox_html_node_get_attribute(two, tbox_string_view_make("selected", 8)) != NULL);
            }
            tbox_context_close(ctx);
        }
    }

    /* Scrollbar parts: ::-tbox-scrollbar-track sets the bar's width (paint,
     * hit test and the stable gutter alike), ::-tbox-scrollbar-thumb its
     * inset, minimum length and shape, with :hover / :active variants; the
     * thumb travels the track less its margins. */
    {
        const char *html = "<div><div id='box'><p>1</p><p>2</p><p>3</p><p>4</p><p>5</p><p>6</p><p>7</p><p>8</p><p>9</p></div></div>";
        const char *css = "#box { width: 200px; height: 100px; overflow-y: auto; scrollbar-gutter: stable; }"
            "#box::-tbox-scrollbar-track { width: 14px; background-color: rgb(1, 2, 3); }"
            "#box::-tbox-scrollbar-thumb { background-color: rgb(4, 5, 6); margin: 3px; min-height: 60px; border: 0; }"
            "#box::-tbox-scrollbar-thumb:hover { background-color: rgb(7, 8, 9); }"
            "#box::-tbox-scrollbar-thumb:active { background-color: rgb(10, 11, 12); }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_html_node *node = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                /* The stable gutter is the track's 14px. */
                TBOX_TEST_ASSERT(box->scrollbar_gutter == 14.0 && box->content_box.width == 200.0 - 14.0);
                double right = box->padding_box.x + box->padding_box.width;
                tbox_rect track = {0}, thumb = {0};
                bool hover_thumb = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                    if (op->color.r == 1 && op->color.g == 2) track = op->rect;
                    if (op->color.r == 4 && op->color.g == 5) thumb = op->rect;
                    if (op->color.r == 7 && op->color.g == 8) hover_thumb = true;
                }
                TBOX_TEST_ASSERT(track.width == 14.0 && track.x == right - 14.0 && track.height == box->padding_box.height);
                TBOX_TEST_ASSERT(thumb.x == track.x + 3.0 && thumb.width == 8.0 && thumb.y == track.y + 3.0 && thumb.height == 60.0);
                TBOX_TEST_ASSERT(!hover_thumb);
                double x = thumb.x + thumb.width / 2.0, y = thumb.y + 10.0;
                /* Pointer on the thumb: :hover. */
                TBOX_TEST_ASSERT(tbox_context_update_hover(ctx, true, x, y));
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                bool hovered = false, active = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 7 && list.items[i].color.g == 8) hovered = true;
                TBOX_TEST_ASSERT(hovered);
                /* Dragging: :active; to the bottom of the rail scrolls to the end. */
                TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, x, y));
                /* The thumb follows the pointer 1:1 along the rail. */
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, x, y + 10.0));
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                bool followed = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 10 && list.items[i].color.g == 11 &&
                        fabs(list.items[i].rect.y - (thumb.y + 10.0)) < 1e-6)
                        followed = true;
                TBOX_TEST_ASSERT(followed);
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, x, y + 1000.0));
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                tbox_rect dragged = {0};
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 10 && list.items[i].color.g == 11) {
                        active = true;
                        dragged = list.items[i].rect;
                    }
                TBOX_TEST_ASSERT(active && dragged.y + dragged.height == track.y + track.height - 3.0);
                tbox_context_scrollbar_release(ctx);
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                active = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 10 && list.items[i].color.g == 11) active = true;
                TBOX_TEST_ASSERT(!active);
            }
            tbox_context_close(ctx);
        }
    }

    /* resize: a handle in the padding box's bottom-right corner (12px,
     * ::-tbox-resizer sizes and colors it) that the pointer drags to change
     * the element's width/height, clamped by max-width and kept through
     * later frames; horizontal resizes only the width; overflow: visible
     * and a plain <textarea> have none, `textarea { resize: both }` does. */
    {
        const char *html = "<div><div id='a'>x</div><div id='b'>y</div><div id='c'>z</div><textarea></textarea><textarea id='t'></textarea></div>";
        const char *css  = "#a, #b, #c { width: 100px; height: 50px; overflow: auto; resize: both; border: 2px solid black; padding: 3px; }"
                           "#a { max-width: 150px; } #b { resize: horizontal; box-sizing: border-box; } #c { overflow: visible; }"
                           "#a::-tbox-resizer { color: rgb(1, 2, 3); width: 16px; height: 16px; }"
                           "#t { resize: both; width: 80px; height: 40px; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 400.0, &list);
            const tbox_html_node *a = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *b = a->next_sibling, *c = b->next_sibling, *plain = c->next_sibling, *t = plain->next_sibling;
            const tbox_layout_box *box = find_context_box(ctx, a);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                double right = box->padding_box.x + box->padding_box.width, bottom = box->padding_box.y + box->padding_box.height;
                bool grip = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind == TBOX_PAINT_FILL_RECT && op->color.r == 1 && op->color.g == 2 && op->polygon_clip != NULL &&
                        op->rect.x == right - 16.0 && op->rect.y == bottom - 16.0 && op->rect.width == 16.0)
                        grip = true;
                }
                TBOX_TEST_ASSERT(grip);
                TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, right - 4.0, bottom - 4.0) == TBOX_STYLE_CURSOR_NWSE_RESIZE);
                /* Drag 30px right and 20px down: content box 130x70. */
                TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, right - 4.0, bottom - 4.0));
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, right + 26.0, bottom + 16.0));
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                box = find_context_box(ctx, a);
                TBOX_TEST_ASSERT(box != NULL && box->content_box.width == 130.0 && box->content_box.height == 70.0);
                /* max-width still applies. */
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, right + 200.0, bottom - 4.0));
                tbox_context_scrollbar_release(ctx);
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                box = find_context_box(ctx, a);
                TBOX_TEST_ASSERT(box != NULL && box->content_box.width == 150.0 && box->content_box.height == 50.0);
            }
            const tbox_layout_box *hb = find_context_box(ctx, b);
            TBOX_TEST_ASSERT(hb != NULL);
            if (hb != NULL) {
                double right = hb->padding_box.x + hb->padding_box.width, bottom = hb->padding_box.y + hb->padding_box.height;
                TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, right - 4.0, bottom - 4.0) == TBOX_STYLE_CURSOR_EW_RESIZE);
                TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, right - 4.0, bottom - 4.0));
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, right - 24.0, bottom + 50.0));
                tbox_context_scrollbar_release(ctx);
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                hb = find_context_box(ctx, b);
                TBOX_TEST_ASSERT(hb != NULL && hb->border_box.width == 80.0 && hb->border_box.height == 50.0);
            }
            const tbox_layout_box *visible = find_context_box(ctx, c), *plain_box = find_context_box(ctx, plain), *tb = find_context_box(ctx, t);
            TBOX_TEST_ASSERT(visible != NULL && plain_box != NULL && tb != NULL);
            if (visible != NULL && plain_box != NULL && tb != NULL) {
                TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, visible->padding_box.x + visible->padding_box.width - 3.0, visible->padding_box.y + visible->padding_box.height - 3.0));
                TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, plain_box->padding_box.x + plain_box->padding_box.width - 3.0, plain_box->padding_box.y + plain_box->padding_box.height - 3.0));
                tbox_context_scrollbar_release(ctx);
                double x = tb->padding_box.x + tb->padding_box.width - 3.0, y = tb->padding_box.y + tb->padding_box.height - 3.0;
                /* The default handle is 12px. */
                TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, x - 8.0, y - 8.0) == TBOX_STYLE_CURSOR_NWSE_RESIZE &&
                                 tbox_context_cursor_at(ctx, x - 11.0, y - 3.0) != TBOX_STYLE_CURSOR_NWSE_RESIZE);
                TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, x, y));
                TBOX_TEST_ASSERT(tbox_context_scrollbar_drag(ctx, x + 10.0, y + 10.0));
                tbox_context_scrollbar_release(ctx);
                tbox_context_run_frame(ctx, 400.0, 400.0, &list);
                tb = find_context_box(ctx, t);
                TBOX_TEST_ASSERT(tb != NULL && tb->content_box.width == 90.0 && tb->content_box.height == 50.0);
            }
            tbox_context_close(ctx);
        }
    }

    /* The default scrollbar is discreet: thin (6px), a transparent track
     * showing the content under it, and a gray pill thumb inset 1px (4px
     * wide); scrollbar-width: auto still restores the 10px bar. */
    {
        const char *html = "<div><div id='a'><div class='tall'></div></div><div id='b'><div class='tall'></div></div></div>";
        const char *css  = "#a, #b { width: 60px; height: 40px; overflow-y: auto; background-color: rgb(250, 0, 0); }"
                           "#b { scrollbar-width: auto; } .tall { height: 200px; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 200.0, &list);
            const tbox_html_node *a = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *box = find_context_box(ctx, a);
            const tbox_layout_box *wide = find_context_box(ctx, a->next_sibling);
            TBOX_TEST_ASSERT(box != NULL && wide != NULL);
            if (box != NULL && wide != NULL) {
                double right = box->padding_box.x + box->padding_box.width;
                bool thumb = false, wide_thumb = false, opaque_track = false, white = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                    tbox_css_rgba c = op->color;
                    if (c.r == 100 && c.g == 110 && c.b == 122 && op->rect.x == right - 5.0 && op->rect.width == 4.0 &&
                        op->rect.y == box->padding_box.y + 1.0 && op->corner_radii[0] == 2.0)
                        thumb = true;
                    if (c.r == 100 && c.g == 110 && c.b == 122 && op->rect.width == 8.0 &&
                        op->rect.x == wide->padding_box.x + wide->padding_box.width - 9.0)
                        wide_thumb = true;
                    if (c.r == 220 && c.g == 224 && c.b == 230) opaque_track = true;
                    if (c.r == 255 && c.g == 255 && c.b == 255 && c.a == 255) white = true;
                }
                TBOX_TEST_ASSERT(thumb && wide_thumb && !opaque_track && !white);
            }
            tbox_context_close(ctx);
        }
    }

    /* Arrow keys step through options past optgroup labels: an option's
     * place among the options, not among the list rows (which include the
     * labels), picks its neighbor. */
    {
        const char *html = "<select><option>Um</option><optgroup label='G'><option>Dois</option><option>Tres</option></optgroup>"
            "<optgroup label='H' disabled><option>Quatro</option></optgroup></select>";
        const char *expected[] = { "Dois", "Tres", "Tres" };
        for (int downs = 1; downs <= 3; downs++) {
            tbox_context *ctx = open_cstr(html, "", fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx == NULL) continue;
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 400.0, &list);
            const tbox_html_node *select = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            for (int i = 0; i < downs; i++)
                tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_DOWN, true, false, false });
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), expected[downs - 1]));
            if (downs == 2) {
                /* Up from Tres on the closed select goes back to Dois. */
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_UP, true, false, false }));
                TBOX_TEST_ASSERT(string_view_equal_cstr(tbox_context_control_value(ctx, select), "Dois"));
            }
            tbox_context_close(ctx);
        }
    }

    /* An emptied text field keeps the baseline of one line of its text, so
     * clearing it moves neither it nor its neighbors on the line. */
    {
        const char *css_cases[2] = { "", "input { line-height: 30px; }" };
        for (int c = 0; c < 2; c++) {
            tbox_context *ctx = open_cstr("<div><input type='search' value='busca'><input type='number' value='7'></div>", css_cases[c], fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx == NULL) continue;
            tbox_display_list list;
            tbox_context_run_frame(ctx, 900.0, 200.0, &list);
            const tbox_html_node *search = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *number = search->next_sibling;
            const tbox_layout_box *search_box = find_context_box(ctx, search), *number_box = find_context_box(ctx, number);
            TBOX_TEST_ASSERT(search_box != NULL && number_box != NULL);
            if (search_box == NULL || number_box == NULL) { tbox_context_close(ctx); continue; }
            tbox_rect search_before = search_box->border_box, number_before = number_box->border_box;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_BACKSPACE, true, false, false }));
            tbox_context_run_frame(ctx, 900.0, 200.0, &list);
            const tbox_html_attribute *value = tbox_html_node_get_attribute(search, tbox_string_view_make("value", 5));
            TBOX_TEST_ASSERT(value == NULL || value->value.size == 0);
            search_box = find_context_box(ctx, search);
            number_box = find_context_box(ctx, number);
            TBOX_TEST_ASSERT(search_box != NULL && memcmp(&search_box->border_box, &search_before, sizeof(tbox_rect)) == 0);
            TBOX_TEST_ASSERT(number_box != NULL && memcmp(&number_box->border_box, &number_before, sizeof(tbox_rect)) == 0);
            TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, tbox_string_view_make("x", 1)));
            tbox_context_run_frame(ctx, 900.0, 200.0, &list);
            number_box = find_context_box(ctx, number);
            TBOX_TEST_ASSERT(number_box != NULL && memcmp(&number_box->border_box, &number_before, sizeof(tbox_rect)) == 0);
            tbox_context_close(ctx);
        }
    }

    /* The focus ring is the UA border's gray; a document border-color
     * leaves it gray, an outline-color replaces it. */
    {
        const char *sheets[2] = { "input { border-color: rgb(200, 0, 0); }", "input:focus { outline-color: rgb(0, 150, 0); }" };
        for (int s = 0; s < 2; s++) {
            tbox_context *ctx = open_cstr("<div><input value='a'></div>", sheets[s], fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx == NULL) continue;
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child);
            TBOX_TEST_ASSERT(box != NULL && box->style->outline_width == 2.0);
            if (box != NULL) {
                tbox_css_rgba ring = box->style->outline_color;
                if (s == 0) TBOX_TEST_ASSERT(ring.r == 128 && ring.g == 128 && ring.b == 128 && box->style->border_colors[0].r == 200);
                else TBOX_TEST_ASSERT(ring.r == 0 && ring.g == 150 && ring.b == 0);
            }
            tbox_context_close(ctx);
        }
    }

    /* Every focused control shows the focus ring, the range input included
     * (Tab must always show where focus is); checking a checkbox or radio
     * moves neither it nor the text beside it. */
    {
        const char *html = "<p><input type='range'><input type='checkbox'>Aceito <input type='radio'>Sim</p>";
        tbox_context *ctx = open_cstr(html, "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            const tbox_html_node *p        = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *range    = p->first_child;
            const tbox_html_node *checkbox = range->next_sibling;
            const tbox_html_node *radio    = checkbox->next_sibling->next_sibling;
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == range);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            double x, y;
            const tbox_layout_box *range_box = hit_point_for(ctx, range, 600.0, 200.0, &x, &y) ? tbox_context_hit_test(ctx, x, y) : NULL;
            TBOX_TEST_ASSERT(range_box != NULL && range_box->style->outline_width == 2.0 &&
                             range_box->style->outline_style != TBOX_STYLE_BORDER_STYLE_NONE);
            const tbox_html_node *toggles[2] = { checkbox, radio };
            for (size_t t = 0; t < 2; t++) {
                const tbox_layout_box *box = hit_point_for(ctx, toggles[t], 600.0, 200.0, &x, &y) ? tbox_context_hit_test(ctx, x, y) : NULL;
                TBOX_TEST_ASSERT(box != NULL);
                if (box == NULL) continue;
                tbox_rect before = box->border_box;
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, x, y));
                tbox_context_run_frame(ctx, 600.0, 200.0, &list);
                box = tbox_context_hit_test(ctx, x, y);
                TBOX_TEST_ASSERT(tbox_html_node_get_attribute(toggles[t], tbox_string_view_make("checked", 7)) != NULL);
                TBOX_TEST_ASSERT(box != NULL && box->node == toggles[t] && memcmp(&box->border_box, &before, sizeof(tbox_rect)) == 0);
            }
            tbox_context_close(ctx);
        }
    }

    /* ::selection: its background behind the selection and its color on
     * the selected text; without it, the built-in blue. */
    {
        const char *html = "<div><input value='hello'><textarea>world</textarea></div>";
        tbox_context *ctx = open_cstr(html, "input::selection { background-color: rgb(31, 32, 33); color: rgb(34, 35, 36); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            for (int field = 0; field < 2; field++) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_A, true, false, true }));
                tbox_context_run_frame(ctx, 600.0, 200.0, &list);
                bool background = false, text = false, builtin = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    tbox_css_rgba c = op->color;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 31 && c.g == 32 && c.b == 33) background = true;
                    if (op->kind == TBOX_PAINT_FILL_RECT && c.r == 130 && c.g == 175 && c.b == 235) builtin = true;
                    if (op->kind == TBOX_PAINT_TEXT_RUN && c.r == 34 && c.g == 35 && c.b == 36 &&
                        string_view_equal_cstr(op->text, "hello")) text = true;
                }
                if (field == 0) TBOX_TEST_ASSERT(background && text && !builtin);
                else TBOX_TEST_ASSERT(builtin && !background && !text);
            }
            tbox_context_close(ctx);
        }
    }

    /* Textarea soft wrap breaks after spaces (which hang at the line end),
     * and inside a word only when the word alone is wider than the line. */
    {
        const tbox_font_face *face = NULL;
        tbox_context *ctx          = open_cstr("<textarea>x</textarea>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 160.0, &list);
            const tbox_layout_box *box = tbox_context_hit_test(ctx, 10.0, 10.0);
            if (box != NULL)
                face = tbox_font_face_cache_get(fonts, tbox_string_view_make(box->style->font_family, strlen(box->style->font_family)), box->style->font_weight_bold, box->style->font_italic, box->style->font_size);
            tbox_context_close(ctx);
        }
        TBOX_TEST_ASSERT(face != NULL);
        if (face != NULL) {
            /* Room for "aaa bbb" plus a bit, not for "aaa bbb c". */
            double width = tbox_font_measure_text(face, tbox_string_view_make("aaa bbb", 7)) + 1.0;
            char css[96];
            snprintf(css, sizeof(css), "textarea { width: %.2fpx; padding: 0; }", width);
            ctx = open_cstr("<textarea>aaa bbb   ccc abcdefghijklmnopqrstuvwxyz</textarea>", css, fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                const tbox_layout_box *box = tbox_context_hit_test(ctx, 10.0, 10.0);
                TBOX_TEST_ASSERT(box != NULL && box->text_run_count >= 4);
                if (box != NULL && box->text_run_count >= 4) {
                    TBOX_TEST_ASSERT(string_view_equal_cstr(box->text_runs[0].text, "aaa bbb   "));
                    TBOX_TEST_ASSERT(string_view_equal_cstr(box->text_runs[1].text, "ccc "));
                    /* The long word fills whole lines, then the rest. */
                    for (size_t i = 2; i < box->text_run_count; i++) {
                        TBOX_TEST_ASSERT(box->text_runs[i].text.size > 1 && memchr(box->text_runs[i].text.data, ' ', box->text_runs[i].text.size) == NULL);
                        if (i + 1 < box->text_run_count)
                            TBOX_TEST_ASSERT(box->text_runs[i].rect.width <= box->content_box.width);
                    }
                    const tbox_layout_text_run *last = &box->text_runs[box->text_run_count - 1];
                    TBOX_TEST_ASSERT(last->text.data[last->text.size - 1] == 'z');
                }
                tbox_context_close(ctx);
            }
        }
    }

    /* A decorative overlay lets clicks reach the box behind it, while a
     * descendant that explicitly restores auto can still receive clicks. */
    {
        tbox_style overlay_style          = { 0 };
        overlay_style.pointer_events_none = true;
        tbox_style child_style            = { 0 };
        tbox_layout_box root_box = { 0 }, behind = { 0 }, overlay = { 0 }, child = { 0 };
        root_box.border_box  = (tbox_rect){ 0, 0, 100, 100 };
        behind.border_box    = (tbox_rect){ 0, 0, 100, 100 };
        overlay.border_box   = (tbox_rect){ 0, 0, 100, 100 };
        overlay.style        = &overlay_style;
        child.border_box     = (tbox_rect){ 10, 10, 20, 20 };
        child.style          = &child_style;
        root_box.first_child = &behind;
        root_box.last_child  = &overlay;
        behind.next_sibling  = &overlay;
        overlay.first_child = overlay.last_child = &child;
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 50, 50) == &behind);
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 20, 20) == &child);
        overlay_style.pointer_events_none = false;
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(&root_box, 50, 50) == &overlay);
    }

    {
        tbox_context *ctx = open_cstr("<div><button>Under</button><span id='overlay'>Tint</span></div>",
            "div { width: 120px; height: 45px; position: relative; }"
            "button { width: 100px; height: 40px; }"
            "#overlay { display: block; position: absolute; top: 0; left: 0;"
            " width: 100px; height: 40px; pointer-events: none; }",
            fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 100.0, &list);
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 10.0, 10.0);
            TBOX_TEST_ASSERT(hit != NULL && hit->node != NULL && string_view_equal_cstr(hit->node->element.tag_name, "button"));
            tbox_context_close(ctx);
        }
    }

    /* a point inside an inline-block hits its own box (it is a
     * child of the text box around it), and the box sits inside the line. */
    {
        tbox_context *ctx = open_cstr("<p>ab <span>cd</span></p>", "span { display: inline-block; width: 60px; height: 30px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_layout_box *root = tbox_context_hit_test(ctx, 5.0, 20.0); /* inside the <p> */
            while (root != NULL && root->parent != NULL)
                root = root->parent;
            const tbox_layout_box *p    = find_box_by_tag(root, "p");
            const tbox_layout_box *span = p != NULL ? p->first_child : NULL;
            TBOX_TEST_ASSERT(span != NULL && span->node != NULL);
            if (span != NULL) {
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, span->border_box.x + 30.0, span->border_box.y + 15.0);
                TBOX_TEST_ASSERT(hit == span || (hit != NULL && hit->node == NULL && hit->parent == span));
                TBOX_TEST_ASSERT(span->border_box.x > p->content_box.x);
            }
            tbox_context_close(ctx);
        }
    }

    /* The separate theme resolves selectors against picker parts, while
     * the resulting geometry is shared by painting and pointer handling. */
    {
        const char *html             = "<input id='picker' type='color' value='#000000'>";
        const char *theme            = "tbox-popup#picker { width:270px; height:170px; background-color:rgb(10,20,30); border-radius:8px }"
                                       "tbox-popup#picker .track { width:210px; height:16px }";
        tbox_context_options options = tbox_context_options_default();
        options.control_css          = theme;
        options.control_css_length   = strlen(theme);
        tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            const tbox_html_node *input = tbox_context_focused_node(ctx);
            const tbox_layout_box *box  = find_context_box(ctx, input);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0, box->border_box.y + 5.0));
                tbox_context_run_frame(ctx, 400.0, 300.0, &list);
                tbox_rect popup = { 0 };
                for (size_t i = 0; i < list.count; i++) {
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 10 && list.items[i].color.g == 20 && list.items[i].color.b == 30) {
                        popup = list.items[i].rect;
                        TBOX_TEST_ASSERT(list.items[i].corner_radii[0] == 8.0);
                    }
                }
                TBOX_TEST_ASSERT(popup.width == 270.0 && popup.height == 170.0);
                if (popup.width > 0.0) {
                    TBOX_TEST_ASSERT(tbox_context_color_drag(ctx, popup.x + 235.0, popup.y + 20.0));
                    const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
                    TBOX_TEST_ASSERT(value != NULL && !string_view_equal_cstr(value->value, "#000000"));
                }
            }
            tbox_context_close(ctx);
        }
    }
    {
        const char *html             = "<input class='custom' type='date' value='2024-03-02'>";
        const char *theme            = "tbox-popup.custom { background-color:rgb(11,22,33) }"
                                       "tbox-popup[type=date] .day { width:40px; height:30px }";
        tbox_context_options options = tbox_context_options_default();
        options.control_css          = theme;
        options.control_css_length   = strlen(theme);
        tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 440.0, 360.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            const tbox_html_node *input = tbox_context_focused_node(ctx);
            const tbox_layout_box *box  = find_context_box(ctx, input);
            if (box != NULL) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, box->border_box.x + 5.0, box->border_box.y + 5.0));
                tbox_context_run_frame(ctx, 440.0, 360.0, &list);
                tbox_rect popup = { 0 };
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 11 && list.items[i].color.g == 22 && list.items[i].color.b == 33)
                        popup = list.items[i].rect;
                TBOX_TEST_ASSERT(popup.width == 308.0 && popup.height == 248.0);
                if (popup.width > 0.0) {
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, popup.x + 14.0 + 5.0 * 40.0 + 10.0, popup.y + 58.0 + 10.0));
                    const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
                    TBOX_TEST_ASSERT(value != NULL && string_view_equal_cstr(value->value, "2024-03-01"));
                }
            }
            tbox_context_close(ctx);
        }
    }
    {
        const char *html             = "<input class='files' type='file'>";
        const char *theme            = "tbox-popup.files { width:350px; background-color:rgb(12,23,34) }"
                                       "tbox-popup.files .row { height:36px }";
        tbox_context_options options = tbox_context_options_default();
        options.control_css          = theme;
        options.control_css_length   = strlen(theme);
        tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 450.0, 400.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 450.0, 400.0, &list);
            bool found_popup = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 12 && list.items[i].color.g == 23 && list.items[i].color.b == 34 && list.items[i].rect.width == 350.0 && list.items[i].rect.height == 316.0)
                    found_popup = true;
            TBOX_TEST_ASSERT(found_popup);
            tbox_context_close(ctx);
        }
    }
    {
        const char *html             = "<input type='color' value='#000000'>";
        tbox_context_options options = tbox_context_options_default();
        options.control_css          = "";
        options.control_css_length   = 0;
        tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, tbox_context_focused_node(ctx));
            if (box != NULL)
                TBOX_TEST_ASSERT(tbox_context_color_drag(ctx, box->border_box.x + 160.0, box->border_box.y + box->border_box.height + 20.0));
            tbox_context_close(ctx);
        }
    }
    {
        static const struct {
            const char *type;
            double height;
        } pickers[] = {
            { "date",           212.0 },
            { "datetime-local", 306.0 },
            { "month",          166.0 },
            { "time",           126.0 },
            { "week",           212.0 },
        };
        for (size_t picker = 0; picker < sizeof(pickers) / sizeof(pickers[0]); picker++) {
            char html[96];
            char theme[160];
            snprintf(html, sizeof(html), "<input type='%s'>", pickers[picker].type);
            snprintf(theme, sizeof(theme), "tbox-popup[type='%s'] { width:320px; background-color:rgb(13,24,35) }", pickers[picker].type);
            tbox_context_options options = tbox_context_options_default();
            options.control_css          = theme;
            options.control_css_length   = strlen(theme);
            tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx == NULL)
                continue;
            tbox_display_list list;
            tbox_context_run_frame(ctx, 420.0, 420.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 420.0, 420.0, &list);
            bool found = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 13 && list.items[i].color.g == 24 && list.items[i].color.b == 35 && list.items[i].rect.width == 320.0 && list.items[i].rect.height == pickers[picker].height)
                    found = true;
            TBOX_TEST_ASSERT(found);
            tbox_context_close(ctx);
        }
    }
    {
        const char *html             = "<input type='color'>";
        const char *theme            = "tbox-popup[type=color] { width:broken; background-color:rgb(14,25,36) }";
        tbox_context_options options = tbox_context_options_default();
        options.control_css          = theme;
        options.control_css_length   = strlen(theme);
        tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            tbox_context_run_frame(ctx, 320.0, 220.0, &list);
            bool used_fallback = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.r == 14 && list.items[i].color.g == 25 && list.items[i].color.b == 36 && list.items[i].rect.width == 224.0)
                    used_fallback = true;
            TBOX_TEST_ASSERT(used_fallback);
            tbox_context_close(ctx);
        }
    }

    /* Hit testing follows z-index paint order; cursor_at reports the CSS
     * cursor, or pointer/default for `auto` -- plain text keeps the arrow
     * unless `cursor: text` asks for the I-beam. */
    {
        const char *html  = "<div id=\"stage\"><div id=\"top\">a</div><div id=\"under\">b</div><p id=\"para\">ver <a href=\"#\">link</a></p><div id=\"busy\">x</div><div id=\"sel\">abc</div></div>";
        const char *css   = "#stage { position: relative; height: 200px; }"
                            "#top, #under { position: absolute; left: 0; top: 0; width: 100px; height: 50px; }"
                            "#top { z-index: 2; cursor: move; } #under { z-index: 1; }"
                            "#para { position: absolute; left: 0; top: 100px; margin: 0; width: 300px; font-size: 16px; } #busy { position: absolute; left: 200px; top: 0; width: 50px; height: 50px; cursor: wait; }"
                            "#sel { position: absolute; left: 300px; top: 0; width: 50px; height: 50px; cursor: text; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 50.0, 25.0);
            const tbox_html_attribute *id = hit != NULL && hit->node != NULL ? tbox_html_node_get_attribute(hit->node, tbox_string_view_make("id", 2)) : NULL;
            TBOX_TEST_ASSERT(id != NULL && string_view_equal_cstr(id->value, "top")); /* z-index 2 wins although first in the DOM */
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 50.0, 25.0) == TBOX_STYLE_CURSOR_MOVE);
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 220.0, 20.0) == TBOX_STYLE_CURSOR_WAIT);
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 5.0, 108.0) == TBOX_STYLE_CURSOR_DEFAULT);  /* "ver": plain text */
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 305.0, 8.0) == TBOX_STYLE_CURSOR_TEXT);     /* explicit cursor: text */
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 45.0, 108.0) == TBOX_STYLE_CURSOR_POINTER); /* the link */
            TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, 390.0, 290.0) == TBOX_STYLE_CURSOR_DEFAULT);
            tbox_context_close(ctx);
        }
    }

    /* position: sticky holds a header at the scrollport top after
     * scrolling, within its section. */
    {
        const char *html  = "<div id=\"s\"><div class=\"sec\"><h2 id=\"h\">A</h2><p>1</p><p>2</p><p>3</p><p>4</p></div><div class=\"sec\"><h2>B</h2><p>5</p><p>6</p><p>7</p><p>8</p></div></div>";
        const char *css   = "#s { height: 100px; overflow-y: auto; } h2 { position: sticky; top: 0; height: 20px; margin: 0; } p { height: 30px; margin: 0; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 300.0, &list);
            const tbox_layout_box *scroller = tbox_context_hit_test(ctx, 20.0, 60.0);
            while (scroller != NULL && (scroller->style == NULL || scroller->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO))
                scroller = scroller->parent;
            TBOX_TEST_ASSERT(scroller != NULL);
            if (scroller != NULL) {
                double x = scroller->padding_box.x + 5.0, top = scroller->padding_box.y;
                TBOX_TEST_ASSERT(tbox_context_scroll(ctx, x, top + 50.0, 40.0));
                tbox_context_run_frame(ctx, 300.0, 300.0, &list);
                const tbox_layout_box *at_top = tbox_context_hit_test(ctx, x, top + 5.0);
                TBOX_TEST_ASSERT(at_top != NULL && at_top->node != NULL && string_view_equal_cstr(at_top->node->element.tag_name, "h2"));
                if (at_top != NULL)
                    TBOX_TEST_ASSERT(at_top->border_box.y == top); /* held at the scrollport top */
            }
            tbox_context_close(ctx);
        }
    }

    /* Stable gutter reserves space even before overflow; :active follows a
     * pointer press and clears on release. */
    {
        const char *html = "<div id='panel'><button id='press'>Press</button><div id='child'>x</div></div>";
        const char *css = "#panel { width: 100px; height: 70px; overflow-y: auto; scrollbar-gutter: stable; scrollbar-width: thin; }"
                          "#child { width: 100%; } button:active { background-color: red; }";
        tbox_context *ctx = open_cstr(html, css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 120.0, &list);
            const tbox_layout_box *button = tbox_context_hit_test(ctx, 5.0, 5.0);
            TBOX_TEST_ASSERT(button != NULL);
            if (button != NULL) {
                double x = button->border_box.x + 2.0, y = button->border_box.y + 2.0;
                const tbox_layout_box *panel = button;
                while (panel->parent != NULL) panel = panel->parent;
                TBOX_TEST_ASSERT(panel->scrollbar_gutter == 6.0 && panel->content_box.width == 94.0 && panel->border_box.width == 100.0);
                TBOX_TEST_ASSERT(tbox_context_set_active_at(ctx, true, x, y));
                tbox_context_run_frame(ctx, 200.0, 120.0, &list);
                button = tbox_context_hit_test(ctx, x, y);
                TBOX_TEST_ASSERT(button != NULL && button->style != NULL && button->style->background_color.r == 255 && button->style->background_color.g == 0);
                TBOX_TEST_ASSERT(tbox_context_set_active_at(ctx, false, 0.0, 0.0));
                tbox_context_run_frame(ctx, 200.0, 120.0, &list);
                button = tbox_context_hit_test(ctx, x, y);
                TBOX_TEST_ASSERT(button != NULL && button->style != NULL && button->style->background_color.r != 255);
            }
            tbox_context_close(ctx);
        }
    }

    /* A media breakpoint changes the target; transition paints intermediate
     * values on subsequent frames and finishes at the resolved color. */
    {
        const char *css = "#box { width: 80px; height: 30px; background-color: red; transition: background-color 1s linear; }"
                          "@media screen and (min-width: 500px) { #box { background-color: blue; }"
                          "@media (orientation: landscape) { #box { color: green; } } }";
        tbox_context *ctx = open_cstr("<div id='box'>x</div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_set_animation_time(ctx, 0.0);
            tbox_context_run_frame(ctx, 400.0, 200.0, &list);
            const tbox_layout_box *box = tbox_context_hit_test(ctx, 5.0, 5.0);
            while (box != NULL && box->parent != NULL) box = box->parent;
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.r == 255 && box->style->background_color.b == 0);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            box = tbox_context_hit_test(ctx, 5.0, 5.0);
            while (box != NULL && box->parent != NULL) box = box->parent;
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.r == 255 && tbox_context_animations_active(ctx));
            TBOX_TEST_ASSERT(box != NULL && box->style->color.g == 128);
            tbox_context_set_animation_time(ctx, 0.5);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            box = tbox_context_hit_test(ctx, 5.0, 5.0);
            while (box != NULL && box->parent != NULL) box = box->parent;
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.r >= 126 && box->style->background_color.r <= 129 && box->style->background_color.b >= 126);
            tbox_context_set_animation_time(ctx, 1.0);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            box = tbox_context_hit_test(ctx, 5.0, 5.0);
            while (box != NULL && box->parent != NULL) box = box->parent;
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.b == 255 && !tbox_context_animations_active(ctx));
            tbox_context_close(ctx);
        }
    }

    /* tbox_context_next_frame_time: idle is INFINITY; a transition-delay or
     * animation-delay is a deadline at its end, not a frame every tick;
     * a running one asks for the next frame; a finished one goes idle. */
    {
        const char *css = "#box { width: 80px; height: 30px; background-color: red; transition: background-color 1s linear 0.5s; }"
                          "@media (min-width: 500px) { #box { background-color: blue; } }";
        tbox_context *ctx = open_cstr("<div id='box'>x</div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_set_animation_time(ctx, 0.0);
            tbox_context_run_frame(ctx, 400.0, 200.0, &list);
            TBOX_TEST_ASSERT(tbox_context_next_frame_time(ctx) == INFINITY && !tbox_context_animations_active(ctx));
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            TBOX_TEST_ASSERT(fabs(tbox_context_next_frame_time(ctx) - 0.5) < 1e-9 && tbox_context_animations_active(ctx));
            tbox_context_set_animation_time(ctx, 0.75);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            TBOX_TEST_ASSERT(tbox_context_next_frame_time(ctx) <= 0.75);
            tbox_context_set_animation_time(ctx, 1.5);
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            TBOX_TEST_ASSERT(tbox_context_next_frame_time(ctx) == INFINITY);
            tbox_context_close(ctx);
        }
        TBOX_TEST_ASSERT(tbox_context_next_frame_time(NULL) == INFINITY);
    }
    /* Transition states exist only for elements that declare a transition
     * (docs/dev/decisoes.md): a hidden one waits without animating; losing
     * `transition` or leaving the document frees the state and ends what
     * was running; gaining `transition` along with the change does not
     * animate it. */
    {
        const char *css = "#w > div { width: 20px; height: 20px; background-color: red; }"
                          "#t { transition: background-color 1s linear; } #t.blue { background-color: blue; } #t.plain { transition: none; }"
                          "#h { display: none; transition: background-color 1s linear; }"
                          "#g.blue { background-color: blue; transition: background-color 1s linear; }";
        tbox_context *ctx = open_cstr("<div id='w'><div id='t'></div><div id='h'></div><div id='g'></div><div></div><div></div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_html_document *document = tbox_context_document(ctx);
            tbox_html_node *t = (tbox_html_node *)tbox_html_document_root(document)->first_child->first_child;
            tbox_html_node *h = t->next_sibling, *g = h->next_sibling;
            tbox_string_view class_name = tbox_string_view_make("class", 5);
            size_t states;
            tbox_display_list list;
            tbox_context_set_animation_time(ctx, 0.0);
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            states = 0;
            for (const tbox_transition_state *state = ctx->transitions; state != NULL; state = state->next) states++;
            TBOX_TEST_ASSERT(states == 2 && !tbox_context_animations_active(ctx));

            tbox_html_node_set_attribute(document, t, class_name, tbox_string_view_make("blue", 4));
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            tbox_context_set_animation_time(ctx, 0.5);
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, t);
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.b >= 126 && box->style->background_color.b <= 129 && tbox_context_animations_active(ctx));

            tbox_html_node_set_attribute(document, t, class_name, tbox_string_view_make("blue plain", 10));
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            box = find_context_box(ctx, t);
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.b == 255 && !tbox_context_animations_active(ctx));

            tbox_html_node_set_attribute(document, g, class_name, tbox_string_view_make("blue", 4));
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            box = find_context_box(ctx, g);
            TBOX_TEST_ASSERT(box != NULL && box->style->background_color.b == 255 && !tbox_context_animations_active(ctx));
            states = 0;
            for (const tbox_transition_state *state = ctx->transitions; state != NULL; state = state->next) states++;
            TBOX_TEST_ASSERT(states == 2);

            tbox_html_node_remove(g);
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            TBOX_TEST_ASSERT(ctx->transitions != NULL && ctx->transitions->node == h && ctx->transitions->next == NULL);
            tbox_context_close(ctx);
        }
    }
    {
        const char *css = "@keyframes fade { from { opacity: 0; } to { opacity: 1; } }"
                          "#a { width: 20px; height: 20px; background: red; animation: fade 1s linear 2s; }";
        tbox_context *ctx = open_cstr("<div id='a'></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_set_animation_time(ctx, 10.0);
            tbox_context_run_frame(ctx, 100.0, 100.0, &list);
            TBOX_TEST_ASSERT(fabs(tbox_context_next_frame_time(ctx) - 12.0) < 1e-9);
            tbox_context_set_animation_time(ctx, 12.5);
            tbox_context_run_frame(ctx, 100.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_next_frame_time(ctx) <= 12.5);
            tbox_context_set_animation_time(ctx, 13.5);
            tbox_context_run_frame(ctx, 100.0, 100.0, &list);
            TBOX_TEST_ASSERT(tbox_context_next_frame_time(ctx) == INFINITY);
            tbox_context_close(ctx);
        }
    }

    /* @font-face registers a local font and the layout uses that exact face. */
    {
        const char *css = "@font-face { font-family: 'Fixture Font'; src: url('../../external/liberation-sans/LiberationSans-Regular.ttf'); }"
                          "#box { font-family: 'Fixture Font'; font-size: 19px; }";
        tbox_context_options options = tbox_context_options_default();
        options.asset_base_dir = TBOX_TEST_ASSETS_DIR;
        tbox_context *ctx = tbox_context_open_with_options("<div id='box'>Font fixture</div>", strlen("<div id='box'>Font fixture</div>"), css, strlen(css), fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            const tbox_font_face *registered = tbox_font_face_cache_get(fonts, tbox_string_view_make("Fixture Font", 12), false, false, 19.0);
            TBOX_TEST_ASSERT(registered != NULL);
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            bool used = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && list.items[i].face == registered) used = true;
            TBOX_TEST_ASSERT(used);
            tbox_context_close(ctx);
        }
    }

    /* The caller's reduced-motion preference participates in @media. */
    {
        const char *html = "<div id='box'>motion</div>";
        const char *css = "#box { color: red; transition: color 1s; }"
                          "@media (prefers-reduced-motion: reduce) { #box { color: green; transition: none; } }";
        tbox_context_options options = tbox_context_options_default();
        options.prefers_reduced_motion = true;
        tbox_context *ctx = tbox_context_open_with_options(html, strlen(html), css, strlen(css), fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            const tbox_layout_box *box = tbox_context_hit_test(ctx, 5.0, 5.0);
            while (box != NULL && box->parent != NULL) box = box->parent;
            TBOX_TEST_ASSERT(box != NULL && box->style->color.g == 128 && tbox_style_transition_find(box->style, TBOX_STYLE_ANIM_COLOR) == NULL);
            tbox_context_close(ctx);
        }
    }

    /* Transitions on layout and paint properties: width re-lays out every
     * frame, border colors follow steps(), translate follows the
     * cubic-bezier, and a later class change retargets mid-flight. */
    {
        const char *css = "#box { width: 100px; height: 20px; border: 2px solid rgb(255, 0, 0);"
                          " transition: width .5s linear, border-color 1s steps(2), transform 1s linear; }"
                          "#box.on { width: 200px; border-color: rgb(0, 0, 255); transform: translateX(40px); }";
        tbox_context *ctx = open_cstr("<div><div id='box'>x</div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_html_node *node = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_context_set_animation_time(ctx, 0.0);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            tbox_html_node_set_attribute(tbox_context_document(ctx), node, tbox_string_view_make("class", 5), tbox_string_view_make("on", 2));
            tbox_context_set_animation_time(ctx, 1.0);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - 100.0) < 0.01 && tbox_context_animations_active(ctx));
            tbox_context_set_animation_time(ctx, 1.25);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - 150.0) < 0.01);
            TBOX_TEST_ASSERT(box != NULL && tbox_style_border_side_color(box->style, 3).r == 255 && tbox_style_border_side_color(box->style, 3).b == 0);
            TBOX_TEST_ASSERT(box != NULL && tbox_style_transforms_of(box->style)->functions[0].kind == TBOX_STYLE_TRANSFORM_TRANSLATE && fabs(tbox_style_transforms_of(box->style)->functions[0].v[1] - 10.0) < 0.01);
            tbox_context_set_animation_time(ctx, 1.5);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            box = find_context_box(ctx, node);
            tbox_css_rgba mid = box != NULL ? tbox_style_border_side_color(box->style, 0) : (tbox_css_rgba){ 0, 0, 0, 0 };
            TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - 200.0) < 0.01 && mid.r >= 126 && mid.r <= 129 && mid.b >= 126 && mid.b <= 129);
            /* back mid-flight: translate reverses from where it is */
            tbox_html_node_remove_attribute(node, tbox_string_view_make("class", 5));
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && fabs(tbox_style_transforms_of(box->style)->functions[0].v[1] - 20.0) < 0.01);
            tbox_context_set_animation_time(ctx, 3.0);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && tbox_style_transforms_of(box->style)->functions[0].kind == TBOX_STYLE_TRANSFORM_NONE && fabs(box->content_box.width - 100.0) < 0.01);
            TBOX_TEST_ASSERT(!tbox_context_animations_active(ctx));
            tbox_context_close(ctx);
        }
    }

    /* Transforms: a quarter turn keeps the layout geometry and paints on
     * a transformed layer, the pointer hits what is drawn (not the
     * untransformed box), the transformed box contains its absolute and
     * fixed descendants, and a translation still just moves the box. */
    {
        const char *css = "body { margin: 0; }"
                          "#turn { margin: 100px 0 0 100px; width: 100px; height: 20px; background: red; transform: rotate(90deg); }"
                          "#abs { position: absolute; left: 5px; top: 0; width: 10px; height: 10px; }"
                          "#fix { position: fixed; left: 0; top: 0; width: 4px; height: 4px; }"
                          "#move { width: 10px; height: 10px; translate: 30px 0; }";
        tbox_context *ctx = open_cstr("<div><div id='turn'><i id='abs'></i><b id='fix'></b></div><p id='move'></p></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_html_node *turn = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *box = find_context_box(ctx, turn);
            TBOX_TEST_ASSERT(box != NULL && box->has_transform && box->border_box.x == 100.0 && box->border_box.y == 100.0);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->transform[0]) < 1e-12 && box->transform[1] == 1.0 && fabs(box->transform[4] - 260.0) < 1e-9 && fabs(box->transform[5] + 40.0) < 1e-9);
            /* drawn as a 20x100 bar centered on (150, 110) */
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 150.0, 70.0) == box && tbox_context_hit_test(ctx, 150.0, 150.0) == box);
            TBOX_TEST_ASSERT(tbox_context_hit_test(ctx, 110.0, 110.0) != box && tbox_context_hit_test(ctx, 190.0, 110.0) != box);
            /* absolute and fixed children sit against the turned box */
            const tbox_layout_box *abs = box != NULL ? box->first_child : NULL;
            TBOX_TEST_ASSERT(abs != NULL && abs->border_box.x == 105.0 && abs->border_box.y == 100.0);
            TBOX_TEST_ASSERT(abs != NULL && abs->next_sibling != NULL && abs->next_sibling->border_box.x == 100.0 && abs->next_sibling->border_box.y == 100.0);
            /* the transform layer wraps the box; nothing inside is clipped from outside */
            size_t begin = list.count;
            for (size_t i = 0; i < list.count && begin == list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_LAYER_BEGIN && list.items[i].has_transform) begin = i;
            TBOX_TEST_ASSERT(begin < list.count && list.items[begin + 1].kind == TBOX_PAINT_FILL_RECT && list.items[begin + 1].rect.width == 100.0);
            const tbox_layout_box *move = find_context_box(ctx, turn->next_sibling);
            TBOX_TEST_ASSERT(move != NULL && !move->has_transform && move->border_box.x == 30.0);
            tbox_context_close(ctx);
        }
    }

    /* A translation alone is a stacking context too: a negative z-index
     * child paints over its translated parent's background. */
    {
        const char *css = "body { margin: 0; } #t { translate: 5px; width: 50px; height: 50px; background: rgb(255, 0, 0); }"
                          "#n { position: relative; z-index: -1; width: 10px; height: 10px; background: rgb(0, 0, 255); }";
        tbox_context *ctx = open_cstr("<div><div id='t'><div id='n'></div></div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 100.0, &list);
            size_t red = list.count, blue = list.count;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind != TBOX_PAINT_FILL_RECT) continue;
                if (list.items[i].color.r == 255 && list.items[i].color.b == 0) red = i;
                if (list.items[i].color.b == 255 && list.items[i].color.r == 0) blue = i;
            }
            TBOX_TEST_ASSERT(red < blue && blue < list.count && list.items[red].rect.x == 5.0);
            tbox_context_close(ctx);
        }
    }

    /* A turned box inside a scroll container keeps turning around its own
     * center once scrolled: the pointer follows it. */
    {
        const char *css = "body { margin: 0; } #sc { width: 200px; height: 100px; overflow-y: auto; }"
                          "#r { margin-left: 50px; width: 100px; height: 20px; transform: rotate(90deg); }";
        tbox_context *ctx = open_cstr("<div><div id='sc'><div style='height: 80px'></div><div id='r'></div><div style='height: 200px'></div></div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            const tbox_html_node *r = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child->first_child->next_sibling;
            const tbox_layout_box *box = find_context_box(ctx, r);
            TBOX_TEST_ASSERT(box != NULL && tbox_context_hit_test(ctx, 100.0, 60.0) == box && tbox_context_hit_test(ctx, 100.0, 20.0) != box);
            tbox_context_scroll(ctx, 100.0, 50.0, 50.0);
            tbox_context_run_frame(ctx, 400.0, 300.0, &list);
            box = find_context_box(ctx, r);
            TBOX_TEST_ASSERT(box != NULL && box->border_box.y == 30.0);
            TBOX_TEST_ASSERT(box != NULL && tbox_context_hit_test(ctx, 100.0, 20.0) == box && tbox_context_hit_test(ctx, 60.0, 40.0) != box);
            tbox_context_close(ctx);
        }
    }

    /* backdrop-filter: one op over the border box, after what is behind
     * it and before the element's own translucent background. */
    {
        const char *css = "body { margin: 0; background: rgb(255, 0, 0); }"
                          "#glass { width: 50px; height: 20px; border-radius: 4px; background: rgba(255, 255, 255, 0.25); backdrop-filter: grayscale(1); }";
        tbox_context *ctx = open_cstr("<body><div id='glass'></div></body>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 200.0, 100.0, &list);
            size_t backdrop = list.count, glass_fill = list.count;
            for (size_t i = 0; i < list.count; i++) {
                if (list.items[i].kind == TBOX_PAINT_BACKDROP_FILTER && backdrop == list.count) backdrop = i;
                if (list.items[i].kind == TBOX_PAINT_FILL_RECT && list.items[i].color.a == 64 && glass_fill == list.count) glass_fill = i;
            }
            TBOX_TEST_ASSERT(backdrop < list.count && glass_fill < list.count && backdrop < glass_fill);
            TBOX_TEST_ASSERT(backdrop < list.count && list.items[backdrop].rect.width == 50.0 && list.items[backdrop].corner_radii[0] == 4.0);
            tbox_context_close(ctx);
        }
    }

    /* @keyframes: implicit 0% from the element's own value, a per-keyframe
     * timing function, alternate iterations, fill-mode forwards, pause,
     * and removal restoring the base style. */
    {
        const char *css = "#box { width: 100px; height: 10px; opacity: 1; background: rgb(255, 0, 0); }"
                          "#box.go { animation: grow 1s linear 2 alternate forwards; }"
                          "#box.hold { animation: grow 1s linear 2 alternate forwards paused; }"
                          "@keyframes grow { 50% { width: 150px; animation-timing-function: steps(1, end); } to { width: 200px; opacity: 0.5; } }"
                          "#dot { width: 10px; height: 10px; animation: blink 2s steps(2) infinite 1s backwards; }"
                          "@keyframes blink { from { background-color: rgb(0, 0, 255); } to { background-color: rgb(0, 255, 0); } }";
        tbox_context *ctx = open_cstr("<div><div id='box'>x</div><div id='dot'></div></div>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_html_node *node = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_html_node *dot = node->next_sibling;
            tbox_context_set_animation_time(ctx, 10.0);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            const tbox_layout_box *d = find_context_box(ctx, dot);
            /* before its delay: backwards fill shows the from keyframe */
            TBOX_TEST_ASSERT(d != NULL && d->style->background_color.b == 255 && tbox_context_animations_active(ctx));
            tbox_html_node_set_attribute(tbox_context_document(ctx), node, tbox_string_view_make("class", 5), tbox_string_view_make("go", 2));
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            const double times[6] = { 10.25, 10.75, 11.25, 11.75, 12.5, 13.0 };
            const double widths[6] = { 125.0, 150.0, 150.0, 125.0, 100.0, 100.0 };
            for (int f = 0; f < 6; f++) {
                tbox_context_set_animation_time(ctx, times[f]);
                tbox_context_run_frame(ctx, 400.0, 100.0, &list);
                const tbox_layout_box *box = find_context_box(ctx, node);
                TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - widths[f]) < 0.01);
            }
            d = find_context_box(ctx, dot);
            /* 13s: 2s into the 2s-long steps(2) cycle that started at 11s */
            TBOX_TEST_ASSERT(d != NULL && d->style->background_color.b == 255);
            tbox_context_set_animation_time(ctx, 14.0);
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            d = find_context_box(ctx, dot);
            TBOX_TEST_ASSERT(d != NULL && d->style->background_color.g >= 127 && d->style->background_color.g <= 128 && d->style->background_color.b >= 127);
            /* pause freezes the progress; the forwards fill then holds */
            tbox_html_node_set_attribute(tbox_context_document(ctx), node, tbox_string_view_make("class", 5), tbox_string_view_make("hold", 4));
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - 100.0) < 0.01);
            tbox_html_node_remove_attribute(node, tbox_string_view_make("class", 5));
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            box = find_context_box(ctx, node);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->content_box.width - 100.0) < 0.01 && box->style->opacity == 1.0);
            tbox_context_close(ctx);
        }
    }

    /* scroll-snap: a wheel gesture snaps once idle, gliding to the nearest
     * snap point (mandatory); proximity leaves far points alone; an
     * `always` stop between the start and the end catches the gesture. */
    {
        const char *modes[3] = { "y mandatory", "y proximity", "y mandatory" };
        const double deltas[3] = { 130.0, 40.0, 250.0 }, expected[3] = { 100.0, 40.0, 100.0 };
        for (int m = 0; m < 3; m++) {
            char css[512];
            snprintf(css, sizeof(css), "body { margin: 0; } #list { height: 100px; overflow-y: auto; scroll-snap-type: %s; }"
                     " #list div { height: 100px; scroll-snap-align: start; %s }", modes[m], m == 2 ? "scroll-snap-stop: always;" : "");
            tbox_context *ctx = open_cstr("<body><div id='list'><div>a</div><div>b</div><div>c</div><div>d</div></div></body>", css, fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx == NULL) continue;
            tbox_display_list list;
            tbox_html_node *first = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child->first_child;
            tbox_context_set_animation_time(ctx, 0.0);
            tbox_context_run_frame(ctx, 200.0, 300.0, &list);
            TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 10.0, 50.0, deltas[m]));
            tbox_context_set_animation_time(ctx, 0.05);
            tbox_context_run_frame(ctx, 200.0, 300.0, &list);
            const tbox_layout_box *box = find_context_box(ctx, first);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->border_box.y + deltas[m]) < 0.01 && tbox_context_animations_active(ctx));
            /* waiting for the wheel to stop is a deadline, not a frame per tick */
            TBOX_TEST_ASSERT(fabs(tbox_context_next_frame_time(ctx) - 0.2) < 1e-9);
            tbox_context_set_animation_time(ctx, 0.25);
            tbox_context_run_frame(ctx, 200.0, 300.0, &list);
            tbox_context_set_animation_time(ctx, 0.6);
            tbox_context_run_frame(ctx, 200.0, 300.0, &list);
            box = find_context_box(ctx, first);
            TBOX_TEST_ASSERT(box != NULL && fabs(box->border_box.y + expected[m]) < 0.01);
            TBOX_TEST_ASSERT(!tbox_context_animations_active(ctx));
            tbox_context_close(ctx);
        }
    }

    /* direction: rtl -- a flex row starts at the right edge (and
     * row-reverse at the left); justify-content: start is the right. */
    {
        const char *css = "body { margin: 0; } .row { display: flex; width: 300px; direction: rtl; }"
                          ".row div { width: 50px; height: 10px; } .rev { flex-direction: row-reverse; }";
        tbox_context *ctx = open_cstr("<body><div class='row'><div>a</div><div>b</div></div><div class='row rev'><div>c</div><div>d</div></div></body>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 100.0, &list);
            tbox_html_node *row = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_layout_box *a = find_context_box(ctx, row->first_child), *b = find_context_box(ctx, row->first_child->next_sibling);
            const tbox_layout_box *c = find_context_box(ctx, row->next_sibling->first_child), *d = find_context_box(ctx, row->next_sibling->first_child->next_sibling);
            TBOX_TEST_ASSERT(a != NULL && b != NULL && a->border_box.x == 250.0 && b->border_box.x == 200.0);
            TBOX_TEST_ASSERT(c != NULL && d != NULL && c->border_box.x == 0.0 && d->border_box.x == 50.0);
            tbox_context_close(ctx);
        }
    }

    /* text-emphasis: one mark per letter (not spaces or punctuation),
     * above the text, centered on its glyph; strings use a half-size face. */
    {
        const char *css = "body { margin: 20px; font-size: 20px; } p { margin: 0; text-emphasis: filled dot rgb(1, 2, 3); }"
                          "p.s { text-emphasis: '*' rgb(1, 2, 3); }";
        tbox_context *ctx = open_cstr("<body><p>ab, c</p><p class='s'>xy</p></body>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 200.0, &list);
            size_t dots = 0, strings = 0;
            double first_x = -1.0, text_y = -1.0, mark_y = -1.0;
            for (size_t k = 0; k < list.count; k++) {
                const tbox_paint_op *op = &list.items[k];
                if (op->color.r != 1 || op->color.g != 2 || op->color.b != 3) {
                    if (op->kind == TBOX_PAINT_TEXT_RUN && text_y < 0.0) text_y = op->rect.y;
                    continue;
                }
                if (op->kind == TBOX_PAINT_TEXT_RUN) {
                    strings++;
                    TBOX_TEST_ASSERT(op->face != NULL && tbox_font_face_line_height(op->face) < 15.0);
                } else {
                    if (dots == 0) first_x = op->rect.x + op->rect.width / 2.0, mark_y = op->rect.y + op->rect.height / 2.0;
                    dots++;
                }
            }
            TBOX_TEST_ASSERT(dots == 3 && strings == 2);
            /* 'a' is about 11px wide at 20px: its mark sits near x = 20 + 5.5 */
            TBOX_TEST_ASSERT(first_x > 23.0 && first_x < 28.0 && mark_y < text_y);
            tbox_context_close(ctx);
        }
    }

    /* Compositing layers from CSS: a blurred element is wrapped in
     * LAYER_BEGIN/END carrying the filter, with clip-path on the composite
     * (not on the ops inside); a mask adds LAYER_MASK; the pixels show the
     * blur bleeding out and the mask cutting the content. */
    {
        const char *css = "body { margin: 0; background: white; }"
                          "#soft { width: 40px; height: 40px; background: black; filter: blur(4px); clip-path: inset(0 20px 0 0); }"
                          "#cut { width: 40px; height: 20px; background: rgb(255, 0, 0); mask: linear-gradient(90deg, black 50%, transparent 50%) luminance; mask-mode: alpha; }"
                          "#mix { width: 40px; height: 20px; background: rgb(0, 255, 255); mix-blend-mode: multiply; }"
                          "#host { background: rgb(255, 255, 0); }";
        tbox_context *ctx = open_cstr("<body><div id='soft'></div><div id='cut'></div><div id='host'><div id='mix'></div></div></body>", css, fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 120.0, 100.0, &list);
            size_t begins = 0, ends = 0, masks = 0;
            const tbox_paint_op *blur_begin = NULL;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_LAYER_BEGIN) {
                    begins++;
                    if (op->filters != NULL && blur_begin == NULL) {
                        blur_begin = op;
                        TBOX_TEST_ASSERT(i + 1 < list.count && list.items[i + 1].kind == TBOX_PAINT_FILL_RECT && !list.items[i + 1].has_rounded_clip);
                    }
                }
                if (op->kind == TBOX_PAINT_LAYER_END) ends++;
                if (op->kind == TBOX_PAINT_LAYER_MASK) {
                    masks++;
                    TBOX_TEST_ASSERT(!op->mask_luminance); /* mask-mode wins over the shorthand */
                }
            }
            TBOX_TEST_ASSERT(begins == 3 && ends == 3 && masks == 1);
            TBOX_TEST_ASSERT(blur_begin != NULL && blur_begin->filters->ops[0].kind == TBOX_STYLE_FILTER_BLUR && blur_begin->has_rounded_clip && blur_begin->rounded_clip.width == 20.0);
            uint32_t *pixels = malloc(120 * 100 * sizeof(uint32_t));
            TBOX_TEST_ASSERT(pixels != NULL);
            if (pixels != NULL) {
                for (size_t i = 0; i < 120 * 100; i++) pixels[i] = 0xFFFFFFFFu;
                tbox_raster_display_list(pixels, 120, 100, &list);
                /* blur: dark inside; the clip-path cuts the composite at
                 * x = 20, so the halo at x = 25 is gone */
                TBOX_TEST_ASSERT(((pixels[20 * 120 + 10] >> 16) & 0xFFu) < 60 && pixels[20 * 120 + 25] == 0xFFFFFFFFu);
                /* the edge at y = 0 is blurred: not fully dark */
                TBOX_TEST_ASSERT(((pixels[0 * 120 + 10] >> 16) & 0xFFu) > 60);
                /* mask: red on the left half, white on the right */
                TBOX_TEST_ASSERT(pixels[50 * 120 + 5] == 0xFFFF0000u && pixels[50 * 120 + 35] == 0xFFFFFFFFu);
                /* multiply: cyan over the yellow host is green */
                TBOX_TEST_ASSERT(pixels[70 * 120 + 5] == 0xFF00FF00u);
                free(pixels);
            }
            tbox_context_close(ctx);
        }
    }

    /* Committing a date the value format can't hold (5-digit year, reached
     * by navigating the calendar) is refused instead of writing a truncated
     * value; 4-digit years still commit, for date, month and week inputs. */
    {
        tbox_context *ctx = open_cstr("<div><input type='date' value='2024-05-06'><input type='month'><input type='week'></div>", "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_html_node *date = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            tbox_html_node *month = date->next_sibling, *week = month->next_sibling;
            tbox_string_view value_name = tbox_string_view_make("value", 5);
            TBOX_TEST_ASSERT(!tbox_context_date_commit(ctx, date, (tbox_date){ 10000, 1, 15 }));
            TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_html_node_get_attribute(date, value_name)->value, "2024-05-06"));
            TBOX_TEST_ASSERT(!tbox_context_date_commit(ctx, week, (tbox_date){ 10000, 1, 15 }));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(week, value_name) == NULL);
            TBOX_TEST_ASSERT(tbox_context_date_commit(ctx, date, (tbox_date){ 9999, 12, 31 }));
            TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_html_node_get_attribute(date, value_name)->value, "9999-12-31"));
            TBOX_TEST_ASSERT(tbox_context_date_commit(ctx, month, (tbox_date){ 2025, 3, 1 }));
            TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_html_node_get_attribute(month, value_name)->value, "2025-03"));
            TBOX_TEST_ASSERT(tbox_context_date_commit(ctx, week, (tbox_date){ 2025, 3, 5 }));
            TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_html_node_get_attribute(week, value_name)->value, "2025-W10"));
            tbox_context_close(ctx);
        }
    }

    /* details keeps only its first summary in layout while closed; Enter and
     * pointer activation toggle open, including keyboard focus traversal. */
    {
        const char *html = "<div><details><summary>Mostrar</summary><p>Conteúdo secreto</p>"
            "<button>Dentro</button></details><button>Depois</button></div>";
        tbox_context *ctx = open_cstr(html, "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 360.0, 240.0, &list);
            const tbox_html_node *root = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *details = root->first_child;
            const tbox_html_node *summary = details->first_child;
            const tbox_html_node *content = summary->next_sibling;
            TBOX_TEST_ASSERT(find_context_box(ctx, summary) != NULL);
            TBOX_TEST_ASSERT(find_context_box(ctx, content) == NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == summary);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
            TBOX_TEST_ASSERT(tbox_html_node_get_attribute(details, tbox_string_view_make("open", 4)) != NULL);
            tbox_context_run_frame(ctx, 360.0, 240.0, &list);
            TBOX_TEST_ASSERT(find_context_box(ctx, content) != NULL);
            const tbox_layout_box *summary_box = find_context_box(ctx, summary);
            TBOX_TEST_ASSERT(summary_box != NULL);
            if (summary_box != NULL) {
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, summary_box->border_box.x + 30.0,
                    summary_box->border_box.y + summary_box->border_box.height / 2.0));
                TBOX_TEST_ASSERT(tbox_html_node_get_attribute(details, tbox_string_view_make("open", 4)) == NULL);
            }
            tbox_context_run_frame(ctx, 360.0, 240.0, &list);
            TBOX_TEST_ASSERT(find_context_box(ctx, content) == NULL);
            TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_TAB, true, false, false }));
            TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == root->last_child);
            tbox_context_close(ctx);
        }
    }
    {
        tbox_context *ctx = open_cstr("<div><details><summary>Título</summary><span>Oculto</span></details></div>",
            "summary { display: inline-block; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 100.0, &list);
            bool hidden_text_painted = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    string_view_equal_cstr(list.items[i].text, "Oculto")) hidden_text_painted = true;
            TBOX_TEST_ASSERT(!hidden_text_painted);
            tbox_context_close(ctx);
        }
    }

    /* A fieldset places its first legend across the top border. The border
     * is split around the legend instead of painting through its text. */
    {
        const char *html = "<div><fieldset><legend>Contato</legend><p>Nome</p></fieldset></div>";
        tbox_context *ctx = open_cstr(html, "", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 400.0, 180.0, &list);
            const tbox_html_node *fieldset = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
            const tbox_html_node *legend = fieldset->first_child;
            const tbox_layout_box *fieldset_box = find_context_box(ctx, fieldset);
            const tbox_layout_box *legend_box = find_context_box(ctx, legend);
            TBOX_TEST_ASSERT(fieldset_box != NULL && legend_box != NULL);
            if (fieldset_box != NULL && legend_box != NULL) {
                TBOX_TEST_ASSERT(legend_box->border_box.y < fieldset_box->border_box.y);
                TBOX_TEST_ASSERT(legend_box->border_box.y + legend_box->border_box.height > fieldset_box->border_box.y);
                double center = legend_box->border_box.x + legend_box->border_box.width / 2.0;
                bool border_through_legend = false, saw_border = false;
                for (size_t i = 0; i < list.count; i++) {
                    const tbox_paint_op *op = &list.items[i];
                    if (op->kind != TBOX_PAINT_FILL_RECT || op->color.r != 128 ||
                        op->rect.y != fieldset_box->border_box.y || op->rect.height != 1.0) continue;
                    saw_border = true;
                    if (op->rect.x <= center && center < op->rect.x + op->rect.width)
                        border_through_legend = true;
                }
                TBOX_TEST_ASSERT(saw_border && !border_through_legend);
            }
            tbox_context_close(ctx);
        }
    }

    /* Passive gauges paint from current DOM attributes; output remains text
     * and can be updated through the existing DOM mutation API. */
    {
        const char *html = "<div><p>Result: <output>25%</output></p>"
            "<progress value='25' max='100'>fallback progress</progress>"
            "<meter min='0' max='100' value='90' low='20' high='80'>fallback meter</meter>"
            "<progress>fallback pending</progress></div>";
        tbox_context *ctx = open_cstr(html, "meter { height: 40px; } progress { height: 24px; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 500.0, 200.0, &list);
            tbox_html_node *root = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child;
            tbox_html_node *output = root->first_child->first_child->next_sibling;
            tbox_html_node *progress = root->first_child->next_sibling;
            const tbox_html_node *meter = progress->next_sibling;
            const tbox_html_node *pending = meter->next_sibling;
            const tbox_layout_box *progress_box = find_context_box(ctx, progress);
            const tbox_layout_box *meter_box = find_context_box(ctx, meter);
            const tbox_layout_box *pending_box = find_context_box(ctx, pending);
            TBOX_TEST_ASSERT(progress_box != NULL && meter_box != NULL && pending_box != NULL);
            TBOX_TEST_ASSERT(progress_box != NULL && progress_box->content_box.height == 24.0);
            TBOX_TEST_ASSERT(meter_box != NULL && meter_box->content_box.height == 40.0);
            bool output_text = false, fallback_text = false, progress_fill = false, meter_fill = false, pending_fill = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_TEXT_RUN) {
                    if (string_view_equal_cstr(op->text, "25%")) output_text = true;
                    if (string_view_equal_cstr(op->text, "fallback")) fallback_text = true;
                }
                if (op->kind == TBOX_PAINT_FILL_RECT && progress_box != NULL &&
                    op->rect.x == progress_box->content_box.x && op->rect.y == progress_box->content_box.y &&
                    op->rect.width == progress_box->content_box.width * 0.25)
                    progress_fill = true;
                if (op->kind == TBOX_PAINT_FILL_RECT && meter_box != NULL &&
                    op->rect.x == meter_box->content_box.x && op->rect.y == meter_box->content_box.y &&
                    op->rect.width == meter_box->content_box.width * 0.9 &&
                    op->rect.height == 40.0 &&
                    op->color.r == 205 && op->color.g == 150)
                    meter_fill = true;
                if (op->kind == TBOX_PAINT_FILL_RECT && pending_box != NULL &&
                    op->rect.x == pending_box->content_box.x + pending_box->content_box.width * 0.35 &&
                    op->rect.y == pending_box->content_box.y &&
                    op->rect.width == pending_box->content_box.width * 0.30)
                    pending_fill = true;
            }
            TBOX_TEST_ASSERT(output_text && !fallback_text && progress_fill && meter_fill && pending_fill);
            tbox_html_node_set_text_content(tbox_context_document(ctx), output, tbox_string_view_make("50%", 3));
            tbox_html_node_set_attribute(tbox_context_document(ctx), progress,
                tbox_string_view_make("value", 5), tbox_string_view_make("50", 2));
            tbox_context_run_frame(ctx, 500.0, 200.0, &list);
            progress_box = find_context_box(ctx, progress);
            bool updated_text = false, updated_fill = false;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind == TBOX_PAINT_TEXT_RUN && string_view_equal_cstr(op->text, "50%")) updated_text = true;
                if (op->kind == TBOX_PAINT_FILL_RECT && progress_box != NULL &&
                    op->rect.x == progress_box->content_box.x && op->rect.y == progress_box->content_box.y &&
                    op->rect.width == progress_box->content_box.width * 0.5)
                    updated_fill = true;
            }
            TBOX_TEST_ASSERT(updated_text && updated_fill);
            tbox_context_close(ctx);
        }
    }

    /* accent-color (inherited) paints the range fill and thumb and the
     * progress bar; auto keeps the built-in blue and meter stays semantic. */
    {
        const char *html = "<div><input id='plain' type='range' value='50'>"
            "<p class='accent'><input type='range' value='50'><progress value='50' max='100'></progress>"
            "<meter min='0' max='100' value='90' low='20' high='80'></meter></p></div>";
        tbox_context *ctx = open_cstr(html, ".accent { accent-color: rgb(200, 30, 120); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 600.0, 200.0, &list);
            tbox_html_node *root = (tbox_html_node *)tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *plain = root->first_child;
            const tbox_html_node *range = plain->next_sibling->first_child;
            const tbox_html_node *progress = range->next_sibling;
            const tbox_html_node *meter = progress->next_sibling;
            const tbox_layout_box *boxes[4] = {find_context_box(ctx, plain), find_context_box(ctx, range),
                find_context_box(ctx, progress), find_context_box(ctx, meter)};
            TBOX_TEST_ASSERT(boxes[0] != NULL && boxes[1] != NULL && boxes[2] != NULL && boxes[3] != NULL);
            /* Per box: fills whose color is the accent, and fills in the old blue. */
            int accent[4] = {0}, blue[4] = {0};
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_FILL_RECT) continue;
                for (size_t b = 0; b < 4; b++) {
                    if (boxes[b] == NULL || op->rect.y < boxes[b]->border_box.y - 1.0 ||
                        op->rect.y + op->rect.height > boxes[b]->border_box.y + boxes[b]->border_box.height + 1.0 ||
                        op->rect.x < boxes[b]->border_box.x - 8.0 ||
                        op->rect.x >= boxes[b]->border_box.x + boxes[b]->border_box.width) continue;
                    if (op->color.r == 200 && op->color.g == 30 && op->color.b == 120) accent[b]++;
                    if (op->color.r == 65 && op->color.g == 115 && op->color.b == 195) blue[b]++;
                }
            }
            TBOX_TEST_ASSERT(blue[0] == 2 && accent[0] == 0);  /* fill + thumb */
            TBOX_TEST_ASSERT(accent[1] == 2 && blue[1] == 0);
            TBOX_TEST_ASSERT(accent[2] == 1 && blue[2] == 0);
            TBOX_TEST_ASSERT(accent[3] == 0);
            tbox_context_close(ctx);
        }
    }

    /* A local stylesheet participates in document order with <style>. */
    {
        tbox_context_options options = tbox_context_options_default();
        options.asset_base_dir = TBOX_TEST_ASSETS_DIR;
        const char *html = "<div><link rel='stylesheet' href='cascade-link.css'><p class='linked'>Ligado</p></div>";
        tbox_context *ctx = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 120.0, &list);
            const tbox_layout_box *p = find_box_by_tag(find_context_box(ctx,
                tbox_html_document_root(tbox_context_document(ctx))->first_child), "p");
            TBOX_TEST_ASSERT(p != NULL && p->style->color.r == 160 && p->style->color.g == 52);
            tbox_context_close(ctx);
        }
        html = "<div><link rel='stylesheet' href='cascade-link.css'><style>.linked { color: blue; }</style><p class='linked'>Depois</p></div>";
        ctx = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 120.0, &list);
            const tbox_layout_box *p = find_box_by_tag(find_context_box(ctx,
                tbox_html_document_root(tbox_context_document(ctx))->first_child), "p");
            TBOX_TEST_ASSERT(p != NULL && p->style->color.b == 255 && p->style->color.r == 0);
            tbox_context_close(ctx);
        }
    }

    /* @import: nested, relative to the importing sheet, with a media list
     * that doesn't match; imports come before the sheet's own rules, and
     * an @import after a rule is ignored. */
    {
        tbox_context_options options = tbox_context_options_default();
        options.asset_base_dir = TBOX_TEST_ASSETS_DIR;
        const char *html = "<div><style>@import url(cascade-import.css); .import-base { padding-left: 3px; } @import url(cascade-link.css);</style>"
                           "<p class='import-color import-base linked'>Importado</p></div>";
        tbox_context *ctx = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 120.0, &list);
            const tbox_layout_box *p = find_box_by_tag(find_context_box(ctx,
                tbox_html_document_root(tbox_context_document(ctx))->first_child), "p");
            TBOX_TEST_ASSERT(p != NULL && p->style->color.r == 15 && p->style->color.g == 118 && p->style->color.b == 110);
            TBOX_TEST_ASSERT(p != NULL && p->style->padding[0].value == 8.0 && p->style->padding[3].value == 3.0);
            TBOX_TEST_ASSERT(p != NULL && p->style->border_widths[0] == 0.0); /* cascade-link.css not imported */
            tbox_context_close(ctx);
        }
    }

    /* @import ... layer(name): the imported rules join that cascade layer,
     * placed by a leading @layer statement; an unlayered rule wins over a
     * more specific layered one. */
    {
        tbox_context_options options = tbox_context_options_default();
        options.asset_base_dir = TBOX_TEST_ASSETS_DIR;
        const char *html = "<div><style>@layer reset, theme; @import url(cascade-layers-reset.css) layer(reset);"
                           "@layer theme { p { background: #dbeafe; } } p { color: #1d4ed8; }</style>"
                           "<p class='layer-demo'>camadas</p></div>";
        tbox_context *ctx = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, NULL, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 120.0, &list);
            const tbox_layout_box *p = find_box_by_tag(find_context_box(ctx,
                tbox_html_document_root(tbox_context_document(ctx))->first_child), "p");
            TBOX_TEST_ASSERT(p != NULL && p->style->color.r == 0x1d && p->style->color.b == 0xd8); /* unlayered wins */
            TBOX_TEST_ASSERT(p != NULL && p->style->background_color.r == 0xdb); /* theme after reset */
            TBOX_TEST_ASSERT(p != NULL && p->style->padding[0].value == 12.0); /* reset still applies */
            tbox_context_close(ctx);
        }
    }

    /* A first supported <source> replaces the <img> fallback; media sources
     * that this version cannot evaluate leave the fallback in place. */
    {
        tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(images != NULL);
        if (images != NULL) {
            const char *html = "<div><picture><source type='image/png' srcset='yellow.png'>"
                "<img src='black.png' alt='fallback'></picture></div>";
            tbox_context *ctx = tbox_context_open(html, strlen(html), "", 0, fonts, images);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 300.0, 300.0, &list);
                bool chosen = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_IMAGE &&
                        list.items[i].image == tbox_image_cache_get(images, tbox_string_view_make("yellow.png", 10))) chosen = true;
                TBOX_TEST_ASSERT(chosen);
                tbox_context_close(ctx);
            }
            html = "<div><picture><source media='(max-width: 500px)' srcset='yellow.png'>"
                "<img src='black.png' alt='fallback'></picture></div>";
            ctx = tbox_context_open(html, strlen(html), "", 0, fonts, images);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 300.0, 300.0, &list);
                bool fallback = false;
                for (size_t i = 0; i < list.count; i++)
                    if (list.items[i].kind == TBOX_PAINT_IMAGE &&
                        list.items[i].image == tbox_image_cache_get(images, tbox_string_view_make("black.png", 9))) fallback = true;
                TBOX_TEST_ASSERT(fallback);
                tbox_context_close(ctx);
            }
            tbox_image_cache_destroy(images);
        }
    }

    /* The first <base href> redirects both linked CSS and image src. */
    {
        tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        tbox_context_options options = tbox_context_options_default();
        options.asset_base_dir = TBOX_TEST_ASSETS_DIR;
        const char *html = "<div><base href='base-demo/'><link rel='stylesheet' href='base.css'>"
            "<p>Base</p><img src='yellow.png' width='40' height='40'></div>";
        tbox_context *ctx = tbox_context_open_with_options(html, strlen(html), "", 0, fonts, images, &options);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 200.0, &list);
            const tbox_layout_box *p = find_box_by_tag(find_context_box(ctx,
                tbox_html_document_root(tbox_context_document(ctx))->first_child), "p");
            TBOX_TEST_ASSERT(p != NULL && tbox_style_border_side_width(p->style, 3) == 4.0);
            bool image_found = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_IMAGE && list.items[i].image ==
                    tbox_image_cache_get(images, tbox_string_view_make("base-demo/yellow.png", 20))) image_found = true;
            TBOX_TEST_ASSERT(image_found);
            tbox_context_close(ctx);
        }
        tbox_image_cache_destroy(images);
    }

    /* Image maps route scaled image coordinates to rect, circle and poly
     * areas, keeping the area node as the callback target. */
    {
        tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        const char *html = "<div><img src='yellow.png' usemap='#regions' width='300' height='300' style='display:block'>"
            "<map name='regions'><area shape='rect' coords='0,0,100,200' href='#left'>"
            "<area shape='circle' coords='150,50,40' href='#circle'>"
            "<area shape='poly' coords='110,110,190,110,150,190' href='#triangle'></map></div>";
        tbox_context *ctx = tbox_context_open(html, strlen(html), "", 0, fonts, images);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 340.0, 340.0, &list);
            const tbox_html_node *root = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *map = root->first_child->next_sibling;
            const tbox_layout_box *img = find_context_box(ctx, root->first_child);
            TBOX_TEST_ASSERT(img != NULL && img->text_run_count > 0);
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "area", 4, record_click, &capture, NULL) >= 0);
            if (img != NULL && img->text_run_count > 0) {
                tbox_rect rect = img->text_runs[0].rect;
                TBOX_TEST_ASSERT(tbox_context_cursor_at(ctx, rect.x + 30, rect.y + 30) == TBOX_STYLE_CURSOR_POINTER);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, rect.x + 30, rect.y + 30));
                TBOX_TEST_ASSERT(capture.node == map->first_child);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, rect.x + 225, rect.y + 75));
                TBOX_TEST_ASSERT(capture.node == map->first_child->next_sibling);
                TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, rect.x + 225, rect.y + 225));
                TBOX_TEST_ASSERT(capture.node == map->last_child);
                TBOX_TEST_ASSERT(capture.call_count == 3);
                TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &(tbox_key_event){ TBOX_KEY_ENTER, true, false, false }));
                TBOX_TEST_ASSERT(capture.call_count == 4);
            }
            tbox_context_close(ctx);
        }
        html = "<div><p><img src='yellow.png' usemap='#small' width='200' height='200'></p>"
            "<map name='small'><area shape='rect' coords='0,0,100,200' href='#left'></map></div>";
        ctx = tbox_context_open(html, strlen(html), "", 0, fonts, images);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 300.0, 300.0, &list);
            click_capture capture;
            click_capture_reset(&capture);
            TBOX_TEST_ASSERT(tbox_context_bind_click(ctx, "area", 4, record_click, &capture, NULL) >= 0);
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_IMAGE && list.items[i].image ==
                    tbox_image_cache_get(images, tbox_string_view_make("yellow.png", 10))) {
                    tbox_rect rect = list.items[i].rect;
                    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, rect.x + 20, rect.y + 20));
                    break;
                }
            TBOX_TEST_ASSERT(capture.call_count == 1);
            tbox_context_close(ctx);
        }
        tbox_image_cache_destroy(images);
    }

    /* Ruby pairs each base segment with its rt, centers the annotation above
     * it, and suppresses rp fallback punctuation. */
    {
        const char *html = "<div><p>Antes <ruby>base<rp>(</rp><rt>annotation</rt><rp>)</rp></ruby> depois</p>"
            "<p><ruby>A<rt>alpha</rt>B<rt>beta</rt></ruby></p></div>";
        tbox_context *ctx = open_cstr(html, "ruby { font-size: 24px; } rt { font-size: 12px; color: red; }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 500.0, 250.0, &list);
            const tbox_html_node *root = tbox_html_document_root(tbox_context_document(ctx))->first_child;
            const tbox_html_node *first_ruby = root->first_child->first_child->next_sibling;
            const tbox_html_node *second_ruby = root->last_child->first_child;
            const tbox_layout_box *first = find_context_box(ctx, first_ruby);
            const tbox_layout_box *second = find_context_box(ctx, second_ruby);
            TBOX_TEST_ASSERT(first != NULL && second != NULL);
            if (first != NULL) {
                TBOX_TEST_ASSERT(first->text_run_count == 2);
                if (first->text_run_count == 2) {
                    const tbox_layout_text_run *rt = &first->text_runs[0];
                    const tbox_layout_text_run *base = &first->text_runs[1];
                    TBOX_TEST_ASSERT(string_view_equal_cstr(rt->text, "annotation"));
                    TBOX_TEST_ASSERT(string_view_equal_cstr(base->text, "base"));
                    TBOX_TEST_ASSERT(rt->rect.y + rt->rect.height <= base->rect.y);
                    TBOX_TEST_ASSERT(rt->style->font_size == 12.0 && base->style->font_size == 24.0);
                    TBOX_TEST_ASSERT(rt->rect.x >= first->content_box.x &&
                        rt->rect.x + rt->rect.width <= first->content_box.x + first->content_box.width + 0.1);
                }
            }
            if (second != NULL) {
                TBOX_TEST_ASSERT(second->text_run_count == 4);
                if (second->text_run_count == 4) {
                    TBOX_TEST_ASSERT(string_view_equal_cstr(second->text_runs[0].text, "alpha"));
                    TBOX_TEST_ASSERT(string_view_equal_cstr(second->text_runs[1].text, "A"));
                    TBOX_TEST_ASSERT(string_view_equal_cstr(second->text_runs[2].text, "beta"));
                    TBOX_TEST_ASSERT(string_view_equal_cstr(second->text_runs[3].text, "B"));
                    TBOX_TEST_ASSERT(second->text_runs[1].rect.x < second->text_runs[3].rect.x);
                }
            }
            bool punctuation = false;
            for (size_t i = 0; i < list.count; i++)
                if (list.items[i].kind == TBOX_PAINT_TEXT_RUN &&
                    (string_view_equal_cstr(list.items[i].text, "(") || string_view_equal_cstr(list.items[i].text, ")")))
                    punctuation = true;
            TBOX_TEST_ASSERT(!punctuation);
            tbox_context_close(ctx);
        }
    }

    /* New functions reach layout and pixels: linear sRGB, exponential
     * sizing, rounded modern rect(), and a crossed rect with no area. */
    {
        tbox_context *ctx = open_cstr(
            "<body><div id='tile'></div><div id='empty'></div></body>",
            "body { margin: 0; background: white; }"
            "#tile { position: absolute; left: 0; top: 0; width: calc(sqrt(16) * 10px); height: calc(log(8, 2) * 10px);"
            " background: color(srgb-linear .5 0 0); clip-path: rect(5px 35px 25px 5px round 4px); }"
            "#empty { position: absolute; left: 0; top: 40px; width: 40px; height: 20px;"
            " background: color(srgb 0 1 0); clip-path: rect(10px 0 0 20px); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 64, 64, &list);
            uint32_t pixels[64 * 64];
            for (size_t i = 0; i < 64 * 64; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 64, 64, &list);
            TBOX_TEST_ASSERT(pixels[10 * 64 + 10] == 0xFFBC0000u);
            TBOX_TEST_ASSERT(pixels[10 * 64 + 1] == 0xFFFFFFFFu && pixels[10 * 64 + 37] == 0xFFFFFFFFu);
            TBOX_TEST_ASSERT(pixels[27 * 64 + 10] == 0xFFFFFFFFu && pixels[5 * 64 + 5] == 0xFFFFFFFFu);
            TBOX_TEST_ASSERT(pixels[50 * 64 + 20] == 0xFFFFFFFFu);
            tbox_context_close(ctx);
        }
    }

    /* Numeric opacity and typed length functions reach composited pixels. */
    {
        tbox_context *ctx = open_cstr("<body><div></div></body>",
            "body { margin: 0; background: white; }"
            "div { width: hypot(30px, 40px); height: abs(-20px); background: red; opacity: calc(pow(.5, 2)); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 64, 32, &list);
            uint32_t pixels[64 * 32];
            for (size_t i = 0; i < 64 * 32; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 64, 32, &list);
            uint32_t inside = pixels[10 * 64 + 25];
            TBOX_TEST_ASSERT(((inside >> 16) & 255u) == 255u);
            TBOX_TEST_ASSERT(((inside >> 8) & 255u) >= 190u && ((inside >> 8) & 255u) <= 192u);
            TBOX_TEST_ASSERT((inside & 255u) == ((inside >> 8) & 255u));
            TBOX_TEST_ASSERT(pixels[10 * 64 + 55] == 0xFFFFFFFFu && pixels[25 * 64 + 25] == 0xFFFFFFFFu);
            tbox_context_close(ctx);
        }
    }

    /* Angular expressions must paint exactly like their literal results,
     * including gradients, filters, numeric opacity and layout dimensions. */
    {
        static const struct { const char *expression; const char *literal; } cases[] = {
            { "background: linear-gradient(asin(1), red, blue);", "background: linear-gradient(90deg, red, blue);" },
            { "background: linear-gradient(atan2(-1, -1), red, blue);", "background: linear-gradient(-135deg, red, blue);" },
            { "background: red; filter: hue-rotate(acos(0));", "background: red; filter: hue-rotate(90deg);" },
            { "background: red; opacity: sin(asin(.5)); width: calc(sin(atan2(3px, 4px)) * 50px);", "background: red; opacity: .5; width: 30px;" },
        };
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            uint32_t pixels[2][64 * 32];
            bool rendered[2] = { false, false };
            for (int variant = 0; variant < 2; variant++) {
                char css[512];
                snprintf(css, sizeof(css), "body { margin: 0; background: white; } div { width: 48px; height: 24px; %s }",
                    variant == 0 ? cases[c].expression : cases[c].literal);
                tbox_context *ctx = open_cstr("<body><div></div></body>", css, fonts);
                TBOX_TEST_ASSERT(ctx != NULL);
                if (ctx != NULL) {
                    tbox_display_list list;
                    tbox_context_run_frame(ctx, 64, 32, &list);
                    for (size_t i = 0; i < 64 * 32; i++) pixels[variant][i] = 0xFFFFFFFFu;
                    tbox_raster_display_list(pixels[variant], 64, 32, &list);
                    rendered[variant] = true;
                    tbox_context_close(ctx);
                }
            }
            if (rendered[0] && rendered[1]) {
                TBOX_TEST_ASSERT_MSG(memcmp(pixels[0], pixels[1], sizeof(pixels[0])) == 0, cases[c].expression);
                TBOX_TEST_ASSERT(pixels[0][10 * 64 + 10] != 0xFFFFFFFFu);
                TBOX_TEST_ASSERT(pixels[0][30 * 64 + 60] == 0xFFFFFFFFu);
            }
        }
    }

    /* Polygon clips remove descendant pixels and pointer targets, even
     * with nonzero layer origins and nested clip-path stacking contexts. */
    {
        tbox_context *ctx = open_cstr("<body><div id='host'><div id='child'></div></div></body>",
            "body { margin: 0; background: white; }"
            "#host { position: absolute; left: 20px; top: 10px; width: 40px; height: 40px; clip-path: polygon(0 0, 100% 0, 0 100%); }"
            "#child { width: 40px; height: 40px; background: red; cursor: crosshair; clip-path: polygon(0 0, 100% 0, 100% 100%); }", fonts);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 80, 70, &list);
            uint32_t pixels[80 * 70];
            for (size_t i = 0; i < 80 * 70; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 80, 70, &list);
            TBOX_TEST_ASSERT(pixels[15 * 80 + 35] == 0xFFFF0000u); /* inside both */
            TBOX_TEST_ASSERT(pixels[25 * 80 + 25] == 0xFFFFFFFFu); /* outside child */
            TBOX_TEST_ASSERT(pixels[25 * 80 + 55] == 0xFFFFFFFFu); /* outside parent */
            const tbox_layout_box *hit = tbox_context_hit_test(ctx, 35, 15);
            const tbox_html_attribute *id = hit != NULL && hit->node != NULL ? tbox_html_node_get_attribute(hit->node, tbox_string_view_from_cstr("id")) : NULL;
            TBOX_TEST_ASSERT(id != NULL && string_view_equal_cstr(id->value, "child"));
            hit = tbox_context_hit_test(ctx, 55, 25);
            TBOX_TEST_ASSERT(hit == NULL || hit->style == NULL || tbox_style_clip_path_of(hit->style)->kind != TBOX_STYLE_CLIP_PATH_POLYGON);
            hit = tbox_context_hit_test(ctx, 25, 25);
            id = hit != NULL && hit->node != NULL ? tbox_html_node_get_attribute(hit->node, tbox_string_view_from_cstr("id")) : NULL;
            TBOX_TEST_ASSERT(id != NULL && string_view_equal_cstr(id->value, "host"));
            tbox_context_close(ctx);
        }
    }

    /* Concavity and winding rules must agree in raster and hit-test. */
    {
        static const struct { const char *shape; bool center; } cases[] = {
            { "polygon(0 0, 100% 0, 100% 30%, 30% 30%, 30% 100%, 0 100%)", false },
            { "polygon(nonzero, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, 100% 0, 100% 100%, 0 100%)", true },
            { "polygon(evenodd, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, 100% 0, 100% 100%, 0 100%)", false },
        };
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            char css[512];
            snprintf(css, sizeof(css), "body { margin: 0; background: white; } div { position: absolute; left: 10px; top: 10px; width: 40px; height: 40px; background: red; clip-path: %s; }", cases[c].shape);
            tbox_context *ctx = open_cstr("<body><div id='shape'></div></body>", css, fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 60, 60, &list);
                uint32_t pixels[60 * 60];
                for (size_t i = 0; i < 60 * 60; i++) pixels[i] = 0xFFFFFFFFu;
                tbox_raster_display_list(pixels, 60, 60, &list);
                TBOX_TEST_ASSERT_MSG(pixels[35 * 60 + 35] == (cases[c].center ? 0xFFFF0000u : 0xFFFFFFFFu), cases[c].shape);
                const tbox_layout_box *hit = tbox_context_hit_test(ctx, 35, 35);
                bool target = hit != NULL && hit->style != NULL && tbox_style_clip_path_of(hit->style)->kind == TBOX_STYLE_CLIP_PATH_POLYGON;
                TBOX_TEST_ASSERT(target == cases[c].center);
                if (c == 0) TBOX_TEST_ASSERT(pixels[15 * 60 + 35] == 0xFFFF0000u && pixels[35 * 60 + 15] == 0xFFFF0000u);
                tbox_context_close(ctx);
            }
        }
    }

    /* Compare new shapes/gradients with independent existing equivalents,
     * including text, opacity, blur, rounded backgrounds, tiling and masks. */
    {
        static const struct { const char *computed; const char *reference; } cases[] = {
            { "clip-path: polygon(0 0, 50% 0, 50% 100%, 0 100%);", "clip-path: inset(0 50% 0 0);" },
            { "clip-path: polygon(0 0, 50% 0, 50% 100%, 0 100%); filter: blur(2px);", "clip-path: inset(0 50% 0 0); filter: blur(2px);" },
            { "mask-image: conic-gradient(black 0 180deg, transparent 180deg 360deg);", "mask-image: linear-gradient(90deg, transparent 50%, black 50%);" },
            { "background: conic-gradient(from asin(1), red 0deg, blue 1turn); background-size: 20px 20px;", "background: conic-gradient(from 90deg, red 0%, blue 100%); background-size: 20px 20px;" },
        };
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            uint32_t pixels[2][90 * 70];
            bool rendered[2] = { false, false };
            for (int variant = 0; variant < 2; variant++) {
                char css[768];
                snprintf(css, sizeof(css), "body { margin: 0; background: white; } div { position: absolute; left: 17px; top: 13px; width: 50px; height: 40px; background: red; border-radius: 8px; opacity: .75; %s } p { margin: 0; color: black; font-size: 12px; }",
                    variant == 0 ? cases[c].computed : cases[c].reference);
                tbox_context *ctx = open_cstr("<body><div><p>ABC</p></div></body>", css, fonts);
                TBOX_TEST_ASSERT(ctx != NULL);
                if (ctx != NULL) {
                    tbox_display_list list;
                    tbox_context_run_frame(ctx, 90, 70, &list);
                    for (size_t i = 0; i < 90 * 70; i++) pixels[variant][i] = 0xFFFFFFFFu;
                    tbox_raster_display_list(pixels[variant], 90, 70, &list);
                    rendered[variant] = true;
                    tbox_context_close(ctx);
                }
            }
            if (rendered[0] && rendered[1])
                TBOX_TEST_ASSERT_MSG(memcmp(pixels[0], pixels[1], sizeof(pixels[0])) == 0, cases[c].computed);
        }
    }

    /* Appended scrollbars obey both own and ancestor polygon clips. */
    {
        static const char *const parent_shapes[] = {"none", "polygon(0 0, 50% 0, 50% 100%, 0 100%)"};
        for (size_t c = 0; c < 2; c++) {
            char css[768];
            snprintf(css, sizeof(css), "body { margin: 0; background: white; } #host { position: absolute; left: 10px; top: 10px; width: 60px; height: 40px; clip-path: %s; } #scroll { width: 60px; height: 40px; overflow-y: auto; scrollbar-width: auto; clip-path: polygon(0 0, 100%% 0, 100%% 50%%, 0 50%%); } #content { height: 120px; background: red; }", parent_shapes[c]);
            tbox_context *ctx = open_cstr("<body><div id='host'><div id='scroll'><div id='content'></div></div></div></body>", css, fonts);
            TBOX_TEST_ASSERT(ctx != NULL);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, 90, 70, &list);
                uint32_t pixels[90 * 70];
                for (size_t i = 0; i < 90 * 70; i++) pixels[i] = 0xFFFFFFFFu;
                tbox_raster_display_list(pixels, 90, 70, &list);
                TBOX_TEST_ASSERT(pixels[45 * 90 + 65] == 0xFFFFFFFFu);
                TBOX_TEST_ASSERT(!tbox_context_scrollbar_press(ctx, 65, 45));
                TBOX_TEST_ASSERT(pixels[15 * 90 + 65] == (c == 0 ? 0xFF646E7Au : 0xFFFFFFFFu));
                TBOX_TEST_ASSERT(tbox_context_scrollbar_press(ctx, 65, 15) == (c == 0));
                tbox_context_scrollbar_release(ctx);
                tbox_context_close(ctx);
            }
        }
    }

    /* Each implicit @scope belongs to its own <style> parent after CSS
     * concatenation; :scope also targets that parent itself. */
    {
        tbox_context *ctx = open_cstr("<body><div id='a'><style>@scope { :scope { background:red; } p { color:red; } }</style><p id='x'>X</p></div><div id='b'><style>@media screen { @scope { :scope { background:blue; } p { color:blue; } } }</style><p id='y'>Y</p></div><p id='z'>Z</p></body>", "p { color:black; }", fonts);
        TBOX_TEST_REQUIRE(ctx != NULL);
        tbox_display_list list;
        tbox_context_run_frame(ctx, 800, 600, &list);
        const char *ids[] = { "a", "b", "x", "y", "z" };
        for (size_t i = 0; i < 5; i++) {
            const tbox_style *style = NULL;
            for (size_t j = 0; j < ctx->styles.count; j++) {
                const tbox_html_attribute *id = tbox_html_node_get_attribute(ctx->styles.items[j].node, tbox_string_view_from_cstr("id"));
                if (id != NULL && string_view_equal_cstr(id->value, ids[i])) style = ctx->styles.items[j].style;
            }
            TBOX_TEST_REQUIRE(style != NULL);
            if (i == 0) TBOX_TEST_ASSERT(style->background_color.r == 255);
            if (i == 1) TBOX_TEST_ASSERT(style->background_color.b == 255);
            if (i == 2) TBOX_TEST_ASSERT(style->color.r == 255);
            if (i == 3) TBOX_TEST_ASSERT(style->color.b == 255);
            if (i == 4) TBOX_TEST_ASSERT(style->color.r == 0 && style->color.b == 0);
        }
        tbox_context_close(ctx);
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);

    return failures;
}
