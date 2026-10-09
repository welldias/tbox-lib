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

#endif
