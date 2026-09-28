#include "tbox_layout_internal.h"
#include <string.h>

const tbox_style tbox_layout_default_style = {
    .display = TBOX_STYLE_DISPLAY_BLOCK,
    .width   = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .height  = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .margin  = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .padding = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .color            = { 0, 0, 0, 255 },
    .background_color = { 0, 0, 0, 0 },
    .font_size         = 16.0,
    .font_weight_bold  = false,
    .opacity           = 1.0,
};

bool tbox_layout_white_space_nowrap(const tbox_style *style) {
    return style->white_space == TBOX_STYLE_WHITE_SPACE_NOWRAP || style->white_space == TBOX_STYLE_WHITE_SPACE_PRE;
}

const tbox_style *tbox_layout_style_or_default(const tbox_style_table *styles, const tbox_html_node *node) {
    const tbox_style *style = tbox_style_table_find(styles, node);
    return style != NULL ? style : &tbox_layout_default_style;
}

void tbox_layout_translate(tbox_layout_box *box, double dx, double dy) {
    tbox_rect *rects[4] = {&box->margin_box, &box->border_box, &box->padding_box, &box->content_box};
    for (size_t i = 0; i < 4; i++) {
        rects[i]->x += dx;
        rects[i]->y += dy;
    }
    for (size_t i = 0; i < box->text_run_count; i++) {
        box->text_runs[i].rect.x += dx;
        box->text_runs[i].rect.y += dy;
    }
    for (size_t i = 0; i < box->table_edge_count; i++) {
        box->table_edges[i].rect.x += dx;
        box->table_edges[i].rect.y += dy;
    }
    for (tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        tbox_layout_translate(child, dx, dy);
}
