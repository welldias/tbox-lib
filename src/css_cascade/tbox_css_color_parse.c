#include <tbox/css_cascade.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"

/* CSS2.1 "white space" -- same definition used across this module (see
 * tbox_css_cascade.c). Only used here to trim the inside of a component
 * ("rgb( 10 , 20, 30 )"), never the whole `value` argument: every public
 * function in this file requires the caller to have already trimmed
 * leading/trailing whitespace off `value` itself, matching
 * tbox_css_hex_to_rgba's contract. */
static bool tbox_css_color_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

static tbox_string_view tbox_css_color_trim(tbox_string_view value) {
    size_t start = 0;
    while (start < value.size && tbox_css_color_is_space(value.data[start])) {
        start++;
    }
    size_t end = value.size;
    while (end > start && tbox_css_color_is_space(value.data[end - 1])) {
        end--;
    }
    return tbox_string_view_make(value.data + start, end - start);
}

static bool tbox_css_color_starts_with_ci(tbox_string_view value, const char *prefix) {
    size_t prefix_length = strlen(prefix);
    if (value.size < prefix_length) {
        return false;
    }
    return tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data, prefix_length), tbox_string_view_from_cstr(prefix));
}

tbox_css_color_format tbox_css_color_detect_format(tbox_string_view value) {
    if (value.data == NULL || value.size == 0) {
        return TBOX_CSS_COLOR_FORMAT_UNKNOWN;
    }
    if (value.data[0] == '#') {
        return TBOX_CSS_COLOR_FORMAT_HEXA;
    }
    if (tbox_css_color_starts_with_ci(value, "rgb(") || tbox_css_color_starts_with_ci(value, "rgba(")) {
        return TBOX_CSS_COLOR_FORMAT_RGBA;
    }
    if (tbox_css_color_starts_with_ci(value, "hsl(") || tbox_css_color_starts_with_ci(value, "hsla(")) {
        return TBOX_CSS_COLOR_FORMAT_HSLA;
    }
    if (tbox_css_named_color_find(value, NULL)) {
        return TBOX_CSS_COLOR_FORMAT_COLOR_NAME;
    }
    return TBOX_CSS_COLOR_FORMAT_UNKNOWN;
}

/* If `value` (already trimmed by the caller) is exactly one of `prefixes`
 * (case-insensitive, each including its opening '(') followed by an
 * argument list and a closing ')', returns true and sets *out_args to the
 * text between them (not yet trimmed or split). */
static bool tbox_css_color_extract_call(tbox_string_view value, const char *const *prefixes, size_t prefix_count, tbox_string_view *out_args) {
    if (value.size == 0 || value.data[value.size - 1] != ')') {
        return false;
    }
    for (size_t i = 0; i < prefix_count; i++) {
        size_t prefix_length = strlen(prefixes[i]);
        if (value.size >= prefix_length && tbox_css_color_starts_with_ci(value, prefixes[i])) {
            *out_args = tbox_string_view_make(value.data + prefix_length, value.size - prefix_length - 1);
            return true;
        }
    }
    return false;
}

/* Splits `args` on top-level ',' into at most `max_components` pieces
 * (trimmed of surrounding whitespace), writing them to `out_components`.
 * Returns the component count, or 0 if there are more than
 * `max_components` of them. */
static size_t tbox_css_color_split_components(tbox_string_view args, tbox_string_view *out_components, size_t max_components) {
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= args.size; i++) {
        if (i == args.size || args.data[i] == ',') {
            if (count >= max_components) {
                return 0;
            }
            out_components[count++] = tbox_css_color_trim(tbox_string_view_make(args.data + start, i - start));
            start                   = i + 1;
        }
    }
    return count;
}

/* Parses one numeric CSS component -- a plain number ("128", "-3.5") or a
 * percentage ("50%") -- via strtod, after copying it into a small stack
 * buffer since tbox_string_view isn't NUL-terminated. Rejects anything that
 * doesn't consume the whole (trimmed) text, including empty input or a
 * component too long to plausibly be a CSS number/percentage. */
static bool tbox_css_color_parse_number(tbox_string_view text, double *out_value, bool *out_is_percentage) {
    text = tbox_css_color_trim(text);
    if (text.size == 0 || text.size >= 64) {
        return false;
    }

    bool is_percentage   = false;
    size_t number_length = text.size;
    if (text.data[text.size - 1] == '%') {
        is_percentage = true;
        number_length = text.size - 1;
        if (number_length == 0) {
            return false;
        }
    }

    char buffer[64];
    memcpy(buffer, text.data, number_length);
    buffer[number_length] = '\0';

    char *end     = NULL;
    double result = strtod(buffer, &end);
    if (end != buffer + number_length) {
        return false;
    }

    *out_value = result;
    if (out_is_percentage != NULL) {
        *out_is_percentage = is_percentage;
    }
    return true;
}

static double tbox_css_color_clamp(double value, double lo, double hi) {
    if (value < lo) {
        return lo;
    }
    if (value > hi) {
        return hi;
    }
    return value;
}

static unsigned char tbox_css_color_byte(double value_0_to_255) {
    return (unsigned char)(tbox_css_color_clamp(value_0_to_255, 0.0, 255.0) + 0.5);
}

bool tbox_css_rgb_to_rgba(tbox_string_view value, tbox_css_rgba *out_color) {
    static const char *const prefixes[] = { "rgb(", "rgba(" };

    tbox_string_view args;
    if (!tbox_css_color_extract_call(value, prefixes, sizeof(prefixes) / sizeof(prefixes[0]), &args)) {
        return false;
    }

    tbox_string_view components[4];
    size_t count = tbox_css_color_split_components(args, components, 4);
    if (count != 3 && count != 4) {
        return false;
    }

    double channel[3];
    for (size_t i = 0; i < 3; i++) {
        double number;
        bool is_percentage;
        if (!tbox_css_color_parse_number(components[i], &number, &is_percentage)) {
            return false;
        }
        channel[i] = is_percentage ? (number / 100.0) * 255.0 : number;
    }

    double alpha_byte = 255.0;
    if (count == 4) {
        double number;
        bool is_percentage;
        if (!tbox_css_color_parse_number(components[3], &number, &is_percentage)) {
            return false;
        }
        double alpha_unit = is_percentage ? number / 100.0 : number;
        alpha_byte        = tbox_css_color_clamp(alpha_unit, 0.0, 1.0) * 255.0;
    }

    if (out_color != NULL) {
        out_color->r = tbox_css_color_byte(channel[0]);
        out_color->g = tbox_css_color_byte(channel[1]);
        out_color->b = tbox_css_color_byte(channel[2]);
        out_color->a = tbox_css_color_byte(alpha_byte);
    }
    return true;
}

bool tbox_css_hsl_to_rgba(tbox_string_view value, tbox_css_rgba *out_color) {
    static const char *const prefixes[] = { "hsl(", "hsla(" };

    tbox_string_view args;
    if (!tbox_css_color_extract_call(value, prefixes, sizeof(prefixes) / sizeof(prefixes[0]), &args)) {
        return false;
    }

    tbox_string_view components[4];
    size_t count = tbox_css_color_split_components(args, components, 4);
    if (count != 3 && count != 4) {
        return false;
    }

    double hue;
    bool hue_is_percentage;
    if (!tbox_css_color_parse_number(components[0], &hue, &hue_is_percentage) || hue_is_percentage) {
        return false;
    }

    double saturation, lightness;
    bool saturation_is_percentage, lightness_is_percentage;
    if (!tbox_css_color_parse_number(components[1], &saturation, &saturation_is_percentage) || !saturation_is_percentage) {
        return false;
    }
    if (!tbox_css_color_parse_number(components[2], &lightness, &lightness_is_percentage) || !lightness_is_percentage) {
        return false;
    }

    tbox_css_hsla hsla = { .h = hue, .s = saturation / 100.0, .l = lightness / 100.0, .a = 1.0 };

    if (count == 4) {
        double alpha;
        bool alpha_is_percentage;
        if (!tbox_css_color_parse_number(components[3], &alpha, &alpha_is_percentage)) {
            return false;
        }
        hsla.a = alpha_is_percentage ? alpha / 100.0 : alpha;
    }

    tbox_css_rgba result = tbox_css_hsla_to_rgba(hsla);
    if (out_color != NULL) {
        *out_color = result;
    }
    return true;
}

bool tbox_css_color_parse(tbox_string_view value, tbox_css_rgba *out_color) {
    switch (tbox_css_color_detect_format(value)) {
    case TBOX_CSS_COLOR_FORMAT_HEXA:
        return tbox_css_hex_to_rgba(value, out_color);
    case TBOX_CSS_COLOR_FORMAT_RGBA:
        return tbox_css_rgb_to_rgba(value, out_color);
    case TBOX_CSS_COLOR_FORMAT_HSLA:
        return tbox_css_hsl_to_rgba(value, out_color);
    case TBOX_CSS_COLOR_FORMAT_COLOR_NAME:
        return tbox_css_named_color_find(value, out_color);
    case TBOX_CSS_COLOR_FORMAT_UNKNOWN:
    default:
        return false;
    }
}
