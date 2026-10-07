#include "tbox_context_internal.h"

#include <stdlib.h>

#include "render/tbox_render_internal.h"

static const tbox_layout_box *tbox_context_hit_test_clipped(const tbox_layout_box *box, double x, double y);

static const tbox_layout_box *tbox_context_hit_test_clipped(const tbox_layout_box *box, double x, double y) {
    if (box == NULL) {
        return NULL;
    }

    const tbox_layout_box *last_hit = NULL;
    if (box->style == NULL || box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_VISIBLE ||
        tbox_context_point_in_rect(box->padding_box, x, y)) {
        for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling) {
            const tbox_layout_box *hit = tbox_context_hit_test_clipped(child, x, y);
            if (hit != NULL) last_hit = hit;
        }
    }

    if (last_hit != NULL) {
        return last_hit;
    }

    return (box->style == NULL || (!box->style->visibility_hidden && !box->style->pointer_events_none)) &&
        tbox_context_point_in_rect(box->border_box, x, y) ? box : NULL;
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
    const tbox_layout_box *hit = NULL;
    for (size_t i = count; i > 0 && hit == NULL; i--) {
        const tbox_render_painted_box *entry = &order[i - 1];
        const tbox_style *style              = entry->box->style;
        if ((style == NULL || (!style->visibility_hidden && !style->pointer_events_none)) && tbox_context_point_in_rect(entry->box->border_box, x, y) &&
            (!entry->has_clip || tbox_context_point_in_rect(entry->clip, x, y)))
            hit = entry->box;
    }
    free(order);
    return hit;
}

/* `cursor: auto` picks text over editable fields and text, a pointing
 * hand over links, and the arrow elsewhere. */
tbox_style_cursor tbox_context_cursor_at(const tbox_context *ctx, double x, double y) {
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    if (box == NULL)
        return TBOX_STYLE_CURSOR_DEFAULT;
    /* Inline elements have no box of their own: the run under the pointer
     * carries their style, which leads back to their node. */
    const tbox_style *style    = box->style;
    const tbox_html_node *node = box->node;
    bool over_text             = false;
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->text.size == 0 || !tbox_context_point_in_rect(run->rect, x, y))
            continue;
        over_text = run->image == NULL;
        for (size_t e = 0; e < ctx->styles.count; e++) {
            if (&ctx->styles.items[e].style == run->style) {
                style = run->style;
                node  = ctx->styles.items[e].node;
                break;
            }
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
    return over_text ? TBOX_STYLE_CURSOR_TEXT : TBOX_STYLE_CURSOR_DEFAULT;
}

const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return NULL;
    }
    return tbox_context_hit_test_box(ctx->root, x, y);
}
