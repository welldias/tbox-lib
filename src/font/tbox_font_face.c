#include <tbox/font.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdlib.h>

#include "utf8.h"

struct tbox_font_face {
    FT_Face ft_face;
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

    FT_Face ft_face;
    if (FT_New_Memory_Face(library, (const FT_Byte *)font_data, (FT_Long)size, 0, &ft_face) != 0) {
        return NULL;
    }

    if (FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)(size_px + 0.5)) != 0) {
        FT_Done_Face(ft_face);
        return NULL;
    }

    tbox_font_face *face = malloc(sizeof(*face));
    if (face == NULL) {
        FT_Done_Face(ft_face);
        return NULL;
    }

    face->ft_face = ft_face;
    return face;
}

void tbox_font_face_destroy(tbox_font_face *face) {
    if (face == NULL) {
        return;
    }

    FT_Done_Face(face->ft_face);
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

double tbox_font_measure_text(const tbox_font_face *face, tbox_string_view text) {
    if (face == NULL || text.size == 0) {
        return 0.0;
    }

    double total       = 0.0;
    const char *cursor = text.data;
    const char *end    = text.data + text.size;

    while (cursor < end) {
        utf8_int32_t codepoint;
        cursor = utf8codepoint(cursor, &codepoint);

        if (FT_Load_Char(face->ft_face, (FT_ULong)codepoint, FT_LOAD_DEFAULT) != 0) {
            continue;
        }

        /* 26.6 fixed-point, no kerning/shaping -- see tbox_font_measure_text's
         * doc comment in <tbox/font.h>. */
        total += face->ft_face->glyph->advance.x / 64.0;
    }

    return total;
}

tbox_font_glyph_bitmap tbox_font_rasterize_glyph(tbox_font_face *face, uint32_t codepoint) {
    tbox_font_glyph_bitmap bitmap = { 0 };
    if (face == NULL) {
        return bitmap;
    }

    if (FT_Load_Char(face->ft_face, (FT_ULong)codepoint, FT_LOAD_DEFAULT) != 0) {
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
