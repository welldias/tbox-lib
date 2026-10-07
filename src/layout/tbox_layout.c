#include "tbox_layout_internal.h"

tbox_layout_classification tbox_layout_classify(const tbox_html_node *node, const tbox_style *style, const double *row_column_widths, size_t row_column_count) {
    tbox_layout_classification kind = { 0 };
    kind.text                       = tbox_layout_is_text_box(node, style);
    if (tbox_layout_table_cell_node(node)) {
        for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
            if (child->type != TBOX_HTML_NODE_ELEMENT)
                continue;
            if (tbox_layout_tag(child, "div") || tbox_layout_tag(child, "p") || tbox_layout_tag(child, "table") || tbox_layout_tag(child, "ul") || tbox_layout_tag(child, "ol") || tbox_layout_tag(child, "section")) {
                kind.text = false;
                break;
            }
        }
    }
    kind.image     = tbox_layout_is_replaced_image(node);
    kind.table     = node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "table");
    kind.table_row = row_column_widths != NULL && row_column_count > 0 && tbox_layout_tag(node, "tr");
    kind.flex      = (style->display == TBOX_STYLE_DISPLAY_FLEX || style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) && !kind.image && !kind.table && !kind.table_row && !tbox_layout_tag(node, "input") && !tbox_layout_is_select(node) && !tbox_layout_is_textarea(node);
    return kind;
}

/* After the whole tree is laid out: decode background images, and apply
 * `transform: translate()`/`translate` -- a purely visual shift of the
 * box and everything in it, like position: relative's, with percentages
 * of the box's own border box. */
static void tbox_layout_finish(tbox_layout_box *box, tbox_image_cache *images) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_style *style = box->style;
        if (style != NULL) {
            if (style->background_image[0] != '\0' && images != NULL)
                box->background_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(style->background_image));
            for (size_t l = 1; l < style->background_layer_count && l < 4 && images != NULL; l++)
                if (style->background_layers[l - 1].image[0] != '\0')
                    box->background_layer_images[l - 1] = tbox_image_cache_get(images, tbox_string_view_from_cstr(style->background_layers[l - 1].image));
            double dx = tbox_style_length_resolve(style->translate_x, box->border_box.width);
            double dy = tbox_style_length_resolve(style->translate_y, box->border_box.height);
            if (dx != 0.0 || dy != 0.0)
                tbox_layout_translate(box, dx, dy);
        }
        tbox_layout_finish(box->first_child, images);
    }
}

tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height) {
    if (root == NULL) {
        return NULL;
    }

    /* A DOCUMENT node is transparent -- it is never itself styled (the
     * Style layer skips it, same as TEXT/COMMENT/DOCTYPE) -- so build the
     * one box for its first ELEMENT child instead (typically <html> in a
     * full document). A caller may also pass an ELEMENT node directly, to
     * lay out one fragment in isolation. Anything else (a bare TEXT/
     * COMMENT/DOCTYPE root) has nothing to lay out. */
    const tbox_html_node *element = NULL;
    if (root->type == TBOX_HTML_NODE_DOCUMENT) {
        for (const tbox_html_node *child = root->first_child; child != NULL; child = child->next_sibling) {
            if (child->type == TBOX_HTML_NODE_ELEMENT) {
                element = child;
                break;
            }
        }
    } else if (root->type == TBOX_HTML_NODE_ELEMENT) {
        element = root;
    }

    if (element == NULL) {
        return NULL;
    }

    if (tbox_layout_is_hidden_input(element) || tbox_layout_style_or_default(styles, element)->display == TBOX_STYLE_DISPLAY_NONE) {
        return NULL;
    }

    tbox_layout_containing_block viewport = {
        .x               = 0.0,
        .y               = 0.0,
        .width           = viewport_width,
        .height          = viewport_height,
        .height_definite = true, /* the viewport's height is always a concrete number */
    };

    /* no positioned ancestor exists yet at the root -- both
     * `nearest_ancestor` and `viewport` start out as the same initial
     * containing block (CSS2.1's rule: with no positioned ancestor, an
     * absolute box's containing block is the initial containing block). See
     * tbox_layout_positioned_context above. */
    tbox_rect viewport_rect                                = { .x = 0.0, .y = 0.0, .width = viewport_width, .height = viewport_height };
    tbox_layout_positioned_context root_positioned_context = {
        .nearest_ancestor = viewport_rect,
        .viewport         = viewport_rect,
    };
    tbox_layout_box *box = tbox_layout_build_element(arena, element, styles, fonts, images, viewport, 0.0, root_positioned_context, NULL, 0);
    tbox_layout_finish(box, images);
    return box;
}
