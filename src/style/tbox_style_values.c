#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"
#include "base/tbox_number.h"
#include "style/tbox_style_internal.h"

/* Parses a plain number ("10", "-3.5") with tbox_number_parse
 * (locale-independent). Rejects anything that doesn't consume the whole
 * text, including empty input. */
bool tbox_style_parse_number(tbox_string_view text, double *out_value) {
    return tbox_number_parse(text, out_value);
}

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
        /* A desktop window has no browser chrome: small, large and dynamic
         * viewports all have the same dimensions here. */
        { "svw", vw, true }, { "svh", vh, true },
        { "lvw", vw, true }, { "lvh", vh, true },
        { "dvw", vw, true }, { "dvh", vh, true },
        { "svmin", vw < vh ? vw : vh, true }, { "svmax", vw > vh ? vw : vh, true },
        { "lvmin", vw < vh ? vw : vh, true }, { "lvmax", vw > vh ? vw : vh, true },
        { "dvmin", vw < vh ? vw : vh, true }, { "dvmax", vw > vh ? vw : vh, true },
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
bool tbox_style_parse_dimension(tbox_string_view text, const tbox_style_units *units, double em_base, double *out) {
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

/* Parses a <length-percentage>: the keyword "auto", a percentage (resolved
 * later by the Layout Tree, against its containing block), a number with
 * any unit tbox_style_unit_scale knows -- folded into px right here, em
 * against the node's own font size (see docs/dev/historico.md's v8 Style section
 * for why this is the own node's font-size, not the parent's, unlike
 * `font-size: em` itself) -- or a calc() expression, whose percentage part
 * becomes a PERCENT length carrying the px part in `px_offset`. Anything
 * else, including a bare unitless number, fails so the caller can fall
 * back to the initial value, same as an undeclared property. */
bool tbox_style_parse_length(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    tbox_string_view text = tbox_string_view_trim_ascii_space(raw);
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

bool tbox_style_parse_spacing_length(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    if (raw.size == 1 && raw.data[0] == '0') {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
        return true;
    }
    return tbox_style_parse_length(raw, units, out);
}

bool tbox_style_parse_edge_color(tbox_string_view raw, tbox_css_rgba current_color, tbox_css_rgba *out) {
    tbox_string_view value = tbox_string_view_trim_ascii_space(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("currentcolor"))) {
        *out = current_color;
        return true;
    }
    return tbox_css_color_parse(value, out);
}

/* Position components shared by object-position and background-position. A percentage aligns the same
 * point of the image and its box; a length offsets from the left/top edge.
 * Keywords may appear in horizontal/vertical or vertical/horizontal order. */
bool tbox_style_parse_object_position_component(tbox_string_view token, const tbox_style_units *units, bool horizontal, tbox_style_length *out) {
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

/* Which of 1-4 tokens applies to `side` (top, right, bottom, left), same
 * pattern as margin/padding. */
size_t tbox_style_four_value_index(size_t count, size_t side) {
    static const size_t map[4][4] = {
        { 0, 0, 0, 0 },
        { 0, 1, 0, 1 },
        { 0, 1, 2, 1 },
        { 0, 1, 2, 3 }
    };
    return map[count - 1][side];
}

/* A nonnegative px/em radius (into `out_px`) or percentage (into
 * `out_percent`); the other output is zeroed. */
bool tbox_style_parse_radius(tbox_string_view value, const tbox_style_units *units, double *out_px, double *out_percent) {
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(tbox_string_view_trim_ascii_space(value), units, &length) || length.kind == TBOX_STYLE_LENGTH_AUTO || length.value < 0.0)
        return false;
    *out_px      = length.kind == TBOX_STYLE_LENGTH_PX ? length.value : length.px_offset;
    *out_percent = length.kind == TBOX_STYLE_LENGTH_PERCENT ? length.value : 0.0;
    return true;
}

/* Parses one shadow: `<x> <y> [<blur> [<spread>]] [<color>]`, in any
 * order between the lengths and the color, `max_lengths` being 4 for
 * box-shadow and 3 for text-shadow. Lengths are px/em (or a bare 0); the
 * color defaults to currentColor. `none` yields a transparent color (no
 * shadow). Returns false -- keep the caller's fallback -- for more than one
 * shadow, too few/many lengths, a negative blur, or an unrecognized token
 * other than `inset` (which is ignored: the shadow still paints outside). */
bool tbox_style_parse_shadow(tbox_string_view raw, const tbox_style_units *units, tbox_css_rgba current_color, int max_lengths, double out_lengths[4], tbox_css_rgba *out_color, bool *out_inset) {
    tbox_string_view text = tbox_string_view_trim_ascii_space(raw);
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
        while (i < text.size && tbox_ascii_is_space(text.data[i]))
            i++;
        if (i >= text.size)
            break;
        size_t start = i;
        paren_depth  = 0;
        while (i < text.size && (paren_depth > 0 || !tbox_ascii_is_space(text.data[i]))) {
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

/* ---- tokens shared by the properties below ---- */

bool tbox_style_is(tbox_string_view value, const char *keyword) {
    return tbox_string_view_equal_ascii_ci(tbox_string_view_trim_ascii_space(value), tbox_string_view_from_cstr(keyword));
}

/* Splits `text` at top-level occurrences of `separator` (outside
 * parentheses and quotes) into at most `max` trimmed parts. Returns the
 * part count, or 0 when there are more than `max` parts or an empty one. */
size_t tbox_style_split_top_level(tbox_string_view text, char separator, tbox_string_view *parts, size_t max) {
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
            tbox_string_view part = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + start, i - start));
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
size_t tbox_style_tokens(tbox_string_view text, tbox_string_view *tokens, size_t max) {
    size_t count = 0, i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_ascii_is_space(text.data[i]))
            i++;
        if (i >= text.size)
            break;
        size_t start = i;
        if (text.data[i] == '/') {
            i++;
        } else {
            int depth  = 0;
            char quote = 0;
            while (i < text.size && (depth > 0 || quote != 0 || (!tbox_ascii_is_space(text.data[i]) && text.data[i] != '/'))) {
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
bool tbox_style_function_args(tbox_string_view token, const char *name, tbox_string_view *args) {
    size_t length = strlen(name);
    if (token.size < length + 2 || token.data[length] != '(' || token.data[token.size - 1] != ')' ||
        !tbox_string_view_equal_ascii_ci(tbox_string_view_make(token.data, length), tbox_string_view_make(name, length)))
        return false;
    *args = tbox_string_view_trim_ascii_space(tbox_string_view_make(token.data + length + 1, token.size - length - 2));
    return true;
}

/* An integer (z-index, line-clamp, ...). */
bool tbox_style_parse_integer(tbox_string_view raw, int *out) {
    double value;
    if (!tbox_style_parse_number(tbox_string_view_trim_ascii_space(raw), &value) || value != (double)(int)value)
        return false;
    *out = (int)value;
    return true;
}

/* An angle in degrees: deg, grad, rad, turn, or a bare 0. */
bool tbox_style_parse_angle_literal(tbox_string_view raw, double *out) {
    static const struct {
        const char *unit;
        double degrees;
    } units[] = {
        { "deg",  1.0                   },
        { "grad", 0.9                   },
        { "rad",  57.29577951308232     },
        { "turn", 360.0                 },
    };
    tbox_string_view text = tbox_string_view_trim_ascii_space(raw);
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

/* background-position / gradient `at` component offsets: a keyword (as
 * object-position), a length-percentage, or (4-value syntax) an edge
 * keyword followed by an offset from that edge -- `right 10px` is
 * `calc(100% - 10px)`. Up to four tokens; false when invalid. */
bool tbox_style_parse_position(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_length out[2]) {
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

/* 1-4 radii (clockwise from top-left, CSS's shorthand expansion) into px
 * and percent arrays; false when any is invalid. */
bool tbox_style_parse_radius_list(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, double px[4], double percent[4]) {
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

