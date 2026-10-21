#ifndef TBOX_RASTER_PATH_H
#define TBOX_RASTER_PATH_H
#include "svg/tbox_svg_internal.h"
typedef struct tbox_raster_path_state {
    void *library;
    bool failed;
} tbox_raster_path_state;
typedef void (*tbox_raster_path_span_fn)(void *userdata, int x, int y, unsigned length, unsigned char coverage);
void tbox_raster_path_paint(tbox_raster_path_state *state, const tbox_paint_path *path, tbox_rect clip, tbox_raster_path_span_fn span, void *userdata);
void tbox_raster_path_destroy(tbox_raster_path_state *state);
#endif
