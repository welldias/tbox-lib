#include "tbox_context_internal.h"

typedef struct tbox_color_popup_geometry {
    tbox_rect rect;
    tbox_rect bars[3];
} tbox_color_popup_geometry;

static bool tbox_context_color_popup_geometry(tbox_context *ctx, tbox_color_popup_geometry *out);

tbox_css_rgba tbox_context_color_value(const tbox_html_node *node) {
    tbox_css_rgba color = {0, 0, 0, 255};
    const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (value != NULL && value->value.size == 7)
        tbox_css_hex_to_rgba(value->value, &color);
    color.a = 255;
    return color;
}

bool tbox_context_color_commit(tbox_context *ctx, const tbox_html_node *node, tbox_css_rgba color) {
    tbox_css_rgba old = tbox_context_color_value(node);
    if (old.r == color.r && old.g == color.g && old.b == color.b) return false;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", color.r, color.g, color.b);
    tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
                                 tbox_string_view_make("value", 5), tbox_string_view_make(hex, 7));
    if (ctx->input_handler != NULL) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (value != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, value->value, ctx->input_userdata);
    }
    return true;
}

static bool tbox_context_color_popup_geometry(tbox_context *ctx, tbox_color_popup_geometry *out) {
    if (ctx->open_color == NULL) return false;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_color);
    if (box == NULL) return false;
    tbox_style root_style = tbox_context_control_style(ctx, ctx->open_color, NULL, NULL);
    tbox_style track_style = tbox_context_control_style(ctx, ctx->open_color, "track", NULL);
    tbox_style preview_style = tbox_context_control_style(ctx, ctx->open_color, "preview", NULL);
    double track_width = tbox_context_control_size(track_style.width, 185.0);
    double track_height = tbox_context_control_size(track_style.height, 12.0);
    double preview_height = tbox_context_control_size(preview_style.height, 14.0);
    double left = tbox_context_control_size(root_style.padding[3], 25.0);
    double top = tbox_context_control_size(root_style.padding[0], 15.0);
    double gap = tbox_context_control_size(root_style.row_gap, 14.0);
    double width = tbox_context_control_size(root_style.width, 224.0);
    double needed_width = left + track_width + 14.0;
    if (width < needed_width) width = needed_width;
    double height = tbox_context_control_size(root_style.height, 124.0);
    double needed_height = top + track_height * 3.0 + gap * 2.0 + 21.0 + preview_height + 10.0;
    if (height < needed_height) height = needed_height;
    double x = box->border_box.x;
    if (x + width > ctx->viewport_width) x = ctx->viewport_width - width;
    if (x < 0.0) x = 0.0;
    double below = ctx->viewport_height - (box->border_box.y + box->border_box.height);
    double above = box->border_box.y;
    double y = below < height && above > below ? box->border_box.y - height :
        box->border_box.y + box->border_box.height;
    out->rect = (tbox_rect){x, y, width, height};
    for (unsigned i = 0; i < 3; i++)
        out->bars[i] = (tbox_rect){x + left, y + top + (track_height + gap) * i,
                                   track_width, track_height};
    return true;
}

bool tbox_context_color_popup_visible(tbox_context *ctx) {
    tbox_color_popup_geometry popup;
    return tbox_context_color_popup_geometry(ctx, &popup);
}

bool tbox_context_color_popup_contains(tbox_context *ctx, double x, double y) {
    tbox_color_popup_geometry popup;
    return tbox_context_color_popup_geometry(ctx, &popup) &&
        tbox_context_point_in_rect(popup.rect, x, y);
}

bool tbox_context_color_choose_point(tbox_context *ctx, double x, double y) {
    tbox_color_popup_geometry popup;
    if (!tbox_context_color_popup_geometry(ctx, &popup)) return false;
    for (unsigned channel = 0; channel < 3; channel++) {
        tbox_rect bar = popup.bars[channel];
        if (y < bar.y - 5.0 || y >= bar.y + bar.height + 5.0) continue;
        if (x < bar.x) x = bar.x;
        if (x > bar.x + bar.width) x = bar.x + bar.width;
        unsigned value = (unsigned)((x - bar.x) * 255.0 / bar.width + 0.5);
        tbox_css_rgba color = tbox_context_color_value(ctx->open_color);
        if (channel == 0) color.r = (unsigned char)value;
        else if (channel == 1) color.g = (unsigned char)value;
        else color.b = (unsigned char)value;
        bool channel_changed = ctx->color_channel != channel;
        ctx->color_channel = channel;
        return tbox_context_color_commit(ctx, ctx->open_color, color) || channel_changed;
    }
    return false;
}

void tbox_context_paint_color_popup(tbox_context *ctx, tbox_vector *items) {
    tbox_color_popup_geometry popup;
    if (!tbox_context_color_popup_geometry(ctx, &popup)) return;
    tbox_css_rgba color = tbox_context_color_value(ctx->open_color);
    tbox_style root_style = tbox_context_control_style(ctx, ctx->open_color, NULL, NULL);
    tbox_context_paint_control_box(items, popup.rect, &root_style, false, (tbox_rect){0});
    tbox_style label_style = tbox_context_control_style(ctx, ctx->open_color, "channel-label", NULL);
    const tbox_font_face *face = tbox_context_control_font(ctx, &label_style);
    static const char labels[] = "RGB";
    for (unsigned channel = 0; channel < 3; channel++) {
        tbox_rect bar = popup.bars[channel];
        if (channel == ctx->color_channel) {
            tbox_style active_style = tbox_context_control_style(ctx, ctx->open_color,
                "track", "is-active");
            tbox_context_paint_control_box(items, (tbox_rect){bar.x - 2.0, bar.y - 2.0,
                bar.width + 4.0, bar.height + 4.0}, &active_style, false, (tbox_rect){0});
        }
        if (face != NULL) {
            tbox_paint_op *label = tbox_vector_push(items);
            *label = (tbox_paint_op){.kind = TBOX_PAINT_TEXT_RUN,
                .rect = {popup.rect.x + 8.0, bar.y - 3.0, 12.0, 16.0},
                .color = label_style.color,
                .text = tbox_string_view_make(labels + channel, 1), .face = face};
        }
        unsigned char current = channel == 0 ? color.r : channel == 1 ? color.g : color.b;
        for (unsigned segment = 0; segment < 16; segment++) {
            tbox_css_rgba sample = color;
            unsigned char level = (unsigned char)(segment * 17);
            if (channel == 0) sample.r = level;
            else if (channel == 1) sample.g = level;
            else sample.b = level;
            double left = bar.x + bar.width * segment / 16.0;
            double right = bar.x + bar.width * (segment + 1) / 16.0;
            tbox_context_push_fill(items, (tbox_rect){left, bar.y, right - left, bar.height},
                                   sample, false, (tbox_rect){0});
        }
        double thumb_x = bar.x + bar.width * current / 255.0;
        tbox_style thumb_style = tbox_context_control_style(ctx, ctx->open_color, "thumb", NULL);
        double thumb_width = tbox_context_control_size(thumb_style.width, 4.0);
        double thumb_height = tbox_context_control_size(thumb_style.height, bar.height + 6.0);
        tbox_context_paint_control_box(items,
            (tbox_rect){thumb_x - thumb_width / 2.0,
                        bar.y + (bar.height - thumb_height) / 2.0,
                        thumb_width, thumb_height}, &thumb_style, false, (tbox_rect){0});
    }
    tbox_style preview_style = tbox_context_control_style(ctx, ctx->open_color, "preview", NULL);
    tbox_rect preview = {popup.rect.x + 10.0,
        popup.bars[2].y + popup.bars[2].height + 21.0,
        tbox_context_control_size(preview_style.width, popup.rect.width - 20.0),
        tbox_context_control_size(preview_style.height, 14.0)};
    tbox_context_paint_control_box(items, preview, &preview_style, false, (tbox_rect){0});
    tbox_context_push_fill(items, (tbox_rect){preview.x + 1.0, preview.y + 1.0,
        preview.width - 2.0, preview.height - 2.0}, color, false, (tbox_rect){0});
}

bool tbox_context_color_drag(tbox_context *ctx, double x, double y) {
    if (ctx == NULL || ctx->open_color == NULL) return false;
    return tbox_context_color_choose_point(ctx, x, y);
}
