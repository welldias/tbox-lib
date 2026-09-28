#include "tbox_context_internal.h"

static double tbox_context_range_clamp_snap(double value, double min, double max,
                                            double step, bool any_step);

void tbox_context_paint_ranges(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_range_input(box->node) && box->style != NULL && !box->style->visibility_hidden &&
            box->content_box.width >= 16.0 && box->content_box.height >= 14.0) {
            double min, max, step, value;
            bool any_step;
            tbox_context_range_limits(box->node, &min, &max, &step, &any_step);
            if (!tbox_context_number_attribute(box->node, "value", &value)) value = min;
            double progress = max > min ? (value - min) / (max - min) : 0.0;
            if (progress < 0.0) progress = 0.0;
            if (progress > 1.0) progress = 1.0;
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            double left = box->content_box.x + 8.0;
            double width = box->content_box.width - 16.0;
            double center_y = box->content_box.y + box->content_box.height / 2.0;
            tbox_context_push_fill(items, (tbox_rect){left, center_y - 2.0, width, 4.0},
                (tbox_css_rgba){205, 210, 216, 255}, true, clip);
            tbox_context_push_fill(items, (tbox_rect){left, center_y - 2.0, width * progress, 4.0},
                (tbox_css_rgba){65, 115, 195, 255}, true, clip);
            tbox_paint_op *thumb = tbox_vector_push(items);
            *thumb = (tbox_paint_op){.kind = TBOX_PAINT_FILL_RECT,
                .rect = {left + width * progress - 7.0, center_y - 7.0, 14.0, 14.0},
                .color = {65, 115, 195, 255}, .radius = 7.0,
                .has_clip = true, .clip = clip};
        }
        tbox_context_paint_ranges(box->first_child, items);
    }
}

void tbox_context_range_limits(const tbox_html_node *node, double *min, double *max,
                                      double *step, bool *any_step) {
    *min = 0.0;
    *max = 100.0;
    *step = 1.0;
    *any_step = false;
    tbox_context_number_attribute(node, "min", min);
    tbox_context_number_attribute(node, "max", max);
    if (*max < *min) *max = *min;
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("step", 4));
    if (attribute != NULL && tbox_string_view_equal_ascii_ci(attribute->value, tbox_string_view_make("any", 3)))
        *any_step = true;
    else {
        double parsed;
        if (tbox_context_number_attribute(node, "step", &parsed) && parsed > 0.0) *step = parsed;
    }
}

static double tbox_context_range_clamp_snap(double value, double min, double max,
                                            double step, bool any_step) {
    if (value < min) value = min;
    if (value > max) value = max;
    if (!any_step) {
        double units = (value - min) / step;
        if (isfinite(units)) value = min + round(units) * step;
        if (value > max) value -= step;
        if (value < min) value = min;
    }
    return value;
}

void tbox_context_range_normalize(tbox_context *ctx, tbox_html_node *node) {
    double min, max, step;
    bool any_step;
    tbox_context_range_limits(node, &min, &max, &step, &any_step);
    double value;
    if (!tbox_context_number_attribute(node, "value", &value)) value = min + (max - min) / 2.0;
    value = tbox_context_range_clamp_snap(value, min, max, step, any_step);
    char buffer[64];
    int length = snprintf(buffer, sizeof(buffer), "%.15g", value);
    if (length <= 0 || (size_t)length >= sizeof(buffer)) return;
    const tbox_html_attribute *old = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (old == NULL || !tbox_string_view_equal(old->value, tbox_string_view_make(buffer, (size_t)length)))
        tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("value", 5),
            tbox_string_view_make(buffer, (size_t)length));
}

bool tbox_context_range_commit(tbox_context *ctx, const tbox_html_node *node, double value) {
    double min, max, step;
    bool any_step;
    tbox_context_range_limits(node, &min, &max, &step, &any_step);
    value = tbox_context_range_clamp_snap(value, min, max, step, any_step);
    char buffer[64];
    int length = snprintf(buffer, sizeof(buffer), "%.15g", value);
    if (length <= 0 || (size_t)length >= sizeof(buffer)) return false;
    const tbox_html_attribute *old = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (old != NULL && tbox_string_view_equal(old->value,
        tbox_string_view_make(buffer, (size_t)length))) return false;
    tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
        tbox_string_view_make("value", 5), tbox_string_view_make(buffer, (size_t)length));
    if (ctx->input_handler != NULL) {
        const tbox_html_attribute *current = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (current != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, current->value, ctx->input_userdata);
    }
    return true;
}

bool tbox_context_range_choose_x(tbox_context *ctx, const tbox_html_node *node, double x) {
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, node);
    if (box == NULL) return false;
    double width = box->content_box.width - 16.0;
    if (width <= 0.0) return false;
    double position = (x - box->content_box.x - 8.0) / width;
    if (position < 0.0) position = 0.0;
    if (position > 1.0) position = 1.0;
    double min, max, step;
    bool any_step;
    tbox_context_range_limits(node, &min, &max, &step, &any_step);
    return tbox_context_range_commit(ctx, node, min + position * (max - min));
}

bool tbox_context_range_drag(tbox_context *ctx, double x, double y) {
    (void)y;
    if (ctx == NULL || ctx->range_drag_node == NULL ||
        !tbox_context_node_attached(ctx, ctx->range_drag_node) ||
        !tbox_context_is_range_input(ctx->range_drag_node) ||
        !tbox_context_focusable(ctx, ctx->range_drag_node)) return false;
    return tbox_context_range_choose_x(ctx, ctx->range_drag_node, x);
}

void tbox_context_range_release(tbox_context *ctx) {
    if (ctx != NULL) ctx->range_drag_node = NULL;
}
