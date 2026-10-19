#include <tbox/output.h>

#include "base/tbox_polygon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tbox_raster_internal.h"
#include "base/tbox_string.h"
#include "font/tbox_font_measure.h"

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
 * whatever coverage/opacity terms apply -- see callers) over `dst_pixel`,
 * returning the new pixel. alpha <= 0 returns `dst_pixel` unchanged;
 * alpha >= 1 returns a plain opaque overwrite.
 *
 * Pixels are ARGB8888 with premultiplied color: the screen buffer is
 * always opaque (alpha 0xFF, so premultiplied and straight agree), while a
 * compositing layer starts transparent. Source-over is the same formula
 * per color channel either way; only the alpha byte needs computing. */
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
    uint32_t dst_a      = dst_pixel >> 24;

    unsigned char r = tbox_raster_blend_channel(color.r, dst_r, alpha);
    unsigned char g = tbox_raster_blend_channel(color.g, dst_g, alpha);
    unsigned char b = tbox_raster_blend_channel(color.b, dst_b, alpha);
    uint32_t a      = dst_a == 0xFFu ? 0xFFu : (uint32_t)(255.0 * alpha + (double)dst_a * (1.0 - alpha) + 0.5);

    return (a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* A pixel's straight (unpremultiplied) color. */
static tbox_css_rgba tbox_raster_unpack(uint32_t pixel) {
    unsigned a = pixel >> 24;
    tbox_css_rgba c = { (unsigned char)((pixel >> 16) & 0xFFu), (unsigned char)((pixel >> 8) & 0xFFu), (unsigned char)(pixel & 0xFFu), (unsigned char)a };
    if (a != 0xFFu && a != 0) {
        c.r = (unsigned char)fmin(255.0, c.r * 255.0 / a + 0.5);
        c.g = (unsigned char)fmin(255.0, c.g * 255.0 / a + 0.5);
        c.b = (unsigned char)fmin(255.0, c.b * 255.0 / a + 0.5);
    }
    return c;
}

/* A straight color as a premultiplied pixel. */
static uint32_t tbox_raster_pack(tbox_css_rgba c) {
    if (c.a == 0xFFu)
        return 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
    double a = c.a / 255.0;
    return ((uint32_t)c.a << 24) | ((uint32_t)(c.r * a + 0.5) << 16) | ((uint32_t)(c.g * a + 0.5) << 8) | (uint32_t)(c.b * a + 0.5);
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
    const bool active[2]     = { op->has_clip, op->has_rounded_clip };
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
    const double in[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 };
    double out[4];
    for (int row = 0; row < 4; row++) {
        double v = m[row * 5 + 4];
        for (int k = 0; k < 4; k++)
            v += m[row * 5 + k] * in[k];
        out[row] = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
    }
    return (tbox_css_rgba){ (unsigned char)(out[0] * 255.0 + 0.5), (unsigned char)(out[1] * 255.0 + 0.5), (unsigned char)(out[2] * 255.0 + 0.5), (unsigned char)(out[3] * 255.0 + 0.5) };
}

static bool tbox_raster_in_shape_clip(const tbox_paint_op *op, double x, double y) {
    if (op->has_rounded_clip && !tbox_raster_point_in_shape(x, y, op->rounded_clip, op->rounded_clip_radii, op->rounded_clip_radii_y)) return false;
    if (op->polygon_clip != NULL && op->polygon_clip->count > TBOX_STYLE_MAX_POLYGON_POINTS) return false;
    return op->polygon_clip == NULL || tbox_polygon_contains(op->polygon_clip->points, op->polygon_clip->count,
        op->polygon_clip->evenodd, x - op->polygon_clip_origin[0], y - op->polygon_clip_origin[1]);
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
            if (tbox_raster_point_in_shape(px, py, op->rect, hr, vr) && tbox_raster_in_shape_clip(op, px, py))
                row[x] = tbox_raster_blend_pixel(row[x], op->color, alpha);
        }
    }
}

/* ---- blend modes (Compositing and Blending 1 §10) ---- */

static double tbox_raster_hard_light(double b, double s) {
    return s <= 0.5 ? b * 2.0 * s : b + (2.0 * s - 1.0) - b * (2.0 * s - 1.0);
}

static double tbox_raster_blend_separable(tbox_style_blend_mode mode, double b, double s) {
    switch (mode) {
    case TBOX_STYLE_BLEND_MULTIPLY: return b * s;
    case TBOX_STYLE_BLEND_SCREEN: return b + s - b * s;
    case TBOX_STYLE_BLEND_OVERLAY: return tbox_raster_hard_light(s, b);
    case TBOX_STYLE_BLEND_DARKEN: return b < s ? b : s;
    case TBOX_STYLE_BLEND_LIGHTEN: return b > s ? b : s;
    case TBOX_STYLE_BLEND_COLOR_DODGE: return b <= 0.0 ? 0.0 : s >= 1.0 ? 1.0 : fmin(1.0, b / (1.0 - s));
    case TBOX_STYLE_BLEND_COLOR_BURN: return b >= 1.0 ? 1.0 : s <= 0.0 ? 0.0 : 1.0 - fmin(1.0, (1.0 - b) / s);
    case TBOX_STYLE_BLEND_HARD_LIGHT: return tbox_raster_hard_light(b, s);
    case TBOX_STYLE_BLEND_SOFT_LIGHT: {
        if (s <= 0.5) return b - (1.0 - 2.0 * s) * b * (1.0 - b);
        double d = b <= 0.25 ? ((16.0 * b - 12.0) * b + 4.0) * b : sqrt(b);
        return b + (2.0 * s - 1.0) * (d - b);
    }
    case TBOX_STYLE_BLEND_DIFFERENCE: return fabs(b - s);
    case TBOX_STYLE_BLEND_EXCLUSION: return b + s - 2.0 * b * s;
    default: return s;
    }
}

static double tbox_raster_lum(const double c[3]) {
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2];
}

static void tbox_raster_set_lum(const double c[3], double l, double out[3]) {
    double d = l - tbox_raster_lum(c);
    for (int i = 0; i < 3; i++) out[i] = c[i] + d;
    double lum = tbox_raster_lum(out), n = fmin(out[0], fmin(out[1], out[2])), x = fmax(out[0], fmax(out[1], out[2]));
    for (int i = 0; i < 3; i++) {
        if (n < 0.0 && lum - n > 1e-12) out[i] = lum + (out[i] - lum) * lum / (lum - n);
        if (x > 1.0 && x - lum > 1e-12) out[i] = lum + (out[i] - lum) * (1.0 - lum) / (x - lum);
    }
}

static void tbox_raster_set_sat(const double c[3], double sat, double out[3]) {
    int max = 0, min = 0;
    for (int i = 1; i < 3; i++) {
        if (c[i] > c[max]) max = i;
        if (c[i] < c[min]) min = i;
    }
    if (max == min) {
        out[0] = out[1] = out[2] = 0.0;
        return;
    }
    int mid = 3 - max - min;
    out[mid] = (c[mid] - c[min]) * sat / (c[max] - c[min]);
    out[max] = sat;
    out[min] = 0.0;
}

/* `src` blended into `dst_pixel` with `mode`, then composited with
 * coverage `alpha` (Compositing and Blending 1's general formula). */
static uint32_t tbox_raster_blend_mode_pixel(uint32_t dst_pixel, tbox_css_rgba src, double alpha, tbox_style_blend_mode mode) {
    if (mode == TBOX_STYLE_BLEND_NORMAL)
        return tbox_raster_blend_pixel(dst_pixel, src, alpha);
    tbox_css_rgba backdrop = tbox_raster_unpack(dst_pixel);
    double b[3] = { backdrop.r / 255.0, backdrop.g / 255.0, backdrop.b / 255.0 };
    double s[3] = { src.r / 255.0, src.g / 255.0, src.b / 255.0 }, out[3];
    if (mode >= TBOX_STYLE_BLEND_HUE) {
        double tmp[3];
        double sat_b = fmax(b[0], fmax(b[1], b[2])) - fmin(b[0], fmin(b[1], b[2]));
        double sat_s = fmax(s[0], fmax(s[1], s[2])) - fmin(s[0], fmin(s[1], s[2]));
        if (mode == TBOX_STYLE_BLEND_HUE) {
            tbox_raster_set_sat(s, sat_b, tmp);
            tbox_raster_set_lum(tmp, tbox_raster_lum(b), out);
        } else if (mode == TBOX_STYLE_BLEND_SATURATION) {
            tbox_raster_set_sat(b, sat_s, tmp);
            tbox_raster_set_lum(tmp, tbox_raster_lum(b), out);
        } else if (mode == TBOX_STYLE_BLEND_COLOR) {
            tbox_raster_set_lum(s, tbox_raster_lum(b), out);
        } else {
            tbox_raster_set_lum(b, tbox_raster_lum(s), out);
        }
    } else {
        for (int i = 0; i < 3; i++) out[i] = tbox_raster_blend_separable(mode, b[i], s[i]);
    }
    /* A translucent backdrop (inside a layer) blends only where it is:
     * Cs' = (1 - ab) Cs + ab B(Cb, Cs). */
    double ab = backdrop.a / 255.0;
    if (backdrop.a != 0xFFu)
        for (int i = 0; i < 3; i++) out[i] = (1.0 - ab) * s[i] + ab * out[i];
    tbox_css_rgba blended;
    blended.r = (unsigned char)(fmin(1.0, fmax(0.0, out[0])) * 255.0 + 0.5);
    blended.g = (unsigned char)(fmin(1.0, fmax(0.0, out[1])) * 255.0 + 0.5);
    blended.b = (unsigned char)(fmin(1.0, fmax(0.0, out[2])) * 255.0 + 0.5);
    blended.a = 255;
    return tbox_raster_blend_pixel(dst_pixel, blended, alpha);
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
    double previous = out[0].position;
    for (size_t i = 1; i < count; i++) {
        if (!known[i]) continue;
        if (out[i].position < previous) out[i].position = previous;
        previous = out[i].position;
    }
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
    const tbox_raster_stop *a = &stops[0], *b = &stops[0]; /* at or before the first stop */
    if (t > stops[0].position) {
        if (t >= stops[count - 1].position) {
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
    /* With a source rect the gradient is laid out at that size and then
     * stretched onto `rect` (a border-image slice). */
    double w = rect.width, h = rect.height, opacity = op->color.a / 255.0;
    if (op->has_source_rect && op->source_rect.width > 0.0 && op->source_rect.height > 0.0)
        w = op->source_rect.width, h = op->source_rect.height;
    double scale_x = w / rect.width, scale_y = h / rect.height;

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
    } else if (gradient->kind == TBOX_STYLE_GRADIENT_CONIC) {
        cx = tbox_raster_resolve_center(gradient->center[0], w);
        cy = tbox_raster_resolve_center(gradient->center[1], h);
        length = 1.0; /* conic positions are percentages of a turn */
    } else {
        cx = tbox_raster_resolve_center(gradient->center[0], w);
        cy = tbox_raster_resolve_center(gradient->center[1], h);
        double near_x = fmin(fabs(cx), fabs(w - cx)), far_x = fmax(fabs(cx), fabs(w - cx));
        double near_y = fmin(fabs(cy), fabs(h - cy)), far_y = fmax(fabs(cy), fabs(h - cy));
        bool closest = gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_SIDE || gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_CORNER;
        bool corner  = gradient->extent == TBOX_STYLE_GRADIENT_CLOSEST_CORNER || gradient->extent == TBOX_STYLE_GRADIENT_FARTHEST_CORNER;
        double sx = closest ? near_x : far_x, sy = closest ? near_y : far_y;
        if (gradient->radius[0].kind != TBOX_STYLE_LENGTH_AUTO) {
            rx = fmax(0.0, tbox_style_length_resolve(gradient->radius[0], w));
            ry = gradient->circle ? rx : fmax(0.0, tbox_style_length_resolve(gradient->radius[1], h));
        } else if (gradient->circle) {
            double r = corner ? sqrt(sx * sx + sy * sy) : closest ? fmin(sx, sy) : fmax(sx, sy);
            rx = ry = r;
        } else if (corner) {
            rx = sx * 1.4142135623730951, ry = sy * 1.4142135623730951;
        } else {
            rx = sx, ry = sy;
        }
        if (!isfinite(rx) || !isfinite(ry)) return;
        if (rx <= 0.0) rx = 1e-6;
        if (ry <= 0.0) ry = 1e-6;
        length = rx;
    }

    if (!isfinite(cx) || !isfinite(cy) || !isfinite(gradient->angle)) return;
    tbox_raster_stop stops[TBOX_STYLE_MAX_GRADIENT_STOPS];
    size_t count = tbox_raster_resolve_stops(gradient, length, stops);
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *row = pixels + (size_t)y * (size_t)width;
        double py     = ((double)y + 0.5 - rect.y) * scale_y;
        for (int32_t x = x0; x < x1; x++) {
            double px = ((double)x + 0.5 - rect.x) * scale_x;
            if (!tbox_raster_in_shape_clip(op, (double)x + 0.5, (double)y + 0.5)) continue;
            double t;
            if (gradient->kind == TBOX_STYLE_GRADIENT_LINEAR) {
                t = length > 0.0 ? ((px - w / 2.0) * dx + (py - h / 2.0) * dy) / length + 0.5 : 0.0;
            } else if (gradient->kind == TBOX_STYLE_GRADIENT_CONIC) {
                t = atan2(px - cx, -(py - cy)) / (2.0 * 3.14159265358979323846) - fmod(gradient->angle, 360.0) / 360.0;
                t -= floor(t);
            } else {
                double ex = (px - cx) / rx, ey = (py - cy) / ry;
                t = sqrt(ex * ex + ey * ey);
            }
            tbox_css_rgba color = tbox_raster_filter(op->color_filter, tbox_raster_gradient_color(stops, count, gradient->repeating, t));
            if (color.a != 0) row[x] = tbox_raster_blend_mode_pixel(row[x], color, color.a / 255.0 * opacity, op->blend_mode);
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
                tbox_raster_in_shape_clip(op, px, py))
                row[x] = tbox_raster_blend_pixel(row[x], op->color, alpha);
        }
    }
}

void tbox_raster_fill_rounded_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, double radius, tbox_css_rgba color) {
    const double corners[4] = {radius, radius, radius, radius};
    tbox_raster_fill_rounded_rect_clipped(pixels, buffer_width, buffer_height, rect, corners, color, false, (tbox_rect){0});
}

/* Where TEXT_RUN glyphs may land (the op's clip, or the whole buffer). */
typedef struct tbox_raster_text_target {
    uint32_t *pixels;
    int32_t buffer_width, buffer_height;
    int32_t clip_x0, clip_y0, clip_x1, clip_y1;
    const tbox_paint_op *rounded;
    tbox_css_rgba color;
    double color_alpha;
    double stroke_width;
} tbox_raster_text_target;

/* Composites a glyph turned by `orient` (and squeezed by `scale`) for
 * vertical text (docs/09-texto-e-fontes.md, "Escrita vertical"): each
 * destination pixel samples the bitmap through the inverse mapping,
 * bilinearly, so quarter turns at whole pixels stay exact. */
static void tbox_raster_oriented_glyph(const tbox_raster_text_target *target, const tbox_font_glyph_bitmap *glyph, unsigned char orient, double scale, double pen_x, double baseline_y) {
    /* bitmap offset (u, v) from the pen -> screen offset (a u + c v, b u + d v) */
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0;
    if (orient == TBOX_GLYPH_ORIENT_ROTATE_CCW) a = 0.0, b = -1.0, c = 1.0, d = 0.0;
    else if (orient == TBOX_GLYPH_ORIENT_TRANSPOSE) a = 0.0, b = 1.0, c = 1.0, d = 0.0;
    else if (orient == TBOX_GLYPH_ORIENT_FLIP) d = -1.0;
    a *= scale;
    b *= scale;
    double det = a * d - b * c;
    if (!(det > 1e-9 || det < -1e-9))
        return;
    double u0 = glyph->bearing_x, v0 = -glyph->bearing_y, u1 = u0 + glyph->width, v1 = v0 + glyph->height;
    const double xs[4] = { a * u0 + c * v0, a * u1 + c * v0, a * u0 + c * v1, a * u1 + c * v1 };
    const double ys[4] = { b * u0 + d * v0, b * u1 + d * v0, b * u0 + d * v1, b * u1 + d * v1 };
    double min_x = xs[0], max_x = xs[0], min_y = ys[0], max_y = ys[0];
    for (int i = 1; i < 4; i++) {
        if (xs[i] < min_x) min_x = xs[i];
        if (xs[i] > max_x) max_x = xs[i];
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    double ox = floor(pen_x + 0.5), oy = floor(baseline_y + 0.5);
    int32_t x0 = (int32_t)floor(ox + min_x), x1 = (int32_t)ceil(ox + max_x);
    int32_t y0 = (int32_t)floor(oy + min_y), y1 = (int32_t)ceil(oy + max_y);
    if (x0 < target->clip_x0) x0 = target->clip_x0;
    if (y0 < target->clip_y0) y0 = target->clip_y0;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > target->clip_x1) x1 = target->clip_x1;
    if (y1 > target->clip_y1) y1 = target->clip_y1;
    if (x1 > target->buffer_width) x1 = target->buffer_width;
    if (y1 > target->buffer_height) y1 = target->buffer_height;
    for (int32_t py = y0; py < y1; py++) {
        uint32_t *pixel_row = target->pixels + (size_t)py * (size_t)target->buffer_width;
        for (int32_t px = x0; px < x1; px++) {
            double dx = px + 0.5 - ox, dy = py + 0.5 - oy;
            /* back to bitmap coordinates, in pixel-center units */
            double u  = (d * dx - c * dy) / det - u0 - 0.5;
            double v  = (a * dy - b * dx) / det - v0 - 0.5;
            int iu = (int)floor(u), iv = (int)floor(v);
            double fu = u - iu, fv = v - iv, coverage = 0.0;
            for (int k = 0; k < 4; k++) {
                int su = iu + (k & 1), sv = iv + (k >> 1);
                if (su < 0 || sv < 0 || su >= glyph->width || sv >= glyph->height) continue;
                double w = ((k & 1) ? fu : 1.0 - fu) * ((k >> 1) ? fv : 1.0 - fv);
                coverage += w * glyph->alpha[(size_t)sv * (size_t)glyph->width + (size_t)su] / 255.0;
            }
            if (coverage <= 0.0 || (target->rounded != NULL && !tbox_raster_in_shape_clip(target->rounded, (double)px + 0.5, (double)py + 0.5)))
                continue;
            pixel_row[px] = tbox_raster_blend_pixel(pixel_row[px], target->color, coverage * target->color_alpha);
        }
    }
}

/* Composites one glyph with its pen at (pen_x, baseline_y). */
static void tbox_raster_glyph(const tbox_raster_text_target *target, tbox_font_face *face, uint32_t glyph_id, double pen_x, double baseline_y, unsigned char orient, double scale) {
    uint32_t *pixels             = target->pixels;
    int32_t buffer_width         = target->buffer_width;
    int32_t buffer_height        = target->buffer_height;
    int32_t clip_x0              = target->clip_x0, clip_y0 = target->clip_y0;
    int32_t clip_x1              = target->clip_x1, clip_y1 = target->clip_y1;
    const tbox_paint_op *rounded = target->rounded;
    tbox_css_rgba color          = target->color;
    double color_alpha           = target->color_alpha;
    tbox_font_glyph_bitmap glyph = target->stroke_width > 0.0 ?
        tbox_font_rasterize_glyph_stroke(face, glyph_id, target->stroke_width) : tbox_font_rasterize_glyph_id(face, glyph_id);
    if (orient != TBOX_GLYPH_ORIENT_NONE || (scale > 0.0 && scale != 1.0)) {
        if (glyph.width > 0 && glyph.height > 0 && glyph.alpha != NULL)
            tbox_raster_oriented_glyph(target, &glyph, orient, scale > 0.0 ? scale : 1.0, pen_x, baseline_y);
        return;
    }
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
                if (glyph_alpha == 0 || (rounded != NULL && !tbox_raster_in_shape_clip(rounded, (double)px + 0.5, (double)py + 0.5))) {
                    continue;
                }

                double combined_alpha = (glyph_alpha / 255.0) * color_alpha;
                pixel_row[px]         = tbox_raster_blend_pixel(pixel_row[px], color, combined_alpha);
            }
        }
    }
}

/* Paints `glyphs` (docs/08-layout-e-pintura.md, "Texto como glifos
 * posicionados") as they are: no UTF-8 decoding, no kerning here. Without
 * glyphs (an op built by hand), `text` is shaped on the fly by the same
 * shaper Layout uses, in a scratch arena. */
static void tbox_raster_text_run_clipped(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_glyph *glyphs, size_t glyph_count, const tbox_font_face *face, tbox_css_rgba color, double letter_spacing, bool has_clip, tbox_rect clip, const tbox_paint_op *rounded, double stroke_width) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || face == NULL || (text.size == 0 && glyph_count == 0) || color.a == 0) {
        return;
    }
    if (has_clip && (clip.width <= 0.0 || clip.height <= 0.0)) return;
    tbox_raster_text_target target = {
        .pixels = pixels, .buffer_width = buffer_width, .buffer_height = buffer_height,
        .clip_x0 = has_clip ? (int32_t)floor(clip.x) : 0,
        .clip_y0 = has_clip ? (int32_t)floor(clip.y) : 0,
        .clip_x1 = has_clip ? (int32_t)floor(clip.x + clip.width) : buffer_width,
        .clip_y1 = has_clip ? (int32_t)floor(clip.y + clip.height) : buffer_height,
        .rounded = rounded, .color = color, .color_alpha = color.a / 255.0,
        .stroke_width = stroke_width,
    };

    /* tbox_font_rasterize_glyph_id mutates the FT_Face's internal glyph
     * slot (see <tbox/font.h>) so it takes a non-const tbox_font_face*,
     * even though from this function's point of view -- and
     * tbox_paint_op's -- `face` is only ever read. */
    tbox_font_face *mutable_face = (tbox_font_face *)face;
    double baseline_y            = origin.y + tbox_font_face_ascent(face);

    if (glyph_count > 0) {
        for (size_t i = 0; i < glyph_count; i++)
            tbox_raster_glyph(&target, mutable_face, glyphs[i].id, origin.x + glyphs[i].x, baseline_y + glyphs[i].y, glyphs[i].orient, glyphs[i].scale);
        return;
    }
    tbox_arena scratch = tbox_arena_create(0);
    tbox_glyph *shaped;
    size_t shaped_count;
    if (tbox_font_shape(&scratch, face, text, letter_spacing, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &shaped, &shaped_count, NULL))
        for (size_t i = 0; i < shaped_count; i++)
            tbox_raster_glyph(&target, mutable_face, shaped[i].id, origin.x + shaped[i].x, baseline_y + shaped[i].y, shaped[i].orient, shaped[i].scale);
    tbox_arena_destroy(&scratch);
}

void tbox_raster_text_run(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color) {
    tbox_raster_text_run_clipped(pixels, buffer_width, buffer_height, origin, text, NULL, 0, face, color, 0.0, false, (tbox_rect){0}, NULL, 0.0);
}

/* Composites `image`'s decoded RGBA8 pixels into `dest_rect`. When
 * `dest_rect`'s (rounded-to-integer) pixel size differs from `image`'s own
 * intrinsic dimensions -- e.g. a `style="width:...;height:..."` different
 * from the source file's own dimensions -- the source is first resampled to
 * that exact size via stb_image_resize2's "easy API"
 * (stbir_resize: Mitchell filter downsampling / triangle (bilinear) upsampling,
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
/* `source` (NULL for the whole image) is the part of the image, in image
 * pixels rounded to whole ones, scaled into dest_rect. */
static void tbox_raster_image_clipped(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect dest_rect, const tbox_image *image, const tbox_rect *source, double opacity, bool has_clip, tbox_rect clip, bool pixelated, const tbox_paint_op *rounded, const double *filter, tbox_style_blend_mode blend_mode) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || image == NULL || image->pixels == NULL || dest_rect.width <= 0.0 || dest_rect.height <= 0.0) {
        return;
    }

    int32_t dest_width  = (int32_t)(dest_rect.width + 0.5);
    int32_t dest_height = (int32_t)(dest_rect.height + 0.5);
    if (dest_width <= 0 || dest_height <= 0) {
        return;
    }

    int32_t crop_x = 0, crop_y = 0, crop_width = image->width, crop_height = image->height;
    if (source != NULL) {
        double sx0 = fmax(0.0, floor(source->x + 0.5)), sy0 = fmax(0.0, floor(source->y + 0.5));
        double sx1 = fmin((double)image->width, floor(source->x + source->width + 0.5));
        double sy1 = fmin((double)image->height, floor(source->y + source->height + 0.5));
        if (sx1 <= sx0 || sy1 <= sy0)
            return;
        crop_x = (int32_t)sx0, crop_y = (int32_t)sy0;
        crop_width = (int32_t)(sx1 - sx0), crop_height = (int32_t)(sy1 - sy0);
    }
    const unsigned char *crop_pixels   = image->pixels + ((size_t)crop_y * (size_t)image->width + (size_t)crop_x) * 4;
    const unsigned char *sample_pixels = crop_pixels;
    int32_t sample_width               = image->width; /* row stride, in pixels */
    unsigned char *resized             = NULL;
    bool nearest = pixelated && (dest_width > crop_width || dest_height > crop_height);

    if (!nearest && (dest_width != crop_width || dest_height != crop_height)) {
        resized = (unsigned char *)malloc((size_t)dest_width * (size_t)dest_height * 4);
        if (resized == NULL) {
            return;
        }

        /* Enlarging uses a triangle (bilinear) filter, as browsers do: the
         * default cubic one overshoots around hard edges of small images. */
        stbir_filter resample = dest_width >= crop_width && dest_height >= crop_height ? STBIR_FILTER_TRIANGLE : STBIR_FILTER_DEFAULT;
        if (stbir_resize(crop_pixels, crop_width, crop_height, image->width * 4, resized, dest_width, dest_height, 0, STBIR_RGBA, STBIR_TYPE_UINT8_SRGB, STBIR_EDGE_CLAMP, resample) == NULL) {
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
        int32_t source_y = nearest ? (int32_t)((int64_t)(y - origin_y) * crop_height / dest_height) : y - origin_y;
        const unsigned char *source_row = sample_pixels + (size_t)source_y * (size_t)sample_width * 4;
        uint32_t *dest_row              = pixels + (size_t)y * (size_t)buffer_width;

        for (int32_t x = x0; x < x1; x++) {
            int32_t source_x = nearest ? (int32_t)((int64_t)(x - origin_x) * crop_width / dest_width) : x - origin_x;
            const unsigned char *source_pixel = source_row + (size_t)source_x * 4;
            unsigned char source_alpha        = source_pixel[3];
            if (source_alpha == 0 || (rounded != NULL && !tbox_raster_in_shape_clip(rounded, (double)x + 0.5, (double)y + 0.5))) {
                continue;
            }

            tbox_css_rgba color = { source_pixel[0], source_pixel[1], source_pixel[2], source_alpha };
            if (filter != NULL)
                color = tbox_raster_filter(filter, color);
            dest_row[x] = tbox_raster_blend_mode_pixel(dest_row[x], color, color.a / 255.0 * opacity, blend_mode);
        }
    }

    free(resized);
}

void tbox_raster_image(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect dest_rect, const tbox_image *image) {
    tbox_raster_image_clipped(pixels, buffer_width, buffer_height, dest_rect, image, NULL, 1.0, false, (tbox_rect){0}, false, NULL, NULL, TBOX_STYLE_BLEND_NORMAL);
}

/* Rasterizes one op -- the dispatch tbox_raster_display_list and
 * tbox_raster_display_list_damaged share. */
static void tbox_raster_paint_op(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_paint_op *op) {
    switch (op->kind) {
    case TBOX_PAINT_GRADIENT:
        tbox_raster_gradient(pixels, buffer_width, buffer_height, op);
        break;
    case TBOX_PAINT_FILL_RECT:
        if (op->elliptical || op->has_rounded_clip || op->polygon_clip != NULL) {
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
        for (int pass = 0; pass < 2; pass++) {
            bool stroke = (pass == 0) == op->text_stroke_first;
            if (stroke && op->text_stroke_width <= 0.0) continue;
            tbox_raster_text_run_clipped(pixels, buffer_width, buffer_height, op->rect, op->text, op->glyphs, op->glyph_count, op->face,
                stroke ? op->text_stroke_color : op->color, op->letter_spacing, op->has_clip, op->has_clip ? op->clip : (tbox_rect){0},
                (op->has_rounded_clip || op->polygon_clip != NULL) ? op : NULL, stroke ? op->text_stroke_width : 0.0);
        }
        break;
    case TBOX_PAINT_IMAGE:
        if (op->has_rounded_clip) {
            tbox_rect clip = op->rounded_clip;
            if (op->has_clip) {
                double cx0 = fmax(clip.x, op->clip.x), cy0 = fmax(clip.y, op->clip.y);
                double cx1 = fmin(clip.x + clip.width, op->clip.x + op->clip.width), cy1 = fmin(clip.y + clip.height, op->clip.y + op->clip.height);
                clip       = (tbox_rect){ cx0, cy0, cx1 > cx0 ? cx1 - cx0 : 0.0, cy1 > cy0 ? cy1 - cy0 : 0.0 };
            }
            tbox_raster_image_clipped(pixels, buffer_width, buffer_height, op->rect, op->image, op->has_source_rect ? &op->source_rect : NULL, op->color.a / 255.0, true, clip, op->image_pixelated, op, op->color_filter, op->blend_mode);
        } else {
            tbox_raster_image_clipped(pixels, buffer_width, buffer_height, op->rect, op->image, op->has_source_rect ? &op->source_rect : NULL, op->color.a / 255.0, op->has_clip, op->clip, op->image_pixelated, op->polygon_clip != NULL ? op : NULL, op->color_filter, op->blend_mode);
        }
        break;
    case TBOX_PAINT_FILL_RING:
        tbox_raster_fill_ring_clipped(pixels, buffer_width, buffer_height, op);
        break;
    case TBOX_PAINT_BACKDROP_FILTER: /* need the paint region: tbox_raster_paint_range */
    case TBOX_PAINT_LAYER_BEGIN:
    case TBOX_PAINT_LAYER_MASK:
    case TBOX_PAINT_LAYER_END:
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

/* ---- compositing layers (docs/08-layout-e-pintura.md) ---- */

/* A buffer ops paint into: the screen, or a layer covering the screen
 * pixels [x, x + width) x [y, y + height). */
typedef struct tbox_raster_target {
    uint32_t *pixels;
    int32_t width, height, x, y;
} tbox_raster_target;

static tbox_rect tbox_raster_rect_intersect(tbox_rect a, tbox_rect b) {
    double x0 = fmax(a.x, b.x), y0 = fmax(a.y, b.y);
    double x1 = fmin(a.x + a.width, b.x + b.width), y1 = fmin(a.y + a.height, b.y + b.height);
    return (tbox_rect){ x0, y0, x1 > x0 ? x1 - x0 : 0.0, y1 > y0 ? y1 - y0 : 0.0 };
}

static bool tbox_raster_rect_empty(tbox_rect r) {
    return r.width <= 0.0 || r.height <= 0.0;
}

/* `r` grown by `by` px on every side and snapped outward to whole pixels. */
static tbox_rect tbox_raster_rect_grow(tbox_rect r, double by) {
    double x0 = floor(r.x - by), y0 = floor(r.y - by), x1 = ceil(r.x + r.width + by), y1 = ceil(r.y + r.height + by);
    return (tbox_rect){ x0, y0, x1 - x0, y1 - y0 };
}

static tbox_rect tbox_raster_target_rect(const tbox_raster_target *t) {
    return (tbox_rect){ (double)t->x, (double)t->y, (double)t->width, (double)t->height };
}

static bool tbox_raster_rect_covers(tbox_rect outer, tbox_rect inner) {
    return outer.x <= inner.x && outer.y <= inner.y && outer.x + outer.width >= inner.x + inner.width && outer.y + outer.height >= inner.y + inner.height;
}

/* Paints `op` into `t`, clipped to `region` (screen coordinates, whole
 * pixels) unless the region covers the whole target: every op clip
 * floors its edges, so clipping to a whole-pixel region changes nothing
 * inside it. A layer's offset is whole pixels too, so an op lands on the
 * same pixels it would on the screen. */
static void tbox_raster_paint_into(const tbox_raster_target *t, const tbox_paint_op *op, tbox_rect region) {
    tbox_paint_op shifted = *op;
    if (!tbox_raster_rect_covers(region, tbox_raster_target_rect(t))) {
        shifted.clip     = shifted.has_clip ? tbox_raster_rect_intersect(shifted.clip, region) : region;
        shifted.has_clip = true;
    }
    if (t->x != 0 || t->y != 0) {
        double dx = -(double)t->x, dy = -(double)t->y;
        shifted.rect.x += dx, shifted.rect.y += dy;
        shifted.inner_rect.x += dx, shifted.inner_rect.y += dy;
        shifted.clip.x += dx, shifted.clip.y += dy;
        shifted.rounded_clip.x += dx, shifted.rounded_clip.y += dy;
        shifted.polygon_clip_origin[0] += dx, shifted.polygon_clip_origin[1] += dy;
    }
    tbox_raster_paint_op(t->pixels, t->width, t->height, &shifted);
}

/* Gaussian blur (standard deviation `sigma`) of premultiplied pixels,
 * approximated by three box blurs per axis; outside the buffer is
 * transparent. */
static void tbox_raster_blur(uint32_t *pixels, int32_t width, int32_t height, double sigma) {
    if (sigma < 0.3 || width <= 0 || height <= 0)
        return;
    size_t count = (size_t)width * (size_t)height;
    float *planes = malloc(count * 4 * sizeof(float)), *scratch = malloc(count * 4 * sizeof(float));
    double *sums  = malloc((size_t)width * sizeof(double));
    if (planes == NULL || scratch == NULL || sums == NULL) {
        free(planes), free(scratch), free(sums);
        return;
    }
    for (size_t i = 0; i < count; i++)
        for (int c = 0; c < 4; c++) planes[(size_t)c * count + i] = (float)((pixels[i] >> (24 - 8 * c)) & 0xFFu);
    /* box sizes for three passes ("boxes for Gauss") */
    double ideal = sqrt(12.0 * sigma * sigma / 3.0 + 1.0);
    int lower    = (int)floor(ideal);
    if (lower % 2 == 0) lower--;
    int upper = lower + 2;
    int m     = (int)lround((12.0 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9.0) / (-4.0 * lower - 4.0));
    for (int pass = 0; pass < 3; pass++) {
        int radius = ((pass < m ? lower : upper) - 1) / 2;
        double scale = 1.0 / (2.0 * radius + 1.0);
        for (int c = 0; c < 4; c++) {
            float *src = planes + (size_t)c * count, *dst = scratch + (size_t)c * count;
            for (int32_t y = 0; y < height; y++) { /* horizontal: src -> dst */
                const float *row = src + (size_t)y * (size_t)width;
                float *out = dst + (size_t)y * (size_t)width;
                double sum = 0.0;
                for (int32_t x = -radius; x <= radius; x++) if (x >= 0 && x < width) sum += row[x];
                for (int32_t x = 0; x < width; x++) {
                    out[x] = (float)(sum * scale);
                    int32_t add = x + radius + 1, sub = x - radius;
                    if (add < width) sum += row[add];
                    if (sub >= 0) sum -= row[sub];
                }
            }
            /* vertical: dst -> src, row by row with one running sum per
             * column (walking columns would defeat the cache) */
            for (int32_t x = 0; x < width; x++) sums[x] = 0.0;
            for (int32_t y = 0; y <= radius && y < height; y++) {
                const float *row = dst + (size_t)y * (size_t)width;
                for (int32_t x = 0; x < width; x++) sums[x] += row[x];
            }
            for (int32_t y = 0; y < height; y++) {
                float *out = src + (size_t)y * (size_t)width;
                for (int32_t x = 0; x < width; x++) out[x] = (float)(sums[x] * scale);
                int32_t add = y + radius + 1, sub = y - radius;
                if (add < height) {
                    const float *row = dst + (size_t)add * (size_t)width;
                    for (int32_t x = 0; x < width; x++) sums[x] += row[x];
                }
                if (sub >= 0) {
                    const float *row = dst + (size_t)sub * (size_t)width;
                    for (int32_t x = 0; x < width; x++) sums[x] -= row[x];
                }
            }
        }
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t v[4];
        for (int c = 0; c < 4; c++) {
            float f = planes[(size_t)c * count + i];
            v[c] = f <= 0.0f ? 0u : f >= 255.0f ? 255u : (uint32_t)(f + 0.5f);
        }
        /* premultiplied: no channel above alpha */
        for (int c = 1; c < 4; c++) if (v[c] > v[0]) v[c] = v[0];
        pixels[i] = (v[0] << 24) | (v[1] << 16) | (v[2] << 8) | v[3];
    }
    free(planes);
    free(scratch);
    free(sums);
}

/* drop-shadow(): the buffer's alpha, in the shadow color, blurred and
 * offset, under the buffer itself. */
static void tbox_raster_drop_shadow(uint32_t *pixels, int32_t width, int32_t height, const tbox_style_shadow *shadow) {
    size_t count = (size_t)width * (size_t)height;
    uint32_t *under = calloc(count, sizeof(uint32_t));
    if (under == NULL || shadow->color.a == 0) {
        free(under);
        return;
    }
    int32_t dx = (int32_t)lround(shadow->offset_x), dy = (int32_t)lround(shadow->offset_y);
    double color_alpha = shadow->color.a / 255.0;
    for (int32_t y = 0; y < height; y++)
        for (int32_t x = 0; x < width; x++) {
            int32_t sx = x - dx, sy = y - dy;
            if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
            double a = (pixels[(size_t)sy * (size_t)width + (size_t)sx] >> 24) / 255.0 * color_alpha;
            if (a <= 0.0) continue;
            tbox_css_rgba c = shadow->color;
            c.a = (unsigned char)(a * 255.0 + 0.5);
            under[(size_t)y * (size_t)width + (size_t)x] = tbox_raster_pack(c);
        }
    tbox_raster_blur(under, width, height, shadow->blur / 2.0);
    for (size_t i = 0; i < count; i++) {
        uint32_t top = pixels[i];
        unsigned a = top >> 24;
        if (a == 0xFFu) continue;
        pixels[i] = a == 0 ? under[i] : tbox_raster_blend_pixel(under[i], tbox_raster_unpack(top), a / 255.0);
    }
    free(under);
}

/* Runs `filters` over a premultiplied buffer, in order. */
static void tbox_raster_apply_filters(uint32_t *pixels, int32_t width, int32_t height, const tbox_style_filter_list *filters) {
    size_t count = (size_t)width * (size_t)height;
    for (size_t f = 0; filters != NULL && f < filters->count; f++) {
        const tbox_style_filter_op *op = &filters->ops[f];
        if (op->kind == TBOX_STYLE_FILTER_MATRIX) {
            for (size_t i = 0; i < count; i++)
                if (pixels[i] != 0) pixels[i] = tbox_raster_pack(tbox_raster_filter(op->matrix, tbox_raster_unpack(pixels[i])));
        } else if (op->kind == TBOX_STYLE_FILTER_BLUR) {
            tbox_raster_blur(pixels, width, height, op->radius);
        } else {
            tbox_raster_drop_shadow(pixels, width, height, &op->shadow);
        }
    }
}

static size_t tbox_raster_paint_range(const tbox_raster_target *t, const tbox_display_list *list, size_t from, size_t to, tbox_rect region, tbox_rect space);

/* BACKDROP_FILTER: the filters run over a copy of what `t` already holds
 * around the op (blur reads past its edges), and the result replaces the
 * pixels inside the op's shape -- over them, so a filtered alpha shows the
 * old pixel through, then mixed with the original by color.a. */
static void tbox_raster_backdrop(const tbox_raster_target *t, const tbox_paint_op *op, tbox_rect region) {
    if (op->filters == NULL || op->filters->count == 0 || op->color.a == 0 || tbox_raster_rect_empty(op->rect))
        return;
    tbox_rect out = tbox_raster_rect_intersect(tbox_raster_rect_grow(op->rect, 0.0), region);
    if (op->has_clip) out = tbox_raster_rect_intersect(out, op->clip);
    if (op->has_rounded_clip) out = tbox_raster_rect_intersect(out, op->rounded_clip);
    out = tbox_raster_rect_intersect(tbox_raster_rect_grow(out, 0.0), tbox_raster_target_rect(t));
    if (tbox_raster_rect_empty(out))
        return;
    tbox_rect source = tbox_raster_rect_intersect(tbox_raster_rect_grow(out, tbox_style_filter_extent(op->filters)), tbox_raster_target_rect(t));
    int32_t sx = (int32_t)source.x, sy = (int32_t)source.y, sw = (int32_t)source.width, sh = (int32_t)source.height;
    uint32_t *copy = malloc((size_t)sw * (size_t)sh * sizeof(uint32_t));
    if (copy == NULL)
        return;
    for (int32_t y = 0; y < sh; y++)
        memcpy(copy + (size_t)y * (size_t)sw, t->pixels + (size_t)(sy + y - t->y) * (size_t)t->width + (size_t)(sx - t->x), (size_t)sw * sizeof(uint32_t));
    tbox_raster_apply_filters(copy, sw, sh, op->filters);
    double strength = op->color.a / 255.0;
    for (int32_t y = (int32_t)out.y; y < (int32_t)(out.y + out.height); y++) {
        uint32_t *row = t->pixels + (size_t)(y - t->y) * (size_t)t->width;
        for (int32_t x = (int32_t)out.x; x < (int32_t)(out.x + out.width); x++) {
            double px = (double)x + 0.5, py = (double)y + 0.5;
            if (!tbox_raster_point_in_shape(px, py, op->rect, op->corner_radii, op->corner_radii_y) || !tbox_raster_in_shape_clip(op, px, py))
                continue;
            uint32_t original = row[x - t->x], filtered = copy[(size_t)(y - sy) * (size_t)sw + (size_t)(x - sx)];
            unsigned fa = filtered >> 24;
            uint32_t result = fa == 0xFFu ? filtered : tbox_raster_blend_pixel(original, tbox_raster_unpack(filtered), fa / 255.0);
            if (strength < 1.0) {
                uint32_t mixed = 0;
                for (int c = 0; c < 4; c++) {
                    double a = (original >> (8 * c)) & 0xFFu, b = (result >> (8 * c)) & 0xFFu;
                    mixed |= (uint32_t)(a + (b - a) * strength + 0.5) << (8 * c);
                }
                result = mixed;
            }
            row[x - t->x] = result;
        }
    }
    free(copy);
}

/* A premultiplied pixel of `layer` at screen point (x, y), bilinear
 * between the four nearest pixel centers; transparent outside. */
static uint32_t tbox_raster_sample(const tbox_raster_target *layer, double x, double y) {
    double fx = x - 0.5 - (double)layer->x, fy = y - 0.5 - (double)layer->y;
    double x0f = floor(fx), y0f = floor(fy);
    if (x0f < -1.0 || y0f < -1.0 || x0f >= (double)layer->width || y0f >= (double)layer->height)
        return 0;
    int32_t x0 = (int32_t)x0f, y0 = (int32_t)y0f;
    double tx = fx - x0f, ty = fy - y0f;
    double sum[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (int j = 0; j < 2; j++) {
        int32_t py = y0 + j;
        if (py < 0 || py >= layer->height)
            continue;
        for (int i = 0; i < 2; i++) {
            int32_t px = x0 + i;
            if (px < 0 || px >= layer->width)
                continue;
            double w = (i ? tx : 1.0 - tx) * (j ? ty : 1.0 - ty);
            uint32_t p = layer->pixels[(size_t)py * (size_t)layer->width + (size_t)px];
            for (int c = 0; c < 4; c++)
                sum[c] += w * (double)((p >> (8 * c)) & 0xFFu);
        }
    }
    uint32_t out = 0;
    for (int c = 0; c < 4; c++) {
        double v = sum[c] + 0.5;
        out |= (uint32_t)(v >= 255.0 ? 255.0 : v) << (8 * c);
    }
    /* premultiplied: no channel above alpha */
    uint32_t a = out >> 24, result = a << 24;
    for (int c = 0; c < 3; c++) {
        uint32_t v = (out >> (8 * c)) & 0xFFu;
        result |= (v > a ? a : v) << (8 * c);
    }
    return result;
}

/* A transformed LAYER_BEGIN at `begin`: its ops paint untransformed into a
 * buffer covering what `region` (around the layer) needs of them, which
 * composites onto `t` through the matrix, sampling bilinearly. Returns
 * the index of its LAYER_END. */
static size_t tbox_raster_paint_transformed(const tbox_raster_target *t, const tbox_display_list *list, size_t begin, tbox_rect region, tbox_rect space) {
    const tbox_paint_op *op = &list->items[begin];
    size_t end = tbox_raster_layer_end(list, begin, NULL);
    const double *m = op->transform;
    double det = m[0] * m[3] - m[1] * m[2];
    tbox_rect content;
    if (!(fabs(det) > 1e-9) || !tbox_raster_layer_content_bounds(list, begin, space, &content))
        return end; /* a singular matrix flattens the layer to nothing */
    /* a pixel more around the content: the edges blend into transparent */
    content = tbox_raster_rect_grow(content, 1.0);
    tbox_rect out = tbox_raster_rect_intersect(tbox_raster_rect_grow(tbox_raster_transform_bounds(m, content), 0.0), region);
    if (op->has_clip) out = tbox_raster_rect_intersect(out, op->clip);
    if (op->has_rounded_clip) out = tbox_raster_rect_intersect(out, op->rounded_clip);
    out = tbox_raster_rect_intersect(tbox_raster_rect_grow(out, 0.0), tbox_raster_target_rect(t));
    if (tbox_raster_rect_empty(out))
        return end;
    /* the inverse maps screen points back into the layer */
    const double inv[6] = { m[3] / det, -m[1] / det, -m[2] / det, m[0] / det, (m[2] * m[5] - m[3] * m[4]) / det, (m[1] * m[4] - m[0] * m[5]) / det };
    tbox_rect needed = tbox_raster_rect_intersect(tbox_raster_rect_grow(tbox_raster_transform_bounds(inv, out), 1.0), content);
    /* a huge enlargement of a tiny source, or the reverse, stays bounded */
    if (tbox_raster_rect_empty(needed) || needed.width * needed.height > 64.0 * 1024.0 * 1024.0)
        return end;
    tbox_raster_target layer = { NULL, (int32_t)needed.width, (int32_t)needed.height, (int32_t)needed.x, (int32_t)needed.y };
    layer.pixels = calloc((size_t)layer.width * (size_t)layer.height, sizeof(uint32_t));
    if (layer.pixels == NULL)
        return end;
    tbox_raster_paint_range(&layer, list, begin + 1, end, needed, TBOX_RASTER_UNBOUNDED);
    double opacity = op->color.a / 255.0;
    for (int32_t y = (int32_t)out.y; y < (int32_t)(out.y + out.height); y++) {
        uint32_t *row = t->pixels + (size_t)(y - t->y) * (size_t)t->width;
        for (int32_t x = (int32_t)out.x; x < (int32_t)(out.x + out.width); x++) {
            double sx = (double)x + 0.5, sy = (double)y + 0.5;
            uint32_t p = tbox_raster_sample(&layer, inv[0] * sx + inv[2] * sy + inv[4], inv[1] * sx + inv[3] * sy + inv[5]);
            if ((p >> 24) == 0 || !tbox_raster_in_shape_clip(op, sx, sy))
                continue;
            tbox_css_rgba color = tbox_raster_unpack(p);
            row[x - t->x] = tbox_raster_blend_mode_pixel(row[x - t->x], color, color.a / 255.0 * opacity, op->blend_mode);
        }
    }
    free(layer.pixels);
    return end;
}

/* LAYER_BEGIN at `begin`: paints the layer and composites it onto `t`
 * within `region`. `space` bounds the coordinates around the layer: the
 * screen, or unbounded inside a transformed layer. Returns the index of
 * its LAYER_END. */
static size_t tbox_raster_paint_layer(const tbox_raster_target *t, const tbox_display_list *list, size_t begin, tbox_rect region, tbox_rect space) {
    const tbox_paint_op *op = &list->items[begin];
    if (op->has_transform)
        return tbox_raster_paint_transformed(t, list, begin, region, space);
    size_t mask_index;
    size_t end = tbox_raster_layer_end(list, begin, &mask_index);
    tbox_rect content;
    if (!tbox_raster_layer_content_bounds(list, begin, space, &content))
        return end;
    double extent = tbox_style_filter_extent(op->filters);
    /* pixels of `region` depend on content up to `extent` away */
    tbox_rect inner = tbox_raster_rect_intersect(tbox_raster_rect_grow(region, extent), space);
    tbox_rect area  = tbox_raster_rect_intersect(tbox_raster_rect_grow(content, extent), inner);
    tbox_rect out   = tbox_raster_rect_intersect(area, region);
    if (op->has_clip) out = tbox_raster_rect_intersect(out, op->clip);
    if (op->has_rounded_clip) out = tbox_raster_rect_intersect(out, op->rounded_clip);
    out = tbox_raster_rect_intersect(tbox_raster_rect_grow(out, 0.0), area);
    if (tbox_raster_rect_empty(out))
        return end;
    tbox_raster_target layer = { NULL, (int32_t)area.width, (int32_t)area.height, (int32_t)area.x, (int32_t)area.y };
    size_t count = (size_t)layer.width * (size_t)layer.height;
    layer.pixels = calloc(count, sizeof(uint32_t));
    if (layer.pixels == NULL)
        return end;
    tbox_raster_paint_range(&layer, list, begin + 1, mask_index, area, space);
    tbox_raster_apply_filters(layer.pixels, layer.width, layer.height, op->filters);
    if (mask_index < end) {
        tbox_raster_target mask = layer;
        mask.pixels = calloc(count, sizeof(uint32_t));
        double *coverage = calloc(count, sizeof(double));
        if (mask.pixels != NULL && coverage != NULL) {
            bool first = true;
            for (size_t m = mask_index; m < end;) {
                size_t next = m + 1;
                while (next < end && list->items[next].kind != TBOX_PAINT_LAYER_MASK) next++;
                memset(mask.pixels, 0, count * sizeof(uint32_t));
                tbox_raster_paint_range(&mask, list, m + 1, next, area, space);
                for (size_t i = 0; i < count; i++) {
                    uint32_t pixel = mask.pixels[i];
                    /* Premultiplied luminance already includes alpha. */
                    double source = list->items[m].mask_luminance ?
                        (0.2125 * ((pixel >> 16) & 0xFFu) + 0.7154 * ((pixel >> 8) & 0xFFu) + 0.0721 * (pixel & 0xFFu)) / 255.0 : (pixel >> 24) / 255.0;
                    double dest = coverage[i];
                    if (first) coverage[i] = source; /* bottom layer has no destination */
                    else switch (list->items[m].mask_composite) {
                    case TBOX_STYLE_MASK_ADD: coverage[i] = source + dest * (1.0 - source); break;
                    case TBOX_STYLE_MASK_SUBTRACT: coverage[i] = source * (1.0 - dest); break;
                    case TBOX_STYLE_MASK_INTERSECT: coverage[i] = source * dest; break;
                    case TBOX_STYLE_MASK_EXCLUDE: coverage[i] = source * (1.0 - dest) + dest * (1.0 - source); break;
                    }
                }
                first = false;
                m = next;
            }
            for (size_t i = 0; i < count; i++) {
                uint32_t pixel = layer.pixels[i], scaled = 0;
                for (int c = 0; c < 4; c++) scaled |= (uint32_t)(((pixel >> (8 * c)) & 0xFFu) * coverage[i] + 0.5) << (8 * c);
                layer.pixels[i] = scaled;
            }
        }
        free(coverage);
        free(mask.pixels);
    }

    double opacity = op->color.a / 255.0;
    for (int32_t y = (int32_t)out.y; y < (int32_t)(out.y + out.height); y++) {
        const uint32_t *source = layer.pixels + (size_t)(y - layer.y) * (size_t)layer.width;
        uint32_t *row = t->pixels + (size_t)(y - t->y) * (size_t)t->width;
        for (int32_t x = (int32_t)out.x; x < (int32_t)(out.x + out.width); x++) {
            uint32_t p = source[x - layer.x];
            if ((p >> 24) == 0 || !tbox_raster_in_shape_clip(op, (double)x + 0.5, (double)y + 0.5))
                continue;
            tbox_css_rgba color = tbox_raster_unpack(p);
            row[x - t->x] = tbox_raster_blend_mode_pixel(row[x - t->x], color, color.a / 255.0 * opacity, op->blend_mode);
        }
    }
    free(layer.pixels);
    return end;
}

/* Paints items [from, to) into `t`, within `region` (whole pixels, in the
 * coordinates `space` bounds). Returns `to`. */
static size_t tbox_raster_paint_range(const tbox_raster_target *t, const tbox_display_list *list, size_t from, size_t to, tbox_rect region, tbox_rect space) {
    for (size_t i = from; i < to; i++) {
        const tbox_paint_op *op = &list->items[i];
        if (op->kind == TBOX_PAINT_LAYER_BEGIN) {
            i = tbox_raster_paint_layer(t, list, i, region, space);
            continue;
        }
        if (op->kind == TBOX_PAINT_LAYER_MASK || op->kind == TBOX_PAINT_LAYER_END)
            continue; /* unmatched marker */
        if (op->kind == TBOX_PAINT_BACKDROP_FILTER) {
            tbox_raster_backdrop(t, op, region);
            continue;
        }
        tbox_rect bounds;
        if (!tbox_raster_op_bounds_in(op, space, &bounds) || tbox_raster_rect_empty(tbox_raster_rect_intersect(bounds, region)))
            continue;
        tbox_raster_paint_into(t, op, region);
    }
    return to;
}

void tbox_raster_display_list(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list) {
    if (list == NULL || pixels == NULL || buffer_width <= 0 || buffer_height <= 0) {
        return;
    }
    tbox_raster_target screen = { pixels, buffer_width, buffer_height, 0, 0 };
    tbox_raster_paint_range(&screen, list, 0, list->count, tbox_raster_target_rect(&screen), tbox_raster_target_rect(&screen));
}

void tbox_raster_display_list_damaged(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list, const tbox_damage *damage) {
    if (pixels == NULL || buffer_width <= 0 || buffer_height <= 0 || damage == NULL)
        return;
    if (damage->full) {
        size_t pixel_count = (size_t)buffer_width * (size_t)buffer_height;
        for (size_t i = 0; i < pixel_count; i++)
            pixels[i] = 0xFFFFFFFFu;
        tbox_raster_display_list(pixels, buffer_width, buffer_height, list);
        return;
    }
    tbox_raster_target screen = { pixels, buffer_width, buffer_height, 0, 0 };
    for (size_t d = 0; d < damage->count; d++) {
        tbox_rect region = damage->rects[d];
        int32_t x0 = (int32_t)region.x, y0 = (int32_t)region.y;
        int32_t x1 = (int32_t)(region.x + region.width), y1 = (int32_t)(region.y + region.height);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > buffer_width) x1 = buffer_width;
        if (y1 > buffer_height) y1 = buffer_height;
        if (x0 >= x1 || y0 >= y1)
            continue;
        for (int32_t y = y0; y < y1; y++) {
            uint32_t *row = pixels + (size_t)y * (size_t)buffer_width;
            for (int32_t x = x0; x < x1; x++)
                row[x] = 0xFFFFFFFFu;
        }
        region = (tbox_rect){ (double)x0, (double)y0, (double)(x1 - x0), (double)(y1 - y0) };
        if (list != NULL)
            tbox_raster_paint_range(&screen, list, 0, list->count, region, tbox_raster_target_rect(&screen));
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
