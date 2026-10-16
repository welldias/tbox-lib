#include <tbox/font.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"
#include "tbox_font_measure.h"

struct tbox_font_face {
    FT_Face ft_face;
    bool kerning; /* apply the font's kerning pairs (font-kerning) */
    void *font_data_copy; /* FT_New_Memory_Face never copies its input -- it keeps
                            * the pointer and reads from it lazily (e.g. cmap
                            * parsing on first use), well past this call
                            * returning -- so tbox_font_face must own a copy
                            * the caller's buffer isn't required to outlive. */
};

/* One process-wide FT_Library, initialized lazily on first use. tbox has no
 * internal locking anywhere else, so this isn't made thread-safe either --
 * matching the rest of the codebase. Never torn down with FT_Done_FreeType:
 * v0 has no notion of shutting the whole library down, and every
 * tbox_font_face created against it is destroyed independently via
 * tbox_font_face_destroy (FT_Done_Face), which doesn't require the parent
 * FT_Library to still be freed. */
static FT_Library tbox_font_ft_library             = NULL;
static bool        tbox_font_ft_library_init_tried = false;

static FT_Library tbox_font_ft_library_get(void) {
    if (!tbox_font_ft_library_init_tried) {
        tbox_font_ft_library_init_tried = true;
        if (FT_Init_FreeType(&tbox_font_ft_library) != 0) {
            tbox_font_ft_library = NULL;
        }
    }

    return tbox_font_ft_library;
}

tbox_font_face *tbox_font_face_load(const void *font_data, size_t size, double size_px) {
    FT_Library library = tbox_font_ft_library_get();
    if (library == NULL || font_data == NULL) {
        return NULL;
    }

    void *font_data_copy = malloc(size);
    if (font_data_copy == NULL) {
        return NULL;
    }
    memcpy(font_data_copy, font_data, size);

    FT_Face ft_face;
    if (FT_New_Memory_Face(library, (const FT_Byte *)font_data_copy, (FT_Long)size, 0, &ft_face) != 0) {
        free(font_data_copy);
        return NULL;
    }

    if (FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)(size_px + 0.5)) != 0) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        return NULL;
    }

    tbox_font_face *face = malloc(sizeof(*face));
    if (face == NULL) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        return NULL;
    }

    face->ft_face        = ft_face;
    face->font_data_copy = font_data_copy;
    face->kerning        = false;
    return face;
}

void tbox_font_face_set_kerning(tbox_font_face *face, bool kerning) {
    if (face != NULL)
        face->kerning = kerning && FT_HAS_KERNING(face->ft_face);
}

double tbox_font_face_kerning(const tbox_font_face *face, uint32_t previous, uint32_t codepoint) {
    if (face == NULL || !face->kerning || previous == 0)
        return 0.0;
    FT_UInt left = FT_Get_Char_Index(face->ft_face, previous), right = FT_Get_Char_Index(face->ft_face, codepoint);
    FT_Vector delta;
    if (left == 0 || right == 0 || FT_Get_Kerning(face->ft_face, left, right, FT_KERNING_DEFAULT, &delta) != 0)
        return 0.0;
    return delta.x / 64.0;
}

void tbox_font_face_destroy(tbox_font_face *face) {
    if (face == NULL) {
        return;
    }

    /* FT_Done_Face must run first: FreeType stops reading font_data_copy
     * only once this returns. */
    FT_Done_Face(face->ft_face);
    free(face->font_data_copy);
    free(face);
}

double tbox_font_face_line_height(const tbox_font_face *face) {
    if (face == NULL) {
        return 0.0;
    }

    /* face->size->metrics.height is ascender - descender + line gap,
     * already scaled to the pixel size set by FT_Set_Pixel_Sizes, in 26.6
     * fixed-point. */
    return face->ft_face->size->metrics.height / 64.0;
}

double tbox_font_face_ascent(const tbox_font_face *face) {
    if (face == NULL) {
        return 0.0;
    }

    /* face->size->metrics.ascender is the distance from the baseline up to
     * the top of the line box, already scaled to the active pixel size, in
     * 26.6 fixed-point -- same conversion as tbox_font_face_line_height
     * above. */
    return face->ft_face->size->metrics.ascender / 64.0;
}

double tbox_font_face_x_height(const tbox_font_face *face) {
    if (face == NULL)
        return 0.0;
    const TT_OS2 *os2 = (const TT_OS2 *)FT_Get_Sfnt_Table(face->ft_face, FT_SFNT_OS2);
    if (os2 != NULL && os2->version >= 2 && os2->sxHeight > 0)
        return FT_MulFix(os2->sxHeight, face->ft_face->size->metrics.y_scale) / 64.0;
    if (FT_Load_Char(face->ft_face, 'x', FT_LOAD_DEFAULT) != 0)
        return 0.0;
    return face->ft_face->glyph->metrics.horiBearingY / 64.0;
}

bool tbox_font_face_has_glyph(const tbox_font_face *face, uint32_t codepoint) {
    return face != NULL && FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint) != 0;
}

void tbox_text_measurer_init(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing) {
    tbox_text_measurer_init_masked(measurer, face, letter_spacing, 0);
}

void tbox_text_measurer_init_masked(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, uint32_t substitute) {
    *measurer = (tbox_text_measurer){ .face = face, .letter_spacing = letter_spacing, .substitute = substitute };
}

size_t tbox_text_measurer_step(tbox_text_measurer *measurer, const char *data, size_t length, size_t at) {
    uint32_t codepoint;
    size_t next = tbox_utf8_decode(data, length, at, &codepoint);
    const tbox_font_face *face = measurer->face;
    if (face == NULL)
        return next;
    if (measurer->substitute != 0)
        codepoint = measurer->substitute;
    measurer->width += tbox_font_face_kerning(face, measurer->previous, codepoint);
    measurer->previous = codepoint;
    measurer->glyph_id = FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint);
    measurer->glyph_x  = measurer->width;

    /* FT_Load_Char is FT_Get_Char_Index + FT_Load_Glyph; loading by index
     * keeps measurement and tbox_font_rasterize_glyph_id on one path. */
    if (FT_Load_Glyph(face->ft_face, (FT_UInt)measurer->glyph_id, FT_LOAD_DEFAULT) != 0) {
        measurer->glyph_advance = measurer->letter_spacing;
        measurer->width += measurer->letter_spacing;
        return next;
    }

    /* 26.6 fixed-point, no shaping -- see tbox_font_measure_text's doc
     * comment in <tbox/font.h>. */
    measurer->glyph_advance = face->ft_face->glyph->advance.x / 64.0 + measurer->letter_spacing;
    measurer->width += measurer->glyph_advance;
    return next;
}

bool tbox_font_shape(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                     double origin_x, uint32_t cluster_base, tbox_glyph **out_glyphs, size_t *out_count, double *out_width) {
    *out_glyphs = NULL;
    *out_count  = 0;
    if (out_width != NULL)
        *out_width = 0.0;
    if (face == NULL || text.size == 0)
        return true;
    tbox_glyph *glyphs = tbox_arena_alloc(arena, text.size * sizeof(*glyphs));
    if (glyphs == NULL)
        return false;
    size_t count = 0;
    tbox_text_measurer measurer;
    tbox_text_measurer_init(&measurer, face, letter_spacing);
    for (size_t at = 0; at < text.size;) {
        size_t start    = at;
        at              = tbox_text_measurer_step(&measurer, text.data, text.size, at);
        glyphs[count++] = (tbox_glyph){
            .id = measurer.glyph_id, .cluster = cluster_base + (uint32_t)start,
            .x = origin_x + measurer.glyph_x, .y = 0.0, .advance = measurer.glyph_advance,
        };
    }
    *out_glyphs = glyphs;
    *out_count  = count;
    if (out_width != NULL)
        *out_width = measurer.width;
    return true;
}

void tbox_font_shape_into(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                          const tbox_glyph **out_glyphs, size_t *out_count) {
    tbox_glyph *glyphs;
    size_t count;
    if (!tbox_font_shape(arena, face, text, letter_spacing, 0.0, 0, &glyphs, &count, NULL)) {
        glyphs = NULL;
        count  = 0;
    }
    *out_glyphs = glyphs;
    *out_count  = count;
}

double tbox_font_measure_text_spaced(const tbox_font_face *face, tbox_string_view text, double letter_spacing) {
    if (face == NULL || text.size == 0) {
        return 0.0;
    }
    tbox_text_measurer measurer;
    tbox_text_measurer_init(&measurer, face, letter_spacing);
    for (size_t at = 0; at < text.size;)
        at = tbox_text_measurer_step(&measurer, text.data, text.size, at);
    return measurer.width;
}

double tbox_font_measure_text(const tbox_font_face *face, tbox_string_view text) {
    return tbox_font_measure_text_spaced(face, text, 0.0);
}

tbox_font_glyph_bitmap tbox_font_rasterize_glyph(tbox_font_face *face, uint32_t codepoint) {
    if (face == NULL) {
        return (tbox_font_glyph_bitmap){ 0 };
    }
    return tbox_font_rasterize_glyph_id(face, FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint));
}

tbox_font_glyph_bitmap tbox_font_rasterize_glyph_id(tbox_font_face *face, uint32_t glyph_id) {
    tbox_font_glyph_bitmap bitmap = { 0 };
    if (face == NULL) {
        return bitmap;
    }

    if (FT_Load_Glyph(face->ft_face, (FT_UInt)glyph_id, FT_LOAD_DEFAULT) != 0) {
        return bitmap;
    }

    FT_GlyphSlot slot = face->ft_face->glyph;
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        return bitmap;
    }

    bitmap.width     = (int)slot->bitmap.width;
    bitmap.height    = (int)slot->bitmap.rows;
    bitmap.bearing_x = slot->bitmap_left;
    bitmap.bearing_y = slot->bitmap_top;
    bitmap.advance   = slot->advance.x / 64.0;
    bitmap.alpha     = (bitmap.width > 0 && bitmap.height > 0) ? slot->bitmap.buffer : NULL;
    return bitmap;
}
