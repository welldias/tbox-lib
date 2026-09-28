#include "tbox_context_internal.h"

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
    return tbox_context_hit_test_clipped(box, x, y);
}

const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return NULL;
    }
    return tbox_context_hit_test_box(ctx->root, x, y);
}
