#include "tbox_context_internal.h"

/* This stylesheet is intentionally separate from the document cascade.
 * The geometry functions retain structural offsets, while every visible
 * color and every user-adjustable size comes from this theme. */
static const char tbox_control_css[] =
    "tbox-popup { background-color:#fff; color:#191919; border:1px solid #69707a; font-size:13px }"
    "tbox-popup[type=color] { width:224px; height:124px; padding:15px 0 0 25px; row-gap:14px }"
    "tbox-popup[type=color] .track { width:185px; height:12px }"
    "tbox-popup[type=color] .track.is-active { border:2px solid #4173c3 }"
    "tbox-popup[type=color] .channel-label { font-size:12px; color:#191919 }"
    "tbox-popup[type=color] .thumb { width:4px; background-color:white; border:1px solid #141414 }"
    "tbox-popup[type=color] .preview { height:14px; border:1px solid #69707a }"
    "tbox-popup[type=date],tbox-popup[type=week] { width:238px; height:212px }"
    "tbox-popup[type=month] { width:238px; height:166px }"
    "tbox-popup[type=time] { width:238px; height:126px }"
    "tbox-popup[type=datetime-local] { width:238px; height:306px }"
    "tbox-popup .heading,tbox-popup .nav { color:#191919 }"
    "tbox-popup .nav { width:24px; height:24px }"
    "tbox-popup .weekday { color:#5a5f69 }"
    "tbox-popup .day,tbox-popup .month { width:30px; height:24px }"
    "tbox-popup .month { width:54px; height:38px }"
    "tbox-popup .is-selected { background-color:#4173c3; color:white }"
    "tbox-popup .is-disabled { color:#aaa }"
    "tbox-popup .time-track { width:140px; height:12px; background-color:#dce1e8 }"
    "tbox-popup .time-fill { background-color:#4173c3 }"
    "tbox-popup .confirm { width:55px; height:25px; background-color:#4173c3; color:white }"
    "tbox-popup .confirm.is-disabled { background-color:#b4b9c1; color:white }"
    "tbox-popup[type=file] { width:320px; height:260px }"
    "tbox-popup[type=file] .back { width:26px; height:26px; background-color:#ebeef3 }"
    "tbox-popup[type=file] .path { color:#191919 }"
    "tbox-popup[type=file] .divider { background-color:#d2d5dc }"
    "tbox-popup[type=file] .row { height:28px; color:#191919 }"
    "tbox-popup[type=file] .row.is-directory { color:#2d559b }"
    "tbox-popup[type=file] .row.is-selected { background-color:#4173c3; color:white }"
    "tbox-popup[type=file] .empty { color:#7d7d7d }"
    "tbox-popup[type=file] .footer { color:#646973 }";

const char *tbox_context_default_control_css(void) { return tbox_control_css; }

static tbox_style tbox_context_style_node(tbox_context *ctx, tbox_html_node *node,
                                          const tbox_style *parent) {
    tbox_css_cascade_source source = {ctx->control_stylesheet, TBOX_CSS_ORIGIN_AUTHOR};
    tbox_css_computed_style computed = tbox_css_cascade_resolve(&source, 1, node);
    tbox_style style = tbox_style_resolve(node, parent, &computed);
    tbox_css_computed_style_destroy(&computed);
    return style;
}

tbox_style tbox_context_control_style(tbox_context *ctx, const tbox_html_node *owner,
                                       const char *part, const char *state) {
    tbox_html_attribute attrs[3];
    size_t count = 0;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(owner, tbox_string_view_from_cstr("type"));
    char type_text[32];
    size_t type_length = type != NULL && type->value.size < sizeof(type_text) - 1 ?
        type->value.size : 0;
    for (size_t i = 0; i < type_length; i++)
        type_text[i] = (char)tolower((unsigned char)type->value.data[i]);
    type_text[type_length] = '\0';
    attrs[count++] = (tbox_html_attribute){tbox_string_view_from_cstr("type"),
        type_length > 0 ? tbox_string_view_make(type_text, type_length) :
            tbox_string_view_from_cstr("text")};
    const tbox_html_attribute *id = tbox_html_node_get_attribute(owner, tbox_string_view_from_cstr("id"));
    if (id != NULL) attrs[count++] = (tbox_html_attribute){tbox_string_view_from_cstr("id"), id->value};
    const tbox_html_attribute *class_attr = tbox_html_node_get_attribute(owner, tbox_string_view_from_cstr("class"));
    if (class_attr != NULL) attrs[count++] = (tbox_html_attribute){tbox_string_view_from_cstr("class"), class_attr->value};
    tbox_html_node popup = {.type = TBOX_HTML_NODE_ELEMENT,
        .element = {.tag_name = tbox_string_view_from_cstr("tbox-popup"),
                    .attributes = attrs, .attribute_count = count}};
    tbox_style root_style = tbox_context_style_node(ctx, &popup, NULL);
    if (part == NULL) return root_style;

    char classes[96];
    int length = snprintf(classes, sizeof(classes), "%s%s%s", part,
        state != NULL && state[0] != '\0' ? " " : "", state != NULL ? state : "");
    if (length < 0 || (size_t)length >= sizeof(classes)) classes[0] = '\0';
    tbox_html_attribute part_attr = {tbox_string_view_from_cstr("class"),
        tbox_string_view_from_cstr(classes)};
    tbox_html_node child = {.type = TBOX_HTML_NODE_ELEMENT, .parent = &popup,
        .element = {.tag_name = tbox_string_view_from_cstr("tbox-part"),
                    .attributes = &part_attr, .attribute_count = 1}};
    popup.first_child = popup.last_child = &child;
    return tbox_context_style_node(ctx, &child, &root_style);
}

double tbox_context_control_size(tbox_style_length length, double fallback) {
    return length.kind == TBOX_STYLE_LENGTH_PX && isfinite(length.value) && length.value > 0.0
        ? length.value : fallback;
}

const tbox_font_face *tbox_context_control_font(tbox_context *ctx, const tbox_style *style) {
    return ctx->fonts != NULL ? tbox_font_face_cache_get(ctx->fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, style->font_size) : NULL;
}

static void tbox_context_control_corners(tbox_paint_op *op, const tbox_style *style,
                                          tbox_rect rect, double inset) {
    bool separate = false;
    for (size_t i = 0; i < 4; i++)
        if (style->border_radius_corners[i] > 0.0 || style->border_radius_percent[i] > 0.0)
            separate = true;
    double base = rect.width < rect.height ? rect.width : rect.height;
    for (size_t i = 0; i < 4; i++) {
        double radius = separate ? style->border_radius_corners[i] +
            style->border_radius_percent[i] * base / 100.0 : style->border_radius;
        op->corner_radii[i] = radius > inset ? radius - inset : 0.0;
    }
}

void tbox_context_paint_control_box(tbox_vector *items, tbox_rect rect,
                                     const tbox_style *style, bool has_clip, tbox_rect clip) {
    double borders[4];
    for (size_t side = 0; side < 4; side++) borders[side] = tbox_style_border_side_width(style, side);
    bool uniform = borders[0] == borders[1] && borders[0] == borders[2] && borders[0] == borders[3];
    if (uniform && borders[0] > 0.0) {
        tbox_paint_op *op = tbox_vector_push(items);
        *op = (tbox_paint_op){.kind = TBOX_PAINT_FILL_RECT, .rect = rect,
            .color = tbox_style_border_side_color(style, 0), .radius = style->border_radius,
            .has_clip = has_clip, .clip = clip};
        tbox_context_control_corners(op, style, rect, 0.0);
        rect.x += borders[0]; rect.y += borders[0];
        rect.width -= 2.0 * borders[0]; rect.height -= 2.0 * borders[0];
    } else {
        for (size_t side = 0; side < 4; side++) {
            if (borders[side] <= 0.0) continue;
            tbox_rect strip = rect;
            if (side == 0) strip.height = borders[side];
            if (side == 1) { strip.x += rect.width - borders[side]; strip.width = borders[side]; }
            if (side == 2) { strip.y += rect.height - borders[side]; strip.height = borders[side]; }
            if (side == 3) strip.width = borders[side];
            tbox_context_push_fill(items, strip, tbox_style_border_side_color(style, side), has_clip, clip);
        }
        rect.x += borders[3]; rect.y += borders[0];
        rect.width -= borders[1] + borders[3]; rect.height -= borders[0] + borders[2];
    }
    if (rect.width > 0.0 && rect.height > 0.0 && style->background_color.a > 0) {
        tbox_paint_op *op = tbox_vector_push(items);
        *op = (tbox_paint_op){.kind = TBOX_PAINT_FILL_RECT, .rect = rect,
            .color = style->background_color,
            .radius = style->border_radius > borders[0] ? style->border_radius - borders[0] : 0.0,
            .has_clip = has_clip, .clip = clip};
        tbox_context_control_corners(op, style, rect, borders[0]);
    }
}
