#include <tbox/layout.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

static tbox_html_document *parse_html_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

static tbox_css_stylesheet *parse_css_cstr(const char *css) {
    return tbox_css_parse(css, strlen(css));
}

/* Mirrors example/css_cascade_origins.c's / tests/font/test_font.c's
 * read_file() helper: reads the vendored LiberationSans-Regular.ttf into a
 * malloc'd buffer so the embedded font backend (no Fontconfig dependency)
 * can load it. */
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

int tbox_test_layout_run(void) {
    int failures = 0;

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");
    if (font_data == NULL) {
        return failures + 1;
    }

    /* NOVO v2: tbox_layout_build now takes a tbox_font_face_cache, not a
     * single tbox_font_face -- both the "regular" and "bold" slots are built
     * from the SAME bytes here (this test never cares whether bold actually
     * *looks* different, only that it resolves to a DIFFERENT tbox_font_face
     * pointer than regular -- which the cache guarantees regardless of
     * whether the underlying bytes happen to be identical, since (bold,
     * size_px) is the cache key). */
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size);
    TBOX_TEST_ASSERT_MSG(fonts != NULL, "failed to create font face cache");
    if (fonts == NULL) {
        free(font_data);
        return failures + 1;
    }

    const tbox_font_face *regular_16 = tbox_font_face_cache_get(fonts, false, 16.0);
    TBOX_TEST_ASSERT_MSG(regular_16 != NULL, "failed to resolve the regular 16px face");

    /* 1: an explicit width/height in px is used as-is. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 200px; height: 100px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->content_box.width == 200.0);
            TBOX_TEST_ASSERT(box->content_box.height == 100.0);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 2: no `width` declared -> AUTO fills the containing block (the
     * viewport, for a root-level box). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->content_box.width == 800.0);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 3: two sibling <div>s stack vertically -- the second's margin_box.y
     * equals the first's margin_box.height (no gap, no overlap). */
    {
        tbox_html_document *doc     = parse_html_cstr("<div><div class=\"a\">x</div><div class=\"b\">y</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".a { height: 30px; } .b { height: 40px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *first  = outer_box->first_child;
            tbox_layout_box *second = outer_box->last_child;
            TBOX_TEST_ASSERT(first != NULL && second != NULL && first != second);
            if (first != NULL && second != NULL) {
                TBOX_TEST_ASSERT(second->margin_box.y == first->margin_box.height);
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 4: <h1>oi</h1>, short text that fits on one line -> exactly 1
     * text_run, content_box.height == that one line's height, text == "oi",
     * run->font is the regular 16px face (h1's own resolved style, no CSS
     * declared -> initial font-size/font-weight). */
    {
        tbox_html_document *doc    = parse_html_cstr("<h1>oi</h1>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "short text that fits on one line must produce exactly 1 text_run");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "oi"), "h1's single run should be \"oi\"");
                TBOX_TEST_ASSERT(box->text_runs[0].font == regular_16);
                TBOX_TEST_ASSERT(box->content_box.height == tbox_font_face_line_height(regular_16));
                TBOX_TEST_ASSERT(box->text_runs[0].rect.height == tbox_font_face_line_height(regular_16));
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 5: <p>oi mundo</p> inside a wider container -> content_box.width
     * follows the container's width, NOT a "fits the text" width, even
     * though the text is much shorter than 800px (the D4 rule). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>oi mundo</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->content_box.width == 800.0, "text must never shrink the box's width");
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "\"oi mundo\" must fit on a single line at 800px");
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 6: <span>texto</span> is outside the fixed text-tag list -> the box
     * exists (a container), but text_run_count is 0 and text_runs is NULL --
     * the loose TEXT child is never shown. */
    {
        tbox_html_document *doc    = parse_html_cstr("<span>texto</span>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->text_run_count == 0);
            TBOX_TEST_ASSERT(box->text_runs == NULL);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 6b: NOVO v2 regression check -- a <div> with loose text inside still
     * gets zero text_runs (the fixed h1-h6/p tag list is unchanged; a <div>
     * never gets text-box treatment, no matter what's inside it). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>texto solto</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 0, "a <div> with loose text must still produce zero text_runs");
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 7: display:none produces no box at all, and doesn't contribute height
     * to the parent's auto-height sum -- a parent with a display:none child
     * plus a normal child has the same auto-height as if the hidden one
     * weren't there. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div><div class=\"hidden\">a</div><div class=\"visible\">b</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".hidden { display: none; height: 500px; } .visible { height: 40px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.height == 40.0, "display:none child must not contribute to auto-height");
            TBOX_TEST_ASSERT(outer_box->first_child != NULL && outer_box->first_child == outer_box->last_child);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 8: a container <div> with two <h1> children and no explicit height
     * gets content_box.height == the sum of both children's
     * margin_box.height (auto/shrink-to-fit). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><h1>a</h1><h1>b</h1></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *div  = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, div, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->first_child != NULL && box->last_child != NULL && box->first_child != box->last_child);
            double expected = box->first_child->margin_box.height + box->last_child->margin_box.height;
            TBOX_TEST_ASSERT(box->content_box.height == expected);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 9: NOVO v2 -- text wider than the container wraps onto 2+ lines, each
     * at a strictly increasing y (real line breaking happened, not just one
     * overflowing line). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>one two three four five six seven eight nine ten</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("p { width: 60px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count >= 2, "text much wider than a 60px container must wrap onto 2+ lines/runs");
            if (box->text_run_count >= 2) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].rect.y > box->text_runs[0].rect.y, "a later line's run must sit strictly below an earlier line's");
                TBOX_TEST_ASSERT(box->text_runs[0].rect.x == box->content_box.x);
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 10: NOVO v2 -- box auto-height grows proportionally with the number of
     * lines: a <p> forced onto 3 lines is (roughly) 3x the height of the
     * same text on a single line, since every line here shares one face
     * (same font-size/weight throughout), so height == line_count *
     * line_height exactly. */
    {
        tbox_html_document *doc_one    = parse_html_cstr("<p>hi</p>");
        const tbox_html_node *root_one = tbox_html_document_root(doc_one);
        tbox_css_stylesheet *sheet_one = parse_css_cstr("");

        tbox_arena arena_one               = tbox_arena_create(0);
        tbox_css_cascade_source source_one = { sheet_one, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_one         = tbox_style_resolve_tree(&arena_one, root_one, &source_one, 1);
        tbox_layout_box *box_one           = tbox_layout_build(&arena_one, root_one, &table_one, fonts, 800.0, 600.0);

        tbox_html_document *doc_three    = parse_html_cstr("<p>alpha beta gamma delta epsilon zeta eta theta</p>");
        const tbox_html_node *root_three = tbox_html_document_root(doc_three);
        tbox_css_stylesheet *sheet_three = parse_css_cstr("p { width: 70px; }");

        tbox_arena arena_three               = tbox_arena_create(0);
        tbox_css_cascade_source source_three = { sheet_three, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_three         = tbox_style_resolve_tree(&arena_three, root_three, &source_three, 1);
        tbox_layout_box *box_three           = tbox_layout_build(&arena_three, root_three, &table_three, fonts, 800.0, 600.0);

        TBOX_TEST_ASSERT(box_one != NULL && box_three != NULL);
        if (box_one != NULL && box_three != NULL) {
            TBOX_TEST_ASSERT(box_one->text_run_count == 1);
            TBOX_TEST_ASSERT_MSG(box_three->text_run_count >= 3, "test setup: 8 short words at 70px must wrap onto at least 3 lines");
            if (box_three->text_run_count >= 3) {
                double expected = (double)box_three->text_run_count * box_one->content_box.height;
                TBOX_TEST_ASSERT_MSG(box_three->content_box.height == expected, "auto-height must be exactly line_count * line_height when every line shares one face");
            }
        }

        tbox_arena_destroy(&arena_one);
        tbox_css_stylesheet_destroy(sheet_one);
        tbox_html_document_destroy(doc_one);
        tbox_arena_destroy(&arena_three);
        tbox_css_stylesheet_destroy(sheet_three);
        tbox_html_document_destroy(doc_three);
    }

    /* 11: NOVO v2 -- a single word wider than the container's whole width
     * still yields exactly one run (no forced mid-word break), with
     * rect.width exceeding the available content width (visual overflow). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>supercalifragilisticexpialidocious</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("p { width: 10px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "a single word alone on a line must never be split into multiple runs");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].rect.width > box->content_box.width, "an overlong single word must overflow the content box's width, not be broken mid-word");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "supercalifragilisticexpialidocious"), "the overlong word must be kept whole");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 12: NOVO v2 -- a <p> with a <b> child produces (at least) two runs,
     * the plain-text run and the bold run, with DIFFERENT font pointers --
     * this is how <b> renders bold within an otherwise-regular <p>. `b` has
     * no default `display: inline` yet (no UA stylesheet until Tarefa 4),
     * so the test declares it explicitly, same as any other inline element
     * would need to at this tier. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>plain <b>bold</b></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("b { display: inline; font-weight: bold; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"plain \" + \"bold\" must merge into exactly two runs (one per face) on one line");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "plain"), "the first run must be the plain-text word");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "bold"), "the second run must be the bold word");
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].font != box->text_runs[1].font, "the bold run must resolve to a DIFFERENT face pointer than the plain run");
                TBOX_TEST_ASSERT(box->text_runs[0].font == regular_16);
                TBOX_TEST_ASSERT(box->text_runs[1].font == tbox_font_face_cache_get(fonts, true, 16.0));
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 13: NOVO v2 -- an <h1> with a larger declared font-size resolves to a
     * face whose measured width/line-height genuinely differ from a <p>'s
     * regular-size face -- proving font-size actually flows through to text
     * measurement (tbox_font_face_cache_get + tbox_font_measure_text), not
     * just sitting unused on the style struct. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><h1>Hi</h1><p>Hi</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("h1 { font-size: 32px; } p { font-size: 16px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            tbox_layout_box *h1_box = box->first_child;
            tbox_layout_box *p_box  = box->last_child;
            TBOX_TEST_ASSERT(h1_box != NULL && p_box != NULL && h1_box != p_box);
            if (h1_box != NULL && p_box != NULL) {
                TBOX_TEST_ASSERT(h1_box->text_run_count == 1 && p_box->text_run_count == 1);
                if (h1_box->text_run_count == 1 && p_box->text_run_count == 1) {
                    const tbox_font_face *h1_face = h1_box->text_runs[0].font;
                    const tbox_font_face *p_face  = p_box->text_runs[0].font;
                    TBOX_TEST_ASSERT_MSG(h1_face != p_face, "a 32px h1 and a 16px p must resolve to different face pointers");
                    TBOX_TEST_ASSERT_MSG(tbox_font_face_line_height(h1_face) > tbox_font_face_line_height(p_face), "the larger font-size must produce a taller line-height");
                    TBOX_TEST_ASSERT_MSG(tbox_font_measure_text(h1_face, tbox_string_view_make("Hi", 2)) > tbox_font_measure_text(p_face, tbox_string_view_make("Hi", 2)), "the same text must measure wider in the larger face");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 14: NOVO v4 -- `border: 3px solid black` grows border_box 3px past
     * padding_box on every side, and (width: auto) shrinks content_width by
     * an extra 6px (2 * 3px) compared to the same element with no border. */
    {
        tbox_html_document *doc_border    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root_border = tbox_html_document_root(doc_border);
        tbox_css_stylesheet *sheet_border  = parse_css_cstr("div { border: 3px solid black; }");

        tbox_arena arena_border               = tbox_arena_create(0);
        tbox_css_cascade_source source_border = { sheet_border, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_border         = tbox_style_resolve_tree(&arena_border, root_border, &source_border, 1);
        tbox_layout_box *box_border           = tbox_layout_build(&arena_border, root_border, &table_border, fonts, 800.0, 600.0);

        tbox_html_document *doc_plain    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root_plain = tbox_html_document_root(doc_plain);
        tbox_css_stylesheet *sheet_plain  = parse_css_cstr("");

        tbox_arena arena_plain               = tbox_arena_create(0);
        tbox_css_cascade_source source_plain = { sheet_plain, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_plain         = tbox_style_resolve_tree(&arena_plain, root_plain, &source_plain, 1);
        tbox_layout_box *box_plain           = tbox_layout_build(&arena_plain, root_plain, &table_plain, fonts, 800.0, 600.0);

        TBOX_TEST_ASSERT(box_border != NULL && box_plain != NULL);
        if (box_border != NULL && box_plain != NULL) {
            TBOX_TEST_ASSERT_MSG(box_border->border_box.x == box_border->padding_box.x - 3.0, "left border must grow border_box 3px past padding_box");
            TBOX_TEST_ASSERT_MSG(box_border->border_box.y == box_border->padding_box.y - 3.0, "top border must grow border_box 3px past padding_box");
            TBOX_TEST_ASSERT_MSG(box_border->border_box.width == box_border->padding_box.width + 6.0, "border_box width must exceed padding_box width by 2*3px (left+right)");
            TBOX_TEST_ASSERT_MSG(box_border->border_box.height == box_border->padding_box.height + 6.0, "border_box height must exceed padding_box height by 2*3px (top+bottom)");
            TBOX_TEST_ASSERT_MSG(box_border->content_box.width == box_plain->content_box.width - 6.0, "width:auto must shrink content_width by an extra 2*border_width when bordered");
        }

        tbox_arena_destroy(&arena_border);
        tbox_css_stylesheet_destroy(sheet_border);
        tbox_html_document_destroy(doc_border);
        tbox_arena_destroy(&arena_plain);
        tbox_css_stylesheet_destroy(sheet_plain);
        tbox_html_document_destroy(doc_plain);
    }

    /* 15: NOVO v4 -- no effective border (no `border` declared at all, or a
     * declared but unsupported style like `dashed`) keeps the v0-v3
     * identity border_box == padding_box exactly. */
    {
        const char *cases[] = { "", "div { border: 5px dashed red; }" };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i]);

            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

            tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                TBOX_TEST_ASSERT_MSG(box->border_box.x == box->padding_box.x && box->border_box.y == box->padding_box.y &&
                                          box->border_box.width == box->padding_box.width && box->border_box.height == box->padding_box.height,
                                      "no effective border (absent, or an unsupported border-style) must keep border_box == padding_box");
            }

            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 16: NOVO v4 -- `position: relative; top: 10px; left: 5px;` shifts
     * content_box/padding_box/border_box/margin_box all by (5, 10) versus
     * the static position, but the NEXT sibling is positioned exactly as if
     * the shifted box hadn't moved (same cursor_y it would get without
     * `position: relative`). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"a\">x</div><div class=\"b\">y</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { height: 30px; position: relative; top: 10px; left: 5px; } .b { height: 40px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *first  = outer_box->first_child;
            tbox_layout_box *second = outer_box->last_child;
            TBOX_TEST_ASSERT(first != NULL && second != NULL && first != second);
            if (first != NULL && second != NULL) {
                TBOX_TEST_ASSERT_MSG(first->content_box.x == outer_box->content_box.x + 5.0, "left: 5px must shift content_box.x by +5");
                TBOX_TEST_ASSERT_MSG(first->content_box.y == outer_box->content_box.y + 10.0, "top: 10px must shift content_box.y by +10");
                TBOX_TEST_ASSERT_MSG(first->padding_box.x == outer_box->content_box.x + 5.0, "padding_box must shift by the same (5, 10)");
                TBOX_TEST_ASSERT_MSG(first->border_box.x == outer_box->content_box.x + 5.0, "border_box must shift by the same (5, 10)");
                TBOX_TEST_ASSERT_MSG(first->margin_box.x == outer_box->content_box.x + 5.0, "margin_box must shift by the same (5, 10)");
                TBOX_TEST_ASSERT_MSG(first->margin_box.y == outer_box->content_box.y + 10.0, "margin_box.y must shift by the same (5, 10)");
                /* the shift never pushes the next sibling: `second` lands at
                 * exactly the same y it would without `position: relative`
                 * (right after the first box's UNSHIFTED 30px height). */
                TBOX_TEST_ASSERT_MSG(second->margin_box.y == outer_box->content_box.y + 30.0, "position:relative must not move the next sibling's flow position");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 17: NOVO v4 -- a child of a `position: relative` parent shifts along
     * automatically (it inherits the parent's offset via the parent's
     * already-shifted children_container, no separate offset propagation
     * needed). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(".outer { position: relative; top: 10px; left: 5px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.x == outer_box->content_box.x, "a child of a shifted parent must land inside the parent's already-shifted content box");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 18: NOVO v4 -- `top: 20%` against a containing block with an
     * indefinite (AUTO) height must resolve to 0, not NaN/crash. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"a\">x</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { position: relative; top: 20%; height: 10px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.y == outer_box->content_box.y, "top:20% against an AUTO-height container must resolve to 0, not NaN");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 19: NOVO v4 -- two block siblings with margin-bottom:10px (first) /
     * margin-top:20px (second) collapse into a single 20px gap between the
     * end of the first's border_box and the start of the second's
     * border_box -- NOT the 30px sum v0-v3 produced. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"a\">x</div><div class=\"b\">y</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { height: 30px; margin: 0px 0px 10px 0px; } .b { height: 40px; margin: 20px 0px 0px 0px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *first  = outer_box->first_child;
            tbox_layout_box *second = outer_box->last_child;
            TBOX_TEST_ASSERT(first != NULL && second != NULL && first != second);
            if (first != NULL && second != NULL) {
                double gap = second->border_box.y - (first->border_box.y + first->border_box.height);
                TBOX_TEST_ASSERT_MSG(gap == 20.0, "adjacent siblings' margins must collapse to max(10, 20) == 20, not sum to 30");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 20: NOVO v4 -- same as above, but the second sibling's margin-top is
     * negative (-5px): the pair must NOT collapse (a negative side always
     * falls back to today's sum), leaving a 5px gap (10 + (-5)). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"a\">x</div><div class=\"b\">y</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { height: 30px; margin: 0px 0px 10px 0px; } .b { height: 40px; margin: -5px 0px 0px 0px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *first  = outer_box->first_child;
            tbox_layout_box *second = outer_box->last_child;
            TBOX_TEST_ASSERT(first != NULL && second != NULL && first != second);
            if (first != NULL && second != NULL) {
                double gap = second->border_box.y - (first->border_box.y + first->border_box.height);
                TBOX_TEST_ASSERT_MSG(gap == 5.0, "a negative margin on either side must disable collapsing, falling back to the sum (10 + -5 == 5)");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 21: NOVO v4 -- the FIRST child of a parent never collapses its top
     * margin with anything, even a large one: it lands exactly
     * margin-top px below the parent's content box top. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"a\">x</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { height: 10px; margin: 50px 0px 0px 0px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *first = outer_box->first_child;
            TBOX_TEST_ASSERT(first != NULL);
            if (first != NULL) {
                TBOX_TEST_ASSERT_MSG(first->border_box.y == outer_box->content_box.y + 50.0, "the first child's top margin must never collapse with anything, even a large one");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);

    return failures;
}
