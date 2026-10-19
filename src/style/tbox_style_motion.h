#ifndef TBOX_STYLE_MOTION_H
#define TBOX_STYLE_MOTION_H

#include "base/tbox_vector.h"
#include "style/tbox_style_internal.h"

/* One sampled segment. Moves between SVG subpaths consume no distance.
 * Endpoint derivatives preserve smooth tangents, including curve ends. */
typedef struct tbox_motion_segment {
    double x0, y0, x1, y1, length;
    double tx0, ty0, tx1, ty1;
} tbox_motion_segment;

#endif
