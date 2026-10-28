#include "base/tbox_unicode.h"
#include "tbox_layout_internal.h"
#include <math.h>

/* Select punctuation followed by one typographic letter, keeping combining
 * marks with its base. Language-specific digraphs and emoji ZWJ sequences
 * still follow the rest of tbox's codepoint model. */
static size_t tbox_layout_initial_end(tbox_string_view text, bool *has_letter) {
    *has_letter = false;
    size_t at   = 0;
    uint32_t cp;
    while (at < text.size) {
        size_t next = tbox_utf8_decode(text.data, text.size, at, &cp);
        at          = next;
        if (!tbox_unicode_is_punctuation(cp)) {
            *has_letter = true;
            break;
        }
    }
    while (at < text.size) {
        size_t next = tbox_utf8_decode(text.data, text.size, at, &cp);
        if (!tbox_unicode_is_combining_mark(cp))
            break;
        at = next;
    }
    return at;
}

static tbox_layout_box *tbox_layout_initial_box(tbox_arena *arena, const tbox_html_node *owner, bool pseudo_element, tbox_string_view text, tbox_style *letter, const tbox_style *block, tbox_font_face_cache *fonts, double width) {
    const tbox_font_face *face       = tbox_layout_style_face(fonts, letter);
    const tbox_font_face *block_face = tbox_layout_style_face(fonts, block);
    if (face == NULL || block_face == NULL)
        return NULL;
    bool initial       = letter->initial_letter_size >= 1.0;
    double line_height = tbox_layout_style_line_height(block, block_face);
    double baseline    = tbox_font_face_ascent(block_face) + (line_height - tbox_font_face_line_height(block_face)) / 2.0;
    if (initial) {
        double cap    = tbox_font_face_cap_height(face);
        double target = (letter->initial_letter_size - 1.0) * line_height + tbox_font_face_cap_height(block_face);
        if (cap <= 0.0 || target <= 0.0)
            return NULL;
        double size = letter->font_size * target / cap;
        /* The face cache loads integer pixel sizes. Bound the computed size
         * before entering FreeType, including extreme but valid CSS numbers. */
        if (!isfinite(size) || size < 1.0 || size > 65535.0)
            return NULL;
        letter->font_size = size;
        face              = tbox_layout_style_face(fonts, letter);
        if (face == NULL)
            return NULL;
        letter->float_side = TBOX_STYLE_FLOAT_LEFT;
        letter->display    = TBOX_STYLE_DISPLAY_BLOCK;
        baseline += (letter->initial_letter_sink - 1.0) * line_height;
    }
    tbox_layout_box *box      = tbox_arena_alloc_zero(arena, sizeof(*box));
    tbox_html_node *pseudo    = pseudo_element ? tbox_arena_alloc_zero(arena, sizeof(*pseudo)) : NULL;
    tbox_layout_text_run *run = tbox_arena_alloc_zero(arena, sizeof(*run));
    if (box == NULL || (pseudo_element && pseudo == NULL) || run == NULL)
        return NULL;
    if (pseudo_element) {
        pseudo->type             = TBOX_HTML_NODE_ELEMENT;
        pseudo->element.tag_name = tbox_string_view_from_cstr("::first-letter");
        pseudo->parent           = (tbox_html_node *)owner;
    }
    letter->generated    = pseudo_element ? 3 : 0;
    letter->first_letter = NULL;
    box->node            = pseudo_element ? pseudo : owner;
    box->style           = letter;
    double margins[4], edges[4];
    for (size_t i = 0; i < 4; i++) {
        margins[i] = tbox_layout_resolve_edge(letter->margin[i], width);
        edges[i]   = tbox_layout_resolve_edge(letter->padding[i], width) + letter->border_widths[i];
    }
    double text_width    = tbox_font_measure_text_spaced(face, text, letter->letter_spacing);
    double height        = initial ? tbox_font_face_cap_height(face) : tbox_layout_style_line_height(letter, face);
    bool floating_letter = pseudo_element && !initial && letter->float_side != TBOX_STYLE_FLOAT_NONE;
    if (!initial && !floating_letter)
        height = fmax(height, letter->font_size);
    tbox_font_shape_into(arena, face, text, letter->letter_spacing, &run->glyphs, &run->glyph_count);
    double ink_top = 0.0, ink_bottom = 0.0;
    bool has_ink            = floating_letter && tbox_font_run_ink_bounds(face, run->glyphs, run->glyph_count, &ink_top, &ink_bottom);
    double top              = initial ? baseline - height : 0.0;
    box->content_box        = (tbox_rect){ margins[3] + edges[3], margins[0] + edges[0] + top, text_width, height };
    box->padding_box        = (tbox_rect){ margins[3] + letter->border_widths[3], margins[0] + letter->border_widths[0] + top, text_width + edges[1] + edges[3] - letter->border_widths[1] - letter->border_widths[3], height + edges[0] + edges[2] - letter->border_widths[0] - letter->border_widths[2] };
    box->border_box         = (tbox_rect){ margins[3], margins[0] + top, text_width + edges[1] + edges[3], height + edges[0] + edges[2] };
    double exclusion_height = initial ? letter->initial_letter_sink * line_height : box->border_box.height;
    box->margin_box         = (tbox_rect){ 0, 0, fmax(0.0, margins[3] + box->border_box.width + margins[1]), fmax(0.0, margins[0] + exclusion_height + margins[2]) };
    double run_y            = initial ? baseline - tbox_font_face_ascent(face) : (height - tbox_font_face_line_height(face)) / 2.0;
    if (has_ink) {
        /* Floating first letters sit by their ink at the paragraph top.
         * Keep the declared line-height box for placement/background, but
         * let text return below the actual letter (CSS 2.1 5.12.2). */
        run_y                 = -ink_top - tbox_font_face_ascent(face);
        box->float_ink_height = fmax(0.0, margins[0] + edges[0] + ink_bottom - ink_top + edges[2] + margins[2]);
    }
    run->rect           = (tbox_rect){ box->content_box.x, margins[0] + edges[0] + run_y, text_width, height };
    run->text           = text;
    run->font           = face;
    run->style          = letter;
    box->text_runs      = run;
    box->text_run_count = 1;
    return box;
}

void tbox_layout_prepare_initial(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, double width, tbox_vector *words) {
    size_t first            = 0;
    const tbox_layout_word *items = (const tbox_layout_word *)words->data;
    while (first < words->length && items[first].floating != NULL)
        first++;
    if (first == words->length || items[first].hard_break || items[first].image != NULL || items[first].atomic != NULL || items[first].text.size == 0)
        return;
    tbox_layout_word source          = items[first];
    const tbox_style *pseudo         = style->first_letter;
    const tbox_style *explicit_style = source.initial_style;
    bool explicit_inline             = explicit_style != NULL;
    if (pseudo == NULL && !explicit_inline && style->initial_letter_size < 1.0)
        return;
    tbox_style *letter = tbox_arena_alloc(arena, sizeof(*letter));
    if (letter == NULL)
        return;
    *letter = pseudo != NULL ? *pseudo : explicit_inline ? *explicit_style : tbox_style_resolve(arena, NULL, source.style, NULL);
    /* ::first-letter's style is in screen terms: inside a vertical
     * writing mode, turn it into the block's layout frame. */
    if (letter->layout_frame != style->layout_frame) {
        tbox_style physical = *letter;
        tbox_layout_frame_remap(arena, letter, &physical, style->layout_frame);
    }
    if (pseudo == NULL && !explicit_inline) {
        letter->initial_letter_size = style->initial_letter_size;
        letter->initial_letter_sink = style->initial_letter_sink;
    }
    size_t end = first + 1;
    bool has_letter;
    size_t selected       = tbox_layout_initial_end(source.text, &has_letter);
    tbox_string_view text = tbox_string_view_make(source.text.data, selected);
    if (!has_letter && !(explicit_inline && pseudo == NULL)) {
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, arena, text.size + 8);
        tbox_string_builder_append_view(&builder, text);
        while (!has_letter && end < words->length && !items[end].hard_break && items[end].atomic == NULL && items[end].floating == NULL && items[end].image == NULL) {
            source   = items[end++];
            selected = tbox_layout_initial_end(source.text, &has_letter);
            if (!source.no_space_before)
                tbox_string_builder_append_byte(&builder, ' ');
            tbox_string_builder_append_view(&builder, tbox_string_view_make(source.text.data, selected));
        }
        if (!has_letter)
            return;
        text = tbox_string_builder_finish(&builder);
    }
    if (explicit_inline && pseudo == NULL) {
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, arena, source.text.size);
        tbox_string_builder_append_view(&builder, source.text);
        while (end < words->length && items[end].initial_style == explicit_style && !items[end].hard_break && items[end].floating == NULL && items[end].atomic == NULL && items[end].image == NULL) {
            if (!items[end].no_space_before)
                tbox_string_builder_append_byte(&builder, ' ');
            tbox_string_builder_append_view(&builder, items[end].text);
            end++;
        }
        text     = tbox_string_builder_finish(&builder);
        selected = source.text.size;
    }
    tbox_layout_box *box = tbox_layout_initial_box(arena, explicit_inline && pseudo == NULL ? source.initial_node : node, !explicit_inline || pseudo != NULL, text, letter, style, fonts, width);
    if (box == NULL)
        return;
    tbox_vector expanded;
    tbox_vector_init(&expanded, words->arena, sizeof(tbox_layout_word), words->length + 1);
    for (size_t i = 0; i < first; i++)
        *(tbox_layout_word *)tbox_vector_push(&expanded) = items[i];
    tbox_layout_word *word = tbox_layout_new_word(&expanded);
    word->face             = source.face;
    word->style            = style;
    word->no_space_before  = source.no_space_before;
    word->space_width      = source.space_width;
    if (letter->float_side != TBOX_STYLE_FLOAT_NONE) {
        word->floating = box;
    } else {
        word->atomic        = box;
        word->width         = box->margin_box.width;
        word->image_height  = box->margin_box.height;
        word->atomic_ascent = box->text_runs[0].rect.y + tbox_font_face_ascent(box->text_runs[0].font);
    }
    if (selected < source.text.size) {
        source.text.data += selected;
        source.text.size -= selected;
        source.width                                     = tbox_font_measure_text_spaced(source.face, source.text, source.style->letter_spacing);
        source.space_width                               = 0.0;
        source.no_space_before                           = true;
        *(tbox_layout_word *)tbox_vector_push(&expanded) = source;
    }
    for (size_t i = end; i < words->length; i++)
        *(tbox_layout_word *)tbox_vector_push(&expanded) = items[i];
    *words = expanded;
}

/* Only text at the actual edge may hang; an image, inline-block or float
 * never acquires typographic punctuation widths. */
double tbox_layout_hang_first(const tbox_layout_word *word) {
    if (word->text.size == 0 || word->atomic != NULL || word->image != NULL || !(word->style->hanging_punctuation & TBOX_STYLE_HANG_FIRST))
        return 0.0;
    uint32_t cp;
    size_t next = tbox_utf8_decode(word->text.data, word->text.size, 0, &cp);
    if (!tbox_unicode_is_open_punctuation(cp) && !tbox_unicode_is_quote(cp) && cp != 0x2014 && cp != 0x3000)
        return 0.0;
    return tbox_text_prefix_width(word->face, word->style->letter_spacing, word->text, next);
}

double tbox_layout_hang_end(const tbox_layout_word *word, bool last, bool overflow) {
    if (word->text.size == 0 || word->atomic != NULL || word->image != NULL)
        return 0.0;
    size_t at = tbox_utf8_previous(word->text.data, word->text.size);
    uint32_t cp;
    tbox_utf8_decode(word->text.data, word->text.size, at, &cp);
    unsigned bits = word->style->hanging_punctuation;
    bool closing  = last && (bits & TBOX_STYLE_HANG_LAST) && (tbox_unicode_is_close_punctuation(cp) || tbox_unicode_is_quote(cp));
    bool stop     = cp == '.' || cp == ',' || cp == 0x60C || cp == 0x6D4 || cp == 0x3001 || cp == 0x3002 || cp == 0xFF0C || cp == 0xFF0E || cp == 0xFE50 || cp == 0xFE51 || cp == 0xFE52 || cp == 0xFF61 || cp == 0xFF64;
    if (!closing && !(stop && ((bits & TBOX_STYLE_HANG_FORCE_END) || (overflow && (bits & TBOX_STYLE_HANG_ALLOW_END)))))
        return 0.0;
    return tbox_font_measure_text_spaced(word->face, tbox_string_view_make(word->text.data + at, word->text.size - at), word->style->letter_spacing);
}
