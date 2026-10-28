#include <math.h>
#include <string.h>
#include <tbox/layout.h>
#include <tbox/render.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "context/tbox_context_internal.h"
#include "test_support.h"

typedef struct tbox_multicol_test {
    tbox_arena arena;
    tbox_html_document *doc;
    tbox_css_stylesheet *sheet;
    tbox_style_table styles;
    tbox_layout_box *box;
} tbox_multicol_test;

static tbox_multicol_test tbox_multicol_test_build(const char *html, const char *css, tbox_font_face_cache *fonts) {
    tbox_multicol_test test = { .arena = tbox_arena_create(0) };
    test.doc = tbox_html_parse(html, strlen(html));
    test.sheet = tbox_css_parse(css, strlen(css));
    tbox_css_cascade_source source = { test.sheet, TBOX_CSS_ORIGIN_AUTHOR };
    test.styles = tbox_style_resolve_tree(&test.arena, tbox_html_document_root(test.doc), &source, 1);
    test.box = tbox_layout_build(&test.arena, tbox_html_document_root(test.doc), &test.styles, fonts, NULL, 800, 600);
    return test;
}

static void tbox_multicol_test_destroy(tbox_multicol_test *test) {
    tbox_arena_destroy(&test->arena);
    tbox_css_stylesheet_destroy(test->sheet);
    tbox_html_document_destroy(test->doc);
}

static bool near(double a, double b) { return fabs(a - b) < 1e-4; }

static size_t tbox_multicol_run_count(const tbox_layout_box *box) {
    size_t count = box->text_run_count;
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        count += tbox_multicol_run_count(child);
    return count;
}

int tbox_test_multicol_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    /* Used sizing: width-only, count-only, both constraints and normal gap.
     * The colored 90px block supplies exactly three 30px fragments. */
    static const struct { const char *css; int count; double width, gap; } sizes[] = {
        { "column-count:3;column-gap:20px", 3, 120, 20 },
        { "column-width:120px;column-gap:20px", 3, 120, 20 },
        { "columns:120px 2;column-gap:20px", 2, 190, 20 },
        { "columns:8 120px;column-gap:20px", 3, 120, 20 },
        { "columns:3;font-size:10px", 3, 380.0 / 3.0, 10 },
        { "column-width:500px;column-gap:20px", 1, 400, 20 },
        { "columns:auto auto;column-gap:20px", 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        tbox_string_builder css;
        tbox_arena temporary = tbox_arena_create(0);
        tbox_string_builder_init(&css, &temporary, 0);
        tbox_string_builder_append_view(&css, tbox_string_view_from_cstr("div{display:block;width:400px;"));
        tbox_string_builder_append_view(&css, tbox_string_view_from_cstr(sizes[i].css));
        tbox_string_builder_append_view(&css, tbox_string_view_from_cstr("}section{display:block;height:90px;background:red}"));
        tbox_string_view text = tbox_string_builder_finish(&css);
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>", text.data, fonts);
        TBOX_TEST_REQUIRE(test.box != NULL);
        TBOX_TEST_ASSERT_MSG(test.box->column_count == sizes[i].count, sizes[i].css);
        if (sizes[i].count > 0) {
            TBOX_TEST_ASSERT(near(test.box->column_width, sizes[i].width));
            TBOX_TEST_ASSERT(near(test.box->column_gap, sizes[i].gap));
            TBOX_TEST_ASSERT(near(test.box->first_child->border_box.width, sizes[i].width));
        }
        tbox_multicol_test_destroy(&test);
        tbox_arena_destroy(&temporary);
    }
    /* Auto fills the definite height; extra columns overflow horizontally.
     * The same auto fill balances when the height is indefinite. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;width:220px;height:40px;columns:2;column-gap:20px;column-fill:auto;column-rule:2px solid red}section{display:block;height:130px}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        const tbox_layout_box *child = test.box->first_child;
        TBOX_TEST_ASSERT(near(test.box->content_box.height, 40));
        for (int i = 0; i < 4; i++) {
            TBOX_TEST_REQUIRE(child != NULL);
            TBOX_TEST_ASSERT(near(child->border_box.x, i * 120));
            TBOX_TEST_ASSERT(near(child->border_box.y, 0));
            TBOX_TEST_ASSERT(near(child->border_box.height, i == 3 ? 10 : 40));
            child = child->next_sibling;
        }
        TBOX_TEST_ASSERT(child == NULL);
        TBOX_TEST_ASSERT(test.box->column_rule_count == 3);
        tbox_multicol_test_destroy(&test);
        test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;width:220px;columns:2;column-gap:20px;column-fill:auto}section{display:block;height:130px}", fonts);
        TBOX_TEST_ASSERT(near(test.box->content_box.height, 65));
        tbox_multicol_test_destroy(&test);
    }
    /* A span has the full width, balanced sets before and after, and no rule
     * through its own band. The 40px item after it occupies two 20px columns. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section><h2></h2><section></section></div>",
            "div{display:block;width:220px;columns:2;column-gap:20px;column-rule:2px solid blue}section{display:block;height:40px}h2{display:block;column-span:all;height:10px;margin:0}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        const tbox_layout_box *first = test.box->first_child;
        TBOX_TEST_REQUIRE(first->next_sibling != NULL && first->next_sibling->next_sibling != NULL);
        const tbox_layout_box *span = first->next_sibling->next_sibling;
        TBOX_TEST_ASSERT(near(span->border_box.x, 0) && near(span->border_box.width, 220));
        TBOX_TEST_ASSERT(near(span->border_box.y, 20));
        TBOX_TEST_REQUIRE(span->next_sibling != NULL);
        TBOX_TEST_ASSERT(near(span->next_sibling->border_box.y, 30));
        TBOX_TEST_ASSERT(near(test.box->content_box.height, 50));
        TBOX_TEST_ASSERT(test.box->column_rule_count == 2);
        TBOX_TEST_ASSERT(near(test.box->column_rules[1].y, 30));
        tbox_multicol_test_destroy(&test);
    }
    /* Line fragments retain all text exactly once; background and side
     * borders appear in each fragment. A click on the second fragment targets
     * the same DOM paragraph and a click in the gap targets its container. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><p>one<br>two<br>three<br>four<br>five<br>six</p></div>",
            "div{display:block;width:220px;columns:2;column-gap:20px;column-rule:2px solid blue}p{display:block;margin:0;line-height:20px;font-size:14px;background:yellow;border:2px solid red}", fonts);
        const tbox_layout_box *a = test.box->first_child;
        TBOX_TEST_REQUIRE(a != NULL && a->next_sibling != NULL);
        const tbox_layout_box *b = a->next_sibling;
        TBOX_TEST_ASSERT(a->node == b->node);
        TBOX_TEST_ASSERT(tbox_multicol_run_count(test.box) == 6);
        TBOX_TEST_ASSERT(a->text_run_count == 3 && b->text_run_count == 3);
        TBOX_TEST_ASSERT(near(tbox_style_border_side_width(a->style, 2), 0));
        TBOX_TEST_ASSERT(near(tbox_style_border_side_width(b->style, 0), 0));
        TBOX_TEST_ASSERT(near(tbox_style_border_side_width(b->style, 1), 2));
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(test.box, 125, 10) == b);
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(test.box, 110, 10) == test.box);
        TBOX_TEST_ASSERT(b->text_runs[0].text.size == 4 && memcmp(b->text_runs[0].text.data, "four", 4) == 0);
        tbox_display_list list = tbox_render_build_display_list(&test.arena, test.box);
        size_t yellow = 0, blue = 0;
        for (size_t i = 0; i < list.count; i++) {
            const tbox_paint_op *op = &list.items[i];
            if (op->kind != TBOX_PAINT_FILL_RECT) continue;
            if (op->color.r == 255 && op->color.g == 255 && op->color.b == 0 && near(op->rect.width, 100)) yellow++;
            if (op->color.r == 0 && op->color.g == 0 && op->color.b == 255) {
                blue++;
                TBOX_TEST_ASSERT(near(op->rect.x, 109) && near(op->rect.width, 2));
            }
        }
        TBOX_TEST_ASSERT(yellow == 2 && blue == 1);
        tbox_multicol_test_destroy(&test);
    }
    /* The line box, rather than the glyph rectangle, controls breaks even
     * with leading, raised inline runs and a column exactly four lines high. */
    {
        const char *html = "<div><p>01<br>02<br>03<br>04<br>05<br>06<br>07<br>08<br>09<br>10<br>11<br>12<br>13<br>14<br>15<br><sup>16</sup></p></div>";
        tbox_multicol_test test = tbox_multicol_test_build(html,
            "div{display:block;width:220px;columns:2;height:72px;column-fill:auto;column-gap:20px}p{display:block;margin:0;font-size:14px;line-height:18px}sup{display:inline;vertical-align:super}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL);
        TBOX_TEST_ASSERT(tbox_multicol_run_count(test.box) == 16);
        size_t columns = 0;
        for (const tbox_layout_box *child = test.box->first_child; child != NULL; child = child->next_sibling) {
            columns++;
            TBOX_TEST_ASSERT(child->text_run_count == 4);
            if (child->text_run_count > 0) TBOX_TEST_ASSERT(near(child->text_runs[0].line_y, 0));
        }
        TBOX_TEST_ASSERT(columns == 4);
        tbox_multicol_test_destroy(&test);
    }
    /* Outside markers share the first line's flow geometry, regardless of
     * glyph leading. Lists with and without UA-like margins balance 5 + 5. */
    {
        const char *html = "<div><ul><li>Item 1</li><li>Item 2</li><li>Item 3</li><li>Item 4</li><li>Item 5</li><li>Item 6</li><li>Item 7</li><li>Item 8</li><li>Item 9</li><li>Item 10</li></ul></div>";
        for (int margin = 0; margin <= 14; margin += 14) {
            char css[256];
            snprintf(css, sizeof(css), "div{display:block;width:400px;columns:2;column-gap:20px;font-size:14px;line-height:18px}ul{display:block;margin:%dpx 0;padding-left:20px}li{display:list-item}", margin);
            tbox_multicol_test test = tbox_multicol_test_build(html, css, fonts);
            TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
            const tbox_layout_box *a = test.box->first_child, *b = a->next_sibling;
            TBOX_TEST_REQUIRE(b != NULL);
            TBOX_TEST_ASSERT(b->next_sibling == NULL);
            TBOX_TEST_ASSERT(near(test.box->content_box.height, 90 + margin));
            size_t items = 0;
            for (const tbox_layout_box *column = a; column != NULL; column = column->next_sibling) {
                size_t in_column = 0;
                for (const tbox_layout_box *item = column->first_child; item != NULL; item = item->next_sibling) {
                    TBOX_TEST_REQUIRE(item->text_run_count == 2);
                    const tbox_layout_text_run *text = &item->text_runs[0], *marker = &item->text_runs[1];
                    TBOX_TEST_ASSERT(near(marker->line_y, text->line_y));
                    TBOX_TEST_ASSERT(near(marker->line_height, text->line_height));
                    TBOX_TEST_ASSERT(near(item->border_box.x, column == a ? 20 : 230));
                    TBOX_TEST_ASSERT(near(item->border_box.y, (column == a ? margin : 0) + 18 * in_column));
                    in_column++;
                    items++;
                }
                TBOX_TEST_ASSERT(in_column == 5);
            }
            TBOX_TEST_ASSERT(items == 10 && tbox_multicol_run_count(test.box) == 20);
            tbox_multicol_test_destroy(&test);
        }
        /* A list item split across columns paints its outside marker once. */
        tbox_multicol_test test = tbox_multicol_test_build("<div><ul><li>one<br>two<br>three<br>four</li></ul></div>",
            "div{display:block;width:400px;columns:2;column-gap:20px;font-size:14px;line-height:18px}ul{display:block;margin:0;padding-left:20px}li{display:list-item}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        const tbox_layout_box *a = test.box->first_child, *b = a->next_sibling;
        TBOX_TEST_REQUIRE(a->first_child != NULL && b != NULL && b->first_child != NULL);
        TBOX_TEST_ASSERT(a->first_child->text_run_count == 3);
        TBOX_TEST_ASSERT(b->first_child->text_run_count == 2);
        tbox_multicol_test_destroy(&test);
    }
    /* Margins adjoining an unforced break are discarded on both sides.
     * An asymmetric prefix/suffix puts the ideal cut inside an 8px gap. */
    {
        const char *cards = "<section class='lead'></section><article>A<br>x<br>y</article><article>B<br>x<br>y</article><article>C<br>x<br>y</article><article>D<br>x<br>y</article><article>E<br>x<br>y</article><article>F<br>x<br>y</article><section class='tail'></section>";
        for (int variant = 0; variant < 4; variant++) {
            int nested = variant % 2, top = variant < 2 ? 0 : 6;
            char html[512], css[512];
            snprintf(html, sizeof(html), "<div>%s%s%s</div>", nested ? "<main>" : "", cards, nested ? "</main>" : "");
            snprintf(css, sizeof(css), "div{display:block;width:500px;columns:2;column-gap:20px;font-size:14px;line-height:18px}main,section,article{display:block}main{margin:0}.lead{height:70px}.tail{height:56px}article{height:54px;break-inside:avoid;margin:%dpx 0 8px;border:2px solid red;padding:4px}", top);
            tbox_multicol_test test = tbox_multicol_test_build(html, css, fonts);
            TBOX_TEST_REQUIRE(test.box != NULL);
            TBOX_TEST_ASSERT(near(test.box->content_box.height, 284 + top));
            size_t total = 0;
            for (const tbox_layout_box *column = test.box->first_child; column != NULL; column = column->next_sibling) {
                const tbox_layout_box *item = nested ? column->first_child : column;
                for (; item != NULL; item = nested ? item->next_sibling : NULL) {
                    if (item->node == NULL || !tbox_string_view_equal_cstr(item->node->element.tag_name, "article")) continue;
                    TBOX_TEST_ASSERT(near(item->border_box.height, 66));
                    TBOX_TEST_ASSERT(near(item->border_box.x, total < 3 ? 0 : 260));
                    if (total == 2) TBOX_TEST_ASSERT(near(item->margin_box.y + item->margin_box.height, item->border_box.y + item->border_box.height));
                    if (total == 3) {
                        TBOX_TEST_ASSERT(near(item->border_box.y, test.box->content_box.y));
                        TBOX_TEST_ASSERT(near(item->margin_box.y, item->border_box.y));
                        TBOX_TEST_ASSERT(tbox_context_hit_test_box(test.box, 265, 5) == item);
                    }
                    total++;
                }
            }
            TBOX_TEST_ASSERT(total == 6 && tbox_multicol_run_count(test.box) == 18);
            tbox_multicol_test_destroy(&test);
        }
    }
    /* Rule styles reuse border painting, including double stripes and dots.
     * Empty columns have no rule, and rules obey the container's clipping. */
    {
        static const char *const rules[] = { "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset" };
        for (size_t i = 0; i < sizeof(rules) / sizeof(rules[0]); i++) {
            char css[256];
            snprintf(css, sizeof(css), "div{display:block;width:220px;columns:2;column-gap:20px;column-rule:6px %s blue}section{display:block;height:120px}", rules[i]);
            tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>", css, fonts);
            TBOX_TEST_REQUIRE(test.box != NULL);
            tbox_display_list list = tbox_render_build_display_list(&test.arena, test.box);
            TBOX_TEST_ASSERT(test.box->column_rule_count == 1);
            TBOX_TEST_ASSERT_MSG(list.count > 0, rules[i]);
            if (i == 0) TBOX_TEST_ASSERT(list.count == 1);
            if (i == 1 || i == 2) TBOX_TEST_ASSERT(list.count >= 2);
            if (i == 3) TBOX_TEST_ASSERT(list.count == 2 && near(list.items[0].rect.width, 2));
            tbox_multicol_test_destroy(&test);
        }
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;width:220px;columns:3;column-rule:2px solid red}section{display:block;height:30px;break-inside:avoid}", fonts);
        TBOX_TEST_ASSERT(test.box->column_rule_count == 0);
        tbox_multicol_test_destroy(&test);
        test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;width:220px;height:30px;columns:2;column-fill:auto;column-gap:20px;column-rule:2px solid red;overflow:hidden}section{display:block;height:120px}", fonts);
        tbox_display_list list = tbox_render_build_display_list(&test.arena, test.box);
        TBOX_TEST_ASSERT(list.count == 3);
        for (size_t i = 0; i < list.count; i++) TBOX_TEST_ASSERT(list.items[i].has_clip && near(list.items[i].clip.width, 220));
        tbox_multicol_test_destroy(&test);
    }
    /* avoid moves a small block intact; an oversized avoid block relaxes.
     * Forced before/after breaks also work inside a nested ordinary block. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><section><p></p><p></p><p></p></section></div>",
            "div{display:block;width:220px;height:50px;columns:2;column-gap:20px;column-fill:auto}section{display:block}p{display:block;margin:0;height:30px;break-inside:avoid}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        const tbox_layout_box *a = test.box->first_child;
        TBOX_TEST_ASSERT(a->first_child->next_sibling == NULL);
        TBOX_TEST_REQUIRE(a->next_sibling != NULL);
        TBOX_TEST_ASSERT(near(a->next_sibling->first_child->border_box.x, 120));
        TBOX_TEST_ASSERT(near(a->next_sibling->first_child->border_box.height, 30));
        tbox_multicol_test_destroy(&test);
        test = tbox_multicol_test_build("<div><section><p></p><p></p><p></p></section></div>",
            "div{display:block;width:220px;height:100px;columns:2;column-gap:20px;column-fill:auto}section{display:block}p{display:block;margin:0;height:20px}p:first-child{break-after:column}p:last-child{break-before:column}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        a = test.box->first_child;
        TBOX_TEST_REQUIRE(a->next_sibling != NULL && a->next_sibling->next_sibling != NULL);
        TBOX_TEST_ASSERT(near(a->next_sibling->first_child->border_box.x, 120));
        TBOX_TEST_ASSERT(near(a->next_sibling->next_sibling->first_child->border_box.x, 240));
        tbox_multicol_test_destroy(&test);
    }
    /* Oversized avoid boxes relax, and a container with only positioned
     * content still paints and targets that content without an empty column. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><p>1<br>2<br>3<br>4<br>5<br>6</p></div>",
            "div{display:block;width:220px;height:40px;columns:2;column-gap:20px;column-fill:auto}p{display:block;margin:0;font-size:14px;line-height:20px;break-inside:avoid}", fonts);
        TBOX_TEST_ASSERT(tbox_multicol_run_count(test.box) == 6);
        size_t fragments = 0;
        for (const tbox_layout_box *child = test.box->first_child; child != NULL; child = child->next_sibling) fragments++;
        TBOX_TEST_ASSERT(fragments == 3);
        tbox_multicol_test_destroy(&test);
        test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;position:relative;width:220px;columns:2}section{display:block;position:absolute;left:5px;top:8px;width:20px;height:30px;background:red}", fonts);
        TBOX_TEST_REQUIRE(test.box != NULL && test.box->first_child != NULL);
        TBOX_TEST_ASSERT(near(test.box->content_box.height, 0));
        TBOX_TEST_ASSERT(tbox_context_hit_test_box(test.box, 10, 10) == test.box->first_child);
        tbox_multicol_test_destroy(&test);
    }
    /* Extreme CSS must bound fragment allocations while preserving the
     * remaining flow as overflow in the last fragment. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;width:220px;height:1px;columns:2;column-fill:auto}section{display:block;height:10000px}", fonts);
        size_t fragments = 0;
        double total = 0;
        for (const tbox_layout_box *child = test.box->first_child; child != NULL; child = child->next_sibling) {
            fragments++;
            total += child->border_box.height;
        }
        TBOX_TEST_ASSERT(fragments > 0 && fragments < 10000);
        TBOX_TEST_ASSERT(near(total, 10000));
        tbox_multicol_test_destroy(&test);
    }
    /* Shorthand cascade, currentColor, resets, inherited keyword, invalid
     * values and the supports table cover style integration end to end. */
    {
        tbox_multicol_test test = tbox_multicol_test_build("<div><section></section></div>",
            "div{display:block;column-count:4!important;columns:100px 2;color:blue;column-rule:thick dashed;column-rule-width:2px}section{display:block;columns:inherit;column-rule:inherit;column-fill:balance-all;column-span:all}", fonts);
        const tbox_style *style = test.box->style;
        TBOX_TEST_ASSERT(style->column_count == 4 && style->column_width.value == 100);
        TBOX_TEST_ASSERT(style->column_rule_width == 2 && style->column_rule_style == TBOX_STYLE_BORDER_STYLE_DASHED);
        TBOX_TEST_ASSERT(style->column_rule_color.b == 255);
        TBOX_TEST_REQUIRE(test.box->first_child != NULL);
        TBOX_TEST_ASSERT(test.box->first_child->style->column_count == 4);
        TBOX_TEST_ASSERT(test.box->first_child->style->column_fill == TBOX_STYLE_COLUMN_FILL_BALANCE_ALL);
        tbox_multicol_test_destroy(&test);
        test = tbox_multicol_test_build("<div></div>",
            "div{display:block;column-count:0;column-width:30%;columns:2 3;column-rule:2px solid nonsense}", fonts);
        TBOX_TEST_ASSERT(test.box->column_count == 0 && test.box->style->column_count == 0);
        TBOX_TEST_ASSERT(test.box->style->column_rule_style == TBOX_STYLE_BORDER_STYLE_NONE);
        tbox_multicol_test_destroy(&test);
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(columns: 120px 3) and (column-fill: auto) and (column-rule: 2px dashed red) and (column-span: all)")));
    }
    /* Numeric transitions: counts round to integers, auto stays discrete,
     * widths/gaps and colors use the ordinary animation accessors. */
    {
        tbox_style from = { .column_count = 2, .column_width = { .kind = TBOX_STYLE_LENGTH_PX, .value = 100 }, .column_rule_width = 2 };
        tbox_style to = from;
        to.column_count = 5;
        double a[TBOX_STYLE_ANIM_VALUE_SIZE], b[TBOX_STYLE_ANIM_VALUE_SIZE], mixed[TBOX_STYLE_ANIM_VALUE_SIZE];
        tbox_style_animatable_read(&from, TBOX_STYLE_ANIM_COLUMN_COUNT, a);
        tbox_style_animatable_read(&to, TBOX_STYLE_ANIM_COLUMN_COUNT, b);
        TBOX_TEST_ASSERT(tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_COLUMN_COUNT, a, b));
        tbox_style_animatable_mix(TBOX_STYLE_ANIM_COLUMN_COUNT, a, b, 0.5, mixed);
        tbox_style_animatable_write(&from, TBOX_STYLE_ANIM_COLUMN_COUNT, mixed, NULL);
        TBOX_TEST_ASSERT(from.column_count == 4);
        a[0] = 0;
        TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_COLUMN_COUNT, a, b));
        TBOX_TEST_ASSERT(tbox_style_animatable_mask(tbox_string_view_from_cstr("columns")) != 0);
        TBOX_TEST_ASSERT(tbox_style_animatable_mask(tbox_string_view_from_cstr("column-rule")) != 0);
        tbox_style_animatable_read(&from, TBOX_STYLE_ANIM_COLUMN_WIDTH, a);
        TBOX_TEST_ASSERT(a[0] == 100);
        to.column_rule_color = (tbox_css_rgba){ 20, 60, 80, 255 };
        tbox_style_animatable_read(&to, TBOX_STYLE_ANIM_COLUMN_RULE_COLOR, a);
        tbox_style_animatable_write(&from, TBOX_STYLE_ANIM_COLUMN_RULE_COLOR, a, NULL);
        TBOX_TEST_ASSERT(from.column_rule_color.b == 80);
    }
    return failures;
}
