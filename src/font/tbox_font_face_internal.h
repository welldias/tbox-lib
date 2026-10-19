#ifndef TBOX_FONT_FACE_INTERNAL_H
#define TBOX_FONT_FACE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#include <tbox/font.h>
#include <tbox/string_view.h>
#include "tbox_font_measure.h"

struct tbox_font_face {
    FT_Face ft_face;
    FT_Glyph stroke_glyph; /* owned auxiliary outline/bitmap, replaced per stroke */
    tbox_font_shaping_options options;
    bool kerning;           /* font-kerning, and the face has a FreeType `kern` table */
    bool kerning_requested; /* font-kerning as asked (the HarfBuzz shaper reads GPOS too) */
    /* FT_New_Memory_Face never copies its input -- it keeps the pointer and
     * reads from it lazily (e.g. cmap parsing on first use), well past this
     * call returning -- so tbox_font_face must own a copy the caller's
     * buffer isn't required to outlive. The HarfBuzz shaper reads it too. */
    void *font_data_copy;
    size_t font_data_size;
    void *shaper; /* the shaper backend's per-face state, NULL for none */
};

/* The shaper backend (docs/09-texto-e-fontes.md, "Shaping"): exactly one
 * of src/font/tbox_font_shaper_harfbuzz.c and tbox_font_shaper_simple.c is
 * compiled, chosen at configure time. */

/* Called once the FT_Face is ready; false fails tbox_font_face_load. */
bool tbox_font_shaper_attach(tbox_font_face *face);
void tbox_font_shaper_detach(tbox_font_face *face);

/* Shapes `text` from x = 0: writes up to `capacity` glyphs to `out`
 * (clusters are byte offsets in `text`; `out` may be NULL when capacity is
 * 0) and returns how many glyphs the whole text takes -- a caller whose
 * buffer was too small calls again with a larger one. *out_width receives
 * the total advance, letter-spacing included. */
size_t tbox_font_shaper_run(tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, tbox_glyph *out, size_t capacity, double *out_width);

#endif /* TBOX_FONT_FACE_INTERNAL_H */
