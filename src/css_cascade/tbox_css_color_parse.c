#include <tbox/css_cascade.h>

#include <math.h>
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

/* The components of a color function in either syntax: legacy commas
 * ("10, 20, 30, 0.5") or CSS Color 4 spaces with an optional "/ alpha"
 * ("10 20 30 / 50%"). `none` components read as 0 (see
 * tbox_css_color_parse_number). Returns the count including the alpha, or
 * 0 when malformed; `*out_has_alpha` tells whether the last one is alpha. */
static size_t tbox_css_color_components(tbox_string_view args, tbox_string_view out[4], size_t channels, bool *out_has_alpha) {
    *out_has_alpha = false;
    if (memchr(args.data, ',', args.size) != NULL) {
        size_t count = tbox_css_color_split_components(args, out, 4);
        if (count != channels && count != channels + 1)
            return 0;
        for (size_t i = 0; i < count; i++)
            if (out[i].size == 0)
                return 0;
        *out_has_alpha = count == channels + 1;
        return count;
    }
    size_t count = 0, i = 0;
    bool slash = false;
    while (i < args.size) {
        while (i < args.size && tbox_css_color_is_space(args.data[i]))
            i++;
        if (i >= args.size)
            break;
        if (args.data[i] == '/') {
            if (slash || count != channels)
                return 0;
            slash = true;
            i++;
            continue;
        }
        size_t start = i;
        while (i < args.size && !tbox_css_color_is_space(args.data[i]) && args.data[i] != '/')
            i++;
        if (count == 4)
            return 0;
        out[count++] = tbox_string_view_make(args.data + start, i - start);
    }
    if (slash ? count != channels + 1 : count != channels)
        return 0;
    *out_has_alpha = slash;
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
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("none"))) {
        *out_value = 0.0;
        if (out_is_percentage != NULL)
            *out_is_percentage = false;
        return true;
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

/* A hue: a bare number (degrees) or deg/grad/rad/turn. */
static bool tbox_css_color_parse_hue(tbox_string_view text, double *out) {
    text = tbox_css_color_trim(text);
    static const struct {
        const char *unit;
        double degrees;
    } units[] = {
        { "deg", 1.0 }, { "grad", 0.9 }, { "rad", 57.29577951308232 }, { "turn", 360.0 },
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        size_t length = strlen(units[i].unit);
        if (text.size > length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data + text.size - length, length), tbox_string_view_make(units[i].unit, length))) {
            double value;
            bool percent;
            if (!tbox_css_color_parse_number(tbox_string_view_make(text.data, text.size - length), &value, &percent) || percent)
                return false;
            *out = value * units[i].degrees;
            return true;
        }
    }
    bool percent;
    return tbox_css_color_parse_number(text, out, &percent) && !percent;
}

/* An alpha component (number 0..1 or percentage) into 0..1. */
static bool tbox_css_color_parse_alpha(tbox_string_view text, double *out) {
    double value;
    bool percent;
    if (!tbox_css_color_parse_number(text, &value, &percent))
        return false;
    *out = tbox_css_color_clamp(percent ? value / 100.0 : value, 0.0, 1.0);
    return true;
}

bool tbox_css_rgb_to_rgba(tbox_string_view value, tbox_css_rgba *out_color) {
    static const char *const prefixes[] = { "rgb(", "rgba(" };

    tbox_string_view args;
    if (!tbox_css_color_extract_call(value, prefixes, sizeof(prefixes) / sizeof(prefixes[0]), &args)) {
        return false;
    }

    tbox_string_view components[4];
    bool has_alpha;
    size_t count = tbox_css_color_components(args, components, 3, &has_alpha);
    if (count == 0) {
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
    if (has_alpha) {
        double alpha_unit;
        if (!tbox_css_color_parse_alpha(components[3], &alpha_unit)) {
            return false;
        }
        alpha_byte = alpha_unit * 255.0;
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
    bool has_alpha;
    size_t count = tbox_css_color_components(args, components, 3, &has_alpha);
    if (count == 0) {
        return false;
    }
    bool modern = memchr(args.data, ',', args.size) == NULL;

    double hue;
    if (!tbox_css_color_parse_hue(components[0], &hue)) {
        return false;
    }

    /* The space syntax also takes plain numbers for saturation/lightness. */
    double saturation, lightness;
    bool saturation_is_percentage, lightness_is_percentage;
    if (!tbox_css_color_parse_number(components[1], &saturation, &saturation_is_percentage) || (!saturation_is_percentage && !modern)) {
        return false;
    }
    if (!tbox_css_color_parse_number(components[2], &lightness, &lightness_is_percentage) || (!lightness_is_percentage && !modern)) {
        return false;
    }

    tbox_css_hsla hsla = { .h = hue, .s = saturation / 100.0, .l = lightness / 100.0, .a = 1.0 };

    if (has_alpha && !tbox_css_color_parse_alpha(components[3], &hsla.a)) {
        return false;
    }

    tbox_css_rgba result = tbox_css_hsla_to_rgba(hsla);
    if (out_color != NULL) {
        *out_color = result;
    }
    return true;
}

/* ---- CSS Color 4/5: hwb(), lab(), lch(), oklab(), oklch(), color-mix() ---- */

/* A color as linear-light sRGB plus alpha, the common currency of the
 * conversions below (channels may leave 0..1 until gamut clipping). */
typedef struct tbox_css_linear_color {
    double r, g, b, a;
} tbox_css_linear_color;

static double tbox_css_srgb_to_linear(double c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double tbox_css_linear_to_srgb(double c) {
    double sign = c < 0.0 ? -1.0 : 1.0;
    c           = fabs(c);
    return sign * (c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

static tbox_css_linear_color tbox_css_rgba_to_linear(tbox_css_rgba c) {
    return (tbox_css_linear_color){ tbox_css_srgb_to_linear(c.r / 255.0), tbox_css_srgb_to_linear(c.g / 255.0), tbox_css_srgb_to_linear(c.b / 255.0), c.a / 255.0 };
}

/* Clips to the sRGB gamut channel by channel (CSS allows smarter gamut
 * mapping; plain clipping is the common simplification). */
static tbox_css_rgba tbox_css_linear_to_rgba(tbox_css_linear_color c) {
    return (tbox_css_rgba){ tbox_css_color_byte(tbox_css_linear_to_srgb(c.r) * 255.0), tbox_css_color_byte(tbox_css_linear_to_srgb(c.g) * 255.0), tbox_css_color_byte(tbox_css_linear_to_srgb(c.b) * 255.0), tbox_css_color_byte(c.a * 255.0) };
}

static tbox_css_linear_color tbox_css_oklab_to_linear(double l, double a, double b, double alpha) {
    double l_ = l + 0.3963377774 * a + 0.2158037573 * b;
    double m_ = l - 0.1055613458 * a - 0.0638541728 * b;
    double s_ = l - 0.0894841775 * a - 1.2914855480 * b;
    double L = l_ * l_ * l_, M = m_ * m_ * m_, S = s_ * s_ * s_;
    return (tbox_css_linear_color){ 4.0767416621 * L - 3.3077115913 * M + 0.2309699292 * S, -1.2684380046 * L + 2.6097574011 * M - 0.3413193965 * S, -0.0041960863 * L - 0.7034186147 * M + 1.7076147010 * S, alpha };
}

static void tbox_css_linear_to_oklab(tbox_css_linear_color c, double out[3]) {
    double l = cbrt(0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b);
    double m = cbrt(0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b);
    double s = cbrt(0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b);
    out[0]   = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    out[1]   = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    out[2]   = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
}

/* CIE Lab (D50, as CSS defines it) through XYZ, Bradford-adapted to D65. */
static tbox_css_linear_color tbox_css_lab_to_linear(double l, double a, double b, double alpha) {
    const double kappa = 24389.0 / 27.0, epsilon = 216.0 / 24389.0;
    double fy = (l + 16.0) / 116.0, fx = fy + a / 500.0, fz = fy - b / 200.0;
    double x = fx * fx * fx > epsilon ? fx * fx * fx : (116.0 * fx - 16.0) / kappa;
    double y = l > kappa * epsilon ? fy * fy * fy : l / kappa;
    double z = fz * fz * fz > epsilon ? fz * fz * fz : (116.0 * fz - 16.0) / kappa;
    x *= 0.3457 / 0.3585;
    z *= (1.0 - 0.3457 - 0.3585) / 0.3585;
    double X = 0.9554734527042182 * x - 0.023098536874261423 * y + 0.0632593086610217 * z;
    double Y = -0.028369706963208136 * x + 1.0099954580058226 * y + 0.021041398966943008 * z;
    double Z = 0.012314001688319899 * x - 0.020507696433157568 * y + 1.3303659366080753 * z;
    return (tbox_css_linear_color){ 3.2409699419045226 * X - 1.537383177570094 * Y - 0.4986107602930034 * Z, -0.9692436362808796 * X + 1.8759675015077202 * Y + 0.04155505740717559 * Z, 0.05563007969699366 * X - 0.20397695888897652 * Y + 1.0569715142428786 * Z, alpha };
}

/* Lightness-chroma-hue/lab components of lab()/lch()/oklab()/oklch():
 * percentages scale by `percent_scale` (100 for L in lab, 1 in oklab, the
 * a/b or chroma reference for the others). */
static bool tbox_css_color_lab_component(tbox_string_view text, double percent_scale, double *out) {
    double value;
    bool percent;
    if (!tbox_css_color_parse_number(text, &value, &percent))
        return false;
    *out = percent ? value / 100.0 * percent_scale : value;
    return true;
}

static bool tbox_css_color_parse_function(tbox_string_view value, tbox_css_rgba *out_color);

static bool tbox_css_color_parse_space(tbox_string_view value, tbox_css_rgba *out_color) {
    static const char *const hwb[] = { "hwb(" }, *const lab[] = { "lab(" }, *const lch[] = { "lch(" }, *const oklab[] = { "oklab(" }, *const oklch[] = { "oklch(" };
    tbox_string_view args, c[4];
    bool has_alpha;
    double alpha = 1.0;
    if (tbox_css_color_extract_call(value, hwb, 1, &args)) {
        double h, w, b;
        bool wp, bp;
        if (tbox_css_color_components(args, c, 3, &has_alpha) == 0 || !tbox_css_color_parse_hue(c[0], &h) ||
            !tbox_css_color_parse_number(c[1], &w, &wp) || !tbox_css_color_parse_number(c[2], &b, &bp) || (has_alpha && !tbox_css_color_parse_alpha(c[3], &alpha)))
            return false;
        w /= 100.0, b /= 100.0;
        if (w + b >= 1.0) {
            double gray = w / (w + b);
            *out_color  = (tbox_css_rgba){ tbox_css_color_byte(gray * 255.0), tbox_css_color_byte(gray * 255.0), tbox_css_color_byte(gray * 255.0), tbox_css_color_byte(alpha * 255.0) };
            return true;
        }
        tbox_css_rgba pure = tbox_css_hsla_to_rgba((tbox_css_hsla){ h, 1.0, 0.5, 1.0 });
        double channels[3] = { pure.r / 255.0, pure.g / 255.0, pure.b / 255.0 };
        for (int i = 0; i < 3; i++)
            channels[i] = channels[i] * (1.0 - w - b) + w;
        *out_color = (tbox_css_rgba){ tbox_css_color_byte(channels[0] * 255.0), tbox_css_color_byte(channels[1] * 255.0), tbox_css_color_byte(channels[2] * 255.0), tbox_css_color_byte(alpha * 255.0) };
        return true;
    }
    bool is_lab = tbox_css_color_extract_call(value, lab, 1, &args), is_lch = !is_lab && tbox_css_color_extract_call(value, lch, 1, &args);
    bool is_oklab = !is_lab && !is_lch && tbox_css_color_extract_call(value, oklab, 1, &args), is_oklch = !is_lab && !is_lch && !is_oklab && tbox_css_color_extract_call(value, oklch, 1, &args);
    if (!is_lab && !is_lch && !is_oklab && !is_oklch)
        return tbox_css_color_parse_function(value, out_color);
    bool ok = tbox_css_color_components(args, c, 3, &has_alpha) != 0 && (!has_alpha || tbox_css_color_parse_alpha(c[3], &alpha));
    double l, x, y;
    bool ok_lab = is_oklab || is_oklch;
    ok          = ok && tbox_css_color_lab_component(c[0], ok_lab ? 1.0 : 100.0, &l);
    if (is_lab || is_oklab) {
        double scale = is_oklab ? 0.4 : 125.0;
        ok           = ok && tbox_css_color_lab_component(c[1], scale, &x) && tbox_css_color_lab_component(c[2], scale, &y);
    } else {
        double chroma, hue;
        ok = ok && tbox_css_color_lab_component(c[1], is_oklch ? 0.4 : 150.0, &chroma) && tbox_css_color_parse_hue(c[2], &hue);
        if (ok) {
            double radians = hue * 3.141592653589793 / 180.0;
            x              = chroma * cos(radians);
            y              = chroma * sin(radians);
        }
    }
    if (!ok)
        return false;
    *out_color = tbox_css_linear_to_rgba(ok_lab ? tbox_css_oklab_to_linear(l, x, y, alpha) : tbox_css_lab_to_linear(l, x, y, alpha));
    return true;
}

/* Splits `args` at top-level commas (outside parentheses). */
static size_t tbox_css_color_split_nested(tbox_string_view args, tbox_string_view *out, size_t max) {
    size_t count = 0, start = 0;
    int depth = 0;
    for (size_t i = 0; i <= args.size; i++) {
        char ch = i < args.size ? args.data[i] : ',';
        if (ch == '(')
            depth++;
        else if (ch == ')' && depth > 0)
            depth--;
        else if (ch == ',' && depth == 0) {
            if (count == max)
                return 0;
            out[count++] = tbox_css_color_trim(tbox_string_view_make(args.data + start, i - start));
            start        = i + 1;
        }
    }
    return count;
}

/* "<color> [<percentage>]" of color-mix(). */
static bool tbox_css_color_mix_operand(tbox_string_view text, tbox_css_rgba *color, double *percent, bool *has_percent) {
    text          = tbox_css_color_trim(text);
    *has_percent  = false;
    size_t split  = text.size;
    int depth     = 0;
    size_t last_space = text.size;
    for (size_t i = 0; i < text.size; i++) {
        if (text.data[i] == '(') depth++;
        else if (text.data[i] == ')') depth--;
        else if (depth == 0 && tbox_css_color_is_space(text.data[i])) last_space = i;
    }
    if (last_space < text.size && text.data[text.size - 1] == '%') {
        bool is_percent;
        if (tbox_css_color_parse_number(tbox_string_view_make(text.data + last_space + 1, text.size - last_space - 1), percent, &is_percent) && is_percent) {
            *has_percent = true;
            split        = last_space;
        }
    }
    /* The percentage may also come first. */
    if (!*has_percent && text.size > 0) {
        size_t first_space = 0;
        while (first_space < text.size && !tbox_css_color_is_space(text.data[first_space]))
            first_space++;
        bool is_percent;
        if (first_space < text.size && text.data[first_space - 1] == '%' && tbox_css_color_parse_number(tbox_string_view_make(text.data, first_space), percent, &is_percent) && is_percent) {
            *has_percent = true;
            return tbox_css_color_parse(tbox_css_color_trim(tbox_string_view_make(text.data + first_space, text.size - first_space)), color);
        }
    }
    return tbox_css_color_parse(tbox_css_color_trim(tbox_string_view_make(text.data, split)), color);
}

/* color-mix(in <space> [<hue method>], <color> [p%], <color> [p%]), mixed
 * in srgb, srgb-linear, oklab, oklch or (other rectangular spaces) oklab,
 * with premultiplied alpha as CSS specifies. */
static bool tbox_css_color_parse_function(tbox_string_view value, tbox_css_rgba *out_color) {
    static const char *const mix[] = { "color-mix(" };
    tbox_string_view args, parts[3];
    if (!tbox_css_color_extract_call(value, mix, 1, &args) || tbox_css_color_split_nested(args, parts, 3) != 3)
        return false;
    tbox_string_view space = parts[0];
    if (space.size < 3 || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(space.data, 3), tbox_string_view_from_cstr("in ")))
        return false;
    space = tbox_css_color_trim(tbox_string_view_make(space.data + 3, space.size - 3));
    size_t word = 0;
    while (word < space.size && !tbox_css_color_is_space(space.data[word]))
        word++;
    space = tbox_string_view_make(space.data, word);

    tbox_css_rgba colors[2];
    double percents[2];
    bool has[2];
    for (int i = 0; i < 2; i++)
        if (!tbox_css_color_mix_operand(parts[i + 1], &colors[i], &percents[i], &has[i]))
            return false;
    if (!has[0] && !has[1]) percents[0] = percents[1] = 50.0;
    else if (!has[0]) percents[0] = 100.0 - percents[1];
    else if (!has[1]) percents[1] = 100.0 - percents[0];
    double total = percents[0] + percents[1];
    if (total <= 0.0)
        return false;
    double alpha_scale = total < 100.0 ? total / 100.0 : 1.0;
    double w1 = percents[1] / total, w0 = 1.0 - w1;

    tbox_css_linear_color a = tbox_css_rgba_to_linear(colors[0]), b = tbox_css_rgba_to_linear(colors[1]);
    double alpha            = a.a * w0 + b.a * w1;
    tbox_css_linear_color result;
    if (tbox_string_view_equal_ascii_ci(space, tbox_string_view_from_cstr("srgb"))) {
        double ca[3] = { colors[0].r / 255.0 * a.a, colors[0].g / 255.0 * a.a, colors[0].b / 255.0 * a.a };
        double cb[3] = { colors[1].r / 255.0 * b.a, colors[1].g / 255.0 * b.a, colors[1].b / 255.0 * b.a };
        double mixed[3];
        for (int i = 0; i < 3; i++)
            mixed[i] = alpha > 0.0 ? (ca[i] * w0 + cb[i] * w1) / alpha : 0.0;
        result = (tbox_css_linear_color){ tbox_css_srgb_to_linear(mixed[0]), tbox_css_srgb_to_linear(mixed[1]), tbox_css_srgb_to_linear(mixed[2]), alpha };
    } else if (tbox_string_view_equal_ascii_ci(space, tbox_string_view_from_cstr("srgb-linear"))) {
        result = (tbox_css_linear_color){ alpha > 0.0 ? (a.r * a.a * w0 + b.r * b.a * w1) / alpha : 0.0, alpha > 0.0 ? (a.g * a.a * w0 + b.g * b.a * w1) / alpha : 0.0, alpha > 0.0 ? (a.b * a.a * w0 + b.b * b.a * w1) / alpha : 0.0, alpha };
    } else {
        double la[3], lb[3], mixed[3];
        tbox_css_linear_to_oklab(a, la);
        tbox_css_linear_to_oklab(b, lb);
        if (tbox_string_view_equal_ascii_ci(space, tbox_string_view_from_cstr("oklch")) || tbox_string_view_equal_ascii_ci(space, tbox_string_view_from_cstr("lch")) || tbox_string_view_equal_ascii_ci(space, tbox_string_view_from_cstr("hsl"))) {
            /* Polar: chroma and the shorter hue arc interpolate. */
            double ca = hypot(la[1], la[2]), cb = hypot(lb[1], lb[2]);
            double ha = atan2(la[2], la[1]), hb = atan2(lb[2], lb[1]);
            if (ca < 1e-6) ha = hb;
            if (cb < 1e-6) hb = ha;
            double dh = hb - ha;
            if (dh > 3.141592653589793) dh -= 2.0 * 3.141592653589793;
            if (dh < -3.141592653589793) dh += 2.0 * 3.141592653589793;
            double l = alpha > 0.0 ? (la[0] * a.a * w0 + lb[0] * b.a * w1) / alpha : 0.0;
            double c = alpha > 0.0 ? (ca * a.a * w0 + cb * b.a * w1) / alpha : 0.0;
            double h = ha + dh * w1;
            mixed[0] = l, mixed[1] = c * cos(h), mixed[2] = c * sin(h);
        } else {
            for (int i = 0; i < 3; i++)
                mixed[i] = alpha > 0.0 ? (la[i] * a.a * w0 + lb[i] * b.a * w1) / alpha : 0.0;
        }
        result = tbox_css_oklab_to_linear(mixed[0], mixed[1], mixed[2], alpha);
    }
    result.a *= alpha_scale;
    *out_color = tbox_css_linear_to_rgba(result);
    return true;
}

bool tbox_css_color_parse(tbox_string_view value, tbox_css_rgba *out_color) {
    /* `transparent` is not one of the named colors but a keyword for
     * transparent black. */
    size_t start = 0, end = value.size;
    while (start < end && (value.data[start] == ' ' || value.data[start] == '\t' || value.data[start] == '\n' || value.data[start] == '\r' || value.data[start] == '\f'))
        start++;
    while (end > start && (value.data[end - 1] == ' ' || value.data[end - 1] == '\t' || value.data[end - 1] == '\n' || value.data[end - 1] == '\r' || value.data[end - 1] == '\f'))
        end--;
    if (tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data + start, end - start), tbox_string_view_from_cstr("transparent"))) {
        *out_color = (tbox_css_rgba){ 0, 0, 0, 0 };
        return true;
    }
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
    default: {
        tbox_css_rgba color;
        if (!tbox_css_color_parse_space(tbox_string_view_make(value.data + start, end - start), &color))
            return false;
        if (out_color != NULL)
            *out_color = color;
        return true;
    }
    }
}
