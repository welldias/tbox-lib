#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "svg/tbox_svg_internal.h"
#include <math.h>
#include <string.h>

static double tbox_svg_number_attr(const tbox_html_node *node, const char *name, double base, double fallback) {
    tbox_string_view v = tbox_svg_attr(node, name);
    double n;
    if (tbox_number_parse(v, &n))
        return n;
    if (v.size > 1 && v.data[v.size - 1] == '%' && tbox_number_parse(tbox_string_view_make(v.data, v.size - 1), &n))
        return n * base / 100;
    return fallback;
}
static tbox_paint_op *tbox_svg_op(tbox_vector *items, tbox_paint_op_kind kind) {
    tbox_paint_op *op = tbox_vector_push(items);
    if (op != NULL)
        *op = (tbox_paint_op){ .kind = kind };
    return op;
}
static void tbox_svg_emit_path(const tbox_svg_node *node, const double matrix[6], double vw, double vh, tbox_vector *items, tbox_arena *arena) {
    if (node->style->visibility_hidden)
        return;
    tbox_vector segments;
    tbox_vector_init(&segments, arena, sizeof(tbox_path_segment), 0);
    double scale = fmax(hypot(matrix[0], matrix[1]), hypot(matrix[2], matrix[3]));
    if (!isfinite(scale) || scale <= 0 || !tbox_svg_shape(node->node, &segments, .025 / fmax(1, scale), vw, vh, node->style->font_size) || segments.length == 0)
        return;
    const tbox_path_segment *s = segments.data;
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (size_t i = 0; i < segments.length; i++) {
        double x = matrix[0] * s[i].x1 + matrix[2] * s[i].y1 + matrix[4], y = matrix[1] * s[i].x1 + matrix[3] * s[i].y1 + matrix[5];
        if (!isfinite(x) || !isfinite(y) || fabs(x) > 1048576 || fabs(y) > 1048576)
            return;
        x0 = fmin(x0, x);
        x1 = fmax(x1, x);
        y0 = fmin(y0, y);
        y1 = fmax(y1, y);
    }
    tbox_paint_path_segment *edges = tbox_arena_alloc(arena, segments.length * sizeof(*edges));
    if (edges == NULL)
        return;
    for (size_t i = 0; i < segments.length; i++)
        edges[i] = (tbox_paint_path_segment){ s[i].x0, s[i].y0, s[i].x1, s[i].y1, s[i].move, s[i].close };
    const tbox_style_svg_paint *paint = tbox_style_svg_paint_of(node->style);
    double base = hypot(vw, vh) / sqrt(2), width = tbox_style_length_resolve(paint->width, base);
    for (size_t stage = 0; stage < 3; stage++) {
        bool stroke = node->style->paint_order[stage] == TBOX_STYLE_PAINT_STROKE;
        if (node->style->paint_order[stage] == TBOX_STYLE_PAINT_MARKERS)
            continue;
        tbox_style_svg_color color = stroke ? paint->stroke : paint->fill;
        if (color.kind == TBOX_STYLE_SVG_NONE || (stroke && !(width > 0)))
            continue;
        tbox_css_rgba rgba = color.kind == TBOX_STYLE_SVG_CURRENT_COLOR ? node->style->color : color.color;
        rgba.a             = (unsigned char)(rgba.a * (stroke ? paint->stroke_opacity : paint->fill_opacity) + .5);
        if (rgba.a == 0)
            continue;
        tbox_paint_path *path = tbox_arena_alloc_zero(arena, sizeof(*path));
        if (path == NULL)
            continue;
        path->segments = edges;
        path->count    = segments.length;
        path->stroke   = stroke;
        path->evenodd  = paint->evenodd;
        memcpy(path->matrix, matrix, sizeof(path->matrix));
        path->width      = width;
        path->linecap    = paint->linecap;
        path->linejoin   = paint->linejoin;
        path->miterlimit = paint->miterlimit;
        double author_length;
        double length_scale = tbox_number_parse(tbox_svg_attr(node->node, "pathlength"), &author_length) && author_length > 0 ? s[segments.length - 1].length / author_length : 1;
        path->dash_count    = paint->dash_count;
        for (size_t i = 0; i < path->dash_count; i++)
            path->dashes[i] = tbox_style_length_resolve(paint->dashes[i], base) * length_scale;
        path->dashoffset = tbox_style_length_resolve(paint->dashoffset, base) * length_scale;
        /* Frobenius norm bounds every direction, including shear. */
        double norm       = hypot(hypot(matrix[0], matrix[1]), hypot(matrix[2], matrix[3]));
        double pad        = stroke ? width * .5 * norm * fmax(1, paint->linejoin == 0 ? paint->miterlimit : 1.5) + 1 : 1;
        tbox_paint_op *op = tbox_svg_op(items, TBOX_PAINT_PATH);
        if (op == NULL)
            continue;
        op->path  = path;
        op->color = rgba;
        op->rect  = (tbox_rect){ x0 - pad, y0 - pad, x1 - x0 + 2 * pad, y1 - y0 + 2 * pad };
    }
}

static void tbox_svg_paint_node(const tbox_svg_node *node, const double parent_matrix[6], double vw, double vh, tbox_vector *items, tbox_arena *arena, bool root, const tbox_html_node *use) {
    double m[6];
    memcpy(m, parent_matrix, sizeof(m));
    const tbox_style *style = node->style;
    if (style->display == TBOX_STYLE_DISPLAY_NONE || style->opacity <= 0)
        return;
    /* HTML root transforms/effects are already handled by Render's box. */
    if (!root) {
        double t[6];
        tbox_style_transform_matrix(style, 0, 0, vw, vh, NULL, t);
        tbox_svg_matrix_multiply(m, t);
    }
    bool viewport = !root && (tbox_svg_tag(node->node, "svg") || tbox_svg_tag(node->node, "symbol"));
    bool clipped  = viewport && (style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE || style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE);
    double viewport_matrix[6];
    memcpy(viewport_matrix, m, sizeof(m));
    size_t layer = items->length;
    bool layered = !root && (style->opacity < 1 || clipped);
    if (layered) {
        tbox_paint_op *begin = tbox_svg_op(items, TBOX_PAINT_LAYER_BEGIN);
        if (begin != NULL)
            begin->color.a = (unsigned char)(style->opacity * 255 + .5);
    }
    tbox_rect vp = { 0, 0, vw, vh };
    if (viewport) {
        vp.x      = tbox_svg_number_attr(node->node, "x", vw, 0);
        vp.y      = tbox_svg_number_attr(node->node, "y", vh, 0);
        vp.width  = tbox_svg_number_attr(use != NULL ? use : node->node, "width", vw, vw);
        vp.height = tbox_svg_number_attr(use != NULL ? use : node->node, "height", vh, vh);
        if (vp.width <= 0 || vp.height <= 0) {
            items->length = layer;
            return;
        }
        double view[6];
        tbox_svg_view_matrix(node->node, vp, view);
        tbox_svg_matrix_multiply(m, view);
        tbox_rect vb;
        vw = tbox_svg_viewbox(node->node, &vb) ? vb.width : vp.width;
        vh = tbox_svg_viewbox(node->node, &vb) ? vb.height : vp.height;
    }
    if (tbox_svg_tag(node->node, "use")) {
        double translate[6] = { 1, 0, 0, 1, tbox_svg_number_attr(node->node, "x", vw, 0), tbox_svg_number_attr(node->node, "y", vh, 0) };
        tbox_svg_matrix_multiply(m, translate);
    }
    tbox_svg_emit_path(node, m, vw, vh, items, arena);
    for (const tbox_svg_node *child = node->first_child; child != NULL; child = child->next_sibling)
        tbox_svg_paint_node(child, m, vw, vh, items, arena, false, tbox_svg_tag(node->node, "use") ? node->node : NULL);
    if (clipped) {
        /* Viewport clip in parent coordinates. Transformed viewports use
         * a polygon clip below rather than an axis-aligned approximation. */
        tbox_paint_polygon *polygon = tbox_arena_alloc_zero(arena, sizeof(*polygon));
        if (polygon != NULL) {
            polygon->count     = 4;
            const double xs[4] = { vp.x, vp.x + vp.width, vp.x + vp.width, vp.x }, ys[4] = { vp.y, vp.y, vp.y + vp.height, vp.y + vp.height };
            for (size_t i = 0; i < 4; i++) {
                polygon->points[i][0] = viewport_matrix[0] * xs[i] + viewport_matrix[2] * ys[i] + viewport_matrix[4];
                polygon->points[i][1] = viewport_matrix[1] * xs[i] + viewport_matrix[3] * ys[i] + viewport_matrix[5];
            }
            /* A layer preserves nested clips independently. */
            tbox_paint_op *begin = layered ? &((tbox_paint_op *)items->data)[layer] : NULL;
            if (begin != NULL)
                begin->polygon_clip = polygon;
            else
                for (size_t i = layer; i < items->length; i++)
                    ((tbox_paint_op *)items->data)[i].polygon_clip = polygon;
        }
    }
    if (layered)
        tbox_svg_op(items, TBOX_PAINT_LAYER_END);
}
void tbox_render_svg(const tbox_layout_box *box, tbox_vector *items, tbox_arena *arena) {
    if (box->style == NULL || box->style->svg == NULL || box->content_box.width <= 0 || box->content_box.height <= 0)
        return;
    double m[6];
    tbox_svg_view_matrix(box->node, box->content_box, m);
    tbox_rect vb;
    double vw = tbox_svg_viewbox(box->node, &vb) ? vb.width : box->content_box.width;
    double vh = tbox_svg_viewbox(box->node, &vb) ? vb.height : box->content_box.height;
    if (vw <= 0 || vh <= 0)
        return;
    size_t start = items->length;
    tbox_svg_paint_node(box->style->svg, m, vw, vh, items, arena, true, NULL);
    bool clip = box->style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE || box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE;
    if (clip)
        for (size_t i = start; i < items->length; i++) {
            tbox_paint_op *op = &((tbox_paint_op *)items->data)[i];
            if (!op->has_clip)
                op->clip = box->content_box;
            else {
                double x = fmax(op->clip.x, box->content_box.x), y = fmax(op->clip.y, box->content_box.y);
                op->clip = (tbox_rect){ x, y, fmax(0, fmin(op->clip.x + op->clip.width, box->content_box.x + box->content_box.width) - x), fmax(0, fmin(op->clip.y + op->clip.height, box->content_box.y + box->content_box.height) - y) };
            }
            op->has_clip = true;
        }
}
