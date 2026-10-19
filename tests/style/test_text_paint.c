#include <tbox/style.h>

#include <string.h>

#include "base/tbox_string.h"
#include "test_support.h"

static tbox_style resolve(const char *css, const tbox_style *parent) {
    tbox_html_document *doc = tbox_html_parse("<div>x</div>", 12);
    tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));
    const tbox_html_node *node = tbox_html_document_root(doc)->first_child;
    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, node);
    tbox_style style = tbox_style_resolve(node, parent, &computed);
    tbox_css_computed_style_destroy(&computed);
    tbox_css_stylesheet_destroy(sheet);
    tbox_html_document_destroy(doc);
    return style;
}

int tbox_test_text_paint_run(void) {
    int failures = 0;
    tbox_style initial = resolve("", NULL);
    TBOX_TEST_ASSERT(initial.text_stroke_width == 0.0 && initial.text_stroke_current_color);
    TBOX_TEST_ASSERT(initial.marker_urls[0][0] == 0 && !initial.mask_type_alpha);
    TBOX_TEST_ASSERT(initial.paint_order[0] == TBOX_STYLE_PAINT_FILL && initial.paint_order[1] == TBOX_STYLE_PAINT_STROKE);

    tbox_style parent = resolve("div { color: yellow; marker: url('#m'); marker-mid: none; mask-type: alpha; -webkit-text-stroke: #c00 3px; paint-order: stroke; }", NULL);
    TBOX_TEST_ASSERT(parent.text_stroke_width == 3.0 && parent.text_stroke_color.r == 204 && parent.text_stroke_color.a == 255 && !parent.text_stroke_current_color);
    TBOX_TEST_ASSERT(strcmp(parent.marker_urls[0], "#m") == 0 && parent.marker_urls[1][0] == 0 && strcmp(parent.marker_urls[2], "#m") == 0 && parent.mask_type_alpha);
    TBOX_TEST_ASSERT(parent.paint_order[0] == TBOX_STYLE_PAINT_STROKE && parent.paint_order[1] == TBOX_STYLE_PAINT_FILL && parent.paint_order[2] == TBOX_STYLE_PAINT_MARKERS);
    tbox_style child = resolve("", &parent);
    TBOX_TEST_ASSERT(child.text_stroke_width == 3 && child.text_stroke_color.r == 204 && !child.text_stroke_current_color);
    TBOX_TEST_ASSERT(strcmp(child.marker_urls[2], "#m") == 0 && !child.mask_type_alpha && child.paint_order[0] == TBOX_STYLE_PAINT_STROKE);
    child = resolve("div { marker: initial; marker-end: inherit; mask-type: inherit; -webkit-text-stroke: initial; paint-order: initial; }", &parent);
    TBOX_TEST_ASSERT(child.marker_urls[0][0] == 0 && child.marker_urls[1][0] == 0 && strcmp(child.marker_urls[2], "#m") == 0 && child.mask_type_alpha);
    TBOX_TEST_ASSERT(child.text_stroke_width == 0.0 && child.text_stroke_current_color && child.paint_order[0] == TBOX_STYLE_PAINT_FILL);
    child = resolve("div { marker: unset; mask-type: unset; -webkit-text-stroke: unset; paint-order: unset; }", &parent);
    TBOX_TEST_ASSERT(strcmp(child.marker_urls[0], "#m") == 0 && !child.mask_type_alpha && child.text_stroke_width == 3.0 && child.paint_order[0] == TBOX_STYLE_PAINT_STROKE);
    child = resolve("div { all: initial; }", &parent);
    TBOX_TEST_ASSERT(child.marker_urls[0][0] == 0 && !child.mask_type_alpha && child.text_stroke_width == 0.0 && child.text_stroke_current_color && child.paint_order[0] == TBOX_STYLE_PAINT_FILL);

    child = resolve("div { marker-start: url(#early); marker: url(#late); -webkit-text-stroke-width: 9px; -webkit-text-stroke: 1px; color: transparent; }", &parent);
    TBOX_TEST_ASSERT(strcmp(child.marker_urls[0], "#late") == 0 && child.text_stroke_width == 1.0 && child.text_stroke_current_color && child.color.a == 0);
    child = resolve("div { marker-start: url(#important) !important; marker: none; -webkit-text-stroke-color: transparent !important; -webkit-text-stroke: 3px red; }", NULL);
    TBOX_TEST_ASSERT(strcmp(child.marker_urls[0], "#important") == 0 && child.marker_urls[1][0] == 0 && child.text_stroke_width == 3.0 && !child.text_stroke_current_color && child.text_stroke_color.a == 0);
    child = resolve("div { font-size: 20px; -webkit-text-stroke: .1em blue; -webkit-text-stroke-color: currentColor; color: red; }", NULL);
    TBOX_TEST_ASSERT(child.text_stroke_width == 2.0 && child.text_stroke_current_color);
    child = resolve("div { -webkit-text-stroke: blue; }", &parent);
    TBOX_TEST_ASSERT(child.text_stroke_width == 0.0 && child.text_stroke_color.b == 255 && !child.text_stroke_current_color);
    child = resolve("div { --stroke: 2px #123456; --order: markers stroke; --m: url(#v); -webkit-text-stroke: var(--stroke); paint-order: var(--order); marker: var(--m); }", NULL);
    TBOX_TEST_ASSERT(child.text_stroke_width == 2 && child.text_stroke_color.r == 0x12 && child.paint_order[0] == TBOX_STYLE_PAINT_MARKERS && child.paint_order[1] == TBOX_STYLE_PAINT_STROKE && strcmp(child.marker_urls[0], "#v") == 0);

    child = resolve("div { -webkit-text-stroke: 1px light-dark(red, blue); }", NULL);
    TBOX_TEST_ASSERT(child.text_stroke_width == 1 && child.text_stroke_color.r == 255 && child.text_stroke_color.b == 0);

    /* Invalid later declarations never displace an earlier valid winner. */
    child = resolve("div { marker: url(#ok); marker: red; marker-end: url(a b); mask-type: alpha; mask-type: red; paint-order: stroke; paint-order: fill fill; -webkit-text-stroke: 2px blue; -webkit-text-stroke: 1px red blue; -webkit-text-stroke-width: -1px; -webkit-text-stroke-width: 20%; -webkit-text-stroke-color: nonsense; }", NULL);
    TBOX_TEST_ASSERT(strcmp(child.marker_urls[2], "#ok") == 0 && child.mask_type_alpha && child.paint_order[0] == TBOX_STYLE_PAINT_STROKE);
    TBOX_TEST_ASSERT(child.text_stroke_width == 2.0 && child.text_stroke_color.b == 255);

    static const char *const orders[] = { "fill stroke markers", "fill markers stroke", "stroke fill markers", "stroke markers fill", "markers fill stroke", "markers stroke fill" };
    static const tbox_style_paint_layer expected[][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    for (size_t i = 0; i < 6; i++) {
        char css[128] = "div { paint-order: ";
        strcat(css, orders[i]);
        strcat(css, "; }");
        child = resolve(css, NULL);
        TBOX_TEST_ASSERT(memcmp(child.paint_order, expected[i], sizeof(child.paint_order)) == 0);
    }
    child = resolve("div { paint-order: markers; }", NULL);
    TBOX_TEST_ASSERT(child.paint_order[0] == TBOX_STYLE_PAINT_MARKERS && child.paint_order[1] == TBOX_STYLE_PAINT_FILL && child.paint_order[2] == TBOX_STYLE_PAINT_STROKE);

    static const char *const unsupported[] = { "(marker: none)", "(marker-start: url(#m))", "(marker-mid: none)", "(marker-end: none)", "(mask-type: alpha)", "at-rule(@page)", "(paint-order: fill fill)", "(paint-order: normal stroke)", "(-webkit-text-stroke-width: -1px)", "(-webkit-text-stroke-width: 10%)", "(-webkit-text-stroke: red blue)", "(-webkit-text-stroke-color: nonsense)" };
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
        TBOX_TEST_ASSERT_MSG(!tbox_css_supports(tbox_string_view_from_cstr(unsupported[i])), unsupported[i]);
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(paint-order: stroke) and (-webkit-text-stroke: 3px #c00) and (-webkit-text-stroke-color: transparent) and (-webkit-text-stroke-width: thin)")));
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(break-before: page)")));

    unsigned long long mask = tbox_style_animatable_mask(tbox_string_view_from_cstr("-webkit-text-stroke"));
    TBOX_TEST_ASSERT(mask == ((1ull << TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH) | (1ull << TBOX_STYLE_ANIM_TEXT_STROKE_COLOR)));
    double value[TBOX_STYLE_ANIM_VALUE_SIZE] = {0};
    child = resolve("div { color: red; -webkit-text-stroke: thin; }", NULL);
    tbox_style_animatable_read(&child, TBOX_STYLE_ANIM_TEXT_STROKE_COLOR, value);
    TBOX_TEST_ASSERT(value[0] == 255 && value[1] == 0 && value[3] == 1);
    value[0] = -2;
    tbox_style_animatable_write(&child, TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH, value);
    TBOX_TEST_ASSERT(child.text_stroke_width == 0);
    value[0] = 0; value[2] = 255; value[3] = 1;
    tbox_style_animatable_write(&child, TBOX_STYLE_ANIM_TEXT_STROKE_COLOR, value);
    TBOX_TEST_ASSERT(!child.text_stroke_current_color && child.text_stroke_color.b == 255);
    double from[TBOX_STYLE_ANIM_VALUE_SIZE] = {1}, to[TBOX_STYLE_ANIM_VALUE_SIZE] = {5};
    TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH, from, to));
    tbox_style_animatable_mix(TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH, from, to, 0.49, value);
    TBOX_TEST_ASSERT(value[0] == 1);
    tbox_style_animatable_mix(TBOX_STYLE_ANIM_TEXT_STROKE_WIDTH, from, to, 0.5, value);
    TBOX_TEST_ASSERT(value[0] == 5);
    /* paint-order is discrete and not animatable (the 64-bit animatable
     * mask is full): it switches at once. */
    TBOX_TEST_ASSERT(tbox_style_animatable_mask(tbox_string_view_from_cstr("paint-order")) == 0);
    return failures;
}
