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

    /* 20: an unrecognized token inside `border` (e.g. "dashed") does not
     * knock down the other two valid tokens -- only border_style stays at
     * its initial value NONE, since no token matched solid/none. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { border: 2px dashed red; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.border_width == 2.0, "an unrecognized token should not knock down other valid tokens");
        TBOX_TEST_ASSERT_MSG(rgba_eq(style.border_color, (tbox_css_rgba){ 255, 0, 0, 255 }), "an unrecognized token should not knock down other valid tokens");
        TBOX_TEST_ASSERT_MSG(style.border_style == TBOX_STYLE_BORDER_STYLE_NONE, "no token matched solid/none, so border_style stays at its initial value");

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

    /* 21b: NOVO v5 -- position: absolute/fixed/sticky each resolve to their
     * own enum value (not folded into STATIC like an unrecognized keyword,
     * and not folded into each other). */
    {
        static const struct {
            const char *css;
            tbox_style_position expected;
        } cases[] = {
            { "div { position: absolute; }", TBOX_STYLE_POSITION_ABSOLUTE },
            { "div { position: fixed; }", TBOX_STYLE_POSITION_FIXED },
            { "div { position: sticky; }", TBOX_STYLE_POSITION_STICKY },
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

    /* 21c: NOVO v5 -- none of absolute/fixed/sticky inherit from the parent;
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

    /* 24: NOVO v8 -- "2em" in `width` resolves against the node's OWN
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

    /* 25: NOVO v8 -- `em` in `margin` (any property other than font-size)
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

    /* 28: NOVO v11 -- text-align: center resolves to TEXT_ALIGN_CENTER. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-align: center; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.text_align == TBOX_STYLE_TEXT_ALIGN_CENTER);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 29: NOVO v11 -- text-align matching is case-insensitive -- "RIGHT"
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

    /* 30: NOVO v11 -- text-align inherits from the parent when undeclared,
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

    /* 31: NOVO v11 -- text-align: justify is out of scope (no `justify`
     * support) -- falls back to the inherited/initial value LEFT, since
     * there is no parent here. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-align: justify; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.text_align == TBOX_STYLE_TEXT_ALIGN_LEFT, "justify is unrecognized, should fall back to the initial value LEFT");

        tbox_css_stylesheet_destroy(sheet);
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

    /* 33: NOVO v12 -- font-family: Verdana resolves style.font_family to
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

    /* 34: NOVO v12 -- font-family: "Courier New", monospace; resolves to
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

    /* 35: NOVO v12 -- font-family: Verdana, Arial, sans-serif; (unquoted,
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

    /* 36: NOVO v12 -- font-family inherits from the parent when undeclared,
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

    /* 38: NOVO v13 -- font-style: italic sets font_italic = true. */
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

    /* 40: NOVO v13 -- font_italic inherits from the parent when undeclared,
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

    /* 41: NOVO v13 -- text-decoration: underline/line-through resolve to
     * their respective enum values; absent resolves to NONE. */
    {
        static const struct {
            const char *css;
            tbox_style_text_decoration expected;
        } cases[] = {
            { "div { text-decoration: underline; }", TBOX_STYLE_TEXT_DECORATION_UNDERLINE },
            { "div { text-decoration: line-through; }", TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH },
            { "", TBOX_STYLE_TEXT_DECORATION_NONE },
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

    /* 42: NOVO v13 -- text-decoration does NOT inherit -- a child with no
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

    /* 43: NOVO v13 -- vertical-align: sub/super resolve to their respective
     * enum values; absent resolves to BASELINE. */
    {
        static const struct {
            const char *css;
            tbox_style_vertical_align expected;
        } cases[] = {
            { "div { vertical-align: sub; }", TBOX_STYLE_VERTICAL_ALIGN_SUB },
            { "div { vertical-align: super; }", TBOX_STYLE_VERTICAL_ALIGN_SUPER },
            { "", TBOX_STYLE_VERTICAL_ALIGN_BASELINE },
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

    /* 44: NOVO v13 -- vertical-align does NOT inherit -- a child with no
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
        /* A comma (multiple shadows, out of scope) makes the whole
         * declaration ignored, falling back to "no shadow" -- not a crash,
         * not a half-parsed result. */
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { box-shadow: 1px 1px red, 2px 2px blue; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(style.box_shadow_color.a == 0, "a comma-separated (multiple shadow) value must be rejected entirely, out of scope");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    {
        tbox_html_document *doc = parse_html_cstr("<table><caption>Title</caption></table>");
        const tbox_html_node *table = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *caption = table->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "table { border-collapse: collapse; border-spacing: 6px 8px; caption-side: bottom; }"
            "caption { vertical-align: middle; border-spacing: 0; }");
        tbox_style table_style = resolve_node(sheet, table, NULL);
        tbox_style caption_style = resolve_node(sheet, caption, &table_style);
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
        tbox_html_document *doc = parse_html_cstr("<div><p>child</p><span>inherit</span></div>");
        const tbox_html_node *parent = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *child = parent->first_child;
        const tbox_html_node *sibling = child->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-weight: 700; font-style: italic; white-space: nowrap; }"
            "p { font-weight: 400; font-style: normal; white-space: normal; }");
        tbox_style outer = resolve_node(sheet, parent, NULL);
        tbox_style reset = resolve_node(sheet, child, &outer);
        tbox_style inherited = resolve_node(sheet, sibling, &outer);
        TBOX_TEST_ASSERT(outer.font_weight_bold && outer.font_italic && outer.white_space_nowrap);
        TBOX_TEST_ASSERT(!reset.font_weight_bold && !reset.font_italic && !reset.white_space_nowrap);
        TBOX_TEST_ASSERT(inherited.font_weight_bold && inherited.font_italic && inherited.white_space_nowrap);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Decoration color/thickness are independent of text color; hidden
     * overflow clips without requesting a scroll state. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>text</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { color: blue; text-decoration: underline; text-decoration-color: red;"
            " text-decoration-thickness: 3px; overflow-y: hidden; }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN);
        TBOX_TEST_ASSERT(rgba_eq(style.color, (tbox_css_rgba){0, 0, 255, 255}));
        TBOX_TEST_ASSERT(rgba_eq(style.text_decoration_color, (tbox_css_rgba){255, 0, 0, 255}));
        TBOX_TEST_ASSERT(style.text_decoration_thickness == 3.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Uniform border longhands obey source order and importance relative to
     * the existing shorthand. */
    {
        tbox_html_document *doc = parse_html_cstr("<div class='x'>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[] = {
            "div { border: 2px solid red; border-color: blue; }",
            "div { border-color: blue; border: 2px solid red; }",
            "div { border-color: blue !important; border: 2px solid red; }",
            "div { border-width: 4px; border-style: solid; border-color: green; }",
            ".x { border-color: blue; } div { border: 2px solid red; }",
        };
        const tbox_css_rgba colors[] = {
            {0, 0, 255, 255}, {255, 0, 0, 255}, {0, 0, 255, 255}, {0, 128, 0, 255},
            {0, 0, 255, 255},
        };
        const double widths[] = {2.0, 2.0, 2.0, 4.0, 2.0};
        for (size_t i = 0; i < 5; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.border_style == TBOX_STYLE_BORDER_STYLE_SOLID);
            TBOX_TEST_ASSERT(style.border_width == widths[i]);
            TBOX_TEST_ASSERT(rgba_eq(style.border_color, colors[i]));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Box-side longhands obey shorthand order, importance, and validity. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[] = {
            "div { margin: 1px; margin-left: 7px; padding: 2px; padding-top: 4px; }",
            "div { margin-left: 7px; margin: 1px; padding-top: 4px; padding: 2px; }",
            "div { margin-left: 7px !important; margin: 1px; padding-top: 4px !important; padding: 2px; }",
            "div { margin: 0; padding: -1px; padding-left: 5px; }"
        };
        const double lefts[] = {7.0, 1.0, 7.0, 0.0};
        const double tops[] = {4.0, 2.0, 4.0, 0.0};
        for (size_t i = 0; i < 4; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.margin[3].value == lefts[i]);
            TBOX_TEST_ASSERT(style.padding[0].value == tops[i]);
            if (i == 3) TBOX_TEST_ASSERT(style.padding[3].value == 5.0);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Background shorthand and longhand compete by normal cascade priority. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[] = {
            "div { background: red; background-color: blue; }",
            "div { background-color: blue; background: red; }",
            "div { background: red; background-color: blue !important; }",
            "div { background-color: blue; background: url(missing.png); }"
        };
        const tbox_css_rgba expected[] = {
            {0, 0, 255, 255}, {255, 0, 0, 255}, {0, 0, 255, 255}, {0, 0, 255, 255}
        };
        for (size_t i = 0; i < 4; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(rgba_eq(style.background_color, expected[i]));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Word spacing and indentation inherit; outline and decoration do not. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { text-indent: 10%; word-spacing: 2px; outline: 3px solid red; text-decoration: overline; }"
            "p { word-spacing: normal; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.text_indent.kind == TBOX_STYLE_LENGTH_PERCENT && parent.text_indent.value == 10.0);
        TBOX_TEST_ASSERT(child.text_indent.kind == TBOX_STYLE_LENGTH_PERCENT && child.text_indent.value == 10.0);
        TBOX_TEST_ASSERT(parent.word_spacing == 2.0 && child.word_spacing == 0.0);
        TBOX_TEST_ASSERT(parent.outline_width == 3.0 && parent.outline_style == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(rgba_eq(parent.outline_color, (tbox_css_rgba){255, 0, 0, 255}));
        TBOX_TEST_ASSERT(child.outline_style == TBOX_STYLE_BORDER_STYLE_NONE);
        TBOX_TEST_ASSERT(parent.text_decoration == TBOX_STYLE_TEXT_DECORATION_OVERLINE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Edge colors use the element's computed color; width keywords work in
     * both shorthand and longhand declarations. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[] = {
            "div { color: #123456; border: thick solid currentColor;"
            " outline: thin solid currentColor; outline-offset: -2px; }",
            "div { color: blue; border: 2px solid red; border-width: medium;"
            " border-color: currentColor; outline: 2px solid red;"
            " outline-color: currentColor; outline-width: thick; }"
        };
        const double border_widths[] = {5.0, 3.0};
        const double outline_widths[] = {1.0, 5.0};
        const double outline_offsets[] = {-2.0, 0.0};
        for (size_t i = 0; i < 2; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
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
        tbox_html_document *doc = parse_html_cstr("<div>box</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 20px; min-width: 2em; max-width: 60%; }");
        tbox_style style = resolve_node(sheet, div, NULL);
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
        tbox_html_document *doc = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 20px; line-height: 1.5; letter-spacing: 2px;"
            " visibility: hidden; box-sizing: border-box; text-overflow: ellipsis; overflow: hidden; }"
            "p { font-size: 10px; visibility: visible; letter-spacing: normal; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER &&
            parent.line_height_value == 1.5);
        TBOX_TEST_ASSERT(child.line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER &&
            child.line_height_value == 1.5 && child.font_size == 10.0);
        TBOX_TEST_ASSERT(parent.letter_spacing == 2.0 && child.letter_spacing == 0.0);
        TBOX_TEST_ASSERT(parent.visibility_hidden && !child.visibility_hidden);
        TBOX_TEST_ASSERT(parent.box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX &&
            child.box_sizing == TBOX_STYLE_BOX_SIZING_CONTENT_BOX);
        TBOX_TEST_ASSERT(parent.text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS &&
            child.text_overflow == TBOX_STYLE_TEXT_OVERFLOW_CLIP);
        TBOX_TEST_ASSERT(parent.overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Height limits resolve like width limits, and the new font/color
     * values preserve inheritance and cascade precedence. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>text</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 20px; min-height: 2em; max-height: 60%;"
            " color: #123456; background: currentColor; font-weight: 600; font-style: oblique; }"
            "p { background: red; background-color: currentColor; font-weight: 500; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.min_height.kind == TBOX_STYLE_LENGTH_PX && parent.min_height.value == 40.0);
        TBOX_TEST_ASSERT(parent.max_height.kind == TBOX_STYLE_LENGTH_PERCENT && parent.max_height.value == 60.0);
        TBOX_TEST_ASSERT(rgba_eq(parent.background_color, parent.color));
        TBOX_TEST_ASSERT(rgba_eq(child.background_color, parent.color));
        TBOX_TEST_ASSERT(parent.font_weight_bold && !child.font_weight_bold);
        TBOX_TEST_ASSERT(parent.font_italic && child.font_italic);
        tbox_css_stylesheet_destroy(sheet);

        const char *weights[] = {"100", "200", "300", "400", "500", "600", "700", "800", "900"};
        for (size_t i = 0; i < sizeof(weights) / sizeof(weights[0]); i++) {
            char css[64];
            snprintf(css, sizeof(css), "div { font-weight: %s; }", weights[i]);
            sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.font_weight_bold == (i >= 5));
            tbox_css_stylesheet_destroy(sheet);
        }
        sheet = parse_css_cstr("div { min-height: -1px; max-height: none; font-weight: 650; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.min_height.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(parent.max_height.kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(!parent.font_weight_bold);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Font-size keyword values and relative sizes resolve before em lengths. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        const char *keywords[] = {"xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large"};
        const double expected[] = {9.0, 10.0, 13.0, 16.0, 18.0, 24.0, 32.0};
        for (size_t i = 0; i < 7; i++) {
            char css[80];
            snprintf(css, sizeof(css), "div { font-size: %s; padding: 1em; }", keywords[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT(style.font_size == expected[i] && style.padding[0].value == expected[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 20px; } p { font-size: larger; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_size == 24.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { font-size: 24px; } p { font-size: smaller; }");
        parent = resolve_node(sheet, div, NULL);
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_size == 20.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Clockwise radius expansion and a later longhand override. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const char *css[] = {
            "div { border-radius: 2px; }",
            "div { border-radius: 2px 4px; }",
            "div { border-radius: 2px 4px 6px; }",
            "div { border-radius: 2px 4px 6px 8px; }",
            "div { font-size: 20px; border-radius: 1em 2px; border-top-right-radius: 5px; }",
            "div { border-top-left-radius: 7px; border-radius: invalid; }"
        };
        const double expected[6][4] = {
            {2, 2, 2, 2}, {2, 4, 2, 4}, {2, 4, 6, 4},
            {2, 4, 6, 8}, {20, 5, 20, 2}, {7, 0, 0, 0}
        };
        for (size_t i = 0; i < 6; i++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(css[i]);
            tbox_style style = resolve_node(sheet, div, NULL);
            for (size_t j = 0; j < 4; j++) TBOX_TEST_ASSERT(style.border_radius_corners[j] == expected[i][j]);
            TBOX_TEST_ASSERT(style.border_radius == (i == 0 ? 2.0 : 0.0));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Text wrapping and pointer targeting inherit, with explicit resets. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { overflow-wrap: break-word; pointer-events: none; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.overflow_wrap_break_word && child.overflow_wrap_break_word);
        TBOX_TEST_ASSERT(parent.pointer_events_none && child.pointer_events_none);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { overflow-wrap: normal; pointer-events: auto; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(!child.overflow_wrap_break_word && !child.pointer_events_none);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
