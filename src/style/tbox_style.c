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

/* Parses a <length> the way v0 scopes it: the keyword "auto", a bare number
 * followed by "px", or a bare number followed by "%". No em/rem (out of
 * scope until the font/text layer exists) -- anything else, including a
 * bare unitless number, fails so the caller can fall back to the initial
 * value, same as an undeclared property. */
static bool tbox_style_parse_length(tbox_string_view raw, tbox_style_length *out) {
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
static bool tbox_style_resolve_box_shorthand(const tbox_css_computed_style *computed, const char *property, tbox_style_length out[4]) {
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
        if (!tbox_style_parse_length(tokens[i], &parsed[i])) {
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

/* NOVO v4: `position` only recognizes `static`/`relative`, case-insensitive
 * -- any other value (absent, unparsable, or an out-of-scope keyword like
 * `absolute`) falls back to the initial value STATIC, same posture as
 * `display` since v0. */
static tbox_style_position tbox_style_resolve_position(const tbox_css_computed_style *computed) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("position"));
    if (decl != NULL && tbox_string_view_equal_ascii_ci(tbox_style_trim(decl->value), tbox_string_view_from_cstr("relative"))) {
        return TBOX_STYLE_POSITION_RELATIVE;
    }
    return TBOX_STYLE_POSITION_STATIC;
}

static tbox_style_length tbox_style_resolve_length_property(const tbox_css_computed_style *computed, const char *property) {
    tbox_style_length result                  = { TBOX_STYLE_LENGTH_AUTO, 0.0 };
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(property));
    if (decl != NULL) {
        tbox_style_length parsed;
        if (tbox_style_parse_length(decl->value, &parsed)) {
            result = parsed;
        }
    }
    return result;
}

tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed) {
    (void)node; /* not needed by any of v0's in-scope properties; kept in the signature for per-tag defaults later (see ARCHITECTURE.md) */

    tbox_style style;

    /* display: v0's initial value is BLOCK, not CSS2.1's spec-correct
     * `inline` -- see the comment on tbox_style.display in style.h. */
    style.display                                     = TBOX_STYLE_DISPLAY_BLOCK;
    const tbox_css_resolved_declaration *display_decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("display"));
    if (display_decl != NULL) {
        tbox_style_display parsed;
        if (tbox_style_parse_display(display_decl->value, &parsed)) {
            style.display = parsed;
        }
    }

    style.width  = tbox_style_resolve_length_property(computed, "width");
    style.height = tbox_style_resolve_length_property(computed, "height");

    for (int i = 0; i < 4; i++) {
        style.margin[i].kind   = TBOX_STYLE_LENGTH_PX;
        style.margin[i].value  = 0.0;
        style.padding[i].kind  = TBOX_STYLE_LENGTH_PX;
        style.padding[i].value = 0.0;
    }
    tbox_style_resolve_box_shorthand(computed, "margin", style.margin);
    tbox_style_resolve_box_shorthand(computed, "padding", style.padding);

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

    /* font-size: NOVO v2. Inheritable through the parent's already-resolved
     * value (not re-parsed); falls back to the CSS2.1-ish 16px initial
     * value with no parent -- same default already used by Fonte/Texto
     * since v0. */
    double parent_font_size = (parent_style != NULL) ? parent_style->font_size : 16.0;
    style.font_size         = tbox_style_resolve_font_size(computed, parent_font_size);

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
    style.offset[0] = tbox_style_resolve_length_property(computed, "top");
    style.offset[1] = tbox_style_resolve_length_property(computed, "right");
    style.offset[2] = tbox_style_resolve_length_property(computed, "bottom");
    style.offset[3] = tbox_style_resolve_length_property(computed, "left");

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
