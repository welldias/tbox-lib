#ifndef TBOX_RASTER_INTERNAL_H
#define TBOX_RASTER_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/output.h>

/* Integral pixel rectangle every pixel `op` can touch lies in, clipped to
 * its own clips and the buffer. Conservative: a text run gets a margin for
 * glyph overhang. False when the op paints nothing in the buffer. Shared by
 * the damage tracker and tbox_raster_display_list_damaged. */
bool tbox_raster_op_paint_bounds(const tbox_paint_op *op, int32_t buffer_width, int32_t buffer_height, tbox_rect *out);

/* Compositing layers. The index of the LAYER_END matching the LAYER_BEGIN
 * at `begin` (list->count when unterminated); `*out_mask` gets its
 * LAYER_MASK, or the end when it has none. */
size_t tbox_raster_layer_end(const tbox_display_list *list, size_t begin, size_t *out_mask);

/* What the content of the layer at `begin` (its ops before the mask, with
 * nested layers grown by their reach) can touch, before the layer's own
 * effects. False when it paints nothing. */
bool tbox_raster_layer_content_bounds(const tbox_display_list *list, size_t begin, int32_t buffer_width, int32_t buffer_height, tbox_rect *out);

#endif
