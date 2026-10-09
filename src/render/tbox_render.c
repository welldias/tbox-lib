#include <tbox/render.h>

#include <tbox/css_cascade.h>

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
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
 * one fill. `dashed` uses dashes of 3x the thickness and `dotted` round dots
 * of 1x, both with gaps of at least 1x stretched so a mark lands on each
 * end of the side -- so corners are always covered. */
static void tbox_render_push_border_side(tbox_vector *items, tbox_rect strip, bool horizontal, tbox_style_border_style style, tbox_css_rgba color) {
    if (strip.width <= 0.0 || strip.height <= 0.0)
        return;
    double thickness = horizontal ? strip.height : strip.width;
    double length    = horizontal ? strip.width : strip.height;

    if (style == TBOX_STYLE_BORDER_STYLE_DASHED || style == TBOX_STYLE_BORDER_STYLE_DOTTED) {
        bool dotted  = style == TBOX_STYLE_BORDER_STYLE_DOTTED;
        double mark  = dotted ? thickness : 3.0 * thickness;
        double gap   = thickness;
        size_t count = (size_t)((length + gap) / (mark + gap));
        if (count >= 2) {
            double spacing = (length - (double)count * mark) / (double)(count - 1);
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

static bool tbox_render_is_3d(tbox_style_border_style style) {
    return style == TBOX_STYLE_BORDER_STYLE_GROOVE || style == TBOX_STYLE_BORDER_STYLE_RIDGE || style == TBOX_STYLE_BORDER_STYLE_INSET || style == TBOX_STYLE_BORDER_STYLE_OUTSET;
}

/* The 3D styles' two shades: the color darkened toward black, or
 * lightened toward white, by a third. */
static tbox_css_rgba tbox_render_shade(tbox_css_rgba color, bool dark) {
    if (dark)
        return (tbox_css_rgba){ (unsigned char)(color.r * 2 / 3), (unsigned char)(color.g * 2 / 3), (unsigned char)(color.b * 2 / 3), color.a };
    return (tbox_css_rgba){ (unsigned char)(color.r + (255 - color.r) / 3), (unsigned char)(color.g + (255 - color.g) / 3), (unsigned char)(color.b + (255 - color.b) / 3), color.a };
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
 * precedes its children's ops -- see ARCHITECTURE.md's "Render Pipeline"
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
static void tbox_render_push_background_image(tbox_vector *items, const tbox_layout_box *box, const tbox_style_background_layer *layer, const tbox_image *image, tbox_rect clip_box, const double clip_h[4], const double clip_v[4], bool rounded, tbox_rect viewport) {
    const tbox_style *style  = box->style;
    bool gradient            = layer->gradient.kind != TBOX_STYLE_GRADIENT_NONE;
    tbox_style_background_repeat mode_x = layer->repeat_mode_x == TBOX_STYLE_BACKGROUND_REPEAT_NO_REPEAT && layer->repeat_x ? TBOX_STYLE_BACKGROUND_REPEAT_REPEAT : layer->repeat_mode_x;
    tbox_style_background_repeat mode_y = layer->repeat_mode_y == TBOX_STYLE_BACKGROUND_REPEAT_NO_REPEAT && layer->repeat_y ? TBOX_STYLE_BACKGROUND_REPEAT_REPEAT : layer->repeat_mode_y;
    tbox_rect area           = layer->attachment_fixed && viewport.width > 0.0 && viewport.height > 0.0 ? viewport : style->background_origin == TBOX_STYLE_BACKGROUND_ORIGIN_BORDER_BOX ? box->border_box : style->background_origin == TBOX_STYLE_BACKGROUND_ORIGIN_CONTENT_BOX ? box->content_box : box->padding_box;
    if ((!gradient && (image == NULL || image->width <= 0 || image->height <= 0)) || area.width <= 0.0 || area.height <= 0.0 || clip_box.width <= 0.0 || clip_box.height <= 0.0)
        return;

    double intrinsic_w = gradient ? area.width : (double)image->width;
    double intrinsic_h = gradient ? area.height : (double)image->height;
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
            op->has_clip      = true;
            op->clip          = clip_box;
            if (rounded)
                tbox_render_set_rounded_clip(op, clip_box, clip_h, clip_v);
            tiles++;
        }
    }
}

/* A box with an explicit z-index on a positioned box: painted out of tree
 * order, by its nearest stacking context (see tbox_render_box). */
static bool tbox_render_is_z_box(const tbox_layout_box *box) {
    return box->style != NULL && box->style->position != TBOX_STYLE_POSITION_STATIC && !box->style->z_index_auto;
}

/* A stacking context root: a z-index box, or one with opacity below 1 --
 * opacity fades the whole subtree, z-indexed descendants included. */
static bool tbox_render_is_context(const tbox_layout_box *box) {
    return tbox_render_is_z_box(box) || (box->style != NULL && box->style->opacity < 1.0);
}

/* A positioned box: painted by its stacking context after the in-flow
 * content, in z-index order (`auto` and 0 in tree order). */
static bool tbox_render_is_layer(const tbox_layout_box *box) {
    return box->style != NULL && box->style->position != TBOX_STYLE_POSITION_STATIC;
}

static int tbox_render_layer_z(const tbox_layout_box *box) {
    return box->style->z_index_auto ? 0 : box->style->z_index;
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
        if (best == count || tbox_render_layer_z(layer->box) < tbox_render_layer_z(((const tbox_render_layer *)tbox_vector_at_const(layers, best))->box))
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
    double in[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 }, out[4];
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
    double mark = dotted ? 0.0 : 3.0 * width, period = mark + (dotted ? width * 2.0 : width);
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
                tbox_render_push_fill_rect_rounded(items, (tbox_rect){ x - half, y - half, width, width }, half, color);
                break;
            }
            segment_start += size;
            segment++;
        }
    }
}

static void tbox_render_apply_filter(tbox_vector *items, size_t start, const double m[20], tbox_arena *arena) {
    for (size_t i = start; i < items->length; i++) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
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
        }
    }
}

/* `clip-path`: the shape as a rounded clip on every op of the subtree.
 * An op that already has a rounded clip (a rounded background) keeps it
 * and is only clipped to the shape's bounding box. */
static void tbox_render_apply_clip_path(tbox_vector *items, size_t start, const tbox_layout_box *box) {
    const tbox_style_clip_path *clip = &box->style->clip_path;
    tbox_rect area = box->border_box, shape;
    double h[4] = { 0.0, 0.0, 0.0, 0.0 }, v[4] = { 0.0, 0.0, 0.0, 0.0 };
    if (clip->kind == TBOX_STYLE_CLIP_PATH_INSET) {
        double top = tbox_style_length_resolve(clip->inset[0], area.height), right = tbox_style_length_resolve(clip->inset[1], area.width);
        double bottom = tbox_style_length_resolve(clip->inset[2], area.height), left = tbox_style_length_resolve(clip->inset[3], area.width);
        shape = (tbox_rect){ area.x + left, area.y + top, area.width - left - right, area.height - top - bottom };
        for (size_t i = 0; i < 4; i++)
            h[i] = clip->round_h[i], v[i] = clip->round_v[i];
        tbox_render_normalize_corners(shape, h, h);
        tbox_render_normalize_corners(shape, v, v);
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
    for (size_t i = start; i < items->length; i++) {
        tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
        if (!op->has_rounded_clip) {
            tbox_render_set_rounded_clip(op, shape, h, v);
        } else {
            op->clip     = op->has_clip ? tbox_render_intersect(op->clip, shape) : shape;
            op->has_clip = true;
        }
    }
}

static void tbox_render_box(const tbox_layout_box *box, tbox_vector *items, bool has_clip, tbox_rect clip, tbox_arena *arena, tbox_rect viewport) {
    size_t own_start  = items->length;
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
     * under the border. */
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
            const tbox_style_background_layer *layer = &box->style->background_layers[l - 2];
            if (layer->image[0] != '\0' || layer->gradient.kind != TBOX_STYLE_GRADIENT_NONE)
                tbox_render_push_background_image(items, box, layer, box->background_layer_images[l - 2], background_box, background_h, background_v, rounded, viewport);
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
                tbox_render_push_background_image(items, box, first, box->background_image, background_box, background_h, background_v, rounded, viewport);
            }
        }
        for (size_t s = shadow_count; s > 0; s--)
            if (shadows[s - 1].inset && shadows[s - 1].color.a != 0)
                tbox_render_push_inset_shadow(items, box->padding_box, padding_h, padding_v, &shadows[s - 1]);
    }

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
        for (size_t k = 0; k < 4; k++) {
            size_t i = order[k];
            if (doubles[i])
                tbox_render_push_double_side(items, i, border_box, padding_box, border, doubles, tbox_style_border_side_color(box->style, i));
            else if (border[i] > 0.0 && tbox_render_is_3d(tbox_style_border_side_style(box->style, i)))
                tbox_render_push_3d_side(items, strips[i], i, tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
            else if (border[i] > 0.0)
                tbox_render_push_border_side(items, strips[i], i == 0 || i == 2, tbox_style_border_side_style(box->style, i), tbox_style_border_side_color(box->style, i));
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
        if (st == TBOX_STYLE_BORDER_STYLE_DOUBLE && w >= 3.0) {
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
                if (tbox_render_is_3d(st))
                    tbox_render_push_3d_side(items, sides[side], side, st, c);
                else
                    tbox_render_push_border_side(items, sides[side], side == 0 || side == 2, st, c);
            }
        }
    }

    tbox_css_rgba input_color;
    if (visible && tbox_render_color_input(box->node, &input_color))
        tbox_render_push_fill_rect(items, box->content_box, input_color);

    if (visible && box->style != NULL && tbox_render_checked_radio(box->node)) {
        tbox_rect content = box->content_box;
        double side       = content.width < content.height ? content.width : content.height;
        side *= 0.5;
        if (side > 0.0) {
            tbox_rect dot = { content.x + (content.width - side) / 2.0, content.y + (content.height - side) / 2.0, side, side };
            tbox_render_push_fill_rect_rounded(items, dot, side / 2.0, tbox_render_accent_color(box->style));
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
                tbox_render_push_fill_rect(items, (tbox_rect){ x + pixels[i][0] * unit, y + pixels[i][1] * unit, unit, unit }, tbox_render_accent_color(box->style));
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
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            *op               = (tbox_paint_op){ 0 };
            op->kind          = TBOX_PAINT_IMAGE;
            op->rect          = run->rect;
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
                run->rect.width > 0.0 && run->rect.height > 0.0) {
                if (run->style->object_fit != TBOX_STYLE_OBJECT_FIT_FILL) {
                    double sx = run->rect.width / (double)run->image->width;
                    double sy = run->rect.height / (double)run->image->height;
                    double scale = run->style->object_fit == TBOX_STYLE_OBJECT_FIT_COVER ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
                    if (run->style->object_fit == TBOX_STYLE_OBJECT_FIT_NONE ||
                        (run->style->object_fit == TBOX_STYLE_OBJECT_FIT_SCALE_DOWN && scale > 1.0))
                        scale = 1.0;
                    op->rect.width = (double)run->image->width * scale;
                    op->rect.height = (double)run->image->height * scale;
                }
                double free_x = run->rect.width - op->rect.width;
                double free_y = run->rect.height - op->rect.height;
                tbox_style_length pos_x = run->style->object_position[0];
                tbox_style_length pos_y = run->style->object_position[1];
                op->rect.x += pos_x.kind == TBOX_STYLE_LENGTH_AUTO ? free_x / 2.0 : tbox_style_length_resolve(pos_x, free_x);
                op->rect.y += pos_y.kind == TBOX_STYLE_LENGTH_AUTO ? free_y / 2.0 : tbox_style_length_resolve(pos_y, free_y);
                if (op->rect.x < run->rect.x || op->rect.y < run->rect.y ||
                    op->rect.x + op->rect.width > run->rect.x + run->rect.width ||
                    op->rect.y + op->rect.height > run->rect.y + run->rect.height) {
                    op->has_clip = true;
                    op->clip = run->rect;
                }
            }
            continue;
        }

        /* <mark> highlight -- a FILL_RECT covering the run's
         * own rect (not the whole box/line), painted before its
         * TEXT_RUN so the glyphs draw on top of it. Same helper as the
         * box background/border FILL_RECTs above.
         *
         * `run->style != box->style` guards against double-painting: a
         * run's own direct text (no inline descendant in between, e.g.
         * `<h1 style="background-color:...">`) is built with
         * `run->style` pointing at the SAME tbox_style as `box->style`
         * (see tbox_layout_build_text_runs/tbox_layout_collect_words),
         * whose background_color the box FILL_RECT above already
         * painted across the whole border_box. Without this guard, that
         * identical color gets painted a second time over just the
         * run's rect, compositing its alpha on top of itself and
         * producing a visibly different (darker/more opaque) patch
         * behind the text than the rest of the box -- only an inline
         * descendant with its OWN resolved style (a distinct pointer,
         * e.g. `<mark>`) should get this highlight. */
        if (run->style != box->style && run->style->background_color.a != 0) {
            tbox_render_push_fill_rect(items, run->rect, run->style->background_color);
        }

        tbox_paint_op *op  = (tbox_paint_op *)tbox_vector_push(items);
        *op                = (tbox_paint_op){ 0 };
        op->kind           = TBOX_PAINT_TEXT_RUN;
        op->rect           = run->rect;
        op->color          = tbox_render_checked_checkbox(box->node) ? tbox_render_accent_color(run->style) : run->style->color; /* per-run color (run->style, never NULL), replacing the one shared box->style->color -- see ARCHITECTURE.md's "v13" section */
        op->text           = run->text;
        op->face           = run->font;
        op->letter_spacing = run->style->letter_spacing;
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
                        copy->rect.x += text_shadow->offset_x + (steps > 1 ? (sx - 1) * blur / 2.0 : 0.0);
                        copy->rect.y += text_shadow->offset_y + (steps > 1 ? (sy - 1) * blur / 2.0 : 0.0);
                    }
                }
            }
            *(tbox_paint_op *)tbox_vector_push(items) = text_op;
        }

        /* <del>/<ins> decoration line -- a thin (1px)
         * FILL_RECT spanning the run's width, positioned off its
         * baseline (same baseline tbox_raster_text_run/Output Display
         * already computes: rect.y + ascent). UNDERLINE sits a little
         * below the baseline, LINE_THROUGH a little above it
         * (approximating x-height by a fraction of ascent -- see
         * ARCHITECTURE.md's "Fora de escopo"). Painted after the
         * TEXT_RUN, same color as the text. */
        unsigned int decoration_lines = run->style->text_decoration_lines;
        if (decoration_lines == 0 && run->style->text_decoration != TBOX_STYLE_TEXT_DECORATION_NONE)
            decoration_lines = run->style->text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE ? 1u : run->style->text_decoration == TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH ? 2u : 4u;
        if (decoration_lines != 0) {
            double baseline         = run->rect.y + tbox_font_face_ascent(run->font);
            double underline_offset = run->style->text_underline_offset.kind == TBOX_STYLE_LENGTH_PX ? run->style->text_underline_offset.value :
                run->style->text_underline_position_under ? tbox_font_face_line_height(run->font) - tbox_font_face_ascent(run->font) + 1.0 : 2.0;
            for (unsigned int bit = 1u; bit <= 4u; bit <<= 1u) {
                if (!(decoration_lines & bit)) continue;
                double line_y = bit == 1u ? baseline + underline_offset : bit == 4u ? baseline - tbox_font_face_ascent(run->font) : baseline - tbox_font_face_ascent(run->font) * 0.3;
                double thickness = run->style->text_decoration_thickness;
                if (run->style->text_decoration_style == TBOX_STYLE_BORDER_STYLE_WAVY) {
                    tbox_paint_op *wave = (tbox_paint_op *)tbox_vector_push(items);
                    *wave = (tbox_paint_op){ 0 };
                    wave->kind = TBOX_PAINT_WAVY_LINE;
                    wave->rect = (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness };
                    wave->color = run->style->text_decoration_color;
                } else if (run->style->text_decoration_style == TBOX_STYLE_BORDER_STYLE_DOUBLE) {
                    tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness }, run->style->text_decoration_color);
                    tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y + 2.0 * thickness, run->rect.width, thickness }, run->style->text_decoration_color);
                } else {
                    tbox_style_border_style decor_style = run->style->text_decoration_style == TBOX_STYLE_BORDER_STYLE_NONE ? TBOX_STYLE_BORDER_STYLE_SOLID : run->style->text_decoration_style;
                    tbox_render_push_border_side(items, (tbox_rect){ run->rect.x, line_y, run->rect.width, thickness }, true, decor_style, run->style->text_decoration_color);
                }
            }
        }
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
    if (box->style != NULL && box->style->clip_path.kind != TBOX_STYLE_CLIP_PATH_NONE)
        tbox_render_apply_clip_path(items, own_start, box);

    /* opacity: every op this box and its subtree emitted fades by the
     * same factor. Each op is blended on its own, so overlapping
     * descendants show through each other -- not a true offscreen
     * group, see tbox_style.opacity. Fully transparent drops them. */
    if (box->style != NULL && box->style->opacity < 1.0) {
        if (box->style->opacity <= 0.0) {
            items->length = own_start;
        } else {
            for (size_t i = own_start; i < items->length; i++) {
                tbox_paint_op *op = tbox_vector_at(items, i);
                op->color.a       = (unsigned char)(op->color.a * box->style->opacity + 0.5);
            }
        }
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
