#include "tbox_context_internal.h"

typedef struct tbox_scrollbar_geometry {
    tbox_rect track, thumb;
    double max_scroll;
} tbox_scrollbar_geometry;

static tbox_scroll_state *tbox_context_scroll_state(tbox_context *ctx, const tbox_html_node *node);
static void tbox_context_shift_subtree(tbox_layout_box *box, double dy);
static bool tbox_context_scrollbar_geometry(const tbox_layout_box *box, double offset, tbox_scrollbar_geometry *out);
static tbox_scroll_state *tbox_context_existing_scroll_state(tbox_context *ctx, const tbox_html_node *node);
static size_t tbox_context_scrollbar_count(const tbox_layout_box *box);
static void tbox_context_paint_scrollbars(tbox_context *ctx, const tbox_layout_box *box,
                                           tbox_paint_op *items, size_t *index);
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

static void tbox_context_shift_subtree(tbox_layout_box *box, double dy) {
    for (; box != NULL; box = box->next_sibling) {
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
                double max_y = box->scroll_content_height - box->content_box.height;
                if (max_y < 0.0) max_y = 0.0;
                if (state->y > max_y) state->y = max_y;
                if (state->y < 0.0) state->y = 0.0;
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
        double view_top = ancestor->padding_box.y;
        double view_bottom = view_top + ancestor->padding_box.height;
        double item_top = focused->border_box.y;
        double item_bottom = item_top + focused->border_box.height;
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
            double movement = next - state->y;
            state->y = next;
            tbox_context_shift_subtree(ancestor->first_child, -movement);
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
    /* scrollbar-width: thin is a narrower bar; none keeps scrolling but
     * paints and hit-tests no bar (see tbox_context_scrollbar_visible). */
    double bar   = box->style->scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_THIN ? 6.0 : 10.0;
    double width = box->padding_box.width < bar ? box->padding_box.width : bar;
    tbox_rect track = {box->padding_box.x + box->padding_box.width - width,
                       box->padding_box.y, width, box->padding_box.height};
    double thumb_height = track.height * track.height / (track.height + max_scroll);
    if (thumb_height < 18.0) thumb_height = 18.0;
    if (thumb_height > track.height) thumb_height = track.height;
    double clamped = offset < 0.0 ? 0.0 : (offset > max_scroll ? max_scroll : offset);
    double travel = track.height - thumb_height;
    *out = (tbox_scrollbar_geometry){
        .track = track,
        .thumb = {track.x, track.y + travel * clamped / max_scroll, width, thumb_height},
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

static size_t tbox_context_scrollbar_count(const tbox_layout_box *box) {
    size_t count = 0;
    for (; box != NULL; box = box->next_sibling) {
        tbox_scrollbar_geometry geometry;
        if (tbox_context_scrollbar_visible(box) && tbox_context_scrollbar_geometry(box, 0.0, &geometry)) count++;
        count += tbox_context_scrollbar_count(box->first_child);
    }
    return count;
}

static void tbox_context_paint_scrollbars(tbox_context *ctx, const tbox_layout_box *box,
                                           tbox_paint_op *items, size_t *index) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_scrollbar_geometry geometry;
        tbox_scroll_state *state = box->node != NULL ? tbox_context_existing_scroll_state(ctx, box->node) : NULL;
        if (tbox_context_scrollbar_visible(box) && tbox_context_scrollbar_geometry(box, state != NULL ? state->y : 0.0, &geometry)) {
            /* scrollbar-color, alpha 0 being the default colors. */
            tbox_css_rgba track_color = box->style->scrollbar_track_color.a != 0 ? box->style->scrollbar_track_color : (tbox_css_rgba){ 220, 224, 230, 255 };
            tbox_css_rgba thumb_color = box->style->scrollbar_thumb_color.a != 0 ? box->style->scrollbar_thumb_color : (tbox_css_rgba){ 100, 110, 122, 255 };
            tbox_rect clip = geometry.track;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, tbox_context_scrollport(ancestor));
            items[(*index)++] = (tbox_paint_op){
                .kind = TBOX_PAINT_FILL_RECT, .rect = geometry.track,
                .color = track_color, .has_clip = true, .clip = clip,
            };
            items[(*index)++] = (tbox_paint_op){
                .kind = TBOX_PAINT_FILL_RECT, .rect = geometry.thumb,
                .color = thumb_color, .radius = geometry.thumb.width / 2.5,
                .has_clip = true, .clip = clip,
            };
        }
        tbox_context_paint_scrollbars(ctx, box->first_child, items, index);
    }
}

void tbox_context_append_scrollbars(tbox_context *ctx, tbox_display_list *list) {
    size_t count = tbox_context_scrollbar_count(ctx->root);
    if (count == 0) return;
    tbox_paint_op *items = tbox_arena_alloc(&ctx->frame_arena,
        (list->count + count * 2) * sizeof(*items));
    if (items == NULL) return;
    if (list->count > 0) memcpy(items, list->items, list->count * sizeof(*items));
    size_t index = list->count;
    tbox_context_paint_scrollbars(ctx, ctx->root, items, &index);
    list->items = items;
    list->count = index;
}

static const tbox_layout_box *tbox_context_scrollbar_at(const tbox_layout_box *box,
                                                         double x, double y, bool has_clip, tbox_rect clip) {
    const tbox_layout_box *last = NULL;
    for (; box != NULL; box = box->next_sibling) {
        if (has_clip && !tbox_context_point_in_rect(clip, x, y)) continue;
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
        }
        if (box->style == NULL || box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_AUTO || box->node == NULL) continue;
        double max_y = box->scroll_content_height - box->content_box.height;
        if (max_y <= 0.0) continue;
        tbox_scroll_state *state = tbox_context_scroll_state(ctx, box->node);
        if (state == NULL) return false;
        double next = state->y + delta_y;
        if (next < 0.0) next = 0.0;
        if (next > max_y) next = max_y;
        if (next == state->y) continue;
        state->y = next;
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
    } else {
        double page = box->content_box.height;
        state->y += y < geometry.thumb.y ? -page : page;
        if (state->y < 0.0) state->y = 0.0;
        if (state->y > geometry.max_scroll) state->y = geometry.max_scroll;
    }
    return true;
}

bool tbox_context_scrollbar_drag(tbox_context *ctx, double x, double y) {
    (void)x;
    if (ctx == NULL || !ctx->scroll_pointer_consumed) return false;
    if (ctx->scroll_drag_node == NULL) return true;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->scroll_drag_node);
    tbox_scroll_state *state = tbox_context_existing_scroll_state(ctx, ctx->scroll_drag_node);
    tbox_scrollbar_geometry geometry;
    if (box == NULL || state == NULL || !tbox_context_scrollbar_geometry(box, state->y, &geometry)) {
        ctx->scroll_drag_node = NULL;
        return true;
    }
    double travel = geometry.track.height - geometry.thumb.height;
    if (travel > 0.0) {
        double thumb_y = y - ctx->scroll_drag_grab_y - geometry.track.y;
        if (thumb_y < 0.0) thumb_y = 0.0;
        if (thumb_y > travel) thumb_y = travel;
        state->y = thumb_y / travel * geometry.max_scroll;
    }
    return true;
}

void tbox_context_scrollbar_release(tbox_context *ctx) {
    if (ctx != NULL) {
        ctx->scroll_drag_node = NULL;
        ctx->scroll_pointer_consumed = false;
    }
}
