#include "tbox_layout_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>

enum { TBOX_MULTICOL_MAX_COLUMNS = 4096 };

/* A measured vertical flow is partitioned at legal line/block boundaries.
 * Keep avoid intervals separate from hard (line/atomic) intervals: an avoid
 * box taller than the fragmentainer must still be allowed to fragment. */
typedef struct tbox_multicol_interval {
    double start, end;
    bool avoid;
} tbox_multicol_interval;

typedef struct tbox_multicol_band {
    double start, end;
} tbox_multicol_band;

bool tbox_layout_is_multicol(const tbox_style *style) {
    return style != NULL && (style->column_count > 0 || style->column_width.kind == TBOX_STYLE_LENGTH_PX) &&
        (style->display == TBOX_STYLE_DISPLAY_BLOCK || style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK);
}

static bool tbox_multicol_out_of_flow(const tbox_layout_box *box) {
    return box->style != NULL && (box->style->position == TBOX_STYLE_POSITION_ABSOLUTE || box->style->position == TBOX_STYLE_POSITION_FIXED);
}

static void tbox_multicol_interval_push(tbox_vector *intervals, double start, double end, bool avoid) {
    if (end <= start) return;
    tbox_multicol_interval *interval = tbox_vector_push(intervals);
    if (interval != NULL) *interval = (tbox_multicol_interval){ start, end, avoid };
}

static void tbox_multicol_forced_push(tbox_vector *forced, double y) {
    double *point = tbox_vector_push(forced);
    if (point != NULL) *point = y;
}

static void tbox_multicol_collect_flow(const tbox_layout_box *first, const tbox_layout_box *stop, tbox_vector *intervals, tbox_vector *forced, tbox_vector *margins);

static void tbox_multicol_collect(const tbox_layout_box *box, tbox_vector *intervals, tbox_vector *forced, tbox_vector *margins) {
    if (tbox_multicol_out_of_flow(box)) return;
    const tbox_style *style = box->style;
    if (style != NULL && style->break_before == TBOX_STYLE_COLUMN_BREAK_COLUMN)
        tbox_multicol_forced_push(forced, box->border_box.y);
    if (style != NULL && style->break_after == TBOX_STYLE_COLUMN_BREAK_COLUMN)
        tbox_multicol_forced_push(forced, box->border_box.y + box->border_box.height);
    if (style != NULL && style->break_inside == TBOX_STYLE_COLUMN_BREAK_AVOID)
        tbox_multicol_interval_push(intervals, box->border_box.y, box->border_box.y + box->border_box.height, true);
    bool atomic = box->column_count > 0 || (style != NULL &&
        (style->display == TBOX_STYLE_DISPLAY_FLEX || style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX ||
         style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK)) || tbox_layout_tag(box->node, "table") || tbox_layout_is_replaced_image(box->node);
    if (atomic) {
        tbox_multicol_interval_push(intervals, box->border_box.y, box->border_box.y + box->border_box.height, false);
        return;
    }
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        double y = run->line_height > 0.0 ? run->line_y : run->rect.y;
        double height = run->line_height > 0.0 ? run->line_height : run->rect.height;
        tbox_multicol_interval_push(intervals, y, y + height, false);
    }
    tbox_multicol_collect_flow(box->first_child, NULL, intervals, forced, margins);
}

static void tbox_multicol_margin_push(tbox_vector *margins, double start, double end) {
    if (end <= start) return;
    tbox_multicol_band *margin = tbox_vector_push(margins);
    if (margin != NULL) *margin = (tbox_multicol_band){ start, end };
}

/* Effective gaps between sibling border boxes already include collapsing.
 * Do not use each sibling's full declared margin independently: a negative
 * adjoining margin can make that rectangle overlap the next box's content. */
static void tbox_multicol_collect_flow(const tbox_layout_box *first, const tbox_layout_box *stop, tbox_vector *intervals, tbox_vector *forced, tbox_vector *margins) {
    const tbox_layout_box *previous = NULL;
    for (const tbox_layout_box *child = first; child != stop; child = child->next_sibling) {
        if (tbox_multicol_out_of_flow(child)) continue;
        if (previous == NULL) {
            tbox_multicol_margin_push(margins, child->margin_box.y, child->border_box.y);
        } else {
            double bottom = previous->border_box.y + previous->border_box.height;
            bool has_margin = child->border_box.y > child->margin_box.y ||
                previous->margin_box.y + previous->margin_box.height > bottom;
            if (has_margin) tbox_multicol_margin_push(margins, bottom, child->border_box.y);
        }
        tbox_multicol_collect(child, intervals, forced, margins);
        previous = child;
    }
    if (previous != NULL)
        tbox_multicol_margin_push(margins, previous->border_box.y + previous->border_box.height,
            previous->margin_box.y + previous->margin_box.height);
}

static int tbox_multicol_interval_compare(const void *a, const void *b) {
    const tbox_multicol_interval *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

/* Greedy fill shared by balancing trials and the final fragmentation pass.
 * No allocations during trials. A too-tall line/atomic box overflows a column
 * rather than being lost or causing an endless sequence of empty columns. */
static size_t tbox_multicol_partition(double start, double end, double height, const tbox_vector *intervals, const tbox_vector *forced, const tbox_vector *margins, size_t limit, tbox_vector *bands) {
    const tbox_multicol_interval *items = intervals->data;
    const double *points = forced->data;
    const tbox_multicol_band *gaps = margins->data;
    size_t count = 0;
    while (start < end - 1e-7 && count < limit) {
        double cut = fmin(end, start + height);
        bool forced_cut = false;
        for (size_t i = 0; i < forced->length; i++)
            if (points[i] > start + 1e-7 && points[i] < cut) cut = points[i], forced_cut = true;
        /* Walking backwards prevents a cut moved into an earlier overlapping
         * interval (mixed-face runs on one line) from splitting that line. */
        for (size_t i = intervals->length; i > 0; i--) {
            const tbox_multicol_interval *interval = &items[i - 1];
            if (interval->avoid && (forced_cut || interval->end - interval->start > height + 1e-7)) continue;
            if (interval->start < cut - 1e-7 && interval->end > cut + 1e-7) {
                if (interval->start > start + 1e-7) cut = interval->start;
                else if (!interval->avoid) cut = fmin(end, interval->end);
            }
        }
        if (cut <= start + 1e-7) cut = fmin(end, start + fmax(height, 1.0));
        double next = cut;
        if (!forced_cut && cut < end - 1e-7) {
            double boundary = cut;
            for (size_t i = 0; i < margins->length; i++) {
                if (gaps[i].start < start + 1e-7 || gaps[i].start > boundary + 1e-7 || gaps[i].end < boundary - 1e-7) continue;
                cut = fmin(cut, gaps[i].start);
                next = fmax(next, fmin(end, gaps[i].end));
            }
        }
        /* Bound memory for extreme CSS. The last fragment keeps the rest
         * of the flow as vertical overflow, including all its text. */
        if (bands != NULL && count + 1 == limit) cut = next = end;
        if (bands != NULL) {
            tbox_multicol_band *band = tbox_vector_push(bands);
            if (band == NULL) break;
            *band = (tbox_multicol_band){ start, cut };
        }
        count++;
        start = next;
    }
    return start < end - 1e-7 ? limit + 1 : count;
}

static tbox_rect tbox_multicol_slice_rect(tbox_rect rect, double start, double end) {
    double top = fmax(rect.y, start), bottom = fmin(rect.y + rect.height, end);
    rect.y = top;
    rect.height = fmax(0.0, bottom - top);
    return rect;
}

static void tbox_multicol_append(tbox_layout_box *parent, tbox_layout_box *child) {
    if (child == NULL) return;
    child->parent = parent;
    child->next_sibling = NULL;
    if (parent->last_child != NULL) parent->last_child->next_sibling = child;
    else parent->first_child = child;
    parent->last_child = child;
}

/* Clone only the portion in this column, keeping the node/style ownership
 * in the source arenas. Runs and mutable geometry belong to the new box.
 * Sliced decorations suppress only the block-start/end edges at a break. */
static tbox_layout_box *tbox_multicol_fragment(tbox_arena *arena, const tbox_layout_box *source, double start, double end, bool first) {
    bool positioned = tbox_multicol_out_of_flow(source);
    if (positioned && !first) return NULL;
    if (positioned) start = -TBOX_LAYOUT_FLEX_INFINITE, end = TBOX_LAYOUT_FLEX_INFINITE;
    double bottom = source->border_box.y + source->border_box.height;
    bool intersects = source->border_box.y < end - 1e-7 && bottom > start + 1e-7;
    tbox_layout_box *box = tbox_arena_alloc(arena, sizeof(*box));
    if (box == NULL) return NULL;
    *box = *source;
    box->parent = box->first_child = box->last_child = box->next_sibling = NULL;
    box->text_runs = NULL;
    box->text_run_count = 0;
    tbox_vector runs;
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 0);
    for (size_t i = 0; i < source->text_run_count; i++) {
        const tbox_layout_text_run *run = &source->text_runs[i];
        double line_y = run->line_height > 0.0 ? run->line_y : run->rect.y;
        if (line_y < start - 1e-7 || line_y >= end - 1e-7) continue;
        tbox_layout_text_run *copy = tbox_vector_push(&runs);
        if (copy != NULL) *copy = *run;
    }
    box->text_runs = runs.data;
    box->text_run_count = runs.length;
    for (const tbox_layout_box *child = source->first_child; child != NULL; child = child->next_sibling)
        tbox_multicol_append(box, tbox_multicol_fragment(arena, child, start, end, first));
    if (!intersects && box->first_child == NULL && box->text_run_count == 0) return NULL;
    box->margin_box = tbox_multicol_slice_rect(source->margin_box, start, end);
    box->border_box = tbox_multicol_slice_rect(source->border_box, start, end);
    box->padding_box = tbox_multicol_slice_rect(source->padding_box, start, end);
    box->content_box = tbox_multicol_slice_rect(source->content_box, start, end);
    bool slice_top = source->border_box.y < start - 1e-7, slice_bottom = bottom > end + 1e-7;
    if ((slice_top || slice_bottom) && source->style != NULL) {
        tbox_style *style = tbox_arena_alloc(arena, sizeof(*style));
        if (style != NULL) {
            *style = *source->style;
            for (size_t i = 0; i < 4; i++) {
                style->border_widths[i] = tbox_style_border_side_width(source->style, i);
                style->border_styles[i] = tbox_style_border_side_style(source->style, i);
                style->border_colors[i] = tbox_style_border_side_color(source->style, i);
            }
            style->border_per_side = true;
            if (slice_top) style->border_widths[0] = 0.0;
            if (slice_bottom) style->border_widths[2] = 0.0;
            box->style = style;
        }
    }
    /* These arrays are mutable under tbox_layout_translate. */
    if (source->column_rule_count > 0) {
        box->column_rules = tbox_arena_alloc(arena, source->column_rule_count * sizeof(tbox_rect));
        if (box->column_rules != NULL)
            for (size_t i = 0; i < source->column_rule_count; i++) box->column_rules[i] = source->column_rules[i];
        else box->column_rule_count = 0;
    }
    if (source->table_edge_count > 0) {
        box->table_edges = tbox_arena_alloc(arena, source->table_edge_count * sizeof(tbox_table_edge));
        if (box->table_edges != NULL)
            for (size_t i = 0; i < source->table_edge_count; i++) box->table_edges[i] = source->table_edges[i];
        else box->table_edge_count = 0;
    }
    return box;
}

static double tbox_multicol_set(tbox_arena *arena, const tbox_layout_box *first, const tbox_layout_box *stop, double start, double end, double y, double available_height, bool balance, const tbox_style *style, tbox_layout_box *box, tbox_vector *rules) {
    if (end <= start + 1e-7) {
        for (const tbox_layout_box *child = first; child != stop; child = child->next_sibling)
            if (tbox_multicol_out_of_flow(child))
                tbox_multicol_append(box, tbox_multicol_fragment(arena, child, start, end, true));
        return 0.0;
    }
    tbox_vector intervals, forced, margins, bands;
    tbox_vector_init(&intervals, arena, sizeof(tbox_multicol_interval), 0);
    tbox_vector_init(&forced, arena, sizeof(double), 0);
    tbox_vector_init(&margins, arena, sizeof(tbox_multicol_band), 0);
    tbox_vector_init(&bands, arena, sizeof(tbox_multicol_band), 0);
    tbox_multicol_collect_flow(first, stop, &intervals, &forced, &margins);
    if (intervals.length > 1) qsort(intervals.data, intervals.length, sizeof(tbox_multicol_interval), tbox_multicol_interval_compare);
    double height = available_height;
    if (balance) {
        double low = 0.0, high = end - start;
        const tbox_multicol_interval *items = intervals.data;
        for (size_t i = 0; i < intervals.length; i++) {
            double size = items[i].end - items[i].start;
            if (!items[i].avoid || available_height < 0.0 || size <= available_height)
                low = fmax(low, size);
        }
        high = fmax(high, low);
        for (int i = 0; i < 48; i++) {
            double trial = (low + high) / 2.0;
            if (tbox_multicol_partition(start, end, trial, &intervals, &forced, &margins, (size_t)box->column_count, NULL) <= (size_t)box->column_count) high = trial;
            else low = trial;
        }
        height = high + 1e-6;
        if (available_height >= 0.0) height = fmin(height, available_height);
    }
    if (height <= 0.0) height = 1.0;
    tbox_multicol_partition(start, end, height, &intervals, &forced, &margins, TBOX_MULTICOL_MAX_COLUMNS, &bands);
    const tbox_multicol_band *slices = bands.data;
    double extent = height;
    for (size_t col = 0; col < bands.length; col++) {
        double index = style->direction_rtl ? (double)box->column_count - 1.0 - (double)col : (double)col;
        double dx = index * (box->column_width + box->column_gap);
        double dy = y - slices[col].start;
        extent = fmax(extent, slices[col].end - slices[col].start);
        for (const tbox_layout_box *child = first; child != stop; child = child->next_sibling) {
            tbox_layout_box *fragment = tbox_multicol_fragment(arena, child, slices[col].start, slices[col].end, col == 0);
            if (fragment != NULL) {
                if (!tbox_multicol_out_of_flow(fragment)) tbox_layout_translate(fragment, dx, dy);
                tbox_multicol_append(box, fragment);
            }
        }
        if (col > 0 && style->column_rule_style != TBOX_STYLE_BORDER_STYLE_NONE && style->column_rule_width > 0.0) {
            double boundary = style->direction_rtl ? index + 1.0 : index;
            tbox_rect *rule = tbox_vector_push(rules);
            if (rule != NULL) *rule = (tbox_rect){ box->content_box.x + boundary * (box->column_width + box->column_gap) - box->column_gap / 2.0 - style->column_rule_width / 2.0, y, style->column_rule_width, height };
        }
    }
    return extent;
}

double tbox_layout_build_multicol(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, tbox_layout_box *box, tbox_layout_positioned_context context) {
    double gap = style->column_gap.kind == TBOX_STYLE_LENGTH_AUTO ? style->font_size : tbox_style_length_resolve(style->column_gap, container.width);
    gap = fmax(0.0, gap);
    int count = style->column_count > 0 ? style->column_count : 1;
    if (style->column_width.kind == TBOX_STYLE_LENGTH_PX) {
        double fit = fmax(1.0, floor((container.width + gap) / (style->column_width.value + gap)));
        int fitting = fit >= INT_MAX ? INT_MAX : (int)fit;
        count = style->column_count > 0 && count < fitting ? count : fitting;
    }
    if (count > TBOX_MULTICOL_MAX_COLUMNS) count = TBOX_MULTICOL_MAX_COLUMNS;
    double width = fmax(0.0, (container.width + gap) / count - gap);
    box->column_count = count;
    box->column_width = width;
    box->column_gap = gap;
    box->content_box = (tbox_rect){ container.x, container.y, container.width, container.height };
    tbox_layout_containing_block narrow = container;
    narrow.width = width;
    tbox_layout_box flow = { 0 };
    double total = tbox_layout_build_children(arena, node, styles, fonts, images, narrow, container.y, &flow, context, style);
    tbox_vector rules;
    tbox_vector_init(&rules, arena, sizeof(tbox_rect), 0);
    const tbox_layout_box *first = flow.first_child;
    double y     = container.y;
    double start = y;
    for (const tbox_layout_box *child = first; child != NULL; child = child->next_sibling) {
        if (child->style == NULL || !child->style->column_span_all || tbox_multicol_out_of_flow(child)) continue;
        double height = tbox_multicol_set(arena, first, child, start, child->margin_box.y, y, -1.0, true, style, box, &rules);
        y += height;
        tbox_layout_box *span = tbox_layout_build_element(arena, child->node, styles, fonts, images, container, y, context, NULL, 0);
        tbox_multicol_append(box, span);
        y += span->margin_box.height;
        start = child->margin_box.y + child->margin_box.height;
        first = child->next_sibling;
    }
    bool balance = !container.height_definite || style->column_fill != TBOX_STYLE_COLUMN_FILL_AUTO;
    double remaining = container.height_definite ? fmax(0.0, container.height - (y - container.y)) : -1.0;
    y += tbox_multicol_set(arena, first, NULL, start, container.y + total, y, remaining, balance, style, box, &rules);
    box->column_rules = rules.data;
    box->column_rule_count = rules.length;
    return y - container.y;
}
