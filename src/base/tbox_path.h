#ifndef TBOX_BASE_PATH_H
#define TBOX_BASE_PATH_H
#include "base/tbox_vector.h"
#include <tbox/string_view.h>

typedef struct tbox_path_point {
    double x, y;
} tbox_path_point;

/* Sampled edges, including pen moves and explicit subpath closure. Tangents
 * and accumulated lengths also serve CSS motion paths. Arena-owned. */
typedef struct tbox_path_segment {
    double x0, y0, x1, y1, length;
    double tx0, ty0, tx1, ty1;
    bool move, close;
} tbox_path_segment;

bool tbox_path_parse(tbox_string_view data, tbox_vector *segments, bool *closed, double tolerance);
bool tbox_path_segment_add(tbox_vector *segments, tbox_path_point a, tbox_path_point b, tbox_path_point ta, tbox_path_point tb, bool move);
bool tbox_path_line(tbox_vector *segments, tbox_path_point a, tbox_path_point b);
bool tbox_path_cubic(tbox_vector *segments, tbox_path_point a, tbox_path_point b, tbox_path_point c, tbox_path_point d, unsigned depth, double tolerance);
bool tbox_path_arc(tbox_vector *segments, tbox_path_point start, double rx, double ry, double rotation, bool large, bool sweep, tbox_path_point end, double tolerance);

#endif
