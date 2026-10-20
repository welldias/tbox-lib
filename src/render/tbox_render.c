#include <tbox/render.h>

#include <tbox/css_cascade.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "font/tbox_font_measure.h"
#include "render/tbox_render_internal.h"

/* Pushes one FILL_RECT of `color` covering `rect` onto `items` -- shared by
 * the background fill and the  4 border strips below, since none
 * of them carry text. `radius` always 0.0 here -- every caller of THIS
 * helper wants a plain rectangle; see tbox_render_push_fill_rect_rounded
 * below for the border-radius/box-shadow paths. */
static void tbox_render_push_fill_rect(tbox_vector *items, tbox_rect rect, tbox_css_rgba color) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    *op               = (tbox_paint_op){ 0 };
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
    op->radius        = 0.0;
    for (size_t i = 0; i < 4; i++)
        op->corner_radii[i] = 0.0;
    op->has_clip = false;
}

static bool tbox_render_checked_checkbox(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(node->element.tag_name, "input"))
        return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8)) && tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

/* `accent-color: auto` (alpha 0) paints form marks in the text color. */
static tbox_css_rgba tbox_render_accent_color(const tbox_style *style) {
    return style->accent_color.a != 0 ? style->accent_color : style->color;
}

/* ::checkmark: a declared `color` wins over accent-color. */
static tbox_css_rgba tbox_render_mark_color(const tbox_style *style) {
    const tbox_style_part_style *mark = tbox_style_part_find(style, TBOX_STYLE_PART_CHECKMARK);
    return mark != NULL && mark->color_declared ? mark->color : tbox_render_accent_color(style);
}

static bool tbox_render_checked_radio(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(node->element.tag_name, "input"))
        return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("radio", 5)) && tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

static bool tbox_render_color_input(const tbox_html_node *node, tbox_css_rgba *out_color) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(node->element.tag_name, "input"))
        return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    if (type == NULL || !tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("color", 5)))
        return false;
    *out_color                       = (tbox_css_rgba){ 0, 0, 0, 255 };
    const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (value != NULL && value->value.size == 7)
        tbox_css_hex_to_rgba(value->value, out_color);
    out_color->a = 255;
    return true;
}

/* NOVO (visual fidelity): same as tbox_render_push_fill_rect above, but
 * with a corner radius -- used only by the border-radius/box-shadow paths
 * below, never by the pre-existing background/border-strip/mark-highlight/
 * text-decoration call sites (which stay exactly as they were, unchanged,
 * at radius 0.0 via tbox_render_push_fill_rect). `radius` is clamped here
 * to at most half of `min(rect.width, rect.height)`, standard CSS
 * border-radius behavior -- callers never need to clamp it themselves. */
static void tbox_render_normalize_corners(tbox_rect rect, const double corners[4], double radii[4]) {
    for (size_t i = 0; i < 4; i++)
        radii[i] = corners[i] > 0.0 ? corners[i] : 0.0;
    double scale           = 1.0;
    const double sums[4]   = { radii[0] + radii[1], radii[2] + radii[3], radii[0] + radii[3], radii[1] + radii[2] };
    const double limits[4] = { rect.width, rect.width, rect.height, rect.height };
    for (size_t i = 0; i < 4; i++)
        if (sums[i] > 0.0 && limits[i] / sums[i] < scale)
            scale = limits[i] / sums[i];
    if (scale < 0.0)
        scale = 0.0;
    for (size_t i = 0; i < 4; i++)
        radii[i] *= scale;
}

static void tbox_render_push_fill_rect_corners(tbox_vector *items, tbox_rect rect, const double corners[4], tbox_css_rgba color) {
    double radii[4];
    tbox_render_normalize_corners(rect, corners, radii);

    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    *op               = (tbox_paint_op){ 0 };
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
    op->radius        = radii[0] == radii[1] && radii[0] == radii[2] && radii[0] == radii[3] ? radii[0] : 0.0;
    for (size_t i = 0; i < 4; i++)
        op->corner_radii[i] = radii[i];
    op->has_clip = false;
}

static void tbox_render_push_fill_rect_rounded(tbox_vector *items, tbox_rect rect, double radius, tbox_css_rgba color) {
    const double corners[4] = { radius, radius, radius, radius };
    tbox_render_push_fill_rect_corners(items, rect, corners, color);
}

static void tbox_render_inset_corners(const double outer[4], tbox_rect outer_rect, tbox_rect inner_rect, double inner[4]) {
    double top = inner_rect.y - outer_rect.y, left = inner_rect.x - outer_rect.x;
    double right = outer_rect.x + outer_rect.width - inner_rect.x - inner_rect.width;
    double bottom = outer_rect.y + outer_rect.height - inner_rect.y - inner_rect.height;
    const double insets[4] = {
        top > left ? top : left, top > right ? top : right,
        bottom > right ? bottom : right, bottom > left ? bottom : left,
    };
    for (size_t i = 0; i < 4; i++) inner[i] = outer[i] > insets[i] ? outer[i] - insets[i] : 0.0;
}

static void tbox_render_push_fill_ring(tbox_vector *items, tbox_rect outer, const double outer_corners[4], tbox_rect inner, const double inner_corners[4], tbox_css_rgba color) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    *op = (tbox_paint_op){ 0 };
    op->kind = TBOX_PAINT_FILL_RING;
    op->rect = outer;
    op->inner_rect = inner;
    op->color = color;
    tbox_render_normalize_corners(outer, outer_corners, op->corner_radii);
    tbox_render_normalize_corners(inner, inner_corners, op->inner_corner_radii);
}

/* Paints one border or outline side: `strip` is the whole band between the
 * side's outer and inner edges, running along x when `horizontal` (top and
 * bottom) or along y (left and right). `solid` (and `double`, which callers
 * route to tbox_render_push_double_side when it is at least 3px thick) is
 * one fill. As Firefox draws them, `dashed` splits the side into an odd
 * number of equal dashes and gaps of at most 3x the thickness, and `dotted`
 * uses round dots of 1x with gaps of at least 1x stretched; either way a
 * mark lands on each end of the side -- so corners are always covered. */
static void tbox_render_push_border_side(tbox_vector *items, tbox_rect strip, bool horizontal, tbox_style_border_style style, tbox_css_rgba color);

/* A box's dashed side between two bordered neighbours, as Firefox draws
 * it: the corner squares the strip includes (`corner_start`/`corner_end`
 * long) each continue into half a dash, and the length between them is
 * split into an even number of equal segments, about 3x the thickness. Any
 * other side falls back to tbox_render_push_border_side. */
static void tbox_render_push_box_side(tbox_vector *items, tbox_rect strip, bool horizontal, tbox_style_border_style style, tbox_css_rgba color, bool cornered, double corner_start, double corner_end) {
    double thickness = horizontal ? strip.height : strip.width;
    double length    = horizontal ? strip.width : strip.height;
    double inner     = length - corner_start - corner_end;
    if (style != TBOX_STYLE_BORDER_STYLE_DASHED || !cornered || thickness <= 0.0 || inner <= 0.0) {
        tbox_render_push_border_side(items, strip, horizontal, style, color);
        return;
    }
    /* the even count nearest to (inner + thickness) / 3x thickness, which
     * matches Firefox's spacing on both short and long sides */
    size_t segments = (size_t)floor((inner + thickness) / (3.0 * thickness) / 2.0 + 0.5) * 2;
    if (segments < 2)
        segments = 2;
    double segment = inner / (double)segments;
    /* dashes: [0, corner_start + s/2], then one per two segments, and the
     * last [length - corner_end - s/2, length] */
    for (size_t i = 0; i <= segments / 2; i++) {
        double from = i == 0 ? 0.0 : corner_start + segment / 2.0 + (double)(2 * i - 1) * segment;
        double to   = i == segments / 2 ? length : corner_start + segment / 2.0 + (double)(2 * i) * segment;
        if (i == 0)
            to = corner_start + segment / 2.0;
        tbox_rect piece = horizontal ? (tbox_rect){ strip.x + from, strip.y, to - from, strip.height } : (tbox_rect){ strip.x, strip.y + from, strip.width, to - from };
        tbox_render_push_fill_rect(items, piece, color);
    }
}

static void tbox_render_push_border_side(tbox_vector *items, tbox_rect strip, bool horizontal, tbox_style_border_style style, tbox_css_rgba color) {
    if (strip.width <= 0.0 || strip.height <= 0.0)
        return;
    double thickness = horizontal ? strip.height : strip.width;
    double length    = horizontal ? strip.width : strip.height;

    if (style == TBOX_STYLE_BORDER_STYLE_DASHED || style == TBOX_STYLE_BORDER_STYLE_DOTTED) {
        bool dotted  = style == TBOX_STYLE_BORDER_STYLE_DOTTED;
        double mark  = dotted ? thickness : 3.0 * thickness;
        size_t count = 0;
        double spacing = 0.0;
        if (dotted) {
            count = (size_t)((length + mark) / (2.0 * mark));
            if (count >= 2)
                spacing = (length - (double)count * mark) / (double)(count - 1);
        } else {
            /* As Firefox: the side split into an odd number of equal
             * segments, dash and gap alternating from a dash at each end,
             * each segment at most 3x the thickness. */
            size_t segments = (size_t)ceil(length / mark);
            if (segments % 2 == 0)
                segments++;
            count   = (segments + 1) / 2;
            mark    = length / (double)segments;
            spacing = mark;
        }
        if (count >= 2) {
            for (size_t i = 0; i < count; i++) {
                double start    = (double)i * (mark + spacing);
                tbox_rect piece = horizontal ? (tbox_rect){ strip.x + start, strip.y, mark, strip.height } : (tbox_rect){ strip.x, strip.y + start, strip.width, mark };
                if (dotted && thickness >= 2.0)
                    tbox_render_push_fill_rect_rounded(items, piece, thickness / 2.0, color);
                else
                    tbox_render_push_fill_rect(items, piece, color);
            }
            return;
        }
    }

    tbox_render_push_fill_rect(items, strip, color);
}

static const tbox_layout_box *tbox_render_find_legend(const tbox_layout_box *box, const tbox_html_node *legend) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == legend) return box;
        const tbox_layout_box *found = tbox_render_find_legend(box->first_child, legend);
        if (found != NULL) return found;
    }
    return NULL;
}

static bool tbox_render_is_3d(tbox_style_border_style style) {
    return style == TBOX_STYLE_BORDER_STYLE_GROOVE || style == TBOX_STYLE_BORDER_STYLE_RIDGE || style == TBOX_STYLE_BORDER_STYLE_INSET || style == TBOX_STYLE_BORDER_STYLE_OUTSET;
}

/* The 3D styles' two shades, as Firefox draws them: the color darkened
 * toward black by a third, and the color itself. */
static tbox_css_rgba tbox_render_shade(tbox_css_rgba color, bool dark) {
    if (dark)
        return (tbox_css_rgba){ (unsigned char)(color.r * 2 / 3), (unsigned char)(color.g * 2 / 3), (unsigned char)(color.b * 2 / 3), color.a };
    return color;
}

/* A 3D side of the ring between `outer` and the rectangle `widths` inset
 * from it, as a trapezoid whose ends are mitered toward the neighbouring
 * sides (the shades meet on the corners' diagonals). Painted as bands one
 * pixel thick across the side; groove/ridge split them in two halves. */
static void tbox_render_push_3d_mitered(tbox_vector *items, tbox_rect outer, const double widths[4], size_t side, tbox_style_border_style style, tbox_css_rgba color) {
    double thickness = widths[side];
    if (thickness <= 0.0)
        return;
    bool top_left = side == 0 || side == 3, horizontal = side == 0 || side == 2;
    bool groove   = style == TBOX_STYLE_BORDER_STYLE_GROOVE || style == TBOX_STYLE_BORDER_STYLE_RIDGE;
    bool outer_dark = groove ? top_left == (style == TBOX_STYLE_BORDER_STYLE_GROOVE) : top_left == (style == TBOX_STYLE_BORDER_STYLE_INSET);
    double start = horizontal ? widths[3] : widths[0], end = horizontal ? widths[1] : widths[2];
    double length = horizontal ? outer.width : outer.height;
    int bands     = (int)ceil(thickness);
    if (bands > 64)
        bands = 64;
    double step = thickness / bands;
    for (int b = 0; b < bands; b++) {
        /* The band's outer edge sets its miter, so neighbouring sides'
         * bands overlap on the diagonal instead of leaving gaps. */
        double depth = b * step, t = depth / thickness;
        double from = start * t, to = length - end * t;
        if (to <= from)
            continue;
        bool dark = groove && depth + step / 2.0 >= thickness / 2.0 ? !outer_dark : outer_dark;
        tbox_rect band;
        if (horizontal)
            band = (tbox_rect){ outer.x + from, side == 0 ? outer.y + depth : outer.y + outer.height - depth - step, to - from, step };
        else
            band = (tbox_rect){ side == 3 ? outer.x + depth : outer.x + outer.width - depth - step, outer.y + from, step, to - from };
        tbox_render_push_fill_rect(items, band, tbox_render_shade(color, dark));
    }
}

/* groove/ridge/inset/outset on `side` (0 top, 1 right, 2 bottom, 3
 * left): inset darkens the top and left sides and lightens the others,
 * outset the reverse; groove paints its outer half like inset and its
 * inner half like outset, ridge the reverse. */
static void tbox_render_push_3d_side(tbox_vector *items, tbox_rect strip, size_t side, tbox_style_border_style style, tbox_css_rgba color) {
    bool top_left = side == 0 || side == 3, horizontal = side == 0 || side == 2;
    if (style == TBOX_STYLE_BORDER_STYLE_INSET || style == TBOX_STYLE_BORDER_STYLE_OUTSET) {
        tbox_render_push_fill_rect(items, strip, tbox_render_shade(color, top_left == (style == TBOX_STYLE_BORDER_STYLE_INSET)));
        return;
    }
    bool outer_dark = top_left == (style == TBOX_STYLE_BORDER_STYLE_GROOVE);
    tbox_rect outer = strip, inner = strip;
    if (horizontal) {
        outer.height = inner.height = strip.height / 2.0;
        if (side == 0)
            inner.y += strip.height / 2.0;
        else
            outer.y += strip.height / 2.0;
    } else {
        outer.width = inner.width = strip.width / 2.0;
        if (side == 3)
            inner.x += strip.width / 2.0;
        else
            outer.x += strip.width / 2.0;
    }
    tbox_render_push_fill_rect(items, outer, tbox_render_shade(color, outer_dark));
    tbox_render_push_fill_rect(items, inner, tbox_render_shade(color, !outer_dark));
}

/* `double` on `side` (0 top, 1 right, 2 bottom, 3 left): two bands a third
 * of the side's width thick, one on `outer`'s edge and one on `inner`'s, so
 * that the double sides together draw two nested rectangles. `widths` and
 * `doubles` describe all four sides: an inner band reaches across a corner
 * only as far as the neighbouring side's own inner band. */
static void tbox_render_push_double_side(tbox_vector *items, size_t side, tbox_rect outer, tbox_rect inner, const double widths[4], const bool doubles[4], tbox_css_rgba color) {
    double band[4];
    for (size_t i = 0; i < 4; i++)
        band[i] = doubles[i] ? widths[i] / 3.0 : 0.0;
    double b           = widths[side] / 3.0;
    double inner_right = inner.x + inner.width, inner_bottom = inner.y + inner.height;
    tbox_rect outer_band, inner_band;
    if (side == 0 || side == 2) {
        outer_band = (tbox_rect){ outer.x, side == 0 ? outer.y : outer.y + outer.height - b, outer.width, b };
        inner_band = (tbox_rect){ inner.x - band[3], side == 0 ? inner.y - b : inner_bottom, inner.width + band[3] + band[1], b };
    } else {
        outer_band = (tbox_rect){ side == 3 ? outer.x : outer.x + outer.width - b, outer.y, b, outer.height };
        inner_band = (tbox_rect){ side == 3 ? inner.x - b : inner_right, inner.y - band[0], b, inner.height + band[0] + band[2] };
    }
    tbox_render_push_fill_rect(items, outer_band, color);
    tbox_render_push_fill_rect(items, inner_band, color);
}

/* The border box's corner radii, horizontal into `h` and vertical into `v`
 * (top-left, top-right, bottom-right, bottom-left), scaled down uniformly
 * when adjacent radii would overlap (CSS Backgrounds 3, 5.5). Horizontal
 * percentages are of the box's width, vertical ones of its height. A style
 * with no vertical radii at all -- hand-built with only
 * border_radius_corners/border_radius_percent -- gets circular corners,
 * percentages taken of the box's smaller side. Returns whether any corner
 * is elliptical (h != v). */
static bool tbox_render_style_corners(const tbox_style *style, tbox_rect border_box, double h[4], double v[4]) {
    bool has_corner = false, has_vertical = false;
    for (size_t i = 0; i < 4; i++) {
        if (style->border_radius_corners[i] > 0.0 || style->border_radius_percent[i] > 0.0)
            has_corner = true;
        if (style->border_radius_vertical[i] > 0.0 || style->border_radius_vertical_percent[i] > 0.0)
            has_vertical = true;
    }
    double base = border_box.width < border_box.height ? border_box.width : border_box.height;
    for (size_t i = 0; i < 4; i++) {
        if (!has_corner) {
            h[i] = v[i] = style->border_radius;
        } else if (!has_vertical) {
            h[i] = v[i] = style->border_radius_corners[i] + style->border_radius_percent[i] / 100.0 * base;
        } else {
            h[i] = style->border_radius_corners[i] + style->border_radius_percent[i] / 100.0 * border_box.width;
            v[i] = style->border_radius_vertical[i] + style->border_radius_vertical_percent[i] / 100.0 * border_box.height;
        }
        if (h[i] < 0.0) h[i] = 0.0;
        if (v[i] < 0.0) v[i] = 0.0;
    }
    double scale           = 1.0;
    const double sums[4]   = { h[0] + h[1], h[3] + h[2], v[0] + v[3], v[1] + v[2] };
    const double limits[4] = { border_box.width, border_box.width, border_box.height, border_box.height };
    for (size_t i = 0; i < 4; i++)
        if (sums[i] > 0.0 && limits[i] / sums[i] < scale)
            scale = limits[i] / sums[i];
    if (scale < 0.0)
        scale = 0.0;
    bool elliptical = false;
    for (size_t i = 0; i < 4; i++) {
        h[i] *= scale;
        v[i] *= scale;
        /* A corner with one zero radius is square either way. */
        if (h[i] <= 0.0 || v[i] <= 0.0)
            h[i] = v[i] = 0.0;
        if (h[i] != v[i])
            elliptical = true;
    }
    return elliptical;
}

static bool tbox_render_has_corners(const double h[4]) {
    return h[0] > 0.0 || h[1] > 0.0 || h[2] > 0.0 || h[3] > 0.0;
}

/* Corner radii of `inner_rect` nested in `outer_rect`: each radius shrinks
 * by the distance between the two edges it touches -- horizontal radii by
 * the left/right gap, vertical ones by the top/bottom gap. */
static void tbox_render_inset_shape(const double h[4], const double v[4], tbox_rect outer_rect, tbox_rect inner_rect, double inner_h[4], double inner_v[4]) {
    double top = inner_rect.y - outer_rect.y, left = inner_rect.x - outer_rect.x;
    double right  = outer_rect.x + outer_rect.width - inner_rect.x - inner_rect.width;
    double bottom = outer_rect.y + outer_rect.height - inner_rect.y - inner_rect.height;
    const double dx[4] = { left, right, right, left }, dy[4] = { top, top, bottom, bottom };
    for (size_t i = 0; i < 4; i++) {
        inner_h[i] = h[i] > dx[i] ? h[i] - dx[i] : 0.0;
        inner_v[i] = v[i] > dy[i] ? v[i] - dy[i] : 0.0;
        if (inner_h[i] <= 0.0 || inner_v[i] <= 0.0)
            inner_h[i] = inner_v[i] = 0.0;
    }
}

/* A FILL_RECT with elliptical corners (`h`/`v` already normalized). */
static void tbox_render_push_fill_rect_elliptical(tbox_vector *items, tbox_rect rect, const double h[4], const double v[4], tbox_css_rgba color) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    *op               = (tbox_paint_op){ 0 };
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->elliptical    = true;
    for (size_t i = 0; i < 4; i++) {
        op->corner_radii[i]   = h[i];
        op->corner_radii_y[i] = v[i];
    }
}

/* A rectangle with corners `h`/`v`: the circular path when they agree. */
static void tbox_render_push_shape(tbox_vector *items, tbox_rect rect, const double h[4], const double v[4], bool elliptical, tbox_css_rgba color) {
    if (elliptical)
        tbox_render_push_fill_rect_elliptical(items, rect, h, v, color);
    else
        tbox_render_push_fill_rect_corners(items, rect, h, color);
}

static void tbox_render_set_rounded_clip(tbox_paint_op *op, tbox_rect rect, const double h[4], const double v[4]) {
    op->has_rounded_clip = true;
    op->rounded_clip     = rect;
    for (size_t i = 0; i < 4; i++) {
        op->rounded_clip_radii[i]   = h[i];
        op->rounded_clip_radii_y[i] = v[i];
    }
}

/* NOVO (visual fidelity): approximates `box-shadow`'s blur with a handful
 * of concentric rounded-rect fills instead of a real Gaussian/box blur --
 * this rasterizer has no blur infrastructure, and a real one is a
 * meaningfully bigger addition than this pair of properties otherwise
 * needs (same class of simplification as v13's sub/sup fixed-fraction
 * offsets, or v11's flat-gray <hr>). Paints TBOX_RENDER_BOX_SHADOW_STEPS
 * layers, largest/faintest FIRST shrinking down to the exact
 * (offset, un-grown) shadow rect LAST: since every layer is painted with
 * the standard "over" operator, the area under the base rect accumulates
 * contributions from ALL layers (converging close to the declared
 * `color`'s own alpha), while the area only reached by the larger outer
 * layers gets progressively fainter -- a soft-looking falloff from cheap,
 * repeated flat fills, not a real convolution. `blur <= 0.0` (the common,
 * simple-shadow case) skips all of this and pushes exactly one hard-edged
 * rect, no loop overhead. `h`/`v` are the box's own border radii (a
 * shadow of a rounded box is itself rounded), grown along with each step
 * so the corners stay proportionally rounded as the shadow expands. */
#define TBOX_RENDER_BOX_SHADOW_STEPS 6

static tbox_css_rgba tbox_render_shadow_step_color(tbox_css_rgba color) {
    tbox_css_rgba step_color = color;
    step_color.a             = (unsigned char)((double)color.a / (double)TBOX_RENDER_BOX_SHADOW_STEPS + 0.5);
    if (step_color.a == 0)
        step_color.a = 1; /* never let rounding vanish a declared shadow entirely */
    return step_color;
}

static void tbox_render_push_box_shadow(tbox_vector *items, tbox_rect border_box, const double h[4], const double v[4], bool elliptical, const tbox_style_shadow *shadow) {
    /* The spread radius grows (or, negative, shrinks) the shadow shape on
     * every side before blurring; rounded corners grow with it, square
     * corners stay square (CSS Backgrounds 3's spread rule, simplified to
     * "radius + spread, never below 0"). */
    double spread = shadow->spread;
    double width = border_box.width + 2.0 * spread, height = border_box.height + 2.0 * spread;
    if (width <= 0.0 || height <= 0.0)
        return;
    tbox_rect base = { border_box.x + shadow->offset_x - spread, border_box.y + shadow->offset_y - spread, width, height };
    double base_h[4], base_v[4];
    for (size_t i = 0; i < 4; i++) {
        base_h[i] = h[i] > 0.0 && h[i] + spread > 0.0 ? h[i] + spread : 0.0;
        base_v[i] = v[i] > 0.0 && v[i] + spread > 0.0 ? v[i] + spread : 0.0;
    }

    if (shadow->blur <= 0.0) {
        tbox_render_push_shape(items, base, base_h, base_v, elliptical, shadow->color);
        return;
    }

    tbox_css_rgba step_color = tbox_render_shadow_step_color(shadow->color);
    for (int step = TBOX_RENDER_BOX_SHADOW_STEPS; step >= 1; step--) {
        double grow        = shadow->blur * (double)(step - 1) / (double)(TBOX_RENDER_BOX_SHADOW_STEPS - 1);
        tbox_rect expanded = { base.x - grow, base.y - grow, base.width + 2.0 * grow, base.height + 2.0 * grow };
        double grown_h[4], grown_v[4];
        for (size_t i = 0; i < 4; i++) {
            grown_h[i] = base_h[i] + grow;
            grown_v[i] = base_v[i] + grow;
        }
        tbox_render_push_shape(items, expanded, grown_h, grown_v, elliptical, step_color);
    }
}

/* An inset `box-shadow`: the padding box (with its rounded corners) minus
 * the shadow's hole -- the padding box shrunk by the spread and moved by
 * the offset. A blur fades the shadow toward the inside with stepped
 * rings whose holes span +-blur/2 around the base hole, same idea as the
 * outer shadow's stepped fills. */
static void tbox_render_push_inset_shadow(tbox_vector *items, tbox_rect padding_box, const double h[4], const double v[4], const tbox_style_shadow *shadow) {
    if (padding_box.width <= 0.0 || padding_box.height <= 0.0)
        return;
    double reach   = fabs(shadow->offset_x) + fabs(shadow->offset_y) + fabs(shadow->spread) + shadow->blur + 1.0;
    tbox_rect wide = { padding_box.x - reach, padding_box.y - reach, padding_box.width + 2.0 * reach, padding_box.height + 2.0 * reach };
    int steps      = shadow->blur > 0.0 ? TBOX_RENDER_BOX_SHADOW_STEPS : 1;
    tbox_css_rgba color = steps > 1 ? tbox_render_shadow_step_color(shadow->color) : shadow->color;
    for (int step = 0; step < steps; step++) {
        double shrink = shadow->spread + (steps > 1 ? shadow->blur * ((double)step / (double)(steps - 1) - 0.5) : 0.0);
        tbox_rect hole = { padding_box.x + shadow->offset_x + shrink, padding_box.y + shadow->offset_y + shrink, padding_box.width - 2.0 * shrink, padding_box.height - 2.0 * shrink };
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
        *op               = (tbox_paint_op){ 0 };
        op->kind          = TBOX_PAINT_FILL_RING;
        op->rect          = wide;
        op->color         = color;
        op->elliptical    = true;
        if (hole.width > 0.0 && hole.height > 0.0) {
            op->inner_rect = hole;
            for (size_t i = 0; i < 4; i++) {
                op->inner_corner_radii[i]   = h[i] > shrink ? h[i] - shrink : 0.0;
                op->inner_corner_radii_y[i] = v[i] > shrink ? v[i] - shrink : 0.0;
            }
        }
        tbox_render_set_rounded_clip(op, padding_box, h, v);
    }
}

/* The box's shadows as a list: box_shadows, or the single legacy fields
 * of a hand-built style. */
static size_t tbox_render_box_shadows(const tbox_style *style, const tbox_style_shadow **out, tbox_style_shadow *legacy) {
    if (style->box_shadow_count > 0) {
        *out = style->box_shadows;
        return style->box_shadow_count;
    }
    if (style->box_shadow_color.a == 0)
        return 0;
    *legacy = (tbox_style_shadow){ style->box_shadow_offset_x, style->box_shadow_offset_y, style->box_shadow_blur, style->box_shadow_spread, style->box_shadow_color, style->box_shadow_inset };
    *out    = legacy;
    return 1;
}

static size_t tbox_render_text_shadows(const tbox_style *style, const tbox_style_shadow **out, tbox_style_shadow *legacy) {
    if (style->text_shadow_count > 0) {
        *out = style->text_shadows;
        return style->text_shadow_count;
    }
    if (style->text_shadow_color.a == 0)
        return 0;
    *legacy = (tbox_style_shadow){ style->text_shadow_offset_x, style->text_shadow_offset_y, style->text_shadow_blur, 0.0, style->text_shadow_color, false };
    *out    = legacy;
    return 1;
}

/* Pre-order walk over `box` and its first_child/next_sibling chain, pushing
 * paint ops onto `items` (see tbox_render_build_display_list). A box's own
 * FILL_RECT (if its background isn't transparent) always precedes its own
 *  border FILL_RECTs (one per side with a painted border), which in turn
 * precede its own TEXT_RUN/IMAGE ops (/image support: one per
 * box->text_runs entry, in the order Layout Tree built them -- IMAGE for a
 * run whose `image` is non-NULL, TEXT_RUN otherwise), and all of that
 * precedes its children's ops -- see docs/dev/historico.md's "Render Pipeline"
 * section. */
static tbox_rect tbox_render_intersect(tbox_rect a, tbox_rect b) {
    double x0 = a.x > b.x ? a.x : b.x;
    double y0 = a.y > b.y ? a.y : b.y;
    double x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    double y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    return (tbox_rect){ x0, y0, x1 > x0 ? x1 - x0 : 0.0, y1 > y0 ? y1 - y0 : 0.0 };
}

/* Upper bound on background tiles per box, so a tiny tile over a large
 * box can't flood the display list. */
#define TBOX_RENDER_MAX_TILES 4096

/* background-image (a decoded url() image or a gradient): sized by
 * background-size against the background-origin box (padding box by
 * default), placed by background-position in the space left over, repeated
 * per background-repeat, and clipped to `clip_box` (background-clip) with
 * its rounded corners. A gradient has no intrinsic size: it fills the
 * padding box unless background-size says otherwise. */
static void tbox_render_push_background_image(tbox_vector *items, const tbox_layout_box *box, const tbox_style_background_layer *layer, const tbox_image *image, tbox_rect clip_box, const double clip_h[4], const double clip_v[4], bool rounded, tbox_rect viewport, tbox_style_background_origin origin) {
    const tbox_style *style  = box->style;
    bool gradient            = layer->gradient.kind != TBOX_STYLE_GRADIENT_NONE;
    tbox_style_background_repeat mode_x = layer->repeat_mode_x == TBOX_STYLE_BACKGROUND_REPEAT_NO_REPEAT && layer->repeat_x ? TBOX_STYLE_BACKGROUND_REPEAT_REPEAT : layer->repeat_mode_x;
    tbox_style_background_repeat mode_y = layer->repeat_mode_y == TBOX_STYLE_BACKGROUND_REPEAT_NO_REPEAT && layer->repeat_y ? TBOX_STYLE_BACKGROUND_REPEAT_REPEAT : layer->repeat_mode_y;
    tbox_rect area           = layer->attachment_fixed && viewport.width > 0.0 && viewport.height > 0.0 ? viewport : origin == TBOX_STYLE_BACKGROUND_ORIGIN_BORDER_BOX ? box->border_box : origin == TBOX_STYLE_BACKGROUND_ORIGIN_CONTENT_BOX ? box->content_box : box->padding_box;
    if ((!gradient && (image == NULL || image->width <= 0 || image->height <= 0)) || area.width <= 0.0 || area.height <= 0.0 || clip_box.width <= 0.0 || clip_box.height <= 0.0)
        return;

    double intrinsic_w = gradient ? area.width : (double)image->width * tbox_style_zoom(style);
    double intrinsic_h = gradient ? area.height : (double)image->height * tbox_style_zoom(style);
    double tile_w = intrinsic_w, tile_h = intrinsic_h;
    bool auto_w = layer->size_kind == TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT && layer->size[0].kind == TBOX_STYLE_LENGTH_AUTO;
    bool auto_h = layer->size_kind == TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT && layer->size[1].kind == TBOX_STYLE_LENGTH_AUTO;
    if (layer->size_kind != TBOX_STYLE_BACKGROUND_SIZE_EXPLICIT) {
        if (!gradient) {
            double sx = area.width / intrinsic_w, sy = area.height / intrinsic_h;
            double scale = layer->size_kind == TBOX_STYLE_BACKGROUND_SIZE_COVER ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
            tile_w = intrinsic_w * scale;
            tile_h = intrinsic_h * scale;
        }
    } else {
        tbox_style_length sw = layer->size[0], sh = layer->size[1];
        if (!auto_w)
            tile_w = tbox_style_length_resolve(sw, area.width);
        if (!auto_h)
            tile_h = tbox_style_length_resolve(sh, area.height);
        if (auto_w && !auto_h && !gradient)
            tile_w = tile_h * intrinsic_w / intrinsic_h;
        else if (auto_h && !auto_w && !gradient)
            tile_h = tile_w * intrinsic_h / intrinsic_w;
    }
    if (tile_w < 0.5 || tile_h < 0.5)
        return;

    /* round fits an integer number of tiles exactly into the positioning area. */
    if (mode_x == TBOX_STYLE_BACKGROUND_REPEAT_ROUND) {
        double old_width = tile_w;
        tile_w = area.width / fmax(1.0, floor(area.width / tile_w + 0.5));
        if (auto_h && mode_y != TBOX_STYLE_BACKGROUND_REPEAT_ROUND && !gradient)
            tile_h *= tile_w / old_width;
    }
    if (mode_y == TBOX_STYLE_BACKGROUND_REPEAT_ROUND) {
        double old_height = tile_h;
        tile_h = area.height / fmax(1.0, floor(area.height / tile_h + 0.5));
        if (auto_w && mode_x != TBOX_STYLE_BACKGROUND_REPEAT_ROUND && !gradient)
            tile_w *= tile_h / old_height;
    }
    if (tile_w < 0.5 || tile_h < 0.5)
        return;

    double x = area.x + tbox_style_length_resolve(layer->position[0], area.width - tile_w);
    double y = area.y + tbox_style_length_resolve(layer->position[1], area.height - tile_h);
    double x_start = x, y_start = y, x_end = x + tile_w, y_end = y + tile_h;
    double x_step = tile_w, y_step = tile_h;
    if (mode_x == TBOX_STYLE_BACKGROUND_REPEAT_ROUND) {
        x_start = area.x;
        x_end = area.x + area.width;
    } else if (mode_x == TBOX_STYLE_BACKGROUND_REPEAT_SPACE && floor(area.width / tile_w) >= 2.0) {
        double count = floor(area.width / tile_w);
        x_start = area.x;
        x_step = tile_w + (area.width - count * tile_w) / (count - 1.0);
        x_end = area.x + area.width;
    } else if (mode_x == TBOX_STYLE_BACKGROUND_REPEAT_REPEAT) {
        x_start = x - ceil((x - clip_box.x) / tile_w) * tile_w;
        x_end   = clip_box.x + clip_box.width;
    }
    if (mode_y == TBOX_STYLE_BACKGROUND_REPEAT_ROUND) {
        y_start = area.y;
        y_end = area.y + area.height;
    } else if (mode_y == TBOX_STYLE_BACKGROUND_REPEAT_SPACE && floor(area.height / tile_h) >= 2.0) {
        double count = floor(area.height / tile_h);
        y_start = area.y;
        y_step = tile_h + (area.height - count * tile_h) / (count - 1.0);
        y_end = area.y + area.height;
    } else if (mode_y == TBOX_STYLE_BACKGROUND_REPEAT_REPEAT) {
        y_start = y - ceil((y - clip_box.y) / tile_h) * tile_h;
        y_end   = clip_box.y + clip_box.height;
    }
    size_t tiles = 0;
    for (double ty = y_start; ty < y_end - 1e-6 && tiles < TBOX_RENDER_MAX_TILES; ty += y_step) {
        for (double tx = x_start; tx < x_end - 1e-6 && tiles < TBOX_RENDER_MAX_TILES; tx += x_step) {
            tbox_rect tile = { tx, ty, tile_w, tile_h };
            if (tbox_render_intersect(tile, clip_box).width <= 0.0 || tbox_render_intersect(tile, clip_box).height <= 0.0)
                continue;
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            *op               = (tbox_paint_op){ 0 };
            op->kind          = gradient ? TBOX_PAINT_GRADIENT : TBOX_PAINT_IMAGE;
            op->rect          = tile;
            op->color         = (tbox_css_rgba){ 0, 0, 0, 255 }; /* alpha: opacity */
            op->image         = gradient ? NULL : image;
            op->gradient      = gradient ? &layer->gradient : NULL;
            op->image_pixelated = style->image_rendering_pixelated;
            op->blend_mode    = layer->blend_mode;
            op->has_clip      = true;
            op->clip          = clip_box;
            if (rounded)
                tbox_render_set_rounded_clip(op, clip_box, clip_h, clip_v);
            tiles++;
        }
    }
}

/* ---- border-image (Backgrounds 3 section 6) ---- */

/* Tiles along one axis of a border-image part (section 6.5's
 * border-image-repeat): `*size` is the scaled tile on entry and the
 * final one on return; tiles start at `*start`, `*step` apart. Returns
 * how many there are. */
static size_t tbox_render_border_image_axis(tbox_style_border_image_repeat mode, double region_start, double region_size, double *size, double *start, double *step) {
    if (region_size <= 0.0)
        return 0;
    if (mode == TBOX_STYLE_BORDER_IMAGE_STRETCH || *size <= 0.0 || !isfinite(*size)) {
        *size = region_size, *start = region_start, *step = region_size;
        return 1;
    }
    double tile = *size;
    if (tile < 0.5)
        tile = 0.5; /* a hair-thin tile would flood the list anyway */
    size_t count;
    if (mode == TBOX_STYLE_BORDER_IMAGE_ROUND) {
        double n = fmax(1.0, floor(region_size / tile + 0.5));
        tile = region_size / n, *start = region_start, *step = tile;
        count = (size_t)n;
    } else if (mode == TBOX_STYLE_BORDER_IMAGE_SPACE) {
        double n = floor(region_size / tile);
        if (n < 1.0)
            return 0;
        double gap = (region_size - n * tile) / (n + 1.0);
        *start = region_start + gap, *step = tile + gap;
        count = (size_t)n;
    } else { /* repeat: centered in the region */
        double first  = region_start + (region_size - tile) / 2.0;
        *start        = first - ceil((first - region_start) / tile) * tile;
        *step         = tile;
        count         = (size_t)ceil((region_start + region_size - *start) / tile - 1e-9);
    }
    *size = tile;
    return count;
}

/* One part (corner, edge or middle) of a border image: the `source`
 * rectangle of the image (image_w x image_h, the border image area for a
 * gradient) tiled over `region` at `tile_w` x `tile_h` per mode. */
static void tbox_render_border_image_part(tbox_vector *items, const tbox_style *style, const tbox_image *image, double image_w, double image_h, tbox_rect source, tbox_rect region, double tile_w, double tile_h, tbox_style_border_image_repeat mode_x, tbox_style_border_image_repeat mode_y) {
    if (source.width <= 0.0 || source.height <= 0.0 || region.width <= 0.0 || region.height <= 0.0)
        return;
    double x_start = 0.0, x_step = 0.0, y_start = 0.0, y_step = 0.0;
    size_t nx = tbox_render_border_image_axis(mode_x, region.x, region.width, &tile_w, &x_start, &x_step);
    size_t ny = tbox_render_border_image_axis(mode_y, region.y, region.height, &tile_h, &y_start, &y_step);
    if (nx * ny > TBOX_RENDER_MAX_TILES)
        return;
    for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
            tbox_rect tile = { x_start + (double)i * x_step, y_start + (double)j * y_step, tile_w, tile_h };
            if (image != NULL) {
                /* whole pixels, so neighboring tiles neither gap nor overlap */
                double x0 = floor(tile.x + 0.5), y0 = floor(tile.y + 0.5);
                tile = (tbox_rect){ x0, y0, floor(tile.x + tile.width + 0.5) - x0, floor(tile.y + tile.height + 0.5) - y0 };
            }
            tbox_rect clip = tbox_render_intersect(tile, region);
            if (clip.width <= 0.0 || clip.height <= 0.0)
                continue;
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            if (op == NULL)
                return;
            *op                 = (tbox_paint_op){ 0 };
            op->color           = (tbox_css_rgba){ 0, 0, 0, 255 }; /* alpha: opacity */
            op->image_pixelated = style->image_rendering_pixelated;
            op->has_clip        = true;
            op->clip            = clip;
            if (image != NULL) {
                op->kind            = TBOX_PAINT_IMAGE;
                op->image           = image;
                op->rect            = tile;
                op->has_source_rect = true;
                op->source_rect     = source;
            } else {
                /* the whole gradient, scaled so `source` lands on the tile */
                double sx    = tile.width / source.width, sy = tile.height / source.height;
                op->kind     = TBOX_PAINT_GRADIENT;
                op->gradient = &tbox_style_border_image_of(style)->gradient;
                op->rect     = (tbox_rect){ tile.x - source.x * sx, tile.y - source.y * sy, image_w * sx, image_h * sy };
                op->has_source_rect = true;
                op->source_rect     = (tbox_rect){ 0.0, 0.0, image_w, image_h };
            }
        }
    }
}

/* `border-image`, painted instead of the border styles: the source cut
 * into nine slices by border-image-slice, drawn into the border image
 * area (the border box grown by border-image-outset) at
 * border-image-width -- corners stretched, edges and the `fill` middle
 * scaled and tiled per border-image-repeat. `border` holds the used
 * border widths. Returns false (nothing pushed) when the source can't
 * paint, so the ordinary border is used instead. */
static bool tbox_render_push_border_image(tbox_vector *items, const tbox_layout_box *box, const double border[4]) {
    const tbox_style *style            = box->style;
    const tbox_style_border_image *bi  = tbox_style_border_image_of(style);
    bool gradient                      = bi->gradient.kind != TBOX_STYLE_GRADIENT_NONE;
    const tbox_image *image            = gradient ? NULL : box->border_image;
    if (!gradient && (image == NULL || image->width <= 0 || image->height <= 0))
        return false;

    double outset[4];
    for (size_t i = 0; i < 4; i++)
        outset[i] = bi->outset[i].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER ? bi->outset[i].number * border[i] : tbox_style_length_resolve(bi->outset[i].length, 0.0);
    tbox_rect area = { box->border_box.x - outset[3], box->border_box.y - outset[0], box->border_box.width + outset[1] + outset[3], box->border_box.height + outset[0] + outset[2] };
    if (area.width <= 0.0 || area.height <= 0.0)
        return true;
    double image_w = gradient ? area.width : (double)image->width;
    double image_h = gradient ? area.height : (double)image->height;

    /* top right bottom left; even sides measure vertically */
    double slice[4], width[4];
    for (size_t i = 0; i < 4; i++) {
        double extent = i % 2 == 0 ? image_h : image_w;
        double s      = bi->slice[i].kind == TBOX_STYLE_BORDER_IMAGE_NUMBER ? bi->slice[i].number : tbox_style_length_resolve(bi->slice[i].length, extent);
        slice[i]      = fmin(fmax(s, 0.0), extent);
        const tbox_style_border_image_value *w = &bi->width[i];
        if (w->kind == TBOX_STYLE_BORDER_IMAGE_NUMBER)
            width[i] = w->number * border[i];
        else if (w->kind == TBOX_STYLE_BORDER_IMAGE_LENGTH)
            width[i] = tbox_style_length_resolve(w->length, i % 2 == 0 ? area.height : area.width);
        else
            width[i] = gradient ? border[i] : slice[i]; /* auto: no intrinsic size falls back to border-width */
        if (width[i] < 0.0)
            width[i] = 0.0;
    }
    /* Opposite widths that overlap scale down together (section 6.3). */
    double f = 1.0;
    if (width[1] + width[3] > area.width)
        f = fmin(f, area.width / (width[1] + width[3]));
    if (width[0] + width[2] > area.height)
        f = fmin(f, area.height / (width[0] + width[2]));
    for (size_t i = 0; i < 4; i++)
        width[i] *= f;

    /* Column/row edges of the nine slices in the image and on screen. */
    const double sx[4] = { 0.0, slice[3], image_w - slice[1], image_w };
    const double sy[4] = { 0.0, slice[0], image_h - slice[2], image_h };
    const double dx[4] = { area.x, area.x + width[3], area.x + area.width - width[1], area.x + area.width };
    const double dy[4] = { area.y, area.y + width[0], area.y + area.height - width[2], area.y + area.height };
    tbox_rect source[3][3], region[3][3];
    for (size_t r = 0; r < 3; r++)
        for (size_t c = 0; c < 3; c++) {
            source[r][c] = (tbox_rect){ sx[c], sy[r], sx[c + 1] - sx[c], sy[r + 1] - sy[r] };
            region[r][c] = (tbox_rect){ dx[c], dy[r], dx[c + 1] - dx[c], dy[r + 1] - dy[r] };
        }
    const tbox_style_border_image_repeat stretch = TBOX_STYLE_BORDER_IMAGE_STRETCH;

    /* Edge scale factors: each edge slice is scaled to its border width. */
    double scale[4];
    for (size_t i = 0; i < 4; i++)
        scale[i] = slice[i] > 0.0 ? width[i] / slice[i] : 0.0;

    if (bi->fill) {
        double fx = scale[0] > 0.0 ? scale[0] : scale[2] > 0.0 ? scale[2] : 1.0;
        double fy = scale[3] > 0.0 ? scale[3] : scale[1] > 0.0 ? scale[1] : 1.0;
        tbox_render_border_image_part(items, style, image, image_w, image_h, source[1][1], region[1][1], source[1][1].width * fx, source[1][1].height * fy, bi->repeat[0], bi->repeat[1]);
    }
    for (size_t r = 0; r < 3; r += 2)
        for (size_t c = 0; c < 3; c += 2)
            tbox_render_border_image_part(items, style, image, image_w, image_h, source[r][c], region[r][c], region[r][c].width, region[r][c].height, stretch, stretch);
    tbox_render_border_image_part(items, style, image, image_w, image_h, source[0][1], region[0][1], source[0][1].width * scale[0], region[0][1].height, bi->repeat[0], stretch);
    tbox_render_border_image_part(items, style, image, image_w, image_h, source[2][1], region[2][1], source[2][1].width * scale[2], region[2][1].height, bi->repeat[0], stretch);
    tbox_render_border_image_part(items, style, image, image_w, image_h, source[1][0], region[1][0], region[1][0].width, source[1][0].height * scale[3], stretch, bi->repeat[1]);
    tbox_render_border_image_part(items, style, image, image_w, image_h, source[1][2], region[1][2], region[1][2].width, source[1][2].height * scale[1], stretch, bi->repeat[1]);
    return true;
}

static bool tbox_render_is_grid_item(const tbox_layout_box *box) {
    const tbox_style *parent = box->parent != NULL ? box->parent->style : NULL;
    return parent != NULL && (parent->display == TBOX_STYLE_DISPLAY_GRID || parent->display == TBOX_STYLE_DISPLAY_INLINE_GRID);
}

/* A box with an explicit z-index on a positioned box: painted out of tree
 * order, by its nearest stacking context (see tbox_render_box). */
static bool tbox_render_is_z_box(const tbox_layout_box *box) {
    return box->style != NULL && (box->style->position != TBOX_STYLE_POSITION_STATIC || tbox_render_is_grid_item(box)) && !box->style->z_index_auto;
}

/* A stacking context root: a z-index box, or one on a compositing layer
 * (opacity, filter, mask, mix-blend-mode, isolation -- the layer holds the
 * whole subtree, z-indexed descendants included), with a color filter,
 * backdrop-filter or a transform (a translation too), as in CSS. */
static bool tbox_render_is_context(const tbox_layout_box *box) {
    return tbox_render_is_z_box(box) || box->has_transform || (box->style != NULL && (tbox_style_needs_layer(box->style) || box->style->has_filter || box->style->has_backdrop_filter || tbox_style_has_transform(box->style)));
}

/* A non-positioned float: painted by its stacking context after the
 * in-flow blocks (and their inline content: lines avoid floats anyway)
 * and before any positioned box, as CSS 2.1 Appendix E's step 5. */
static bool tbox_render_is_float(const tbox_layout_box *box) {
    return box->node != NULL && box->style != NULL && box->style->float_side != TBOX_STYLE_FLOAT_NONE && box->style->position == TBOX_STYLE_POSITION_STATIC;
}

/* A positioned box: painted by its stacking context after the in-flow
 * content, in z-index order (`auto` and 0 in tree order). Floats join
 * the same list, ahead of every positioned box. */
static bool tbox_render_is_layer(const tbox_layout_box *box) {
    return box->style != NULL && (box->style->position != TBOX_STYLE_POSITION_STATIC || tbox_render_is_float(box) || (tbox_render_is_grid_item(box) && !box->style->z_index_auto));
}

static int tbox_render_layer_z(const tbox_layout_box *box) {
    return box->style->z_index_auto || tbox_render_is_float(box) ? 0 : box->style->z_index;
}

/* Paint order among the non-negative layers: floats first. */
static double tbox_render_layer_order(const tbox_layout_box *box) {
    return tbox_render_is_float(box) ? -0.5 : (double)tbox_render_layer_z(box);
}

static bool tbox_render_clips(const tbox_layout_box *box) {
    return box->style != NULL && (box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE || box->style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE);
}

static tbox_rect tbox_render_overflow_clip(const tbox_layout_box *box) {
    tbox_rect clip = box->padding_box;
    clip.width = clip.width > box->scrollbar_gutter ? clip.width - box->scrollbar_gutter : 0.0;
    return clip;
}

/* A z-indexed box waiting for its stacking context, with the clip its
 * overflow ancestors put on it. */
typedef struct tbox_render_layer {
    const tbox_layout_box *box;
    bool has_clip;
    tbox_rect clip;
} tbox_render_layer;

/* Collects the positioned boxes of one stacking context: `first`'s chain
 * and descendants, stopping at nested contexts (collected themselves when
 * positioned). A positioned box without z-index is no context: its own
 * positioned descendants belong to this one too. */
static void tbox_render_collect_layers(const tbox_layout_box *first, bool has_clip, tbox_rect clip, tbox_vector *layers) {
    for (const tbox_layout_box *box = first; box != NULL; box = box->next_sibling) {
        if (tbox_render_is_layer(box))
            *(tbox_render_layer *)tbox_vector_push(layers) = (tbox_render_layer){ box, has_clip, clip };
        if (tbox_render_is_context(box))
            continue;
        bool child_has_clip  = has_clip;
        tbox_rect child_clip = clip;
        if (tbox_render_clips(box)) {
            tbox_rect scrollport = tbox_render_overflow_clip(box);
            child_clip     = child_has_clip ? tbox_render_intersect(child_clip, scrollport) : scrollport;
            child_has_clip = true;
        }
        tbox_render_collect_layers(box->first_child, child_has_clip, child_clip, layers);
    }
}

static void tbox_render_box(const tbox_layout_box *box, tbox_vector *items, bool has_clip, tbox_rect clip, tbox_arena *arena, tbox_rect viewport);

/* The next layer to paint among `layers` whose z-index is negative
 * (`negative`) or not: the lowest z-index not `done`, tree order among
 * equals (small lists, so repeated selection). `count` when none is left. */
static size_t tbox_render_next_layer(const tbox_vector *layers, const bool *done, bool negative) {
    size_t count = layers->length, best = count;
    for (size_t i = 0; i < count; i++) {
        const tbox_render_layer *layer = (const tbox_render_layer *)tbox_vector_at_const(layers, i);
        if (done[i] || (tbox_render_layer_z(layer->box) < 0) != negative)
            continue;
        if (best == count || tbox_render_layer_order(layer->box) < tbox_render_layer_order(((const tbox_render_layer *)tbox_vector_at_const(layers, best))->box))
            best = i;
    }
    return best;
}

/* Paints `layers` whose z-index is negative (`negative`) or not, in
 * increasing z-index order, tree order among equals. */
static void tbox_render_paint_layers(const tbox_vector *layers, bool negative, tbox_vector *items, tbox_arena *arena, tbox_rect viewport) {
    bool *done = layers->length > 0 ? (bool *)tbox_arena_alloc_zero(arena, layers->length) : NULL;
    for (size_t best; (best = tbox_render_next_layer(layers, done, negative)) < layers->length;) {
        done[best]                     = true;
        const tbox_render_layer *layer = (const tbox_render_layer *)tbox_vector_at_const(layers, best);
        tbox_render_box(layer->box, items, layer->has_clip, layer->clip, arena, viewport);
    }
}

/* tbox_render_box's traversal without painting: each box once, at the
 * point where its own background would be painted. */
static void tbox_render_order_box(const tbox_layout_box *box, bool has_clip, tbox_rect clip, tbox_arena *arena, tbox_vector *out) {
    *(tbox_render_painted_box *)tbox_vector_push(out) = (tbox_render_painted_box){ box, has_clip, clip };
    if (box->has_transform)
        has_clip = false; /* clips outside are in other coordinates, see tbox_render_box */
    bool context         = tbox_render_is_context(box) || box->parent == NULL;
    bool child_has_clip  = has_clip;
    tbox_rect child_clip = clip;
    if (tbox_render_clips(box)) {
        tbox_rect scrollport = tbox_render_overflow_clip(box);
        child_clip     = child_has_clip ? tbox_render_intersect(child_clip, scrollport) : scrollport;
        child_has_clip = true;
    }
    tbox_vector layers;
    tbox_vector_init(&layers, arena, sizeof(tbox_render_layer), 0);
    bool *done = NULL;
    if (context) {
        tbox_render_collect_layers(box->first_child, child_has_clip, child_clip, &layers);
        done = layers.length > 0 ? (bool *)tbox_arena_alloc_zero(arena, layers.length) : NULL;
        for (size_t best; (best = tbox_render_next_layer(&layers, done, true)) < layers.length;) {
            done[best]                     = true;
            const tbox_render_layer *layer = (const tbox_render_layer *)tbox_vector_at_const(&layers, best);
            tbox_render_order_box(layer->box, layer->has_clip, layer->clip, arena, out);
        }
    }
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        if (!tbox_render_is_layer(child))
            tbox_render_order_box(child, child_has_clip, child_clip, arena, out);
    if (context) {
        for (size_t best; (best = tbox_render_next_layer(&layers, done, false)) < layers.length;) {
            done[best]                     = true;
            const tbox_render_layer *layer = (const tbox_render_layer *)tbox_vector_at_const(&layers, best);
            tbox_render_order_box(layer->box, layer->has_clip, layer->clip, arena, out);
        }
    }
}

tbox_render_painted_box *tbox_render_paint_order(const tbox_layout_box *root, size_t *out_count) {
    *out_count = 0;
    if (root == NULL)
        return NULL;
    tbox_arena arena = tbox_arena_create(0);
    tbox_vector order;
    tbox_vector_init(&order, &arena, sizeof(tbox_render_painted_box), 0);
    for (const tbox_layout_box *box = root; box != NULL; box = box->next_sibling)
        tbox_render_order_box(box, false, (tbox_rect){ 0 }, &arena, &order);
    tbox_render_painted_box *result = order.length > 0 ? (tbox_render_painted_box *)malloc(order.length * sizeof(tbox_render_painted_box)) : NULL;
    if (result != NULL) {
        memcpy(result, order.data, order.length * sizeof(tbox_render_painted_box));
        *out_count = order.length;
    }
    tbox_arena_destroy(&arena);
    return result;
}

/* `filter`: solid colors are transformed now; images and gradients carry
 * the matrix (composed with an inner element's) for the rasterizer. */
static tbox_css_rgba tbox_render_filter_color(const double m[20], tbox_css_rgba c) {
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

/* dashed/dotted borders on a rounded box: round stamps along the border's
 * center line (straight sides plus elliptical corners, `h`/`v` being the
 * outer radii) -- dots one width apart for dotted, runs of stamps three
 * widths long with one-width gaps for dashed, both stretched so the
 * pattern closes evenly around the box. */
static void tbox_render_push_rounded_dashes(tbox_vector *items, tbox_rect border_box, const double h[4], const double v[4], double width, bool dotted, tbox_css_rgba color) {
    double half = width / 2.0;
    tbox_rect line = { border_box.x + half, border_box.y + half, border_box.width - width, border_box.height - width };
    if (line.width <= 0.0 || line.height <= 0.0)
        return;
    /* The center line as a closed polyline, clockwise from the top-left
     * corner's end. */
    enum { STEPS = 24 };
    double points[4 * (STEPS + 1)][2];
    size_t count = 0;
    static const double start_angle[4] = { 180.0, 270.0, 0.0, 90.0 };
    for (int corner = 0; corner < 4; corner++) {
        double rx = h[corner] - half > 0.0 ? h[corner] - half : 0.0, ry = v[corner] - half > 0.0 ? v[corner] - half : 0.0;
        double cx = corner == 0 || corner == 3 ? line.x + rx : line.x + line.width - rx;
        double cy = corner == 0 || corner == 1 ? line.y + ry : line.y + line.height - ry;
        for (int i = 0; i <= STEPS; i++) {
            double angle      = (start_angle[corner] + 90.0 * i / STEPS) * 3.141592653589793 / 180.0;
            points[count][0]  = cx + rx * cos(angle);
            points[count][1]  = cy + ry * sin(angle);
            count++;
        }
    }
    double perimeter = 0.0;
    for (size_t i = 0; i < count; i++) {
        size_t j = (i + 1) % count;
        perimeter += hypot(points[j][0] - points[i][0], points[j][1] - points[i][1]);
    }
    double mark = dotted ? 0.0 : 3.0 * width, period = mark + (dotted ? width * 2.0 : 3.0 * width);
    size_t marks = (size_t)(perimeter / period);
    if (marks < 1)
        marks = 1;
    period       = perimeter / (double)marks; /* stretched to close evenly */
    double step  = width / 2.0 > 1.0 ? width / 2.0 : 1.0;
    size_t segment = 0;
    double segment_start = 0.0;
    for (double d = 0.0; d < perimeter && items->length < 1u << 20; d += dotted ? period : step) {
        double phase = fmod(d, period);
        if (!dotted && phase > mark)
            continue;
        /* Locate distance `d` on the polyline. */
        while (segment < count) {
            size_t j    = (segment + 1) % count;
            double size = hypot(points[j][0] - points[segment][0], points[j][1] - points[segment][1]);
            if (segment_start + size >= d) {
                double t = size > 0.0 ? (d - segment_start) / size : 0.0;
                double x = points[segment][0] + (points[j][0] - points[segment][0]) * t;
                double y = points[segment][1] + (points[j][1] - points[segment][1]) * t;
                /* Dots are round; dashes, square-ended, are stamped
                 * with squares so a thin one stays solid. */
                tbox_render_push_fill_rect_rounded(items, (tbox_rect){ x - half, y - half, width, width }, dotted ? half : 0.0, color);
                break;
            }
            segment_start += size;
            segment++;
        }
    }
}

static void tbox_render_apply_filter(tbox_vector *items, size_t start, const double m[20], tbox_arena *arena) {
    bool in_mask = false; /* a mask is never filtered */
    for (size_t i = start; i < items->length; i++) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
        if (op->kind == TBOX_PAINT_LAYER_MASK) in_mask = true;
        else if (op->kind == TBOX_PAINT_LAYER_END) in_mask = false;
        if (in_mask || op->kind == TBOX_PAINT_LAYER_BEGIN || op->kind == TBOX_PAINT_LAYER_MASK || op->kind == TBOX_PAINT_LAYER_END || op->kind == TBOX_PAINT_BACKDROP_FILTER)
            continue;
        if (op->kind == TBOX_PAINT_IMAGE || op->kind == TBOX_PAINT_GRADIENT) {
            if (op->color_filter == NULL) {
                op->color_filter = m;
            } else {
                /* outer * inner: the inner filter applies first. */
                double *composed = (double *)tbox_arena_alloc(arena, 20 * sizeof(double));
                if (composed == NULL)
                    continue;
                for (int row = 0; row < 4; row++)
                    for (int col = 0; col < 5; col++) {
                        double sum = col == 4 ? m[row * 5 + 4] : 0.0;
                        for (int k = 0; k < 4; k++)
                            sum += m[row * 5 + k] * op->color_filter[k * 5 + col];
                        composed[row * 5 + col] = sum;
                    }
                op->color_filter = composed;
            }
        } else {
            op->color = tbox_render_filter_color(m, op->color);
            if (op->kind == TBOX_PAINT_TEXT_RUN)
                op->text_stroke_color = tbox_render_filter_color(m, op->text_stroke_color);
        }
    }
}

const tbox_paint_polygon *tbox_render_resolve_polygon(const tbox_layout_box *box, tbox_arena *arena) {
    const tbox_style_clip_path *clip = tbox_style_clip_path_of(box->style);
    tbox_paint_polygon *polygon = tbox_arena_alloc_zero(arena, sizeof(*polygon));
    if (polygon == NULL) return NULL;
    polygon->count = clip->point_count <= TBOX_STYLE_MAX_POLYGON_POINTS ? clip->point_count : 0;
    polygon->evenodd = clip->evenodd;
    for (size_t i = 0; i < polygon->count; i++) {
        polygon->points[i][0] = tbox_style_length_resolve(clip->points[i][0], box->border_box.width);
        polygon->points[i][1] = tbox_style_length_resolve(clip->points[i][1], box->border_box.height);
    }
    return polygon;
}

/* `clip-path`: the shape as a rounded clip on every op of the subtree.
 * An op that already has a rounded clip (a rounded background) keeps it
 * and is only clipped to the shape's bounding box. */
static void tbox_render_apply_clip_path(tbox_vector *items, size_t start, size_t end, const tbox_layout_box *box, tbox_arena *arena) {
    const tbox_style_clip_path *clip = tbox_style_clip_path_of(box->style);
    tbox_rect area = box->border_box, shape;
    if (clip->kind == TBOX_STYLE_CLIP_PATH_POLYGON) {
        const tbox_paint_polygon *polygon = tbox_render_resolve_polygon(box, arena);
        if (polygon == NULL) return;
        for (size_t i = start; i < end; i++) {
            tbox_paint_op *op = tbox_vector_at(items, i);
            op->polygon_clip = polygon;
            op->polygon_clip_origin[0] = area.x;
            op->polygon_clip_origin[1] = area.y;
        }
        return;
    }
    double h[4] = { 0.0, 0.0, 0.0, 0.0 }, v[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (clip->kind == TBOX_STYLE_CLIP_PATH_INSET) {
        double top = tbox_style_length_resolve(clip->inset[0], area.height), right = tbox_style_length_resolve(clip->inset[1], area.width);
        double bottom = tbox_style_length_resolve(clip->inset[2], area.height), left = tbox_style_length_resolve(clip->inset[3], area.width);
        shape = (tbox_rect){ area.x + left, area.y + top, area.width - left - right, area.height - top - bottom };
        for (size_t i = 0; i < 4; i++)
            h[i] = fmax(0.0, clip->round_h[i] + area.width * clip->round_h_percent[i] / 100.0),
            v[i] = fmax(0.0, clip->round_v[i] + area.height * clip->round_v_percent[i] / 100.0);
        /* Elliptical corners share one scale, using each axis separately. */
        const double sums[4] = { h[0] + h[1], h[2] + h[3], v[0] + v[3], v[1] + v[2] };
        const double limits[4] = { fmax(0.0, shape.width), fmax(0.0, shape.width), fmax(0.0, shape.height), fmax(0.0, shape.height) };
        double scale = 1.0;
        for (size_t i = 0; i < 4; i++)
            if (sums[i] > 0.0) scale = fmin(scale, limits[i] / sums[i]);
        for (size_t i = 0; i < 4; i++)
            h[i] *= scale, v[i] *= scale;
    } else {
        double cx = area.x + tbox_style_length_resolve(clip->center[0], area.width), cy = area.y + tbox_style_length_resolve(clip->center[1], area.height);
        double near_x = fmin(cx - area.x, area.x + area.width - cx), far_x = fmax(cx - area.x, area.x + area.width - cx);
        double near_y = fmin(cy - area.y, area.y + area.height - cy), far_y = fmax(cy - area.y, area.y + area.height - cy);
        double rx, ry;
        if (clip->kind == TBOX_STYLE_CLIP_PATH_CIRCLE) {
            tbox_style_length r = clip->radius[0];
            double reference    = sqrt((area.width * area.width + area.height * area.height) / 2.0);
            rx = ry = r.kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(r, reference) : r.value < 0.0 ? fmax(far_x, far_y) : fmin(near_x, near_y);
        } else {
            rx = clip->radius[0].kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(clip->radius[0], area.width) : clip->radius[0].value < 0.0 ? far_x : near_x;
            ry = clip->radius[1].kind != TBOX_STYLE_LENGTH_AUTO ? tbox_style_length_resolve(clip->radius[1], area.height) : clip->radius[1].value < 0.0 ? far_y : near_y;
        }
        shape = (tbox_rect){ cx - rx, cy - ry, 2.0 * rx, 2.0 * ry };
        for (size_t i = 0; i < 4; i++)
            h[i] = rx, v[i] = ry;
    }
    if (shape.width < 0.0) shape.width = 0.0;
    if (shape.height < 0.0) shape.height = 0.0;
    for (size_t i = start; i < end; i++) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
        if (!op->has_rounded_clip) {
            tbox_render_set_rounded_clip(op, shape, h, v);
        } else {
            op->clip     = op->has_clip ? tbox_render_intersect(op->clip, shape) : shape;
            op->has_clip = true;
        }
    }
}

/* ---- text-emphasis ---- */

/* The next UTF-8 codepoint of `text` at `*at`. */
static uint32_t tbox_render_next_codepoint(tbox_string_view text, size_t *at) {
    uint32_t c;
    *at = tbox_utf8_decode(text.data, text.size, *at, &c);
    return c;
}

/* No emphasis mark: separators, controls and punctuation (CSS Text
 * Decoration 3 §3.1), approximated by the ASCII, general and CJK
 * punctuation blocks. */
static bool tbox_render_emphasis_skips(uint32_t c) {
    if (c <= 0x20 || c == 0x7F || c == 0xA0 || c == 0xAD || c == 0x3000) return true;
    if (c < 0x80) return !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    return (c >= 0x2000 && c <= 0x206F) || (c >= 0x3001 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF0F) ||
           (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65) ||
           c == 0xA1 || c == 0xA7 || c == 0xAB || c == 0xB6 || c == 0xB7 || c == 0xBB || c == 0xBF;
}

static void tbox_render_push_ellipse(tbox_vector *items, tbox_rect rect, tbox_css_rgba color, bool ring, double thickness) {
    double h[4] = { rect.width / 2.0, rect.width / 2.0, rect.width / 2.0, rect.width / 2.0 };
    double v[4] = { rect.height / 2.0, rect.height / 2.0, rect.height / 2.0, rect.height / 2.0 };
    bool elliptical = rect.width != rect.height;
    if (!ring) {
        tbox_render_push_shape(items, rect, h, v, elliptical, color);
        return;
    }
    tbox_rect inner = { rect.x + thickness, rect.y + thickness, rect.width - 2.0 * thickness, rect.height - 2.0 * thickness };
    if (inner.width <= 0.0 || inner.height <= 0.0) {
        tbox_render_push_shape(items, rect, h, v, elliptical, color);
        return;
    }
    double ih[4] = { inner.width / 2.0, inner.width / 2.0, inner.width / 2.0, inner.width / 2.0 };
    const double iv[4] = { inner.height / 2.0, inner.height / 2.0, inner.height / 2.0, inner.height / 2.0 };
    tbox_render_push_fill_ring(items, rect, h, inner, ih, color);
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, items->length - 1);
    op->elliptical = true;
    for (size_t i = 0; i < 4; i++) {
        op->corner_radii[i] = h[i], op->corner_radii_y[i] = v[i];
        op->inner_corner_radii[i] = ih[i], op->inner_corner_radii_y[i] = iv[i];
    }
}

/* A `width` x `height` rect centered on (cx, cy), snapped to whole pixels:
 * the raster tests coverage at pixel centers, so a mark only a few pixels
 * wide comes out lopsided at a fractional position. */
static tbox_rect tbox_render_mark_rect(double cx, double cy, double width, double height) {
    width = fmax(1.0, round(width)), height = fmax(1.0, round(height));
    return (tbox_rect){ floor(cx - width / 2.0 + 0.5), floor(cy - height / 2.0 + 0.5), width, height };
}

/* One mark centered at (cx, cy), `size` the em of the text. */
static void tbox_render_push_emphasis_mark(tbox_vector *items, const tbox_layout_text_run *run, double cx, double cy, double size, tbox_css_rgba color) {
    const tbox_style *style = run->style;
    bool open = style->text_emphasis_open;
    double thickness = fmax(1.0, round(size * 0.05));
    switch (style->text_emphasis_mark) {
    case TBOX_STYLE_EMPHASIS_DOT: {
        double d = fmax(open ? 5.0 : 3.0, size * (open ? 0.3 : 0.24));
        tbox_render_push_ellipse(items, tbox_render_mark_rect(cx, cy, d, d), color, open, thickness);
        break;
    }
    case TBOX_STYLE_EMPHASIS_CIRCLE: {
        double d = fmax(5.0, size * 0.36);
        tbox_render_push_ellipse(items, tbox_render_mark_rect(cx, cy, d, d), color, open, thickness);
        break;
    }
    case TBOX_STYLE_EMPHASIS_DOUBLE_CIRCLE: {
        double d = fmax(7.0, size * 0.42);
        tbox_rect outer = tbox_render_mark_rect(cx, cy, d, d);
        double inner = fmax(1.0, outer.width - 4.0 * thickness);
        tbox_render_push_ellipse(items, outer, color, true, thickness);
        tbox_render_push_ellipse(items, tbox_render_mark_rect(outer.x + outer.width / 2.0, outer.y + outer.height / 2.0, inner, inner), color, open, thickness);
        break;
    }
    case TBOX_STYLE_EMPHASIS_TRIANGLE: {
        /* scanlines: a 1px row per step, the apex on top */
        tbox_rect box = tbox_render_mark_rect(cx, cy, fmax(5.0, size * 0.38), fmax(4.0, size * 0.33));
        int rows = (int)box.height;
        double mid = box.x + box.width / 2.0;
        for (int r = 0; r < rows; r++) {
            double width = fmax(1.0, round(box.width * (r + 1.0) / rows));
            double x = floor(mid - width / 2.0 + 0.5);
            if (!open || r == rows - 1 || width <= 2.0 * thickness) {
                tbox_render_push_fill_rect(items, (tbox_rect){ x, box.y + r, width, 1.0 }, color);
            } else {
                tbox_render_push_fill_rect(items, (tbox_rect){ x, box.y + r, thickness, 1.0 }, color);
                tbox_render_push_fill_rect(items, (tbox_rect){ x + width - thickness, box.y + r, thickness, 1.0 }, color);
            }
        }
        break;
    }
    case TBOX_STYLE_EMPHASIS_SESAME: {
        tbox_render_push_ellipse(items, tbox_render_mark_rect(cx, cy, fmax(open ? 4.0 : 2.0, size * 0.16), fmax(5.0, size * 0.36)), color, open, thickness);
        break;
    }
    case TBOX_STYLE_EMPHASIS_STRING: {
        if (run->emphasis_font == NULL) break;
        tbox_string_view mark = tbox_string_view_from_cstr(style->text_emphasis_string);
        double width = tbox_font_measure_text(run->emphasis_font, mark);
        double line = tbox_font_face_line_height(run->emphasis_font);
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
        *op       = (tbox_paint_op){ 0 };
        op->kind  = TBOX_PAINT_TEXT_RUN;
        op->rect  = (tbox_rect){ cx - width / 2.0, cy - line / 2.0, width, line };
        op->color = color;
        op->text  = mark;
        op->face  = run->emphasis_font;
        tbox_font_shape_into(items->arena, run->emphasis_font, mark, 0.0, &op->glyphs, &op->glyph_count);
        break;
    }
    default:
        break;
    }
}

/* text-emphasis: a mark centered over (or under) each character of `run`
 * that takes one, just outside the text's ascent (descent). */
/* text-decoration lines `lines` over `run`: a thin line spanning the run's
 * width, positioned off its baseline (rect.y + ascent). UNDERLINE sits a
 * little below the baseline, LINE_THROUGH a little above it
 * (approximating x-height by a fraction of ascent -- see
 * docs/dev/historico.md's "Fora de escopo"), OVERLINE at the ascent.
 * Painted after the TEXT_RUN. */
static void tbox_render_push_decoration(tbox_vector *items, const tbox_layout_text_run *run, unsigned int lines, tbox_style_border_style style, tbox_css_rgba color, double thickness) {
    if (lines == 0)
        return;
    double baseline         = run->rect.y + tbox_font_face_ascent(run->font);
    double underline_offset = run->style->text_underline_offset.kind == TBOX_STYLE_LENGTH_PX ? run->style->text_underline_offset.value :
        run->style->text_underline_position_under ? tbox_font_face_line_height(run->font) - tbox_font_face_ascent(run->font) + 1.0 : 2.0;
    for (unsigned int bit = 1u; bit <= 4u; bit <<= 1u) {
        if (!(lines & bit)) continue;
        double line_y = bit == 1u ? baseline + underline_offset : bit == 4u ? baseline - tbox_font_face_ascent(run->font) : baseline - tbox_font_face_ascent(run->font) * 0.3;
        if (style == TBOX_STYLE_BORDER_STYLE_WAVY) {
            tbox_paint_op *wave = (tbox_paint_op *)tbox_vector_push(items);
            *wave = (tbox_paint_op){ 0 };
            wave->kind = TBOX_PAINT_WAVY_LINE;
            wave->rect = (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness };
            wave->color = color;
        } else if (style == TBOX_STYLE_BORDER_STYLE_DOUBLE) {
            tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness }, color);
            tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y + 2.0 * thickness, run->rect.width, thickness }, color);
        } else {
            tbox_style_border_style decor_style = style == TBOX_STYLE_BORDER_STYLE_NONE ? TBOX_STYLE_BORDER_STYLE_SOLID : style;
            tbox_render_push_border_side(items, (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness }, true, decor_style, color);
        }
    }
}

static void tbox_render_push_emphasis(tbox_vector *items, const tbox_layout_text_run *run) {
    const tbox_style *style = run->style;
    if (style->text_emphasis_mark == TBOX_STYLE_EMPHASIS_NONE || run->font == NULL || run->text.size == 0)
        return;
    tbox_css_rgba color = style->text_emphasis_color.a != 0 ? style->text_emphasis_color : style->color;
    double size = style->font_size > 0.0 ? style->font_size : 16.0;
    double extent = style->text_emphasis_mark == TBOX_STYLE_EMPHASIS_STRING ? size * 0.5 : fmax(7.0, size * 0.42);
    double cy = style->text_emphasis_under ? run->rect.y + tbox_font_face_line_height(run->font) + extent / 2.0 : run->rect.y - extent / 2.0;
    /* One mark centered on each painted glyph (its advance without the
     * trailing letter-spacing); the source character, found through the
     * glyph's cluster, decides whether it takes one. */
    const tbox_glyph *glyphs = run->glyphs;
    size_t glyph_count       = run->glyph_count;
    if (glyph_count == 0)
        tbox_font_shape_into(items->arena, run->font, run->text, style->letter_spacing, &glyphs, &glyph_count);
    for (size_t g = 0; g < glyph_count; g++) {
        size_t at = glyphs[g].cluster;
        if (at >= run->text.size)
            continue;
        if (tbox_render_emphasis_skips(tbox_render_next_codepoint(run->text, &at)))
            continue;
        double left  = glyphs[g].x;
        double right = glyphs[g].x + glyphs[g].advance - style->letter_spacing;
        tbox_render_push_emphasis_mark(items, run, run->rect.x + (left + right) / 2.0, cy, size, color);
    }
}

/* A layer marker op (LAYER_BEGIN/MASK/END), appended. */
static tbox_paint_op *tbox_render_push_marker(tbox_vector *items, tbox_paint_op_kind kind) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    if (op != NULL) {
        *op       = (tbox_paint_op){ 0 };
        op->kind  = kind;
        op->color = (tbox_css_rgba){ 0, 0, 0, 255 };
    }
    return op;
}

static bool tbox_render_has_box_decoration(const tbox_style *style) {
    if (style->background_color.a != 0 || style->background_image[0] != '\0' || style->background_gradient.kind != TBOX_STYLE_GRADIENT_NONE || style->box_shadow_color.a != 0 || style->box_shadow_count > 0 || style->has_border_image) return true;
    for (size_t i = 0; i < 4; i++)
        if (tbox_style_border_side_width(style, i) > 0.0) return true;
    for (size_t i = 1; i < style->background_layer_count; i++)
        if (tbox_style_background_layers_of(style)->layers[i - 1].image[0] != '\0' || tbox_style_background_layers_of(style)->layers[i - 1].gradient.kind != TBOX_STYLE_GRADIENT_NONE) return true;
    return false;
}

static void tbox_render_box(const tbox_layout_box *box, tbox_vector *items, bool has_clip, tbox_rect clip, tbox_arena *arena, tbox_rect viewport) {
    /* A transform (other than a translation, which Layout already
     * applied) wraps the box in a layer of its own, outside any effects
     * layer: the subtree paints untransformed, and the layer composites
     * it through the matrix, clipped by the ancestors' clip, which is in
     * the coordinates around the box -- so nothing inside takes it. */
    size_t transform_start = items->length;
    bool transformed       = box->has_transform;
    bool outer_has_clip    = has_clip;
    bool reflected = box->style != NULL && tbox_style_box_reflect_of(box->style)->direction != TBOX_STYLE_REFLECT_NONE;
    if (transformed || reflected) {
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_BEGIN);
        has_clip = false;
    }
    size_t own_start  = items->length;
    /* A compositing layer wraps everything this box and its subtree emit;
     * the effects ride on the LAYER_BEGIN, filled in at the end. */
    bool layered = box->style != NULL && tbox_style_needs_layer(box->style);
    if (layered)
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_BEGIN);
    bool visible      = box->style == NULL || !box->style->visibility_hidden;
    bool decorated    = visible && !box->empty_cell_hidden;
    double corners[4] = { 0.0, 0.0, 0.0, 0.0 }, corners_y[4] = { 0.0, 0.0, 0.0, 0.0 };
    bool elliptical   = false;
    if (box->style != NULL)
        elliptical = tbox_render_style_corners(box->style, box->border_box, corners, corners_y);

    const tbox_style_shadow *shadows = NULL;
    tbox_style_shadow legacy_shadow;
    size_t shadow_count = decorated && box->style != NULL ? tbox_render_box_shadows(box->style, &shadows, &legacy_shadow) : 0;
    /* NOVO (visual fidelity): box-shadow, painted BEFORE the box's own
     * background/border so paint order alone makes them correctly cover
     * the shadow wherever the two overlap -- no explicit clipping
     * needed, same reasoning as any other paint-order z-stack in this
     * pipeline. The first shadow is on top, so the list paints backwards. */
    for (size_t s = shadow_count; s > 0; s--)
        if (!shadows[s - 1].inset && shadows[s - 1].color.a != 0)
            tbox_render_push_box_shadow(items, box->border_box, corners, corners_y, elliptical, &shadows[s - 1]);

    /* backdrop-filter: recolors what is already painted behind the
     * border box, before the box's own background goes on top. */
    if (decorated && box->style != NULL && box->style->has_backdrop_filter) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
        if (op != NULL) {
            memset(op, 0, sizeof(*op));
            op->kind         = TBOX_PAINT_BACKDROP_FILTER;
            op->rect         = box->border_box;
            op->color        = (tbox_css_rgba){ 0, 0, 0, 255 };
            op->filters      = tbox_style_backdrop_filters_of(box->style);
            op->elliptical   = true;
            for (size_t i = 0; i < 4; i++) {
                op->corner_radii[i]   = corners[i];
                op->corner_radii_y[i] = elliptical ? corners_y[i] : corners[i];
            }
        }
    }

    /* border painting. Render Pipeline isn't handed the
     * already-computed value from Layout Tree, so it re-derives it here
     * from `style` alone -- same formula as tbox_layout_build_element
     * (Tarefa 2): only `solid` ever paints. */
    double border[4] = { 0.0, 0.0, 0.0, 0.0 };
    bool has_border  = false;
    for (size_t i = 0; i < 4 && !box->table_suppress_border && box->style != NULL; i++) {
        border[i] = tbox_style_border_side_width(box->style, i);
        if (border[i] > 0.0)
            has_border = true;
    }
    bool rounded = tbox_render_has_corners(corners);
    tbox_style_background_clip background_clip = box->style != NULL ? box->style->background_clip : TBOX_STYLE_BACKGROUND_CLIP_BORDER_BOX;
    tbox_rect background_box = background_clip == TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX ? box->content_box :
                               background_clip == TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX ? box->padding_box : box->border_box;
    double background_h[4], background_v[4], padding_h[4], padding_v[4];
    if (elliptical) {
        tbox_render_inset_shape(corners, corners_y, box->border_box, background_box, background_h, background_v);
        tbox_render_inset_shape(corners, corners_y, box->border_box, box->padding_box, padding_h, padding_v);
    } else {
        tbox_render_inset_corners(corners, box->border_box, background_box, background_h);
        tbox_render_inset_corners(corners, box->border_box, box->padding_box, padding_h);
        for (size_t i = 0; i < 4; i++) {
            background_v[i] = background_h[i];
            padding_v[i]    = padding_h[i];
        }
    }

    /* Background color, then the image layer, then inset shadows, all
     * under the border. With a background-blend-mode the color and the
     * images paint in a layer of their own: they blend only with each
     * other, as CSS isolates them. */
    bool blend_group = false;
    for (size_t l = 0; decorated && box->style != NULL && l < (box->style->background_layer_count > 1 ? box->style->background_layer_count : 1); l++)
        if ((l == 0 ? box->style->background_blend_mode : tbox_style_background_layers_of(box->style)->layers[l - 1].blend_mode) != TBOX_STYLE_BLEND_NORMAL)
            blend_group = true;
    if (blend_group)
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_BEGIN);
    if (decorated && box->style != NULL) {
        if (box->style->background_color.a != 0) {
            if (rounded)
                tbox_render_push_shape(items, background_box, background_h, background_v, elliptical, box->style->background_color);
            else
                tbox_render_push_fill_rect(items, background_box, box->style->background_color);
        }
        /* Layers paint bottom (last) to top (first). The first layer's
         * fields live directly in the style; copy them into a layer view,
         * except the gradient, which the op must point at in place. */
        for (size_t l = box->style->background_layer_count > 1 ? box->style->background_layer_count : 1; l > 1; l--) {
            const tbox_style_background_layer *layer = &tbox_style_background_layers_of(box->style)->layers[l - 2];
            if (layer->image[0] != '\0' || layer->gradient.kind != TBOX_STYLE_GRADIENT_NONE)
                tbox_render_push_background_image(items, box, layer, box->background_layer_images[l - 2], background_box, background_h, background_v, rounded, viewport, box->style->background_origin);
        }
        if (box->style->background_image[0] != '\0' || box->style->background_gradient.kind != TBOX_STYLE_GRADIENT_NONE) {
            tbox_style_background_layer *first = (tbox_style_background_layer *)tbox_arena_alloc(arena, sizeof(tbox_style_background_layer));
            if (first != NULL) {
                memcpy(first->image, box->style->background_image, sizeof(first->image));
                first->gradient    = box->style->background_gradient;
                first->size_kind   = box->style->background_size_kind;
                first->size[0]     = box->style->background_size[0];
                first->size[1]     = box->style->background_size[1];
                first->position[0] = box->style->background_position[0];
                first->position[1] = box->style->background_position[1];
                first->repeat_x    = box->style->background_repeat_x;
                first->repeat_y    = box->style->background_repeat_y;
                first->repeat_mode_x = box->style->background_repeat_mode_x;
                first->repeat_mode_y = box->style->background_repeat_mode_y;
                first->attachment_fixed = box->style->background_attachment_fixed;
                first->blend_mode       = box->style->background_blend_mode;
                tbox_render_push_background_image(items, box, first, box->background_image, background_box, background_h, background_v, rounded, viewport, box->style->background_origin);
            }
        }
        if (blend_group)
            tbox_render_push_marker(items, TBOX_PAINT_LAYER_END);
        for (size_t s = shadow_count; s > 0; s--)
            if (shadows[s - 1].inset && shadows[s - 1].color.a != 0)
                tbox_render_push_inset_shadow(items, box->padding_box, padding_h, padding_v, &shadows[s - 1]);
    }

    /* border-image replaces the border styles (not the widths), and
     * ignores border-radius. */
    if (decorated && box->style != NULL && box->style->has_border_image && tbox_render_push_border_image(items, box, border))
        has_border = false;

    tbox_style_border_style ring_style = TBOX_STYLE_BORDER_STYLE_SOLID;
    if (decorated && rounded && has_border) {
        size_t first = 0;
        while (first < 3 && border[first] <= 0.0) first++;
        ring_style = tbox_style_border_side_style(box->style, first);
        for (size_t i = 0; i < 4; i++)
            if (border[i] != border[first] || tbox_style_border_side_style(box->style, i) != ring_style)
                ring_style = TBOX_STYLE_BORDER_STYLE_SOLID; /* only uniform borders dash */
    }
    if (decorated && rounded && has_border && (ring_style == TBOX_STYLE_BORDER_STYLE_DASHED || ring_style == TBOX_STYLE_BORDER_STYLE_DOTTED)) {
        size_t color_side = 0;
        while (color_side < 3 && border[color_side] <= 0.0) color_side++;
        tbox_render_push_rounded_dashes(items, box->border_box, corners, corners_y, border[color_side], ring_style == TBOX_STYLE_BORDER_STYLE_DOTTED, tbox_style_border_side_color(box->style, color_side));
    } else if (decorated && rounded && has_border) {
        /* A rounded border paints as one ring, in one color: the first
         * painted side. This keeps transparent padding and alpha borders
         * correct. */
        size_t color_side = 0;
        while (color_side < 3 && border[color_side] <= 0.0) color_side++;
        tbox_render_push_fill_ring(items, box->border_box, corners, box->padding_box, padding_h, tbox_style_border_side_color(box->style, color_side));
        if (elliptical) {
            tbox_paint_op *ring = (tbox_paint_op *)tbox_vector_at(items, items->length - 1);
            ring->elliptical    = true;
            for (size_t i = 0; i < 4; i++) {
                ring->corner_radii[i]         = corners[i];
                ring->corner_radii_y[i]       = corners_y[i];
                ring->inner_corner_radii[i]   = padding_h[i];
                ring->inner_corner_radii_y[i] = padding_v[i];
            }
        }
    } else if (decorated && has_border) {
        tbox_rect border_box  = box->border_box;
        tbox_rect padding_box = box->padding_box;

        /* Top and bottom span the full border_box width (including
         * corners); left and right span only the padding_box
         * height, so the 4 corners are each covered exactly once --
         * by the top/bottom color when adjacent sides differ. */
        const tbox_rect strips[4] = {
            { border_box.x,                      border_box.y,                       border_box.width,                                                        padding_box.y - border_box.y                                              },
            { padding_box.x + padding_box.width, padding_box.y,                      (border_box.x + border_box.width) - (padding_box.x + padding_box.width), padding_box.height                                                        },
            { border_box.x,                      padding_box.y + padding_box.height, border_box.width,                                                        (border_box.y + border_box.height) - (padding_box.y + padding_box.height) },
            { border_box.x,                      padding_box.y,                      padding_box.x - border_box.x,                                            padding_box.height                                                        },
        };
        static const size_t order[4] = { 0, 2, 3, 1 }; /* top, bottom, left, right */
        bool doubles[4];
        for (size_t i = 0; i < 4; i++)
            doubles[i] = border[i] >= 3.0 && tbox_style_border_side_style(box->style, i) == TBOX_STYLE_BORDER_STYLE_DOUBLE;
        const tbox_layout_box *legend_box = NULL;
        if (box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(box->node->element.tag_name, "fieldset")) {
            const tbox_html_node *first = box->node->first_child;
            while (first != NULL && first->type != TBOX_HTML_NODE_ELEMENT) first = first->next_sibling;
            if (first != NULL && tbox_string_view_equal_cstr(first->element.tag_name, "legend"))
                legend_box = tbox_render_find_legend(box->first_child, first);
        }
        for (size_t k = 0; k < 4; k++) {
            size_t i = order[k];
            if (i == 0 && border[i] > 0.0 && legend_box != NULL) {
                double left = legend_box->border_box.x;
                double right = legend_box->border_box.x + legend_box->border_box.width;
                if (left < strips[i].x) left = strips[i].x;
                if (right > strips[i].x + strips[i].width) right = strips[i].x + strips[i].width;
                if (left < right) {
                    tbox_render_push_border_side(items, (tbox_rect){strips[i].x, strips[i].y,
                        left - strips[i].x, strips[i].height}, true,
                        tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
                    tbox_render_push_border_side(items, (tbox_rect){right, strips[i].y,
                        strips[i].x + strips[i].width - right, strips[i].height}, true,
                        tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
                } else
                    tbox_render_push_border_side(items, strips[i], true,
                        tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
            } else if (doubles[i])
                tbox_render_push_double_side(items, i, border_box, padding_box, border, doubles, tbox_style_border_side_color(box->style, i));
            else if (border[i] > 0.0 && tbox_render_is_3d(tbox_style_border_side_style(box->style, i)))
                tbox_render_push_3d_mitered(items, border_box, border, i, tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
            else if (border[i] > 0.0) {
                /* top/bottom strips include the corners, left/right don't */
                bool horizontal = i == 0 || i == 2;
                bool cornered   = horizontal ? border[3] > 0.0 && border[1] > 0.0 : border[0] > 0.0 && border[2] > 0.0;
                tbox_render_push_box_side(items, strips[i], horizontal, tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i), cornered, horizontal ? border[3] : 0.0, horizontal ? border[1] : 0.0);
            }
        }
    }
    if (visible && box->style != NULL && box->style->outline_style != TBOX_STYLE_BORDER_STYLE_NONE && box->style->outline_width > 0.0) {
        double w          = box->style->outline_width;
        tbox_rect b       = box->border_box;
        tbox_css_rgba c   = box->style->outline_color;
        double offset     = box->style->outline_offset;
        double min_offset = -(b.width < b.height ? b.width : b.height) / 2.0;
        if (offset < min_offset)
            offset = min_offset;
        tbox_rect inner            = { b.x - offset, b.y - offset, b.width + 2.0 * offset, b.height + 2.0 * offset };
        tbox_style_border_style st = box->style->outline_style;
        /* A solid outline follows a rounded border box (as in current
         * browsers): its edge radii are the border's grown by the offset,
         * then by the outline's width. Elliptical corners stay square. */
        double h[4], v[4];
        bool rounded_outline = st == TBOX_STYLE_BORDER_STYLE_SOLID && !tbox_render_style_corners(box->style, b, h, v) &&
                       tbox_render_has_corners(h);
        if (rounded_outline) {
            double inner_corners[4], outer_corners[4];
            for (size_t i = 0; i < 4; i++) {
                inner_corners[i] = h[i] > 0.0 && h[i] + offset > 0.0 ? h[i] + offset : 0.0;
                outer_corners[i] = h[i] > 0.0 ? inner_corners[i] + w : 0.0;
            }
            tbox_rect outer = { inner.x - w, inner.y - w, inner.width + 2.0 * w, inner.height + 2.0 * w };
            tbox_render_push_fill_ring(items, outer, outer_corners, inner, inner_corners, c);
        } else if (st == TBOX_STYLE_BORDER_STYLE_DOUBLE && w >= 3.0) {
            const double widths[4] = { w, w, w, w };
            const bool doubles[4]  = { true, true, true, true };
            tbox_rect outer        = { inner.x - w, inner.y - w, inner.width + 2.0 * w, inner.height + 2.0 * w };
            for (size_t i = 0; i < 4; i++)
                tbox_render_push_double_side(items, i, outer, inner, widths, doubles, c);
        } else {
            const tbox_rect sides[4] = {
                { inner.x - w, inner.y - w, inner.width + 2.0 * w, w },
                { inner.x + inner.width, inner.y, w, inner.height },
                { inner.x - w, inner.y + inner.height, inner.width + 2.0 * w, w },
                { inner.x - w, inner.y, w, inner.height },
            };
            static const size_t outline_order[4] = { 0, 2, 3, 1 }; /* top, bottom, left, right */
            for (size_t k = 0; k < 4; k++) {
                size_t side = outline_order[k];
                if (tbox_render_is_3d(st)) {
                    const double widths[4] = { w, w, w, w };
                    tbox_render_push_3d_mitered(items, (tbox_rect){ inner.x - w, inner.y - w, inner.width + 2.0 * w, inner.height + 2.0 * w }, widths, side, st, c);
                }
                else
                    tbox_render_push_border_side(items, sides[side], side == 0 || side == 2, st, c);
            }
        }
    }

    /* ::color-swatch: the value's color over the content box, with the
     * part's radii and border (one ring in the top side's color). */
    tbox_css_rgba input_color;
    if (visible && tbox_render_color_input(box->node, &input_color)) {
        const tbox_style_part_style *swatch = tbox_style_part_find(box->style, TBOX_STYLE_PART_COLOR_SWATCH);
        tbox_rect rect = box->content_box;
        double swatch_corners[4] = { 0.0, 0.0, 0.0, 0.0 };
        if (swatch != NULL) {
            double base = rect.width < rect.height ? rect.width : rect.height;
            for (size_t i = 0; i < 4; i++)
                swatch_corners[i] = swatch->border_radii[i] + swatch->border_radii_percent[i] * base / 100.0;
            if (swatch->border_widths[0] > 0.0 || swatch->border_widths[1] > 0.0 || swatch->border_widths[2] > 0.0 || swatch->border_widths[3] > 0.0) {
                tbox_rect inner = { rect.x + swatch->border_widths[3], rect.y + swatch->border_widths[0],
                                    rect.width - swatch->border_widths[1] - swatch->border_widths[3],
                                    rect.height - swatch->border_widths[0] - swatch->border_widths[2] };
                double inner_corners[4];
                tbox_render_inset_corners(swatch_corners, rect, inner, inner_corners);
                tbox_render_push_fill_ring(items, rect, swatch_corners, inner, inner_corners, swatch->border_colors[0]);
                rect = inner;
                memcpy(swatch_corners, inner_corners, sizeof(swatch_corners));
            }
        }
        if (rect.width > 0.0 && rect.height > 0.0)
            tbox_render_push_fill_rect_corners(items, rect, swatch_corners, input_color);
    }

    if (visible && box->style != NULL && tbox_render_checked_radio(box->node)) {
        tbox_rect content = box->content_box;
        double side       = content.width < content.height ? content.width : content.height;
        /* ::checkmark width sizes the dot; half the box by default. */
        const tbox_style_part_style *mark = tbox_style_part_find(box->style, TBOX_STYLE_PART_CHECKMARK);
        double dot_side   = side * 0.5;
        if (mark != NULL && mark->width.kind != TBOX_STYLE_LENGTH_AUTO)
            dot_side = tbox_style_length_resolve(mark->width, side);
        side = dot_side < side ? dot_side : side;
        if (side > 0.0) {
            tbox_rect dot = { content.x + (content.width - side) / 2.0, content.y + (content.height - side) / 2.0, side, side };
            tbox_render_push_fill_rect_rounded(items, dot, side / 2.0, tbox_render_mark_color(box->style));
        }
    }

    /* Layout supplies the U+2713 text run when a suitable font exists.
     * Keep the small geometric mark for embedded fonts without it. */
    if (visible && box->style != NULL && box->text_run_count == 0 && tbox_render_checked_checkbox(box->node)) {
        tbox_rect content = box->content_box;
        double side       = content.width < content.height ? content.width : content.height;
        double unit       = side / 8.0;
        if (unit > 0.0) {
            double x                        = content.x + (content.width - side) / 2.0;
            double y                        = content.y + (content.height - side) / 2.0;
            const unsigned char pixels[][2] = {
                { 1, 4 },
                { 2, 5 },
                { 3, 6 },
                { 4, 5 },
                { 5, 4 },
                { 6, 3 }
            };
            for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++)
                tbox_render_push_fill_rect(items, (tbox_rect){ x + pixels[i][0] * unit, y + pixels[i][1] * unit, unit, unit }, tbox_render_mark_color(box->style));
        }
    }

    /* A stacking context paints its negative z-index layers over its own
     * background and border, under its text and children. */
    bool context = tbox_render_is_context(box) || box->parent == NULL;
    bool child_has_clip  = has_clip;
    tbox_rect child_clip = clip;
    if (tbox_render_clips(box)) {
        tbox_rect scrollport = tbox_render_overflow_clip(box);
        child_clip     = child_has_clip ? tbox_render_intersect(child_clip, scrollport) : scrollport;
        child_has_clip = true;
    }
    /* Column rules sit above the container background, below its content. */
    size_t rule_start = items->length;
    for (size_t i = 0; visible && box->style != NULL && i < box->column_rule_count; i++) {
        tbox_rect rule = box->column_rules[i];
        tbox_style_border_style st = box->style->column_rule_style;
        tbox_css_rgba color = box->style->column_rule_color;
        if (st == TBOX_STYLE_BORDER_STYLE_NONE) continue;
        if (st == TBOX_STYLE_BORDER_STYLE_DOUBLE && rule.width >= 3.0) {
            double third = rule.width / 3.0;
            tbox_render_push_fill_rect(items, (tbox_rect){ rule.x, rule.y, third, rule.height }, color);
            tbox_render_push_fill_rect(items, (tbox_rect){ rule.x + rule.width - third, rule.y, third, rule.height }, color);
        } else if (tbox_render_is_3d(st)) {
            tbox_render_push_3d_side(items, rule, 1, st, color);
        } else {
            tbox_render_push_border_side(items, rule, false, st, color);
        }
    }
    if (child_has_clip)
        for (size_t i = rule_start; i < items->length; i++) {
            tbox_paint_op *op = tbox_vector_at(items, i);
            op->has_clip = true;
            op->clip = child_clip;
        }
    tbox_vector layers;
    tbox_vector_init(&layers, arena, sizeof(tbox_render_layer), 0);
    if (context) {
        tbox_render_collect_layers(box->first_child, child_has_clip, child_clip, &layers);
        tbox_render_paint_layers(&layers, true, items, arena, viewport);
    }

    size_t text_start = items->length;
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->style != NULL && run->style->visibility_hidden)
            continue;

        /* NOVO (image support): an <img> word's run paints its decoded
         * pixels instead of text -- no background highlight, no
         * text-decoration line (neither applies to a replaced element
         * in this project's scope), just one IMAGE op straight into
         * `run->rect` (already the resolved destination size -- see
         * tbox_layout_push_image_word/tbox_layout_build_line_runs).
         * Skips the rest of this loop body entirely for this run. */
        if (run->image != NULL) {
            /* An inline image's run is its margin box: background and border
             * are painted like a box's, the image fills the content box. (A
             * replaced box's own image run is already its content box.) */
            tbox_rect content = run->rect;
            if (run->style != NULL && run->style != box->style) {
                double margins[4], borders[4], paddings[4];
                for (size_t side = 0; side < 4; side++) {
                    margins[side]  = tbox_style_length_resolve(run->style->margin[side], box->content_box.width);
                    borders[side]  = tbox_style_border_side_width(run->style, side);
                    paddings[side] = tbox_style_length_resolve(run->style->padding[side], box->content_box.width);
                }
                tbox_layout_box frame = { 0 };
                frame.border_box      = (tbox_rect){ run->rect.x + margins[3], run->rect.y + margins[0], run->rect.width - margins[3] - margins[1], run->rect.height - margins[0] - margins[2] };
                frame.padding_box     = (tbox_rect){ frame.border_box.x + borders[3], frame.border_box.y + borders[0], frame.border_box.width - borders[3] - borders[1], frame.border_box.height - borders[0] - borders[2] };
                content               = (tbox_rect){ frame.padding_box.x + paddings[3], frame.padding_box.y + paddings[0], frame.padding_box.width - paddings[3] - paddings[1], frame.padding_box.height - paddings[0] - paddings[2] };
                frame.content_box     = content;
                if (content.width < 0.0) content.width = 0.0;
                if (content.height < 0.0) content.height = 0.0;
                if (tbox_render_has_box_decoration(run->style)) {
                    tbox_style *decoration = (tbox_style *)tbox_arena_alloc(arena, sizeof(tbox_style));
                    if (decoration != NULL) {
                        *decoration = *run->style;
                        decoration->opacity = 1.0;
                        decoration->has_filter = decoration->has_mask = decoration->has_backdrop_filter = false;
                        decoration->filters = NULL;
                        decoration->mix_blend_mode = TBOX_STYLE_BLEND_NORMAL;
                        decoration->box_reflect = NULL;
                        decoration->clip_path = NULL;
                        decoration->outline_style = TBOX_STYLE_BORDER_STYLE_NONE;
                        frame.style = decoration;
                        tbox_render_box(&frame, items, false, (tbox_rect){ 0 }, arena, viewport);
                    }
                }
            }
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            *op               = (tbox_paint_op){ 0 };
            op->kind          = TBOX_PAINT_IMAGE;
            op->rect          = content;
            op->color         = (tbox_css_rgba){ 0, 0, 0, 255 }; /* alpha: the image's opacity */
            op->text          = tbox_string_view_make(NULL, 0);
            op->face          = NULL;
            op->image         = run->image;
            op->image_pixelated = run->style != NULL && run->style->image_rendering_pixelated;
            op->radius        = 0.0;
            for (size_t j = 0; j < 4; j++)
                op->corner_radii[j] = 0.0;
            op->has_clip = false;
            if (run->style != NULL && run->image->width > 0 && run->image->height > 0 &&
                content.width > 0.0 && content.height > 0.0) {
                if (run->style->object_fit != TBOX_STYLE_OBJECT_FIT_FILL) {
                    double sx = content.width / (double)run->image->width;
                    double sy = content.height / (double)run->image->height;
                    double scale = run->style->object_fit == TBOX_STYLE_OBJECT_FIT_COVER ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
                    /* the natural size, zoomed */
                    double natural = tbox_style_zoom(run->style);
                    if (run->style->object_fit == TBOX_STYLE_OBJECT_FIT_NONE ||
                        (run->style->object_fit == TBOX_STYLE_OBJECT_FIT_SCALE_DOWN && scale > natural))
                        scale = natural;
                    op->rect.width = (double)run->image->width * scale;
                    op->rect.height = (double)run->image->height * scale;
                }
                double free_x = content.width - op->rect.width;
                double free_y = content.height - op->rect.height;
                tbox_style_length pos_x = run->style->object_position[0];
                tbox_style_length pos_y = run->style->object_position[1];
                op->rect.x += pos_x.kind == TBOX_STYLE_LENGTH_AUTO ? free_x / 2.0 : tbox_style_length_resolve(pos_x, free_x);
                op->rect.y += pos_y.kind == TBOX_STYLE_LENGTH_AUTO ? free_y / 2.0 : tbox_style_length_resolve(pos_y, free_y);
                if (op->rect.x < content.x || op->rect.y < content.y ||
                    op->rect.x + op->rect.width > content.x + content.width ||
                    op->rect.y + op->rect.height > content.y + content.height) {
                    op->has_clip = true;
                    op->clip = content;
                }
            }
            continue;
        }

        /* Inline decoration belongs to the fragment; direct text already
         * has its containing box's background and border. */
        if (run->style != box->style && tbox_render_has_box_decoration(run->style)) {
            /* One decoration per same-style fragment, even when shaping
             * split it into several faces/runs. Reuse normal box painting. */
            bool continued = i > 0 && box->text_runs[i - 1].style == run->style && box->text_runs[i - 1].rect.y == run->rect.y;
            if (!continued) {
                size_t last_run = i;
                while (last_run + 1 < box->text_run_count && box->text_runs[last_run + 1].style == run->style && box->text_runs[last_run + 1].rect.y == run->rect.y) last_run++;
                bool first = i == 0 || box->text_runs[i - 1].style != run->style;
                bool last = last_run + 1 == box->text_run_count || box->text_runs[last_run + 1].style != run->style;
                bool left = run->style->box_decoration_clone || (run->style->direction_rtl ? last : first);
                bool right = run->style->box_decoration_clone || (run->style->direction_rtl ? first : last);
                tbox_style *decoration = (tbox_style *)tbox_arena_alloc(arena, sizeof(tbox_style));
                if (decoration != NULL) {
                    *decoration = *run->style;
                    for (size_t side = 0; side < 4; side++) {
                        decoration->border_widths[side] = tbox_style_border_side_width(run->style, side);
                        decoration->border_styles[side] = tbox_style_border_side_style(run->style, side);
                        decoration->border_colors[side] = tbox_style_border_side_color(run->style, side);
                    }
                    decoration->border_per_side = true;
                    if (!left) decoration->border_widths[3] = 0.0;
                    if (!right) decoration->border_widths[1] = 0.0;
                    decoration->opacity = 1.0;
                    decoration->has_filter = decoration->has_mask = decoration->has_backdrop_filter = false;
                    decoration->filters = NULL;
                    decoration->mix_blend_mode = TBOX_STYLE_BLEND_NORMAL;
                    decoration->box_reflect = NULL;
                    decoration->clip_path = NULL;
                    decoration->outline_style = TBOX_STYLE_BORDER_STYLE_NONE;
                    double edges[4];
                    for (size_t side = 0; side < 4; side++)
                        edges[side] = tbox_style_length_resolve(decoration->padding[side], box->content_box.width) + decoration->border_widths[side];
                    if (!left) edges[3] = 0.0;
                    if (!right) edges[1] = 0.0;
                    tbox_layout_box fragment = { 0 };
                    fragment.style = decoration;
                    fragment.background_image = run->decoration_images[0];
                    for (size_t l = 1; l < TBOX_STYLE_MAX_BACKGROUND_LAYERS; l++) fragment.background_layer_images[l - 1] = run->decoration_images[l];
                    fragment.border_image = run->decoration_border_image;
                    fragment.content_box = run->rect;
                    const tbox_rect *end = &box->text_runs[last_run].rect;
                    fragment.content_box.width = end->x + end->width - run->rect.x;
                    fragment.border_box = (tbox_rect){ run->rect.x - edges[3], run->rect.y - edges[0], fragment.content_box.width + edges[3] + edges[1], run->rect.height + edges[0] + edges[2] };
                    fragment.padding_box = (tbox_rect){ fragment.border_box.x + decoration->border_widths[3], fragment.border_box.y + decoration->border_widths[0], fragment.border_box.width - decoration->border_widths[3] - decoration->border_widths[1], fragment.border_box.height - decoration->border_widths[0] - decoration->border_widths[2] };
                    /* Resolve radii before suppressing interior corners. */
                    double rh[4], rv[4];
                    tbox_render_style_corners(run->style, fragment.border_box, rh, rv);
                    for (size_t c = 0; c < 4; c++) {
                        bool keep = (c == 0 || c == 3) ? left : right;
                        decoration->border_radius_corners[c] = keep ? rh[c] : 0.0;
                        decoration->border_radius_vertical[c] = keep ? rv[c] : 0.0;
                        decoration->border_radius_percent[c] = decoration->border_radius_vertical_percent[c] = 0.0;
                    }
                    decoration->border_radius = 0.0;
                    tbox_render_box(&fragment, items, false, (tbox_rect){ 0 }, arena, viewport);
                }
            }
        }

        tbox_paint_op *op  = (tbox_paint_op *)tbox_vector_push(items);
        *op                = (tbox_paint_op){ 0 };
        op->kind           = TBOX_PAINT_TEXT_RUN;
        op->rect           = run->rect;
        op->color          = tbox_render_checked_checkbox(box->node) ? tbox_render_mark_color(run->style) : run->style->color; /* per-run color (run->style, never NULL), replacing the one shared box->style->color -- see docs/dev/historico.md's "v13" section */
        op->text           = run->text;
        op->face           = run->font;
        op->letter_spacing = run->style->letter_spacing;
        op->glyphs         = run->glyphs;
        op->glyph_count    = run->glyph_count;
        op->text_stroke_width = run->style->text_stroke_width;
        op->text_stroke_color = run->style->text_stroke_current_color ? run->style->color : run->style->text_stroke_color;
        op->text_stroke_first = run->style->paint_order[0] == TBOX_STYLE_PAINT_STROKE ||
            (run->style->paint_order[0] == TBOX_STYLE_PAINT_MARKERS && run->style->paint_order[1] == TBOX_STYLE_PAINT_STROKE);
        op->image          = NULL;
        op->radius         = 0.0;
        for (size_t j = 0; j < 4; j++)
            op->corner_radii[j] = 0.0;
        bool is_select = box->node != NULL && tbox_string_view_equal_cstr(box->node->element.tag_name, "select");
        op->has_clip   = box->node != NULL && (tbox_string_view_equal_cstr(box->node->element.tag_name, "input") || is_select || tbox_string_view_equal_cstr(box->node->element.tag_name, "textarea"));
        if (box->style != NULL && box->style->text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS && (box->style->white_space == TBOX_STYLE_WHITE_SPACE_NOWRAP || box->style->white_space == TBOX_STYLE_WHITE_SPACE_PRE) && (box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN || box->style->overflow_x != TBOX_STYLE_OVERFLOW_Y_VISIBLE))
            op->has_clip = true;
        if (op->has_clip) {
            op->clip = box->content_box;
            if (is_select) {
                op->clip.width -= 18.0;
                if (op->clip.width < 0.0)
                    op->clip.width = 0.0;
            }
        }

        /* text-shadow: copies of the finished TEXT_RUN, offset and in
         * the shadow color, painted before it -- the last shadow first, so
         * the first one ends up on top. The op is popped and pushed back
         * after them, since pushing may move `items`. A blur is
         * approximated by a 3x3 grid of faint copies spread over +-blur/2
         * -- no real Gaussian, same spirit as box-shadow's stepped rings.
         * Decoration lines get no shadow. */
        const tbox_style_shadow *text_shadows = NULL;
        tbox_style_shadow legacy_text_shadow;
        size_t text_shadow_count = tbox_render_text_shadows(run->style, &text_shadows, &legacy_text_shadow);
        if (text_shadow_count > 0) {
            tbox_paint_op text_op = *op;
            items->length--;
            for (size_t s = text_shadow_count; s > 0; s--) {
                const tbox_style_shadow *text_shadow = &text_shadows[s - 1];
                tbox_css_rgba shadow                 = text_shadow->color;
                double blur                          = text_shadow->blur;
                int steps                            = blur > 0.0 ? 3 : 1;
                if (shadow.a == 0)
                    continue;
                if (steps > 1) {
                    shadow.a = (unsigned char)((double)shadow.a / 5.0 + 0.5);
                    if (shadow.a == 0)
                        shadow.a = 1;
                }
                for (int sy = 0; sy < steps; sy++) {
                    for (int sx = 0; sx < steps; sx++) {
                        tbox_paint_op *copy = (tbox_paint_op *)tbox_vector_push(items);
                        *copy               = text_op;
                        copy->color         = shadow;
                        copy->text_stroke_color = shadow;
                        copy->rect.x += text_shadow->offset_x + (steps > 1 ? (sx - 1) * blur / 2.0 : 0.0);
                        copy->rect.y += text_shadow->offset_y + (steps > 1 ? (sy - 1) * blur / 2.0 : 0.0);
                    }
                }
            }
            *(tbox_paint_op *)tbox_vector_push(items) = text_op;
        }

        /* Its own lines, then the ones propagated from ancestors that it
         * doesn't draw itself, each set in its own style and color. */
        unsigned int own_lines = tbox_style_decoration_lines(run->style);
        tbox_render_push_decoration(items, run, own_lines, run->style->text_decoration_style, run->style->text_decoration_color, run->style->text_decoration_thickness);
        tbox_render_push_decoration(items, run, run->style->decoration_propagated.lines & ~own_lines, run->style->decoration_propagated.style, run->style->decoration_propagated.color, run->style->decoration_propagated.thickness);
    }

    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->image == NULL && run->style != NULL && !run->style->visibility_hidden)
            tbox_render_push_emphasis(items, run);
    }

    if (tbox_render_clips(box)) {
        for (size_t i = text_start; i < items->length; i++) {
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
            tbox_rect scrollport = tbox_render_overflow_clip(box);
            op->clip          = op->has_clip ? tbox_render_intersect(op->clip, scrollport) : scrollport;
            op->has_clip      = true;
        }
    }

    for (size_t i = own_start; i < items->length; i++) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
        if (has_clip) {
            op->clip     = op->has_clip ? tbox_render_intersect(op->clip, clip) : clip;
            op->has_clip = true;
        }
    }
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        if (!tbox_render_is_layer(child))
            tbox_render_box(child, items, child_has_clip, child_clip, arena, viewport);
    if (context)
        tbox_render_paint_layers(&layers, false, items, arena, viewport);
    for (size_t i = 0; visible && i < box->table_edge_count; i++) {
        size_t index = items->length;
        tbox_render_push_fill_rect(items, box->table_edges[i].rect, box->table_edges[i].color);
        if (has_clip) {
            tbox_paint_op *op = tbox_vector_at(items, index);
            op->has_clip      = true;
            op->clip          = clip;
        }
    }

    if (box->style != NULL && box->style->has_filter)
        tbox_render_apply_filter(items, own_start, box->style->filter_matrix, arena);
    /* clip-path: on a layer it clips the composite (after the filter, as
     * CSS orders them); otherwise every op of the subtree. */
    if (box->style != NULL && tbox_style_clip_path_of(box->style)->kind != TBOX_STYLE_CLIP_PATH_NONE)
        tbox_render_apply_clip_path(items, own_start, layered ? own_start + 1 : items->length, box, arena);

    /* Fully transparent: nothing of the subtree paints. */
    if (box->style != NULL && box->style->opacity <= 0.0) {
        items->length = transform_start;
        return;
    }
    if (layered) {
        tbox_paint_op *begin = (tbox_paint_op *)tbox_vector_at(items, own_start);
        begin->color.a       = (unsigned char)(box->style->opacity * 255.0 + 0.5);
        /* Outer transform/reflection groups own the final blend. */
        begin->blend_mode    = (transformed || reflected) ? TBOX_STYLE_BLEND_NORMAL : box->style->mix_blend_mode;
        begin->filters       = tbox_style_filters_of(box->style)->spreads ? tbox_style_filters_of(box->style) : NULL;
        if (box->style->has_mask) {
            const tbox_style_masks *masks = tbox_style_masks_of(box->style);
            size_t count                  = masks->count;
            for (size_t l = count > 0 ? count : 1; l > 0; l--) {
                const tbox_style_mask_layer *layer = count > 0 ? &masks->layers[l - 1] : NULL;
                tbox_paint_op *mask = tbox_render_push_marker(items, TBOX_PAINT_LAYER_MASK);
                if (mask != NULL) {
                    mask->mask_luminance = layer != NULL ? layer->luminance : box->style->mask_luminance;
                    mask->mask_composite = layer != NULL ? layer->composite : TBOX_STYLE_MASK_ADD;
                }
                const tbox_style_background_layer *source = layer != NULL ? &layer->source : &masks->layers[0].source;
                tbox_rect mask_box = layer != NULL && layer->no_clip ? viewport :
                    layer != NULL && layer->clip == TBOX_STYLE_BACKGROUND_CLIP_PADDING_BOX ? box->padding_box :
                    layer != NULL && layer->clip == TBOX_STYLE_BACKGROUND_CLIP_CONTENT_BOX ? box->content_box : box->border_box;
                double mh[4], mv[4];
                tbox_render_inset_shape(corners, corners_y, box->border_box, mask_box, mh, mv);
                size_t mask_start = items->length;
                tbox_render_push_background_image(items, box, source, layer != NULL ? box->mask_layer_images[l - 1] : box->mask_image, mask_box, mh, mv, layer == NULL || !layer->no_clip, viewport, layer != NULL ? layer->origin : TBOX_STYLE_BACKGROUND_ORIGIN_BORDER_BOX);
                for (size_t i = mask_start; i < items->length; i++) {
                    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
                    op->blend_mode = TBOX_STYLE_BLEND_NORMAL;
                }
            }
        }
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_END);
    }
    if (reflected && visible) {
        size_t source_end = items->length;
        size_t reflection = source_end; /* the LAYER_BEGIN pushed next */
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_BEGIN);
        /* An inner effects group keeps its mask in source coordinates. */
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_BEGIN);
        for (size_t i = own_start; i < source_end; i++) {
            tbox_paint_op copy = *(tbox_paint_op *)tbox_vector_at(items, i);
            tbox_paint_op *slot = (tbox_paint_op *)tbox_vector_push(items);
            if (slot != NULL) *slot = copy;
        }
        const tbox_style_reflection *reflect = tbox_style_box_reflect_of(box->style);
        if (reflect->gradient.kind != TBOX_STYLE_GRADIENT_NONE || reflect->image[0] != '\0') {
            tbox_render_push_marker(items, TBOX_PAINT_LAYER_MASK);
            tbox_paint_op *gradient = (tbox_paint_op *)tbox_vector_push(items);
            if (gradient != NULL) {
                *gradient = (tbox_paint_op){ 0 };
                gradient->kind = reflect->image[0] != '\0' ? TBOX_PAINT_IMAGE : TBOX_PAINT_GRADIENT;
                gradient->image = box->reflection_image;
                gradient->rect = box->border_box;
                gradient->gradient = &reflect->gradient;
                gradient->color.a = 255;
            }
        }
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_END);
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_END);
        tbox_paint_op *begin = (tbox_paint_op *)tbox_vector_at(items, reflection);
        begin->has_transform = true;
        begin->transform[0] = begin->transform[3] = 1.0;
        tbox_rect r = box->border_box;
        bool vertical = reflect->direction == TBOX_STYLE_REFLECT_ABOVE || reflect->direction == TBOX_STYLE_REFLECT_BELOW;
        double offset = tbox_style_length_resolve(reflect->offset, vertical ? r.height : r.width);
        if (vertical) {
            begin->transform[3] = -1.0;
            begin->transform[5] = reflect->direction == TBOX_STYLE_REFLECT_BELOW ? 2.0 * (r.y + r.height) + offset : 2.0 * r.y - offset;
        } else {
            begin->transform[0] = -1.0;
            begin->transform[4] = reflect->direction == TBOX_STYLE_REFLECT_RIGHT ? 2.0 * (r.x + r.width) + offset : 2.0 * r.x - offset;
        }
        size_t source_count = source_end - own_start;
        tbox_paint_op *source = (tbox_paint_op *)tbox_arena_alloc(arena, source_count * sizeof(tbox_paint_op));
        if (source != NULL) {
            memcpy(source, (tbox_paint_op *)items->data + own_start, source_count * sizeof(tbox_paint_op));
            size_t reflection_count = items->length - source_end;
            memmove((tbox_paint_op *)items->data + own_start, (tbox_paint_op *)items->data + source_end, reflection_count * sizeof(tbox_paint_op));
            memcpy((tbox_paint_op *)items->data + own_start + reflection_count, source, source_count * sizeof(tbox_paint_op));
        }
    }
    if (transformed || reflected) {
        tbox_paint_op *begin = (tbox_paint_op *)tbox_vector_at(items, transform_start);
        begin->has_transform = transformed;
        if (transformed) memcpy(begin->transform, box->transform, sizeof(begin->transform));
        begin->has_clip      = outer_has_clip;
        begin->clip          = clip; /* never reassigned; only has_clip is cleared above */
        if (box->style != NULL)
            begin->blend_mode = box->style->mix_blend_mode;
        tbox_render_push_marker(items, TBOX_PAINT_LAYER_END);
    }
}

tbox_display_list tbox_render_build_display_list_in_viewport(tbox_arena *arena, const tbox_layout_box *root, double viewport_width, double viewport_height) {
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_paint_op), 0);

    /* The root is the outermost stacking context; its siblings (none in a
     * normal tree) paint in order after it. */
    for (const tbox_layout_box *box = root; box != NULL; box = box->next_sibling)
        tbox_render_box(box, &items, false, (tbox_rect){ 0 }, arena, (tbox_rect){ 0.0, 0.0, viewport_width, viewport_height });

    tbox_display_list list;
    list.items = (tbox_paint_op *)items.data;
    list.count = items.length;
    return list;
}

tbox_display_list tbox_render_build_display_list(tbox_arena *arena, const tbox_layout_box *root) {
    return tbox_render_build_display_list_in_viewport(arena, root,
        root != NULL ? root->border_box.width : 0.0,
        root != NULL ? root->border_box.height : 0.0);
}
