#include "tbox_context_internal.h"

double tbox_context_style_line_height(const tbox_style *style, const tbox_font_face *face) {
    double natural = tbox_font_face_line_height(face);
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_NUMBER)
        return style->font_size * style->line_height_value;
    if (style->line_height_kind == TBOX_STYLE_LINE_HEIGHT_PX)
        return style->line_height_value;
    return natural;
}

const tbox_layout_box *tbox_context_find_box(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node) return box;
        const tbox_layout_box *child = tbox_context_find_box(box->first_child, node);
        if (child != NULL) return child;
    }
    return NULL;
}

tbox_rect tbox_context_rect_intersection(tbox_rect a, tbox_rect b) {
    double x0 = a.x > b.x ? a.x : b.x;
    double y0 = a.y > b.y ? a.y : b.y;
    double x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    double y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    return (tbox_rect){x0, y0, x1 > x0 ? x1 - x0 : 0.0, y1 > y0 ? y1 - y0 : 0.0};
}

void tbox_context_push_fill(tbox_vector *items, tbox_rect rect, tbox_css_rgba color,
                                   bool has_clip, tbox_rect clip) {
    tbox_paint_op *op = tbox_vector_push(items);
    *op = (tbox_paint_op){ .kind = TBOX_PAINT_FILL_RECT, .rect = rect,
                           .color = color, .has_clip = has_clip, .clip = clip };
}

bool tbox_context_point_in_rect(tbox_rect r, double x, double y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

bool tbox_context_node_attached(const tbox_context *ctx, const tbox_html_node *node) {
    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    while (node != NULL && node->parent != NULL) {
        node = node->parent;
    }
    return node == root;
}
