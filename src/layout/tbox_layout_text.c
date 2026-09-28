#include "tbox_layout_internal.h"
#include <string.h>

bool tbox_layout_is_text_tag(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }
    if (tbox_layout_tag(node, "input"))
        return tbox_layout_input_is_text_tag(node);
    if (tbox_layout_is_select(node) || tbox_layout_is_textarea(node))
        return true;

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "pre", "td", "th", "caption", "button" };
    tbox_string_view tag_name            = node->element.tag_name;
    for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, text_tags[i])) {
            return true;
        }
    }
    return false;
}

/* `<pre>`'s own word-collection function, called by
 * tbox_layout_build_text_runs INSTEAD of tbox_layout_collect_words (never
 * both -- <pre> has no list marker and no normal word-splitting; see
 * ARCHITECTURE.md "v11 -- Layout Tree -- <pre> (texto verbatim)"). Pulls
 * `node`'s ENTIRE flattened text via tbox_html_node_text_content (same
 * function any nested-inline text tag already uses -- entities still
 * decode, only structure/nested faces are lost, an accepted simplification,
 * see ARCHITECTURE.md's "Escopo") WITHOUT tbox_string_collapse_whitespace:
 * internal whitespace (runs of spaces, tabs) is preserved byte-for-byte.
 * Scans that text byte by byte for '\n': each physical line (the bytes
 * between two consecutive '\n's, or between the start/end and the nearest
 * '\n') becomes exactly ONE tbox_layout_word -- deliberately NOT split on
 * ' ' via tbox_layout_push_words, which is the whole point of `<pre>`: a run
 * of spaces inside one physical line must stay literal within that one
 * word's own text, never treated as separate word boundaries. Between two
 * physical lines, pushes a hard break (tbox_layout_push_hard_break, the
 * exact same function `<br>` uses -- no separate forced-break logic here).
 * A NULL `face` (unloadable font, same guard tbox_layout_push_words already
 * has) contributes no words at all rather than crashing on
 * tbox_font_measure_text(NULL, ...). */

void tbox_layout_collect_preformatted_words(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words) {
    const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
    if (face == NULL) {
        return;
    }

    static const tbox_string_view space = { " ", 1 };
    double space_width                  = tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing;

    tbox_string_view text = tbox_layout_transform_text(arena, tbox_html_node_text_content(arena, node), style->text_transform);

    size_t line_start = 0;
    for (size_t i = 0; i <= text.size; i++) {
        bool at_break = (i == text.size) || (text.data[i] == '\n');
        if (!at_break) {
            continue;
        }

        tbox_string_view line = tbox_string_view_make(text.data + line_start, i - line_start);

        tbox_layout_word *entry = tbox_layout_new_word(words);
        entry->text             = line;
        entry->face             = face;
        entry->style            = style;
        entry->width            = tbox_font_measure_text_spaced(face, line, style->letter_spacing);
        entry->space_width      = space_width;
        entry->image            = NULL;
        entry->image_height     = 0.0;
        entry->hard_break       = false;
        entry->no_space_before  = false;

        if (i < text.size) {
            /* A real '\n' (not the end-of-text sentinel iteration) -- more
             * physical lines follow, so close this one with a hard break. */
            tbox_layout_push_hard_break(words, face, style);
        }

        line_start = i + 1;
    }
}

/* Builds `box`'s text_runs/text_run_count (a leaf box, one of the fixed
 * text tags) and returns its content-box height: the sum of every line's
 * height (replaces v0/v1's single fixed line height), or, for a
 * box whose collected words come out empty (no words at all -- an empty tag
 * after whitespace-collapsing, or every child skipped), the box's OWN
 * face's line-height alone, matching v0/v1's choice to never collapse an
 * empty text box's height to 0.
 *
 * `<pre>` (`is_preformatted`) takes a whole separate word-
 * collection path (tbox_layout_collect_preformatted_words, never
 * tbox_layout_push_list_marker + tbox_layout_collect_words -- <pre> has no
 * list marker and no normal word/whitespace collapsing to speak of) and is
 * passed to tbox_layout_break_lines as `no_wrap` (only `<pre>` disables
 * width-based wrapping; every other text tag keeps wrapping exactly as
 * before). Also once a line's runs are built, when
 * `style->text_align` isn't the initial LEFT, that line's just-pushed runs
 * (the `[runs_before, runs_after)` range -- tbox_layout_build_line_runs
 * itself never shifts anything, see ARCHITECTURE.md "v11 -- Layout Tree --
 * aplicação de text-align") are shifted right by the line's leftover
 * width, split evenly for CENTER or given wholly to the left gap for RIGHT
 * -- computed straight from the LAST run just pushed (`rect.x + rect.width`
 * minus `content_x` is exactly this line's rendered width, since runs on
 * one line are laid out left-to-right with no gaps between them and
 * `content_x`). A negative/zero offset (an overflowing line, wider than
 * `available_width`) is left alone -- same "never shift left" policy D4
 * already has for overflow.
 *
 * `node` may now be NULL (an anonymous box, see
 * tbox_layout_build_anonymous_box -- no real element to speak of), in which
 * case `is_preformatted` is forced false (a <pre> tag can't exist without a
 * node) and tbox_layout_push_list_marker is skipped entirely (a <li> marker
 * makes no sense without a real <li> node either). `first_sibling`/
 * `end_exclusive` replace the implicit `node->first_child`/NULL range
 * tbox_layout_collect_words used to derive on its own -- the only pre-v14
 * call site (inside tbox_layout_build_element, for a real text-tag element)
 * passes (node, node->first_child, NULL, ...), behavior identical to
 * before. */

double tbox_layout_build_text_runs(tbox_arena *arena, const tbox_html_node *node, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double available_width, tbox_layout_box *box, const tbox_layout_positioned_context *context) {
    /* <pre> keeps its own verbatim path (whole-subtree text in the <pre>'s
     * face) for AUTO/`pre`; any other white-space mode on a <pre> goes
     * through the general per-node collection like every other element. */
    bool is_preformatted = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "pre") && (style->white_space == TBOX_STYLE_WHITE_SPACE_AUTO || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE);
    bool is_input        = node != NULL && tbox_string_view_equal_cstr(node->element.tag_name, "input");
    bool is_select       = tbox_layout_is_select(node) || tbox_layout_is_textarea(node);

    tbox_vector words;
    tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
    if (is_input) {
        tbox_layout_input_push_value_word(arena, node, style, fonts, &words);
    } else if (is_select) {
        /* The selected label is supplied by Context after layout; option
         * descendants do not create boxes or contribute to select height. */
    } else if (is_preformatted) {
        tbox_layout_collect_preformatted_words(arena, node, style, fonts, &words);
    } else {
        if (node != NULL) {
            tbox_layout_push_list_marker(arena, node, style, fonts, &words);
        }
        tbox_layout_collect_words(arena, first_sibling, end_exclusive, style, styles, fonts, images, available_width, &words);
    }

    size_t word_count = tbox_vector_length(&words);
    if (word_count == 0) {
        box->text_runs      = NULL;
        box->text_run_count = 0;

        const tbox_font_face *own_face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
        return own_face != NULL ? tbox_layout_style_line_height(style, own_face) : 0.0;
    }

    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_line), 0);
    double indent = style->text_indent.kind == TBOX_STYLE_LENGTH_PX ? style->text_indent.value : style->text_indent.kind == TBOX_STYLE_LENGTH_PERCENT ? available_width * style->text_indent.value / 100.0 : 0.0;
    bool no_wrap  = is_preformatted || is_input || tbox_layout_white_space_nowrap(style);
    if (!no_wrap)
        tbox_layout_split_overlong_words(arena, &words, available_width, indent);
    const tbox_layout_word *word_items = (const tbox_layout_word *)words.data;
    word_count                         = tbox_vector_length(&words);
    const tbox_font_face *block_face   = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
    tbox_layout_break_lines(word_items, word_count, available_width, indent, no_wrap, block_face, &lines);

    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);

    size_t line_count                  = tbox_vector_length(&lines);
    const tbox_layout_line *line_items = (const tbox_layout_line *)lines.data;

    double cumulative_y = content_y;
    double total_height = 0.0;
    for (size_t li = 0; li < line_count; li++) {
        const tbox_layout_line *line = &line_items[li];

        size_t runs_before = tbox_vector_length(&runs);
        double justify_gap = 0.0;
        if (style->text_align == TBOX_STYLE_TEXT_ALIGN_JUSTIFY && !no_wrap && li + 1 < line_count && !(line->end < word_count && word_items[line->end].hard_break)) {
            double used = li == 0 ? indent : 0.0;
            size_t gaps = 0;
            for (size_t w = line->start; w < line->end; w++) {
                used += word_items[w].width;
                if (w != line->start)
                    used += word_items[w].space_width;
                if (w != line->start && word_items[w].space_width > 0.0)
                    gaps++;
            }
            if (gaps > 0 && used < available_width)
                justify_gap = (available_width - used) / (double)gaps;
        }
        double line_used = tbox_layout_build_line_runs(arena, word_items, line, cumulative_y, content_x + (li == 0 ? indent : 0.0), block_face, justify_gap, &runs);
        if (style->text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS && tbox_layout_white_space_nowrap(style) && style->overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN && !is_input && !is_select) {
            const tbox_font_face *face = tbox_font_face_cache_get(fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
            tbox_layout_ellipsize_line(&runs, runs_before, content_x, available_width, style, face, line, cumulative_y);
        }
        size_t runs_after = tbox_vector_length(&runs);

        bool line_has_atomic = false;
        for (size_t w = line->start; w < line->end; w++)
            if (word_items[w].atomic != NULL)
                line_has_atomic = true;
        if ((style->text_align == TBOX_STYLE_TEXT_ALIGN_CENTER || style->text_align == TBOX_STYLE_TEXT_ALIGN_RIGHT) && (runs_after > runs_before || line_has_atomic)) {
            tbox_layout_text_run *run_items = (tbox_layout_text_run *)runs.data;
            double line_width               = (li == 0 ? indent : 0.0) + line_used;
            if (runs_after > runs_before) {
                const tbox_layout_text_run *last_run = &run_items[runs_after - 1];
                double run_right                     = (last_run->rect.x + last_run->rect.width) - content_x;
                if (run_right > line_width)
                    line_width = run_right; /* an ellipsis mark */
            }

            double offset = (style->text_align == TBOX_STYLE_TEXT_ALIGN_CENTER) ? (available_width - line_width) / 2.0 : (available_width - line_width);

            if (offset > 0.0) {
                for (size_t ri = runs_before; ri < runs_after; ri++) {
                    run_items[ri].rect.x += offset;
                }
                for (size_t w = line->start; w < line->end; w++)
                    if (word_items[w].atomic != NULL)
                        tbox_layout_translate(word_items[w].atomic, offset, 0.0);
            }
        }

        cumulative_y += line->height;
        total_height += line->height;
    }

    box->text_runs      = (tbox_layout_text_run *)runs.data;
    box->text_run_count = runs.length;

    /* inline-blocks become this box's children, in document
     * order, so Render paints them and hit testing reaches them.
     * one whose absolute/fixed descendants resolve against a containing
     * block outside it (so moving it from the origin would misplace them)
     * is laid out again in place, against the real positioned context. */
    for (size_t w = 0; w < word_count; w++) {
        tbox_layout_box *atomic = word_items[w].atomic;
        if (atomic == NULL)
            continue;
        const tbox_html_node *atomic_node = word_items[w].atomic_node;
        const tbox_style *atomic_style    = tbox_layout_style_or_default(styles, atomic_node);
        if (context != NULL && tbox_layout_has_escaping_positioned(atomic_node, styles, atomic_style->position != TBOX_STYLE_POSITION_STATIC)) {
            tbox_layout_forced_size forced         = { true, false, atomic->border_box.width, 0.0 };
            tbox_layout_containing_block container = { atomic->margin_box.x, atomic->margin_box.y, word_items[w].atomic_containing_width, 0.0, false };
            atomic                                 = tbox_layout_build_element_sized(arena, atomic_node, styles, fonts, images, container, atomic->margin_box.y, *context, NULL, 0, &forced);
        }
        atomic->parent = box;
        if (box->last_child == NULL)
            box->first_child = atomic;
        else
            box->last_child->next_sibling = atomic;
        box->last_child = atomic;
    }
    return total_height;
}
