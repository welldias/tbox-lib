/* Bidirectional text in the inline formatting context
 * (docs/09-texto-e-fontes.md, "Texto bidirecional"). Words keep their
 * logical order through collection and line breaking; tbox_layout_resolve_bidi
 * runs UAX #9 (src/base/tbox_bidi.c) once per paragraph over their text,
 * the inter-word spaces and the formatting characters `unicode-bidi` puts
 * around inline elements, and each line with a level above 0 is then
 * reordered piece by piece (rule L2) by tbox_layout_build_bidi_line_runs. */
#include "tbox_layout_internal.h"

#include <stdlib.h>
#include <string.h>

#include "base/tbox_bidi.h"

#define TBOX_LAYOUT_BIDI_STACK 64

void tbox_layout_mark_bidi(tbox_vector *words, size_t first, const tbox_style *style) {
    if (style == NULL || words->length <= first)
        return;
    bool rtl = style->direction_rtl;
    uint32_t open[2], close[2];
    size_t n = 1;
    switch (style->unicode_bidi) {
    case TBOX_STYLE_UNICODE_BIDI_EMBED:
        open[0]  = rtl ? TBOX_BIDI_CHAR_RLE : TBOX_BIDI_CHAR_LRE;
        close[0] = TBOX_BIDI_CHAR_PDF;
        break;
    case TBOX_STYLE_UNICODE_BIDI_ISOLATE:
        open[0]  = rtl ? TBOX_BIDI_CHAR_RLI : TBOX_BIDI_CHAR_LRI;
        close[0] = TBOX_BIDI_CHAR_PDI;
        break;
    case TBOX_STYLE_UNICODE_BIDI_BIDI_OVERRIDE:
        open[0]  = rtl ? TBOX_BIDI_CHAR_RLO : TBOX_BIDI_CHAR_LRO;
        close[0] = TBOX_BIDI_CHAR_PDF;
        break;
    case TBOX_STYLE_UNICODE_BIDI_ISOLATE_OVERRIDE:
        open[0]  = rtl ? TBOX_BIDI_CHAR_RLI : TBOX_BIDI_CHAR_LRI;
        open[1]  = rtl ? TBOX_BIDI_CHAR_RLO : TBOX_BIDI_CHAR_LRO;
        close[0] = TBOX_BIDI_CHAR_PDF;
        close[1] = TBOX_BIDI_CHAR_PDI;
        n        = 2;
        break;
    case TBOX_STYLE_UNICODE_BIDI_PLAINTEXT:
        open[0]  = TBOX_BIDI_CHAR_FSI;
        close[0] = TBOX_BIDI_CHAR_PDI;
        break;
    case TBOX_STYLE_UNICODE_BIDI_NORMAL:
    default:
        return;
    }
    tbox_layout_word *head = (tbox_layout_word *)tbox_vector_at(words, first);
    tbox_layout_word *tail = (tbox_layout_word *)tbox_vector_at(words, words->length - 1);
    /* Deeper nesting than the word can hold is dropped whole (both ends). */
    if (head->bidi_open_count + n > TBOX_LAYOUT_BIDI_MARKS || tail->bidi_close_count + n > TBOX_LAYOUT_BIDI_MARKS)
        return;
    memmove(head->bidi_open + n, head->bidi_open, head->bidi_open_count * sizeof(uint32_t));
    memcpy(head->bidi_open, open, n * sizeof(uint32_t));
    head->bidi_open_count = (uint8_t)(head->bidi_open_count + n);
    memcpy(tail->bidi_close + tail->bidi_close_count, close, n * sizeof(uint32_t));
    tail->bidi_close_count = (uint8_t)(tail->bidi_close_count + n);
}

static bool tbox_layout_word_has_text(const tbox_layout_word *word) {
    return !word->hard_break && word->atomic == NULL && word->image == NULL;
}

tbox_layout_word_bidi *tbox_layout_resolve_bidi(tbox_arena *arena, const tbox_layout_word *words, size_t count, const tbox_style *container) {
    if (count == 0 || container == NULL)
        return NULL;
    bool rtl       = container->direction_rtl;
    bool override  = container->unicode_bidi == TBOX_STYLE_UNICODE_BIDI_BIDI_OVERRIDE || container->unicode_bidi == TBOX_STYLE_UNICODE_BIDI_ISOLATE_OVERRIDE;
    bool plaintext = container->unicode_bidi == TBOX_STYLE_UNICODE_BIDI_PLAINTEXT;
    bool needed    = rtl || override || plaintext;
    for (size_t w = 0; w < count && !needed; w++) {
        const tbox_layout_word *word = &words[w];
        if (word->bidi_open_count > 0 || word->bidi_close_count > 0)
            needed = true;
        for (size_t at = 0; tbox_layout_word_has_text(word) && at < word->text.size && !needed;) {
            uint32_t codepoint;
            at     = tbox_utf8_decode(word->text.data, word->text.size, at, &codepoint);
            needed = codepoint >= 0x0590 && tbox_bidi_class_is_rtl_trigger(tbox_bidi_class_of(codepoint));
        }
    }
    if (!needed)
        return NULL;

    tbox_layout_word_bidi *result = (tbox_layout_word_bidi *)tbox_arena_alloc_zero(arena, count * sizeof(*result));
    if (result == NULL)
        return NULL;
    uint32_t active[TBOX_LAYOUT_BIDI_STACK];
    size_t active_count = 0, active_overflow = 0;
    uint8_t base        = plaintext ? TBOX_BIDI_LEVEL_AUTO : rtl ? 1 : 0;

    /* One paragraph per forced break: the hard-break word ends it, and the
     * marks still open are opened again in the next one (CSS Writing
     * Modes 3, 2.4.2). */
    for (size_t p0 = 0; p0 < count;) {
        size_t last = p0;
        while (last + 1 < count && !words[last].hard_break)
            last++;
        size_t capacity = 1 + active_count;
        for (size_t w = p0; w <= last; w++)
            capacity += words[w].text.size + 2 + words[w].bidi_open_count + words[w].bidi_close_count;
        uint32_t *text = malloc(capacity * sizeof(*text));
        uint8_t *levels = malloc(capacity);
        size_t *starts  = malloc((last - p0 + 1) * 2 * sizeof(*starts));
        if (text == NULL || levels == NULL || starts == NULL) {
            free(text);
            free(levels);
            free(starts);
            for (size_t w = p0; w <= last; w++)
                result[w] = (tbox_layout_word_bidi){ NULL, rtl, rtl, rtl };
            p0 = last + 1;
            continue;
        }
        size_t *gaps = starts + (last - p0 + 1);
        size_t n     = 0;
        if (override)
            text[n++] = rtl ? TBOX_BIDI_CHAR_RLO : TBOX_BIDI_CHAR_LRO;
        for (size_t i = 0; i < active_count; i++)
            text[n++] = active[i];
        for (size_t w = p0; w <= last; w++) {
            const tbox_layout_word *word = &words[w];
            gaps[w - p0]                 = SIZE_MAX;
            if (w != p0 && !word->no_space_before && !word->hard_break) {
                gaps[w - p0] = n;
                text[n++]    = ' ';
            }
            for (size_t i = 0; i < word->bidi_open_count; i++) {
                text[n++] = word->bidi_open[i];
                if (active_count < TBOX_LAYOUT_BIDI_STACK)
                    active[active_count++] = word->bidi_open[i];
                else
                    active_overflow++;
            }
            starts[w - p0] = n;
            if (word->atomic != NULL || word->image != NULL)
                text[n++] = 0xFFFC; /* an atomic inline is a neutral */
            else if (!word->hard_break)
                for (size_t at = 0; at < word->text.size;)
                    at = tbox_utf8_decode(word->text.data, word->text.size, at, &text[n++]);
            for (size_t i = 0; i < word->bidi_close_count; i++) {
                text[n++] = word->bidi_close[i];
                if (active_overflow > 0)
                    active_overflow--;
                else if (active_count > 0)
                    active_count--;
            }
        }
        uint8_t paragraph_level;
        tbox_bidi_resolve(text, n, base, levels, &paragraph_level);

        for (size_t w = p0; w <= last; w++) {
            const tbox_layout_word *word = &words[w];
            tbox_layout_word_bidi *slot  = &result[w];
            slot->paragraph_level        = paragraph_level;
            slot->gap_level              = gaps[w - p0] != SIZE_MAX ? levels[gaps[w - p0]] : paragraph_level;
            slot->level                  = paragraph_level;
            size_t k                     = starts[w - p0];
            if (word->atomic != NULL || word->image != NULL) {
                slot->level = levels[k];
                continue;
            }
            if (word->hard_break || word->text.size == 0)
                continue;
            slot->level  = levels[k];
            bool uniform = true;
            for (size_t at = 0, j = k; at < word->text.size; j++) {
                at = tbox_utf8_next(word->text.data, word->text.size, at);
                if (levels[j] != slot->level)
                    uniform = false;
            }
            if (uniform)
                continue;
            uint8_t *bytes = (uint8_t *)tbox_arena_alloc(arena, word->text.size);
            if (bytes == NULL)
                continue;
            for (size_t at = 0, j = k; at < word->text.size; j++) {
                size_t next = tbox_utf8_next(word->text.data, word->text.size, at);
                memset(bytes + at, levels[j], next - at);
                at = next;
            }
            slot->levels = bytes;
        }
        free(text);
        free(levels);
        free(starts);
        p0 = last + 1;
    }
    return result;
}

bool tbox_layout_line_is_bidi(const tbox_layout_word *words, const tbox_layout_word_bidi *bidi, const tbox_layout_line *line) {
    for (size_t w = line->start; w < line->end; w++) {
        if (words[w].hard_break)
            continue;
        if (bidi[w].level != 0 || bidi[w].levels != NULL || (w != line->start && bidi[w].gap_level != 0))
            return true;
    }
    return false;
}

typedef enum tbox_layout_piece_kind {
    TBOX_LAYOUT_PIECE_GAP,
    TBOX_LAYOUT_PIECE_TEXT,
    TBOX_LAYOUT_PIECE_OBJECT, /* an image or an inline-block */
} tbox_layout_piece_kind;

/* A same-level stretch of one word (or the space before it), in logical
 * order, shaped on its own at x = 0. */
typedef struct tbox_layout_piece {
    size_t word;
    tbox_layout_piece_kind kind;
    uint8_t level;
    bool bare;             /* a gap between words of different styles: no run */
    tbox_string_view text; /* what it adds to its run's text */
    double width;
    double x;              /* visual, from the line start */
    double leading_edge, trailing_edge; /* decoration around a visual fragment */
    size_t text_offset;    /* in its run's text */
    tbox_vector glyphs;
} tbox_layout_piece;

static tbox_font_direction tbox_layout_level_direction(uint8_t level) {
    return (level & 1) ? TBOX_FONT_DIRECTION_RTL : TBOX_FONT_DIRECTION_LTR;
}

static tbox_layout_piece *tbox_layout_new_piece(tbox_vector *pieces, tbox_arena *arena, size_t word, tbox_layout_piece_kind kind, uint8_t level) {
    tbox_layout_piece *piece = (tbox_layout_piece *)tbox_vector_push(pieces);
    if (piece == NULL)
        return NULL;
    *piece = (tbox_layout_piece){ .word = word, .kind = kind, .level = level };
    tbox_vector_init(&piece->glyphs, arena, sizeof(tbox_glyph), 0);
    return piece;
}

/* One run from the visually consecutive pieces group[0..count) (same face
 * and style): text in logical order, glyphs in visual order. */
static void tbox_layout_emit_bidi_run(tbox_arena *arena, tbox_layout_piece *pieces, size_t *group, size_t count, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, const tbox_font_face *block_face, double end_x, tbox_vector *runs) {
    if (count == 0)
        return;
    size_t *logical = (size_t *)tbox_arena_alloc(arena, count * sizeof(*logical));
    if (logical == NULL)
        return;
    memcpy(logical, group, count * sizeof(*logical));
    for (size_t i = 1; i < count; i++)
        for (size_t j = i; j > 0 && logical[j - 1] > logical[j]; j--) {
            size_t swap    = logical[j];
            logical[j]     = logical[j - 1];
            logical[j - 1] = swap;
        }
    tbox_string_builder text;
    tbox_string_builder_init(&text, arena, 16);
    for (size_t i = 0; i < count; i++) {
        pieces[logical[i]].text_offset = text.length;
        tbox_string_builder_append_view(&text, pieces[logical[i]].text);
    }
    double start_x = pieces[group[0]].x;
    tbox_vector glyphs;
    tbox_vector_init(&glyphs, arena, sizeof(tbox_glyph), 0);
    for (size_t i = 0; i < count; i++) {
        const tbox_layout_piece *piece = &pieces[group[i]];
        for (size_t g = 0; g < piece->glyphs.length; g++) {
            tbox_glyph *slot = (tbox_glyph *)tbox_vector_push(&glyphs);
            if (slot == NULL)
                break;
            *slot = *(const tbox_glyph *)tbox_vector_at_const(&piece->glyphs, g);
            slot->x += piece->x - start_x;
            slot->cluster += (uint32_t)piece->text_offset;
        }
    }
    const tbox_layout_word *word = &words[pieces[group[0]].word];
    tbox_layout_push_run(arena, runs, line, line_y, content_x, block_face, start_x, end_x, word->face, word->style, NULL, 0.0, tbox_string_builder_finish(&text), &glyphs);
}

double tbox_layout_build_bidi_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_word_bidi *bidi, const tbox_layout_line *line, double line_y, double content_x, double available_width, size_t word_count, const tbox_font_face *block_face, double justify_gap, tbox_vector *runs) {
    tbox_vector pieces;
    tbox_vector_init(&pieces, arena, sizeof(tbox_layout_piece), 0);
    size_t previous = SIZE_MAX;
    for (size_t i = line->start; i < line->end; i++) {
        const tbox_layout_word *word   = &words[i];
        const tbox_layout_word_bidi *b = &bidi[i];
        if (word->floating != NULL) continue;
        size_t before_index = previous;
        previous = i;
        if (before_index != SIZE_MAX) {
            bool justified = justify_gap != 0.0 && word->space_width > 0.0;
            double gap     = word->space_width + (justified ? justify_gap : 0.0);
            if (gap != 0.0 || !word->no_space_before) {
                tbox_layout_piece *piece = tbox_layout_new_piece(&pieces, arena, i, TBOX_LAYOUT_PIECE_GAP, b->gap_level);
                if (piece != NULL) {
                    const tbox_layout_word *before = &words[before_index];
                    piece->width                   = gap;
                    /* like the left-to-right path, a space joins a run only
                     * between two words of the same face and style */
                    piece->bare = !tbox_layout_word_has_text(before) || before->face != word->face || before->style != word->style;
                    if (!piece->bare && !word->no_space_before && word->face != NULL) {
                        piece->text = tbox_string_view_make(" ", 1);
                        tbox_layout_append_glyphs(arena, &piece->glyphs, word->face, piece->text, word->style->letter_spacing, tbox_layout_level_direction(b->gap_level), 0.0, 0);
                    }
                }
            }
        }
        if (word->hard_break)
            continue;
        if (word->atomic != NULL || word->image != NULL) {
            tbox_layout_piece *piece = tbox_layout_new_piece(&pieces, arena, i, TBOX_LAYOUT_PIECE_OBJECT, b->level);
            if (piece != NULL) {
                piece->width = word->width;
                piece->text  = word->text;
            }
            continue;
        }
        if (word->text.size == 0 || word->face == NULL)
            continue;
        /* L1: whitespace ending the line goes back to the paragraph level. */
        size_t trailing = word->text.size;
        if (i + 1 == line->end)
            while (trailing > 0 && (word->text.data[trailing - 1] == ' ' || word->text.data[trailing - 1] == '\t'))
                trailing--;
        bool hyphen = word->soft_hyphen_after && i + 1 == line->end;
        for (size_t start = 0; start < word->text.size;) {
            uint8_t level = start >= trailing ? b->paragraph_level : b->levels != NULL ? b->levels[start] : b->level;
            size_t end    = start;
            while (end < word->text.size) {
                uint8_t at_level = end >= trailing ? b->paragraph_level : b->levels != NULL ? b->levels[end] : b->level;
                if (at_level != level)
                    break;
                end = tbox_utf8_next(word->text.data, word->text.size, end);
            }
            tbox_layout_piece *piece = tbox_layout_new_piece(&pieces, arena, i, TBOX_LAYOUT_PIECE_TEXT, level);
            if (piece == NULL)
                break;
            piece->text = tbox_string_view_make(word->text.data + start, end - start);
            if (hyphen && end == word->text.size) {
                /* A line broken at a soft hyphen shows the hyphen
                 * (hyphenate-character, "-" when auto). */
                tbox_string_view mark = word->style->hyphenate_character[0] != '\0' ? tbox_string_view_from_cstr(word->style->hyphenate_character) : tbox_string_view_make("-", 1);
                tbox_string_builder joined;
                tbox_string_builder_init(&joined, arena, piece->text.size + mark.size);
                tbox_string_builder_append_view(&joined, piece->text);
                tbox_string_builder_append_view(&joined, mark);
                piece->text = tbox_string_builder_finish(&joined);
            }
            piece->width = tbox_layout_append_glyphs(arena, &piece->glyphs, word->face, piece->text, word->style->letter_spacing, tbox_layout_level_direction(level), 0.0, 0);
            start        = end;
        }
    }

    size_t count = pieces.length;
    if (count == 0)
        return 0.0;
    tbox_layout_piece *items = (tbox_layout_piece *)pieces.data;
    uint8_t *levels          = (uint8_t *)tbox_arena_alloc(arena, count);
    size_t *order            = (size_t *)tbox_arena_alloc(arena, count * sizeof(*order));
    size_t *group            = (size_t *)tbox_arena_alloc(arena, count * sizeof(*group));
    if (levels == NULL || order == NULL || group == NULL)
        return 0.0;
    for (size_t p = 0; p < count; p++)
        levels[p] = items[p].level;
    tbox_bidi_reorder(levels, count, order);

    /* Decorations surround visual same-style fragments, independently of
     * the face/level groups used for shaping. Slice keeps element ends;
     * RTL's logical first/last correspond to physical right/left. */
    for (size_t v = 0; v < count;) {
        tbox_layout_piece *head = &items[order[v]];
        if (head->bare || head->kind == TBOX_LAYOUT_PIECE_OBJECT) { v++; continue; }
        const tbox_layout_word *word = &words[head->word];
        size_t end = v + 1, first_word = head->word;
        size_t last_word = first_word;
        while (end < count) {
            const tbox_layout_piece *next = &items[order[end]];
            if (next->bare || next->kind == TBOX_LAYOUT_PIECE_OBJECT || words[next->word].style != word->style) break;
            if (next->word < first_word) first_word = next->word;
            if (next->word > last_word) last_word = next->word;
            end++;
        }
        bool first = tbox_layout_inline_first(words, first_word);
        bool last = tbox_layout_inline_last(words, word_count, last_word);
        head->leading_edge = tbox_layout_inline_edge(word, true, word->style->direction_rtl ? last : first, available_width);
        items[order[end - 1]].trailing_edge = tbox_layout_inline_edge(word, false, word->style->direction_rtl ? first : last, available_width);
        v = end;
    }

    double x            = 0.0;
    size_t group_count  = 0;
    for (size_t v = 0; v < count; v++) {
        tbox_layout_piece *piece     = &items[order[v]];
        const tbox_layout_word *word = &words[piece->word];
        if (piece->leading_edge != 0.0) {
            tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
            group_count = 0;
            x += piece->leading_edge;
        }
        piece->x                     = x;
        if (piece->bare) {
            tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
            group_count = 0;
            x += piece->width;
            continue;
        }
        if (piece->kind == TBOX_LAYOUT_PIECE_OBJECT) {
            tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
            group_count = 0;
            if (word->atomic != NULL) {
                double y = tbox_layout_atomic_top(word->style, word->face, word->atomic_ascent, word->image_height, line, line_y, block_face);
                tbox_layout_translate(word->atomic, content_x + x - word->atomic->margin_box.x, y - word->atomic->margin_box.y);
            } else {
                tbox_vector none;
                tbox_vector_init(&none, arena, sizeof(tbox_glyph), 0);
                tbox_layout_push_run(arena, runs, line, line_y, content_x, block_face, x, x + piece->width, word->face, word->style, word->image, word->image_height, word->text, &none);
            }
            x += piece->width;
            continue;
        }
        if (group_count > 0) {
            const tbox_layout_word *open = &words[items[group[0]].word];
            if (open->face != word->face || open->style != word->style) {
                tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
                group_count = 0;
            }
        }
        group[group_count++] = order[v];
        x += piece->width;
        if (piece->trailing_edge != 0.0) {
            tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
            group_count = 0;
            x += piece->trailing_edge;
        }
    }
    tbox_layout_emit_bidi_run(arena, items, group, group_count, words, line, line_y, content_x, block_face, x, runs);
    return x;
}

double tbox_layout_bidi_caret_x(const tbox_font_face *face, double letter_spacing, tbox_string_view text, size_t offset, bool rtl) {
    bool needed = rtl;
    for (size_t at = 0; at < text.size && !needed;) {
        uint32_t codepoint;
        at     = tbox_utf8_decode(text.data, text.size, at, &codepoint);
        needed = codepoint >= 0x0590 && tbox_bidi_class_is_rtl_trigger(tbox_bidi_class_of(codepoint));
    }
    if (!needed || face == NULL || text.size == 0)
        return tbox_text_prefix_width(face, letter_spacing, text, offset);

    size_t count       = tbox_string_view_codepoint_count(text);
    uint32_t *cps      = malloc(count * sizeof(*cps));
    uint8_t *levels    = malloc(count + 1);
    size_t *starts     = malloc((count + 1) * sizeof(*starts));
    uint8_t *run_level = malloc(count);
    size_t *order      = malloc(count * sizeof(*order));
    double *widths     = malloc(count * sizeof(*widths));
    double result      = 0.0;
    if (cps != NULL && levels != NULL && starts != NULL && run_level != NULL && order != NULL && widths != NULL) {
        size_t n = 0;
        for (size_t at = 0; at < text.size; n++) {
            starts[n] = at;
            at        = tbox_utf8_decode(text.data, text.size, at, &cps[n]);
        }
        starts[n] = text.size;
        uint8_t paragraph;
        tbox_bidi_resolve(cps, n, rtl ? 1 : 0, levels, &paragraph);
        tbox_bidi_reset_line(cps, 0, n, paragraph, levels);
        /* same-level stretches, as the line builder cuts them */
        size_t *bounds = malloc((n + 1) * sizeof(*bounds));
        if (bounds != NULL) {
            size_t runs             = 0;
            const size_t *run_start = starts; /* byte offset of each codepoint */
            for (size_t i = 0; i < n; i++)
                if (i == 0 || levels[i] != levels[i - 1]) {
                    bounds[runs]    = i;
                    run_level[runs] = levels[i];
                    runs++;
                }
            bounds[runs] = n;
            for (size_t r = 0; r < runs; r++)
                widths[r] = tbox_font_measure_text_spaced(face, tbox_string_view_make(text.data + run_start[bounds[r]], run_start[bounds[r + 1]] - run_start[bounds[r]]), letter_spacing);
            tbox_bidi_reorder(run_level, runs, order);
            /* the stretch holding the character after the caret, or the
             * last one (its end edge) at the end of the text */
            size_t target = runs - 1;
            for (size_t r = 0; r < runs; r++)
                if (offset >= run_start[bounds[r]] && offset < run_start[bounds[r + 1]])
                    target = r;
            double x = 0.0;
            for (size_t v = 0; v < runs; v++) {
                size_t r = order[v];
                if (r == target) {
                    tbox_string_view piece = tbox_string_view_make(text.data + run_start[bounds[r]], run_start[bounds[r + 1]] - run_start[bounds[r]]);
                    size_t inside          = offset > run_start[bounds[r]] ? offset - run_start[bounds[r]] : 0;
                    if (inside > piece.size)
                        inside = piece.size;
                    double before = tbox_text_prefix_width(face, letter_spacing, piece, inside);
                    result        = (run_level[r] & 1) ? x + widths[r] - before : x + before;
                    break;
                }
                x += widths[r];
            }
            free(bounds);
        }
    }
    free(cps);
    free(levels);
    free(starts);
    free(run_level);
    free(order);
    free(widths);
    return result;
}
