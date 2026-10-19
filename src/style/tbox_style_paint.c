/* Text paint and SVG-only properties. SVG references are retained without
 * loading resources: tbox has no SVG drawing pipeline. */
#include "style/tbox_style_internal.h"

#include <math.h>
#include <string.h>

#include "base/tbox_string.h"

static bool tbox_style_stroke_width(tbox_string_view value, const tbox_style_units *units, double *out) {
    value = tbox_string_view_trim_ascii_space(value);
    if (tbox_style_is(value, "thin")) *out = 1.0;
    else if (tbox_style_is(value, "medium")) *out = 3.0;
    else if (tbox_style_is(value, "thick")) *out = 5.0;
    else {
        tbox_style_length length;
        if (!tbox_style_parse_spacing_length(value, units, &length) || length.kind != TBOX_STYLE_LENGTH_PX || !isfinite(length.value) || length.value < 0.0)
            return false;
        *out = length.value;
    }
    return true;
}

static bool tbox_style_stroke_color(tbox_string_view value, tbox_style *style) {
    tbox_css_rgba color;
    if (!tbox_style_parse_edge_color(value, style->color, &color)) return false;
    style->text_stroke_color = color;
    style->text_stroke_current_color = tbox_style_is(value, "currentcolor");
    return true;
}

static bool tbox_style_paint_order_parse(tbox_string_view value, tbox_style_paint_layer out[3]) {
    static const char *const names[] = { "fill", "stroke", "markers" };
    size_t used = 0;
    unsigned seen = 0;
    if (!tbox_style_is(value, "normal")) {
        tbox_string_view tokens[4];
        size_t count = tbox_style_tokens(value, tokens, 4);
        if (count == 0 || count > 3) return false;
        for (size_t i = 0; i < count; i++) {
            unsigned k = 0;
            while (k < 3 && !tbox_style_is(tokens[i], names[k])) k++;
            if (k == 3 || (seen & (1u << k))) return false;
            seen |= 1u << k;
            out[used++] = (tbox_style_paint_layer)k;
        }
    }
    for (unsigned k = 0; k < 3; k++)
        if (!(seen & (1u << k))) out[used++] = (tbox_style_paint_layer)k;
    return true;
}

static bool tbox_style_marker_url(tbox_string_view value, char out[256]) {
    if (tbox_style_is(value, "none")) {
        out[0] = '\0';
        return true;
    }
    tbox_string_view args;
    value = tbox_string_view_trim_ascii_space(value);
    if (!tbox_style_function_args(value, "url", &args)) return false;
    args = tbox_string_view_trim_ascii_space(args);
    if (args.size > 0 && (args.data[0] == '\'' || args.data[0] == '"')) {
        char quote = args.data[0];
        if (args.size < 2 || args.data[args.size - 1] != quote) return false;
        args = tbox_string_view_make(args.data + 1, args.size - 2);
        for (size_t i = 0; i < args.size; i++) {
            if (args.data[i] == '\\' && i + 1 < args.size) { i++; continue; }
            if (args.data[i] == quote || args.data[i] == '\n' || args.data[i] == '\r') return false;
        }
    } else {
        for (size_t i = 0; i < args.size; i++) {
            if (args.data[i] == '\\' && i + 1 < args.size) { i++; continue; }
            if (tbox_ascii_is_space(args.data[i]) || args.data[i] == '\'' || args.data[i] == '"' || args.data[i] == '(' || args.data[i] == ')') return false;
        }
    }
    tbox_utf8_copy_truncated(out, 256, args);
    return true;
}

static bool tbox_style_paint_parse(tbox_string_view property, tbox_string_view value, const tbox_style_units *units, tbox_style *style) {
    if (tbox_style_is(property, "paint-order"))
        return tbox_style_paint_order_parse(value, style->paint_order);
    if (tbox_style_is(property, "mask-type")) {
        if (!tbox_style_is(value, "alpha") && !tbox_style_is(value, "luminance")) return false;
        style->mask_type_alpha = tbox_style_is(value, "alpha");
        return true;
    }
    if (tbox_style_is(property, "marker")) {
        char url[256] = {0};
        if (!tbox_style_marker_url(value, url)) return false;
        for (size_t i = 0; i < 3; i++) memcpy(style->marker_urls[i], url, sizeof(url));
        return true;
    }
    static const char *const markers[] = { "marker-start", "marker-mid", "marker-end" };
    for (size_t i = 0; i < 3; i++)
        if (tbox_style_is(property, markers[i])) return tbox_style_marker_url(value, style->marker_urls[i]);
    if (tbox_style_is(property, "-webkit-text-stroke-width"))
        return tbox_style_stroke_width(value, units, &style->text_stroke_width);
    if (tbox_style_is(property, "-webkit-text-stroke-color"))
        return tbox_style_stroke_color(value, style);
    /* Shorthand: omitted components reset, duplicates invalidate everything. */
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(value, tokens, 3);
    if (count == 0 || count > 2) return false;
    bool width = false, color = false;
    style->text_stroke_width = 0.0;
    style->text_stroke_color = style->color;
    style->text_stroke_current_color = true;
    for (size_t i = 0; i < count; i++) {
        double pixels;
        if (!width && tbox_style_stroke_width(tokens[i], units, &pixels)) {
            style->text_stroke_width = pixels;
            width = true;
        } else if (!color && tbox_style_stroke_color(tokens[i], style)) color = true;
        else return false;
    }
    return true;
}

bool tbox_style_paint_declaration_valid(tbox_string_view property, tbox_string_view value) {
    static const char *const names[] = { "paint-order", "marker", "marker-start", "marker-mid", "marker-end", "mask-type", "-webkit-text-stroke", "-webkit-text-stroke-width", "-webkit-text-stroke-color" };
    bool known = false;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (tbox_style_is(property, names[i])) known = true;
    if (!known) return true;
    if (tbox_style_is(value, "initial") || tbox_style_is(value, "inherit") || tbox_style_is(value, "unset") || tbox_style_is(value, "revert") || tbox_style_is(value, "revert-layer")) return true;
    /* These functions are substituted with the node's environment before
     * the resolver validates the resulting value. */
    for (size_t i = 0; i + 4 <= value.size; i++) {
        if (tbox_style_is(tbox_string_view_make(value.data + i, 4), "var(")) return true;
        if (i + 11 <= value.size && tbox_style_is(tbox_string_view_make(value.data + i, 11), "light-dark(")) return true;
    }
    const tbox_style_units units = { 16.0, 16.0, 800.0, 600.0, false, -1.0, -1.0, 1.0, TBOX_STYLE_FRAME_NONE, 0.0, 0.0 };
    tbox_style scratch = {0};
    return tbox_style_paint_parse(property, value, &units, &scratch);
}

void tbox_style_resolve_paint(const tbox_css_computed_style *computed, const tbox_style *parent, const tbox_style_units *units, tbox_style *style) {
    style->paint_order[0] = TBOX_STYLE_PAINT_FILL;
    style->paint_order[1] = TBOX_STYLE_PAINT_STROKE;
    style->paint_order[2] = TBOX_STYLE_PAINT_MARKERS;
    style->text_stroke_current_color = true;
    if (parent != NULL) {
        memcpy(style->paint_order, parent->paint_order, sizeof(style->paint_order));
        memcpy(style->marker_urls, parent->marker_urls, sizeof(style->marker_urls));
        style->text_stroke_width = parent->text_stroke_width;
        style->text_stroke_color = parent->text_stroke_color;
        style->text_stroke_current_color = parent->text_stroke_current_color;
    }
    static const char *const names[] = { "marker-start", "marker-mid", "marker-end", "mask-type", "paint-order", "-webkit-text-stroke-width", "-webkit-text-stroke-color" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        tbox_string_view property = tbox_string_view_from_cstr(names[i]);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, property);
        const tbox_css_resolved_declaration *shorthand = i < 3 ? tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("marker")) :
            i >= 5 ? tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("-webkit-text-stroke")) : NULL;
        if (shorthand != NULL && (decl == NULL || tbox_css_cascade_priority_compare(shorthand, decl) > 0)) {
            tbox_style parsed = *style;
            if (tbox_style_paint_parse(shorthand->property, shorthand->value, units, &parsed)) {
                if (i < 3) memcpy(style->marker_urls[i], parsed.marker_urls[i], sizeof(style->marker_urls[i]));
                else if (i == 5) style->text_stroke_width = parsed.text_stroke_width;
                else {
                    style->text_stroke_color = parsed.text_stroke_color;
                    style->text_stroke_current_color = parsed.text_stroke_current_color;
                }
            }
        } else if (decl != NULL) {
            tbox_style parsed = *style;
            if (tbox_style_paint_parse(property, decl->value, units, &parsed)) *style = parsed;
        }
    }
}
