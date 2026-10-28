#include "tbox_context_internal.h"

/* `resize`: a handle in the bottom inline-end corner of the padding box
 * (bottom-right, bottom-left under `direction: rtl`) that the user drags
 * to change the element's width and/or height. The dragged size lives in
 * ctx->resize_states, like scroll offsets, and is laid over the element's
 * resolved style at every frame (tbox_context_apply_resize), so it
 * survives restyles; min-/max-width/height still clamp it in layout. */

#define TBOX_RESIZER_SIZE 12.0

static tbox_style_resize tbox_context_resize_axes(const tbox_layout_box *box) {
    if (box == NULL || box->node == NULL || box->style == NULL || box->style->visibility_hidden)
        return TBOX_STYLE_RESIZE_NONE;
    return tbox_style_resize_allowed(box->style, tbox_context_is_textarea(box->node));
}

bool tbox_context_resizer_rect(const tbox_layout_box *box, tbox_rect *out) {
    if (tbox_context_resize_axes(box) == TBOX_STYLE_RESIZE_NONE)
        return false;
    tbox_rect port = box->padding_box;
    if (port.width <= 0.0 || port.height <= 0.0)
        return false;
    /* ::-tbox-resizer width/height size the handle (12px by default). */
    const tbox_style_part_style *part = tbox_context_part(box->style, TBOX_STYLE_PART_RESIZER);
    double width                      = tbox_context_part_size(part->width, port.width, TBOX_RESIZER_SIZE);
    double height                     = tbox_context_part_size(part->height, port.height, TBOX_RESIZER_SIZE);
    if (width > port.width)
        width = port.width;
    if (height > port.height)
        height = port.height;
    double x = box->style->direction_rtl ? port.x : port.x + port.width - width;
    *out     = (tbox_rect){ x, port.y + port.height - height, width, height };
    return width > 0.0 && height > 0.0;
}

static const tbox_layout_box *tbox_context_resizer_search(const tbox_layout_box *box, double x, double y, bool has_clip, tbox_rect clip) {
    const tbox_layout_box *last = NULL;
    for (; box != NULL; box = box->next_sibling) {
        if (has_clip && !tbox_context_point_in_rect(clip, x, y))
            continue;
        if (!tbox_context_in_polygon_clips(box, x, y))
            continue;
        tbox_rect handle;
        if ((box->style == NULL || !box->style->pointer_events_none) && tbox_context_resizer_rect(box, &handle) && tbox_context_point_in_rect(handle, x, y))
            last = box;
        bool child_has_clip  = has_clip;
        tbox_rect child_clip = clip;
        if (box->style != NULL && box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE) {
            child_clip     = has_clip ? tbox_context_rect_intersection(clip, box->padding_box) : box->padding_box;
            child_has_clip = true;
        }
        const tbox_layout_box *child = tbox_context_resizer_search(box->first_child, x, y, child_has_clip, child_clip);
        if (child != NULL)
            last = child;
    }
    return last;
}

const tbox_layout_box *tbox_context_resizer_at(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL || ctx->root == NULL)
        return NULL;
    return tbox_context_resizer_search(ctx->root, x, y, false, (tbox_rect){ 0 });
}

static tbox_resize_state *tbox_context_resize_state(tbox_context *ctx, const tbox_html_node *node, bool create) {
    for (tbox_resize_state *state = ctx->resize_states; state != NULL; state = state->next)
        if (state->node == node)
            return state;
    if (!create)
        return NULL;
    tbox_resize_state *state = calloc(1, sizeof(*state));
    if (state == NULL)
        return NULL;
    state->node        = node;
    state->next        = ctx->resize_states;
    ctx->resize_states = state;
    return state;
}

void tbox_context_free_resize_states(tbox_context *ctx) {
    for (tbox_resize_state *state = ctx->resize_states; state != NULL;) {
        tbox_resize_state *next = state->next;
        free(state);
        state = next;
    }
    ctx->resize_states    = NULL;
    ctx->resize_drag_node = NULL;
}

void tbox_context_apply_resize(tbox_context *ctx, tbox_style_table *styles) {
    if (ctx->resize_drag_node != NULL && !tbox_context_node_attached(ctx, ctx->resize_drag_node))
        ctx->resize_drag_node = NULL;
    tbox_resize_state **link = &ctx->resize_states;
    while (*link != NULL) {
        tbox_resize_state *state = *link;
        if (!tbox_context_node_attached(ctx, state->node)) {
            *link = state->next;
            free(state);
            continue;
        }
        link = &state->next;
        for (size_t i = 0; i < styles->count; i++) {
            if (styles->items[i].node != state->node)
                continue;
            tbox_style *style = tbox_style_table_mutable(styles, i, &ctx->frame_arena);
            if (style == NULL)
                continue;
            tbox_style_resize axes = tbox_style_resize_allowed(style, tbox_context_is_textarea(state->node));
            if (state->has_width && (axes == TBOX_STYLE_RESIZE_BOTH || axes == TBOX_STYLE_RESIZE_HORIZONTAL)) {
                style->width         = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, state->width, 0.0, 0, 0.0, 0.0 };
                style->width_keyword = TBOX_STYLE_SIZE_KEYWORD_NONE;
            }
            if (state->has_height && (axes == TBOX_STYLE_RESIZE_BOTH || axes == TBOX_STYLE_RESIZE_VERTICAL)) {
                style->height         = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, state->height, 0.0, 0, 0.0, 0.0 };
                style->height_keyword = TBOX_STYLE_SIZE_KEYWORD_NONE;
            }
            break;
        }
    }
}

/* One grip line: the band between two diagonals across `rect`, from
 * `offset` px into the corner and `thickness` wide, mirrored for rtl. */
static void tbox_context_resizer_line(tbox_context *ctx, tbox_vector *items, tbox_rect rect, double offset, double thickness, bool rtl, tbox_css_rgba color, bool has_clip, tbox_rect clip) {
    tbox_paint_polygon *polygon = tbox_arena_alloc(&ctx->frame_arena, sizeof(*polygon));
    if (polygon == NULL)
        return;
    double w = rect.width, h = rect.height;
    /* ltr: from the right edge (offset down) to the bottom edge (offset in). */
    double points[4][2] = {
        { w,                        h - offset - thickness },
        { w,                        h - offset             },
        { w - offset,               h                      },
        { w - offset - thickness,   h                      },
    };
    *polygon = (tbox_paint_polygon){ .count = 4, .evenodd = false };
    for (size_t i = 0; i < 4; i++) {
        polygon->points[i][0] = rtl ? w - points[i][0] : points[i][0];
        polygon->points[i][1] = points[i][1];
    }
    tbox_paint_op *op = tbox_vector_push(items);
    if (op == NULL)
        return;
    *op = (tbox_paint_op){
        .kind                = TBOX_PAINT_FILL_RECT,
        .rect                = rect,
        .color               = color,
        .has_clip            = has_clip,
        .clip                = clip,
        .polygon_clip        = polygon,
        .polygon_clip_origin = { rect.x, rect.y },
    };
}

/* The handle: ::-tbox-resizer's box (transparent by default), then two
 * diagonal grip lines in its `color` (a discreet gray by default). */
void tbox_context_paint_resizers(tbox_context *ctx, const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_rect handle;
        if (tbox_context_resizer_rect(box, &handle)) {
            tbox_rect clip;
            bool has_clip                     = tbox_context_ancestor_clip(box, &clip);
            const tbox_style_part_style *part = tbox_context_part(box->style, TBOX_STYLE_PART_RESIZER);
            tbox_context_paint_part(items, handle, part, (tbox_css_rgba){ 0, 0, 0, 0 }, has_clip, clip);
            tbox_css_rgba color = part->color_declared ? part->color : (tbox_css_rgba){ 128, 128, 128, 255 };
            double unit         = (handle.width < handle.height ? handle.width : handle.height) / TBOX_RESIZER_SIZE;
            bool rtl            = box->style->direction_rtl;
            tbox_context_resizer_line(ctx, items, handle, 2.0 * unit, 1.5 * unit, rtl, color, has_clip, clip);
            tbox_context_resizer_line(ctx, items, handle, 6.0 * unit, 1.5 * unit, rtl, color, has_clip, clip);
        }
        tbox_context_paint_resizers(ctx, box->first_child, items);
    }
}

bool tbox_context_resize_press(tbox_context *ctx, double x, double y) {
    ctx->resize_drag_node        = NULL;
    const tbox_layout_box *box   = tbox_context_resizer_at(ctx, x, y);
    if (box == NULL)
        return false;
    bool border_box              = box->style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX;
    ctx->resize_drag_node        = box->node;
    ctx->resize_drag_axes        = tbox_context_resize_axes(box);
    ctx->resize_drag_rtl         = box->style->direction_rtl;
    ctx->resize_grab_x           = x;
    ctx->resize_grab_y           = y;
    /* The current size in the box `width`/`height` measure. */
    ctx->resize_start_width      = border_box ? box->border_box.width : box->content_box.width + box->scrollbar_gutter;
    ctx->resize_start_height     = border_box ? box->border_box.height : box->content_box.height;
    return true;
}

bool tbox_context_resize_drag(tbox_context *ctx, double x, double y) {
    if (ctx->resize_drag_node == NULL)
        return false;
    tbox_resize_state *state = tbox_context_resize_state(ctx, ctx->resize_drag_node, true);
    if (state == NULL)
        return true;
    tbox_style_resize axes = ctx->resize_drag_axes;
    if (axes == TBOX_STYLE_RESIZE_BOTH || axes == TBOX_STYLE_RESIZE_HORIZONTAL) {
        double dx        = x - ctx->resize_grab_x;
        double width     = ctx->resize_start_width + (ctx->resize_drag_rtl ? -dx : dx);
        state->width     = width > 0.0 ? width : 0.0;
        state->has_width = true;
    }
    if (axes == TBOX_STYLE_RESIZE_BOTH || axes == TBOX_STYLE_RESIZE_VERTICAL) {
        double height     = ctx->resize_start_height + y - ctx->resize_grab_y;
        state->height     = height > 0.0 ? height : 0.0;
        state->has_height = true;
    }
    return true;
}
