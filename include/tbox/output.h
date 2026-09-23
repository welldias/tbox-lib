#ifndef TBOX_OUTPUT_H
#define TBOX_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/render.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform-independent software rasterizer over caller-allocated XRGB8888
 * pixels (0xFFRRGGBB). Window backends are private to src/output. */

/* Fills `rect` (clipped to the buffer's [0, buffer_width) x [0, buffer_height)
 * bounds -- rect may legitimately extend past any edge, or lie entirely
 * outside) with `color`, alpha-blended over whatever is already in `pixels`
 * using the standard "over" operator per channel:
 *   result = src * alpha + dst * (1 - alpha), alpha = color.a / 255.0
 * color.a == 255 short-circuits to a plain overwrite; color.a == 0 is a
 * no-op. `pixels` must hold at least buffer_width * buffer_height entries,
 * row-major, no padding between rows. A NULL `pixels`, or a non-positive
 * buffer_width/buffer_height, is a no-op. */
void tbox_raster_fill_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, tbox_css_rgba color);

/* Decodes `text` as UTF-8 and rasterizes it as a single line (v0: no
 * wrapping, matching tbox_font_measure_text/tbox_paint_op's TEXT_RUN scope)
 * into `pixels`, one glyph at a time via tbox_font_rasterize_glyph(face, ...).
 * `origin` is the TEXT_RUN's content-box origin (only origin.x/origin.y are
 * read, matching tbox_paint_op's rect for TEXT_RUN -- width/height are
 * ignored). The baseline is origin.y + tbox_font_face_ascent(face), constant
 * for the whole call; each glyph is composited at
 * (pen_x + bearing_x, baseline_y - bearing_y) and the pen then advances by
 * the glyph's `advance`. Each covered pixel is alpha-blended (same "over"
 * formula as tbox_raster_fill_rect) using
 * (glyph_alpha / 255.0) * (color.a / 255.0) as the combined alpha against
 * `color`'s RGB, clipped to the buffer's bounds exactly like
 * tbox_raster_fill_rect. A NULL `pixels`/`face`, a non-positive
 * buffer_width/buffer_height, an empty `text`, or color.a == 0 is a no-op. */
void tbox_raster_text_run(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color);

/* Composites `image`'s decoded RGBA8 pixels (see <tbox/image.h>) into
 * `dest_rect`. When `dest_rect`'s pixel size differs from `image->width`/
 * `height` (e.g. an `<img>`'s CSS-resolved width/height differing from its
 * source file's own dimensions), the source is first resampled to that
 * exact size via stb_image_resize2 (Mitchell/cubic, sRGB-aware -- real
 * quality resampling, not nearest-neighbor); when the sizes already match,
 * `image`'s own pixels are used directly with no resize step. Each
 * resulting pixel is alpha-blended (same "over" formula as
 * tbox_raster_fill_rect) using (source_pixel.a / 255.0) as the alpha
 * against that pixel's own RGB, clipped to the buffer's bounds exactly like
 * tbox_raster_fill_rect. A NULL `pixels`/`image`, a non-positive
 * buffer_width/buffer_height, a non-positive dest_rect.width/height, or a
 * resize failure (allocation failure), is a no-op. */
void tbox_raster_image(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect dest_rect, const tbox_image *image);

/* NOVO (visual fidelity): same as tbox_raster_fill_rect above, but with
 * rounded corners -- used for `border-radius`/`box-shadow` (see
 * tbox_paint_op.radius, <tbox/render.h>). Per-pixel: any pixel in `rect`'s
 * own bounding box (clamped to the buffer, same as tbox_raster_fill_rect)
 * is painted UNLESS it falls within one of the 4 `radius x radius` corner
 * squares AND lies outside that corner's circle (`dx*dx + dy*dy >
 * radius*radius`, distance from the circle's own center, `radius` px
 * inset from that corner) -- a hard edge, no anti-aliasing, matching
 * tbox_raster_fill_rect's own hard rectangular edges (this rasterizer has
 * no other anti-aliased fill to be consistent with; only glyph rendering,
 * via FreeType's own coverage bitmaps, is anti-aliased). `radius` is
 * clamped here to at most half of `min(rect.width, rect.height)`. A NULL
 * `pixels`, a non-positive buffer_width/buffer_height/rect.width/
 * rect.height, or color.a == 0, is a no-op; `radius <= 0.0` degenerates to
 * exactly tbox_raster_fill_rect's own output. */
void tbox_raster_fill_rounded_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, double radius, tbox_css_rgba color);

/* Convenience: walks `list->items` in order and dispatches each op to
 * tbox_raster_fill_rect or (NOVO, visual fidelity: op->radius > 0.0)
 * tbox_raster_fill_rounded_rect (TBOX_PAINT_FILL_RECT), tbox_raster_text_run
 * (TBOX_PAINT_TEXT_RUN), or tbox_raster_image (TBOX_PAINT_IMAGE) -- what a
 * backend's present/frame function calls once per frame instead of
 * switching on op->kind itself. A NULL `list` is a no-op. */
void tbox_raster_display_list(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list);

/* Writes `pixels` (buffer_width x buffer_height, XRGB8888 -- same layout
 * every tbox_raster_* function above reads/writes) to a PNG file at `path`:
 * color type 2 (truecolor, no alpha channel -- every pixel this rasterizer
 * ever produces is fully opaque, so the buffer's unused top byte is
 * dropped), 8 bits per channel. Self-contained: no libpng/zlib dependency
 * -- the IDAT stream uses uncompressed ("stored") DEFLATE blocks, a valid
 * (if larger than a real compressor's output) encoding every PNG decoder
 * accepts, avoiding a new build dependency for what is a development/
 * testing tool, not part of tbox's core rendering path (see
 * tbox_app_screenshot_from_files in <tbox/app.h>, its main caller). Returns
 * false, leaving no partial file behind, if `pixels` is NULL,
 * buffer_width/buffer_height is non-positive, or the file can't be created/
 * written (bad path, no permission, disk full, ...); true on success. */
bool tbox_raster_write_png(const char *path, const uint32_t *pixels, int32_t buffer_width, int32_t buffer_height);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_OUTPUT_H */
