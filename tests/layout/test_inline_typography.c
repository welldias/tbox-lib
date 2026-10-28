#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "css_parser/tbox_css_parser.h"
#include "test_support.h"
#include <math.h>
#include <string.h>
#include <tbox/layout.h>
#include <tbox/render.h>

typedef struct tbox_typography_case {
    tbox_html_document *doc;
    tbox_css_stylesheet *sheet;
    tbox_arena arena;
    tbox_style_table styles;
    const tbox_layout_box *root;
} tbox_typography_case;

static void tbox_typography_build(tbox_typography_case *c, tbox_font_face_cache *fonts, const char *html, const char *css) {
    c->arena = tbox_arena_create(0);
    tbox_string_builder markup;
    tbox_string_builder_init(&markup, &c->arena, strlen(html) + 11);
    tbox_string_builder_append_view(&markup, tbox_string_view_from_cstr("<div>"));
    tbox_string_builder_append_view(&markup, tbox_string_view_from_cstr(html));
    tbox_string_builder_append_view(&markup, tbox_string_view_from_cstr("</div>"));
    tbox_string_view view          = tbox_string_builder_finish(&markup);
    c->doc                         = tbox_html_parse(view.data, view.size);
    c->sheet                       = tbox_css_parse(css, strlen(css));
    tbox_css_cascade_source source = { c->sheet, TBOX_CSS_ORIGIN_AUTHOR };
    c->styles                      = tbox_style_resolve_tree(&c->arena, tbox_html_document_root(c->doc), &source, 1);
    c->root                        = tbox_layout_build(&c->arena, tbox_html_document_root(c->doc), &c->styles, fonts, NULL, 800, 600);
}

static void tbox_typography_destroy(tbox_typography_case *c) {
    tbox_arena_destroy(&c->arena);
    tbox_css_stylesheet_destroy(c->sheet);
    tbox_html_document_destroy(c->doc);
}

static const tbox_layout_box *tbox_typography_find(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_html_attribute *a = box->node != NULL ? tbox_html_node_get_attribute(box->node, tbox_string_view_from_cstr("id")) : NULL;
        if (a != NULL && tbox_string_view_equal_cstr(a->value, id))
            return box;
        const tbox_layout_box *found = tbox_typography_find(box->first_child, id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static double tbox_typography_baseline(const tbox_layout_text_run *run) {
    return run->rect.y + tbox_font_face_ascent(run->font);
}

int tbox_test_inline_typography_run(tbox_font_face_cache *fonts) {
    int failures                          = 0;
    static const char *const css_values[] = { "3", "3 2", "3 drop", "3 raise", "2.5", "normal" };
    const char *text                      = "<p id='p'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau.</p>";
    for (size_t i = 0; i < sizeof(css_values) / sizeof(css_values[0]); i++) {
        tbox_typography_case c;
        char css[300];
        /* Values are literals; the number formatter is unnecessary. */
        strcpy(css, "p{width:150px;margin:0;font-size:15px;line-height:20px}p::first-letter{margin-right:4px;color:#bb0033;initial-letter:");
        strcat(css, css_values[i]);
        strcat(css, "}");
        tbox_typography_build(&c, fonts, text, css);
        const tbox_layout_box *p = tbox_typography_find(c.root, "p");
        TBOX_TEST_REQUIRE(p != NULL && p->first_child != NULL && p->text_run_count >= 4);
        const tbox_layout_box *cap = p->first_child;
        TBOX_TEST_REQUIRE(cap->text_run_count == 1);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(cap->text_runs[0].text, "A"));
        TBOX_TEST_ASSERT(cap->text_runs[0].style->color.r == 187);
        const tbox_layout_text_run *first = &p->text_runs[0];
        if (i < 5) {
            int sink = i == 1 ? 2 : i == 3 ? 1 : i == 4 ? 2 : 3;
            TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&cap->text_runs[0]) - tbox_typography_baseline(first) - (sink - 1) * 20.0) < 1e-6);
            double target = ((i == 4 ? 2.5 : 3.0) - 1.0) * 20.0 + tbox_font_face_cap_height(first->font);
            TBOX_TEST_ASSERT(fabs(tbox_font_face_cap_height(cap->text_runs[0].font) - target) < 1.1);
            TBOX_TEST_ASSERT(fabs(cap->margin_box.height - sink * 20.0) < 1e-6);
            for (int line = 0; line < sink; line++) {
                TBOX_TEST_ASSERT(fabs(p->text_runs[line].rect.x - (cap->margin_box.x + cap->margin_box.width)) < 1e-6);
                TBOX_TEST_ASSERT(p->text_runs[line].rect.height == 20.0);
            }
            TBOX_TEST_ASSERT(p->text_runs[sink].rect.x == p->content_box.x);
        } else {
            TBOX_TEST_ASSERT(cap->style->float_side == TBOX_STYLE_FLOAT_NONE);
            TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&cap->text_runs[0]) - tbox_typography_baseline(first)) < 1e-6);
        }
        tbox_display_list display = tbox_render_build_display_list(&c.arena, c.root);
        TBOX_TEST_ASSERT(display.count > 0);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='p'>  “A&#x301;bc alpha beta gamma delta epsilon zeta eta theta</p>", "p{width:140px;margin:0;font-size:15px;line-height:20px}p:first-letter{font-size:48px;line-height:1;float:left;margin-right:4px;background:#ffcc00;color:#bb0033}");
        const tbox_layout_box *p = tbox_typography_find(c.root, "p");
        TBOX_TEST_REQUIRE(p != NULL && p->first_child != NULL && p->text_run_count >= 3);
        const tbox_layout_box *cap = p->first_child;
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(cap->text_runs[0].text, "“A\xcc\x81"));
        TBOX_TEST_ASSERT(cap->content_box.height == 48.0);
        TBOX_TEST_ASSERT(cap->style->background_color.r == 255 && cap->style->background_color.g == 204);
        TBOX_TEST_ASSERT(p->text_runs[0].rect.x == cap->margin_box.x + cap->margin_box.width);
        TBOX_TEST_ASSERT(p->text_runs[1].rect.x == p->text_runs[0].rect.x);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts,
            "<p id='short'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau.</p>"
            "<p id='tall'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau.</p>",
            "p{width:150px;margin:0;font-size:15px;line-height:20px}#tall{line-height:40px}"
            "p::first-letter{font-size:48px;line-height:1;float:left;margin-right:4px}");
        const tbox_layout_box *short_lines = tbox_typography_find(c.root, "short"), *tall_lines = tbox_typography_find(c.root, "tall");
        TBOX_TEST_REQUIRE(short_lines != NULL && tall_lines != NULL);
        const tbox_layout_box *paragraphs[] = { short_lines, tall_lines };
        for (size_t i = 0; i < 2; i++) {
            const tbox_layout_box *p = paragraphs[i];
            TBOX_TEST_REQUIRE(p->first_child != NULL && p->text_run_count >= 3);
            const tbox_layout_box *cap      = p->first_child;
            const tbox_layout_text_run *run = &cap->text_runs[0];
            TBOX_TEST_ASSERT(cap->content_box.height == 48 && cap->margin_box.height == 48);
            TBOX_TEST_ASSERT(cap->margin_box.y == p->content_box.y && cap->border_box.y == p->content_box.y);
            tbox_font_glyph_bitmap glyph = tbox_font_rasterize_glyph_id((tbox_font_face *)run->font, run->glyphs[0].id);
            double ink_top               = tbox_typography_baseline(run) + run->glyphs[0].y - glyph.bearing_y;
            TBOX_TEST_ASSERT(fabs(ink_top - p->content_box.y) < 1e-6);
            TBOX_TEST_ASSERT(cap->float_ink_height == glyph.height);
            double line_height = i == 0 ? 20.0 : 40.0;
            for (size_t line = 0; line < 3; line++) {
                bool covered = line * line_height < cap->float_ink_height;
                double x     = covered ? cap->margin_box.x + cap->margin_box.width : p->content_box.x;
                TBOX_TEST_ASSERT(fabs(p->text_runs[line].rect.x - x) < 1e-6);
            }
            TBOX_TEST_ASSERT(p->text_runs[2].rect.x == p->content_box.x);
        }
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts,
            "<p id='plain'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa.</p>"
            "<p id='large'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa.</p>"
            "<p id='explicit'>Abc alpha beta gamma delta epsilon zeta eta theta iota kappa.</p>",
            "p{width:150px;margin:0;font-size:15px;line-height:20px}"
            "#large::first-letter{font-size:30px}#explicit::first-letter{font-size:30px;line-height:1.5}");
        const tbox_layout_box *plain = tbox_typography_find(c.root, "plain"), *large = tbox_typography_find(c.root, "large"), *explicit = tbox_typography_find(c.root, "explicit");
        TBOX_TEST_REQUIRE(plain != NULL && large != NULL && explicit != NULL);
        TBOX_TEST_REQUIRE(plain->text_run_count > 1 && large->text_run_count > 1 && explicit->text_run_count > 1 && large->first_child != NULL && explicit->first_child != NULL);
        TBOX_TEST_ASSERT(plain->text_runs[0].rect.height == 20);
        TBOX_TEST_ASSERT(large->first_child->content_box.height == 30);
        TBOX_TEST_ASSERT(large->text_runs[0].rect.height >= 30);
        TBOX_TEST_ASSERT(large->text_runs[0].rect.height - plain->text_runs[0].rect.height >= 9);
        TBOX_TEST_ASSERT(explicit->first_child->content_box.height == 45);
        TBOX_TEST_ASSERT(explicit->text_runs[0].rect.height >= 45);
        TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&large->first_child->text_runs[0]) - tbox_typography_baseline(&large->text_runs[0])) < 1e-6);
        TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&explicit->first_child->text_runs[0]) - tbox_typography_baseline(&explicit->text_runs[0])) < 1e-6);
        TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&large->text_runs[1]) - tbox_typography_baseline(&large->text_runs[0]) - 20.0) < 1e-6);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='p'><span>A</span>bc alpha beta gamma delta epsilon zeta eta theta</p><p id='q'>X<span>A</span>bc</p><p id='b'>Abc alpha beta gamma delta epsilon</p>", "p{width:180px;margin:0;font-size:15px;line-height:20px}span{display:inline;initial-letter:3 2;margin-right:4px}#b{initial-letter:2}");
        const tbox_layout_box *p = tbox_typography_find(c.root, "p"), *q = tbox_typography_find(c.root, "q"), *b = tbox_typography_find(c.root, "b");
        TBOX_TEST_REQUIRE(p != NULL && q != NULL && b != NULL);
        TBOX_TEST_REQUIRE(p->first_child != NULL && p->text_run_count > 0 && b->first_child != NULL);
        TBOX_TEST_ASSERT(p->first_child->margin_box.height == 40.0);
        TBOX_TEST_ASSERT(p->first_child->style->generated == 0);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(p->first_child->node->element.tag_name, "span"));
        TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&p->first_child->text_runs[0]) - tbox_typography_baseline(&p->text_runs[0]) - 20.0) < 1e-6);
        TBOX_TEST_ASSERT(q->first_child == NULL); /* only the beginning of the block */
        TBOX_TEST_ASSERT(b->first_child->margin_box.height == 40.0);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='p'>“Alpha beta gamma delta epsilon</p><p id='q'>“Alpha beta gamma delta epsilon</p>", "p{width:100px;margin:0;font-size:15px;line-height:20px}#p{hanging-punctuation:first}");
        const tbox_layout_box *p = tbox_typography_find(c.root, "p"), *q = tbox_typography_find(c.root, "q");
        TBOX_TEST_REQUIRE(p != NULL && q != NULL && p->text_run_count > 1 && q->text_run_count > 0);
        double quote = tbox_font_measure_text(p->text_runs[0].font, tbox_string_view_from_cstr("“"));
        TBOX_TEST_ASSERT(fabs(p->text_runs[0].rect.x - p->content_box.x + quote) < 1e-6);
        TBOX_TEST_ASSERT(p->text_runs[1].rect.x == p->content_box.x);
        TBOX_TEST_ASSERT(q->text_runs[0].rect.x == q->content_box.x);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='p'><span>“Alpha</span> beta gamma delta epsilon zeta eta</p>",
            "p{width:140px;margin:0;font-size:15px;line-height:30px;border-left:2px solid #b03;padding-left:24px;hanging-punctuation:first}"
            "span{display:inline;font-size:24px;font-weight:bold;letter-spacing:1px}");
        const tbox_layout_box *p = tbox_typography_find(c.root, "p");
        TBOX_TEST_REQUIRE(p != NULL && p->text_run_count > 1);
        const tbox_layout_text_run *quote_run = &p->text_runs[0];
        double quote                          = tbox_font_measure_text_spaced(quote_run->font, tbox_string_view_from_cstr("“"), 1);
        TBOX_TEST_ASSERT(quote_run->style->font_size == 24);
        TBOX_TEST_ASSERT(fabs(quote_run->rect.x + quote - p->content_box.x) < 1e-6);
        TBOX_TEST_ASSERT(quote_run->rect.x > p->padding_box.x); /* wholly inside the padding */
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        /* Set the width from the real font: the comma alone would overflow. */
        const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(""), false, false, 15);
        double plain               = tbox_font_measure_text(face, tbox_string_view_from_cstr("aa bb"));
        tbox_typography_build(&c, fonts, "<p id='allow'>aa bb, cc</p><p id='none'>aa bb, cc</p><p id='last'>aa bb”</p>", "p{width:200px;margin:0;font-size:15px;line-height:20px}#allow{hanging-punctuation:allow-end}#last{hanging-punctuation:last}");
        /* Rebuild layout with an exact measured width; styles are public values. */
        for (size_t i = 0; i < c.styles.count; i++)
            if (c.styles.items[i].node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(c.styles.items[i].node->element.tag_name, "p"))
                tbox_style_table_mutable(&c.styles, i, &c.arena)->width = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_PX, .value = plain + 0.1 };
        c.root                       = tbox_layout_build(&c.arena, tbox_html_document_root(c.doc), &c.styles, fonts, NULL, 800, 600);
        const tbox_layout_box *allow = tbox_typography_find(c.root, "allow"), *none = tbox_typography_find(c.root, "none"), *last = tbox_typography_find(c.root, "last");
        TBOX_TEST_REQUIRE(allow != NULL && none != NULL && last != NULL);
        TBOX_TEST_REQUIRE(allow->text_run_count >= 2 && none->text_run_count >= 2 && last->text_run_count > 0);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(allow->text_runs[0].text, "aa bb,"));
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(none->text_runs[0].text, "aa"));
        TBOX_TEST_ASSERT(last->text_run_count == 1);
        TBOX_TEST_ASSERT(allow->text_runs[0].rect.width > allow->content_box.width);
        TBOX_TEST_ASSERT(last->text_runs[0].rect.width > last->content_box.width);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='force'>aa bb, cc dd ee ff gg hh</p><p id='allow'>aa bb, cc dd ee ff gg hh</p>", "p{width:55px;margin:0;font-size:15px;line-height:20px;text-align:justify}#force{hanging-punctuation:force-end}#allow{hanging-punctuation:allow-end}");
        const tbox_layout_box *force = tbox_typography_find(c.root, "force"), *allow = tbox_typography_find(c.root, "allow");
        TBOX_TEST_REQUIRE(force != NULL && allow != NULL && force->text_run_count > 1 && allow->text_run_count > 1);
        double comma = tbox_font_measure_text(force->text_runs[0].font, tbox_string_view_from_cstr(","));
        TBOX_TEST_ASSERT(fabs(force->text_runs[0].rect.width - force->content_box.width - comma) < 1e-6);
        TBOX_TEST_ASSERT(fabs(allow->text_runs[0].rect.width - allow->content_box.width) < 1e-6);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<div><p id='p'>Alpha beta</p></div>", "div{hanging-punctuation:first last}p{initial-letter:3 2;initial-letter:0;hanging-punctuation:force-end;hanging-punctuation:allow-end force-end}span{display:inline}p::first-letter{initial-letter:inherit}");
        const tbox_layout_box *p = tbox_typography_find(c.root, "p");
        TBOX_TEST_REQUIRE(p != NULL);
        TBOX_TEST_ASSERT(p->style->initial_letter_size == 3 && p->style->initial_letter_sink == 2);
        TBOX_TEST_ASSERT(p->style->hanging_punctuation == TBOX_STYLE_HANG_FORCE_END);
        TBOX_TEST_ASSERT(p->style->first_letter != NULL && p->style->first_letter->initial_letter_size == 3);
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<div id='anon'>“ <em>A&#x301;bc</em> alpha beta gamma delta epsilon zeta eta theta<div>Outro bloco</div>Mais texto depois</div>", "#anon{width:160px;font-size:15px;line-height:20px}em{display:inline}#anon::first-letter{initial-letter:2;margin-right:4px}");
        const tbox_layout_box *block = tbox_typography_find(c.root, "anon");
        TBOX_TEST_REQUIRE(block != NULL && block->first_child != NULL && block->last_child != NULL);
        const tbox_layout_box *first = block->first_child;
        TBOX_TEST_REQUIRE(first->node == NULL && first->first_child != NULL && first->text_run_count > 0);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(first->first_child->text_runs[0].text, "“ A\xcc\x81"));
        TBOX_TEST_ASSERT(block->last_child->first_child == NULL); /* only the first sequence */
        TBOX_TEST_ASSERT(fabs(tbox_typography_baseline(&first->first_child->text_runs[0]) - tbox_typography_baseline(&first->text_runs[0]) - 20.0) < 1e-6);
        /* A second layout must leave the document intact and select once. */
        c.root = tbox_layout_build(&c.arena, tbox_html_document_root(c.doc), &c.styles, fonts, NULL, 800, 600);
        block  = tbox_typography_find(c.root, "anon");
        TBOX_TEST_REQUIRE(block != NULL && block->first_child != NULL && block->first_child->first_child != NULL);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(block->first_child->first_child->text_runs[0].text, "“ A\xcc\x81"));
        tbox_typography_destroy(&c);
    }
    {
        tbox_typography_case c;
        tbox_typography_build(&c, fonts, "<p id='inherit'><span>A</span> bc</p><p id='right'>aa bb”</p>", "p{width:200px;margin:0;font-size:15px;line-height:30px}span{display:inline;color:#008877;font-size:22px}#inherit::first-letter{background:#ffeeaa}#right{hanging-punctuation:last;text-align:right}");
        const tbox_layout_box *inherit = tbox_typography_find(c.root, "inherit"), *right = tbox_typography_find(c.root, "right");
        TBOX_TEST_REQUIRE(inherit != NULL && inherit->first_child != NULL && right != NULL && right->text_run_count == 1);
        const tbox_style *letter = inherit->first_child->style;
        TBOX_TEST_ASSERT(letter->font_size == 22 && letter->color.g == 136 && letter->color.b == 119);
        const tbox_layout_text_run *run = &right->text_runs[0];
        double quote                    = tbox_font_measure_text(run->font, tbox_string_view_from_cstr("”"));
        TBOX_TEST_ASSERT(fabs(run->rect.x + run->rect.width - right->content_box.x - right->content_box.width - quote) < 1e-6);
        tbox_typography_destroy(&c);
    }
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 3 2)")));
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 2.5 raise)")));
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(hanging-punctuation: last first allow-end)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 0)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 3 1.5)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 3 2.0)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 3 999999999999999999999999999999)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 1e300)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(initial-letter: 3 2 drop)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(hanging-punctuation: first first)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(hanging-punctuation: allow-end force-end)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(hanging-punctuation: none last)")));
    return failures;
}
