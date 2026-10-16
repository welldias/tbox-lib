#ifndef TBOX_FONT_MEASURE_H
#define TBOX_FONT_MEASURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/font.h>
#include <tbox/string_view.h>

#include "base/tbox_arena.h"

/* Prefix widths (docs/09-texto-e-fontes.md, "Largura de cada prefixo").
 * init shapes the whole text once; each step then advances one codepoint
 * and `width` becomes the width of data[0..at) *within that shaping* (a
 * cluster spanning several codepoints, such as a ligature, has its width
 * split evenly among them). After the last step `width` equals
 * tbox_font_measure_text_spaced of the whole text. One shaping plus O(n)
 * steps; tbox_text_measurer_finish frees it. Do not copy a measurer. */
typedef struct tbox_text_measurer {
    const char *data;
    size_t length;
    size_t at;      /* byte offset reached */
    size_t index;   /* codepoints stepped */
    size_t count;   /* codepoints in data */
    double width;   /* of data[0..at) */
    double *prefix; /* count + 1 widths; NULL (all widths 0) for a NULL face or on allocation failure */
} tbox_text_measurer;

void tbox_text_measurer_init(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length);

/* Measures `data` as if every codepoint were `substitute` (a password
 * mask), shaped as such. */
void tbox_text_measurer_init_masked(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length, uint32_t substitute);

/* Advances one codepoint; returns the new `at` (`length` once done). */
size_t tbox_text_measurer_step(tbox_text_measurer *measurer);

/* The width of the whole text, without stepping. */
double tbox_text_measurer_total(const tbox_text_measurer *measurer);

void tbox_text_measurer_finish(tbox_text_measurer *measurer);

/* The width of text[0..offset) within the shaping of all of `text` -- a
 * caret or selection edge. */
double tbox_text_prefix_width(const tbox_font_face *face, double letter_spacing, tbox_string_view text, size_t offset);

/* Shapes `text` (docs/09-texto-e-fontes.md, "Shaping") into glyphs
 * allocated in `arena`, at x = origin_x + their pen position and cluster =
 * cluster_base + the byte offset in `text`. *out_width receives the width
 * (may be NULL). Empty text or a NULL face gives no glyphs and returns
 * true; false on allocation failure. */
bool tbox_font_shape(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                     double origin_x, uint32_t cluster_base, tbox_glyph **out_glyphs, size_t *out_count, double *out_width);

/* tbox_font_shape at x = 0 into a run's or a paint op's glyphs/glyph_count.
 * On allocation failure they stay empty and Output shapes the text itself. */
void tbox_font_shape_into(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                          const tbox_glyph **out_glyphs, size_t *out_count);

#endif /* TBOX_FONT_MEASURE_H */
