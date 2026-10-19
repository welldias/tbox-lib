#include "tbox_layout_internal.h"
#include <string.h>

/* An image word's "line-height"/"ascent" contribution, wherever
 * tbox_layout_break_lines would otherwise call tbox_font_face_line_height/
 * tbox_font_face_ascent(word->face): an image, at the default
 * `vertical-align: baseline`, sits with its BOTTOM edge exactly on the
 * line's shared baseline (no descent below it) -- so its own resolved
 * height (`word->image_height`) is both its ascent AND its line-height
 * contribution, unlike a font face's ascent/line-height (which differ by
 * the descent + any line gap). This is exact, not an approximation, for
 * the one vertical-align this project supports on a replaced element. */
double tbox_layout_style_line_height(const tbox_style *style, const tbox_font_face *face) {
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER)
        return style->font_size * style->line_height_value;
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_PX)
        return style->line_height_value;
    return tbox_font_face_line_height(face);
}

static double tbox_layout_word_line_height(const tbox_layout_word *word) {
    return word->image != NULL || word->atomic != NULL ? word->image_height : tbox_layout_style_line_height(word->style, word->face);
}

static double tbox_layout_word_ascent(const tbox_layout_word *word) {
    if (word->atomic != NULL)
        return word->atomic_ascent;
    if (word->image != NULL)
        return word->image_height;
    double leading = (tbox_layout_style_line_height(word->style, word->face) - tbox_font_face_line_height(word->face)) / 2.0;
    return tbox_font_face_ascent(word->face) + leading;
}

/* A line's height and dominant ascent over words [start, end): the max
 * line-height and the max ascent among them. with an inline-block
 * on the line, whose content can hang below the baseline, the height also
 * covers the max ascent plus the max descent (line-height minus ascent).
 * an inline-block or image aligned `middle` contributes its
 * extent around the baseline raised by half an x-height (the same 55%-of-
 * ascent approximation tbox_layout_atomic_top uses), and one aligned `top`
 * or `bottom` makes the line at least as tall as itself. */
static void tbox_layout_line_metrics(const tbox_layout_word *words, size_t start, size_t end, const tbox_font_face *block_face, double *out_height, double *out_ascent) {
    double height = 0.0, ascent = 0.0, descent = 0.0, edge_aligned = 0.0;
    bool compose = false;
    for (size_t j = start; j < end; j++) {
        const tbox_layout_word *word    = &words[j];
        if (word->floating != NULL)
            continue; /* takes no room on the line */
        double word_height              = tbox_layout_word_line_height(word);
        bool atomic_like                = word->atomic != NULL || word->image != NULL;
        tbox_style_vertical_align align = word->style->vertical_align;
        if (atomic_like && (align == TBOX_STYLE_VERTICAL_ALIGN_TOP || align == TBOX_STYLE_VERTICAL_ALIGN_BOTTOM)) {
            if (word_height > edge_aligned)
                edge_aligned = word_height;
            continue;
        }
        double word_ascent;
        if (atomic_like && align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE) {
            double x_height = block_face != NULL ? tbox_font_face_ascent(block_face) * 0.55 : 0.0;
            word_ascent     = word_height / 2.0 + x_height / 2.0;
            compose         = true;
        } else {
            word_ascent = tbox_layout_word_ascent(word);
            if (word_height > height)
                height = word_height;
        }
        if (word_ascent > ascent)
            ascent = word_ascent;
        if (word_height - word_ascent > descent)
            descent = word_height - word_ascent;
        if (word->atomic != NULL)
            compose = true;
    }
    if (compose && ascent + descent > height)
        height = ascent + descent;
    if (edge_aligned > height)
        height = edge_aligned;
    *out_height = height;
    *out_ascent = ascent;
}

/* The room a line whose top is at `y` has beside the floats: `*left`
 * px into the text area, `*width` px wide. */
static void tbox_layout_line_room(const tbox_layout_line_space *space, double y, double available_width, double *left, double *width) {
    *left  = 0.0;
    *width = available_width;
    if (space == NULL || tbox_layout_floats_empty(space->floats))
        return;
    double l, r;
    tbox_layout_floats_band(space->floats, y, y + space->line_height, space->x, space->x + space->width, true, &l, &r);
    *left  = l - space->x;
    *width = r - l > 0.0 ? r - l : 0.0;
}

/* Closes the line [start, end) and returns its height. `break_word` is
 * the hard break ending it, if any: a line with no words takes its
 * height from that break's face. */
static double tbox_layout_close_line(const tbox_layout_word *words, size_t start, size_t end, bool has_word, const tbox_layout_word *break_word, const tbox_font_face *block_face, double left, double width, double y_offset, double used, bool paragraph_end, tbox_vector *lines) {
    double height, ascent;
    tbox_layout_line_metrics(words, start, end, block_face, &height, &ascent);
    if (!has_word && break_word != NULL) {
        height = tbox_layout_word_line_height(break_word);
        ascent = tbox_layout_word_ascent(break_word);
    }
    double hang_left = 0.0, hang_right = 0.0;
    size_t first = start, last = end;
    while (first < end && words[first].floating != NULL) first++;
    while (last > first && words[last - 1].floating != NULL) last--;
    if (first < last) {
        if (lines->length == 0)
            hang_left = tbox_layout_hang_first(&words[first]);
        hang_right = tbox_layout_hang_end(&words[last - 1], paragraph_end, used - hang_left > width);
    } else if (block_face != NULL && start < end) {
        height = tbox_layout_style_line_height(words[start].style, block_face);
        ascent = tbox_font_face_ascent(block_face);
    }
    tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
    line->start            = start;
    line->end              = end;
    line->height           = height;
    line->ascent           = ascent;
    line->left             = left;
    line->width            = width;
    line->y_offset         = y_offset;
    line->hang_left        = hang_left;
    line->hang_right       = hang_right;
    return height;
}

/* Greedy, per-word line breaking. Optional break-word preprocessing splits
 * only words wider than an empty line into codepoint-aligned pieces before
 * this pass. This loop accumulates words onto the current
 * line (measuring each one's own advance + a preceding space, both already
 * cached on the word) until the next word wouldn't fit within
 * `available_width`; then closes the line and starts a new one. A single
 * word wider than `available_width` still lands alone on its own line
 * (`i == line_start`, so the fit check is skipped) rather than being force-
 * split -- it simply overflows visually, same policy box width already has
 * for content that's too wide (see docs/dev/historico.md's D4). Pushes one
 * tbox_layout_line per line onto `lines`, each carrying its own height
 * (the max line-height among the faces used by the words in its range).
 *
 * `no_wrap` (true for `<pre>`, see
 * tbox_layout_collect_preformatted_words/tbox_layout_build_text_runs) turns
 * off the width-based fit check entirely -- `<pre>` never wraps by width
 * (CSS `white-space: pre`, not `pre-wrap`), only at an explicit hard break.
 * Independently of `no_wrap`, any word with `hard_break == true` (see
 * tbox_layout_push_hard_break above -- `<br>`, or a `<pre>` physical-line
 * boundary) unconditionally closes the current line right there, checked at
 * the TOP of the loop body, before the width fit check even runs: the
 * current line's range is `[line_start, i)` (the hard break itself is never
 * part of any line's word range, on either side); when that range is EMPTY
 * (`i == line_start` -- this break immediately follows another break, e.g.
 * `<br><br>`, or is the very first word), the normal "max line-height among
 * the words in range" loop has nothing to iterate, so
 * `tbox_font_face_line_height(words[i].face)` -- the face
 * tbox_layout_push_hard_break stashed on the break itself purely for this
 * purpose -- is used instead, so a blank line between two consecutive
 * breaks still occupies roughly one line's height rather than 0. The push
 * of the FINAL line, after the loop, is now guarded by
 * `line_start < word_count`: without it, a hard break sitting at the very
 * end of the words (`"texto<br>"`, nothing after) would unconditionally
 * push one more, phantom, empty line that no real browser shows -- the
 * guard means a final line is only pushed when real content actually
 * follows the last break.
 *
 * `space` (NULL without floats) makes it float-aware: each line gets the
 * room the floats leave in its band (tbox_layout_line.left/width), moves
 * down past them when its first word does not fit (y_offset), and float
 * words are placed when the line reaches them. */

/* Horizontal decorations participate in wrapping; vertical padding does
 * not enlarge a CSS inline line box. Slice keeps only the element ends. */
double tbox_layout_inline_edge(const tbox_layout_word *word, bool left, bool element_end, double width) {
    const tbox_style *style = word->style;
    if (word->floating != NULL || word->image != NULL || word->atomic != NULL || style->display != TBOX_STYLE_DISPLAY_INLINE || (!style->box_decoration_clone && !element_end)) return 0.0;
    size_t side = left ? 3 : 1;
    return tbox_style_border_side_width(style, side) + tbox_style_length_resolve(style->padding[side], width);
}

static size_t tbox_layout_inline_previous(const tbox_layout_word *words, size_t index) {
    while (index > 0) {
        index--;
        if (words[index].floating == NULL) return index;
    }
    return SIZE_MAX;
}

static size_t tbox_layout_inline_next(const tbox_layout_word *words, size_t count, size_t index) {
    do { index++; } while (index < count && words[index].floating != NULL);
    return index;
}

bool tbox_layout_inline_first(const tbox_layout_word *words, size_t index) {
    size_t previous = tbox_layout_inline_previous(words, index);
    return previous == SIZE_MAX || words[previous].style != words[index].style;
}

bool tbox_layout_inline_last(const tbox_layout_word *words, size_t count, size_t index) {
    size_t next = tbox_layout_inline_next(words, count, index);
    return next == count || words[next].style != words[index].style;
}

void tbox_layout_inline_edges(const tbox_layout_word *words, size_t count, size_t index, size_t start, size_t end, double width, double edges[2]) {
    size_t previous = tbox_layout_inline_previous(words, index);
    size_t next = tbox_layout_inline_next(words, count, index);
    bool first = previous == SIZE_MAX || words[previous].style != words[index].style;
    bool last = next == count || words[next].style != words[index].style;
    edges[0] = previous == SIZE_MAX || previous < start || first ? tbox_layout_inline_edge(&words[index], true, first, width) : 0.0;
    edges[1] = next >= end || last ? tbox_layout_inline_edge(&words[index], false, last, width) : 0.0;
}

/* Width with the candidate word and its closing decoration. When another
 * word joins that fragment, remove the previous provisional clone end. */
static double tbox_layout_inline_prospective(const tbox_layout_word *words, size_t count, size_t index, size_t start, double width, double used, bool has_word) {
    double edges[2];
    tbox_layout_inline_edges(words, count, index, start, index + 1, width, edges);
    double result = used + words[index].width + edges[0] + edges[1];
    if (has_word) {
        result += words[index].space_width;
        size_t previous = tbox_layout_inline_previous(words, index);
        if (previous != SIZE_MAX && words[previous].style == words[index].style)
            result -= tbox_layout_inline_edge(&words[previous], false, false, width);
    }
    return result;
}

void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, double first_line_indent, bool no_wrap, bool indent_each_line, const tbox_font_face *block_face, const tbox_layout_line_space *space, tbox_vector *lines) {
    if (word_count == 0) {
        return;
    }

    size_t line_start = 0;
    double line_width = first_line_indent;
    bool has_word     = false; /* a real word (not a float) is on the line */
    double hang_left = 0.0;
    /* With floats: the current line's top, how far it was pushed down
     * past floats, and the room it has beside them. */
    double y        = space != NULL ? space->y : 0.0;
    double y_offset = 0.0;
    double left, width;
    tbox_layout_line_room(space, y, available_width, &left, &width);
    /* Floats that did not fit beside the line's words: placed at the top
     * of the next line (CSS 2.1 section 9.5.1, rule 6). */
    tbox_vector deferred;
    tbox_vector_init(&deferred, lines->arena, sizeof(tbox_layout_box *), 0);

    for (size_t i = 0; i < word_count; i++) {
        const tbox_layout_word *word = &words[i];
        if (word->floating != NULL) {
            if (space != NULL) {
                if (!has_word || word->floating->margin_box.width <= width - line_width) {
                    tbox_layout_floats_place(space->floats, word->floating, space->float_x, space->float_width, y, space->images, space->arena);
                    tbox_layout_line_room(space, y, available_width, &left, &width);
                } else {
                    *(tbox_layout_box **)tbox_vector_push(&deferred) = word->floating;
                }
            }
            continue;
        }

        bool new_line = false;
        if (word->hard_break) {
            y += tbox_layout_close_line(words, line_start, i, has_word, word, block_face, left, width, y_offset, line_width, false, lines);
            line_start = i + 1;
            line_width = indent_each_line ? first_line_indent : 0.0; /* text-indent: each-line */
            has_word   = false;
            new_line   = true;
        } else {
            double prospective = tbox_layout_inline_prospective(words, word_count, i, line_start, available_width, line_width, has_word);
            if (!has_word)
                hang_left = lines->length == 0 ? tbox_layout_hang_first(word) : 0.0;
            double hang_end = tbox_layout_hang_end(word, i + 1 == word_count, prospective - hang_left > width);
            if (!no_wrap && has_word && prospective - hang_left - hang_end > width) {
                y += tbox_layout_close_line(words, line_start, i, true, NULL, block_face, left, width, y_offset, line_width, false, lines);
                line_start = i;
                line_width = 0.0;
                has_word   = false;
                new_line   = true;
            }
        }
        if (new_line) {
            hang_left = 0.0;
            y_offset = 0.0;
            for (size_t f = 0; space != NULL && f < deferred.length; f++)
                tbox_layout_floats_place(space->floats, *(tbox_layout_box **)tbox_vector_at(&deferred, f), space->float_x, space->float_width, y, space->images, space->arena);
            deferred.length = 0;
            tbox_layout_line_room(space, y, available_width, &left, &width);
        }
        if (word->hard_break)
            continue;

        double prospective = tbox_layout_inline_prospective(words, word_count, i, line_start, available_width, line_width, has_word);
        /* The first word of a line too narrow beside the floats moves the
         * line down until it fits or the floats end (rule 9.5's "shift
         * downward"). */
        if (!has_word && space != NULL && !no_wrap) {
            for (int guard = 0; guard < 10000 && prospective - hang_left - tbox_layout_hang_end(word, i + 1 == word_count, prospective - hang_left > width) > width && (left > 0.0 || width < available_width); guard++) {
                double next = tbox_layout_floats_next_y(space->floats, y, y + space->line_height, space->x, space->x + space->width, true);
                if (next <= y)
                    break;
                y_offset += next - y;
                y = next;
                tbox_layout_line_room(space, y, available_width, &left, &width);
            }
        }
        line_width = prospective;
        has_word   = true;
    }

    if (line_start < word_count) {
        y += tbox_layout_close_line(words, line_start, word_count, has_word, NULL, block_face, left, width, y_offset, line_width, true, lines);
        for (size_t f = 0; space != NULL && f < deferred.length; f++)
            tbox_layout_floats_place(space->floats, *(tbox_layout_box **)tbox_vector_at(&deferred, f), space->float_x, space->float_width, y, space->images, space->arena);
    }
}

/* the extra vertical offset (px) `vertical_align: sub`/`super`
 * adds ON TOP OF baseline alignment (see tbox_layout_build_line_runs below)
 * -- `0.0` for the initial BASELINE, the universal case through v12 (zero
 * visual change). `0.15`/`0.35` are fixed fractions of `style->font_size`,
 * an approximation by common visual convention (browsers' usual order of
 * magnitude), not a real OpenType `subs`/`sups` table lookup -- out of scope,
 * see docs/dev/historico.md's v13 "Fora de escopo". Positive moves DOWN (`sub`),
 * negative moves UP (`super`), matching this project's y-down coordinate
 * space.
 *
 * `text-top`/`text-bottom` align the run's top/bottom edge with the
 * ascent/descent of `block_face` -- the text box's own font, standing in for
 * the parent's (inline elements have no box of their own to take it from).
 * `run_ascent`/`run_height` are the run's own extent, the same values
 * tbox_layout_build_line_runs already uses for baseline alignment. A length
 * raises the run by that many px; a percentage is of the run's own used
 * line-height. The line box's height is not grown for any of these, same
 * simplification as `sub`/`super`. */
static double tbox_layout_vertical_align_offset(const tbox_style *style, const tbox_font_face *run_face, double run_ascent, double run_height, const tbox_font_face *block_face) {
    switch (style->vertical_align) {
    case TBOX_STYLE_VERTICAL_ALIGN_SUB:
        return 0.15 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_SUPER:
        return -0.35 * style->font_size;
    case TBOX_STYLE_VERTICAL_ALIGN_TEXT_TOP:
        return block_face != NULL ? run_ascent - tbox_font_face_ascent(block_face) : 0.0;
    case TBOX_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM:
        return block_face != NULL ? run_ascent - run_height + tbox_font_face_line_height(block_face) - tbox_font_face_ascent(block_face) : 0.0;
    case TBOX_STYLE_VERTICAL_ALIGN_LENGTH:
        if (style->vertical_align_length.kind == TBOX_STYLE_LENGTH_PERCENT)
            return run_face != NULL ? -tbox_style_length_resolve(style->vertical_align_length, tbox_layout_style_line_height(style, run_face)) : 0.0;
        return -style->vertical_align_length.value;
    case TBOX_STYLE_VERTICAL_ALIGN_BASELINE:
    default:
        return 0.0;
    }
}

/* Places and merges one line's words into runs, appending them to `runs`.
 * `line_y` is this line's already-computed absolute top (content_y plus
 * every earlier line's height); `content_x` is the text box's content-box
 * left edge. Consecutive words sharing the exact same face AND style
 * ( -- previously face alone) merge into one tbox_layout_text_run
 * (their text joined by single spaces, matching
 * tbox_string_collapse_whitespace's own separator); a new run starts when
 * EITHER changes (a line boundary is handled by the caller looping per
 * line, never straddled by a single run) -- so e.g. a `<mark>` run never
 * merges with an adjacent plain-text run even when both resolve to the
 * IDENTICAL face (no weight/size/family/italic difference declared), since
 * they still need separate `tbox_layout_text_run.style` pointers for
 * Render Pipeline's per-run background/decoration (see docs/dev/historico.md's
 * "v13 -- Layout Tree" section). The gap between two adjacent runs (the
 * space that would sit between them in the source text) is accounted for in
 * each run's absolute x position but deliberately belongs to neither run's
 * own text/width -- unchanged since before v13.
 *
 * `rect.y` is no longer `line_y` alone -- every run's baseline is
 * aligned with the LINE's dominant baseline first (`line->ascent -
 * tbox_font_face_ascent(run_face)`, zero when every run on the line shares
 * one face/size, the universal case through v12), then shifted further by
 * `tbox_layout_vertical_align_offset` for `sub`/`super` (zero for the
 * initial BASELINE). `rect.height` still spans the WHOLE line, not the
 * run's own reduced size -- a deliberate simplification, see
 * docs/dev/historico.md. `run->style` is set to the SAME style that decided
 * `run_face`, for the reasons above. */
/* the top y of an atomic inline (inline-block or image) of
 * `height` whose baseline sits `ascent` below its top. `middle` centers it
 * on the baseline raised by half an x-height (approximated as 55% of the
 * block font's ascent); `top`/`bottom` align it with the line box's top/
 * bottom; anything else uses the ordinary run offset. Only atomic inlines
 * get middle/top/bottom here -- on a table cell those values align the
 * cell's content instead, never the cell's own text runs. */
double tbox_layout_atomic_top(const tbox_style *style, const tbox_font_face *face, double ascent, double height, const tbox_layout_line *line, double line_y, const tbox_font_face *block_face) {
    switch (style->vertical_align) {
    case TBOX_STYLE_VERTICAL_ALIGN_TOP:
        return line_y;
    case TBOX_STYLE_VERTICAL_ALIGN_BOTTOM:
        return line_y + line->height - height;
    case TBOX_STYLE_VERTICAL_ALIGN_MIDDLE: {
        double x_height = block_face != NULL ? tbox_font_face_ascent(block_face) * 0.55 : 0.0;
        return line_y + line->ascent - x_height / 2.0 - height / 2.0;
    }
    default:
        return line_y + line->ascent - ascent + tbox_layout_vertical_align_offset(style, face, ascent, height, block_face);
    }
}

/* Closes a run of the line: [start_x, end_x) relative to `content_x`, on
 * the line's baseline (an image run on its own top), with `glyphs`. */
void tbox_layout_push_run(tbox_vector *runs, const tbox_layout_line *line, double line_y, double content_x, const tbox_font_face *block_face, double start_x, double end_x, const tbox_font_face *face, const tbox_style *style, const tbox_image *image, double image_height, tbox_string_view text, const tbox_vector *glyphs) {
    tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
    if (run == NULL)
        return;
    *run             = (tbox_layout_text_run){ 0 };
    run->rect.x      = content_x + start_x;
    double ascent    = image != NULL ? image_height : tbox_font_face_ascent(face);
    run->rect.y      = image != NULL ? tbox_layout_atomic_top(style, face, ascent, image_height, line, line_y, block_face) : line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(style, face, ascent, tbox_font_face_line_height(face), block_face);
    run->rect.width  = end_x - start_x;
    run->rect.height = image != NULL ? image_height : line->height;
    run->text        = text;
    run->font        = face;
    run->style       = style;
    run->image       = image;
    run->glyphs      = (const tbox_glyph *)glyphs->data;
    run->glyph_count = tbox_vector_length(glyphs);
}

/* Shapes `text` at `x` (relative to the run) in `direction` and appends
 * its glyphs, their clusters offset by `cluster_base` (where `text` starts
 * in the run). Returns the shaped width. */
double tbox_layout_append_glyphs(tbox_arena *arena, tbox_vector *glyphs, const tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, double x, size_t cluster_base) {
    tbox_glyph *shaped;
    size_t count;
    double width;
    if (!tbox_font_shape(arena, face, text, letter_spacing, direction, x, (uint32_t)cluster_base, &shaped, &count, &width))
        return 0.0;
    for (size_t i = 0; i < count; i++) {
        tbox_glyph *slot = (tbox_glyph *)tbox_vector_push(glyphs);
        if (slot != NULL)
            *slot = shaped[i];
    }
    return width;
}

double tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_word_bidi *bidi, const tbox_layout_line *line, double line_y, double content_x, double available_width, size_t word_count, const tbox_font_face *block_face, double justify_gap, tbox_vector *runs) {
    /* A line with any level above 0 is reordered (tbox_layout_bidi.c);
     * the plain left-to-right path below is unchanged. */
    if (bidi != NULL && tbox_layout_line_is_bidi(words, bidi, line))
        return tbox_layout_build_bidi_line_runs(arena, words, bidi, line, line_y, content_x, available_width, word_count, block_face, justify_gap, runs);
    double cursor_x                = -line->hang_left;
    double run_start_x             = 0.0;
    double run_end_x               = 0.0;
    const tbox_font_face *run_face = NULL;
    const tbox_style *run_style    = NULL;
    /* An image word (see tbox_layout_push_image_word) always closes its own
     * singleton run -- `run_is_image`/`run_image`/`run_image_height` are
     * only ever set alongside `run_face`/`run_style` below, at a run's
     * opening word, and read back at that SAME run's closing point. */
    bool run_is_image           = false;
    const tbox_image *run_image = NULL;
    double run_image_height     = 0.0;
    tbox_string_builder run_builder;
    /* The run's glyphs, each word shaped where this line places it, so the
     * drawing follows the layout exactly (spaces, word-spacing, justify). */
    tbox_vector run_glyphs;
    bool have_run = false;

    bool started = false; /* a word (not a float) was placed */
    for (size_t i = line->start; i < line->end; i++) {
        const tbox_layout_word *word = &words[i];
        if (word->floating != NULL)
            continue; /* placed by tbox_layout_break_lines, off the line */

        /* `justify_gap` widens every real word gap (text-align: justify).
         * The run keeps a single ' ' in its text; the wider gap lives only
         * in the glyph positions, like word-spacing's. */
        bool justified = justify_gap != 0.0 && started && word->space_width > 0.0;
        double gap_x   = cursor_x;
        if (started) {
            cursor_x += word->space_width + (justified ? justify_gap : 0.0);
        }
        started = true;

        double edges[2];
        tbox_layout_inline_edges(words, word_count, i, line->start, line->end, available_width, edges);
        cursor_x += edges[0];

        /* an inline-block closes any open run and moves its
         * already-built box onto the line: baseline on the line's baseline,
         * then the same vertical-align offset a run would get. */
        if (word->atomic != NULL) {
            if (have_run) {
                tbox_layout_push_run(runs, line, line_y, content_x, block_face, run_start_x, run_end_x, run_face, run_style, run_is_image ? run_image : NULL, run_image_height, tbox_string_builder_finish(&run_builder), &run_glyphs);
                have_run = false;
            }
            double x = content_x + cursor_x;
            double y = tbox_layout_atomic_top(word->style, word->face, word->atomic_ascent, word->image_height, line, line_y, block_face);
            tbox_layout_translate(word->atomic, x - word->atomic->margin_box.x, y - word->atomic->margin_box.y);
            cursor_x += word->width;
            run_end_x = cursor_x;
            continue;
        }

        /* An image word never merges with a neighbor -- forced by
         * `word->image != NULL` (opening one) OR `run_is_image` (the
         * currently-open run already is one, so THIS word, whatever it is,
         * must start a fresh run) -- same "never merges" treatment a
         * `<mark>` word already gets today via the `style` mismatch, just
         * unconditional here since an image's face/style are otherwise
         * ordinary values that could otherwise coincidentally match. */
        bool new_run = !have_run || word->face != run_face || word->style != run_style || word->image != NULL || run_is_image;
        if (new_run) {
            if (have_run)
                tbox_layout_push_run(runs, line, line_y, content_x, block_face, run_start_x, run_end_x, run_face, run_style, run_is_image ? run_image : NULL, run_image_height, tbox_string_builder_finish(&run_builder), &run_glyphs);

            run_start_x      = cursor_x;
            run_face         = word->face;
            run_style        = word->style;
            run_is_image     = word->image != NULL;
            run_image        = word->image;
            run_image_height = word->image_height;
            tbox_string_builder_init(&run_builder, arena, word->text.size + 8);
            tbox_vector_init(&run_glyphs, arena, sizeof(tbox_glyph), word->text.size + 1);
            have_run = true;
        } else if (!word->no_space_before) {
            tbox_layout_append_glyphs(arena, &run_glyphs, word->face, tbox_string_view_make(" ", 1), word->style->letter_spacing, TBOX_FONT_DIRECTION_AUTO, gap_x - run_start_x, run_builder.length);
            tbox_string_builder_append_byte(&run_builder, ' ');
        }

        if (!run_is_image)
            tbox_layout_append_glyphs(arena, &run_glyphs, word->face, word->text, word->style->letter_spacing, TBOX_FONT_DIRECTION_AUTO, cursor_x - run_start_x, run_builder.length);
        tbox_string_builder_append_view(&run_builder, word->text);
        cursor_x += word->width;
        /* A line broken at a soft hyphen shows the hyphen
         * (hyphenate-character, "-" when auto). */
        if (word->soft_hyphen_after && i + 1 == line->end && word->face != NULL) {
            tbox_string_view hyphen = word->style->hyphenate_character[0] != '\0' ? tbox_string_view_from_cstr(word->style->hyphenate_character) : tbox_string_view_make("-", 1);
            tbox_layout_append_glyphs(arena, &run_glyphs, word->face, hyphen, word->style->letter_spacing, TBOX_FONT_DIRECTION_AUTO, cursor_x - run_start_x, run_builder.length);
            tbox_string_builder_append_view(&run_builder, hyphen);
            cursor_x += tbox_font_measure_text_spaced(word->face, hyphen, word->style->letter_spacing);
        }
        run_end_x = cursor_x;
        cursor_x += edges[1];
    }

    if (have_run)
        tbox_layout_push_run(runs, line, line_y, content_x, block_face, run_start_x, run_end_x, run_face, run_style, run_is_image ? run_image : NULL, run_image_height, tbox_string_builder_finish(&run_builder), &run_glyphs);
    return cursor_x - line->hang_right;
}

/* The pieces [from, end) of one split word: the bidi opening marks stay
 * on the first piece, the closing ones on the last. */
static void tbox_layout_split_bidi_marks(tbox_vector *pieces, size_t from) {
    for (size_t i = from; i < pieces->length; i++) {
        tbox_layout_word *piece = (tbox_layout_word *)tbox_vector_at(pieces, i);
        if (i != from)
            piece->bidi_open_count = 0;
        if (i + 1 != pieces->length)
            piece->bidi_close_count = 0;
    }
}

/* Split only text words too wide for an empty line. Each piece is a UTF-8
 * codepoint-aligned view into the original word; continuation pieces carry
 * no preceding space, so both measurement and painted text stay intact. */
void tbox_layout_split_overlong_words(tbox_arena *arena, tbox_vector *words, double available_width, double first_indent) {
    if (available_width <= 0.0)
        return;
    tbox_vector expanded;
    tbox_vector_init(&expanded, arena, sizeof(tbox_layout_word), words->length);
    const tbox_layout_word *source = (const tbox_layout_word *)words->data;
    for (size_t i = 0; i < words->length; i++) {
        const tbox_layout_word *word = &source[i];
        double first_limit           = i == 0 ? available_width - first_indent : available_width;
        /* word-break: break-all and line-break: anywhere -- every codepoint
         * becomes its own word glued to the previous one (no space), so the
         * greedy line breaker can break between any two characters and
         * fill each line. Run
         * building merges the pieces back into one run per line. */
        size_t pieces_from = expanded.length;
        if ((word->style->word_break_all || word->style->line_break_anywhere) && !word->hard_break && word->image == NULL && word->atomic == NULL && word->text.size > 0) {
            size_t start = 0;
            while (start < word->text.size) {
                size_t next = tbox_utf8_next(word->text.data, word->text.size, start);
                tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
                *part                  = *word;
                part->text             = tbox_string_view_make(word->text.data + start, next - start);
                part->width            = tbox_font_measure_text_spaced(word->face, part->text, word->style->letter_spacing);
                part->space_width      = start == 0 ? word->space_width : 0.0;
                part->no_space_before  = start == 0 ? word->no_space_before : true;
                start                  = next;
            }
            tbox_layout_split_bidi_marks(&expanded, pieces_from);
            continue;
        }
        if (word->hard_break || word->image != NULL || word->text.size == 0 || !word->style->overflow_wrap_break_word || word->width <= first_limit) {
            *(tbox_layout_word *)tbox_vector_push(&expanded) = *word;
            continue;
        }
        size_t start = 0;
        while (start < word->text.size) {
            double limit = start == 0 ? first_limit : available_width;
            size_t end = start, best = start;
            tbox_text_measurer piece;
            tbox_text_measurer_init(&piece, word->face, word->style->letter_spacing, word->text.data + start, word->text.size - start);
            while (end < word->text.size) {
                size_t next     = start + tbox_text_measurer_step(&piece);
                double measured = piece.width;
                if (measured > limit && best > start)
                    break;
                best = next;
                end  = next;
                if (measured > limit)
                    break; /* one glyph exceeds the line */
            }
            tbox_text_measurer_finish(&piece);
            /* The cut is chosen within the shaped word; the piece is then
             * shaped (and drawn) on its own, so its width is its own. */
            tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
            *part                  = *word;
            part->text             = tbox_string_view_make(word->text.data + start, best - start);
            part->width            = tbox_font_measure_text_spaced(word->face, part->text, word->style->letter_spacing);
            part->space_width      = start == 0 ? word->space_width : 0.0;
            part->no_space_before  = start != 0;
            start                  = best;
        }
        tbox_layout_split_bidi_marks(&expanded, pieces_from);
    }
    *words = expanded;
}

/* The ellipsis (or text-overflow string) as its own shaped run. */
static void tbox_layout_push_ellipsis_mark(tbox_vector *runs, tbox_rect rect, tbox_string_view text, const tbox_font_face *face, const tbox_style *style) {
    tbox_layout_text_run *mark = (tbox_layout_text_run *)tbox_vector_push(runs);
    *mark                      = (tbox_layout_text_run){ .rect = rect, .text = text, .font = face, .style = style };
    tbox_font_shape_into(runs->arena, face, text, style->letter_spacing, &mark->glyphs, &mark->glyph_count);
}

void tbox_layout_ellipsize_line(tbox_vector *runs, size_t first, double content_x, double available_width, const tbox_style *style, const tbox_font_face *face, const tbox_layout_line *line, double line_y, bool force) {
    if (runs->length == first || face == NULL)
        return;
    tbox_layout_text_run *items = (tbox_layout_text_run *)runs->data;
    tbox_layout_text_run *last  = &items[runs->length - 1];
    double right                = content_x + available_width;
    if (!force && last->rect.x + last->rect.width <= right)
        return;

    tbox_string_view ellipsis = tbox_font_face_has_glyph(face, 0x2026) ? tbox_string_view_make("\xe2\x80\xa6", 3) : tbox_string_view_make("...", 3);
    if (!force && style->text_overflow_string[0] != '\0') /* text-overflow: "<string>" */
        ellipsis = tbox_string_view_from_cstr(style->text_overflow_string);
    double glyph_width        = tbox_font_measure_text_spaced(face, ellipsis, style->letter_spacing);
    double limit              = right - glyph_width;
    /* line-clamp (`force`): the mark follows the line's text, truncating it
     * only when there is no room left for the mark. */
    if (force && last->rect.x + last->rect.width <= limit) {
        double end = last->rect.x + last->rect.width;
        tbox_layout_push_ellipsis_mark(runs, (tbox_rect){ end, line_y + line->ascent - tbox_font_face_ascent(face), glyph_width, line->height }, ellipsis, face, style);
        return;
    }
    while (runs->length > first) {
        items = (tbox_layout_text_run *)runs->data;
        last  = &items[runs->length - 1];
        if (last->image != NULL || last->rect.x >= limit) {
            runs->length--;
            continue;
        }
        /* Keep the most glyphs whose painted extent still ends before
         * `limit`; the text is cut at the first dropped glyph's cluster. */
        if (last->glyph_count == 0 && last->text.size > 0)
            tbox_font_shape_into(runs->arena, last->font, last->text, last->style->letter_spacing, &last->glyphs, &last->glyph_count);
        size_t fit       = 0;
        double fit_width = 0.0;
        for (size_t g = 0; g < last->glyph_count; g++) {
            double end = last->glyphs[g].x + last->glyphs[g].advance;
            if (last->rect.x + end <= limit) {
                fit       = g + 1;
                fit_width = end;
            }
        }
        if (fit == 0) {
            runs->length--;
            continue;
        }
        if (fit < last->glyph_count)
            last->text.size = last->glyphs[fit].cluster;
        last->glyph_count = fit;
        last->rect.width  = fit_width;
        break;
    }
    /* The mark follows the truncated text (as in browsers), not the box's
     * end edge, so no gap opens between them. */
    double mark_x = content_x;
    if (runs->length > first) {
        items  = (tbox_layout_text_run *)runs->data;
        last   = &items[runs->length - 1];
        mark_x = last->rect.x + last->rect.width;
    }
    tbox_layout_push_ellipsis_mark(runs, (tbox_rect){ mark_x, line_y + line->ascent - tbox_font_face_ascent(face), glyph_width, line->height }, ellipsis, face, style);
}
