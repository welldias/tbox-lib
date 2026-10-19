#ifndef TBOX_STYLE_GRID_H
#define TBOX_STYLE_GRID_H

#include "tbox_style_internal.h"

/* Bounded working data; no pointers escape into the resolved style. */
#define TBOX_GRID_MAX_TRACKS 128
#define TBOX_GRID_MAX_NAMES 256
#define TBOX_GRID_MAX_AREAS 64

typedef enum tbox_grid_breadth_kind {
    TBOX_GRID_AUTO,
    TBOX_GRID_LENGTH,
    TBOX_GRID_MIN_CONTENT,
    TBOX_GRID_MAX_CONTENT,
    TBOX_GRID_FR,
    TBOX_GRID_FIT_CONTENT
} tbox_grid_breadth_kind;

typedef struct tbox_grid_breadth {
    tbox_grid_breadth_kind kind;
    tbox_style_length length;
    double flex;
} tbox_grid_breadth;

typedef struct tbox_grid_track {
    tbox_grid_breadth min, max;
    bool auto_repeat, auto_fit;
} tbox_grid_track;

typedef struct tbox_grid_line_name {
    tbox_string_view name;
    int line;
    bool repeated;
} tbox_grid_line_name;

typedef struct tbox_grid_track_list {
    tbox_grid_track tracks[TBOX_GRID_MAX_TRACKS];
    int count;
    tbox_grid_line_name names[TBOX_GRID_MAX_NAMES];
    int name_count;
} tbox_grid_track_list;

typedef struct tbox_grid_area {
    tbox_string_view name;
    int row, column, rows, columns;
} tbox_grid_area;

typedef struct tbox_grid_areas {
    tbox_grid_area areas[TBOX_GRID_MAX_AREAS];
    int count, rows, columns;
} tbox_grid_areas;

bool tbox_style_grid_parse_tracks(tbox_string_view text, const tbox_style_units *units, double available, double gap, bool definite, bool implicit, tbox_grid_track_list *out);
bool tbox_style_grid_parse_areas(tbox_string_view text, tbox_grid_areas *out);
void tbox_style_resolve_grid(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style);

#endif
