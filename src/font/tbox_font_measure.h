#ifndef TBOX_FONT_MEASURE_H
#define TBOX_FONT_MEASURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/font.h>
#include <tbox/string_view.h>

#include "base/tbox_arena.h"

/* Incremental text measurement (docs/09-texto-e-fontes.md, "Medição
 * incremental"). Each step adds one codepoint -- kerning with the previous
 * one, its advance and letter-spacing -- in the same order
 * tbox_font_measure_text_spaced sums them (it is built on this), so
 * `width` is bit-identical to measuring the prefix stepped so far. Callers
 * that need every prefix width pay O(n) instead of O(n^2). */
typedef struct tbox_text_measurer {
    const tbox_font_face *face; /* NULL measures everything as 0 */
    double letter_spacing;
    uint32_t substitute; /* nonzero: every codepoint measures as this one */
    uint32_t previous;
    double width; /* of everything stepped since init */
    /* The glyph the last step produced: its index in `face`, its pen x
     * (kerning already applied, relative to init) and advance (with
     * letter-spacing). tbox_font_shape records these. */
    uint32_t glyph_id;
    double glyph_x, glyph_advance;
} tbox_text_measurer;

void tbox_text_measurer_init(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing);

/* Measures each codepoint as `substitute` (a password mask). */
void tbox_text_measurer_init_masked(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, uint32_t substitute);

/* Adds the codepoint at data[at] (at < length) and returns the offset just
 * past it (tbox_utf8_next). */
size_t tbox_text_measurer_step(tbox_text_measurer *measurer, const char *data, size_t length, size_t at);

/* The shaper (docs/09-texto-e-fontes.md, "Do texto aos glifos"): one
 * tbox_glyph per measurer step, allocated in `arena`, at x = origin_x + the
 * pen position and cluster = cluster_base + the byte offset in `text`.
 * *out_width receives the measured width (may be NULL). Today one glyph per
 * codepoint, so the array never exceeds text.size entries. Empty text or a
 * NULL face gives no glyphs and returns true; false on allocation failure. */
bool tbox_font_shape(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                     double origin_x, uint32_t cluster_base, tbox_glyph **out_glyphs, size_t *out_count, double *out_width);

/* tbox_font_shape at x = 0 into a run's or a paint op's glyphs/glyph_count.
 * On allocation failure they stay empty and Output shapes the text itself. */
void tbox_font_shape_into(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                          const tbox_glyph **out_glyphs, size_t *out_count);

#endif /* TBOX_FONT_MEASURE_H */
