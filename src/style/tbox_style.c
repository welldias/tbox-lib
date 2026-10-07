#include <tbox/style.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "style/tbox_style_internal.h"

/* What relative length units resolve against: em/ex/ch the element's own
 * font size, rem the root's, vw/vh/vmin/vmax the viewport (0 = unknown). */
typedef struct tbox_style_units {
    double font_size, root_font_size, viewport_width, viewport_height;
} tbox_style_units;

/* CSS2.1 "white space": space, tab, line feed, carriage return, form feed --
 * same definition tbox_css_cascade.c and tbox_css_color_parse.c use. */
static bool tbox_style_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

static tbox_string_view tbox_style_trim(tbox_string_view value) {
    size_t start = 0;
    while (start < value.size && tbox_style_is_space(value.data[start])) {
        start++;
    }
    size_t end = value.size;
    while (end > start && tbox_style_is_space(value.data[end - 1])) {
        end--;
    }
    return tbox_string_view_make(value.data + start, end - start);
}

static size_t tbox_style_tokens(tbox_string_view text, tbox_string_view *tokens, size_t max);
static bool tbox_style_parse_radius_list(const tbox_string_view *tokens, size_t count, const struct tbox_style_units *units, double px[4], double percent[4]);

/* Parses a plain number ("10", "-3.5") via strtod, after copying it into a
 * small stack buffer since tbox_string_view isn't NUL-terminated -- same
 * approach as tbox_css_color_parse_number in tbox_css_color_parse.c. Rejects
 * anything that doesn't consume the whole text, including empty input or
 * text too long to plausibly be a CSS number. */
static bool tbox_style_parse_number(tbox_string_view text, double *out_value) {
    if (text.size == 0 || text.size >= 64) {
        return false;
    }

    char buffer[64];
    memcpy(buffer, text.data, text.size);
    buffer[text.size] = '\0';

    char *end     = NULL;
    double result = strtod(buffer, &end);
    if (end != buffer + text.size) {
        return false;
    }

    *out_value = result;
    return true;
}

/* Used when a caller has no node context (the `font` shorthand's size
 * detection): 16px em/rem, no viewport, so vw/vh/vmin/vmax fail. */
static const tbox_style_units tbox_style_default_units = { 16.0, 16.0, 0.0, 0.0 };

/* px per one `unit`, or false for an unknown unit (or a viewport unit with
 * no viewport). em is `em_base` -- the element's own font size, except for
 * `font-size` itself, which uses the parent's; rem is the root element's
 * font size; ex and ch use CSS's 0.5em fallback, since this layer has no
 * font metrics; absolute units use CSS's fixed 96px-per-inch ratios. */
static bool tbox_style_unit_scale(tbox_string_view unit, const tbox_style_units *units, double em_base, double *out) {
    static const struct {
        const char *name;
        double px;
    } absolute[] = {
        { "px", 1.0           },
        { "pt", 96.0 / 72.0   },
        { "pc", 16.0          },
        { "in", 96.0          },
        { "cm", 96.0 / 2.54   },
        { "mm", 96.0 / 25.4   },
        { "q",  96.0 / 101.6  },
    };
    for (size_t i = 0; i < sizeof(absolute) / sizeof(absolute[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(unit, tbox_string_view_from_cstr(absolute[i].name))) {
            *out = absolute[i].px;
            return true;
        }
    }
    double vw = units->viewport_width / 100.0, vh = units->viewport_height / 100.0;
    const struct {
        const char *name;
        double px;
        bool viewport;
    } relative[] = {
        { "em",   em_base,                 false },
        { "rem",  units->root_font_size,   false },
        { "ex",   em_base * 0.5,           false },
        { "ch",   em_base * 0.5,           false },
        { "vw",   vw,                      true  },
        { "vh",   vh,                      true  },
        { "vmin", vw < vh ? vw : vh,       true  },
        { "vmax", vw > vh ? vw : vh,       true  },
    };
    for (size_t i = 0; i < sizeof(relative) / sizeof(relative[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(unit, tbox_string_view_from_cstr(relative[i].name))) {
            if (relative[i].viewport && (units->viewport_width <= 0.0 || units->viewport_height <= 0.0))
                return false;
            *out = relative[i].px;
            return true;
        }
    }
    return false;
}

/* A number immediately followed by a unit ("12px", "1.5rem") into px.
 * A bare number or a percentage fails. */
static bool tbox_style_parse_dimension(tbox_string_view text, const tbox_style_units *units, double em_base, double *out) {
    size_t split = text.size;
    while (split > 0 && ((text.data[split - 1] >= 'a' && text.data[split - 1] <= 'z') || (text.data[split - 1] >= 'A' && text.data[split - 1] <= 'Z')))
        split--;
    if (split == 0 || split == text.size)
        return false;
    /* "1e3px": an exponent letter belongs to the number, not the unit. */
    double number, scale;
    if (!tbox_style_parse_number(tbox_string_view_make(text.data, split), &number) ||
        !tbox_style_unit_scale(tbox_string_view_make(text.data + split, text.size - split), units, em_base, &scale))
        return false;
    *out = number * scale;
    return true;
}

/* calc() value: px + percent, or a plain number (`number` set) for the
 * operands of `*` and `/`. */
typedef struct tbox_style_calc_value {
    double px, percent;
    bool number;
    /* px bounds of a min()/max()/clamp() over a percentage; such a value
     * can't take part in further arithmetic. */
    unsigned char bounds;
    double lo, hi;
} tbox_style_calc_value;

typedef struct tbox_style_calc_parser {
    tbox_string_view text;
    size_t pos;
    const tbox_style_units *units;
    double em_base;
    int depth;
} tbox_style_calc_parser;

static void tbox_style_calc_skip_space(tbox_style_calc_parser *p) {
    while (p->pos < p->text.size && tbox_style_is_space(p->text.data[p->pos]))
        p->pos++;
}

static bool tbox_style_calc_sum(tbox_style_calc_parser *p, tbox_style_calc_value *out);

static bool tbox_style_calc_operand(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    tbox_style_calc_skip_space(p);
    if (p->pos >= p->text.size || p->depth > 16)
        return false;
    const char *rest = p->text.data + p->pos;
    size_t left      = p->text.size - p->pos;
    /* min()/max()/clamp(): comma-separated sums of one shape -- all px,
     * all percentages or all numbers -- since a px/percentage mix can only
     * be compared once the percentage base is known. */
    static const char *const comparisons[3] = { "min(", "max(", "clamp(" };
    for (int f = 0; f < 3; f++) {
        size_t length = strlen(comparisons[f]);
        if (left < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, length), tbox_string_view_make(comparisons[f], length)))
            continue;
        p->pos += length;
        p->depth++;
        tbox_style_calc_value args[8];
        size_t count = 0;
        for (;;) {
            if (count == 8 || !tbox_style_calc_sum(p, &args[count]))
                return false;
            count++;
            tbox_style_calc_skip_space(p);
            if (p->pos < p->text.size && p->text.data[p->pos] == ',') {
                p->pos++;
                continue;
            }
            if (p->pos < p->text.size && p->text.data[p->pos] == ')') {
                p->pos++;
                break;
            }
            return false;
        }
        p->depth--;
        if ((f == 2 && count != 3) || count == 0)
            return false;
        /* Pure px (or numbers) compare right away; with a percentage among
         * px values, the percentage stays and the px values become its
         * bounds (one percentage argument at most). */
        size_t percent_index = count;
        for (size_t i = 0; i < count; i++) {
            if (args[i].bounds != 0 || args[i].number != args[0].number)
                return false;
            if (args[i].percent != 0.0) {
                if (percent_index != count)
                    return false;
                percent_index = i;
            }
        }
        if (percent_index == count) {
            double result = args[0].px;
            if (f == 2) {
                result = args[1].px > args[2].px ? args[2].px : args[1].px;
                if (result < args[0].px)
                    result = args[0].px; /* clamp(): the minimum wins */
            } else {
                for (size_t i = 1; i < count; i++)
                    if (f == 0 ? args[i].px < result : args[i].px > result)
                        result = args[i].px;
            }
            *out = (tbox_style_calc_value){ result, 0.0, args[0].number, 0, 0.0, 0.0 };
            return true;
        }
        if (args[0].number || (f == 2 && percent_index != 1))
            return false;
        tbox_style_calc_value value = args[percent_index];
        bool have                   = false;
        double bound                = 0.0;
        for (size_t i = 0; i < count; i++) {
            if (i == percent_index || f == 2)
                continue;
            if (!have || (f == 0 ? args[i].px < bound : args[i].px > bound))
                bound = args[i].px;
            have = true;
        }
        if (f == 0)
            value.bounds = 2, value.hi = bound;
        else if (f == 1)
            value.bounds = 1, value.lo = bound;
        else
            value.bounds = 3, value.lo = args[0].px, value.hi = args[2].px;
        *out = value;
        return true;
    }
    bool nested_calc = left >= 5 && tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest, 5), tbox_string_view_from_cstr("calc("));
    if (nested_calc || rest[0] == '(') {
        p->pos += nested_calc ? 5 : 1;
        p->depth++;
        bool ok = tbox_style_calc_sum(p, out);
        p->depth--;
        tbox_style_calc_skip_space(p);
        if (!ok || p->pos >= p->text.size || p->text.data[p->pos] != ')')
            return false;
        p->pos++;
        return true;
    }
    /* One token: an optional sign, digits/dot/exponent, then a unit or %. */
    size_t start = p->pos;
    if (p->pos < p->text.size && (p->text.data[p->pos] == '+' || p->text.data[p->pos] == '-'))
        p->pos++;
    while (p->pos < p->text.size) {
        char c = p->text.data[p->pos];
        bool exponent_sign = (c == '+' || c == '-') && p->pos > start && (p->text.data[p->pos - 1] == 'e' || p->text.data[p->pos - 1] == 'E') &&
                             p->pos >= start + 2 && p->text.data[p->pos - 2] >= '0' && p->text.data[p->pos - 2] <= '9';
        if (!((c >= '0' && c <= '9') || c == '.' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '%' || exponent_sign))
            break;
        p->pos++;
    }
    tbox_string_view token = tbox_string_view_make(p->text.data + start, p->pos - start);
    if (token.size == 0)
        return false;
    *out = (tbox_style_calc_value){ 0.0, 0.0, false, 0, 0.0, 0.0 };
    if (token.data[token.size - 1] == '%')
        return tbox_style_parse_number(tbox_string_view_make(token.data, token.size - 1), &out->percent);
    if (tbox_style_parse_number(token, &out->px)) {
        out->number = true;
        return true;
    }
    return tbox_style_parse_dimension(token, p->units, p->em_base, &out->px);
}

static bool tbox_style_calc_product(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    if (!tbox_style_calc_operand(p, out))
        return false;
    for (;;) {
        size_t before = p->pos;
        tbox_style_calc_skip_space(p);
        if (p->pos >= p->text.size || (p->text.data[p->pos] != '*' && p->text.data[p->pos] != '/')) {
            p->pos = before; /* the sum checks the space before + and - */
            return true;
        }
        char op = p->text.data[p->pos++];
        tbox_style_calc_value rhs;
        if (!tbox_style_calc_operand(p, &rhs) || rhs.bounds != 0 || out->bounds != 0)
            return false;
        if (op == '*') {
            if (!out->number && !rhs.number)
                return false;
            double factor = out->number ? out->px : rhs.px;
            tbox_style_calc_value scaled = out->number ? rhs : *out;
            *out = (tbox_style_calc_value){ scaled.px * factor, scaled.percent * factor, out->number && rhs.number, 0, 0.0, 0.0 };
        } else {
            if (!rhs.number || rhs.px == 0.0)
                return false;
            out->px /= rhs.px;
            out->percent /= rhs.px;
        }
    }
}

/* `+` and `-` need whitespace on both sides, as in CSS. */
static bool tbox_style_calc_sum(tbox_style_calc_parser *p, tbox_style_calc_value *out) {
    if (!tbox_style_calc_product(p, out))
        return false;
    for (;;) {
        size_t before = p->pos;
        tbox_style_calc_skip_space(p);
        if (p->pos >= p->text.size || (p->text.data[p->pos] != '+' && p->text.data[p->pos] != '-'))
            return true;
        if (p->pos == before || p->pos + 1 >= p->text.size || !tbox_style_is_space(p->text.data[p->pos + 1]))
            return false;
        char op = p->text.data[p->pos++];
        tbox_style_calc_value rhs;
        if (!tbox_style_calc_product(p, &rhs) || out->number != rhs.number || rhs.bounds != 0 || out->bounds != 0)
            return false;
        double sign = op == '+' ? 1.0 : -1.0;
        out->px += sign * rhs.px;
        out->percent += sign * rhs.percent;
    }
}

/* `calc(...)` -- or min()/max()/clamp() -- with + - * / and nested
 * parentheses/calc()/min()/max()/clamp(). Lengths of any
 * supported unit fold into px right here; percentages stay separate, for
 * the Layout Tree to resolve against its containing block, unless
 * `percent_base` is nonnegative (font-size: percentages of the parent's
 * size). A unitless result is invalid. */
static bool tbox_style_parse_calc_bounded(tbox_string_view text, const tbox_style_units *units, double em_base, double percent_base, tbox_style_calc_value *out);

static bool tbox_style_parse_calc(tbox_string_view text, const tbox_style_units *units, double em_base, double percent_base, double *out_px, double *out_percent) {
    tbox_style_calc_value value;
    if (!tbox_style_parse_calc_bounded(text, units, em_base, percent_base, &value) || value.bounds != 0)
        return false;
    *out_px      = value.px;
    *out_percent = value.percent;
    return true;
}

static bool tbox_style_parse_calc_bounded(tbox_string_view text, const tbox_style_units *units, double em_base, double percent_base, tbox_style_calc_value *out) {
    bool function = false;
    static const char *const names[4] = { "calc(", "min(", "max(", "clamp(" };
    for (int i = 0; i < 4; i++) {
        size_t length = strlen(names[i]);
        if (text.size > length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data, length), tbox_string_view_make(names[i], length)))
            function = true;
    }
    if (!function || text.data[text.size - 1] != ')')
        return false;
    tbox_style_calc_parser parser = { text, 0, units, em_base, 0 };
    tbox_style_calc_value value;
    if (!tbox_style_calc_operand(&parser, &value) || value.number)
        return false;
    tbox_style_calc_skip_space(&parser);
    if (parser.pos != text.size)
        return false;
    if (percent_base >= 0.0) {
        value.px += percent_base * value.percent / 100.0;
        value.percent = 0.0;
        if ((value.bounds & 2u) && value.px > value.hi)
            value.px = value.hi;
        if ((value.bounds & 1u) && value.px < value.lo)
            value.px = value.lo;
        value.bounds = 0;
    }
    *out = value;
    return true;
}

/* Parses a <length-percentage>: the keyword "auto", a percentage (resolved
 * later by the Layout Tree, against its containing block), a number with
 * any unit tbox_style_unit_scale knows -- folded into px right here, em
 * against the node's own font size (see ARCHITECTURE.md's v8 Style section
 * for why this is the own node's font-size, not the parent's, unlike
 * `font-size: em` itself) -- or a calc() expression, whose percentage part
 * becomes a PERCENT length carrying the px part in `px_offset`. Anything
 * else, including a bare unitless number, fails so the caller can fall
 * back to the initial value, same as an undeclared property. */
static bool tbox_style_parse_length(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    tbox_string_view text = tbox_style_trim(raw);
    if (text.size == 0) {
        return false;
    }

    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("auto"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }

    if (text.data[text.size - 1] == '%') {
        double value;
        if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 1), &value)) {
            return false;
        }
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, value, 0.0, 0, 0.0, 0.0 };
        return true;
    }

    double px;
    tbox_style_calc_value calc;
    if (tbox_style_parse_calc_bounded(text, units, units->font_size, -1.0, &calc)) {
        if (calc.percent != 0.0)
            *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, calc.percent, calc.px, calc.bounds, calc.lo, calc.hi };
        else
            *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, calc.px, 0.0, 0, 0.0, 0.0 };
        return true;
    }

    if (tbox_style_parse_dimension(text, units, units->font_size, &px)) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, px, 0.0, 0, 0.0, 0.0 };
        return true;
    }

    return false;
}

static bool tbox_style_parse_spacing_length(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    if (raw.size == 1 && raw.data[0] == '0') {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_length(raw, units, out);
}

static bool tbox_style_parse_border_width(tbox_string_view raw, const tbox_style_units *units, double *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("thin"))) {
        *out = 1.0;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("medium"))) {
        *out = 3.0;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("thick"))) {
        *out = 5.0;
        return true;
    }
    tbox_style_length length;
    if (tbox_style_parse_spacing_length(value, units, &length) && length.kind == TBOX_STYLE_LENGTH_PX && length.value >= 0.0) {
        *out = length.value;
        return true;
    }
    return false;
}

static bool tbox_style_parse_edge_color(tbox_string_view raw, tbox_css_rgba current_color, tbox_css_rgba *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("currentcolor"))) {
        *out = current_color;
        return true;
    }
    return tbox_css_color_parse(value, out);
}

/* Resolves lengths against the parent's font size and named absolute sizes
 * against the fixed 16px medium scale. Relative keywords use a 1.2 ratio. */
static bool tbox_style_parse_font_size(tbox_string_view raw, double parent_font_size, const tbox_style_units *units, double *out);

/* The `font` shorthand, split into the longhand values it sets: `[style]
 * [variant] [weight] [stretch] size[/line-height] family`. Omitted parts
 * reset to `normal`, as for any shorthand; of the variants only
 * `small-caps` is recognized. System font keywords (`caption`,
 * `menu`, ...) are not supported and make the whole value invalid. */
typedef struct tbox_style_font_shorthand {
    const tbox_css_resolved_declaration *decl; /* NULL when absent or invalid */
    tbox_string_view style, variant, weight, stretch, size, line_height, family;
} tbox_style_font_shorthand;

static tbox_style_font_shorthand tbox_style_parse_font_shorthand(const tbox_css_computed_style *computed) {
    static const char *const stretches[] = {
        "ultra-condensed",
        "extra-condensed",
        "condensed",
        "semi-condensed",
        "semi-expanded",
        "expanded",
        "extra-expanded",
        "ultra-expanded",
    };
    tbox_style_font_shorthand font            = { 0 };
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font"));
    if (decl == NULL)
        return font;
    font.style = font.variant = font.weight = font.stretch = font.line_height = tbox_string_view_from_cstr("normal");

    tbox_string_view text = tbox_style_trim(decl->value);
    size_t i              = 0;
    while (i < text.size) {
        size_t start = i;
        while (i < text.size && !tbox_style_is_space(text.data[i]) && text.data[i] != '/')
            i++;
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);
        if (token.size == 0)
            return font; /* a stray '/' before the size */

        double size;
        if (tbox_style_parse_font_size(token, 16.0, &tbox_style_default_units, &size)) {
            font.size = token;
            while (i < text.size && tbox_style_is_space(text.data[i]))
                i++;
            if (i < text.size && text.data[i] == '/') {
                i++;
                while (i < text.size && tbox_style_is_space(text.data[i]))
                    i++;
                size_t lh_start = i;
                while (i < text.size && !tbox_style_is_space(text.data[i]))
                    i++;
                font.line_height = tbox_string_view_make(text.data + lh_start, i - lh_start);
                if (font.line_height.size == 0)
                    return font;
            }
            font.family = tbox_style_trim(tbox_string_view_make(text.data + i, text.size - i));
            if (font.family.size > 0)
                font.decl = decl;
            return font;
        }

        bool numeric_weight = token.size == 3 && token.data[1] == '0' && token.data[2] == '0' && token.data[0] >= '1' && token.data[0] <= '9';
        if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("italic")) || tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("oblique"))) {
            font.style = token;
        } else if (numeric_weight || tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("bold")) || tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("bolder")) || tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("lighter"))) {
            font.weight = token;
        } else if (!tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("normal"))) {
            bool known = false;
            for (size_t k = 0; k < sizeof(stretches) / sizeof(stretches[0]); k++)
                if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr(stretches[k])))
                    known = true;
            if (known)
                font.stretch = token;
            else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("small-caps")))
                font.variant = token;
            else
                return font;
        }
        while (i < text.size && tbox_style_is_space(text.data[i]))
            i++;
    }
    return font; /* no size: invalid */
}

/* The value that decides font longhand `property`: the `font` shorthand's
 * part when the shorthand wins the cascade over the longhand (or the
 * longhand is absent), otherwise the longhand's own value. NULL when
 * neither is declared. */
static const tbox_string_view *tbox_style_font_value(const tbox_css_computed_style *computed, const tbox_style_font_shorthand *font, const char *property, tbox_string_view *storage) {
    const tbox_css_resolved_declaration *longhand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (font->decl != NULL && (longhand == NULL || tbox_css_cascade_priority_compare(font->decl, longhand) > 0)) {
        *storage = strcmp(property, "font-style") == 0 ? font->style : strcmp(property, "font-weight") == 0 ? font->weight : strcmp(property, "font-size") == 0 ? font->size : strcmp(property, "line-height") == 0 ? font->line_height : strcmp(property, "font-stretch") == 0 ? font->stretch : strcmp(property, "font-variant") == 0 ? font->variant : font->family;
        return storage;
    }
    if (longhand == NULL)
        return NULL;
    *storage = longhand->value;
    return storage;
}

static double tbox_style_resolve_font_size(const tbox_string_view *value, double parent_font_size, const tbox_style_units *units) {
    double size;
    return value != NULL && tbox_style_parse_font_size(*value, parent_font_size, units, &size) ? size : parent_font_size;
}

static bool tbox_style_parse_font_size(tbox_string_view raw, double parent_font_size, const tbox_style_units *units, double *out) {
    tbox_string_view text = tbox_style_trim(raw);
    if (text.size == 0) {
        return false;
    }

    static const struct {
        const char *name;
        double pixels;
    } absolute_sizes[] = {
        { "xx-small", 9.0  },
        { "x-small",  10.0 },
        { "small",    13.0 },
        { "medium",   16.0 },
        { "large",    18.0 },
        { "x-large",  24.0 },
        { "xx-large", 32.0 },
    };
    for (size_t i = 0; i < sizeof(absolute_sizes) / sizeof(absolute_sizes[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr(absolute_sizes[i].name))) {
            *out = absolute_sizes[i].pixels;
            return true;
        }
    }
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("smaller"))) {
        *out = parent_font_size / 1.2;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("larger"))) {
        *out = parent_font_size * 1.2;
        return true;
    }

    if (text.data[text.size - 1] == '%') {
        double value;
        if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 1), &value)) {
            return false;
        }
        *out = parent_font_size * value / 100.0;
        return true;
    }

    /* em and percentages (also inside calc()) are relative to the parent's
     * size; rem to the root's; other units are absolute or viewport-based. */
    double percent;
    if (tbox_style_parse_calc(text, units, parent_font_size, parent_font_size, out, &percent))
        return *out >= 0.0;
    return tbox_style_parse_dimension(text, units, parent_font_size, out) && *out >= 0.0;
}

static bool tbox_style_parse_display(tbox_string_view raw, tbox_style_display *out) {
    /* `-webkit-box` only appears with -webkit-line-clamp in practice; it
     * lays out as a block here. */
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("block")) || tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("-webkit-box")) ||
        tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("flow-root")) || tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("list-item")) ||
        tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("block flow-root"))) {
        *out = TBOX_STYLE_DISPLAY_BLOCK;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("inline"))) {
        *out = TBOX_STYLE_DISPLAY_INLINE;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("none"))) {
        *out = TBOX_STYLE_DISPLAY_NONE;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("inline-block"))) {
        *out = TBOX_STYLE_DISPLAY_INLINE_BLOCK;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("flex"))) {
        *out = TBOX_STYLE_DISPLAY_FLEX;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("inline-flex"))) {
        *out = TBOX_STYLE_DISPLAY_INLINE_FLEX;
        return true;
    }
    return false;
}

/* parses `text-align` keywords (`left`, `center`, `right`,
 * `justify`, `start`, `end`), case-insensitive -- same pattern as
 * tbox_style_parse_display above. Returns false for any other value, so the
 * caller falls back to inheritance/the initial value the same way an
 * unrecognized `font-weight` already does. */
static bool tbox_style_parse_text_align(tbox_string_view raw, tbox_style_text_align *out) {
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("left"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_LEFT;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("center"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_CENTER;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("right")) || tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("end"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_RIGHT;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("start"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_LEFT;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("justify"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_JUSTIFY;
        return true;
    }
    return false;
}

/* Text decoration is not inherited. */
static bool tbox_style_parse_decoration_line(tbox_string_view raw, tbox_style_text_decoration *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none"))) {
        *out = TBOX_STYLE_TEXT_DECORATION_NONE;
    } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("underline"))) {
        *out = TBOX_STYLE_TEXT_DECORATION_UNDERLINE;
    } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("line-through"))) {
        *out = TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH;
    } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("overline"))) {
        *out = TBOX_STYLE_TEXT_DECORATION_OVERLINE;
    } else {
        return false;
    }
    return true;
}

static unsigned int tbox_style_decoration_bit(tbox_style_text_decoration line) {
    return line == TBOX_STYLE_TEXT_DECORATION_UNDERLINE ? 1u :
           line == TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH ? 2u :
           line == TBOX_STYLE_TEXT_DECORATION_OVERLINE ? 4u : 0u;
}

static bool tbox_style_parse_decoration_lines(tbox_string_view text, unsigned int *out) {
    unsigned int lines = 0;
    bool found = false;
    size_t i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) i++;
        if (i == text.size) break;
        size_t start = i;
        while (i < text.size && !tbox_style_is_space(text.data[i])) i++;
        tbox_style_text_decoration line;
        if (!tbox_style_parse_decoration_line(tbox_string_view_make(text.data + start, i - start), &line)) return false;
        if ((line == TBOX_STYLE_TEXT_DECORATION_NONE && found) || (found && lines == 0u)) return false;
        lines |= tbox_style_decoration_bit(line);
        found = true;
    }
    if (!found) return false;
    *out = lines;
    return true;
}

static bool tbox_style_parse_decoration_style(tbox_string_view value, tbox_style_border_style *out) {
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("solid"))) *out = TBOX_STYLE_BORDER_STYLE_SOLID;
    else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("dashed"))) *out = TBOX_STYLE_BORDER_STYLE_DASHED;
    else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("dotted"))) *out = TBOX_STYLE_BORDER_STYLE_DOTTED;
    else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("double"))) *out = TBOX_STYLE_BORDER_STYLE_DOUBLE;
    else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("wavy"))) *out = TBOX_STYLE_BORDER_STYLE_WAVY;
    else return false;
    return true;
}

/* `auto` and `from-font` both use the 1px default thickness. */
static bool tbox_style_parse_decoration_thickness(tbox_string_view raw, const tbox_style_units *units, double *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto")) || tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("from-font"))) {
        *out = 1.0;
        return true;
    }
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(value, units, &length) || length.kind != TBOX_STYLE_LENGTH_PX || length.value < 0.0)
        return false;
    *out = length.value;
    return true;
}

/* The `text-decoration` shorthand: line keywords, a thickness, a color and
 * a stroke style, in any order, each optional. Parts left out reset to
 * their initial values, as with any shorthand. Unsupported styles such as
 * `wavy` paint solid, and unrecognized tokens are ignored -- same posture as
 * tbox_style_resolve_border. */
static void tbox_style_resolve_text_decoration(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_text_decoration *out_line, unsigned int *out_lines, tbox_style_border_style *out_style, tbox_css_rgba *out_color, double *out_thickness) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration"));
    if (decl == NULL)
        return;

    tbox_string_view text = tbox_style_trim(decl->value);
    bool have_line        = false;
    size_t i              = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i]))
            i++;
        if (i >= text.size)
            break;
        size_t start = i;
        int depth    = 0;
        while (i < text.size && (depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(')
                depth++;
            else if (text.data[i] == ')' && depth > 0)
                depth--;
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        tbox_style_text_decoration line;
        double thickness;
        tbox_css_rgba color;
        if (tbox_style_parse_decoration_line(token, &line)) {
            if (!have_line)
                *out_line = line;
            *out_lines |= tbox_style_decoration_bit(line);
            have_line = true;
        } else if (tbox_style_parse_decoration_style(token, out_style)) {
            /* style kept */
        } else if (tbox_style_parse_decoration_thickness(token, units, &thickness)) {
            *out_thickness = thickness;
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            *out_color = color;
        }
    }
}

/* Resolves supported inline and table-cell vertical alignment keywords, plus
 * a px/em/% length (percentages stay PERCENT for the Layout Tree, which
 * knows the used line-height). */
static tbox_style_vertical_align tbox_style_resolve_vertical_align(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style_length *out_length) {
    *out_length                               = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("vertical-align"));
    if (decl != NULL) {
        tbox_style_length length;
        tbox_string_view value = tbox_style_trim(decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("sub"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_SUB;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("super"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_SUPER;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("top"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_TOP;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("middle"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_MIDDLE;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("bottom"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_BOTTOM;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("text-top"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_TEXT_TOP;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("text-bottom"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM;
        } else if (tbox_style_parse_spacing_length(value, units, &length) && length.kind != TBOX_STYLE_LENGTH_AUTO) {
            *out_length = length;
            return TBOX_STYLE_VERTICAL_ALIGN_LENGTH;
        }
    }
    return TBOX_STYLE_VERTICAL_ALIGN_BASELINE;
}

/* parses `font-family` per ARCHITECTURE.md's v12 Style section --
 * only the FIRST name of a comma-separated list is ever used (the rest,
 * meant for fallback, is discarded -- decision confirmed with the
 * maintainer, see ARCHITECTURE.md's v12 "Escopo"). If the (trimmed) value
 * starts with `"` or `'`, the first name is the text between that quote and
 * its matching close (an unterminated quote fails the whole parse, since a
 * comma inside a quoted name must never be treated as the list separator);
 * otherwise, the first name is the text before the first top-level `,` (or
 * the whole value, if there is none). The extracted name is trimmed again
 * (its edges can carry stray whitespace, e.g. the leading space in the
 * second name of "Verdana, Arial") and, if still non-empty, copied into
 * `out`, truncated to `out_capacity - 1` bytes and always NUL-terminated --
 * same truncate-don't-reject posture as
 * tbox_font_source_fontconfig_family_cstr (src/font/tbox_font_source_fontconfig.c).
 * Returns false (leaving `out` untouched) when `raw` is empty/all
 * whitespace, an opening quote has no matching close, or the extracted name
 * is empty after trimming. */
static bool tbox_style_parse_font_family(tbox_string_view raw, char *out, size_t out_capacity) {
    tbox_string_view text = tbox_style_trim(raw);
    if (text.size == 0) {
        return false;
    }

    tbox_string_view first_name;

    char quote = text.data[0];
    if (quote == '"' || quote == '\'') {
        size_t close = 0;
        bool found   = false;
        for (size_t i = 1; i < text.size; i++) {
            if (text.data[i] == quote) {
                close = i;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
        first_name = tbox_string_view_make(text.data + 1, close - 1);
    } else {
        size_t comma = text.size;
        for (size_t i = 0; i < text.size; i++) {
            if (text.data[i] == ',') {
                comma = i;
                break;
            }
        }
        first_name = tbox_string_view_make(text.data, comma);
    }

    first_name = tbox_style_trim(first_name);
    if (first_name.size == 0) {
        return false;
    }

    size_t n = first_name.size < out_capacity - 1 ? first_name.size : out_capacity - 1;
    memcpy(out, first_name.data, n);
    out[n] = '\0';
    return true;
}

/* Splits `text` on runs of ASCII whitespace into at most 4 tokens (the max
 * a margin/padding shorthand ever takes). Returns false -- meaning the
 * whole shorthand is invalid -- if there are no tokens at all, or more than
 * 4 (an out-of-grammar 5th value), so the caller can fall back to the
 * initial value for every side rather than guess. */
static bool tbox_style_split_box_shorthand(tbox_string_view text, tbox_string_view tokens[4], size_t *out_count) {
    size_t count = 0;
    size_t i     = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) {
            i++;
        }
        if (i >= text.size) {
            break;
        }
        size_t start    = i;
        int paren_depth = 0; /* rgba(1, 2, 3) stays one token */
        while (i < text.size && (paren_depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(')
                paren_depth++;
            else if (text.data[i] == ')' && paren_depth > 0)
                paren_depth--;
            i++;
        }
        if (count >= 4) {
            return false;
        }
        tokens[count++] = tbox_string_view_make(text.data + start, i - start);
    }
    if (count == 0) {
        return false;
    }
    *out_count = count;
    return true;
}

/* The one/two-value subset of object-position. A percentage aligns the same
 * point of the image and its box; a length offsets from the left/top edge.
 * Keywords may appear in horizontal/vertical or vertical/horizontal order. */
static bool tbox_style_parse_object_position_component(tbox_string_view token, const tbox_style_units *units, bool horizontal, tbox_style_length *out) {
    if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("center"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (horizontal && tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("left"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (horizontal && tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("right"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (!horizontal && tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("top"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (!horizontal && tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("bottom"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_spacing_length(token, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO;
}

static bool tbox_style_parse_object_position(tbox_string_view raw, const tbox_style_units *units, tbox_style_length out[2]) {
    tbox_string_view tokens[4];
    size_t count;
    if (!tbox_style_split_box_shorthand(raw, tokens, &count) || count > 2) return false;
    tbox_style_length x = { TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    tbox_style_length y = x;
    if (count == 1) {
        if (!tbox_style_parse_object_position_component(tokens[0], units, true, &x) &&
            !tbox_style_parse_object_position_component(tokens[0], units, false, &y)) return false;
    } else {
        if (!(tbox_style_parse_object_position_component(tokens[0], units, true, &x) &&
              tbox_style_parse_object_position_component(tokens[1], units, false, &y)) &&
            !(tbox_style_parse_object_position_component(tokens[0], units, false, &y) &&
              tbox_style_parse_object_position_component(tokens[1], units, true, &x))) return false;
    }
    out[0] = x;
    out[1] = y;
    return true;
}

/* Expands the standard CSS2.1 margin/padding shorthand (1/2/3/4-value
 * syntax) into `out[4]` (top, right, bottom, left). On any parse failure --
 * no declaration for `property`, an out-of-grammar token count, or any
 * single token failing to parse as a <length> -- leaves `out` untouched, so
 * the caller can pre-fill it with the initial value (0px on every side)
 * before calling this. */
static bool tbox_style_resolve_box_shorthand(const tbox_css_computed_style *computed, const char *property, const tbox_style_units *units, bool allow_auto, tbox_style_length out[4]) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl == NULL) {
        return false;
    }

    tbox_string_view tokens[4];
    size_t count;
    if (!tbox_style_split_box_shorthand(decl->value, tokens, &count)) {
        return false;
    }

    tbox_style_length parsed[4];
    for (size_t i = 0; i < count; i++) {
        if (!tbox_style_parse_spacing_length(tokens[i], units, &parsed[i]) || (!allow_auto && (parsed[i].kind == TBOX_STYLE_LENGTH_AUTO || parsed[i].value < 0.0))) {
            return false;
        }
    }

    switch (count) {
    case 1:
        out[0] = out[1] = out[2] = out[3] = parsed[0];
        break;
    case 2:
        out[0] = out[2] = parsed[0]; /* top, bottom */
        out[1] = out[3] = parsed[1]; /* left, right */
        break;
    case 3:
        out[0] = parsed[0];          /* top */
        out[1] = out[3] = parsed[1]; /* left, right */
        out[2]          = parsed[2]; /* bottom */
        break;
    default:                /* 4 */
        out[0] = parsed[0]; /* top */
        out[1] = parsed[1]; /* right */
        out[2] = parsed[2]; /* bottom */
        out[3] = parsed[3]; /* left */
        break;
    }
    return true;
}

static bool tbox_style_parse_edge_length(tbox_string_view raw, const tbox_style_units *units, bool allow_auto, tbox_style_length *out) {
    return tbox_style_parse_spacing_length(tbox_style_trim(raw), units, out) && (allow_auto || (out->kind != TBOX_STYLE_LENGTH_AUTO && out->value >= 0.0));
}

/* A side takes `parsed` only when `decl` beats the declaration that set it
 * so far, so physical, shorthand and logical properties follow ordinary
 * cascade precedence among themselves. */
static void tbox_style_offer_edge(const tbox_css_resolved_declaration *decl, tbox_style_length parsed, int side, const tbox_css_resolved_declaration *winners[4], tbox_style_length out[4]) {
    if (winners[side] != NULL && tbox_css_cascade_priority_compare(decl, winners[side]) <= 0)
        return;
    winners[side] = decl;
    out[side]     = parsed;
}

/* Resolves `property` (margin, padding or inset), its physical longhands and
 * the logical `-block`/`-inline` forms. Writing is always horizontal
 * left-to-right, so block-start/end map to top/bottom and inline-start/end
 * map to left/right. */
static void tbox_style_resolve_box_edges(const tbox_css_computed_style *computed, const char *property, const char *const longhands[4], const tbox_style_units *units, bool allow_auto, tbox_style_length out[4]) {
    const tbox_css_resolved_declaration *winners[4] = { NULL, NULL, NULL, NULL };
    if (tbox_style_resolve_box_shorthand(computed, property, units, allow_auto, out)) {
        const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
        for (int i = 0; i < 4; i++)
            winners[i] = shorthand;
    }
    for (int i = 0; i < 4; i++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(longhands[i]));
        tbox_style_length parsed;
        if (decl != NULL && tbox_style_parse_edge_length(decl->value, units, allow_auto, &parsed))
            tbox_style_offer_edge(decl, parsed, i, winners, out);
    }

    /* Axis shorthands take one or two values: start, then end. */
    static const struct {
        const char *suffix;
        int start, end;
    } axes[] = {
        { "-block",  0, 2 },
        { "-inline", 3, 1 },
    };
    for (size_t a = 0; a < sizeof(axes) / sizeof(axes[0]); a++) {
        char name[32];
        snprintf(name, sizeof(name), "%s%s", property, axes[a].suffix);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
        tbox_string_view tokens[4];
        size_t count;
        if (decl == NULL || !tbox_style_split_box_shorthand(decl->value, tokens, &count) || count > 2)
            continue;
        tbox_style_length start, end;
        if (!tbox_style_parse_edge_length(tokens[0], units, allow_auto, &start))
            continue;
        if (count == 1)
            end = start;
        else if (!tbox_style_parse_edge_length(tokens[1], units, allow_auto, &end))
            continue;
        tbox_style_offer_edge(decl, start, axes[a].start, winners, out);
        tbox_style_offer_edge(decl, end, axes[a].end, winners, out);
    }

    static const struct {
        const char *suffix;
        int side;
    } logical[] = {
        { "-block-start",  0 },
        { "-block-end",    2 },
        { "-inline-start", 3 },
        { "-inline-end",   1 },
    };
    for (size_t l = 0; l < sizeof(logical) / sizeof(logical[0]); l++) {
        char name[32];
        snprintf(name, sizeof(name), "%s%s", property, logical[l].suffix);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
        tbox_style_length parsed;
        if (decl != NULL && tbox_style_parse_edge_length(decl->value, units, allow_auto, &parsed))
            tbox_style_offer_edge(decl, parsed, logical[l].side, winners, out);
    }
}

/* A border/outline style keyword. `hidden` is only valid for borders
 * (`allow_hidden`). */
static bool tbox_style_parse_border_style(tbox_string_view raw, bool allow_hidden, tbox_style_border_style *out) {
    static const struct {
        const char *name;
        tbox_style_border_style value;
    } styles[] = {
        { "none",   TBOX_STYLE_BORDER_STYLE_NONE   },
        { "solid",  TBOX_STYLE_BORDER_STYLE_SOLID  },
        { "dashed", TBOX_STYLE_BORDER_STYLE_DASHED },
        { "dotted", TBOX_STYLE_BORDER_STYLE_DOTTED },
        { "double", TBOX_STYLE_BORDER_STYLE_DOUBLE },
        { "groove", TBOX_STYLE_BORDER_STYLE_GROOVE },
        { "ridge",  TBOX_STYLE_BORDER_STYLE_RIDGE  },
        { "inset",  TBOX_STYLE_BORDER_STYLE_INSET  },
        { "outset", TBOX_STYLE_BORDER_STYLE_OUTSET },
    };
    tbox_string_view value = tbox_style_trim(raw);
    if (allow_hidden && tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("hidden"))) {
        *out = TBOX_STYLE_BORDER_STYLE_NONE;
        return true;
    }
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(styles[i].name))) {
            *out = styles[i].value;
            return true;
        }
    }
    return false;
}

/* Parses uniform `border` and `outline` shorthands. Tokens are classified as
 * a nonnegative px/em/keyword width, a style keyword (see
 * tbox_style_parse_border_style; `hidden` only when `allow_hidden`, i.e.
 * for `border` but not `outline`), or color (including
 * currentColor). A token matching none of the three
 * is silently ignored -- it never invalidates the other tokens, nor the
 * declaration as a whole (same robustness posture as the rest of Style/CSS
 * Parser). `out_width`/`out_style`/`out_color` are only written when their
 * respective token classifies successfully; on entry they already hold the
 * caller's initial values. Returns whether a declaration was found
 * at all (false when absent, callers just keep the pre-filled initial
 * values). */
static bool tbox_style_resolve_border(const tbox_css_computed_style *computed, const char *property, bool allow_hidden, const tbox_style_units *units, tbox_css_rgba current_color, double *out_width, tbox_style_border_style *out_style, tbox_css_rgba *out_color) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl == NULL) {
        return false;
    }

    tbox_string_view text = tbox_style_trim(decl->value);
    size_t i              = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) {
            i++;
        }
        if (i >= text.size) {
            break;
        }
        size_t start = i;
        int depth    = 0; /* rgb(0 0 0) stays one token */
        while (i < text.size && (depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(')
                depth++;
            else if (text.data[i] == ')' && depth > 0)
                depth--;
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        double width;
        tbox_css_rgba color;
        if (tbox_style_parse_border_width(token, units, &width)) {
            *out_width = width;
        } else if (tbox_style_parse_border_style(token, allow_hidden, out_style)) {
            /* style kept */
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            *out_color = color;
        }
        /* else: unrecognized token, ignored -- keep scanning. */
    }

    return true;
}

static bool tbox_style_border_longhand_wins(const tbox_css_resolved_declaration *longhand, const tbox_css_resolved_declaration *shorthand) {
    return longhand != NULL && (shorthand == NULL || tbox_css_cascade_priority_compare(longhand, shorthand) > 0);
}

static bool tbox_style_rgba_equal(tbox_css_rgba a, tbox_css_rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* True, recording `candidate` as the new winner, when it beats `*winner`
 * (or nothing has won yet) -- ordinary cascade precedence between a
 * shorthand and its longhands. */
static bool tbox_style_claim(const tbox_css_resolved_declaration *candidate, const tbox_css_resolved_declaration **winner) {
    if (*winner != NULL && tbox_css_cascade_priority_compare(candidate, *winner) <= 0)
        return false;
    *winner = candidate;
    return true;
}

/* Which of 1-4 tokens applies to `side` (top, right, bottom, left), same
 * pattern as margin/padding. */
static size_t tbox_style_four_value_index(size_t count, size_t side) {
    static const size_t map[4][4] = {
        { 0, 0, 0, 0 },
        { 0, 1, 0, 1 },
        { 0, 1, 2, 1 },
        { 0, 1, 2, 3 }
    };
    return map[count - 1][side];
}

static void tbox_style_resolve_border_sides(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_css_rgba current_color, double widths[4], tbox_style_border_style styles[4], tbox_css_rgba colors[4]) {
    static const char *const sides[4]                    = { "top", "right", "bottom", "left" };
    const tbox_css_resolved_declaration *width_winner[4] = { NULL, NULL, NULL, NULL };
    const tbox_css_resolved_declaration *style_winner[4] = { NULL, NULL, NULL, NULL };
    const tbox_css_resolved_declaration *color_winner[4] = { NULL, NULL, NULL, NULL };
    for (size_t i = 0; i < 4; i++) {
        widths[i] = 0.0;
        styles[i] = TBOX_STYLE_BORDER_STYLE_NONE;
        colors[i] = current_color;
    }

    /* Shorthands: `border` for every side, `border-<side>` for one. Parts
     * a shorthand leaves out take their initial values. */
    for (size_t s = 0; s < 5; s++) {
        char name[32];
        snprintf(name, sizeof(name), s == 0 ? "border" : "border-%s", s == 0 ? "" : sides[s - 1]);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
        if (decl == NULL)
            continue;
        double width                         = 0.0;
        tbox_style_border_style border_style = TBOX_STYLE_BORDER_STYLE_NONE;
        tbox_css_rgba color                  = current_color;
        tbox_style_resolve_border(computed, name, true, units, current_color, &width, &border_style, &color);
        for (size_t i = (s == 0 ? 0 : s - 1); i < (s == 0 ? 4 : s); i++) {
            if (tbox_style_claim(decl, &width_winner[i]))
                widths[i] = width;
            if (tbox_style_claim(decl, &style_winner[i]))
                styles[i] = border_style;
            if (tbox_style_claim(decl, &color_winner[i]))
                colors[i] = color;
        }
    }

    /* `border-width/-style/-color`: 1-4 values, all valid or ignored. */
    static const char *const parts[3] = { "width", "style", "color" };
    for (size_t part = 0; part < 3; part++) {
        char name[32];
        snprintf(name, sizeof(name), "border-%s", parts[part]);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
        tbox_string_view tokens[4];
        size_t count;
        if (decl == NULL || !tbox_style_split_box_shorthand(decl->value, tokens, &count))
            continue;
        double parsed_widths[4];
        tbox_style_border_style parsed_styles[4];
        tbox_css_rgba parsed_colors[4];
        bool valid = true;
        for (size_t t = 0; t < count && valid; t++) {
            valid = part == 0 ? tbox_style_parse_border_width(tokens[t], units, &parsed_widths[t]) : part == 1 ? tbox_style_parse_border_style(tokens[t], true, &parsed_styles[t]) : tbox_style_parse_edge_color(tokens[t], current_color, &parsed_colors[t]);
        }
        if (!valid)
            continue;
        for (size_t i = 0; i < 4; i++) {
            size_t t = tbox_style_four_value_index(count, i);
            if (part == 0 && tbox_style_claim(decl, &width_winner[i]))
                widths[i] = parsed_widths[t];
            if (part == 1 && tbox_style_claim(decl, &style_winner[i]))
                styles[i] = parsed_styles[t];
            if (part == 2 && tbox_style_claim(decl, &color_winner[i]))
                colors[i] = parsed_colors[t];
        }
    }

    /* `border-<side>-width/-style/-color`. */
    for (size_t i = 0; i < 4; i++) {
        for (size_t part = 0; part < 3; part++) {
            char name[40];
            snprintf(name, sizeof(name), "border-%s-%s", sides[i], parts[part]);
            const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
            if (decl == NULL)
                continue;
            double width;
            tbox_style_border_style border_style;
            tbox_css_rgba color;
            if (part == 0 && tbox_style_parse_border_width(decl->value, units, &width) && tbox_style_claim(decl, &width_winner[i]))
                widths[i] = width;
            if (part == 1 && tbox_style_parse_border_style(decl->value, true, &border_style) && tbox_style_claim(decl, &style_winner[i]))
                styles[i] = border_style;
            if (part == 2 && tbox_style_parse_edge_color(decl->value, current_color, &color) && tbox_style_claim(decl, &color_winner[i]))
                colors[i] = color;
        }
    }

    /* Logical axis shorthands affect both sides of their axis. */
    static const struct { const char *name; size_t first, second; } axes[] = {
        { "border-block", 0, 2 }, { "border-inline", 1, 3 },
    };
    for (size_t axis = 0; axis < 2; axis++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(axes[axis].name));
        if (decl == NULL) continue;
        double width = 0.0;
        tbox_style_border_style border_style = TBOX_STYLE_BORDER_STYLE_NONE;
        tbox_css_rgba color = current_color;
        tbox_style_resolve_border(computed, axes[axis].name, true, units, current_color, &width, &border_style, &color);
        size_t targets[] = { axes[axis].first, axes[axis].second };
        for (size_t t = 0; t < 2; t++) {
            size_t i = targets[t];
            if (tbox_style_claim(decl, &width_winner[i])) widths[i] = width;
            if (tbox_style_claim(decl, &style_winner[i])) styles[i] = border_style;
            if (tbox_style_claim(decl, &color_winner[i])) colors[i] = color;
        }
    }

    /* Logical sides map to physical sides in the engine's left-to-right
     * writing mode. Their shorthands reset omitted components, just like
     * border-top/right/bottom/left, and compete by normal cascade priority. */
    static const struct { const char *name; size_t side; } logical[] = {
        { "block-start", 0 }, { "inline-end", 1 },
        { "block-end", 2 }, { "inline-start", 3 },
    };
    for (size_t l = 0; l < 4; l++) {
        size_t i = logical[l].side;
        char name[48];
        snprintf(name, sizeof(name), "border-%s", logical[l].name);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
        if (decl != NULL) {
            double width = 0.0;
            tbox_style_border_style border_style = TBOX_STYLE_BORDER_STYLE_NONE;
            tbox_css_rgba color = current_color;
            tbox_style_resolve_border(computed, name, true, units, current_color, &width, &border_style, &color);
            if (tbox_style_claim(decl, &width_winner[i])) widths[i] = width;
            if (tbox_style_claim(decl, &style_winner[i])) styles[i] = border_style;
            if (tbox_style_claim(decl, &color_winner[i])) colors[i] = color;
        }
        for (size_t part = 0; part < 3; part++) {
            snprintf(name, sizeof(name), "border-%s-%s", logical[l].name, parts[part]);
            decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(name));
            if (decl == NULL) continue;
            double width;
            tbox_style_border_style border_style;
            tbox_css_rgba color;
            if (part == 0 && tbox_style_parse_border_width(decl->value, units, &width) && tbox_style_claim(decl, &width_winner[i])) widths[i] = width;
            if (part == 1 && tbox_style_parse_border_style(decl->value, true, &border_style) && tbox_style_claim(decl, &style_winner[i])) styles[i] = border_style;
            if (part == 2 && tbox_style_parse_edge_color(decl->value, current_color, &color) && tbox_style_claim(decl, &color_winner[i])) colors[i] = color;
        }
    }
}

/* A nonnegative px/em radius (into `out_px`) or percentage (into
 * `out_percent`); the other output is zeroed. */
static bool tbox_style_parse_radius(tbox_string_view value, const tbox_style_units *units, double *out_px, double *out_percent) {
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(tbox_style_trim(value), units, &length) || length.kind == TBOX_STYLE_LENGTH_AUTO || length.value < 0.0)
        return false;
    *out_px      = length.kind == TBOX_STYLE_LENGTH_PX ? length.value : 0.0;
    *out_percent = length.kind == TBOX_STYLE_LENGTH_PERCENT ? length.value : 0.0;
    return true;
}

/* CSS clockwise shorthand expansion, `horizontal / vertical` for
 * elliptical corners; individual corners obey the same cascade precedence
 * as the existing border longhands and take one (circular) or two
 * (horizontal vertical) radii. */
static void tbox_style_resolve_border_radius(const tbox_css_computed_style *computed, const tbox_style_units *units, double out[4], double out_percent[4], double out_vertical[4], double out_vertical_percent[4]) {
    for (size_t i = 0; i < 4; i++)
        out[i] = out_percent[i] = out_vertical[i] = out_vertical_percent[i] = 0.0;
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("border-radius"));
    bool shorthand_valid                           = false;
    if (shorthand != NULL) {
        tbox_string_view tokens[9];
        size_t count = tbox_style_tokens(shorthand->value, tokens, 9);
        size_t slash = count;
        for (size_t i = 0; i < count; i++)
            if (tokens[i].size == 1 && tokens[i].data[0] == '/')
                slash = i;
        double h[4], hp[4], v[4], vp[4];
        if (count > 0 && tbox_style_parse_radius_list(tokens, slash, units, h, hp)) {
            if (slash == count) {
                memcpy(v, h, sizeof(h));
                memcpy(vp, hp, sizeof(hp));
                shorthand_valid = true;
            } else {
                shorthand_valid = tbox_style_parse_radius_list(tokens + slash + 1, count - slash - 1, units, v, vp);
            }
        }
        if (shorthand_valid) {
            memcpy(out, h, sizeof(h));
            memcpy(out_percent, hp, sizeof(hp));
            memcpy(out_vertical, v, sizeof(v));
            memcpy(out_vertical_percent, vp, sizeof(vp));
        }
    }

    static const char *const names[4] = { "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius", "border-bottom-left-radius" };
    for (size_t i = 0; i < 4; i++) {
        const tbox_css_resolved_declaration *longhand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(names[i]));
        if (tbox_style_border_longhand_wins(longhand, shorthand_valid ? shorthand : NULL)) {
            tbox_string_view tokens[3];
            size_t count = tbox_style_tokens(longhand->value, tokens, 3);
            double radius, percent, vertical, vertical_percent;
            if ((count == 1 || count == 2) && tbox_style_parse_radius(tokens[0], units, &radius, &percent) &&
                tbox_style_parse_radius(tokens[count - 1], units, &vertical, &vertical_percent)) {
                out[i]                  = radius;
                out_percent[i]          = percent;
                out_vertical[i]         = vertical;
                out_vertical_percent[i] = vertical_percent;
            }
        }
    }
}

/* NOVO (visual fidelity): `box-shadow: <offset-x> <offset-y> [<blur-radius>]
 * <color>` -- a whitespace-tokenizing loop shaped after
 * tbox_style_resolve_border above, with ONE addition that function doesn't
 * need: a token may itself contain internal whitespace when inside
 * parentheses (e.g. the color `rgba(0, 0, 0, 0.5)`, commonly authored with
 * a space after each comma) -- the scanner tracks paren depth and only
 * treats whitespace as a token boundary at depth 0, so that whole color
 * function call is captured as ONE token rather than shredded into
 * "rgba(0,", "0,", "0,", "0.5)". The first two "px" tokens found become
 * offset-x/offset-y (in order); a third "px" token (if any) becomes
 * blur-radius; the first token that parses as a color (tbox_css_color_parse
 * -- named/hex/rgb()/rgba()/hsl()/hsla(), all already supported) becomes
 * the shadow color. A comma at paren depth 0 (separating multiple shadows,
 * out of scope -- only ONE shadow is supported; a comma INSIDE a color
 * function's own argument list doesn't count) makes the whole declaration
 * ignored, same "recognized syntax only" posture as every other property
 * here. Writes nothing and returns false unless a color AND both offsets
 * were found -- an incomplete/unrecognized declaration is not a shadow at
 * all, same as `border` needing at least a recognized token to do
 * anything. */
/* Parses one shadow: `<x> <y> [<blur> [<spread>]] [<color>]`, in any
 * order between the lengths and the color, `max_lengths` being 4 for
 * box-shadow and 3 for text-shadow. Lengths are px/em (or a bare 0); the
 * color defaults to currentColor. `none` yields a transparent color (no
 * shadow). Returns false -- keep the caller's fallback -- for more than one
 * shadow, too few/many lengths, a negative blur, or an unrecognized token
 * other than `inset` (which is ignored: the shadow still paints outside). */
static bool tbox_style_parse_shadow(tbox_string_view raw, const tbox_style_units *units, tbox_css_rgba current_color, int max_lengths, double out_lengths[4], tbox_css_rgba *out_color, bool *out_inset) {
    tbox_string_view text = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("none"))) {
        for (int i = 0; i < 4; i++)
            out_lengths[i] = 0.0;
        *out_color = (tbox_css_rgba){ 0, 0, 0, 0 };
        *out_inset = false;
        return true;
    }

    /* A comma OUTSIDE parentheses separates multiple shadows (out of
     * scope, rejected entirely) -- a comma INSIDE parentheses is just an
     * rgba()/hsla() color's own argument separator. A separate pass (not
     * folded into the tokenizer below) so a comma with no surrounding
     * whitespace, e.g. "...red,2px...", is still caught. */
    int paren_depth = 0;
    for (size_t i = 0; i < text.size; i++) {
        if (text.data[i] == '(') {
            paren_depth++;
        } else if (text.data[i] == ')') {
            if (paren_depth > 0)
                paren_depth--;
        } else if (text.data[i] == ',' && paren_depth == 0) {
            return false;
        }
    }

    double lengths[4]   = { 0.0, 0.0, 0.0, 0.0 };
    int length_count    = 0;
    bool inset          = false;
    tbox_css_rgba color = current_color;
    size_t i            = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i]))
            i++;
        if (i >= text.size)
            break;
        size_t start = i;
        paren_depth  = 0;
        while (i < text.size && (paren_depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(')
                paren_depth++;
            else if (text.data[i] == ')' && paren_depth > 0)
                paren_depth--;
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        tbox_style_length length;
        if (tbox_style_parse_spacing_length(token, units, &length) && length.kind == TBOX_STYLE_LENGTH_PX) {
            if (length_count >= max_lengths)
                return false;
            lengths[length_count++] = length.value;
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            /* color kept */
        } else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("inset"))) {
            inset = true;
        } else {
            return false;
        }
    }

    if (length_count < 2 || lengths[2] < 0.0)
        return false;
    for (int j = 0; j < 4; j++)
        out_lengths[j] = lengths[j];
    *out_color = color;
    *out_inset = inset;
    return true;
}

static bool tbox_style_parse_list_style_type(tbox_string_view raw, tbox_style_list_style_type *out) {
    static const struct {
        const char *name;
        tbox_style_list_style_type value;
    } types[] = {
        { "disc",        TBOX_STYLE_LIST_STYLE_DISC        },
        { "circle",      TBOX_STYLE_LIST_STYLE_CIRCLE      },
        { "square",      TBOX_STYLE_LIST_STYLE_SQUARE      },
        { "decimal",     TBOX_STYLE_LIST_STYLE_DECIMAL     },
        { "decimal-leading-zero", TBOX_STYLE_LIST_STYLE_DECIMAL_LEADING_ZERO },
        { "lower-alpha", TBOX_STYLE_LIST_STYLE_LOWER_ALPHA },
        { "lower-latin", TBOX_STYLE_LIST_STYLE_LOWER_ALPHA },
        { "upper-alpha", TBOX_STYLE_LIST_STYLE_UPPER_ALPHA },
        { "upper-latin", TBOX_STYLE_LIST_STYLE_UPPER_ALPHA },
        { "lower-roman", TBOX_STYLE_LIST_STYLE_LOWER_ROMAN },
        { "upper-roman", TBOX_STYLE_LIST_STYLE_UPPER_ROMAN },
        { "none",        TBOX_STYLE_LIST_STYLE_NONE        },
    };
    tbox_string_view value = tbox_style_trim(raw);
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(types[i].name))) {
            *out = types[i].value;
            return true;
        }
    }
    return false;
}

/* `list-style-type` plus the `list-style` shorthand, of which only the type
 * keyword is supported (position and image tokens are ignored). A shorthand
 * without a type resets it to `disc`, as any shorthand resets its omitted
 * longhands. Inheritable. */
static tbox_style_list_style_type tbox_style_resolve_list_style_type(const tbox_css_computed_style *computed, tbox_style_list_style_type inherited) {
    tbox_style_list_style_type result              = inherited;
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("list-style"));
    if (shorthand != NULL) {
        result = TBOX_STYLE_LIST_STYLE_DISC;
        tbox_string_view tokens[4];
        size_t count;
        if (tbox_style_split_box_shorthand(shorthand->value, tokens, &count))
            for (size_t i = 0; i < count; i++)
                tbox_style_parse_list_style_type(tokens[i], &result);
    }
    const tbox_css_resolved_declaration *longhand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("list-style-type"));
    tbox_style_list_style_type parsed;
    if (tbox_style_border_longhand_wins(longhand, shorthand) && tbox_style_parse_list_style_type(longhand->value, &parsed))
        result = parsed;
    return result;
}

/* ---- flexbox ---- */

typedef struct tbox_style_keyword {
    const char *name;
    int value;
} tbox_style_keyword;

static bool tbox_style_lookup(tbox_string_view raw, const tbox_style_keyword *table, size_t count, int *out) {
    tbox_string_view value = tbox_style_trim(raw);
    for (size_t i = 0; i < count; i++) {
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(table[i].name))) {
            *out = table[i].value;
            return true;
        }
    }
    return false;
}

static const tbox_style_keyword tbox_style_flex_directions[] = {
    { "row",            TBOX_STYLE_FLEX_DIRECTION_ROW            },
    { "row-reverse",    TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE    },
    { "column",         TBOX_STYLE_FLEX_DIRECTION_COLUMN         },
    { "column-reverse", TBOX_STYLE_FLEX_DIRECTION_COLUMN_REVERSE },
};
static const tbox_style_keyword tbox_style_flex_wraps[] = {
    { "nowrap",       TBOX_STYLE_FLEX_WRAP_NOWRAP       },
    { "wrap",         TBOX_STYLE_FLEX_WRAP_WRAP         },
    { "wrap-reverse", TBOX_STYLE_FLEX_WRAP_WRAP_REVERSE },
};
/* justify-content: `stretch` behaves as `flex-start` there. */
static const tbox_style_keyword tbox_style_flex_justifies[] = {
    { "normal",        TBOX_STYLE_FLEX_JUSTIFY_NORMAL        },
    { "flex-start",    TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "start",         TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "left",          TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "stretch",       TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "flex-end",      TBOX_STYLE_FLEX_JUSTIFY_END           },
    { "end",           TBOX_STYLE_FLEX_JUSTIFY_END           },
    { "right",         TBOX_STYLE_FLEX_JUSTIFY_END           },
    { "center",        TBOX_STYLE_FLEX_JUSTIFY_CENTER        },
    { "space-between", TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN },
    { "space-around",  TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND  },
    { "space-evenly",  TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY  },
};
static const tbox_style_keyword tbox_style_flex_align_contents[] = {
    { "normal",        TBOX_STYLE_FLEX_JUSTIFY_NORMAL        },
    { "stretch",       TBOX_STYLE_FLEX_JUSTIFY_STRETCH       },
    { "flex-start",    TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "start",         TBOX_STYLE_FLEX_JUSTIFY_START         },
    { "flex-end",      TBOX_STYLE_FLEX_JUSTIFY_END           },
    { "end",           TBOX_STYLE_FLEX_JUSTIFY_END           },
    { "center",        TBOX_STYLE_FLEX_JUSTIFY_CENTER        },
    { "space-between", TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN },
    { "space-around",  TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND  },
    { "space-evenly",  TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY  },
};
/* align-items/align-self; `auto` (align-self only) is NORMAL. `last
 * baseline` aligns first baselines, the only kind supported. */
static const tbox_style_keyword tbox_style_flex_aligns[] = {
    { "normal",         TBOX_STYLE_FLEX_ALIGN_NORMAL   },
    { "auto",           TBOX_STYLE_FLEX_ALIGN_NORMAL   },
    { "stretch",        TBOX_STYLE_FLEX_ALIGN_STRETCH  },
    { "flex-start",     TBOX_STYLE_FLEX_ALIGN_START    },
    { "start",          TBOX_STYLE_FLEX_ALIGN_START    },
    { "self-start",     TBOX_STYLE_FLEX_ALIGN_START    },
    { "flex-end",       TBOX_STYLE_FLEX_ALIGN_END      },
    { "end",            TBOX_STYLE_FLEX_ALIGN_END      },
    { "self-end",       TBOX_STYLE_FLEX_ALIGN_END      },
    { "center",         TBOX_STYLE_FLEX_ALIGN_CENTER   },
    { "baseline",       TBOX_STYLE_FLEX_ALIGN_BASELINE },
    { "first baseline", TBOX_STYLE_FLEX_ALIGN_BASELINE },
    { "last baseline",  TBOX_STYLE_FLEX_ALIGN_BASELINE },
};
#define TBOX_STYLE_COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* A keyword property: the declared value when it's in `table`, else
 * `initial` (these properties are not inherited). */
static int tbox_style_resolve_keyword(const tbox_css_computed_style *computed, const char *property, const tbox_style_keyword *table, size_t count, int initial) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    int value                                 = initial;
    if (decl != NULL)
        tbox_style_lookup(decl->value, table, count, &value);
    return value;
}

/* A nonnegative number (flex-grow/flex-shrink). */
static bool tbox_style_parse_flex_factor(tbox_string_view raw, double *out) {
    double value;
    if (!tbox_style_parse_number(tbox_style_trim(raw), &value) || value < 0.0)
        return false;
    *out = value;
    return true;
}

/* A gap: `normal` (AUTO, i.e. 0) or a nonnegative px/em/percentage. */
static bool tbox_style_parse_gap(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_spacing_length(value, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO && out->value >= 0.0;
}

/* flex-basis: `auto`/`content` (AUTO) or a nonnegative length/percentage. */
static bool tbox_style_parse_flex_basis(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("content"))) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_spacing_length(value, units, out) && out->value >= 0.0;
}

/* The `flex` shorthand: `none` (0 0 auto), `auto` (1 1 auto), `initial`
 * (0 1 auto), or up to two numbers (grow, then shrink) and a basis in any
 * order; with a number but no basis the basis is 0%, and an omitted shrink
 * is 1. */
static bool tbox_style_parse_flex(tbox_string_view raw, const tbox_style_units *units, double *grow, double *shrink, tbox_style_length *basis) {
    tbox_string_view value             = tbox_style_trim(raw);
    const tbox_style_length auto_basis = { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none"))) {
        *grow = 0.0, *shrink = 0.0, *basis = auto_basis;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto"))) {
        *grow = 1.0, *shrink = 1.0, *basis = auto_basis;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("initial"))) {
        *grow = 0.0, *shrink = 1.0, *basis = auto_basis;
        return true;
    }
    tbox_string_view tokens[4];
    size_t count;
    if (!tbox_style_split_box_shorthand(value, tokens, &count) || count > 3)
        return false;
    /* [<grow> <shrink>?] || <basis>: the two factors must be adjacent. */
    double factors[2];
    size_t factor_count = 0;
    bool has_basis = false, previous_was_factor = false;
    tbox_style_length parsed_basis = { TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
    for (size_t i = 0; i < count; i++) {
        double factor;
        bool can_take_factor = factor_count == 0 || (factor_count == 1 && previous_was_factor);
        if (can_take_factor && tbox_style_parse_flex_factor(tokens[i], &factor)) {
            factors[factor_count++] = factor;
            previous_was_factor     = true;
        } else if (!has_basis && tbox_style_parse_flex_basis(tokens[i], units, &parsed_basis)) {
            has_basis           = true;
            previous_was_factor = false;
        } else {
            return false;
        }
    }
    *grow   = factor_count > 0 ? factors[0] : 1.0;
    *shrink = factor_count > 1 ? factors[1] : 1.0;
    *basis  = parsed_basis;
    return true;
}

static void tbox_style_resolve_flex(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style) {
    const tbox_css_resolved_declaration *flow = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-flow"));
    style->flex_direction                     = TBOX_STYLE_FLEX_DIRECTION_ROW;
    style->flex_wrap                          = TBOX_STYLE_FLEX_WRAP_NOWRAP;
    if (flow != NULL) {
        tbox_string_view tokens[4];
        size_t count;
        if (tbox_style_split_box_shorthand(flow->value, tokens, &count) && count <= 2) {
            for (size_t i = 0; i < count; i++) {
                int value;
                if (tbox_style_lookup(tokens[i], tbox_style_flex_directions, TBOX_STYLE_COUNT(tbox_style_flex_directions), &value))
                    style->flex_direction = (tbox_style_flex_direction)value;
                else if (tbox_style_lookup(tokens[i], tbox_style_flex_wraps, TBOX_STYLE_COUNT(tbox_style_flex_wraps), &value))
                    style->flex_wrap = (tbox_style_flex_wrap)value;
            }
        }
    }
    const tbox_css_resolved_declaration *direction = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-direction"));
    int value;
    if (tbox_style_border_longhand_wins(direction, flow) && tbox_style_lookup(direction->value, tbox_style_flex_directions, TBOX_STYLE_COUNT(tbox_style_flex_directions), &value))
        style->flex_direction = (tbox_style_flex_direction)value;
    const tbox_css_resolved_declaration *wrap = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-wrap"));
    if (tbox_style_border_longhand_wins(wrap, flow) && tbox_style_lookup(wrap->value, tbox_style_flex_wraps, TBOX_STYLE_COUNT(tbox_style_flex_wraps), &value))
        style->flex_wrap = (tbox_style_flex_wrap)value;

    style->justify_content = (tbox_style_flex_justify)tbox_style_resolve_keyword(computed, "justify-content", tbox_style_flex_justifies, TBOX_STYLE_COUNT(tbox_style_flex_justifies), TBOX_STYLE_FLEX_JUSTIFY_NORMAL);
    style->align_content   = (tbox_style_flex_justify)tbox_style_resolve_keyword(computed, "align-content", tbox_style_flex_align_contents, TBOX_STYLE_COUNT(tbox_style_flex_align_contents), TBOX_STYLE_FLEX_JUSTIFY_NORMAL);
    style->align_items     = (tbox_style_flex_align)tbox_style_resolve_keyword(computed, "align-items", tbox_style_flex_aligns, TBOX_STYLE_COUNT(tbox_style_flex_aligns), TBOX_STYLE_FLEX_ALIGN_NORMAL);
    style->align_self      = (tbox_style_flex_align)tbox_style_resolve_keyword(computed, "align-self", tbox_style_flex_aligns, TBOX_STYLE_COUNT(tbox_style_flex_aligns), TBOX_STYLE_FLEX_ALIGN_NORMAL);

    /* place-content: align-content [justify-content]; place-items and
     * place-self: their align- longhand (justify-items/-self have no
     * meaning in flex layout). A longhand of higher priority still wins. */
    const tbox_css_resolved_declaration *place = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("place-content"));
    if (place != NULL) {
        tbox_string_view tokens[3];
        size_t count = tbox_style_tokens(place->value, tokens, 3);
        int align, justify;
        if ((count == 1 || count == 2) && tbox_style_lookup(tokens[0], tbox_style_flex_align_contents, TBOX_STYLE_COUNT(tbox_style_flex_align_contents), &align) &&
            tbox_style_lookup(tokens[count - 1], tbox_style_flex_justifies, TBOX_STYLE_COUNT(tbox_style_flex_justifies), &justify)) {
            const tbox_css_resolved_declaration *ac = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("align-content"));
            const tbox_css_resolved_declaration *jc = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("justify-content"));
            if (!tbox_style_border_longhand_wins(ac, place))
                style->align_content = (tbox_style_flex_justify)align;
            if (!tbox_style_border_longhand_wins(jc, place))
                style->justify_content = (tbox_style_flex_justify)justify;
        }
    }
    static const char *const places[2][2] = { { "place-items", "align-items" }, { "place-self", "align-self" } };
    for (int i = 0; i < 2; i++) {
        place = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(places[i][0]));
        tbox_string_view tokens[3];
        size_t count;
        int value;
        if (place != NULL && (count = tbox_style_tokens(place->value, tokens, 3)) >= 1 && count <= 2 && tbox_style_lookup(tokens[0], tbox_style_flex_aligns, TBOX_STYLE_COUNT(tbox_style_flex_aligns), &value) &&
            !tbox_style_border_longhand_wins(tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(places[i][1])), place)) {
            if (i == 0)
                style->align_items = (tbox_style_flex_align)value;
            else
                style->align_self = (tbox_style_flex_align)value;
        }
    }

    /* gap: one value for both axes, or row then column. */
    style->row_gap = style->column_gap       = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *gap = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("gap"));
    if (gap != NULL) {
        tbox_string_view tokens[4];
        size_t count;
        tbox_style_length row, column;
        if (tbox_style_split_box_shorthand(gap->value, tokens, &count) && count <= 2 && tbox_style_parse_gap(tokens[0], units, &row) && tbox_style_parse_gap(tokens[count - 1], units, &column)) {
            style->row_gap    = row;
            style->column_gap = column;
        }
    }
    const tbox_css_resolved_declaration *row_gap = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("row-gap"));
    tbox_style_length parsed_gap;
    if (tbox_style_border_longhand_wins(row_gap, gap) && tbox_style_parse_gap(row_gap->value, units, &parsed_gap))
        style->row_gap = parsed_gap;
    const tbox_css_resolved_declaration *column_gap = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("column-gap"));
    if (tbox_style_border_longhand_wins(column_gap, gap) && tbox_style_parse_gap(column_gap->value, units, &parsed_gap))
        style->column_gap = parsed_gap;

    style->flex_grow                          = 0.0;
    style->flex_shrink                        = 1.0;
    style->flex_basis                         = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *flex = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex"));
    if (flex != NULL && !tbox_style_parse_flex(flex->value, units, &style->flex_grow, &style->flex_shrink, &style->flex_basis))
        flex = NULL; /* invalid: the longhands decide alone */
    const tbox_css_resolved_declaration *grow = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-grow"));
    double factor;
    if (tbox_style_border_longhand_wins(grow, flex) && tbox_style_parse_flex_factor(grow->value, &factor))
        style->flex_grow = factor;
    const tbox_css_resolved_declaration *shrink = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-shrink"));
    if (tbox_style_border_longhand_wins(shrink, flex) && tbox_style_parse_flex_factor(shrink->value, &factor))
        style->flex_shrink = factor;
    const tbox_css_resolved_declaration *basis = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("flex-basis"));
    tbox_style_length parsed_basis;
    if (tbox_style_border_longhand_wins(basis, flex) && tbox_style_parse_flex_basis(basis->value, units, &parsed_basis))
        style->flex_basis = parsed_basis;

    style->order                               = 0;
    const tbox_css_resolved_declaration *order = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("order"));
    double parsed_order;
    if (order != NULL && tbox_style_parse_number(tbox_style_trim(order->value), &parsed_order) && parsed_order == (double)(int)parsed_order)
        style->order = (int)parsed_order;
}

/* `accent-color`/`caret-color`: inheritable, `auto` stored as alpha 0 (see
 * tbox_style.accent_color). An unparsable value keeps the inherited one. */
static tbox_css_rgba tbox_style_resolve_control_color(const tbox_css_computed_style *computed, const char *property, tbox_css_rgba inherited, tbox_css_rgba current_color) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl == NULL)
        return inherited;
    if (tbox_string_view_equal_ascii_ci(tbox_style_trim(decl->value), tbox_string_view_from_cstr("auto")))
        return (tbox_css_rgba){ 0, 0, 0, 0 };
    tbox_css_rgba parsed;
    return tbox_style_parse_edge_color(decl->value, current_color, &parsed) ? parsed : inherited;
}

/* `position` recognizes `static`/`relative`, case-insensitive.
 * `absolute`/`fixed`/`sticky` added, same case-insensitive
 * treatment -- see ARCHITECTURE.md's v5 Style section (`sticky` is just
 * another enum value here; it is only treated as a synonym of `relative`
 * outside the Style layer). Any other value (absent, unparsable, or an
 * out-of-scope keyword) falls back to the initial value STATIC, same
 * posture as `display` since v0. */
static tbox_style_position tbox_style_resolve_position(const tbox_css_computed_style *computed) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("position"));
    if (decl != NULL) {
        tbox_string_view value = tbox_style_trim(decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("relative"))) {
            return TBOX_STYLE_POSITION_RELATIVE;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("absolute"))) {
            return TBOX_STYLE_POSITION_ABSOLUTE;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("fixed"))) {
            return TBOX_STYLE_POSITION_FIXED;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("sticky"))) {
            return TBOX_STYLE_POSITION_STICKY;
        }
    }
    return TBOX_STYLE_POSITION_STATIC;
}

/* In horizontal LTR mode, logical sizes share the physical dimension's
 * cascade. Parse only the winning declaration. */
static tbox_style_length tbox_style_resolve_logical_size(const tbox_css_computed_style *computed, const char *physical, const char *logical, const tbox_style_units *units, bool limit, tbox_style_size_keyword *out_keyword) {
    tbox_style_length result = { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *winner = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(physical));
    const tbox_css_resolved_declaration *alias = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(logical));
    if (alias != NULL && (winner == NULL || tbox_css_cascade_priority_compare(alias, winner) > 0)) winner = alias;
    if (out_keyword != NULL)
        *out_keyword = TBOX_STYLE_SIZE_KEYWORD_NONE;
    if (winner != NULL && out_keyword != NULL) {
        static const tbox_style_keyword keywords[] = {
            { "min-content", TBOX_STYLE_SIZE_KEYWORD_MIN_CONTENT },
            { "max-content", TBOX_STYLE_SIZE_KEYWORD_MAX_CONTENT },
            { "fit-content", TBOX_STYLE_SIZE_KEYWORD_FIT_CONTENT },
            { "-webkit-fit-content", TBOX_STYLE_SIZE_KEYWORD_FIT_CONTENT },
            { "-moz-fit-content", TBOX_STYLE_SIZE_KEYWORD_FIT_CONTENT },
        };
        int keyword;
        if (tbox_style_lookup(winner->value, keywords, sizeof(keywords) / sizeof(keywords[0]), &keyword)) {
            *out_keyword = (tbox_style_size_keyword)keyword;
            return result;
        }
    }
    if (winner != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_spacing_length(winner->value, units, &parsed) &&
            (!limit || (parsed.kind != TBOX_STYLE_LENGTH_AUTO && parsed.value >= 0.0))) result = parsed;
    }
    return result;
}

/* `<img width="100" height="100">`: real HTML lets these bare numeric
 * attributes (no unit, unlike CSS) set the SAME properties as `width`/
 * `height` in CSS, but as a low-priority "presentational hint" -- any CSS
 * declaration (author OR the UA stylesheet) still wins outright, which is
 * exactly what calling this ONLY when the CSS dimension resolver
 * already came back AUTO (no cascade declaration won) already guarantees,
 * with zero cascade/specificity machinery of its own. Applies to `<img>` and
 * `<input type="image">`, and is
 * deliberately narrow: the first place Style reads a plain HTML attribute
 * for anything beyond `style`/`class`/`id` (see tbox_style_resolve's own
 * `(void)node` comment below, still true for every OTHER property). Returns
 * AUTO (a no-op override) unless `node` is an image element with a
 * `name`-named attribute (e.g. `name == "width"`) whose value parses as a
 * bare number via tbox_style_parse_number (e.g. "100" -- not "100px", real
 * HTML doesn't allow a unit here). */
static tbox_style_length tbox_style_resolve_img_dimension_attribute(const tbox_html_node *node, const char *name) {
    tbox_style_length result = { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) {
        return result;
    }
    bool is_img                     = tbox_string_view_equal_ascii_ci(node->element.tag_name, tbox_string_view_from_cstr("img"));
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("type"));
    bool is_image_input             = tbox_string_view_equal_ascii_ci(node->element.tag_name, tbox_string_view_from_cstr("input")) && type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_from_cstr("image"));
    if (!is_img && !is_image_input)
        return result;

    const tbox_html_attribute *attr = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    if (attr == NULL) {
        return result;
    }

    double value;
    if (tbox_style_parse_number(tbox_style_trim(attr->value), &value)) {
        result.kind  = TBOX_STYLE_LENGTH_PX;
        result.value = value;
    }
    return result;
}

/* ---- tokens shared by the properties below ---- */

static bool tbox_style_is(tbox_string_view value, const char *keyword) {
    return tbox_string_view_equal_ascii_ci(tbox_style_trim(value), tbox_string_view_from_cstr(keyword));
}

/* Splits `text` at top-level occurrences of `separator` (outside
 * parentheses and quotes) into at most `max` trimmed parts. Returns the
 * part count, or 0 when there are more than `max` parts or an empty one. */
static size_t tbox_style_split_top_level(tbox_string_view text, char separator, tbox_string_view *parts, size_t max) {
    size_t count = 0, start = 0;
    int depth  = 0;
    char quote = 0;
    for (size_t i = 0; i <= text.size; i++) {
        char c = i < text.size ? text.data[i] : separator;
        if (quote != 0) {
            if (c == quote)
                quote = 0;
            if (i < text.size)
                continue;
        }
        if (c == '"' || c == '\'')
            quote = c;
        else if (c == '(')
            depth++;
        else if (c == ')' && depth > 0)
            depth--;
        else if ((c == separator && depth == 0) || i == text.size) {
            tbox_string_view part = tbox_style_trim(tbox_string_view_make(text.data + start, i - start));
            if (part.size == 0 || count == max)
                return 0;
            parts[count++] = part;
            start          = i + 1;
        }
    }
    return count;
}

/* Whitespace-separated tokens, keeping `f(a, b)` and quoted strings whole;
 * `/` is a token of its own. Returns the count, 0 when over `max`. */
static size_t tbox_style_tokens(tbox_string_view text, tbox_string_view *tokens, size_t max) {
    size_t count = 0, i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i]))
            i++;
        if (i >= text.size)
            break;
        size_t start = i;
        if (text.data[i] == '/') {
            i++;
        } else {
            int depth  = 0;
            char quote = 0;
            while (i < text.size && (depth > 0 || quote != 0 || (!tbox_style_is_space(text.data[i]) && text.data[i] != '/'))) {
                char c = text.data[i];
                if (quote != 0) {
                    if (c == quote)
                        quote = 0;
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '(') {
                    depth++;
                } else if (c == ')' && depth > 0) {
                    depth--;
                }
                i++;
            }
        }
        if (count == max)
            return 0;
        tokens[count++] = tbox_string_view_make(text.data + start, i - start);
    }
    return count;
}

/* `name(...)`: the arguments between the parentheses. */
static bool tbox_style_function_args(tbox_string_view token, const char *name, tbox_string_view *args) {
    size_t length = strlen(name);
    if (token.size < length + 2 || token.data[length] != '(' || token.data[token.size - 1] != ')' ||
        !tbox_string_view_equal_ascii_ci(tbox_string_view_make(token.data, length), tbox_string_view_make(name, length)))
        return false;
    *args = tbox_style_trim(tbox_string_view_make(token.data + length + 1, token.size - length - 2));
    return true;
}

/* An integer (z-index, line-clamp, ...). */
static bool tbox_style_parse_integer(tbox_string_view raw, int *out) {
    double value;
    if (!tbox_style_parse_number(tbox_style_trim(raw), &value) || value != (double)(int)value)
        return false;
    *out = (int)value;
    return true;
}

/* An angle in degrees: deg, grad, rad, turn, or a bare 0. */
static bool tbox_style_parse_angle(tbox_string_view raw, double *out) {
    static const struct {
        const char *unit;
        double degrees;
    } units[] = {
        { "deg",  1.0                   },
        { "grad", 0.9                   },
        { "rad",  57.29577951308232     },
        { "turn", 360.0                 },
    };
    tbox_string_view text = tbox_style_trim(raw);
    if (text.size == 1 && text.data[0] == '0') {
        *out = 0.0;
        return true;
    }
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        size_t length = strlen(units[i].unit);
        double value;
        if (text.size > length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data + text.size - length, length), tbox_string_view_make(units[i].unit, length)) &&
            tbox_style_parse_number(tbox_string_view_make(text.data, text.size - length), &value)) {
            *out = value * units[i].degrees;
            return true;
        }
    }
    return false;
}

/* ---- background ---- */

/* `url(x)`, `url("x")` or `url('x')` into `out` (NUL-terminated,
 * truncated to `capacity`). */
static bool tbox_style_parse_url(tbox_string_view token, char *out, size_t capacity) {
    tbox_string_view args;
    if (!tbox_style_function_args(token, "url", &args))
        return false;
    if (args.size >= 2 && (args.data[0] == '"' || args.data[0] == '\'') && args.data[args.size - 1] == args.data[0])
        args = tbox_string_view_make(args.data + 1, args.size - 2);
    size_t n = args.size < capacity - 1 ? args.size : capacity - 1;
    memcpy(out, args.data, n);
    out[n] = '\0';
    return n > 0;
}

/* background-position / gradient `at` component offsets: a keyword (as
 * object-position), a length-percentage, or (4-value syntax) an edge
 * keyword followed by an offset from that edge -- `right 10px` is
 * `calc(100% - 10px)`. Up to four tokens; false when invalid. */
static bool tbox_style_parse_position(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_length out[2]) {
    tbox_style_length x = { TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 }, y = x;
    if (count == 1 || count == 2) {
        tbox_string_view pair[2];
        for (size_t i = 0; i < count; i++)
            pair[i] = tokens[i];
        /* A single token is horizontal unless it is top/bottom. */
        if (count == 1) {
            if (!tbox_style_parse_object_position_component(pair[0], units, true, &x) && !tbox_style_parse_object_position_component(pair[0], units, false, &y))
                return false;
        } else if (!(tbox_style_parse_object_position_component(pair[0], units, true, &x) && tbox_style_parse_object_position_component(pair[1], units, false, &y)) &&
                   !(tbox_style_parse_object_position_component(pair[0], units, false, &y) && tbox_style_parse_object_position_component(pair[1], units, true, &x))) {
            return false;
        }
        out[0] = x;
        out[1] = y;
        return true;
    }
    if (count != 3 && count != 4)
        return false;
    /* Edge keyword + optional offset, twice. */
    bool have_x = false, have_y = false;
    size_t i = 0;
    while (i < count) {
        tbox_string_view keyword = tokens[i++];
        tbox_style_length offset = { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
        bool has_offset          = i < count && tbox_style_parse_spacing_length(tokens[i], units, &offset) && offset.kind != TBOX_STYLE_LENGTH_AUTO;
        if (has_offset)
            i++;
        double sign = 1.0, base = 0.0;
        bool horizontal;
        if (tbox_style_is(keyword, "left") || tbox_style_is(keyword, "right")) {
            horizontal = true;
            if (tbox_style_is(keyword, "right"))
                sign = -1.0, base = 100.0;
        } else if (tbox_style_is(keyword, "top") || tbox_style_is(keyword, "bottom")) {
            horizontal = false;
            if (tbox_style_is(keyword, "bottom"))
                sign = -1.0, base = 100.0;
        } else if (tbox_style_is(keyword, "center") && !has_offset) {
            horizontal = !have_x;
        } else {
            return false;
        }
        tbox_style_length value;
        if (tbox_style_is(keyword, "center"))
            value = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
        else if (offset.kind == TBOX_STYLE_LENGTH_PERCENT)
            value = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, base + sign * offset.value, sign * offset.px_offset, 0, 0.0, 0.0 };
        else
            value = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, base, sign * offset.value, 0, 0.0, 0.0 };
        if (horizontal ? have_x : have_y)
            return false;
        if (horizontal)
            x = value, have_x = true;
        else
            y = value, have_y = true;
    }
    out[0] = x;
    out[1] = y;
    return true;
}

static void tbox_style_init_gradient(tbox_style_gradient *gradient) {
    memset(gradient, 0, sizeof(*gradient));
    gradient->angle     = 180.0;
    gradient->center[0] = gradient->center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
}

/* One `<color> [<length-percentage> [<length-percentage>]]` stop (two
 * positions become two stops). */
static bool tbox_style_parse_gradient_stop(tbox_string_view part, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_gradient *out) {
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(part, tokens, 3);
    if (count == 0)
        return false;
    tbox_css_rgba color;
    size_t color_index = 0;
    if (!tbox_style_parse_edge_color(tokens[0], current_color, &color)) {
        /* The color may also come after the position. */
        if (count != 2 || !tbox_style_parse_edge_color(tokens[1], current_color, &color))
            return false;
        color_index = 1;
    }
    tbox_style_length positions[2];
    size_t position_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (i == color_index)
            continue;
        if (!tbox_style_parse_spacing_length(tokens[i], units, &positions[position_count]) || positions[position_count].kind == TBOX_STYLE_LENGTH_AUTO)
            return false;
        position_count++;
    }
    if (position_count == 0)
        positions[position_count++] = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    for (size_t i = 0; i < position_count; i++) {
        if (out->stop_count == TBOX_STYLE_MAX_GRADIENT_STOPS)
            return false;
        out->stops[out->stop_count++] = (tbox_style_gradient_stop){ color, positions[i] };
    }
    return true;
}

/* `[repeating-]linear-gradient(...)` / `[repeating-]radial-gradient(...)`. */
static bool tbox_style_parse_gradient(tbox_string_view token, const tbox_style_units *units, tbox_css_rgba current_color, tbox_style_gradient *out) {
    tbox_style_gradient gradient;
    tbox_style_init_gradient(&gradient);
    tbox_string_view args;
    if (tbox_style_function_args(token, "linear-gradient", &args) || (gradient.repeating = tbox_style_function_args(token, "repeating-linear-gradient", &args)))
        gradient.kind = TBOX_STYLE_GRADIENT_LINEAR;
    else if (tbox_style_function_args(token, "radial-gradient", &args) || (gradient.repeating = tbox_style_function_args(token, "repeating-radial-gradient", &args)))
        gradient.kind = TBOX_STYLE_GRADIENT_RADIAL;
    else
        return false;

    tbox_string_view parts[TBOX_STYLE_MAX_GRADIENT_STOPS + 1];
    size_t part_count = tbox_style_split_top_level(args, ',', parts, TBOX_STYLE_MAX_GRADIENT_STOPS + 1);
    if (part_count == 0)
        return false;
    size_t first_stop = 0;

    /* The optional first part: direction or shape/extent/center. */
    tbox_string_view tokens[8];
    size_t count = tbox_style_tokens(parts[0], tokens, 8);
    if (gradient.kind == TBOX_STYLE_GRADIENT_LINEAR) {
        double angle;
        if (count == 1 && tbox_style_parse_angle(tokens[0], &angle)) {
            gradient.angle = angle;
            first_stop     = 1;
        } else if (count >= 2 && count <= 3 && tbox_style_is(tokens[0], "to")) {
            int dx = 0, dy = 0;
            for (size_t i = 1; i < count; i++) {
                if (tbox_style_is(tokens[i], "left") && dx == 0) dx = -1;
                else if (tbox_style_is(tokens[i], "right") && dx == 0) dx = 1;
                else if (tbox_style_is(tokens[i], "top") && dy == 0) dy = -1;
                else if (tbox_style_is(tokens[i], "bottom") && dy == 0) dy = 1;
                else return false;
            }
            if (dx != 0 && dy != 0) {
                gradient.corner[0] = dx;
                gradient.corner[1] = dy;
            } else {
                gradient.angle = dx == 1 ? 90.0 : dx == -1 ? 270.0 : dy == -1 ? 0.0 : 180.0;
            }
            first_stop = 1;
        }
    } else {
        bool shape_part = false;
        size_t i        = 0;
        while (i < count) {
            if (tbox_style_is(tokens[i], "circle")) gradient.circle = true;
            else if (tbox_style_is(tokens[i], "ellipse")) gradient.circle = false;
            else if (tbox_style_is(tokens[i], "farthest-corner")) gradient.extent = TBOX_STYLE_GRADIENT_FARTHEST_CORNER;
            else if (tbox_style_is(tokens[i], "farthest-side")) gradient.extent = TBOX_STYLE_GRADIENT_FARTHEST_SIDE;
            else if (tbox_style_is(tokens[i], "closest-corner")) gradient.extent = TBOX_STYLE_GRADIENT_CLOSEST_CORNER;
            else if (tbox_style_is(tokens[i], "closest-side")) gradient.extent = TBOX_STYLE_GRADIENT_CLOSEST_SIDE;
            else if (tbox_style_is(tokens[i], "at")) {
                if (!tbox_style_parse_position(tokens + i + 1, count - i - 1, units, gradient.center))
                    return false;
                i = count;
                shape_part = true;
                break;
            } else break;
            shape_part = true;
            i++;
        }
        if (shape_part && i != count)
            return false;
        if (shape_part)
            first_stop = 1;
    }

    for (size_t p = first_stop; p < part_count; p++)
        if (!tbox_style_parse_gradient_stop(parts[p], units, current_color, &gradient))
            return false;
    if (gradient.stop_count < 2)
        return false;
    *out = gradient;
    return true;
}

static bool tbox_style_parse_background_size(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_background_size_kind *kind, tbox_style_length size[2]) {
    if (count == 1 && tbox_style_is(tokens[0], "cover")) {
        *kind = TBOX_STYLE_BACKGROUND_SIZE_COVER;
        return true;
    }
    if (count == 1 && tbox_style_is(tokens[0], "contain")) {
        *kind = TBOX_STYLE_BACKGROUND_SIZE_CONTAIN;
        return true;
    }
    if (count < 1 || count > 2)
        return false;
    tbox_style_length parsed[2] = { { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 }, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
    for (size_t i = 0; i < count; i++)
        if (!tbox_style_parse_spacing_length(tokens[i], units, &parsed[i]) || parsed[i].value < 0.0)
            return false;
    *kind   = TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT;
    size[0] = parsed[0];
    size[1] = parsed[1];
    return true;
}

/* One or two repeat keywords; `space`/`round` repeat plainly. */
static bool tbox_style_parse_background_repeat(const tbox_string_view *tokens, size_t count, bool *x, bool *y) {
    if (count == 1 && tbox_style_is(tokens[0], "repeat-x")) {
        *x = true, *y = false;
        return true;
    }
    if (count == 1 && tbox_style_is(tokens[0], "repeat-y")) {
        *x = false, *y = true;
        return true;
    }
    if (count < 1 || count > 2)
        return false;
    bool values[2];
    for (size_t i = 0; i < count; i++) {
        if (tbox_style_is(tokens[i], "repeat") || tbox_style_is(tokens[i], "space") || tbox_style_is(tokens[i], "round"))
            values[i] = true;
        else if (tbox_style_is(tokens[i], "no-repeat"))
            values[i] = false;
        else
            return false;
    }
    *x = values[0];
    *y = values[count - 1];
    return true;
}

static bool tbox_style_is_position_token(tbox_string_view token, const tbox_style_units *units) {
    tbox_style_length length;
    return tbox_style_is(token, "left") || tbox_style_is(token, "right") || tbox_style_is(token, "top") || tbox_style_is(token, "bottom") || tbox_style_is(token, "center") ||
           (tbox_style_parse_spacing_length(token, units, &length) && length.kind != TBOX_STYLE_LENGTH_AUTO);
}

static bool tbox_style_is_repeat_token(tbox_string_view token) {
    return tbox_style_is(token, "repeat") || tbox_style_is(token, "no-repeat") || tbox_style_is(token, "repeat-x") || tbox_style_is(token, "repeat-y") ||
           tbox_style_is(token, "space") || tbox_style_is(token, "round");
}

/* An image layer value: none, url() or a gradient. */
static bool tbox_style_parse_background_image(tbox_string_view token, const tbox_style_units *units, tbox_css_rgba current_color, char image[256], tbox_style_gradient *gradient) {
    if (tbox_style_is(token, "none")) {
        image[0]       = '\0';
        gradient->kind = TBOX_STYLE_GRADIENT_NONE;
        return true;
    }
    char url[256];
    if (tbox_style_parse_url(token, url, sizeof(url))) {
        memcpy(image, url, sizeof(url));
        gradient->kind = TBOX_STYLE_GRADIENT_NONE;
        return true;
    }
    tbox_style_gradient parsed;
    if (tbox_style_parse_gradient(token, units, current_color, &parsed)) {
        image[0]  = '\0';
        *gradient = parsed;
        return true;
    }
    return false;
}

/* Everything the `background` shorthand and its longhands set, resolved
 * with ordinary cascade precedence between them. Each comma-separated
 * layer keeps its own image, size, position and repeat (the color only in
 * the last one, where CSS allows it); longhand lists repeat to cover the
 * image layers. */
static void tbox_style_init_layer(tbox_style_background_layer *layer) {
    memset(layer, 0, sizeof(*layer));
    tbox_style_init_gradient(&layer->gradient);
    layer->size[0] = layer->size[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    layer->position[0] = layer->position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 };
    layer->repeat_x = layer->repeat_y = true;
}

static void tbox_style_resolve_background(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style) {
    tbox_style_background_layer layers[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
    size_t layer_count = 1;
    for (size_t l = 0; l < TBOX_STYLE_MAX_BACKGROUND_LAYERS; l++)
        tbox_style_init_layer(&layers[l]);
    tbox_css_rgba background_color                  = { 0, 0, 0, 0 };
    tbox_style_background_clip background_clip      = TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX;
    tbox_style_background_origin background_origin  = TBOX_STYLE_BACKGROUND_ORIGIN_PADDING_BOX;

    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background"));
    if (shorthand != NULL) {
        tbox_string_view parts[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
        size_t part_count = tbox_style_split_top_level(shorthand->value, ',', parts, TBOX_STYLE_MAX_BACKGROUND_LAYERS);
        tbox_style_background_layer parsed[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
        tbox_css_rgba parsed_color                   = background_color;
        tbox_style_background_clip parsed_clip       = background_clip;
        tbox_style_background_origin parsed_origin   = background_origin;
        bool valid                                   = part_count > 0;
        for (size_t l = 0; valid && l < part_count; l++) {
            tbox_style_init_layer(&parsed[l]);
            tbox_string_view tokens[16];
            size_t count      = tbox_style_tokens(parts[l], tokens, 16);
            valid             = count > 0;
            size_t i          = 0;
            size_t boxes_seen = 0;
            while (valid && i < count) {
                tbox_css_rgba color;
                if (tbox_style_is_position_token(tokens[i], units)) {
                    size_t begin = i;
                    while (i < count && i - begin < 4 && tbox_style_is_position_token(tokens[i], units))
                        i++;
                    valid = tbox_style_parse_position(tokens + begin, i - begin, units, parsed[l].position);
                    if (valid && i < count && tokens[i].size == 1 && tokens[i].data[0] == '/') {
                        size_t size_start = ++i;
                        while (i < count && i - size_start < 2 && !tbox_style_is_repeat_token(tokens[i]) && (tbox_style_is(tokens[i], "auto") || tbox_style_is(tokens[i], "cover") || tbox_style_is(tokens[i], "contain") || tbox_style_is_position_token(tokens[i], units)))
                            i++;
                        valid = tbox_style_parse_background_size(tokens + size_start, i - size_start, units, &parsed[l].size_kind, parsed[l].size);
                    }
                } else if (tbox_style_is_repeat_token(tokens[i])) {
                    size_t begin = i;
                    while (i < count && i - begin < 2 && tbox_style_is_repeat_token(tokens[i]))
                        i++;
                    valid = tbox_style_parse_background_repeat(tokens + begin, i - begin, &parsed[l].repeat_x, &parsed[l].repeat_y);
                } else if (tbox_style_parse_background_image(tokens[i], units, style->color, parsed[l].image, &parsed[l].gradient)) {
                    i++;
                } else if (tbox_style_is(tokens[i], "border-box") || tbox_style_is(tokens[i], "padding-box") || tbox_style_is(tokens[i], "content-box")) {
                    /* One box sets origin and clip; a second sets the clip
                     * (of the first layer only: both apply per element). */
                    tbox_style_background_clip clip = tbox_style_is(tokens[i], "padding-box") ? TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX : tbox_style_is(tokens[i], "content-box") ? TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX : TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX;
                    if (l == 0) {
                        if (!boxes_seen)
                            parsed_origin = clip == TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX ? TBOX_STYLE_BACKGROUND_ORIGIN_PADDING_BOX : clip == TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX ? TBOX_STYLE_BACKGROUND_ORIGIN_CONTENT_BOX : TBOX_STYLE_BACKGROUND_ORIGIN_BORDER_BOX;
                        parsed_clip = clip;
                        boxes_seen++;
                    }
                    i++;
                } else if (tbox_style_is(tokens[i], "scroll") || tbox_style_is(tokens[i], "fixed") || tbox_style_is(tokens[i], "local")) {
                    i++; /* attachment: always scrolls with the box */
                } else if (l == part_count - 1 && tbox_style_parse_edge_color(tokens[i], style->color, &color)) {
                    parsed_color = color;
                    i++;
                } else {
                    valid = false;
                }
            }
        }
        if (valid) {
            memcpy(layers, parsed, part_count * sizeof(parsed[0]));
            layer_count       = part_count;
            background_color  = parsed_color;
            background_clip   = parsed_clip;
            background_origin = parsed_origin;
        } else {
            shorthand = NULL;
        }
    }

    /* Longhands beat the shorthand only with higher cascade priority. */
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-color"));
    tbox_css_rgba color;
    if (tbox_style_border_longhand_wins(decl, shorthand) && tbox_style_parse_edge_color(decl->value, style->color, &color))
        background_color = color;
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-image"));
    if (tbox_style_border_longhand_wins(decl, shorthand)) {
        tbox_string_view parts[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
        size_t count = tbox_style_split_top_level(decl->value, ',', parts, TBOX_STYLE_MAX_BACKGROUND_LAYERS);
        if (count > 0) {
            for (size_t l = 0; l < count; l++)
                if (!tbox_style_parse_background_image(parts[l], units, style->color, layers[l].image, &layers[l].gradient))
                    tbox_style_parse_background_image(tbox_string_view_from_cstr("none"), units, style->color, layers[l].image, &layers[l].gradient);
            for (size_t l = count; l < layer_count; l++)
                tbox_style_parse_background_image(tbox_string_view_from_cstr("none"), units, style->color, layers[l].image, &layers[l].gradient);
            layer_count = count;
        }
    }
    /* The per-layer longhands: their lists repeat over the layers. */
    static const char *const lists[4] = { "background-size", "background-position", "background-repeat", NULL };
    for (int k = 0; lists[k] != NULL; k++) {
        decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(lists[k]));
        if (!tbox_style_border_longhand_wins(decl, shorthand))
            continue;
        tbox_string_view parts[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
        size_t count = tbox_style_split_top_level(decl->value, ',', parts, TBOX_STYLE_MAX_BACKGROUND_LAYERS);
        for (size_t l = 0; count > 0 && l < layer_count; l++) {
            tbox_string_view tokens[4];
            size_t token_count = tbox_style_tokens(parts[l % count], tokens, 4);
            if (token_count == 0)
                continue;
            if (k == 0)
                tbox_style_parse_background_size(tokens, token_count, units, &layers[l].size_kind, layers[l].size);
            else if (k == 1)
                tbox_style_parse_position(tokens, token_count, units, layers[l].position);
            else
                tbox_style_parse_background_repeat(tokens, token_count, &layers[l].repeat_x, &layers[l].repeat_y);
        }
    }
    static const char *const axes[2] = { "background-position-x", "background-position-y" };
    for (int axis = 0; axis < 2; axis++) {
        decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(axes[axis]));
        if (!tbox_style_border_longhand_wins(decl, shorthand))
            continue;
        tbox_string_view parts[TBOX_STYLE_MAX_BACKGROUND_LAYERS];
        size_t count = tbox_style_split_top_level(decl->value, ',', parts, TBOX_STYLE_MAX_BACKGROUND_LAYERS);
        for (size_t l = 0; count > 0 && l < layer_count; l++) {
            tbox_style_length length;
            if (tbox_style_parse_object_position_component(parts[l % count], units, axis == 0, &length))
                layers[l].position[axis] = length;
        }
    }
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-origin"));
    if (tbox_style_border_longhand_wins(decl, shorthand)) {
        if (tbox_style_is(decl->value, "padding-box")) background_origin = TBOX_STYLE_BACKGROUND_ORIGIN_PADDING_BOX;
        else if (tbox_style_is(decl->value, "content-box")) background_origin = TBOX_STYLE_BACKGROUND_ORIGIN_CONTENT_BOX;
        else if (tbox_style_is(decl->value, "border-box")) background_origin = TBOX_STYLE_BACKGROUND_ORIGIN_BORDER_BOX;
    }
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-clip"));
    if (tbox_style_border_longhand_wins(decl, shorthand)) {
        if (tbox_style_is(decl->value, "padding-box")) background_clip = TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX;
        else if (tbox_style_is(decl->value, "content-box")) background_clip = TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX;
        else if (tbox_style_is(decl->value, "border-box")) background_clip = TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX;
    }

    style->background_color     = background_color;
    style->background_clip      = background_clip;
    style->background_origin    = background_origin;
    memcpy(style->background_image, layers[0].image, sizeof(style->background_image));
    style->background_gradient  = layers[0].gradient;
    style->background_size_kind = layers[0].size_kind;
    style->background_size[0]   = layers[0].size[0];
    style->background_size[1]   = layers[0].size[1];
    style->background_position[0] = layers[0].position[0];
    style->background_position[1] = layers[0].position[1];
    style->background_repeat_x  = layers[0].repeat_x;
    style->background_repeat_y  = layers[0].repeat_y;
    style->background_layer_count = layer_count;
    for (size_t l = 1; l < layer_count; l++)
        style->background_layers[l - 1] = layers[l];
}

/* ---- shadows ---- */

/* A comma-separated shadow list, or `none` (count 0). Each entry follows
 * tbox_style_parse_shadow; `inset` is only accepted when `allow_inset`. */
static bool tbox_style_parse_shadow_list(tbox_string_view raw, const tbox_style_units *units, tbox_css_rgba current_color, int max_lengths, bool allow_inset, tbox_style_shadow out[TBOX_STYLE_MAX_SHADOWS], size_t *out_count) {
    if (tbox_style_is(raw, "none")) {
        *out_count = 0;
        return true;
    }
    tbox_string_view parts[TBOX_STYLE_MAX_SHADOWS];
    size_t count = tbox_style_split_top_level(raw, ',', parts, TBOX_STYLE_MAX_SHADOWS);
    if (count == 0)
        return false;
    tbox_style_shadow parsed[TBOX_STYLE_MAX_SHADOWS];
    for (size_t i = 0; i < count; i++) {
        double lengths[4];
        tbox_css_rgba color;
        bool inset = false;
        if (!tbox_style_parse_shadow(parts[i], units, current_color, max_lengths, lengths, &color, &inset) || (inset && !allow_inset))
            return false;
        parsed[i] = (tbox_style_shadow){ lengths[0], lengths[1], lengths[2], lengths[3], color, inset };
    }
    memcpy(out, parsed, count * sizeof(parsed[0]));
    *out_count = count;
    return true;
}

/* ---- border-radius with an optional `/ vertical` part ---- */

/* 1-4 radii (clockwise from top-left, CSS's shorthand expansion) into px
 * and percent arrays; false when any is invalid. */
static bool tbox_style_parse_radius_list(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, double px[4], double percent[4]) {
    if (count < 1 || count > 4)
        return false;
    double parsed[4], parsed_percent[4];
    for (size_t i = 0; i < count; i++)
        if (!tbox_style_parse_radius(tokens[i], units, &parsed[i], &parsed_percent[i]))
            return false;
    const size_t index[4] = { 0, count > 1 ? 1 : 0, count > 2 ? 2 : 0, count > 3 ? 3 : count > 1 ? 1 : 0 };
    for (size_t i = 0; i < 4; i++) {
        px[i]      = parsed[index[i]];
        percent[i] = parsed_percent[index[i]];
    }
    return true;
}

/* ---- transform: translate ---- */

/* `translate(x[, y])`, `translateX(x)`, `translateY(y)` (several functions
 * add up), `none`; any other transform function makes the whole value
 * invalid. Percentages are of the element's own border box. */
static bool tbox_style_parse_transform(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out_x, tbox_style_length *out_y) {
    tbox_style_length x = { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 }, y = x;
    if (tbox_style_is(raw, "none")) {
        *out_x = x;
        *out_y = y;
        return true;
    }
    tbox_string_view functions[8];
    size_t count = tbox_style_tokens(raw, functions, 8);
    if (count == 0)
        return false;
    for (size_t f = 0; f < count; f++) {
        tbox_string_view args, parts[2];
        size_t part_count;
        tbox_style_length values[2] = { { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 }, { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 } };
        int axis;
        if (tbox_style_function_args(functions[f], "translate", &args))
            axis = 2;
        else if (tbox_style_function_args(functions[f], "translateX", &args))
            axis = 0;
        else if (tbox_style_function_args(functions[f], "translateY", &args))
            axis = 1;
        else
            return false;
        part_count = tbox_style_split_top_level(args, ',', parts, axis == 2 ? 2 : 1);
        if (part_count == 0)
            return false;
        for (size_t i = 0; i < part_count; i++)
            if (!tbox_style_parse_spacing_length(parts[i], units, &values[i]) || values[i].kind == TBOX_STYLE_LENGTH_AUTO)
                return false;
        tbox_style_length *targets[2] = { axis == 1 ? &y : &x, &y };
        for (size_t i = 0; i < (axis == 2 ? 2u : 1u); i++) {
            tbox_style_length *t = targets[i];
            tbox_style_length v  = values[i];
            /* Sum into one PX or PERCENT length. */
            if (v.kind == TBOX_STYLE_LENGTH_PX)
                t->kind == TBOX_STYLE_LENGTH_PX ? (void)(t->value += v.value) : (void)(t->px_offset += v.value);
            else if (t->kind == TBOX_STYLE_LENGTH_PX)
                *t = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, v.value, v.px_offset + t->value, 0, 0.0, 0.0 };
            else
                t->value += v.value, t->px_offset += v.px_offset;
        }
    }
    *out_x = x;
    *out_y = y;
    return true;
}

/* The `translate` property: `none` or `x [y [z]]` (z ignored). */
static bool tbox_style_parse_translate(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out_x, tbox_style_length *out_y) {
    tbox_style_length x = { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 }, y = x;
    if (!tbox_style_is(raw, "none")) {
        tbox_string_view tokens[3];
        size_t count = tbox_style_tokens(raw, tokens, 3);
        if (count == 0 || !tbox_style_parse_spacing_length(tokens[0], units, &x) || x.kind == TBOX_STYLE_LENGTH_AUTO ||
            (count > 1 && (!tbox_style_parse_spacing_length(tokens[1], units, &y) || y.kind == TBOX_STYLE_LENGTH_AUTO)))
            return false;
    }
    *out_x = x;
    *out_y = y;
    return true;
}

/* ---- font weight / stretch ---- */

/* CSS Fonts' relative weight table for bolder/lighter. */
static int tbox_style_relative_weight(int inherited, bool bolder) {
    if (bolder)
        return inherited < 350 ? 400 : inherited < 550 ? 700 : inherited < 900 ? 900 : inherited;
    return inherited < 100 ? inherited : inherited < 550 ? 100 : inherited < 750 ? 400 : 700;
}

static bool tbox_style_parse_font_weight(tbox_string_view raw, int inherited, int *out) {
    tbox_string_view value = tbox_style_trim(raw);
    double number;
    if (tbox_style_is(value, "normal")) *out = 400;
    else if (tbox_style_is(value, "bold")) *out = 700;
    else if (tbox_style_is(value, "bolder")) *out = tbox_style_relative_weight(inherited, true);
    else if (tbox_style_is(value, "lighter")) *out = tbox_style_relative_weight(inherited, false);
    else if (tbox_style_parse_number(value, &number) && number >= 1.0 && number <= 1000.0) *out = (int)(number + 0.5);
    else return false;
    return true;
}

static const struct {
    const char *name;
    double percent;
} tbox_style_font_stretches[] = {
    { "ultra-condensed", 50.0  },
    { "extra-condensed", 62.5  },
    { "condensed",       75.0  },
    { "semi-condensed",  87.5  },
    { "normal",          100.0 },
    { "semi-expanded",   112.5 },
    { "expanded",        125.0 },
    { "extra-expanded",  150.0 },
    { "ultra-expanded",  200.0 },
};

static bool tbox_style_parse_font_stretch(tbox_string_view raw, double *out) {
    tbox_string_view value = tbox_style_trim(raw);
    for (size_t i = 0; i < sizeof(tbox_style_font_stretches) / sizeof(tbox_style_font_stretches[0]); i++) {
        if (tbox_style_is(value, tbox_style_font_stretches[i].name)) {
            *out = tbox_style_font_stretches[i].percent;
            return true;
        }
    }
    double percent;
    if (value.size > 1 && value.data[value.size - 1] == '%' && tbox_style_parse_number(tbox_string_view_make(value.data, value.size - 1), &percent) && percent > 0.0) {
        *out = percent;
        return true;
    }
    return false;
}

/* ---- cursor ---- */

static const tbox_style_keyword tbox_style_cursors[] = {
    { "auto",        TBOX_STYLE_CURSOR_AUTO        },
    { "default",     TBOX_STYLE_CURSOR_DEFAULT     },
    { "pointer",     TBOX_STYLE_CURSOR_POINTER     },
    { "text",        TBOX_STYLE_CURSOR_TEXT        },
    { "vertical-text", TBOX_STYLE_CURSOR_TEXT      },
    { "move",        TBOX_STYLE_CURSOR_MOVE        },
    { "all-scroll",  TBOX_STYLE_CURSOR_MOVE        },
    { "wait",        TBOX_STYLE_CURSOR_WAIT        },
    { "progress",    TBOX_STYLE_CURSOR_PROGRESS    },
    { "help",        TBOX_STYLE_CURSOR_HELP        },
    { "crosshair",   TBOX_STYLE_CURSOR_CROSSHAIR   },
    { "cell",        TBOX_STYLE_CURSOR_CROSSHAIR   },
    { "not-allowed", TBOX_STYLE_CURSOR_NOT_ALLOWED },
    { "no-drop",     TBOX_STYLE_CURSOR_NOT_ALLOWED },
    { "grab",        TBOX_STYLE_CURSOR_GRAB        },
    { "grabbing",    TBOX_STYLE_CURSOR_GRABBING    },
    { "col-resize",  TBOX_STYLE_CURSOR_COL_RESIZE  },
    { "row-resize",  TBOX_STYLE_CURSOR_ROW_RESIZE  },
    { "ew-resize",   TBOX_STYLE_CURSOR_EW_RESIZE   },
    { "e-resize",    TBOX_STYLE_CURSOR_EW_RESIZE   },
    { "w-resize",    TBOX_STYLE_CURSOR_EW_RESIZE   },
    { "ns-resize",   TBOX_STYLE_CURSOR_NS_RESIZE   },
    { "n-resize",    TBOX_STYLE_CURSOR_NS_RESIZE   },
    { "s-resize",    TBOX_STYLE_CURSOR_NS_RESIZE   },
    { "none",        TBOX_STYLE_CURSOR_NONE        },
};

/* `cursor: [url(...) [x y],]* <keyword>`: url() entries are skipped, the
 * fallback keyword decides. */
static bool tbox_style_parse_cursor(tbox_string_view raw, tbox_style_cursor *out) {
    tbox_string_view parts[8];
    size_t count = tbox_style_split_top_level(raw, ',', parts, 8);
    int value;
    if (count == 0 || !tbox_style_lookup(parts[count - 1], tbox_style_cursors, sizeof(tbox_style_cursors) / sizeof(tbox_style_cursors[0]), &value))
        return false;
    *out = (tbox_style_cursor)value;
    return true;
}

/* `aspect-ratio: auto | <ratio> | auto && <ratio>`, a ratio being
 * `w / h` or a single number. The `auto &&` form behaves as the ratio. */
static bool tbox_style_parse_aspect_ratio(tbox_string_view raw, double *out) {
    tbox_string_view tokens[4];
    size_t count = tbox_style_tokens(raw, tokens, 4);
    if (count == 1 && tbox_style_is(tokens[0], "auto")) {
        *out = 0.0;
        return true;
    }
    size_t start = 0;
    if (count > 1 && tbox_style_is(tokens[0], "auto"))
        start = 1;
    else if (count > 1 && tbox_style_is(tokens[count - 1], "auto"))
        count--;
    double width, height = 1.0;
    if (count - start == 1) {
        if (!tbox_style_parse_number(tokens[start], &width))
            return false;
    } else if (count - start == 3 && tokens[start + 1].size == 1 && tokens[start + 1].data[0] == '/') {
        if (!tbox_style_parse_number(tokens[start], &width) || !tbox_style_parse_number(tokens[start + 2], &height))
            return false;
    } else {
        return false;
    }
    if (width <= 0.0 || height <= 0.0)
        return false;
    *out = width / height;
    return true;
}

/* white-space-collapse, text-wrap-mode and the text-wrap shorthand,
 * folded into the white_space mode (CSS Text 4 makes white-space their
 * shorthand; each competes with it by cascade priority). `balance`/
 * `pretty` wrap; balance also evens out line lengths. */
static void tbox_style_resolve_white_space_longhands(const tbox_css_computed_style *computed, const tbox_css_resolved_declaration *white_space, const tbox_style *parent_style, tbox_style *style) {
    style->text_wrap_balance = parent_style != NULL && parent_style->text_wrap_balance;
    tbox_style_white_space mode = style->white_space;
    /* Decompose: collapse 0 collapse, 1 preserve, 2 preserve-breaks,
     * 3 break-spaces; wrap true/false. */
    int collapse = mode == TBOX_STYLE_WHITE_SPACE_PRE || mode == TBOX_STYLE_WHITE_SPACE_PRE_WRAP ? 1 : mode == TBOX_STYLE_WHITE_SPACE_PRE_LINE ? 2 : mode == TBOX_STYLE_WHITE_SPACE_BREAK_SPACES ? 3 : 0;
    bool wrap    = !(mode == TBOX_STYLE_WHITE_SPACE_NOWRAP || mode == TBOX_STYLE_WHITE_SPACE_PRE);
    bool changed = false;
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("white-space-collapse"));
    if (tbox_style_border_longhand_wins(decl, white_space)) {
        static const tbox_style_keyword collapses[] = { { "collapse", 0 }, { "preserve", 1 }, { "preserve-breaks", 2 }, { "break-spaces", 3 }, { "preserve-spaces", 1 } };
        changed |= tbox_style_lookup(decl->value, collapses, TBOX_STYLE_COUNT(collapses), &collapse);
    }
    static const char *const wraps[2] = { "text-wrap", "text-wrap-mode" };
    for (int w = 0; w < 2; w++) {
        decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(wraps[w]));
        if (!tbox_style_border_longhand_wins(decl, white_space))
            continue;
        tbox_string_view tokens[2];
        size_t count = tbox_style_tokens(decl->value, tokens, 2);
        for (size_t i = 0; i < count; i++) {
            if (tbox_style_is(tokens[i], "nowrap"))
                wrap = false, changed = true;
            else if (tbox_style_is(tokens[i], "wrap") || tbox_style_is(tokens[i], "pretty") || tbox_style_is(tokens[i], "stable") || tbox_style_is(tokens[i], "auto"))
                wrap = true, changed = true, style->text_wrap_balance = w == 0 ? false : style->text_wrap_balance;
            else if (w == 0 && tbox_style_is(tokens[i], "balance"))
                wrap = true, changed = true, style->text_wrap_balance = true;
        }
    }
    if (!changed)
        return;
    static const tbox_style_white_space modes[4][2] = {
        { TBOX_STYLE_WHITE_SPACE_NOWRAP, TBOX_STYLE_WHITE_SPACE_NORMAL },
        { TBOX_STYLE_WHITE_SPACE_PRE, TBOX_STYLE_WHITE_SPACE_PRE_WRAP },
        { TBOX_STYLE_WHITE_SPACE_PRE_LINE, TBOX_STYLE_WHITE_SPACE_PRE_LINE }, /* no non-wrapping pre-line mode */
        { TBOX_STYLE_WHITE_SPACE_PRE, TBOX_STYLE_WHITE_SPACE_BREAK_SPACES },
    };
    style->white_space = modes[collapse][wrap ? 1 : 0];
}

/* ---- filter ---- */

static void tbox_style_matrix_identity(double m[20]) {
    memset(m, 0, 20 * sizeof(double));
    m[0] = m[6] = m[12] = m[18] = 1.0;
}

/* out = a * b (apply b first, then a). */
static void tbox_style_matrix_multiply(const double a[20], const double b[20], double out[20]) {
    double r[20];
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 5; col++) {
            double sum = col == 4 ? a[row * 5 + 4] : 0.0;
            for (int k = 0; k < 4; k++)
                sum += a[row * 5 + k] * b[k * 5 + col];
            r[row * 5 + col] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

/* An amount: a number or percentage, `default_amount` when omitted. */
static bool tbox_style_parse_amount(tbox_string_view args, double default_amount, double *out) {
    args = tbox_style_trim(args);
    if (args.size == 0) {
        *out = default_amount;
        return true;
    }
    bool percent = args.data[args.size - 1] == '%';
    double value;
    if (!tbox_style_parse_number(percent ? tbox_string_view_make(args.data, args.size - 1) : args, &value) || value < 0.0)
        return false;
    *out = percent ? value / 100.0 : value;
    return true;
}

/* filter: none | <function>+ -- the Filter Effects 1 matrices, composed
 * left to right. Unsupported functions (blur, drop-shadow, url) are
 * skipped; anything unknown invalidates the value. */
static bool tbox_style_parse_filter(tbox_string_view raw, bool *out_has, double out[20]) {
    if (tbox_style_is(raw, "none")) {
        *out_has = false;
        tbox_style_matrix_identity(out);
        return true;
    }
    tbox_string_view functions[12];
    size_t count = tbox_style_tokens(raw, functions, 12);
    if (count == 0)
        return false;
    double total[20];
    tbox_style_matrix_identity(total);
    bool any = false;
    for (size_t f = 0; f < count; f++) {
        tbox_string_view args;
        double a, m[20];
        tbox_style_matrix_identity(m);
        if (tbox_style_function_args(functions[f], "grayscale", &args) || tbox_style_function_args(functions[f], "sepia", &args) || tbox_style_function_args(functions[f], "saturate", &args)) {
            bool saturate = tbox_string_view_equal_ascii_ci(tbox_string_view_make(functions[f].data, 3), tbox_string_view_from_cstr("sat"));
            bool sepia    = !saturate && tbox_string_view_equal_ascii_ci(tbox_string_view_make(functions[f].data, 3), tbox_string_view_from_cstr("sep"));
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            if (saturate) {
                const double v[9] = { 0.213 + 0.787 * a, 0.715 - 0.715 * a, 0.072 - 0.072 * a, 0.213 - 0.213 * a, 0.715 + 0.285 * a, 0.072 - 0.072 * a, 0.213 - 0.213 * a, 0.715 - 0.715 * a, 0.072 + 0.928 * a };
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        m[r * 5 + c] = v[r * 3 + c];
            } else {
                if (a > 1.0)
                    a = 1.0;
                const double g[9] = { 0.2126 + 0.7874 * (1 - a), 0.7152 - 0.7152 * (1 - a), 0.0722 - 0.0722 * (1 - a), 0.2126 - 0.2126 * (1 - a), 0.7152 + 0.2848 * (1 - a), 0.0722 - 0.0722 * (1 - a), 0.2126 - 0.2126 * (1 - a), 0.7152 - 0.7152 * (1 - a), 0.0722 + 0.9278 * (1 - a) };
                const double p[9] = { 0.393 + 0.607 * (1 - a), 0.769 - 0.769 * (1 - a), 0.189 - 0.189 * (1 - a), 0.349 - 0.349 * (1 - a), 0.686 + 0.314 * (1 - a), 0.168 - 0.168 * (1 - a), 0.272 - 0.272 * (1 - a), 0.534 - 0.534 * (1 - a), 0.131 + 0.869 * (1 - a) };
                const double *v   = sepia ? p : g;
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        m[r * 5 + c] = v[r * 3 + c];
            }
        } else if (tbox_style_function_args(functions[f], "hue-rotate", &args)) {
            double degrees = 0.0;
            if (tbox_style_trim(args).size > 0 && !tbox_style_parse_angle(args, &degrees))
                return false;
            double c = cos(degrees * 3.141592653589793 / 180.0), sn = sin(degrees * 3.141592653589793 / 180.0);
            const double v[9] = { 0.213 + c * 0.787 - sn * 0.213, 0.715 - c * 0.715 - sn * 0.715, 0.072 - c * 0.072 + sn * 0.928, 0.213 - c * 0.213 + sn * 0.143, 0.715 + c * 0.285 + sn * 0.140, 0.072 - c * 0.072 - sn * 0.283, 0.213 - c * 0.213 - sn * 0.787, 0.715 - c * 0.715 + sn * 0.715, 0.072 + c * 0.928 + sn * 0.072 };
            for (int r = 0; r < 3; r++)
                for (int col = 0; col < 3; col++)
                    m[r * 5 + col] = v[r * 3 + col];
        } else if (tbox_style_function_args(functions[f], "invert", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            if (a > 1.0)
                a = 1.0;
            for (int r = 0; r < 3; r++) {
                m[r * 5 + r] = 1.0 - 2.0 * a;
                m[r * 5 + 4] = a;
            }
        } else if (tbox_style_function_args(functions[f], "opacity", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            m[18] = a > 1.0 ? 1.0 : a;
        } else if (tbox_style_function_args(functions[f], "brightness", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            m[0] = m[6] = m[12] = a;
        } else if (tbox_style_function_args(functions[f], "contrast", &args)) {
            if (!tbox_style_parse_amount(args, 1.0, &a))
                return false;
            for (int r = 0; r < 3; r++) {
                m[r * 5 + r] = a;
                m[r * 5 + 4] = 0.5 - 0.5 * a;
            }
        } else if (tbox_style_function_args(functions[f], "blur", &args) || tbox_style_function_args(functions[f], "drop-shadow", &args) || tbox_style_function_args(functions[f], "url", &args)) {
            continue; /* not supported: skipped */
        } else {
            return false;
        }
        tbox_style_matrix_multiply(m, total, total);
        any = true;
    }
    *out_has = any;
    memcpy(out, total, sizeof(total));
    return true;
}

/* ---- clip-path ---- */

/* `closest-side`/`farthest-side` or a length-percentage for a shape
 * radius; closest-side is AUTO, farthest-side is stored as a negative
 * percentage (-1) for Render. */
static bool tbox_style_parse_shape_radius(tbox_string_view token, const tbox_style_units *units, tbox_style_length *out) {
    if (tbox_style_is(token, "closest-side")) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    if (tbox_style_is(token, "farthest-side")) {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, -1.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_spacing_length(token, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO && out->value >= 0.0;
}

static bool tbox_style_parse_clip_path(tbox_string_view raw, const tbox_style_units *units, tbox_style_clip_path *out) {
    tbox_style_clip_path clip;
    memset(&clip, 0, sizeof(clip));
    clip.center[0] = clip.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    if (tbox_style_is(raw, "none")) {
        *out = clip;
        return true;
    }
    tbox_string_view tokens[2];
    if (tbox_style_tokens(raw, tokens, 2) != 1)
        return false; /* a reference box after the shape is not supported */
    tbox_string_view args, parts[12];
    size_t count;
    if (tbox_style_function_args(tokens[0], "inset", &args)) {
        clip.kind = TBOX_STYLE_CLIP_PATH_INSET;
        count     = tbox_style_tokens(args, parts, 12);
        size_t round = count;
        for (size_t i = 0; i < count; i++)
            if (tbox_style_is(parts[i], "round"))
                round = i;
        tbox_style_length lengths[4];
        if (round < 1 || round > 4)
            return false;
        for (size_t i = 0; i < round; i++)
            if (!tbox_style_parse_spacing_length(parts[i], units, &lengths[i]) || lengths[i].kind == TBOX_STYLE_LENGTH_AUTO)
                return false;
        for (size_t side = 0; side < 4; side++)
            clip.inset[side] = lengths[tbox_style_four_value_index(round, side)];
        if (round < count) {
            size_t slash = count;
            for (size_t i = round + 1; i < count; i++)
                if (parts[i].size == 1 && parts[i].data[0] == '/')
                    slash = i;
            double hp[4], vp[4];
            if (!tbox_style_parse_radius_list(parts + round + 1, slash - round - 1, units, clip.round_h, hp))
                return false;
            if (slash < count) {
                if (!tbox_style_parse_radius_list(parts + slash + 1, count - slash - 1, units, clip.round_v, vp))
                    return false;
            } else {
                memcpy(clip.round_v, clip.round_h, sizeof(clip.round_v));
            }
        }
    } else if (tbox_style_function_args(tokens[0], "circle", &args) || tbox_style_function_args(tokens[0], "ellipse", &args)) {
        bool circle = tolower((unsigned char)tokens[0].data[0]) == 'c';
        clip.kind   = circle ? TBOX_STYLE_CLIP_PATH_CIRCLE : TBOX_STYLE_CLIP_PATH_ELLIPSE;
        count       = tbox_style_tokens(args, parts, 12);
        size_t at   = count;
        for (size_t i = 0; i < count; i++)
            if (tbox_style_is(parts[i], "at"))
                at = i;
        size_t radii = circle ? 1 : 2;
        if (at != 0 && at != radii)
            return false;
        for (size_t i = 0; i < at; i++)
            if (!tbox_style_parse_shape_radius(parts[i], units, &clip.radius[i]))
                return false;
        if (at < count && !tbox_style_parse_position(parts + at + 1, count - at - 1, units, clip.center))
            return false;
    } else {
        return false;
    }
    *out = clip;
    return true;
}

static const tbox_style_keyword tbox_style_overflows[] = {
    { "visible", TBOX_STYLE_OVERFLOW_Y_VISIBLE },
    { "auto",    TBOX_STYLE_OVERFLOW_Y_AUTO    },
    { "scroll",  TBOX_STYLE_OVERFLOW_Y_AUTO    },
    { "hidden",  TBOX_STYLE_OVERFLOW_Y_HIDDEN  },
    { "clip",    TBOX_STYLE_OVERFLOW_Y_HIDDEN  },
};

static const tbox_style_keyword tbox_style_text_align_lasts[] = {
    { "auto",    TBOX_STYLE_TEXT_ALIGN_LAST_AUTO    },
    { "left",    TBOX_STYLE_TEXT_ALIGN_LAST_LEFT    },
    { "start",   TBOX_STYLE_TEXT_ALIGN_LAST_LEFT    },
    { "center",  TBOX_STYLE_TEXT_ALIGN_LAST_CENTER  },
    { "right",   TBOX_STYLE_TEXT_ALIGN_LAST_RIGHT   },
    { "end",     TBOX_STYLE_TEXT_ALIGN_LAST_RIGHT   },
    { "justify", TBOX_STYLE_TEXT_ALIGN_LAST_JUSTIFY },
};

static const tbox_style_keyword tbox_style_user_selects[] = {
    { "auto", TBOX_STYLE_USER_SELECT_AUTO },
    { "none", TBOX_STYLE_USER_SELECT_NONE },
    { "text", TBOX_STYLE_USER_SELECT_TEXT },
    { "all",  TBOX_STYLE_USER_SELECT_ALL  },
    { "contain", TBOX_STYLE_USER_SELECT_TEXT },
};

/* Properties with no interplay with the rest of tbox_style_resolve:
 * tab-size, list-style-position, user-select, cursor, aspect-ratio,
 * line-clamp, z-index, text-align-last, empty-cells, table-layout and the
 * translate transforms. */
static void tbox_style_resolve_extras(const tbox_css_computed_style *computed, const tbox_style_units *units, const tbox_style *parent_style, tbox_style *style) {
    const tbox_css_resolved_declaration *decl;
    int value;

    /* tab-size: inheritable, a nonnegative number of spaces or a length. */
    style->tab_size        = parent_style != NULL && parent_style->tab_size > 0.0 ? parent_style->tab_size : 8.0;
    style->tab_size_length = parent_style != NULL && parent_style->tab_size > 0.0 && parent_style->tab_size_length;
    decl                   = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("tab-size"));
    if (decl != NULL) {
        double number;
        tbox_style_length length;
        if (tbox_style_parse_number(tbox_style_trim(decl->value), &number) && number >= 0.0)
            style->tab_size = number, style->tab_size_length = false;
        else if (tbox_style_parse_spacing_length(decl->value, units, &length) && length.kind == TBOX_STYLE_LENGTH_PX && length.value >= 0.0)
            style->tab_size = length.value, style->tab_size_length = true;
    }

    /* list-style-position, also from the list-style shorthand (which resets
     * it to outside when omitted). Inheritable. */
    style->list_style_inside                       = parent_style != NULL && parent_style->list_style_inside;
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("list-style"));
    if (shorthand != NULL) {
        tbox_string_view tokens[4];
        size_t count             = tbox_style_tokens(shorthand->value, tokens, 4);
        style->list_style_inside = false;
        for (size_t i = 0; i < count; i++)
            if (tbox_style_is(tokens[i], "inside"))
                style->list_style_inside = true;
    }
    /* list-style-image (and a url() in the list-style shorthand). */
    if (parent_style != NULL)
        memcpy(style->list_style_image, parent_style->list_style_image, sizeof(style->list_style_image));
    if (shorthand != NULL) {
        tbox_string_view tokens[4];
        size_t count               = tbox_style_tokens(shorthand->value, tokens, 4);
        style->list_style_image[0] = '\0';
        for (size_t i = 0; i < count; i++)
            tbox_style_parse_url(tokens[i], style->list_style_image, sizeof(style->list_style_image));
    }
    const tbox_css_resolved_declaration *image_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("list-style-image"));
    if (tbox_style_border_longhand_wins(image_decl, shorthand)) {
        char url[256];
        if (tbox_style_is(image_decl->value, "none"))
            style->list_style_image[0] = '\0';
        else if (tbox_style_parse_url(tbox_style_trim(image_decl->value), url, sizeof(url)))
            memcpy(style->list_style_image, url, sizeof(url));
    }
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("list-style-position"));
    if (tbox_style_border_longhand_wins(decl, shorthand)) {
        if (tbox_style_is(decl->value, "inside"))
            style->list_style_inside = true;
        else if (tbox_style_is(decl->value, "outside"))
            style->list_style_inside = false;
    }

    /* user-select: `auto` takes the parent's none/all, as CSS's used
     * value does; not inherited otherwise. */
    style->user_select = parent_style != NULL && (parent_style->user_select == TBOX_STYLE_USER_SELECT_NONE || parent_style->user_select == TBOX_STYLE_USER_SELECT_ALL) ? parent_style->user_select : TBOX_STYLE_USER_SELECT_AUTO;
    decl               = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("user-select"));
    if (decl == NULL)
        decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("-webkit-user-select"));
    if (decl != NULL && tbox_style_lookup(decl->value, tbox_style_user_selects, TBOX_STYLE_COUNT(tbox_style_user_selects), &value))
        style->user_select = (tbox_style_user_select)value;

    /* scrollbar-color: auto | <thumb> <track> (inherited);
     * scrollbar-width: auto | thin | none. */
    style->scrollbar_thumb_color = parent_style != NULL ? parent_style->scrollbar_thumb_color : (tbox_css_rgba){ 0, 0, 0, 0 };
    style->scrollbar_track_color = parent_style != NULL ? parent_style->scrollbar_track_color : (tbox_css_rgba){ 0, 0, 0, 0 };
    decl                         = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("scrollbar-color"));
    if (decl != NULL) {
        tbox_string_view tokens[3];
        size_t count = tbox_style_tokens(decl->value, tokens, 3);
        tbox_css_rgba thumb, track;
        if (count == 1 && tbox_style_is(tokens[0], "auto")) {
            style->scrollbar_thumb_color = style->scrollbar_track_color = (tbox_css_rgba){ 0, 0, 0, 0 };
        } else if (count == 2 && tbox_style_parse_edge_color(tokens[0], style->color, &thumb) && tbox_style_parse_edge_color(tokens[1], style->color, &track)) {
            style->scrollbar_thumb_color = thumb;
            style->scrollbar_track_color = track;
        }
    }
    style->scrollbar_width = TBOX_STYLE_SCROLLBAR_WIDTH_AUTO;
    decl                   = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("scrollbar-width"));
    if (decl != NULL) {
        if (tbox_style_is(decl->value, "thin")) style->scrollbar_width = TBOX_STYLE_SCROLLBAR_WIDTH_THIN;
        else if (tbox_style_is(decl->value, "none")) style->scrollbar_width = TBOX_STYLE_SCROLLBAR_WIDTH_NONE;
    }

    style->cursor = parent_style != NULL ? parent_style->cursor : TBOX_STYLE_CURSOR_AUTO;
    decl          = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("cursor"));
    if (decl != NULL)
        tbox_style_parse_cursor(decl->value, &style->cursor);

    style->aspect_ratio = 0.0;
    decl                = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("aspect-ratio"));
    if (decl != NULL)
        tbox_style_parse_aspect_ratio(decl->value, &style->aspect_ratio);

    /* line-clamp / -webkit-line-clamp: `none` or a positive integer. The
     * -webkit- form normally needs `display: -webkit-box` too; that
     * display is accepted (as block) for it. */
    style->line_clamp = 0;
    static const char *const clamps[2] = { "-webkit-line-clamp", "line-clamp" };
    for (int i = 0; i < 2; i++) {
        decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(clamps[i]));
        int lines;
        if (decl == NULL)
            continue;
        if (tbox_style_is(decl->value, "none"))
            style->line_clamp = 0;
        else if (tbox_style_parse_integer(decl->value, &lines) && lines > 0)
            style->line_clamp = lines;
    }

    style->z_index      = 0;
    style->z_index_auto = true;
    decl                = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("z-index"));
    if (decl != NULL) {
        int z;
        if (tbox_style_parse_integer(decl->value, &z))
            style->z_index = z, style->z_index_auto = false;
    }

    style->text_align_last = parent_style != NULL ? parent_style->text_align_last : TBOX_STYLE_TEXT_ALIGN_LAST_AUTO;
    decl                   = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-align-last"));
    if (decl != NULL && tbox_style_lookup(decl->value, tbox_style_text_align_lasts, TBOX_STYLE_COUNT(tbox_style_text_align_lasts), &value))
        style->text_align_last = (tbox_style_text_align_last)value;

    style->empty_cells_hide = parent_style != NULL && parent_style->empty_cells_hide;
    decl                    = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("empty-cells"));
    if (decl != NULL) {
        if (tbox_style_is(decl->value, "hide"))
            style->empty_cells_hide = true;
        else if (tbox_style_is(decl->value, "show"))
            style->empty_cells_hide = false;
    }
    decl                      = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("table-layout"));
    style->table_layout_fixed = decl != NULL && tbox_style_is(decl->value, "fixed");

    /* transform and translate add up, as CSS applies both. */
    style->translate_x = style->translate_y = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
    decl                                    = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("transform"));
    if (decl != NULL)
        tbox_style_parse_transform(decl->value, units, &style->translate_x, &style->translate_y);
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("translate"));
    tbox_style_length x, y;
    if (decl != NULL && tbox_style_parse_translate(decl->value, units, &x, &y)) {
        tbox_style_length *targets[2] = { &style->translate_x, &style->translate_y };
        tbox_style_length values[2]   = { x, y };
        for (int i = 0; i < 2; i++) {
            tbox_style_length *t = targets[i];
            tbox_style_length v  = values[i];
            if (v.kind == TBOX_STYLE_LENGTH_PX)
                t->kind == TBOX_STYLE_LENGTH_PX ? (void)(t->value += v.value) : (void)(t->px_offset += v.value);
            else if (t->kind == TBOX_STYLE_LENGTH_PX)
                *t = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, v.value, v.px_offset + t->value, 0, 0.0, 0.0 };
            else
                t->value += v.value, t->px_offset += v.px_offset;
        }
    }
}

tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed) {
    return tbox_style_resolve_in_viewport(node, parent_style, computed, 0.0, 0.0);
}

/* The declared (non-keyword) values; tbox_style_resolve_in_viewport adds
 * the CSS-wide keywords on top. */
static tbox_style tbox_style_resolve_declared(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height);

static tbox_style tbox_style_resolve_keywords(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height);

/* var() substitution first (custom properties come from the cascade like
 * any declaration), then CSS-wide keywords, then the declared values. */
static tbox_style tbox_style_resolve_full(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height, tbox_arena *arena) {
    const tbox_style_custom_properties *inherited = parent_style != NULL ? parent_style->custom_properties : NULL;
    if (!tbox_style_vars_present(computed)) {
        tbox_style style        = tbox_style_resolve_keywords(node, parent_style, computed, viewport_width, viewport_height);
        style.custom_properties = inherited;
        return style;
    }
    tbox_arena local;
    bool own_arena = arena == NULL;
    if (own_arena) {
        local = tbox_arena_create(0);
        arena = &local;
    }
    const tbox_style_custom_properties *own;
    tbox_css_computed_style substituted = tbox_style_vars_substitute(computed, inherited, arena, &own, !own_arena);
    tbox_style style                    = tbox_style_resolve_keywords(node, parent_style, &substituted, viewport_width, viewport_height);
    style.custom_properties             = own_arena ? inherited : own;
    if (own_arena)
        tbox_arena_destroy(&local);
    return style;
}

tbox_style tbox_style_resolve_in_viewport(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height) {
    return tbox_style_resolve_full(node, parent_style, computed, viewport_width, viewport_height, NULL);
}

static tbox_style tbox_style_resolve_keywords(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height) {
    if (!tbox_style_keywords_present(computed))
        return tbox_style_resolve_declared(node, parent_style, computed, viewport_width, viewport_height);
    tbox_css_computed_style stripped = tbox_style_keywords_strip(computed);
    tbox_style style                 = tbox_style_resolve_declared(node, parent_style, &stripped, viewport_width, viewport_height);
    tbox_css_computed_style empty    = { NULL, 0, NULL };
    tbox_style initial               = tbox_style_resolve_declared(NULL, NULL, &empty, viewport_width, viewport_height);
    /* CSS initial values the resolved "nothing declared" style doesn't
     * carry: border widths are `medium` (stored as 0 while the style is
     * none), and border/outline/decoration colors are currentColor. */
    for (size_t i = 0; i < 4; i++) {
        initial.border_widths[i] = 3.0;
        initial.border_colors[i] = style.color;
    }
    initial.border_width          = 3.0;
    initial.border_color          = style.color;
    initial.outline_color         = style.color;
    initial.text_decoration_color = style.color;
    tbox_style_keywords_apply(&style, parent_style, &initial, computed);
    tbox_style_keywords_release(&stripped);
    return style;
}

static tbox_style tbox_style_resolve_declared(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed, double viewport_width, double viewport_height) {
    /* `node` is used below by width/height's image HTML-attribute
     * fallback (tbox_style_resolve_img_dimension_attribute) -- every other
     * property here is still a pure function of `computed`/`parent_style`,
     * unchanged from v0's original "kept in the signature for per-tag
     * defaults later" stance (see ARCHITECTURE.md). */

    /* Zeroed first, so every field this function doesn't set explicitly
     * has a defined (zero) initial value. */
    tbox_style style;
    memset(&style, 0, sizeof(style));

    /* display: v0's initial value is BLOCK, not CSS2.1's spec-correct
     * `inline` -- see the comment on tbox_style.display in style.h. */
    style.display           = TBOX_STYLE_DISPLAY_BLOCK;
    style.overflow_y        = TBOX_STYLE_OVERFLOW_Y_VISIBLE;
    style.box_sizing        = TBOX_STYLE_BOX_SIZING_CONTENT_BOX;
    style.visibility_hidden = parent_style != NULL && parent_style->visibility_hidden;
    style.visibility_collapse = parent_style != NULL && parent_style->visibility_collapse;
    style.text_overflow     = TBOX_STYLE_TEXT_OVERFLOW_CLIP;
    style.background_clip   = TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX;
    style.object_fit        = TBOX_STYLE_OBJECT_FIT_FILL;
    style.image_rendering_pixelated = parent_style != NULL && parent_style->image_rendering_pixelated;

    const tbox_css_resolved_declaration *image_rendering = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("image-rendering"));
    if (image_rendering != NULL) {
        tbox_string_view value = tbox_style_trim(image_rendering->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("pixelated")) ||
            tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("crisp-edges"))) style.image_rendering_pixelated = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto"))) style.image_rendering_pixelated = false;
    }

    const tbox_css_resolved_declaration *object_fit = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("object-fit"));
    if (object_fit != NULL) {
        tbox_string_view value = tbox_style_trim(object_fit->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("contain"))) style.object_fit = TBOX_STYLE_OBJECT_FIT_CONTAIN;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("cover"))) style.object_fit = TBOX_STYLE_OBJECT_FIT_COVER;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none"))) style.object_fit = TBOX_STYLE_OBJECT_FIT_NONE;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("scale-down"))) style.object_fit = TBOX_STYLE_OBJECT_FIT_SCALE_DOWN;
    }

    const tbox_css_resolved_declaration *sizing_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("box-sizing"));
    if (sizing_decl != NULL && tbox_string_view_equal_ascii_ci(tbox_style_trim(sizing_decl->value), tbox_string_view_from_cstr("border-box")))
        style.box_sizing = TBOX_STYLE_BOX_SIZING_BORDER_BOX;
    const tbox_css_resolved_declaration *visibility_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("visibility"));
    if (visibility_decl != NULL) {
        tbox_string_view value = tbox_style_trim(visibility_decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("hidden")))
            style.visibility_hidden = true, style.visibility_collapse = false;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("collapse")))
            style.visibility_hidden = true, style.visibility_collapse = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("visible")))
            style.visibility_hidden = false, style.visibility_collapse = false;
    }
    /* text-overflow: clip | ellipsis | "<string>"; of a two-value form
     * (left and right ends) only the end one applies, text being LTR. */
    const tbox_css_resolved_declaration *text_overflow_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-overflow"));
    if (text_overflow_decl != NULL) {
        tbox_string_view tokens[2];
        size_t count = tbox_style_tokens(text_overflow_decl->value, tokens, 2);
        if (count > 0) {
            tbox_string_view end = tokens[count - 1];
            if (tbox_style_is(end, "ellipsis")) {
                style.text_overflow = TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS;
            } else if (end.size >= 2 && (end.data[0] == '"' || end.data[0] == '\'') && end.data[end.size - 1] == end.data[0]) {
                size_t n = end.size - 2 < sizeof(style.text_overflow_string) - 1 ? end.size - 2 : sizeof(style.text_overflow_string) - 1;
                memcpy(style.text_overflow_string, end.data + 1, n);
                style.text_overflow_string[n] = '\0';
                style.text_overflow           = n > 0 ? TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS : TBOX_STYLE_TEXT_OVERFLOW_CLIP;
            }
        }
    }

    /* `overflow` takes one value for both axes or `x y`; `overflow-x`/
     * `overflow-y` compete with it by cascade priority. `scroll` behaves
     * as `auto` (no always-visible scrollbar), and `clip` as `hidden`
     * (content is not programmatically scrollable in either case here). */
    style.overflow_x                                    = TBOX_STYLE_OVERFLOW_Y_VISIBLE;
    const tbox_css_resolved_declaration *overflow_short = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("overflow"));
    if (overflow_short != NULL) {
        tbox_string_view tokens[3];
        size_t count = tbox_style_tokens(overflow_short->value, tokens, 3);
        int x, y;
        if ((count == 1 || count == 2) && tbox_style_lookup(tokens[0], tbox_style_overflows, TBOX_STYLE_COUNT(tbox_style_overflows), &x) &&
            tbox_style_lookup(tokens[count - 1], tbox_style_overflows, TBOX_STYLE_COUNT(tbox_style_overflows), &y)) {
            style.overflow_x = (tbox_style_overflow)x;
            style.overflow_y = (tbox_style_overflow_y)y;
        } else {
            overflow_short = NULL;
        }
    }
    static const char *const overflow_axes[2] = { "overflow-x", "overflow-y" };
    for (int axis = 0; axis < 2; axis++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(overflow_axes[axis]));
        int value;
        if (tbox_style_border_longhand_wins(decl, overflow_short) && tbox_style_lookup(decl->value, tbox_style_overflows, TBOX_STYLE_COUNT(tbox_style_overflows), &value)) {
            if (axis == 0)
                style.overflow_x = (tbox_style_overflow)value;
            else
                style.overflow_y = (tbox_style_overflow_y)value;
        }
    }
    /* CSS: `visible` paired with a non-visible value on the other axis
     * computes to `auto`. */
    if (style.overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE && style.overflow_y == TBOX_STYLE_OVERFLOW_Y_VISIBLE)
        style.overflow_y = TBOX_STYLE_OVERFLOW_Y_AUTO;
    else if (style.overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE && style.overflow_x == TBOX_STYLE_OVERFLOW_Y_VISIBLE)
        style.overflow_x = TBOX_STYLE_OVERFLOW_Y_AUTO;
    style.white_space                                = parent_style != NULL ? parent_style->white_space : TBOX_STYLE_WHITE_SPACE_AUTO;
    const tbox_css_resolved_declaration *white_space = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("white-space"));
    if (white_space != NULL) {
        static const struct {
            const char *name;
            tbox_style_white_space value;
        } modes[] = {
            { "normal",   TBOX_STYLE_WHITE_SPACE_NORMAL   },
            { "nowrap",   TBOX_STYLE_WHITE_SPACE_NOWRAP   },
            { "pre",      TBOX_STYLE_WHITE_SPACE_PRE      },
            { "pre-wrap", TBOX_STYLE_WHITE_SPACE_PRE_WRAP },
            { "pre-line", TBOX_STYLE_WHITE_SPACE_PRE_LINE },
            { "break-spaces", TBOX_STYLE_WHITE_SPACE_BREAK_SPACES },
        };
        tbox_string_view value = tbox_style_trim(white_space->value);
        for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(modes[i].name)))
                style.white_space = modes[i].value;
    }
    tbox_style_resolve_white_space_longhands(computed, white_space, parent_style, &style);
    style.overflow_wrap_break_word                     = parent_style != NULL && parent_style->overflow_wrap_break_word;
    style.overflow_wrap_anywhere                       = parent_style != NULL && parent_style->overflow_wrap_anywhere;
    const tbox_css_resolved_declaration *overflow_wrap = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("overflow-wrap"));
    if (overflow_wrap != NULL) {
        tbox_string_view value = tbox_style_trim(overflow_wrap->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("anywhere"))) {
            style.overflow_wrap_break_word = true;
            style.overflow_wrap_anywhere = true;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("break-word"))) {
            style.overflow_wrap_break_word = true;
            style.overflow_wrap_anywhere = false;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal"))) {
            style.overflow_wrap_break_word = false;
            style.overflow_wrap_anywhere = false;
        }
    }
    style.word_break_all                            = parent_style != NULL && parent_style->word_break_all;
    style.word_break_keep_all                       = parent_style != NULL && parent_style->word_break_keep_all;
    style.hyphens_none                              = parent_style != NULL && parent_style->hyphens_none;
    const tbox_css_resolved_declaration *hyphens    = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("hyphens"));
    if (hyphens == NULL)
        hyphens = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("-webkit-hyphens"));
    if (hyphens != NULL) {
        if (tbox_style_is(hyphens->value, "none"))
            style.hyphens_none = true;
        else if (tbox_style_is(hyphens->value, "manual") || tbox_style_is(hyphens->value, "auto"))
            style.hyphens_none = false;
    }
    const tbox_css_resolved_declaration *word_break = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("word-break"));
    if (word_break != NULL) {
        tbox_string_view value = tbox_style_trim(word_break->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("break-all")))
            style.word_break_all = true, style.word_break_keep_all = false;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("keep-all")))
            style.word_break_all = false, style.word_break_keep_all = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")) || tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("break-word")))
            style.word_break_all = false, style.word_break_keep_all = false;
    }
    style.text_transform                                = parent_style != NULL ? parent_style->text_transform : TBOX_STYLE_TEXT_TRANSFORM_NONE;
    const tbox_css_resolved_declaration *text_transform = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-transform"));
    if (text_transform != NULL) {
        static const struct {
            const char *name;
            tbox_style_text_transform value;
        } transforms[] = {
            { "none",       TBOX_STYLE_TEXT_TRANSFORM_NONE       },
            { "uppercase",  TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE  },
            { "lowercase",  TBOX_STYLE_TEXT_TRANSFORM_LOWERCASE  },
            { "capitalize", TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE },
        };
        tbox_string_view value = tbox_style_trim(text_transform->value);
        for (size_t i = 0; i < sizeof(transforms) / sizeof(transforms[0]); i++)
            if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(transforms[i].name)))
                style.text_transform = transforms[i].value;
    }
    style.list_style_type                               = tbox_style_resolve_list_style_type(computed, parent_style != NULL ? parent_style->list_style_type : TBOX_STYLE_LIST_STYLE_AUTO);
    style.pointer_events_none                           = parent_style != NULL && parent_style->pointer_events_none;
    const tbox_css_resolved_declaration *pointer_events = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("pointer-events"));
    if (pointer_events != NULL) {
        tbox_string_view value = tbox_style_trim(pointer_events->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none")))
            style.pointer_events_none = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto")))
            style.pointer_events_none = false;
    }
    const tbox_css_resolved_declaration *display_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("display"));
    if (display_decl != NULL) {
        tbox_style_display parsed;
        if (tbox_style_parse_display(display_decl->value, &parsed)) {
            style.display           = parsed;
            style.display_list_item = tbox_style_is(display_decl->value, "list-item");
        }
    }

    /* font-size: . Inheritable through the parent's already-resolved
     * value (not re-parsed); falls back to the CSS2.1-ish 16px initial
     * value with no parent -- same default already used by Fonte/Texto
     * since v0. MOVIDO v8: precisa ser calculado antes de
     * width/height/margin/padding/offset, já que `em` nessas propriedades
     *  resolve contra este mesmo `style.font_size`. */
    double parent_font_size        = (parent_style != NULL) ? parent_style->font_size : 16.0;
    tbox_style_font_shorthand font = tbox_style_parse_font_shorthand(computed);
    tbox_string_view font_storage;
    double root_font_size          = parent_style != NULL ? parent_style->root_font_size : 16.0;
    tbox_style_units units         = { parent_font_size, root_font_size, viewport_width, viewport_height };
    style.font_size                = tbox_style_resolve_font_size(tbox_style_font_value(computed, &font, "font-size", &font_storage), parent_font_size, &units);
    style.root_font_size           = parent_style != NULL ? parent_style->root_font_size : style.font_size;
    units.font_size                = style.font_size;
    style.object_position[0] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    style.object_position[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *object_position = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("object-position"));
    if (object_position != NULL)
        tbox_style_parse_object_position(object_position->value, &units, style.object_position);
    style.line_height_kind                    = parent_style != NULL ? parent_style->line_height_kind : TBOX_STYLE_LINE_HEIGHT_NORMAL;
    style.line_height_value                   = parent_style != NULL ? parent_style->line_height_value : 0.0;
    const tbox_string_view *line_height_value = tbox_style_font_value(computed, &font, "line-height", &font_storage);
    if (line_height_value != NULL) {
        tbox_string_view value = tbox_style_trim(*line_height_value);
        double multiplier;
        tbox_style_length length = { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
        bool has_length          = tbox_style_parse_spacing_length(value, &units, &length);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal"))) {
            style.line_height_kind  = TBOX_STYLE_LINE_HEIGHT_NORMAL;
            style.line_height_value = 0.0;
        } else if (tbox_style_parse_number(value, &multiplier) && multiplier >= 0.0) {
            style.line_height_kind  = TBOX_STYLE_LINE_HEIGHT_NUMBER;
            style.line_height_value = multiplier;
        } else if (has_length && length.kind == TBOX_STYLE_LENGTH_PX && length.value >= 0.0) {
            style.line_height_kind  = TBOX_STYLE_LINE_HEIGHT_PX;
            style.line_height_value = length.value;
        } else if (has_length && length.kind == TBOX_STYLE_LENGTH_PERCENT && length.value >= 0.0) {
            style.line_height_kind  = TBOX_STYLE_LINE_HEIGHT_PX;
            style.line_height_value = style.font_size * length.value / 100.0;
        }
    }
    style.letter_spacing                                     = parent_style != NULL ? parent_style->letter_spacing : 0.0;
    const tbox_css_resolved_declaration *letter_spacing_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("letter-spacing"));
    if (letter_spacing_decl != NULL) {
        tbox_string_view value = tbox_style_trim(letter_spacing_decl->value);
        tbox_style_length length;
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")))
            style.letter_spacing = 0.0;
        else if (tbox_style_parse_spacing_length(value, &units, &length) && length.kind == TBOX_STYLE_LENGTH_PX)
            style.letter_spacing = length.value;
    }

    style.width      = tbox_style_resolve_logical_size(computed, "width", "inline-size", &units, false, &style.width_keyword);
    style.height     = tbox_style_resolve_logical_size(computed, "height", "block-size", &units, false, &style.height_keyword);
    style.min_width  = tbox_style_resolve_logical_size(computed, "min-width", "min-inline-size", &units, true, NULL);
    style.max_width  = tbox_style_resolve_logical_size(computed, "max-width", "max-inline-size", &units, true, NULL);
    style.min_height = tbox_style_resolve_logical_size(computed, "min-height", "min-block-size", &units, true, NULL);
    style.max_height = tbox_style_resolve_logical_size(computed, "max-height", "max-block-size", &units, true, NULL);
    if (style.width.kind == TBOX_STYLE_LENGTH_AUTO) {
        style.width = tbox_style_resolve_img_dimension_attribute(node, "width");
    }
    if (style.height.kind == TBOX_STYLE_LENGTH_AUTO) {
        style.height = tbox_style_resolve_img_dimension_attribute(node, "height");
    }

    for (int i = 0; i < 4; i++) {
        style.margin[i].kind   = TBOX_STYLE_LENGTH_PX;
        style.margin[i].value  = 0.0;
        style.padding[i].kind  = TBOX_STYLE_LENGTH_PX;
        style.padding[i].value = 0.0;
    }
    static const char *const margin_sides[4]  = { "margin-top", "margin-right", "margin-bottom", "margin-left" };
    static const char *const padding_sides[4] = { "padding-top", "padding-right", "padding-bottom", "padding-left" };
    tbox_style_resolve_box_edges(computed, "margin", margin_sides, &units, true, style.margin);
    tbox_style_resolve_box_edges(computed, "padding", padding_sides, &units, false, style.padding);
    style.text_indent                                = parent_style != NULL ? parent_style->text_indent : (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
    /* text-indent: <length-percentage> && hanging? && each-line? */
    style.text_indent_hanging                        = parent_style != NULL && parent_style->text_indent_hanging;
    style.text_indent_each_line                      = parent_style != NULL && parent_style->text_indent_each_line;
    const tbox_css_resolved_declaration *indent_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-indent"));
    if (indent_decl != NULL) {
        tbox_string_view tokens[3];
        size_t count = tbox_style_tokens(indent_decl->value, tokens, 3);
        tbox_style_length indent;
        bool have_length = false, hanging = false, each_line = false, valid = count > 0;
        for (size_t i = 0; i < count && valid; i++) {
            if (tbox_style_is(tokens[i], "hanging") && !hanging)
                hanging = true;
            else if (tbox_style_is(tokens[i], "each-line") && !each_line)
                each_line = true;
            else if (!have_length && tbox_style_parse_spacing_length(tokens[i], &units, &indent) && indent.kind != TBOX_STYLE_LENGTH_AUTO)
                have_length = true;
            else
                valid = false;
        }
        if (valid && have_length) {
            style.text_indent           = indent;
            style.text_indent_hanging   = hanging;
            style.text_indent_each_line = each_line;
        }
    }
    style.word_spacing                                     = parent_style != NULL ? parent_style->word_spacing : 0.0;
    const tbox_css_resolved_declaration *word_spacing_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("word-spacing"));
    if (word_spacing_decl != NULL) {
        tbox_style_length spacing;
        if (tbox_style_parse_spacing_length(tbox_style_trim(word_spacing_decl->value), &units, &spacing) && spacing.kind == TBOX_STYLE_LENGTH_PX)
            style.word_spacing = spacing.value;
        else if (tbox_string_view_equal_ascii_ci(tbox_style_trim(word_spacing_decl->value), tbox_string_view_from_cstr("normal")))
            style.word_spacing = 0.0;
    }

    /* color: inheritable. Falls back to the parent's resolved color when
     * undeclared/unparsable and there is a parent, otherwise to opaque
     * black (CSS2.1 leaves color's initial value UA-defined; black is the
     * conventional choice). */
    const tbox_css_resolved_declaration *color_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("color"));
    tbox_css_rgba color;
    if (color_decl != NULL && tbox_css_color_parse(color_decl->value, &color)) {
        style.color = color;
    } else if (parent_style != NULL) {
        style.color = parent_style->color;
    } else {
        style.color.r = 0;
        style.color.g = 0;
        style.color.b = 0;
        style.color.a = 255;
    }

    /* background and its longhands: not inheritable; initial value is a
     * transparent color and no image. */
    tbox_style_resolve_background(computed, &units, &style);

    /* font-weight: numeric, inheritable; bolder/lighter are relative to
     * the inherited weight. font_weight_bold keeps the two-face view. */
    const tbox_string_view *weight_value = tbox_style_font_value(computed, &font, "font-weight", &font_storage);
    int inherited_weight                 = parent_style == NULL ? 400 : parent_style->font_weight > 0 ? parent_style->font_weight : parent_style->font_weight_bold ? 700 : 400;
    style.font_weight                    = inherited_weight;
    if (weight_value != NULL)
        tbox_style_parse_font_weight(*weight_value, inherited_weight, &style.font_weight);
    style.font_weight_bold = style.font_weight >= 600;

    style.font_stretch = parent_style != NULL && parent_style->font_stretch > 0.0 ? parent_style->font_stretch : 100.0;
    const tbox_string_view *stretch_value = tbox_style_font_value(computed, &font, "font-stretch", &font_storage);
    if (stretch_value != NULL)
        tbox_style_parse_font_stretch(*stretch_value, &style.font_stretch);
    style.font_small_caps = parent_style != NULL && parent_style->font_small_caps;
    const tbox_string_view *variant_value = tbox_style_font_value(computed, &font, "font-variant", &font_storage);
    if (variant_value != NULL) {
        if (tbox_style_is(*variant_value, "small-caps") || tbox_style_is(*variant_value, "all-small-caps"))
            style.font_small_caps = true;
        else if (tbox_style_is(*variant_value, "normal") || tbox_style_is(*variant_value, "none"))
            style.font_small_caps = false;
    }
    style.font_kerning_none                      = parent_style != NULL && parent_style->font_kerning_none;
    const tbox_css_resolved_declaration *kerning = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-kerning"));
    if (kerning != NULL) {
        if (tbox_style_is(kerning->value, "none"))
            style.font_kerning_none = true;
        else if (tbox_style_is(kerning->value, "auto") || tbox_style_is(kerning->value, "normal"))
            style.font_kerning_none = false;
    }
    const tbox_css_resolved_declaration *variant_caps = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-variant-caps"));
    if (variant_caps != NULL) {
        if (tbox_style_is(variant_caps->value, "small-caps") || tbox_style_is(variant_caps->value, "all-small-caps"))
            style.font_small_caps = true;
        else if (tbox_style_is(variant_caps->value, "normal"))
            style.font_small_caps = false;
    }

    /* border: . Not inheritable -- always cascade-or-initial. Each
     * side's width/style/color takes the winning declaration among `border`,
     * `border-<side>`, the 1-4 value `border-width/-style/-color` and the
     * `border-<side>-width/-style/-color` longhands. */
    tbox_style_resolve_border_sides(computed, &units, style.color, style.border_widths, style.border_styles, style.border_colors);
    style.border_per_side = false;
    for (size_t i = 1; i < 4; i++) {
        if (style.border_widths[i] != style.border_widths[0] || style.border_styles[i] != style.border_styles[0] || !tbox_style_rgba_equal(style.border_colors[i], style.border_colors[0]))
            style.border_per_side = true;
    }
    style.border_width = style.border_widths[0];
    style.border_style = style.border_styles[0];
    style.border_color = style.border_colors[0];

    style.outline_width = 3.0;
    style.outline_style = TBOX_STYLE_BORDER_STYLE_NONE;
    style.outline_color = style.color;
    tbox_style_resolve_border(computed, "outline", false, &units, style.color, &style.outline_width, &style.outline_style, &style.outline_color);
    const tbox_css_resolved_declaration *outline       = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("outline"));
    const tbox_css_resolved_declaration *outline_width = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("outline-width"));
    if (tbox_style_border_longhand_wins(outline_width, outline)) {
        double parsed;
        if (tbox_style_parse_border_width(outline_width->value, &units, &parsed))
            style.outline_width = parsed;
    }
    const tbox_css_resolved_declaration *outline_style = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("outline-style"));
    if (tbox_style_border_longhand_wins(outline_style, outline)) {
        tbox_style_border_style parsed;
        if (tbox_style_parse_border_style(outline_style->value, false, &parsed))
            style.outline_style = parsed;
    }
    const tbox_css_resolved_declaration *outline_color = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("outline-color"));
    if (tbox_style_border_longhand_wins(outline_color, outline)) {
        tbox_css_rgba parsed;
        if (tbox_style_parse_edge_color(outline_color->value, style.color, &parsed))
            style.outline_color = parsed;
    }
    style.outline_offset                                = 0.0;
    const tbox_css_resolved_declaration *outline_offset = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("outline-offset"));
    if (outline_offset != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_spacing_length(tbox_style_trim(outline_offset->value), &units, &parsed) && parsed.kind == TBOX_STYLE_LENGTH_PX)
            style.outline_offset = parsed.value;
    }

    /* position + offsets: . Not inheritable. */
    style.position                           = tbox_style_resolve_position(computed);
    static const char *const offset_sides[4] = { "top", "right", "bottom", "left" };
    for (int i = 0; i < 4; i++)
        style.offset[i] = (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    tbox_style_resolve_box_edges(computed, "inset", offset_sides, &units, true, style.offset);

    /* text-align: . Same inheritance mechanism as `font-weight`
     * above -- a recognized declaration wins; otherwise inherits the
     * parent's already-resolved value; otherwise falls back to the initial
     * value LEFT with no parent. */
    const tbox_css_resolved_declaration *text_align_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-align"));
    tbox_style_text_align parsed_text_align;
    if (text_align_decl != NULL && tbox_style_parse_text_align(text_align_decl->value, &parsed_text_align)) {
        style.text_align = parsed_text_align;
    } else if (parent_style != NULL) {
        style.text_align = parent_style->text_align;
    } else {
        style.text_align = TBOX_STYLE_TEXT_ALIGN_LEFT;
    }

    /* font-family: . Same inheritance mechanism as `text-align`/
     * `font-weight` above -- a recognized declaration wins (only its first
     * comma-separated name, quotes stripped -- see
     * tbox_style_parse_font_family); otherwise inherits the parent's
     * already-resolved value; otherwise falls back to the initial value ""
     * (no override) with no parent. */
    const tbox_string_view *family_value = tbox_style_font_value(computed, &font, "font-family", &font_storage);
    char parsed_font_family[sizeof(style.font_family)];
    if (family_value != NULL && tbox_style_parse_font_family(*family_value, parsed_font_family, sizeof(parsed_font_family))) {
        memcpy(style.font_family, parsed_font_family, sizeof(style.font_family));
    } else if (parent_style != NULL) {
        memcpy(style.font_family, parent_style->font_family, sizeof(style.font_family));
    } else {
        style.font_family[0] = '\0';
    }

    /* An explicit normal value resets inherited italic text. */
    const tbox_string_view *font_style_value = tbox_style_font_value(computed, &font, "font-style", &font_storage);
    style.font_italic                        = parent_style != NULL && parent_style->font_italic;
    if (font_style_value != NULL) {
        tbox_string_view value = tbox_style_trim(*font_style_value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("italic")) || tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("oblique")))
            style.font_italic = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")))
            style.font_italic = false;
    }

    /* text-decoration / vertical-align: . Neither inherits --
     * always cascade-or-initial, same posture as background-color/border. */
    style.text_decoration           = TBOX_STYLE_TEXT_DECORATION_NONE;
    style.text_decoration_lines     = 0;
    style.text_decoration_style     = TBOX_STYLE_BORDER_STYLE_SOLID;
    style.text_decoration_color     = style.color;
    style.text_decoration_thickness = 1.0;
    tbox_style_resolve_text_decoration(computed, &units, style.color, &style.text_decoration, &style.text_decoration_lines, &style.text_decoration_style, &style.text_decoration_color, &style.text_decoration_thickness);
    const tbox_css_resolved_declaration *decoration      = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration"));
    const tbox_css_resolved_declaration *decoration_line = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration-line"));
    if (tbox_style_border_longhand_wins(decoration_line, decoration)) {
        unsigned int parsed;
        if (tbox_style_parse_decoration_lines(decoration_line->value, &parsed)) {
            style.text_decoration_lines = parsed;
            style.text_decoration = (parsed & 1u) ? TBOX_STYLE_TEXT_DECORATION_UNDERLINE : (parsed & 2u) ? TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH : (parsed & 4u) ? TBOX_STYLE_TEXT_DECORATION_OVERLINE : TBOX_STYLE_TEXT_DECORATION_NONE;
        }
    }
    const tbox_css_resolved_declaration *decoration_style = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration-style"));
    if (tbox_style_border_longhand_wins(decoration_style, decoration)) {
        tbox_style_border_style parsed;
        if (tbox_style_parse_decoration_style(tbox_style_trim(decoration_style->value), &parsed))
            style.text_decoration_style = parsed;
    }
    const tbox_css_resolved_declaration *decoration_color = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration-color"));
    if (tbox_style_border_longhand_wins(decoration_color, decoration)) {
        tbox_css_rgba parsed;
        if (tbox_style_parse_edge_color(decoration_color->value, style.color, &parsed))
            style.text_decoration_color = parsed;
    }
    const tbox_css_resolved_declaration *decoration_thickness = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration-thickness"));
    if (tbox_style_border_longhand_wins(decoration_thickness, decoration)) {
        double parsed;
        if (tbox_style_parse_decoration_thickness(decoration_thickness->value, &units, &parsed))
            style.text_decoration_thickness = parsed;
    }
    /* text-underline-offset: inheritable; `auto` keeps the default
     * position, a percentage is relative to the element's font size. */
    style.text_underline_offset                           = parent_style != NULL ? parent_style->text_underline_offset : (tbox_style_length){ TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *underline_offset = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-underline-offset"));
    if (underline_offset != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_spacing_length(tbox_style_trim(underline_offset->value), &units, &parsed)) {
            if (parsed.kind == TBOX_STYLE_LENGTH_PERCENT)
                parsed = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, style.font_size * parsed.value / 100.0, 0.0, 0, 0.0, 0.0 };
            style.text_underline_offset = parsed;
        }
    }
    style.text_underline_position_under = parent_style != NULL && parent_style->text_underline_position_under;
    const tbox_css_resolved_declaration *underline_position = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-underline-position"));
    if (underline_position != NULL) {
        tbox_string_view value = tbox_style_trim(underline_position->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("under"))) style.text_underline_position_under = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto"))) style.text_underline_position_under = false;
    }
    style.vertical_align                              = tbox_style_resolve_vertical_align(computed, &units, &style.vertical_align_length);
    const tbox_css_resolved_declaration *caption_side = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("caption-side"));
    style.caption_side                                = parent_style != NULL ? parent_style->caption_side : TBOX_STYLE_CAPTION_TOP;
    if (caption_side != NULL) {
        tbox_string_view value = tbox_style_trim(caption_side->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("bottom")))
            style.caption_side = TBOX_STYLE_CAPTION_BOTTOM;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("top")))
            style.caption_side = TBOX_STYLE_CAPTION_TOP;
    }
    const tbox_css_resolved_declaration *collapse = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("border-collapse"));
    style.border_collapse                         = parent_style != NULL && parent_style->border_collapse;
    if (collapse != NULL) {
        tbox_string_view value = tbox_style_trim(collapse->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("collapse")))
            style.border_collapse = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("separate")))
            style.border_collapse = false;
    }
    style.border_spacing_x                       = parent_style != NULL ? parent_style->border_spacing_x : 0.0;
    style.border_spacing_y                       = parent_style != NULL ? parent_style->border_spacing_y : 0.0;
    const tbox_css_resolved_declaration *spacing = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("border-spacing"));
    if (spacing != NULL) {
        tbox_string_view raw = tbox_style_trim(spacing->value);
        size_t split         = 0;
        while (split < raw.size && !tbox_style_is_space(raw.data[split]))
            split++;
        tbox_style_length first, second;
        if (tbox_style_parse_spacing_length(tbox_string_view_make(raw.data, split), &units, &first) && first.kind == TBOX_STYLE_LENGTH_PX && first.value >= 0.0) {
            tbox_string_view rest = split < raw.size ? tbox_style_trim(tbox_string_view_make(raw.data + split, raw.size - split)) : tbox_string_view_make(NULL, 0);
            if (rest.size == 0 || (tbox_style_parse_spacing_length(rest, &units, &second) && second.kind == TBOX_STYLE_LENGTH_PX && second.value >= 0.0)) {
                style.border_spacing_x = first.value;
                style.border_spacing_y = rest.size == 0 ? first.value : second.value;
            }
        }
    }

    /* border-radius / box-shadow: NOVO (visual fidelity). Neither inherits
     * -- always cascade-or-initial, same posture as border/background-color
     * above. */
    tbox_style_resolve_border_radius(computed, &units, style.border_radius_corners, style.border_radius_percent, style.border_radius_vertical, style.border_radius_vertical_percent);
    style.border_radius = style.border_radius_corners[0] == style.border_radius_corners[1] && style.border_radius_corners[0] == style.border_radius_corners[2] && style.border_radius_corners[0] == style.border_radius_corners[3] ? style.border_radius_corners[0] : 0.0;

    /* box-shadow: a list, first entry on top; the single fields mirror the
     * first entry. Not inheritable. */
    const tbox_css_resolved_declaration *box_shadow = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("box-shadow"));
    if (box_shadow != NULL)
        tbox_style_parse_shadow_list(box_shadow->value, &units, style.color, 4, true, style.box_shadows, &style.box_shadow_count);
    if (style.box_shadow_count > 0) {
        style.box_shadow_offset_x = style.box_shadows[0].offset_x;
        style.box_shadow_offset_y = style.box_shadows[0].offset_y;
        style.box_shadow_blur     = style.box_shadows[0].blur;
        style.box_shadow_spread   = style.box_shadows[0].spread;
        style.box_shadow_color    = style.box_shadows[0].color;
        style.box_shadow_inset    = style.box_shadows[0].inset;
    }

    /* text-shadow: inheritable, so an explicit `none` is what resets it. */
    if (parent_style != NULL) {
        memcpy(style.text_shadows, parent_style->text_shadows, sizeof(style.text_shadows));
        style.text_shadow_count = parent_style->text_shadow_count;
        if (style.text_shadow_count == 0 && parent_style->text_shadow_color.a > 0) {
            style.text_shadows[0]   = (tbox_style_shadow){ parent_style->text_shadow_offset_x, parent_style->text_shadow_offset_y, parent_style->text_shadow_blur, 0.0, parent_style->text_shadow_color, false };
            style.text_shadow_count = 1;
        }
    }
    const tbox_css_resolved_declaration *text_shadow = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-shadow"));
    if (text_shadow != NULL)
        tbox_style_parse_shadow_list(text_shadow->value, &units, style.color, 3, false, style.text_shadows, &style.text_shadow_count);
    if (style.text_shadow_count > 0) {
        style.text_shadow_offset_x = style.text_shadows[0].offset_x;
        style.text_shadow_offset_y = style.text_shadows[0].offset_y;
        style.text_shadow_blur     = style.text_shadows[0].blur;
        style.text_shadow_color    = style.text_shadows[0].color;
    }

    style.opacity                                = 1.0;
    const tbox_css_resolved_declaration *opacity = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("opacity"));
    if (opacity != NULL) {
        tbox_string_view value = tbox_style_trim(opacity->value);
        bool percent           = value.size > 0 && value.data[value.size - 1] == '%';
        double parsed;
        if (tbox_style_parse_number(percent ? tbox_string_view_make(value.data, value.size - 1) : value, &parsed)) {
            if (percent)
                parsed /= 100.0;
            style.opacity = parsed < 0.0 ? 0.0 : parsed > 1.0 ? 1.0 : parsed;
        }
    }

    style.accent_color = tbox_style_resolve_control_color(computed, "accent-color", parent_style != NULL ? parent_style->accent_color : (tbox_css_rgba){ 0, 0, 0, 0 }, style.color);
    tbox_style_resolve_flex(computed, &units, &style);
    style.caret_color = tbox_style_resolve_control_color(computed, "caret-color", parent_style != NULL ? parent_style->caret_color : (tbox_css_rgba){ 0, 0, 0, 0 }, style.color);

    tbox_style_resolve_extras(computed, &units, parent_style, &style);
    tbox_style_matrix_identity(style.filter_matrix);
    const tbox_css_resolved_declaration *filter = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("filter"));
    if (filter != NULL)
        tbox_style_parse_filter(filter->value, &style.has_filter, style.filter_matrix);
    style.clip_path.center[0] = style.clip_path.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    const tbox_css_resolved_declaration *clip_path = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("clip-path"));
    if (clip_path != NULL)
        tbox_style_parse_clip_path(clip_path->value, &units, &style.clip_path);
    return style;
}

/* `::marker` of a list item: resolved like a child element of it, then
 * the properties a marker honors copied into the item's own style. */
static void tbox_style_resolve_marker(const tbox_html_node *node, tbox_style *style, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, tbox_arena *arena) {
    tbox_css_computed_style computed = tbox_css_cascade_resolve_pseudo_element(sources, source_count, node, "marker");
    if (computed.count > 0) {
        tbox_style marker          = tbox_style_resolve_full(NULL, style, &computed, viewport_width, viewport_height, arena);
        style->marker_styled       = true;
        style->marker_color        = marker.color;
        style->marker_font_size    = marker.font_size;
        style->marker_font_weight  = marker.font_weight;
        style->marker_italic       = marker.font_italic;
        memcpy(style->marker_font_family, marker.font_family, sizeof(style->marker_font_family));
        const tbox_css_resolved_declaration *content = tbox_css_computed_style_find(&computed, tbox_string_view_from_cstr("content"));
        if (content != NULL) {
            tbox_string_view value = tbox_style_trim(content->value);
            if (value.size >= 2 && (value.data[0] == '"' || value.data[0] == '\'') && value.data[value.size - 1] == value.data[0]) {
                size_t n = value.size - 2 < sizeof(style->marker_content) - 1 ? value.size - 2 : sizeof(style->marker_content) - 1;
                memcpy(style->marker_content, value.data + 1, n);
                style->marker_content[n]  = '\0';
                style->marker_has_content = true;
            } else if (tbox_style_is(value, "none")) {
                style->marker_content[0]  = '\0';
                style->marker_has_content = true;
            }
        }
    }
    tbox_css_computed_style_destroy(&computed);
}

/* Recursive pre-order walk: a node's tbox_style_resolve always runs after
 * its parent's (parent_style is the parent's already-resolved style, kept
 * alive on this call's stack frame for as long as its subtree is being
 * walked), which is exactly what inheritance needs. TEXT/COMMENT/DOCTYPE/
 * DOCUMENT nodes have no style of their own -- they're walked through
 * (so ELEMENT descendants are still reached) but contribute no entry and
 * pass `parent_style` through unchanged. */
static void tbox_style_resolve_tree_walk(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, tbox_arena *arena, tbox_vector *items) {
    if (node == NULL) {
        return;
    }

    const tbox_style *effective_parent = parent_style;
    tbox_style node_style;

    if (node->type == TBOX_HTML_NODE_ELEMENT) {
        tbox_css_computed_style computed = tbox_css_cascade_resolve(sources, source_count, node);
        node_style                       = tbox_style_resolve_full(node, parent_style, &computed, viewport_width, viewport_height, arena);
        tbox_css_computed_style_destroy(&computed);
        if (tbox_string_view_equal_cstr(node->element.tag_name, "li") || node_style.display_list_item)
            tbox_style_resolve_marker(node, &node_style, sources, source_count, viewport_width, viewport_height, arena);

        tbox_style_entry *entry = (tbox_style_entry *)tbox_vector_push(items);
        entry->node             = node;
        entry->style            = node_style;

        effective_parent = &node_style;
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        tbox_style_resolve_tree_walk(child, effective_parent, sources, source_count, viewport_width, viewport_height, arena, items);
    }
}

tbox_style_table tbox_style_resolve_tree(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count) {
    return tbox_style_resolve_tree_in_viewport(arena, root, sources, source_count, 0.0, 0.0);
}

tbox_style_table tbox_style_resolve_tree_in_viewport(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height) {
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_style_entry), 0);

    tbox_style_resolve_tree_walk(root, NULL, sources, source_count, viewport_width, viewport_height, arena, &items);

    tbox_style_table table;
    table.items = (tbox_style_entry *)items.data;
    table.count = items.length;
    return table;
}

const tbox_style *tbox_style_table_find(const tbox_style_table *table, const tbox_html_node *node) {
    if (table == NULL || node == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < table->count; i++) {
        if (table->items[i].node == node) {
            return &table->items[i].style;
        }
    }
    return NULL;
}

double tbox_style_border_side_width(const tbox_style *style, size_t side) {
    if (style == NULL || side > 3)
        return 0.0;
    if (!style->border_per_side)
        return style->border_style != TBOX_STYLE_BORDER_STYLE_NONE ? style->border_width : 0.0;
    return style->border_styles[side] != TBOX_STYLE_BORDER_STYLE_NONE ? style->border_widths[side] : 0.0;
}

tbox_style_border_style tbox_style_border_side_style(const tbox_style *style, size_t side) {
    if (style == NULL || side > 3)
        return TBOX_STYLE_BORDER_STYLE_NONE;
    return style->border_per_side ? style->border_styles[side] : style->border_style;
}

tbox_css_rgba tbox_style_border_side_color(const tbox_style *style, size_t side) {
    if (style == NULL || side > 3)
        return (tbox_css_rgba){ 0, 0, 0, 0 };
    return style->border_per_side ? style->border_colors[side] : style->border_color;
}
