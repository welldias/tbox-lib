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

/* Greedy, per-word line breaking. Optional break-word preprocessing splits
 * only words wider than an empty line into codepoint-aligned pieces before
 * this pass. This loop accumulates words onto the current
 * line (measuring each one's own advance + a preceding space, both already
 * cached on the word) until the next word wouldn't fit within
 * `available_width`; then closes the line and starts a new one. A single
 * word wider than `available_width` still lands alone on its own line
 * (`i == line_start`, so the fit check is skipped) rather than being force-
 * split -- it simply overflows visually, same policy box width already has
 * for content that's too wide (see ARCHITECTURE.md's D4). Pushes one
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
 * follows the last break. */

void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, double first_line_indent, bool no_wrap, const tbox_font_face *block_face, tbox_vector *lines) {
    if (word_count == 0) {
        return;
    }

    size_t line_start = 0;
    double line_width = first_line_indent;

    for (size_t i = 0; i < word_count; i++) {
        if (words[i].hard_break) {
            double height, ascent;
            tbox_layout_line_metrics(words, line_start, i, block_face, &height, &ascent);
            if (i == line_start) {
                height = tbox_layout_word_line_height(&words[i]);
                ascent = tbox_layout_word_ascent(&words[i]);
            }

            tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
            line->start            = line_start;
            line->end              = i;
            line->height           = height;
            line->ascent           = ascent;

            line_start = i + 1;
            line_width = 0.0;
            continue;
        }

        double prospective = (i == line_start) ? line_width + words[i].width : line_width + words[i].space_width + words[i].width;

        if (!no_wrap && i > line_start && prospective > available_width) {
            double height, ascent;
            tbox_layout_line_metrics(words, line_start, i, block_face, &height, &ascent);

            tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
            line->start            = line_start;
            line->end              = i;
            line->height           = height;
            line->ascent           = ascent;

            line_start = i;
            line_width = words[i].width;
        } else {
            line_width = prospective;
        }
    }

    if (line_start < word_count) {
        double height, ascent;
        tbox_layout_line_metrics(words, line_start, word_count, block_face, &height, &ascent);
        tbox_layout_line *line = (tbox_layout_line *)tbox_vector_push(lines);
        line->start            = line_start;
        line->end              = word_count;
        line->height           = height;
        line->ascent           = ascent;
    }
}

/* the extra vertical offset (px) `vertical_align: sub`/`super`
 * adds ON TOP OF baseline alignment (see tbox_layout_build_line_runs below)
 * -- `0.0` for the initial BASELINE, the universal case through v12 (zero
 * visual change). `0.15`/`0.35` are fixed fractions of `style->font_size`,
 * an approximation by common visual convention (browsers' usual order of
 * magnitude), not a real OpenType `subs`/`sups` table lookup -- out of scope,
 * see ARCHITECTURE.md's v13 "Fora de escopo". Positive moves DOWN (`sub`),
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
            return run_face != NULL ? -tbox_layout_style_line_height(style, run_face) * style->vertical_align_length.value / 100.0 : 0.0;
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
 * Render Pipeline's per-run background/decoration (see ARCHITECTURE.md's
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
 * ARCHITECTURE.md. `run->style` is set to the SAME style that decided
 * `run_face`, for the reasons above. */
/* the top y of an atomic inline (inline-block or image) of
 * `height` whose baseline sits `ascent` below its top. `middle` centers it
 * on the baseline raised by half an x-height (approximated as 55% of the
 * block font's ascent); `top`/`bottom` align it with the line box's top/
 * bottom; anything else uses the ordinary run offset. Only atomic inlines
 * get middle/top/bottom here -- on a table cell those values align the
 * cell's content instead, never the cell's own text runs. */
static double tbox_layout_atomic_top(const tbox_style *style, const tbox_font_face *face, double ascent, double height, const tbox_layout_line *line, double line_y, const tbox_font_face *block_face) {
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

double tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, const tbox_font_face *block_face, double justify_gap, tbox_vector *runs) {
    double cursor_x                = 0.0;
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
    bool have_run = false;

    for (size_t i = line->start; i < line->end; i++) {
        const tbox_layout_word *word = &words[i];

        /* `justify_gap` widens every real word gap (text-align: justify);
         * each such word then opens its own run, since a run's text can
         * only carry single, unwidened spaces. */
        bool justified = justify_gap != 0.0 && i != line->start && word->space_width > 0.0;
        if (i != line->start) {
            cursor_x += word->space_width + (justified ? justify_gap : 0.0);
        }

        /* an inline-block closes any open run and moves its
         * already-built box onto the line: baseline on the line's baseline,
         * then the same vertical-align offset a run would get. */
        if (word->atomic != NULL) {
            if (have_run) {
                tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
                run->rect.x               = content_x + run_start_x;
                double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
                run->rect.y               = run_is_image ? tbox_layout_atomic_top(run_style, run_face, ascent, run_image_height, line, line_y, block_face) : line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style, run_face, ascent, tbox_font_face_line_height(run_face), block_face);
                run->rect.width           = run_end_x - run_start_x;
                run->rect.height          = run_is_image ? run_image_height : line->height;
                run->text                 = tbox_string_builder_finish(&run_builder);
                run->font                 = run_face;
                run->style                = run_style;
                run->image                = run_is_image ? run_image : NULL;
                have_run                  = false;
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
        bool new_run = !have_run || word->face != run_face || word->style != run_style || word->image != NULL || run_is_image || word->style->word_spacing != 0.0 || justified;
        if (new_run) {
            if (have_run) {
                tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
                run->rect.x               = content_x + run_start_x;
                double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
                run->rect.y               = run_is_image ? tbox_layout_atomic_top(run_style, run_face, ascent, run_image_height, line, line_y, block_face) : line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style, run_face, ascent, tbox_font_face_line_height(run_face), block_face);
                run->rect.width           = run_end_x - run_start_x;
                run->rect.height          = run_is_image ? run_image_height : line->height;
                run->text                 = tbox_string_builder_finish(&run_builder);
                run->font                 = run_face;
                run->style                = run_style;
                run->image                = run_is_image ? run_image : NULL;
            }

            run_start_x      = cursor_x;
            run_face         = word->face;
            run_style        = word->style;
            run_is_image     = word->image != NULL;
            run_image        = word->image;
            run_image_height = word->image_height;
            tbox_string_builder_init(&run_builder, arena, word->text.size + 8);
            have_run = true;
        } else if (!word->no_space_before) {
            tbox_string_builder_append_byte(&run_builder, ' ');
        }

        tbox_string_builder_append_view(&run_builder, word->text);
        cursor_x += word->width;
        run_end_x = cursor_x;
    }

    if (have_run) {
        tbox_layout_text_run *run = (tbox_layout_text_run *)tbox_vector_push(runs);
        run->rect.x               = content_x + run_start_x;
        double ascent             = run_is_image ? run_image_height : tbox_font_face_ascent(run_face);
        run->rect.y               = run_is_image ? tbox_layout_atomic_top(run_style, run_face, ascent, run_image_height, line, line_y, block_face) : line_y + (line->ascent - ascent) + tbox_layout_vertical_align_offset(run_style, run_face, ascent, tbox_font_face_line_height(run_face), block_face);
        run->rect.width           = run_end_x - run_start_x;
        run->rect.height          = run_is_image ? run_image_height : line->height;
        run->text                 = tbox_string_builder_finish(&run_builder);
        run->font                 = run_face;
        run->style                = run_style;
        run->image                = run_is_image ? run_image : NULL;
    }
    return cursor_x;
}

static size_t tbox_layout_previous_codepoint(tbox_string_view text) {
    size_t at = text.size;
    if (at == 0)
        return 0;
    at--;
    while (at > 0 && ((unsigned char)text.data[at] & 0xc0) == 0x80)
        at--;
    return at;
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
        /* word-break: break-all -- every codepoint becomes its own word
         * glued to the previous one (no space), so the greedy line breaker
         * can break between any two characters and fill each line. Run
         * building merges the pieces back into one run per line. */
        if (word->style->word_break_all && !word->hard_break && word->image == NULL && word->atomic == NULL && word->text.size > 0) {
            size_t start = 0;
            while (start < word->text.size) {
                size_t next = start + 1;
                while (next < word->text.size && ((unsigned char)word->text.data[next] & 0xc0) == 0x80)
                    next++;
                tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
                *part                  = *word;
                part->text             = tbox_string_view_make(word->text.data + start, next - start);
                part->width            = tbox_font_measure_text_spaced(word->face, part->text, word->style->letter_spacing);
                part->space_width      = start == 0 ? word->space_width : 0.0;
                part->no_space_before  = start == 0 ? word->no_space_before : true;
                start                  = next;
            }
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
            double best_width = 0.0;
            while (end < word->text.size) {
                size_t next = end + 1;
                while (next < word->text.size && ((unsigned char)word->text.data[next] & 0xc0) == 0x80)
                    next++;
                tbox_string_view piece = tbox_string_view_make(word->text.data + start, next - start);
                double measured        = tbox_font_measure_text_spaced(word->face, piece, word->style->letter_spacing);
                if (measured > limit && best > start)
                    break;
                best       = next;
                best_width = measured;
                end        = next;
                if (measured > limit)
                    break; /* one glyph exceeds the line */
            }
            tbox_layout_word *part = (tbox_layout_word *)tbox_vector_push(&expanded);
            *part                  = *word;
            part->text             = tbox_string_view_make(word->text.data + start, best - start);
            part->width            = best_width;
            part->space_width      = start == 0 ? word->space_width : 0.0;
            part->no_space_before  = start != 0;
            start                  = best;
        }
    }
    *words = expanded;
}

void tbox_layout_ellipsize_line(tbox_vector *runs, size_t first, double content_x, double available_width, const tbox_style *style, const tbox_font_face *face, const tbox_layout_line *line, double line_y) {
    if (runs->length == first || face == NULL)
        return;
    tbox_layout_text_run *items = (tbox_layout_text_run *)runs->data;
    tbox_layout_text_run *last  = &items[runs->length - 1];
    double right                = content_x + available_width;
    if (last->rect.x + last->rect.width <= right)
        return;

    tbox_string_view ellipsis = tbox_font_face_has_glyph(face, 0x2026) ? tbox_string_view_make("\xe2\x80\xa6", 3) : tbox_string_view_make("...", 3);
    double glyph_width        = tbox_font_measure_text_spaced(face, ellipsis, style->letter_spacing);
    double limit              = right - glyph_width;
    while (runs->length > first) {
        items = (tbox_layout_text_run *)runs->data;
        last  = &items[runs->length - 1];
        if (last->image != NULL || last->rect.x >= limit) {
            runs->length--;
            continue;
        }
        while (last->text.size > 0 && last->rect.x + tbox_font_measure_text_spaced(last->font, last->text, last->style->letter_spacing) > limit) {
            last->text.size = tbox_layout_previous_codepoint(last->text);
        }
        if (last->text.size == 0) {
            runs->length--;
            continue;
        }
        last->rect.width = tbox_font_measure_text_spaced(last->font, last->text, last->style->letter_spacing);
        break;
    }
    tbox_layout_text_run *mark = (tbox_layout_text_run *)tbox_vector_push(runs);
    mark->rect                 = (tbox_rect){ right - glyph_width > content_x ? right - glyph_width : content_x, line_y + line->ascent - tbox_font_face_ascent(face), glyph_width, line->height };
    mark->text                 = ellipsis;
    mark->font                 = face;
    mark->style                = style;
    mark->image                = NULL;
}
