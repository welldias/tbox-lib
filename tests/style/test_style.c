#include <tbox/style.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "test_support.h"
#include "style/tbox_style_internal.h"
#include "css_parser/tbox_css_parser.h"

/* Out-of-line style groups (tbox_style_masks_of, ...) of the styles this
 * file resolves live here, for the whole run. */
static tbox_arena style_arena;

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
    tbox_style style                 = tbox_style_resolve(&style_arena, node, parent_style, &computed);
    tbox_css_computed_style_destroy(&computed);
    return style;
}

static bool rgba_eq(tbox_css_rgba a, tbox_css_rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* A fake tbox_style_font_metrics: "mono" has ex 0.5 / ch 0.6, every other
 * family ex 0.4 / ch 0.55. Counts its calls in *userdata. */
static bool fake_font_metrics(void *userdata, const char *font_family, double *out_ex, double *out_ch) {
    (*(int *)userdata)++;
    bool mono = strcmp(font_family, "mono") == 0;
    *out_ex   = mono ? 0.5 : 0.4;
    *out_ch   = mono ? 0.6 : 0.55;
    return true;
}

int tbox_test_style_at_rules_run(void);
int tbox_test_grid_style_run(void);
int tbox_test_text_paint_run(void);
int tbox_test_motion_run(void);

int tbox_test_style_run(void) {
    style_arena = tbox_arena_create(0);
    int failures = tbox_test_style_at_rules_run();

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

    /* zoom: the effective factor is the product down the tree; absolute
     * lengths, font sizes (named ones too), border keywords and img size
     * attributes scale by it, em follows the zoomed font size, % stays,
     * and inherited lengths scale by the child's own factor only. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p><span>x</span></p><img width='10'><b>y</b><i>z</i></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->first_child;
        const tbox_html_node *img  = p->next_sibling;
        const tbox_html_node *b    = img->next_sibling;
        const tbox_html_node *i    = b->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { zoom: 2; width: 100px; font-size: 10px; letter-spacing: 1px; border: thin solid; padding: 5%; }"
            "p { zoom: 50%; width: 10em; height: 1in; margin: 2px; font-size: medium; line-height: 20px; }"
            "span { zoom: 3; } b { zoom: normal; } i { zoom: -1; }");
        tbox_style d = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(d.zoom == 2.0 && d.width.value == 200.0 && d.font_size == 20.0 && d.letter_spacing == 2.0);
        TBOX_TEST_ASSERT(d.border_widths[0] == 2.0);
        TBOX_TEST_ASSERT(d.padding[0].kind == TBOX_STYLE_LENGTH_PERCENT && d.padding[0].value == 5.0);
        tbox_style ps = resolve_node(sheet, p, &d);
        TBOX_TEST_ASSERT(ps.zoom == 1.0 && ps.font_size == 16.0 && ps.width.value == 160.0 && ps.height.value == 96.0 && ps.margin[0].value == 2.0);
        TBOX_TEST_ASSERT(ps.letter_spacing == 1.0);
        tbox_style sp = resolve_node(sheet, span, &ps);
        TBOX_TEST_ASSERT(sp.zoom == 3.0 && sp.font_size == 48.0 && sp.line_height_value == 60.0 && sp.letter_spacing == 3.0);
        tbox_style im = resolve_node(sheet, img, &d);
        TBOX_TEST_ASSERT(im.width.kind == TBOX_STYLE_LENGTH_PX && im.width.value == 20.0);
        tbox_style bs = resolve_node(sheet, b, &d), is = resolve_node(sheet, i, &d);
        TBOX_TEST_ASSERT(bs.zoom == 2.0 && is.zoom == 2.0 && bs.font_size == 20.0);
        TBOX_TEST_ASSERT(tbox_style_zoom(NULL) == 1.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* resize: the keywords (block/inline as vertical/horizontal), not
     * inherited, effective only where overflow clips or on a textarea. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p><b>y</b><i>z</i><u>w</u></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { resize: both; overflow: hidden; } p { resize: inline; overflow: auto; }"
                                                    "b { resize: block; } i { resize: diagonal; } u { resize: vertical; overflow-x: auto; }");
        tbox_style d  = resolve_node(sheet, div, NULL);
        tbox_style ps = resolve_node(sheet, p, &d);
        tbox_style bs = resolve_node(sheet, p->next_sibling, &d);
        tbox_style is = resolve_node(sheet, p->next_sibling->next_sibling, &d);
        tbox_style us = resolve_node(sheet, p->next_sibling->next_sibling->next_sibling, &d);
        TBOX_TEST_ASSERT(d.resize == TBOX_STYLE_RESIZE_BOTH && tbox_style_resize_allowed(&d, false) == TBOX_STYLE_RESIZE_BOTH);
        TBOX_TEST_ASSERT(ps.resize == TBOX_STYLE_RESIZE_HORIZONTAL && bs.resize == TBOX_STYLE_RESIZE_VERTICAL && is.resize == TBOX_STYLE_RESIZE_NONE);
        TBOX_TEST_ASSERT(tbox_style_resize_allowed(&bs, false) == TBOX_STYLE_RESIZE_NONE && tbox_style_resize_allowed(&bs, true) == TBOX_STYLE_RESIZE_VERTICAL);
        TBOX_TEST_ASSERT(tbox_style_resize_allowed(&us, false) == TBOX_STYLE_RESIZE_VERTICAL);
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

    /* 34:  -- font-family: "Courier New", monospace; keeps the list
     * "Courier New,monospace" -- quotes stripped, names joined by ",". */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: \"Courier New\", monospace; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "Courier New,monospace") == 0, "quoted font-family should have quotes stripped and the list kept");

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 35:  -- font-family: Verdana, Arial, sans-serif; (unquoted,
     * multiple names) keeps the whole fallback list for the font cache. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-family: Verdana, Arial, sans-serif; }");

        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT_MSG(strcmp(style.font_family, "Verdana,Arial,sans-serif") == 0, "the whole font-family list should be kept");

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
        sheet = parse_css_cstr("li { list-style-type: bogus(); }");
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
        TBOX_TEST_ASSERT(strcmp(parent.font_family, "Courier New,monospace") == 0);
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

    /* Logical border axis longhands (one or two values), overscroll-behavior,
     * hyphenate-character (inherited), scroll-margin/-padding and the legacy
     * clip: rect() as a clip-path inset on absolutely positioned boxes. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { border-block-width: 2px 6px; border-block-style: solid; border-inline-style: dashed dotted;"
            " border-inline-width: 3px; border-inline-color: red blue; border-left-width: 9px;"
            " overscroll-behavior: auto contain; hyphenate-character: \"=\"; scroll-padding: 1px 2px;"
            " scroll-margin-block-start: 4px; position: absolute; clip: rect(5px, 80px, auto, 10px); }"
            "p { overscroll-behavior-y: none; overscroll-behavior: auto; position: absolute; clip: rect(1px 2px 3px 4px); clip-path: circle(5px); }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.border_widths[0] == 2.0 && parent.border_widths[2] == 6.0);
        TBOX_TEST_ASSERT(parent.border_styles[0] == TBOX_STYLE_BORDER_STYLE_SOLID && parent.border_styles[2] == TBOX_STYLE_BORDER_STYLE_SOLID);
        TBOX_TEST_ASSERT(parent.border_styles[3] == TBOX_STYLE_BORDER_STYLE_DASHED && parent.border_styles[1] == TBOX_STYLE_BORDER_STYLE_DOTTED);
        TBOX_TEST_ASSERT(parent.border_widths[1] == 3.0 && parent.border_widths[3] == 9.0);
        TBOX_TEST_ASSERT(rgba_eq(parent.border_colors[3], (tbox_css_rgba){ 255, 0, 0, 255 }) && rgba_eq(parent.border_colors[1], (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(parent.overscroll_y == TBOX_STYLE_OVERSCROLL_CONTAIN && child.overscroll_y == TBOX_STYLE_OVERSCROLL_AUTO);
        TBOX_TEST_ASSERT(strcmp(parent.hyphenate_character, "=") == 0 && strcmp(child.hyphenate_character, "=") == 0);
        TBOX_TEST_ASSERT(parent.scroll_padding[0].value == 1.0 && parent.scroll_padding[3].value == 2.0);
        TBOX_TEST_ASSERT(parent.scroll_margin[0].value == 4.0 && parent.scroll_margin[2].value == 0.0 && child.scroll_padding[0].value == 0.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&parent)->kind == TBOX_STYLE_CLIP_PATH_INSET && tbox_style_clip_path_of(&parent)->inset[0].value == 5.0 && tbox_style_clip_path_of(&parent)->inset[3].value == 10.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&parent)->inset[1].kind == TBOX_STYLE_LENGTH_PERCENT && tbox_style_clip_path_of(&parent)->inset[1].value == 100.0 && tbox_style_clip_path_of(&parent)->inset[1].px_offset == -80.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&parent)->inset[2].kind == TBOX_STYLE_LENGTH_PERCENT && tbox_style_clip_path_of(&parent)->inset[2].value == 0.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&child)->kind == TBOX_STYLE_CLIP_PATH_CIRCLE);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip: rect(1px, 2px, 3px, 4px); hyphenate-character: auto; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&parent)->kind == TBOX_STYLE_CLIP_PATH_NONE && parent.hyphenate_character[0] == '\0');
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* isolation, text-justify, line-break (inherited), font-variant-position
     * (synthesized: shrinks once where it changes, moves as sub/super) and
     * `all`, which loses to later declarations of single properties. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p><span>x</span><b>y</b></p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->first_child;
        const tbox_html_node *b    = span->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { isolation: isolate; text-justify: none; line-break: anywhere; font-size: 24px; width: 50px; color: red; }"
            "p { font-variant-position: super; display: inline; margin-top: 7px; color: blue; all: initial; color: green; }"
            "span { display: inline; text-justify: inter-word; line-break: strict; }"
            "b { all: inherit; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style mid    = resolve_node(sheet, p, &parent);
        tbox_style child  = resolve_node(sheet, span, &mid);
        tbox_style bold   = resolve_node(sheet, b, &mid);
        TBOX_TEST_ASSERT(parent.isolation_isolate && !mid.isolation_isolate);
        TBOX_TEST_ASSERT(parent.text_justify_none && parent.line_break_anywhere);
        /* all: initial on p resets the inherited and its own values, but
         * the later color wins; font-variant-position is reset too. */
        TBOX_TEST_ASSERT(!mid.text_justify_none && !mid.line_break_anywhere && mid.margin[0].value == 0.0);
        TBOX_TEST_ASSERT(rgba_eq(mid.color, (tbox_css_rgba){ 0, 128, 0, 255 }));
        TBOX_TEST_ASSERT(mid.display == TBOX_STYLE_DISPLAY_INLINE && mid.font_variant_position == TBOX_STYLE_FONT_VARIANT_POSITION_NORMAL);
        TBOX_TEST_ASSERT(!child.text_justify_none && !child.line_break_anywhere);
        TBOX_TEST_ASSERT(bold.margin[0].value == mid.margin[0].value && rgba_eq(bold.color, mid.color));
        tbox_css_stylesheet_destroy(sheet);

        sheet = parse_css_cstr("div { font-size: 24px; } p { display: inline; font-variant-position: super; } span { display: inline; } b { display: inline-block; font-variant-position: sub; }");
        parent = resolve_node(sheet, div, NULL);
        mid    = resolve_node(sheet, p, &parent);
        child  = resolve_node(sheet, span, &mid);
        bold   = resolve_node(sheet, b, &mid);
        TBOX_TEST_ASSERT(mid.font_size == 20.0 && mid.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_SUPER);
        TBOX_TEST_ASSERT(child.font_size == 20.0 && child.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_SUPER); /* inherited, not compounded */
        TBOX_TEST_ASSERT(bold.font_variant_position == TBOX_STYLE_FONT_VARIANT_POSITION_SUB && bold.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BASELINE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* color-scheme picks light-dark()'s argument: light by default and with
     * `light dark`, dark with `dark` alone (inherited); nested and var()
     * forms; a malformed light-dark() makes the declaration unset. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p><span>x</span></p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { color-scheme: light dark; --fg: light-dark(rgb(1, 2, 3), rgb(4, 5, 6)); color: var(--fg); background-color: light-dark(white, black); }"
            "p { color-scheme: only dark; --fg: light-dark(rgb(1, 2, 3), rgb(4, 5, 6)); color: var(--fg); border-top: 1px solid light-dark(red, light-dark(lime, blue)); }"
            "span { color: light-dark(red); background-color: LIGHT-DARK(white, black); }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style mid    = resolve_node(sheet, p, &parent);
        tbox_style child  = resolve_node(sheet, span, &mid);
        TBOX_TEST_ASSERT(!parent.color_scheme_dark && mid.color_scheme_dark && child.color_scheme_dark);
        TBOX_TEST_ASSERT(rgba_eq(parent.color, (tbox_css_rgba){ 1, 2, 3, 255 }) && rgba_eq(parent.background_color, (tbox_css_rgba){ 255, 255, 255, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(mid.color, (tbox_css_rgba){ 4, 5, 6, 255 }) && rgba_eq(mid.border_colors[0], (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(rgba_eq(child.color, mid.color)); /* invalid: unset, inherits */
        TBOX_TEST_ASSERT(rgba_eq(child.background_color, (tbox_css_rgba){ 0, 0, 0, 255 }));
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* ::before/::after: synthetic entries (tag ::before/::after, parent the
     * element, one TEXT child) with the generated text -- strings with
     * escapes, attr(), counter()/counters() with scopes, quotes -- inline
     * by default; content none/normal and display: none subtrees generate
     * nothing; CSS2 :after works too; replaced elements get no boxes. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<div><p class=\"a\" data-k=\"v\">x</p>"
            "<ol><li>1<ol><li>a</li><li>b</li></ol></li><li>2</li></ol>"
            "<q>o<q>i</q></q><p class=\"n\">n</p><div style=\"display:none\"><span class=\"c\">h</span></div>"
            "<input class=\"a\"><span class=\"c\">z</span></div>");
        tbox_css_stylesheet *sheet = parse_css_cstr(
            ".a::before { content: \"\\2192 \" attr(data-k) \"!\"; color: red; }"
            ".a:after { content: \"\\\"\"; display: block; }"
            "ol { counter-reset: n; } li { counter-increment: n; } li::before { content: counters(n, \".\") \" \" counter(n, lower-roman); }"
            "q { quotes: \"<\" \">\" \"(\" \")\"; } q::before { content: open-quote; } q::after { content: close-quote; }"
            ".n::before { content: none; } .n::after { content: normal; }"
            ".c { counter-increment: hidden; } .c::after { content: counter(hidden); }");
        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const char *texts[16];
        int which[16];
        const tbox_style *styles[16];
        size_t generated = 0;
        for (size_t i = 0; i < table.count && generated < 16; i++) {
            if (table.items[i].style.generated == 0)
                continue;
            const tbox_html_node *node = table.items[i].node;
            static char buffers[16][32];
            const tbox_html_node *text = node->first_child;
            size_t n = text != NULL ? (text->text.text.size < 31 ? text->text.text.size : 31) : 0;
            if (n > 0) memcpy(buffers[generated], text->text.text.data, n);
            buffers[generated][n] = '\0';
            texts[generated]  = buffers[generated];
            which[generated]  = table.items[i].style.generated;
            styles[generated] = &table.items[i].style;
            TBOX_TEST_ASSERT(node->parent != NULL && node->parent->type == TBOX_HTML_NODE_ELEMENT && node->prev_sibling == NULL && node->next_sibling == NULL);
            generated++;
        }
        static const char *const expected[] = { "\xE2\x86\x92v!", "\"", "1 i", "1.1 i", "1.2 ii", "2 ii", "<", "(", ")", ">", "1" };
        TBOX_TEST_ASSERT(generated == sizeof(expected) / sizeof(expected[0]));
        for (size_t i = 0; i < generated && i < sizeof(expected) / sizeof(expected[0]); i++)
            TBOX_TEST_ASSERT_MSG(strcmp(texts[i], expected[i]) == 0, expected[i]);
        if (generated >= 2) {
            TBOX_TEST_ASSERT(which[0] == 1 && which[1] == 2);
            TBOX_TEST_ASSERT(styles[0]->display == TBOX_STYLE_DISPLAY_INLINE && rgba_eq(styles[0]->color, (tbox_css_rgba){ 255, 0, 0, 255 }));
            TBOX_TEST_ASSERT(styles[1]->display == TBOX_STYLE_DISPLAY_BLOCK);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* font-size-adjust: a number or `ex-height <number>`, inherited;
     * none/from-font turn it off; other metrics are ignored. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p><span>x</span></p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { font-size-adjust: 0.5; } p { font-size-adjust: cap-height 0.7; } span { font-size-adjust: from-font; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style mid    = resolve_node(sheet, p, &parent);
        tbox_style child  = resolve_node(sheet, span, &mid);
        TBOX_TEST_ASSERT(parent.font_size_adjust == 0.5 && mid.font_size_adjust == 0.5 && child.font_size_adjust == 0.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { font-size-adjust: ex-height 0.6; }");
        parent = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(parent.font_size_adjust == 0.6);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* font-synthesis: the shorthand lists what may be synthesized, the
     * longhands toggle one part; without position synthesis,
     * font-variant-position neither shrinks nor moves the text. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-size: 24px; font-synthesis: weight small-caps; }"
            "p { display: inline; font-variant-position: super; font-synthesis-small-caps: none; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(!parent.font_synthesis_no_small_caps && parent.font_synthesis_no_position);
        TBOX_TEST_ASSERT(child.font_synthesis_no_small_caps && child.font_synthesis_no_position);
        TBOX_TEST_ASSERT(child.font_size == 24.0 && child.vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BASELINE);
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { font-synthesis: none; } p { font-synthesis: bogus; }");
        parent = resolve_node(sheet, div, NULL);
        child  = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.font_synthesis_no_small_caps && parent.font_synthesis_no_position);
        TBOX_TEST_ASSERT(child.font_synthesis_no_small_caps && child.font_synthesis_no_position); /* invalid: inherited */
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
        tbox_style parent                = tbox_style_resolve_in_viewport(&style_arena, div, NULL, &computed, 800.0, 600.0);
        tbox_css_computed_style_destroy(&computed);
        computed         = tbox_css_cascade_resolve_stylesheet(sheet, p);
        tbox_style child = tbox_style_resolve_in_viewport(&style_arena, p, &parent, &computed, 800.0, 600.0);
        TBOX_TEST_ASSERT(parent.root_font_size == 20.0 && child.root_font_size == 20.0);
        TBOX_TEST_ASSERT(child.width.kind == TBOX_STYLE_LENGTH_PX && child.width.value == 40.0);
        TBOX_TEST_ASSERT(child.height.value == 96.0);
        TBOX_TEST_ASSERT(child.margin[0].value == 16.0 && child.margin[1].value == 16.0);
        TBOX_TEST_ASSERT(child.margin[2].value > 95.99 && child.margin[2].value < 96.01);
        TBOX_TEST_ASSERT(child.margin[3].value > 37.79 && child.margin[3].value < 37.8);
        TBOX_TEST_ASSERT(child.padding[3].value == 10.0 && child.padding[0].value == 20.0);
        TBOX_TEST_ASSERT(child.min_width.value == 400.0 && child.max_height.value == 60.0);
        /* Without a viewport, vw is invalid and falls back. */
        tbox_style no_viewport = tbox_style_resolve(&style_arena, p, &parent, &computed);
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
        /* transform functions keep their own slots (translateX is a translate) */
        const tbox_style_transform_function *t = parent.transforms;
        TBOX_TEST_ASSERT(t[0].kind == TBOX_STYLE_TRANSFORM_TRANSLATE && t[0].v[0] == 0.0 && t[0].v[1] == 10.0 && t[0].v[2] == 50.0 && t[0].v[3] == 0.0);
        TBOX_TEST_ASSERT(t[1].kind == TBOX_STYLE_TRANSFORM_TRANSLATE && t[1].v[1] == 5.0 && t[2].kind == TBOX_STYLE_TRANSFORM_NONE);
        TBOX_TEST_ASSERT(parent.translate_x.value == 0.0 && child.transforms[0].kind == TBOX_STYLE_TRANSFORM_NONE);
        TBOX_TEST_ASSERT(child.translate_x.value == 3.0 && child.translate_y.value == 0.0);
        double m[6];
        TBOX_TEST_ASSERT(tbox_style_transform_matrix(&parent, 0.0, 0.0, 100.0, 40.0, NULL, m) && m[0] == 1.0 && m[3] == 1.0 && m[4] == 15.0 && m[5] == 20.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* transform: 2D functions, flattened 3D ones, the individual
     * properties in CSS order, transform-origin, invalid values. */
    {
        tbox_html_document *doc   = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        struct {
            const char *css;
            double m[6];
        } cases[] = {
            { "div { transform: rotate(90deg); transform-origin: 0 0; }", { 0, 1, -1, 0, 0, 0 } },
            { "div { transform: rotate(90deg); }", { 0, 1, -1, 0, 70, -30 } }, /* around the center (50, 20) */
            { "div { transform: scale(2, .5); transform-origin: left top; }", { 2, 0, 0, 0.5, 0, 0 } },
            { "div { transform: scaleY(50%); transform-origin: 0 0 4px; }", { 1, 0, 0, 0.5, 0, 0 } },
            { "div { transform: skewX(45deg); transform-origin: 0 0; }", { 1, 0, 1, 1, 0, 0 } },
            { "div { transform: matrix(1, 2, 3, 4, 5, 6); transform-origin: 0 0; }", { 1, 2, 3, 4, 5, 6 } },
            { "div { transform: translate(10px) rotate(90deg); transform-origin: 0 0; }", { 0, 1, -1, 0, 10, 0 } },
            { "div { transform: rotateZ(90deg) translateX(10px); transform-origin: 0 0; }", { 0, 1, -1, 0, 0, 10 } },
            /* rotateX(60deg): y shrinks by cos 60 */
            { "div { transform: rotateX(60deg); transform-origin: 0 0; }", { 1, 0, 0, 0.5, 0, 0 } },
            { "div { transform: rotate3d(0, 0, 1, 90deg) perspective(100px) translateZ(9px); transform-origin: 0 0; }", { 0, 1, -1, 0, 0, 0 } },
            { "div { transform: matrix3d(2,0,0,0, 0,3,0,0, 0,0,1,0, 4,5,0,1); transform-origin: 0 0; }", { 2, 0, 0, 3, 4, 5 } },
            /* translate, rotate, scale, then transform */
            { "div { transform: translateX(1px); scale: 2; rotate: 90deg; translate: 10px 20px; transform-origin: 0 0; }", { 0, 2, -2, 0, 10, 22 } },
            { "div { rotate: z 90deg; transform-origin: 100% 0; }", { 0, 1, -1, 0, 100, -100 } },
            { "div { rotate: 1 0 0 60deg; transform-origin: 0 0; }", { 1, 0, 0, 0.5, 0, 0 } },
            { "div { transform: rotate(90deg) bogus(1); transform-origin: 0 0; }", { 1, 0, 0, 1, 0, 0 } },
            { "div { transform: scale(); }", { 1, 0, 0, 1, 0, 0 } },
        };
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            tbox_css_stylesheet *sheet = parse_css_cstr(cases[c].css);
            tbox_style style = resolve_node(sheet, div, NULL);
            double m[6];
            tbox_style_transform_matrix(&style, 0.0, 0.0, 100.0, 40.0, NULL, m);
            bool same = true;
            for (int i = 0; i < 6; i++)
                same = same && fabs(m[i] - cases[c].m[i]) < 1e-9;
            if (!same)
                fprintf(stderr, "transform case %zu: %g %g %g %g %g %g\n", c, m[0], m[1], m[2], m[3], m[4], m[5]);
            TBOX_TEST_ASSERT(same);
            tbox_css_stylesheet_destroy(sheet);
        }
        /* an animatable round trip of one slot and of rotate/scale */
        tbox_css_stylesheet *sheet = parse_css_cstr("div { transform: skew(10deg, 20deg); rotate: 0 1 0 5deg; scale: 3 4; }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_has_transform(&style));
        double v[TBOX_STYLE_ANIM_VALUE_SIZE], none[TBOX_STYLE_ANIM_VALUE_SIZE], mixed[TBOX_STYLE_ANIM_VALUE_SIZE];
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_TRANSFORM_0, v);
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_TRANSFORM_0 + 1, none);
        TBOX_TEST_ASSERT(v[0] == TBOX_STYLE_TRANSFORM_SKEW && v[1] == 10.0 && v[2] == 20.0 && none[0] == TBOX_STYLE_TRANSFORM_NONE);
        /* an empty slot blends from the identity of the other side */
        TBOX_TEST_ASSERT(tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_TRANSFORM_0, none, v));
        tbox_style_animatable_mix(TBOX_STYLE_ANIM_TRANSFORM_0, none, v, 0.5, mixed);
        TBOX_TEST_ASSERT(mixed[0] == TBOX_STYLE_TRANSFORM_SKEW && mixed[1] == 5.0 && mixed[2] == 10.0);
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_SCALE, v);
        TBOX_TEST_ASSERT(v[0] == 3.0 && v[1] == 4.0);
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_ROTATE, v);
        TBOX_TEST_ASSERT(v[1] == 1.0 && v[3] == 5.0);
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_TRANSFORM_ORIGIN_X, v);
        TBOX_TEST_ASSERT(v[0] == 50.0 && lround(v[2]) == TBOX_STYLE_LENGTH_PERCENT);
        tbox_css_stylesheet_destroy(sheet);
        /* rotate and scale differ: not interpolable across functions */
        sheet = parse_css_cstr("div { transform: rotate(10deg); }");
        style = resolve_node(sheet, div, NULL);
        double r[TBOX_STYLE_ANIM_VALUE_SIZE] = { TBOX_STYLE_TRANSFORM_SCALE, 2, 2, 0, 0, 0, 0, 0 };
        tbox_style_animatable_read(&style, TBOX_STYLE_ANIM_TRANSFORM_0, v);
        TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_TRANSFORM_0, v, r));
        TBOX_TEST_ASSERT(tbox_style_animatable_mask(tbox_string_view_from_cstr("transform")) == (0xFFull << TBOX_STYLE_ANIM_TRANSFORM_0));
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
    /* Strings longer than their fixed field are cut on a codepoint
     * boundary: "a" plus six 3-byte arrows does not fit
     * text_overflow_string[16], and a byte cut would leave half an arrow. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { text-overflow: \"a\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92\xe2\x86\x92\";"
            " hyphenate-character: \"\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\"; }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(strlen(style.text_overflow_string) == 13);
        TBOX_TEST_ASSERT(tbox_string_view_valid_utf8(tbox_string_view_from_cstr(style.text_overflow_string)));
        TBOX_TEST_ASSERT(strlen(style.hyphenate_character) == 14);
        TBOX_TEST_ASSERT(tbox_string_view_valid_utf8(tbox_string_view_from_cstr(style.hyphenate_character)));
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

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
        /* blur() makes it a layer filter: no op-by-op matrix */
        TBOX_TEST_ASSERT(!style.has_filter && tbox_style_filters_of(&style)->count == 2 && tbox_style_filters_of(&style)->ops[1].kind == TBOX_STYLE_FILTER_BLUR && tbox_style_filters_of(&style)->ops[1].radius == 2.0);
        TBOX_TEST_ASSERT(tbox_style_filters_of(&style)->ops[0].matrix[0] == -1.0 && tbox_style_filters_of(&style)->ops[0].matrix[4] == 1.0 && tbox_style_filters_of(&style)->ops[0].matrix[18] == 1.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_CIRCLE && tbox_style_clip_path_of(&style)->radius[0].value == 30.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->center[0].px_offset == 10.0 && tbox_style_clip_path_of(&style)->center[1].px_offset == 20.0);
        TBOX_TEST_ASSERT(style.background_layer_count == 3 && strcmp(style.background_image, "a.png") == 0 && style.background_size[0].value == 10.0);
        TBOX_TEST_ASSERT(tbox_style_background_layers_of(&style)->layers[1].image[0] == '\0' && tbox_style_background_layers_of(&style)->layers[1].gradient.kind == TBOX_STYLE_GRADIENT_NONE); /* color-only last layer */
        TBOX_TEST_ASSERT(tbox_style_background_layers_of(&style)->layers[0].gradient.kind == TBOX_STYLE_GRADIENT_LINEAR);
        TBOX_TEST_ASSERT(style.background_repeat_x && !style.background_repeat_y && tbox_style_background_layers_of(&style)->layers[0].repeat_x && !tbox_style_background_layers_of(&style)->layers[0].repeat_y); /* the list repeats */
        TBOX_TEST_ASSERT(rgba_eq(style.background_color, (tbox_css_rgba){ 255, 255, 255, 255 }));
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip-path: inset(5px 10% round 4px / 8px); filter: none; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_INSET && tbox_style_clip_path_of(&style)->inset[0].value == 5.0 && tbox_style_clip_path_of(&style)->inset[1].value == 10.0);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h[2] == 4.0 && tbox_style_clip_path_of(&style)->round_v[2] == 8.0 && !style.has_filter);
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

    /* Control parts: pseudo-elements resolved like a child of the control
     * (inheriting color, honoring var() and the control's own selectors),
     * kept only for the elements that have each part. */
    {
        tbox_html_document *doc = parse_html_cstr("<div class=\"dark\"><input type=\"RANGE\"><input type=\"range\" disabled>"
                                                  "<meter></meter><input><textarea></textarea><div></div></div>");
        const tbox_html_node *div      = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *range    = div->first_child;
        const tbox_html_node *disabled = range->next_sibling;
        const tbox_html_node *meter    = disabled->next_sibling;
        const tbox_html_node *text     = meter->next_sibling;
        const tbox_html_node *textarea = text->next_sibling;
        const tbox_html_node *plain    = textarea->next_sibling;
        tbox_css_stylesheet *sheet     = parse_css_cstr(
            ".dark { color: rgb(9 9 9); --thumb: rgb(200 30 120); }"
            ".dark input::slider-thumb { width: 18px; height: 10px; background-color: var(--thumb); border: 2px solid red; border-radius: 4px; }"
            "input[disabled]::slider-thumb { background-color: gray; }"
            "input::slider-track { border-top-left-radius: 3px; border-bottom-right-radius: 50%; }"
            "input::selection, textarea::selection { background-color: yellow; color: black; }"
            "div::slider-thumb { width: 99px; }");
        tbox_arena arena               = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table         = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *range_style  = tbox_style_table_find(&table, range);
        const tbox_style *off_style    = tbox_style_table_find(&table, disabled);
        const tbox_style *meter_style  = tbox_style_table_find(&table, meter);
        const tbox_style *text_style   = tbox_style_table_find(&table, text);
        const tbox_style *area_style   = tbox_style_table_find(&table, textarea);
        const tbox_style *plain_style  = tbox_style_table_find(&table, plain);
        TBOX_TEST_ASSERT(range_style != NULL && off_style != NULL && meter_style != NULL && text_style != NULL && area_style != NULL && plain_style != NULL);
        if (range_style != NULL && off_style != NULL && meter_style != NULL && text_style != NULL && area_style != NULL && plain_style != NULL) {
            const tbox_style_part_style *thumb = tbox_style_part_find(range_style, TBOX_STYLE_PART_SLIDER_THUMB);
            const tbox_style_part_style *track = tbox_style_part_find(range_style, TBOX_STYLE_PART_SLIDER_TRACK);
            const tbox_style_part_style *fill  = tbox_style_part_find(range_style, TBOX_STYLE_PART_SLIDER_FILL);
            TBOX_TEST_ASSERT(thumb != NULL && track != NULL && fill != NULL);
            if (thumb != NULL && track != NULL && fill != NULL) {
                TBOX_TEST_ASSERT(thumb->width.kind == TBOX_STYLE_LENGTH_PX && thumb->width.value == 18.0 && thumb->height.value == 10.0);
                TBOX_TEST_ASSERT(rgba_eq(thumb->background_color, (tbox_css_rgba){ 200, 30, 120, 255 }));
                TBOX_TEST_ASSERT(thumb->border_widths[0] == 2.0 && thumb->border_widths[3] == 2.0 && rgba_eq(thumb->border_colors[1], (tbox_css_rgba){ 255, 0, 0, 255 }));
                TBOX_TEST_ASSERT(thumb->border_radii[0] == 4.0 && thumb->border_radii[2] == 4.0 && thumb->border_radii_percent[0] == 0.0);
                TBOX_TEST_ASSERT(track->border_radii[0] == 3.0 && track->border_radii[1] == 0.0 && track->border_radii_percent[2] == 50.0);
                /* Unstyled: inherits color, transparent, no border, built-in size. */
                TBOX_TEST_ASSERT(rgba_eq(fill->color, (tbox_css_rgba){ 9, 9, 9, 255 }) && fill->background_color.a == 0);
                TBOX_TEST_ASSERT(fill->border_widths[0] == 0.0 && fill->width.kind == TBOX_STYLE_LENGTH_AUTO);
            }
            const tbox_style_part_style *off_thumb = tbox_style_part_find(off_style, TBOX_STYLE_PART_SLIDER_THUMB);
            TBOX_TEST_ASSERT(off_thumb != NULL && rgba_eq(off_thumb->background_color, (tbox_css_rgba){ 128, 128, 128, 255 }) && off_thumb->width.value == 18.0);
            TBOX_TEST_ASSERT(tbox_style_part_find(range_style, TBOX_STYLE_PART_SELECTION) == NULL);
            TBOX_TEST_ASSERT(tbox_style_part_find(meter_style, TBOX_STYLE_PART_SLIDER_FILL) != NULL && tbox_style_part_find(meter_style, TBOX_STYLE_PART_SLIDER_THUMB) == NULL);
            const tbox_style_part_style *selection = tbox_style_part_find(text_style, TBOX_STYLE_PART_SELECTION);
            TBOX_TEST_ASSERT(selection != NULL && rgba_eq(selection->background_color, (tbox_css_rgba){ 255, 255, 0, 255 }) && rgba_eq(selection->color, (tbox_css_rgba){ 0, 0, 0, 255 }));
            TBOX_TEST_ASSERT(tbox_style_part_find(text_style, TBOX_STYLE_PART_SLIDER_THUMB) == NULL);
            TBOX_TEST_ASSERT(tbox_style_part_find(area_style, TBOX_STYLE_PART_SELECTION) != NULL);
            for (int part = 0; part < TBOX_STYLE_PART_COUNT; part++)
                TBOX_TEST_ASSERT(tbox_style_part_find(plain_style, (tbox_style_part)part) == NULL);
            TBOX_TEST_ASSERT(tbox_style_part_find(range_style, TBOX_STYLE_PART_COUNT) == NULL && tbox_style_part_find(NULL, TBOX_STYLE_PART_SLIDER_THUMB) == NULL);
        }
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Small CSS additions reuse existing wrapping, positioning and radius paths. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p></p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { word-wrap: break-word; text-wrap-style: balance; object-position: right 10px bottom 20%;"
            " border-radius: 2px; border-start-start-radius: 12px 8px; border-end-end-radius: 6px;"
            " background: url(pixel-heart.png) 0 0 / 20px 20px space round; }"
            "p { overflow-wrap: normal; text-wrap-style: auto; border-top-left-radius: 4px;"
            " border-start-start-radius: 9px; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(parent.overflow_wrap_break_word && !parent.overflow_wrap_anywhere);
        TBOX_TEST_ASSERT(!child.overflow_wrap_break_word && !child.text_wrap_balance && parent.text_wrap_balance);
        TBOX_TEST_ASSERT(parent.object_position[0].kind == TBOX_STYLE_LENGTH_PERCENT && parent.object_position[0].value == 100.0 && parent.object_position[0].px_offset == -10.0);
        TBOX_TEST_ASSERT(parent.object_position[1].value == 80.0);
        TBOX_TEST_ASSERT(parent.border_radius_corners[0] == 12.0 && parent.border_radius_vertical[0] == 8.0 && parent.border_radius_corners[2] == 6.0);
        TBOX_TEST_ASSERT(child.border_radius_corners[0] == 9.0);
        TBOX_TEST_ASSERT(parent.background_repeat_mode_x == TBOX_STYLE_BACKGROUND_REPEAT_SPACE && parent.background_repeat_mode_y == TBOX_STYLE_BACKGROUND_REPEAT_ROUND);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Desktop viewport aliases, new cursor/marker values and background modes. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { width: 25dvw; height: 10svh; margin-left: 5lvmin; cursor: copy;"
            " background: url(tile.png) fixed no-repeat; scrollbar-gutter: stable; overflow-y: auto; }"
            "p { min-width: 20dvmax; padding-left: 10lvw; cursor: zoom-in;"
            " list-style-type: lower-greek; background-attachment: scroll; }");
        tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, div);
        tbox_style parent = tbox_style_resolve_in_viewport(&style_arena, div, NULL, &computed, 800.0, 600.0);
        tbox_css_computed_style_destroy(&computed);
        computed = tbox_css_cascade_resolve_stylesheet(sheet, p);
        tbox_style child = tbox_style_resolve_in_viewport(&style_arena, p, &parent, &computed, 800.0, 600.0);
        TBOX_TEST_ASSERT(parent.width.value == 200.0 && parent.height.value == 60.0 && parent.margin[3].value == 30.0);
        TBOX_TEST_ASSERT(child.min_width.value == 160.0 && child.padding[3].value == 80.0);
        TBOX_TEST_ASSERT(parent.cursor == TBOX_STYLE_CURSOR_COPY && child.cursor == TBOX_STYLE_CURSOR_ZOOM_IN);
        TBOX_TEST_ASSERT(child.list_style_type == TBOX_STYLE_LIST_STYLE_LOWER_GREEK);
        TBOX_TEST_ASSERT(parent.background_attachment_fixed && !child.background_attachment_fixed);
        TBOX_TEST_ASSERT(parent.scrollbar_gutter_stable && !child.scrollbar_gutter_stable);
        tbox_css_computed_style_destroy(&computed);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* transition lists: shorthand entries keep their order and timing;
     * longhands pair by index and repeat when shorter; easing functions. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div><p>y</p>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { transition: width .5s linear, border-color 1s steps(2, jump-start) 200ms, transform 2s cubic-bezier(0.1, 0.7, 1, 0.1); }"
            "p { transition-property: opacity, padding, bogus; transition-duration: 1s; transition-delay: 0s, 1s; transition-timing-function: step-end; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style b = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(a.transition_count == 3);
        const tbox_style_transition *width = tbox_style_transition_find(&a, TBOX_STYLE_ANIM_WIDTH);
        const tbox_style_transition *border = tbox_style_transition_find(&a, TBOX_STYLE_ANIM_BORDER_LEFT_COLOR);
        const tbox_style_transition *move = tbox_style_transition_find(&a, TBOX_STYLE_ANIM_TRANSFORM_0);
        TBOX_TEST_ASSERT(width != NULL && width->duration == 0.5 && tbox_style_easing_apply(&width->easing, 0.3) == 0.3);
        TBOX_TEST_ASSERT(border != NULL && border->delay == 0.2 && border->easing.steps && tbox_style_easing_apply(&border->easing, 0.0) == 0.5);
        TBOX_TEST_ASSERT(move != NULL && move->duration == 2.0 && move->easing.x2 == 1.0 && move->easing.y1 == 0.7);
        TBOX_TEST_ASSERT(tbox_style_transition_find(&a, TBOX_STYLE_ANIM_COLOR) == NULL);
        TBOX_TEST_ASSERT(b.transition_count == 3);
        const tbox_style_transition *opacity = tbox_style_transition_find(&b, TBOX_STYLE_ANIM_OPACITY);
        const tbox_style_transition *padding = tbox_style_transition_find(&b, TBOX_STYLE_ANIM_PADDING_RIGHT);
        TBOX_TEST_ASSERT(opacity != NULL && opacity->delay == 0.0 && padding != NULL && padding->delay == 1.0 && padding->duration == 1.0);
        TBOX_TEST_ASSERT(b.transitions[2].properties == 0 && b.transitions[2].delay == 0.0);
        TBOX_TEST_ASSERT(tbox_style_easing_apply(&opacity->easing, 0.99) == 0.0 && tbox_style_easing_apply(&opacity->easing, 1.0) == 1.0);
        tbox_style_easing ease = tbox_style_easing_default();
        TBOX_TEST_ASSERT(fabs(tbox_style_easing_apply(&ease, 0.5) - 0.8024) < 0.002);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* filter as an ordered list: consecutive color functions fold into one
     * matrix, blur()/drop-shadow() keep their place and need a layer (no
     * op-by-op matrix then); a color-only filter keeps has_filter. The
     * backdrop list drops drop-shadow(). mix-blend-mode and mask-*. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div><p>y</p><i>z</i>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->next_sibling, *i = p->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { background: url(a.png), linear-gradient(red, blue), white; background-blend-mode: multiply, screen; box-shadow: 1px 1px red;"
            " filter: grayscale(1) invert(1) drop-shadow(2px 3px 4px rgb(0, 0, 255)) blur(3px) sepia(1); mix-blend-mode: multiply; }"
            "p { color: rgb(0, 128, 0); filter: brightness(2); backdrop-filter: blur(4px) drop-shadow(1px 1px red) grayscale(1);"
            " -webkit-mask: linear-gradient(black, transparent) no-repeat 0 0 / 50% 100% luminance; }"
            "i { mask-image: url(m.png); mask-size: cover; opacity: 0.5; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style b = resolve_node(sheet, p, NULL);
        tbox_style c = resolve_node(sheet, i, NULL);
        TBOX_TEST_ASSERT(tbox_style_filters_of(&a)->count == 4 && tbox_style_filters_of(&a)->spreads && !a.has_filter && a.box_shadow_count == 1);
        TBOX_TEST_ASSERT(tbox_style_filters_of(&a)->ops[0].kind == TBOX_STYLE_FILTER_MATRIX && tbox_style_filters_of(&a)->ops[1].kind == TBOX_STYLE_FILTER_DROP_SHADOW && tbox_style_filters_of(&a)->ops[2].kind == TBOX_STYLE_FILTER_BLUR && tbox_style_filters_of(&a)->ops[3].kind == TBOX_STYLE_FILTER_MATRIX);
        TBOX_TEST_ASSERT(tbox_style_filters_of(&a)->ops[1].shadow.offset_y == 3.0 && tbox_style_filters_of(&a)->ops[1].shadow.color.b == 255 && tbox_style_filters_of(&a)->ops[2].radius == 3.0);
        /* 3 * 3px of blur + 3px offset + half of the 4px shadow blur * 3 */
        TBOX_TEST_ASSERT(tbox_style_filter_extent(tbox_style_filters_of(&a)) == 18.0 && tbox_style_needs_layer(&a) && a.mix_blend_mode == TBOX_STYLE_BLEND_MULTIPLY);
        /* three layers (the last is the color one): the list repeats */
        TBOX_TEST_ASSERT(a.background_layer_count == 3 && a.background_blend_mode == TBOX_STYLE_BLEND_MULTIPLY && tbox_style_background_layers_of(&a)->layers[0].blend_mode == TBOX_STYLE_BLEND_SCREEN && tbox_style_background_layers_of(&a)->layers[1].blend_mode == TBOX_STYLE_BLEND_MULTIPLY);
        TBOX_TEST_ASSERT(b.has_filter && !tbox_style_filters_of(&b)->spreads && b.filter_matrix[0] == 2.0 && !a.has_backdrop_filter);
        TBOX_TEST_ASSERT(b.has_backdrop_filter && tbox_style_backdrop_filters_of(&b)->count == 2 && tbox_style_backdrop_filters_of(&b)->ops[0].kind == TBOX_STYLE_FILTER_BLUR && fabs(tbox_style_backdrop_filters_of(&b)->ops[1].matrix[0] - 0.2126) < 1e-6);
        TBOX_TEST_ASSERT(b.has_mask && b.mask_luminance && tbox_style_masks_of(&b)->layers[0].source.gradient.kind == TBOX_STYLE_GRADIENT_LINEAR && !tbox_style_masks_of(&b)->layers[0].source.repeat_x && tbox_style_masks_of(&b)->layers[0].source.size[0].value == 50.0 && tbox_style_needs_layer(&b));
        TBOX_TEST_ASSERT(c.has_mask && !c.mask_luminance && strcmp(tbox_style_masks_of(&c)->layers[0].source.image, "m.png") == 0 && tbox_style_masks_of(&c)->layers[0].source.size_kind == TBOX_STYLE_BACKGROUND_SIZE_COVER);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* border-image: the shorthand's parts in any order (slice / width /
     * outset), longhands over it by cascade order, `border` resetting it,
     * invalid longhand values ignored, and the initial values. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div><p>y</p><i>z</i><b>w</b><s>v</s>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->next_sibling, *i = p->next_sibling, *b = i->next_sibling, *st = b->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { border: 10px solid; border-image: url(f.png) 30 40% fill / 2 10px auto / 5px 1 round space; }"
            "p { border-image: linear-gradient(red, blue) 10; border-image-repeat: repeat; border-image-width: 20%; }"
            "i { border-image: url(x.png) 10; border: 2px solid red; }"
            "b { border-image-source: url(z.png); border-image-slice: -3; border-image-outset: 10%; border-image-repeat: stretch round; }"
            "s { border-image: url(x.png) 10 / / 3px; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style c = resolve_node(sheet, p, NULL);
        tbox_style d = resolve_node(sheet, i, NULL);
        tbox_style e = resolve_node(sheet, b, NULL);
        tbox_style f = resolve_node(sheet, st, NULL);
        const tbox_style_border_image *bi = tbox_style_border_image_of(&a);
        TBOX_TEST_ASSERT(a.has_border_image && strcmp(bi->image, "f.png") == 0 && bi->fill);
        TBOX_TEST_ASSERT(bi->slice[0].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER && bi->slice[0].number == 30.0 && bi->slice[2].number == 30.0);
        TBOX_TEST_ASSERT(bi->slice[1].kind == TBOX_STYLE_BORDER_IMAGE_LENGTH && bi->slice[1].length.kind == TBOX_STYLE_LENGTH_PERCENT && bi->slice[3].length.value == 40.0);
        TBOX_TEST_ASSERT(bi->width[0].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER && bi->width[0].number == 2.0 && bi->width[1].length.kind == TBOX_STYLE_LENGTH_PX && bi->width[1].length.value == 10.0);
        TBOX_TEST_ASSERT(bi->width[2].kind == TBOX_STYLE_BORDER_IMAGE_AUTO && bi->width[3].length.value == 10.0);
        TBOX_TEST_ASSERT(bi->outset[0].kind == TBOX_STYLE_BORDER_IMAGE_LENGTH && bi->outset[0].length.value == 5.0 && bi->outset[1].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER && bi->outset[3].number == 1.0);
        TBOX_TEST_ASSERT(bi->repeat[0] == TBOX_STYLE_BORDER_IMAGE_ROUND && bi->repeat[1] == TBOX_STYLE_BORDER_IMAGE_SPACE && a.border_widths[0] == 10.0);
        bi = tbox_style_border_image_of(&c);
        TBOX_TEST_ASSERT(c.has_border_image && bi->image[0] == '\0' && bi->gradient.kind == TBOX_STYLE_GRADIENT_LINEAR && bi->slice[3].number == 10.0 && !bi->fill);
        TBOX_TEST_ASSERT(bi->repeat[0] == TBOX_STYLE_BORDER_IMAGE_REPEAT && bi->repeat[1] == TBOX_STYLE_BORDER_IMAGE_REPEAT && bi->width[2].length.kind == TBOX_STYLE_LENGTH_PERCENT);
        TBOX_TEST_ASSERT(!d.has_border_image && tbox_style_border_image_of(&d)->slice[0].length.value == 100.0);
        bi = tbox_style_border_image_of(&e);
        TBOX_TEST_ASSERT(e.has_border_image && strcmp(bi->image, "z.png") == 0 && bi->slice[0].kind == TBOX_STYLE_BORDER_IMAGE_LENGTH && bi->slice[0].length.value == 100.0);
        TBOX_TEST_ASSERT(bi->outset[2].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER && bi->outset[2].number == 0.0 && bi->width[1].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER && bi->width[1].number == 1.0);
        TBOX_TEST_ASSERT(bi->repeat[0] == TBOX_STYLE_BORDER_IMAGE_STRETCH && bi->repeat[1] == TBOX_STYLE_BORDER_IMAGE_ROUND);
        /* `10 / / 3px`: an empty width keeps its initial value */
        TBOX_TEST_ASSERT(strcmp(tbox_style_border_image_of(&f)->image, "x.png") == 0 && tbox_style_border_image_of(&f)->outset[1].length.value == 3.0 && tbox_style_border_image_of(&f)->width[0].number == 1.0);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* animation: shorthand keywords in any order; longhands pair with
     * animation-name and repeat; `none` entries are dropped. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div><p>y</p>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { animation: 2s infinite alternate-reverse spin ease-in 500ms both paused, none, fade 1s; }"
            "p { animation-name: a, b, c; animation-duration: 1s, 2s; animation-iteration-count: 3; animation-fill-mode: forwards; animation-direction: reverse, alternate; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style b = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(a.animation_count == 2);
        const tbox_style_animation *spin = &a.animations[0];
        TBOX_TEST_ASSERT(strcmp(spin->name, "spin") == 0 && spin->duration == 2.0 && spin->delay == 0.5 && isinf(spin->iterations));
        TBOX_TEST_ASSERT(spin->direction == TBOX_STYLE_ANIMATION_ALTERNATE_REVERSE && spin->fill_forwards && spin->fill_backwards && spin->paused && spin->easing.x1 == 0.42);
        TBOX_TEST_ASSERT(strcmp(a.animations[1].name, "fade") == 0 && a.animations[1].iterations == 1.0 && !a.animations[1].paused);
        TBOX_TEST_ASSERT(b.animation_count == 3 && b.animations[2].duration == 1.0 && b.animations[1].duration == 2.0 && b.animations[2].iterations == 3.0);
        TBOX_TEST_ASSERT(b.animations[0].fill_forwards && !b.animations[0].fill_backwards && b.animations[2].direction == TBOX_STYLE_ANIMATION_REVERSE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* direction: rtl maps inline-start to the right side (edges, borders,
     * corners), makes the initial text-align right, and a child's `dir`
     * attribute or another direction resolves start/end again. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p dir='ltr'>x</p><span>y</span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child, *span = p->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { direction: rtl; margin-inline-start: 5px; padding-inline: 1px 2px; border-inline-start: 3px solid red; border-start-start-radius: 7px; inset-inline-end: 4px; }"
            "span { text-align: end; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style b = resolve_node(sheet, p, &a);
        tbox_style c = resolve_node(sheet, span, &a);
        TBOX_TEST_ASSERT(a.direction_rtl && a.margin[1].value == 5.0 && a.margin[3].value == 0.0);
        TBOX_TEST_ASSERT(a.padding[1].value == 1.0 && a.padding[3].value == 2.0 && a.offset[3].value == 4.0);
        TBOX_TEST_ASSERT(tbox_style_border_side_width(&a, 1) == 3.0 && tbox_style_border_side_width(&a, 3) == 0.0);
        TBOX_TEST_ASSERT(a.border_radius_corners[1] == 7.0 && a.border_radius_corners[0] == 0.0);
        TBOX_TEST_ASSERT(a.text_align == TBOX_STYLE_TEXT_ALIGN_RIGHT);
        TBOX_TEST_ASSERT(!b.direction_rtl && b.text_align == TBOX_STYLE_TEXT_ALIGN_LEFT);
        TBOX_TEST_ASSERT(c.direction_rtl && c.text_align == TBOX_STYLE_TEXT_ALIGN_LEFT);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* unicode-bidi: every keyword, invalid ignored, not inherited; the
     * HTML defaults below CSS (bdo overrides, bdi and dir=auto plaintext,
     * dir isolates); `all` leaves direction and unicode-bidi alone. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><bdo dir='rtl'>a</bdo><bdo>b</bdo><bdi>c</bdi><span dir='auto'>d</span><span dir='rtl'>e</span><em dir='rtl'>f</em><i>g</i><b>h</b><u>i</u><s>j</s></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *n[10];
        n[0] = div->first_child;
        for (size_t i = 1; i < 10; i++)
            n[i] = n[i - 1]->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { unicode-bidi: bidi-override; direction: rtl; } em { unicode-bidi: normal; }"
            "i { unicode-bidi: embed; } b { unicode-bidi: isolate-override; } u { unicode-bidi: bogus; } s { all: initial; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style st[10];
        for (size_t i = 0; i < 10; i++)
            st[i] = resolve_node(sheet, n[i], &parent);
        TBOX_TEST_ASSERT(parent.unicode_bidi == TBOX_STYLE_UNICODE_BIDI_BIDI_OVERRIDE);
        TBOX_TEST_ASSERT(st[0].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_ISOLATE_OVERRIDE && st[0].direction_rtl);
        TBOX_TEST_ASSERT(st[1].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_BIDI_OVERRIDE);
        TBOX_TEST_ASSERT(st[2].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_PLAINTEXT);
        TBOX_TEST_ASSERT(st[3].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_PLAINTEXT);
        TBOX_TEST_ASSERT(st[4].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_ISOLATE);
        TBOX_TEST_ASSERT(st[5].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_NORMAL); /* CSS beats dir */
        TBOX_TEST_ASSERT(st[6].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_EMBED);
        TBOX_TEST_ASSERT(st[7].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_ISOLATE_OVERRIDE);
        TBOX_TEST_ASSERT(st[8].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_NORMAL); /* invalid, not inherited */
        TBOX_TEST_ASSERT(st[9].direction_rtl && st[9].unicode_bidi == TBOX_STYLE_UNICODE_BIDI_NORMAL);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* text-emphasis: the shorthand in either order, a lone open/filled is
     * a dot, strings, the position, inheritance and `none`. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p><i>y</i><b>z</b></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child, *i = p->next_sibling, *b = i->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { text-emphasis: rgb(0, 0, 255) open triangle; text-emphasis-position: under right; }"
            "p { text-emphasis-style: open; }"
            "i { text-emphasis: '*' ; text-emphasis-color: currentColor; }"
            "b { text-emphasis-style: none; }");
        tbox_style a = resolve_node(sheet, div, NULL);
        tbox_style pp = resolve_node(sheet, p, &a);
        tbox_style ii = resolve_node(sheet, i, &a);
        tbox_style bb = resolve_node(sheet, b, &a);
        TBOX_TEST_ASSERT(a.text_emphasis_mark == TBOX_STYLE_EMPHASIS_TRIANGLE && a.text_emphasis_open && a.text_emphasis_color.b == 255 && a.text_emphasis_under);
        TBOX_TEST_ASSERT(pp.text_emphasis_mark == TBOX_STYLE_EMPHASIS_DOT && pp.text_emphasis_open && pp.text_emphasis_color.b == 255 && pp.text_emphasis_under);
        TBOX_TEST_ASSERT(ii.text_emphasis_mark == TBOX_STYLE_EMPHASIS_STRING && strcmp(ii.text_emphasis_string, "*") == 0 && ii.text_emphasis_color.a == 0);
        TBOX_TEST_ASSERT(bb.text_emphasis_mark == TBOX_STYLE_EMPHASIS_NONE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Percentage comparisons remain responsive to the containing block,
     * including singleton and nested compatible affine expressions. */
    {
        static const struct { const char *value; double small, large; } cases[] = {
            { "min(20%, 50%)", 20, 80 }, { "max(20%, 50%)", 50, 200 },
            { "clamp(20%, 50%, 80%)", 50, 200 }, { "clamp(80%, 50%, 20%)", 80, 320 },
            { "min(50%)", 50, 200 }, { "max(50%)", 50, 200 },
            { "min(0%, 50%)", 0, 0 }, { "max(0%, 50%)", 50, 200 },
            { "min(calc(20% + 10px), calc(50% + 10px))", 30, 90 },
            { "max(calc(20% + 10px), calc(20% + 30px))", 50, 110 },
            { "min(max(20%, 40%), 60%)", 40, 160 },
            { "min(min(50%, 100px))", 50, 100 },
            { "clamp(10px, 50%, 100px)", 50, 100 },
            { "max(50%, 10px)", 50, 200 }, { "min(10px, 20px)", 10, 10 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { width: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.width.kind != TBOX_STYLE_LENGTH_AUTO, cases[i].value);
            TBOX_TEST_ASSERT_MSG(tbox_style_length_resolve(style.width, 100) == cases[i].small, cases[i].value);
            TBOX_TEST_ASSERT_MSG(tbox_style_length_resolve(style.width, 400) == cases[i].large, cases[i].value);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* xywh uses the same resolved rectangle as inset, preserving px/%
     * offsets and both axes of rounded corners. Invalid calls stay unset. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { clip-path: xywh(10% 5px calc(50% - 4px) 30px round 10% 20% / 3px 4px); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_INSET);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->inset[0], 100) == 5);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->inset[3], 200) == 20);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->inset[1], 200) == 84);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->inset[2], 100) == 65);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h_percent[0] == 10 && tbox_style_clip_path_of(&style)->round_h_percent[1] == 20);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_v[0] == 3 && tbox_style_clip_path_of(&style)->round_v[1] == 4);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip-path: inset(0 round 10% 20% 30% 40% / 5% 6% 7% 8%); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h_percent[3] == 40 && tbox_style_clip_path_of(&style)->round_v_percent[3] == 8);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip-path: inset(0 round calc(10% + 2px) / calc(20% + 3px)); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h[0] == 2 && tbox_style_clip_path_of(&style)->round_h_percent[0] == 10);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_v[0] == 3 && tbox_style_clip_path_of(&style)->round_v_percent[0] == 20);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { clip-path: xywh(-10px -20px 0 0 round 25%); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_INSET);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h_percent[2] == 25 && tbox_style_clip_path_of(&style)->round_v_percent[2] == 25);
        tbox_css_stylesheet_destroy(sheet);
        static const char *const invalid[] = {
            "xywh(0 0 -1px 10px)", "xywh(0 0 10px -1%)", "xywh(0 0 10px)",
            "xywh(0, 0, 10px, 10px)", "xywh(auto 0 10px 10px)",
            "xywh(0 0 10px 10px round)", "xywh(0 0 10px 10px round -1%)",
            "xywh(0 0 10px 10px) padding-box", "inset(0 round 10% /)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { clip-path: %s; }", invalid[i]);
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Numeric exponential functions compose with lengths, percentages,
     * variables and nested arithmetic without adding layout expressions. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "calc(pow(2, 3) * 10px)", 80 }, { "calc(pow(-2, 3) * 1px)", -8 },
            { "calc(pow(0, 0) * 10px)", 10 }, { "calc(pow(4, -1) * 100px)", 25 },
            { "calc(sqrt(9) * 10px)", 30 }, { "calc(exp(0) * 10px)", 10 },
            { "calc(log(8, 2) * 10px)", 30 }, { "calc(log(1) * 10px)", 0 },
            { "calc(log(exp(2)) * 10px)", 20 }, { "calc(sqrt(pow(3, 2) + pow(4, 2)) * 10px)", 50 },
            { "calc(pow(1 + 1, 2 * 2) * 1em)", 256 },
            { "min(calc(sqrt(16) * 10px), 60px)", 40 },
            { "calc(exp(-1000) * 10px)", 0 }, { "calc(log(4, 0.5) * 10px)", -20 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { margin-left: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.margin[3].kind == TBOX_STYLE_LENGTH_PX && fabs(style.margin[3].value - cases[i].expected) < 1e-9, cases[i].value);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr("div { --n: 4; width: calc(sqrt(var(--n)) * 10%); color: color(srgb 1 0 0); background: linear-gradient(color(srgb-linear .5 0 0), color(srgb 0 0 1)); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_PERCENT && tbox_style_length_resolve(style.width, 200) == 40);
        TBOX_TEST_ASSERT(style.color.r == 255 && style.color.g == 0);
        TBOX_TEST_ASSERT(style.background_gradient.stop_count == 2 && style.background_gradient.stops[0].color.r == 188);
        tbox_css_stylesheet_destroy(sheet);
        static const char *const invalid[] = {
            "calc(pow(2) * 1px)", "calc(pow(2, 3, 4) * 1px)", "calc(pow(-2, 0.5) * 1px)",
            "calc(pow(0, -1) * 1px)", "calc(sqrt(-1) * 1px)", "calc(sqrt(4, 2) * 1px)",
            "calc(sqrt(4px) * 1px)", "calc(sqrt(25%) * 1px)", "calc(exp() * 1px)",
            "calc(exp(1000) * 1px)", "calc(log(0) * 1px)", "calc(log(-1) * 1px)",
            "calc(log(4, 0) * 1px)", "calc(log(4, 1) * 1px)", "calc(log(4, -2) * 1px)",
            "calc(pow(2, 3,) * 1px)", "calc(exp(1e999) * 1px)", "sqrt(4)",
            "calc(sqrt(4) * 1e308px * 1e308)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { width: %s; }", invalid[i]);
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.width.kind == TBOX_STYLE_LENGTH_AUTO, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Modern rect() is top/right/bottom/left coordinates with round,
     * distinct from the legacy comma syntax accepted by clip. */
    {
        static const struct { const char *value; double edges[4]; } cases[] = {
            { "rect(10px 150px 80px 20px round 10% / 20%)", { 10, 50, 20, 20 } },
            { "rect(auto auto auto auto)", { 0, 0, 0, 0 } },
            { "rect(10% 80% 90% 20%)", { 10, 40, 10, 40 } },
            { "rect(0 calc(100% - 10px) calc(100% - 5px) 0)", { 0, 10, 5, 0 } },
            { "rect(-10px auto auto -20px)", { -10, 0, 0, -20 } },
            { "rect(10px 0 0 20px)", { 10, 200, 100, 20 } },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { clip-path: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_INSET, cases[i].value);
            for (size_t side = 0; side < 4; side++)
                TBOX_TEST_ASSERT_MSG(fabs(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->inset[side], side % 2 ? 200 : 100) - cases[i].edges[side]) < 1e-9, cases[i].value);
            if (i == 0) TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->round_h_percent[0] == 10 && tbox_style_clip_path_of(&style)->round_v_percent[0] == 20);
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "rect(0, 100px, 100px, 0)", "rect(0 100px 100px)", "rect(0 auto auto 0 0)",
            "rect(0 auto auto 0 round)", "rect(0 auto auto 0 round -1px)",
            "rect(0 auto auto 0) padding-box", "rect(0 bogus auto 0)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { clip-path: %s; }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Stepped values preserve units and distinguish divisor/dividend
     * signs; nearest ties go toward positive infinity, not away from zero. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "calc(hypot(3, 4) * 10px)", 50 }, { "calc(hypot(-3) * 1px)", 3 },
            { "calc(hypot(1, 2, 2, 0, 0, 0, 0, 0) * 1px)", 3 },
            { "calc(hypot(1e200, 1e200) * 1px)", 1.414213562373095e200 },
            { "calc(hypot(sqrt(9), pow(2, 2)) * 1px)", 5 },
            { "mod(18px, 5px)", 3 }, { "mod(-18px, 5px)", 2 },
            { "mod(18px, -5px)", -2 }, { "mod(-18px, -5px)", -3 },
            { "rem(18px, 5px)", 3 }, { "rem(-18px, 5px)", -3 },
            { "rem(18px, -5px)", 3 }, { "rem(-18px, -5px)", -3 },
            { "mod(1em, 6px)", 4 }, { "rem(10rem, 3rem)", 16 },
            { "calc(mod(-18, 5) * 10px)", 20 }, { "calc(rem(-18, 5) * 10px)", -30 },
            { "mod(10px, -5px)", 0 }, { "rem(-10px, 5px)", 0 },
            { "round(123px, 10px)", 120 }, { "round(up, 123px, 10px)", 130 },
            { "round(down, 123px, 10px)", 120 }, { "round(to-zero, 123px, 10px)", 120 },
            { "round(nearest, 125px, 10px)", 130 }, { "round(nearest, -125px, 10px)", -120 },
            { "round(up, -123px, 10px)", -120 }, { "round(down, -123px, 10px)", -130 },
            { "round(to-zero, -123px, 10px)", -120 }, { "round(123px, -10px)", 120 },
            { "round(2em, 10px)", 30 }, { "calc(round(2.5) * 10px)", 30 },
            { "calc(round(-2.5) * 10px)", -20 }, { "calc(round(up, 2.1) * 10px)", 30 },
            { "round(120px, 10px)", 120 }, { "round(up, 120px, 10px)", 120 },
            { "calc(round(to-zero, -0.1) * 10px)", 0 },
            { "round(up, -1e-200px, 1e200px)", 0 },
            { "min(round(123px, 10px), 140px)", 120 },
            { "calc(round(up, mod(17px, 10px), 4px) + rem(18px, 5px))", 11 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { margin-left: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.margin[3].kind == TBOX_STYLE_LENGTH_PX &&
                fabs(style.margin[3].value - cases[i].expected) / fmax(1.0, fabs(cases[i].expected)) < 1e-12, cases[i].value);
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "calc(hypot() * 1px)", "hypot(3px, 4)", "calc(hypot(3, 4px) * 1px)",
            "calc(hypot(3, 4,) * 1px)", "calc(hypot(1,2,3,4,5,6,7,8,9) * 1px)",
            "calc(hypot(1e309, 1) * 1px)", "calc(log(2, 3, 4) * 1px)",
            "round(10px)", "round(10px, 0px)", "round(bogus, 10px, 2px)",
            "round(up 10px, 2px)", "round(10px, 2)", "round(10px, 2px, 3px)",
            "round(10%, 2px)", "round(10px, 2%)", "round(clamp(1px, 50%, 20px), 2px)",
            "mod(10px, 0px)", "rem(10px, 0px)", "mod(10px)", "rem(10px, 2px, 1px)",
            "mod(10px, 2)", "rem(10%, 2px)", "mod(10px, 1e999px)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { width: %s; }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.width.kind == TBOX_STYLE_LENGTH_AUTO, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Unary functions preserve length types, sign returns a number,
     * hypot converts compatible units, and constants are calculation-only. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "abs(-12px)", 12 }, { "abs(2em - 40px)", 8 },
            { "calc(abs(-2) * 10px)", 20 }, { "calc(sign(-2px) * 10px)", -10 },
            { "calc(sign(2em - 40px) * 10px)", -10 }, { "calc(sign(0) * 10px)", 0 },
            { "calc(sign(2) * abs(-5px))", 5 }, { "hypot(30px, 40px)", 50 },
            { "hypot(-30px, 40px, 120px)", 130 }, { "hypot(3em, 64px)", 80 },
            { "hypot(0.5in, -4em)", 80 }, { "hypot(abs(-30px), 40px)", 50 },
            { "calc(pi * 10px)", 31.41592653589793 },
            { "calc(E * 10px)", 27.18281828459045 },
            { "calc(log(e) * 10px)", 10 }, { "calc(pow(pi, 0) * 10px)", 10 },
            { "max(abs(-12px), hypot(3px, 4px))", 12 },
            { "abs(-0px)", 0 }, { "calc(sign(-0px) * 1px)", 0 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { margin-left: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.margin[3].kind == TBOX_STYLE_LENGTH_PX && fabs(style.margin[3].value - cases[i].expected) < 1e-9, cases[i].value);
            if (strcmp(cases[i].value, "abs(-0px)") == 0) TBOX_TEST_ASSERT(!signbit(style.margin[3].value));
            if (strcmp(cases[i].value, "calc(sign(-0px) * 1px)") == 0) TBOX_TEST_ASSERT(signbit(style.margin[3].value));
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "abs()", "abs(1px, 2px)", "sign(1px)", "abs(-25%)", "hypot(3px, 4)",
            "hypot(3px, 4%)", "hypot(clamp(1px, 50%, 100px), 4px)", "abs(1e999px)",
            "pi", "e", "calc(pie * 1px)", "calc(2pi * 1px)", "calc(sign(2deg) * 1px)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { width: %s; }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.width.kind == TBOX_STYLE_LENGTH_AUTO, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Opacity shares the evaluator with a number-percentage context;
     * dimensions cannot leak into its result, and clamping is final. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "calc(pow(.5, 2))", .25 }, { "sqrt(.25)", .5 },
            { "calc(50%)", .5 }, { "calc(50% + .25)", .75 },
            { "calc(100% - 25%)", .75 }, { "min(75%, .5)", .5 },
            { "clamp(25%, 75%, .5)", .5 }, { "abs(-50%)", .5 },
            { "calc(1 + sign(-2px) * .25)", .75 },
            { "calc(pi - 3)", .141592653589793 }, { "calc(e - 2)", .718281828459045 },
            { "hypot(.3, .4)", .5 }, { "round(.46, .1)", .5 },
            { "mod(.7, .5)", .2 }, { "rem(.7, .5)", .2 },
            { "calc(150% - 75%)", .75 }, { "calc(2 - 1.5)", .5 },
            { "calc(2)", 1 }, { "calc(-2)", 0 },
            { "50%", .5 }, { "-25%", 0 }, { "150%", 1 },
        };
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { opacity: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(isfinite(style.opacity) && fabs(style.opacity - cases[i].expected) < 1e-9, cases[i].value);
            tbox_style child = resolve_node(sheet, div->first_child, &style);
            TBOX_TEST_ASSERT(child.opacity == 1.0); /* opacity is not inherited */
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr("div { --fade: .5; opacity: calc(pow(var(--fade), 2)); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.opacity == .25);
        tbox_css_stylesheet_destroy(sheet);
        static const char *const invalid[] = {
            "calc(1px)", "calc(1px - 1px)", "calc(1px + .5)", "hypot(3px, 4px)",
            "sqrt(-1)", "calc(1 / 0)", "calc(pi * 1e999)", "calc(sign(25deg))",
            "calc(abs())", "calc(.5) extra", "e", "pi", "NaN", "1e999", "",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { opacity: %s; }", invalid[i]);
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.opacity == 1.0, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Radian expressions and literal angles share numeric trig results. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "calc(sin(pi / 2) * 100px)", 100 }, { "calc(cos(pi) * 10px)", -10 },
            { "calc(tan(pi / 4) * 10px)", 10 }, { "calc(sin(90deg) * 10px)", 10 },
            { "calc(sin(-270deg) * 10px)", 10 }, { "calc(sin(180deg) * 10px)", 0 },
            { "calc(cos(100grad) * 10px)", 0 }, { "calc(cos(.5turn) * 10px)", -10 },
            { "calc(tan(45deg) * 10px)", 10 }, { "calc(sin(1.5707963267948966rad) * 10px)", 10 },
            { "calc(sin(cos(0) * pi / 2) * 10px)", 10 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { margin-left: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.margin[3].kind == TBOX_STYLE_LENGTH_PX && fabs(style.margin[3].value - cases[i].expected) < 1e-9, cases[i].value);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr("div { --angle: .25turn; opacity: sin(var(--angle)); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.opacity == 1.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { opacity: cos(60deg); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(fabs(style.opacity - .5) < 1e-9);
        tbox_css_stylesheet_destroy(sheet);
        static const char *const invalid[] = {
            "sin()", "calc(sin(1, 2) * 1px)", "calc(sin(1px) * 1px)",
            "calc(sin(50%) * 1px)", "calc(sin(1e999) * 1px)", "calc(cos(1e999deg) * 1px)",
            "calc(tan(90deg) * 1px)", "calc(tan(-90deg) * 1px)", "calc(tan(pi / 2) * 1px)",
            "calc(sin(45deg + 1px) * 1px)", "calc(cos(10deg + 20) * 1px)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { width: %s; }", invalid[i]);
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.width.kind == TBOX_STYLE_LENGTH_AUTO, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Explicit radial sizes infer shape and preserve each ellipse axis. */
    {
        static const struct { const char *value; bool circle; double rx, ry; bool repeating; } cases[] = {
            { "radial-gradient(circle 40px, red, blue)", true, 40, 40, false },
            { "radial-gradient(40px circle, red, blue)", true, 40, 40, false },
            { "radial-gradient(2em, red, blue)", true, 32, 32, false },
            { "radial-gradient(ellipse 80px 40px at 25% 75%, red, blue)", false, 80, 40, false },
            { "radial-gradient(50% 25%, red, blue)", false, 100, 25, false },
            { "radial-gradient(50% 25% ellipse, red, blue)", false, 100, 25, false },
            { "radial-gradient(circle calc(20px + 10px), red, blue)", true, 30, 30, false },
            { "radial-gradient(ellipse calc(50% - 10px) 20px, red, blue)", false, 90, 20, false },
            { "repeating-radial-gradient(circle 10px, red, blue)", true, 10, 10, true },
            { "radial-gradient(circle 0px, red, blue)", true, 0, 0, false },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { background-image: %s; }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            const tbox_style_gradient *g = &style.background_gradient;
            TBOX_TEST_ASSERT_MSG(g->kind == TBOX_STYLE_GRADIENT_RADIAL && g->circle == cases[i].circle && g->repeating == cases[i].repeating, cases[i].value);
            TBOX_TEST_ASSERT_MSG(fabs(tbox_style_length_resolve(g->radius[0], 200) - cases[i].rx) < 1e-9 &&
                                fabs(tbox_style_length_resolve(g->radius[1], 100) - cases[i].ry) < 1e-9, cases[i].value);
            if (i == 3) TBOX_TEST_ASSERT(g->center[0].value == 25 && g->center[1].value == 75);
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "radial-gradient(circle 50%, red, blue)", "radial-gradient(50%, red, blue)",
            "radial-gradient(ellipse 10px, red, blue)", "radial-gradient(circle 10px 20px, red, blue)",
            "radial-gradient(circle -10px, red, blue)", "radial-gradient(ellipse 10px -20%, red, blue)",
            "radial-gradient(circle 10px closest-side, red, blue)", "radial-gradient(circle circle, red, blue)",
            "radial-gradient(closest-side farthest-side, red, blue)", "radial-gradient(1e999px, red, blue)",
            "radial-gradient(circle min(10px, 50%), red, blue)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div { background-image: %s; }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.background_gradient.kind == TBOX_STYLE_GRADIENT_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Inverse functions preserve angular types, quadrants and unit contexts. */
    {
        static const struct { const char *value; double expected; } cases[] = {
            { "asin(1)", 90 }, { "asin(-1)", -90 }, { "acos(1)", 0 },
            { "acos(-1)", 180 }, { "atan(1)", 45 }, { "atan(-1)", -45 },
            { "atan2(1, -1)", 135 }, { "atan2(-1, -1)", -135 },
            { "atan2(-1, 1)", -45 }, { "atan2(1, 0)", 90 },
            { "atan2(0, -1)", 180 }, { "atan2(-0, -1)", -180 },
            { "atan2(0, 0)", 0 }, { "atan2(-0, 1)", -0.0 },
            { "atan2(1em, 16px)", 45 }, { "atan2(1in, 96px)", 45 },
            { "atan2(.25turn, 100grad)", 45 }, { "ASIN(.5)", 30 },
            { "calc(asin(1) + .25turn)", 180 }, { "calc(2 * atan(1))", 90 },
            { "calc(atan(1) * 2 / 3)", 30 }, { "calc(asin(1) - 30deg)", 60 },
            { "min(asin(1), 45deg)", 45 }, { "max(atan(1), 90deg)", 90 },
            { "clamp(0deg, asin(-1), .5turn)", 0 },
            { "atan2(sqrt(4), pow(2, 1))", 45 },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { background-image: linear-gradient(%s, red, blue); }", cases[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.background_gradient.kind == TBOX_STYLE_GRADIENT_LINEAR &&
                                fabs(style.background_gradient.angle - cases[i].expected) < 1e-9, cases[i].value);
            if (i == 13) TBOX_TEST_ASSERT(signbit(style.background_gradient.angle));
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "asin()", "acos(1, 2)", "atan2(1)", "atan2(1, 2, 3)",
            "asin(1.01)", "acos(-1.01)", "atan(1px)", "asin(90deg)",
            "atan2(1px, 1)", "atan2(1deg, 1px)", "atan2(10%, 20%)",
            "atan2(0px, 20%)", "atan2(min(1px, 10%), 1px)",
            "atan(1e999)", "atan2(1e999, 1)", "calc(asin(1) + 1px)",
            "calc(asin(1) * 1deg)", "calc(asin(1) / 1deg)", "calc(asin(1) / 0)",
            "asin(1) extra", "calc(1deg + 0)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { background-image: linear-gradient(%s, red, blue); }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.background_gradient.kind == TBOX_STYLE_GRADIENT_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        static const struct { const char *value; double expected; } numeric[] = {
            { "sin(asin(.5))", .5 }, { "cos(acos(.5))", .5 },
            { "tan(atan(.5))", .5 }, { "sin(atan2(3px, 4px))", .6 },
            { "sin(calc(45deg + 45deg))", 1 }, { "cos(10deg + 20deg)", .8660254037844386 },
        };
        for (size_t i = 0; i < sizeof(numeric) / sizeof(numeric[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { margin-left: calc(%s * 10px); opacity: %s; }", numeric[i].value, numeric[i].value);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.margin[3].kind == TBOX_STYLE_LENGTH_PX &&
                fabs(style.margin[3].value - numeric[i].expected * 10) < 1e-9 &&
                fabs(style.opacity - numeric[i].expected) < 1e-9, numeric[i].value);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: asin(1); opacity: atan(.5); filter: hue-rotate(acos(0)); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_AUTO && style.opacity == 1.0 && style.has_filter);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { opacity: sin(atan2(30%, 40%)); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(fabs(style.opacity - .6) < 1e-9);
        tbox_css_stylesheet_destroy(sheet);
        /* Angle consumers must use the element's font size for relative lengths. */
        sheet = parse_css_cstr("div { font-size: 32px; background-image: linear-gradient(atan2(1em, 32px), red, blue); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.background_gradient.kind == TBOX_STYLE_GRADIENT_LINEAR && fabs(style.background_gradient.angle - 45) < 1e-9);
        tbox_css_stylesheet_destroy(sheet);
        char css[1024] = "div { width: calc(";
        for (int i = 0; i < 24; i++) strcat(css, "sin(asin(");
        strcat(css, ".5");
        for (int i = 0; i < 24; i++) strcat(css, "))");
        strcat(css, " * 10px); }");
        sheet = parse_css_cstr(css);
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.width.kind == TBOX_STYLE_LENGTH_AUTO);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Conic gradients use angular fractions, independent of image size. */
    {
        static const struct { const char *css; double angle; double first, last; bool repeat; } cases[] = {
            { "conic-gradient(red, blue)", 0, 0, 0, false },
            { "conic-gradient(from 90deg, red 0deg, blue 1turn)", 90, 0, 100, false },
            { "conic-gradient(from asin(1) at 25% 75%, red -90deg, blue 150%)", 90, -25, 150, false },
            { "conic-gradient(at left top, red 10%, blue calc(.5turn + 90deg))", 0, 10, 75, false },
            { "repeating-conic-gradient(from -45deg at center, red 0 45deg, blue 45deg 90deg)", -45, 0, 25, true },
            { "conic-gradient(red calc(10% + 15%), blue 100grad)", 0, 25, 25, false },
        };
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { background-image: %s; mask-image: %s; }", cases[i].css, cases[i].css);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            const tbox_style_gradient *g = &style.background_gradient;
            TBOX_TEST_ASSERT_MSG(g->kind == TBOX_STYLE_GRADIENT_CONIC && g->angle == cases[i].angle && g->repeating == cases[i].repeat, cases[i].css);
            TBOX_TEST_ASSERT_MSG(g->stop_count >= 2, cases[i].css);
            if (g->stop_count >= 2) {
                TBOX_TEST_ASSERT(fabs(g->stops[0].position.value - cases[i].first) < 1e-9);
                TBOX_TEST_ASSERT(fabs(g->stops[g->stop_count - 1].position.value - cases[i].last) < 1e-9);
            }
            TBOX_TEST_ASSERT(style.has_mask && tbox_style_masks_of(&style)->layers[0].source.gradient.kind == TBOX_STYLE_GRADIENT_CONIC);
            if (i == 2) TBOX_TEST_ASSERT(g->center[0].value == 25 && g->center[1].value == 75);
            if (i == 4) TBOX_TEST_ASSERT(g->stop_count == 4);
            tbox_css_stylesheet_destroy(sheet);
        }
        static const char *const invalid[] = {
            "conic-gradient(red)", "conic-gradient(from, red, blue)", "conic-gradient(from 10px, red, blue)",
            "conic-gradient(from 45deg from 90deg, red, blue)", "conic-gradient(at, red, blue)",
            "conic-gradient(at center at top, red, blue)", "conic-gradient(red 10px, blue)",
            "conic-gradient(red calc(10% + 1px), blue)", "conic-gradient(red 1e999deg, blue)",
            "conic-gradient(from 1e999deg, red, blue)", "conic-gradient(red, 50%, blue)",
            "conic-gradient(in oklch, red, blue)", "conic-gradient(red 0 20% 30%, blue)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { background-image: %s; }", invalid[i]);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            tbox_style style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(style.background_gradient.kind == TBOX_STYLE_GRADIENT_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* Polygon vertices retain each coordinate's percentage base and rule. */
    {
        tbox_html_document *doc = parse_html_cstr("<div>x</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { clip-path: polygon(evenodd, -10px 0, calc(100% - 5px) 0, 50% min(30px, 90%)); }");
        tbox_style style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_POLYGON && tbox_style_clip_path_of(&style)->evenodd && tbox_style_clip_path_of(&style)->point_count == 3);
        TBOX_TEST_ASSERT(tbox_style_needs_layer(&style));
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->points[1][0], 100) == 95);
        TBOX_TEST_ASSERT(tbox_style_length_resolve(tbox_style_clip_path_of(&style)->points[2][1], 100) == 30);
        tbox_css_stylesheet_destroy(sheet);
        static const char *const invalid[] = {
            "polygon()", "polygon(0 0, 100% 0)", "polygon(0, 100% 0, 0 100%)",
            "polygon(0 0 0, 100% 0, 0 100%)", "polygon(winding, 0 0, 100% 0, 0 100%)",
            "polygon(evenodd 0 0, 100% 0, 0 100%)", "polygon(0 0, 100% 0, 0 100%,)",
            "polygon(0 0, 100% auto, 0 100%)", "polygon(0 0, 100% 0, NaN 100%)",
            "polygon(0 0, 100% 0, 1e999px 100%)", "polygon(0 0, 100% 0, 1deg 100%)",
            "polygon(0 0, 100% 0, 0 100%) content-box", "polygon(round 5px, 0 0, 100% 0, 0 100%)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            char css[512];
            snprintf(css, sizeof(css), "div { clip-path: %s; }", invalid[i]);
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT_MSG(tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_NONE, invalid[i]);
            tbox_css_stylesheet_destroy(sheet);
        }
        for (int n = 32; n <= 33; n++) {
            char css[1024] = "div { clip-path: polygon(nonzero";
            for (int i = 0; i < n; i++) strcat(css, ", 10% 20%");
            strcat(css, "); }");
            sheet = parse_css_cstr(css);
            style = resolve_node(sheet, div, NULL);
            TBOX_TEST_ASSERT((tbox_style_clip_path_of(&style)->kind == TBOX_STYLE_CLIP_PATH_POLYGON) == (n == 32));
            tbox_css_stylesheet_destroy(sheet);
        }
        tbox_html_document_destroy(doc);
    }

    /* OpenType selection is inherited; both shorthands reset their fields,
     * respecting declaration order and !important across longhands. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr(
            "div { font-variant: no-common-ligatures oldstyle-nums tabular-nums diagonal-fractions ordinal slashed-zero jis90 full-width ruby historical-forms;"
            "font-feature-settings: 'liga' on, 'smcp', 'ss01' 3, 'liga' 0; font-language-override: 'TRK'; font-palette: --demo; }");
        tbox_style parent = resolve_node(sheet, div, NULL);
        tbox_style child = resolve_node(sheet, p, &parent);
        tbox_font_shaping_options options;
        tbox_style_shaping_options(&child, &options);
        TBOX_TEST_ASSERT(child.font_variant_numeric == parent.font_variant_numeric && child.font_variant_numeric != 0);
        TBOX_TEST_ASSERT(child.font_variant_ligatures == parent.font_variant_ligatures && child.font_variant_east_asian != 0);
        TBOX_TEST_ASSERT(child.font_variant_alternates && child.font_language_override == 0x54524b20u);
        TBOX_TEST_ASSERT(strcmp(child.font_palette, "--demo") == 0);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 3 && child.font_feature_settings.items[0].value == 0);
        bool liga = false, hist = false, frac = false;
        for (size_t i = 0; i < options.count; i++) {
            if (options.items[i].tag == 0x6c696761u) liga = options.items[i].value == 0;
            if (options.items[i].tag == 0x68697374u) hist = options.items[i].value == 1;
            if (options.items[i].tag == 0x66726163u) frac = options.items[i].value == 1;
        }
        TBOX_TEST_ASSERT(liga && hist && frac);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-variant: normal; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_variant_numeric == 0 && child.font_variant_east_asian == 0 && !child.font_variant_alternates);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 3 && child.font_language_override == parent.font_language_override);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-variant-numeric: tabular-nums; font: 24px sans-serif; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 0 && child.font_language_override == 0 && child.font_palette[0] == '\0');
        TBOX_TEST_ASSERT(child.font_variant_numeric == 0 && child.font_variant_ligatures == 0 && child.font_variant_east_asian == 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-variant-numeric: tabular-nums !important; font: 24px sans-serif; font-variant: none; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_variant_numeric != 0 && child.font_variant_ligatures != 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-variant: small-caps oldstyle-nums super; font-variant-caps: normal; }");
        child = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(!child.font_small_caps && child.font_variant_numeric != 0 && child.font_variant_position == TBOX_STYLE_FONT_VARIANT_POSITION_SUPER);
        TBOX_TEST_ASSERT(child.font_size < 16.0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-feature-settings: initial; font-variant: inherit; font-language-override: unset; font-palette: initial; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 0 && child.font_language_override == parent.font_language_override);
        TBOX_TEST_ASSERT(child.font_variant_numeric == parent.font_variant_numeric && child.font_palette[0] == '\0');
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { all: initial; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 0 && child.font_language_override == 0 && child.font_variant_numeric == 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-feature-settings: 'bad' 1; font-variant-numeric: lining-nums oldstyle-nums; font-language-override: 'TOOLONG'; }");
        child = resolve_node(sheet, p, &parent);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == parent.font_feature_settings.count && child.font_variant_numeric == parent.font_variant_numeric);
        TBOX_TEST_ASSERT(child.font_language_override == parent.font_language_override);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { font-feature-settings: 'liga' 0; font-feature-settings: 'bad'; font-variant-numeric: tabular-nums; font-variant-numeric: oldstyle-nums lining-nums; }");
        child = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 1 && child.font_feature_settings.items[0].value == 0);
        TBOX_TEST_ASSERT(child.font_variant_numeric != 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("p { --features: 'smcp'; font-feature-settings: var(--features); }");
        child = resolve_node(sheet, p, NULL);
        TBOX_TEST_ASSERT(child.font_feature_settings.count == 1 && child.font_feature_settings.items[0].tag == 0x736d6370u);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* float/clear (logical values through the containing block's
     * direction), blockification, float dropped for absolute boxes and
     * flex items, shape-outside/-margin/-image-threshold. */
    {
        tbox_html_document *doc     = parse_html_cstr("<div><span>x</span></div>");
        const tbox_html_node *div   = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *span  = div->first_child;
        tbox_css_stylesheet *sheet  = parse_css_cstr("span { display: inline-block; float: inline-start; clear: inline-end; shape-outside: circle(40%) border-box; shape-margin: 6px; shape-image-threshold: 2; }");
        tbox_style parent           = resolve_node(sheet, div, NULL);
        tbox_style style            = resolve_node(sheet, span, &parent);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_LEFT && style.clear == TBOX_STYLE_CLEAR_RIGHT);
        TBOX_TEST_ASSERT(style.display == TBOX_STYLE_DISPLAY_BLOCK); /* blockified */
        TBOX_TEST_ASSERT(tbox_style_shape_outside_of(&style)->kind == TBOX_STYLE_SHAPE_OUTSIDE_SHAPE && tbox_style_shape_outside_of(&style)->box == TBOX_STYLE_SHAPE_BOX_BORDER);
        TBOX_TEST_ASSERT(tbox_style_shape_outside_of(&style)->shape.kind == TBOX_STYLE_CLIP_PATH_CIRCLE);
        TBOX_TEST_ASSERT(style.shape_margin.kind == TBOX_STYLE_LENGTH_PX && style.shape_margin.value == 6.0);
        TBOX_TEST_ASSERT(style.shape_image_threshold == 1.0);
        parent.direction_rtl = true;
        style                = resolve_node(sheet, span, &parent);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_RIGHT && style.clear == TBOX_STYLE_CLEAR_LEFT);
        tbox_css_stylesheet_destroy(sheet);

        sheet  = parse_css_cstr("div { display: flex; } span { float: right; }");
        parent = resolve_node(sheet, div, NULL);
        style  = resolve_node(sheet, span, &parent);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_NONE); /* a flex item */
        tbox_css_stylesheet_destroy(sheet);
        sheet  = parse_css_cstr("div { display: grid; } span { float: right; }");
        parent = resolve_node(sheet, div, NULL);
        style  = resolve_node(sheet, span, &parent);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_NONE); /* a grid item */
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("span { float: left; position: absolute; } div { display: flow-root; shape-outside: url(a.png); }");
        style = resolve_node(sheet, span, NULL);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_NONE);
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(style.display == TBOX_STYLE_DISPLAY_BLOCK && style.display_flow_root);
        TBOX_TEST_ASSERT(tbox_style_shape_outside_of(&style)->kind == TBOX_STYLE_SHAPE_OUTSIDE_IMAGE && strcmp(tbox_style_shape_outside_of(&style)->image, "a.png") == 0);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { shape-outside: content-box; float: bogus; }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_shape_outside_of(&style)->kind == TBOX_STYLE_SHAPE_OUTSIDE_BOX && tbox_style_shape_outside_of(&style)->box == TBOX_STYLE_SHAPE_BOX_CONTENT);
        TBOX_TEST_ASSERT(style.float_side == TBOX_STYLE_FLOAT_NONE);
        tbox_css_stylesheet_destroy(sheet);
        sheet = parse_css_cstr("div { shape-outside: circle() circle(); }");
        style = resolve_node(sheet, div, NULL);
        TBOX_TEST_ASSERT(tbox_style_shape_outside_of(&style)->kind == TBOX_STYLE_SHAPE_OUTSIDE_NONE);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Every advertised keyword maps to the expected OpenType tag/value.
     * Unsupported values and incompatible groups must fail @supports too. */
    {
        static const struct { const char *property, *value, *tag; unsigned setting; } cases[] = {
            {"font-variant-ligatures", "common-ligatures", "liga", 1},
            {"font-variant-ligatures", "no-common-ligatures", "clig", 0},
            {"font-variant-ligatures", "discretionary-ligatures", "dlig", 1},
            {"font-variant-ligatures", "no-discretionary-ligatures", "dlig", 0},
            {"font-variant-ligatures", "historical-ligatures", "hlig", 1},
            {"font-variant-ligatures", "no-historical-ligatures", "hlig", 0},
            {"font-variant-ligatures", "contextual", "calt", 1},
            {"font-variant-ligatures", "no-contextual", "calt", 0},
            {"font-variant-numeric", "lining-nums", "lnum", 1},
            {"font-variant-numeric", "oldstyle-nums", "onum", 1},
            {"font-variant-numeric", "proportional-nums", "pnum", 1},
            {"font-variant-numeric", "tabular-nums", "tnum", 1},
            {"font-variant-numeric", "diagonal-fractions", "frac", 1},
            {"font-variant-numeric", "stacked-fractions", "afrc", 1},
            {"font-variant-numeric", "ordinal", "ordn", 1},
            {"font-variant-numeric", "slashed-zero", "zero", 1},
            {"font-variant-east-asian", "jis78", "jp78", 1},
            {"font-variant-east-asian", "jis83", "jp83", 1},
            {"font-variant-east-asian", "jis90", "jp90", 1},
            {"font-variant-east-asian", "jis04", "jp04", 1},
            {"font-variant-east-asian", "simplified", "smpl", 1},
            {"font-variant-east-asian", "traditional", "trad", 1},
            {"font-variant-east-asian", "full-width", "fwid", 1},
            {"font-variant-east-asian", "proportional-width", "pwid", 1},
            {"font-variant-east-asian", "ruby", "ruby", 1},
            {"font-variant-alternates", "historical-forms", "hist", 1},
            {"font-feature-settings", "'ss01' 7", "ss01", 7},
            {"font-feature-settings", "'liga' off", "liga", 0},
            {"font-feature-settings", "'\\6c iga'", "liga", 1},
        };
        for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
            tbox_style style = {0};
            TBOX_TEST_ASSERT(tbox_style_parse_opentype(cases[k].property, tbox_string_view_from_cstr(cases[k].value), &style));
            tbox_font_shaping_options options;
            tbox_style_shaping_options(&style, &options);
            uint32_t tag = (uint32_t)cases[k].tag[0] << 24 | (uint32_t)cases[k].tag[1] << 16 | (uint32_t)cases[k].tag[2] << 8 | (uint32_t)cases[k].tag[3];
            bool found = false;
            for (size_t i = 0; i < options.count; i++)
                if (options.items[i].tag == tag && options.items[i].value == cases[k].setting) found = true;
            TBOX_TEST_ASSERT_MSG(found, cases[k].value);
            char condition[256];
            snprintf(condition, sizeof(condition), "(%s: %s)", cases[k].property, cases[k].value);
            TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr(condition)));
        }
        static const char *const invalid[] = {
            "(font-feature-settings: 'abc')", "(font-feature-settings: 'liga' -1)",
            "(font-feature-settings: 'liga' 1.0)", "(font-feature-settings: 'liga' 1e0)",
            "(font-feature-settings: 'liga' 999999999999999)", "(font-feature-settings: 'liga',)",
            "(font-variant-ligatures: none contextual)", "(font-variant-ligatures: common-ligatures no-common-ligatures)",
            "(font-variant-numeric: proportional-nums tabular-nums)",
            "(font-variant-east-asian: jis78 jis90)", "(font-language-override: 'ABCDE')",
            "(font-palette: demo)", "(font-variant-alternates: unknown(test))",
            "(font-variant-alternates: styleset())", "(font-variant-alternates: swash(one,two))"
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
            TBOX_TEST_ASSERT_MSG(!tbox_css_supports(tbox_string_view_from_cstr(invalid[i])), invalid[i]);
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(font-variant-alternates: historical-forms styleset(one, two) stylistic(alt) character-variant(one, two) swash(a) ornaments(a) annotation(a))")));
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(font-palette: light) and (font-palette: dark) and (font-palette: --local)")));
    }

    /* The fixed feature capacity rejects the entire oversized declaration,
     * while a full list plus all variant groups fits the shaping options. */
    {
        char text[2048] = "";
        size_t used = 0;
        tbox_style style = {0};
        for (unsigned i = 0; i <= TBOX_FONT_MAX_FEATURE_SETTINGS; i++) {
            int written = snprintf(text + used, sizeof(text) - used, "%s'a%03u' 1", i == 0 ? "" : ", ", i);
            TBOX_TEST_REQUIRE(written > 0 && (size_t)written < sizeof(text) - used);
            used += (size_t)written;
            bool valid = tbox_style_parse_opentype("font-feature-settings", tbox_string_view_make(text, used), &style);
            TBOX_TEST_ASSERT(valid == (i < TBOX_FONT_MAX_FEATURE_SETTINGS));
        }
        TBOX_TEST_ASSERT(style.font_feature_settings.count == TBOX_FONT_MAX_FEATURE_SETTINGS);
        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-variant", tbox_string_view_from_cstr(
            "common-ligatures discretionary-ligatures historical-ligatures contextual oldstyle-nums tabular-nums diagonal-fractions ordinal slashed-zero jis90 full-width ruby historical-forms"), &style));
        tbox_font_shaping_options options;
        tbox_style_shaping_options(&style, &options);
        TBOX_TEST_ASSERT(options.count > TBOX_FONT_MAX_FEATURE_SETTINGS && options.count <= TBOX_FONT_MAX_FEATURES);
    }

    /* ex/ch: measured through tbox_style_font_metrics where the family
     * changes, inherited otherwise, 0.5em without a provider. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p><code></code></p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *code = p->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { width: 10ch; height: 10ex; } p { width: 10ch; } code { font-family: mono; width: 10ch; font-size: 10px; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        int calls = 0;
        tbox_style_font_metrics metrics = { fake_font_metrics, &calls };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree_in_viewport_with_preferences(&arena, tbox_html_document_root(doc), &source, 1, 800, 600, false, NULL, 0, &metrics);
        const tbox_style *ds = tbox_style_table_find(&table, div), *ps = tbox_style_table_find(&table, p), *cs = tbox_style_table_find(&table, code);
        TBOX_TEST_REQUIRE(ds != NULL && ps != NULL && cs != NULL);
        TBOX_TEST_ASSERT(fabs(ds->width.value - 88.0) < 1e-9 && fabs(ds->height.value - 64.0) < 1e-9);
        TBOX_TEST_ASSERT(fabs(ps->width.value - 88.0) < 1e-9);
        TBOX_TEST_ASSERT(fabs(cs->width.value - 60.0) < 1e-9 && cs->ch_ratio == 0.6);
        TBOX_TEST_ASSERT(calls == 2); /* the root's family and code's */
        table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        ds    = tbox_style_table_find(&table, div);
        TBOX_TEST_ASSERT(ds != NULL && ds->width.value == 80.0);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* The generic monospace family's 13px medium: keyword-derived sizes
     * follow it, nested monospace doesn't shrink again, px sizes stay. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><pre><code>a</code></pre><p><code>b</code></p><section><pre>c</pre></section></div>");
        const tbox_html_node *div     = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *pre     = div->first_child;
        const tbox_html_node *nested  = pre->first_child;
        const tbox_html_node *p       = pre->next_sibling;
        const tbox_html_node *code    = p->first_child;
        const tbox_html_node *section = p->next_sibling;
        const tbox_html_node *px_pre  = section->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("pre, code { font-family: monospace; } p { font-size: 2em; } section { font-size: 16px; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *pre_s = tbox_style_table_find(&table, pre), *nested_s = tbox_style_table_find(&table, nested);
        const tbox_style *code_s = tbox_style_table_find(&table, code), *px_s = tbox_style_table_find(&table, px_pre);
        TBOX_TEST_REQUIRE(pre_s != NULL && nested_s != NULL && code_s != NULL && px_s != NULL);
        TBOX_TEST_ASSERT(pre_s->font_size == 13.0 && nested_s->font_size == 13.0);
        TBOX_TEST_ASSERT(code_s->font_size == 26.0);
        TBOX_TEST_ASSERT(px_s->font_size == 16.0 && !px_s->font_size_keyword);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* text-decoration propagates to descendants, except into atomic
     * inlines and floats; the nearest decorating ancestor sets its color. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p><span>a</span><b>b</b><i>c</i></p></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p    = div->first_child;
        const tbox_html_node *span = p->first_child;
        const tbox_html_node *b    = span->next_sibling;
        const tbox_html_node *i    = b->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr("div { text-decoration: underline red; } p { text-decoration: line-through blue; } b { display: inline-block; } i { float: left; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *ps = tbox_style_table_find(&table, p), *ss = tbox_style_table_find(&table, span);
        const tbox_style *bs = tbox_style_table_find(&table, b), *is = tbox_style_table_find(&table, i);
        TBOX_TEST_REQUIRE(ps != NULL && ss != NULL && bs != NULL && is != NULL);
        TBOX_TEST_ASSERT(ps->decoration_propagated.lines == 1u && rgba_eq(ps->decoration_propagated.color, (tbox_css_rgba){ 255, 0, 0, 255 }));
        TBOX_TEST_ASSERT(ss->decoration_propagated.lines == 3u && rgba_eq(ss->decoration_propagated.color, (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(tbox_style_decoration_lines(ss) == 0u);
        TBOX_TEST_ASSERT(bs->decoration_propagated.lines == 0u && is->decoration_propagated.lines == 0u);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* Style sharing: alike elements (same tag and attributes, alike
     * parents, same cascade result) take a copy of the first one's style,
     * out-of-line data included; a different parent, cascade result or
     * attribute keeps them apart. */
    {
        tbox_html_document *doc = parse_html_cstr("<div class='a'><span>1</span><span>2</span><span>3</span></div>"
                                                  "<div class='b'><span>4</span></div><p><img width='10'><img width='20'><img width='10'></p>");
        const tbox_html_node *a = tbox_html_document_root(doc)->first_child, *b = a->next_sibling, *p = b->next_sibling;
        const tbox_html_node *s1 = a->first_child, *s2 = s1->next_sibling, *s3 = s2->next_sibling, *s4 = b->first_child;
        const tbox_html_node *i1 = p->first_child, *i2 = i1->next_sibling, *i3 = i2->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { --c: red; } .b { --c: blue; } span { --k: 1; color: var(--c); } span:nth-child(3) { font-weight: bold; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *st1 = tbox_style_table_find(&table, s1), *st2 = tbox_style_table_find(&table, s2);
        const tbox_style *st3 = tbox_style_table_find(&table, s3), *st4 = tbox_style_table_find(&table, s4);
        const tbox_style *it1 = tbox_style_table_find(&table, i1), *it2 = tbox_style_table_find(&table, i2), *it3 = tbox_style_table_find(&table, i3);
        TBOX_TEST_REQUIRE(st1 != NULL && st2 != NULL && st3 != NULL && st4 != NULL && it1 != NULL && it2 != NULL && it3 != NULL);
        TBOX_TEST_ASSERT(st1->custom_properties != NULL && st1->custom_properties == st2->custom_properties);
        TBOX_TEST_ASSERT(rgba_eq(st1->color, (tbox_css_rgba){ 255, 0, 0, 255 }) && rgba_eq(st2->color, st1->color));
        TBOX_TEST_ASSERT(st3->custom_properties != st1->custom_properties && st3->font_weight != st1->font_weight);
        TBOX_TEST_ASSERT(st4->custom_properties != st1->custom_properties && rgba_eq(st4->color, (tbox_css_rgba){ 0, 0, 255, 255 }));
        TBOX_TEST_ASSERT(it1->width.value == 10.0 && it2->width.value == 20.0 && it3->width.value == 10.0);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* `border` alone leaves border-image at its initial value without an
     * out-of-line copy; it still resets an earlier border-image, and one
     * declared after it still applies. */
    {
        tbox_html_document *doc = parse_html_cstr("<p class='a'>a</p><p class='b'>b</p><p class='c'>c</p>");
        const tbox_html_node *a = tbox_html_document_root(doc)->first_child, *b = a->next_sibling, *c = b->next_sibling;
        tbox_css_stylesheet *sheet = parse_css_cstr(".a { border: 1px solid red; }"
                                                    ".b { border-image: linear-gradient(red, blue) 1; border: 2px solid; }"
                                                    ".c { border: 2px solid; border-image: linear-gradient(red, blue) 1; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_arena arena       = tbox_arena_create(0);
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *as = tbox_style_table_find(&table, a), *bs = tbox_style_table_find(&table, b), *cs = tbox_style_table_find(&table, c);
        TBOX_TEST_REQUIRE(as != NULL && bs != NULL && cs != NULL);
        TBOX_TEST_ASSERT(as->border_image == NULL && !as->has_border_image && as->border_widths[0] == 1.0);
        TBOX_TEST_ASSERT(bs->border_image == NULL && !bs->has_border_image);
        TBOX_TEST_ASSERT(cs->border_image != NULL && cs->has_border_image && tbox_style_border_image_of(cs)->gradient.kind != TBOX_STYLE_GRADIENT_NONE);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* tbox_style_declaration_checked, which lets the cascade skip the
     * declaration validators, covers every property one of them rejects a
     * bad value for, and only those; its table stays sorted. */
    {
        tbox_string_view bogus = tbox_string_view_from_cstr("@@ not a value (");
        for (size_t i = 0; tbox_css_supported_property(i) != NULL; i++) {
            tbox_string_view name = tbox_string_view_from_cstr(tbox_css_supported_property(i));
            bool rejected = !tbox_style_opentype_declaration_valid(name, bogus) || !tbox_style_paint_declaration_valid(name, bogus) ||
                            !tbox_style_motion_declaration_valid(name, bogus) || !tbox_style_composition_declaration_valid(name, bogus) ||
                            !tbox_style_inline_declaration_valid(name, bogus) || !tbox_style_svg_declaration_valid(name, bogus);
            if (rejected && !tbox_style_declaration_checked(name)) {
                fprintf(stderr, "  validated but not checked: %s\n", tbox_css_supported_property(i));
                failures++;
            }
        }
        for (size_t i = 0; tbox_style_checked_property(i) != NULL; i++) {
            tbox_string_view name = tbox_string_view_from_cstr(tbox_style_checked_property(i));
            bool rejected = !tbox_style_opentype_declaration_valid(name, bogus) || !tbox_style_paint_declaration_valid(name, bogus) ||
                            !tbox_style_motion_declaration_valid(name, bogus) || !tbox_style_composition_declaration_valid(name, bogus) ||
                            !tbox_style_inline_declaration_valid(name, bogus) || !tbox_style_svg_declaration_valid(name, bogus);
            if (!rejected || !tbox_style_declaration_checked(name) || (i > 0 && strcmp(tbox_style_checked_property(i - 1), tbox_style_checked_property(i)) >= 0)) {
                fprintf(stderr, "  stale or out of order in the checked table: %s\n", tbox_style_checked_property(i));
                failures++;
            }
        }
        TBOX_TEST_ASSERT(!tbox_style_declaration_checked(tbox_string_view_from_cstr("color")) && !tbox_style_declaration_checked(tbox_string_view_from_cstr("mas")));
        TBOX_TEST_ASSERT(tbox_style_declaration_checked(tbox_string_view_from_cstr("-WEBKIT-mask")) && tbox_style_declaration_checked(tbox_string_view_from_cstr(" Fill ")));
    }

    failures += tbox_test_grid_style_run();
    failures += tbox_test_text_paint_run();
    failures += tbox_test_motion_run();
    tbox_arena_destroy(&style_arena);
    return failures;
}
