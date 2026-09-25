#include <tbox/style.h>

#include <stdio.h>
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

static bool tbox_style_parse_spacing_length(tbox_string_view raw, double font_size, tbox_style_length *out) {
    if (raw.size == 1 && raw.data[0] == '0') {
        *out = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0 };
        return true;
    }
    return tbox_style_parse_length(raw, font_size, out);
}

static bool tbox_style_parse_border_width(tbox_string_view raw, double font_size, double *out) {
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
    if (tbox_style_parse_spacing_length(value, font_size, &length) &&
        length.kind == TBOX_STYLE_LENGTH_PX && length.value >= 0.0) {
        *out = length.value;
        return true;
    }
    return false;
}

static bool tbox_style_parse_edge_color(tbox_string_view raw, tbox_css_rgba current_color,
                                        tbox_css_rgba *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("currentcolor"))) {
        *out = current_color;
        return true;
    }
    return tbox_css_color_parse(value, out);
}

/* Resolves lengths against the parent's font size and named absolute sizes
 * against the fixed 16px medium scale. Relative keywords use a 1.2 ratio. */
static double tbox_style_resolve_font_size(const tbox_css_computed_style *computed, double parent_font_size) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-size"));
    if (decl == NULL) {
        return parent_font_size;
    }

    tbox_string_view text = tbox_style_trim(decl->value);
    if (text.size == 0) {
        return parent_font_size;
    }

    static const struct { const char *name; double pixels; } absolute_sizes[] = {
        {"xx-small", 9.0}, {"x-small", 10.0}, {"small", 13.0},
        {"medium", 16.0}, {"large", 18.0}, {"x-large", 24.0},
        {"xx-large", 32.0},
    };
    for (size_t i = 0; i < sizeof(absolute_sizes) / sizeof(absolute_sizes[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr(absolute_sizes[i].name)))
            return absolute_sizes[i].pixels;
    }
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("smaller")))
        return parent_font_size / 1.2;
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("larger")))
        return parent_font_size * 1.2;

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

/* Text decoration is not inherited. Recognize one line at a time. */
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

/* `auto` and `from-font` both use the 1px default thickness. */
static bool tbox_style_parse_decoration_thickness(tbox_string_view raw, double font_size, double *out) {
    tbox_string_view value = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto")) ||
        tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("from-font"))) {
        *out = 1.0;
        return true;
    }
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(value, font_size, &length) ||
        length.kind != TBOX_STYLE_LENGTH_PX || length.value < 0.0) return false;
    *out = length.value;
    return true;
}

/* The `text-decoration` shorthand: a line keyword, a thickness, a color and
 * the `solid` style, in any order, each optional. Parts left out reset to
 * their initial values, as with any shorthand. Only the first line keyword
 * counts (a single line at a time), other styles such as `wavy` paint solid,
 * and unrecognized tokens are ignored -- same posture as
 * tbox_style_resolve_border. */
static void tbox_style_resolve_text_decoration(const tbox_css_computed_style *computed, double font_size,
                                               tbox_css_rgba current_color, tbox_style_text_decoration *out_line,
                                               tbox_css_rgba *out_color, double *out_thickness) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("text-decoration"));
    if (decl == NULL) return;

    tbox_string_view text = tbox_style_trim(decl->value);
    bool have_line = false;
    size_t i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) i++;
        if (i >= text.size) break;
        size_t start = i;
        while (i < text.size && !tbox_style_is_space(text.data[i])) i++;
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        tbox_style_text_decoration line;
        double thickness;
        tbox_css_rgba color;
        if (tbox_style_parse_decoration_line(token, &line)) {
            if (!have_line) *out_line = line;
            have_line = true;
        } else if (tbox_style_parse_decoration_thickness(token, font_size, &thickness)) {
            *out_thickness = thickness;
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            *out_color = color;
        }
    }
}

/* Resolves supported inline and table-cell vertical alignment keywords, plus
 * a px/em/% length (percentages stay PERCENT for the Layout Tree, which
 * knows the used line-height). */
static tbox_style_vertical_align tbox_style_resolve_vertical_align(const tbox_css_computed_style *computed, double font_size, tbox_style_length *out_length) {
    *out_length = (tbox_style_length){TBOX_STYLE_LENGTH_PX, 0.0};
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
        } else if (tbox_style_parse_spacing_length(value, font_size, &length) &&
                   length.kind != TBOX_STYLE_LENGTH_AUTO) {
            *out_length = length;
            return TBOX_STYLE_VERTICAL_ALIGN_LENGTH;
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
static bool tbox_style_resolve_box_shorthand(const tbox_css_computed_style *computed, const char *property, double font_size, bool allow_auto, tbox_style_length out[4]) {
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
        if (!tbox_style_parse_spacing_length(tokens[i], font_size, &parsed[i]) ||
            (!allow_auto && (parsed[i].kind == TBOX_STYLE_LENGTH_AUTO || parsed[i].value < 0.0))) {
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

static bool tbox_style_parse_edge_length(tbox_string_view raw, double font_size, bool allow_auto,
                                         tbox_style_length *out) {
    return tbox_style_parse_spacing_length(tbox_style_trim(raw), font_size, out) &&
        (allow_auto || (out->kind != TBOX_STYLE_LENGTH_AUTO && out->value >= 0.0));
}

/* A side takes `parsed` only when `decl` beats the declaration that set it
 * so far, so physical, shorthand and logical properties follow ordinary
 * cascade precedence among themselves. */
static void tbox_style_offer_edge(const tbox_css_resolved_declaration *decl, tbox_style_length parsed,
                                  int side, const tbox_css_resolved_declaration *winners[4],
                                  tbox_style_length out[4]) {
    if (winners[side] != NULL && tbox_css_cascade_priority_compare(decl, winners[side]) <= 0) return;
    winners[side] = decl;
    out[side] = parsed;
}

/* Resolves `property` (margin, padding or inset), its physical longhands and
 * the logical `-block`/`-inline` forms. Writing is always horizontal
 * left-to-right, so block-start/end map to top/bottom and inline-start/end
 * map to left/right. */
static void tbox_style_resolve_box_edges(const tbox_css_computed_style *computed, const char *property,
                                         const char *const longhands[4], double font_size,
                                         bool allow_auto, tbox_style_length out[4]) {
    const tbox_css_resolved_declaration *winners[4] = {NULL, NULL, NULL, NULL};
    if (tbox_style_resolve_box_shorthand(computed, property, font_size, allow_auto, out)) {
        const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed,
            tbox_string_view_from_cstr(property));
        for (int i = 0; i < 4; i++) winners[i] = shorthand;
    }
    for (int i = 0; i < 4; i++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed,
            tbox_string_view_from_cstr(longhands[i]));
        tbox_style_length parsed;
        if (decl != NULL && tbox_style_parse_edge_length(decl->value, font_size, allow_auto, &parsed))
            tbox_style_offer_edge(decl, parsed, i, winners, out);
    }

    /* Axis shorthands take one or two values: start, then end. */
    static const struct { const char *suffix; int start, end; } axes[] = {
        {"-block", 0, 2}, {"-inline", 3, 1},
    };
    for (size_t a = 0; a < sizeof(axes) / sizeof(axes[0]); a++) {
        char name[32];
        snprintf(name, sizeof(name), "%s%s", property, axes[a].suffix);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed,
            tbox_string_view_from_cstr(name));
        tbox_string_view tokens[4];
        size_t count;
        if (decl == NULL || !tbox_style_split_box_shorthand(decl->value, tokens, &count) || count > 2) continue;
        tbox_style_length start, end;
        if (!tbox_style_parse_edge_length(tokens[0], font_size, allow_auto, &start)) continue;
        if (count == 1) end = start;
        else if (!tbox_style_parse_edge_length(tokens[1], font_size, allow_auto, &end)) continue;
        tbox_style_offer_edge(decl, start, axes[a].start, winners, out);
        tbox_style_offer_edge(decl, end, axes[a].end, winners, out);
    }

    static const struct { const char *suffix; int side; } logical[] = {
        {"-block-start", 0}, {"-block-end", 2}, {"-inline-start", 3}, {"-inline-end", 1},
    };
    for (size_t l = 0; l < sizeof(logical) / sizeof(logical[0]); l++) {
        char name[32];
        snprintf(name, sizeof(name), "%s%s", property, logical[l].suffix);
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed,
            tbox_string_view_from_cstr(name));
        tbox_style_length parsed;
        if (decl != NULL && tbox_style_parse_edge_length(decl->value, font_size, allow_auto, &parsed))
            tbox_style_offer_edge(decl, parsed, logical[l].side, winners, out);
    }
}

/* Parses uniform `border` and `outline` shorthands. Tokens are classified as
 * a nonnegative px/em/keyword width, solid/none style (plus `hidden` when
 * `allow_hidden`, i.e. for `border` but not `outline`), or color (including
 * currentColor). A token matching none of the three
 * is silently ignored -- it never invalidates the other tokens, nor the
 * declaration as a whole (same robustness posture as the rest of Style/CSS
 * Parser). `out_width`/`out_style`/`out_color` are only written when their
 * respective token classifies successfully; on entry they already hold the
 * caller's initial values. Returns whether a declaration was found
 * at all (false when absent, callers just keep the pre-filled initial
 * values). */
static bool tbox_style_resolve_border(const tbox_css_computed_style *computed, const char *property,
                                      bool allow_hidden, double font_size, tbox_css_rgba current_color,
                                      double *out_width, tbox_style_border_style *out_style, tbox_css_rgba *out_color) {
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
        while (i < text.size && !tbox_style_is_space(text.data[i])) {
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        double width;
        tbox_css_rgba color;
        if (tbox_style_parse_border_width(token, font_size, &width)) {
            *out_width = width;
        } else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("solid"))) {
            *out_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        } else if (tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("none")) ||
                   (allow_hidden && tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("hidden")))) {
            *out_style = TBOX_STYLE_BORDER_STYLE_NONE;
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            *out_color = color;
        }
        /* else: unrecognized token, ignored -- keep scanning. */
    }

    return true;
}

static bool tbox_style_border_longhand_wins(const tbox_css_resolved_declaration *longhand,
                                            const tbox_css_resolved_declaration *shorthand) {
    return longhand != NULL && (shorthand == NULL ||
        tbox_css_cascade_priority_compare(longhand, shorthand) > 0);
}

static bool tbox_style_parse_radius(tbox_string_view value, double font_size, double *out) {
    tbox_style_length length;
    if (!tbox_style_parse_spacing_length(tbox_style_trim(value), font_size, &length) ||
        length.kind != TBOX_STYLE_LENGTH_PX || length.value < 0.0) return false;
    *out = length.value;
    return true;
}

/* CSS clockwise shorthand expansion; individual corners obey the same
 * cascade precedence as the existing border longhands. Elliptical and
 * percentage radii remain outside this renderer's circular-pixel model. */
static void tbox_style_resolve_border_radius(const tbox_css_computed_style *computed,
                                             double font_size, double out[4]) {
    for (size_t i = 0; i < 4; i++) out[i] = 0.0;
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("border-radius"));
    bool shorthand_valid = false;
    if (shorthand != NULL) {
        tbox_string_view value = tbox_style_trim(shorthand->value);
        double parsed[4];
        size_t count = 0, pos = 0;
        bool valid = true;
        while (pos < value.size) {
            while (pos < value.size && tbox_style_is_space(value.data[pos])) pos++;
            if (pos == value.size) break;
            size_t start = pos;
            while (pos < value.size && !tbox_style_is_space(value.data[pos])) pos++;
            if (count == 4 || !tbox_style_parse_radius(tbox_string_view_make(value.data + start, pos - start),
                    font_size, &parsed[count])) { valid = false; break; }
            count++;
        }
        if (valid && count > 0) {
            shorthand_valid = true;
            out[0] = parsed[0];
            out[1] = parsed[count > 1 ? 1 : 0];
            out[2] = parsed[count > 2 ? 2 : 0];
            out[3] = parsed[count > 3 ? 3 : count > 1 ? 1 : 0];
        }
    }

    static const char *const names[4] = {
        "border-top-left-radius", "border-top-right-radius",
        "border-bottom-right-radius", "border-bottom-left-radius"
    };
    for (size_t i = 0; i < 4; i++) {
        const tbox_css_resolved_declaration *longhand = tbox_css_computed_style_find(computed,
            tbox_string_view_from_cstr(names[i]));
        if (tbox_style_border_longhand_wins(longhand, shorthand_valid ? shorthand : NULL)) {
            double radius;
            if (tbox_style_parse_radius(longhand->value, font_size, &radius)) out[i] = radius;
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
static bool tbox_style_parse_shadow(tbox_string_view raw, double font_size, tbox_css_rgba current_color,
                                    int max_lengths, double out_lengths[4], tbox_css_rgba *out_color) {
    tbox_string_view text = tbox_style_trim(raw);
    if (tbox_string_view_equal_ascii_ci(text, tbox_string_view_from_cstr("none"))) {
        for (int i = 0; i < 4; i++) out_lengths[i] = 0.0;
        *out_color = (tbox_css_rgba){0, 0, 0, 0};
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
            if (paren_depth > 0) paren_depth--;
        } else if (text.data[i] == ',' && paren_depth == 0) {
            return false;
        }
    }

    double lengths[4] = {0.0, 0.0, 0.0, 0.0};
    int length_count = 0;
    tbox_css_rgba color = current_color;
    size_t i = 0;
    while (i < text.size) {
        while (i < text.size && tbox_style_is_space(text.data[i])) i++;
        if (i >= text.size) break;
        size_t start = i;
        paren_depth = 0;
        while (i < text.size && (paren_depth > 0 || !tbox_style_is_space(text.data[i]))) {
            if (text.data[i] == '(') paren_depth++;
            else if (text.data[i] == ')' && paren_depth > 0) paren_depth--;
            i++;
        }
        tbox_string_view token = tbox_string_view_make(text.data + start, i - start);

        tbox_style_length length;
        if (tbox_style_parse_spacing_length(token, font_size, &length) && length.kind == TBOX_STYLE_LENGTH_PX) {
            if (length_count >= max_lengths) return false;
            lengths[length_count++] = length.value;
        } else if (tbox_style_parse_edge_color(token, current_color, &color)) {
            /* color kept */
        } else if (!tbox_string_view_equal_ascii_ci(token, tbox_string_view_from_cstr("inset"))) {
            return false;
        }
    }

    if (length_count < 2 || lengths[2] < 0.0) return false;
    for (int j = 0; j < 4; j++) out_lengths[j] = lengths[j];
    *out_color = color;
    return true;
}

static bool tbox_style_parse_list_style_type(tbox_string_view raw, tbox_style_list_style_type *out) {
    static const struct { const char *name; tbox_style_list_style_type value; } types[] = {
        {"disc", TBOX_STYLE_LIST_STYLE_DISC}, {"circle", TBOX_STYLE_LIST_STYLE_CIRCLE},
        {"square", TBOX_STYLE_LIST_STYLE_SQUARE}, {"decimal", TBOX_STYLE_LIST_STYLE_DECIMAL},
        {"lower-alpha", TBOX_STYLE_LIST_STYLE_LOWER_ALPHA}, {"lower-latin", TBOX_STYLE_LIST_STYLE_LOWER_ALPHA},
        {"upper-alpha", TBOX_STYLE_LIST_STYLE_UPPER_ALPHA}, {"upper-latin", TBOX_STYLE_LIST_STYLE_UPPER_ALPHA},
        {"lower-roman", TBOX_STYLE_LIST_STYLE_LOWER_ROMAN}, {"upper-roman", TBOX_STYLE_LIST_STYLE_UPPER_ROMAN},
        {"none", TBOX_STYLE_LIST_STYLE_NONE},
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
static tbox_style_list_style_type tbox_style_resolve_list_style_type(const tbox_css_computed_style *computed,
                                                                     tbox_style_list_style_type inherited) {
    tbox_style_list_style_type result = inherited;
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("list-style"));
    if (shorthand != NULL) {
        result = TBOX_STYLE_LIST_STYLE_DISC;
        tbox_string_view tokens[4];
        size_t count;
        if (tbox_style_split_box_shorthand(shorthand->value, tokens, &count))
            for (size_t i = 0; i < count; i++) tbox_style_parse_list_style_type(tokens[i], &result);
    }
    const tbox_css_resolved_declaration *longhand = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("list-style-type"));
    tbox_style_list_style_type parsed;
    if (tbox_style_border_longhand_wins(longhand, shorthand) &&
        tbox_style_parse_list_style_type(longhand->value, &parsed)) result = parsed;
    return result;
}

/* `accent-color`/`caret-color`: inheritable, `auto` stored as alpha 0 (see
 * tbox_style.accent_color). An unparsable value keeps the inherited one. */
static tbox_css_rgba tbox_style_resolve_control_color(const tbox_css_computed_style *computed, const char *property,
                                                      tbox_css_rgba inherited, tbox_css_rgba current_color) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl == NULL) return inherited;
    if (tbox_string_view_equal_ascii_ci(tbox_style_trim(decl->value), tbox_string_view_from_cstr("auto")))
        return (tbox_css_rgba){0, 0, 0, 0};
    tbox_css_rgba parsed;
    return tbox_style_parse_edge_color(decl->value, current_color, &parsed) ? parsed : inherited;
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

tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed) {
    /* `node` is used below by width/height's image HTML-attribute
     * fallback (tbox_style_resolve_img_dimension_attribute) -- every other
     * property here is still a pure function of `computed`/`parent_style`,
     * unchanged from v0's original "kept in the signature for per-tag
     * defaults later" stance (see ARCHITECTURE.md). */

    tbox_style style;

    /* display: v0's initial value is BLOCK, not CSS2.1's spec-correct
     * `inline` -- see the comment on tbox_style.display in style.h. */
    style.display    = TBOX_STYLE_DISPLAY_BLOCK;
    style.overflow_y = TBOX_STYLE_OVERFLOW_Y_VISIBLE;
    style.box_sizing = TBOX_STYLE_BOX_SIZING_CONTENT_BOX;
    style.visibility_hidden = parent_style != NULL && parent_style->visibility_hidden;
    style.text_overflow = TBOX_STYLE_TEXT_OVERFLOW_CLIP;

    const tbox_css_resolved_declaration *sizing_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("box-sizing"));
    if (sizing_decl != NULL && tbox_string_view_equal_ascii_ci(tbox_style_trim(sizing_decl->value),
        tbox_string_view_from_cstr("border-box"))) style.box_sizing = TBOX_STYLE_BOX_SIZING_BORDER_BOX;
    const tbox_css_resolved_declaration *visibility_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("visibility"));
    if (visibility_decl != NULL) {
        tbox_string_view value = tbox_style_trim(visibility_decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("hidden")))
            style.visibility_hidden = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("visible")))
            style.visibility_hidden = false;
    }
    const tbox_css_resolved_declaration *text_overflow_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-overflow"));
    if (text_overflow_decl != NULL && tbox_string_view_equal_ascii_ci(tbox_style_trim(text_overflow_decl->value),
        tbox_string_view_from_cstr("ellipsis"))) style.text_overflow = TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS;

    const tbox_css_resolved_declaration *overflow_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("overflow-y"));
    const tbox_css_resolved_declaration *overflow_short = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("overflow"));
    if (overflow_short != NULL && (overflow_decl == NULL ||
        tbox_css_cascade_priority_compare(overflow_short, overflow_decl) > 0))
        overflow_decl = overflow_short;
    if (overflow_decl != NULL) {
        tbox_string_view value = tbox_style_trim(overflow_decl->value);
        /* `scroll` behaves as `auto` (no always-visible scrollbar), and
         * `clip` as `hidden` (content is not programmatically scrollable
         * in either case here). */
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto")) ||
            tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("scroll")))
            style.overflow_y = TBOX_STYLE_OVERFLOW_Y_AUTO;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("hidden")) ||
                 tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("clip")))
            style.overflow_y = TBOX_STYLE_OVERFLOW_Y_HIDDEN;
    }
    style.white_space_nowrap = parent_style != NULL && parent_style->white_space_nowrap;
    const tbox_css_resolved_declaration *white_space = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("white-space"));
    if (white_space != NULL) {
        tbox_string_view value = tbox_style_trim(white_space->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("nowrap")))
            style.white_space_nowrap = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")))
            style.white_space_nowrap = false;
    }
    style.overflow_wrap_break_word = parent_style != NULL && parent_style->overflow_wrap_break_word;
    const tbox_css_resolved_declaration *overflow_wrap = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("overflow-wrap"));
    if (overflow_wrap != NULL) {
        tbox_string_view value = tbox_style_trim(overflow_wrap->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("break-word")))
            style.overflow_wrap_break_word = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")))
            style.overflow_wrap_break_word = false;
    }
    style.word_break_all = parent_style != NULL && parent_style->word_break_all;
    const tbox_css_resolved_declaration *word_break = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("word-break"));
    if (word_break != NULL) {
        tbox_string_view value = tbox_style_trim(word_break->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("break-all")))
            style.word_break_all = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")) ||
                 tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("keep-all")))
            style.word_break_all = false;
    }
    style.text_transform = parent_style != NULL ? parent_style->text_transform : TBOX_STYLE_TEXT_TRANSFORM_NONE;
    const tbox_css_resolved_declaration *text_transform = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-transform"));
    if (text_transform != NULL) {
        static const struct { const char *name; tbox_style_text_transform value; } transforms[] = {
            {"none", TBOX_STYLE_TEXT_TRANSFORM_NONE}, {"uppercase", TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE},
            {"lowercase", TBOX_STYLE_TEXT_TRANSFORM_LOWERCASE}, {"capitalize", TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE},
        };
        tbox_string_view value = tbox_style_trim(text_transform->value);
        for (size_t i = 0; i < sizeof(transforms) / sizeof(transforms[0]); i++)
            if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(transforms[i].name)))
                style.text_transform = transforms[i].value;
    }
    style.list_style_type = tbox_style_resolve_list_style_type(computed, parent_style != NULL ?
        parent_style->list_style_type : TBOX_STYLE_LIST_STYLE_AUTO);
    style.pointer_events_none = parent_style != NULL && parent_style->pointer_events_none;
    const tbox_css_resolved_declaration *pointer_events = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("pointer-events"));
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
    style.line_height_kind = parent_style != NULL ? parent_style->line_height_kind :
        TBOX_STYLE_LINE_HEIGHT_NORMAL;
    style.line_height_value = parent_style != NULL ? parent_style->line_height_value : 0.0;
    const tbox_css_resolved_declaration *line_height_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("line-height"));
    if (line_height_decl != NULL) {
        tbox_string_view value = tbox_style_trim(line_height_decl->value);
        double multiplier;
        tbox_style_length length = {TBOX_STYLE_LENGTH_AUTO, 0.0};
        bool has_length = tbox_style_parse_spacing_length(value, style.font_size, &length);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal"))) {
            style.line_height_kind = TBOX_STYLE_LINE_HEIGHT_NORMAL;
            style.line_height_value = 0.0;
        } else if (tbox_style_parse_number(value, &multiplier) && multiplier >= 0.0) {
            style.line_height_kind = TBOX_STYLE_LINE_HEIGHT_NUMBER;
            style.line_height_value = multiplier;
        } else if (has_length &&
                   length.kind == TBOX_STYLE_LENGTH_PX && length.value >= 0.0) {
            style.line_height_kind = TBOX_STYLE_LINE_HEIGHT_PX;
            style.line_height_value = length.value;
        } else if (has_length && length.kind == TBOX_STYLE_LENGTH_PERCENT && length.value >= 0.0) {
            style.line_height_kind = TBOX_STYLE_LINE_HEIGHT_PX;
            style.line_height_value = style.font_size * length.value / 100.0;
        }
    }
    style.letter_spacing = parent_style != NULL ? parent_style->letter_spacing : 0.0;
    const tbox_css_resolved_declaration *letter_spacing_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("letter-spacing"));
    if (letter_spacing_decl != NULL) {
        tbox_string_view value = tbox_style_trim(letter_spacing_decl->value);
        tbox_style_length length;
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")))
            style.letter_spacing = 0.0;
        else if (tbox_style_parse_spacing_length(value, style.font_size, &length) &&
                 length.kind == TBOX_STYLE_LENGTH_PX) style.letter_spacing = length.value;
    }

    style.width  = tbox_style_resolve_length_property(computed, "width", style.font_size);
    style.height = tbox_style_resolve_length_property(computed, "height", style.font_size);
    style.min_width = (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    style.max_width = (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    style.min_height = (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    style.max_height = (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    const tbox_css_resolved_declaration *min_width_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("min-width"));
    const tbox_css_resolved_declaration *max_width_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("max-width"));
    tbox_style_length width_limit;
    if (min_width_decl != NULL && tbox_style_parse_spacing_length(min_width_decl->value,
        style.font_size, &width_limit) && width_limit.kind != TBOX_STYLE_LENGTH_AUTO &&
        width_limit.value >= 0.0) style.min_width = width_limit;
    if (max_width_decl != NULL && tbox_style_parse_spacing_length(max_width_decl->value,
        style.font_size, &width_limit) && width_limit.kind != TBOX_STYLE_LENGTH_AUTO &&
        width_limit.value >= 0.0) style.max_width = width_limit;
    const tbox_css_resolved_declaration *min_height_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("min-height"));
    const tbox_css_resolved_declaration *max_height_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("max-height"));
    tbox_style_length height_limit;
    if (min_height_decl != NULL && tbox_style_parse_spacing_length(min_height_decl->value,
        style.font_size, &height_limit) && height_limit.kind != TBOX_STYLE_LENGTH_AUTO &&
        height_limit.value >= 0.0) style.min_height = height_limit;
    if (max_height_decl != NULL && tbox_style_parse_spacing_length(max_height_decl->value,
        style.font_size, &height_limit) && height_limit.kind != TBOX_STYLE_LENGTH_AUTO &&
        height_limit.value >= 0.0) style.max_height = height_limit;
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
    static const char *const margin_sides[4] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
    static const char *const padding_sides[4] = {"padding-top", "padding-right", "padding-bottom", "padding-left"};
    tbox_style_resolve_box_edges(computed, "margin", margin_sides, style.font_size, true, style.margin);
    tbox_style_resolve_box_edges(computed, "padding", padding_sides, style.font_size, false, style.padding);
    style.text_indent = parent_style != NULL ? parent_style->text_indent :
        (tbox_style_length){TBOX_STYLE_LENGTH_PX, 0.0};
    const tbox_css_resolved_declaration *indent_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-indent"));
    if (indent_decl != NULL) {
        tbox_style_length indent;
        if (tbox_style_parse_spacing_length(tbox_style_trim(indent_decl->value), style.font_size, &indent) &&
            indent.kind != TBOX_STYLE_LENGTH_AUTO) style.text_indent = indent;
    }
    style.word_spacing = parent_style != NULL ? parent_style->word_spacing : 0.0;
    const tbox_css_resolved_declaration *word_spacing_decl = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("word-spacing"));
    if (word_spacing_decl != NULL) {
        tbox_style_length spacing;
        if (tbox_style_parse_spacing_length(tbox_style_trim(word_spacing_decl->value), style.font_size, &spacing) &&
            spacing.kind == TBOX_STYLE_LENGTH_PX) style.word_spacing = spacing.value;
        else if (tbox_string_view_equal_ascii_ci(tbox_style_trim(word_spacing_decl->value),
                 tbox_string_view_from_cstr("normal"))) style.word_spacing = 0.0;
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

    /* background-color: not inheritable; initial value is transparent. */
    const tbox_css_resolved_declaration *bg_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background-color"));
    const tbox_css_resolved_declaration *bg_short = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("background"));
    tbox_css_rgba background;
    tbox_css_rgba short_color;
    bool short_valid = bg_short != NULL && tbox_style_parse_edge_color(bg_short->value, style.color, &short_color);
    if (short_valid && (bg_decl == NULL ||
        tbox_css_cascade_priority_compare(bg_short, bg_decl) > 0)) {
        style.background_color = short_color;
    } else if (bg_decl != NULL && tbox_style_parse_edge_color(bg_decl->value, style.color, &background)) {
        style.background_color = background;
    } else {
        style.background_color.r = 0;
        style.background_color.g = 0;
        style.background_color.b = 0;
        style.background_color.a = 0;
    }

    /* The font backend selects either regular or bold. Map numeric weights
     * through 500 to regular, and 600 through 900 to bold. */
    const tbox_css_resolved_declaration *weight_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-weight"));
    style.font_weight_bold = parent_style != NULL && parent_style->font_weight_bold;
    if (weight_decl != NULL) {
        tbox_string_view value = tbox_style_trim(weight_decl->value);
        /* With only two faces, `bolder` always lands on bold (400 -> 700,
         * 700 -> 900) and `lighter` always on regular (700 -> 400,
         * 400 -> 100), whatever the inherited weight was. */
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("bold")) ||
            tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("bolder")))
            style.font_weight_bold = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal")) ||
                 tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("lighter")))
            style.font_weight_bold = false;
        else if (value.size == 3 && value.data[1] == '0' && value.data[2] == '0' &&
                 value.data[0] >= '1' && value.data[0] <= '9')
            style.font_weight_bold = value.data[0] >= '6';
    }

    /* border: NOVO v4. Not inheritable -- always cascade-or-initial. */
    style.border_width   = 0.0;
    style.border_style   = TBOX_STYLE_BORDER_STYLE_NONE;
    style.border_color = style.color;
    tbox_style_resolve_border(computed, "border", true, style.font_size, style.color,
                              &style.border_width, &style.border_style, &style.border_color);
    const tbox_css_resolved_declaration *border = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("border"));
    const tbox_css_resolved_declaration *border_width = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("border-width"));
    if (tbox_style_border_longhand_wins(border_width, border)) {
        double parsed;
        if (tbox_style_parse_border_width(border_width->value, style.font_size, &parsed))
            style.border_width = parsed;
    }
    const tbox_css_resolved_declaration *border_style = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("border-style"));
    if (tbox_style_border_longhand_wins(border_style, border)) {
        tbox_string_view value = tbox_style_trim(border_style->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("solid")))
            style.border_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none")) ||
                 tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("hidden")))
            style.border_style = TBOX_STYLE_BORDER_STYLE_NONE;
    }
    const tbox_css_resolved_declaration *border_color = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("border-color"));
    if (tbox_style_border_longhand_wins(border_color, border)) {
        tbox_css_rgba parsed;
        if (tbox_style_parse_edge_color(border_color->value, style.color, &parsed))
            style.border_color = parsed;
    }

    style.outline_width = 3.0;
    style.outline_style = TBOX_STYLE_BORDER_STYLE_NONE;
    style.outline_color = style.color;
    tbox_style_resolve_border(computed, "outline", false, style.font_size, style.color,
                              &style.outline_width, &style.outline_style, &style.outline_color);
    const tbox_css_resolved_declaration *outline = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("outline"));
    const tbox_css_resolved_declaration *outline_width = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("outline-width"));
    if (tbox_style_border_longhand_wins(outline_width, outline)) {
        double parsed;
        if (tbox_style_parse_border_width(outline_width->value, style.font_size, &parsed))
            style.outline_width = parsed;
    }
    const tbox_css_resolved_declaration *outline_style = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("outline-style"));
    if (tbox_style_border_longhand_wins(outline_style, outline)) {
        tbox_string_view value = tbox_style_trim(outline_style->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("solid")))
            style.outline_style = TBOX_STYLE_BORDER_STYLE_SOLID;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none")))
            style.outline_style = TBOX_STYLE_BORDER_STYLE_NONE;
    }
    const tbox_css_resolved_declaration *outline_color = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("outline-color"));
    if (tbox_style_border_longhand_wins(outline_color, outline)) {
        tbox_css_rgba parsed;
        if (tbox_style_parse_edge_color(outline_color->value, style.color, &parsed)) style.outline_color = parsed;
    }
    style.outline_offset = 0.0;
    const tbox_css_resolved_declaration *outline_offset = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("outline-offset"));
    if (outline_offset != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_spacing_length(tbox_style_trim(outline_offset->value),
            style.font_size, &parsed) && parsed.kind == TBOX_STYLE_LENGTH_PX)
            style.outline_offset = parsed.value;
    }

    /* position + offsets: NOVO v4. Not inheritable. */
    style.position  = tbox_style_resolve_position(computed);
    static const char *const offset_sides[4] = {"top", "right", "bottom", "left"};
    for (int i = 0; i < 4; i++) style.offset[i] = (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    tbox_style_resolve_box_edges(computed, "inset", offset_sides, style.font_size, true, style.offset);

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

    /* An explicit normal value resets inherited italic text. */
    const tbox_css_resolved_declaration *font_style_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-style"));
    style.font_italic = parent_style != NULL && parent_style->font_italic;
    if (font_style_decl != NULL) {
        tbox_string_view value = tbox_style_trim(font_style_decl->value);
        if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("italic")) ||
            tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("oblique"))) style.font_italic = true;
        else if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("normal"))) style.font_italic = false;
    }

    /* text-decoration / vertical-align: NOVO v13. Neither inherits --
     * always cascade-or-initial, same posture as background-color/border. */
    style.text_decoration                             = TBOX_STYLE_TEXT_DECORATION_NONE;
    style.text_decoration_color                       = style.color;
    style.text_decoration_thickness                   = 1.0;
    tbox_style_resolve_text_decoration(computed, style.font_size, style.color, &style.text_decoration,
                                       &style.text_decoration_color, &style.text_decoration_thickness);
    const tbox_css_resolved_declaration *decoration = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-decoration"));
    const tbox_css_resolved_declaration *decoration_line = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-decoration-line"));
    if (tbox_style_border_longhand_wins(decoration_line, decoration)) {
        tbox_style_text_decoration parsed;
        if (tbox_style_parse_decoration_line(decoration_line->value, &parsed))
            style.text_decoration = parsed;
    }
    const tbox_css_resolved_declaration *decoration_color = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-decoration-color"));
    if (tbox_style_border_longhand_wins(decoration_color, decoration)) {
        tbox_css_rgba parsed;
        if (tbox_style_parse_edge_color(decoration_color->value, style.color, &parsed))
            style.text_decoration_color = parsed;
    }
    const tbox_css_resolved_declaration *decoration_thickness = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-decoration-thickness"));
    if (tbox_style_border_longhand_wins(decoration_thickness, decoration)) {
        double parsed;
        if (tbox_style_parse_decoration_thickness(decoration_thickness->value, style.font_size, &parsed))
            style.text_decoration_thickness = parsed;
    }
    /* text-underline-offset: inheritable; `auto` keeps the default
     * position, a percentage is relative to the element's font size. */
    style.text_underline_offset = parent_style != NULL ? parent_style->text_underline_offset :
        (tbox_style_length){TBOX_STYLE_LENGTH_AUTO, 0.0};
    const tbox_css_resolved_declaration *underline_offset = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-underline-offset"));
    if (underline_offset != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_spacing_length(tbox_style_trim(underline_offset->value), style.font_size, &parsed)) {
            if (parsed.kind == TBOX_STYLE_LENGTH_PERCENT)
                parsed = (tbox_style_length){TBOX_STYLE_LENGTH_PX, style.font_size * parsed.value / 100.0};
            style.text_underline_offset = parsed;
        }
    }
    style.vertical_align = tbox_style_resolve_vertical_align(computed, style.font_size, &style.vertical_align_length);
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
        if (tbox_style_parse_spacing_length(tbox_string_view_make(raw.data, split), style.font_size, &first) && first.kind == TBOX_STYLE_LENGTH_PX && first.value >= 0.0) {
            tbox_string_view rest = split < raw.size ? tbox_style_trim(tbox_string_view_make(raw.data + split, raw.size - split)) : tbox_string_view_make(NULL, 0);
            if (rest.size == 0 || (tbox_style_parse_spacing_length(rest, style.font_size, &second) && second.kind == TBOX_STYLE_LENGTH_PX && second.value >= 0.0)) {
                style.border_spacing_x = first.value;
                style.border_spacing_y = rest.size == 0 ? first.value : second.value;
            }
        }
    }

    /* border-radius / box-shadow: NOVO (visual fidelity). Neither inherits
     * -- always cascade-or-initial, same posture as border/background-color
     * above. */
    tbox_style_resolve_border_radius(computed, style.font_size, style.border_radius_corners);
    style.border_radius = style.border_radius_corners[0] == style.border_radius_corners[1] &&
        style.border_radius_corners[0] == style.border_radius_corners[2] &&
        style.border_radius_corners[0] == style.border_radius_corners[3] ?
        style.border_radius_corners[0] : 0.0;

    double shadow[4] = {0.0, 0.0, 0.0, 0.0};
    tbox_css_rgba shadow_color = {0, 0, 0, 0};
    const tbox_css_resolved_declaration *box_shadow = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("box-shadow"));
    if (box_shadow != NULL)
        tbox_style_parse_shadow(box_shadow->value, style.font_size, style.color, 4, shadow, &shadow_color);
    style.box_shadow_offset_x = shadow[0];
    style.box_shadow_offset_y = shadow[1];
    style.box_shadow_blur     = shadow[2];
    style.box_shadow_spread   = shadow[3];
    style.box_shadow_color    = shadow_color;

    /* text-shadow: inheritable, so an explicit `none` is what resets it. */
    style.text_shadow_offset_x = parent_style != NULL ? parent_style->text_shadow_offset_x : 0.0;
    style.text_shadow_offset_y = parent_style != NULL ? parent_style->text_shadow_offset_y : 0.0;
    style.text_shadow_blur     = parent_style != NULL ? parent_style->text_shadow_blur : 0.0;
    style.text_shadow_color    = parent_style != NULL ? parent_style->text_shadow_color :
        (tbox_css_rgba){0, 0, 0, 0};
    const tbox_css_resolved_declaration *text_shadow = tbox_css_computed_style_find(computed,
        tbox_string_view_from_cstr("text-shadow"));
    if (text_shadow != NULL && tbox_style_parse_shadow(text_shadow->value, style.font_size, style.color, 3,
                                                       shadow, &shadow_color)) {
        style.text_shadow_offset_x = shadow[0];
        style.text_shadow_offset_y = shadow[1];
        style.text_shadow_blur     = shadow[2];
        style.text_shadow_color    = shadow_color;
    }

    style.accent_color = tbox_style_resolve_control_color(computed, "accent-color", parent_style != NULL ?
        parent_style->accent_color : (tbox_css_rgba){0, 0, 0, 0}, style.color);
    style.caret_color = tbox_style_resolve_control_color(computed, "caret-color", parent_style != NULL ?
        parent_style->caret_color : (tbox_css_rgba){0, 0, 0, 0}, style.color);

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
