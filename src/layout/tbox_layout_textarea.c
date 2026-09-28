#include "tbox_layout_internal.h"
#include <string.h>

unsigned tbox_layout_textarea_size(const tbox_html_node *node, const char *name, unsigned fallback) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    if (attribute == NULL || attribute->value.size == 0) return fallback;
    unsigned value = 0;
    for (size_t i = 0; i < attribute->value.size; i++) {
        char c = attribute->value.data[i];
        if (c < '0' || c > '9' || value > 10000) return fallback;
        value = value * 10 + (unsigned)(c - '0');
    }
    return value > 0 && value <= 10000 ? value : fallback;
}

bool tbox_layout_is_textarea(const tbox_html_node *node) {
    return tbox_layout_tag(node, "textarea");
}

double tbox_layout_textarea_auto_width(const tbox_html_node *node,
    const tbox_style *style, tbox_font_face_cache *fonts, double fallback_width) {
    const tbox_font_face *face = tbox_font_face_cache_get(fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, style->font_size);
    return face != NULL ? tbox_layout_textarea_size(node, "cols", 20) *
        tbox_font_measure_text(face, tbox_string_view_make("0", 1)) : fallback_width;
}

double tbox_layout_textarea_height(const tbox_html_node *node,
    const tbox_style *style, tbox_font_face_cache *fonts,
    tbox_layout_containing_block container, double vertical_edges,
    double text_height) {
    const tbox_font_face *face = tbox_font_face_cache_get(fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, style->font_size);
    double height = text_height;
    if (style->height.kind == TBOX_STYLE_LENGTH_PX) height = style->height.value;
    else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
        height = style->height.value / 100.0 * container.height;
    else if (face != NULL)
        height = tbox_layout_textarea_size(node, "rows", 2) * tbox_font_face_line_height(face);
    if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX &&
        (style->height.kind == TBOX_STYLE_LENGTH_PX ||
         (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)))
        height = height > vertical_edges ? height - vertical_edges : 0.0;
    return height;
}
