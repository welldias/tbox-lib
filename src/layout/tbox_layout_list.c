#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* prepends the <li> marker word (bullet or number), if any, as the
 * FIRST entry of `words` -- called before tbox_layout_collect_words so the
 * marker always lands ahead of the <li>'s own text. A no-op unless `node` is
 * an ELEMENT `<li>` whose DIRECT parent is an ELEMENT `<ul>` or `<ol>` (see
 * ARCHITECTURE.md "v8 -- Escopo": the marker kind is decided by the parent's
 * tag name alone, never a CSS property, and a <li> outside <ul>/<ol> gets no
 * marker at all). Reuses tbox_layout_push_words for the actual push --
 * measuring/word-splitting the marker exactly like any other word, so it
 * gets the same space_width/line-break treatment as real text, with no
 * duplicated tbox_font_measure_text call here. */
/* Formats `index` (1-based) as a counter marker plus its trailing '.':
 * alphabetic markers count a..z, aa..zz, ...; roman numerals cover 1-3999
 * and fall back to decimal outside that range, like browsers do. Returns
 * the length written (without a NUL), 0 on failure. */

static size_t tbox_layout_format_list_counter(tbox_style_list_style_type type, size_t index, char *buffer, size_t size) {
    char digits[20];
    size_t count = 0;
    bool upper   = type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA || type == TBOX_STYLE_LIST_STYLE_UPPER_ROMAN;
    if ((type == TBOX_STYLE_LIST_STYLE_LOWER_ALPHA || type == TBOX_STYLE_LIST_STYLE_UPPER_ALPHA) && index > 0) {
        for (size_t n = index; n > 0 && count < sizeof(digits); n = (n - 1) / 26)
            digits[count++] = (char)((upper ? 'A' : 'a') + (n - 1) % 26);
        for (size_t i = 0; i < count / 2; i++) {
            char tmp              = digits[i];
            digits[i]             = digits[count - 1 - i];
            digits[count - 1 - i] = tmp;
        }
    } else if ((type == TBOX_STYLE_LIST_STYLE_LOWER_ROMAN || type == TBOX_STYLE_LIST_STYLE_UPPER_ROMAN) && index > 0 && index < 4000) {
        static const struct {
            size_t value;
            const char *text;
        } numerals[] = {
            { 1000, "m"  },
            { 900,  "cm" },
            { 500,  "d"  },
            { 400,  "cd" },
            { 100,  "c"  },
            { 90,   "xc" },
            { 50,   "l"  },
            { 40,   "xl" },
            { 10,   "x"  },
            { 9,    "ix" },
            { 5,    "v"  },
            { 4,    "iv" },
            { 1,    "i"  },
        };
        size_t n = index;
        for (size_t i = 0; i < sizeof(numerals) / sizeof(numerals[0]); i++) {
            for (; n >= numerals[i].value; n -= numerals[i].value)
                for (const char *c = numerals[i].text; *c != '\0'; c++)
                    digits[count++] = upper ? (char)(*c - 'a' + 'A') : *c;
        }
    } else {
        int written = snprintf(digits, sizeof(digits), "%zu", index);
        if (written <= 0)
            return 0;
        count = (size_t)written < sizeof(digits) ? (size_t)written : sizeof(digits) - 1;
        if (type == TBOX_STYLE_LIST_STYLE_DECIMAL_LEADING_ZERO && index < 10 && count + 1 < sizeof(digits)) {
            memmove(digits + 1, digits, count);
            digits[0] = '0';
            count++;
        }
    }
    if (count + 1 > size)
        return 0;
    memcpy(buffer, digits, count);
    buffer[count] = '.';
    return count + 1;
}

void tbox_layout_push_list_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words) {
    if (node->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(node->element.tag_name, "li")) {
        return;
    }

    const tbox_html_node *parent = node->parent;
    if (parent == NULL || parent->type != TBOX_HTML_NODE_ELEMENT) {
        return;
    }

    bool parent_is_ul = tbox_string_view_equal_cstr(parent->element.tag_name, "ul");
    bool parent_is_ol = tbox_string_view_equal_cstr(parent->element.tag_name, "ol");
    if (!parent_is_ul && !parent_is_ol) {
        return;
    }

    /* The marker always uses the <li>'s OWN face -- never a nested <b>/<em>'s
     * -- same call tbox_layout_collect_words already makes for the <li>'s
     * direct TEXT children. */
    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
    if (face == NULL) {
        return;
    }

    tbox_style_list_style_type type = style->list_style_type;
    if (type == TBOX_STYLE_LIST_STYLE_AUTO)
        type = parent_is_ul ? TBOX_STYLE_LIST_STYLE_DISC : TBOX_STYLE_LIST_STYLE_DECIMAL;
    if (type == TBOX_STYLE_LIST_STYLE_NONE) {
        return;
    }

    /* Glyph markers fall back to the plain bullet when the face lacks
     * U+25E6 (white bullet) or U+25AA (small black square). */
    if (type == TBOX_STYLE_LIST_STYLE_DISC || type == TBOX_STYLE_LIST_STYLE_CIRCLE || type == TBOX_STYLE_LIST_STYLE_SQUARE) {
        static const tbox_string_view bullet = { "\xE2\x80\xA2", 3 };
        static const tbox_string_view circle = { "\xE2\x97\xA6", 3 };
        static const tbox_string_view square = { "\xE2\x96\xAA", 3 };
        tbox_string_view marker              = bullet;
        if (type == TBOX_STYLE_LIST_STYLE_CIRCLE && tbox_font_face_has_glyph(face, 0x25E6))
            marker = circle;
        if (type == TBOX_STYLE_LIST_STYLE_SQUARE && tbox_font_face_has_glyph(face, 0x25AA))
            marker = square;
        tbox_layout_push_words(words, marker, face, style);
        return;
    }

    /* Counters: this <li>'s 1-based position among its direct <li>
     * siblings (same parent), in document order, never restarting across
     * sibling groups. */
    size_t index = 0;
    for (const tbox_html_node *sibling = parent->first_child; sibling != NULL; sibling = sibling->next_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(sibling->element.tag_name, "li")) {
            index++;
        }
        if (sibling == node) {
            break;
        }
    }

    char buffer[24];
    size_t length = tbox_layout_format_list_counter(type, index, buffer, sizeof(buffer));
    if (length == 0) {
        return;
    }

    /* `tbox_layout_word.text` must point at memory that outlives this call
     * (the rest of the frame) -- `buffer` is a stack array, so the formatted
     * number is copied into `arena` before becoming a tbox_string_view. */
    char *copy = (char *)tbox_arena_alloc(arena, length);
    memcpy(copy, buffer, length);

    tbox_string_view number = tbox_string_view_make(copy, length);
    tbox_layout_push_words(words, number, face, style);
}
