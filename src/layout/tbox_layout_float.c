/* Floats (CSS 2.1 section 9.5) and their wrap shapes (CSS Shapes 1).
 *
 * Each block formatting context owns one tbox_layout_floats, created by
 * the box that establishes it (tbox_layout_build_element_sized) and
 * handed down to its in-flow descendants inside
 * tbox_layout_positioned_context. It records every float already placed
 * in that context, in absolute coordinates, so later floats, `clear`,
 * blocks that establish their own context and line boxes can all ask the
 * same question: how much horizontal room is left in a band of y. */

#include "tbox_layout_internal.h"

#include <math.h>
#include <string.h>

typedef enum tbox_layout_float_shape {
    TBOX_LAYOUT_FLOAT_SHAPE_RECT,    /* `rect` with corner radii `h`/`v` */
    TBOX_LAYOUT_FLOAT_SHAPE_ELLIPSE, /* inscribed in `rect` */
    TBOX_LAYOUT_FLOAT_SHAPE_POLYGON, /* `points`, absolute x/y pairs */
    TBOX_LAYOUT_FLOAT_SHAPE_IMAGE,   /* `rows` (left/right per image row) over `rect` */
} tbox_layout_float_shape;

/* One placed float: its margin box (what other floats and blocks avoid)
 * and the area line boxes avoid. */
typedef struct tbox_layout_float_area {
    tbox_rect margin_box;
    bool right;
    tbox_layout_float_shape shape;
    tbox_rect rect;
    double h[4], v[4];
    const double *points;
    size_t point_count;
    const float *rows; /* per image row: left, right in image pixels; left > right when empty */
    size_t row_count;
    double image_width;
    double margin; /* shape-margin */
} tbox_layout_float_area;

struct tbox_layout_floats {
    tbox_vector areas;
    double last_top; /* a float's top may not be above an earlier one's */
};

tbox_layout_floats *tbox_layout_floats_create(tbox_arena *arena) {
    tbox_layout_floats *floats = (tbox_layout_floats *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_floats));
    if (floats == NULL)
        return NULL;
    tbox_vector_init(&floats->areas, arena, sizeof(tbox_layout_float_area), 0);
    floats->last_top = -1e300;
    return floats;
}

bool tbox_layout_floats_empty(const tbox_layout_floats *floats) {
    return floats == NULL || floats->areas.length == 0;
}

static const tbox_layout_float_area *tbox_layout_float_at(const tbox_layout_floats *floats, size_t i) {
    return (const tbox_layout_float_area *)tbox_vector_at_const(&floats->areas, i);
}

double tbox_layout_floats_clear_y(const tbox_layout_floats *floats, tbox_style_clear clear) {
    double bottom = -1e300;
    if (floats == NULL || clear == TBOX_STYLE_CLEAR_NONE)
        return bottom;
    for (size_t i = 0; i < floats->areas.length; i++) {
        const tbox_layout_float_area *area = tbox_layout_float_at(floats, i);
        bool applies = clear == TBOX_STYLE_CLEAR_BOTH || (clear == TBOX_STYLE_CLEAR_RIGHT) == area->right;
        if (applies && area->margin_box.y + area->margin_box.height > bottom)
            bottom = area->margin_box.y + area->margin_box.height;
    }
    return bottom;
}

double tbox_layout_floats_bottom(const tbox_layout_floats *floats) {
    return tbox_layout_floats_clear_y(floats, TBOX_STYLE_CLEAR_BOTH);
}

/* x of a rounded rect's left (`right` false) or right edge at `y`, inside
 * its vertical extent. */
static double tbox_layout_rounded_edge(const tbox_layout_float_area *area, double y, bool right) {
    tbox_rect r = area->rect;
    size_t top = right ? 1 : 0, bottom = right ? 2 : 3;
    double inset = 0.0;
    if (area->v[top] > 0.0 && y < r.y + area->v[top]) {
        double dy = (r.y + area->v[top] - y) / area->v[top];
        inset     = area->h[top] * (1.0 - sqrt(fmax(0.0, 1.0 - dy * dy)));
    } else if (area->v[bottom] > 0.0 && y > r.y + r.height - area->v[bottom]) {
        double dy = (y - (r.y + r.height - area->v[bottom])) / area->v[bottom];
        inset     = area->h[bottom] * (1.0 - sqrt(fmax(0.0, 1.0 - dy * dy)));
    }
    return right ? r.x + r.width - inset : r.x + inset;
}

/* The point of [y0, y1] closest to [low, high]. */
static double tbox_layout_closest(double y0, double y1, double low, double high) {
    if (y1 < low)
        return y1;
    if (y0 > high)
        return y0;
    return y0 > low ? y0 : (low < y1 ? low : y1);
}

/* The horizontal extent [*x0, *x1] of the float's wrap area within the
 * band [y0, y1), clipped to its margin box; false when the band misses it.
 * `shapes` false: the margin box alone (placement of floats and blocks). */
static bool tbox_layout_float_extent(const tbox_layout_float_area *area, double y0, double y1, bool shapes, double *x0, double *x1) {
    tbox_rect box = area->margin_box;
    if (y1 <= box.y || y0 >= box.y + box.height)
        return false;
    *x0 = box.x;
    *x1 = box.x + box.width;
    if (!shapes)
        return true;
    double m = area->margin, left = 0.0, right = 0.0;
    switch (area->shape) {
    case TBOX_LAYOUT_FLOAT_SHAPE_RECT: {
        tbox_rect r = area->rect;
        if (y1 <= r.y - m || y0 >= r.y + r.height + m || r.width <= 0.0)
            return false;
        /* The band's point nearest the straight part of each side. */
        double top_l = r.y + area->v[0], bottom_l = r.y + r.height - area->v[3];
        double top_r = r.y + area->v[1], bottom_r = r.y + r.height - area->v[2];
        double yl = tbox_layout_closest(y0, y1, top_l, bottom_l > top_l ? bottom_l : top_l);
        double yr = tbox_layout_closest(y0, y1, top_r, bottom_r > top_r ? bottom_r : top_r);
        yl = yl < r.y ? r.y : yl > r.y + r.height ? r.y + r.height : yl;
        yr = yr < r.y ? r.y : yr > r.y + r.height ? r.y + r.height : yr;
        left  = tbox_layout_rounded_edge(area, yl, false) - m;
        right = tbox_layout_rounded_edge(area, yr, true) + m;
        break;
    }
    case TBOX_LAYOUT_FLOAT_SHAPE_ELLIPSE: {
        double rx = area->rect.width / 2.0 + m, ry = area->rect.height / 2.0 + m;
        double cx = area->rect.x + area->rect.width / 2.0, cy = area->rect.y + area->rect.height / 2.0;
        if (rx <= 0.0 || ry <= 0.0)
            return false;
        double y  = tbox_layout_closest(y0, y1, cy, cy);
        double dy = (y - cy) / ry;
        if (dy * dy >= 1.0)
            return false;
        double half = rx * sqrt(1.0 - dy * dy);
        left        = cx - half;
        right       = cx + half;
        break;
    }
    case TBOX_LAYOUT_FLOAT_SHAPE_POLYGON: {
        /* Each edge clipped to the band (grown by the margin); the extremes
         * of the clipped pieces bound the polygon inside the band. */
        double b0 = y0 - m, b1 = y1 + m;
        bool any = false;
        for (size_t i = 0; i < area->point_count; i++) {
            size_t j    = (i + 1) % area->point_count;
            double ax   = area->points[2 * i], ay = area->points[2 * i + 1];
            double bx   = area->points[2 * j], by = area->points[2 * j + 1];
            double low  = ay < by ? ay : by, high = ay < by ? by : ay;
            if (high < b0 || low > b1)
                continue;
            double c0 = low > b0 ? low : b0, c1 = high < b1 ? high : b1;
            for (int k = 0; k < 2; k++) {
                double y = k == 0 ? c0 : c1;
                double x = by == ay ? (k == 0 ? ax : bx) : ax + (bx - ax) * (y - ay) / (by - ay);
                if (!any || x < left) left = x;
                if (!any || x > right) right = x;
                any = true;
            }
            if (by == ay) {
                if (bx < left) left = bx;
                if (bx > right) right = bx;
                if (ax < left) left = ax;
                if (ax > right) right = ax;
            }
        }
        if (!any)
            return false;
        left -= m;
        right += m;
        break;
    }
    case TBOX_LAYOUT_FLOAT_SHAPE_IMAGE: {
        tbox_rect r = area->rect;
        if (area->row_count == 0 || r.height <= 0.0 || r.width <= 0.0)
            return false;
        double scale_y = (double)area->row_count / r.height, scale_x = r.width / area->image_width;
        double from = (y0 - m - r.y) * scale_y, to = (y1 + m - r.y) * scale_y;
        long first = (long)floor(from), last = (long)ceil(to) - 1;
        if (first < 0) first = 0;
        if (last >= (long)area->row_count) last = (long)area->row_count - 1;
        bool any = false;
        for (long row = first; row <= last; row++) {
            double l = area->rows[2 * row], rr = area->rows[2 * row + 1];
            if (l > rr)
                continue;
            if (!any || l < left) left = l;
            if (!any || rr > right) right = rr;
            any = true;
        }
        if (!any)
            return false;
        left  = r.x + left * scale_x - m;
        right = r.x + right * scale_x + m;
        break;
    }
    }
    if (left < *x0) left = *x0;
    if (right > *x1) right = *x1;
    if (left >= right)
        return false;
    *x0 = left;
    *x1 = right;
    return true;
}

void tbox_layout_floats_band(const tbox_layout_floats *floats, double y0, double y1, double x0, double x1, bool shapes, double *out_left, double *out_right) {
    double left = x0, right = x1;
    if (y1 <= y0)
        y1 = y0 + 0.01;
    for (size_t i = 0; floats != NULL && i < floats->areas.length; i++) {
        const tbox_layout_float_area *area = tbox_layout_float_at(floats, i);
        double ex0, ex1;
        if (!tbox_layout_float_extent(area, y0, y1, shapes, &ex0, &ex1))
            continue;
        if (area->right) {
            if (ex0 < right) right = ex0;
        } else if (ex1 > left) {
            left = ex1;
        }
    }
    *out_left  = left;
    *out_right = right;
}

double tbox_layout_floats_next_y(const tbox_layout_floats *floats, double y0, double y1, double x0, double x1, bool shapes) {
    double next = 1e300;
    if (y1 <= y0)
        y1 = y0 + 0.01;
    for (size_t i = 0; floats != NULL && i < floats->areas.length; i++) {
        const tbox_layout_float_area *area = tbox_layout_float_at(floats, i);
        double ex0, ex1;
        if (!tbox_layout_float_extent(area, y0, y1, shapes, &ex0, &ex1))
            continue;
        if ((area->right && ex0 >= x1) || (!area->right && ex1 <= x0))
            continue; /* does not narrow this band */
        double bottom = area->margin_box.y + area->margin_box.height;
        if (shapes && area->shape == TBOX_LAYOUT_FLOAT_SHAPE_RECT) {
            double shape_bottom = area->rect.y + area->rect.height + area->margin;
            if (shape_bottom < bottom)
                bottom = shape_bottom;
        }
        /* A shaped float frees room gradually: step down a little. */
        if (shapes && area->shape != TBOX_LAYOUT_FLOAT_SHAPE_RECT && y0 + 2.0 < bottom)
            bottom = y0 + 2.0;
        if (bottom > y0 && bottom < next)
            next = bottom;
    }
    return next == 1e300 ? y0 : next;
}

void tbox_layout_floats_position(const tbox_layout_floats *floats, const tbox_style *style, double width, double height, double cb_x, double cb_width, double y_min, double *out_x, double *out_y) {
    double y = y_min;
    if (floats != NULL && floats->last_top > y)
        y = floats->last_top;
    double clear_y = tbox_layout_floats_clear_y(floats, style->clear);
    if (clear_y > y)
        y = clear_y;
    double left = cb_x, right = cb_x + cb_width;
    /* Down past floats until the margin box fits beside them, or nothing
     * narrows the containing block any more. */
    for (int guard = 0; guard < 10000; guard++) {
        tbox_layout_floats_band(floats, y, y + height, cb_x, cb_x + cb_width, false, &left, &right);
        if (right - left >= width - 0.01 || (left <= cb_x && right >= cb_x + cb_width))
            break;
        double next = tbox_layout_floats_next_y(floats, y, y + height, cb_x, cb_x + cb_width, false);
        if (next <= y)
            break;
        y = next;
    }
    *out_x = style->float_side == TBOX_STYLE_FLOAT_RIGHT ? right - width : left;
    *out_y = y;
}

/* The float's border-radius corners for its reference box `rect`, grown
 * (or shrunk) from the border box by `outset` (CSS Shapes 1 section
 * 3.2). */
static void tbox_layout_float_corners(const tbox_style *style, tbox_rect border_box, double outset, tbox_rect rect, double h[4], double v[4]) {
    bool vertical = false;
    for (size_t i = 0; i < 4; i++)
        if (style->border_radius_vertical[i] > 0.0 || style->border_radius_vertical_percent[i] > 0.0)
            vertical = true;
    double base = border_box.width < border_box.height ? border_box.width : border_box.height;
    for (size_t i = 0; i < 4; i++) {
        h[i] = style->border_radius_corners[i] + style->border_radius_percent[i] / 100.0 * (vertical ? border_box.width : base);
        v[i] = vertical ? style->border_radius_vertical[i] + style->border_radius_vertical_percent[i] / 100.0 * border_box.height : h[i];
        if (h[i] <= 0.0 || v[i] <= 0.0) {
            h[i] = v[i] = 0.0;
            continue;
        }
        h[i] = fmax(0.0, h[i] + outset);
        v[i] = fmax(0.0, v[i] + outset);
    }
    double scale           = 1.0;
    const double sums[4]   = { h[0] + h[1], h[3] + h[2], v[0] + v[3], v[1] + v[2] };
    const double limits[4] = { rect.width, rect.width, rect.height, rect.height };
    for (size_t i = 0; i < 4; i++)
        if (sums[i] > 0.0 && limits[i] / sums[i] < scale)
            scale = limits[i] / sums[i];
    for (size_t i = 0; i < 4; i++)
        h[i] *= fmax(0.0, scale), v[i] *= fmax(0.0, scale);
}

/* The wrap area of `box` from its `shape-outside`, the same basic-shape
 * geometry Render gives clip-path, over the chosen reference box. */
static void tbox_layout_float_shape_of(tbox_layout_float_area *area, const tbox_layout_box *box, tbox_image_cache *images, double cb_width, tbox_arena *arena) {
    const tbox_style *style             = box->style;
    const tbox_style_shape_outside *out = &style->shape_outside;
    area->shape                         = TBOX_LAYOUT_FLOAT_SHAPE_RECT;
    area->rect                          = box->margin_box;
    if (box->float_ink_height > 0.0)
        area->rect.height = box->float_ink_height;
    if (out->kind == TBOX_STYLE_SHAPE_OUTSIDE_NONE)
        return;
    area->margin = tbox_style_length_resolve(style->shape_margin, cb_width);
    if (area->margin < 0.0)
        area->margin = 0.0;

    tbox_rect reference = box->margin_box;
    double outset       = box->margin_box.x < box->border_box.x ? box->border_box.x - box->margin_box.x : 0.0;
    switch (out->box) {
    case TBOX_STYLE_SHAPE_BOX_BORDER: reference = box->border_box, outset = 0.0; break;
    case TBOX_STYLE_SHAPE_BOX_PADDING: reference = box->padding_box, outset = -(box->padding_box.x - box->border_box.x); break;
    case TBOX_STYLE_SHAPE_BOX_CONTENT: reference = box->content_box, outset = -(box->content_box.x - box->border_box.x); break;
    case TBOX_STYLE_SHAPE_BOX_MARGIN:
    default: break;
    }

    if (out->kind == TBOX_STYLE_SHAPE_OUTSIDE_BOX) {
        area->rect = reference;
        tbox_layout_float_corners(style, box->border_box, outset, reference, area->h, area->v);
        return;
    }
    if (out->kind == TBOX_STYLE_SHAPE_OUTSIDE_IMAGE) {
        const tbox_image *image = images != NULL ? tbox_image_cache_get(images, tbox_string_view_from_cstr(out->image)) : NULL;
        if (image == NULL || image->width <= 0 || image->height <= 0 || image->pixels == NULL) {
            area->margin = 0.0; /* no image: the float area is the margin box */
            return;
        }
        float *rows = (float *)tbox_arena_alloc(arena, sizeof(float) * 2u * (size_t)image->height);
        if (rows == NULL)
            return;
        /* A pixel is inside the shape when its alpha exceeds the threshold. */
        double threshold = style->shape_image_threshold * 255.0;
        for (int32_t y = 0; y < image->height; y++) {
            const unsigned char *row = image->pixels + (size_t)y * (size_t)image->width * 4u;
            int32_t l = -1, r = -1;
            for (int32_t x = 0; x < image->width; x++)
                if ((double)row[4 * x + 3] > threshold) {
                    if (l < 0) l = x;
                    r = x + 1;
                }
            rows[2 * y]     = l < 0 ? 1.0f : (float)l;
            rows[2 * y + 1] = l < 0 ? 0.0f : (float)r;
        }
        area->shape       = TBOX_LAYOUT_FLOAT_SHAPE_IMAGE;
        area->rect        = box->content_box; /* the image is sized like the element's own content */
        area->rows        = rows;
        area->row_count   = (size_t)image->height;
        area->image_width = (double)image->width;
        return;
    }

    const tbox_style_clip_path *clip = &out->shape;
    tbox_rect r                      = reference;
    if (clip->kind == TBOX_STYLE_CLIP_PATH_INSET) {
        double top = tbox_style_length_resolve(clip->inset[0], r.height), right = tbox_style_length_resolve(clip->inset[1], r.width);
        double bottom = tbox_style_length_resolve(clip->inset[2], r.height), left = tbox_style_length_resolve(clip->inset[3], r.width);
        area->rect = (tbox_rect){ r.x + left, r.y + top, fmax(0.0, r.width - left - right), fmax(0.0, r.height - top - bottom) };
        for (size_t i = 0; i < 4; i++) {
            area->h[i] = fmax(0.0, clip->round_h[i] + r.width * clip->round_h_percent[i] / 100.0);
            area->v[i] = fmax(0.0, clip->round_v[i] + r.height * clip->round_v_percent[i] / 100.0);
        }
        const double sums[4]   = { area->h[0] + area->h[1], area->h[2] + area->h[3], area->v[0] + area->v[3], area->v[1] + area->v[2] };
        const double limits[4] = { area->rect.width, area->rect.width, area->rect.height, area->rect.height };
        double scale           = 1.0;
        for (size_t i = 0; i < 4; i++)
            if (sums[i] > 0.0) scale = fmin(scale, limits[i] / sums[i]);
        for (size_t i = 0; i < 4; i++)
            area->h[i] *= scale, area->v[i] *= scale;
    } else if (clip->kind == TBOX_STYLE_CLIP_PATH_POLYGON) {
        double *points = (double *)tbox_arena_alloc(arena, sizeof(double) * 2u * clip->point_count);
        if (points == NULL)
            return;
        for (size_t i = 0; i < clip->point_count; i++) {
            points[2 * i]     = r.x + tbox_style_length_resolve(clip->points[i][0], r.width);
            points[2 * i + 1] = r.y + tbox_style_length_resolve(clip->points[i][1], r.height);
        }
        area->shape       = TBOX_LAYOUT_FLOAT_SHAPE_POLYGON;
        area->points      = points;
        area->point_count = clip->point_count;
    } else {
        /* circle()/ellipse(): radius keywords as Render resolves them
         * (closest-side is AUTO, farthest-side a negative AUTO). */
        double cx = r.x + tbox_style_length_resolve(clip->center[0], r.width), cy = r.y + tbox_style_length_resolve(clip->center[1], r.height);
        double near_x = fmin(cx - r.x, r.x + r.width - cx), far_x = fmax(cx - r.x, r.x + r.width - cx);
        double near_y = fmin(cy - r.y, r.y + r.height - cy), far_y = fmax(cy - r.y, r.y + r.height - cy);
        double rx, ry;
        if (clip->kind == TBOX_STYLE_CLIP_PATH_CIRCLE) {
            tbox_style_length radius = clip->radius[0];
            double reference_length  = sqrt((r.width * r.width + r.height * r.height) / 2.0);
            rx = ry = radius.kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(radius, reference_length) : radius.value < 0.0 ? fmax(far_x, far_y) : fmin(near_x, near_y);
        } else {
            rx = clip->radius[0].kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(clip->radius[0], r.width) : clip->radius[0].value < 0.0 ? far_x : near_x;
            ry = clip->radius[1].kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(clip->radius[1], r.height) : clip->radius[1].value < 0.0 ? far_y : near_y;
        }
        area->shape = TBOX_LAYOUT_FLOAT_SHAPE_ELLIPSE;
        area->rect  = (tbox_rect){ cx - rx, cy - ry, 2.0 * fmax(0.0, rx), 2.0 * fmax(0.0, ry) };
    }
}

void tbox_layout_floats_add(tbox_layout_floats *floats, const tbox_layout_box *box, tbox_image_cache *images, double cb_width, tbox_arena *arena) {
    if (floats == NULL)
        return;
    tbox_layout_float_area *area = (tbox_layout_float_area *)tbox_vector_push(&floats->areas);
    if (area == NULL)
        return;
    memset(area, 0, sizeof(*area));
    area->margin_box = box->margin_box;
    area->right      = box->style->float_side == TBOX_STYLE_FLOAT_RIGHT;
    tbox_layout_float_shape_of(area, box, images, cb_width, arena);
    if (box->margin_box.y > floats->last_top)
        floats->last_top = box->margin_box.y;
}

void tbox_layout_floats_place(tbox_layout_floats *floats, tbox_layout_box *box, double cb_x, double cb_width, double y_min, tbox_image_cache *images, tbox_arena *arena) {
    double x, y;
    tbox_layout_floats_position(floats, box->style, box->margin_box.width, box->margin_box.height, cb_x, cb_width, y_min, &x, &y);
    tbox_layout_translate(box, x - box->margin_box.x, y - box->margin_box.y);
    tbox_layout_floats_add(floats, box, images, cb_width, arena);
}

/* A float's box, built against `container` at `y`: auto width is
 * shrink-to-fit (CSS 2.1 section 10.3.5), as an inline-block's. The caller
 * moves it to its place. */
tbox_layout_box *tbox_layout_build_float_box(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double y, tbox_layout_positioned_context context) {
    tbox_layout_forced_size forced = { 0 };
    if (style->width.kind == TBOX_STYLE_LENGTH_AUTO && style->width_keyword == TBOX_STYLE_SIZE_KEYWORD_NONE && !tbox_layout_is_replaced_image(node) && !(style->aspect_ratio > 0.0 && style->height.kind != TBOX_STYLE_LENGTH_AUTO)) {
        tbox_layout_intrinsic outer = tbox_layout_intrinsic_outer(arena, node, styles, fonts, images);
        double margins              = tbox_layout_resolve_edge(style->margin[1], container.width) + tbox_layout_resolve_edge(style->margin[3], container.width);
        double available            = container.width - margins;
        double width                = outer.max - margins;
        if (width > available)
            width = available;
        if (width < outer.min - margins)
            width = outer.min - margins;
        forced.has_width = true;
        forced.width     = width > 0.0 ? width : 0.0;
    }
    return tbox_layout_build_element_sized(arena, node, styles, fonts, images, container, y, context, NULL, 0, forced.has_width ? &forced : NULL);
}

bool tbox_layout_is_float(const tbox_html_node *node, const tbox_style *style) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && style->float_side != TBOX_STYLE_FLOAT_NONE &&
           style->position != TBOX_STYLE_POSITION_ABSOLUTE && style->position != TBOX_STYLE_POSITION_FIXED && style->display != TBOX_STYLE_DISPLAY_NONE;
}

/* Whether `node` establishes a new block formatting context (CSS 2.1
 * section 9.4.1, Display 3): such a box contains its floats, and in flow
 * it is placed beside the floats around it instead of under them. */
bool tbox_layout_establishes_bfc(const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT)
        return false;
    if (style->float_side != TBOX_STYLE_FLOAT_NONE || style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED)
        return true;
    if (style->display != TBOX_STYLE_DISPLAY_BLOCK || style->display_flow_root)
        return true; /* inline-block, flex and inline-flex too */
    if (style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE || style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
        return true;
    if (tbox_layout_tag(node, "table") || tbox_layout_table_cell_node(node) || tbox_layout_tag(node, "caption") || tbox_layout_tag(node, "html"))
        return true;
    /* Replaced elements and form controls never let lines into them. */
    if (tbox_layout_is_replaced_image(node) || tbox_layout_tag(node, "input") || tbox_layout_tag(node, "button") || tbox_layout_is_select(node) || tbox_layout_is_textarea(node))
        return true;
    if (node->parent != NULL && node->parent->type == TBOX_HTML_NODE_ELEMENT) {
        const tbox_style *parent = tbox_layout_style_or_default(styles, node->parent);
        if (parent->display == TBOX_STYLE_DISPLAY_FLEX || parent->display == TBOX_STYLE_DISPLAY_INLINE_FLEX)
            return true; /* a flex item */
    }
    return false;
}
