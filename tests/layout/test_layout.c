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

    tbox_font_face *font = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT_MSG(font != NULL, "failed to load embedded font face");
    if (font == NULL) {
        free(font_data);
        return failures + 1;
    }

    /* 1: an explicit width/height in px is used as-is. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 200px; height: 100px; }");

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, font, 800.0, 600.0);
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

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, font, 800.0, 600.0);
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

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, font, 800.0, 600.0);
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

    /* 4: <h1>oi</h1> -> content_box.height == line_height, text == "oi",
     * font is the injected face. */
    {
        tbox_html_document *doc    = parse_html_cstr("<h1>oi</h1>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, font, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(box->content_box.height == tbox_font_face_line_height(font));
            TBOX_TEST_ASSERT_MSG(string_view_equal_cstr(box->text, "oi"), "h1's box->text should be \"oi\"");
            TBOX_TEST_ASSERT(box->font == font);
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

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, font, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT_MSG(box->content_box.width == 800.0, "text must never shrink the box's width");
        }

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 6: <span>texto</span> is outside the fixed text-tag list -> the box
     * exists (a container), but box->text is empty and box->font is NULL --
     * the loose TEXT child is never shown in v0. */
    {
        tbox_html_document *doc    = parse_html_cstr("<span>texto</span>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, root, &table, font, 800.0, 600.0);
        TBOX_TEST_ASSERT(box != NULL);
        if (box != NULL) {
            TBOX_TEST_ASSERT(tbox_string_view_empty(box->text));
            TBOX_TEST_ASSERT(box->font == NULL);
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
        tbox_html_document *doc    = parse_html_cstr("<div><div class=\"hidden\">a</div><div class=\"visible\">b</div></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *outer = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(".hidden { display: none; height: 500px; } .visible { height: 40px; }");

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *outer_box = tbox_layout_build(&arena, outer, &table, font, 800.0, 600.0);
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

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, root, sheet);

        tbox_layout_box *box = tbox_layout_build(&arena, div, &table, font, 800.0, 600.0);
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

    tbox_font_face_destroy(font);
    free(font_data);

    return failures;
}
