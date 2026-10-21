#include "style/tbox_style_grid.h"
#include "tbox_layout_internal.h"

#include <math.h>
#include <string.h>

#define TBOX_GRID_AUTO_LINE (-1000000)
#define TBOX_GRID_INFINITY 1e300

typedef struct tbox_layout_grid_item {
    const tbox_html_node *node, *run_end;
    const tbox_style *style;
    bool anonymous, out_of_flow;
    int order;
    int start[2], span[2]; /* columns, rows; logical lines before shifting */
    double min[2], max[2], auto_min[2];
    double baseline;
    tbox_layout_box *box;
} tbox_layout_grid_item;

typedef struct tbox_layout_grid_track {
    tbox_grid_track sizing;
    double base, limit, fit, position;
    bool collapsed;
} tbox_layout_grid_track;

typedef struct tbox_layout_grid_axis {
    tbox_grid_track_list explicit_tracks, implicit_tracks;
    int explicit_count, first, end;
    tbox_layout_grid_track tracks[TBOX_GRID_MAX_TRACKS];
    double gap, extent;
} tbox_layout_grid_axis;

static int tbox_grid_clamp(int n, int lo, int hi) {
    return n < lo ? lo : n > hi ? hi : n;
}

static bool tbox_grid_named_line(const tbox_layout_grid_axis *axis, const tbox_grid_areas *areas, int dimension, int line, tbox_string_view name) {
    for (int i = 0; i < axis->explicit_tracks.name_count; i++) {
        const tbox_grid_line_name *entry = &axis->explicit_tracks.names[i];
        if (entry->line == line && tbox_string_view_equal(entry->name, name))
            return true;
    }
    for (int i = 0; i < areas->count; i++) {
        const tbox_grid_area *area = &areas->areas[i];
        int start                  = dimension == 0 ? area->column : area->row;
        int span                   = dimension == 0 ? area->columns : area->rows;
        if (name.size > area->name.size && memcmp(name.data, area->name.data, area->name.size) == 0) {
            tbox_string_view suffix = tbox_string_view_make(name.data + area->name.size, name.size - area->name.size);
            if ((line == start && tbox_string_view_equal_cstr(suffix, "-start")) || (line == start + span && tbox_string_view_equal_cstr(suffix, "-end")))
                return true;
        }
    }
    return false;
}

static int tbox_grid_resolve_line(const tbox_style_grid_line *value, const tbox_layout_grid_axis *axis, const tbox_grid_areas *areas, int dimension, bool end) {
    if (value->span || (value->number == 0 && value->name[0] == '\0'))
        return TBOX_GRID_AUTO_LINE;
    if (value->name[0] == '\0')
        return value->number > 0 ? value->number - 1 : axis->explicit_count + value->number + 1;
    tbox_string_view name = tbox_string_view_from_cstr(value->name);
    if (value->number == 0)
        for (int i = 0; i < areas->count; i++) {
            const tbox_grid_area *area = &areas->areas[i];
            if (tbox_string_view_equal(area->name, name))
                return (dimension == 0 ? area->column : area->row) + (end ? (dimension == 0 ? area->columns : area->rows) : 0);
        }
    if (value->number == 0) {
        char implicit_name[TBOX_STYLE_GRID_NAME_SIZE + 6];
        memcpy(implicit_name, name.data, name.size);
        const char *suffix = end ? "-end" : "-start";
        size_t suffix_size = strlen(suffix);
        memcpy(implicit_name + name.size, suffix, suffix_size);
        tbox_string_view area_line = tbox_string_view_make(implicit_name, name.size + suffix_size);
        for (int line = 0; line <= axis->explicit_count; line++)
            if (tbox_grid_named_line(axis, areas, dimension, line, area_line))
                return line;
    }
    int occurrence = value->number == 0 ? 1 : value->number;
    int step       = occurrence > 0 ? 1 : -1;
    int left       = occurrence > 0 ? occurrence : -occurrence;
    int line       = step > 0 ? 0 : axis->explicit_count;
    for (int n = 0; n < TBOX_GRID_MAX_TRACKS * 3; n++, line += step) {
        bool outside = line < 0 || line > axis->explicit_count;
        if (outside || tbox_grid_named_line(axis, areas, dimension, line, name))
            if (--left == 0)
                return line;
    }
    return step > 0 ? TBOX_GRID_MAX_TRACKS : -TBOX_GRID_MAX_TRACKS;
}

static int tbox_grid_resolve_span(const tbox_style_grid_line *span, int opposite, int step, const tbox_layout_grid_axis *axis, const tbox_grid_areas *areas, int dimension) {
    int count = span->number > 0 ? span->number : 1;
    if (span->name[0] == '\0')
        return opposite + step * count;
    tbox_string_view name = tbox_string_view_from_cstr(span->name);
    int line              = opposite + step;
    for (int n = 0; n < TBOX_GRID_MAX_TRACKS * 3; n++, line += step) {
        if (line < 0 || line > axis->explicit_count || tbox_grid_named_line(axis, areas, dimension, line, name))
            if (--count == 0)
                return line;
    }
    return opposite + step;
}

static void tbox_grid_resolve_item(tbox_layout_grid_item *item, const tbox_layout_grid_axis axes[2], const tbox_grid_areas *areas) {
    const tbox_style *s                   = item->style;
    const tbox_style_grid_line *starts[2] = { &s->grid_column_start, &s->grid_row_start };
    const tbox_style_grid_line *ends[2]   = { &s->grid_column_end, &s->grid_row_end };
    for (int d = 0; d < 2; d++) {
        int start = item->anonymous ? TBOX_GRID_AUTO_LINE : tbox_grid_resolve_line(starts[d], &axes[d], areas, d, false);
        int end   = item->anonymous ? TBOX_GRID_AUTO_LINE : tbox_grid_resolve_line(ends[d], &axes[d], areas, d, true);
        int span  = item->anonymous ? 1 : starts[d]->span ? starts[d]->number : ends[d]->span ? ends[d]->number : 1;
        if (!item->anonymous && start != TBOX_GRID_AUTO_LINE && ends[d]->span)
            end = tbox_grid_resolve_span(ends[d], start, 1, &axes[d], areas, d);
        else if (!item->anonymous && end != TBOX_GRID_AUTO_LINE && starts[d]->span)
            start = tbox_grid_resolve_span(starts[d], end, -1, &axes[d], areas, d);
        if (start != TBOX_GRID_AUTO_LINE && end != TBOX_GRID_AUTO_LINE) {
            if (end < start) {
                int swap = start;
                start    = end;
                end      = swap;
            }
            span = end == start ? 1 : end - start;
        }
        span = tbox_grid_clamp(span, 1, TBOX_GRID_MAX_TRACKS);
        if (start == TBOX_GRID_AUTO_LINE && end != TBOX_GRID_AUTO_LINE)
            start = end - span;
        item->start[d] = start == TBOX_GRID_AUTO_LINE ? start : tbox_grid_clamp(start, 1 - TBOX_GRID_MAX_TRACKS, TBOX_GRID_MAX_TRACKS - 1);
        item->span[d]  = span;
    }
}

/* Bounded occupancy avoids allocation proportional to untrusted line numbers. */
static bool tbox_grid_free(const unsigned char *cells, const int start[2], const int span[2], const tbox_layout_grid_axis axes[2]) {
    int x = start[0] - axes[0].first, y = start[1] - axes[1].first;
    if (x < 0 || y < 0 || x + span[0] > TBOX_GRID_MAX_TRACKS || y + span[1] > TBOX_GRID_MAX_TRACKS)
        return false;
    for (int row = y; row < y + span[1]; row++)
        for (int col = x; col < x + span[0]; col++)
            if (cells[row * TBOX_GRID_MAX_TRACKS + col])
                return false;
    return true;
}

static void tbox_grid_occupy(unsigned char *cells, tbox_layout_grid_item *item, tbox_layout_grid_axis axes[2]) {
    for (int d = 0; d < 2; d++) {
        item->span[d]  = tbox_grid_clamp(item->span[d], 1, TBOX_GRID_MAX_TRACKS);
        item->start[d] = tbox_grid_clamp(item->start[d], axes[d].first, axes[d].first + TBOX_GRID_MAX_TRACKS - item->span[d]);
        if (item->start[d] + item->span[d] > axes[d].end)
            axes[d].end = item->start[d] + item->span[d];
    }
    int x = item->start[0] - axes[0].first, y = item->start[1] - axes[1].first;
    for (int row = y; row < y + item->span[1]; row++)
        for (int col = x; col < x + item->span[0]; col++)
            cells[row * TBOX_GRID_MAX_TRACKS + col] = 1;
}

static void tbox_grid_place(tbox_layout_grid_item *items, size_t count, const tbox_style *style, tbox_layout_grid_axis axes[2], unsigned char *cells) {
    int major = style->grid_auto_flow_column ? 0 : 1, minor = 1 - major;
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (item->out_of_flow)
            continue;
        for (int d = 0; d < 2; d++)
            if (item->start[d] != TBOX_GRID_AUTO_LINE) {
                if (item->start[d] < axes[d].first)
                    axes[d].first = item->start[d];
                if (item->start[d] + item->span[d] > axes[d].end)
                    axes[d].end = item->start[d] + item->span[d];
            }
    }
    for (int d = 0; d < 2; d++)
        axes[d].end = tbox_grid_clamp(axes[d].end, axes[d].first, axes[d].first + TBOX_GRID_MAX_TRACKS);
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (!item->out_of_flow && item->start[0] != TBOX_GRID_AUTO_LINE && item->start[1] != TBOX_GRID_AUTO_LINE)
            tbox_grid_occupy(cells, item, axes);
    }
    /* Items locked to a major-axis track precede the auto-placement cursor. */
    int row_cursor[TBOX_GRID_MAX_TRACKS];
    for (int i = 0; i < TBOX_GRID_MAX_TRACKS; i++)
        row_cursor[i] = axes[minor].first;
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (item->out_of_flow || item->start[major] == TBOX_GRID_AUTO_LINE || item->start[minor] != TBOX_GRID_AUTO_LINE)
            continue;
        item->start[major] = tbox_grid_clamp(item->start[major], axes[major].first, axes[major].first + TBOX_GRID_MAX_TRACKS - item->span[major]);
        int row            = item->start[major] - axes[major].first;
        int start          = style->grid_auto_flow_dense ? axes[minor].first : row_cursor[row];
        for (int p = start; p <= axes[minor].first + TBOX_GRID_MAX_TRACKS - item->span[minor]; p++) {
            item->start[minor] = p;
            if (tbox_grid_free(cells, item->start, item->span, axes))
                break;
        }
        tbox_grid_occupy(cells, item, axes);
        for (int r = row; r < row + item->span[major]; r++) {
            int end = item->start[minor] + item->span[minor];
            if (end > row_cursor[r])
                row_cursor[r] = end;
        }
    }
    for (size_t i = 0; i < count; i++)
        if (!items[i].out_of_flow && items[i].start[minor] == TBOX_GRID_AUTO_LINE && axes[minor].end - axes[minor].first < items[i].span[minor])
            axes[minor].end = axes[minor].first + items[i].span[minor];
    int cursor[2] = { axes[0].first, axes[1].first };
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (item->out_of_flow || item->start[major] != TBOX_GRID_AUTO_LINE)
            continue;
        if (style->grid_auto_flow_dense) {
            cursor[0] = axes[0].first;
            cursor[1] = axes[1].first;
        }
        bool fixed_minor = item->start[minor] != TBOX_GRID_AUTO_LINE;
        if (fixed_minor) {
            if (item->start[minor] < cursor[minor])
                cursor[major]++;
            cursor[minor] = item->start[minor];
        }
        for (int attempt = 0; attempt < TBOX_GRID_MAX_TRACKS * TBOX_GRID_MAX_TRACKS; attempt++) {
            item->start[0] = cursor[0];
            item->start[1] = cursor[1];
            if ((fixed_minor || cursor[minor] + item->span[minor] <= axes[minor].end) && tbox_grid_free(cells, item->start, item->span, axes))
                break;
            if (fixed_minor)
                cursor[major]++;
            else if (++cursor[minor] + item->span[minor] > axes[minor].end) {
                cursor[minor] = axes[minor].first;
                cursor[major]++;
            }
            if (cursor[major] + item->span[major] > axes[major].first + TBOX_GRID_MAX_TRACKS)
                break;
        }
        tbox_grid_occupy(cells, item, axes);
    }
}

static tbox_grid_breadth tbox_grid_used_breadth(tbox_grid_breadth value, double available, bool definite) {
    if (value.kind == TBOX_GRID_LENGTH && value.length.kind == TBOX_STYLE_LENGTH_PERCENT && !definite)
        value.kind = TBOX_GRID_AUTO;
    else if (value.kind == TBOX_GRID_LENGTH)
        value.length = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_PX, .value = fmax(0, tbox_style_length_resolve(value.length, available)) };
    return value;
}

static void tbox_grid_initialize(tbox_layout_grid_axis *axis, double available, bool definite, const tbox_layout_grid_item *items, size_t count, int dimension) {
    for (int logical = axis->first; logical < axis->end; logical++) {
        tbox_layout_grid_track *track = &axis->tracks[logical - axis->first];
        if (logical >= 0 && logical < axis->explicit_tracks.count)
            track->sizing = axis->explicit_tracks.tracks[logical];
        else if (axis->implicit_tracks.count > 0) {
            int index = logical < 0 ? logical % axis->implicit_tracks.count : (logical - axis->explicit_count) % axis->implicit_tracks.count;
            if (index < 0)
                index += axis->implicit_tracks.count;
            track->sizing = axis->implicit_tracks.tracks[index];
        }
        if (track->sizing.auto_fit) {
            track->collapsed = true;
            for (size_t i = 0; i < count; i++)
                if (!items[i].out_of_flow && items[i].start[dimension] <= logical && items[i].start[dimension] + items[i].span[dimension] > logical)
                    track->collapsed = false;
        }
        track->sizing.min = tbox_grid_used_breadth(track->sizing.min, available, definite);
        track->sizing.max = tbox_grid_used_breadth(track->sizing.max, available, definite);
        track->base       = track->sizing.min.kind == TBOX_GRID_LENGTH ? track->sizing.min.length.value : 0;
        track->limit      = track->sizing.max.kind == TBOX_GRID_LENGTH ? fmax(track->base, track->sizing.max.length.value) : TBOX_GRID_INFINITY;
        track->fit        = track->sizing.max.kind == TBOX_GRID_FIT_CONTENT && (definite || track->sizing.max.length.kind != TBOX_STYLE_LENGTH_PERCENT) ? fmax(0, tbox_style_length_resolve(track->sizing.max.length, available)) : TBOX_GRID_INFINITY;
        if (track->collapsed)
            track->base = track->limit = 0;
    }
}

static double tbox_grid_gaps(const tbox_layout_grid_axis *axis, int start, int span) {
    int visible = 0;
    for (int i = start; i < start + span; i++)
        if (!axis->tracks[i].collapsed)
            visible++;
    return visible > 1 ? axis->gap * (visible - 1) : 0;
}

/* Intrinsic contributions are applied in increasing span order. Spans
 * crossing flex tracks use a bounded equal-share approximation (§11.5). */
static void tbox_grid_intrinsic(tbox_layout_grid_axis *axis, tbox_layout_grid_item *items, size_t count, int dimension) {
    for (int span = 1; span <= TBOX_GRID_MAX_TRACKS; span++)
        for (size_t i = 0; i < count; i++) {
            tbox_layout_grid_item *item = &items[i];
            if (item->out_of_flow || item->span[dimension] != span)
                continue;
            int start   = item->start[dimension] - axis->first;
            double used = tbox_grid_gaps(axis, start, span);
            int grow    = 0;
            for (int j = start; j < start + span; j++) {
                const tbox_layout_grid_track *t = &axis->tracks[j];
                used += t->base;
                if (!t->collapsed && t->sizing.min.kind != TBOX_GRID_LENGTH)
                    grow++;
            }
            double minimum     = item->auto_min[dimension];
            bool fixed_maximum = true;
            double maximum     = tbox_grid_gaps(axis, start, span);
            for (int j = start; j < start + span; j++) {
                const tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->sizing.max.kind != TBOX_GRID_LENGTH)
                    fixed_maximum = false;
                else
                    maximum += t->sizing.max.length.value;
            }
            if (fixed_maximum)
                minimum = fmin(minimum, maximum);
            double extra = fmax(0, minimum - used);
            for (int j = start; j < start + span; j++) {
                tbox_layout_grid_track *t = &axis->tracks[j];
                if (!t->collapsed && t->sizing.min.kind != TBOX_GRID_LENGTH) {
                    double contribution = t->sizing.min.kind == TBOX_GRID_MAX_CONTENT ? fmax(extra, item->max[dimension] - used) : t->sizing.min.kind == TBOX_GRID_MIN_CONTENT ? fmax(0, item->min[dimension] - used) : extra;
                    t->base += grow > 0 ? contribution / grow : 0;
                    if (t->limit < t->base)
                        t->limit = t->base;
                }
            }
            /* Intrinsic growth limits (max-content, min-content, auto and fit). */
            used = tbox_grid_gaps(axis, start, span);
            grow = 0;
            for (int j = start; j < start + span; j++) {
                const tbox_layout_grid_track *t = &axis->tracks[j];
                used += t->base;
                if (!t->collapsed && t->sizing.max.kind != TBOX_GRID_LENGTH && t->sizing.max.kind != TBOX_GRID_FR)
                    grow++;
            }
            for (int j = start; j < start + span; j++) {
                tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->collapsed || t->sizing.max.kind == TBOX_GRID_LENGTH || t->sizing.max.kind == TBOX_GRID_FR)
                    continue;
                double target = t->sizing.max.kind == TBOX_GRID_MIN_CONTENT ? item->min[dimension] : item->max[dimension];
                double limit  = t->base + (grow > 0 ? fmax(0, target - used) / grow : 0);
                limit         = fmax(t->base, fmin(limit, t->fit));
                if (t->limit == TBOX_GRID_INFINITY)
                    t->limit = limit;
                else
                    t->limit = fmax(t->limit, limit);
            }
        }
    for (int j = 0; j < axis->end - axis->first; j++) {
        tbox_layout_grid_track *t = &axis->tracks[j];
        if (t->limit == TBOX_GRID_INFINITY && t->sizing.max.kind != TBOX_GRID_FR)
            t->limit = t->base;
    }
}

static double tbox_grid_axis_sum(const tbox_layout_grid_axis *axis) {
    int n      = axis->end - axis->first;
    double sum = tbox_grid_gaps(axis, 0, n);
    for (int i = 0; i < n; i++)
        sum += axis->tracks[i].base;
    return sum;
}

static void tbox_grid_size(tbox_layout_grid_axis *axis, tbox_layout_grid_item *items, size_t count, int dimension, double available, bool definite, tbox_style_flex_justify alignment) {
    tbox_grid_initialize(axis, available, definite, items, count, dimension);
    tbox_grid_intrinsic(axis, items, count, dimension);
    int n = axis->end - axis->first;
    /* Maximize non-flex tracks up to their intrinsic/fixed growth limits. */
    if (!definite) {
        for (int j = 0; j < n; j++)
            if (axis->tracks[j].sizing.max.kind != TBOX_GRID_FR)
                axis->tracks[j].base = axis->tracks[j].limit;
    } else
        for (int pass = 0; pass < n; pass++) {
            double free = available - tbox_grid_axis_sum(axis);
            int grow    = 0;
            for (int j = 0; j < n; j++)
                if (axis->tracks[j].sizing.max.kind != TBOX_GRID_FR && axis->tracks[j].base < axis->tracks[j].limit)
                    grow++;
            if (free <= 0 || grow == 0)
                break;
            for (int j = 0; j < n; j++) {
                tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->sizing.max.kind != TBOX_GRID_FR)
                    t->base = fmin(t->limit, t->base + free / grow);
            }
        }
    /* Find an fr, freezing tracks whose base is larger than their share. */
    double unit = 0;
    if (definite) {
        bool frozen[TBOX_GRID_MAX_TRACKS] = { false };
        for (int pass = 0; pass <= n; pass++) {
            double space = available - tbox_grid_gaps(axis, 0, n), factors = 0;
            for (int j = 0; j < n; j++) {
                tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->sizing.max.kind == TBOX_GRID_FR && !frozen[j] && !t->collapsed)
                    factors += t->sizing.max.flex;
                else
                    space -= t->base;
            }
            unit         = fmax(0, space) / fmax(1, factors);
            bool changed = false;
            for (int j = 0; j < n; j++) {
                const tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->sizing.max.kind == TBOX_GRID_FR && !frozen[j] && t->base > unit * t->sizing.max.flex) {
                    frozen[j] = true;
                    changed   = true;
                }
            }
            if (!changed)
                break;
        }
    } else {
        for (int j = 0; j < n; j++) {
            const tbox_layout_grid_track *t = &axis->tracks[j];
            if (t->sizing.max.kind == TBOX_GRID_FR)
                unit = fmax(unit, t->base / fmax(1, t->sizing.max.flex));
        }
        for (size_t i = 0; i < count; i++) {
            const tbox_layout_grid_item *item = &items[i];
            if (item->out_of_flow)
                continue;
            int start = item->start[dimension] - axis->first, span = item->span[dimension];
            double space = item->max[dimension] - tbox_grid_gaps(axis, start, span), factors = 0;
            for (int j = start; j < start + span; j++) {
                const tbox_layout_grid_track *t = &axis->tracks[j];
                if (t->sizing.max.kind == TBOX_GRID_FR)
                    factors += t->sizing.max.flex;
                else
                    space -= t->base;
            }
            if (factors > 0)
                unit = fmax(unit, space / fmax(1, factors));
        }
    }
    for (int j = 0; j < n; j++) {
        tbox_layout_grid_track *t = &axis->tracks[j];
        if (!t->collapsed && t->sizing.max.kind == TBOX_GRID_FR)
            t->base = fmax(t->base, unit * t->sizing.max.flex);
    }
    double free = definite ? available - tbox_grid_axis_sum(axis) : 0;
    if (free > 0 && (alignment == TBOX_STYLE_FLEX_JUSTIFY_NORMAL || alignment == TBOX_STYLE_FLEX_JUSTIFY_STRETCH)) {
        int grow = 0;
        for (int j = 0; j < n; j++)
            if (!axis->tracks[j].collapsed && axis->tracks[j].sizing.max.kind == TBOX_GRID_AUTO)
                grow++;
        for (int j = 0; j < n; j++)
            if (!axis->tracks[j].collapsed && axis->tracks[j].sizing.max.kind == TBOX_GRID_AUTO && grow > 0)
                axis->tracks[j].base += free / grow;
    }
    axis->extent = tbox_grid_axis_sum(axis);
}

static void tbox_grid_align_tracks(tbox_layout_grid_axis *axis, double available, tbox_style_flex_justify align) {
    int n = axis->end - axis->first, visible = 0;
    for (int i = 0; i < n; i++)
        if (!axis->tracks[i].collapsed)
            visible++;
    double free = available - axis->extent, offset = 0, extra = 0;
    if (align == TBOX_STYLE_FLEX_JUSTIFY_END)
        offset = free;
    else if (align == TBOX_STYLE_FLEX_JUSTIFY_CENTER)
        offset = free / 2;
    else if (free > 0 && visible > 0) {
        if (align == TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN && visible > 1)
            extra = free / (visible - 1);
        else if (align == TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND) {
            extra  = free / visible;
            offset = extra / 2;
        } else if (align == TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY) {
            extra  = free / (visible + 1);
            offset = extra;
        }
    }
    bool previous = false;
    for (int i = 0; i < n; i++) {
        tbox_layout_grid_track *t = &axis->tracks[i];
        if (!t->collapsed && previous)
            offset += axis->gap + extra;
        t->position = offset;
        offset += t->base;
        if (!t->collapsed)
            previous = true;
    }
}

static double tbox_grid_area_size(const tbox_layout_grid_axis *axis, int start, int span) {
    int first = start - axis->first, last = first + span - 1;
    return axis->tracks[last].position + axis->tracks[last].base - axis->tracks[first].position;
}

static tbox_style_flex_align tbox_grid_item_align(const tbox_style *container, const tbox_layout_grid_item *item, int d) {
    tbox_style_flex_align align = item->anonymous ? TBOX_STYLE_FLEX_ALIGN_NORMAL : d == 0 ? item->style->justify_self : item->style->align_self;
    if (align == TBOX_STYLE_FLEX_ALIGN_NORMAL)
        align = d == 0 ? container->justify_items : container->align_items;
    if (align == TBOX_STYLE_FLEX_ALIGN_NORMAL)
        return !item->anonymous && (item->style->aspect_ratio > 0 || tbox_layout_is_replaced_image(item->node) || tbox_layout_tag(item->node, "svg")) ? TBOX_STYLE_FLEX_ALIGN_START : TBOX_STYLE_FLEX_ALIGN_STRETCH;
    return align;
}

static double tbox_grid_item_width(tbox_arena *arena, const tbox_layout_grid_item *item, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double available) {
    if (item->anonymous)
        return available;
    const tbox_style *s = item->style;
    double edges        = tbox_layout_resolve_edge(s->padding[1], available) + tbox_layout_resolve_edge(s->padding[3], available) + tbox_style_border_side_width(s, 1) + tbox_style_border_side_width(s, 3);
    double margins      = tbox_layout_resolve_edge(s->margin[1], available) + tbox_layout_resolve_edge(s->margin[3], available);
    double width        = fmax(0, available - margins - edges);
    if (s->width.kind != TBOX_STYLE_LENGTH_AUTO) {
        width = tbox_style_length_resolve(s->width, available) - (s->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? edges : 0);
    } else if (tbox_grid_item_align(container_style, item, 0) != TBOX_STYLE_FLEX_ALIGN_STRETCH || s->width_keyword != TBOX_STYLE_SIZE_KEYWORD_NONE || s->margin[1].kind == TBOX_STYLE_LENGTH_AUTO || s->margin[3].kind == TBOX_STYLE_LENGTH_AUTO) {
        tbox_layout_intrinsic intrinsic = tbox_layout_intrinsic_outer(arena, item->node, styles, fonts, images);
        double minimum = fmax(0, intrinsic.min - margins - edges), maximum = fmax(0, intrinsic.max - margins - edges);
        width = s->width_keyword == TBOX_STYLE_SIZE_KEYWORD_MIN_CONTENT ? minimum : s->width_keyword == TBOX_STYLE_SIZE_KEYWORD_MAX_CONTENT ? maximum : fmax(minimum, fmin(width, maximum));
    }
    width = tbox_layout_constrain_width(s, fmax(0, width), available, edges);
    return width + edges;
}

static tbox_layout_box *tbox_grid_build_item(tbox_arena *arena, const tbox_layout_grid_item *item, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double x, double y, double width, double height, double area_width, double area_height, bool definite, tbox_layout_positioned_context context) {
    if (item->anonymous)
        return tbox_layout_build_anonymous_box(arena, item->node, item->run_end, container_style, styles, fonts, images, x, y, width, &context, NULL);
    tbox_layout_forced_size forced         = { true, height >= 0, width, height };
    tbox_layout_containing_block container = { x, y, area_width, area_height, definite };
    return tbox_layout_build_element_sized(arena, item->node, styles, fonts, images, container, y, context, NULL, 0, &forced);
}

static void tbox_grid_baseline(const tbox_layout_box *box, double *top, double *baseline) {
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->image == NULL && run->font != NULL && run->rect.y < *top) {
            *top      = run->rect.y;
            *baseline = run->rect.y + tbox_font_face_ascent(run->font);
        }
    }
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        tbox_grid_baseline(child, top, baseline);
}

static double tbox_grid_item_shift(const tbox_layout_grid_item *item, int dimension, double available, double outer, tbox_style_flex_align align, bool rtl) {
    if (item->anonymous)
        return 0;
    int start = dimension == 0 ? 3 : 0, end = dimension == 0 ? 1 : 2;
    double free     = available - outer;
    bool auto_start = item->style->margin[start].kind == TBOX_STYLE_LENGTH_AUTO;
    bool auto_end   = item->style->margin[end].kind == TBOX_STYLE_LENGTH_AUTO;
    if (auto_start || auto_end)
        return free > 0 ? auto_start ? free / (auto_end ? 2 : 1) : 0 : 0;
    if (dimension == 0 && rtl) {
        if (align == TBOX_STYLE_FLEX_ALIGN_START || align == TBOX_STYLE_FLEX_ALIGN_STRETCH)
            align = TBOX_STYLE_FLEX_ALIGN_END;
        else if (align == TBOX_STYLE_FLEX_ALIGN_END)
            align = TBOX_STYLE_FLEX_ALIGN_START;
    }
    return align == TBOX_STYLE_FLEX_ALIGN_END ? free : align == TBOX_STYLE_FLEX_ALIGN_CENTER ? free / 2 : 0;
}

static void tbox_grid_append_box(tbox_layout_box *parent, tbox_layout_box *child) {
    child->parent = parent;
    if (parent->last_child != NULL)
        parent->last_child->next_sibling = child;
    else
        parent->first_child = child;
    parent->last_child = child;
}

static double tbox_grid_automatic_min(const tbox_layout_grid_item *item, int dimension, double intrinsic) {
    if (item->anonymous)
        return intrinsic;
    const tbox_style *s          = item->style;
    tbox_style_length preferred  = dimension == 0 ? s->width : s->height;
    tbox_style_length minimum    = dimension == 0 ? s->min_width : s->min_height;
    tbox_style_overflow overflow = dimension == 0 ? s->overflow_x : s->overflow_y;
    if (preferred.kind != TBOX_STYLE_LENGTH_AUTO || (minimum.kind == TBOX_STYLE_LENGTH_AUTO && overflow == TBOX_STYLE_OVERFLOW_Y_VISIBLE))
        return intrinsic;
    int start = dimension == 0 ? 3 : 0, end = dimension == 0 ? 1 : 2;
    double edges = tbox_layout_resolve_edge(s->padding[start], 0) + tbox_layout_resolve_edge(s->padding[end], 0) + tbox_style_border_side_width(s, (size_t)start) + tbox_style_border_side_width(s, (size_t)end);
    double size  = minimum.kind == TBOX_STYLE_LENGTH_PX ? minimum.value : 0;
    if (s->box_sizing != TBOX_STYLE_BOX_SIZING_BORDER_BOX)
        size += edges;
    return fmax(edges, size) + tbox_layout_resolve_edge(s->margin[start], 0) + tbox_layout_resolve_edge(s->margin[end], 0);
}

static tbox_layout_grid_item *tbox_grid_collect(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, const tbox_layout_grid_axis axes[2], const tbox_grid_areas *areas, double content_width, size_t *out_count) {
    tbox_vector vector;
    tbox_vector_init(&vector, arena, sizeof(tbox_layout_grid_item), 0);
    for (const tbox_html_node *child = node->first_child; child != NULL;) {
        const tbox_html_node *end = child->next_sibling;
        bool anonymous = child->type == TBOX_HTML_NODE_TEXT, visible = false;
        const tbox_style *s = style;
        if (anonymous) {
            end = child;
            while (end != NULL && end->type != TBOX_HTML_NODE_ELEMENT) {
                if (end->type == TBOX_HTML_NODE_TEXT && tbox_string_collapse_whitespace(arena, end->text.text).size > 0)
                    visible = true;
                end = end->next_sibling;
            }
        } else if (child->type == TBOX_HTML_NODE_ELEMENT && !tbox_layout_is_hidden_input(child)) {
            s       = tbox_layout_style_or_default(styles, child);
            visible = s->display != TBOX_STYLE_DISPLAY_NONE;
        }
        if (visible) {
            tbox_layout_grid_item *item = tbox_vector_push(&vector);
            memset(item, 0, sizeof(*item));
            item->node        = child;
            item->run_end     = end;
            item->style       = s;
            item->anonymous   = anonymous;
            item->order       = anonymous ? 0 : s->order;
            item->out_of_flow = !anonymous && (s->position == TBOX_STYLE_POSITION_ABSOLUTE || s->position == TBOX_STYLE_POSITION_FIXED);
            if (item->out_of_flow)
                item->order = 0;
            tbox_grid_resolve_item(item, axes, areas);
            tbox_layout_intrinsic intrinsic;
            if (anonymous) {
                tbox_vector words;
                tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
                tbox_layout_collect_words(arena, child, end, style, styles, fonts, images, content_width, &words);
                intrinsic = tbox_layout_measure_words(words.data, words.length, tbox_layout_white_space_nowrap(style));
            } else
                intrinsic = tbox_layout_intrinsic_outer(arena, child, styles, fonts, images);
            item->min[0]      = intrinsic.min;
            item->max[0]      = intrinsic.max;
            item->auto_min[0] = tbox_grid_automatic_min(item, 0, intrinsic.min);
        }
        child = end;
    }
    tbox_layout_grid_item *items = vector.data;
    size_t count                 = vector.length;
    *out_count                   = count;
    for (size_t i = 1; i < count; i++) {
        tbox_layout_grid_item key = items[i];
        size_t j                  = i;
        while (j > 0 && items[j - 1].order > key.order) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
    return items;
}

static tbox_style_units tbox_grid_units(const tbox_style *style, int index, tbox_style_units fallback) {
    const tbox_style_grid_units *units = &style->grid_units[index];
    if (units->font_size > 0)
        return (tbox_style_units){ units->font_size, units->root_font_size, units->viewport_width, units->viewport_height, style->direction_rtl, -1.0, -1.0, tbox_style_zoom(style), TBOX_STYLE_FRAME_NONE, style->ex_ratio, style->ch_ratio };
    return fallback;
}

double tbox_layout_build_grid(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double content_width, double content_height, bool height_definite, double min_height, double max_height, tbox_layout_box *box, tbox_layout_positioned_context context) {
    tbox_layout_grid_axis *axes = tbox_arena_alloc_zero(arena, sizeof(tbox_layout_grid_axis) * 2);
    tbox_style_units units      = { style->font_size, style->root_font_size, context.viewport.width, context.viewport.height, style->direction_rtl, -1.0, -1.0, tbox_style_zoom(style), TBOX_STYLE_FRAME_NONE, style->ex_ratio, style->ch_ratio };
    tbox_style_units track_units[4];
    for (int i = 0; i < 4; i++)
        track_units[i] = tbox_grid_units(style, i, units);
    axes[0].gap = tbox_layout_resolve_edge(style->column_gap, content_width);
    axes[1].gap = style->row_gap.kind == TBOX_STYLE_LENGTH_PERCENT && !height_definite ? 0 : tbox_layout_resolve_edge(style->row_gap, content_height);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_columns), &track_units[0], content_width, axes[0].gap, true, false, &axes[0].explicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_rows), &track_units[1], content_height, axes[1].gap, height_definite, false, &axes[1].explicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_auto_columns), &track_units[2], 0, 0, false, true, &axes[0].implicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_auto_rows), &track_units[3], 0, 0, false, true, &axes[1].implicit_tracks);
    tbox_grid_areas areas;
    tbox_style_grid_parse_areas(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_areas), &areas);
    for (int d = 0; d < 2; d++) {
        axes[d].explicit_count = axes[d].explicit_tracks.count;
        int area_count         = d == 0 ? areas.columns : areas.rows;
        if (axes[d].explicit_count < area_count)
            axes[d].explicit_count = area_count;
        axes[d].end = axes[d].explicit_count;
    }
    size_t count;
    tbox_layout_grid_item *items = tbox_grid_collect(arena, node, style, styles, fonts, images, axes, &areas, content_width, &count);
    unsigned char *cells         = tbox_arena_alloc_zero(arena, TBOX_GRID_MAX_TRACKS * TBOX_GRID_MAX_TRACKS);
    tbox_grid_place(items, count, style, axes, cells);
    tbox_grid_size(&axes[0], items, count, 0, content_width, true, style->justify_content);
    tbox_grid_align_tracks(&axes[0], content_width, style->justify_content);
    double baselines[TBOX_GRID_MAX_TRACKS] = { 0 };
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (item->out_of_flow)
            continue;
        double area_width = tbox_grid_area_size(&axes[0], item->start[0], item->span[0]);
        double width      = tbox_grid_item_width(arena, item, style, styles, fonts, images, area_width);
        item->box         = tbox_grid_build_item(arena, item, style, styles, fonts, images, 0, 0, width, -1, area_width, 0, false, context);
        item->min[1] = item->max[1] = item->box->margin_box.height;
        item->auto_min[1]           = tbox_grid_automatic_min(item, 1, item->min[1]);
        double top = TBOX_GRID_INFINITY, baseline = item->box->border_box.y + item->box->border_box.height;
        tbox_grid_baseline(item->box, &top, &baseline);
        item->baseline = baseline - item->box->margin_box.y;
        int row        = item->start[1] - axes[1].first;
        if (tbox_grid_item_align(style, item, 1) == TBOX_STYLE_FLEX_ALIGN_BASELINE && item->baseline > baselines[row])
            baselines[row] = item->baseline;
    }
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (!item->out_of_flow && tbox_grid_item_align(style, item, 1) == TBOX_STYLE_FLEX_ALIGN_BASELINE) {
            double extra = baselines[item->start[1] - axes[1].first] - item->baseline;
            item->min[1] += extra;
            item->auto_min[1] += extra;
            item->max[1] += extra;
        }
    }
    tbox_grid_size(&axes[1], items, count, 1, content_height, height_definite, style->align_content);
    double extent = height_definite ? content_height : fmax(min_height, fmin(max_height, axes[1].extent));
    if (!height_definite && style->row_gap.kind == TBOX_STYLE_LENGTH_PERCENT)
        axes[1].gap = tbox_style_length_resolve(style->row_gap, extent);
    if (!height_definite)
        tbox_grid_size(&axes[1], items, count, 1, extent, true, style->align_content);
    tbox_grid_align_tracks(&axes[1], extent, style->align_content);
    if (style->position != TBOX_STYLE_POSITION_STATIC)
        context.nearest_ancestor.height += extent - content_height;
    double overflow = axes[1].extent;
    for (size_t i = 0; i < count; i++) {
        tbox_layout_grid_item *item = &items[i];
        if (item->out_of_flow) {
            tbox_rect basis = item->style->position == TBOX_STYLE_POSITION_FIXED ? context.viewport : context.nearest_ancestor;
            if (item->style->position == TBOX_STYLE_POSITION_ABSOLUTE && style->position != TBOX_STYLE_POSITION_STATIC) {
                for (int d = 0; d < 2; d++) {
                    int start = item->start[d];
                    if (start < axes[d].first || start + item->span[d] > axes[d].end)
                        continue;
                    double pos  = axes[d].tracks[start - axes[d].first].position;
                    double size = tbox_grid_area_size(&axes[d], start, item->span[d]);
                    if (d == 0) {
                        basis.x     = content_x + (style->direction_rtl ? content_width - pos - size : pos);
                        basis.width = size;
                    } else {
                        basis.y      = content_y + pos;
                        basis.height = size;
                    }
                }
            }
            tbox_layout_containing_block container = { basis.x, basis.y, basis.width, basis.height, true };
            tbox_grid_append_box(box, tbox_layout_build_element(arena, item->node, styles, fonts, images, container, basis.y, context, NULL, 0));
            continue;
        }
        double area_width  = tbox_grid_area_size(&axes[0], item->start[0], item->span[0]);
        double area_height = tbox_grid_area_size(&axes[1], item->start[1], item->span[1]);
        double x           = axes[0].tracks[item->start[0] - axes[0].first].position;
        if (style->direction_rtl)
            x = content_width - x - area_width;
        x += content_x;
        double y      = content_y + axes[1].tracks[item->start[1] - axes[1].first].position;
        double width  = tbox_grid_item_width(arena, item, style, styles, fonts, images, area_width);
        double height = -1;
        if (!item->anonymous) {
            const tbox_style *s = item->style;
            double edges        = tbox_layout_resolve_edge(s->padding[0], area_width) + tbox_layout_resolve_edge(s->padding[2], area_width) + tbox_style_border_side_width(s, 0) + tbox_style_border_side_width(s, 2);
            double margins      = tbox_layout_resolve_edge(s->margin[0], area_width) + tbox_layout_resolve_edge(s->margin[2], area_width);
            if (s->height.kind == TBOX_STYLE_LENGTH_AUTO && s->height_keyword == TBOX_STYLE_SIZE_KEYWORD_NONE && tbox_grid_item_align(style, item, 1) == TBOX_STYLE_FLEX_ALIGN_STRETCH && s->margin[0].kind != TBOX_STYLE_LENGTH_AUTO && s->margin[2].kind != TBOX_STYLE_LENGTH_AUTO) {
                height = tbox_layout_constrain_height(s, fmax(0, area_height - margins - edges), area_height, true, edges) + edges;
            }
        }
        tbox_layout_box *child = tbox_grid_build_item(arena, item, style, styles, fonts, images, x, y, width, height, area_width, area_height, true, context);
        double dx              = tbox_grid_item_shift(item, 0, area_width, child->margin_box.width, tbox_grid_item_align(style, item, 0), style->direction_rtl);
        double dy              = tbox_grid_item_shift(item, 1, area_height, child->margin_box.height, tbox_grid_item_align(style, item, 1), false);
        if (tbox_grid_item_align(style, item, 1) == TBOX_STYLE_FLEX_ALIGN_BASELINE && !item->anonymous && item->style->margin[0].kind != TBOX_STYLE_LENGTH_AUTO && item->style->margin[2].kind != TBOX_STYLE_LENGTH_AUTO)
            dy = baselines[item->start[1] - axes[1].first] - item->baseline;
        if ((dx != 0 || dy != 0) && !item->anonymous && tbox_layout_has_escaping_positioned(item->node, styles, item->style->position != TBOX_STYLE_POSITION_STATIC)) {
            child = tbox_grid_build_item(arena, item, style, styles, fonts, images, x + dx, y + dy, width, height, area_width, area_height, true, context);
        } else
            tbox_layout_translate(child, dx, dy);
        if (child->margin_box.y + child->margin_box.height - content_y > overflow)
            overflow = child->margin_box.y + child->margin_box.height - content_y;
        tbox_grid_append_box(box, child);
    }
    box->scroll_content_height = overflow;
    return extent;
}

/* Intrinsic widths for inline-grid and grids nested in shrink-to-fit boxes. */
tbox_layout_intrinsic tbox_layout_grid_intrinsic(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    const tbox_style *style     = tbox_layout_style_or_default(styles, node);
    tbox_layout_grid_axis *axes = tbox_arena_alloc_zero(arena, sizeof(tbox_layout_grid_axis) * 2);
    tbox_style_units units      = { style->font_size, style->root_font_size, 0, 0, style->direction_rtl, -1.0, -1.0, tbox_style_zoom(style), TBOX_STYLE_FRAME_NONE, style->ex_ratio, style->ch_ratio };
    tbox_style_units track_units[4];
    for (int i = 0; i < 4; i++)
        track_units[i] = tbox_grid_units(style, i, units);
    axes[0].gap = style->column_gap.kind == TBOX_STYLE_LENGTH_PX ? style->column_gap.value : 0;
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_columns), &track_units[0], 0, 0, false, false, &axes[0].explicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_rows), &track_units[1], 0, 0, false, false, &axes[1].explicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_auto_columns), &track_units[2], 0, 0, false, true, &axes[0].implicit_tracks);
    tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_auto_rows), &track_units[3], 0, 0, false, true, &axes[1].implicit_tracks);
    tbox_grid_areas areas;
    tbox_style_grid_parse_areas(tbox_string_view_from_cstr(tbox_style_grid_templates_of(style)->grid_template_areas), &areas);
    for (int d = 0; d < 2; d++) {
        axes[d].explicit_count = axes[d].explicit_tracks.count;
        int area_count         = d == 0 ? areas.columns : areas.rows;
        if (axes[d].explicit_count < area_count)
            axes[d].explicit_count = area_count;
        axes[d].end = axes[d].explicit_count;
    }
    size_t count;
    tbox_layout_grid_item *items = tbox_grid_collect(arena, node, style, styles, fonts, images, axes, &areas, 0, &count);
    unsigned char *cells         = tbox_arena_alloc_zero(arena, TBOX_GRID_MAX_TRACKS * TBOX_GRID_MAX_TRACKS);
    tbox_grid_place(items, count, style, axes, cells);
    tbox_grid_size(&axes[0], items, count, 0, 0, false, TBOX_STYLE_FLEX_JUSTIFY_START);
    tbox_layout_intrinsic result = { 0, axes[0].extent };
    memset(axes[0].tracks, 0, sizeof(axes[0].tracks));
    for (size_t i = 0; i < count; i++)
        items[i].max[0] = items[i].min[0];
    tbox_grid_size(&axes[0], items, count, 0, 0, false, TBOX_STYLE_FLEX_JUSTIFY_START);
    result.min = axes[0].extent;
    return result;
}
