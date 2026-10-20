#include <math.h>
#include "tbox_context_internal.h"
#include "render/tbox_render_internal.h"

/* `rail` is the track less the thumb's margins: where the thumb travels. */
typedef struct tbox_scrollbar_geometry {
    tbox_rect track, rail, thumb;
    double max_scroll;
} tbox_scrollbar_geometry;

static tbox_scroll_state *tbox_context_scroll_state(tbox_context *ctx, const tbox_html_node *node);
static void tbox_context_shift_subtree(tbox_layout_box *box, double dy);
static bool tbox_context_scrollbar_geometry(const tbox_layout_box *box, double offset, tbox_scrollbar_geometry *out);
static tbox_scroll_state *tbox_context_existing_scroll_state(tbox_context *ctx, const tbox_html_node *node);
static void tbox_context_paint_scrollbars(tbox_context *ctx, const tbox_layout_box *box, tbox_vector *items);
static const tbox_layout_box *tbox_context_scrollbar_at(const tbox_layout_box *box,
                                                         double x, double y, bool has_clip, tbox_rect clip);

static tbox_scroll_state *tbox_context_scroll_state(tbox_context *ctx, const tbox_html_node *node) {
    for (tbox_scroll_state *state = ctx->scroll_states; state != NULL; state = state->next)
        if (state->node == node) return state;
    tbox_scroll_state *state = calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->node = node;
    state->next = ctx->scroll_states;
    ctx->scroll_states = state;
    return state;
}

#define TBOX_SCROLL_SMOOTH_SECONDS 0.3

/* Eases `state` to `target` from where it is now, which
 * tbox_context_apply_scroll advances frame by frame; at once with the
 * reduced-motion preference. */
static void tbox_context_scroll_glide(tbox_context *ctx, tbox_scroll_state *state, double target) {
    if (ctx->prefers_reduced_motion) {
        state->animating = false;
        state->y         = target;
        return;
    }
    state->animating  = true;
    state->from_y     = state->y;
    state->target_y   = target;
    state->start_time = ctx->animation_time;
    tbox_context_wake_at(ctx, -INFINITY);
}

/* Scrolls `state` (of a container styled `style`) to `target`: at once,
 * or -- with scroll-behavior: smooth and no reduced-motion preference --
 * gliding there. */
static void tbox_context_scroll_to(tbox_context *ctx, tbox_scroll_state *state, const tbox_style *style, double target) {
    if (style == NULL || !style->scroll_behavior_smooth) {
        state->animating = false;
        state->y         = target;
        return;
    }
    tbox_context_scroll_glide(ctx, state, target);
}

#define TBOX_SCROLL_SNAP_IDLE_SECONDS 0.15

/* A scroll of a snapping container (`style`) by a user gesture: the snap
 * happens once no gesture moved it for TBOX_SCROLL_SNAP_IDLE_SECONDS
 * (`immediate`: at the next frame). */
static void tbox_context_scroll_moved(tbox_context *ctx, tbox_scroll_state *state, const tbox_style *style, double origin, bool immediate) {
    if (style == NULL || style->scroll_snap_type == TBOX_STYLE_SCROLL_SNAP_NONE) return;
    if (!state->snap_pending) state->snap_origin_y = origin;
    state->snap_pending    = true;
    /* the idle wait starts at the next frame: ctx->animation_time is the
     * last frame's, maybe long ago */
    state->snap_time       = immediate ? -INFINITY : NAN;
    tbox_context_wake_at(ctx, -INFINITY);
}

/* Snap positions found under a container, as scroll offsets. */
typedef struct tbox_snap_search {
    double port_top, port_bottom, port_height, max_y;
    double y, origin;
    bool found, found_stop;
    double best, best_stop; /* nearest to y; the `always` one nearest to origin, between origin and y */
} tbox_snap_search;

static void tbox_context_snap_candidates(const tbox_layout_box *box, tbox_snap_search *search) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_style *style = box->style;
        if (style != NULL && style->scroll_snap_align != TBOX_STYLE_SCROLL_SNAP_ALIGN_NONE) {
            double top    = box->border_box.y - tbox_style_length_resolve(style->scroll_margin[0], search->port_height);
            double bottom = box->border_box.y + box->border_box.height + tbox_style_length_resolve(style->scroll_margin[2], search->port_height);
            double position = style->scroll_snap_align == TBOX_STYLE_SCROLL_SNAP_ALIGN_START ? top - search->port_top :
                              style->scroll_snap_align == TBOX_STYLE_SCROLL_SNAP_ALIGN_END   ? bottom - search->port_bottom :
                                                                                               (top + bottom - search->port_top - search->port_bottom) / 2.0;
            if (position < 0.0) position = 0.0;
            if (position > search->max_y) position = search->max_y;
            if (!search->found || fabs(position - search->y) < fabs(search->best - search->y)) {
                search->best  = position;
                search->found = true;
            }
            bool between = search->y > search->origin ? position > search->origin + 0.5 && position < search->y - 0.5
                                                      : position < search->origin - 0.5 && position > search->y + 0.5;
            if (style->scroll_snap_stop_always && between &&
                (!search->found_stop || fabs(position - search->origin) < fabs(search->best_stop - search->origin))) {
                search->best_stop  = position;
                search->found_stop = true;
            }
        }
        /* a nested scroll container snaps its own content */
        if (style == NULL || style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO)
            tbox_context_snap_candidates(box->first_child, search);
    }
}

/* Where `container`, scrolled to `y` by a gesture that began at `origin`,
 * snaps to; false when it stays put (no snap point, or none close enough
 * for `proximity`). */
static bool tbox_context_snap_target(const tbox_layout_box *container, double y, double origin, double max_y, double *out) {
    const tbox_style *style = container->style;
    double port = container->padding_box.height;
    const tbox_style_length *padding = style->scroll_padding;
    tbox_snap_search search = { 0 };
    search.port_height = port;
    search.port_top    = container->padding_box.y + (padding[0].kind == TBOX_STYLE_LENGTH_AUTO ? 0.0 : tbox_style_length_resolve(padding[0], port));
    search.port_bottom = container->padding_box.y + port - (padding[2].kind == TBOX_STYLE_LENGTH_AUTO ? 0.0 : tbox_style_length_resolve(padding[2], port));
    search.max_y       = max_y;
    search.y           = y;
    search.origin      = origin;
    tbox_context_snap_candidates(container->first_child, &search);
    if (search.found_stop) {
        *out = search.best_stop;
        return true;
    }
    if (!search.found)
        return false;
    /* proximity: only snap points within a third of the scrollport */
    if (style->scroll_snap_type == TBOX_STYLE_SCROLL_SNAP_PROXIMITY && fabs(search.best - y) > port / 3.0)
        return false;
    *out = search.best;
    return true;
}

/* Advances a smooth scroll to ctx->animation_time. */
static void tbox_context_scroll_step(tbox_context *ctx, tbox_scroll_state *state) {
    if (!state->animating) return;
    double t = (ctx->animation_time - state->start_time) / TBOX_SCROLL_SMOOTH_SECONDS;
    if (t >= 1.0 || t < 0.0) {
        state->y         = state->target_y;
        state->animating = false;
        return;
    }
    double eased = t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
    state->y     = state->from_y + (state->target_y - state->from_y) * eased;
    tbox_context_wake_at(ctx, -INFINITY);
}

/* A transformed box's matrix moves with it: conjugated by the shift, so
 * it still turns around the same point of the box. */
static void tbox_context_shift_transform(tbox_layout_box *box, double dy) {
    if (box->has_transform) {
        box->transform[4] -= box->transform[2] * dy;
        box->transform[5] += dy - box->transform[3] * dy;
    }
}

static void tbox_context_shift_subtree(tbox_layout_box *box, double dy) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_context_shift_transform(box, dy);
        box->margin_box.y += dy;
        box->border_box.y += dy;
        box->padding_box.y += dy;
        box->content_box.y += dy;
        for (size_t i = 0; i < box->text_run_count; i++) box->text_runs[i].rect.y += dy;
        tbox_context_shift_subtree(box->first_child, dy);
    }
}

void tbox_context_apply_scroll(tbox_context *ctx, tbox_layout_box *box) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->style != NULL && box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO && box->node != NULL) {
            tbox_scroll_state *state = tbox_context_scroll_state(ctx, box->node);
            if (state != NULL) {
                tbox_context_scroll_step(ctx, state);
                double max_y = box->scroll_content_height - box->content_box.height;
                if (max_y < 0.0) max_y = 0.0;
                if (state->y > max_y) state->y = max_y;
                if (state->y < 0.0) state->y = 0.0;
                if (state->snap_pending && isnan(state->snap_time))
                    state->snap_time = ctx->animation_time;
                if (state->snap_pending && box->style->scroll_snap_type == TBOX_STYLE_SCROLL_SNAP_NONE) {
                    state->snap_pending = false;
                } else if (state->snap_pending && !state->animating && ctx->animation_time - state->snap_time >= TBOX_SCROLL_SNAP_IDLE_SECONDS) {
                    state->snap_pending = false;
                    double target;
                    if (tbox_context_snap_target(box, state->y, state->snap_origin_y, max_y, &target) && fabs(target - state->y) > 0.25) {
                        tbox_context_scroll_glide(ctx, state, target);
                        tbox_context_scroll_step(ctx, state);
                    }
                } else if (state->snap_pending) {
                    tbox_context_wake_at(ctx, state->animating ? -INFINITY : state->snap_time + TBOX_SCROLL_SNAP_IDLE_SECONDS);
                }
                if (state->y > 0.0) tbox_context_shift_subtree(box->first_child, -state->y);
            }
        }
        tbox_context_apply_scroll(ctx, box->first_child);
    }
}

/* position: sticky -- after scrolling, a sticky box whose `top` (or
 * `bottom`) edge would pass the matching edge of its scrollport (the
 * nearest scroll container's padding box, or the viewport) plus the offset
 * is held there, but never pushed out of its parent's content box. */
static void tbox_context_stick(tbox_layout_box *box, tbox_rect scrollport) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_style *style = box->style;
        tbox_rect child_port    = scrollport;
        if (style != NULL && style->overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO)
            child_port = box->padding_box;
        if (style != NULL && style->position == TBOX_STYLE_POSITION_STICKY && box->parent != NULL) {
            const tbox_layout_box *parent = box->parent;
            double dy                     = 0.0;
            if (style->offset[0].kind != TBOX_STYLE_LENGTH_AUTO) {
                double threshold = scrollport.y + tbox_style_length_resolve(style->offset[0], scrollport.height);
                if (box->border_box.y < threshold) {
                    double room = parent->content_box.y + parent->content_box.height - (box->margin_box.y + box->margin_box.height);
                    dy          = threshold - box->border_box.y;
                    if (dy > room)
                        dy = room > 0.0 ? room : 0.0;
                }
            } else if (style->offset[2].kind != TBOX_STYLE_LENGTH_AUTO) {
                double threshold = scrollport.y + scrollport.height - tbox_style_length_resolve(style->offset[2], scrollport.height);
                double bottom    = box->border_box.y + box->border_box.height;
                if (bottom > threshold) {
                    double room = box->margin_box.y - parent->content_box.y;
                    dy          = threshold - bottom;
                    if (-dy > room)
                        dy = room > 0.0 ? -room : 0.0;
                }
            }
            if (dy != 0.0) {
                tbox_context_shift_transform(box, dy);
                box->margin_box.y += dy;
                box->border_box.y += dy;
                box->padding_box.y += dy;
                box->content_box.y += dy;
                for (size_t i = 0; i < box->text_run_count; i++) box->text_runs[i].rect.y += dy;
                tbox_context_shift_subtree(box->first_child, dy);
            }
        }
        tbox_context_stick(box->first_child, child_port);
    }
}

void tbox_context_apply_sticky(tbox_context *ctx, double viewport_width, double viewport_height) {
    tbox_context_stick(ctx->root, (tbox_rect){ 0.0, 0.0, viewport_width, viewport_height });
}

void tbox_context_reveal_focused(tbox_context *ctx) {
    if (!ctx->focus_scroll_pending) return;
    ctx->focus_scroll_pending = false;
    if (ctx->focused_node == NULL) return;
    tbox_layout_box *focused = (tbox_layout_box *)tbox_context_find_box(ctx->root, ctx->focused_node);
    if (focused == NULL) return;
    for (tbox_layout_box *ancestor = focused->parent; ancestor != NULL; ancestor = ancestor->parent) {
        if (ancestor->style == NULL || ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO ||
            ancestor->node == NULL) continue;
        double max_scroll = ancestor->scroll_content_height - ancestor->content_box.height;
        if (max_scroll <= 0.0) continue;
        tbox_scroll_state *state = tbox_context_scroll_state(ctx, ancestor->node);
        if (state == NULL) continue;
        /* scroll-padding shrinks the scrollport, scroll-margin grows the
         * element's box; percentages of scroll-padding are of the port. */
        double port = ancestor->padding_box.height;
        const tbox_style_length *padding = ancestor->style->scroll_padding;
        double view_top = ancestor->padding_box.y +
            (padding[0].kind == TBOX_STYLE_LENGTH_AUTO ? 0.0 : tbox_style_length_resolve(padding[0], port));
        double view_bottom = ancestor->padding_box.y + port -
            (padding[2].kind == TBOX_STYLE_LENGTH_AUTO ? 0.0 : tbox_style_length_resolve(padding[2], port));
        const tbox_style_length *margin = focused->style != NULL ? focused->style->scroll_margin : NULL;
        double item_top = focused->border_box.y - (margin != NULL ? tbox_style_length_resolve(margin[0], port) : 0.0);
        double item_bottom = focused->border_box.y + focused->border_box.height +
            (margin != NULL ? tbox_style_length_resolve(margin[2], port) : 0.0);
        double delta = 0.0;
        if (item_top < view_top && item_bottom > view_bottom) {
            continue; /* taller than the viewport and already spans it */
        } else if (item_top < view_top) {
            delta = item_top - view_top;
        } else if (item_bottom > view_bottom) {
            delta = item_bottom - view_bottom;
        }
        double next = state->y + delta;
        if (next < 0.0) next = 0.0;
        if (next > max_scroll) next = max_scroll;
        if (next != state->y) {
            double before = state->y;
            tbox_context_scroll_to(ctx, state, ancestor->style, next);
            tbox_context_shift_subtree(ancestor->first_child, -(state->y - before)); /* 0 when smooth: it starts next frame */
        }
    }
}

static bool tbox_context_scrollbar_geometry(const tbox_layout_box *box, double offset, tbox_scrollbar_geometry *out) {
    if (box == NULL || box->style == NULL || box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO ||
        box->style->visibility_hidden ||
        box->node == NULL || box->padding_box.width <= 0.0 || box->padding_box.height <= 0.0 ||
        box->content_box.height <= 0.0) return false;
    double max_scroll = box->scroll_content_height - box->content_box.height;
    if (max_scroll <= 0.0) return false;
    /* The width is ::-tbox-scrollbar-track's or scrollbar-width's; none
     * keeps scrolling but paints and hit-tests no bar (see
     * tbox_context_scrollbar_visible), measured here as the default. */
    double bar   = tbox_style_scrollbar_size(box->style, box->padding_box.width);
    if (bar <= 0.0) bar = 10.0;
    double width = box->padding_box.width < bar ? box->padding_box.width : bar;
    tbox_rect track = {box->padding_box.x + box->padding_box.width - width,
                       box->padding_box.y, width, box->padding_box.height};
    /* A `resize` handle in the same corner takes the track's bottom. */
    tbox_rect handle;
    if (tbox_context_resizer_rect(box, &handle) && handle.x + handle.width > track.x && handle.y < track.y + track.height) {
        track.height = handle.y - track.y;
        if (track.height <= 0.0) return false;
    }
    /* ::-tbox-scrollbar-thumb: `margin` insets it in the track (a % of the
     * track's width), `min-height` is its shortest length. */
    const tbox_style_part_style *thumb = tbox_context_part(box->style, TBOX_STYLE_PART_SCROLLBAR_THUMB);
    double margins[4];
    for (size_t i = 0; i < 4; i++) {
        margins[i] = tbox_style_length_resolve(thumb->margin[i], track.width);
        if (!(margins[i] > 0.0)) margins[i] = 0.0;
    }
    tbox_rect rail = {track.x + margins[3], track.y + margins[0],
                      track.width - margins[1] - margins[3], track.height - margins[0] - margins[2]};
    if (rail.width < 0.0) rail.width = 0.0;
    if (rail.height < 0.0) rail.height = 0.0;
    double thumb_height = rail.height * rail.height / (rail.height + max_scroll);
    double minimum      = tbox_context_part_size(thumb->min_height, rail.height, 18.0);
    if (thumb_height < minimum) thumb_height = minimum;
    if (thumb_height > rail.height) thumb_height = rail.height;
    double clamped = offset < 0.0 ? 0.0 : (offset > max_scroll ? max_scroll : offset);
    double travel = rail.height - thumb_height;
    *out = (tbox_scrollbar_geometry){
        .track = track,
        .rail = rail,
        .thumb = {rail.x, rail.y + travel * clamped / max_scroll, rail.width, thumb_height},
        .max_scroll = max_scroll,
    };
    return true;
}

static bool tbox_context_scrollbar_visible(const tbox_layout_box *box) {
    return box->style != NULL && box->style->scrollbar_width != TBOX_STYLE_SCROLLBAR_WIDTH_NONE;
}

static tbox_rect tbox_context_scrollport(const tbox_layout_box *box) {
    tbox_rect port = box->padding_box;
    port.width = port.width > box->scrollbar_gutter ? port.width - box->scrollbar_gutter : 0.0;
    return port;
}

static tbox_scroll_state *tbox_context_existing_scroll_state(tbox_context *ctx, const tbox_html_node *node) {
    for (tbox_scroll_state *state = ctx->scroll_states; state != NULL; state = state->next)
        if (state->node == node) return state;
    return NULL;
}

/* The track and thumb are the ::-tbox-scrollbar-track/-thumb parts; the
 * thumb's :active variant while it is dragged, :hover while the pointer is
 * on it. Undeclared colors fall back to scrollbar-color, then the built-in
 * grays. */
static void tbox_context_paint_scrollbars(tbox_context *ctx, const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_scrollbar_geometry geometry;
        const tbox_scroll_state *state = box->node != NULL ? tbox_context_existing_scroll_state(ctx, box->node) : NULL;
        if (tbox_context_scrollbar_visible(box) && tbox_context_scrollbar_geometry(box, state != NULL ? state->y : 0.0, &geometry)) {
            /* Scrollbars are appended after content: wrap them in the same
             * ancestor polygon intersections instead of escaping the clip. */
            size_t polygon_count = 0;
            for (const tbox_layout_box *ancestor = box; ancestor != NULL; ancestor = ancestor->parent) {
                if (ancestor->style == NULL || tbox_style_clip_path_of(ancestor->style)->kind != TBOX_STYLE_CLIP_PATH_POLYGON) continue;
                tbox_paint_op *op = tbox_vector_push(items);
                *op = (tbox_paint_op){
                    .kind = TBOX_PAINT_LAYER_BEGIN, .color = {0, 0, 0, 255},
                    .polygon_clip = tbox_render_resolve_polygon(ancestor, &ctx->frame_arena),
                    .polygon_clip_origin = {ancestor->border_box.x, ancestor->border_box.y},
                };
                polygon_count++;
            }
            /* scrollbar-color, alpha 0 being the default colors (a
             * transparent track by default). */
            tbox_css_rgba track_color = box->style->scrollbar_track_color.a != 0 ? box->style->scrollbar_track_color : (tbox_css_rgba){ 0, 0, 0, 0 };
            tbox_css_rgba thumb_color = box->style->scrollbar_thumb_color.a != 0 ? box->style->scrollbar_thumb_color : (tbox_css_rgba){ 100, 110, 122, 255 };
            tbox_style_part thumb_part = ctx->scroll_drag_node == box->node ? TBOX_STYLE_PART_SCROLLBAR_THUMB_ACTIVE :
                                         ctx->scroll_thumb_hover == box->node ? TBOX_STYLE_PART_SCROLLBAR_THUMB_HOVER :
                                         TBOX_STYLE_PART_SCROLLBAR_THUMB;
            tbox_rect clip = geometry.track;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, tbox_context_scrollport(ancestor));
            tbox_context_paint_part(items, geometry.track, tbox_context_part(box->style, TBOX_STYLE_PART_SCROLLBAR_TRACK),
                                    track_color, true, clip);
            tbox_context_paint_part(items, geometry.thumb, tbox_context_part(box->style, thumb_part), thumb_color, true, clip);
            for (size_t i = 0; i < polygon_count; i++) {
                tbox_paint_op *op = tbox_vector_push(items);
                *op = (tbox_paint_op){.kind = TBOX_PAINT_LAYER_END};
            }
        }
        tbox_context_paint_scrollbars(ctx, box->first_child, items);
    }
}

void tbox_context_append_scrollbars(tbox_context *ctx, tbox_display_list *list) {
    tbox_vector items;
    tbox_vector_init(&items, &ctx->frame_arena, sizeof(tbox_paint_op), list->count + 16);
    for (size_t i = 0; i < list->count; i++)
        *(tbox_paint_op *)tbox_vector_push(&items) = list->items[i];
    size_t before = items.length;
    tbox_context_paint_scrollbars(ctx, ctx->root, &items);
    tbox_context_paint_resizers(ctx, ctx->root, &items);
    if (items.length == before) return;
    list->items = (tbox_paint_op *)items.data;
    list->count = items.length;
}

const tbox_html_node *tbox_context_scrollbar_thumb_at(tbox_context *ctx, double x, double y) {
    const tbox_layout_box *box = ctx->root != NULL ? tbox_context_scrollbar_at(ctx->root, x, y, false, (tbox_rect){0}) : NULL;
    if (box == NULL) return NULL;
    const tbox_scroll_state *state = tbox_context_existing_scroll_state(ctx, box->node);
    tbox_scrollbar_geometry geometry;
    return tbox_context_scrollbar_geometry(box, state != NULL ? state->y : 0.0, &geometry) &&
           tbox_context_point_in_rect(geometry.thumb, x, y) ? box->node : NULL;
}

static const tbox_layout_box *tbox_context_scrollbar_at(const tbox_layout_box *box,
                                                         double x, double y, bool has_clip, tbox_rect clip) {
    const tbox_layout_box *last = NULL;
    for (; box != NULL; box = box->next_sibling) {
        if ((has_clip && !tbox_context_point_in_rect(clip, x, y)) || !tbox_context_in_polygon_clips(box, x, y)) continue;
        tbox_scrollbar_geometry geometry;
        if ((box->style == NULL || !box->style->pointer_events_none) &&
            tbox_context_scrollbar_visible(box) && tbox_context_scrollbar_geometry(box, 0.0, &geometry) &&
            tbox_context_point_in_rect(geometry.track, x, y)) last = box;
        bool child_has_clip = has_clip;
        tbox_rect child_clip = clip;
        if (box->style != NULL && box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE) {
            tbox_rect scrollport = tbox_context_scrollport(box);
            child_clip = has_clip ? tbox_context_rect_intersection(clip, scrollport) : scrollport;
            child_has_clip = true;
        }
        const tbox_layout_box *child = tbox_context_scrollbar_at(box->first_child, x, y,
                                                                  child_has_clip, child_clip);
        if (child != NULL) last = child;
    }
    return last;
}

bool tbox_context_scroll(tbox_context *ctx, double x, double y, double delta_y) {
    if (ctx == NULL || ctx->root == NULL || delta_y == 0.0) return false;
    if (tbox_context_file_popup_scroll(ctx, x, y, delta_y)) return true;
    if (tbox_context_select_popup_scroll(ctx, x, y, delta_y)) return true;
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    for (; box != NULL; box = box->parent) {
        if (tbox_context_is_textarea(box->node)) {
            tbox_text_field *field = tbox_context_text_field(ctx, box->node);
            if (field == NULL) return false;
            double max_y = box->scroll_content_height - box->content_box.height;
            if (max_y < 0.0) max_y = 0.0;
            double next = field->scroll_y + delta_y;
            if (next < 0.0) next = 0.0;
            if (next > max_y) next = max_y;
            if (next != field->scroll_y) {
                field->scroll_y = next;
                field->reveal = false;
                return true;
            }
            /* At its limit: overscroll-behavior decides whether to chain. */
            if (box->style != NULL && box->style->overscroll_y != TBOX_STYLE_OVERSCROLL_AUTO) return false;
        }
        if (box->style == NULL || box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO || box->node == NULL) continue;
        double max_y = box->scroll_content_height - box->content_box.height;
        if (max_y <= 0.0) continue;
        tbox_scroll_state *state = tbox_context_scroll_state(ctx, box->node);
        if (state == NULL) return false;
        double next = state->y + delta_y;
        if (next < 0.0) next = 0.0;
        if (next > max_y) next = max_y;
        if (next == state->y) {
            if (box->style->overscroll_y != TBOX_STYLE_OVERSCROLL_AUTO) return false;
            continue;
        }
        double before    = state->y;
        state->y         = next; /* the wheel is direct manipulation: never smoothed */
        state->animating = false;
        tbox_context_scroll_moved(ctx, state, box->style, before, false);
        return true;
    }
    return false;
}

bool tbox_context_scrollbar_press(tbox_context *ctx, double x, double y) {
    if (ctx == NULL) return false;
    if (tbox_context_file_popup_contains(ctx, x, y) ||
        tbox_context_date_popup_contains(ctx, x, y) ||
        tbox_context_color_popup_contains(ctx, x, y) ||
        tbox_context_select_popup_contains(ctx, x, y)) return false;
    ctx->scroll_drag_node = NULL;
    ctx->scroll_pointer_consumed = false;
    if (tbox_context_resize_press(ctx, x, y)) {
        ctx->scroll_pointer_consumed = true;
        return true;
    }
    const tbox_layout_box *box = tbox_context_scrollbar_at(ctx->root, x, y, false, (tbox_rect){0});
    if (box == NULL) return false;
    ctx->scroll_pointer_consumed = true;
    tbox_scroll_state *state = tbox_context_scroll_state(ctx, box->node);
    if (state == NULL) return true;
    tbox_scrollbar_geometry geometry;
    if (!tbox_context_scrollbar_geometry(box, state->y, &geometry)) return true;
    if (tbox_context_point_in_rect(geometry.thumb, x, y)) {
        ctx->scroll_drag_node = box->node;
        ctx->scroll_drag_grab_y = y - geometry.thumb.y;
        state->snap_pending  = false;
        state->snap_origin_y = state->y;
    } else {
        /* A page per track click, from where a running smooth scroll is
         * heading, so quick clicks add up. */
        double page   = box->content_box.height;
        double from   = state->animating ? state->target_y : state->y;
        double target = from + (y < geometry.thumb.y ? -page : page);
        if (target < 0.0) target = 0.0;
        if (target > geometry.max_scroll) target = geometry.max_scroll;
        tbox_context_scroll_to(ctx, state, box->style, target);
        tbox_context_scroll_moved(ctx, state, box->style, from, true);
    }
    return true;
}

bool tbox_context_scrollbar_drag(tbox_context *ctx, double x, double y) {
    (void)x;
    if (ctx == NULL || !ctx->scroll_pointer_consumed) return false;
    if (ctx->resize_drag_node != NULL) return tbox_context_resize_drag(ctx, x, y);
    if (ctx->scroll_drag_node == NULL) return true;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->scroll_drag_node);
    tbox_scroll_state *state = tbox_context_existing_scroll_state(ctx, ctx->scroll_drag_node);
    tbox_scrollbar_geometry geometry;
    if (box == NULL || state == NULL || !tbox_context_scrollbar_geometry(box, state->y, &geometry)) {
        ctx->scroll_drag_node = NULL;
        return true;
    }
    double travel = geometry.rail.height - geometry.thumb.height;
    if (travel > 0.0) {
        double thumb_y = y - ctx->scroll_drag_grab_y - geometry.rail.y;
        if (thumb_y < 0.0) thumb_y = 0.0;
        if (thumb_y > travel) thumb_y = travel;
        state->y         = thumb_y / travel * geometry.max_scroll;
        state->animating = false; /* snaps once released (tbox_context_scrollbar_release) */
    }
    return true;
}

void tbox_context_scrollbar_release(tbox_context *ctx) {
    if (ctx != NULL) {
        if (ctx->scroll_drag_node != NULL) {
            const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->scroll_drag_node);
            tbox_scroll_state *state = tbox_context_existing_scroll_state(ctx, ctx->scroll_drag_node);
            if (box != NULL && state != NULL) {
                tbox_context_scroll_moved(ctx, state, box->style, state->snap_origin_y, true);
            }
        }
        ctx->scroll_drag_node = NULL;
        ctx->resize_drag_node = NULL;
        ctx->scroll_pointer_consumed = false;
    }
}
