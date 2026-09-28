#include "tbox_layout_internal.h"

bool tbox_layout_is_select(const tbox_html_node *node) {
    return tbox_layout_tag(node, "select");
}
