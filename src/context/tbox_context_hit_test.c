#include "tbox_context_internal.h"

#include <math.h>
#include <stdlib.h>

#include "render/tbox_render_internal.h"
#include "base/tbox_polygon.h"

bool tbox_context_box_point(const tbox_layout_box *box, double *x, double *y) {
    if (box == NULL)
        return true;
    if (!tbox_context_box_point(box->parent, x, y))
        return false;
    if (!box->has_transform)
        return true;
    const double *m = box->transform;
    double det = m[0] * m[3] - m[1] * m[2];
    if (!(fabs(det) > 1e-12))
        return false; /* flattened to nothing: no point lands on it */
    double dx = *x - m[4], dy = *y - m[5];
    *x = (m[3] * dx - m[2] * dy) / det;
    *y = (m[0] * dy - m[1] * dx) / det;
    return true;
}

/* A clipped ancestor also removes descendants from pointer targeting,
 * even when the descendant escaped its ancestor's border box. Each
 * polygon is in its own element's coordinates (inside its transform). */
bool tbox_context_in_polygon_clips(const tbox_layout_box *box, double x, double y) {
    for (const tbox_layout_box *ancestor = box; ancestor != NULL; ancestor = ancestor->parent) {
        if (ancestor->style == NULL || tbox_style_clip_path_of(ancestor->style)->kind != TBOX_STYLE_CLIP_PATH_POLYGON) continue;
        const tbox_style_clip_path *clip = tbox_style_clip_path_of(ancestor->style);
        double points[TBOX_STYLE_MAX_POLYGON_POINTS][2];
        if (clip->point_count > TBOX_STYLE_MAX_POLYGON_POINTS) return false;
        for (size_t i = 0; i < clip->point_count; i++) {
            points[i][0] = tbox_style_length_resolve(clip->points[i][0], ancestor->border_box.width);
            points[i][1] = tbox_style_length_resolve(clip->points[i][1], ancestor->border_box.height);
        }
        double lx = x, ly = y;
        if (!tbox_context_box_point(ancestor, &lx, &ly)) return false;
        if (!tbox_polygon_contains(points, clip->point_count, clip->evenodd,
            lx - ancestor->border_box.x, ly - ancestor->border_box.y)) return false;
    }
    return true;
}

static const tbox_layout_box *tbox_context_hit_test_clipped(const tbox_layout_box *box, double x, double y);

static tbox_rect tbox_context_scrollport(const tbox_layout_box *box) {
    tbox_rect rect = box->padding_box;
    rect.width = rect.width > box->scrollbar_gutter ? rect.width - box->scrollbar_gutter : 0.0;
    return rect;
}

/* (x, y) are screen coordinates throughout; each box tests them in its
 * own (tbox_context_box_point). */
static const tbox_layout_box *tbox_context_hit_test_clipped(const tbox_layout_box *box, double x, double y) {
    if (box == NULL || !tbox_context_in_polygon_clips(box, x, y)) {
        return NULL;
    }
    double lx = x, ly = y;
    if (!tbox_context_box_point(box, &lx, &ly))
        return NULL;

    const tbox_layout_box *last_hit = NULL;
    if (box->style == NULL || box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_VISIBLE ||
        tbox_context_point_in_rect(tbox_context_scrollport(box), lx, ly)) {
        for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling) {
            const tbox_layout_box *hit = tbox_context_hit_test_clipped(child, x, y);
            if (hit != NULL) last_hit = hit;
        }
    }

    if (last_hit != NULL) {
        return last_hit;
    }

    return (box->style == NULL || (!box->style->visibility_hidden && !box->style->pointer_events_none)) &&
        tbox_context_point_in_rect(box->border_box, lx, ly) ? box : NULL;
}

/* Recursive part of tbox_context_hit_test.
 *
 * v0-v4 assumed "if box's border_box doesn't contain (x, y), no descendant
 * of box can contain it either" -- true only while every box stays fully
 * nested inside its DOM parent's border_box, which is what plain block flow
 * guarantees. From v5 on (`position: absolute`/`fixed`, and already true
 * today via v4 negative margins) that guarantee is gone: an out-of-flow
 * descendant can be positioned entirely outside its own parent's
 * border_box on purpose. So this always visits every child first,
 * regardless of whether `box` itself contains the point, and only falls
 * back to checking `box` once none of them matched. A box with
 * pointer-events:none cannot be the target itself; its children are still
 * searched so an explicit pointer-events:auto descendant can be targeted.
 *
 * Overlap is also now possible between two boxes that are not
 * ancestor/descendant of each other (e.g. two positioned siblings, or a
 * sibling and an absolute box that escaped its parent). The project has no
 * stacking context/z-index, so paint order is always document order
 * (tbox_render_build_display_list walks the tree pre-order) -- whichever
 * box comes later in `next_sibling` order is painted on top. To match that
 * visually, among the children whose recursive hit-test matched, this
 * keeps the LAST one instead of returning on the first match. For v0-v4
 * content (no intentional overlap), at most one child ever matches at a
 * time, so this is behavior-preserving there -- zero regression.
 *
 * Not `static`: declared in tbox_context_hit_test.h (not <tbox/context.h>
 * -- still not part of the public API) so tests/context/test_context.c can
 * drive this recursion directly against a hand-built tbox_layout_box tree,
 * without going through the opaque tbox_context/the whole Style+Layout
 * pipeline just to get overlapping or out-of-parent-bounds geometry. */
const tbox_layout_box *tbox_context_hit_test_box(const tbox_layout_box *box, double x, double y) {
    /* With z-index, paint order is no longer tree order: walk the boxes in
     * the order Render paints them, topmost first. */
    size_t count                   = 0;
    tbox_render_painted_box *order = tbox_render_paint_order(box, &count);
    if (order == NULL)
        return tbox_context_hit_test_clipped(box, x, y);
    /* A transformed box tests its border box inside its transform, and
     * the clip it got from outside in its parent's coordinates. */
    const tbox_layout_box *hit = NULL;
    for (size_t i = count; i > 0 && hit == NULL; i--) {
        const tbox_render_painted_box *entry = &order[i - 1];
        const tbox_style *style              = entry->box->style;
        double px = x, py = y, lx = x, ly = y;
        if (!tbox_context_box_point(entry->box->parent, &px, &py) || !tbox_context_box_point(entry->box, &lx, &ly))
            continue;
        if ((style == NULL || (!style->visibility_hidden && !style->pointer_events_none)) && tbox_context_point_in_rect(entry->box->border_box, lx, ly) &&
            (!entry->has_clip || tbox_context_point_in_rect(entry->clip, px, py)) &&
            tbox_context_in_polygon_clips(entry->box, x, y))
            hit = entry->box;
    }
    free(order);
    return hit;
}

/* `cursor: auto` picks text over editable fields, a pointing hand over
 * links, and the arrow elsewhere -- static text included: this is an
 * application UI, not a document, so plain text doesn't invite selecting
 * (`cursor: text` asks for it). */
tbox_style_cursor tbox_context_cursor_at(const tbox_context *ctx, double x, double y) {
    /* Over a `resize` handle: the resize arrow of its axes. */
    const tbox_layout_box *resizable = tbox_context_resizer_at(ctx, x, y);
    if (resizable != NULL) {
        tbox_style_resize axes = tbox_style_resize_allowed(resizable->style, tbox_context_is_textarea(resizable->node));
        return axes == TBOX_STYLE_RESIZE_HORIZONTAL ? TBOX_STYLE_CURSOR_EW_RESIZE :
               axes == TBOX_STYLE_RESIZE_VERTICAL   ? TBOX_STYLE_CURSOR_NS_RESIZE :
               resizable->style->direction_rtl     ? TBOX_STYLE_CURSOR_NESW_RESIZE : TBOX_STYLE_CURSOR_NWSE_RESIZE;
    }
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    if (box == NULL)
        return TBOX_STYLE_CURSOR_DEFAULT;
    if (tbox_context_image_area_at(ctx, box, x, y) != NULL)
        return TBOX_STYLE_CURSOR_POINTER;
    /* Inline elements have no box of their own: the run under the pointer
     * carries their style, which leads back to their node. */
    const tbox_style *style    = box->style;
    const tbox_html_node *node = box->node;
    double lx = x, ly = y;
    tbox_context_box_point(box, &lx, &ly);
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->text.size == 0 || !tbox_context_point_in_rect(run->rect, lx, ly))
            continue;
        const tbox_html_node *element = tbox_context_run_element(ctx, box, run);
        if (element != NULL) {
            style = run->style;
            node  = element;
        }
        break;
    }
    /* `cursor` inherits, so the innermost element's value already
     * reflects its ancestors'. */
    if (style != NULL && style->cursor != TBOX_STYLE_CURSOR_AUTO)
        return style->cursor;
    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent)
        if (ancestor->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(ancestor->element.tag_name, "a") && tbox_html_node_get_attribute(ancestor, tbox_string_view_from_cstr("href")) != NULL)
            return TBOX_STYLE_CURSOR_POINTER;
    if (box->node != NULL && (tbox_context_is_text_input(box->node) || tbox_context_is_textarea(box->node)))
        return TBOX_STYLE_CURSOR_TEXT;
    return TBOX_STYLE_CURSOR_DEFAULT;
}

const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return NULL;
    }
    const tbox_layout_box *box = tbox_context_hit_test_box(ctx->root, x, y);
    /* A ::before/::after box belongs to its element: report the element's
     * box. Its synthetic node lives in the frame arena, so nothing kept
     * across frames (hover, active, focus, drag) may point at it. */
    const tbox_layout_box *generated = box;
    while (generated != NULL && (generated->style == NULL || generated->style->generated == 0))
        generated = generated->parent;
    if (generated != NULL && generated->node != NULL) {
        const tbox_html_node *owner = generated->node->parent;
        for (box = generated; box != NULL && box->node != owner;)
            box = box->parent;
    }
    return box;
}
