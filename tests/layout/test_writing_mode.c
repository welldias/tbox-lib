/* Vertical writing modes (docs/08-layout-e-pintura.md, "Escrita vertical"):
 * style mapping of logical properties, the frame change in layout, the
 * vertical shaping of upright glyphs and hit testing through the frame. */
#include <math.h>
#include <string.h>
#include <tbox/css_parser.h>
#include <tbox/layout.h>

#include "base/tbox_arena.h"
#include "context/tbox_context_internal.h"
#include "font/tbox_font_measure.h"
#include "test_support.h"

typedef struct tbox_wm_test {
    tbox_arena arena;
    tbox_html_document *doc;
    tbox_css_stylesheet *sheet;
    tbox_style_table styles;
    tbox_layout_box *box;
} tbox_wm_test;

static tbox_wm_test tbox_wm_build(const char *html, const char *css, tbox_font_face_cache *fonts) {
    tbox_wm_test test = { .arena = tbox_arena_create(0) };
    test.doc          = tbox_html_parse(html, strlen(html));
    test.sheet        = tbox_css_parse(css, strlen(css));
    tbox_css_cascade_source source = { test.sheet, TBOX_CSS_ORIGIN_AUTHOR };
    test.styles = tbox_style_resolve_tree(&test.arena, tbox_html_document_root(test.doc), &source, 1);
    test.box    = tbox_layout_build(&test.arena, tbox_html_document_root(test.doc), &test.styles, fonts, NULL, 800, 600);
    return test;
}

static void tbox_wm_destroy(tbox_wm_test *test) {
    tbox_arena_destroy(&test->arena);
    tbox_css_stylesheet_destroy(test->sheet);
    tbox_html_document_destroy(test->doc);
}

static bool tbox_wm_is(const tbox_layout_box *box, const char *id) {
    if (box == NULL || box->node == NULL || box->node->type != TBOX_HTML_NODE_ELEMENT)
        return false;
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(box->node, tbox_string_view_from_cstr("id"));
    return attribute != NULL && tbox_string_view_equal_cstr(attribute->value, id);
}

static const tbox_layout_box *tbox_wm_find(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_wm_is(box, id))
            return box;
        const tbox_layout_box *found = tbox_wm_find(box->first_child, id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static const tbox_style *tbox_wm_style(const tbox_wm_test *test, const char *id) {
    for (size_t i = 0; i < test->styles.count; i++) {
        const tbox_html_attribute *attribute = tbox_html_node_get_attribute(test->styles.items[i].node, tbox_string_view_from_cstr("id"));
        if (attribute != NULL && tbox_string_view_equal_cstr(attribute->value, id))
            return &test->styles.items[i].style;
    }
    return NULL;
}

/* `box`'s border box on the screen: through every frame above it. */
static tbox_rect tbox_wm_screen_rect(const tbox_layout_box *box) {
    const double xs[2] = { box->border_box.x, box->border_box.x + box->border_box.width };
    const double ys[2] = { box->border_box.y, box->border_box.y + box->border_box.height };
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            double x = xs[i], y = ys[j];
            for (const tbox_layout_box *b = box; b != NULL; b = b->parent)
                if (b->has_transform) {
                    const double *m = b->transform;
                    double nx = m[0] * x + m[2] * y + m[4], ny = m[1] * x + m[3] * y + m[5];
                    x = nx, y = ny;
                }
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    return (tbox_rect){ x0, y0, x1 - x0, y1 - y0 };
}

static bool near(double a, double b) { return fabs(a - b) < 0.01; }

int tbox_test_writing_mode_run(tbox_font_face_cache *fonts) {
    int failures = 0;

    /* Style: values, legacy aliases, inheritance and the logical mapping. */
    {
        tbox_wm_test test = tbox_wm_build(
            "<div id=rl><p id=child>x</p><span id=combine>12</span></div><div id=lr></div><div id=slr></div><div id=legacy></div><div id=bad></div>",
            "#rl{writing-mode:vertical-rl;margin-block-start:7px;padding-inline-start:5px;inline-size:40px;block-size:30px;border-block-end:3px solid red;border-start-end-radius:4px}"
            "#lr{writing-mode:vertical-lr;margin-block-start:7px;padding-inline-end:5px}"
            "#slr{writing-mode:sideways-lr;padding-inline-start:5px;text-orientation:upright}"
            "#combine{text-combine-upright:all}#legacy{writing-mode:tb-rl}#bad{writing-mode:diagonal}",
            fonts);
        const tbox_style *rl = tbox_wm_style(&test, "rl"), *lr = tbox_wm_style(&test, "lr"), *slr = tbox_wm_style(&test, "slr");
        const tbox_style *child = tbox_wm_style(&test, "child"), *combine = tbox_wm_style(&test, "combine");
        TBOX_TEST_REQUIRE(rl != NULL && lr != NULL && slr != NULL && child != NULL && combine != NULL);
        TBOX_TEST_ASSERT(rl->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_RL && child->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_RL);
        TBOX_TEST_ASSERT(lr->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_LR && slr->writing_mode == TBOX_STYLE_WRITING_MODE_SIDEWAYS_LR);
        TBOX_TEST_ASSERT(tbox_wm_style(&test, "legacy")->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_RL);
        TBOX_TEST_ASSERT(tbox_wm_style(&test, "bad")->writing_mode == TBOX_STYLE_WRITING_MODE_HORIZONTAL_TB);
        TBOX_TEST_ASSERT(slr->text_orientation == TBOX_STYLE_TEXT_ORIENTATION_UPRIGHT && rl->text_orientation == TBOX_STYLE_TEXT_ORIENTATION_MIXED);
        TBOX_TEST_ASSERT(combine->text_combine_upright && !rl->text_combine_upright);
        /* vertical-rl: block-start is the right side, inline-start the top */
        TBOX_TEST_ASSERT(near(rl->margin[1].value, 7.0) && near(rl->margin[3].value, 0.0));
        TBOX_TEST_ASSERT(near(rl->padding[0].value, 5.0) && near(rl->padding[3].value, 0.0));
        TBOX_TEST_ASSERT(rl->height.kind == TBOX_STYLE_LENGTH_PX && near(rl->height.value, 40.0) && near(rl->width.value, 30.0));
        TBOX_TEST_ASSERT(near(tbox_style_border_side_width(rl, 3), 3.0) && near(tbox_style_border_side_width(rl, 2), 0.0));
        /* start-end: block-start (right) and inline-end (bottom) */
        TBOX_TEST_ASSERT(near(rl->border_radius_corners[2], 4.0) && near(rl->border_radius_corners[1], 0.0));
        /* vertical-lr: block-start is the left side; inline-end the bottom */
        TBOX_TEST_ASSERT(near(lr->margin[3].value, 7.0) && near(lr->padding[2].value, 5.0));
        /* sideways-lr: inline-start is the bottom */
        TBOX_TEST_ASSERT(near(slr->padding[2].value, 5.0));
        tbox_wm_destroy(&test);
    }

    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(writing-mode: vertical-rl)")));
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(text-orientation: upright) and (text-combine-upright: all)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(text-combine-upright: digits 2)")));

    /* vertical-rl: the box keeps its screen geometry, its content is laid
     * out under a rotated frame, the first paragraph is the rightmost. */
    {
        tbox_wm_test test = tbox_wm_build(
            "<div id=v><p id=a>one</p><p id=b>two</p></div>",
            "body{margin:0}div{writing-mode:vertical-rl;width:200px;height:100px;border:2px solid black;padding:3px}p{margin:0}",
            fonts);
        const tbox_layout_box *v = tbox_wm_find(test.box, "v"), *a = tbox_wm_find(test.box, "a"), *b = tbox_wm_find(test.box, "b");
        TBOX_TEST_REQUIRE(v != NULL && a != NULL && b != NULL);
        TBOX_TEST_ASSERT(near(v->border_box.x, 0.0) && near(v->border_box.width, 210.0) && near(v->border_box.height, 110.0));
        TBOX_TEST_ASSERT(v->first_child != NULL && v->first_child->has_transform && v->first_child->style == NULL);
        tbox_rect ra = tbox_wm_screen_rect(a), rb = tbox_wm_screen_rect(b);
        TBOX_TEST_ASSERT(near(ra.x + ra.width, 205.0)); /* against the right padding edge */
        TBOX_TEST_ASSERT(near(ra.y, 5.0) && near(ra.height, 100.0));
        TBOX_TEST_ASSERT(near(rb.x + rb.width, ra.x)); /* the next column, to the left */
        /* hit testing goes through the frame */
        const tbox_layout_box *hit = tbox_context_hit_test_box(test.box, ra.x + ra.width / 2.0, ra.y + 2.0);
        TBOX_TEST_ASSERT(tbox_wm_is(hit, "a"));
        hit = tbox_context_hit_test_box(test.box, rb.x + rb.width / 2.0, rb.y + 2.0);
        TBOX_TEST_ASSERT(tbox_wm_is(hit, "b"));
        tbox_wm_destroy(&test);
    }

    /* vertical-lr and sideways-lr: columns from the left. */
    for (int mode = 0; mode < 2; mode++) {
        tbox_wm_test test = tbox_wm_build("<div id=v><p id=a>one</p><p id=b>two</p></div>",
            mode == 0 ? "body{margin:0}div{writing-mode:vertical-lr;width:200px;height:100px}p{margin:0}" :
                        "body{margin:0}div{writing-mode:sideways-lr;width:200px;height:100px}p{margin:0}",
            fonts);
        const tbox_layout_box *a = tbox_wm_find(test.box, "a"), *b = tbox_wm_find(test.box, "b");
        TBOX_TEST_REQUIRE(a != NULL && b != NULL);
        tbox_rect ra = tbox_wm_screen_rect(a), rb = tbox_wm_screen_rect(b);
        TBOX_TEST_ASSERT(near(ra.x, 0.0) && near(rb.x, ra.x + ra.width) && near(ra.height, 100.0));
        tbox_wm_destroy(&test);
    }

    /* An orthogonal flow with an auto width is as wide as its columns,
     * and a flex parent places it from that width. */
    {
        tbox_wm_test test = tbox_wm_build("<div id=row><div id=v>ab cd</div><div id=n>x</div></div>",
            "body{margin:0}#row{display:flex}#v{writing-mode:vertical-rl;height:300px;margin-block-start:10px}#n{width:50px;height:20px}",
            fonts);
        const tbox_layout_box *v = tbox_wm_find(test.box, "v"), *n = tbox_wm_find(test.box, "n");
        TBOX_TEST_REQUIRE(v != NULL && n != NULL);
        TBOX_TEST_ASSERT(v->border_box.width > 5.0 && v->border_box.width < 60.0);
        TBOX_TEST_ASSERT(near(v->margin_box.width, v->border_box.width + 10.0)); /* block-start: the right margin */
        TBOX_TEST_ASSERT(near(n->border_box.x, v->margin_box.x + v->margin_box.width));
        tbox_wm_destroy(&test);
    }

    /* Shaping: upright glyphs advance 1em and are turned; mixed keeps
     * Latin sideways and stands CJK upright; combine takes 1em. */
    {
        tbox_style style = { 0 };
        style.font_size        = 20.0;
        style.writing_mode     = TBOX_STYLE_WRITING_MODE_VERTICAL_RL;
        style.text_orientation = TBOX_STYLE_TEXT_ORIENTATION_UPRIGHT;
        const tbox_font_face *upright = tbox_layout_style_face(fonts, &style);
        style.text_orientation        = TBOX_STYLE_TEXT_ORIENTATION_MIXED;
        const tbox_font_face *mixed   = tbox_layout_style_face(fonts, &style);
        style.text_combine_upright    = true;
        const tbox_font_face *combine = tbox_layout_style_face(fonts, &style);
        style.writing_mode            = TBOX_STYLE_WRITING_MODE_VERTICAL_LR;
        style.text_combine_upright    = false;
        const tbox_font_face *lr      = tbox_layout_style_face(fonts, &style);
        TBOX_TEST_REQUIRE(upright != NULL && mixed != NULL && combine != NULL && lr != NULL);
        TBOX_TEST_ASSERT(upright != mixed);
        TBOX_TEST_ASSERT(near(tbox_font_measure_text(upright, tbox_string_view_from_cstr("AB")), 40.0));
        tbox_arena arena = tbox_arena_create(0);
        tbox_glyph *glyphs;
        size_t count;
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, upright, tbox_string_view_from_cstr("AB"), 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &count, NULL) && count == 2);
        TBOX_TEST_ASSERT(count == 2 && glyphs[0].orient == TBOX_GLYPH_ORIENT_ROTATE_CCW && near(glyphs[1].x - glyphs[0].x, 20.0));
        /* "A" + U+6C34 (CJK) */
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, mixed, tbox_string_view_from_cstr("A\xE6\xB0\xB4"), 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &count, NULL));
        TBOX_TEST_ASSERT(count == 2 && glyphs[0].orient == TBOX_GLYPH_ORIENT_NONE && glyphs[1].orient == TBOX_GLYPH_ORIENT_ROTATE_CCW);
        TBOX_TEST_ASSERT(near(tbox_font_measure_text(combine, tbox_string_view_from_cstr("2024")), 20.0));
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, combine, tbox_string_view_from_cstr("2024"), 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &count, NULL));
        TBOX_TEST_ASSERT(count == 4 && glyphs[0].scale > 0.0f && glyphs[0].scale < 1.0f && glyphs[3].advance == 0.0);
        /* vertical-lr mirrors sideways glyphs for its transposed frame */
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, lr, tbox_string_view_from_cstr("A"), 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &count, NULL));
        TBOX_TEST_ASSERT(count == 1 && glyphs[0].orient == TBOX_GLYPH_ORIENT_FLIP);
        tbox_arena_destroy(&arena);
    }

    /* Motion path inside a vertical frame is sampled on the screen:
     * ray(90deg) from the containing block's center goes right. */
    {
        tbox_wm_test test = tbox_wm_build("<div id=v><div id=m></div></div>",
            "body{margin:0}#v{writing-mode:vertical-rl;width:200px;height:250px;position:relative}"
            "#m{width:20px;height:40px;offset-path:ray(90deg);offset-distance:60px}",
            fonts);
        const tbox_layout_box *m = tbox_wm_find(test.box, "m");
        TBOX_TEST_REQUIRE(m != NULL);
        tbox_rect r = tbox_wm_screen_rect(m);
        TBOX_TEST_ASSERT(fabs(r.x + r.width / 2.0 - 160.0) < 0.5 && fabs(r.y + r.height / 2.0 - 125.0) < 0.5 && near(r.width, 20.0));
        tbox_wm_destroy(&test);
    }

    /* Horizontal documents never get a frame. */
    {
        tbox_wm_test test = tbox_wm_build("<div id=h><p>x</p></div>", "div{width:100px}", fonts);
        const tbox_layout_box *h = tbox_wm_find(test.box, "h");
        TBOX_TEST_REQUIRE(h != NULL);
        TBOX_TEST_ASSERT(!h->first_child->has_transform && h->first_child->style != NULL);
        tbox_wm_destroy(&test);
    }
    return failures;
}
