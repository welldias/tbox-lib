#include <tbox/output.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/font.h>

#include "tbox_raster_internal.h"

/* Beyond this fraction of the buffer, repainting regions costs about as much
 * as repainting everything. */
#define TBOX_DAMAGE_FULL_FRACTION 0.6

typedef struct tbox_damage_signature {
    uint64_t hash;
    tbox_rect bounds; /* empty (zero size) when the op paints nothing */
} tbox_damage_signature;

struct tbox_damage_tracker {
    tbox_damage_signature *previous, *current; /* swapped after every update */
    size_t previous_count, capacity;
    int32_t width, height;
    bool valid; /* false until the first update and after invalidate */
};

/* ---- op bounds ---- */

static tbox_rect tbox_damage_intersect(tbox_rect a, tbox_rect b) {
    double x0 = fmax(a.x, b.x), y0 = fmax(a.y, b.y);
    double x1 = fmin(a.x + a.width, b.x + b.width), y1 = fmin(a.y + a.height, b.y + b.height);
    return (tbox_rect){ x0, y0, x1 > x0 ? x1 - x0 : 0.0, y1 > y0 ? y1 - y0 : 0.0 };
}

static bool tbox_damage_empty(tbox_rect r) {
    return r.width <= 0.0 || r.height <= 0.0;
}

/* `r` grown to whole pixels and clipped to the buffer. */
static tbox_rect tbox_damage_snap(tbox_rect r, int32_t width, int32_t height) {
    double x0 = floor(r.x), y0 = floor(r.y), x1 = ceil(r.x + r.width), y1 = ceil(r.y + r.height);
    return tbox_damage_intersect((tbox_rect){ x0, y0, x1 - x0, y1 - y0 }, (tbox_rect){ 0.0, 0.0, (double)width, (double)height });
}

bool tbox_raster_op_paint_bounds(const tbox_paint_op *op, int32_t buffer_width, int32_t buffer_height, tbox_rect *out) {
    tbox_rect r = op->rect;
    switch (op->kind) {
    case TBOX_PAINT_TEXT_RUN: {
        /* Glyphs may overhang the run's line box (italics, descenders,
         * tight line-height, letter-spacing), so pad by half a line. */
        double pad = (op->face != NULL ? tbox_font_face_line_height(op->face) * 0.5 : 0.0) + fabs(op->letter_spacing) + 1.0;
        r = (tbox_rect){ r.x - pad, r.y - pad, r.width + 2.0 * pad, r.height + 2.0 * pad };
        break;
    }
    case TBOX_PAINT_IMAGE:
        /* The destination size is rounded to whole pixels from floor(x). */
        r.width += 1.0, r.height += 1.0;
        break;
    case TBOX_PAINT_WAVY_LINE: {
        double amplitude = r.height > 1.0 ? r.height : 1.0;
        r.height += 2.0 * amplitude;
        break;
    }
    case TBOX_PAINT_BACKDROP_FILTER: {
        /* everything it reads: a blur samples past its edges */
        double extent = tbox_style_filter_extent(op->filters);
        r = (tbox_rect){ r.x - extent, r.y - extent, r.width + 2.0 * extent, r.height + 2.0 * extent };
        break;
    }
    case TBOX_PAINT_LAYER_BEGIN:
    case TBOX_PAINT_LAYER_MASK:
    case TBOX_PAINT_LAYER_END:
        *out = (tbox_rect){ 0.0, 0.0, 0.0, 0.0 }; /* see tbox_raster_layer_content_bounds */
        return false;
    default:
        break;
    }
    if (op->has_clip)
        r = tbox_damage_intersect(r, op->clip);
    if (op->has_rounded_clip)
        r = tbox_damage_intersect(r, op->rounded_clip);
    *out = tbox_damage_empty(r) ? (tbox_rect){ 0.0, 0.0, 0.0, 0.0 } : tbox_damage_snap(r, buffer_width, buffer_height);
    return !tbox_damage_empty(*out);
}

/* ---- compositing layers ---- */

size_t tbox_raster_layer_end(const tbox_display_list *list, size_t begin, size_t *out_mask) {
    size_t depth = 0, mask = SIZE_MAX;
    for (size_t i = begin; i < list->count; i++) {
        tbox_paint_op_kind kind = list->items[i].kind;
        if (kind == TBOX_PAINT_LAYER_BEGIN) {
            depth++;
        } else if (kind == TBOX_PAINT_LAYER_MASK && depth == 1 && mask == SIZE_MAX) {
            mask = i;
        } else if (kind == TBOX_PAINT_LAYER_END && --depth == 0) {
            if (out_mask != NULL) *out_mask = mask != SIZE_MAX ? mask : i;
            return i;
        }
    }
    if (out_mask != NULL) *out_mask = list->count;
    return list->count; /* unterminated: runs to the end */
}

/* What one op inside a layer can touch: its bounds, or a nested layer's
 * content grown by that layer's reach. Sets `*next` past it. */
static bool tbox_raster_layer_item_bounds(const tbox_display_list *list, size_t i, int32_t width, int32_t height, tbox_rect *out, size_t *next) {
    const tbox_paint_op *op = &list->items[i];
    *next = i + 1;
    if (op->kind != TBOX_PAINT_LAYER_BEGIN)
        return tbox_raster_op_paint_bounds(op, width, height, out);
    *next = tbox_raster_layer_end(list, i, NULL) + 1;
    tbox_rect content;
    if (!tbox_raster_layer_content_bounds(list, i, width, height, &content))
        return false;
    double extent = tbox_style_filter_extent(op->filters);
    tbox_rect r = { content.x - extent, content.y - extent, content.width + 2.0 * extent, content.height + 2.0 * extent };
    if (op->has_clip) r = tbox_damage_intersect(r, op->clip);
    if (op->has_rounded_clip) r = tbox_damage_intersect(r, op->rounded_clip);
    *out = tbox_damage_snap(r, width, height);
    return !tbox_damage_empty(*out);
}

bool tbox_raster_layer_content_bounds(const tbox_display_list *list, size_t begin, int32_t width, int32_t height, tbox_rect *out) {
    size_t mask;
    tbox_raster_layer_end(list, begin, &mask);
    bool any = false;
    tbox_rect total = { 0.0, 0.0, 0.0, 0.0 };
    for (size_t i = begin + 1; i < mask;) {
        tbox_rect r;
        size_t next;
        if (tbox_raster_layer_item_bounds(list, i, width, height, &r, &next)) {
            if (!any) {
                total = r;
            } else {
                double x0 = fmin(total.x, r.x), y0 = fmin(total.y, r.y);
                double x1 = fmax(total.x + total.width, r.x + r.width), y1 = fmax(total.y + total.height, r.y + r.height);
                total = (tbox_rect){ x0, y0, x1 - x0, y1 - y0 };
            }
            any = true;
        }
        i = next;
    }
    *out = total;
    return any;
}

/* ---- op hash: FNV-1a over each field, never over struct padding ---- */

static uint64_t tbox_damage_mix(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t tbox_damage_mix_double(uint64_t hash, double value) {
    if (value == 0.0)
        value = 0.0; /* -0.0 paints like 0.0 */
    return tbox_damage_mix(hash, &value, sizeof(value));
}

static uint64_t tbox_damage_mix_int(uint64_t hash, long long value) {
    return tbox_damage_mix(hash, &value, sizeof(value));
}

static uint64_t tbox_damage_mix_rect(uint64_t hash, tbox_rect r) {
    hash = tbox_damage_mix_double(hash, r.x);
    hash = tbox_damage_mix_double(hash, r.y);
    hash = tbox_damage_mix_double(hash, r.width);
    return tbox_damage_mix_double(hash, r.height);
}

static uint64_t tbox_damage_mix_color(uint64_t hash, tbox_css_rgba c) {
    unsigned char bytes[4] = { c.r, c.g, c.b, c.a };
    return tbox_damage_mix(hash, bytes, sizeof(bytes));
}

static uint64_t tbox_damage_mix_doubles(uint64_t hash, const double *values, size_t count) {
    for (size_t i = 0; i < count; i++)
        hash = tbox_damage_mix_double(hash, values[i]);
    return hash;
}

static uint64_t tbox_damage_mix_pointer(uint64_t hash, const void *pointer) {
    uintptr_t value = (uintptr_t)pointer;
    return tbox_damage_mix(hash, &value, sizeof(value));
}

static uint64_t tbox_damage_mix_length(uint64_t hash, tbox_style_length length) {
    hash = tbox_damage_mix_int(hash, (long long)length.kind);
    hash = tbox_damage_mix_double(hash, length.value);
    hash = tbox_damage_mix_double(hash, length.px_offset);
    hash = tbox_damage_mix_int(hash, length.bounds);
    hash = tbox_damage_mix_double(hash, length.clamp_min);
    return tbox_damage_mix_double(hash, length.clamp_max);
}

static uint64_t tbox_damage_mix_gradient(uint64_t hash, const tbox_style_gradient *g) {
    hash = tbox_damage_mix_int(hash, (long long)g->kind);
    hash = tbox_damage_mix_int(hash, g->repeating);
    hash = tbox_damage_mix_double(hash, g->angle);
    hash = tbox_damage_mix_int(hash, g->corner[0]);
    hash = tbox_damage_mix_int(hash, g->corner[1]);
    hash = tbox_damage_mix_int(hash, g->circle);
    hash = tbox_damage_mix_int(hash, (long long)g->extent);
    hash = tbox_damage_mix_length(hash, g->center[0]);
    hash = tbox_damage_mix_length(hash, g->center[1]);
    hash = tbox_damage_mix_length(hash, g->radius[0]);
    hash = tbox_damage_mix_length(hash, g->radius[1]);
    hash = tbox_damage_mix_int(hash, (long long)g->stop_count);
    for (size_t i = 0; i < g->stop_count && i < TBOX_STYLE_MAX_GRADIENT_STOPS; i++) {
        hash = tbox_damage_mix_color(hash, g->stops[i].color);
        hash = tbox_damage_mix_length(hash, g->stops[i].position);
    }
    return hash;
}

static uint64_t tbox_damage_hash_op(const tbox_paint_op *op) {
    uint64_t hash = 14695981039346656037ull;
    hash          = tbox_damage_mix_int(hash, (long long)op->kind);
    hash          = tbox_damage_mix_rect(hash, op->rect);
    hash          = tbox_damage_mix_color(hash, op->color);
    hash          = tbox_damage_mix_int(hash, (long long)op->text.size);
    if (op->text.size > 0)
        hash = tbox_damage_mix(hash, op->text.data, op->text.size);
    hash = tbox_damage_mix_pointer(hash, op->face);
    hash = tbox_damage_mix_double(hash, op->letter_spacing);
    hash = tbox_damage_mix_pointer(hash, op->image);
    hash = tbox_damage_mix_int(hash, op->image_pixelated);
    hash = tbox_damage_mix_double(hash, op->radius);
    hash = tbox_damage_mix_doubles(hash, op->corner_radii, 4);
    hash = tbox_damage_mix_rect(hash, op->inner_rect);
    hash = tbox_damage_mix_doubles(hash, op->inner_corner_radii, 4);
    hash = tbox_damage_mix_int(hash, op->elliptical);
    hash = tbox_damage_mix_doubles(hash, op->corner_radii_y, 4);
    hash = tbox_damage_mix_doubles(hash, op->inner_corner_radii_y, 4);
    hash = tbox_damage_mix_int(hash, op->gradient != NULL);
    if (op->gradient != NULL)
        hash = tbox_damage_mix_gradient(hash, op->gradient);
    hash = tbox_damage_mix_int(hash, op->color_filter != NULL);
    if (op->color_filter != NULL)
        hash = tbox_damage_mix_doubles(hash, op->color_filter, 20);
    hash = tbox_damage_mix_int(hash, (long long)op->blend_mode);
    hash = tbox_damage_mix_int(hash, op->mask_luminance);
    hash = tbox_damage_mix_int(hash, op->filters != NULL ? (long long)op->filters->count : -1);
    for (size_t i = 0; op->filters != NULL && i < op->filters->count; i++) {
        const tbox_style_filter_op *filter = &op->filters->ops[i];
        hash = tbox_damage_mix_int(hash, (long long)filter->kind);
        hash = tbox_damage_mix_doubles(hash, filter->matrix, 20);
        hash = tbox_damage_mix_double(hash, filter->radius);
        hash = tbox_damage_mix_double(hash, filter->shadow.offset_x);
        hash = tbox_damage_mix_double(hash, filter->shadow.offset_y);
        hash = tbox_damage_mix_double(hash, filter->shadow.blur);
        hash = tbox_damage_mix_color(hash, filter->shadow.color);
    }
    hash = tbox_damage_mix_int(hash, op->has_clip);
    if (op->has_clip)
        hash = tbox_damage_mix_rect(hash, op->clip);
    hash = tbox_damage_mix_int(hash, op->has_rounded_clip);
    if (op->has_rounded_clip) {
        hash = tbox_damage_mix_rect(hash, op->rounded_clip);
        hash = tbox_damage_mix_doubles(hash, op->rounded_clip_radii, 4);
        hash = tbox_damage_mix_doubles(hash, op->rounded_clip_radii_y, 4);
    }
    hash = tbox_damage_mix_int(hash, op->polygon_clip != NULL);
    if (op->polygon_clip != NULL) {
        const tbox_paint_polygon *polygon = op->polygon_clip;
        hash = tbox_damage_mix_doubles(hash, op->polygon_clip_origin, 2);
        hash = tbox_damage_mix_int(hash, (long long)polygon->count);
        hash = tbox_damage_mix_int(hash, polygon->evenodd);
        for (size_t i = 0; i < polygon->count && i < TBOX_STYLE_MAX_POLYGON_POINTS; i++)
            hash = tbox_damage_mix_doubles(hash, polygon->points[i], 2);
    }
    return hash;
}

/* ---- damage region ---- */

static double tbox_damage_area(tbox_rect r) {
    return r.width * r.height;
}

static tbox_rect tbox_damage_union(tbox_rect a, tbox_rect b) {
    double x0 = fmin(a.x, b.x), y0 = fmin(a.y, b.y);
    double x1 = fmax(a.x + a.width, b.x + b.width), y1 = fmax(a.y + a.height, b.y + b.height);
    return (tbox_rect){ x0, y0, x1 - x0, y1 - y0 };
}

/* Overlapping or edge-adjacent. */
static bool tbox_damage_touch(tbox_rect a, tbox_rect b) {
    return a.x <= b.x + b.width && b.x <= a.x + a.width && a.y <= b.y + b.height && b.y <= a.y + a.height;
}

static void tbox_damage_remove(tbox_damage *damage, size_t index) {
    damage->rects[index] = damage->rects[--damage->count];
}

void tbox_damage_add(tbox_damage *damage, tbox_rect rect, int32_t buffer_width, int32_t buffer_height) {
    if (damage == NULL || damage->full)
        return;
    rect = tbox_damage_snap(rect, buffer_width, buffer_height);
    if (tbox_damage_empty(rect))
        return;
    /* Absorb every touching rect; the union may now touch others. */
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t i = 0; i < damage->count; i++) {
            if (tbox_damage_touch(damage->rects[i], rect)) {
                rect = tbox_damage_union(rect, damage->rects[i]);
                tbox_damage_remove(damage, i);
                merged = true;
                break;
            }
        }
    }
    if (damage->count == TBOX_DAMAGE_MAX_RECTS) {
        /* Out of slots: grow the rect whose union with `rect` adds the
         * least area. The grown rect may then touch others. */
        size_t best      = 0;
        double best_cost = INFINITY;
        for (size_t i = 0; i < damage->count; i++) {
            tbox_rect u = tbox_damage_union(damage->rects[i], rect);
            double cost = tbox_damage_area(u) - tbox_damage_area(damage->rects[i]) - tbox_damage_area(rect);
            if (cost < best_cost)
                best = i, best_cost = cost;
        }
        tbox_rect grown = tbox_damage_union(damage->rects[best], rect);
        tbox_damage_remove(damage, best);
        tbox_damage_add(damage, grown, buffer_width, buffer_height);
        return;
    }
    damage->rects[damage->count++] = rect;

    double total = 0.0;
    for (size_t i = 0; i < damage->count; i++)
        total += tbox_damage_area(damage->rects[i]);
    if (total > TBOX_DAMAGE_FULL_FRACTION * (double)buffer_width * (double)buffer_height) {
        damage->full  = true;
        damage->count = 0;
    }
}

/* ---- tracker ---- */

tbox_damage_tracker *tbox_damage_tracker_create(void) {
    return calloc(1, sizeof(tbox_damage_tracker));
}

void tbox_damage_tracker_destroy(tbox_damage_tracker *tracker) {
    if (tracker == NULL)
        return;
    free(tracker->previous);
    free(tracker->current);
    free(tracker);
}

void tbox_damage_tracker_invalidate(tbox_damage_tracker *tracker) {
    if (tracker != NULL)
        tracker->valid = false;
}

static bool tbox_damage_signature_equal(const tbox_damage_signature *a, const tbox_damage_signature *b) {
    return a->hash == b->hash && a->bounds.x == b->bounds.x && a->bounds.y == b->bounds.y && a->bounds.width == b->bounds.width && a->bounds.height == b->bounds.height;
}

static int tbox_damage_signature_compare(const void *pa, const void *pb) {
    const tbox_damage_signature *a = pa, *b = pb;
    if (a->hash != b->hash)
        return a->hash < b->hash ? -1 : 1;
    const double ka[4] = { a->bounds.x, a->bounds.y, a->bounds.width, a->bounds.height };
    const double kb[4] = { b->bounds.x, b->bounds.y, b->bounds.width, b->bounds.height };
    for (int i = 0; i < 4; i++)
        if (ka[i] != kb[i])
            return ka[i] < kb[i] ? -1 : 1;
    return 0;
}

static void tbox_damage_add_signature(tbox_damage *damage, const tbox_damage_signature *s, int32_t width, int32_t height) {
    if (!tbox_damage_empty(s->bounds))
        tbox_damage_add(damage, s->bounds, width, height);
}

/* The ops between the common prefix and suffix: an op present on only one
 * side damages its bounds. When both sides hold the same ops in another
 * order (a z-order change), everything in between is damaged. */
static void tbox_damage_diff_middle(tbox_damage *damage, const tbox_damage_signature *old_ops, size_t old_count, const tbox_damage_signature *new_ops, size_t new_count, int32_t width, int32_t height) {
    tbox_damage_signature *sorted = malloc((old_count + new_count) * sizeof(*sorted));
    if (sorted == NULL) {
        damage->full = true, damage->count = 0;
        return;
    }
    tbox_damage_signature *a = sorted, *b = sorted + old_count;
    memcpy(a, old_ops, old_count * sizeof(*a));
    memcpy(b, new_ops, new_count * sizeof(*b));
    qsort(a, old_count, sizeof(*a), tbox_damage_signature_compare);
    qsort(b, new_count, sizeof(*b), tbox_damage_signature_compare);
    size_t i = 0, j = 0;
    bool any_unmatched = false;
    while ((i < old_count || j < new_count) && !damage->full) {
        int order = i == old_count ? 1 : j == new_count ? -1 : tbox_damage_signature_compare(&a[i], &b[j]);
        if (order == 0) {
            i++, j++;
        } else if (order < 0) {
            tbox_damage_add_signature(damage, &a[i++], width, height);
            any_unmatched = true;
        } else {
            tbox_damage_add_signature(damage, &b[j++], width, height);
            any_unmatched = true;
        }
    }
    if (!any_unmatched)
        for (size_t k = 0; k < new_count && !damage->full; k++)
            tbox_damage_add_signature(damage, &new_ops[k], width, height);
    free(sorted);
}

/* Layers in the signatures: a LAYER_BEGIN (and its MASK/END) covers all its
 * layer can touch, so changing its effects repaints the whole layer; an
 * op inside layers that spread (blur, drop shadow) grows by their reach,
 * since changing it changes pixels that far away. */
static void tbox_damage_layer_bounds(const tbox_display_list *list, tbox_damage_signature *signatures, int32_t width, int32_t height) {
    double reach[64];
    size_t ends[64], depth = 0;
    for (size_t i = 0; list != NULL && i < list->count; i++) {
        const tbox_paint_op *op = &list->items[i];
        while (depth > 0 && i > ends[depth - 1]) depth--;
        double grow = depth > 0 ? reach[depth - 1] : 0.0;
        if (op->kind == TBOX_PAINT_LAYER_BEGIN) {
            size_t mask, next;
            size_t end = tbox_raster_layer_end(list, i, &mask);
            tbox_rect r;
            if (!tbox_raster_layer_item_bounds(list, i, width, height, &r, &next))
                r = (tbox_rect){ 0.0, 0.0, 0.0, 0.0 };
            if (grow > 0.0 && !tbox_damage_empty(r))
                r = tbox_damage_snap((tbox_rect){ r.x - grow, r.y - grow, r.width + 2.0 * grow, r.height + 2.0 * grow }, width, height);
            signatures[i].bounds = r;
            if (mask < list->count) signatures[mask].bounds = r;
            if (end < list->count) signatures[end].bounds = r;
            if (depth < 64) {
                reach[depth] = grow + tbox_style_filter_extent(op->filters);
                ends[depth++] = end;
            }
        } else if (op->kind != TBOX_PAINT_LAYER_MASK && op->kind != TBOX_PAINT_LAYER_END && grow > 0.0 && !tbox_damage_empty(signatures[i].bounds)) {
            tbox_rect r = signatures[i].bounds;
            signatures[i].bounds = tbox_damage_snap((tbox_rect){ r.x - grow, r.y - grow, r.width + 2.0 * grow, r.height + 2.0 * grow }, width, height);
        }
    }
}

/* A blurred backdrop-filter reads past its own rect: damage touching what
 * it reads must repaint all of it, or the blur would read pixels it
 * already filtered last frame. */
static void tbox_damage_grow_for_backdrops(tbox_damage *damage, const tbox_display_list *list, const tbox_damage_signature *signatures, int32_t width, int32_t height) {
    for (size_t i = 0; list != NULL && i < list->count && !damage->full; i++) {
        const tbox_paint_op *op = &list->items[i];
        if (op->kind != TBOX_PAINT_BACKDROP_FILTER || tbox_style_filter_extent(op->filters) <= 0.0 || tbox_damage_empty(signatures[i].bounds))
            continue;
        for (size_t d = 0; d < damage->count; d++)
            if (!tbox_damage_empty(tbox_damage_intersect(damage->rects[d], signatures[i].bounds))) {
                tbox_damage_add(damage, signatures[i].bounds, width, height);
                break;
            }
    }
}

tbox_damage tbox_damage_tracker_update(tbox_damage_tracker *tracker, const tbox_display_list *list, int32_t buffer_width, int32_t buffer_height) {
    tbox_damage damage = { .count = 0, .full = true };
    if (tracker == NULL)
        return damage;
    size_t count = list != NULL ? list->count : 0;
    if (count > tracker->capacity) {
        size_t capacity = tracker->capacity > 0 ? tracker->capacity : 64;
        while (capacity < count)
            capacity *= 2;
        tbox_damage_signature *previous = realloc(tracker->previous, capacity * sizeof(*previous));
        if (previous != NULL)
            tracker->previous = previous;
        tbox_damage_signature *current = realloc(tracker->current, capacity * sizeof(*current));
        if (current != NULL)
            tracker->current = current;
        if (previous == NULL || current == NULL) {
            tracker->valid = false;
            return damage;
        }
        tracker->capacity = capacity;
    }
    for (size_t i = 0; i < count; i++) {
        tracker->current[i].hash = tbox_damage_hash_op(&list->items[i]);
        tbox_raster_op_paint_bounds(&list->items[i], buffer_width, buffer_height, &tracker->current[i].bounds);
    }
    tbox_damage_layer_bounds(list, tracker->current, buffer_width, buffer_height);

    if (tracker->valid && tracker->width == buffer_width && tracker->height == buffer_height) {
        damage.full                       = false;
        const tbox_damage_signature *old_ops = tracker->previous, *new_ops = tracker->current;
        size_t old_count = tracker->previous_count, new_count = count;
        size_t prefix = 0;
        while (prefix < old_count && prefix < new_count && tbox_damage_signature_equal(&old_ops[prefix], &new_ops[prefix]))
            prefix++;
        size_t suffix = 0;
        while (suffix < old_count - prefix && suffix < new_count - prefix && tbox_damage_signature_equal(&old_ops[old_count - 1 - suffix], &new_ops[new_count - 1 - suffix]))
            suffix++;
        if (prefix + suffix < old_count || prefix + suffix < new_count)
            tbox_damage_diff_middle(&damage, old_ops + prefix, old_count - prefix - suffix, new_ops + prefix, new_count - prefix - suffix, buffer_width, buffer_height);
        tbox_damage_grow_for_backdrops(&damage, list, new_ops, buffer_width, buffer_height);
    }

    tbox_damage_signature *swap = tracker->previous;
    tracker->previous           = tracker->current;
    tracker->current            = swap;
    tracker->previous_count     = count;
    tracker->width              = buffer_width;
    tracker->height             = buffer_height;
    tracker->valid              = true;
    return damage;
}
