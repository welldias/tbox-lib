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

    tbox_font_face_destroy(font);
    free(font_data);

    return failures;
}
