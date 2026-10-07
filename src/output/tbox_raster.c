#include <tbox/output.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf8.h"

/* stb_image_resize2 is third-party, vendored code (external/stb_image/, see
 * its own README.md) -- not held to this project's own -Wall -Wextra
 * -Wpedantic -Werror bar. Its implementation is pulled in exactly once,
 * here, guarded by pragmas so its own warnings never fail this project's
 * build -- same pattern src/image/tbox_image.c already uses for
 * stb_image.h's implementation. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

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

/* Whether (x, y) lies inside `rect` with elliptical corners: horizontal
 * radii `hr`, vertical radii `vr` (top-left, top-right, bottom-right,
 * bottom-left). A corner with either radius 0 is square. */
static bool tbox_raster_point_in_shape(double x, double y, tbox_rect rect, const double hr[4], const double vr[4]) {
    double rx = x - rect.x, ry = y - rect.y;
    if (rx < 0.0 || ry < 0.0 || rx >= rect.width || ry >= rect.height) return false;
    /* Every corner region holding the point must also hold it inside its
     * ellipse: with large radii (`100% 0 / 100% 0`) the regions overlap. */
    for (int corner = 0; corner < 4; corner++) {
        if (hr[corner] <= 0.0 || vr[corner] <= 0.0) continue;
        bool left = corner == 0 || corner == 3, top = corner == 0 || corner == 1;
        double center_x = left ? hr[corner] : rect.width - hr[corner];
        double center_y = top ? vr[corner] : rect.height - vr[corner];
        if ((left ? rx >= center_x : rx <= center_x) || (top ? ry >= center_y : ry <= center_y)) continue;
        double dx = (rx - center_x) / hr[corner], dy = (ry - center_y) / vr[corner];
        if (dx * dx + dy * dy > 1.0) return false;
    }
    return true;
}

/* NOVO (visual fidelity): same as tbox_raster_fill_rect above, but with
 * rounded corners -- see this function's own doc comment in
 * <tbox/output.h> for the exact per-pixel corner test. `radius <= 0.0`
 * (after clamping) degenerates to a plain call to tbox_raster_fill_rect, so
 * every other codepath in this file that already produces a correct plain
 * rectangle keeps doing so unchanged. */
static void tbox_raster_fill_rounded_rect_clipped(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, const double corners[4], tbox_css_rgba color, bool has_clip, tbox_rect clip) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || rect.width <= 0.0 || rect.height <= 0.0 || color.a == 0) {
        return;
    }

    double radius[4];
    for (size_t i = 0; i < 4; i++) radius[i] = corners[i] > 0.0 ? corners[i] : 0.0;
    double scale = 1.0;
    const double sums[4] = {radius[0] + radius[1], radius[2] + radius[3],
                            radius[0] + radius[3], radius[1] + radius[2]};
    const double limits[4] = {rect.width, rect.width, rect.height, rect.height};
    for (size_t i = 0; i < 4; i++)
        if (sums[i] > 0.0 && limits[i] / sums[i] < scale) scale = limits[i] / sums[i];
    for (size_t i = 0; i < 4; i++) radius[i] *= scale;

    if (radius[0] <= 0.0 && radius[1] <= 0.0 && radius[2] <= 0.0 && radius[3] <= 0.0 && !has_clip) {
        tbox_raster_fill_rect(pixels, buffer_width, buffer_height, rect, color);
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
    if (has_clip) {
        int32_t cx0 = (int32_t)floor(clip.x), cy0 = (int32_t)floor(clip.y);
        int32_t cx1 = (int32_t)floor(clip.x + clip.width), cy1 = (int32_t)floor(clip.y + clip.height);
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }

    double alpha = color.a / 255.0;

    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)buffer_width;

        for (int32_t x = x0; x < x1; x++) {
            if (!tbox_raster_point_in_shape((double)x + 0.5, (double)y + 0.5, rect, radius, radius)) continue;

            row[x] = tbox_raster_blend_pixel(row[x], color, alpha);
        }
    }
}

/* Pixel bounds of `rect` within the buffer, `op`'s clip and rounded clip
 * box. False when nothing is left. */
static bool tbox_raster_op_bounds(const tbox_paint_op *op, tbox_rect rect, int32_t width, int32_t height, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1) {
    *x0 = (int32_t)floor(rect.x), *y0 = (int32_t)floor(rect.y);
    *x1 = (int32_t)ceil(rect.x + rect.width), *y1 = (int32_t)ceil(rect.y + rect.height);
    tbox_rect clips[2] = { op->clip, op->rounded_clip };
    bool active[2]     = { op->has_clip, op->has_rounded_clip };
    for (int i = 0; i < 2; i++) {
        if (!active[i]) continue;
        int32_t cx0 = (int32_t)floor(clips[i].x), cy0 = (int32_t)floor(clips[i].y);
        int32_t cx1 = (int32_t)floor(clips[i].x + clips[i].width), cy1 = (int32_t)floor(clips[i].y + clips[i].height);
        if (*x0 < cx0) *x0 = cx0;
        if (*y0 < cy0) *y0 = cy0;
        if (*x1 > cx1) *x1 = cx1;
        if (*y1 > cy1) *y1 = cy1;
    }
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > width) *x1 = width;
    if (*y1 > height) *y1 = height;
    return *x0 < *x1 && *y0 < *y1;
}

/* Applies `m` (a filter color matrix, may be NULL) to one pixel color. */
static tbox_css_rgba tbox_raster_filter(const double *m, tbox_css_rgba c) {
    if (m == NULL)
        return c;
    double in[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 }, out[4];
    for (int row = 0; row < 4; row++) {
        double v = m[row * 5 + 4];
        for (int k = 0; k < 4; k++)
            v += m[row * 5 + k] * in[k];
        out[row] = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
    }
    return (tbox_css_rgba){ (unsigned char)(out[0] * 255.0 + 0.5), (unsigned char)(out[1] * 255.0 + 0.5), (unsigned char)(out[2] * 255.0 + 0.5), (unsigned char)(out[3] * 255.0 + 0.5) };
}

static bool tbox_raster_in_rounded_clip(const tbox_paint_op *op, double x, double y) {
    return !op->has_rounded_clip || tbox_raster_point_in_shape(x, y, op->rounded_clip, op->rounded_clip_radii, op->rounded_clip_radii_y);
}

/* FILL_RECT with elliptical corners and/or a rounded clip. */
static void tbox_raster_fill_shape(uint32_t *pixels, int32_t width, int32_t height, const tbox_paint_op *op) {
    if (op->color.a == 0 || op->rect.width <= 0.0 || op->rect.height <= 0.0) return;
    int32_t x0, y0, x1, y1;
    if (!tbox_raster_op_bounds(op, op->rect, width, height, &x0, &y0, &x1, &y1)) return;
    double legacy[4] = { op->radius, op->radius, op->radius, op->radius };
    bool use_legacy  = !op->elliptical && op->corner_radii[0] <= 0.0 && op->corner_radii[1] <= 0.0 && op->corner_radii[2] <= 0.0 && op->corner_radii[3] <= 0.0 && op->radius > 0.0;
    const double *hr = use_legacy ? legacy : op->corner_radii;
    const double *vr = use_legacy ? legacy : op->elliptical ? op->corner_radii_y : op->corner_radii;
    double alpha     = op->color.a / 255.0;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)width;
        for (int32_t x = x0; x < x1; x++) {
            double px = (double)x + 0.5, py = (double)y + 0.5;
            if (tbox_raster_point_in_shape(px, py, op->rect, hr, vr) && tbox_raster_in_rounded_clip(op, px, py))
                row[x] = tbox_raster_blend_pixel(row[x], op->color, alpha);
        }
    }
}

/* A gradient stop resolved to a fraction of the gradient line. */
typedef struct tbox_raster_stop {
    double position;
    double r, g, b, a; /* premultiplied, 0..1 */
} tbox_raster_stop;

/* Resolves `gradient`'s stops along a line of `length` px: missing
 * positions spread evenly between their neighbors, and a stop never sits
 * before the previous one (CSS's fix-up rules). */
static size_t tbox_raster_resolve_stops(const tbox_style_gradient *gradient, double length, tbox_raster_stop out[TBOX_STYLE_MAX_GRADIENT_STOPS]) {
    size_t count = gradient->stop_count;
    bool known[TBOX_STYLE_MAX_GRADIENT_STOPS];
    for (size_t i = 0; i < count; i++) {
        const tbox_style_gradient_stop *stop = &gradient->stops[i];
        known[i]                             = stop->position.kind != TBOX_STYLE_LENGTH_AUTO;
        out[i].position                      = known[i] && length > 0.0 ? tbox_style_length_resolve(stop->position, length) / length : 0.0;
        double a                             = stop->color.a / 255.0;
        out[i].r = stop->color.r / 255.0 * a, out[i].g = stop->color.g / 255.0 * a, out[i].b = stop->color.b / 255.0 * a, out[i].a = a;
    }
    if (!known[0]) out[0].position = 0.0, known[0] = true;
    if (!known[count - 1]) out[count - 1].position = 1.0, known[count - 1] = true;
    for (size_t i = 1; i < count; i++)
        if (out[i].position < out[i - 1].position && known[i]) out[i].position = out[i - 1].position;
    for (size_t i = 1; i < count; i++) {
        if (known[i]) continue;
        size_t next = i;
        while (!known[next]) next++;
        double start = out[i - 1].position, step = (out[next].position - start) / (double)(next - i + 1);
        for (size_t j = i; j < next; j++) {
            out[j].position = start + step * (double)(j - i + 1);
            known[j]        = true;
        }
    }
    return count;
}

static tbox_css_rgba tbox_raster_gradient_color(const tbox_raster_stop *stops, size_t count, bool repeating, double t) {
    if (repeating) {
        double first = stops[0].position, period = stops[count - 1].position - first;
        if (period > 1e-9) {
            t = fmod(t - first, period);
            if (t < 0.0) t += period;
            t += first;
        }
    }
    const tbox_raster_stop *a = &stops[0], *b = &stops[0];
    if (t <= stops[0].position) {
        a = b = &stops[0];
    } else if (t >= stops[count - 1].position) {
        a = b = &stops[count - 1];
    } else {
        for (size_t i = 1; i < count; i++) {
            if (t <= stops[i].position) {
                a = &stops[i - 1];
                b = &stops[i];
                break;
            }
        }
    }
    double span = b->position - a->position, f = span > 1e-9 ? (t - a->position) / span : 1.0;
    double alpha = a->a + (b->a - a->a) * f;
    if (alpha <= 0.0) return (tbox_css_rgba){ 0, 0, 0, 0 };
    double r = (a->r + (b->r - a->r) * f) / alpha, g = (a->g + (b->g - a->g) * f) / alpha, bl = (a->b + (b->b - a->b) * f) / alpha;
    return (tbox_css_rgba){ (unsigned char)(r * 255.0 + 0.5), (unsigned char)(g * 255.0 + 0.5), (unsigned char)(bl * 255.0 + 0.5), (unsigned char)(alpha * 255.0 + 0.5) };
}

static double tbox_raster_resolve_center(tbox_style_length length, double size) {
    return length.kind == TBOX_STYLE_LENGTH_AUTO ? size / 2.0 : tbox_style_length_resolve(length, size);
}

/* GRADIENT: CSS Images 3's gradient geometry over `op->rect`, colors
 * interpolated in premultiplied sRGB. `op->color.a` scales the result
 * (an `opacity` ancestor). */
static void tbox_raster_gradient(uint32_t *pixels, int32_t width, int32_t height, const tbox_paint_op *op) {
    const tbox_style_gradient *gradient = op->gradient;
    tbox_rect rect                      = op->rect;
    if (gradient == NULL || gradient->stop_count < 2 || rect.width <= 0.0 || rect.height <= 0.0 || op->color.a == 0) return;
    int32_t x0, y0, x1, y1;
    if (!tbox_raster_op_bounds(op, rect, width, height, &x0, &y0, &x1, &y1)) return;
    double w = rect.width, h = rect.height, opacity = op->color.a / 255.0;

    double dx = 0.0, dy = 0.0, length = 0.0, cx = 0.0, cy = 0.0, rx = 1.0, ry = 1.0;
    if (gradient->kind == TBOX_STYLE_GRADIENT_LINEAR) {
        if (gradient->corner[0] != 0 && gradient->corner[1] != 0) {
            dx = gradient->corner[0] * h;
            dy = gradient->corner[1] * w;
            double norm = sqrt(dx * dx + dy * dy);
            dx /= norm, dy /= norm;
        } else {
            double radians = gradient->angle * 3.141592653589793 / 180.0;
            dx = sin(radians), dy = -cos(radians);
        }
        length = fabs(w * dx) + fabs(h * dy);
    } else {
        cx = tbox_raster_resolve_center(gradient->center[0], w);
        cy = tbox_raster_resolve_center(gradient->center[1], h);
        double near_x = fmin(fabs(cx), fabs(w - cx)), far_x = fmax(fabs(cx), fabs(w - cx));
        double near_y = fmin(fabs(cy), fabs(h - cy)), far_y = fmax(fabs(cy), fabs(h - cy));
        bool closest = gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_SIDE || gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_CORNER;
        bool corner  = gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_CORNER || gradient->extent == TBOX_STYLE_GRADIENT_FARTHEST_CORNER;
        double sx = closest ? near_x : far_x, sy = closest ? near_y : far_y;
        if (gradient->circle) {
            double r = corner ? sqrt(sx * sx + sy * sy) : closest ? fmin(sx, sy) : fmax(sx, sy);
            rx = ry = r;
        } else if (corner) {
            rx = sx * 1.4142135623730951, ry = sy * 1.4142135623730951;
        } else {
            rx = sx, ry = sy;
        }
        if (rx <= 0.0) rx = 1e-6;
        if (ry <= 0.0) ry = 1e-6;
        length = rx;
    }

    tbox_raster_stop stops[TBOX_STYLE_MAX_GRADIENT_STOPS];
    size_t count = tbox_raster_resolve_stops(gradient, length, stops);
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)width;
        double py     = (double)y + 0.5 - rect.y;
        for (int32_t x = x0; x < x1; x++) {
            double px = (double)x + 0.5 - rect.x;
            if (!tbox_raster_in_rounded_clip(op, px + rect.x, py + rect.y)) continue;
            double t;
            if (gradient->kind == TBOX_STYLE_GRADIENT_LINEAR) {
                t = length > 0.0 ? ((px - w / 2.0) * dx + (py - h / 2.0) * dy) / length + 0.5 : 0.0;
            } else {
                double ex = (px - cx) / rx, ey = (py - cy) / ry;
                t = sqrt(ex * ex + ey * ey);
            }
            tbox_css_rgba color = tbox_raster_filter(op->color_filter, tbox_raster_gradient_color(stops, count, gradient->repeating, t));
            if (color.a != 0) row[x] = tbox_raster_blend_pixel(row[x], color, color.a / 255.0 * opacity);
        }
    }
}

static void tbox_raster_fill_ring_clipped(uint32_t *pixels, int32_t width, int32_t height, const tbox_paint_op *op) {
    if (pixels == NULL || width <= 0 || height <= 0 || op->color.a == 0 || op->rect.width <= 0.0 || op->rect.height <= 0.0) return;
    int32_t x0 = (int32_t)floor(op->rect.x), y0 = (int32_t)floor(op->rect.y);
    int32_t x1 = (int32_t)floor(op->rect.x + op->rect.width), y1 = (int32_t)floor(op->rect.y + op->rect.height);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (op->has_clip) {
        int32_t cx0 = (int32_t)floor(op->clip.x), cy0 = (int32_t)floor(op->clip.y);
        int32_t cx1 = (int32_t)floor(op->clip.x + op->clip.width), cy1 = (int32_t)floor(op->clip.y + op->clip.height);
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }
    double alpha = op->color.a / 255.0;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)width;
        for (int32_t x = x0; x < x1; x++) {
            double px = (double)x + 0.5, py = (double)y + 0.5;
            if (tbox_raster_point_in_shape(px, py, op->rect, op->corner_radii, op->elliptical ? op->corner_radii_y : op->corner_radii) &&
                !tbox_raster_point_in_shape(px, py, op->inner_rect, op->inner_corner_radii, op->elliptical ? op->inner_corner_radii_y : op->inner_corner_radii) &&
                tbox_raster_in_rounded_clip(op, px, py))
                row[x] = tbox_raster_blend_pixel(row[x], op->color, alpha);
        }
    }
}

void tbox_raster_fill_rounded_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, double radius, tbox_css_rgba color) {
    const double corners[4] = {radius, radius, radius, radius};
    tbox_raster_fill_rounded_rect_clipped(pixels, buffer_width, buffer_height, rect, corners, color, false, (tbox_rect){0});
}

static void tbox_raster_text_run_clipped(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color, double letter_spacing, bool has_clip, tbox_rect clip, const tbox_paint_op *rounded) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || face == NULL || text.size == 0 || color.a == 0) {
        return;
    }
    if (has_clip && (clip.width <= 0.0 || clip.height <= 0.0)) return;
    int32_t clip_x0 = has_clip ? (int32_t)floor(clip.x) : 0;
    int32_t clip_y0 = has_clip ? (int32_t)floor(clip.y) : 0;
    int32_t clip_x1 = has_clip ? (int32_t)floor(clip.x + clip.width) : buffer_width;
    int32_t clip_y1 = has_clip ? (int32_t)floor(clip.y + clip.height) : buffer_height;

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
    const char *end    = text.data + text.size;

    uint32_t previous = 0;
    while (cursor < end) {
        utf8_int32_t codepoint;
        cursor = utf8codepoint(cursor, &codepoint);
        pen_x += tbox_font_face_kerning(face, previous, (uint32_t)codepoint);
        previous = (uint32_t)codepoint;

        tbox_font_glyph_bitmap glyph = tbox_font_rasterize_glyph(mutable_face, (uint32_t)codepoint);

        if (glyph.width > 0 && glyph.height > 0 && glyph.alpha != NULL) {
            int32_t glyph_x0 = (int32_t)floor(pen_x + glyph.bearing_x);
            int32_t glyph_y0 = (int32_t)floor(baseline_y - glyph.bearing_y);

            for (int gy = 0; gy < glyph.height; gy++) {
                int32_t py = glyph_y0 + gy;
                if (py < 0 || py >= buffer_height || py < clip_y0 || py >= clip_y1) {
                    continue;
                }

                const unsigned char *glyph_row = glyph.alpha + (size_t)gy * (size_t)glyph.width;
                uint32_t *pixel_row            = pixels + (size_t)py * (size_t)buffer_width;

                for (int gx = 0; gx < glyph.width; gx++) {
                    int32_t px = glyph_x0 + gx;
                    if (px < 0 || px >= buffer_width || px < clip_x0 || px >= clip_x1) {
                        continue;
                    }

                    unsigned char glyph_alpha = glyph_row[gx];
                    if (glyph_alpha == 0 || (rounded != NULL && !tbox_raster_in_rounded_clip(rounded, (double)px + 0.5, (double)py + 0.5))) {
                        continue;
                    }

                    double combined_alpha = (glyph_alpha / 255.0) * color_alpha;
                    pixel_row[px]         = tbox_raster_blend_pixel(pixel_row[px], color, combined_alpha);
                }
            }
        }

        pen_x += glyph.advance + letter_spacing;
    }
}

void tbox_raster_text_run(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color) {
    tbox_raster_text_run_clipped(pixels, buffer_width, buffer_height, origin, text, face, color, 0.0, false, (tbox_rect){0}, NULL);
}

/* Composites `image`'s decoded RGBA8 pixels into `dest_rect`. When
 * `dest_rect`'s (rounded-to-integer) pixel size differs from `image`'s own
 * intrinsic dimensions -- e.g. a `style="width:...;height:..."` different
 * from the source file's own dimensions -- the source is first resampled to
 * that exact size via stb_image_resize2's "easy API"
 * (stbir_resize_uint8_srgb: Mitchell filter downsampling / cubic upsampling,
 * sRGB-aware so scaling happens in linear light -- real quality resampling,
 * not nearest-neighbor), into a temporary heap buffer freed before this
 * function returns; when the sizes already match, `image->pixels` is
 * blitted directly with no resize step at all. `STBIR_RGBA` tells it the
 * source is straight (non-premultiplied) alpha, exactly what stb_image
 * decoded it as (tbox_image_cache_get always requests 4 channels), so
 * alpha-weighted resampling avoids partially-transparent edge pixels
 * bleeding color from fully-transparent neighbors. `pixelated` skips this
 * resize buffer when enlarging and samples the closest source pixel.
 * Every visible
 * destination pixel is then composited via tbox_raster_blend_pixel with
 * `alpha = (source_pixel.a / 255.0)`, same "over" formula as
 * tbox_raster_fill_rect/tbox_raster_text_run. NULL `pixels`/`image`, a
 * non-positive buffer_width/buffer_height, a non-positive
 * dest_rect.width/height, or a resize failure (allocation failure, treated
 * as a no-op rather than a crash), skips painting entirely. */
static void tbox_raster_image_clipped(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect dest_rect, const tbox_image *image, double opacity, bool has_clip, tbox_rect clip, bool pixelated, const tbox_paint_op *rounded, const double *filter) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || image == NULL || image->pixels == NULL || dest_rect.width <= 0.0 || dest_rect.height <= 0.0) {
        return;
    }

    int32_t dest_width  = (int32_t)(dest_rect.width + 0.5);
    int32_t dest_height = (int32_t)(dest_rect.height + 0.5);
    if (dest_width <= 0 || dest_height <= 0) {
        return;
    }

    const unsigned char *sample_pixels = image->pixels;
    int32_t sample_width               = image->width;
    unsigned char *resized             = NULL;
    bool nearest = pixelated && (dest_width > image->width || dest_height > image->height);

    if (!nearest && (dest_width != image->width || dest_height != image->height)) {
        resized = (unsigned char *)malloc((size_t)dest_width * (size_t)dest_height * 4);
        if (resized == NULL) {
            return;
        }

        if (stbir_resize_uint8_srgb(image->pixels, image->width, image->height, 0, resized, dest_width, dest_height, 0, STBIR_RGBA) == NULL) {
            free(resized);
            return;
        }

        sample_pixels = resized;
        sample_width  = dest_width;
    }

    int32_t origin_x = (int32_t)floor(dest_rect.x);
    int32_t origin_y = (int32_t)floor(dest_rect.y);

    int32_t x0 = origin_x;
    int32_t y0 = origin_y;
    int32_t x1 = origin_x + dest_width;
    int32_t y1 = origin_y + dest_height;

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
    if (has_clip) {
        int32_t cx0 = (int32_t)floor(clip.x), cy0 = (int32_t)floor(clip.y);
        int32_t cx1 = (int32_t)floor(clip.x + clip.width), cy1 = (int32_t)floor(clip.y + clip.height);
        if (x0 < cx0) x0 = cx0;
        if (y0 < cy0) y0 = cy0;
        if (x1 > cx1) x1 = cx1;
        if (y1 > cy1) y1 = cy1;
    }

    for (int32_t y = y0; y < y1; y++) {
        int32_t source_y = nearest ? (int32_t)((int64_t)(y - origin_y) * image->height / dest_height) : y - origin_y;
        const unsigned char *source_row = sample_pixels + (size_t)source_y * (size_t)sample_width * 4;
        uint32_t *dest_row              = pixels + (size_t)y * (size_t)buffer_width;

        for (int32_t x = x0; x < x1; x++) {
            int32_t source_x = nearest ? (int32_t)((int64_t)(x - origin_x) * image->width / dest_width) : x - origin_x;
            const unsigned char *source_pixel = source_row + (size_t)source_x * 4;
            unsigned char source_alpha        = source_pixel[3];
            if (source_alpha == 0 || (rounded != NULL && !tbox_raster_in_rounded_clip(rounded, (double)x + 0.5, (double)y + 0.5))) {
                continue;
            }

            tbox_css_rgba color = { source_pixel[0], source_pixel[1], source_pixel[2], source_alpha };
            if (filter != NULL)
                color = tbox_raster_filter(filter, color);
            dest_row[x] = tbox_raster_blend_pixel(dest_row[x], color, color.a / 255.0 * opacity);
        }
    }

    free(resized);
}

void tbox_raster_image(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect dest_rect, const tbox_image *image) {
    tbox_raster_image_clipped(pixels, buffer_width, buffer_height, dest_rect, image, 1.0, false, (tbox_rect){0}, false, NULL, NULL);
}

void tbox_raster_display_list(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list) {
    if (list == NULL) {
        return;
    }

    for (size_t i = 0; i < list->count; i++) {
        const tbox_paint_op *op = &list->items[i];
        switch (op->kind) {
        case TBOX_PAINT_GRADIENT:
            tbox_raster_gradient(pixels, buffer_width, buffer_height, op);
            break;
        case TBOX_PAINT_FILL_RECT:
            if (op->elliptical || op->has_rounded_clip) {
                tbox_raster_fill_shape(pixels, buffer_width, buffer_height, op);
            } else if (op->corner_radii[0] > 0.0 || op->corner_radii[1] > 0.0 ||
                op->corner_radii[2] > 0.0 || op->corner_radii[3] > 0.0 || op->radius > 0.0) {
                double corners[4];
                bool has_corners = op->corner_radii[0] > 0.0 || op->corner_radii[1] > 0.0 ||
                    op->corner_radii[2] > 0.0 || op->corner_radii[3] > 0.0;
                for (size_t j = 0; j < 4; j++)
                    corners[j] = has_corners ? op->corner_radii[j] : op->radius;
                tbox_raster_fill_rounded_rect_clipped(pixels, buffer_width, buffer_height, op->rect, corners, op->color, op->has_clip, op->clip);
            } else {
                tbox_rect rect = op->rect;
                if (op->has_clip) {
                    double x0 = rect.x > op->clip.x ? rect.x : op->clip.x;
                    double y0 = rect.y > op->clip.y ? rect.y : op->clip.y;
                    double x1 = rect.x + rect.width < op->clip.x + op->clip.width ? rect.x + rect.width : op->clip.x + op->clip.width;
                    double y1 = rect.y + rect.height < op->clip.y + op->clip.height ? rect.y + rect.height : op->clip.y + op->clip.height;
                    rect = (tbox_rect){x0, y0, x1 - x0, y1 - y0};
                }
                if (rect.width > 0.0 && rect.height > 0.0) tbox_raster_fill_rect(pixels, buffer_width, buffer_height, rect, op->color);
            }
            break;
        case TBOX_PAINT_TEXT_RUN:
            tbox_raster_text_run_clipped(pixels, buffer_width, buffer_height, op->rect, op->text, op->face, op->color, op->letter_spacing, op->has_clip, op->has_clip ? op->clip : (tbox_rect){0}, op->has_rounded_clip ? op : NULL);
            break;
        case TBOX_PAINT_IMAGE:
            if (op->has_rounded_clip) {
                tbox_rect clip = op->rounded_clip;
                if (op->has_clip) {
                    double cx0 = fmax(clip.x, op->clip.x), cy0 = fmax(clip.y, op->clip.y);
                    double cx1 = fmin(clip.x + clip.width, op->clip.x + op->clip.width), cy1 = fmin(clip.y + clip.height, op->clip.y + op->clip.height);
                    clip       = (tbox_rect){ cx0, cy0, cx1 > cx0 ? cx1 - cx0 : 0.0, cy1 > cy0 ? cy1 - cy0 : 0.0 };
                }
                tbox_raster_image_clipped(pixels, buffer_width, buffer_height, op->rect, op->image, op->color.a / 255.0, true, clip, op->image_pixelated, op, op->color_filter);
            } else {
                tbox_raster_image_clipped(pixels, buffer_width, buffer_height, op->rect, op->image, op->color.a / 255.0, op->has_clip, op->clip, op->image_pixelated, NULL, op->color_filter);
            }
            break;
        case TBOX_PAINT_FILL_RING:
            tbox_raster_fill_ring_clipped(pixels, buffer_width, buffer_height, op);
            break;
        case TBOX_PAINT_WAVY_LINE: {
            double thickness = op->rect.height;
            if (thickness <= 0.0 || op->rect.width <= 0.0) break;
            double amplitude = thickness > 1.0 ? thickness : 1.0;
            double period = 6.0 * amplitude;
            int32_t x0 = (int32_t)floor(op->rect.x);
            int32_t x1 = (int32_t)floor(op->rect.x + op->rect.width);
            for (int32_t x = x0; x < x1; x++) {
                double phase = ((double)x + 0.5 - op->rect.x) / period;
                double wave_y = op->rect.y + amplitude * (1.0 - cos(6.283185307179586 * phase));
                tbox_rect segment = { (double)x, wave_y, 1.0, thickness };
                if (op->has_clip) {
                    double top = segment.y > op->clip.y ? segment.y : op->clip.y;
                    double bottom = segment.y + segment.height < op->clip.y + op->clip.height ? segment.y + segment.height : op->clip.y + op->clip.height;
                    if (x < op->clip.x || x >= op->clip.x + op->clip.width || bottom <= top) continue;
                    segment.y = top;
                    segment.height = bottom - top;
                }
                tbox_raster_fill_rect(pixels, buffer_width, buffer_height, segment, op->color);
            }
            break;
        }
        }
    }
}

/* -- tbox_raster_write_png -------------------------------------------------
 * Self-contained PNG encoder: no zlib/libpng dependency (this is a
 * development/testing tool, not part of tbox's core rendering path -- not
 * worth a new build dependency). The IDAT stream is a valid zlib stream
 * whose DEFLATE data uses only uncompressed ("stored") blocks (RFC 1951
 * §3.2.4) -- larger than a real compressor would produce, but every
 * conforming PNG decoder accepts it, since "stored" is a first-class DEFLATE
 * block type, not a hack. */

/* IEEE 802.3 CRC-32 (the variant PNG's chunk footers and zlib do NOT use --
 * PNG chunks use this one; zlib's checksum is Adler-32, see below), computed
 * bit-by-bit rather than via a lookup table: a table would have to be either
 * a `static` mutual initialized at runtime (a new mutable global, against
 * this project's stated policy) or a giant literal (256 entries) that adds
 * nothing readers need -- this function runs once per screenshot, not a hot
 * path, so the extra per-byte work is irrelevant. `crc` is the running value
 * (start at 0xFFFFFFFF for a fresh chunk; the caller XORs the final result
 * with 0xFFFFFFFF once, standard CRC-32 framing). */
static uint32_t tbox_png_crc32_update(uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = (crc & 1u) ? 0xFFFFFFFFu : 0u;
            crc           = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

/* zlib's own stream checksum (RFC 1950), unrelated to the CRC-32 above --
 * PNG's IDAT payload is a zlib stream, and zlib streams always end in one of
 * these over the *uncompressed* data, regardless of which DEFLATE block type
 * carried it. `adler` is the running value (start at 1, per RFC 1950). */
static uint32_t tbox_png_adler32_update(uint32_t adler, const uint8_t *data, size_t length) {
    uint32_t a = adler & 0xFFFFu;
    uint32_t b = (adler >> 16) & 0xFFFFu;
    for (size_t i = 0; i < length; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void tbox_png_write_u32_be(uint8_t out[4], uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

/* Writes one PNG chunk (length + 4-byte type + data + CRC-32 of type+data)
 * to `file`. `data`/`length` may be NULL/0 (IEND has no data). Returns false
 * on any short write, leaving `file`'s position wherever the failed write
 * left it -- the caller treats any false here as fatal for the whole file. */
static bool tbox_png_write_chunk(FILE *file, const char type[4], const uint8_t *data, size_t length) {
    uint8_t length_be[4];
    tbox_png_write_u32_be(length_be, (uint32_t)length);
    if (fwrite(length_be, 1, 4, file) != 4) {
        return false;
    }
    if (fwrite(type, 1, 4, file) != 4) {
        return false;
    }
    if (length > 0 && fwrite(data, 1, length, file) != length) {
        return false;
    }

    uint32_t crc = tbox_png_crc32_update(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (length > 0) {
        crc = tbox_png_crc32_update(crc, data, length);
    }
    crc ^= 0xFFFFFFFFu;

    uint8_t crc_be[4];
    tbox_png_write_u32_be(crc_be, crc);
    return fwrite(crc_be, 1, 4, file) == 4;
}

bool tbox_raster_write_png(const char *path, const uint32_t *pixels, int32_t buffer_width, int32_t buffer_height) {
    if (path == NULL || pixels == NULL || buffer_width <= 0 || buffer_height <= 0) {
        return false;
    }

    size_t width  = (size_t)buffer_width;
    size_t height = (size_t)buffer_height;

    /* Raw scanline data PNG's filter step expects: one filter-type byte (0 =
     * "None", the only filter this encoder ever emits) followed by 3 bytes
     * per pixel (R, G, B -- color type 2, truecolor, no alpha: every pixel
     * tbox_raster_* ever produces is fully opaque, see tbox_raster_blend_pixel
     * above and tbox_backend_wayland_present's doc comment, so the XRGB
     * buffer's top byte carries no information worth keeping). */
    size_t row_bytes = 1 + width * 3;
    size_t raw_size  = row_bytes * height;
    uint8_t *raw     = (uint8_t *)malloc(raw_size);
    if (raw == NULL) {
        return false;
    }
    for (size_t y = 0; y < height; y++) {
        uint8_t *row = raw + y * row_bytes;
        row[0]       = 0; /* filter: None */
        for (size_t x = 0; x < width; x++) {
            uint32_t pixel     = pixels[y * width + x];
            row[1 + x * 3 + 0] = (uint8_t)((pixel >> 16) & 0xFFu);
            row[1 + x * 3 + 1] = (uint8_t)((pixel >> 8) & 0xFFu);
            row[1 + x * 3 + 2] = (uint8_t)(pixel & 0xFFu);
        }
    }

    /* Wrap `raw` in a minimal zlib stream: 2-byte header, then `raw` split
     * into "stored" DEFLATE blocks (max 65535 bytes each -- the block
     * format's LEN field is 16-bit), then the 4-byte Adler-32 trailer. Each
     * stored block costs 5 bytes of framing (1 header byte + LEN + NLEN)
     * -- computed upfront so the whole stream can be a single malloc. */
    size_t max_stored_block = 65535;
    size_t block_count      = (raw_size + max_stored_block - 1) / max_stored_block;
    if (block_count == 0) {
        block_count = 1; /* an empty image still needs one (empty) final block */
    }
    size_t zlib_size   = 2 + block_count * 5 + raw_size + 4;
    uint8_t *zlib_data = (uint8_t *)malloc(zlib_size);
    if (zlib_data == NULL) {
        free(raw);
        return false;
    }

    size_t pos       = 0;
    zlib_data[pos++] = 0x78; /* CMF: DEFLATE, 32K window */
    zlib_data[pos++] = 0x01; /* FLG: fastest, no preset dictionary (valid check bits for 0x78) */

    size_t remaining = raw_size;
    size_t raw_pos   = 0;
    for (size_t block = 0; block < block_count; block++) {
        size_t chunk = remaining < max_stored_block ? remaining : max_stored_block;
        bool final   = (block == block_count - 1);

        zlib_data[pos++] = final ? 0x01 : 0x00; /* BFINAL | BTYPE=00 (stored), byte-aligned */
        zlib_data[pos++] = (uint8_t)(chunk & 0xFFu);
        zlib_data[pos++] = (uint8_t)((chunk >> 8) & 0xFFu);
        uint16_t nlen    = (uint16_t)(~(uint16_t)chunk);
        zlib_data[pos++] = (uint8_t)(nlen & 0xFFu);
        zlib_data[pos++] = (uint8_t)((nlen >> 8) & 0xFFu);

        memcpy(zlib_data + pos, raw + raw_pos, chunk);
        pos += chunk;
        raw_pos += chunk;
        remaining -= chunk;
    }

    uint32_t adler   = tbox_png_adler32_update(1u, raw, raw_size);
    zlib_data[pos++] = (uint8_t)((adler >> 24) & 0xFFu);
    zlib_data[pos++] = (uint8_t)((adler >> 16) & 0xFFu);
    zlib_data[pos++] = (uint8_t)((adler >> 8) & 0xFFu);
    zlib_data[pos++] = (uint8_t)(adler & 0xFFu);

    free(raw);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        free(zlib_data);
        return false;
    }

    static const uint8_t signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    bool ok                           = fwrite(signature, 1, sizeof(signature), file) == sizeof(signature);

    uint8_t ihdr[13];
    tbox_png_write_u32_be(ihdr + 0, (uint32_t)width);
    tbox_png_write_u32_be(ihdr + 4, (uint32_t)height);
    ihdr[8]  = 8; /* bit depth */
    ihdr[9]  = 2; /* color type: truecolor (no alpha) */
    ihdr[10] = 0; /* compression method: always 0 */
    ihdr[11] = 0; /* filter method: always 0 */
    ihdr[12] = 0; /* interlace method: 0 = no interlacing */

    ok = ok && tbox_png_write_chunk(file, "IHDR", ihdr, sizeof(ihdr));
    ok = ok && tbox_png_write_chunk(file, "IDAT", zlib_data, zlib_size);
    ok = ok && tbox_png_write_chunk(file, "IEND", NULL, 0);

    free(zlib_data);
    fclose(file);

    if (!ok) {
        remove(path); /* don't leave a truncated/corrupt file behind */
    }
    return ok;
}
