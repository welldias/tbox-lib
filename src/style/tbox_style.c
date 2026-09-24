#include <tbox/style.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

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

/* Parses a <length> the way v0 scopes it, extended by v8 with "em": the
 * keyword "auto", a bare number followed by "px", a bare number followed by
 * "%", or (NOVO v8) a bare number followed by "em" -- resolved immediately
 * against `font_size` (the caller's own node, already computed earlier in
 * tbox_style_resolve -- see ARCHITECTURE.md's v8 Style section for why this
 * is the own node's font-size, not the parent's, unlike `font-size: em`
 * itself) into a plain TBOX_STYLE_LENGTH_PX. Still no rem (out of scope --
 * no "root element" notion in this engine) -- anything else, including a
 * bare unitless number, fails so the caller can fall back to the initial
 * value, same as an undeclared property. */
static bool tbox_style_parse_length(tbox_string_view raw, double font_size, tbox_style_length *out) {
    tbox_string_view text = tbox_style_trim(raw);
    if (text.size == 0) {
        return false;
    }

    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("auto"))) {
        out->kind  = TBOX_STYLE_LENGTH_AUTO;
        out->value = 0.0;
        return true;
    }

    if (text.data[text.size - 1] == '%') {
        double value;
        if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 1), &value)) {
            return false;
        }
        out->kind  = TBOX_STYLE_LENGTH_PERCENT;
        out->value = value;
        return true;
    }

    if (text.size > 2) {
        tbox_string_view suffix = tbox_string_view_make(text.data + text.size - 2, 2);
        if (tbox_string_view_equal_ascii_ci(suffix, tbox_string_view_from_cstr("px"))) {
            double value;
            if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 2), &value)) {
                return false;
            }
            out->kind  = TBOX_STYLE_LENGTH_PX;
            out->value = value;
            return true;
        }
        if (tbox_string_view_equal_ascii_ci(suffix, tbox_string_view_from_cstr("em"))) {
            double value;
            if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 2), &value)) {
                return false;
            }
            out->kind  = TBOX_STYLE_LENGTH_PX;
            out->value = font_size * value;
            return true;
        }
    }

    return false;
}

/* NOVO v2: resolves `font-size` per ARCHITECTURE.md's Style section --
 * "<number>px" (absolute), "<number>em" (parent_font_size * number), or
 * "<number>%" (parent_font_size * number / 100). Anything else (absent,
 * unparsable, or any CSS2.1 keyword like "medium"/"larger" -- out of
 * scope) inherits `parent_font_size` unchanged. `parent_font_size` is
 * already the caller's fallback (16px with no parent -- see
 * tbox_style_resolve), so this function never needs a separate "no
 * parent" case of its own. */
static double tbox_style_resolve_font_size(const tbox_css_computed_style *computed, double parent_font_size) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-size"));
    if (decl == NULL) {
        return parent_font_size;
    }

    tbox_string_view text = tbox_style_trim(decl->value);
    if (text.size == 0) {
        return parent_font_size;
    }

    if (text.data[text.size - 1] == '%') {
        double value;
        if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 1), &value)) {
            return parent_font_size;
        }
        return parent_font_size * value / 100.0;
    }

    if (text.size > 2) {
        tbox_string_view suffix = tbox_string_view_make(text.data + text.size - 2, 2);
        if (tbox_string_view_equal_ascii_ci(suffix, tbox_string_view_from_cstr("px"))) {
            double value;
            if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 2), &value)) {
                return parent_font_size;
            }
            return value;
        }
        if (tbox_string_view_equal_ascii_ci(suffix, tbox_string_view_from_cstr("em"))) {
            double value;
            if (!tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 2), &value)) {
                return parent_font_size;
            }
            return parent_font_size * value;
        }
    }

    return parent_font_size;
}

static bool tbox_style_parse_display(tbox_string_view raw, tbox_style_display *out) {
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("block"))) {
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
    return false;
}

/* NOVO v11: parses `text-align`'s three supported keywords, case-insensitive
 * -- same pattern as tbox_style_parse_display above. Returns false for any
 * other value, including "justify" (out of scope, see ARCHITECTURE.md's v11
 * Style section), so the caller falls back to inheritance/the initial value
 * the same way an unrecognized `font-weight` already does. */
static bool tbox_style_parse_text_align(tbox_string_view raw, tbox_style_text_align *out) {
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("left"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_LEFT;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("center"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_CENTER;
        return true;
    }
    if (tbox_string_view_equal_ascii_ci(raw, tbox_string_view_from_cstr("right"))) {
        *out = TBOX_STYLE_TEXT_ALIGN_RIGHT;
        return true;
    }
    return false;
}

/* NOVO v13: resolves `text-decoration` per ARCHITECTURE.md's v13 Style
 * section -- same two-branch pattern as tbox_style_resolve_position/the
 * `border` block above: only looks at `computed`, never at `parent_style`
 * (text-decoration is not inheritable). Recognizes the case-insensitive
 * keywords `underline`/`line-through`; anything else -- absent, unparsable,
 * or any other keyword (e.g. `overline`, out of scope) -- falls back to the
 * initial value NONE. */
static tbox_style_text_decoration tbox_style_resolve_text_decoration(const tbox_css_computed_style *computed) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration"));
    if (decl != NULL) {
        tbox_string_view value = tbox_style_trim(decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("underline"))) {
            return TBOX_STYLE_TEXT_DECORATION_UNDERLINE;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("line-through"))) {
            return TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH;
        }
    }
    return TBOX_STYLE_TEXT_DECORATION_NONE;
}

/* NOVO v13: resolves `vertical-align` per ARCHITECTURE.md's v13 Style
 * section -- same two-branch, not-inheritable pattern as
 * tbox_style_resolve_text_decoration above. Recognizes the case-insensitive
 * keywords `sub`/`super`; anything else -- absent, unparsable, or any other
 * keyword (e.g. `top`/`middle`/`bottom`, out of scope) -- falls back to the
 * initial value BASELINE. */
static tbox_style_vertical_align tbox_style_resolve_vertical_align(const tbox_css_computed_style *computed) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("vertical-align"));
    if (decl != NULL) {
        tbox_string_view value = tbox_style_trim(decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("sub"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_SUB;
        } else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("super"))) {
            return TBOX_STYLE_VERTICAL_ALIGN_SUPER;
        }
    }
    return TBOX_STYLE_VERTICAL_ALIGN_BASELINE;
}

/* NOVO v12: parses `font-family` per ARCHITECTURE.md's v12 Style section --
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
        size_t start = i;
        while (i < text.size && !tbox_style_is_space(text.data[i])) {
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

/* Expands the standard CSS2.1 margin/padding shorthand (1/2/3/4-value
 * syntax) into `out[4]` (top, right, bottom, left). On any parse failure --
 * no declaration for `property`, an out-of-grammar token count, or any
 * single token failing to parse as a <length> -- leaves `out` untouched, so
 * the caller can pre-fill it with the initial value (0px on every side)
 * before calling this. */
static bool tbox_style_resolve_box_shorthand(const tbox_css_computed_style *computed, const char *property, double font_size, tbox_style_length out[4]) {
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
        if (!tbox_style_parse_length(tokens[i], font_size, &parsed[i])) {
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

/* NOVO v4: parses the `border` shorthand per ARCHITECTURE.md's v4 Style
 * section -- splits on whitespace into up to 3 tokens (order-free, each
 * optional), classifying each token by the first rule that accepts it:
 * (1) ends in "px" and the rest parses as a number -> border_width; (2)
 * case-insensitive "solid"/"none" -> border_style; (3) otherwise, tries
 * tbox_css_color_parse -> border_color. A token matching none of the three
 * is silently ignored -- it never invalidates the other tokens, nor the
 * declaration as a whole (same robustness posture as the rest of Style/CSS
 * Parser). `out_width`/`out_style`/`out_color` are only written when their
 * respective token classifies successfully; on entry they already hold the
 * caller's initial values. Returns whether a `border` declaration was found
 * at all (false when absent, callers just keep the pre-filled initial
 * values). */
static bool tbox_style_resolve_border(const tbox_css_computed_style *computed, double *out_width, tbox_style_border_style *out_style, tbox_css_rgba *out_color) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("border"));
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
        while (i < text.size && !tbox_style_is_space(text.data[i])) {
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        double width;
        tbox_css_rgba color;
        if (token.size > 2 && tbox_string_view_equal_ascii_ci(tbox_string_view_make(token.data + token.size - 2, 2), tbox_string_view_from_cstr("px")) && tbox_style_parse_number(tbox_string_view_make(token.data, token.size - 2), &width)) {
            *out_width = width;
        } else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("solid"))) {
            *out_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        } else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("none"))) {
            *out_style = TBOX_STYLE_BORDER_STYLE_NONE;
        } else if (tbox_css_color_parse(token, &color)) {
            *out_color = color;
        }
        /* else: unrecognized token, ignored -- keep scanning. */
    }

    return true;
}

/* NOVO (visual fidelity): `border-radius` -- a single, uniform px length.
 * Same token shape as `border`'s own width token (tbox_style_resolve_border
 * above): a bare number followed by "px", nothing else recognized (no
 * percentages, no per-corner values -- see include/tbox/style.h's field
 * comment for why). Falls back to `0.0` (no rounding) on anything else,
 * same "unrecognized token, ignored" posture as `border`. */
static double tbox_style_resolve_border_radius(const tbox_css_computed_style *computed) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("border-radius"));
    if (decl == NULL) {
        return 0.0;
    }

    tbox_string_view value = tbox_style_trim(decl->value);
    double radius;
    if (value.size > 2 && tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data + value.size - 2, 2), tbox_string_view_from_cstr("px")) && tbox_style_parse_number(tbox_string_view_make(value.data, value.size - 2), &radius) && radius >= 0.0) {
        return radius;
    }
    return 0.0;
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
static bool tbox_style_resolve_box_shadow(const tbox_css_computed_style *computed, double *out_offset_x, double *out_offset_y, double *out_blur, tbox_css_rgba *out_color) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("box-shadow"));
    if (decl == NULL) {
        return false;
    }

    tbox_string_view text = tbox_style_trim(decl->value);

    /* A comma OUTSIDE parentheses separates multiple shadows (out of
     * scope, rejected entirely) -- a comma INSIDE parentheses is just an
     * rgba()/hsla() color's own argument separator, which must NOT trigger
     * this. A separate pass (not folded into the tokenizer below) so a
     * comma with no surrounding whitespace, e.g. "...red,2px...", is still
     * caught -- the tokenizer's own paren-tracking only governs where IT
     * splits on whitespace, not comma detection. */
    {
        int paren_depth = 0;
        for (size_t i = 0; i < text.size; i++) {
            if (text.data[i] == '(') {
                paren_depth++;
            } else if (text.data[i] == ')') {
                if (paren_depth > 0) {
                    paren_depth--;
                }
            } else if (text.data[i] == ',' && paren_depth == 0) {
                return false;
            }
        }
    }

    double offsets[3];
    int offset_count = 0;
    bool have_color   = false;
    tbox_css_rgba color;

    size_t i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) {
            i++;
        }
        if (i >= text.size) {
            break;
        }

        size_t start    = i;
        int paren_depth = 0;
        while (i < text.size && (paren_depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(') {
                paren_depth++;
            } else if (text.data[i] == ')' && paren_depth > 0) {
                paren_depth--;
            }
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        double length;
        if (token.size > 2 && tbox_string_view_equal_ascii_ci(tbox_string_view_make(token.data + token.size - 2, 2), tbox_string_view_from_cstr("px")) && tbox_style_parse_number(tbox_string_view_make(token.data, token.size - 2), &length)) {
            if (offset_count < 3) {
                offsets[offset_count++] = length;
            }
        } else if (tbox_css_color_parse(token, &color)) {
            have_color = true;
        }
        /* else: unrecognized token (e.g. "inset"), ignored -- keep scanning. */
    }

    if (!have_color || offset_count < 2) {
        return false;
    }

    *out_offset_x = offsets[0];
    *out_offset_y = offsets[1];
    *out_blur     = offset_count >= 3 ? offsets[2] : 0.0;
    *out_color    = color;
    return true;
}

/* NOVO v4: `position` recognizes `static`/`relative`, case-insensitive.
 * NOVO v5: `absolute`/`fixed`/`sticky` added, same case-insensitive
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

static tbox_style_length tbox_style_resolve_length_property(const tbox_css_computed_style *computed, const char *property, double font_size) {
    tbox_style_length result                  = { TBOX_STYLE_LENGTH_AUTO, 0.0 };
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_length(decl->value, font_size, &parsed)) {
            result = parsed;
        }
    }
    return result;
}

/* `<img width="100" height="100">`: real HTML lets these bare numeric
 * attributes (no unit, unlike CSS) set the SAME properties as `width`/
 * `height` in CSS, but as a low-priority "presentational hint" -- any CSS
 * declaration (author OR the UA stylesheet) still wins outright, which is
 * exactly what calling this ONLY when `tbox_style_resolve_length_property`
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
    tbox_style_length result = { TBOX_STYLE_LENGTH_AUTO, 0.0 };
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) {
        return result;
    }
    bool is_img = tbox_string_view_equal_ascii_ci(node->element.tag_name, tbox_string_view_from_cstr("img"));
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("type"));
    bool is_image_input = tbox_string_view_equal_ascii_ci(node->element.tag_name, tbox_string_view_from_cstr("input")) &&
        type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_from_cstr("image"));
    if (!is_img && !is_image_input) return result;

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

tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed) {
    /* `node` is used below by width/height's image HTML-attribute
     * fallback (tbox_style_resolve_img_dimension_attribute) -- every other
     * property here is still a pure function of `computed`/`parent_style`,
     * unchanged from v0's original "kept in the signature for per-tag
     * defaults later" stance (see ARCHITECTURE.md). */

    tbox_style style;

    /* display: v0's initial value is BLOCK, not CSS2.1's spec-correct
     * `inline` -- see the comment on tbox_style.display in style.h. */
    style.display                                     = TBOX_STYLE_DISPLAY_BLOCK;
    style.overflow_y = TBOX_STYLE_OVERFLOW_Y_VISIBLE;
    const tbox_css_resolved_declaration *overflow_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("overflow-y"));
    if (overflow_decl != NULL && tbox_string_view_equal_ascii_ci(tbox_style_trim(overflow_decl->value), tbox_string_view_from_cstr("auto"))) {
        style.overflow_y = TBOX_STYLE_OVERFLOW_Y_AUTO;
    }
    const tbox_css_resolved_declaration *display_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("display"));
    if (display_decl != NULL) {
        tbox_style_display parsed;
        if (tbox_style_parse_display(display_decl->value, &parsed)) {
            style.display = parsed;
        }
    }

    /* font-size: NOVO v2. Inheritable through the parent's already-resolved
     * value (not re-parsed); falls back to the CSS2.1-ish 16px initial
     * value with no parent -- same default already used by Fonte/Texto
     * since v0. MOVIDO v8: precisa ser calculado antes de
     * width/height/margin/padding/offset, já que `em` nessas propriedades
     * (NOVO v8) resolve contra este mesmo `style.font_size`. */
    double parent_font_size = (parent_style != NULL) ? parent_style->font_size : 16.0;
    style.font_size         = tbox_style_resolve_font_size(computed, parent_font_size);

    style.width  = tbox_style_resolve_length_property(computed, "width", style.font_size);
    style.height = tbox_style_resolve_length_property(computed, "height", style.font_size);
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
    tbox_style_resolve_box_shorthand(computed, "margin", style.font_size, style.margin);
    tbox_style_resolve_box_shorthand(computed, "padding", style.font_size, style.padding);

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

    /* background-color: not inheritable; initial value is transparent. */
    const tbox_css_resolved_declaration *bg_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-color"));
    tbox_css_rgba background;
    if (bg_decl != NULL && tbox_css_color_parse(bg_decl->value, &background)) {
        style.background_color = background;
    } else {
        style.background_color.r = 0;
        style.background_color.g = 0;
        style.background_color.b = 0;
        style.background_color.a = 0;
    }

    /* font-weight: NOVO v2. Only the exact case-insensitive keyword "bold"
     * sets true; anything else (absent, "normal", 100-900, bolder/lighter
     * -- all out of scope) inherits the parent's already-resolved value,
     * the same inheritance mechanism as `color` above, or false with no
     * parent. */
    const tbox_css_resolved_declaration *weight_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-weight"));
    if (weight_decl != NULL && tbox_string_view_equal_ascii_ci(weight_decl->value, tbox_string_view_from_cstr("bold"))) {
        style.font_weight_bold = true;
    } else if (parent_style != NULL) {
        style.font_weight_bold = parent_style->font_weight_bold;
    } else {
        style.font_weight_bold = false;
    }

    /* border: NOVO v4. Not inheritable -- always cascade-or-initial. */
    style.border_width   = 0.0;
    style.border_style   = TBOX_STYLE_BORDER_STYLE_NONE;
    style.border_color.r = 0;
    style.border_color.g = 0;
    style.border_color.b = 0;
    style.border_color.a = 255;
    tbox_style_resolve_border(computed, &style.border_width, &style.border_style, &style.border_color);

    /* position + offsets: NOVO v4. Not inheritable. */
    style.position  = tbox_style_resolve_position(computed);
    style.offset[0] = tbox_style_resolve_length_property(computed, "top", style.font_size);
    style.offset[1] = tbox_style_resolve_length_property(computed, "right", style.font_size);
    style.offset[2] = tbox_style_resolve_length_property(computed, "bottom", style.font_size);
    style.offset[3] = tbox_style_resolve_length_property(computed, "left", style.font_size);

    /* text-align: NOVO v11. Same inheritance mechanism as `font-weight`
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

    /* font-family: NOVO v12. Same inheritance mechanism as `text-align`/
     * `font-weight` above -- a recognized declaration wins (only its first
     * comma-separated name, quotes stripped -- see
     * tbox_style_parse_font_family); otherwise inherits the parent's
     * already-resolved value; otherwise falls back to the initial value ""
     * (no override) with no parent. */
    const tbox_css_resolved_declaration *font_family_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-family"));
    char parsed_font_family[sizeof(style.font_family)];
    if (font_family_decl != NULL && tbox_style_parse_font_family(font_family_decl->value, parsed_font_family, sizeof(parsed_font_family))) {
        memcpy(style.font_family, parsed_font_family, sizeof(style.font_family));
    } else if (parent_style != NULL) {
        memcpy(style.font_family, parent_style->font_family, sizeof(style.font_family));
    } else {
        style.font_family[0] = '\0';
    }

    /* font-style: NOVO v13. Same three-branch inheritance mechanism as
     * `font-weight` above -- only the exact case-insensitive keyword
     * "italic" sets true; anything else (absent, "normal", "oblique" -- out
     * of scope) inherits the parent's already-resolved value, or false with
     * no parent. */
    const tbox_css_resolved_declaration *font_style_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-style"));
    if (font_style_decl != NULL && tbox_string_view_equal_ascii_ci(font_style_decl->value, tbox_string_view_from_cstr("italic"))) {
        style.font_italic = true;
    } else if (parent_style != NULL) {
        style.font_italic = parent_style->font_italic;
    } else {
        style.font_italic = false;
    }

    /* text-decoration / vertical-align: NOVO v13. Neither inherits --
     * always cascade-or-initial, same posture as background-color/border. */
    style.text_decoration = tbox_style_resolve_text_decoration(computed);
    style.vertical_align  = tbox_style_resolve_vertical_align(computed);

    /* border-radius / box-shadow: NOVO (visual fidelity). Neither inherits
     * -- always cascade-or-initial, same posture as border/background-color
     * above. */
    style.border_radius = tbox_style_resolve_border_radius(computed);

    style.box_shadow_offset_x = 0.0;
    style.box_shadow_offset_y = 0.0;
    style.box_shadow_blur     = 0.0;
    style.box_shadow_color    = (tbox_css_rgba){ 0, 0, 0, 0 };
    tbox_style_resolve_box_shadow(computed, &style.box_shadow_offset_x, &style.box_shadow_offset_y, &style.box_shadow_blur, &style.box_shadow_color);

    return style;
}

/* Recursive pre-order walk: a node's tbox_style_resolve always runs after
 * its parent's (parent_style is the parent's already-resolved style, kept
 * alive on this call's stack frame for as long as its subtree is being
 * walked), which is exactly what inheritance needs. TEXT/COMMENT/DOCTYPE/
 * DOCUMENT nodes have no style of their own -- they're walked through
 * (so ELEMENT descendants are still reached) but contribute no entry and
 * pass `parent_style` through unchanged. */
static void tbox_style_resolve_tree_walk(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_cascade_source *sources, size_t source_count, tbox_vector *items) {
    if (node == NULL) {
        return;
    }

    const tbox_style *effective_parent = parent_style;
    tbox_style node_style;

    if (node->type == TBOX_HTML_NODE_ELEMENT) {
        tbox_css_computed_style computed = tbox_css_cascade_resolve(sources, source_count, node);
        node_style                       = tbox_style_resolve(node, parent_style, &computed);
        tbox_css_computed_style_destroy(&computed);

        tbox_style_entry *entry = (tbox_style_entry *)tbox_vector_push(items);
        entry->node             = node;
        entry->style            = node_style;

        effective_parent = &node_style;
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        tbox_style_resolve_tree_walk(child, effective_parent, sources, source_count, items);
    }
}

tbox_style_table tbox_style_resolve_tree(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count) {
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_style_entry), 0);

    tbox_style_resolve_tree_walk(root, NULL, sources, source_count, &items);

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
