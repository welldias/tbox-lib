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

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p", "li", "pre", "td", "th", "caption", "button", "progress", "meter", "summary", "ruby" };
    tbox_string_view tag_name            = node->element.tag_name;
    for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, text_tags[i])) {
            return true;
        }
    }
    return false;
}

/* A box laid out as one inline formatting context: a text tag, or an
 * element with `display: list-item` whose children are all inline (its
 * marker then joins the text like an <li>'s). */
bool tbox_layout_is_text_box(const tbox_html_node *node, const tbox_style *style) {
    if (tbox_layout_is_text_tag(node))
        return true;
    if (style == NULL || !style->display_list_item || node->type != TBOX_HTML_NODE_ELEMENT)
        return false;
    static const char *const blocks[] = { "div", "p", "ul", "ol", "li", "table", "section", "article", "header", "footer", "nav", "aside", "main", "form", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "blockquote", "hr", "dl", "fieldset", "figure" };
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
        if (child->type == TBOX_HTML_NODE_ELEMENT)
            for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++)
                if (tbox_string_view_equal_cstr(child->element.tag_name, blocks[i]))
                    return false;
    return true;
}

/* An in-flow block-level child: a box of its own in a block container. */
static bool tbox_layout_is_block_child(const tbox_html_node *child, const tbox_style_table *styles) {
    if (child->type != TBOX_HTML_NODE_ELEMENT || tbox_layout_is_hidden_input(child) || tbox_layout_tag(child, "br"))
        return false;
    const tbox_style *style = tbox_layout_style_or_default(styles, child);
    return (style->display == TBOX_STYLE_DISPLAY_BLOCK || style->display == TBOX_STYLE_DISPLAY_FLEX || style->display == TBOX_STYLE_DISPLAY_GRID) &&
           style->position != TBOX_STYLE_POSITION_ABSOLUTE && style->position != TBOX_STYLE_POSITION_FIXED &&
           !tbox_layout_is_float(child, style); /* a float stays among the words */
}

/* A text box laid out as one inline formatting context. A paragraph-like
 * text tag (p, h1-h6, li, td, th, caption, summary) with an in-flow block
 * child -- a <div> in a <p>, a nested list in an <li> -- is a block
 * container instead: its loose inline content goes into anonymous boxes
 * around the block (CSS 2.1 section 9.2.1.1). Form controls, <pre>, <ruby>
 * and gauges always stay text boxes. */
bool tbox_layout_is_inline_box(const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles) {
    if (!tbox_layout_is_text_box(node, style))
        return false;
    static const char *const splittable[] = { "p", "h1", "h2", "h3", "h4", "h5", "h6", "li", "td", "th", "caption", "summary" };
    bool can_split = false;
    for (size_t i = 0; i < sizeof(splittable) / sizeof(splittable[0]) && !can_split; i++)
        can_split = tbox_layout_tag(node, splittable[i]);
    if (!can_split)
        return true;
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
        if (tbox_layout_is_block_child(child, styles))
            return false;
    return true;
}

/* `<pre>`'s own word-collection function, called by
 * tbox_layout_build_text_runs INSTEAD of tbox_layout_collect_words (never
 * both -- <pre> has no list marker and no normal word-splitting; see
 * docs/dev/historico.md "v11 -- Layout Tree -- <pre> (texto verbatim)"). Pulls
 * `node`'s ENTIRE flattened text via tbox_html_node_text_content (same
 * function any nested-inline text tag already uses -- entities still
 * decode, only structure/nested faces are lost, an accepted simplification,
 * see docs/dev/historico.md's "Escopo") WITHOUT tbox_string_collapse_whitespace:
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
    const tbox_font_face *face = tbox_layout_style_face(fonts, style);
    if (face == NULL) {
        return;
    }

    static const tbox_string_view space = { " ", 1 };
    double space_width                  = tbox_font_measure_text_spaced(face, space, style->letter_spacing) + style->word_spacing;

    tbox_string_view text = tbox_layout_expand_tabs(arena, tbox_layout_transform_text(arena, tbox_html_node_text_content(arena, node), style->text_transform), style, face);

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

/* Positioned descendants do not become words. Walk the same inline
 * subtree after line layout, preserving independent clipping and hit tests.
 * Atomic inline boxes and floats already lay out their own descendants. */
static void tbox_layout_inline_positioned(tbox_arena *arena, const tbox_html_node *first,
    const tbox_html_node *end, const tbox_style_table *styles, tbox_font_face_cache *fonts,
    tbox_image_cache *images, tbox_layout_box *parent, const tbox_layout_positioned_context *context,
    bool inside_inline) {
    if (context == NULL) return;
    for (const tbox_html_node *node = first; node != end && node != NULL; node = node->next_sibling) {
        if (node->type != TBOX_HTML_NODE_ELEMENT || tbox_layout_is_hidden_input(node)) continue;
        const tbox_style *style = tbox_layout_style_or_default(styles, node);
        if (style->display == TBOX_STYLE_DISPLAY_NONE) continue;
        if (style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED) {
            tbox_rect basis = style->position == TBOX_STYLE_POSITION_FIXED ? context->viewport : context->nearest_ancestor;
            tbox_layout_containing_block container = { basis.x, basis.y, basis.width, basis.height, true };
            tbox_layout_box *child = tbox_layout_build_element(arena, node, styles, fonts, images, container, basis.y, *context, NULL, 0);
            if (child == NULL) continue;
            child->parent = parent;
            if (parent->last_child == NULL) parent->first_child = child;
            else parent->last_child->next_sibling = child;
            parent->last_child = child;
        } else if (!tbox_layout_is_float(node, style) &&
                   style->display != TBOX_STYLE_DISPLAY_INLINE_BLOCK &&
                   style->display != TBOX_STYLE_DISPLAY_INLINE_FLEX &&
                   style->display != TBOX_STYLE_DISPLAY_INLINE_GRID &&
                   (style->display == TBOX_STYLE_DISPLAY_INLINE || inside_inline)) {
            tbox_layout_inline_positioned(arena, node->first_child, NULL, styles, fonts, images, parent, context, true);
        }
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
 * itself never shifts anything, see docs/dev/historico.md "v11 -- Layout Tree --
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

double tbox_layout_build_text_runs(tbox_arena *arena, const tbox_html_node *node, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double available_width, tbox_layout_box *box, const tbox_layout_positioned_context *context, const tbox_html_node *marker_node, const tbox_style *marker_style) {
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
        if (marker_node != NULL) {
            tbox_layout_push_list_marker(arena, marker_node, marker_style, fonts, images, &words);
        }
        tbox_layout_collect_words(arena, first_sibling, end_exclusive, style, styles, fonts, images, available_width, &words);
    }

    if (!is_input && !is_select && !is_preformatted)
        tbox_layout_prepare_initial(arena, node != NULL ? node : first_sibling != NULL ? first_sibling->parent : NULL, style, fonts, available_width, &words);
    size_t word_count = tbox_vector_length(&words);
    if (word_count == 0) {
        tbox_vector marker_runs;
        tbox_vector_init(&marker_runs, arena, sizeof(tbox_layout_text_run), 0);
        if (!is_preformatted && !is_input && !is_select)
            tbox_layout_append_outside_marker(arena, marker_node, marker_style, fonts, images, content_x, content_y, NULL, &marker_runs);
        box->text_runs      = marker_runs.length > 0 ? (tbox_layout_text_run *)marker_runs.data : NULL;
        box->text_run_count = marker_runs.length;

        if (!is_input && !is_select && !is_preformatted)
            tbox_layout_inline_positioned(arena, first_sibling, end_exclusive, styles, fonts, images, box, context, false);
        const tbox_font_face *own_face = tbox_layout_style_face(fonts, style);
        return own_face != NULL ? tbox_layout_style_line_height(style, own_face) : 0.0;
    }

    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_line), 0);
    double indent = style->text_indent.kind == TBOX_STYLE_LENGTH_PX ? style->text_indent.value : style->text_indent.kind == TBOX_STYLE_LENGTH_PERCENT ? tbox_style_length_resolve(style->text_indent, available_width) : 0.0;
    /* text-indent: hanging inverts it (every line but the first moves in,
     * so the text area narrows and the first line reaches back out);
     * each-line also indents lines that follow a forced break. */
    double hang       = style->text_indent_hanging ? indent : 0.0;
    double text_x     = content_x + hang;
    double text_width = available_width - hang;
    if (style->text_indent_hanging)
        indent = -indent;
    bool no_wrap  = is_preformatted || is_input || tbox_layout_white_space_nowrap(style);
    if (!no_wrap)
        tbox_layout_split_overlong_words(arena, &words, text_width, indent);
    const tbox_layout_word *word_items = (const tbox_layout_word *)words.data;
    word_count                         = tbox_vector_length(&words);
    const tbox_font_face *block_face   = tbox_layout_style_face(fonts, style);
    /* Bidi levels over the final words (after the overlong splits). */
    const tbox_layout_word_bidi *bidi = tbox_layout_resolve_bidi(arena, word_items, word_count, style);
    /* Floats around (or among the words) make each line's room depend on
     * where it sits; without them lines keep the whole width. */
    tbox_layout_floats *floats = context != NULL ? context->floats : NULL;
    bool float_words           = false;
    for (size_t w = 0; w < word_count && !float_words; w++)
        float_words = word_items[w].floating != NULL;
    if (float_words && floats == NULL)
        floats = tbox_layout_floats_create(arena);
    tbox_layout_line_space space = {
        .floats      = floats,
        .x           = text_x,
        .width       = text_width,
        .y           = content_y,
        .line_height = block_face != NULL ? tbox_layout_style_line_height(style, block_face) : 0.0,
        .float_x     = content_x,
        .float_width = available_width,
        .images      = images,
        .arena       = arena,
    };
    bool use_space = floats != NULL && (float_words || !tbox_layout_floats_empty(floats));
    tbox_layout_break_lines(word_items, word_count, text_width, indent, no_wrap, style->text_indent_each_line, block_face, use_space ? &space : NULL, &lines);
    /* text-wrap: balance -- the narrowest width that keeps the same line
     * count, so lines come out about equally long (alignment still uses
     * the full width). */
    if (style->text_wrap_balance && !no_wrap && !use_space && lines.length > 1 && lines.length <= 10) {
        size_t target = lines.length;
        double low = text_width * 0.25, high = text_width;
        for (int step = 0; step < 14; step++) {
            double middle = (low + high) / 2.0;
            lines.length  = 0;
            tbox_layout_break_lines(word_items, word_count, middle, indent, no_wrap, style->text_indent_each_line, block_face, NULL, &lines);
            if (lines.length <= target)
                high = middle;
            else
                low = middle;
        }
        lines.length = 0;
        tbox_layout_break_lines(word_items, word_count, high, indent, no_wrap, style->text_indent_each_line, block_face, NULL, &lines);
    }

    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);

    size_t line_count                  = tbox_vector_length(&lines);
    const tbox_layout_line *line_items = (const tbox_layout_line *)lines.data;

    double cumulative_y = content_y;
    double total_height = 0.0;
    /* line-clamp: only the first lines are laid out, the last of them
     * ending in an ellipsis when text was cut. */
    bool clamped = style->line_clamp > 0 && line_count > (size_t)style->line_clamp && !is_input && !is_select;
    if (clamped)
        line_count = (size_t)style->line_clamp;
    for (size_t li = 0; li < line_count; li++) {
        const tbox_layout_line *line = &line_items[li];
        /* Beside floats: pushed down past them, and narrowed. */
        cumulative_y += line->y_offset;
        total_height += line->y_offset;
        double line_x     = text_x + line->left;
        double line_avail = line->width;
        bool after_break             = li > 0 && line_items[li - 1].end < word_count && word_items[line_items[li - 1].end].hard_break;
        double line_indent           = li == 0 || (style->text_indent_each_line && after_break) ? indent : 0.0;

        /* The paragraph's last line, or one ended by a forced break, takes
         * text-align-last (auto: text-align, with justify falling back to
         * left). */
        bool last_line                   = (li + 1 == line_count && !clamped) || (line->end < word_count && word_items[line->end].hard_break);
        tbox_style_text_align line_align = style->text_align;
        if (last_line) {
            switch (style->text_align_last) {
            case TBOX_STYLE_TEXT_ALIGN_LAST_LEFT: line_align = TBOX_STYLE_TEXT_ALIGN_LEFT; break;
            case TBOX_STYLE_TEXT_ALIGN_LAST_CENTER: line_align = TBOX_STYLE_TEXT_ALIGN_CENTER; break;
            case TBOX_STYLE_TEXT_ALIGN_LAST_RIGHT: line_align = TBOX_STYLE_TEXT_ALIGN_RIGHT; break;
            case TBOX_STYLE_TEXT_ALIGN_LAST_JUSTIFY: line_align = TBOX_STYLE_TEXT_ALIGN_JUSTIFY; break;
            case TBOX_STYLE_TEXT_ALIGN_LAST_AUTO:
            default:
                if (line_align == TBOX_STYLE_TEXT_ALIGN_JUSTIFY)
                    line_align = TBOX_STYLE_TEXT_ALIGN_LEFT;
                break;
            }
        }

        size_t runs_before = tbox_vector_length(&runs);
        double justify_gap = 0.0;
        if (line_align == TBOX_STYLE_TEXT_ALIGN_JUSTIFY && !no_wrap && !(clamped && li + 1 == line_count) && !style->text_justify_none) {
            double used  = line_indent - line->hang_left - line->hang_right;
            size_t gaps  = 0;
            bool started = false;
            for (size_t w = line->start; w < line->end; w++) {
                if (word_items[w].floating != NULL)
                    continue;
                double edges[2];
                tbox_layout_inline_edges(word_items, word_count, w, line->start, line->end, text_width, edges);
                used += word_items[w].width + edges[0] + edges[1];
                if (started)
                    used += word_items[w].space_width;
                if (started && word_items[w].space_width > 0.0)
                    gaps++;
                started = true;
            }
            if (gaps > 0 && used < line_avail)
                justify_gap = (line_avail - used) / (double)gaps;
        }
        double line_used = tbox_layout_build_line_runs(arena, word_items, bidi, line, cumulative_y, line_x + line_indent, text_width, word_count, block_face, justify_gap, &runs);
        if (style->text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS && tbox_layout_white_space_nowrap(style) && (style->overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN || style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE) && !is_input && !is_select) {
            const tbox_font_face *face = tbox_layout_style_face(fonts, style);
            tbox_layout_ellipsize_line(&runs, runs_before, line_x, line_avail, style, face, line, cumulative_y, false);
        }
        if (clamped && li + 1 == line_count) {
            const tbox_font_face *face = tbox_layout_style_face(fonts, style);
            tbox_layout_ellipsize_line(&runs, runs_before, line_x + line_indent, line_avail - line_indent, style, face, line, cumulative_y, true);
        }
        size_t runs_after = tbox_vector_length(&runs);
        for (size_t ri = runs_before; ri < runs_after; ri++) {
            tbox_layout_text_run *run = tbox_vector_at(&runs, ri);
            run->line_y = cumulative_y;
            run->line_height = line->height;
        }

        bool line_has_atomic = false;
        for (size_t w = line->start; w < line->end; w++)
            if (word_items[w].atomic != NULL)
                line_has_atomic = true;
        if ((line_align == TBOX_STYLE_TEXT_ALIGN_CENTER || line_align == TBOX_STYLE_TEXT_ALIGN_RIGHT) && (runs_after > runs_before || line_has_atomic)) {
            tbox_layout_text_run *run_items = (tbox_layout_text_run *)runs.data;
            double line_width               = line_indent + line_used;
            if (runs_after > runs_before) {
                const tbox_layout_text_run *last_run = &run_items[runs_after - 1];
                double run_right                     = (last_run->rect.x + last_run->rect.width) - line_x - line->hang_right;
                if (run_right > line_width)
                    line_width = run_right; /* an ellipsis mark */
            }

            double offset = (line_align == TBOX_STYLE_TEXT_ALIGN_CENTER) ? (line_avail - line_width) / 2.0 : (line_avail - line_width);

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

    if (!is_preformatted && !is_input && !is_select)
        tbox_layout_append_outside_marker(arena, marker_node, marker_style, fonts, images, content_x, content_y, line_count > 0 ? &line_items[0] : NULL, &runs);
    box->text_runs      = (tbox_layout_text_run *)runs.data;
    box->text_run_count = runs.length;

    /* inline-blocks become this box's children, in document
     * order, so Render paints them and hit testing reaches them.
     * one whose absolute/fixed descendants resolve against a containing
     * block outside it (so moving it from the origin would misplace them)
     * is laid out again in place, against the real positioned context. */
    for (size_t w = 0; w < word_count; w++) {
        tbox_layout_box *floating = word_items[w].floating;
        if (floating != NULL) {
            /* A float among the words: a child too, painted with the floats
             * of its stacking context (tbox_render.c). */
            if (!use_space) /* nowhere to place it: at the top-left */
                tbox_layout_translate(floating, content_x - floating->margin_box.x, content_y - floating->margin_box.y);
            floating->parent = box;
            if (box->last_child == NULL)
                box->first_child = floating;
            else
                box->last_child->next_sibling = floating;
            box->last_child = floating;
            continue;
        }
        tbox_layout_box *atomic = word_items[w].atomic;
        if (atomic == NULL)
            continue;
        const tbox_html_node *atomic_node = word_items[w].atomic_node;
        const tbox_style *atomic_style    = atomic->style;
        if (context != NULL && atomic_node != NULL && tbox_layout_has_escaping_positioned(atomic_node, styles, atomic_style->position != TBOX_STYLE_POSITION_STATIC)) {
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
    if (!is_input && !is_select && !is_preformatted)
        tbox_layout_inline_positioned(arena, first_sibling, end_exclusive, styles, fonts, images, box, context, false);
    if (is_input && style->placeholder_style != NULL) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (value == NULL || value->value.size == 0) {
            const tbox_font_face *face = tbox_layout_style_face(fonts, style);
            return face != NULL ? tbox_layout_style_line_height(style, face) : 0.0;
        }
    }
    return total_height;
}
