#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* ---- intrinsic sizes (min-content / max-content widths) ----
 *
 * What a box needs horizontally when laid out without a width to fill:
 * `max` is its widest line when nothing wraps except at forced breaks,
 * `min` its widest unbreakable piece (a word, an image, a nested fixed or
 * min-content width). `overflow-wrap: anywhere` can reduce that piece to
 * its widest codepoint. Used for inline-block shrink-to-fit and flex base
 * sizes. Approximations: percentages (widths, padding, margins, min/max)
 * count as 0, `word-break: break-all` still measures whole words, and a
 * table sums its widest row's cells. */

tbox_layout_intrinsic tbox_layout_intrinsic_outer(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images);
bool tbox_layout_tag(const tbox_html_node *node, const char *name);

/* Min/max widths of a word sequence: lines end only at hard breaks; a
 * piece stays unbreakable across words glued without a space. `no_wrap`
 * (nowrap/pre text) makes every line unbreakable. */
tbox_layout_intrinsic tbox_layout_measure_words(const tbox_layout_word *words, size_t count, bool no_wrap) {
    tbox_layout_intrinsic result = { 0.0, 0.0 };
    double line = 0.0, piece = 0.0;
    bool line_start = true;
    for (size_t i = 0; i < count; i++) {
        const tbox_layout_word *word = &words[i];
        if (word->hard_break) {
            line = piece = 0.0;
            line_start   = true;
            continue;
        }
        double space     = line_start ? 0.0 : word->space_width;
        double min_width = word->has_min_width ? word->min_width : word->width;
        bool anywhere = !no_wrap && word->style != NULL && word->style->overflow_wrap_anywhere &&
                        !word->hard_break && word->image == NULL && word->atomic == NULL &&
                        word->face != NULL && word->text.size > 0;
        if (anywhere) {
            min_width = 0.0;
            for (size_t start = 0; start < word->text.size;) {
                size_t end = start + 1;
                while (end < word->text.size && ((unsigned char)word->text.data[end] & 0xc0) == 0x80) end++;
                double width = tbox_font_measure_text_spaced(word->face,
                    tbox_string_view_make(word->text.data + start, end - start), word->style->letter_spacing);
                if (width > min_width) min_width = width;
                start = end;
            }
        }
        line += space + word->width;
        if (no_wrap || (!anywhere && !line_start && word->no_space_before))
            piece += space + min_width;
        else
            piece = min_width;
        line_start = false;
        if (line > result.max)
            result.max = line;
        if (piece > result.min)
            result.min = piece;
    }
    if (result.min > result.max)
        result.max = result.min;
    return result;
}

static double tbox_layout_px_or_zero(tbox_style_length length) {
    return length.kind == TBOX_STYLE_LENGTH_PX ? length.value : 0.0;
}

/* The content-box min/max widths of `node`'s own content. */
tbox_layout_intrinsic tbox_layout_intrinsic_content(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    tbox_layout_intrinsic result = { 0.0, 0.0 };
    const tbox_style *style      = tbox_layout_style_or_default(styles, node);
    const tbox_font_face *face   = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);

    if (tbox_layout_is_replaced_image(node)) {
        const tbox_html_attribute *src = tbox_html_node_get_attribute(node, tbox_string_view_make("src", 3));
        const tbox_image *image        = src != NULL ? tbox_image_cache_get(images, src->value) : NULL;
        result.min = result.max = image != NULL ? (double)image->width : 16.0;
        /* A px height with an auto width keeps the image's aspect ratio. */
        if (image != NULL && image->height > 0 && style->height.kind == TBOX_STYLE_LENGTH_PX) {
            double height = style->height.value;
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                height -= tbox_layout_px_or_zero(style->padding[0]) + tbox_layout_px_or_zero(style->padding[2]) + tbox_style_border_side_width(style, 0) + tbox_style_border_side_width(style, 2);
            result.min = result.max = (height > 0.0 ? height : 0.0) * image->width / image->height;
        }
        return result;
    }
    if (tbox_layout_tag(node, "textarea")) {
        if (face != NULL)
            result.min = result.max = tbox_layout_textarea_size(node, "cols", 20) * tbox_font_measure_text(face, tbox_string_view_make("0", 1));
        return result;
    }
    if (tbox_layout_tag(node, "tr")) {
        for (const tbox_html_node *cell = node->first_child; cell != NULL; cell = cell->next_sibling) {
            if (cell->type != TBOX_HTML_NODE_ELEMENT)
                continue;
            tbox_layout_intrinsic outer = tbox_layout_intrinsic_outer(arena, cell, styles, fonts, images);
            result.min += outer.min;
            result.max += outer.max;
        }
        return result;
    }

    /* a row sums its items (a nowrap row also sums their minimums);
     * a column, like a block, takes the widest item. */
    if ((style->display == TBOX_STYLE_DISPLAY_FLEX || style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) && !tbox_layout_tag(node, "input") && !tbox_layout_tag(node, "select")) {
        bool row     = style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW || style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE;
        bool wrap    = style->flex_wrap != TBOX_STYLE_FLEX_WRAP_NOWRAP;
        double gap   = row && style->column_gap.kind == TBOX_STYLE_LENGTH_PX ? style->column_gap.value : 0.0;
        size_t items = 0;
        for (const tbox_html_node *child = node->first_child; child != NULL;) {
            tbox_layout_intrinsic contribution = { 0.0, 0.0 };
            bool is_item                       = false;
            if (child->type == TBOX_HTML_NODE_TEXT) {
                const tbox_html_node *end = child;
                while (end != NULL && end->type != TBOX_HTML_NODE_ELEMENT)
                    end = end->next_sibling;
                tbox_vector words;
                tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
                tbox_layout_inline_state state = { false, false };
                tbox_layout_collect_words_in(arena, child, end, style, styles, fonts, images, 0.0, false, true, &state, &words);
                if (words.length > 0) {
                    contribution = tbox_layout_measure_words((const tbox_layout_word *)words.data, words.length, tbox_layout_white_space_nowrap(style));
                    is_item      = true;
                }
                child = end;
            } else {
                if (child->type == TBOX_HTML_NODE_ELEMENT && !tbox_layout_is_hidden_input(child)) {
                    const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
                    if (child_style->display != TBOX_STYLE_DISPLAY_NONE && child_style->position != TBOX_STYLE_POSITION_ABSOLUTE && child_style->position != TBOX_STYLE_POSITION_FIXED) {
                        contribution = tbox_layout_intrinsic_outer(arena, child, styles, fonts, images);
                        is_item      = true;
                    }
                }
                child = child->next_sibling;
            }
            if (!is_item)
                continue;
            if (row) {
                result.max += contribution.max + (items > 0 ? gap : 0.0);
                if (wrap) {
                    if (contribution.min > result.min)
                        result.min = contribution.min;
                } else {
                    result.min += contribution.min + (items > 0 ? gap : 0.0);
                }
            } else {
                if (contribution.min > result.min)
                    result.min = contribution.min;
                if (contribution.max > result.max)
                    result.max = contribution.max;
            }
            items++;
        }
        return result;
    }

    if (tbox_layout_is_text_tag(node)) {
        tbox_vector words;
        tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
        bool is_pre = tbox_layout_tag(node, "pre") && (style->white_space == TBOX_STYLE_WHITE_SPACE_AUTO || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE);
        if (is_pre) {
            tbox_layout_collect_preformatted_words(arena, node, style, fonts, &words);
        } else {
            tbox_layout_push_list_marker(arena, node, style, fonts, &words);
            tbox_layout_inline_state state = { false, false };
            tbox_layout_collect_words_in(arena, node->first_child, NULL, style, styles, fonts, images, 0.0, false, true, &state, &words);
        }
        return tbox_layout_measure_words((const tbox_layout_word *)words.data, words.length, is_pre || tbox_layout_white_space_nowrap(style));
    }

    /* Block container: loose inline content measures like an anonymous
     * box; each in-flow element child contributes its outer size. */
    for (const tbox_html_node *child = node->first_child; child != NULL;) {
        if (child->type != TBOX_HTML_NODE_ELEMENT && child->type != TBOX_HTML_NODE_TEXT) {
            child = child->next_sibling;
            continue;
        }
        if (tbox_layout_is_inline_run_trigger(arena, child, styles)) {
            const tbox_html_node *run_end = tbox_layout_inline_run_end(child, styles);
            tbox_vector words;
            tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
            tbox_layout_inline_state state = { false, false };
            tbox_layout_collect_words_in(arena, child, run_end, style, styles, fonts, images, 0.0, false, true, &state, &words);
            tbox_layout_intrinsic run = tbox_layout_measure_words((const tbox_layout_word *)words.data, words.length, tbox_layout_white_space_nowrap(style));
            if (run.min > result.min)
                result.min = run.min;
            if (run.max > result.max)
                result.max = run.max;
            child = run_end;
            continue;
        }
        if (child->type == TBOX_HTML_NODE_ELEMENT && !tbox_layout_is_hidden_input(child)) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            if (child_style->display != TBOX_STYLE_DISPLAY_NONE && child_style->position != TBOX_STYLE_POSITION_ABSOLUTE && child_style->position != TBOX_STYLE_POSITION_FIXED) {
                tbox_layout_intrinsic outer = tbox_layout_intrinsic_outer(arena, child, styles, fonts, images);
                if (outer.min > result.min)
                    result.min = outer.min;
                if (outer.max > result.max)
                    result.max = outer.max;
            }
        }
        child = child->next_sibling;
    }
    return result;
}

/* `node`'s min/max contribution to its parent: content plus padding,
 * border and px margins, with a px `width` and px min/max-width applied. */
tbox_layout_intrinsic tbox_layout_intrinsic_outer(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    const tbox_style *style      = tbox_layout_style_or_default(styles, node);
    tbox_layout_intrinsic result = { 0.0, 0.0 };
    if (style->display == TBOX_STYLE_DISPLAY_NONE)
        return result;
    double edges    = tbox_layout_px_or_zero(style->padding[1]) + tbox_layout_px_or_zero(style->padding[3]) + tbox_style_border_side_width(style, 1) + tbox_style_border_side_width(style, 3);
    double margins  = tbox_layout_px_or_zero(style->margin[1]) + tbox_layout_px_or_zero(style->margin[3]);
    bool border_box = style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX;

    if (style->width.kind == TBOX_STYLE_LENGTH_PX) {
        double width = style->width.value - (border_box ? edges : 0.0);
        result.min = result.max = width > 0.0 ? width : 0.0;
    } else {
        result = tbox_layout_intrinsic_content(arena, node, styles, fonts, images);
    }
    if (style->max_width.kind == TBOX_STYLE_LENGTH_PX) {
        double maximum = style->max_width.value - (border_box ? edges : 0.0);
        if (result.max > maximum)
            result.max = maximum;
        if (result.min > maximum)
            result.min = maximum;
    }
    if (style->min_width.kind == TBOX_STYLE_LENGTH_PX) {
        double minimum = style->min_width.value - (border_box ? edges : 0.0);
        if (result.max < minimum)
            result.max = minimum;
        if (result.min < minimum)
            result.min = minimum;
    }
    if (result.min < 0.0)
        result.min = 0.0;
    if (result.max < result.min)
        result.max = result.min;
    result.min += edges + margins;
    result.max += edges + margins;
    return result;
}

/* The baseline of a laid-out inline-block, from its margin box top: the
 * baseline of its last text line (the lowest text run in its subtree), or
 * its bottom margin edge when it has none or clips its overflow (CSS2.1
 * 10.8.1). */
static void tbox_layout_last_baseline(const tbox_layout_box *box, double *best_y, double *baseline) {
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->image != NULL || run->font == NULL || run->rect.y < *best_y)
            continue;
        *best_y   = run->rect.y;
        *baseline = run->rect.y + tbox_font_face_ascent(run->font);
    }
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        tbox_layout_last_baseline(child, best_y, baseline);
}

/* pushes an `inline-block` as one atomic word. Its width is the
 * declared one or, when auto, shrink-to-fit: min(max(min-content,
 * available), max-content) per CSS2.1 10.3.9. The box is built at the
 * origin (tbox_layout_build_line_runs moves it onto its line afterwards);
 * absolute/fixed descendants whose containing block lies outside it are not
 * supported and resolve against that provisional origin. */
void tbox_layout_push_atomic_word(tbox_arena *arena, const tbox_html_node *node, const tbox_style *node_style, const tbox_font_face *context_face, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, bool measure_only, tbox_vector *words) {
    tbox_layout_intrinsic outer = tbox_layout_intrinsic_outer(arena, node, styles, fonts, images);
    tbox_layout_word *word      = tbox_layout_new_word(words);
    word->face                  = context_face;
    word->style                 = node_style;
    word->space_width           = context_face != NULL ? tbox_font_measure_text_spaced(context_face, tbox_string_view_make(" ", 1), node_style->letter_spacing) + node_style->word_spacing : 0.0;
    word->has_min_width         = true;
    word->min_width             = outer.min;
    word->width                 = outer.max;
    if (measure_only)
        return;

    tbox_layout_forced_size forced = { 0 };
    if (node_style->width.kind == TBOX_STYLE_LENGTH_AUTO) {
        double margins   = tbox_layout_resolve_edge(node_style->margin[1], containing_width) + tbox_layout_resolve_edge(node_style->margin[3], containing_width);
        double available = containing_width - margins;
        double width     = outer.max - margins;
        if (width > available)
            width = available;
        if (width < outer.min - margins)
            width = outer.min - margins;
        forced.has_width = true;
        forced.width     = width > 0.0 ? width : 0.0;
    }
    tbox_layout_containing_block container = { 0.0, 0.0, containing_width, 0.0, false };
    tbox_rect origin                       = { 0.0, 0.0, containing_width, 0.0 };
    tbox_layout_positioned_context context = { origin, origin };
    tbox_layout_box *box                   = tbox_layout_build_element_sized(arena, node, styles, fonts, images, container, 0.0, context, NULL, 0, forced.has_width ? &forced : NULL);

    double best_y = -1e300, baseline = box->margin_box.y + box->margin_box.height;
    if (node_style->overflow_y == TBOX_STYLE_OVERFLOW_Y_VISIBLE)
        tbox_layout_last_baseline(box, &best_y, &baseline);
    word->atomic                  = box;
    word->atomic_node             = node;
    word->atomic_containing_width = containing_width;
    word->width                   = box->margin_box.width;
    word->image_height            = box->margin_box.height;
    word->atomic_ascent           = baseline - box->margin_box.y;
}
