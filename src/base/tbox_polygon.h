#ifndef TBOX_POLYGON_H
#define TBOX_POLYGON_H

#include <stdbool.h>
#include <stddef.h>
#include <math.h>

/* Half-open edge crossings avoid counting vertices twice. Both winding
 * rules close the last edge; no allocation or persistent state. Shared by
 * raster and hit-test so self-intersections use the same interior. */
static inline bool tbox_polygon_contains(const double (*points)[2], size_t count, bool evenodd, double x, double y) {
    if (count < 3 || !isfinite(x) || !isfinite(y)) return false;
    for (size_t i = 0; i < count; i++)
        if (!isfinite(points[i][0]) || !isfinite(points[i][1])) return false;
    int winding = 0;
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        double ax = points[j][0], ay = points[j][1], bx = points[i][0], by = points[i][1];
        long double side = ((long double)bx - ax) * ((long double)y - ay) - ((long double)x - ax) * ((long double)by - ay);
        if (ay <= y && by > y && side > 0.0L) winding++;
        else if (by <= y && ay > y && side < 0.0L) winding--;
    }
    return evenodd ? winding % 2 != 0 : winding != 0;
}

#endif
