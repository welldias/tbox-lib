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

/* tbox_raster_op_paint_bounds clipped to `area` instead of a buffer: the
 * ops of a transformed layer live in untransformed coordinates, where
 * the screen's edges mean nothing (TBOX_RASTER_UNBOUNDED). */
bool tbox_raster_op_bounds_in(const tbox_paint_op *op, tbox_rect area, tbox_rect *out);

#define TBOX_RASTER_UNBOUNDED ((tbox_rect){ -1048576.0, -1048576.0, 2097152.0, 2097152.0 })

/* The bounding box of `r` mapped through the affine matrix `m`
 * (a b c d e f). */
tbox_rect tbox_raster_transform_bounds(const double m[6], tbox_rect r);

/* Compositing layers. The index of the LAYER_END matching the LAYER_BEGIN
 * at `begin` (list->count when unterminated); `*out_mask` gets its
 * LAYER_MASK, or the end when it has none. */
size_t tbox_raster_layer_end(const tbox_display_list *list, size_t begin, size_t *out_mask);

/* What the content of the layer at `begin` (its ops before the mask, with
 * nested layers grown by their reach) can touch, before the layer's own
 * effects and transform, within `area` (the coordinates around the layer;
 * a transformed layer's content is unbounded). False when it paints
 * nothing. */
bool tbox_raster_layer_content_bounds(const tbox_display_list *list, size_t begin, tbox_rect area, tbox_rect *out);

#endif
