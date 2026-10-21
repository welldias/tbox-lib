#ifndef TBOX_STYLE_MOTION_H
#define TBOX_STYLE_MOTION_H

#include "base/tbox_vector.h"
#include "base/tbox_path.h"
#include "style/tbox_style_internal.h"

/* One sampled segment. Moves between SVG subpaths consume no distance.
 * Endpoint derivatives preserve smooth tangents, including curve ends. */
typedef tbox_path_segment tbox_motion_segment;

#endif
