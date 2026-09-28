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
    double width = 224.0, height = 124.0;
    double x = box->border_box.x;
    if (x + width > ctx->viewport_width) x = ctx->viewport_width - width;
    if (x < 0.0) x = 0.0;
    double below = ctx->viewport_height - (box->border_box.y + box->border_box.height);
    double above = box->border_box.y;
    double y = below < height && above > below ? box->border_box.y - height :
        box->border_box.y + box->border_box.height;
    out->rect = (tbox_rect){x, y, width, height};
    for (unsigned i = 0; i < 3; i++)
        out->bars[i] = (tbox_rect){x + 25.0, y + 15.0 + 26.0 * i, 185.0, 12.0};
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
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_color);
    const tbox_font_face *face = box != NULL && box->style != NULL ?
        tbox_font_face_cache_get(ctx->fonts, tbox_string_view_from_cstr(box->style->font_family),
            false, false, 12.0) : NULL;
    tbox_context_push_fill(items, popup.rect, (tbox_css_rgba){105, 112, 122, 255}, false, (tbox_rect){0});
    tbox_context_push_fill(items, (tbox_rect){popup.rect.x + 1.0, popup.rect.y + 1.0,
        popup.rect.width - 2.0, popup.rect.height - 2.0}, (tbox_css_rgba){255, 255, 255, 255}, false, (tbox_rect){0});
    static const char labels[] = "RGB";
    for (unsigned channel = 0; channel < 3; channel++) {
        tbox_rect bar = popup.bars[channel];
        if (channel == ctx->color_channel)
            tbox_context_push_fill(items, (tbox_rect){bar.x - 2.0, bar.y - 2.0, bar.width + 4.0,
                bar.height + 4.0}, (tbox_css_rgba){65, 115, 195, 255}, false, (tbox_rect){0});
        if (face != NULL) {
            tbox_paint_op *label = tbox_vector_push(items);
            *label = (tbox_paint_op){.kind = TBOX_PAINT_TEXT_RUN,
                .rect = {popup.rect.x + 8.0, bar.y - 3.0, 12.0, 16.0},
                .color = (tbox_css_rgba){25, 25, 25, 255},
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
        tbox_context_push_fill(items, (tbox_rect){thumb_x - 2.0, bar.y - 3.0, 4.0,
            bar.height + 6.0}, (tbox_css_rgba){20, 20, 20, 255}, false, (tbox_rect){0});
        tbox_context_push_fill(items, (tbox_rect){thumb_x - 1.0, bar.y - 2.0, 2.0,
            bar.height + 4.0}, (tbox_css_rgba){255, 255, 255, 255}, false, (tbox_rect){0});
    }
    tbox_rect preview = {popup.rect.x + 10.0, popup.rect.y + 100.0, popup.rect.width - 20.0, 14.0};
    tbox_context_push_fill(items, preview, (tbox_css_rgba){105, 112, 122, 255}, false, (tbox_rect){0});
    tbox_context_push_fill(items, (tbox_rect){preview.x + 1.0, preview.y + 1.0,
        preview.width - 2.0, preview.height - 2.0}, color, false, (tbox_rect){0});
}

bool tbox_context_color_drag(tbox_context *ctx, double x, double y) {
    if (ctx == NULL || ctx->open_color == NULL) return false;
    return tbox_context_color_choose_point(ctx, x, y);
}
