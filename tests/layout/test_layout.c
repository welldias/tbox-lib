#include <tbox/layout.h>

#include <tbox/image.h>
#include <tbox/render.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

/* NOVO v13: baseline-alignment assertions below compare a run's rect.y
 * against a value computed independently in the test from
 * tbox_font_face_ascent/style->font_size -- both sides go through the same
 * double-precision arithmetic tbox_layout.c itself does, so exact equality
 * would normally hold, but a small epsilon avoids any brittleness from
 * operation-order/rounding differences between this file and
 * tbox_layout_build_line_runs's own expression. */
static bool tbox_test_double_approx_equal(double a, double b) {
    return fabs(a - b) < 1e-6;
}

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

#ifndef TBOX_TEST_ASSETS_DIR
#define TBOX_TEST_ASSETS_DIR "tests/assets"
#endif

static bool string_view_equal_cstr(tbox_string_view view, const char *cstr) {
    size_t len = strlen(cstr);
    return view.size == len && (len == 0 || memcmp(view.data, cstr, len) == 0);
}

static const tbox_layout_box *find_box_for_node(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node) return box;
        const tbox_layout_box *child = find_box_for_node(box->first_child, node);
        if (child != NULL) return child;
    }
    return NULL;
}

static const tbox_html_node *find_html_id(const tbox_html_node *node, const char *id) {
    for (; node != NULL; node = node->next_sibling) {
        if (node->type == TBOX_HTML_NODE_ELEMENT) {
            const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node,
                tbox_string_view_make("id", 2));
            if (attribute != NULL && string_view_equal_cstr(attribute->value, id)) return node;
        }
        const tbox_html_node *found = find_html_id(node->first_child, id);
        if (found != NULL) return found;
    }
    return NULL;
}

/* NOVO v12 (Tarefa 5): test-only resolver state/callback, same pattern as
 * tests/font/test_font.c's tbox_test_font_resolver_state/
 * tbox_test_font_resolver -- a plain local (stack) variable passed as
 * resolver_userdata, never a global/static mutable, per the project's "no
 * new global/static mutable state" rule. Always resolves to the same
 * vendored bytes already used by the rest of this file, no matter which
 * family/bold/italic is requested, and counts how many times it was
 * called -- used to prove (a) a declared `font-family` reaches
 * tbox_font_face_cache_get from the Layout Tree, and (b) a <p> with no
 * font-family at all never invokes the resolver (default/empty-family
 * fast path, untouched by v12). */
typedef struct tbox_test_layout_resolver_state {
    const void *font_data;
    size_t font_size;
    int calls;
} tbox_test_layout_resolver_state;

static bool tbox_test_layout_resolver(void *userdata, tbox_font_query query, const void **out_data, size_t *out_size) {
    (void)query;
    tbox_test_layout_resolver_state *state = userdata;
    state->calls++;
    *out_data = state->font_data;
    *out_size = state->font_size;
    return true;
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
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL);
    TBOX_TEST_ASSERT_MSG(fonts != NULL, "failed to create font face cache");
    if (fonts == NULL) {
        free(font_data);
        return failures + 1;
    }

    const tbox_font_face *regular_16 = tbox_font_face_cache_get(fonts, tbox_string_view_make(NULL, 0), false, false, 16.0);
    TBOX_TEST_ASSERT_MSG(regular_16 != NULL, "failed to resolve the regular 16px face");

    /* 1: an explicit width/height in px is used as-is. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 200px; height: 100px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *first  = outer_box->first_child;
            const tbox_layout_box *second = outer_box->last_child;
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
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

        tbox_layout_box *box = tbox_layout_build(&arena, div, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->first_child != NULL && box->last_child != NULL && box->first_child != box->last_child);
            if (box->first_child != NULL && box->last_child != NULL) {
                double expected = box->first_child->margin_box.height + box->last_child->margin_box.height;
                TBOX_TEST_ASSERT(box->content_box.height == expected);
            }
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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
        tbox_layout_box *box_one           = tbox_layout_build(&arena_one, root_one, &table_one, fonts, NULL, 800.0, 600.0);

        tbox_html_document *doc_three    = parse_html_cstr("<p>alpha beta gamma delta epsilon zeta eta theta</p>");
        const tbox_html_node *root_three = tbox_html_document_root(doc_three);
        tbox_css_stylesheet *sheet_three = parse_css_cstr("p { width: 70px; }");

        tbox_arena arena_three               = tbox_arena_create(0);
        tbox_css_cascade_source source_three = { sheet_three, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_three         = tbox_style_resolve_tree(&arena_three, root_three, &source_three, 1);
        const tbox_layout_box *box_three           = tbox_layout_build(&arena_three, root_three, &table_three, fonts, NULL, 800.0, 600.0);

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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"plain \" + \"bold\" must merge into exactly two runs (one per face) on one line");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "plain"), "the first run must be the plain-text word");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "bold"), "the second run must be the bold word");
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].font != box->text_runs[1].font, "the bold run must resolve to a DIFFERENT face pointer than the plain run");
                TBOX_TEST_ASSERT(box->text_runs[0].font == regular_16);
                TBOX_TEST_ASSERT(box->text_runs[1].font == tbox_font_face_cache_get(fonts, tbox_string_view_make(NULL, 0), true, false, 16.0));
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

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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
        const tbox_layout_box *box_border           = tbox_layout_build(&arena_border, root_border, &table_border, fonts, NULL, 800.0, 600.0);

        tbox_html_document *doc_plain    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root_plain = tbox_html_document_root(doc_plain);
        tbox_css_stylesheet *sheet_plain  = parse_css_cstr("");

        tbox_arena arena_plain               = tbox_arena_create(0);
        tbox_css_cascade_source source_plain = { sheet_plain, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_plain         = tbox_style_resolve_tree(&arena_plain, root_plain, &source_plain, 1);
        const tbox_layout_box *box_plain           = tbox_layout_build(&arena_plain, root_plain, &table_plain, fonts, NULL, 800.0, 600.0);

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

            const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *first  = outer_box->first_child;
            const tbox_layout_box *second = outer_box->last_child;
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *inner_box = outer_box->first_child;
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *inner_box = outer_box->first_child;
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

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
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

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *first = outer_box->first_child;
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *inner_box = outer_box->first_child;
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.x == 50.0, "test setup: outer's own content_box.x must be shifted by its 50px left margin");

            const tbox_layout_box *inner_box = outer_box->first_child;
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.x == 100.0, "test setup: outer's relative shift must land it at x=100");

            const tbox_layout_box *inner_box = outer_box->first_child;
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
        const tbox_layout_box *outer_box_relative     = tbox_layout_build(&arena_relative, outer_relative, &table_relative, fonts, NULL, 800.0, 600.0);

        tbox_html_document *doc_sticky    = parse_html_cstr("<div><div class=\"a\">x</div></div>");
        const tbox_html_node *root_sticky  = tbox_html_document_root(doc_sticky);
        const tbox_html_node *outer_sticky = root_sticky->first_child;
        tbox_css_stylesheet *sheet_sticky  = parse_css_cstr(".a { height: 30px; width: 40px; position: sticky; top: 10px; left: 5px; }");

        tbox_arena arena_sticky               = tbox_arena_create(0);
        tbox_css_cascade_source source_sticky = { sheet_sticky, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table_sticky         = tbox_style_resolve_tree(&arena_sticky, root_sticky, &source_sticky, 1);
        const tbox_layout_box *outer_box_sticky     = tbox_layout_build(&arena_sticky, outer_sticky, &table_sticky, fonts, NULL, 800.0, 600.0);

        TBOX_TEST_ASSERT(outer_box_relative != NULL && outer_box_sticky != NULL);
        if (outer_box_relative != NULL && outer_box_sticky != NULL) {
            const tbox_layout_box *relative_box = outer_box_relative->first_child;
            const tbox_layout_box *sticky_box   = outer_box_sticky->first_child;
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

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            TBOX_TEST_ASSERT_MSG(outer_box->content_box.height == 90.0, "a 500px absolute child must not inflate the parent's auto-height (30 + collapsed-20 + 40 == 90)");

            tbox_layout_box *a_box   = outer_box->first_child;
            const tbox_layout_box *pos_box = a_box != NULL ? a_box->next_sibling : NULL;
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
     * SAME box's `height: auto` comes out as the (unchanged since v0)
     * flow auto-height formula: the sum of every child box's margin_box
     * height -- ARCHITECTURE.md documents content_height as "inalterado ...
     * AUTO continua sendo a soma dos filhos ... exatamente como hoje" for
     * absolute/fixed too -- only content_width reuses the flow AUTO formula
     * in a way that happens to fill rather than shrink.
     *
     * NOVO v14 update: `.inner`'s content is the loose TEXT "x" (no
     * ELEMENT child at all) -- pre-v14 this was silently ignored (the
     * fixed-text-tag debt this version fixes), so the auto-height sum came
     * out as 0 with zero children counted. Now "x" triggers exactly ONE
     * anonymous box (see tbox_layout_build_anonymous_box), so the sum is
     * that one box's own height (a single line at the default 16px face) --
     * proving the auto-height formula itself is unchanged, just now correctly
     * counting the anonymous box like any other child. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div class=\"outer\"><div class=\"inner\">x</div></div>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *outer = root;
        tbox_css_stylesheet *sheet  = parse_css_cstr(".outer { position: relative; width: 300px; height: 200px; padding: 10px; } .inner { position: absolute; top: 0px; left: 0px; width: auto; height: auto; margin: 5px; padding: 3px; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *inner_box = outer_box->first_child;
            TBOX_TEST_ASSERT(inner_box != NULL);
            if (inner_box != NULL) {
                /* outer's padding_box is 320x220 (300x200 content + 10px padding
                 * all around); inner's width:auto must fill that minus its own
                 * 5px margin and 3px padding on each side: 320 - 10 - 6 == 304. */
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.width == 304.0, "width:auto on an absolute box must fill the containing block, not shrink to content");
                TBOX_TEST_ASSERT_MSG(inner_box->content_box.height == tbox_font_face_line_height(regular_16), "NOVO v14: height:auto must now sum in the anonymous box built for the loose \"x\" text -- a single line at the default face");
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

        const tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(outer_box != NULL);
        if (outer_box != NULL) {
            const tbox_layout_box *inner_box = outer_box->first_child;
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

        const tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            const tbox_layout_box *li_box = ul_box->first_child;
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

        tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            tbox_layout_box *first  = ul_box->first_child;
            tbox_layout_box *second = first != NULL ? first->next_sibling : NULL;
            const tbox_layout_box *third  = second != NULL ? second->next_sibling : NULL;
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

        const tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            const tbox_layout_box *li_box = ul_box->first_child;
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

        tbox_layout_box *ol_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ol_box != NULL);
        if (ol_box != NULL) {
            tbox_layout_box *first  = ol_box->first_child;
            tbox_layout_box *second = first != NULL ? first->next_sibling : NULL;
            const tbox_layout_box *third  = second != NULL ? second->next_sibling : NULL;
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

    /* list-style-type picks the marker; the fourth <li> shows counters past
     * the first cycle (alpha) and multi-letter numerals (roman). */
    {
        static const struct { const char *css; const char *expected[4]; } cases[] = {
            {"ol { list-style-type: lower-alpha; }", {"a. x", "b. x", "c. x", "d. x"}},
            {"ol { list-style: upper-roman inside; }", {"I. x", "II. x", "III. x", "IV. x"}},
            {"ol { list-style-type: lower-roman; } li + li + li { list-style-type: none; }", {"i. x", "ii. x", "x", "x"}},
            {"ol { list-style-type: disc; }", {"\xE2\x80\xA2 x", "\xE2\x80\xA2 x", "\xE2\x80\xA2 x", "\xE2\x80\xA2 x"}},
            {"ol { list-style-type: decimal; list-style: square; }", {NULL, NULL, NULL, NULL}},
        };
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            tbox_html_document *doc    = parse_html_cstr("<ol><li>x</li><li>x</li><li>x</li><li>x</li></ol>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[c].css);
            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);
            tbox_layout_box *ol_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(ol_box != NULL);
            size_t i = 0;
            for (tbox_layout_box *li = ol_box != NULL ? ol_box->first_child : NULL; li != NULL; li = li->next_sibling, i++) {
                TBOX_TEST_ASSERT(li->text_run_count == 1);
                if (li->text_run_count != 1) continue;
                if (cases[c].expected[i] != NULL) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(li->text_runs[0].text, cases[c].expected[i]), cases[c].css);
                } else {
                    /* square: U+25AA when the face has it, else the bullet */
                    const char *square = tbox_font_face_has_glyph(li->text_runs[0].font, 0x25AA) ?
                        "\xE2\x96\xAA x" : "\xE2\x80\xA2 x";
                    TBOX_TEST_ASSERT(string_view_equal_cstr(li->text_runs[0].text, square));
                }
            }
            TBOX_TEST_ASSERT(i == 4);
            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* text-transform rewrites the measured text; inline children inherit
     * it and can reset it. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>olá mundo <span>são paulo</span> <b>fim</b></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "p { text-transform: uppercase; } span { display: inline; text-transform: capitalize; }"
            " b { display: inline; text-transform: none; }");
        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);
        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL && box->text_run_count == 3);
        if (box != NULL && box->text_run_count == 3) {
            TBOX_TEST_ASSERT(string_view_equal_cstr(box->text_runs[0].text, "OLÁ MUNDO"));
            TBOX_TEST_ASSERT(string_view_equal_cstr(box->text_runs[1].text, "São Paulo"));
            TBOX_TEST_ASSERT(string_view_equal_cstr(box->text_runs[2].text, "fim"));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* word-break: break-all fills each line up to the width, breaking
     * inside words; without it the long word moves to its own line. */
    {
        for (int all = 0; all < 2; all++) {
            tbox_html_document *doc    = parse_html_cstr("<p>ab cdefghijklmnopqrstuvwxyz</p>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr(all ? "p { width: 100px; word-break: break-all; }" : "p { width: 100px; }");
            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);
            tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(box != NULL && box->text_run_count >= 2);
            if (box != NULL && box->text_run_count >= 2) {
                const tbox_layout_text_run *first = &box->text_runs[0];
                if (!all) {
                    TBOX_TEST_ASSERT(string_view_equal_cstr(first->text, "ab"));
                } else {
                    TBOX_TEST_ASSERT(first->text.size > 3 && first->text.data[0] == 'a' && first->text.data[2] == ' ');
                    TBOX_TEST_ASSERT(first->rect.width <= 100.0);
                    size_t total = 0;
                    for (size_t r = 0; r < box->text_run_count; r++) {
                        TBOX_TEST_ASSERT(box->text_runs[r].rect.width <= 100.0);
                        total += box->text_runs[r].text.size;
                    }
                    TBOX_TEST_ASSERT(total == strlen("ab cdefghijklmnopqrstuvwxyz"));
                }
            }
            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
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

        const tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            const tbox_layout_box *li_box = ul_box->first_child;
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *li_box = div_box->first_child;
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
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

        const tbox_layout_box *ul_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(ul_box != NULL);
        if (ul_box != NULL) {
            const tbox_layout_box *li_box = ul_box->first_child;
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

    /* 47: NOVO v12 (Tarefa 5) -- <p style="font-family: Verdana;">x</p>: the
     * style="" declared font-family must flow all the way through the
     * Layout Tree's tbox_font_face_cache_get calls (Tarefa 2's
     * style->font_family, threaded by this task) to the face actually
     * chosen for the run -- proven by identity against a direct
     * tbox_font_face_cache_get(fonts, "Verdana", false, 16.0) call using
     * the SAME cache. Needs its own cache (built with a test resolver, same
     * pattern as tests/font/test_font.c) since the shared `fonts` cache
     * above has resolver == NULL and would fail to resolve any non-empty
     * family. */
    {
        tbox_test_layout_resolver_state resolver_state = {
            .font_data = font_data,
            .font_size = font_size,
            .calls     = 0,
        };
        tbox_font_face_cache *family_fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, tbox_test_layout_resolver, &resolver_state);
        TBOX_TEST_ASSERT_MSG(family_fonts != NULL, "failed to create font face cache with test resolver");

        if (family_fonts != NULL) {
            tbox_html_document *doc    = parse_html_cstr("<p style=\"font-family: Verdana;\">x</p>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr("");

            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

            const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, family_fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                TBOX_TEST_ASSERT_MSG(box->text_run_count == 1, "\"x\" must fit on a single run");
                if (box->text_run_count == 1) {
                    const tbox_font_face *expected = tbox_font_face_cache_get(family_fonts, tbox_string_view_make("Verdana", strlen("Verdana")), false, false, 16.0);
                    TBOX_TEST_ASSERT_MSG(box->text_runs[0].font == expected, "the declared style=\"font-family: Verdana;\" must reach the face chosen by the Layout Tree");
                }
            }

            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);

            /* 48: NOVO v12 (Tarefa 5) regression -- a plain <p> with NO
             * font-family declared anywhere resolves style->font_family ==
             * "" (Tarefa 2), so tbox_font_face_cache_get must take the
             * default/empty-family fast path and never call the resolver at
             * all -- even on a cache that HAS a resolver configured (this
             * same family_fonts cache, used by test 47 above). Uses the
             * SAME resolver_state so the call count is directly comparable
             * -- it must still read exactly 1 (from test 47's single
             * resolve), not 2. */
            tbox_html_document *doc_plain    = parse_html_cstr("<p>x</p>");
            const tbox_html_node *root_plain = tbox_html_document_root(doc_plain);
            tbox_css_stylesheet *sheet_plain = parse_css_cstr("");

            tbox_arena arena_plain               = tbox_arena_create(0);
            tbox_css_cascade_source source_plain = { sheet_plain, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table_plain         = tbox_style_resolve_tree(&arena_plain, root_plain, &source_plain, 1);

            int calls_before = resolver_state.calls;

            const tbox_layout_box *box_plain = tbox_layout_build(&arena_plain, root_plain, &table_plain, family_fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(box_plain != NULL);
            if (box_plain != NULL) {
                TBOX_TEST_ASSERT_MSG(box_plain->text_run_count == 1, "\"x\" must fit on a single run");
                if (box_plain->text_run_count == 1) {
                    const tbox_font_face *default_regular_16 = tbox_font_face_cache_get(family_fonts, tbox_string_view_make(NULL, 0), false, false, 16.0);
                    TBOX_TEST_ASSERT_MSG(box_plain->text_runs[0].font == default_regular_16, "a <p> with no font-family declared must keep resolving the same default (empty-family) face");
                }
            }
            TBOX_TEST_ASSERT_MSG(resolver_state.calls == calls_before, "a <p> with no font-family declared must never invoke the resolver (default/empty-family fast path)");

            tbox_arena_destroy(&arena_plain);
            tbox_css_stylesheet_destroy(sheet_plain);
            tbox_html_document_destroy(doc_plain);

            tbox_font_face_cache_destroy(family_fonts);
        }
    }

    /* 49: NOVO v13 -- <p style="font-family: Verdana;"><i>italic</i> normal</p>:
     * font_italic reaches face selection -- the <i> run resolves to a
     * DIFFERENT face pointer than the plain run even though weight/size/
     * family are all identical (only italic differs). Needs its own
     * resolver-backed cache (same pattern as test 47) since the default
     * empty-family fast path deliberately ignores `italic` (see
     * tbox_font_face_cache_get's doc comment: the default family never had
     * an italic face of its own) -- a non-empty, shared font-family is
     * required to actually exercise the italic axis. */
    {
        tbox_test_layout_resolver_state resolver_state = {
            .font_data = font_data,
            .font_size = font_size,
            .calls     = 0,
        };
        tbox_font_face_cache *italic_fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, tbox_test_layout_resolver, &resolver_state);
        TBOX_TEST_ASSERT_MSG(italic_fonts != NULL, "failed to create font face cache with test resolver");

        if (italic_fonts != NULL) {
            tbox_html_document *doc    = parse_html_cstr("<p style=\"font-family: Verdana;\"><i>italic</i> normal</p>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr("i { display: inline; font-style: italic; }");

            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

            const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, italic_fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(box != NULL);
            if (box != NULL) {
                TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"italic\" + \"normal\" must merge into exactly two runs (one per face) on one line");
                if (box->text_run_count == 2) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "italic"), "the first run must be the italic word");
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "normal"), "the second run must be the plain word");
                    TBOX_TEST_ASSERT_MSG(box->text_runs[0].font != box->text_runs[1].font, "the <i> run must resolve to a DIFFERENT face pointer than the plain run, even with identical weight/size/family");

                    const tbox_font_face *expected_italic = tbox_font_face_cache_get(italic_fonts, tbox_string_view_make("Verdana", strlen("Verdana")), false, true, 16.0);
                    const tbox_font_face *expected_normal  = tbox_font_face_cache_get(italic_fonts, tbox_string_view_make("Verdana", strlen("Verdana")), false, false, 16.0);
                    TBOX_TEST_ASSERT(box->text_runs[0].font == expected_italic);
                    TBOX_TEST_ASSERT(box->text_runs[1].font == expected_normal);
                }
            }

            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);

            tbox_font_face_cache_destroy(italic_fonts);
        }
    }

    /* 50: NOVO v13 -- <p>Normal <small>pequeno</small></p>: a run whose
     * face is SMALLER than the line's dominant face gets shifted DOWN by
     * exactly `line->ascent - tbox_font_face_ascent(small_face)` so its own
     * baseline lines up with the rest of the line instead of "floating"
     * high, aligned to the line's TOP like every run before v13. Declares
     * `font-size: 50%` directly (TASKS.md: fine to author this manually
     * instead of depending on the real UA stylesheet's `small { font-size:
     * 80%; }`, which Tarefa 5 owns) so this stays isolated from that other
     * task. Verified with the EXACT formula, not just "is different". */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>Normal <small>pequeno</small></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("small { display: inline; font-size: 50%; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"Normal\" + \"pequeno\" must merge into exactly two runs (one per face) on one line");
            if (box->text_run_count == 2) {
                const tbox_font_face *small_face = tbox_font_face_cache_get(fonts, tbox_string_view_make(NULL, 0), false, false, 8.0);
                TBOX_TEST_ASSERT_MSG(small_face != NULL, "failed to resolve the 8px (50% of 16px) small face");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].font == small_face, "the <small> run must resolve to the 50%-of-16px face");

                double ascent_normal = tbox_font_face_ascent(regular_16);
                double ascent_small  = tbox_font_face_ascent(small_face);
                TBOX_TEST_ASSERT_MSG(ascent_normal >= ascent_small, "a 16px face's ascent must be at least as large as an 8px face's -- otherwise this test's premise doesn't hold");
                double line_ascent = ascent_normal > ascent_small ? ascent_normal : ascent_small;

                double line_y             = box->content_box.y;
                double expected_normal_y  = line_y + (line_ascent - ascent_normal);
                double expected_small_y   = line_y + (line_ascent - ascent_small);

                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(box->text_runs[0].rect.y, expected_normal_y), "the normal-size run's rect.y must equal line_y + (line->ascent - its own ascent)");
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(box->text_runs[1].rect.y, expected_small_y), "the <small> run's rect.y must equal line_y + (line->ascent - its own ascent)");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].rect.y > box->text_runs[0].rect.y, "the smaller run must sit LOWER (larger rect.y) than the line's dominant baseline, not float at the line's top");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 51: NOVO v13 -- <p>Normal <sub>baixo</sub></p>: `vertical-align: sub`
     * adds `+ 0.15 * font_size` ON TOP OF baseline alignment. Declares ONLY
     * `vertical-align: sub` (no font-size change), so this run shares the
     * SAME face as the plain run -- isolating the sub/sup term from test
     * 50's face-size-driven baseline-alignment term (which is exactly 0
     * here, since both runs share one face/ascent): the ENTIRE rect.y
     * difference between the two runs must be exactly `0.15 * font_size`. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>Normal <sub>baixo</sub></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("sub { display: inline; vertical-align: sub; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"Normal\" + \"baixo\" must still be TWO runs (style differs) even though the face is identical");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].font == box->text_runs[1].font, "the <sub> run shares the SAME face as the plain run here (no font-size change declared)");

                double expected_extra = 0.15 * 16.0;
                double actual_extra   = box->text_runs[1].rect.y - box->text_runs[0].rect.y;
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(actual_extra, expected_extra), "the <sub> run must sit exactly 0.15 * font_size BELOW the plain run's rect.y");
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(box->text_runs[0].rect.y, box->content_box.y), "the plain run must still sit exactly at line_y (same face as the line's other run, zero baseline-alignment offset)");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 52: NOVO v13 -- <p>Normal <sup>alto</sup></p>: `vertical-align: super`
     * subtracts `0.35 * font_size` past baseline alignment (moves UP, the
     * opposite sign of test 51's SUB). Same isolation strategy: no
     * font-size change declared, so both runs share one face/ascent and the
     * entire rect.y difference is exactly the sup term. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>Normal <sup>alto</sup></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("sup { display: inline; vertical-align: super; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"Normal\" + \"alto\" must still be TWO runs (style differs) even though the face is identical");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].font == box->text_runs[1].font, "the <sup> run shares the SAME face as the plain run here (no font-size change declared)");

                double expected_extra = -0.35 * 16.0;
                double actual_extra   = box->text_runs[1].rect.y - box->text_runs[0].rect.y;
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(actual_extra, expected_extra), "the <sup> run must sit exactly 0.35 * font_size ABOVE the plain run's rect.y");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* vertical-align lengths raise the run; text-top/text-bottom line a
     * smaller run's top/bottom up with the block font's own run. */
    {
        const char *css[] = {
            "span { display: inline; vertical-align: 4px; }",
            "span { display: inline; vertical-align: -0.5em; }",
            "span { display: inline; font-size: 8px; vertical-align: text-top; }",
            "span { display: inline; font-size: 8px; vertical-align: text-bottom; }",
        };
        for (size_t i = 0; i < sizeof(css) / sizeof(css[0]); i++) {
            tbox_html_document *doc    = parse_html_cstr("<p>Normal <span>alto</span></p>");
            const tbox_html_node *root = tbox_html_document_root(doc);
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);

            tbox_arena arena               = tbox_arena_create(0);
            tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
            tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

            tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
            TBOX_TEST_ASSERT(box != NULL && box->text_run_count == 2);
            if (box != NULL && box->text_run_count == 2) {
                const tbox_layout_text_run *plain = &box->text_runs[0];
                const tbox_layout_text_run *span = &box->text_runs[1];
                double extra = span->rect.y - plain->rect.y;
                if (i == 0) TBOX_TEST_ASSERT(tbox_test_double_approx_equal(extra, -4.0));
                if (i == 1) TBOX_TEST_ASSERT(tbox_test_double_approx_equal(extra, 8.0));
                if (i == 2) TBOX_TEST_ASSERT(tbox_test_double_approx_equal(extra, 0.0));
                if (i == 3) TBOX_TEST_ASSERT(tbox_test_double_approx_equal(
                    span->rect.y + tbox_font_face_line_height(span->font),
                    plain->rect.y + tbox_font_face_line_height(plain->font)));
            }

            tbox_arena_destroy(&arena);
            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 53: NOVO v13 regression -- <p>um<br>dois</p>, all text sharing ONE
     * face/style (no <small>/<sub>/<sup>/<mark> anywhere): every run's
     * rect.y stays EXACTLY `line_y` (zero baseline-alignment offset, zero
     * vertical-align offset) -- proves v0-v12 didn't regress now that
     * rect.y is a computed expression instead of `line_y` copied verbatim.
     * Also checks run->style is populated (never NULL) and points at the
     * SAME style that decided the run's face -- here, the <p>'s own. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>um<br>dois</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "\"um\" and \"dois\" must be two runs, one per line");
            if (box->text_run_count == 2) {
                double line_height = tbox_font_face_line_height(regular_16);
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].rect.y == box->content_box.y, "the first line's run must sit EXACTLY at line_y (zero offset when every run shares one face)");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].rect.y == box->content_box.y + line_height, "the second line's run must sit EXACTLY at its own line_y, same zero-offset rule");
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].style == box->style, "run->style must be populated with the SAME style that decided the run's face -- here, the <p>'s own resolved style");
                TBOX_TEST_ASSERT_MSG(box->text_runs[1].style == box->style, "run->style must be populated with the SAME style that decided the run's face -- here, the <p>'s own resolved style");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 54: NOVO v13 -- <p><mark>x</mark> normal</p> where BOTH resolve to
     * the IDENTICAL face (no weight/size/family/italic declared on `mark`)
     * -- style is now ALSO part of the run-merge key (not just face), so
     * this still produces TWO separate runs, not one merged run: Render
     * Pipeline needs a distinct run->style per stretch for the <mark>
     * background highlight, which a merged run couldn't carry. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p><mark>x</mark> normal</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("mark { display: inline; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->text_run_count == 2, "<mark> must still produce a SEPARATE run from the plain text even though both resolve to the identical face -- style is now part of the merge key too");
            if (box->text_run_count == 2) {
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].font == box->text_runs[1].font, "both runs must share the IDENTICAL face -- <mark> declares no weight/size/family/italic override");
                TBOX_TEST_ASSERT_MSG(box->text_runs[0].style != box->text_runs[1].style, "the two runs must carry DIFFERENT style pointers (the <mark>'s own vs. the <p>'s), even with an identical face");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[0].text, "x"), "the first run must be the <mark> word");
                TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text_runs[1].text, "normal"), "the second run must be the plain word");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 55: NOVO v14 -- <div><strong>Bold</strong></div>: an inline element
     * SOLTO directly inside a block container, with no <p>/h1-h6/li/pre
     * wrapping it at all, must now produce visible text -- the whole point
     * of v14's fix. The div's ONLY child box is an ANONYMOUS one
     * (box->node == NULL), reusing the same inline formatting context a
     * real text-tag element already uses. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><strong>Bold</strong></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("strong { display: inline; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *anon = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(anon != NULL, "a loose <strong> with no wrapping text tag must still produce a child box");
            if (anon != NULL) {
                TBOX_TEST_ASSERT_MSG(anon->node == NULL, "the box built for loose inline content must be ANONYMOUS (node == NULL)");
                TBOX_TEST_ASSERT_MSG(anon->text_run_count == 1, "\"Bold\" must fit on a single run");
                if (anon->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(anon->text_runs[0].text, "Bold"), "the anonymous box's run must be the <strong>'s own text");
                }
                TBOX_TEST_ASSERT_MSG(anon->next_sibling == NULL, "the div must have exactly ONE child box -- the anonymous box for the whole loose sequence");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 56: NOVO v14 regression -- <div><p>a</p>\n  <p>b</p></div>: the
     * whitespace-only TEXT node sitting between the two <p>s must NOT
     * trigger an anonymous box (it never becomes non-empty after
     * tbox_string_collapse_whitespace) -- the div's children stay exactly
     * the two <p> boxes, in document order, immediately stacked with no
     * gap (same "no margin declared" geometry test 3 above already
     * exercises for two plain <div>s). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>a</p>\n  <p>b</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *p1 = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(p1 != NULL, "the div must have a first child box");
            if (p1 != NULL) {
                TBOX_TEST_ASSERT_MSG(p1->node != NULL && string_view_equal_cstr(p1->node->element.tag_name, "p"), "the FIRST child box must be the real <p> element, not an anonymous box for the whitespace before it");
                TBOX_TEST_ASSERT_MSG(p1->text_run_count == 1 && string_view_equal_cstr(p1->text_runs[0].text, "a"), "the first <p>'s own text must be \"a\"");

                const tbox_layout_box *p2 = p1->next_sibling;
                TBOX_TEST_ASSERT_MSG(p2 != NULL, "the div must have a second child box");
                if (p2 != NULL) {
                    TBOX_TEST_ASSERT_MSG(p2->node != NULL && string_view_equal_cstr(p2->node->element.tag_name, "p"), "the SECOND child box must be the real <p> element, not an anonymous box for the whitespace between the two <p>s");
                    TBOX_TEST_ASSERT_MSG(p2->text_run_count == 1 && string_view_equal_cstr(p2->text_runs[0].text, "b"), "the second <p>'s own text must be \"b\"");
                    TBOX_TEST_ASSERT_MSG(p2->margin_box.y == p1->margin_box.height, "the two <p>s must stack with NO gap -- no anonymous box was inserted for the whitespace-only text between them");
                    TBOX_TEST_ASSERT_MSG(p2->next_sibling == NULL, "the div must have EXACTLY two child boxes -- no anonymous box after the second <p> either");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 57: NOVO v14 -- the exact mixed pattern 011.html exercises: a <div>
     * with a loose inline element (<small>) as its FIRST child, followed by
     * two block <p>s. Produces THREE child boxes in document order: an
     * anonymous box for the <small>'s text, then the two <p> boxes --
     * proving anonymous and real block boxes interleave correctly and
     * document order/geometry (increasing, non-overlapping Y) is
     * preserved. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><small>Small</small><p>a</p><p>b</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("small { display: inline; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *anon = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(anon != NULL, "the div must have a first child box (the anonymous box for the loose <small>)");
            if (anon != NULL) {
                TBOX_TEST_ASSERT_MSG(anon->node == NULL, "the FIRST child box must be anonymous (the loose <small>'s text)");
                TBOX_TEST_ASSERT_MSG(anon->text_run_count == 1 && string_view_equal_cstr(anon->text_runs[0].text, "Small"), "the anonymous box's run must be the <small>'s own text");

                const tbox_layout_box *p1 = anon->next_sibling;
                TBOX_TEST_ASSERT_MSG(p1 != NULL && p1->node != NULL && string_view_equal_cstr(p1->node->element.tag_name, "p"), "the SECOND child box must be the real first <p>");
                if (p1 != NULL) {
                    TBOX_TEST_ASSERT_MSG(p1->margin_box.y == anon->margin_box.height, "the first <p> must sit immediately below the anonymous box, no overlap/gap (anonymous box has no margin)");

                    const tbox_layout_box *p2 = p1->next_sibling;
                    TBOX_TEST_ASSERT_MSG(p2 != NULL && p2->node != NULL && string_view_equal_cstr(p2->node->element.tag_name, "p"), "the THIRD child box must be the real second <p>");
                    if (p2 != NULL) {
                        TBOX_TEST_ASSERT_MSG(p2->margin_box.y == p1->margin_box.y + p1->margin_box.height, "the second <p> must sit immediately below the first, no overlap/gap");
                        TBOX_TEST_ASSERT_MSG(p2->next_sibling == NULL, "the div must have EXACTLY three child boxes -- anonymous, <p>, <p>, in document order");
                    }
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 58: NOVO v14 -- <div>texto <span style="display:none">oculto</span>
     * mais texto</div>: a display:none ELEMENT sitting in the MIDDLE of a
     * loose-inline sequence is transparent to the sequence-detection scan
     * (per ARCHITECTURE.md's algorithm) -- it neither starts nor ends the
     * sequence, so the text before AND after it merge into a SINGLE
     * anonymous box, not two separate ones. The hidden element itself
     * contributes no words (same display:none treatment as anywhere else). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>texto <span style=\"display:none;\">oculto</span> mais texto</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *anon = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(anon != NULL, "the div must have a first child box");
            if (anon != NULL) {
                TBOX_TEST_ASSERT_MSG(anon->node == NULL, "the child box must be anonymous");
                TBOX_TEST_ASSERT_MSG(anon->text_run_count == 1, "the text before and after the hidden <span> must merge into a SINGLE run -- the display:none element never breaks the sequence");
                if (anon->text_run_count == 1) {
                    TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(anon->text_runs[0].text, "texto mais texto"), "the hidden <span>'s text must contribute nothing -- only \"texto\" and \"mais texto\" survive, joined as one run");
                }
                TBOX_TEST_ASSERT_MSG(anon->next_sibling == NULL, "the div must have EXACTLY one child box -- one anonymous box for the whole sequence");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 59: NOVO v14 -- <div>antes <span style="position:absolute;">flutuante
     * </span> depois</div>: an out-of-flow (position:absolute) inline
     * element sitting in the MIDDLE of loose inline content TERMINATES the
     * sequence before it (per ARCHITECTURE.md's "Fora de escopo") -- three
     * child boxes result, in document order: an anonymous box for "antes",
     * the <span> itself (built via the ordinary out-of-flow path, own box,
     * own node), and a SECOND, separate anonymous box for "depois". */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>antes <span style=\"position:absolute;\">flutuante</span> depois</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *first = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(first != NULL, "the div must have a first child box");
            if (first != NULL) {
                TBOX_TEST_ASSERT_MSG(first->node == NULL, "the FIRST child box must be anonymous (the text before the positioned <span>)");
                TBOX_TEST_ASSERT_MSG(first->text_run_count == 1 && string_view_equal_cstr(first->text_runs[0].text, "antes"), "the first anonymous box's text must be exactly \"antes\" -- the positioned <span> must NOT be absorbed into it");

                const tbox_layout_box *second = first->next_sibling;
                TBOX_TEST_ASSERT_MSG(second != NULL, "the div must have a second child box");
                if (second != NULL) {
                    TBOX_TEST_ASSERT_MSG(second->node != NULL && string_view_equal_cstr(second->node->element.tag_name, "span"), "the SECOND child box must be the <span> itself, built via the ordinary out-of-flow path -- not folded into any anonymous box");

                    const tbox_layout_box *third = second->next_sibling;
                    TBOX_TEST_ASSERT_MSG(third != NULL, "the div must have a third child box");
                    if (third != NULL) {
                        TBOX_TEST_ASSERT_MSG(third->node == NULL, "the THIRD child box must be a SEPARATE anonymous box (the text after the positioned <span>)");
                        TBOX_TEST_ASSERT_MSG(third->text_run_count == 1 && string_view_equal_cstr(third->text_runs[0].text, "depois"), "the second anonymous box's text must be exactly \"depois\"");
                        TBOX_TEST_ASSERT_MSG(third->next_sibling == NULL, "the div must have EXACTLY three child boxes -- anonymous, <span>, anonymous, in document order");
                    }
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 60: NOVO v14 -- the anonymous box's synthesized style inherits ONLY
     * the six inheritable tbox_style fields (color/font_family/
     * font_weight_bold/font_italic/font_size/text_align) from the
     * container, and NOTHING else -- proven with two separate documents:
     * (a) <div style="color: red; font-weight: bold;">texto solto</div>
     * must reach the resulting run's style with color red and
     * font_weight_bold true (inheritable properties DO flow through); (b)
     * <div style="background-color: blue; border: 1px solid black;">texto
     * solto</div> must NOT give the anonymous box itself (not the text
     * inside it -- the BOX) any background/border of its own, proving it
     * never paints a second copy of the container's background/border (see
     * ARCHITECTURE.md's "Escopo" rationale for why the alternative --
     * reusing the container's own tbox_style* outright -- was rejected). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div style=\"color: red; font-weight: bold;\">texto solto</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            tbox_layout_box *anon = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(anon != NULL && anon->node == NULL, "the div's loose text must produce an anonymous child box");
            if (anon != NULL && anon->text_run_count == 1) {
                const tbox_style *run_style = anon->text_runs[0].style;
                TBOX_TEST_ASSERT_MSG(run_style->color.r == 255 && run_style->color.g == 0 && run_style->color.b == 0 && run_style->color.a == 255, "the anonymous box's run must inherit the container's declared color: red");
                TBOX_TEST_ASSERT_MSG(run_style->font_weight_bold == true, "the anonymous box's run must inherit the container's declared font-weight: bold");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }
    {
        tbox_html_document *doc    = parse_html_cstr("<div style=\"background-color: blue; border: 1px solid black;\">texto solto</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(div_box != NULL);
        if (div_box != NULL) {
            const tbox_layout_box *anon = div_box->first_child;
            TBOX_TEST_ASSERT_MSG(anon != NULL && anon->node == NULL, "the div's loose text must produce an anonymous child box");
            if (anon != NULL) {
                TBOX_TEST_ASSERT_MSG(anon->style->background_color.a == 0, "the anonymous box ITSELF must have a transparent background -- background-color is NOT inheritable, and reusing the container's own style would have double-painted its blue background");
                TBOX_TEST_ASSERT_MSG(anon->style->border_style == TBOX_STYLE_BORDER_STYLE_NONE, "the anonymous box ITSELF must have no border of its own -- border is NOT inheritable, and reusing the container's own style would have double-painted its black border");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* NOVO (image support): <img> as an inline "word" -- black.png/
     * yellow.png (tests/assets/, also used by tests/assets/024.html's real
     * regression fixture) are known 200x200 RGBA fixtures. */
    {
        tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(images != NULL);
        if (images != NULL) {
            /* 1: both width/height AUTO falls back to the image's intrinsic
             * pixel dimensions. */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"black.png\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon != NULL && anon->text_run_count == 1, "a lone <img> under a block container must produce one run in its anonymous box");
                    if (anon != NULL && anon->text_run_count == 1) {
                        TBOX_TEST_ASSERT_MSG(anon->text_runs[0].image != NULL, "the run must carry the decoded image");
                        TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(anon->text_runs[0].rect.width, 200.0) && tbox_test_double_approx_equal(anon->text_runs[0].rect.height, 200.0), "both-AUTO must resolve to black.png's own 200x200 intrinsic size");
                    }
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 2: bare HTML width/height attributes (no style="") override
             * AUTO -- see tbox_style_resolve_img_dimension_attribute in
             * src/style/tbox_style.c, same behavior tests/assets/024.html's
             * first <img> (width="100" height="100") exercises for real. */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"black.png\" width=\"50\" height=\"50\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon != NULL && anon->text_run_count == 1, "a lone <img> must still produce one run");
                    if (anon != NULL && anon->text_run_count == 1) {
                        TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(anon->text_runs[0].rect.width, 50.0) && tbox_test_double_approx_equal(anon->text_runs[0].rect.height, 50.0), "HTML width/height attributes must override the intrinsic 200x200 size");
                    }
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 3: a CSS declaration wins over the HTML attribute (CSS always
             * has priority over the "presentational hint" attribute
             * fallback). */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"black.png\" width=\"50\" height=\"50\" style=\"width:10px;height:10px;\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon != NULL && anon->text_run_count == 1, "a lone <img> must still produce one run");
                    if (anon != NULL && anon->text_run_count == 1) {
                        TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(anon->text_runs[0].rect.width, 10.0) && tbox_test_double_approx_equal(anon->text_runs[0].rect.height, 10.0), "an inline style=\"\" declaration must win over the width/height attributes");
                    }
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 4: a missing/undecodable src with NO `alt` attribute
             * contributes nothing -- no run at all, same as an empty text
             * word, never a crash. */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"does-not-exist.png\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon == NULL || anon->text_run_count == 0, "a missing src with no alt must contribute zero runs, never crash");
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 4b: a missing/undecodable src WITH a non-empty `alt` falls
             * back to that text, rendered as an ordinary text run (not an
             * image run) -- the exact tests/assets/024.html scenario
             * (`<img src="notfound.png" alt="Image not found">`). */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"does-not-exist.png\" alt=\"Image not found\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon != NULL && anon->text_run_count == 1, "a missing src WITH alt text must produce one text run");
                    if (anon != NULL && anon->text_run_count == 1) {
                        TBOX_TEST_ASSERT_MSG(anon->text_runs[0].image == NULL, "the alt-text fallback must be an ordinary TEXT run, not an image run");
                        TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(anon->text_runs[0].text, "Image not found"), "the fallback run's text must be the alt attribute's value");
                    }
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 4c: alt="" (explicitly empty -- the standard "decorative
             * image" marker) contributes nothing, same as no alt at all. */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><img src=\"does-not-exist.png\" alt=\"\"></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon == NULL || anon->text_run_count == 0, "an explicitly empty alt=\"\" must contribute zero runs, same as no alt at all");
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* 5: <a><img></a> -- the bounded one-level img-in-inline
             * extension (tests/assets/024.html's linked-image case). The
             * <a> itself is display:inline with no <img> sibling text, so
             * this must still produce exactly one image run, not a
             * flattened-to-empty-text run. */
            {
                tbox_html_document *doc    = parse_html_cstr("<div><a href=\"x\"><img src=\"yellow.png\" width=\"20\" height=\"20\"></a></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr("a { display: inline; } img { display: inline; }");

                tbox_arena arena               = tbox_arena_create(0);
                tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
                tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

                const tbox_layout_box *div_box = tbox_layout_build(&arena, root, &table, fonts, images, 800.0, 600.0);
                TBOX_TEST_ASSERT(div_box != NULL);
                if (div_box != NULL) {
                    const tbox_layout_box *anon = div_box->first_child;
                    TBOX_TEST_ASSERT_MSG(anon != NULL && anon->text_run_count == 1, "<a><img></a> must produce exactly one image run");
                    if (anon != NULL && anon->text_run_count == 1) {
                        TBOX_TEST_ASSERT_MSG(anon->text_runs[0].image != NULL, "the run must carry the decoded image, not be flattened to empty text");
                        TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(anon->text_runs[0].rect.width, 20.0) && tbox_test_double_approx_equal(anon->text_runs[0].rect.height, 20.0), "the nested <img>'s own width/height attributes must still apply");
                    }
                }

                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            /* Percentage max-width on an inline image uses the text box's
             * available width; min-width wins when both limits conflict. */
            {
                tbox_html_document *doc = parse_html_cstr("<div><img src='black.png'></div>");
                const tbox_html_node *root = tbox_html_document_root(doc);
                tbox_css_stylesheet *sheet = parse_css_cstr(
                    "div { width: 100px; } img { display: inline; max-width: 50%; min-width: 60px; }");
                tbox_arena arena = tbox_arena_create(0);
                tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
                tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
                const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, images, 800.0, 600.0);
                const tbox_layout_box *anon = layout != NULL ? layout->first_child : NULL;
                TBOX_TEST_ASSERT(anon != NULL && anon->text_run_count == 1);
                if (anon != NULL && anon->text_run_count == 1)
                    TBOX_TEST_ASSERT(tbox_test_double_approx_equal(anon->text_runs[0].rect.width, 60.0));
                tbox_arena_destroy(&arena);
                tbox_css_stylesheet_destroy(sheet);
                tbox_html_document_destroy(doc);
            }

            tbox_image_cache_destroy(images);
        }
    }

    /* NOVO (table support): <table>/<tr>/<th>/<td> -- real column-aligned
     * tables (see ARCHITECTURE.md's table-support section). */

    /* 1: two columns, content-proportional widths that sum to exactly the
     * table's own content width, laid out side by side (not stacked). */
    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr><td>Hi</td><td>Hello there, a much longer cell</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL);
        if (table_box != NULL) {
            tbox_layout_box *row_box = table_box->first_child;
            TBOX_TEST_ASSERT_MSG(row_box != NULL && row_box->next_sibling == NULL, "the table must have exactly one row box");
            if (row_box != NULL) {
                tbox_layout_box *cell1 = row_box->first_child;
                const tbox_layout_box *cell2 = cell1 != NULL ? cell1->next_sibling : NULL;
                TBOX_TEST_ASSERT_MSG(cell1 != NULL && cell2 != NULL && cell2->next_sibling == NULL, "the row must have exactly two cells");
                if (cell1 != NULL && cell2 != NULL) {
                    TBOX_TEST_ASSERT_MSG(cell2->border_box.width > cell1->border_box.width, "the column with longer text must be wider");
                    TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(cell1->border_box.width + cell2->border_box.width, table_box->content_box.width), "columns must sum to exactly the table's own content width");
                    TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(cell1->border_box.x, table_box->content_box.x), "the first column must start at the table's own content edge");
                    TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(cell2->border_box.x, cell1->border_box.x + cell1->border_box.width), "the second column must start right after the first one ends");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 2: every cell empty (natural width 0) falls back to an equal share
     * per column, never a divide-by-zero/crash. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr><td></td><td></td><td></td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL);
        if (table_box != NULL && table_box->first_child != NULL) {
            tbox_layout_box *cell1 = table_box->first_child->first_child;
            tbox_layout_box *cell2 = cell1 != NULL ? cell1->next_sibling : NULL;
            const tbox_layout_box *cell3 = cell2 != NULL ? cell2->next_sibling : NULL;
            TBOX_TEST_ASSERT(cell1 != NULL && cell2 != NULL && cell3 != NULL);
            if (cell1 != NULL && cell2 != NULL && cell3 != NULL) {
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(cell1->border_box.width, cell2->border_box.width) && tbox_test_double_approx_equal(cell2->border_box.width, cell3->border_box.width), "all-empty cells must fall back to an equal-share column width");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 3: a ragged row (fewer cells than the widest row) gets only as many
     * cell boxes as it actually has -- and that cell still uses the SAME
     * table-wide column width as the row above it, never a width computed
     * from its own row in isolation. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr><td>A</td><td>B</td><td>C</td></tr><tr><td>Only one</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL);
        if (table_box != NULL) {
            tbox_layout_box *row1 = table_box->first_child;
            const tbox_layout_box *row2 = row1 != NULL ? row1->next_sibling : NULL;
            TBOX_TEST_ASSERT_MSG(row1 != NULL && row2 != NULL && row2->next_sibling == NULL, "the table must have exactly two row boxes");
            if (row1 != NULL && row2 != NULL) {
                const tbox_layout_box *row1_cell1 = row1->first_child;
                const tbox_layout_box *row2_cell1 = row2->first_child;
                TBOX_TEST_ASSERT_MSG(row2_cell1 != NULL && row2_cell1->next_sibling == NULL, "the ragged row must have exactly one cell box, not padded with empty ones");
                TBOX_TEST_ASSERT(row1_cell1 != NULL);
                if (row1_cell1 != NULL && row2_cell1 != NULL) {
                    TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(row1_cell1->border_box.width, row2_cell1->border_box.width), "a ragged row's cell must use the SAME table-wide column width as the row above it");
                }
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* The long ragged row and wide final header must leave enough room for
     * styled inline words in the middle column. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table><tr><th>Name</th><th>Role</th><th>Years Years Years</th></tr>"
            "<tr><td>Alice</td><td id='role'>Senior <b>Backend</b> Engineer</td><td>5</td></tr>"
            "<tr><td>Carol -- ragged row, só esta célula (sem Role/Years)</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "table { width: 500px; border: 1px solid black; }"
            "th, td { border: 1px solid gray; padding: 4px; }"
            "th { font-weight: bold; text-align: center; }"
            "b { display: inline; font-weight: bold; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, &source, 1);
        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        const tbox_layout_box *role = find_box_for_node(table_box, find_html_id(root, "role"));
        TBOX_TEST_ASSERT(role != NULL && role->text_run_count == 3);
        if (role != NULL && role->text_run_count == 3) {
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(role->text_runs[0].text, "Senior") &&
                string_view_equal_cstr(role->text_runs[1].text, "Backend"),
                "the first two role runs must be Senior and Backend");
            TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(role->text_runs[0].rect.y, role->text_runs[1].rect.y),
                "Senior and Backend must fit on the same line");
            TBOX_TEST_ASSERT_MSG(role->text_runs[2].rect.y > role->text_runs[1].rect.y,
                "Engineer may wrap to the next line");
        }
        TBOX_TEST_ASSERT(table_box != NULL && table_box->first_child != NULL);
        if (table_box != NULL && table_box->first_child != NULL) {
            tbox_layout_box *header = table_box->first_child;
            tbox_layout_box *years = header->last_child;
            TBOX_TEST_ASSERT(years != NULL && years->text_run_count > 0);
            if (years != NULL && years->text_run_count > 0) {
                for (size_t i = 0; i < years->text_run_count; i++) {
                    const tbox_layout_text_run *run = &years->text_runs[i];
                    TBOX_TEST_ASSERT_MSG(run->rect.x + run->rect.width <= years->content_box.x + years->content_box.width + 1e-6,
                        "each Years run must fit inside its own cell");
                }
            }
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 4: a wrapping cell sets the row height, and the short cell's border
     * stretches to the same bottom edge while its text stays on one line. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table style=\"width:100px;\"><tr><td>Short</td><td>This is a much longer piece of text with many separate words that will wrap across several lines when squeezed into a narrow column</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL && table_box->first_child != NULL);
        if (table_box != NULL && table_box->first_child != NULL) {
            tbox_layout_box *row_box = table_box->first_child;
            tbox_layout_box *cell1   = row_box->first_child;
            const tbox_layout_box *cell2   = cell1 != NULL ? cell1->next_sibling : NULL;
            TBOX_TEST_ASSERT(cell1 != NULL && cell2 != NULL);
            if (cell1 != NULL && cell2 != NULL) {
                TBOX_TEST_ASSERT_MSG(cell2->text_run_count > 1 && cell2->text_runs[cell2->text_run_count - 1].rect.y > cell2->text_runs[0].rect.y, "the long-text cell must wrap across multiple lines");
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(cell1->border_box.y + cell1->border_box.height, cell2->border_box.y + cell2->border_box.height), "both cells' borders must reach the same row bottom");
                TBOX_TEST_ASSERT_MSG(tbox_test_double_approx_equal(row_box->content_box.height, cell2->margin_box.height), "the row's own height must match its cells' stretched height");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 5: <th> resolves bold + centered by default (UA stylesheet), unlike
     * <td>. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr><th>Header</th></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        /* This harness resolves style against ONLY the CSS given here, no
         * UA stylesheet layered in (unlike the real tbox_context_open
         * pipeline) -- th's bold/center default is normally a UA rule
         * (tbox_ua_style_generate_css), so it must be restated here. */
        tbox_css_stylesheet *sheet = parse_css_cstr("th { font-weight: bold; text-align: center; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL && table_box->first_child != NULL && table_box->first_child->first_child != NULL);
        if (table_box != NULL && table_box->first_child != NULL && table_box->first_child->first_child != NULL) {
            const tbox_layout_box *th_box = table_box->first_child->first_child;
            TBOX_TEST_ASSERT_MSG(th_box->style->font_weight_bold, "<th> must be bold by default");
            TBOX_TEST_ASSERT_MSG(th_box->style->text_align == TBOX_STYLE_TEXT_ALIGN_CENTER, "<th> must be centered by default");
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 6: a <td> is a real text tag -- inline children (<b>) still produce
     * separate text runs with a distinct (bold) face, exactly like the
     * same markup inside a <p>. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr><td>Texto <b>negrito</b></td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        /* No UA stylesheet in this harness (see the <th> test above) --
         * <b>'s "display: inline; font-weight: bold;" is normally a UA
         * rule, restated here. */
        tbox_css_stylesheet *sheet = parse_css_cstr("b { display: inline; font-weight: bold; }");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT(table_box != NULL && table_box->first_child != NULL && table_box->first_child->first_child != NULL);
        if (table_box != NULL && table_box->first_child != NULL && table_box->first_child->first_child != NULL) {
            const tbox_layout_box *td_box = table_box->first_child->first_child;
            TBOX_TEST_ASSERT_MSG(td_box->text_run_count == 2, "a <td> with a <b> child must produce two runs, exactly like the same markup inside a <p>");
            if (td_box->text_run_count == 2) {
                TBOX_TEST_ASSERT(string_view_equal_cstr(td_box->text_runs[0].text, "Texto"));
                TBOX_TEST_ASSERT(string_view_equal_cstr(td_box->text_runs[1].text, "negrito"));
                TBOX_TEST_ASSERT_MSG(td_box->text_runs[1].font != td_box->text_runs[0].font, "the <b> run must use a different (bold) face than the plain-text run");
            }
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 7: an empty table (no rows) and a table with only empty rows (no
     * cells anywhere) must both produce zero row/cell boxes, never crash. */
    {
        tbox_html_document *doc    = parse_html_cstr("<table></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT_MSG(table_box != NULL && table_box->first_child == NULL, "an empty table must produce a box with no children, never crash");

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc    = parse_html_cstr("<table><tr></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);

        const tbox_layout_box *table_box = tbox_layout_build(&arena, root, &table, fonts, NULL, 800.0, 600.0);
        TBOX_TEST_ASSERT_MSG(table_box != NULL && table_box->first_child == NULL, "a table with only empty rows (no cells anywhere) must produce zero row boxes, never crash");

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Explicit sections, columns, caption and merged cells share one grid. */
    {
        const char *html = "<table id='t'><caption id='cap'>Totals</caption>"
            "<colgroup id='cg'><col id='c1'><col id='c2'></colgroup>"
            "<thead id='head'><tr><th>A</th><th>B</th><th>C</th></tr></thead>"
            "<tbody id='body'><tr><td id='tall' rowspan='2'>X</td>"
            "<td id='wide' colspan='2'>Y</td></tr>"
            "<tr><td id='lower'>Z</td><td>Q</td></tr></tbody>"
            "<tfoot id='foot'><tr><td colspan='3'>End</td></tr></tfoot></table>";
        tbox_html_document *doc = parse_html_cstr(html);
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "table { width: 300px; border-collapse: collapse; border: 1px solid black; caption-side: bottom; }"
            "#c1 { width: 60px; background-color: red; } #c2 { width: 120px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 800.0, 600.0);
        const tbox_layout_box *table_box = find_box_for_node(layout, find_html_id(root, "t"));
        const tbox_layout_box *caption = find_box_for_node(layout, find_html_id(root, "cap"));
        const tbox_layout_box *head = find_box_for_node(layout, find_html_id(root, "head"));
        const tbox_layout_box *body = find_box_for_node(layout, find_html_id(root, "body"));
        const tbox_layout_box *foot = find_box_for_node(layout, find_html_id(root, "foot"));
        const tbox_layout_box *col1 = find_box_for_node(layout, find_html_id(root, "c1"));
        const tbox_layout_box *col2 = find_box_for_node(layout, find_html_id(root, "c2"));
        const tbox_layout_box *tall = find_box_for_node(layout, find_html_id(root, "tall"));
        const tbox_layout_box *wide = find_box_for_node(layout, find_html_id(root, "wide"));
        const tbox_layout_box *lower = find_box_for_node(layout, find_html_id(root, "lower"));
        TBOX_TEST_ASSERT(table_box != NULL && caption != NULL && head != NULL && body != NULL && foot != NULL);
        TBOX_TEST_ASSERT(col1 != NULL && col2 != NULL && tall != NULL && wide != NULL && lower != NULL);
        if (table_box != NULL && caption != NULL && head != NULL && body != NULL && foot != NULL &&
            col1 != NULL && col2 != NULL && tall != NULL && wide != NULL && lower != NULL) {
            TBOX_TEST_ASSERT(head->border_box.y < body->border_box.y && body->border_box.y < foot->border_box.y);
            TBOX_TEST_ASSERT(caption->border_box.y >= foot->border_box.y + foot->border_box.height);
            TBOX_TEST_ASSERT(col2->border_box.width > col1->border_box.width);
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(wide->border_box.x, col2->border_box.x));
            TBOX_TEST_ASSERT(wide->border_box.width > col2->border_box.width);
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(lower->border_box.x, wide->border_box.x));
            TBOX_TEST_ASSERT(tall->border_box.height > wide->border_box.height);
            TBOX_TEST_ASSERT(table_box->table_edge_count > 0 && table_box->table_suppress_border);
            tbox_display_list display = tbox_render_build_display_list(&arena, layout);
            bool painted_column = false, painted_grid_edge = false;
            for (size_t op = 0; op < display.count; op++) {
                if (display.items[op].kind != TBOX_PAINT_FILL_RECT) continue;
                if (display.items[op].color.r == 255 && display.items[op].color.g == 0 &&
                    display.items[op].rect.x == col1->border_box.x &&
                    display.items[op].rect.width == col1->border_box.width) painted_column = true;
                if (display.items[op].color.r == 0 && display.items[op].color.g == 0 &&
                    display.items[op].color.b == 0 && display.items[op].rect.height == 1.0)
                    painted_grid_edge = true;
            }
            TBOX_TEST_ASSERT(painted_column && painted_grid_edge);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Separate border spacing and block content inside a cell. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table id='t'><tbody><tr><td id='blocks'><div>First</div><p>Second</p></td>"
            "<td id='short'>Short</td></tr></tbody></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "table { width: 300px; border-spacing: 6px 8px; }"
            "#short { vertical-align: bottom; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 800.0, 600.0);
        const tbox_layout_box *table_box = find_box_for_node(layout, find_html_id(root, "t"));
        const tbox_layout_box *blocks = find_box_for_node(layout, find_html_id(root, "blocks"));
        const tbox_layout_box *short_cell = find_box_for_node(layout, find_html_id(root, "short"));
        TBOX_TEST_ASSERT(table_box != NULL && blocks != NULL && short_cell != NULL);
        if (table_box != NULL && blocks != NULL && short_cell != NULL) {
            TBOX_TEST_ASSERT(blocks->first_child != NULL && blocks->first_child->next_sibling != NULL);
            TBOX_TEST_ASSERT(blocks->border_box.x >= table_box->content_box.x + 6.0);
            TBOX_TEST_ASSERT(short_cell->border_box.x >= blocks->border_box.x + blocks->border_box.width + 6.0);
            TBOX_TEST_ASSERT(short_cell->text_run_count > 0);
            if (short_cell->text_run_count > 0)
                TBOX_TEST_ASSERT(short_cell->text_runs[0].rect.y > blocks->border_box.y);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* A zero rowspan stops at its section; hidden rows and cells occupy no grid slots. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table><tbody><tr><td id='span' rowspan='0'>A</td><td>B</td></tr>"
            "<tr id='hidden'><td>Ignored</td></tr>"
            "<tr><td id='hidden-cell'>Skip</td><td id='next'>C</td></tr></tbody>"
            "<tfoot><tr><td id='foot'>D</td></tr></tfoot></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("#hidden, #hidden-cell { display: none; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 800.0, 600.0);
        const tbox_layout_box *span = find_box_for_node(layout, find_html_id(root, "span"));
        const tbox_layout_box *next = find_box_for_node(layout, find_html_id(root, "next"));
        const tbox_layout_box *foot = find_box_for_node(layout, find_html_id(root, "foot"));
        TBOX_TEST_ASSERT(find_box_for_node(layout, find_html_id(root, "hidden")) == NULL);
        TBOX_TEST_ASSERT(find_box_for_node(layout, find_html_id(root, "hidden-cell")) == NULL);
        TBOX_TEST_ASSERT(span != NULL && next != NULL && foot != NULL);
        if (span != NULL && next != NULL && foot != NULL) {
            TBOX_TEST_ASSERT(next->border_box.x > span->border_box.x);
            TBOX_TEST_ASSERT(span->border_box.y + span->border_box.height <= foot->border_box.y);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Direct rows remain direct DOM children and align with explicit groups. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table><colgroup id='group' span='2'></colgroup>"
            "<tr><td id='a'>A</td><td>A2</td></tr>"
            "<tbody><tr><td id='b'>B</td><td>B2</td></tr></tbody>"
            "<tr><td id='c'>C</td><td>C2</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *a_node = find_html_id(root, "a");
        TBOX_TEST_ASSERT(a_node != NULL && a_node->parent != NULL &&
            a_node->parent->parent != NULL && tbox_string_view_equal_cstr(a_node->parent->parent->element.tag_name, "table"));
        tbox_css_stylesheet *sheet = parse_css_cstr("#group { width: 80px; background-color: yellow; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 800.0, 600.0);
        const tbox_layout_box *a = find_box_for_node(layout, a_node);
        const tbox_layout_box *b = find_box_for_node(layout, find_html_id(root, "b"));
        const tbox_layout_box *c = find_box_for_node(layout, find_html_id(root, "c"));
        const tbox_layout_box *group = find_box_for_node(layout, find_html_id(root, "group"));
        TBOX_TEST_ASSERT(a != NULL && b != NULL && c != NULL && group != NULL);
        if (a != NULL && b != NULL && c != NULL && group != NULL) {
            TBOX_TEST_ASSERT(a->border_box.y < b->border_box.y && b->border_box.y < c->border_box.y);
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(a->border_box.x, b->border_box.x) &&
                tbox_test_double_approx_equal(b->border_box.x, c->border_box.x));
            TBOX_TEST_ASSERT(group->border_box.width >= 160.0);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* nowrap keeps a whole line even when its measured text exceeds width;
     * normal restores wrapping on a descendant. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div id='outer'><p id='nowrap'>one two three four</p>"
            "<p id='normal'>one two three four</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#outer { white-space: nowrap; } p { width: 55px; }"
            "#normal { white-space: normal; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 300.0, 200.0);
        const tbox_layout_box *nowrap_box = find_box_for_node(layout, find_html_id(root, "nowrap"));
        const tbox_layout_box *normal_box = find_box_for_node(layout, find_html_id(root, "normal"));
        TBOX_TEST_ASSERT(nowrap_box != NULL && normal_box != NULL);
        if (nowrap_box != NULL && normal_box != NULL) {
            TBOX_TEST_ASSERT(nowrap_box->text_run_count == 1);
            TBOX_TEST_ASSERT(normal_box->text_run_count > 1);
            TBOX_TEST_ASSERT(normal_box->content_box.height > nowrap_box->content_box.height);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Indentation consumes first-line width; word spacing moves both the
     * measured line break and the painted start of following words. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div><p id='plain'>one two three</p><p id='spaced'>one two three</p>"
            "<p id='indented'>one two three</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "p { width: 100px; white-space: nowrap; }"
            "#spaced { word-spacing: 10px; } #indented { text-indent: 20px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 300.0, 200.0);
        const tbox_layout_box *plain = find_box_for_node(layout, find_html_id(root, "plain"));
        const tbox_layout_box *spaced = find_box_for_node(layout, find_html_id(root, "spaced"));
        const tbox_layout_box *indented = find_box_for_node(layout, find_html_id(root, "indented"));
        TBOX_TEST_ASSERT(plain != NULL && spaced != NULL && indented != NULL);
        if (plain != NULL && spaced != NULL && indented != NULL) {
            TBOX_TEST_ASSERT(plain->text_run_count == 1);
            TBOX_TEST_ASSERT(spaced->text_run_count == 3);
            if (spaced->text_run_count == 3) {
                double gap = spaced->text_runs[1].rect.x -
                    (spaced->text_runs[0].rect.x + spaced->text_runs[0].rect.width);
                TBOX_TEST_ASSERT(gap > 10.0);
            }
            TBOX_TEST_ASSERT(indented->text_run_count == 1);
            if (indented->text_run_count == 1)
                TBOX_TEST_ASSERT(tbox_test_double_approx_equal(indented->text_runs[0].rect.x,
                    indented->content_box.x + 20.0));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc = parse_html_cstr(
            "<div><p id='plain'>one two</p><p id='indented'>one two</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "p { width: 65px; } #indented { text-indent: 20px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 300.0, 200.0);
        const tbox_layout_box *plain = find_box_for_node(layout, find_html_id(root, "plain"));
        const tbox_layout_box *indented = find_box_for_node(layout, find_html_id(root, "indented"));
        TBOX_TEST_ASSERT(plain != NULL && indented != NULL);
        if (plain != NULL && indented != NULL) {
            TBOX_TEST_ASSERT(plain->text_run_count == 1);
            TBOX_TEST_ASSERT(indented->text_run_count == 2);
            if (indented->text_run_count == 2)
                TBOX_TEST_ASSERT(indented->text_runs[1].rect.y > indented->text_runs[0].rect.y);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Width constraints apply before children are laid out. Percentages use
     * the containing block, and min-width wins if it exceeds max-width. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div id='host'><p id='max'>Text</p><p id='min'>Text</p>"
            "<p id='percent'>Text</p><p id='conflict'>Text</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#host { width: 200px; }"
            "#max { width: 180px; max-width: 100px; }"
            "#min { width: 20px; min-width: 80px; }"
            "#percent { max-width: 50%; }"
            "#conflict { width: 50px; min-width: 120px; max-width: 80px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 400.0, 300.0);
        const tbox_layout_box *max = find_box_for_node(layout, find_html_id(root, "max"));
        const tbox_layout_box *min = find_box_for_node(layout, find_html_id(root, "min"));
        const tbox_layout_box *percent = find_box_for_node(layout, find_html_id(root, "percent"));
        const tbox_layout_box *conflict = find_box_for_node(layout, find_html_id(root, "conflict"));
        TBOX_TEST_ASSERT(max != NULL && min != NULL && percent != NULL && conflict != NULL);
        if (max != NULL && min != NULL && percent != NULL && conflict != NULL) {
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(max->content_box.width, 100.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(min->content_box.width, 80.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(percent->content_box.width, 100.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(conflict->content_box.width, 120.0));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* A table's max-width limits the grid, while a column's min-width is
     * included before cell positions are assigned. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table id='grid'><colgroup><col id='first'><col></colgroup>"
            "<tr><td id='a'>A</td><td id='b'>B</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#grid { width: 200px; max-width: 120px; }"
            "#first { min-width: 80px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 400.0, 300.0);
        const tbox_layout_box *grid = find_box_for_node(layout, find_html_id(root, "grid"));
        const tbox_layout_box *a = find_box_for_node(layout, find_html_id(root, "a"));
        const tbox_layout_box *b = find_box_for_node(layout, find_html_id(root, "b"));
        TBOX_TEST_ASSERT(grid != NULL && a != NULL && b != NULL);
        if (grid != NULL && a != NULL && b != NULL) {
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(grid->content_box.width, 120.0));
            TBOX_TEST_ASSERT(a->border_box.width >= 80.0);
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(b->border_box.x,
                a->border_box.x + a->border_box.width));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Plain direct-row tables also grow to satisfy a cell's min-width. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<table id='grid'><tr><td id='a'>A</td><td id='b'>B</td></tr></table>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#grid { width: 60px; } #a { min-width: 80px; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 400.0, 300.0);
        const tbox_layout_box *grid = find_box_for_node(layout, find_html_id(root, "grid"));
        const tbox_layout_box *a = find_box_for_node(layout, find_html_id(root, "a"));
        const tbox_layout_box *b = find_box_for_node(layout, find_html_id(root, "b"));
        TBOX_TEST_ASSERT(grid != NULL && a != NULL && b != NULL);
        if (grid != NULL && a != NULL && b != NULL) {
            TBOX_TEST_ASSERT(a->content_box.width >= 80.0);
            TBOX_TEST_ASSERT(grid->content_box.width >=
                a->border_box.width + b->border_box.width);
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(b->border_box.x,
                a->border_box.x + a->border_box.width));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Border-box dimensions include padding and border; line-height and
     * letter-spacing affect both line geometry and measured run width. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div><p id='sized'>Hi</p><p id='line'>one<br>two</p>"
            "<p id='spaced'>WWW</p><p id='ellip'>ABCDEFGHIJKLM</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#sized { box-sizing: border-box; width: 120px; height: 40px;"
            " padding: 10px; border: 2px solid black; }"
            "#line { line-height: 30px; }"
            "#spaced { letter-spacing: 4px; }"
            "#ellip { width: 40px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 300.0, 300.0);
        const tbox_layout_box *sized = find_box_for_node(layout, find_html_id(root, "sized"));
        const tbox_layout_box *line = find_box_for_node(layout, find_html_id(root, "line"));
        const tbox_layout_box *spaced = find_box_for_node(layout, find_html_id(root, "spaced"));
        const tbox_layout_box *ellip = find_box_for_node(layout, find_html_id(root, "ellip"));
        TBOX_TEST_ASSERT(sized != NULL && line != NULL && spaced != NULL && ellip != NULL);
        if (sized != NULL && line != NULL && spaced != NULL && ellip != NULL) {
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(sized->border_box.width, 120.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(sized->border_box.height, 40.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(sized->content_box.width, 96.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(sized->content_box.height, 16.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(line->content_box.height, 60.0));
            TBOX_TEST_ASSERT(line->text_run_count == 2);
            if (line->text_run_count == 2)
                TBOX_TEST_ASSERT(tbox_test_double_approx_equal(
                    line->text_runs[1].rect.y - line->text_runs[0].rect.y, 30.0));
            TBOX_TEST_ASSERT(spaced->text_run_count == 1);
            if (spaced->text_run_count == 1)
                TBOX_TEST_ASSERT(tbox_test_double_approx_equal(spaced->text_runs[0].rect.width,
                    tbox_font_measure_text_spaced(spaced->text_runs[0].font,
                        spaced->text_runs[0].text, 4.0)));
            TBOX_TEST_ASSERT(ellip->text_run_count > 0);
            if (ellip->text_run_count > 0) {
                const tbox_layout_text_run *last = &ellip->text_runs[ellip->text_run_count - 1];
                TBOX_TEST_ASSERT(string_view_equal_cstr(last->text, "\xe2\x80\xa6") ||
                    string_view_equal_cstr(last->text, "..."));
                TBOX_TEST_ASSERT(last->rect.x + last->rect.width <=
                    ellip->content_box.x + ellip->content_box.width + 1e-6);
            }
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Height constraints change the visible box without losing the full
     * content height needed for overflow-y: auto. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div id='host'><div id='min'></div><div id='max'></div>"
            "<div id='percent'></div><div id='conflict'></div>"
            "<div id='border'></div><div id='scroll'>"
            "<p>one</p><p>two</p><p>three</p></div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "#host { height: 200px; }"
            "#min { height: 20px; min-height: 80px; }"
            "#max { height: 180px; max-height: 100px; }"
            "#percent { height: 150px; max-height: 50%; }"
            "#conflict { height: 50px; min-height: 120px; max-height: 80px; }"
            "#border { box-sizing: border-box; height: 20px; min-height: 60px;"
            " padding: 10px; border: 2px solid black; }"
            "#scroll { max-height: 40px; overflow-y: auto; }"
            "#scroll p { height: 25px; margin: 0; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 400.0, 300.0);
        const tbox_layout_box *min = find_box_for_node(layout, find_html_id(root, "min"));
        const tbox_layout_box *max = find_box_for_node(layout, find_html_id(root, "max"));
        const tbox_layout_box *percent = find_box_for_node(layout, find_html_id(root, "percent"));
        const tbox_layout_box *conflict = find_box_for_node(layout, find_html_id(root, "conflict"));
        const tbox_layout_box *border = find_box_for_node(layout, find_html_id(root, "border"));
        const tbox_layout_box *scroll = find_box_for_node(layout, find_html_id(root, "scroll"));
        TBOX_TEST_ASSERT(min && max && percent && conflict && border && scroll);
        if (min && max && percent && conflict && border && scroll) {
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(min->content_box.height, 80.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(max->content_box.height, 100.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(percent->content_box.height, 100.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(conflict->content_box.height, 120.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(border->border_box.height, 60.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(border->content_box.height, 36.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(scroll->content_box.height, 40.0));
            TBOX_TEST_ASSERT(tbox_test_double_approx_equal(scroll->scroll_content_height, 75.0));
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* break-word splits only overlong text, preserving every UTF-8 byte;
     * normal and nowrap retain their existing overflow behavior. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div><p id='normal'>supercalifragilisticexpialidocious</p>"
            "<p id='wrapped'>supercalifragilisticexpialidocious</p>"
            "<p id='nowrap'>supercalifragilisticexpialidocious</p>"
            "<p id='utf8'>éééééééééééééééé</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "p { width: 50px; } #wrapped, #utf8 { overflow-wrap: break-word; }"
            "#nowrap { overflow-wrap: break-word; white-space: nowrap; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = {sheet, TBOX_CSS_ORIGIN_AUTHOR};
        tbox_style_table resolved = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_layout_box *layout = tbox_layout_build(&arena, root, &resolved, fonts, NULL, 300.0, 300.0);
        const tbox_layout_box *normal = find_box_for_node(layout, find_html_id(root, "normal"));
        const tbox_layout_box *wrapped = find_box_for_node(layout, find_html_id(root, "wrapped"));
        const tbox_layout_box *nowrap = find_box_for_node(layout, find_html_id(root, "nowrap"));
        const tbox_layout_box *utf8 = find_box_for_node(layout, find_html_id(root, "utf8"));
        TBOX_TEST_ASSERT(normal && wrapped && nowrap && utf8);
        if (normal && wrapped && nowrap && utf8) {
            const char *long_word = "supercalifragilisticexpialidocious";
            TBOX_TEST_ASSERT(normal->text_run_count == 1 && normal->text_runs[0].rect.width > 50.0);
            TBOX_TEST_ASSERT(nowrap->text_run_count == 1 && nowrap->text_runs[0].rect.width > 50.0);
            TBOX_TEST_ASSERT(wrapped->text_run_count > 1);
            size_t offset = 0;
            for (size_t i = 0; i < wrapped->text_run_count; i++) {
                const tbox_layout_text_run *run = &wrapped->text_runs[i];
                TBOX_TEST_ASSERT(run->rect.width <= 50.0 + 1e-6);
                TBOX_TEST_ASSERT(offset + run->text.size <= strlen(long_word));
                if (offset + run->text.size <= strlen(long_word))
                    TBOX_TEST_ASSERT(memcmp(run->text.data, long_word + offset, run->text.size) == 0);
                offset += run->text.size;
            }
            TBOX_TEST_ASSERT(offset == strlen(long_word));
            TBOX_TEST_ASSERT(utf8->text_run_count > 1);
            size_t codepoints = 0;
            for (size_t i = 0; i < utf8->text_run_count; i++) {
                const tbox_layout_text_run *run = &utf8->text_runs[i];
                TBOX_TEST_ASSERT(run->text.size % 2 == 0);
                TBOX_TEST_ASSERT(run->rect.width <= 50.0 + 1e-6);
                for (size_t j = 0; j + 1 < run->text.size; j += 2) {
                    TBOX_TEST_ASSERT((unsigned char)run->text.data[j] == 0xc3);
                    TBOX_TEST_ASSERT((unsigned char)run->text.data[j + 1] == 0xa9);
                    codepoints++;
                }
            }
            TBOX_TEST_ASSERT(codepoints == 16);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);

    return failures;
}
