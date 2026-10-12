#include "tbox_layout_internal.h"

tbox_layout_classification tbox_layout_classify(const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, const double *row_column_widths, size_t row_column_count) {
    tbox_layout_classification kind = { 0 };
    kind.text                       = tbox_layout_is_inline_box(node, style, styles);
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
            if (style->has_mask && style->mask.image[0] != '\0' && images != NULL)
                box->mask_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(style->mask.image));
            double dx = tbox_style_length_resolve(style->translate_x, box->border_box.width);
            double dy = tbox_style_length_resolve(style->translate_y, box->border_box.height);
            if (dx != 0.0 || dy != 0.0)
                tbox_layout_translate(box, dx, dy);
        }
        tbox_layout_finish(box->first_child, images);
    }
}

/* ::before/::after: the style table's synthetic nodes (tbox_style
 * `generated`) become the first/last child of their element while the
 * tree is laid out, so every layout path sees them as ordinary children;
 * tbox_layout_build unlinks them before returning, leaving the caller's
 * document as it was. Boxes keep pointing at the synthetic nodes, which
 * live as long as the style table. */
static void tbox_layout_link_generated(const tbox_style_table *styles, bool link) {
    for (size_t i = 0; styles != NULL && i < styles->count; i++) {
        unsigned char generated = styles->items[i].style.generated;
        if (generated == 0)
            continue;
        tbox_html_node *node   = (tbox_html_node *)styles->items[i].node;
        tbox_html_node *parent = node->parent;
        if (link) {
            node->prev_sibling = node->next_sibling = NULL;
            if (generated == 1) {
                node->next_sibling = parent->first_child;
                if (parent->first_child != NULL) parent->first_child->prev_sibling = node;
                else parent->last_child = node;
                parent->first_child = node;
            } else {
                node->prev_sibling = parent->last_child;
                if (parent->last_child != NULL) parent->last_child->next_sibling = node;
                else parent->first_child = node;
                parent->last_child = node;
            }
        } else {
            if (node->prev_sibling != NULL) node->prev_sibling->next_sibling = node->next_sibling;
            else parent->first_child = node->next_sibling;
            if (node->next_sibling != NULL) node->next_sibling->prev_sibling = node->prev_sibling;
            else parent->last_child = node->prev_sibling;
            node->prev_sibling = node->next_sibling = NULL;
        }
    }
}

static tbox_layout_box *tbox_layout_build_tree(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height);

/* Half-size faces for string emphasis marks, on every text run whose
 * style asks for them (NULL on the others). */
static void tbox_layout_emphasis_fonts(tbox_layout_box *box, tbox_font_face_cache *fonts) {
    for (; box != NULL; box = box->next_sibling) {
        for (size_t i = 0; i < box->text_run_count; i++) {
            tbox_layout_text_run *run = &box->text_runs[i];
            run->emphasis_font        = NULL; /* runs are pushed unzeroed */
            if (run->image != NULL || run->style == NULL || run->style->text_emphasis_mark != TBOX_STYLE_EMPHASIS_STRING)
                continue;
            tbox_style half = *run->style;
            half.font_size *= 0.5;
            half.font_size_adjust = 0.0;
            run->emphasis_font = tbox_layout_style_face(fonts, &half);
        }
        tbox_layout_emphasis_fonts(box->first_child, fonts);
    }
}

tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height) {
    tbox_layout_link_generated(styles, true);
    tbox_layout_box *box = tbox_layout_build_tree(arena, root, styles, fonts, images, viewport_width, viewport_height);
    tbox_layout_link_generated(styles, false);
    tbox_layout_emphasis_fonts(box, fonts);
    return box;
}

static tbox_layout_box *tbox_layout_build_tree(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height) {
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
