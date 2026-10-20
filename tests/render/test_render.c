#include <tbox/render.h>
#include <tbox/output.h>

#include <tbox/image.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "render/tbox_render_internal.h"
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
    style.color   = (tbox_css_rgba){ 0, 0, 0, 255 };
    style.opacity = 1.0;
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

int tbox_test_composition_run(const void *font_data, size_t font_size);

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

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 15.0, 25.0, 40.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("hello");
        run.font                 = font;
        run.style                = &style; /* never NULL, per <tbox/layout.h> */

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

        /* Own style for the run, distinct from the box's -- the box's
         * `style` has a non-transparent background_color, which would
         * otherwise  also trigger the run's own highlight
         * FILL_RECT and break this test's "exactly 2 ops" expectation. */
        tbox_style run_style = tbox_test_render_default_style();

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 0.0, 0.0, 16.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("hi");
        run.font                 = font;
        run.style                = &run_style; /* never NULL; transparent run background here, no extra FILL_RECT */

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

    /* 7: a box with N (> 1) text_runs emits exactly N TEXT_RUN
     * ops, in the same order as the array, one per run's own rect/text/
     * face. */
    {
        tbox_style style    = tbox_test_render_default_style();
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run runs[3] = { 0 };
        runs[0].rect                 = (tbox_rect){ 0.0, 0.0, 20.0, 16.0 };
        runs[0].text                 = tbox_test_render_view_from_cstr("one");
        runs[0].font                 = font;
        runs[0].style                = &style;
        runs[1].rect                 = (tbox_rect){ 20.0, 0.0, 20.0, 16.0 };
        runs[1].text                 = tbox_test_render_view_from_cstr("two");
        runs[1].font                 = bold_font;
        runs[1].style                = &style;
        runs[2].rect                 = (tbox_rect){ 0.0, 16.0, 20.0, 16.0 };
        runs[2].text                 = tbox_test_render_view_from_cstr("three");
        runs[2].font                 = font;
        runs[2].style                = &style;

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

    /* 8: a box with text_run_count == 0 emits no TEXT_RUN ops at
     * all, even with text_runs left non-NULL (dangling but never
     * dereferenced when the count is 0). */
    {
        tbox_style style    = tbox_test_render_default_style();
        tbox_layout_box box = tbox_test_render_default_box(&style);

        tbox_layout_text_run unused_run = { 0 };
        box.text_runs                   = &unused_run;
        box.text_run_count              = 0;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 0, "text_run_count == 0 must emit zero TEXT_RUN ops");
        tbox_arena_destroy(&arena);
    }

    /* 9:  -- each TEXT_RUN carries its OWN run->style->color, which
     * may now differ between runs of the same box (e.g. a <b> nested in a
     * <p> with a different color) -- superseding v2-v12's shared
     * box->style->color. */
    {
        tbox_style box_style = tbox_test_render_default_style();
        box_style.color      = (tbox_css_rgba){ 42, 43, 44, 255 };
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run1_style = tbox_test_render_default_style();
        run1_style.color      = (tbox_css_rgba){ 42, 43, 44, 255 };
        tbox_style run2_style = tbox_test_render_default_style();
        run2_style.color      = (tbox_css_rgba){ 99, 88, 77, 255 };

        tbox_layout_text_run runs[2] = { 0 };
        runs[0].rect                 = (tbox_rect){ 0.0, 0.0, 20.0, 16.0 };
        runs[0].text                 = tbox_test_render_view_from_cstr("regular");
        runs[0].font                 = font;
        runs[0].style                = &run1_style;
        runs[1].rect                 = (tbox_rect){ 20.0, 0.0, 20.0, 16.0 };
        runs[1].text                 = tbox_test_render_view_from_cstr("bold");
        runs[1].font                 = bold_font;
        runs[1].style                = &run2_style;

        box.text_runs      = runs;
        box.text_run_count = 2;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "two text_runs must produce two ops");
        if (list.count == 2) {
            TBOX_TEST_ASSERT_MSG(list.items[0].face != list.items[1].face, "test setup: the two runs must actually use different faces");
            TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 42 && list.items[0].color.g == 43 && list.items[0].color.b == 44, "first run must carry its OWN run->style->color");
            TBOX_TEST_ASSERT_MSG(list.items[1].color.r == 99 && list.items[1].color.g == 88 && list.items[1].color.b == 77, "second run must carry its own (different) run->style->color, not the box's or the first run's");
        }
        tbox_arena_destroy(&arena);
    }

    /* 10: a box with an effective border (border_style == SOLID,
     * border_width > 0) produces, after the background FILL_RECT, exactly 4
     * more FILL_RECTs in border_color: top/bottom spanning the full
     * border_box width (including corners), left/right spanning only the
     * padding_box height -- together covering exactly border_box minus
     * padding_box, no overlap/gap at the corners. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 50, 50, 50, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width     = 3.0;
        style.border_color     = (tbox_css_rgba){ 200, 0, 0, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.content_box        = (tbox_rect){ 13.0, 13.0, 100.0, 50.0 };
        box.padding_box        = (tbox_rect){ 3.0, 3.0, 120.0, 70.0 };
        box.border_box         = (tbox_rect){ 0.0, 0.0, 126.0, 76.0 };

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 5, "background + effective border must produce 1 background + 4 border FILL_RECTs");
        if (list.count == 5) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[0].rect, box.border_box), "op 0 must still be the background over border_box");

            for (size_t i = 1; i <= 4; i++) {
                TBOX_TEST_ASSERT(list.items[i].kind == TBOX_PAINT_FILL_RECT);
                TBOX_TEST_ASSERT_MSG(list.items[i].color.r == 200 && list.items[i].color.g == 0 && list.items[i].color.b == 0 && list.items[i].color.a == 255, "border FILL_RECTs must carry style->border_color");
            }

            tbox_rect top    = list.items[1].rect;
            tbox_rect bottom = list.items[2].rect;
            tbox_rect left   = list.items[3].rect;
            tbox_rect right  = list.items[4].rect;

            TBOX_TEST_ASSERT_MSG(rect_equal(top, ((tbox_rect){ 0.0, 0.0, 126.0, 3.0 })), "top border strip must span the full border_box width, from border_box.y to padding_box.y");
            TBOX_TEST_ASSERT_MSG(rect_equal(bottom, ((tbox_rect){ 0.0, 73.0, 126.0, 3.0 })), "bottom border strip must span the full border_box width, from padding_box bottom to border_box bottom");
            TBOX_TEST_ASSERT_MSG(rect_equal(left, ((tbox_rect){ 0.0, 3.0, 3.0, 70.0 })), "left border strip must span only the padding_box height, from border_box.x to padding_box.x");
            TBOX_TEST_ASSERT_MSG(rect_equal(right, ((tbox_rect){ 123.0, 3.0, 3.0, 70.0 })), "right border strip must span only the padding_box height, from padding_box right edge to border_box right edge");
        }
        tbox_arena_destroy(&arena);
    }

    /* 11: no effective border (border_style != SOLID, or
     * border_width == 0) produces zero border FILL_RECTs -- only the
     * background's, same as v0-v3 (regression). Two sub-cases: NONE style
     * with a non-zero width, and SOLID style with a zero width. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 60, 60, 60, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_NONE;
        style.border_width     = 5.0; /* declared but style isn't SOLID -- must not paint */
        style.border_color     = (tbox_css_rgba){ 200, 0, 0, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0.0, 0.0, 100.0, 50.0 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "border_style != SOLID must produce zero border FILL_RECTs, only the background's");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
        }
        tbox_arena_destroy(&arena);
    }
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 60, 60, 60, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width     = 0.0; /* SOLID but zero width -- must not paint */
        style.border_color     = (tbox_css_rgba){ 200, 0, 0, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0.0, 0.0, 100.0, 50.0 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "border_width == 0 must produce zero border FILL_RECTs, only the background's");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
        }
        tbox_arena_destroy(&arena);
    }

    /* 12: op order for a box with background + effective border +
     * text: background, then the 4 border FILL_RECTs, then TEXT_RUN. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 9, 9, 9, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width     = 2.0;
        style.border_color     = (tbox_css_rgba){ 100, 100, 100, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.padding_box        = (tbox_rect){ 2.0, 2.0, 50.0, 16.0 };
        box.border_box         = (tbox_rect){ 0.0, 0.0, 54.0, 20.0 };

        /* Own style for the run, distinct from the box's: transparent
         * background/no decoration, so this test stays about box
         * background+border+text ordering, not the  run-level
         * effects (covered separately below). */
        tbox_style run_style = tbox_test_render_default_style();

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 2.0, 2.0, 16.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("hi");
        run.font                 = font;
        run.style                = &run_style;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 6, "background + border + text must produce 1 + 4 + 1 = 6 ops");
        if (list.count == 6) {
            TBOX_TEST_ASSERT_MSG(list.items[0].kind == TBOX_PAINT_FILL_RECT, "op 0 must be the background");
            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_FILL_RECT && list.items[2].kind == TBOX_PAINT_FILL_RECT && list.items[3].kind == TBOX_PAINT_FILL_RECT && list.items[4].kind == TBOX_PAINT_FILL_RECT, "ops 1-4 must be the 4 border strips");
            TBOX_TEST_ASSERT_MSG(list.items[5].kind == TBOX_PAINT_TEXT_RUN, "op 5 must be the TEXT_RUN, after background and border");
        }
        tbox_arena_destroy(&arena);
    }

    /* 13: a box with an effective border but no children/text
     * still just produces background + 4 border FILL_RECTs, and its child
     * (in tree pre-order) is visited afterwards. */
    {
        tbox_style parent_style      = tbox_test_render_default_style();
        parent_style.border_style    = TBOX_STYLE_BORDER_STYLE_SOLID;
        parent_style.border_width    = 1.0;
        parent_style.border_color    = (tbox_css_rgba){ 7, 7, 7, 255 };
        tbox_style child_style       = tbox_test_render_default_style();
        child_style.background_color = (tbox_css_rgba){ 3, 3, 3, 255 };

        tbox_layout_box child  = tbox_test_render_default_box(&child_style);
        tbox_layout_box parent = tbox_test_render_default_box(&parent_style);
        parent.border_box      = (tbox_rect){ 0.0, 0.0, 10.0, 10.0 };
        parent.padding_box     = (tbox_rect){ 1.0, 1.0, 8.0, 8.0 };
        parent.first_child     = &child;
        parent.last_child      = &child;
        child.parent           = &parent;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &parent);
        TBOX_TEST_ASSERT_MSG(list.count == 5, "borderless-background parent with effective border + one child with a background must produce 4 border ops + 1 child background op");
        if (list.count == 5) {
            for (size_t i = 0; i < 4; i++) {
                TBOX_TEST_ASSERT(list.items[i].kind == TBOX_PAINT_FILL_RECT);
                TBOX_TEST_ASSERT(list.items[i].color.r == 7 && list.items[i].color.g == 7 && list.items[i].color.b == 7);
            }
            TBOX_TEST_ASSERT_MSG(list.items[4].color.r == 3, "child's own background must come after the parent's border ops");
        }
        tbox_arena_destroy(&arena);
    }

    /* 14:  -- a run with a non-transparent run->style->background_color
     * (<mark>) produces an extra FILL_RECT immediately BEFORE its TEXT_RUN,
     * covering exactly run->rect (not the box's border_box/the whole
     * line). */
    {
        tbox_style box_style = tbox_test_render_default_style();
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run_style       = tbox_test_render_default_style();
        run_style.color            = (tbox_css_rgba){ 1, 2, 3, 255 };
        run_style.background_color = (tbox_css_rgba){ 255, 255, 0, 255 }; /* yellow highlight */

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 8.0, 4.0, 30.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("marked");
        run.font                 = font;
        run.style                = &run_style;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "a run with a non-transparent background must produce exactly 2 ops: the highlight FILL_RECT + its TEXT_RUN");
        if (list.count == 2) {
            TBOX_TEST_ASSERT_MSG(list.items[0].kind == TBOX_PAINT_FILL_RECT, "the highlight FILL_RECT must come before the TEXT_RUN");
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[0].rect, run.rect), "the highlight FILL_RECT must cover exactly run->rect");
            TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 255 && list.items[0].color.g == 255 && list.items[0].color.b == 0 && list.items[0].color.a == 255, "the highlight FILL_RECT must use run->style->background_color");
            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_TEXT_RUN, "op 1 must be the TEXT_RUN");
            TBOX_TEST_ASSERT_MSG(list.items[1].color.r == 1 && list.items[1].color.g == 2 && list.items[1].color.b == 3, "the TEXT_RUN's color must be run->style->color, unaffected by its own background");
        }
        tbox_arena_destroy(&arena);
    }

    /* 15:  -- a run with text_decoration == UNDERLINE produces an
     * extra thin FILL_RECT immediately AFTER its TEXT_RUN, 1px tall,
     * spanning run->rect.width from run->rect.x, at y = baseline + 2 (where
     * baseline = run->rect.y + tbox_font_face_ascent(run->font)), in
     * run->style->color. */
    {
        tbox_style box_style = tbox_test_render_default_style();
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run_style                = tbox_test_render_default_style();
        run_style.color                     = (tbox_css_rgba){ 11, 22, 33, 255 };
        run_style.text_decoration           = TBOX_STYLE_TEXT_DECORATION_UNDERLINE;
        run_style.text_decoration_color     = run_style.color;
        run_style.text_decoration_thickness = 1.0;

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 5.0, 10.0, 25.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("ins");
        run.font                 = font;
        run.style                = &run_style;

        box.text_runs      = &run;
        box.text_run_count = 1;

        double expected_y = run.rect.y + tbox_font_face_ascent(font) + 2.0;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "a run with UNDERLINE must produce exactly 2 ops: its TEXT_RUN + the decoration FILL_RECT");
        if (list.count == 2) {
            TBOX_TEST_ASSERT_MSG(list.items[0].kind == TBOX_PAINT_TEXT_RUN, "op 0 must still be the TEXT_RUN");
            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_FILL_RECT, "the decoration FILL_RECT must come after the TEXT_RUN");
            TBOX_TEST_ASSERT_MSG(list.items[1].rect.x == run.rect.x && list.items[1].rect.width == run.rect.width && list.items[1].rect.height == 1.0, "the underline FILL_RECT must span run->rect.x/width, 1px tall");
            TBOX_TEST_ASSERT_MSG(list.items[1].rect.y == expected_y, "the underline FILL_RECT must sit 2px below the run's baseline");
            TBOX_TEST_ASSERT_MSG(list.items[1].color.r == 11 && list.items[1].color.g == 22 && list.items[1].color.b == 33, "the underline FILL_RECT must use run->style->color, same as the text");
        }
        tbox_arena_destroy(&arena);
    }

    /* 16:  -- a run with text_decoration == LINE_THROUGH produces an
     * extra thin FILL_RECT after its TEXT_RUN at y = baseline -
     * ascent * 0.3, above the baseline (unlike UNDERLINE's below). */
    {
        tbox_style box_style = tbox_test_render_default_style();
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run_style                = tbox_test_render_default_style();
        run_style.color                     = (tbox_css_rgba){ 44, 55, 66, 255 };
        run_style.text_decoration           = TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH;
        run_style.text_decoration_color     = (tbox_css_rgba){ 200, 30, 20, 255 };
        run_style.text_decoration_thickness = 3.0;

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 5.0, 10.0, 25.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("del");
        run.font                 = font;
        run.style                = &run_style;

        box.text_runs      = &run;
        box.text_run_count = 1;

        double baseline   = run.rect.y + tbox_font_face_ascent(font);
        double expected_y = baseline - tbox_font_face_ascent(font) * 0.3;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "a run with LINE_THROUGH must produce exactly 2 ops: its TEXT_RUN + the decoration FILL_RECT");
        if (list.count == 2) {
            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_FILL_RECT, "the decoration FILL_RECT must come after the TEXT_RUN");
            TBOX_TEST_ASSERT_MSG(list.items[1].rect.y == expected_y, "the line-through FILL_RECT must sit ascent*0.3 above the run's baseline");
            TBOX_TEST_ASSERT_MSG(list.items[1].rect.y < baseline, "the line-through FILL_RECT must be above the baseline, unlike UNDERLINE below it");
            TBOX_TEST_ASSERT(list.items[1].rect.height == 3.0);
            TBOX_TEST_ASSERT(list.items[1].color.r == 200 && list.items[1].color.g == 30 && list.items[1].color.b == 20);
        }
        tbox_arena_destroy(&arena);
    }

    /* 17:  regression -- a run with a transparent background AND
     * text_decoration == NONE still produces only its TEXT_RUN, no extra
     * FILL_RECT (v0-v12 behavior), and that TEXT_RUN's color is the run's
     * own run->style->color, not a color shared with the box's style. */
    {
        tbox_style box_style = tbox_test_render_default_style();
        box_style.color      = (tbox_css_rgba){ 200, 200, 200, 255 }; /* deliberately different from the run's, to prove it's NOT used */
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run_style = tbox_test_render_default_style();
        run_style.color      = (tbox_css_rgba){ 9, 8, 7, 255 };
        /* run_style.background_color left transparent, text_decoration left NONE by tbox_test_render_default_style() */

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 0.0, 0.0, 20.0, 16.0 };
        run.text                 = tbox_test_render_view_from_cstr("plain");
        run.font                 = font;
        run.style                = &run_style;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "transparent run background + NONE decoration must produce only the TEXT_RUN, no extra FILL_RECT");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_TEXT_RUN);
            TBOX_TEST_ASSERT_MSG(list.items[0].color.r == 9 && list.items[0].color.g == 8 && list.items[0].color.b == 7, "TEXT_RUN color must be run->style->color, not box->style->color");
        }
        tbox_arena_destroy(&arena);
    }

    /* 18: NOVO (image support) -- a run whose `image` is non-NULL produces
     * an IMAGE op (never TEXT_RUN) with the run's rect and image pointer,
     * over its own background like a browser's <img>, and no text
     * decoration line (see tbox_render_walk's `continue` right after
     * pushing the IMAGE op). */
    {
        tbox_image fake_image = { 42, 24, (const unsigned char *)"fake pixel data" };

        /* box_style stays fully transparent/default (tbox_test_render_default_box
         * below), so the box ITSELF paints no FILL_RECT of its own -- the
         * only thing under test is whether the RUN's own background_color/
         * text_decoration (on a DIFFERENT style, run_style) get skipped. */
        tbox_style box_style = tbox_test_render_default_style();
        tbox_layout_box box  = tbox_test_render_default_box(&box_style);

        tbox_style run_style       = tbox_test_render_default_style();
        run_style.background_color = (tbox_css_rgba){ 1, 2, 3, 255 };      /* painted under the image */
        run_style.text_decoration  = TBOX_STYLE_TEXT_DECORATION_UNDERLINE; /* must also be IGNORED */
        run_style.image_rendering_pixelated = true;

        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 5.0, 6.0, 42.0, 24.0 };
        run.style                = &run_style;
        run.image                = &fake_image;

        box.text_runs      = &run;
        box.text_run_count = 1;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "an image run paints its background, then the image, and no decoration line");
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[0].color.r == 1);
            TBOX_TEST_ASSERT(list.items[1].kind == TBOX_PAINT_IMAGE);
            TBOX_TEST_ASSERT(list.items[1].image == &fake_image);
            TBOX_TEST_ASSERT(list.items[1].image_pixelated);
            TBOX_TEST_ASSERT_MSG(list.items[1].rect.x == 5.0 && list.items[1].rect.y == 6.0 && list.items[1].rect.width == 42.0 && list.items[1].rect.height == 24.0, "IMAGE op's rect must be the run's own rect");
        }
        tbox_arena_destroy(&arena);
    }

    /* 19: NOVO (visual fidelity) regression -- border_radius == 0.0 (the
     * default) must produce EXACTLY the same ops as before this feature
     * existed: one background FILL_RECT (radius 0.0) then 4 border
     * FILL_RECTs (radius 0.0 each), never the rounded-rect path. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 10, 20, 30, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width     = 2.0;
        style.border_color     = (tbox_css_rgba){ 40, 50, 60, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0, 0, 100, 50 };
        box.padding_box        = (tbox_rect){ 2, 2, 96, 46 };

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 5, "radius==0 must produce 1 background + 4 border strips, unchanged");
        for (size_t i = 0; i < list.count; i++) {
            TBOX_TEST_ASSERT_MSG(list.items[i].radius == 0.0, "radius==0 ops must all have radius 0.0 (the plain-rect path)");
        }
        tbox_arena_destroy(&arena);
    }

    /* Rounded borders paint as a ring over the rounded background. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 10, 20, 30, 255 };
        style.border_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width     = 2.0;
        style.border_color     = (tbox_css_rgba){ 40, 50, 60, 255 };
        style.border_radius    = 8.0;
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0, 0, 100, 50 };
        box.padding_box        = (tbox_rect){ 2, 2, 96, 46 };

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "radius>0 with a border must produce a rounded background and border ring");
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[0].radius == 8.0);
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[0].rect, box.border_box), "rounded background must cover border_box");
            TBOX_TEST_ASSERT(list.items[0].color.r == 10 && list.items[0].color.g == 20 && list.items[0].color.b == 30);

            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_FILL_RING && list.items[1].inner_corner_radii[0] == 6.0, "inner radius must be outer radius minus border_width (8 - 2 = 6)");
            TBOX_TEST_ASSERT_MSG(rect_equal(list.items[1].inner_rect, box.padding_box), "border ring must exclude padding_box");
            TBOX_TEST_ASSERT(list.items[1].color.r == 40 && list.items[1].color.g == 50 && list.items[1].color.b == 60);
        }
        tbox_arena_destroy(&arena);
    }

    /* 21: NOVO (visual fidelity) -- border_radius > 0.0 with NO border
     * produces exactly 1 rounded FILL_RECT in background_color. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 70, 80, 90, 255 };
        style.border_radius    = 12.0;
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0, 0, 40, 40 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 1, "radius>0 with no border must produce exactly 1 rounded FILL_RECT");
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].radius == 12.0);
            TBOX_TEST_ASSERT(list.items[0].color.r == 70 && list.items[0].color.g == 80 && list.items[0].color.b == 90);
        }
        tbox_arena_destroy(&arena);
    }

    /* 22: NOVO (visual fidelity) -- a radius exceeding half of
     * min(border_box.width, border_box.height) must clamp, not paint a
     * self-intersecting/broken shape. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 1, 1, 1, 255 };
        style.border_radius    = 1000.0;
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0, 0, 20, 10 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            TBOX_TEST_ASSERT_MSG(list.items[0].radius == 5.0, "radius must clamp to half of min(width,height) -- min(20,10)/2 = 5");
        }
        tbox_arena_destroy(&arena);
    }

    /* 23: NOVO (visual fidelity) -- box-shadow with blur == 0.0 produces
     * exactly ONE extra FILL_RECT, positioned at border_box offset by
     * (offset_x, offset_y), painted BEFORE the box's own background op. */
    {
        tbox_style style          = tbox_test_render_default_style();
        style.background_color    = (tbox_css_rgba){ 200, 200, 200, 255 };
        style.box_shadow_offset_x = 3.0;
        style.box_shadow_offset_y = 4.0;
        style.box_shadow_blur     = 0.0;
        style.box_shadow_color    = (tbox_css_rgba){ 0, 0, 0, 128 };
        tbox_layout_box box       = tbox_test_render_default_box(&style);
        box.border_box            = (tbox_rect){ 10, 10, 50, 50 };
        box.padding_box           = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count == 2, "box-shadow with blur==0 must produce exactly 1 shadow op before the background op");
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT);
            TBOX_TEST_ASSERT_MSG(list.items[0].rect.x == 13.0 && list.items[0].rect.y == 14.0 && list.items[0].rect.width == 50.0 && list.items[0].rect.height == 50.0, "shadow rect must be border_box offset by (offset_x, offset_y)");
            TBOX_TEST_ASSERT(list.items[0].color.a == 128);
            TBOX_TEST_ASSERT_MSG(list.items[1].kind == TBOX_PAINT_FILL_RECT && rect_equal(list.items[1].rect, box.border_box), "background op must still be the box's own border_box, unaffected by the shadow");
        }
        tbox_arena_destroy(&arena);
    }

    /* 24: NOVO (visual fidelity) -- box-shadow with blur > 0.0 produces
     * MULTIPLE shadow ops (the cheap multi-step falloff approximation),
     * all before the background op: the innermost (last) layer matches the
     * exact un-grown shadow rect, and the outermost (first) layer is
     * strictly larger -- proving the layers actually expand outward, not a
     * flat repeat of the same rect. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 200, 200, 200, 255 };
        style.box_shadow_blur  = 12.0;
        style.box_shadow_color = (tbox_css_rgba){ 0, 0, 0, 255 };
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0, 0, 100, 100 };
        box.padding_box        = box.border_box;

        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT_MSG(list.count > 2, "box-shadow with blur>0 must produce multiple shadow ops plus the background op");
        if (list.count > 2) {
            size_t shadow_count        = list.count - 1; /* everything except the final background op */
            tbox_paint_op last_shadow  = list.items[shadow_count - 1];
            tbox_paint_op first_shadow = list.items[0];
            TBOX_TEST_ASSERT_MSG(rect_equal(last_shadow.rect, box.border_box), "the innermost shadow layer must exactly match the un-grown shadow rect");
            TBOX_TEST_ASSERT_MSG(first_shadow.rect.width > last_shadow.rect.width, "outermost shadow layer must be larger than the innermost one");
            TBOX_TEST_ASSERT(list.items[shadow_count].kind == TBOX_PAINT_FILL_RECT && rect_equal(list.items[shadow_count].rect, box.border_box) && list.items[shadow_count].color.r == 200);
        }
        tbox_arena_destroy(&arena);
    }

    /* Hidden overflow clips descendants while its own background remains
     * visible. The same clip is used by auto, without a scrollbar here. */
    {
        tbox_style parent_style       = tbox_test_render_default_style();
        parent_style.overflow_y       = TBOX_STYLE_OVERFLOW_Y_HIDDEN;
        parent_style.background_color = (tbox_css_rgba){ 0, 0, 255, 255 };
        tbox_style child_style        = tbox_test_render_default_style();
        child_style.background_color  = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_layout_box parent        = tbox_test_render_default_box(&parent_style);
        tbox_layout_box child         = tbox_test_render_default_box(&child_style);
        parent.border_box = parent.padding_box = (tbox_rect){ 0, 0, 40, 20 };
        child.border_box = child.padding_box = (tbox_rect){ 0, 15, 40, 20 };
        parent.first_child = parent.last_child = &child;
        child.parent                           = &parent;
        tbox_arena arena                       = tbox_arena_create(0);
        tbox_display_list list                 = tbox_render_build_display_list(&arena, &parent);
        TBOX_TEST_ASSERT(list.count == 2);
        if (list.count == 2) {
            TBOX_TEST_ASSERT(!list.items[0].has_clip);
            TBOX_TEST_ASSERT(list.items[1].has_clip && rect_equal(list.items[1].clip, parent.padding_box));
        }
        tbox_arena_destroy(&arena);
    }

    /* A box's own overflowing text also obeys its clip. */
    {
        tbox_style style         = tbox_test_render_default_style();
        style.overflow_y         = TBOX_STYLE_OVERFLOW_Y_HIDDEN;
        tbox_layout_box box      = tbox_test_render_default_box(&style);
        box.padding_box          = (tbox_rect){ 0, 0, 40, 20 };
        tbox_layout_text_run run = { 0 };
        run.rect                 = (tbox_rect){ 0, 0, 100, 20 };
        run.text                 = tbox_test_render_view_from_cstr("long text");
        run.font                 = font;
        run.style                = &style;
        box.text_runs            = &run;
        box.text_run_count       = 1;
        tbox_arena arena         = tbox_arena_create(0);
        tbox_display_list list   = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1)
            TBOX_TEST_ASSERT(list.items[0].has_clip && rect_equal(list.items[0].clip, box.padding_box));
        tbox_arena_destroy(&arena);
    }

    /* A solid outline is painted outside the border box without changing it. */
    {
        tbox_style style    = tbox_test_render_default_style();
        style.outline_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.outline_width = 2.0;
        style.outline_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = (tbox_rect){ 10, 20, 40, 30 };
        tbox_arena arena                 = tbox_arena_create(0);
        tbox_display_list list           = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 4);
        if (list.count == 4) {
            TBOX_TEST_ASSERT(rect_equal(list.items[0].rect, (tbox_rect){ 8, 18, 44, 2 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[1].rect, (tbox_rect){ 8, 50, 44, 2 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[2].rect, (tbox_rect){ 8, 20, 2, 30 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[3].rect, (tbox_rect){ 50, 20, 2, 30 }));
        }
        tbox_arena_destroy(&arena);
    }

    /* A positive outline offset leaves a visible gap around the box. */
    {
        tbox_style style     = tbox_test_render_default_style();
        style.outline_style  = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.outline_width  = 2.0;
        style.outline_offset = 4.0;
        style.outline_color  = (tbox_css_rgba){ 0, 0, 255, 255 };
        tbox_layout_box box  = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = (tbox_rect){ 10, 20, 40, 30 };
        tbox_arena arena                 = tbox_arena_create(0);
        tbox_display_list list           = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 4);
        if (list.count == 4) {
            TBOX_TEST_ASSERT(rect_equal(list.items[0].rect, (tbox_rect){ 4, 14, 52, 2 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[1].rect, (tbox_rect){ 4, 54, 52, 2 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[2].rect, (tbox_rect){ 4, 16, 2, 38 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[3].rect, (tbox_rect){ 54, 16, 2, 38 }));
        }
        tbox_arena_destroy(&arena);
    }

    /* A visible descendant can paint inside a hidden parent; hidden boxes
     * still participate in layout but contribute no paint operations. */
    {
        tbox_style hidden        = tbox_test_render_default_style();
        hidden.visibility_hidden = true;
        hidden.background_color  = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_style visible       = tbox_test_render_default_style();
        visible.background_color = (tbox_css_rgba){ 0, 255, 0, 255 };
        tbox_layout_box parent   = tbox_test_render_default_box(&hidden);
        tbox_layout_box child    = tbox_test_render_default_box(&visible);
        parent.border_box = parent.padding_box = (tbox_rect){ 0, 0, 100, 30 };
        child.border_box = child.padding_box = (tbox_rect){ 0, 0, 50, 20 };
        parent.first_child = parent.last_child = &child;
        child.parent                           = &parent;
        tbox_arena arena                       = tbox_arena_create(0);
        tbox_display_list list                 = tbox_render_build_display_list(&arena, &parent);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1)
            TBOX_TEST_ASSERT(list.items[0].color.g == 255);
        tbox_arena_destroy(&arena);
    }

    /* Distinct outer and inner corner radii reach the display list. */
    {
        tbox_style style               = tbox_test_render_default_style();
        style.background_color         = (tbox_css_rgba){ 255, 255, 255, 255 };
        style.border_color             = (tbox_css_rgba){ 0, 0, 0, 255 };
        style.border_style             = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_width             = 2.0;
        style.border_radius_corners[0] = 8.0;
        style.border_radius_corners[1] = 4.0;
        style.border_radius_corners[2] = 0.0;
        style.border_radius_corners[3] = 12.0;
        tbox_layout_box box            = tbox_test_render_default_box(&style);
        box.border_box                 = (tbox_rect){ 0, 0, 100, 50 };
        box.padding_box                = (tbox_rect){ 2, 2, 96, 46 };
        tbox_arena arena               = tbox_arena_create(0);
        tbox_display_list list         = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2);
        if (list.count == 2) {
            const double outer[4] = { 8, 4, 0, 12 }, inner[4] = { 6, 2, 0, 10 };
            for (size_t i = 0; i < 4; i++) {
                TBOX_TEST_ASSERT(list.items[0].corner_radii[i] == outer[i]);
                TBOX_TEST_ASSERT(list.items[1].inner_corner_radii[i] == inner[i]);
            }
        }
        tbox_arena_destroy(&arena);
    }

    /* text-underline-offset moves the underline to baseline + offset. */
    {
        tbox_style box_style                = tbox_test_render_default_style();
        tbox_layout_box box                 = tbox_test_render_default_box(&box_style);
        tbox_style run_style                = tbox_test_render_default_style();
        run_style.text_decoration           = TBOX_STYLE_TEXT_DECORATION_UNDERLINE;
        run_style.text_decoration_thickness = 1.0;
        run_style.text_underline_offset     = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 6.0, 0.0, 0, 0.0, 0.0 };
        tbox_layout_text_run run            = { 0 };
        run.rect                            = (tbox_rect){ 5.0, 10.0, 25.0, 16.0 };
        run.text                            = tbox_test_render_view_from_cstr("ins");
        run.font                            = font;
        run.style                           = &run_style;
        box.text_runs                       = &run;
        box.text_run_count                  = 1;
        tbox_arena arena                    = tbox_arena_create(0);
        tbox_display_list list              = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2);
        if (list.count == 2)
            TBOX_TEST_ASSERT(list.items[1].rect.y == run.rect.y + tbox_font_face_ascent(font) + 6.0);
        tbox_arena_destroy(&arena);
    }

    /* Under places the line below the font's descender; an explicit offset
     * still wins over the automatic placement. */
    {
        tbox_style box_style = tbox_test_render_default_style();
        tbox_layout_box box = tbox_test_render_default_box(&box_style);
        tbox_style run_style = tbox_test_render_default_style();
        run_style.text_decoration = TBOX_STYLE_TEXT_DECORATION_UNDERLINE;
        run_style.text_decoration_thickness = 1.0;
        run_style.text_underline_position_under = true;
        tbox_layout_text_run run = { 0 };
        run.rect = (tbox_rect){ 5.0, 10.0, 25.0, 16.0 };
        run.text = tbox_test_render_view_from_cstr("under");
        run.font = font;
        run.style = &run_style;
        box.text_runs = &run;
        box.text_run_count = 1;
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2);
        if (list.count == 2)
            TBOX_TEST_ASSERT(list.items[1].rect.y == run.rect.y + tbox_font_face_line_height(font) + 1.0);
        tbox_arena_destroy(&arena);
    }

    /* accent-color paints a checked radio's dot; auto falls back to color. */
    {
        const char *html            = "<input type=radio checked>";
        tbox_html_document *doc     = tbox_html_parse(html, strlen(html));
        const tbox_html_node *input = tbox_html_document_root(doc)->first_child;
        for (int accent = 0; accent < 2; accent++) {
            tbox_style style = tbox_test_render_default_style();
            style.color      = (tbox_css_rgba){ 1, 2, 3, 255 };
            if (accent)
                style.accent_color = (tbox_css_rgba){ 200, 100, 50, 255 };
            tbox_layout_box box    = tbox_test_render_default_box(&style);
            box.node               = input;
            box.content_box        = (tbox_rect){ 0, 0, 12, 12 };
            tbox_arena arena       = tbox_arena_create(0);
            tbox_display_list list = tbox_render_build_display_list(&arena, &box);
            TBOX_TEST_ASSERT(list.count == 1);
            if (list.count == 1) {
                unsigned char expected_r = accent ? 200 : 1;
                TBOX_TEST_ASSERT(list.items[0].color.r == expected_r);
            }
            tbox_arena_destroy(&arena);
        }
        tbox_html_document_destroy(doc);
    }

    /* A solid outline follows the border radius (the radio's focus ring):
     * one ring whose inner radii are the border's plus the offset and
     * outer radii add the width; a square box keeps four straight sides. */
    for (int rounded = 0; rounded < 2; rounded++) {
        tbox_style style     = tbox_test_render_default_style();
        style.outline_style  = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.outline_width  = 2.0;
        style.outline_offset = -1.0;
        style.outline_color  = (tbox_css_rgba){ 0, 0, 255, 255 };
        style.border_radius  = rounded ? 7.0 : 0.0;
        tbox_layout_box box  = tbox_test_render_default_box(&style);
        box.border_box       = (tbox_rect){ 10, 10, 14, 14 };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        size_t rings = 0, sides = 0;
        for (size_t i = 0; i < list.count; i++) {
            const tbox_paint_op *op = &list.items[i];
            if (op->color.b != 255 || op->color.r != 0) continue;
            if (op->kind == TBOX_PAINT_FILL_RING) {
                rings++;
                TBOX_TEST_ASSERT(op->rect.x == 9.0 && op->rect.width == 16.0);         /* 1px outside the box */
                TBOX_TEST_ASSERT(op->inner_rect.x == 11.0 && op->inner_rect.width == 12.0); /* 1px inside it */
                TBOX_TEST_ASSERT(op->corner_radii[0] == 8.0 && op->inner_corner_radii[0] == 6.0);
            } else if (op->kind == TBOX_PAINT_FILL_RECT) {
                sides++;
            }
        }
        TBOX_TEST_ASSERT(rounded ? rings == 1 && sides == 0 : rings == 0 && sides == 4);
        tbox_arena_destroy(&arena);
    }

    /* text-shadow: one offset copy in the shadow color before the text; a
     * blur paints a 3x3 grid of fainter copies instead. */
    for (int blurred = 0; blurred < 2; blurred++) {
        tbox_style box_style           = tbox_test_render_default_style();
        tbox_layout_box box            = tbox_test_render_default_box(&box_style);
        tbox_style run_style           = tbox_test_render_default_style();
        run_style.text_shadow_offset_x = 2.0;
        run_style.text_shadow_offset_y = 3.0;
        run_style.text_shadow_blur     = blurred ? 4.0 : 0.0;
        run_style.text_shadow_color    = (tbox_css_rgba){ 10, 20, 30, 250 };
        tbox_layout_text_run run       = { 0 };
        run.rect                       = (tbox_rect){ 5.0, 10.0, 25.0, 16.0 };
        run.text                       = tbox_test_render_view_from_cstr("abc");
        run.font                       = font;
        run.style                      = &run_style;
        box.text_runs                  = &run;
        box.text_run_count             = 1;
        tbox_arena arena               = tbox_arena_create(0);
        tbox_display_list list         = tbox_render_build_display_list(&arena, &box);
        size_t copies                  = blurred ? 9 : 1;
        TBOX_TEST_ASSERT(list.count == copies + 1);
        if (list.count == copies + 1) {
            for (size_t i = 0; i <= copies; i++)
                TBOX_TEST_ASSERT(list.items[i].kind == TBOX_PAINT_TEXT_RUN);
            TBOX_TEST_ASSERT(list.items[copies].color.a == 255 && list.items[copies].rect.x == 5.0);
            TBOX_TEST_ASSERT(list.items[0].color.r == 10 && list.items[0].color.a == (blurred ? 50 : 250));
            double spread = blurred ? 2.0 : 0.0;
            TBOX_TEST_ASSERT(list.items[0].rect.x == 7.0 - spread && list.items[0].rect.y == 13.0 - spread);
            TBOX_TEST_ASSERT(list.items[copies - 1].rect.x == 7.0 + spread);
        }
        tbox_arena_destroy(&arena);
    }

    /* box-shadow spread grows the shadow rect on every side. */
    {
        tbox_style style          = tbox_test_render_default_style();
        style.box_shadow_offset_x = 1.0;
        style.box_shadow_spread   = 3.0;
        style.box_shadow_color    = (tbox_css_rgba){ 0, 0, 0, 255 };
        tbox_layout_box box       = tbox_test_render_default_box(&style);
        box.border_box            = (tbox_rect){ 10.0, 10.0, 20.0, 20.0 };
        tbox_arena arena          = tbox_arena_create(0);
        tbox_display_list list    = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            tbox_rect r = list.items[0].rect;
            TBOX_TEST_ASSERT(r.x == 8.0 && r.y == 7.0 && r.width == 26.0 && r.height == 26.0);
        }
        tbox_arena_destroy(&arena);
    }

    /* opacity puts the box and its descendants on a compositing layer
     * (LAYER_BEGIN carries it; the ops inside keep their own alpha); 0
     * drops the whole subtree. */
    for (int hidden = 0; hidden < 2; hidden++) {
        tbox_style parent_style       = tbox_test_render_default_style();
        parent_style.background_color = (tbox_css_rgba){ 10, 20, 30, 200 };
        parent_style.opacity          = hidden ? 0.0 : 0.5;
        tbox_style child_style        = tbox_test_render_default_style();
        child_style.background_color  = (tbox_css_rgba){ 40, 50, 60, 255 };
        tbox_layout_box parent        = tbox_test_render_default_box(&parent_style);
        tbox_layout_box child         = tbox_test_render_default_box(&child_style);
        tbox_image fake_image         = { 0 };
        tbox_style image_style        = tbox_test_render_default_style();
        tbox_layout_text_run run      = { 0 };
        run.rect                      = (tbox_rect){ 0.0, 0.0, 4.0, 4.0 };
        run.style                     = &image_style;
        run.image                     = &fake_image;
        child.text_runs               = &run;
        child.text_run_count          = 1;
        parent.first_child = parent.last_child = &child;
        tbox_layout_box sibling                = tbox_test_render_default_box(&child_style);
        parent.next_sibling                    = &sibling;
        tbox_arena arena                       = tbox_arena_create(0);
        tbox_display_list list                 = tbox_render_build_display_list(&arena, &parent);
        if (hidden) {
            TBOX_TEST_ASSERT(list.count == 1 && list.items[0].color.a == 255);
        } else {
            TBOX_TEST_ASSERT(list.count == 6);
            if (list.count == 6) {
                TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_LAYER_BEGIN && list.items[0].color.a == 128 && list.items[0].filters == NULL);
                TBOX_TEST_ASSERT(list.items[1].color.a == 200);
                TBOX_TEST_ASSERT(list.items[2].color.a == 255);
                TBOX_TEST_ASSERT(list.items[3].kind == TBOX_PAINT_IMAGE && list.items[3].color.a == 255);
                TBOX_TEST_ASSERT(list.items[4].kind == TBOX_PAINT_LAYER_END && list.items[5].color.a == 255);
            }
        }
        tbox_arena_destroy(&arena);
    }

    /* Per-side borders paint only their non-zero sides, each in its own
     * color. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.border_per_side  = true;
        const double widths[4] = { 1.0, 0.0, 3.0, 4.0 };
        for (size_t i = 0; i < 4; i++) {
            style.border_widths[i] = widths[i];
            style.border_styles[i] = TBOX_STYLE_BORDER_STYLE_SOLID;
            style.border_colors[i] = (tbox_css_rgba){ (unsigned char)(i + 1), 0, 0, 255 };
        }
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0.0, 0.0, 50.0, 30.0 };
        box.padding_box        = (tbox_rect){ 4.0, 1.0, 46.0, 26.0 };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 3);
        if (list.count == 3) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 1 && list.items[0].rect.height == 1.0);
            TBOX_TEST_ASSERT(list.items[1].color.r == 3 && list.items[1].rect.height == 3.0);
            TBOX_TEST_ASSERT(list.items[2].color.r == 4 && list.items[2].rect.width == 4.0);
        }
        tbox_arena_destroy(&arena);
    }

    /* A box dashed on all sides, 10px: the top strip (222px, corners
     * included) is a corner square plus half a dash at each end and 8 equal
     * segments of 202/8 between them, as Firefox draws it. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.border_per_side = true;
        for (size_t i = 0; i < 4; i++) {
            style.border_widths[i] = 10.0;
            style.border_styles[i] = TBOX_STYLE_BORDER_STYLE_DASHED;
            style.border_colors[i] = (tbox_css_rgba){ 9, 9, 9, 255 };
        }
        tbox_layout_box box    = tbox_test_render_default_box(&style);
        box.border_box         = (tbox_rect){ 0.0, 0.0, 222.0, 152.0 };
        box.padding_box        = (tbox_rect){ 10.0, 10.0, 202.0, 132.0 };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_REQUIRE(list.count >= 5);
        double segment = 202.0 / 8.0;
        TBOX_TEST_ASSERT(list.items[0].rect.x == 0.0 && fabs(list.items[0].rect.width - (10.0 + segment / 2.0)) < 1e-9);
        TBOX_TEST_ASSERT(fabs(list.items[1].rect.x - (10.0 + 1.5 * segment)) < 1e-9 && fabs(list.items[1].rect.width - segment) < 1e-9);
        TBOX_TEST_ASSERT(fabs(list.items[4].rect.x + list.items[4].rect.width - 222.0) < 1e-9);
        tbox_arena_destroy(&arena);
    }

    /* A 40px-long top border: double (3px) = two 1px bands at the strip's
     * edges, dashed (2px) = 7 equal segments of 40/7 (4 dashes, one at
     * each end), dotted
     * (2px) = round 2px dots. Only the top side is painted. */
    {
        const tbox_style_border_style styles[3] = { TBOX_STYLE_BORDER_STYLE_DOUBLE, TBOX_STYLE_BORDER_STYLE_DASHED, TBOX_STYLE_BORDER_STYLE_DOTTED };
        for (size_t k = 0; k < 3; k++) {
            tbox_style style       = tbox_test_render_default_style();
            style.border_per_side  = true;
            style.border_widths[0] = styles[k] == TBOX_STYLE_BORDER_STYLE_DOUBLE ? 3.0 : 2.0;
            style.border_styles[0] = styles[k];
            style.border_colors[0] = (tbox_css_rgba){ 9, 9, 9, 255 };
            tbox_layout_box box    = tbox_test_render_default_box(&style);
            double w               = style.border_widths[0];
            box.border_box         = (tbox_rect){ 0.0, 0.0, 40.0, 20.0 };
            box.padding_box        = (tbox_rect){ 0.0, w, 40.0, 20.0 - w };
            tbox_arena arena       = tbox_arena_create(0);
            tbox_display_list list = tbox_render_build_display_list(&arena, &box);
            if (k == 0) {
                TBOX_TEST_ASSERT(list.count == 2);
                if (list.count == 2) {
                    TBOX_TEST_ASSERT(list.items[0].rect.y == 0.0 && list.items[0].rect.height == 1.0);
                    TBOX_TEST_ASSERT(list.items[1].rect.y == 2.0 && list.items[1].rect.width == 40.0);
                }
            } else {
                size_t expected = k == 1 ? 4 : 10;
                TBOX_TEST_ASSERT(list.count == expected);
                if (list.count == expected) {
                    const tbox_paint_op *last = &list.items[list.count - 1];
                    TBOX_TEST_ASSERT(list.items[0].rect.x == 0.0);
                    TBOX_TEST_ASSERT(fabs(last->rect.x + last->rect.width - 40.0) < 1e-9);
                    TBOX_TEST_ASSERT(fabs(list.items[0].rect.width - (k == 1 ? 40.0 / 7.0 : 2.0)) < 1e-9);
                    TBOX_TEST_ASSERT(k == 1 ? list.items[0].corner_radii[0] == 0.0 : list.items[0].corner_radii[0] == 1.0);
                }
            }
            tbox_arena_destroy(&arena);
        }
    }

    /* A percentage radius resolves against the smaller border-box side. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 1, 2, 3, 255 };
        for (size_t i = 0; i < 4; i++)
            style.border_radius_percent[i] = 25.0;
        style.border_radius_corners[1] = 2.0;
        tbox_layout_box box            = tbox_test_render_default_box(&style);
        box.border_box                 = (tbox_rect){ 0.0, 0.0, 80.0, 40.0 };
        tbox_arena arena               = tbox_arena_create(0);
        tbox_display_list list         = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].corner_radii[0] == 10.0);
            TBOX_TEST_ASSERT(list.items[0].corner_radii[1] == 12.0);
        }
        tbox_arena_destroy(&arena);
    }

    /* object-fit changes the image destination without changing its CSS
     * box; cover clips the larger destination to that original box. */
    {
        tbox_image image = { 0 };
        image.width = 200;
        image.height = 100;
        tbox_style style = tbox_test_render_default_style();
        tbox_layout_text_run run = { 0 };
        run.rect = (tbox_rect){ 10.0, 20.0, 100.0, 100.0 };
        run.image = &image;
        run.style = &style;
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.text_runs = &run;
        box.text_run_count = 1;
        tbox_arena arena = tbox_arena_create(0);
        style.object_fit = TBOX_STYLE_OBJECT_FIT_CONTAIN;
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ 10.0, 45.0, 100.0, 50.0 }) && !list.items[0].has_clip);
        tbox_arena_destroy(&arena);
        arena = tbox_arena_create(0);
        style.object_fit = TBOX_STYLE_OBJECT_FIT_COVER;
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ -40.0, 20.0, 200.0, 100.0 }) && list.items[0].has_clip && rect_equal(list.items[0].clip, run.rect));
        tbox_arena_destroy(&arena);
        arena = tbox_arena_create(0);
        style.object_fit = TBOX_STYLE_OBJECT_FIT_CONTAIN;
        style.object_position[0] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
        style.object_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 };
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ 10.0, 70.0, 100.0, 50.0 }));
        tbox_arena_destroy(&arena);
        arena = tbox_arena_create(0);
        style.object_fit = TBOX_STYLE_OBJECT_FIT_COVER;
        style.object_position[0] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 };
        style.object_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ -90.0, 20.0, 200.0, 100.0 }) && list.items[0].has_clip && rect_equal(list.items[0].clip, run.rect));
        tbox_arena_destroy(&arena);

        /* none keeps intrinsic pixels and crops; scale-down chooses the
         * smaller of intrinsic size and contain's fitted size. */
        style.object_position[0] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
        style.object_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
        style.object_fit = TBOX_STYLE_OBJECT_FIT_NONE;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ -40.0, 20.0, 200.0, 100.0 }) && list.items[0].has_clip && rect_equal(list.items[0].clip, run.rect));
        tbox_arena_destroy(&arena);
        style.object_fit = TBOX_STYLE_OBJECT_FIT_SCALE_DOWN;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ 10.0, 45.0, 100.0, 50.0 }) && !list.items[0].has_clip);
        tbox_arena_destroy(&arena);

        run.rect = (tbox_rect){ 10.0, 20.0, 300.0, 200.0 };
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && rect_equal(list.items[0].rect, (tbox_rect){ 60.0, 70.0, 200.0, 100.0 }) && !list.items[0].has_clip);
        tbox_arena_destroy(&arena);
    }

    /* A combined decoration emits a stroke for each selected line. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.text_decoration_lines = 5u;
        style.text_decoration_style = TBOX_STYLE_BORDER_STYLE_DOUBLE;
        style.text_decoration_thickness = 1.0;
        style.text_decoration_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_layout_text_run run = { 0 };
        run.rect = (tbox_rect){ 0.0, 0.0, 40.0, 20.0 };
        run.text = tbox_test_render_view_from_cstr("Hi");
        run.font = font;
        run.style = &style;
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.text_runs = &run;
        box.text_run_count = 1;
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 5 && list.items[0].kind == TBOX_PAINT_TEXT_RUN);
        tbox_arena_destroy(&arena);
        style.text_decoration_style = TBOX_STYLE_BORDER_STYLE_WAVY;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 3 && list.items[1].kind == TBOX_PAINT_WAVY_LINE && list.items[2].kind == TBOX_PAINT_WAVY_LINE);
        tbox_arena_destroy(&arena);
    }

    /* background-clip uses the selected box. Rounded content-box keeps
     * padding transparent while retaining a curved border ring. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 0, 0, 255, 255 };
        style.border_width = 2.0;
        style.border_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = (tbox_rect){ 0, 0, 40, 30 };
        box.padding_box = (tbox_rect){ 2, 2, 36, 26 };
        box.content_box = (tbox_rect){ 6, 6, 28, 18 };
        const tbox_rect expected[3] = { box.border_box, box.padding_box, box.content_box };
        for (int mode = 0; mode < 3; mode++) {
            style.background_clip = (tbox_style_background_clip)mode;
            tbox_arena arena = tbox_arena_create(0);
            tbox_display_list list = tbox_render_build_display_list(&arena, &box);
            TBOX_TEST_ASSERT(list.count == 5 && list.items[0].kind == TBOX_PAINT_FILL_RECT && rect_equal(list.items[0].rect, expected[mode]));
            tbox_arena_destroy(&arena);
        }
        style.background_clip = TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX;
        style.border_radius = 8.0;
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2 && list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[1].kind == TBOX_PAINT_FILL_RING);
        if (list.count == 2) {
            TBOX_TEST_ASSERT(rect_equal(list.items[0].rect, box.content_box));
            TBOX_TEST_ASSERT(rect_equal(list.items[1].inner_rect, box.padding_box));
            uint32_t pixels[40 * 30];
            for (size_t i = 0; i < 40u * 30u; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 40, 30, &list);
            TBOX_TEST_ASSERT(pixels[1 * 40 + 20] == 0xFFFF0000u);
            TBOX_TEST_ASSERT(pixels[4 * 40 + 20] == 0xFFFFFFFFu);
            TBOX_TEST_ASSERT(pixels[10 * 40 + 20] == 0xFF0000FFu);
            TBOX_TEST_ASSERT(pixels[0] == 0xFFFFFFFFu);
        }
        tbox_arena_destroy(&arena);

        /* A translucent curved border blends with the background only
         * when the background extends through the border box. */
        style.border_color.a = 128;
        uint32_t border_pixels[2];
        for (int mode = 0; mode < 2; mode++) {
            style.background_clip = (tbox_style_background_clip)mode;
            arena = tbox_arena_create(0);
            list = tbox_render_build_display_list(&arena, &box);
            uint32_t pixels[40 * 30];
            for (size_t i = 0; i < 40u * 30u; i++) pixels[i] = 0xFFFFFFFFu;
            tbox_raster_display_list(pixels, 40, 30, &list);
            border_pixels[mode] = pixels[1 * 40 + 20];
            tbox_arena_destroy(&arena);
        }
        TBOX_TEST_ASSERT(border_pixels[0] != border_pixels[1]);
        style.border_radius = 100.0;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2 && list.items[1].kind == TBOX_PAINT_FILL_RING);
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[1].corner_radii[0] == 15.0);
            TBOX_TEST_ASSERT(list.items[1].inner_corner_radii[0] == 13.0);
        }
        tbox_arena_destroy(&arena);
    }

    /* border-image: nine slices instead of the border fills -- corners
     * stretched, `repeat` edges centered tiles clipped to the edge, `fill`
     * the middle first; outset grows the area; a gradient source is laid
     * out at the area size; a missing image falls back to the border. */
    {
        unsigned char pixels[30 * 30 * 4] = { 0 };
        tbox_image image = { 30, 30, pixels };
        tbox_style style = tbox_test_render_default_style();
        tbox_style_border_image border_image = *tbox_style_border_image_of(&style);
        style.border_image = &border_image;
        style.border_width = 10.0;
        style.border_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        style.border_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        style.has_border_image = true;
        strcpy(border_image.image, "b.png");
        for (size_t i = 0; i < 4; i++) {
            border_image.slice[i]  = (tbox_style_border_image_value){ TBOX_STYLE_BORDER_IMAGE_NUMBER, 10.0, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
            border_image.width[i]  = (tbox_style_border_image_value){ TBOX_STYLE_BORDER_IMAGE_NUMBER, 1.0, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
            border_image.outset[i] = (tbox_style_border_image_value){ TBOX_STYLE_BORDER_IMAGE_NUMBER, 0.0, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
        }
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box  = box.margin_box = (tbox_rect){ 0, 0, 100, 60 };
        box.padding_box = box.content_box = (tbox_rect){ 10, 10, 80, 40 };
        box.border_image = &image;
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 8);
        for (size_t i = 0; i < list.count; i++)
            TBOX_TEST_ASSERT(list.items[i].kind == TBOX_PAINT_IMAGE && list.items[i].image == &image && list.items[i].has_source_rect);
        if (list.count == 8) {
            TBOX_TEST_ASSERT(rect_equal(list.items[0].rect, (tbox_rect){ 0, 0, 10, 10 }) && rect_equal(list.items[0].source_rect, (tbox_rect){ 0, 0, 10, 10 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[3].rect, (tbox_rect){ 90, 50, 10, 10 }) && rect_equal(list.items[3].source_rect, (tbox_rect){ 20, 20, 10, 10 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[4].rect, (tbox_rect){ 10, 0, 80, 10 }) && rect_equal(list.items[4].source_rect, (tbox_rect){ 10, 0, 10, 10 }));
            TBOX_TEST_ASSERT(rect_equal(list.items[6].rect, (tbox_rect){ 0, 10, 10, 40 }) && rect_equal(list.items[6].source_rect, (tbox_rect){ 0, 10, 10, 10 }));
        }
        tbox_arena_destroy(&arena);

        /* horizontal repeat: 10px tiles centered on the 80px top/bottom
         * edges (and the `fill` middle, stretched vertically) start at
         * x 5, so 9 tiles each, the first clipped to x 10. */
        border_image.repeat[0] = TBOX_STYLE_BORDER_IMAGE_REPEAT;
        border_image.fill      = true;
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 9 + 4 + 9 + 9 + 2);
        if (list.count == 33) {
            TBOX_TEST_ASSERT(rect_equal(list.items[0].rect, (tbox_rect){ 5, 10, 10, 40 }) && rect_equal(list.items[0].source_rect, (tbox_rect){ 10, 10, 10, 10 }));
            TBOX_TEST_ASSERT(list.items[13].rect.x == 5.0 && list.items[13].rect.y == 0.0 && list.items[13].clip.x == 10.0 && list.items[13].clip.width == 5.0);
            TBOX_TEST_ASSERT(list.items[21].rect.x == 85.0 && list.items[21].clip.width == 5.0);
        }
        tbox_arena_destroy(&arena);

        /* outset 1 (x border width) and a gradient: corners move out by
         * 10px, the gradient box is the 120x80 area */
        border_image.repeat[0] = TBOX_STYLE_BORDER_IMAGE_STRETCH;
        border_image.fill      = false;
        border_image.image[0]  = '\0';
        border_image.gradient.kind       = TBOX_STYLE_GRADIENT_LINEAR;
        border_image.gradient.stop_count = 2;
        for (size_t i = 0; i < 4; i++)
            border_image.outset[i].number = 1.0;
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 8);
        if (list.count == 8) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_GRADIENT && list.items[0].has_source_rect && list.items[0].source_rect.width == 120.0 && list.items[0].source_rect.height == 80.0);
            TBOX_TEST_ASSERT(rect_equal(list.items[0].clip, (tbox_rect){ -10, -10, 10, 10 }));
        }
        tbox_arena_destroy(&arena);

        /* an image that failed to load: the ordinary border paints */
        border_image.gradient.kind = TBOX_STYLE_GRADIENT_NONE;
        strcpy(border_image.image, "b.png");
        box.border_image = NULL;
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 4 && list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[0].color.r == 255);
        tbox_arena_destroy(&arena);
    }

    /* Multiple shadows paint last-first under the background; an inset
     * one paints after the background as a ring clipped to the padding
     * box; a gradient background emits a GRADIENT op over the padding box;
     * elliptical radii mark the fill elliptical. */
    {
        tbox_style style       = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 255, 255, 255, 255 };
        style.box_shadow_count = 3;
        style.box_shadows[0]   = (tbox_style_shadow){ 2, 2, 0, 0, { 255, 0, 0, 255 }, false };
        style.box_shadows[1]   = (tbox_style_shadow){ 4, 4, 0, 0, { 0, 255, 0, 255 }, false };
        style.box_shadows[2]   = (tbox_style_shadow){ 0, 0, 0, 3, { 0, 0, 255, 255 }, true };
        style.background_gradient.kind       = TBOX_STYLE_GRADIENT_LINEAR;
        style.background_gradient.stop_count = 2;
        style.background_size[0] = style.background_size[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        style.background_position[0] = style.background_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = box.margin_box = (tbox_rect){ 10, 10, 40, 20 };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 5);
        if (list.count == 5) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[0].color.g == 255); /* second shadow first */
            TBOX_TEST_ASSERT(list.items[1].kind == TBOX_PAINT_FILL_RECT && list.items[1].color.r == 255 && list.items[1].rect.x == 12.0);
            TBOX_TEST_ASSERT(list.items[2].kind == TBOX_PAINT_FILL_RECT && list.items[2].color.b == 255 && list.items[2].color.r == 255); /* background */
            TBOX_TEST_ASSERT(list.items[3].kind == TBOX_PAINT_GRADIENT && rect_equal(list.items[3].rect, box.padding_box));
            TBOX_TEST_ASSERT(list.items[4].kind == TBOX_PAINT_FILL_RING && list.items[4].has_rounded_clip && list.items[4].inner_rect.x == 13.0);
        }
        tbox_arena_destroy(&arena);

        style = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 0, 0, 255, 255 };
        for (size_t i = 0; i < 4; i++) {
            style.border_radius_corners[i]  = 20.0;
            style.border_radius_vertical[i] = 5.0;
        }
        box   = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = box.margin_box = (tbox_rect){ 0, 0, 40, 20 };
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1 && list.items[0].elliptical && list.items[0].corner_radii[0] == 20.0 && list.items[0].corner_radii_y[0] == 5.0);
        tbox_arena_destroy(&arena);
    }

    /* z-index: positioned siblings paint by z-index, negatives under the
     * stacking context's other children; an opacity box keeps its
     * z-indexed descendants inside its own range. */
    {
        tbox_style root_style = tbox_test_render_default_style();
        tbox_style styles[3];
        const int z[3] = { 2, -1, 1 };
        const unsigned char red[3] = { 10, 20, 30 };
        tbox_layout_box root = tbox_test_render_default_box(&root_style);
        tbox_layout_box kids[3];
        for (int i = 0; i < 3; i++) {
            styles[i]                  = tbox_test_render_default_style();
            styles[i].position         = TBOX_STYLE_POSITION_ABSOLUTE;
            styles[i].z_index          = z[i];
            styles[i].background_color = (tbox_css_rgba){ red[i], 0, 0, 255 };
            kids[i]                    = tbox_test_render_default_box(&styles[i]);
            kids[i].border_box         = (tbox_rect){ 0, 0, 10, 10 };
            kids[i].parent             = &root;
            if (i > 0)
                kids[i - 1].next_sibling = &kids[i];
        }
        tbox_style plain = tbox_test_render_default_style();
        plain.background_color = (tbox_css_rgba){ 99, 0, 0, 255 };
        tbox_layout_box flow = tbox_test_render_default_box(&plain);
        flow.border_box = (tbox_rect){ 0, 0, 10, 10 };
        flow.parent = &root;
        kids[2].next_sibling = &flow;
        root.first_child = &kids[0];
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &root);
        TBOX_TEST_ASSERT(list.count == 4);
        if (list.count == 4) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 20); /* z -1 */
            TBOX_TEST_ASSERT(list.items[1].color.r == 99); /* in flow */
            TBOX_TEST_ASSERT(list.items[2].color.r == 30); /* z 1 */
            TBOX_TEST_ASSERT(list.items[3].color.r == 10); /* z 2 */
        }
        tbox_arena_destroy(&arena);
    }

    /* isolation: isolate makes an in-flow box a stacking context, so its
     * z-index:-1 child paints over the box's background, not under it. */
    for (int isolate = 0; isolate < 2; isolate++) {
        tbox_style root_style = tbox_test_render_default_style();
        tbox_style host_style = tbox_test_render_default_style();
        tbox_style kid_style  = tbox_test_render_default_style();
        host_style.background_color  = (tbox_css_rgba){ 50, 0, 0, 255 };
        host_style.isolation_isolate = isolate != 0;
        kid_style.position           = TBOX_STYLE_POSITION_ABSOLUTE;
        kid_style.z_index            = -1;
        kid_style.z_index_auto       = false;
        kid_style.background_color   = (tbox_css_rgba){ 60, 0, 0, 255 };
        tbox_layout_box root = tbox_test_render_default_box(&root_style);
        tbox_layout_box host = tbox_test_render_default_box(&host_style);
        tbox_layout_box kid  = tbox_test_render_default_box(&kid_style);
        host.border_box = kid.border_box = (tbox_rect){ 0, 0, 10, 10 };
        host.parent = &root, root.first_child = &host;
        kid.parent = &host, host.first_child = &kid;
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &root);
        /* isolated: a layer around the host and its child */
        size_t first = isolate ? 1 : 0;
        TBOX_TEST_ASSERT(list.count == (isolate ? 4u : 2u));
        if (list.count == (isolate ? 4u : 2u)) {
            TBOX_TEST_ASSERT(list.items[first].color.r == (isolate ? 50 : 60) && list.items[first + 1].color.r == (isolate ? 60 : 50));
            if (isolate) TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_LAYER_BEGIN && list.items[3].kind == TBOX_PAINT_LAYER_END);
        }
        tbox_arena_destroy(&arena);
    }

    /* 3D borders shade by side; filter transforms solid colors and hands
     * images/gradients a matrix; clip-path sets a rounded clip on the
     * subtree's ops; a dashed rounded border becomes round stamps. */
    {
        tbox_style style = tbox_test_render_default_style();
        for (size_t i = 0; i < 4; i++) {
            style.border_widths[i] = 4.0;
            style.border_styles[i] = TBOX_STYLE_BORDER_STYLE_INSET;
            style.border_colors[i] = (tbox_css_rgba){ 150, 150, 150, 255 };
        }
        style.border_width = 4.0;
        style.border_style = TBOX_STYLE_BORDER_STYLE_INSET;
        style.border_color = (tbox_css_rgba){ 150, 150, 150, 255 };
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box  = (tbox_rect){ 0, 0, 40, 20 };
        box.padding_box = (tbox_rect){ 4, 4, 32, 12 };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        /* Each side in 1px bands mitered at the corners: 4 per side. */
        TBOX_TEST_ASSERT(list.count == 16);
        if (list.count == 16) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 100); /* top: darker */
            TBOX_TEST_ASSERT(list.items[0].rect.x == 0.0 && list.items[0].rect.width == 40.0);
            TBOX_TEST_ASSERT(list.items[3].rect.x == 3.0 && list.items[3].rect.width == 34.0);
            TBOX_TEST_ASSERT(list.items[4].color.r == 150); /* bottom: the color itself */
        }
        tbox_arena_destroy(&arena);

        style = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        style.has_filter       = true;
        memset(style.filter_matrix, 0, sizeof(style.filter_matrix));
        style.filter_matrix[0] = style.filter_matrix[6] = style.filter_matrix[12] = -1.0;
        style.filter_matrix[4] = style.filter_matrix[9] = style.filter_matrix[14] = 1.0;
        style.filter_matrix[18] = 1.0; /* invert(1) */
        tbox_style_clip_path circle = *tbox_style_clip_path_of(&style);
        circle.kind                 = TBOX_STYLE_CLIP_PATH_CIRCLE;
        circle.center[0] = circle.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
        style.clip_path = &circle;
        box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = (tbox_rect){ 0, 0, 40, 20 };
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].color.r == 0 && list.items[0].color.g == 255 && list.items[0].color.b == 255);
            TBOX_TEST_ASSERT(list.items[0].has_rounded_clip && list.items[0].rounded_clip.width == 20.0 && list.items[0].rounded_clip_radii[0] == 10.0); /* closest side */
        }
        tbox_arena_destroy(&arena);

        style = tbox_test_render_default_style();
        for (size_t i = 0; i < 4; i++) {
            style.border_widths[i] = 2.0;
            style.border_styles[i] = TBOX_STYLE_BORDER_STYLE_DOTTED;
            style.border_colors[i] = (tbox_css_rgba){ 0, 0, 255, 255 };
            style.border_radius_corners[i] = 10.0;
        }
        style.border_per_side = true;
        box = tbox_test_render_default_box(&style);
        box.border_box  = (tbox_rect){ 0, 0, 60, 40 };
        box.padding_box = (tbox_rect){ 2, 2, 56, 36 };
        arena = tbox_arena_create(0);
        list  = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count > 20);
        if (list.count > 0)
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_FILL_RECT && list.items[0].rect.width == 2.0 && list.items[0].radius == 1.0);
        tbox_arena_destroy(&arena);
    }

    /* `space` distributes fixed tiles; `round` resizes them to an integer fit. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.background_gradient.kind = TBOX_STYLE_GRADIENT_LINEAR;
        style.background_gradient.stop_count = 2;
        style.background_size_kind = TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT;
        style.background_size[0] = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 30.0, 0.0, 0, 0.0, 0.0 };
        style.background_size[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 18.0, 0.0, 0, 0.0, 0.0 };
        style.background_repeat_x = style.background_repeat_y = true;
        style.background_repeat_mode_x = style.background_repeat_mode_y = TBOX_STYLE_BACKGROUND_REPEAT_SPACE;
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = (tbox_rect){ 0, 0, 100, 50 };
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 6);
        if (list.count == 6) {
            TBOX_TEST_ASSERT(list.items[0].kind == TBOX_PAINT_GRADIENT && list.items[0].rect.x == 0.0 && list.items[0].rect.y == 0.0);
            TBOX_TEST_ASSERT(list.items[1].rect.x == 35.0 && list.items[2].rect.x == 70.0 && list.items[3].rect.y == 32.0);
        }
        tbox_arena_destroy(&arena);

        style.background_repeat_mode_x = style.background_repeat_mode_y = TBOX_STYLE_BACKGROUND_REPEAT_ROUND;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 9);
        if (list.count == 9) {
            TBOX_TEST_ASSERT(fabs(list.items[0].rect.width - 100.0 / 3.0) < 0.001);
            TBOX_TEST_ASSERT(fabs(list.items[0].rect.height - 50.0 / 3.0) < 0.001);
            TBOX_TEST_ASSERT(fabs(list.items[8].rect.x - 200.0 / 3.0) < 0.001);
        }
        tbox_arena_destroy(&arena);

        /* An auto opposite axis keeps an image's aspect ratio after round. */
        style.background_gradient.kind = TBOX_STYLE_GRADIENT_NONE;
        strcpy(style.background_image, "tile.png");
        style.background_size[1].kind = TBOX_STYLE_LENGTH_AUTO;
        style.background_repeat_mode_y = TBOX_STYLE_BACKGROUND_REPEAT_NO_REPEAT;
        style.background_repeat_y = false;
        tbox_image image = { .width = 30, .height = 10, .pixels = NULL };
        box.background_image = &image;
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 3);
        if (list.count == 3)
            TBOX_TEST_ASSERT(fabs(list.items[0].rect.height - 100.0 / 9.0) < 0.001);
        tbox_arena_destroy(&arena);
    }

    /* A fixed background positions its tile against the viewport while
     * retaining the element's own background clip. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.background_gradient.kind = TBOX_STYLE_GRADIENT_LINEAR;
        style.background_gradient.stop_count = 2;
        style.background_attachment_fixed = true;
        style.background_size_kind = TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT;
        style.background_size[0] = style.background_size[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 20.0, 0.0, 0, 0.0, 0.0 };
        style.background_position[0] = style.background_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 25.0, 0.0, 0, 0.0, 0.0 };
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = (tbox_rect){ 40, 20, 80, 50 };
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list_in_viewport(&arena, &box, 200.0, 100.0);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].rect.x == 45.0 && list.items[0].rect.y == 20.0);
            TBOX_TEST_ASSERT(list.items[0].clip.x == 40.0 && list.items[0].clip.width == 80.0);
        }
        tbox_arena_destroy(&arena);
    }

    /* Rounded inset percentages refer to the border box, then all
     * elliptical radii shrink together to fit the resulting rectangle. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.background_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        tbox_style_clip_path inset = *tbox_style_clip_path_of(&style);
        inset.kind                 = TBOX_STYLE_CLIP_PATH_INSET;
        for (size_t i = 0; i < 4; i++) {
            inset.inset[i]           = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 10, 0, 0, 0, 0 };
            inset.round_h_percent[i] = 25;
            inset.round_v_percent[i] = 25;
        }
        style.clip_path = &inset;
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.border_box = box.padding_box = box.content_box = (tbox_rect){ 0, 0, 200, 100 };
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            const tbox_paint_op *op = &list.items[0];
            TBOX_TEST_ASSERT(op->has_rounded_clip && op->rounded_clip.x == 10 && op->rounded_clip.y == 10);
            TBOX_TEST_ASSERT(op->rounded_clip.width == 180 && op->rounded_clip.height == 80);
            TBOX_TEST_ASSERT(op->rounded_clip_radii[0] == 50 && op->rounded_clip_radii_y[0] == 25);
        }
        tbox_arena_destroy(&arena);
        for (size_t i = 0; i < 4; i++) {
            inset.round_h_percent[i] = 100;
            inset.round_v_percent[i] = 50;
        }
        arena = tbox_arena_create(0);
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 1);
        if (list.count == 1) {
            TBOX_TEST_ASSERT(list.items[0].rounded_clip_radii[0] == 90);
            TBOX_TEST_ASSERT(list.items[0].rounded_clip_radii_y[0] == 22.5);
        }
        tbox_arena_destroy(&arena);
    }

    /* A float paints after the in-flow block that follows it (whose
     * background passes under the float), before positioned boxes, and
     * hit testing follows the same order. */
    {
        tbox_html_node node;
        memset(&node, 0, sizeof(node));
        tbox_style root_style  = tbox_test_render_default_style();
        tbox_style float_style = tbox_test_render_default_style();
        tbox_style block_style = tbox_test_render_default_style();
        tbox_style pos_style   = tbox_test_render_default_style();
        float_style.float_side       = TBOX_STYLE_FLOAT_LEFT;
        float_style.background_color = (tbox_css_rgba){ 255, 0, 0, 255 };
        block_style.background_color = (tbox_css_rgba){ 0, 0, 255, 255 };
        pos_style.position           = TBOX_STYLE_POSITION_RELATIVE;
        pos_style.z_index_auto       = true;
        pos_style.background_color   = (tbox_css_rgba){ 0, 255, 0, 255 };
        tbox_layout_box root = tbox_test_render_default_box(&root_style), positioned = tbox_test_render_default_box(&pos_style);
        tbox_layout_box floating = tbox_test_render_default_box(&float_style), block = tbox_test_render_default_box(&block_style);
        root.border_box = root.padding_box = root.content_box = (tbox_rect){ 0, 0, 200, 100 };
        positioned.border_box = positioned.padding_box = positioned.content_box = (tbox_rect){ 0, 0, 30, 30 };
        floating.border_box = floating.padding_box = floating.content_box = (tbox_rect){ 0, 0, 50, 50 };
        block.border_box = block.padding_box = block.content_box = (tbox_rect){ 0, 0, 200, 80 };
        positioned.node = floating.node = block.node = &node;
        root.first_child = &positioned;
        positioned.next_sibling = &floating;
        floating.next_sibling = &block;
        root.last_child = &block;
        positioned.parent = floating.parent = block.parent = &root;
        tbox_arena arena       = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &root);
        TBOX_TEST_ASSERT(list.count == 3);
        if (list.count == 3) {
            TBOX_TEST_ASSERT(list.items[0].color.b == 255); /* the block */
            TBOX_TEST_ASSERT(list.items[1].color.r == 255); /* the float */
            TBOX_TEST_ASSERT(list.items[2].color.g == 255); /* the positioned box */
        }
        size_t count                   = 0;
        tbox_render_painted_box *order = tbox_render_paint_order(&root, &count);
        TBOX_TEST_ASSERT(count == 4 && order != NULL);
        if (count == 4 && order != NULL)
            TBOX_TEST_ASSERT(order[1].box == &block && order[2].box == &floating && order[3].box == &positioned);
        free(order);
        tbox_arena_destroy(&arena);
    }

    /* Stroke color/order come from the run, including transparent fill and
     * currentColor. The shadow uses its own color for both text passes. */
    {
        tbox_style style = tbox_test_render_default_style();
        style.color = (tbox_css_rgba){ 255, 255, 0, 0 };
        style.text_stroke_width = 3;
        style.text_stroke_color = (tbox_css_rgba){ 204, 0, 0, 255 };
        style.paint_order[0] = TBOX_STYLE_PAINT_MARKERS;
        style.paint_order[1] = TBOX_STYLE_PAINT_STROKE;
        style.paint_order[2] = TBOX_STYLE_PAINT_FILL;
        style.text_shadow_color = (tbox_css_rgba){ 0, 0, 255, 255 };
        style.text_shadow_offset_x = 6;
        tbox_layout_text_run run = {0};
        run.rect = (tbox_rect){ 10, 10, 60, 20 };
        run.text = tbox_test_render_view_from_cstr("Hi");
        run.font = font;
        run.style = &style;
        tbox_layout_box box = tbox_test_render_default_box(&style);
        box.text_runs = &run;
        box.text_run_count = 1;
        tbox_arena arena = tbox_arena_create(0);
        tbox_display_list list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2);
        if (list.count == 2) {
            TBOX_TEST_ASSERT(list.items[0].text_stroke_color.b == 255 && list.items[0].color.b == 255);
            TBOX_TEST_ASSERT(list.items[1].color.a == 0 && list.items[1].text_stroke_color.r == 204 && list.items[1].text_stroke_color.a == 255);
            TBOX_TEST_ASSERT(list.items[1].text_stroke_width == 3 && list.items[1].text_stroke_first);
        }
        style.text_stroke_current_color = true;
        style.color.a = 255;
        list = tbox_render_build_display_list(&arena, &box);
        TBOX_TEST_ASSERT(list.count == 2 && list.items[1].text_stroke_color.g == 255);
        tbox_arena_destroy(&arena);
    }

    /* Real block text goes through an anonymous layout box. It and an
     * inline child must preserve the same inherited text painting. */
    {
        const char *html = "<div>direct<span>inline</span><div class='hollow'>hollow</div></div>";
        const char *css = "div { font-size: 48px; color: yellow; -webkit-text-stroke: 3px #c00; paint-order: stroke; } span { display: inline; } .hollow { color: transparent; -webkit-text-stroke-width: 1px; }";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
        TBOX_TEST_ASSERT(fonts != NULL);
        if (fonts != NULL) {
            tbox_arena arena = tbox_arena_create(0);
            const tbox_html_node *root = tbox_html_document_root(doc)->first_child;
            tbox_style_table styles = tbox_style_resolve_tree(&arena, root, &source, 1);
            const tbox_layout_box *box = tbox_layout_build(&arena, root, &styles, fonts, NULL, 800, 600);
            tbox_display_list list = tbox_render_build_display_list(&arena, box);
            size_t text_count = 0, hollow_count = 0;
            for (size_t i = 0; i < list.count; i++) {
                const tbox_paint_op *op = &list.items[i];
                if (op->kind != TBOX_PAINT_TEXT_RUN) continue;
                text_count++;
                TBOX_TEST_ASSERT(op->text_stroke_color.r == 204 && op->text_stroke_first);
                if (op->color.a == 0) {
                    hollow_count++;
                    TBOX_TEST_ASSERT(op->text_stroke_width == 1);
                } else TBOX_TEST_ASSERT(op->text_stroke_width == 3);
            }
            TBOX_TEST_ASSERT(text_count == 3 && hollow_count == 1);
            tbox_arena_destroy(&arena);
            tbox_font_face_cache_destroy(fonts);
        }
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    tbox_font_face_destroy(bold_font);
    tbox_font_face_destroy(font);
    failures += tbox_test_composition_run(font_data, font_size);
    free(font_data);

    return failures;
}
