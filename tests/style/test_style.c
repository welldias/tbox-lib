#include <tbox/style.h>

#include <stdio.h>
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

        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);

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

    /* 10: font-size absent inherits the parent's already-resolved value;
     * with no parent at all, the root falls back to the 16px initial
     * value. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; }");

        tbox_style root_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(root_style.font_size == 20.0);

        tbox_style child_style = resolve_node(sheet, p, &root_style);
        TBOX_TEST_ASSERT_MSG(child_style.font_size == 20.0, "font-size should inherit when undeclared");

        tbox_html_document *doc2    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div2  = tbox_html_document_root(doc2)->first_child;
        tbox_css_stylesheet *sheet2 = parse_css_cstr("");
        tbox_style no_parent_style  = resolve_node(sheet2, div2, NULL);
        TBOX_TEST_ASSERT_MSG(no_parent_style.font_size == 16.0, "font-size with no parent falls back to the 16px initial value");

        tbox_css_stylesheet_destroy(sheet2);
        tbox_html_document_destroy(doc2);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 11: "2em" resolves to 2x the parent's resolved font_size; "150%"
     * behaves exactly like "1.5em"; "24px" is absolute, independent of the
     * parent's font-size. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 10px; } p { font-size: 2em; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.font_size == 10.0);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.font_size == 20.0, "2em should resolve to 2x the parent's font-size");

        tbox_css_stylesheet *sheet_pct = parse_css_cstr("div { font-size: 10px; } p { font-size: 150%; }");
        tbox_style parent_style_pct    = resolve_node(sheet_pct, div, NULL);
        tbox_style child_style_pct     = resolve_node(sheet_pct, p, &parent_style_pct);
        TBOX_TEST_ASSERT_MSG(child_style_pct.font_size == 15.0, "150%% should behave like 1.5em");

        tbox_css_stylesheet *sheet_px = parse_css_cstr("div { font-size: 10px; } p { font-size: 24px; }");
        tbox_style parent_style_px    = resolve_node(sheet_px, div, NULL);
        tbox_style child_style_px     = resolve_node(sheet_px, p, &parent_style_px);
        TBOX_TEST_ASSERT_MSG(child_style_px.font_size == 24.0, "24px is absolute, independent of the parent");

        tbox_css_stylesheet_destroy(sheet_px);
        tbox_css_stylesheet_destroy(sheet_pct);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 12: unparsable font-size values inherit
     * the parent's font-size, same as if the property were undeclared. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 12px; } p { font-size: gigantic; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        tbox_style child_style  = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.font_size == 12.0, "an unknown keyword should fall back to inheriting the parent's font-size");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 13: font-weight: bold sets font_weight_bold = true. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-weight: bold; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.font_weight_bold == true);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 14: absent font-weight inherits; normal resets inherited bold. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p><span>y</span></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-weight: bold; } p { font-weight: normal; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.font_weight_bold == true);

        tbox_style absent_child = resolve_node(sheet, span, &parent_style);
        TBOX_TEST_ASSERT_MSG(absent_child.font_weight_bold == true, "absent font-weight should inherit the parent's bold-ness");

        tbox_style normal_child = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(normal_child.font_weight_bold == false, "explicit normal must reset inherited bold");

        tbox_css_stylesheet *sheet2 = parse_css_cstr("");
        tbox_style root_style       = resolve_node(sheet2, div, NULL);
        TBOX_TEST_ASSERT_MSG(root_style.font_weight_bold == false, "font-weight with no parent falls back to false");

        tbox_css_stylesheet_destroy(sheet2);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 15: font-weight matching is the exact keyword "bold", case
     * insensitive -- "BOLD" still sets true. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-weight: BOLD; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.font_weight_bold == true, "font-weight matching should be case-insensitive");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 16: tbox_style_resolve_tree with 2 cascade sources -- one
     * USER_AGENT, one AUTHOR, both declaring the same property with
     * different values -- resolves to the author's value, proving origin
     * priority actually works through the new multi-source signature. */
    {
        tbox_html_document *doc           = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div         = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *ua_sheet     = parse_css_cstr("div { font-size: 10px; }");
        tbox_css_stylesheet *author_sheet = parse_css_cstr("div { font-size: 30px; }");

        tbox_css_cascade_source sources[2] = {
            { ua_sheet,     TBOX_CSS_ORIGIN_USER_AGENT },
            { author_sheet, TBOX_CSS_ORIGIN_AUTHOR     },
        };

        tbox_arena arena        = tbox_arena_create(0);
        tbox_style_table table  = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), sources, 2);
        const tbox_style *style = tbox_style_table_find(&table, div);
        TBOX_TEST_ASSERT(style != NULL);
        TBOX_TEST_ASSERT_MSG(style != NULL && style->font_size == 30.0, "author origin should win over user-agent origin through the multi-source signature");

        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(author_sheet);
        tbox_css_stylesheet_destroy(ua_sheet);
        tbox_html_document_destroy(doc);
    }

    /* 17: border shorthand with all 3 tokens, in the "canonical" order
     * width/style/color. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: 2px solid red; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_width == 2.0);
        TBOX_TEST_ASSERT(style.border_style == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(rgba_eq(style.border_color, (tbox_css_rgba){ 255, 0, 0, 255 }));

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 18: border shorthand tokens in a different order produce the exact
     * same result -- order-free parsing. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: solid red 2px; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.border_width == 2.0, "border shorthand should be order-free");
        TBOX_TEST_ASSERT(style.border_style == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(rgba_eq(style.border_color, (tbox_css_rgba){ 255, 0, 0, 255 }));

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 19: "border: none;" sets border_style to NONE; absence of `border`
     * altogether also produces the initial values (NONE / 0px / opaque
     * black). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: none; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_style == TBOX_STYLE_BORDER_STYLE_NONE);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);

        tbox_html_document *doc2    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div2  = tbox_html_document_root(doc2)->first_child;
        tbox_css_stylesheet *sheet2 = parse_css_cstr("");

        tbox_style style2 = resolve_node(sheet2, div2, NULL);
        TBOX_TEST_ASSERT_MSG(style2.border_style == TBOX_STYLE_BORDER_STYLE_NONE, "no border declared should be the initial value NONE");
        TBOX_TEST_ASSERT(style2.border_width == 0.0);
        TBOX_TEST_ASSERT_MSG(rgba_eq(style2.border_color, (tbox_css_rgba){ 0, 0, 0, 255 }), "border-color initial value is opaque black");

        tbox_css_stylesheet_destroy(sheet2);
        tbox_html_document_destroy(doc2);
    }

    /* 20: an unrecognized token inside `border` (e.g. "wavy") does not
     * knock down the other two valid tokens -- only border_style stays at
     * its initial value NONE, since no token matched a style keyword. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: 2px wavy red; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.border_width == 2.0, "an unrecognized token should not knock down other valid tokens");
        TBOX_TEST_ASSERT_MSG(rgba_eq(style.border_color, (tbox_css_rgba){ 255, 0, 0, 255 }), "an unrecognized token should not knock down other valid tokens");
        TBOX_TEST_ASSERT_MSG(style.border_style == TBOX_STYLE_BORDER_STYLE_NONE, "no token matched a style keyword, so border_style stays at its initial value");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 21: position: relative is recognized; absence, or an unrecognized
     * value (e.g. "sticky-typo"), fall back to the initial value STATIC. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { position: relative; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.position == TBOX_STYLE_POSITION_RELATIVE);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);

        tbox_html_document *doc2    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div2  = tbox_html_document_root(doc2)->first_child;
        tbox_css_stylesheet *sheet2 = parse_css_cstr("");

        tbox_style style2 = resolve_node(sheet2, div2, NULL);
        TBOX_TEST_ASSERT_MSG(style2.position == TBOX_STYLE_POSITION_STATIC, "no position declared should be the initial value STATIC");

        tbox_css_stylesheet_destroy(sheet2);
        tbox_html_document_destroy(doc2);

        tbox_html_document *doc3    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div3  = tbox_html_document_root(doc3)->first_child;
        tbox_css_stylesheet *sheet3 = parse_css_cstr("div { position: sticky-typo; }");

        tbox_style style3 = resolve_node(sheet3, div3, NULL);
        TBOX_TEST_ASSERT_MSG(style3.position == TBOX_STYLE_POSITION_STATIC, "an unrecognized position value should fall back to STATIC");

        tbox_css_stylesheet_destroy(sheet3);
        tbox_html_document_destroy(doc3);
    }

    /* 21b: position: absolute/fixed/sticky each resolve to their
     * own enum value (not folded into STATIC like an unrecognized keyword,
     * and not folded into each other). */
    {
        static const struct {
            const char *css;
            tbox_style_position expected;
        } cases[] = {
            { "div { position: absolute; }", TBOX_STYLE_POSITION_ABSOLUTE },
            { "div { position: fixed; }",    TBOX_STYLE_POSITION_FIXED    },
            { "div { position: sticky; }",   TBOX_STYLE_POSITION_STICKY   },
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
            const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);

            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.position == cases[i].expected);

            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 21c: none of absolute/fixed/sticky inherit from the parent;
     * a child with no `position` declared stays STATIC even though its
     * parent is `position: absolute` (same non-inheritance already proven
     * for `relative` in test 23 below). */
    {
        static const char *parent_positions[] = { "absolute", "fixed", "sticky" };

        for (size_t i = 0; i < sizeof(parent_positions) / sizeof(parent_positions[0]); i++) {
            tbox_html_document *doc   = parse_html_cstr("<div><p>x</p></div>");
            const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
            const tbox_html_node *p   = div->first_child;

            char css[64];
            snprintf(css, sizeof(css), "div { position: %s; }", parent_positions[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);

            tbox_style parent_style = resolve_node(sheet, div, NULL);
            tbox_style child_style  = resolve_node(sheet, p, &parent_style);
            TBOX_TEST_ASSERT_MSG(child_style.position == TBOX_STYLE_POSITION_STATIC, "position must not inherit from the parent");

            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 22: top/left declared resolve to the right offset[]; bottom/right
     * absent stay AUTO (the initial value). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { top: 10px; left: 5%; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.offset[0].kind == TBOX_STYLE_LENGTH_PX && style.offset[0].value == 10.0);                 /* top */
        TBOX_TEST_ASSERT(style.offset[3].kind == TBOX_STYLE_LENGTH_PERCENT && style.offset[3].value == 5.0);             /* left */
        TBOX_TEST_ASSERT_MSG(style.offset[1].kind == TBOX_STYLE_LENGTH_AUTO, "right should stay AUTO when undeclared");  /* right */
        TBOX_TEST_ASSERT_MSG(style.offset[2].kind == TBOX_STYLE_LENGTH_AUTO, "bottom should stay AUTO when undeclared"); /* bottom */

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 23: none of border/position/offset inherit from the parent -- a
     * child with no `top` declared stays AUTO even though its parent has
     * `top: 10px`. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { position: relative; top: 10px; border: 2px solid red; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.offset[0].kind == TBOX_STYLE_LENGTH_PX && parent_style.offset[0].value == 10.0);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.offset[0].kind == TBOX_STYLE_LENGTH_AUTO, "top must not inherit from the parent");
        TBOX_TEST_ASSERT_MSG(child_style.position == TBOX_STYLE_POSITION_STATIC, "position must not inherit from the parent");
        TBOX_TEST_ASSERT_MSG(child_style.border_style == TBOX_STYLE_BORDER_STYLE_NONE, "border must not inherit from the parent");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 24: "2em" in `width` resolves against the node's OWN
     * already-computed font-size (2 x 20px = 40px), not the 16px default
     * nor a parent's font-size (there is no parent here). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; width: 2em; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PX);
        TBOX_TEST_ASSERT_MSG(style.width.value == 40.0, "width: 2em should resolve against the node's own font-size (20px), not the 16px default");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 25: `em` in `margin` (any property other than font-size)
     * resolves against the node's OWN font-size, not the parent's -- unlike
     * `font-size: em`, which is the one exception that resolves against the
     * parent. Here <div> has font-size 10px and <p> has font-size 30px;
     * `p { margin: 1em; }` must use 30px, giving margin[0].value == 30.0,
     * not 10.0. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 10px; } p { font-size: 30px; margin: 1em; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.font_size == 10.0);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT(child_style.font_size == 30.0);
        TBOX_TEST_ASSERT_MSG(child_style.margin[0].value == 30.0, "margin: 1em should resolve against the child's own font-size (30px), not the parent's (10px)");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 26: regression -- `width: 50%` and `margin: 10px` still resolve
     * exactly as before adding `em` support (kind PERCENT/PX, values
     * unchanged). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 50%; margin: 10px; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PERCENT);
        TBOX_TEST_ASSERT(style.width.value == 50.0);
        TBOX_TEST_ASSERT(style.margin[0].kind == TBOX_STYLE_LENGTH_PX && style.margin[0].value == 10.0);
        TBOX_TEST_ASSERT(style.margin[1].kind == TBOX_STYLE_LENGTH_PX && style.margin[1].value == 10.0);
        TBOX_TEST_ASSERT(style.margin[2].kind == TBOX_STYLE_LENGTH_PX && style.margin[2].value == 10.0);
        TBOX_TEST_ASSERT(style.margin[3].kind == TBOX_STYLE_LENGTH_PX && style.margin[3].value == 10.0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 27: regression -- a value with an unrecognized unit (e.g. "2foo")
     * still fails to parse and falls back to the initial value AUTO, same
     * as before `em` support was added. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 2foo; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.width.kind == TBOX_STYLE_LENGTH_AUTO, "an unrecognized unit should still fail to parse and fall back to AUTO");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 28:  -- text-align: center resolves to TEXT_ALIGN_CENTER. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-align: center; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.text_align == TBOX_STYLE_TEXT_ALIGN_CENTER);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 29:  -- text-align matching is case-insensitive -- "RIGHT"
     * still resolves to TEXT_ALIGN_RIGHT. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-align: RIGHT; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.text_align == TBOX_STYLE_TEXT_ALIGN_RIGHT, "text-align matching should be case-insensitive");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 30:  -- text-align inherits from the parent when undeclared,
     * same inheritance mechanism as color/font-weight. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-align: center; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.text_align == TBOX_STYLE_TEXT_ALIGN_CENTER);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.text_align == TBOX_STYLE_TEXT_ALIGN_CENTER, "text-align should inherit when undeclared");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 31: text-align: justify, start and end are recognized; an unknown
     * keyword falls back to the inherited/initial value LEFT, since there
     * is no parent here. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        static const struct {
            const char *css;
            tbox_style_text_align expected;
        } cases[] = {
            { "div { text-align: justify; }",      TBOX_STYLE_TEXT_ALIGN_JUSTIFY },
            { "div { text-align: end; }",          TBOX_STYLE_TEXT_ALIGN_RIGHT   },
            { "div { text-align: start; }",        TBOX_STYLE_TEXT_ALIGN_LEFT    },
            { "div { text-align: match-parent; }", TBOX_STYLE_TEXT_ALIGN_LEFT    },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.text_align == cases[i].expected, cases[i].css);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* 32: regression -- a div with no text-align declared and no parent
     * resolves to the initial value LEFT. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.text_align == TBOX_STYLE_TEXT_ALIGN_LEFT, "no text-align declared, no parent, should be the initial value LEFT");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 33:  -- font-family: Verdana resolves style.font_family to
     * "Verdana". */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: Verdana; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(strcmp(style.font_family, "Verdana") == 0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 34:  -- font-family: "Courier New", monospace; resolves to
     * "Courier New" -- quotes stripped, and the comma INSIDE the quotes
     * must not be mistaken for the list separator. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: \"Courier New\", monospace; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "Courier New") == 0, "quoted font-family should have quotes stripped and not split on the comma inside the quotes");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 35:  -- font-family: Verdana, Arial, sans-serif; (unquoted,
     * multiple names) resolves only the FIRST name, "Verdana" -- no
     * fallback list is kept. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: Verdana, Arial, sans-serif; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "Verdana") == 0, "only the first name of a comma-separated font-family list should be used");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 36:  -- font-family inherits from the parent when undeclared,
     * same inheritance mechanism as color/text-align. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: Verdana; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(strcmp(parent_style.font_family, "Verdana") == 0);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(strcmp(child_style.font_family, "Verdana") == 0, "font-family should inherit when undeclared");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 37: regression -- a div with no font-family declared and no parent
     * resolves to the initial value "" (no override). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.font_family[0] == '\0', "no font-family declared, no parent, should be the initial value \"\"");
        TBOX_TEST_ASSERT(strcmp(style.font_family, "") == 0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 38:  -- font-style: italic sets font_italic = true. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-style: italic; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.font_italic == true);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 39: regression -- no font-style declared, no parent, resolves to the
     * initial value false. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.font_italic == false, "no font-style declared, no parent, should be the initial value false");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 40:  -- font_italic inherits from the parent when undeclared,
     * same inheritance mechanism as color/font-weight/text-align. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-style: italic; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.font_italic == true);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.font_italic == true, "font_italic should inherit when undeclared");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 41:  -- text-decoration: underline/line-through resolve to
     * their respective enum values; absent resolves to NONE. */
    {
        static const struct {
            const char *css;
            tbox_style_text_decoration expected;
        } cases[] = {
            { "div { text-decoration: underline; }",    TBOX_STYLE_TEXT_DECORATION_UNDERLINE    },
            { "div { text-decoration: line-through; }", TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH },
            { "",                                       TBOX_STYLE_TEXT_DECORATION_NONE         },
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
            const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);

            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.text_decoration == cases[i].expected);

            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 42:  -- text-decoration does NOT inherit -- a child with no
     * text-decoration declared stays NONE even though its parent has
     * `text-decoration: underline` (unlike font_italic in test 40 above). */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-decoration: underline; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.text_decoration == TBOX_STYLE_TEXT_DECORATION_NONE, "text-decoration must not inherit from the parent");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 43:  -- vertical-align: sub/super resolve to their respective
     * enum values; absent resolves to BASELINE. */
    {
        static const struct {
            const char *css;
            tbox_style_vertical_align expected;
        } cases[] = {
            { "div { vertical-align: sub; }",   TBOX_STYLE_VERTICAL_ALIGN_SUB      },
            { "div { vertical-align: super; }", TBOX_STYLE_VERTICAL_ALIGN_SUPER    },
            { "",                               TBOX_STYLE_VERTICAL_ALIGN_BASELINE },
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
            const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);

            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.vertical_align == cases[i].expected);

            tbox_css_stylesheet_destroy(sheet);
            tbox_html_document_destroy(doc);
        }
    }

    /* 44:  -- vertical-align does NOT inherit -- a child with no
     * vertical-align declared stays BASELINE even though its parent has
     * `vertical-align: sub`. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { vertical-align: sub; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_SUB);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BASELINE, "vertical-align must not inherit from the parent");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 45: NOVO (visual fidelity) -- border-radius: a recognized px value is
     * used as-is; no declaration, or an unrecognized one (percentage,
     * keyword), falls back to the initial value 0.0. Not inheritable. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border-radius: 8px; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.border_radius == 8.0);

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.border_radius == 0.0, "border-radius must not inherit from the parent");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border-radius: 50%; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.border_radius == 0.0, "a percentage border-radius (out of scope) must fall back to 0.0, never crash");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 46: NOVO (visual fidelity) -- box-shadow: offset-x/offset-y/color
     * recognized, blur-radius optional (defaults to 0.0 when only 2 length
     * tokens are present). Not inheritable. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { box-shadow: 2px 4px 6px red; }");

        tbox_style parent_style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent_style.box_shadow_offset_x == 2.0);
        TBOX_TEST_ASSERT(parent_style.box_shadow_offset_y == 4.0);
        TBOX_TEST_ASSERT(parent_style.box_shadow_blur == 6.0);
        TBOX_TEST_ASSERT(rgba_eq(parent_style.box_shadow_color, (tbox_css_rgba){ 255, 0, 0, 255 }));

        tbox_style child_style = resolve_node(sheet, p, &parent_style);
        TBOX_TEST_ASSERT_MSG(child_style.box_shadow_color.a == 0, "box-shadow must not inherit from the parent");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { box-shadow: 1px 2px rgba(0, 0, 0, 0.5); }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.box_shadow_blur == 0.0, "a missing blur-radius token must default to 0.0");
        TBOX_TEST_ASSERT(style.box_shadow_offset_x == 1.0 && style.box_shadow_offset_y == 2.0);
        TBOX_TEST_ASSERT(style.box_shadow_color.a == 128); /* 0.5 * 255, rounded, same convention as every other alpha-parsing test in this file */

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        /* No declaration at all -- initial value: transparent (no shadow). */
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.box_shadow_color.a == 0);
        TBOX_TEST_ASSERT(style.box_shadow_offset_x == 0.0 && style.box_shadow_offset_y == 0.0 && style.box_shadow_blur == 0.0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        /* Comma-separated shadows are all kept, in order; the single
         * box_shadow_* fields mirror the first (topmost) one. */
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { box-shadow: 1px 1px red, 2px 2px blue; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.box_shadow_count == 2);
        TBOX_TEST_ASSERT(rgba_eq(style.box_shadow_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(style.box_shadows[1].color, (tbox_css_rgba){ 0, 0, 255, 255 }) && style.box_shadows[1].offset_x == 2.0);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc       = parse_html_cstr("<table><caption>Title</caption></table>");
        const tbox_html_node *table   = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *caption = table->first_child;
        tbox_css_stylesheet *sheet    = parse_css_cstr("table { border-collapse: collapse; border-spacing: 6px 8px; caption-side: bottom; }"
                                                       "caption { vertical-align: middle; border-spacing: 0; }");
        tbox_style table_style        = resolve_node(sheet, table, NULL);
        tbox_style caption_style      = resolve_node(sheet, caption, &table_style);
        TBOX_TEST_ASSERT(table_style.border_collapse);
        TBOX_TEST_ASSERT(table_style.border_spacing_x == 6.0 && table_style.border_spacing_y == 8.0);
        TBOX_TEST_ASSERT(caption_style.caption_side == TBOX_STYLE_CAPTION_BOTTOM);
        TBOX_TEST_ASSERT(caption_style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE);
        TBOX_TEST_ASSERT(caption_style.border_spacing_x == 0.0 && caption_style.border_spacing_y == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Font resets and nowrap are inherited until explicitly replaced. */
    {
        tbox_html_document *doc       = parse_html_cstr("<div><p>child</p><span>inherit</span></div>");
        const tbox_html_node *parent  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *child   = parent->first_child;
        const tbox_html_node *sibling = child->next_sibling;
        tbox_css_stylesheet *sheet    = parse_css_cstr("div { font-weight: 700; font-style: italic; white-space: nowrap; }"
                                                       "p { font-weight: 400; font-style: normal; white-space: normal; }");
        tbox_style outer              = resolve_node(sheet, parent, NULL);
        tbox_style reset              = resolve_node(sheet, child, &outer);
        tbox_style inherited          = resolve_node(sheet, sibling, &outer);
        TBOX_TEST_ASSERT(outer.font_weight_bold && outer.font_italic && outer.white_space == TBOX_STYLE_WHITE_SPACE_NOWRAP);
        TBOX_TEST_ASSERT(!reset.font_weight_bold && !reset.font_italic && reset.white_space == TBOX_STYLE_WHITE_SPACE_NORMAL);
        TBOX_TEST_ASSERT(inherited.font_weight_bold && inherited.font_italic && inherited.white_space == TBOX_STYLE_WHITE_SPACE_NOWRAP);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Decoration color/thickness are independent of text color; hidden
     * overflow clips without requesting a scroll state. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>text</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: blue; text-decoration: underline; text-decoration-color: red;"
                                                    " text-decoration-thickness: 3px; overflow-y: hidden; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN);
        TBOX_TEST_ASSERT(rgba_eq(style.color, (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(style.text_decoration_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(style.text_decoration_thickness == 3.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Uniform border longhands obey source order and importance relative to
     * the existing shorthand. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div class='x'>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[]         = {
            "div { border: 2px solid red; border-color: blue; }",
            "div { border-color: blue; border: 2px solid red; }",
            "div { border-color: blue !important; border: 2px solid red; }",
            "div { border-width: 4px; border-style: solid; border-color: green; }",
            ".x { border-color: blue; } div { border: 2px solid red; }",
        };
        const tbox_css_rgba colors[] = {
            { 0,   0,   255, 255 },
            { 255, 0,   0,   255 },
            { 0,   0,   255, 255 },
            { 0,   128, 0,   255 },
            { 0,   0,   255, 255 },
        };
        const double widths[] = { 2.0, 2.0, 2.0, 4.0, 2.0 };
        for (size_t i = 0; i < 5; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.border_style == TBOX_STYLE_BORDER_STYLE_SOLID);
            TBOX_TEST_ASSERT(style.border_width == widths[i]);
            TBOX_TEST_ASSERT(rgba_eq(style.border_color, colors[i]));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Box-side longhands obey shorthand order, importance, and validity. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[]         = { "div { margin: 1px; margin-left: 7px; padding: 2px; padding-top: 4px; }", "div { margin-left: 7px; margin: 1px; padding-top: 4px; padding: 2px; }", "div { margin-left: 7px !important; margin: 1px; padding-top: 4px !important; padding: 2px; }", "div { margin: 0; padding: -1px; padding-left: 5px; }" };
        const double lefts[]      = { 7.0, 1.0, 7.0, 0.0 };
        const double tops[]       = { 4.0, 2.0, 4.0, 0.0 };
        for (size_t i = 0; i < 4; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.margin[3].value == lefts[i]);
            TBOX_TEST_ASSERT(style.padding[0].value == tops[i]);
            if (i == 3)
                TBOX_TEST_ASSERT(style.padding[3].value == 5.0);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Background shorthand and longhand compete by normal cascade priority. */
    {
        tbox_html_document *doc        = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div      = tbox_html_document_root(doc)->first_child;
        const char *css[]              = { "div { background: red; background-color: blue; }", "div { background-color: blue; background: red; }", "div { background: red; background-color: blue !important; }", "div { background-color: blue; background: url(missing.png); }" };
        const tbox_css_rgba expected[] = {
            { 0,   0, 255, 255 },
            { 255, 0, 0,   255 },
            { 0,   0, 255, 255 },
            { 0,   0, 0,   0   } /* a later image-only shorthand resets the color */
        };
        for (size_t i = 0; i < 4; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(rgba_eq(style.background_color, expected[i]));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Word spacing and indentation inherit; outline and decoration do not. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-indent: 10%; word-spacing: 2px; outline: 3px solid red; text-decoration: overline; }"
                                                    "p { word-spacing: normal; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.text_indent.kind == TBOX_STYLE_LENGTH_PERCENT && parent.text_indent.value == 10.0);
        TBOX_TEST_ASSERT(child.text_indent.kind == TBOX_STYLE_LENGTH_PERCENT && child.text_indent.value == 10.0);
        TBOX_TEST_ASSERT(parent.word_spacing == 2.0 && child.word_spacing == 0.0);
        TBOX_TEST_ASSERT(parent.outline_width == 3.0 && parent.outline_style == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(rgba_eq(parent.outline_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(child.outline_style == TBOX_STYLE_BORDER_STYLE_NONE);
        TBOX_TEST_ASSERT(parent.text_decoration == TBOX_STYLE_TEXT_DECORATION_OVERLINE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Edge colors use the element's computed color; width keywords work in
     * both shorthand and longhand declarations. */
    {
        tbox_html_document *doc        = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div      = tbox_html_document_root(doc)->first_child;
        const char *css[]              = { "div { color: #123456; border: thick solid currentColor;"
                                           " outline: thin solid currentColor; outline-offset: -2px; }",
            "div { color: blue; border: 2px solid red; border-width: medium;"
            " border-color: currentColor; outline: 2px solid red;"
            " outline-color: currentColor; outline-width: thick; }" };
        const double border_widths[]   = { 5.0, 3.0 };
        const double outline_widths[]  = { 1.0, 5.0 };
        const double outline_offsets[] = { -2.0, 0.0 };
        for (size_t i = 0; i < 2; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.border_width == border_widths[i]);
            TBOX_TEST_ASSERT(style.outline_width == outline_widths[i]);
            TBOX_TEST_ASSERT(style.outline_offset == outline_offsets[i]);
            TBOX_TEST_ASSERT(rgba_eq(style.border_color, style.color));
            TBOX_TEST_ASSERT(rgba_eq(style.outline_color, style.color));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Width limits accept nonnegative lengths; max-width:none removes its
     * limit and an invalid negative value leaves the initial state. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; min-width: 2em; max-width: 60%; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.min_width.kind == TBOX_STYLE_LENGTH_PX && style.min_width.value == 40.0);
        TBOX_TEST_ASSERT(style.max_width.kind == TBOX_STYLE_LENGTH_PERCENT && style.max_width.value == 60.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { min-width: -5px; max-width: none; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.min_width.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(style.max_width.kind == TBOX_STYLE_LENGTH_AUTO);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* New text and sizing properties retain the appropriate inheritance. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; line-height: 1.5; letter-spacing: 2px;"
                                                    " visibility: hidden; box-sizing: border-box; text-overflow: ellipsis; overflow: hidden; }"
                                                    "p { font-size: 10px; visibility: visible; letter-spacing: normal; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER && parent.line_height_value == 1.5);
        TBOX_TEST_ASSERT(child.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER && child.line_height_value == 1.5 && child.font_size == 10.0);
        TBOX_TEST_ASSERT(parent.letter_spacing == 2.0 && child.letter_spacing == 0.0);
        TBOX_TEST_ASSERT(parent.visibility_hidden && !child.visibility_hidden);
        TBOX_TEST_ASSERT(parent.box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX && child.box_sizing == TBOX_STYLE_BOX_SIZING_CONTENT_BOX);
        TBOX_TEST_ASSERT(parent.text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS && child.text_overflow == TBOX_STYLE_TEXT_OVERFLOW_CLIP);
        TBOX_TEST_ASSERT(parent.overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Height limits resolve like width limits, and the new font/color
     * values preserve inheritance and cascade precedence. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; min-height: 2em; max-height: 60%;"
                                                    " color: #123456; background: currentColor; font-weight: 600; font-style: oblique; }"
                                                    "p { background: red; background-color: currentColor; font-weight: 500; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.min_height.kind == TBOX_STYLE_LENGTH_PX && parent.min_height.value == 40.0);
        TBOX_TEST_ASSERT(parent.max_height.kind == TBOX_STYLE_LENGTH_PERCENT && parent.max_height.value == 60.0);
        TBOX_TEST_ASSERT(rgba_eq(parent.background_color, parent.color));
        TBOX_TEST_ASSERT(rgba_eq(child.background_color, parent.color));
        TBOX_TEST_ASSERT(parent.font_weight_bold && !child.font_weight_bold);
        TBOX_TEST_ASSERT(parent.font_italic && child.font_italic);
        tbox_css_stylesheet_destroy(sheet);

        const char *weights[] = { "100", "200", "300", "400", "500", "600", "700", "800", "900" };
        for (size_t i = 0; i < sizeof(weights) / sizeof(weights[0]); i++) {
            char css[64];
            snprintf(css, sizeof(css), "div { font-weight: %s; }", weights[i]);
            sheet            = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.font_weight_bold == (i >= 5));
            tbox_css_stylesheet_destroy(sheet);
        }
        sheet  = parse_css_cstr("div { min-height: -1px; max-height: none; font-weight: 650; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.min_height.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(parent.max_height.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(parent.font_weight == 650 && parent.font_weight_bold);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Font-size keyword values and relative sizes resolve before em lengths. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p   = div->first_child;
        const char *keywords[]    = { "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large" };
        const double expected[]   = { 9.0, 10.0, 13.0, 16.0, 18.0, 24.0, 32.0 };
        for (size_t i = 0; i < 7; i++) {
            char css[80];
            snprintf(css, sizeof(css), "div { font-size: %s; padding: 1em; }", keywords[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style           = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.font_size == expected[i] && style.padding[0].value == expected[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size: 20px; } p { font-size: larger; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_size == 24.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { font-size: 24px; } p { font-size: smaller; }");
        parent = resolve_node(sheet, div, NULL);
        child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_size == 20.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Clockwise radius expansion and a later longhand override. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div   = tbox_html_document_root(doc)->first_child;
        const char *css[]           = { "div { border-radius: 2px; }", "div { border-radius: 2px 4px; }", "div { border-radius: 2px 4px 6px; }", "div { border-radius: 2px 4px 6px 8px; }", "div { font-size: 20px; border-radius: 1em 2px; border-top-right-radius: 5px; }", "div { border-top-left-radius: 7px; border-radius: invalid; }" };
        const double expected[6][4] = {
            { 2,  2, 2,  2 },
            { 2,  4, 2,  4 },
            { 2,  4, 6,  4 },
            { 2,  4, 6,  8 },
            { 20, 5, 20, 2 },
            { 7,  0, 0,  0 }
        };
        for (size_t i = 0; i < 6; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            for (size_t j = 0; j < 4; j++)
                TBOX_TEST_ASSERT(style.border_radius_corners[j] == expected[i][j]);
            TBOX_TEST_ASSERT(style.border_radius == (i == 0 ? 2.0 : 0.0));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Text wrapping and pointer targeting inherit, with explicit resets. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { overflow-wrap: break-word; pointer-events: none; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.overflow_wrap_break_word && child.overflow_wrap_break_word);
        TBOX_TEST_ASSERT(parent.pointer_events_none && child.pointer_events_none);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { overflow-wrap: normal; pointer-events: auto; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(!child.overflow_wrap_break_word && !child.pointer_events_none);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* inset expands to the four offsets; physical longhands follow cascade
     * order against it. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { inset: 1px 2px 3px; left: 9px; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.offset[0].kind == TBOX_STYLE_LENGTH_PX && style.offset[0].value == 1.0);
        TBOX_TEST_ASSERT(style.offset[1].value == 2.0 && style.offset[2].value == 3.0);
        TBOX_TEST_ASSERT(style.offset[3].value == 9.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { left: 9px; inset: auto; }");
        style = resolve_node(sheet, div, NULL);
        for (int i = 0; i < 4; i++)
            TBOX_TEST_ASSERT(style.offset[i].kind == TBOX_STYLE_LENGTH_AUTO);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Logical margin/padding/inset map onto physical sides, left-to-right. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { margin: 1px; margin-inline: 4px 6px; margin-block-end: 8px;"
                                                    " padding-block: 2px; padding-inline-start: 3px; padding: 5px; padding-inline-end: 7px;"
                                                    " inset-block-start: 10px; inset-inline: 1em; font-size: 10px; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.margin[0].value == 1.0 && style.margin[1].value == 6.0);
        TBOX_TEST_ASSERT(style.margin[2].value == 8.0 && style.margin[3].value == 4.0);
        TBOX_TEST_ASSERT(style.padding[0].value == 5.0 && style.padding[1].value == 7.0);
        TBOX_TEST_ASSERT(style.padding[2].value == 5.0 && style.padding[3].value == 5.0);
        TBOX_TEST_ASSERT(style.offset[0].value == 10.0 && style.offset[2].kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(style.offset[1].value == 10.0 && style.offset[3].value == 10.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { padding-inline: -1px; margin-block: 1px 2px 3px; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.padding[1].value == 0.0 && style.padding[3].value == 0.0);
        TBOX_TEST_ASSERT(style.margin[0].value == 0.0 && style.margin[2].value == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* bolder/lighter, overflow clip/scroll and border-style hidden. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-weight: bolder; overflow: clip; border: 2px hidden red; }"
                                                    " p { font-weight: lighter; overflow-y: scroll; border: 1px solid; border-style: hidden;"
                                                    " outline: 1px hidden red; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.font_weight_bold && !child.font_weight_bold);
        TBOX_TEST_ASSERT(parent.overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN);
        TBOX_TEST_ASSERT(child.overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO);
        TBOX_TEST_ASSERT(parent.border_style == TBOX_STYLE_BORDER_STYLE_NONE && parent.border_width == 2.0);
        TBOX_TEST_ASSERT(child.border_style == TBOX_STYLE_BORDER_STYLE_NONE);
        TBOX_TEST_ASSERT(child.outline_style == TBOX_STYLE_BORDER_STYLE_NONE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* text-decoration shorthand with color/thickness, its longhands and
     * text-underline-offset. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: blue; text-decoration: underline red 3px; text-underline-offset: 4px; }"
                                                    " p { text-decoration-color: currentColor; text-decoration: overline wavy 0.5em;"
                                                    " text-decoration-line: line-through; font-size: 10px; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE);
        TBOX_TEST_ASSERT(rgba_eq(parent.text_decoration_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(parent.text_decoration_thickness == 3.0);
        TBOX_TEST_ASSERT(parent.text_underline_offset.kind == TBOX_STYLE_LENGTH_PX && parent.text_underline_offset.value == 4.0);
        TBOX_TEST_ASSERT(child.text_decoration == TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH);
        TBOX_TEST_ASSERT(rgba_eq(child.text_decoration_color, (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(child.text_decoration_thickness == 5.0);
        TBOX_TEST_ASSERT(child.text_underline_offset.value == 4.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { text-decoration: none; text-underline-offset: 50%; font-size: 10px; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.text_decoration == TBOX_STYLE_TEXT_DECORATION_NONE);
        TBOX_TEST_ASSERT(parent.text_underline_offset.kind == TBOX_STYLE_LENGTH_PX && parent.text_underline_offset.value == 5.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* vertical-align text-top/text-bottom and lengths. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[]         = { "div { vertical-align: text-top; }", "div { vertical-align: TEXT-BOTTOM; }", "div { vertical-align: -3px; }", "div { vertical-align: 0.5em; font-size: 10px; }", "div { vertical-align: 50%; }", "div { vertical-align: 3; }" };
        for (size_t i = 0; i < sizeof(css) / sizeof(css[0]); i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style           = resolve_node(sheet, div, NULL);
            if (i == 0)
                TBOX_TEST_ASSERT(style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_TEXT_TOP);
            if (i == 1)
                TBOX_TEST_ASSERT(style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM);
            if (i == 2)
                TBOX_TEST_ASSERT(style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_LENGTH && style.vertical_align_length.value == -3.0);
            if (i == 3)
                TBOX_TEST_ASSERT(style.vertical_align_length.kind == TBOX_STYLE_LENGTH_PX && style.vertical_align_length.value == 5.0);
            if (i == 4)
                TBOX_TEST_ASSERT(style.vertical_align_length.kind == TBOX_STYLE_LENGTH_PERCENT && style.vertical_align_length.value == 50.0);
            if (i == 5)
                TBOX_TEST_ASSERT(style.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BASELINE);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* accent-color and caret-color inherit; auto is stored as alpha 0. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { accent-color: red; caret-color: blue; } p { caret-color: auto; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(rgba_eq(parent.accent_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(parent.caret_color, (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(child.accent_color, parent.accent_color));
        TBOX_TEST_ASSERT(child.caret_color.a == 0);
        tbox_style root = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(root.accent_color.a == 0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* list-style-type inherits; the list-style shorthand resets it to disc
     * unless it names a type, and follows cascade order with the longhand. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ol><li>x</li></ol>");
        const tbox_html_node *ol   = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *li   = ol->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("ol { list-style-type: upper-latin; } li { list-style: inside; }");
        tbox_style parent          = resolve_node(sheet, ol, NULL);
        tbox_style child           = resolve_node(sheet, li, &parent);
        TBOX_TEST_ASSERT(parent.list_style_type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA);
        TBOX_TEST_ASSERT(child.list_style_type == TBOX_STYLE_LIST_STYLE_DISC);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("li { list-style: none outside; list-style-type: circle; }");
        child = resolve_node(sheet, li, &parent);
        TBOX_TEST_ASSERT(child.list_style_type == TBOX_STYLE_LIST_STYLE_CIRCLE);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("li { list-style-type: bogus; }");
        child = resolve_node(sheet, li, &parent);
        TBOX_TEST_ASSERT(child.list_style_type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA);
        tbox_style root = resolve_node(sheet, ol, NULL);
        TBOX_TEST_ASSERT(root.list_style_type == TBOX_STYLE_LIST_STYLE_AUTO);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* text-transform and word-break inherit and can be reset. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-transform: UPPERCASE; word-break: break-all; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.text_transform == TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE && child.word_break_all);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { text-transform: capitalize; word-break: keep-all; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.text_transform == TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE && !child.word_break_all);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* box-shadow spread and default currentColor; em lengths; too many
     * lengths or a negative blur are rejected. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: red; font-size: 10px; box-shadow: 1px 2px 0.3em -4px; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.box_shadow_offset_x == 1.0 && style.box_shadow_offset_y == 2.0);
        TBOX_TEST_ASSERT(style.box_shadow_blur == 3.0 && style.box_shadow_spread == -4.0);
        TBOX_TEST_ASSERT(rgba_eq(style.box_shadow_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        tbox_css_stylesheet_destroy(sheet);
        const char *invalid[] = { "div { box-shadow: 1px 2px 3px 4px 5px red; }", "div { box-shadow: 1px 2px -3px red; }", "div { box-shadow: 1px red; }", "div { box-shadow: none; }" };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            sheet = parse_css_cstr(invalid[i]);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.box_shadow_color.a == 0, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* text-shadow: one shadow with up to three lengths, inherited, reset by
     * none, and rejected with a spread length. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-shadow: rgba(0, 0, 255, 0.5) 1px 2px 3px; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.text_shadow_offset_x == 1.0 && parent.text_shadow_offset_y == 2.0);
        TBOX_TEST_ASSERT(parent.text_shadow_blur == 3.0 && parent.text_shadow_color.b == 255 && parent.text_shadow_color.a == 128);
        TBOX_TEST_ASSERT(rgba_eq(child.text_shadow_color, parent.text_shadow_color) && child.text_shadow_blur == 3.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { text-shadow: none; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.text_shadow_color.a == 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { text-shadow: 1px 1px 1px 1px red; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(rgba_eq(child.text_shadow_color, parent.text_shadow_color));
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* white-space modes inherit; AUTO is only the no-parent initial value. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p   = div->first_child;
        static const struct {
            const char *css;
            tbox_style_white_space expected;
        } cases[] = {
            { "div { white-space: pre; }",          TBOX_STYLE_WHITE_SPACE_PRE      },
            { "div { white-space: Pre-Wrap; }",     TBOX_STYLE_WHITE_SPACE_PRE_WRAP },
            { "div { white-space: pre-line; }",     TBOX_STYLE_WHITE_SPACE_PRE_LINE },
            { "div { white-space: break-spaces; }", TBOX_STYLE_WHITE_SPACE_BREAK_SPACES },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);
            tbox_style parent          = resolve_node(sheet, div, NULL);
            tbox_style child           = resolve_node(sheet, p, &parent);
            TBOX_TEST_ASSERT_MSG(parent.white_space == cases[i].expected && child.white_space == cases[i].expected, cases[i].css);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* opacity: numbers and percentages, clamped, not inherited. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p   = div->first_child;
        static const struct {
            const char *css;
            double expected;
        } cases[] = {
            { "div { opacity: 0.25; }", 0.25 },
            { "div { opacity: 40%; }",  0.4  },
            { "div { opacity: 3; }",    1.0  },
            { "div { opacity: -1; }",   0.0  },
            { "div { opacity: half; }", 1.0  },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[i].css);
            tbox_style parent          = resolve_node(sheet, div, NULL);
            tbox_style child           = resolve_node(sheet, p, &parent);
            TBOX_TEST_ASSERT_MSG(parent.opacity > cases[i].expected - 1e-9 && parent.opacity < cases[i].expected + 1e-9, cases[i].css);
            TBOX_TEST_ASSERT(child.opacity == 1.0);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Per-side borders: shorthands, 1-4 value longhands and per-side
     * longhands resolve per side in cascade order. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: blue; border: 1px solid red; border-left: 4px solid; border-bottom-width: 3px;"
                                                    " border-top-style: none; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_per_side);
        TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, 0) == 0.0);
        TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, 1) == 1.0);
        TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, 2) == 3.0);
        TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, 3) == 4.0);
        TBOX_TEST_ASSERT(rgba_eq(tbox_style_border_side_color(&style, 1), (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_style_border_side_color(&style, 3), (tbox_css_rgba){ 0, 0, 255, 255 }));
        tbox_css_stylesheet_destroy(sheet);

        sheet                  = parse_css_cstr("div { border-style: solid; border-width: 1px 2px 3px;"
                                                " border-color: red rgba(0, 128, 0, 0.5); }");
        style                  = resolve_node(sheet, div, NULL);
        const double widths[4] = { 1.0, 2.0, 3.0, 2.0 };
        for (size_t i = 0; i < 4; i++)
            TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, i) == widths[i]);
        TBOX_TEST_ASSERT(rgba_eq(tbox_style_border_side_color(&style, 2), (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_style_border_side_color(&style, 3), (tbox_css_rgba){ 0, 128, 0, 128 }));
        tbox_css_stylesheet_destroy(sheet);

        /* A later shorthand overrides earlier per-side longhands; uniform
         * results keep border_per_side false and fill the scalars. */
        sheet = parse_css_cstr("div { border-top-width: 9px; border-top: 2px solid green; border: 2px solid green; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(!style.border_per_side && style.border_width == 2.0 && style.border_style == TBOX_STYLE_BORDER_STYLE_SOLID);
        tbox_css_stylesheet_destroy(sheet);

        /* An invalid token invalidates a 1-4 value longhand as a whole. */
        sheet = parse_css_cstr("div { border: 1px solid; border-width: 2px bogus; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(!style.border_per_side && style.border_width == 1.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* dashed/dotted/double borders and outlines; 3D styles keep their own
     * values (shaded by Render). */
    {
        tbox_html_document *doc                   = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div                 = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet                = parse_css_cstr("div { border: 2px dashed; border-style: dashed dotted double groove; outline: 1px dotted; }");
        tbox_style style                          = resolve_node(sheet, div, NULL);
        const tbox_style_border_style expected[4] = { TBOX_STYLE_BORDER_STYLE_DASHED, TBOX_STYLE_BORDER_STYLE_DOTTED, TBOX_STYLE_BORDER_STYLE_DOUBLE, TBOX_STYLE_BORDER_STYLE_GROOVE };
        for (size_t i = 0; i < 4; i++) {
            TBOX_TEST_ASSERT(tbox_style_border_side_style(&style, i) == expected[i]);
            TBOX_TEST_ASSERT(tbox_style_border_side_width(&style, i) == 2.0);
        }
        TBOX_TEST_ASSERT(style.outline_style == TBOX_STYLE_BORDER_STYLE_DOTTED);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* The font shorthand sets style/weight/size/line-height/family,
     * resetting the omitted ones, and follows cascade order with the
     * longhands. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font: italic small-caps bold 20px/1.5 \"Courier New\", monospace; }"
                                                    " p { font-style: italic; font-weight: bold; line-height: 3; font: 0.5em serif; font-family: sans-serif; }");
        tbox_style parent          = resolve_node(sheet, div, NULL);
        tbox_style child           = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.font_italic && parent.font_weight_bold && parent.font_size == 20.0);
        TBOX_TEST_ASSERT(parent.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER && parent.line_height_value == 1.5);
        TBOX_TEST_ASSERT(strcmp(parent.font_family, "Courier New") == 0);
        TBOX_TEST_ASSERT(!child.font_italic && !child.font_weight_bold && child.font_size == 10.0);
        TBOX_TEST_ASSERT(child.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NORMAL);
        TBOX_TEST_ASSERT(strcmp(child.font_family, "sans-serif") == 0);
        tbox_css_stylesheet_destroy(sheet);

        const char *invalid[] = { "div { font: bold serif; }", "div { font: caption; }", "div { font: 12px; }", "div { font: fancy 12px serif; }", "div { font: 12px/ serif; }" };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            sheet            = parse_css_cstr(invalid[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.font_size == 16.0 && !style.font_weight_bold && style.font_family[0] == '\0', invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* border-radius percentages are kept apart from px radii. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border-radius: 50% 4px; border-bottom-left-radius: 10%; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_radius_percent[0] == 50.0 && style.border_radius_corners[0] == 0.0);
        TBOX_TEST_ASSERT(style.border_radius_corners[1] == 4.0 && style.border_radius_percent[1] == 0.0);
        TBOX_TEST_ASSERT(style.border_radius_percent[2] == 50.0 && style.border_radius_percent[3] == 10.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* flexbox properties, their shorthands and defaults. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW && style.flex_wrap == TBOX_STYLE_FLEX_WRAP_NOWRAP);
        TBOX_TEST_ASSERT(style.flex_grow == 0.0 && style.flex_shrink == 1.0 && style.flex_basis.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(style.align_items == TBOX_STYLE_FLEX_ALIGN_NORMAL && style.order == 0);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { display: inline-flex; flex-flow: column wrap; flex-direction: row-reverse;"
                               " justify-content: space-between; align-items: first baseline; align-self: flex-end;"
                               " align-content: center; gap: 4px 2em; row-gap: 10%; order: -2; font-size: 10px; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.display == TBOX_STYLE_DISPLAY_INLINE_FLEX);
        TBOX_TEST_ASSERT(style.flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE);
        TBOX_TEST_ASSERT(style.flex_wrap == TBOX_STYLE_FLEX_WRAP_WRAP);
        TBOX_TEST_ASSERT(style.justify_content == TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN);
        TBOX_TEST_ASSERT(style.align_items == TBOX_STYLE_FLEX_ALIGN_BASELINE);
        TBOX_TEST_ASSERT(style.align_self == TBOX_STYLE_FLEX_ALIGN_END);
        TBOX_TEST_ASSERT(style.align_content == TBOX_STYLE_FLEX_JUSTIFY_CENTER);
        TBOX_TEST_ASSERT(style.row_gap.kind == TBOX_STYLE_LENGTH_PERCENT && style.row_gap.value == 10.0);
        TBOX_TEST_ASSERT(style.column_gap.kind == TBOX_STYLE_LENGTH_PX && style.column_gap.value == 20.0);
        TBOX_TEST_ASSERT(style.order == -2);
        tbox_css_stylesheet_destroy(sheet);

        static const struct {
            const char *css;
            double grow, shrink;
            tbox_style_length_kind kind;
            double basis;
        } flex[] = {
            { "div { flex: 1; }",                                    1.0, 1.0, TBOX_STYLE_LENGTH_PERCENT, 0.0  },
            { "div { flex: 2 3; }",                                  2.0, 3.0, TBOX_STYLE_LENGTH_PERCENT, 0.0  },
            { "div { flex: 30px; }",                                 1.0, 1.0, TBOX_STYLE_LENGTH_PX,      30.0 },
            { "div { flex: 2 40%; }",                                2.0, 1.0, TBOX_STYLE_LENGTH_PERCENT, 40.0 },
            { "div { flex: 10px 2 0; }",                             2.0, 0.0, TBOX_STYLE_LENGTH_PX,      10.0 },
            { "div { flex: none; }",                                 0.0, 0.0, TBOX_STYLE_LENGTH_AUTO,    0.0  },
            { "div { flex: auto; }",                                 1.0, 1.0, TBOX_STYLE_LENGTH_AUTO,    0.0  },
            { "div { flex: 1 2 3 4; }",                              0.0, 1.0, TBOX_STYLE_LENGTH_AUTO,    0.0  },
            { "div { flex: 1 10px 2; }",                             0.0, 1.0, TBOX_STYLE_LENGTH_AUTO,    0.0  },
            { "div { flex: 1; flex-grow: 5; flex-basis: content; }", 5.0, 1.0, TBOX_STYLE_LENGTH_AUTO,    0.0  },
        };
        for (size_t i = 0; i < sizeof(flex) / sizeof(flex[0]); i++) {
            sheet = parse_css_cstr(flex[i].css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.flex_grow == flex[i].grow && style.flex_shrink == flex[i].shrink && style.flex_basis.kind == flex[i].kind && style.flex_basis.value == flex[i].basis, flex[i].css);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Logical borders compete with physical sides; text decoration can
     * combine lines and use an independently cascaded stroke style. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: 1px solid red; border-left: 2px solid blue;"
            " border-inline-start: 4px dashed green; border-block-end-width: 5px;"
            " text-decoration: underline overline red 2px; text-decoration-style: dotted;"
            " object-fit: cover; }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_widths[3] == 4.0 && style.border_styles[3] == TBOX_STYLE_BORDER_STYLE_DASHED);
        TBOX_TEST_ASSERT(style.border_widths[2] == 5.0 && style.border_styles[2] == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(style.text_decoration_lines == 5u && style.text_decoration_style == TBOX_STYLE_BORDER_STYLE_DOTTED);
        TBOX_TEST_ASSERT(style.object_fit == TBOX_STYLE_OBJECT_FIT_COVER);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { border-inline-end: 3px double blue; border-right: 2px solid red !important;"
            " border-block-start: 2px dotted green; text-decoration: underline dashed;"
            " text-decoration-line: underline line-through overline; text-decoration-style: double; object-fit: contain; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.border_widths[1] == 2.0 && style.border_styles[1] == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(style.border_widths[0] == 2.0 && style.border_styles[0] == TBOX_STYLE_BORDER_STYLE_DOTTED);
        TBOX_TEST_ASSERT(style.text_decoration_lines == 7u && style.text_decoration_style == TBOX_STYLE_BORDER_STYLE_DOUBLE);
        TBOX_TEST_ASSERT(style.object_fit == TBOX_STYLE_OBJECT_FIT_CONTAIN);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { text-decoration: underline wavy red 2px; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.text_decoration_lines == 1u && style.text_decoration_style == TBOX_STYLE_BORDER_STYLE_WAVY);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { object-fit: none; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.object_fit == TBOX_STYLE_OBJECT_FIT_NONE);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { object-fit: scale-down; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.object_fit == TBOX_STYLE_OBJECT_FIT_SCALE_DOWN);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* object-position accepts one/two values and resolves em after font-size. */
    {
        tbox_html_document *doc = parse_html_cstr("<img>");
        const tbox_html_node *img = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("img { object-position: bottom left; font-size: 20px; }");
        tbox_style style = resolve_node(sheet, img, NULL);
        TBOX_TEST_ASSERT(style.object_position[0].kind == TBOX_STYLE_LENGTH_PERCENT && style.object_position[0].value == 0.0);
        TBOX_TEST_ASSERT(style.object_position[1].kind == TBOX_STYLE_LENGTH_PERCENT && style.object_position[1].value == 100.0);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("img { object-position: 25% 2em; font-size: 20px; }");
        style = resolve_node(sheet, img, NULL);
        TBOX_TEST_ASSERT(style.object_position[0].kind == TBOX_STYLE_LENGTH_PERCENT && style.object_position[0].value == 25.0);
        TBOX_TEST_ASSERT(style.object_position[1].kind == TBOX_STYLE_LENGTH_PX && style.object_position[1].value == 40.0);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("img { object-position: top; }");
        style = resolve_node(sheet, img, NULL);
        TBOX_TEST_ASSERT(style.object_position[0].value == 50.0 && style.object_position[1].value == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* background-clip selects a box without inheriting from the parent. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { background-clip: content-box; } p { background-clip: padding-box; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.background_clip == TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX);
        TBOX_TEST_ASSERT(child.background_clip == TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { background-clip: unknown; }");
        parent = resolve_node(sheet, div, NULL);
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.background_clip == TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX);
        TBOX_TEST_ASSERT(child.background_clip == TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* image-rendering inherits; auto resets an inherited pixelated value. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><img></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *img = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { image-rendering: pixelated; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, img, &parent);
        TBOX_TEST_ASSERT(parent.image_rendering_pixelated && child.image_rendering_pixelated);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("img { image-rendering: auto; }");
        child = resolve_node(sheet, img, &parent);
        TBOX_TEST_ASSERT(!child.image_rendering_pixelated);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("img { image-rendering: crisp-edges; }");
        child = resolve_node(sheet, img, &parent);
        TBOX_TEST_ASSERT(child.image_rendering_pixelated);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* anywhere inherits, enables emergency wrapping, and can be reset
     * independently to break-word or normal. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { overflow-wrap: anywhere; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.overflow_wrap_break_word && parent.overflow_wrap_anywhere);
        TBOX_TEST_ASSERT(child.overflow_wrap_break_word && child.overflow_wrap_anywhere);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { overflow-wrap: break-word; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.overflow_wrap_break_word && !child.overflow_wrap_anywhere);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Logical dimensions compete with physical names in normal cascade order. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { width: 40px; inline-size: 90px; height: 20px; block-size: 35px;"
            " min-width: 10px; min-inline-size: 30px; max-width: 150px; max-inline-size: 110px;"
            " min-height: 5px; min-block-size: 15px; max-height: 70px; max-block-size: 50px;"
            " border-block: 2px solid red; border-inline: 3px dashed blue; border-top: 4px solid green;"
            " text-underline-position: under; }"
            "p { inline-size: 60px; width: 45px; border-left: 1px dotted red; text-underline-position: auto; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.width.value == 90.0 && parent.height.value == 35.0);
        TBOX_TEST_ASSERT(parent.min_width.value == 30.0 && parent.max_width.value == 110.0);
        TBOX_TEST_ASSERT(parent.min_height.value == 15.0 && parent.max_height.value == 50.0);
        TBOX_TEST_ASSERT(parent.border_widths[0] == 4.0 && parent.border_widths[2] == 2.0);
        TBOX_TEST_ASSERT(parent.border_widths[1] == 3.0 && parent.border_widths[3] == 3.0);
        TBOX_TEST_ASSERT(parent.text_underline_position_under && !child.text_underline_position_under);
        TBOX_TEST_ASSERT(child.width.value == 45.0 && child.border_widths[3] == 1.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Length units: rem against the root's font size, absolute units at
     * 96px per inch, ex/ch as half an em, viewport units only with a
     * viewport. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 20px; }"
            "p { font-size: 10px; width: 2rem; height: 1in; margin: 12pt 1pc 2.54cm 10mm; padding-left: 2ch; padding-top: 4ex; min-width: 50vw; max-height: 10vh; }");
        tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, div);
        tbox_style parent                = tbox_style_resolve_in_viewport(div, NULL, &computed, 800.0, 600.0);
        tbox_css_computed_style_destroy(&computed);
        computed         = tbox_css_cascade_resolve_stylesheet(sheet, p);
        tbox_style child = tbox_style_resolve_in_viewport(p, &parent, &computed, 800.0, 600.0);
        TBOX_TEST_ASSERT(parent.root_font_size == 20.0 && child.root_font_size == 20.0);
        TBOX_TEST_ASSERT(child.width.kind == TBOX_STYLE_LENGTH_PX && child.width.value == 40.0);
        TBOX_TEST_ASSERT(child.height.value == 96.0);
        TBOX_TEST_ASSERT(child.margin[0].value == 16.0 && child.margin[1].value == 16.0);
        TBOX_TEST_ASSERT(child.margin[2].value > 95.99 && child.margin[2].value < 96.01);
        TBOX_TEST_ASSERT(child.margin[3].value > 37.79 && child.margin[3].value < 37.8);
        TBOX_TEST_ASSERT(child.padding[3].value == 10.0 && child.padding[0].value == 20.0);
        TBOX_TEST_ASSERT(child.min_width.value == 400.0 && child.max_height.value == 60.0);
        /* Without a viewport, vw is invalid and falls back. */
        tbox_style no_viewport = tbox_style_resolve(p, &parent, &computed);
        TBOX_TEST_ASSERT(no_viewport.min_width.kind == TBOX_STYLE_LENGTH_AUTO);
        tbox_css_computed_style_destroy(&computed);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { font-size: 1.5rem; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.font_size == 24.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* calc(): px parts fold, percentages stay for the Layout Tree with the
     * px part in px_offset; invalid expressions fall back. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 10px; width: calc(100% - 20px); height: calc(2 * (1em + 5px)); margin-left: calc(10px*3);"
            " padding-top: calc(50% / 2 + 1rem); min-width: calc(10px + 5); max-width: calc(10px -5px); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PERCENT && style.width.value == 100.0 && style.width.px_offset == -20.0);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(style.width, 300.0) == 280.0);
        TBOX_TEST_ASSERT(style.height.kind == TBOX_STYLE_LENGTH_PX && style.height.value == 30.0);
        TBOX_TEST_ASSERT(style.margin[3].value == 30.0);
        TBOX_TEST_ASSERT(style.padding[0].kind == TBOX_STYLE_LENGTH_PERCENT && style.padding[0].value == 25.0 && style.padding[0].px_offset == 16.0);
        TBOX_TEST_ASSERT(style.min_width.kind == TBOX_STYLE_LENGTH_AUTO); /* length + number */
        TBOX_TEST_ASSERT(style.max_width.kind == TBOX_STYLE_LENGTH_AUTO); /* `-` needs spaces */
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { font-size: calc(10px + 50%); }");
        tbox_style parent = { 0 };
        parent.font_size  = 20.0;
        parent.root_font_size = 16.0;
        style = resolve_node(sheet, div, &parent);
        TBOX_TEST_ASSERT(style.font_size == 20.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* CSS-wide keywords: inherit copies the parent's value even for
     * non-inherited properties, initial resets inherited ones, unset picks
     * by property, and a more specific ordinary longhand still wins. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p class=\"c\">x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { color: red; margin: 7px; border: 3px solid blue; background-color: lime; font-size: 30px; text-align: center; }"
            "p { margin: inherit; border: inherit; background-color: INHERIT; color: initial; text-align: unset; padding: unset; font-size: initial; width: 2em; }"
            "p.c { margin-top: 1px; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.margin[0].value == 1.0 && child.margin[1].value == 7.0 && child.margin[3].value == 7.0);
        TBOX_TEST_ASSERT(child.border_widths[2] == 3.0 && child.border_styles[0] == TBOX_STYLE_BORDER_STYLE_SOLID && !child.border_per_side && child.border_width == 3.0);
        TBOX_TEST_ASSERT(rgba_eq(child.background_color, (tbox_css_rgba){ 0, 255, 0, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(child.color, (tbox_css_rgba){ 0, 0, 0, 255 }));
        TBOX_TEST_ASSERT(child.text_align == TBOX_STYLE_TEXT_ALIGN_CENTER);
        TBOX_TEST_ASSERT(child.padding[0].value == 0.0);
        TBOX_TEST_ASSERT(child.font_size == 16.0 && child.width.value == 32.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* overflow two-value syntax and longhands; visibility: collapse;
     * keep-all; break-spaces; tab-size; list-style-position. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { overflow: hidden scroll; visibility: collapse; word-break: keep-all; tab-size: 4; list-style: square inside; }"
            "p { overflow: auto; overflow-x: clip; tab-size: 20px; list-style-position: outside; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.overflow_x == TBOX_STYLE_OVERFLOW_Y_HIDDEN && parent.overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO);
        TBOX_TEST_ASSERT(child.overflow_x == TBOX_STYLE_OVERFLOW_Y_HIDDEN && child.overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO);
        TBOX_TEST_ASSERT(parent.visibility_hidden && parent.visibility_collapse);
        TBOX_TEST_ASSERT(child.word_break_keep_all && !child.word_break_all);
        TBOX_TEST_ASSERT(parent.tab_size == 4.0 && !parent.tab_size_length);
        TBOX_TEST_ASSERT(child.tab_size == 20.0 && child.tab_size_length);
        TBOX_TEST_ASSERT(parent.list_style_inside && parent.list_style_type == TBOX_STYLE_LIST_STYLE_SQUARE && !child.list_style_inside);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* text-align-last, empty-cells, table-layout, user-select, cursor,
     * aspect-ratio, line-clamp, z-index. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { text-align-last: center; empty-cells: hide; table-layout: fixed; user-select: none; cursor: url(a.cur) 2 2, pointer;"
            " aspect-ratio: 16 / 9; -webkit-line-clamp: 3; display: -webkit-box; z-index: -2; position: relative; }"
            "p { aspect-ratio: 2; z-index: auto; line-clamp: none; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.text_align_last == TBOX_STYLE_TEXT_ALIGN_LAST_CENTER && child.text_align_last == TBOX_STYLE_TEXT_ALIGN_LAST_CENTER);
        TBOX_TEST_ASSERT(parent.empty_cells_hide && child.empty_cells_hide);
        TBOX_TEST_ASSERT(parent.table_layout_fixed && !child.table_layout_fixed);
        TBOX_TEST_ASSERT(parent.user_select == TBOX_STYLE_USER_SELECT_NONE && child.user_select == TBOX_STYLE_USER_SELECT_NONE);
        TBOX_TEST_ASSERT(parent.cursor == TBOX_STYLE_CURSOR_POINTER && child.cursor == TBOX_STYLE_CURSOR_POINTER);
        TBOX_TEST_ASSERT(parent.aspect_ratio > 1.777 && parent.aspect_ratio < 1.778 && child.aspect_ratio == 2.0);
        TBOX_TEST_ASSERT(parent.line_clamp == 3 && parent.display == TBOX_STYLE_DISPLAY_BLOCK && child.line_clamp == 0);
        TBOX_TEST_ASSERT(!parent.z_index_auto && parent.z_index == -2 && child.z_index_auto);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Numeric font weights with the relative bolder/lighter table,
     * font-stretch, small-caps, also through the font shorthand. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-weight: 300; font-stretch: condensed; font-variant: small-caps; } p { font-weight: bolder; font-stretch: 150%; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.font_weight == 300 && !parent.font_weight_bold && parent.font_stretch == 75.0 && parent.font_small_caps);
        TBOX_TEST_ASSERT(child.font_weight == 400 && child.font_stretch == 150.0 && child.font_small_caps);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { font: small-caps 800 expanded 12px serif; } p { font-weight: lighter; }");
        parent = resolve_node(sheet, div, NULL);
        child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.font_weight == 800 && parent.font_weight_bold && parent.font_stretch == 125.0 && parent.font_small_caps);
        TBOX_TEST_ASSERT(child.font_weight == 700);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* background shorthand with image, position, size and repeat; the
     * longhands; gradients. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { background: #fff url(\"img/a.png\") right 10px bottom 20% / 50% auto no-repeat padding-box; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(strcmp(style.background_image, "img/a.png") == 0);
        TBOX_TEST_ASSERT(rgba_eq(style.background_color, (tbox_css_rgba){ 255, 255, 255, 255 }));
        TBOX_TEST_ASSERT(style.background_position[0].kind == TBOX_STYLE_LENGTH_PERCENT && style.background_position[0].value == 100.0 && style.background_position[0].px_offset == -10.0);
        TBOX_TEST_ASSERT(style.background_position[1].value == 80.0);
        TBOX_TEST_ASSERT(style.background_size[0].value == 50.0 && style.background_size[1].kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(!style.background_repeat_x && !style.background_repeat_y);
        TBOX_TEST_ASSERT(style.background_clip == TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { background-image: linear-gradient(to right, red, rgba(0, 0, 255, 0.5) 40%, lime); background-size: cover; background-repeat: repeat-x; background-position: center; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.background_gradient.kind == TBOX_STYLE_GRADIENT_LINEAR && style.background_gradient.angle == 90.0);
        TBOX_TEST_ASSERT(style.background_gradient.stop_count == 3 && style.background_gradient.stops[1].position.value == 40.0);
        TBOX_TEST_ASSERT(style.background_gradient.stops[0].position.kind == TBOX_STYLE_LENGTH_AUTO && style.background_gradient.stops[1].color.a == 128);
        TBOX_TEST_ASSERT(style.background_size_kind == TBOX_STYLE_BACKGROUND_SIZE_COVER);
        TBOX_TEST_ASSERT(style.background_repeat_x && !style.background_repeat_y);
        TBOX_TEST_ASSERT(style.background_position[0].value == 50.0 && style.background_position[1].value == 50.0);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { background: repeating-radial-gradient(circle closest-side at 25% 75%, red 0 10px, blue 20px) }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.background_gradient.kind == TBOX_STYLE_GRADIENT_RADIAL && style.background_gradient.repeating && style.background_gradient.circle);
        TBOX_TEST_ASSERT(style.background_gradient.extent == TBOX_STYLE_GRADIENT_CLOSEST_SIDE);
        TBOX_TEST_ASSERT(style.background_gradient.center[0].value == 25.0 && style.background_gradient.center[1].value == 75.0);
        TBOX_TEST_ASSERT(style.background_gradient.stop_count == 3 && style.background_gradient.stops[1].position.value == 10.0);
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { background-image: linear-gradient(45deg, red); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.background_gradient.kind == TBOX_STYLE_GRADIENT_NONE); /* one stop is invalid */
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { background-image: linear-gradient(to top left, red, blue); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.background_gradient.corner[0] == -1 && style.background_gradient.corner[1] == -1);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* inset shadows, multiple text shadows, elliptical radii, translate. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { box-shadow: inset 0 0 4px red, 2px 2px blue; text-shadow: 1px 1px red, -1px -1px 2px blue; border-radius: 10px 20px / 5px;"
            " transform: translate(10px, 50%) translateX(5px); }"
            "p { border-top-left-radius: 8px 4px; translate: 3px; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.box_shadow_count == 2 && parent.box_shadows[0].inset && parent.box_shadow_inset && !parent.box_shadows[1].inset);
        TBOX_TEST_ASSERT(parent.text_shadow_count == 2 && child.text_shadow_count == 2 && child.text_shadows[1].blur == 2.0);
        TBOX_TEST_ASSERT(parent.border_radius_corners[0] == 10.0 && parent.border_radius_corners[1] == 20.0 && parent.border_radius_vertical[1] == 5.0);
        TBOX_TEST_ASSERT(child.border_radius_corners[0] == 8.0 && child.border_radius_vertical[0] == 4.0);
        TBOX_TEST_ASSERT(parent.translate_x.kind == TBOX_STYLE_LENGTH_PX && parent.translate_x.value == 15.0);
        TBOX_TEST_ASSERT(parent.translate_y.kind == TBOX_STYLE_LENGTH_PERCENT && parent.translate_y.value == 50.0);
        TBOX_TEST_ASSERT(child.translate_x.value == 3.0 && child.translate_y.value == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { transform: rotate(10deg); }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.translate_x.value == 0.0 && parent.translate_y.value == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Modern color syntax through the style layer. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { color: rgb(10 20 30 / 50%); background: hwb(0 0% 0%); border: 1px solid oklch(0.628 0.258 29.23); outline: 1px solid color-mix(in srgb, red, blue); }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(rgba_eq(style.color, (tbox_css_rgba){ 10, 20, 30, 128 }));
        TBOX_TEST_ASSERT(rgba_eq(style.background_color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(style.border_color.r > 240 && style.border_color.g < 20 && style.border_color.b < 20);
        TBOX_TEST_ASSERT(rgba_eq(style.outline_color, (tbox_css_rgba){ 128, 0, 128, 255 }));
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* min()/max()/clamp(): pure px folds; a percentage among px keeps px
     * bounds; intrinsic size keywords stay AUTO with a keyword. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { margin-left: min(10px, 2em, 30px); margin-right: max(1em, 4px); width: clamp(200px, 40%, 600px); min-width: min(100%, 300px); height: max-content; padding-top: clamp(1px, 50px, 10px); }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.margin[3].value == 10.0 && style.margin[1].value == 16.0);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PERCENT && style.width.bounds == 3);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(style.width, 100.0) == 200.0 && tbox_style_length_resolve(style.width, 1000.0) == 400.0 && tbox_style_length_resolve(style.width, 2000.0) == 600.0);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(style.min_width, 1000.0) == 300.0 && tbox_style_length_resolve(style.min_width, 200.0) == 200.0);
        TBOX_TEST_ASSERT(style.height.kind == TBOX_STYLE_LENGTH_AUTO && style.height_keyword == TBOX_STYLE_SIZE_KEYWORD_MAX_CONTENT);
        TBOX_TEST_ASSERT(style.padding[0].value == 10.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { width: fit-content; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width_keyword == TBOX_STYLE_SIZE_KEYWORD_FIT_CONTENT && style.width.kind == TBOX_STYLE_LENGTH_AUTO);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* place-*, text-wrap family, text-overflow strings, display keywords,
     * text-indent keywords, background-origin, scrollbars, 3D borders,
     * kerning and hyphens. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { place-content: center space-between; place-items: end; text-wrap: balance; white-space-collapse: preserve; text-overflow: \"[+]\";"
            " display: list-item; text-indent: 2em hanging each-line; background: url(a.png) content-box; scrollbar-color: red blue; scrollbar-width: thin;"
            " border: 4px ridge; font-kerning: none; hyphens: none; }"
            "p { text-wrap-mode: nowrap; display: flow-root; place-self: center; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.align_content == TBOX_STYLE_FLEX_JUSTIFY_CENTER && parent.justify_content == TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN);
        TBOX_TEST_ASSERT(parent.align_items == TBOX_STYLE_FLEX_ALIGN_END && child.align_self == TBOX_STYLE_FLEX_ALIGN_CENTER);
        TBOX_TEST_ASSERT(parent.text_wrap_balance && parent.white_space == TBOX_STYLE_WHITE_SPACE_PRE_WRAP);
        TBOX_TEST_ASSERT(child.white_space == TBOX_STYLE_WHITE_SPACE_PRE && child.text_wrap_balance);
        TBOX_TEST_ASSERT(parent.text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS && strcmp(parent.text_overflow_string, "[+]") == 0);
        TBOX_TEST_ASSERT(parent.display == TBOX_STYLE_DISPLAY_BLOCK && parent.display_list_item && child.display == TBOX_STYLE_DISPLAY_BLOCK && !child.display_list_item);
        TBOX_TEST_ASSERT(parent.text_indent.value == 32.0 && parent.text_indent_hanging && parent.text_indent_each_line && child.text_indent_hanging);
        TBOX_TEST_ASSERT(parent.background_origin == TBOX_STYLE_BACKGROUND_ORIGIN_CONTENT_BOX && parent.background_clip == TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX);
        TBOX_TEST_ASSERT(rgba_eq(child.scrollbar_thumb_color, (tbox_css_rgba){ 255, 0, 0, 255 }) && child.scrollbar_track_color.b == 255);
        TBOX_TEST_ASSERT(parent.scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_THIN && child.scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_AUTO);
        TBOX_TEST_ASSERT(parent.border_style == TBOX_STYLE_BORDER_STYLE_RIDGE);
        TBOX_TEST_ASSERT(child.font_kerning_none && child.hyphens_none);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* filter matrices, clip-path shapes, several background layers. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { filter: invert(1) blur(2px); clip-path: circle(30px at left 10px top 20px);"
                                                    " background: url(a.png) 0 0 / 10px no-repeat, linear-gradient(red, blue), #fff; background-repeat: repeat-x; }");
        tbox_style style           = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.has_filter && style.filter_matrix[0] == -1.0 && style.filter_matrix[4] == 1.0 && style.filter_matrix[18] == 1.0);
        TBOX_TEST_ASSERT(style.clip_path.kind == TBOX_STYLE_CLIP_PATH_CIRCLE && style.clip_path.radius[0].value == 30.0);
        TBOX_TEST_ASSERT(style.clip_path.center[0].px_offset == 10.0 && style.clip_path.center[1].px_offset == 20.0);
        TBOX_TEST_ASSERT(style.background_layer_count == 3 && strcmp(style.background_image, "a.png") == 0 && style.background_size[0].value == 10.0);
        TBOX_TEST_ASSERT(style.background_layers[1].image[0] == '\0' && style.background_layers[1].gradient.kind == TBOX_STYLE_GRADIENT_NONE); /* color-only last layer */
        TBOX_TEST_ASSERT(style.background_layers[0].gradient.kind == TBOX_STYLE_GRADIENT_LINEAR);
        TBOX_TEST_ASSERT(style.background_repeat_x && !style.background_repeat_y && style.background_layers[0].repeat_x && !style.background_layers[0].repeat_y); /* the list repeats */
        TBOX_TEST_ASSERT(rgba_eq(style.background_color, (tbox_css_rgba){ 255, 255, 255, 255 }));
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip-path: inset(5px 10% round 4px / 8px); filter: none; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.clip_path.kind == TBOX_STYLE_CLIP_PATH_INSET && style.clip_path.inset[0].value == 5.0 && style.clip_path.inset[1].value == 10.0);
        TBOX_TEST_ASSERT(style.clip_path.round_h[2] == 4.0 && style.clip_path.round_v[2] == 8.0 && !style.has_filter);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Custom properties: inherited through the tree, a fallback, an
     * undefined reference making the declaration unset, a cycle; ::marker
     * and list-style-image. */
    {
        tbox_html_document *doc        = parse_html_cstr("<ul><li id=\"a\"><span>x</span></li></ul>");
        const tbox_html_node *root     = tbox_html_document_root(doc);
        const tbox_html_node *ul       = root->first_child;
        const tbox_html_node *li       = ul->first_child;
        const tbox_html_node *span     = li->first_child;
        tbox_css_stylesheet *sheet     = parse_css_cstr(
            "ul { --gap: 7px; --main: rgb(1 2 3); list-style: url(dot.png) inside; }"
            "li { margin-left: var(--gap); color: var(--main); border-top: 2px solid var(--missing); --loop: var(--loop); padding-top: var(--loop, 3px); }"
            "span { margin-top: calc(var(--gap) * 2); background-color: var(--nope, lime); }"
            "li::marker { color: red; font-size: 30px; content: \"> \"; }");
        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, root, &source, 1);
        const tbox_style *li_style     = tbox_style_table_find(&table, li);
        const tbox_style *span_style   = tbox_style_table_find(&table, span);
        TBOX_TEST_ASSERT(li_style != NULL && span_style != NULL && tbox_style_table_find(&table, ul) != NULL);
        if (li_style != NULL && span_style != NULL) {
            TBOX_TEST_ASSERT(li_style->margin[3].value == 7.0 && rgba_eq(li_style->color, (tbox_css_rgba){ 1, 2, 3, 255 }));
            TBOX_TEST_ASSERT(rgba_eq(li_style->border_colors[0], li_style->color)); /* var(--missing): unset, currentColor */
            TBOX_TEST_ASSERT(li_style->padding[0].value == 3.0);                   /* a cyclic variable is invalid, so the fallback applies */
            TBOX_TEST_ASSERT(span_style->margin[0].value == 14.0 && rgba_eq(span_style->background_color, (tbox_css_rgba){ 0, 255, 0, 255 }));
            TBOX_TEST_ASSERT(li_style->marker_styled && rgba_eq(li_style->marker_color, (tbox_css_rgba){ 255, 0, 0, 255 }) && li_style->marker_font_size == 30.0);
            TBOX_TEST_ASSERT(li_style->marker_has_content && strcmp(li_style->marker_content, "> ") == 0);
            TBOX_TEST_ASSERT(strcmp(li_style->list_style_image, "dot.png") == 0 && li_style->list_style_inside);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
