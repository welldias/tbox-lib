#include <tbox/style.h>

#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

static tbox_html_document *parse_html_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

static tbox_css_stylesheet *parse_css_cstr(const char *css) {
    return tbox_css_parse(css, strlen(css));
}

/* Resolves a single node's style against `sheet`, the same two-call
 * sequence tbox_style_resolve_tree performs per node. */
static tbox_style resolve_node(const tbox_css_stylesheet *sheet, const tbox_html_node *node, const tbox_style *parent_style) {
    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, node);
    tbox_style style                 = tbox_style_resolve(node, parent_style, &computed);
    tbox_css_computed_style_destroy(&computed);
    return style;
}

static bool rgba_eq(tbox_css_rgba a, tbox_css_rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int tbox_test_style_run(void) {
    int failures = 0;

    /* 1: an explicit property beats the initial value. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 10px; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PX);
        TBOX_TEST_ASSERT(style.width.value == 10.0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 2: color inherits from the parent when undeclared; background-color
     * never does, even when the parent has one set. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: red; background-color: blue; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(rgba_eq(parent_style.color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(parent_style.background_color, (tbox_css_rgba){ 0, 0, 255, 255 }));

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(rgba_eq(child_style.color, (tbox_css_rgba){ 255, 0, 0, 255 }), "color should inherit");
        TBOX_TEST_ASSERT_MSG(rgba_eq(child_style.background_color, (tbox_css_rgba){ 0, 0, 0, 0 }), "background-color must not inherit");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 3: margin shorthand, 2-value form -- top/bottom vs left/right. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { margin: 1px 2px; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.margin[0].kind == TBOX_STYLE_LENGTH_PX && style.margin[0].value == 1.0); /* top */
        TBOX_TEST_ASSERT(style.margin[1].kind == TBOX_STYLE_LENGTH_PX && style.margin[1].value == 2.0); /* right */
        TBOX_TEST_ASSERT(style.margin[2].kind == TBOX_STYLE_LENGTH_PX && style.margin[2].value == 1.0); /* bottom */
        TBOX_TEST_ASSERT(style.margin[3].kind == TBOX_STYLE_LENGTH_PX && style.margin[3].value == 2.0); /* left */

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 4: margin shorthand, 4-value form -- top right bottom left, in order. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { margin: 1px 2px 3px 4px; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.margin[0].value == 1.0); /* top */
        TBOX_TEST_ASSERT(style.margin[1].value == 2.0); /* right */
        TBOX_TEST_ASSERT(style.margin[2].value == 3.0); /* bottom */
        TBOX_TEST_ASSERT(style.margin[3].value == 4.0); /* left */

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 5: width/height resolve to the right length kind for auto/px/%. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: auto; height: 50%; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(style.height.kind == TBOX_STYLE_LENGTH_PERCENT);
        TBOX_TEST_ASSERT(style.height.value == 50.0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);

        tbox_html_document *doc2    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div2  = tbox_html_document_root(doc2)->first_child;
        tbox_css_stylesheet *sheet2 = parse_css_cstr("div { width: 10px; }");

        tbox_style style2 = resolve_node(sheet2, div2, NULL);
        TBOX_TEST_ASSERT(style2.width.kind == TBOX_STYLE_LENGTH_PX);
        TBOX_TEST_ASSERT(style2.width.value == 10.0);

        tbox_css_stylesheet_destroy(sheet2);
        tbox_html_document_destroy(doc2);
    }

    /* 6: no `display` declared -> the v0-specific initial value BLOCK (not
     * CSS2.1's spec-correct `inline`). Easy to get wrong, test explicitly. */
    {
        tbox_html_document *doc    = parse_html_cstr("<span>x</span>");
        const tbox_html_node *span = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_style style = resolve_node(sheet, span, NULL);
        TBOX_TEST_ASSERT_MSG(style.display == TBOX_STYLE_DISPLAY_BLOCK, "v0 initial value of display is BLOCK, not INLINE");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 7: display: none is respected when explicitly declared. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { display: none; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.display == TBOX_STYLE_DISPLAY_NONE);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 8: tbox_style_resolve_tree walks parent-before-child so inheritance
     * flows correctly across the whole tree, and tbox_style_table_find
     * looks entries up by node. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: red; }");

        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), sheet);

        const tbox_style *div_style = tbox_style_table_find(&table, div);
        const tbox_style *p_style   = tbox_style_table_find(&table, p);
        TBOX_TEST_ASSERT(div_style != NULL);
        TBOX_TEST_ASSERT(p_style != NULL);
        TBOX_TEST_ASSERT(rgba_eq(div_style->color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT_MSG(rgba_eq(p_style->color, (tbox_css_rgba){ 255, 0, 0, 255 }), "<p> should inherit red from <div>");

        /* 9: a node not in the table returns NULL. */
        tbox_html_document *other_doc = parse_html_cstr("<span>y</span>");
        const tbox_html_node *span    = tbox_html_document_root(other_doc)->first_child;
        TBOX_TEST_ASSERT(tbox_style_table_find(&table, span) == NULL);
        tbox_html_document_destroy(other_doc);

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
