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
    } else if (type == TBOX_STYLE_LIST_STYLE_LOWER_GREEK && index > 0) {
        static const char *const greek[24] = {
            "α", "β", "γ", "δ", "ε", "ζ", "η", "θ", "ι", "κ", "λ", "μ",
            "ν", "ξ", "ο", "π", "ρ", "σ", "τ", "υ", "φ", "χ", "ψ", "ω"
        };
        unsigned char letters[10];
        size_t length = 0;
        for (size_t n = index; n > 0; n = (n - 1) / 24) {
            if (length == sizeof(letters)) return 0;
            letters[length++] = (unsigned char)((n - 1) % 24);
        }
        for (size_t i = length; i > 0; i--) {
            if (count + 2 > sizeof(digits)) return 0;
            memcpy(digits + count, greek[letters[i - 1]], 2);
            count += 2;
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

/* The marker text of `node` (an <li> in a <ul>/<ol>) and the face it is
 * measured in; false when it has none. */
/* A list item's marker: text in `face`/`style`, or an image. */
typedef struct tbox_layout_marker {
    tbox_string_view text;
    const tbox_font_face *face;
    const tbox_style *style;
    const tbox_image *image;
} tbox_layout_marker;

/* The item's style with its ::marker overrides (color, font), allocated
 * in `arena`; the item's own style when there are none. */
static const tbox_style *tbox_layout_marker_style(tbox_arena *arena, const tbox_style *style) {
    if (!style->marker_styled)
        return style;
    tbox_style *marker = (tbox_style *)tbox_arena_alloc(arena, sizeof(tbox_style));
    if (marker == NULL)
        return style;
    *marker                  = *style;
    marker->color            = style->marker_color;
    marker->font_size        = style->marker_font_size;
    marker->font_weight      = style->marker_font_weight;
    marker->font_weight_bold = style->marker_font_weight >= 600;
    marker->font_italic      = style->marker_italic;
    marker->background_color = (tbox_css_rgba){ 0, 0, 0, 0 };
    marker->text_decoration_lines = 0;
    marker->text_decoration  = TBOX_STYLE_TEXT_DECORATION_NONE;
    memcpy(marker->font_family, style->marker_font_family, sizeof(marker->font_family));
    return marker;
}

static bool tbox_layout_list_marker_text(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_string_view *out_text, const tbox_font_face **out_face);

/* The marker of `node`: list-style-image when it loads, else the
 * ::marker content string, else the list-style-type marker. */
static bool tbox_layout_list_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_marker *out) {
    *out       = (tbox_layout_marker){ { NULL, 0 }, NULL, tbox_layout_marker_style(arena, style), NULL };
    if (!tbox_layout_list_marker_text(arena, node, out->style, fonts, &out->text, &out->face))
        return false;
    if (style->list_style_image[0] != '\0' && images != NULL) {
        out->image = tbox_image_cache_get(images, tbox_string_view_from_cstr(style->list_style_image));
        if (out->image != NULL && out->image->width > 0 && out->image->height > 0)
            return true;
        out->image = NULL;
    }
    if (style->marker_has_content) {
        out->text = tbox_string_view_from_cstr(style->marker_content);
        return out->text.size > 0;
    }
    return true;
}

static bool tbox_layout_list_marker_text(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_string_view *out_text, const tbox_font_face **out_face) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }
    /* `display: list-item` gives any element a marker (disc unless
     * list-style-type says otherwise), counted among its same-tag
     * siblings. */
    bool is_li                   = tbox_string_view_equal_cstr(node->element.tag_name, "li");
    const tbox_html_node *parent = node->parent;
    bool parent_is_ul = false, parent_is_ol = false;
    if (is_li && parent != NULL && parent->type == TBOX_HTML_NODE_ELEMENT) {
        parent_is_ul = tbox_string_view_equal_cstr(parent->element.tag_name, "ul");
        parent_is_ol = tbox_string_view_equal_cstr(parent->element.tag_name, "ol");
    }
    if (!parent_is_ul && !parent_is_ol) {
        if (!style->display_list_item || parent == NULL)
            return false;
        parent_is_ul = true; /* AUTO marker: disc */
    }

    /* The marker always uses the <li>'s OWN face -- never a nested <b>/<em>'s
     * -- same call tbox_layout_collect_words already makes for the <li>'s
     * direct TEXT children. */
    const tbox_font_face *face = tbox_layout_style_face(fonts, style);
    if (face == NULL) {
        return false;
    }
    *out_face = face;

    tbox_style_list_style_type type = style->list_style_type;
    if (type == TBOX_STYLE_LIST_STYLE_AUTO)
        type = parent_is_ul ? TBOX_STYLE_LIST_STYLE_DISC : TBOX_STYLE_LIST_STYLE_DECIMAL;
    if (type == TBOX_STYLE_LIST_STYLE_NONE) {
        return false;
    }

    /* Glyph markers fall back to the plain bullet when the face lacks
     * U+25E6 (white bullet) or U+25AA (small black square). */
    if (type == TBOX_STYLE_LIST_STYLE_DISC || type == TBOX_STYLE_LIST_STYLE_CIRCLE || type == TBOX_STYLE_LIST_STYLE_SQUARE ||
        type == TBOX_STYLE_LIST_STYLE_DISCLOSURE_OPEN || type == TBOX_STYLE_LIST_STYLE_DISCLOSURE_CLOSED) {
        static const tbox_string_view bullet = { "\xE2\x80\xA2", 3 };
        static const tbox_string_view circle = { "\xE2\x97\xA6", 3 };
        static const tbox_string_view square = { "\xE2\x96\xAA", 3 };
        static const tbox_string_view open = { "\xE2\x96\xBE", 3 };
        static const tbox_string_view closed = { "\xE2\x96\xB8", 3 };
        tbox_string_view marker              = bullet;
        if (type == TBOX_STYLE_LIST_STYLE_CIRCLE && tbox_font_face_has_glyph(face, 0x25E6))
            marker = circle;
        if (type == TBOX_STYLE_LIST_STYLE_SQUARE && tbox_font_face_has_glyph(face, 0x25AA))
            marker = square;
        if (type == TBOX_STYLE_LIST_STYLE_DISCLOSURE_OPEN && tbox_font_face_has_glyph(face, 0x25BE))
            marker = open;
        if (type == TBOX_STYLE_LIST_STYLE_DISCLOSURE_CLOSED && tbox_font_face_has_glyph(face, 0x25B8))
            marker = closed;
        *out_text = marker;
        return true;
    }

    /* Counters: this <li>'s 1-based position among its direct <li>
     * siblings (same parent), in document order, never restarting across
     * sibling groups. */
    size_t index = 0;
    for (const tbox_html_node *sibling = parent->first_child; sibling != NULL; sibling = sibling->next_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal(sibling->element.tag_name, node->element.tag_name)) {
            index++;
        }
        if (sibling == node) {
            break;
        }
    }

    char buffer[24];
    size_t length = tbox_layout_format_list_counter(type, index, buffer, sizeof(buffer));
    if (length == 0) {
        return false;
    }

    /* `tbox_layout_word.text` must point at memory that outlives this call
     * (the rest of the frame) -- `buffer` is a stack array, so the formatted
     * number is copied into `arena` before becoming a tbox_string_view. */
    char *copy = (char *)tbox_arena_alloc(arena, length);
    memcpy(copy, buffer, length);

    *out_text = tbox_string_view_make(copy, length);
    return true;
}

/* `list-style-position: inside`: the marker is the first word of the
 * <li>'s text, wrapping with it (an image marker as an image word). */
void tbox_layout_push_list_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_vector *words) {
    tbox_layout_marker marker;
    if (!style->list_style_inside || !tbox_layout_list_marker(arena, node, style, fonts, images, &marker))
        return;
    if (marker.image == NULL) {
        tbox_layout_push_words(words, marker.text, marker.face, marker.style);
        return;
    }
    tbox_layout_word *entry = tbox_layout_new_word(words);
    entry->face             = marker.face;
    entry->style            = marker.style;
    entry->width            = (double)marker.image->width;
    entry->image            = marker.image;
    entry->image_height     = (double)marker.image->height;
    entry->space_width      = marker.face != NULL ? tbox_font_measure_text_spaced(marker.face, tbox_string_view_make(" ", 1), style->letter_spacing) : 0.0;
}

/* `outside` (the initial value): a run of its own hanging left of the
 * content box on the first line, followed by a space's gap, taking no
 * room from the text. `first_line` NULL (no text at all) puts it at
 * `line_y` with its face's own line metrics. An image marker sits on the
 * first line's baseline at its natural size. */
void tbox_layout_append_outside_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double line_y, const tbox_layout_line *first_line, tbox_vector *runs) {
    tbox_layout_marker marker;
    if (node == NULL || style->list_style_inside || !tbox_layout_list_marker(arena, node, style, fonts, images, &marker))
        return;
    static const tbox_string_view space = { " ", 1 };
    const tbox_font_face *face          = marker.face;
    double gap                          = tbox_font_measure_text_spaced(face, space, style->letter_spacing);
    double ascent                       = first_line != NULL ? first_line->ascent : tbox_font_face_ascent(face);
    double height                       = first_line != NULL ? first_line->height : tbox_layout_style_line_height(style, face);
    tbox_layout_text_run *run           = (tbox_layout_text_run *)tbox_vector_push(runs);
    run->font                           = face;
    run->style                          = marker.style;
    if (marker.image != NULL) {
        double width  = (double)marker.image->width, image_height = (double)marker.image->height;
        run->rect     = (tbox_rect){ content_x - width - gap, line_y + ascent - image_height, width, image_height };
        run->text     = tbox_string_view_make(NULL, 0);
        run->image    = marker.image;
        return;
    }
    double width = tbox_font_measure_text_spaced(face, marker.text, marker.style->letter_spacing);
    run->rect    = (tbox_rect){ content_x - width - gap, line_y + ascent - tbox_font_face_ascent(face), width, height };
    run->text    = marker.text;
    run->image   = NULL;
}
