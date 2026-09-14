#include <tbox/output.h>

#include <math.h>

#include "utf8.h"

/* Blends `src` (0-255) over `dst` (0-255) using the standard "over" operator:
 * result = src*alpha + dst*(1-alpha). Rounds to nearest. */
static unsigned char tbox_raster_blend_channel(unsigned char src, unsigned char dst, double alpha) {
    double result = (double)src * alpha + (double)dst * (1.0 - alpha);
    return (unsigned char)(result + 0.5);
}

/* Alpha-blends `color` (with combined alpha `alpha`, already the product of
 * whatever coverage/opacity terms apply -- see callers) over `dst_pixel`
 * (an XRGB8888 pixel), returning the new XRGB8888 pixel. alpha <= 0 returns
 * `dst_pixel` unchanged; alpha >= 1 returns a plain opaque overwrite. */
static uint32_t tbox_raster_blend_pixel(uint32_t dst_pixel, tbox_css_rgba color, double alpha) {
    if (alpha <= 0.0) {
        return dst_pixel;
    }
    if (alpha >= 1.0) {
        return 0xFF000000u | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
    }

    unsigned char dst_r = (unsigned char)((dst_pixel >> 16) & 0xFFu);
    unsigned char dst_g = (unsigned char)((dst_pixel >> 8) & 0xFFu);
    unsigned char dst_b = (unsigned char)(dst_pixel & 0xFFu);

    unsigned char r = tbox_raster_blend_channel(color.r, dst_r, alpha);
    unsigned char g = tbox_raster_blend_channel(color.g, dst_g, alpha);
    unsigned char b = tbox_raster_blend_channel(color.b, dst_b, alpha);

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void tbox_raster_fill_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, tbox_css_rgba color) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || color.a == 0) {
        return;
    }

    int32_t x0 = (int32_t)floor(rect.x);
    int32_t y0 = (int32_t)floor(rect.y);
    int32_t x1 = (int32_t)floor(rect.x + rect.width);
    int32_t y1 = (int32_t)floor(rect.y + rect.height);

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > buffer_width) {
        x1 = buffer_width;
    }
    if (y1 > buffer_height) {
        y1 = buffer_height;
    }

    double alpha = color.a / 255.0;

    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)buffer_width;
        for (int32_t x = x0; x < x1; x++) {
            row[x] = tbox_raster_blend_pixel(row[x], color, alpha);
        }
    }
}

void tbox_raster_text_run(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || face == NULL || text.size == 0 || color.a == 0) {
        return;
    }

    /* tbox_font_rasterize_glyph mutates the FT_Face's internal glyph slot
     * (see <tbox/font.h>) so it takes a non-const tbox_font_face*, even
     * though from this function's point of view -- and tbox_paint_op's --
     * `face` is only ever read. Casting away const here is the same
     * logical-vs-bitwise-constness situation tbox_font_measure_text's own
     * FT_Load_Char call already lives with inside font.h's implementation. */
    tbox_font_face *mutable_face = (tbox_font_face *)face;

    double pen_x       = origin.x;
    double baseline_y  = origin.y + tbox_font_face_ascent(face);
    double color_alpha = color.a / 255.0;

    const char *cursor = text.data;
    const char *end     = text.data + text.size;

    while (cursor < end) {
        utf8_int32_t codepoint;
        cursor = utf8codepoint(cursor, &codepoint);

        tbox_font_glyph_bitmap glyph = tbox_font_rasterize_glyph(mutable_face, (uint32_t)codepoint);

        if (glyph.width > 0 && glyph.height > 0 && glyph.alpha != NULL) {
            int32_t glyph_x0 = (int32_t)floor(pen_x + glyph.bearing_x);
            int32_t glyph_y0 = (int32_t)floor(baseline_y - glyph.bearing_y);

            for (int gy = 0; gy < glyph.height; gy++) {
                int32_t py = glyph_y0 + gy;
                if (py < 0 || py >= buffer_height) {
                    continue;
                }

                const unsigned char *glyph_row = glyph.alpha + (size_t)gy * (size_t)glyph.width;
                uint32_t *pixel_row            = pixels + (size_t)py * (size_t)buffer_width;

                for (int gx = 0; gx < glyph.width; gx++) {
                    int32_t px = glyph_x0 + gx;
                    if (px < 0 || px >= buffer_width) {
                        continue;
                    }

                    unsigned char glyph_alpha = glyph_row[gx];
                    if (glyph_alpha == 0) {
                        continue;
                    }

                    double combined_alpha = (glyph_alpha / 255.0) * color_alpha;
                    pixel_row[px]         = tbox_raster_blend_pixel(pixel_row[px], color, combined_alpha);
                }
            }
        }

        pen_x += glyph.advance;
    }
}

void tbox_raster_display_list(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list) {
    if (list == NULL) {
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        const tbox_paint_op *op = &list->items[i];
        switch (op->kind) {
        case TBOX_PAINT_FILL_RECT:
            tbox_raster_fill_rect(pixels, buffer_width, buffer_height, op->rect, op->color);
            break;
        case TBOX_PAINT_TEXT_RUN:
            tbox_raster_text_run(pixels, buffer_width, buffer_height, op->rect, op->text, op->face, op->color);
            break;
        }
    }
}
