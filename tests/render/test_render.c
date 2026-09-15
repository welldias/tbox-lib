#include <tbox/render.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

/* <tbox/string_view.h> (pulled in by <tbox/render.h>) only exposes
 * tbox_string_view_make -- same note as tests/font/test_font.c. */
static tbox_string_view tbox_test_render_view_from_cstr(const char *nul_terminated) {
    return tbox_string_view_make(nul_terminated, strlen(nul_terminated));
}

#ifndef TBOX_TEST_LIBERATION_SANS_PATH
#define TBOX_TEST_LIBERATION_SANS_PATH "external/liberation-sans/LiberationSans-Regular.ttf"
#endif

/* Mirrors tests/font/test_font.c's / tests/layout/test_layout.c's
 * read_file() helper: reads the vendored LiberationSans-Regular.ttf into a
 * malloc'd buffer so the embedded font backend (no Fontconfig dependency)
 * can load it. Render Pipeline never dereferences a run's `font` (it only
 * copies the pointer into the paint op), but a real face keeps this test
 * free of a fabricated/fake pointer. */
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

/* Zero-initialized style: transparent background, empty text left to the
 * caller (style has no "text" field, that's box->text_runs). Individual
 * test cases override the fields they care about. */
static tbox_style tbox_test_render_default_style(void) {
    tbox_style style;
    memset(&style, 0, sizeof(style));
    style.color = (tbox_css_rgba){ 0, 0, 0, 255 };
    return style;
}

/* Zero-initialized box: no parent/children/siblings, no text_runs, empty
 * rects -- same "fill in only what the test needs" spirit as the style
 * helper above. `tbox_layout_box` is a plain, non-opaque struct (see
 * <tbox/layout.h>), so tests can hand-construct trees directly instead of
 * going through HTML parsing/Style/Layout. */
static tbox_layout_box tbox_test_render_default_box(const tbox_style *style) {
    tbox_layout_box box;
    memset(&box, 0, sizeof(box));
    box.style = style;
    return box;
}

static bool string_view_equal_cstr(tbox_string_view view, const char *cstr) {
    size_t len = strlen(cstr);
    return view.size == len && (len == 0 || memcmp(view.data, cstr, len) == 0);
}

static bool rect_equal(tbox_rect a, tbox_rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int tbox_test_render_run(void) {
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

    tbox_font_face *bold_font = tbox_font_face_load(font_data, font_size, 24.0);
    TBOX_TEST_ASSERT_MSG(bold_font != NULL, "failed to load a second (distinct) embedded font face");
    if (bold_font == NULL) {
        tbox_font_face_destroy(font);
        free(font_data);
        return failures + 1;
    }

    /* 1: transparent background, no text -> no ops at all for a lone box. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 0, 0, 0, 0 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 0, "transparent background + empty text must produce zero ops");
        tbox_arena_destroy(&arena);
    }

    /* 2: opaque background -> exactly one FILL_RECT whose rect equals the
     * box's border_box, not its content_box (non-zero padding, so the two
     * actually differ -- this exercises the distinction, not a
     * padding-less box where they'd coincide). */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 10, 20, 30, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.content_box        = (tbox_rect){ 10.0, 10.0, 100.0, 50.0 };
        box.border_box         = (tbox_rect){ 0.0, 0.0, 120.0, 70.0 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "opaque background must produce exactly one op");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[0].rect, box.border_box), "FILL_RECT rect must be border_box, not content_box");
            TBOX_TEST_ASSERT_MSG(!rect_equal(list.items[0].rect, box.content_box), "border_box and content_box must actually differ in this test");
            TBOX_TEST_ASSERT(list.items[0].color.r == 10 && list.items[0].color.g == 20 && list.items[0].color.b == 30 && list.items[0].color.a == 255);
        }
        tbox_arena_destroy(&arena);
    }

    /* 3: paint order is tree pre-order -- a root with a background and one
     * child with a background yields the root's FILL_RECT before the
     * child's. */
    {
        tbox_style root_style        = tbox_test_render_default_style();
        root_style.background_color  = (tbox_css_rgba){ 1, 1, 1, 255 };
        tbox_style child_style       = tbox_test_render_default_style();
        child_style.background_color = (tbox_css_rgba){ 2, 2, 2, 255 };

        tbox_layout_box child = tbox_test_render_default_box(&child_style);
        tbox_layout_box root  = tbox_test_render_default_box(&root_style);
        root.first_child      = &child;
        root.last_child       = &child;
        child.parent          = &root;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &root);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "root + child backgrounds must produce two ops");
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 1);
            TBOX_TEST_ASSERT(list.items[1].color.r == 2);
        }
        tbox_arena_destroy(&arena);
    }

    /* 4: a box with a single text_run -> exactly one TEXT_RUN with the
     * right text/color/face/rect, taken straight from the run. */
    {
        tbox_style style    = tbox_test_render_default_style();
        style.color         = (tbox_css_rgba){ 5, 6, 7, 255 };
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run run;
        run.rect = (tbox_rect){ 15.0, 25.0, 40.0, 16.0 };
        run.text = tbox_test_render_view_from_cstr("hello");
        run.font = font;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "a box with one text_run must produce exactly one op");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_TEXT_RUN);
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[0].text, "hello"), "TEXT_RUN text must match the run's text");
            TBOX_TEST_ASSERT(list.items[0].face == font);
            TBOX_TEST_ASSERT(list.items[0].color.r == 5 && list.items[0].color.g == 6 && list.items[0].color.b == 7);
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[0].rect, run.rect), "TEXT_RUN rect must be the run's own rect");
        }
        tbox_arena_destroy(&arena);
    }

    /* 5: a box with both a background and one text_run produces FILL_RECT
     * immediately before TEXT_RUN, in that order. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 9, 9, 9, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.content_box        = (tbox_rect){ 0.0, 0.0, 50.0, 16.0 };
        box.border_box         = (tbox_rect){ 0.0, 0.0, 50.0, 16.0 };

        tbox_layout_text_run run;
        run.rect = (tbox_rect){ 0.0, 0.0, 16.0, 16.0 };
        run.text = tbox_test_render_view_from_cstr("hi");
        run.font = font;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "background + text box must produce exactly two ops");
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
            TBOX_TEST_ASSERT(list.items[1].kind == TBOX_PAINT_TEXT_RUN);
        }
        tbox_arena_destroy(&arena);
    }

    /* 6: transparent background + zero text_runs on the root produces zero
     * ops for the root itself, but its child (with its own background) is
     * still visited and produces its own op. */
    {
        tbox_style root_style        = tbox_test_render_default_style();
        root_style.background_color  = (tbox_css_rgba){ 0, 0, 0, 0 };
        tbox_style child_style       = tbox_test_render_default_style();
        child_style.background_color = (tbox_css_rgba){ 3, 3, 3, 255 };

        tbox_layout_box child = tbox_test_render_default_box(&child_style);
        tbox_layout_box root  = tbox_test_render_default_box(&root_style);
        root.first_child      = &child;
        root.last_child       = &child;
        child.parent          = &root;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &root);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "transparent root with no text_runs must produce no op of its own, only the child's");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 3);
        }
        tbox_arena_destroy(&arena);
    }

    /* 7: NOVO v2 -- a box with N (> 1) text_runs emits exactly N TEXT_RUN
     * ops, in the same order as the array, one per run's own rect/text/
     * face. */
    {
        tbox_style style    = tbox_test_render_default_style();
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run runs[3];
        runs[0].rect = (tbox_rect){ 0.0, 0.0, 20.0, 16.0 };
        runs[0].text = tbox_test_render_view_from_cstr("one");
        runs[0].font = font;
        runs[1].rect = (tbox_rect){ 20.0, 0.0, 20.0, 16.0 };
        runs[1].text = tbox_test_render_view_from_cstr("two");
        runs[1].font = bold_font;
        runs[2].rect = (tbox_rect){ 0.0, 16.0, 20.0, 16.0 };
        runs[2].text = tbox_test_render_view_from_cstr("three");
        runs[2].font = font;

        box.text_runs      = runs;
        box.text_run_count = 3;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 3, "a box with 3 text_runs must emit exactly 3 TEXT_RUN ops");
        if (list.count == 3) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_TEXT_RUN && list.items[1].kind == TBOX_PAINT_TEXT_RUN && list.items[2].kind == TBOX_PAINT_TEXT_RUN);
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[0].text, "one"), "op order must match text_runs order");
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[1].text, "two"), "op order must match text_runs order");
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(list.items[2].text, "three"), "op order must match text_runs order");
            TBOX_TEST_ASSERT(list.items[0].face == font && list.items[1].face == bold_font && list.items[2].face == font);
        }
        tbox_arena_destroy(&arena);
    }

    /* 8: NOVO v2 -- a box with text_run_count == 0 emits no TEXT_RUN ops at
     * all, even with text_runs left non-NULL (dangling but never
     * dereferenced when the count is 0). */
    {
        tbox_style style    = tbox_test_render_default_style();
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run unused_run;
        box.text_runs      = &unused_run;
        box.text_run_count = 0;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 0, "text_run_count == 0 must emit zero TEXT_RUN ops");
        tbox_arena_destroy(&arena);
    }

    /* 9: NOVO v2 -- every TEXT_RUN from the same box carries the SAME
     * color (box->style->color), even though the two runs have different
     * faces -- no per-run color in this version. */
    {
        tbox_style style    = tbox_test_render_default_style();
        style.color         = (tbox_css_rgba){ 42, 43, 44, 255 };
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run runs[2];
        runs[0].rect = (tbox_rect){ 0.0, 0.0, 20.0, 16.0 };
        runs[0].text = tbox_test_render_view_from_cstr("regular");
        runs[0].font = font;
        runs[1].rect = (tbox_rect){ 20.0, 0.0, 20.0, 16.0 };
        runs[1].text = tbox_test_render_view_from_cstr("bold");
        runs[1].font = bold_font;

        box.text_runs      = runs;
        box.text_run_count = 2;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "two text_runs must produce two ops");
        if (list.count == 2) {
            TBOX_TEST_ASSERT_MSG(list.items[0].face != list.items[1].face, "test setup: the two runs must actually use different faces");
            TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 42 && list.items[0].color.g == 43 && list.items[0].color.b == 44, "first run must carry the box's style color");
            TBOX_TEST_ASSERT_MSG(list.items[1].color.r == 42 && list.items[1].color.g == 43 && list.items[1].color.b == 44, "second run must carry the SAME color as the first, despite the different face");
        }
        tbox_arena_destroy(&arena);
    }

    tbox_font_face_destroy(bold_font);
    tbox_font_face_destroy(font);
    free(font_data);

    return failures;
}
