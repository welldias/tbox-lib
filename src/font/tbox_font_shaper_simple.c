/* The shaper without HarfBuzz (docs/09-texto-e-fontes.md, "Shaping"): one
 * glyph per codepoint, positioned by FreeType advances and its `kern`
 * table. Compiled only when HarfBuzz is not found or TBOX_ENABLE_HARFBUZZ
 * is OFF. */
#include "tbox_font_face_internal.h"

#include "base/tbox_string.h"

bool tbox_font_shaper_attach(tbox_font_face *face) {
    face->shaper = NULL;
    return true;
}

void tbox_font_shaper_detach(tbox_font_face *face) {
    (void)face;
}

size_t tbox_font_shaper_run(tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_glyph *out, size_t capacity, double *out_width) {
    double pen        = 0.0;
    uint32_t previous = 0;
    size_t count      = 0;
    for (size_t at = 0; at < text.size;) {
        size_t start = at;
        uint32_t codepoint;
        at = tbox_utf8_decode(text.data, text.size, at, &codepoint);
        pen += tbox_font_face_kerning(face, previous, codepoint);
        previous   = codepoint;
        FT_UInt id = FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint);
        /* 26.6 fixed-point advance; a glyph that fails to load only takes
         * the letter-spacing. */
        double advance = FT_Load_Glyph(face->ft_face, id, FT_LOAD_DEFAULT) == 0 ?
            face->ft_face->glyph->advance.x / 64.0 + letter_spacing : letter_spacing;
        if (count < capacity)
            out[count] = (tbox_glyph){ .id = id, .cluster = (uint32_t)start, .x = pen, .y = 0.0, .advance = advance };
        count++;
        pen += advance;
    }
    *out_width = pen;
    return count;
}
