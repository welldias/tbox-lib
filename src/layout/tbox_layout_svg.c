#include "layout/tbox_layout_internal.h"
#include "svg/tbox_svg_internal.h"
#include <math.h>

/* The HTML box uses CSS sizing; the vector tree never participates in
 * min-content or text layout. A viewBox supplies a ratio, not pixel size. */
double tbox_layout_svg_auto_width(const tbox_html_node *node, const tbox_style *style, double vertical_edges) {
    tbox_rect vb;
    bool ratio = tbox_svg_viewbox(node, &vb) && vb.width > 0 && vb.height > 0;
    if (style->height.kind == TBOX_STYLE_LENGTH_PX && ratio) {
        double h = style->height.value;
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            h -= vertical_edges;
        return fmax(0, h) * vb.width / vb.height;
    }
    return 300 * tbox_style_zoom(style);
}
double tbox_layout_svg_height(const tbox_html_node *node, const tbox_style *style, tbox_layout_containing_block container, double width, double vertical_edges) {
    double height;
    bool explicit_height = style->height.kind == TBOX_STYLE_LENGTH_PX || (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite);
    if (explicit_height) {
        height = tbox_style_length_resolve(style->height, container.height);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            height -= vertical_edges;
    } else {
        tbox_rect vb;
        height = tbox_svg_viewbox(node, &vb) && vb.width > 0 && vb.height > 0 ? width * vb.height / vb.width : 150 * tbox_style_zoom(style);
    }
    return fmax(0, height);
}
