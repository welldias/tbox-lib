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

    /* 22: NOVO v5 -- `position: absolute` child of a `position: relative`
     * parent positions against the PARENT'S padding_box (not the viewport,
     * not the parent's content_box) -- see ARCHITECTURE.md "Layout Tree --
     * containing block posicionado". The parent has non-zero padding so its
     * padding_box origin differs from both its content_box origin and
     * (0, 0), making this a discriminating test. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { position: relative; width: 300px; height: 200px; padding: 10px; } .inner { position: absolute; top: 5px; left: 7px; width: 50px; height: 20px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.x == outer_box->padding_box.x + 7.0, "absolute child must position against the parent's padding_box, not content_box/viewport");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.y == outer_box->padding_box.y + 5.0, "absolute child's top must be measured from the parent's padding_box");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 23: NOVO v5 -- `position: absolute` with NO positioned ancestor
     * positions against the viewport (the initial containing block), not
     * its immediate DOM parent -- even though that parent has a margin that
     * would shift its own content_box away from the viewport origin. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { width: 300px; height: 200px; margin: 0px 0px 0px 50px; } .inner { position: absolute; top: 15px; left: 20px; width: 50px; height: 10px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.x == 50.0, "test setup: outer's own content_box.x must be shifted by its 50px left margin");

            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.x == 20.0, "absolute with no positioned ancestor must position against the viewport (x=0+left), not the parent's shifted content_box");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.y == 15.0, "same, vertically");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 24: NOVO v5 -- `position: fixed` nested inside a `position: relative`
     * ancestor still positions against the viewport, ignoring that
     * ancestor entirely -- different from `absolute` (test 22 above). The
     * relative ancestor is shifted (left: 100px) so this is discriminating:
     * a bug that let FIXED see the relative ancestor as its containing
     * block would land the fixed child 100px further right than expected. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { position: relative; left: 100px; width: 300px; height: 200px; } .inner { position: fixed; top: 15px; left: 20px; width: 50px; height: 10px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.x == 100.0, "test setup: outer's relative shift must land it at x=100");

            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.x == 20.0, "fixed must ignore the relative ancestor and position against the viewport (x=0+left)");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.y == 15.0, "same, vertically");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 25: NOVO v5 -- `position: sticky` with `top`/`left` must produce
     * EXACTLY the same content_box/border_box/margin_box as the same
     * element with `position: relative` and the same offsets -- proof of
     * "sticky == relative" (ARCHITECTURE.md's "Escopo deliberadamente
     * contido"). Built as two independent trees (one per position value)
     * so this doesn't depend on any shared mutable state. */
    {
        tbox_html_document *doc_relative     = parse_html_cstr("<div><div class=\"a\">x</div></div>");
        const tbox_html_node *root_relative   = tbox_html_document_root(doc_relative);
        const tbox_html_node *outer_relative  = root_relative->first_child;
        tbox_css_stylesheet *sheet_relative   = parse_css_cstr(".a { height: 30px; width: 40px; position: relative; top: 10px; left: 5px; }");

        tbox_arena arena_relative               = tbox_arena_create(0);
        tbox_css_cascade_source source_relative = { sheet_relative, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_relative         = tbox_style_resolve_tree(&arena_relative, root_relative, &source_relative, 1);
        tbox_layout_box *outer_box_relative     = tbox_layout_build(&arena_relative, outer_relative, &table_relative, fonts, 800.0, 600.0);

        tbox_html_document *doc_sticky    = parse_html_cstr("<div><div class=\"a\">x</div></div>");
        const tbox_html_node *root_sticky  = tbox_html_document_root(doc_sticky);
        const tbox_html_node *outer_sticky = root_sticky->first_child;
        tbox_css_stylesheet *sheet_sticky  = parse_css_cstr(".a { height: 30px; width: 40px; position: sticky; top: 10px; left: 5px; }");

        tbox_arena arena_sticky               = tbox_arena_create(0);
        tbox_css_cascade_source source_sticky = { sheet_sticky, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_sticky         = tbox_style_resolve_tree(&arena_sticky, root_sticky, &source_sticky, 1);
        tbox_layout_box *outer_box_sticky     = tbox_layout_build(&arena_sticky, outer_sticky, &table_sticky, fonts, 800.0, 600.0);

        TBOX_TEST_ASSERT(outer_box_relative != NULL && outer_box_sticky != NULL);
        if (outer_box_relative != NULL && outer_box_sticky != NULL) {
            tbox_layout_box *relative_box = outer_box_relative->first_child;
            tbox_layout_box *sticky_box   = outer_box_sticky->first_child;
            TBOX_TEST_ASSERT(relative_box != NULL && sticky_box != NULL);
            if (relative_box != NULL && sticky_box != NULL) {
                TBOX_TEST_ASSERT_MSG(relative_box->content_box.x == sticky_box->content_box.x && relative_box->content_box.y == sticky_box->content_box.y &&
                                          relative_box->content_box.width == sticky_box->content_box.width && relative_box->content_box.height == sticky_box->content_box.height,
                                      "sticky's content_box must exactly match relative's with the same offsets");
                TBOX_TEST_ASSERT_MSG(relative_box->border_box.x == sticky_box->border_box.x && relative_box->border_box.y == sticky_box->border_box.y, "sticky's border_box must exactly match relative's");
                TBOX_TEST_ASSERT_MSG(relative_box->margin_box.x == sticky_box->margin_box.x && relative_box->margin_box.y == sticky_box->margin_box.y, "sticky's margin_box must exactly match relative's");
            }
        }

        tbox_arena_destroy(&arena_relative);
        tbox_css_stylesheet_destroy(sheet_relative);
        tbox_html_document_destroy(doc_relative);
        tbox_arena_destroy(&arena_sticky);
        tbox_css_stylesheet_destroy(sheet_sticky);
        tbox_html_document_destroy(doc_sticky);
    }

    /* 26: NOVO v5 -- an `absolute`/`fixed` child does not add to the
     * parent's auto-height (its 500px height must NOT appear in the
     * parent's summed content_box.height), and does not participate in
     * margin collapsing with either flow sibling even though it sits
     * between them in document order -- the gap between the two flow
     * siblings must still collapse to max(10, 20) == 20, exactly as if the
     * out-of-flow sibling weren't there (see test 19). The out-of-flow
     * child must still be linked normally into first_child/next_sibling/
     * last_child. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div><div class=\"a\">x</div><div class=\"pos\">y</div><div class=\"b\">z</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root->first_child;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".a { height: 30px; margin: 0px 0px 10px 0px; } .pos { position: absolute; top: 0px; left: 0px; width: 20px; height: 500px; } .b { height: 40px; margin: 20px 0px 0px 0px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.height == 90.0, "a 500px absolute child must not inflate the parent's auto-height (30 + collapsed-20 + 40 == 90)");

            tbox_layout_box *a_box   = outer_box->first_child;
            tbox_layout_box *pos_box = a_box != NULL ? a_box->next_sibling : NULL;
            tbox_layout_box *b_box   = outer_box->last_child;
            TBOX_TEST_ASSERT(a_box != NULL && pos_box != NULL && b_box != NULL && pos_box->next_sibling == b_box);
            if (a_box != NULL && b_box != NULL) {
                double gap = b_box->border_box.y - (a_box->border_box.y + a_box->border_box.height);
                TBOX_TEST_ASSERT_MSG(gap == 20.0, "the out-of-flow sibling between them must not disturb margin collapsing between the two flow siblings");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 27: NOVO v5 -- `width: auto` on an `absolute` box fills the
     * containing block (minus its own margin/padding/border), rather than
     * shrinking to its content -- the documented CSS 10.3.7 simplification
     * (ARCHITECTURE.md "Layout Tree -- geometria de absolute/fixed"). The
     * SAME box's `height: auto` -- with zero ELEMENT children -- comes out
     * as 0, exactly like the pre-existing (unchanged since v0) flow
     * auto-height formula: ARCHITECTURE.md documents content_height as
     * "inalterado ... AUTO continua sendo a soma dos filhos ... exatamente
     * como hoje" for absolute/fixed too -- only content_width reuses the
     * flow AUTO formula in a way that happens to fill rather than shrink. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { position: relative; width: 300px; height: 200px; padding: 10px; } .inner { position: absolute; top: 0px; left: 0px; width: auto; height: auto; margin: 5px; padding: 3px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                /* outer's padding_box is 320x220 (300x200 content + 10px padding
                 * all around); inner's width:auto must fill that minus its own
                 * 5px margin and 3px padding on each side: 320 - 10 - 6 == 304. */
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.width == 304.0, "width:auto on an absolute box must fill the containing block, not shrink to content");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.height == 0.0, "height:auto with zero element children must still be the (unchanged) children sum, not an implicit fill");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 28: NOVO v5 -- `left`/`right`/`top`/`bottom` all `auto` on an
     * `absolute` box falls back to the containing block's own origin
     * (the documented "no real static position" simplification). */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { position: relative; width: 300px; height: 200px; padding: 10px; } .inner { position: absolute; width: 50px; height: 20px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.x == outer_box->padding_box.x, "all-AUTO offsets must fall back to the containing block's own x origin");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.y == outer_box->padding_box.y, "all-AUTO offsets must fall back to the containing block's own y origin");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 29: NOVO v7 -- <ul><li>oi mundo</li></ul>: <li> is now on the fixed
     * text-tag list, so its box gets the same text-box treatment as a <p>
     * (see test 5). NOVO v8: tbox_layout_push_list_marker now prepends the
     * bullet "•" (U+2022) as the FIRST word of this <li> (its direct parent
     * is <ul>), so the merged run (marker + "oi" + "mundo" all share the
     * <li>'s own face, so they land in a single run, same as before) is now
     * "• oi mundo", not "oi mundo" -- an intentional v8 behavior change, see
     * ARCHITECTURE.md "v8 -- Layout Tree -- marcador de <li>". */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li>oi mundo</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *li_box = ul_box->first_child;
            TBOX_TEST_ASSERT_MSG(li_box != NULL, "<ul> must have the <li> as its first child box");
            if (li_box != NULL) {
                TBOX_TEST_ASSERT_MSG(li_box->node != NULL && string_view_equal_cstr(li_box->node->element.tag_name, "li"), "test setup: first child box must be the <li>");
                TBOX_TEST_ASSERT_MSG(li_box->text_run_count == 1, "<li> is on the fixed text-tag list now -- \"\xE2\x80\xA2 oi mundo\" must fit on a single run (marker + text share the same face)");
                if (li_box->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li_box->text_runs[0].text, "\xE2\x80\xA2 oi mundo"), "NOVO v8: the <li>'s run must be prefixed with the bullet marker ahead of its own text content");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 30: NOVO v7 -- <ul><li>um</li><li>dois</li><li>três</li></ul>: three
     * sibling <li> boxes, each with its OWN text_run_count == 1 and its own
     * correct text -- not a single box with the three words concatenated.
     * NOVO v8: each <li>'s single run is now prefixed with the bullet
     * marker (its direct parent is <ul>) ahead of its own word. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li>um</li><li>dois</li><li>três</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *first  = ul_box->first_child;
            tbox_layout_box *second = first != NULL ? first->next_sibling : NULL;
            tbox_layout_box *third  = second != NULL ? second->next_sibling : NULL;
            TBOX_TEST_ASSERT_MSG(first != NULL && second != NULL && third != NULL, "<ul> must have three <li> sibling boxes");
            if (first != NULL && second != NULL && third != NULL) {
                TBOX_TEST_ASSERT(third->next_sibling == NULL);

                TBOX_TEST_ASSERT_MSG(first->text_run_count == 1, "first <li> must have its own single run");
                TBOX_TEST_ASSERT_MSG(second->text_run_count == 1, "second <li> must have its own single run");
                TBOX_TEST_ASSERT_MSG(third->text_run_count == 1, "third <li> must have its own single run");
                if (first->text_run_count == 1 && second->text_run_count == 1 && third->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(first->text_runs[0].text, "\xE2\x80\xA2 um"), "NOVO v8: first <li>'s run must be prefixed with the bullet marker, and must not include the other items' words");
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(second->text_runs[0].text, "\xE2\x80\xA2 dois"), "NOVO v8: second <li>'s text must be its own (with its own bullet), not concatenated");
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(third->text_runs[0].text, "\xE2\x80\xA2 três"), "NOVO v8: third <li>'s text must be its own (with its own bullet), not concatenated");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 31: NOVO v7 regression -- <ul><li>texto</li></ul> with no CSS at all:
     * the <ul> box itself is still a plain container (text_run_count == 0,
     * same pattern as test 6's <span>) -- <ul> does NOT become a text tag,
     * only <li> does. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li>texto</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            TBOX_TEST_ASSERT_MSG(ul_box->text_run_count == 0, "<ul> must not become a text tag just because Tarefa 1 added <li> to the list");
            TBOX_TEST_ASSERT(ul_box->text_runs == NULL);
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 32: NOVO v8 -- <ul><li>um</li></ul>: the marker word ("•") and the
     * <li>'s own word ("um") share the exact same face (the <li>'s own,
     * since no CSS is declared) -- tbox_layout_build_line_runs only starts a
     * new run when the face changes, so in practice they land in a SINGLE
     * merged run, "\xE2\x80\xA2 um", not two separate text_runs. Verified
     * against the actual code behavior (not assumed) -- see the sibling
     * comment on test 29 above for the same reasoning applied there. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li>um</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *li_box = ul_box->first_child;
            TBOX_TEST_ASSERT_MSG(li_box != NULL, "<ul> must have the <li> as its first child box");
            if (li_box != NULL) {
                TBOX_TEST_ASSERT_MSG(li_box->text_run_count == 1, "marker + \"um\" share the same face -- they must merge into a single run");
                if (li_box->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li_box->text_runs[0].text, "\xE2\x80\xA2 um"), "the <li>'s only run must begin with the bullet marker, followed by its own text");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 33: NOVO v8 -- <ol><li>um</li><li>dois</li><li>três</li></ol>: each
     * <li>'s marker is "N." where N counts <li> siblings of the same <ol> in
     * document order, 1-based, never restarting -- "1.", "2.", "3." in that
     * exact order. Same face-merging behavior as test 32 above, so each
     * <li> still produces a single run. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ol><li>um</li><li>dois</li><li>três</li></ol>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ol_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ol_box != NULL);
        if (ol_box != NULL) {
            tbox_layout_box *first  = ol_box->first_child;
            tbox_layout_box *second = first != NULL ? first->next_sibling : NULL;
            tbox_layout_box *third  = second != NULL ? second->next_sibling : NULL;
            TBOX_TEST_ASSERT_MSG(first != NULL && second != NULL && third != NULL, "<ol> must have three <li> sibling boxes");
            if (first != NULL && second != NULL && third != NULL) {
                TBOX_TEST_ASSERT_MSG(first->text_run_count == 1 && second->text_run_count == 1 && third->text_run_count == 1, "each <ol> <li> must have its own single merged run");
                if (first->text_run_count == 1 && second->text_run_count == 1 && third->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(first->text_runs[0].text, "1. um"), "first <li> must be numbered \"1.\", not reset or skipped");
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(second->text_runs[0].text, "2. dois"), "second <li> must be numbered \"2.\", incrementing from the first");
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(third->text_runs[0].text, "3. três"), "third <li> must be numbered \"3.\", incrementing from the second");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 34: NOVO v8 -- <ul><li></li></ul>, an EMPTY <li>: intentional
     * behavior change from v7 -- an empty text-tag box used to have
     * word_count == 0 and thus text_run_count == 0 (the "no words at all"
     * path in tbox_layout_build_text_runs). Now the marker itself is a
     * word, so the <li> has word_count == 1 (just "•") and takes the normal
     * line-breaking path, producing text_run_count == 1 -- a single run
     * containing only the bullet. This matches a real browser (an empty
     * <li></li> still shows an empty bullet), and is NOT a bug to "fix" by
     * preserving the old text_run_count == 0. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li></li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *li_box = ul_box->first_child;
            TBOX_TEST_ASSERT_MSG(li_box != NULL, "<ul> must have the empty <li> as its first child box");
            if (li_box != NULL) {
                TBOX_TEST_ASSERT_MSG(li_box->text_run_count == 1, "NOVO v8: an empty <li> inside <ul> must now have text_run_count == 1 (just the marker), not 0 like v7");
                if (li_box->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li_box->text_runs[0].text, "\xE2\x80\xA2"), "the empty <li>'s only run must be the bare bullet marker, no trailing text");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 35: NOVO v8 regression -- <p>texto</p> continues with NO marker at
     * all: <li> is the only tag list_marker ever touches, so a <p> (also on
     * the fixed text-tag list, see tbox_layout_is_text_tag) must render its
     * text exactly as before, unprefixed. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>texto</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "<p>texto</p> must still produce a single run");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "texto"), "NOVO v8 regression: <p> must never gain a list marker -- text must be exactly \"texto\"");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 36: NOVO v8 regression -- <h1>texto</h1> continues with NO marker,
     * same reasoning as test 35 above but for another fixed text tag. */
    {
        tbox_html_document *doc    = parse_html_cstr("<h1>texto</h1>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "<h1>texto</h1> must still produce a single run");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "texto"), "NOVO v8 regression: <h1> must never gain a list marker -- text must be exactly \"texto\"");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 37: NOVO v8 regression -- <div><li>solto</li></div>: a <li> whose
     * DIRECT parent is neither <ul> nor <ol> (here, a <div>) must get NO
     * marker at all -- deliberate simplification documented in
     * ARCHITECTURE.md's "v8 -- Escopo" (a loose/malformed <li> is not
     * treated as "always disc" the way real CSS does). The <li> itself is
     * still on the fixed text-tag list (tbox_layout_is_text_tag doesn't
     * care about the parent), so it still gets a text box -- just without a
     * marker prefix. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><li>solto</li></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            tbox_layout_box *li_box = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(li_box != NULL, "<div> must have the <li> as its first child box");
            if (li_box != NULL) {
                TBOX_TEST_ASSERT_MSG(li_box->text_run_count == 1, "a loose <li> (parent isn't <ul>/<ol>) must still get a text box, just no marker");
                if (li_box->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li_box->text_runs[0].text, "solto"), "NOVO v8 regression: a <li> whose direct parent is not <ul>/<ol> must get NO marker prefix");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 38: NOVO v11 -- <p>um<br>dois</p>: the <br> forces a line break, so
     * "um" and "dois" land in two SEPARATE text_runs on two different lines
     * (different rect.y), not merged into one "um dois" run the way a plain
     * space between them would. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>um<br>dois</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "<br> must force \"um\"/\"dois\" onto two separate runs, not merge them into one");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "um"), "the first run must be \"um\"");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "dois"), "the second run must be \"dois\"");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].rect.y > box->text_runs[0].rect.y, "the run after <br> must sit on a strictly lower line");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 39: NOVO v11 -- <p>um<br><br>tres</p>: two CONSECUTIVE <br>s must
     * produce a genuine blank line between "um" and "tres" -- not just a
     * single line break. Since a blank line produces no run of its own (the
     * word range between the two hard breaks is empty), this is verified
     * indirectly: the gap between "um"'s and "tres"'s runs must be exactly
     * TWO line-heights (one line's worth for "um" itself, plus one full
     * blank line's worth), not one -- a bug collapsing "<br><br>" into a
     * single break would only advance by one line-height. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>um<br><br>tres</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "a blank line between two <br>s produces no run of its own -- only \"um\" and \"tres\" render");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "um"), "the first run must be \"um\"");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "tres"), "the second run must be \"tres\"");
                double line_height = tbox_font_face_line_height(regular_16);
                double gap         = box->text_runs[1].rect.y - box->text_runs[0].rect.y;
                TBOX_TEST_ASSERT_MSG(gap == 2.0 * line_height, "\"<br><br>\" must occupy 3 lines total (um / blank / tres) -- a 2-line-height gap between \"um\" and \"tres\", not 1");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 40: NOVO v11 -- <p>x<br></p>: a <br> at the very END of the text, with
     * nothing after it, must NOT create a phantom blank final line -- just
     * one run, "x", and nothing else (the tbox_layout_break_lines
     * `line_start < word_count` guard). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>x<br></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "a trailing <br> with nothing after it must NOT create a phantom blank line/run");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "x"), "the only run must be \"x\"");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 41: NOVO v11 -- <pre>a    b</pre> (4 literal spaces between "a" and
     * "b"): <pre> must preserve internal whitespace verbatim -- the run's
     * text must contain all 4 spaces, not collapse them down to 1 the way
     * <p>/h1-h6/<li> already do (tbox_string_collapse_whitespace). Exact
     * string comparison, not a substring check. */
    {
        tbox_html_document *doc    = parse_html_cstr("<pre>a    b</pre>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "a single physical <pre> line with no '\\n' must produce exactly one run");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "a    b"), "<pre> must preserve all 4 internal spaces literally, not collapse them to 1");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 42: NOVO v11 -- <pre>linha um\nlinha dois</pre> (a literal '\n' in the
     * HTML source): the '\n' must split the text into two separate
     * runs/lines (different rect.y), each keeping its own internal space
     * ("linha um"/"linha dois" each still has one un-collapsed space of
     * their own) -- entirely without any <br>. */
    {
        tbox_html_document *doc    = parse_html_cstr("<pre>linha um\nlinha dois</pre>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "a literal '\\n' inside <pre> must split into two runs, without any <br>");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "linha um"), "the first line's run must be \"linha um\"");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "linha dois"), "the second line's run must be \"linha dois\"");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].rect.y > box->text_runs[0].rect.y, "the second physical line must sit strictly below the first");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 43: NOVO v11 -- a <pre> physical line far wider than the container's
     * available_width must NOT wrap (CSS `white-space: pre`, not
     * `pre-wrap`) -- exactly one run for that line, its rect.width
     * exceeding content_box.width, even though the line has plenty of
     * spaces a normal <p> would happily wrap on. Proof of the `no_wrap`
     * flag threaded into tbox_layout_break_lines. */
    {
        tbox_html_document *doc    = parse_html_cstr("<pre>linha muito comprida dentro do pre nao deveria quebrar</pre>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("pre { width: 10px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "a <pre> line, however wide, must never wrap onto more than one run/line");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].rect.width > box->content_box.width, "the overlong <pre> line must overflow the 10px content box rather than wrap");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "linha muito comprida dentro do pre nao deveria quebrar"), "the whole physical line must be kept intact, spaces and all");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 44: NOVO v11 -- <div style="text-align: center;"><p>oi</p></div>:
     * `text-align` is inheritable (Tarefa 1), so the <p> (no `text-align` of
     * its own) inherits CENTER from its parent <div>. The "oi" run's
     * rect.x must land to the RIGHT of where it would sit for `left` (i.e.
     * past the <p>'s own content_box.x), shifted by exactly
     * (available_width - line_width) / 2 -- derived here from the actually
     * measured word width and the <p>'s own resolved geometry, never a
     * hardcoded pixel number. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div style=\"text-align: center;\"><p>oi</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            tbox_layout_box *p_box = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(p_box != NULL, "<div> must have the <p> as its first child box");
            if (p_box != NULL) {
                TBOX_TEST_ASSERT_MSG(p_box->text_run_count == 1, "\"oi\" must fit on a single run/line");
                if (p_box->text_run_count == 1) {
                    double word_width      = tbox_font_measure_text(regular_16, tbox_string_view_make("oi", 2));
                    double available_width = p_box->content_box.width;
                    double expected_offset = (available_width - word_width) / 2.0;
                    TBOX_TEST_ASSERT_MSG(expected_offset > 0.0, "test setup: \"oi\" must be far narrower than the container for this to be a discriminating test");
                    double expected_x = p_box->content_box.x + expected_offset;
                    TBOX_TEST_ASSERT_MSG(p_box->text_runs[0].rect.x == expected_x, "inherited text-align: center must shift the run's rect.x by (available_width - line_width) / 2 past content_box.x");
                    TBOX_TEST_ASSERT_MSG(p_box->text_runs[0].rect.x > p_box->content_box.x, "the centered run must sit strictly to the right of where a left-aligned run would start");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 45: NOVO v11 regression -- <p>texto normal</p>, no <br>/text-align at
     * all: must render exactly as every prior version -- a single run,
     * rect.x == content_box.x (no accidental offset from the new
     * text-align post-processing step, since style->text_align defaults to
     * the initial LEFT and that branch is skipped entirely). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>texto normal</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "\"texto normal\" must fit on a single run, unchanged from prior versions");
            if (box->text_run_count == 1) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "texto normal"), "the run's text must be exactly \"texto normal\"");
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].rect.x == box->content_box.x, "default (left) text-align must never offset rect.x");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 46: NOVO v11 regression -- <ul><li>item</li></ul>: the v8 list-marker
     * pipeline must be entirely unaffected by <pre>/<br>/text-align -- <li>
     * must not have accidentally fallen onto the <pre> (no-word-splitting)
     * code path just because both are on the fixed text-tag list now. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul><li>item</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *li_box = ul_box->first_child;
            TBOX_TEST_ASSERT_MSG(li_box != NULL, "<ul> must have the <li> as its first child box");
            if (li_box != NULL) {
                TBOX_TEST_ASSERT_MSG(li_box->text_run_count == 1, "NOVO v11 regression: the v8 marker + text must still merge into a single run");
                if (li_box->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li_box->text_runs[0].text, "\xE2\x80\xA2 item"), "the <li>'s run must still be prefixed with the bullet marker, unaffected by the <pre>/<br>/text-align changes");
                }
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
