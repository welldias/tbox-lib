/* `border-image` and its longhands (CSS Backgrounds 3 section 6). Render
 * paints the nine slices (tbox_render.c); this file only resolves the
 * values. */

#include <string.h>

#include <tbox/style.h>

#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

static tbox_style_border_image_value tbox_style_border_image_number(double number) {
    tbox_style_border_image_value value;
    memset(&value, 0, sizeof(value));
    value.kind   = TBOX_STYLE_BORDER_IMAGE_NUMBER;
    value.number = number;
    return value;
}

static void tbox_style_border_image_init(tbox_style_border_image *image) {
    memset(image, 0, sizeof(*image));
    tbox_style_init_gradient(&image->gradient);
    for (size_t i = 0; i < 4; i++) {
        image->slice[i].kind   = TBOX_STYLE_BORDER_IMAGE_LENGTH;
        image->slice[i].length = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 };
        image->width[i]        = tbox_style_border_image_number(1.0);
        image->outset[i]       = tbox_style_border_image_number(0.0);
    }
}

/* none, url() or a gradient. */
static bool tbox_style_border_image_parse_source(tbox_string_view token, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_border_image *out) {
    if (tbox_style_is(token, "none")) {
        out->image[0]      = '\0';
        out->gradient.kind = TBOX_STYLE_GRADIENT_NONE;
        return true;
    }
    char url[256];
    if (tbox_style_parse_url(token, url, sizeof(url))) {
        memcpy(out->image, url, sizeof(url));
        out->gradient.kind = TBOX_STYLE_GRADIENT_NONE;
        return true;
    }
    tbox_style_gradient gradient;
    if (tbox_style_parse_gradient(token, units, current_color, &gradient)) {
        out->image[0] = '\0';
        out->gradient = gradient;
        return true;
    }
    return false;
}

/* What a side value may be. */
enum {
    TBOX_STYLE_BI_NUMBER  = 1, /* <number> */
    TBOX_STYLE_BI_PERCENT = 2, /* <percentage> */
    TBOX_STYLE_BI_LENGTH  = 4, /* <length> */
    TBOX_STYLE_BI_AUTO    = 8,
};

static bool tbox_style_border_image_parse_value(tbox_string_view token, const tbox_style_units *units, unsigned allowed, tbox_style_border_image_value *out) {
    double number;
    if ((allowed & TBOX_STYLE_BI_NUMBER) && tbox_style_parse_number(token, &number)) {
        if (number < 0.0)
            return false;
        *out = tbox_style_border_image_number(number);
        return true;
    }
    if ((allowed & TBOX_STYLE_BI_AUTO) && tbox_style_is(token, "auto")) {
        *out      = tbox_style_border_image_number(0.0);
        out->kind = TBOX_STYLE_BORDER_IMAGE_AUTO;
        return true;
    }
    if (tbox_style_is(token, "auto"))
        return false;
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(token, units, &length))
        return false;
    if (length.kind == TBOX_STYLE_LENGTH_PERCENT ? !(allowed & TBOX_STYLE_BI_PERCENT) : !(allowed & TBOX_STYLE_BI_LENGTH))
        return false;
    if (length.value < 0.0 && length.px_offset == 0.0)
        return false;
    *out        = tbox_style_border_image_number(0.0);
    out->kind   = TBOX_STYLE_BORDER_IMAGE_LENGTH;
    out->length = length;
    return true;
}

/* One to four side values, expanded like margin's. */
static bool tbox_style_border_image_parse_sides(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, unsigned allowed, tbox_style_border_image_value out[4]) {
    if (count == 0 || count > 4)
        return false;
    tbox_style_border_image_value parsed[4];
    for (size_t i = 0; i < count; i++)
        if (!tbox_style_border_image_parse_value(tokens[i], units, allowed, &parsed[i]))
            return false;
    for (size_t side = 0; side < 4; side++)
        out[side] = parsed[tbox_style_four_value_index(count, side)];
    return true;
}

/* `border-image-slice`: 1-4 numbers/percentages and `fill` before or
 * after them. */
static bool tbox_style_border_image_parse_slice(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_border_image_value out[4], bool *fill) {
    bool has_fill = false;
    size_t begin = 0, end = count;
    if (count > 0 && tbox_style_is(tokens[0], "fill"))
        has_fill = true, begin = 1;
    else if (count > 0 && tbox_style_is(tokens[count - 1], "fill"))
        has_fill = true, end = count - 1;
    if (!tbox_style_border_image_parse_sides(tokens + begin, end - begin, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_PERCENT, out))
        return false;
    *fill = has_fill;
    return true;
}

static bool tbox_style_border_image_repeat_of(tbox_string_view token, tbox_style_border_image_repeat *out) {
    static const char *const names[] = { "stretch", "repeat", "round", "space" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (tbox_style_is(token, names[i])) {
            *out = (tbox_style_border_image_repeat)i;
            return true;
        }
    return false;
}

static bool tbox_style_border_image_parse_repeat(const tbox_string_view *tokens, size_t count, tbox_style_border_image_repeat out[2]) {
    if (count == 0 || count > 2)
        return false;
    tbox_style_border_image_repeat parsed[2];
    for (size_t i = 0; i < count; i++)
        if (!tbox_style_border_image_repeat_of(tokens[i], &parsed[i]))
            return false;
    out[0] = parsed[0];
    out[1] = parsed[count - 1];
    return true;
}

static bool tbox_style_border_image_is_slice_token(tbox_string_view token, const tbox_style_units *units) {
    tbox_style_border_image_value value;
    return tbox_style_is(token, "fill") || tbox_style_border_image_parse_value(token, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_PERCENT, &value);
}

static bool tbox_style_border_image_is_width_token(tbox_string_view token, const tbox_style_units *units) {
    tbox_style_border_image_value value;
    return tbox_style_border_image_parse_value(token, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_PERCENT | TBOX_STYLE_BI_LENGTH | TBOX_STYLE_BI_AUTO, &value);
}

/* The `border-image` shorthand: <source> || <slice> [/ <width>? [/
 * <outset>]?]? || <repeat>. Omitted parts take their initial values. */
static bool tbox_style_border_image_parse_shorthand(tbox_string_view value, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_border_image *out) {
    tbox_string_view tokens[24];
    size_t count = tbox_style_tokens(value, tokens, 24);
    if (count == 0)
        return false;
    tbox_style_border_image parsed;
    tbox_style_border_image_init(&parsed);
    bool has_source = false, has_slice = false, has_repeat = false;
    tbox_style_border_image_repeat repeat;
    for (size_t i = 0; i < count;) {
        if (!has_source && tbox_style_border_image_parse_source(tokens[i], units, current_color, &parsed)) {
            has_source = true;
            i++;
        } else if (!has_repeat && tbox_style_border_image_repeat_of(tokens[i], &repeat)) {
            size_t begin = i;
            while (i < count && i - begin < 2 && tbox_style_border_image_repeat_of(tokens[i], &repeat))
                i++;
            if (!tbox_style_border_image_parse_repeat(tokens + begin, i - begin, parsed.repeat))
                return false;
            has_repeat = true;
        } else if (!has_slice && tbox_style_border_image_is_slice_token(tokens[i], units)) {
            size_t begin = i;
            while (i < count && i - begin < 6 && tbox_style_border_image_is_slice_token(tokens[i], units))
                i++;
            if (!tbox_style_border_image_parse_slice(tokens + begin, i - begin, units, parsed.slice, &parsed.fill))
                return false;
            has_slice = true;
            if (i < count && tokens[i].size == 1 && tokens[i].data[0] == '/') {
                begin = ++i;
                while (i < count && i - begin < 4 && tbox_style_border_image_is_width_token(tokens[i], units))
                    i++;
                bool has_width = i > begin;
                if (has_width && !tbox_style_border_image_parse_sides(tokens + begin, i - begin, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_PERCENT | TBOX_STYLE_BI_LENGTH | TBOX_STYLE_BI_AUTO, parsed.width))
                    return false;
                if (i < count && tokens[i].size == 1 && tokens[i].data[0] == '/') {
                    begin = ++i;
                    while (i < count && i - begin < 4 && !(tokens[i].size == 1 && tokens[i].data[0] == '/') && tbox_style_border_image_is_width_token(tokens[i], units))
                        i++;
                    if (!tbox_style_border_image_parse_sides(tokens + begin, i - begin, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_LENGTH, parsed.outset))
                        return false;
                } else if (!has_width) {
                    return false; /* a lone '/' */
                }
            }
        } else {
            return false;
        }
    }
    *out = parsed;
    return true;
}

static bool tbox_style_border_image_parse_longhand(size_t which, tbox_string_view value, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_border_image *out) {
    tbox_string_view tokens[8];
    size_t count = tbox_style_tokens(value, tokens, 8);
    switch (which) {
    case 0:
        return count == 1 && tbox_style_border_image_parse_source(tokens[0], units, current_color, out);
    case 1: {
        tbox_style_border_image_value slice[4];
        bool fill;
        if (!tbox_style_border_image_parse_slice(tokens, count, units, slice, &fill))
            return false;
        memcpy(out->slice, slice, sizeof(slice));
        out->fill = fill;
        return true;
    }
    case 2:
        return tbox_style_border_image_parse_sides(tokens, count, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_PERCENT | TBOX_STYLE_BI_LENGTH | TBOX_STYLE_BI_AUTO, out->width);
    case 3:
        return tbox_style_border_image_parse_sides(tokens, count, units, TBOX_STYLE_BI_NUMBER | TBOX_STYLE_BI_LENGTH, out->outset);
    default:
        return tbox_style_border_image_parse_repeat(tokens, count, out->repeat);
    }
}

void tbox_style_resolve_border_image(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style) {
    /* `border` resets border-image to its initial value, `border-image`
     * sets every longhand: apply the declarations from the lowest cascade
     * priority up, so the winner of each part is the last one applied. */
    static const char *const names[] = {
        "border-image-source",
        "border-image-slice",
        "border-image-width",
        "border-image-outset",
        "border-image-repeat",
        "border-image",
        "border",
    };
    enum {
        SHORTHAND  = 5,
        BORDER     = 6,
        NAME_COUNT = 7
    };
    const tbox_css_resolved_declaration *decls[NAME_COUNT];
    size_t kinds[NAME_COUNT], count = 0;
    for (size_t n = 0; n < NAME_COUNT; n++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(names[n]));
        if (decl == NULL)
            continue;
        size_t at = count++;
        while (at > 0 && tbox_css_cascade_priority_compare(decls[at - 1], decl) > 0) {
            decls[at] = decls[at - 1];
            kinds[at] = kinds[at - 1];
            at--;
        }
        decls[at] = decl;
        kinds[at] = n;
    }
    tbox_style_border_image image;
    tbox_style_border_image_init(&image);
    for (size_t i = 0; i < count; i++) {
        if (kinds[i] == BORDER) {
            tbox_style_border_image_init(&image);
        } else if (kinds[i] == SHORTHAND) {
            tbox_style_border_image_parse_shorthand(decls[i]->value, units, style->color, &image);
        } else {
            /* an invalid value leaves the part alone */
            tbox_style_border_image_parse_longhand(kinds[i], decls[i]->value, units, style->color, &image);
        }
    }
    style->border_image     = image;
    style->has_border_image = image.image[0] != '\0' || image.gradient.kind != TBOX_STYLE_GRADIENT_NONE;
}
