#ifndef TBOX_RENDER_INTERNAL_H
#define TBOX_RENDER_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/layout.h>

/* One box in paint order, with the overflow clip its ancestors put on it. */
typedef struct tbox_render_painted_box {
    const tbox_layout_box *box;
    bool has_clip;
    tbox_rect clip;
} tbox_render_painted_box;

/* Every box of `root`'s tree in the order tbox_render_build_display_list
 * paints them -- stacking contexts and z-index included -- so hit testing
 * can pick the topmost box the same way painting stacks them. A malloc'd
 * array of `*out_count` entries the caller frees; NULL for an empty tree
 * (or on allocation failure, with `*out_count` 0). */
tbox_render_painted_box *tbox_render_paint_order(const tbox_layout_box *root, size_t *out_count);

#endif /* TBOX_RENDER_INTERNAL_H */
