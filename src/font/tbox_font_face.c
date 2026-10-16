#include <tbox/font.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"
#include "tbox_font_face_internal.h"
#include "tbox_font_measure.h"

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

    face->ft_face           = ft_face;
    face->font_data_copy    = font_data_copy;
    face->font_data_size    = size;
    face->kerning           = false;
    face->kerning_requested = false;
    face->shaper            = NULL;
    if (!tbox_font_shaper_attach(face)) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        free(face);
        return NULL;
    }
    return face;
}

void tbox_font_face_set_kerning(tbox_font_face *face, bool kerning) {
    if (face != NULL) {
        face->kerning_requested = kerning;
        face->kerning           = kerning && FT_HAS_KERNING(face->ft_face);
    }
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

    /* The shaper and FT_Done_Face must run first: they stop reading
     * font_data_copy only once they return. */
    tbox_font_shaper_detach(face);
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

/* Shapes `text` into a malloc'd array, growing it once if the shaper
 * reports more glyphs than the first guess. NULL (and *out_count 0) for
 * empty text, a NULL face or an allocation failure. */
static tbox_glyph *tbox_font_shape_malloc(const tbox_font_face *face, tbox_string_view text, double letter_spacing, size_t *out_count, double *out_width) {
    *out_count = 0;
    *out_width = 0.0;
    if (face == NULL || text.size == 0)
        return NULL;
    tbox_font_face *mutable_face = (tbox_font_face *)face; /* the shaper reuses per-face buffers */
    size_t capacity              = text.size + 8;
    tbox_glyph *glyphs           = malloc(capacity * sizeof(*glyphs));
    if (glyphs == NULL)
        return NULL;
    size_t count = tbox_font_shaper_run(mutable_face, text, letter_spacing, glyphs, capacity, out_width);
    if (count > capacity) {
        tbox_glyph *grown = realloc(glyphs, count * sizeof(*glyphs));
        if (grown == NULL) {
            free(glyphs);
            return NULL;
        }
        glyphs = grown;
        tbox_font_shaper_run(mutable_face, text, letter_spacing, glyphs, count, out_width);
    }
    *out_count = count;
    return glyphs;
}

/* prefix[i] = the width of the first i codepoints of `text` within its
 * shaping: each cluster's advance is split evenly among its codepoints,
 * the last one taking the exact remainder. */
static void tbox_text_measurer_fill(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, tbox_string_view text) {
    size_t glyph_count;
    double total;
    tbox_glyph *glyphs = tbox_font_shape_malloc(face, text, letter_spacing, &glyph_count, &total);
    double *cluster_width = text.size > 0 ? calloc(text.size, sizeof(*cluster_width)) : NULL;
    bool *cluster_start   = text.size > 0 ? calloc(text.size, sizeof(*cluster_start)) : NULL;
    measurer->prefix      = malloc((measurer->count + 1) * sizeof(*measurer->prefix));
    if (measurer->prefix == NULL || (text.size > 0 && (cluster_width == NULL || cluster_start == NULL))) {
        free(measurer->prefix);
        measurer->prefix = NULL;
    } else {
        for (size_t g = 0; g < glyph_count; g++) {
            if (glyphs[g].cluster < text.size) {
                cluster_width[glyphs[g].cluster] += glyphs[g].advance;
                cluster_start[glyphs[g].cluster] = true;
            }
        }
        measurer->prefix[0] = 0.0;
        size_t index        = 0;
        double before       = 0.0;
        for (size_t at = 0; at < text.size;) {
            /* one cluster: its first codepoint and the ones that follow
             * until the next cluster start */
            size_t first   = index;
            double width   = cluster_width[at];
            size_t members = 0;
            do {
                at = tbox_utf8_next(text.data, text.size, at);
                members++;
            } while (at < text.size && !cluster_start[at]);
            for (size_t k = 1; k <= members; k++)
                measurer->prefix[first + k] = k == members ? before + width : before + width * (double)k / (double)members;
            index += members;
            before += width;
        }
        measurer->prefix[measurer->count] = glyph_count > 0 ? total : before;
    }
    free(cluster_width);
    free(cluster_start);
    free(glyphs);
}

void tbox_text_measurer_init(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length) {
    tbox_text_measurer_init_masked(measurer, face, letter_spacing, data, length, 0);
}

void tbox_text_measurer_init_masked(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length, uint32_t substitute) {
    *measurer = (tbox_text_measurer){ .data = data, .length = length };
    measurer->count = tbox_string_view_codepoint_count(tbox_string_view_make(data, length));
    if (face == NULL)
        return;
    if (substitute == 0) {
        tbox_text_measurer_fill(measurer, face, letter_spacing, tbox_string_view_make(data, length));
        return;
    }
    /* The mask: one `substitute` per codepoint of `data`. */
    char unit[4];
    size_t unit_size = tbox_utf8_encode(unit, sizeof(unit), substitute);
    char *mask       = unit_size > 0 && measurer->count <= SIZE_MAX / unit_size ? malloc(measurer->count * unit_size + 1) : NULL;
    if (mask == NULL)
        return;
    for (size_t i = 0; i < measurer->count; i++)
        memcpy(mask + i * unit_size, unit, unit_size);
    tbox_text_measurer_fill(measurer, face, letter_spacing, tbox_string_view_make(mask, measurer->count * unit_size));
    free(mask);
}

size_t tbox_text_measurer_step(tbox_text_measurer *measurer) {
    if (measurer->at >= measurer->length)
        return measurer->length;
    measurer->at = tbox_utf8_next(measurer->data, measurer->length, measurer->at);
    measurer->index++;
    measurer->width = measurer->prefix != NULL ? measurer->prefix[measurer->index] : 0.0;
    return measurer->at;
}

double tbox_text_measurer_total(const tbox_text_measurer *measurer) {
    return measurer->prefix != NULL ? measurer->prefix[measurer->count] : 0.0;
}

void tbox_text_measurer_finish(tbox_text_measurer *measurer) {
    free(measurer->prefix);
    measurer->prefix = NULL;
}

double tbox_text_prefix_width(const tbox_font_face *face, double letter_spacing, tbox_string_view text, size_t offset) {
    tbox_text_measurer measurer;
    tbox_text_measurer_init(&measurer, face, letter_spacing, text.data, text.size);
    while (measurer.at < offset && measurer.at < measurer.length)
        tbox_text_measurer_step(&measurer);
    double width = measurer.width;
    tbox_text_measurer_finish(&measurer);
    return width;
}

bool tbox_font_shape(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                     double origin_x, uint32_t cluster_base, tbox_glyph **out_glyphs, size_t *out_count, double *out_width) {
    *out_glyphs = NULL;
    *out_count  = 0;
    if (out_width != NULL)
        *out_width = 0.0;
    if (face == NULL || text.size == 0)
        return true;
    tbox_font_face *mutable_face = (tbox_font_face *)face; /* the shaper reuses per-face buffers */
    size_t capacity              = text.size + 8;
    tbox_glyph *glyphs           = tbox_arena_alloc(arena, capacity * sizeof(*glyphs));
    if (glyphs == NULL)
        return false;
    double width;
    size_t count = tbox_font_shaper_run(mutable_face, text, letter_spacing, glyphs, capacity, &width);
    if (count > capacity) {
        glyphs = tbox_arena_alloc(arena, count * sizeof(*glyphs));
        if (glyphs == NULL)
            return false;
        tbox_font_shaper_run(mutable_face, text, letter_spacing, glyphs, count, &width);
    }
    for (size_t i = 0; i < count; i++) {
        glyphs[i].x += origin_x;
        glyphs[i].cluster += cluster_base;
    }
    *out_glyphs = glyphs;
    *out_count  = count;
    if (out_width != NULL)
        *out_width = width;
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
    double width;
    tbox_font_shaper_run((tbox_font_face *)face, text, letter_spacing, NULL, 0, &width);
    return width;
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
