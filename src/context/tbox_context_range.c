#include "tbox_context_internal.h"

static double tbox_context_range_clamp_snap(double value, double min, double max,
                                            double step, bool any_step);

#define TBOX_RANGE_THUMB 14.0
#define TBOX_RANGE_TRACK 4.0
static const tbox_css_rgba tbox_range_track_color = {205, 210, 216, 255};

/* The span the thumb's center travels: the content box inset by half the
 * thumb on each side. Painting and pointer input share it. */
static bool tbox_context_range_span(const tbox_layout_box *box, double *left, double *width) {
    const tbox_style_part_style *thumb = tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_THUMB);
    double thumb_width = tbox_context_part_size(thumb->width, box->content_box.width, TBOX_RANGE_THUMB);
    *left  = box->content_box.x + thumb_width / 2.0;
    *width = box->content_box.width - thumb_width;
    return *width > 0.0;
}

/* Track, fill and thumb are the ::slider-track/-fill/-thumb parts; the
 * fill and thumb default to accent-color. They may overflow the input's
 * box (a thumb taller than it), clipped only by scrolling ancestors. */
void tbox_context_paint_ranges(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        double left, width;
        if (tbox_context_is_range_input(box->node) && box->style != NULL && !box->style->visibility_hidden &&
            box->content_box.height > 0.0 && tbox_context_range_span(box, &left, &width)) {
            double min, max, step, value;
            bool any_step;
            tbox_context_range_limits(box->node, &min, &max, &step, &any_step);
            if (!tbox_context_number_attribute(box->node, "value", &value)) value = min;
            double progress = max > min ? (value - min) / (max - min) : 0.0;
            if (progress < 0.0) progress = 0.0;
            if (progress > 1.0) progress = 1.0;
            tbox_rect clip = {0};
            bool has_clip = tbox_context_ancestor_clip(box, &clip);
            const tbox_style_part_style *track = tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_TRACK);
            const tbox_style_part_style *fill  = tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_FILL);
            const tbox_style_part_style *thumb = tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_THUMB);
            double height        = box->content_box.height;
            double center_y      = box->content_box.y + height / 2.0;
            double track_height  = tbox_context_part_size(track->height, height, TBOX_RANGE_TRACK);
            double fill_height   = tbox_context_part_size(fill->height, height, track_height);
            double thumb_width   = tbox_context_part_size(thumb->width, box->content_box.width, TBOX_RANGE_THUMB);
            double thumb_height  = tbox_context_part_size(thumb->height, height, TBOX_RANGE_THUMB);
            tbox_css_rgba accent = tbox_context_accent_color(box->style);
            tbox_context_paint_part(items, (tbox_rect){left, center_y - track_height / 2.0, width, track_height},
                track, tbox_range_track_color, has_clip, clip);
            tbox_context_paint_part(items, (tbox_rect){left, center_y - fill_height / 2.0, width * progress, fill_height},
                fill, accent, has_clip, clip);
            tbox_context_paint_part(items, (tbox_rect){left + width * progress - thumb_width / 2.0,
                center_y - thumb_height / 2.0, thumb_width, thumb_height}, thumb, accent, has_clip, clip);
        }
        tbox_context_paint_ranges(box->first_child, items);
    }
}

/* Passive gauges share the range control's rectangle painter but have no
 * thumb, focus handling, or input events. Their fallback text stays in the
 * DOM and is suppressed by Layout while the visual gauge is available.
 * ::slider-track is the whole content box and ::slider-fill the value's
 * share of the inside of its border; an undeclared fill is accent-color (progress) or the
 * meter's low/high/optimum color. */
void tbox_context_paint_gauges(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        bool progress = box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(box->node->element.tag_name, "progress");
        bool meter = box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(box->node->element.tag_name, "meter");
        if ((progress || meter) && box->style != NULL && !box->style->visibility_hidden &&
            box->content_box.width > 0.0 && box->content_box.height > 0.0) {
            double min = 0.0, max = 1.0, value = 0.0;
            if (meter) tbox_context_number_attribute(box->node, "min", &min);
            if (!tbox_context_number_attribute(box->node, "max", &max) || max <= min)
                max = progress ? 1.0 : (min > 1.0 ? min : 1.0);
            bool determinate = tbox_context_number_attribute(box->node, "value", &value);
            if (value < min) value = min;
            if (value > max) value = max;
            double fraction = max > min ? (value - min) / (max - min) : 0.0;
            if (!determinate && meter) fraction = 0.0;

            tbox_css_rgba fill = progress ? tbox_context_accent_color(box->style) :
                (tbox_css_rgba){46, 145, 83, 255};
            if (meter) {
                double low = min, high = max, optimum = (min + max) / 2.0;
                tbox_context_number_attribute(box->node, "low", &low);
                tbox_context_number_attribute(box->node, "high", &high);
                tbox_context_number_attribute(box->node, "optimum", &optimum);
                if (low < min) low = min;
                if (low > max) low = max;
                if (high < low) high = low;
                if (high > max) high = max;
                if (optimum < min) optimum = min;
                if (optimum > max) optimum = max;
                bool suboptimal = (optimum < low && value >= low && value <= high) ||
                    (optimum > high && value >= low && value <= high) ||
                    (optimum >= low && optimum <= high && (value < low || value > high));
                bool bad = (optimum < low && value > high) || (optimum > high && value < low);
                if (bad) fill = (tbox_css_rgba){190, 65, 65, 255};
                else if (suboptimal) fill = (tbox_css_rgba){205, 150, 35, 255};
            }

            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            const tbox_style_part_style *track = tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_TRACK);
            tbox_context_paint_part(items, box->content_box, track, tbox_range_track_color, true, clip);
            /* The fill runs inside the track's border. */
            tbox_rect filled = {box->content_box.x + track->border_widths[3], box->content_box.y + track->border_widths[0],
                box->content_box.width - track->border_widths[1] - track->border_widths[3],
                box->content_box.height - track->border_widths[0] - track->border_widths[2]};
            if (progress && !determinate) {
                filled.x += filled.width * 0.35;
                filled.width *= 0.30;
            } else {
                filled.width *= fraction;
            }
            if (filled.width > 0.0)
                tbox_context_paint_part(items, filled, tbox_context_part(box->style, TBOX_STYLE_PART_SLIDER_FILL),
                    fill, true, clip);
        }
        tbox_context_paint_gauges(box->first_child, items);
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
    int length = tbox_number_format(buffer, sizeof(buffer), value, 15);
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
    int length = tbox_number_format(buffer, sizeof(buffer), value, 15);
    if (length <= 0 || (size_t)length >= sizeof(buffer)) return false;
    const tbox_html_attribute *old = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (old != NULL && tbox_string_view_equal(old->value,
        tbox_string_view_make(buffer, (size_t)length))) return false;
    tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
        tbox_string_view_make("value", 5), tbox_string_view_make(buffer, (size_t)length));
    if (ctx->input_handler != NULL) {
        const tbox_html_attribute *current = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (current != NULL) tbox_context_emit_input(ctx, node, current->value);
    }
    return true;
}

bool tbox_context_range_choose_x(tbox_context *ctx, const tbox_html_node *node, double x) {
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, node);
    double left, width;
    if (box == NULL || box->style == NULL || !tbox_context_range_span(box, &left, &width)) return false;
    double position = (x - left) / width;
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
