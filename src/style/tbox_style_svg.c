/* Basic SVG paint participates in the ordinary cascade and CSS-wide keywords. */
#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "svg/tbox_svg_internal.h"
#include <math.h>
#include <string.h>

static const tbox_style_svg_paint tbox_svg_initial_paint = {
    .fill           = { { 0, 0, 0, 255 }, TBOX_STYLE_SVG_COLOR },
    .stroke         = { { 0, 0, 0, 255 }, TBOX_STYLE_SVG_NONE },
    .width          = { .kind = TBOX_STYLE_LENGTH_PX, .value = 1.0 },
    .dashoffset     = { .kind = TBOX_STYLE_LENGTH_PX },
    .fill_opacity   = 1.0,
    .stroke_opacity = 1.0,
    .miterlimit     = 4.0,
};

const tbox_style_svg_paint *tbox_style_svg_paint_of(const tbox_style *style) {
    return style != NULL && style->svg_paint != NULL ? style->svg_paint : &tbox_svg_initial_paint;
}

static const char *const tbox_svg_properties[] = { "fill", "stroke", "stroke-width", "stroke-dasharray", "stroke-dashoffset", "fill-opacity", "stroke-opacity", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit", "fill-rule" };

bool tbox_svg_is_element(const tbox_html_node *node) {
    for (const tbox_html_node *n = node; n != NULL; n = n->parent) {
        if (n->type != TBOX_HTML_NODE_ELEMENT)
            continue;
        if (tbox_style_is(n->element.tag_name, "foreignobject"))
            return false;
        if (tbox_style_is(n->element.tag_name, "svg"))
            return true;
    }
    return false;
}

bool tbox_svg_presentation_property(tbox_string_view property) {
    for (size_t i = 0; i < sizeof(tbox_svg_properties) / sizeof(tbox_svg_properties[0]); i++)
        if (tbox_style_is(property, tbox_svg_properties[i]))
            return true;
    static const char *const common[] = { "color", "opacity", "transform", "display", "visibility", "overflow", "paint-order", "width", "height" };
    for (size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++)
        if (tbox_style_is(property, common[i]))
            return true;
    return false;
}

tbox_string_view tbox_svg_presentation_value(tbox_arena *arena, tbox_string_view property, tbox_string_view value) {
    /* An attribute is a value, never a declaration or an !important rule. */
    for (size_t i = 0; i < value.size; i++)
        if (value.data[i] == '!' || value.data[i] == ';' || value.data[i] == '{' || value.data[i] == '}')
            return (tbox_string_view){ 0 };
    if (tbox_style_is(property, "width") || tbox_style_is(property, "height")) {
        double n;
        if (tbox_number_parse(value, &n) && n >= 0) {
            char *copy = tbox_arena_alloc(arena, value.size + 2);
            if (copy == NULL)
                return (tbox_string_view){ 0 };
            memcpy(copy, value.data, value.size);
            memcpy(copy + value.size, "px", 2);
            return tbox_string_view_make(copy, value.size + 2);
        }
    }
    if (!tbox_style_is(property, "transform"))
        return value;
    double m[6];
    if (!tbox_svg_parse_transform(value, m))
        return (tbox_string_view){ 0 };
    char text[256] = "matrix(";
    size_t n       = 7;
    for (size_t i = 0; i < 6; i++) {
        int wrote = tbox_number_format(text + n, sizeof(text) - n, m[i], 15);
        if (wrote < 0 || (size_t)wrote >= sizeof(text) - n - 1)
            return (tbox_string_view){ 0 };
        n += (size_t)wrote;
        text[n++] = i == 5 ? ')' : ',';
    }
    char *copy = tbox_arena_alloc(arena, n);
    if (copy == NULL)
        return (tbox_string_view){ 0 };
    memcpy(copy, text, n);
    return tbox_string_view_make(copy, n);
}

static bool tbox_svg_length(tbox_string_view value, const tbox_style_units *units, tbox_style_length *out) {
    double number;
    if (tbox_style_parse_number(value, &number)) {
        *out = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_PX, .value = number };
        return true;
    }
    return tbox_style_parse_spacing_length(value, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO;
}

static bool tbox_svg_parse_paint(tbox_string_view property, tbox_string_view value, const tbox_style_units *units, tbox_style_svg_paint *paint) {
    if (tbox_style_is(property, "fill") || tbox_style_is(property, "stroke")) {
        tbox_style_svg_color *color = tbox_style_is(property, "fill") ? &paint->fill : &paint->stroke;
        if (tbox_style_is(value, "none"))
            color->kind = TBOX_STYLE_SVG_NONE;
        else if (tbox_style_is(value, "currentcolor"))
            color->kind = TBOX_STYLE_SVG_CURRENT_COLOR;
        else {
            if (!tbox_css_color_parse(value, &color->color))
                return false;
            color->kind = TBOX_STYLE_SVG_COLOR;
        }
        return true;
    }
    if (tbox_style_is(property, "stroke-width") || tbox_style_is(property, "stroke-dashoffset")) {
        tbox_style_length length;
        bool width = tbox_style_is(property, "stroke-width");
        if (!tbox_svg_length(value, units, &length) || (width && length.value < 0.0))
            return false;
        if (width)
            paint->width = length;
        else
            paint->dashoffset = length;
        return true;
    }
    if (tbox_style_is(property, "stroke-dasharray")) {
        paint->dash_count = 0;
        if (tbox_style_is(value, "none"))
            return true;
        /* Split at comma/space, keeping functional lengths intact. */
        size_t at = 0;
        while (at < value.size) {
            while (at < value.size && tbox_ascii_is_space(value.data[at]))
                at++;
            size_t start = at, depth = 0;
            while (at < value.size) {
                char c = value.data[at];
                if (c == '(')
                    depth++;
                if (c == ')' && depth > 0)
                    depth--;
                if (depth == 0 && (c == ',' || tbox_ascii_is_space(c)))
                    break;
                at++;
            }
            if (start == at || depth != 0 || paint->dash_count == TBOX_STYLE_MAX_SVG_DASHES)
                return false;
            tbox_style_length length;
            if (!tbox_svg_length(tbox_string_view_make(value.data + start, at - start), units, &length) || length.value < 0.0)
                return false;
            paint->dashes[paint->dash_count++] = length;
            while (at < value.size && tbox_ascii_is_space(value.data[at]))
                at++;
            if (at < value.size && value.data[at] == ',') {
                at++;
                while (at < value.size && tbox_ascii_is_space(value.data[at]))
                    at++;
                if (at == value.size)
                    return false;
            }
        }
        return paint->dash_count > 0;
    }
    if (tbox_style_is(property, "stroke-linecap") || tbox_style_is(property, "stroke-linejoin")) {
        bool cap = tbox_style_is(property, "stroke-linecap");
        unsigned v;
        if (tbox_style_is(value, cap ? "butt" : "miter"))
            v = 0;
        else if (tbox_style_is(value, "round"))
            v = 1;
        else if (tbox_style_is(value, cap ? "square" : "bevel"))
            v = 2;
        else
            return false;
        if (cap)
            paint->linecap = v;
        else
            paint->linejoin = v;
        return true;
    }
    if (tbox_style_is(property, "fill-rule")) {
        if (!tbox_style_is(value, "nonzero") && !tbox_style_is(value, "evenodd"))
            return false;
        paint->evenodd = tbox_style_is(value, "evenodd");
        return true;
    }
    double n;
    if (tbox_style_is(property, "stroke-miterlimit")) {
        if (!tbox_style_parse_number(value, &n) || n < 1)
            return false;
        paint->miterlimit = n;
        return true;
    }
    if (!tbox_style_parse_calc_number(value, units, &n)) {
        if (!tbox_style_parse_number(value, &n)) {
            if (value.size < 2 || value.data[value.size - 1] != '%' || !tbox_style_parse_number(tbox_string_view_make(value.data, value.size - 1), &n))
                return false;
            n /= 100.0;
        }
    }
    if (!isfinite(n))
        return false;
    if (tbox_style_is(property, "fill-opacity"))
        paint->fill_opacity = fmax(0.0, fmin(1.0, n));
    else if (tbox_style_is(property, "stroke-opacity"))
        paint->stroke_opacity = fmax(0.0, fmin(1.0, n));
    else
        return false;
    return true;
}

bool tbox_style_svg_declaration_valid(tbox_string_view property, tbox_string_view value) {
    bool known = false;
    for (size_t i = 0; i < sizeof(tbox_svg_properties) / sizeof(tbox_svg_properties[0]); i++)
        if (tbox_style_is(property, tbox_svg_properties[i]))
            known = true;
    if (!known)
        return true;
    static const char *const wide[] = { "initial", "inherit", "unset", "revert", "revert-layer" };
    for (size_t i = 0; i < sizeof(wide) / sizeof(wide[0]); i++)
        if (tbox_style_is(value, wide[i]))
            return true;
    for (size_t i = 0; i + 4 <= value.size; i++)
        if (tbox_style_is(tbox_string_view_make(value.data + i, 4), "var("))
            return true;
    const tbox_style_units units = { .font_size = 16, .root_font_size = 16, .viewport_width = 800, .viewport_height = 600, .zoom = 1 };
    tbox_style_svg_paint scratch = tbox_svg_initial_paint;
    return tbox_svg_parse_paint(property, tbox_string_view_trim_ascii_space(value), &units, &scratch);
}

void tbox_style_resolve_svg(const tbox_css_computed_style *computed, const tbox_style *parent, const tbox_style_units *units, tbox_style *style, tbox_arena *arena) {
    style->svg_paint           = parent != NULL ? parent->svg_paint : NULL;
    tbox_style_svg_paint paint = *tbox_style_svg_paint_of(parent);
    bool changed               = false;
    for (size_t i = 0; i < sizeof(tbox_svg_properties) / sizeof(tbox_svg_properties[0]); i++) {
        const tbox_css_resolved_declaration *d = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(tbox_svg_properties[i]));
        if (d == NULL)
            continue;
        tbox_style_svg_paint parsed = paint;
        if (tbox_svg_parse_paint(d->property, tbox_string_view_trim_ascii_space(d->value), units, &parsed)) {
            paint   = parsed;
            changed = true;
        }
    }
    if (changed)
        style->svg_paint = tbox_style_group_copy(arena, &paint, sizeof(paint));
}
