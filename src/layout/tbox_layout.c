#include "tbox_layout_internal.h"
#include <string.h>

#include "style/tbox_style_internal.h"

tbox_layout_classification tbox_layout_classify(const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, const double *row_column_widths, size_t row_column_count) {
    tbox_layout_classification kind = { 0 };
    kind.text                       = tbox_layout_is_inline_box(node, style, styles) && !tbox_layout_is_multicol(style);
    kind.image     = tbox_layout_is_replaced_image(node);
    kind.table     = node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "table");
    kind.table_row = row_column_widths != NULL && row_column_count > 0 && tbox_layout_tag(node, "tr");
    kind.flex      = (style->display == TBOX_STYLE_DISPLAY_FLEX || style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) && !tbox_layout_tag(node, "svg") && !kind.image && !kind.table && !kind.table_row && !tbox_layout_tag(node, "input") && !tbox_layout_is_select(node) && !tbox_layout_is_textarea(node);
    kind.grid = (style->display == TBOX_STYLE_DISPLAY_GRID || style->display == TBOX_STYLE_DISPLAY_INLINE_GRID) && !tbox_layout_tag(node, "svg") && !kind.image && !kind.table && !kind.table_row && !tbox_layout_tag(node, "input") && !tbox_layout_is_select(node) && !tbox_layout_is_textarea(node);
    return kind;
}

/* Motion paths refer to the box establishing the containing block,
 * before transforms. Ancestor translations have already moved both this
 * box and its reference box by the same amount. */
static tbox_rect tbox_layout_motion_reference(const tbox_layout_box *box, tbox_rect viewport, tbox_style_clip_path *outline) {
    const tbox_style *style = box->style;
    const tbox_layout_box *parent = box->parent;
    bool positioned = style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED;
    while (parent != NULL) {
        const tbox_style *p = parent->style;
        if (p == NULL && parent->has_transform)
            break; /* a writing-mode frame: its box, in the frame's coordinates (tbox_layout_writing.c) */
        if (p != NULL && ((!positioned && p->display != TBOX_STYLE_DISPLAY_INLINE) ||
            (positioned && (tbox_style_has_transform(p) || (style->position != TBOX_STYLE_POSITION_FIXED && p->position != TBOX_STYLE_POSITION_STATIC)))))
            break;
        parent = parent->parent;
    }
    if (parent == NULL) return viewport;
    tbox_rect reference = parent->border_box;
    switch (tbox_style_offset_path_of(style)->coord_box) {
    case TBOX_STYLE_COORD_PADDING_BOX: reference = parent->padding_box; break;
    case TBOX_STYLE_COORD_CONTENT_BOX: reference = parent->content_box; break;
    case TBOX_STYLE_COORD_MARGIN_BOX: reference = parent->margin_box; break;
    default: break;
    }
    if (tbox_style_offset_path_of(style)->kind == TBOX_STYLE_OFFSET_PATH_BOX && parent->style != NULL) {
        const tbox_style *p = parent->style;
        const double edges[4] = {
            reference.y - parent->border_box.y,
            parent->border_box.x + parent->border_box.width - reference.x - reference.width,
            parent->border_box.y + parent->border_box.height - reference.y - reference.height,
            reference.x - parent->border_box.x,
        };
        /* Inner/outer parallel curves use the selected box's edge
         * offsets, with percent radii resolved against the border box. */
        const size_t horizontal[4] = { 3, 1, 1, 3 }, vertical[4] = { 0, 0, 2, 2 };
        for (size_t i = 0; i < 4; i++) {
            double rx = p->border_radius_corners[i] + parent->border_box.width * p->border_radius_percent[i] / 100.0;
            double ry = p->border_radius_vertical[i] + parent->border_box.height * p->border_radius_vertical_percent[i] / 100.0;
            outline->round_h[i] = rx > edges[horizontal[i]] ? rx - edges[horizontal[i]] : 0.0;
            outline->round_v[i] = ry > edges[vertical[i]] ? ry - edges[vertical[i]] : 0.0;
            if (rx == 0.0 || ry == 0.0) outline->round_h[i] = outline->round_v[i] = 0.0;
        }
    }
    return reference;
}

/* After the whole tree is laid out: decode background, mask and border
 * images, and apply transforms. A pure translation is a visual shift of
 * the box and everything in it, like position: relative's; any other
 * transform stays a matrix on the box (has_transform), relative to its
 * border box as the parent's own shift left it. */
static void tbox_layout_finish(tbox_layout_box *box, tbox_image_cache *images, tbox_arena *arena, tbox_rect viewport) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_style *style = box->style;
        if (style != NULL) {
            const char *background = tbox_style_background_source_of(style)->image;
            if (background[0] != '\0' && images != NULL)
                box->background_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(background));
            for (size_t l = 1; l < style->background_layer_count && l < 4 && images != NULL; l++)
                if (tbox_style_background_layers_of(style)->layers[l - 1].image[0] != '\0')
                    box->background_layer_images[l - 1] = tbox_image_cache_get(images, tbox_string_view_from_cstr(tbox_style_background_layers_of(style)->layers[l - 1].image));
            const tbox_style_masks *masks = tbox_style_masks_of(style);
            if (style->has_mask && masks->layers[0].source.image[0] != '\0' && images != NULL)
                box->mask_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(masks->layers[0].source.image));
            for (size_t l = 0; l < masks->count && style->has_mask && images != NULL; l++)
                if (masks->layers[l].source.image[0] != '\0')
                    box->mask_layer_images[l] = tbox_image_cache_get(images, tbox_string_view_from_cstr(masks->layers[l].source.image));
            if (style->has_border_image && tbox_style_border_image_of(style)->image[0] != '\0' && images != NULL)
                box->border_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(tbox_style_border_image_of(style)->image));
            if (tbox_style_box_reflect_of(style)->image[0] != '\0' && images != NULL)
                box->reflection_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(tbox_style_box_reflect_of(style)->image));
            for (size_t r = 0; r < box->text_run_count; r++) {
                tbox_layout_text_run *run = &box->text_runs[r];
                memset(run->decoration_images, 0, sizeof(run->decoration_images));
                run->decoration_border_image = NULL;
                if (images == NULL || run->style == NULL || run->style == style || run->image != NULL) continue;
                for (size_t l = 0; l < TBOX_STYLE_MAX_BACKGROUND_LAYERS; l++) {
                    const char *image = l == 0 ? tbox_style_background_source_of(run->style)->image : tbox_style_background_layers_of(run->style)->layers[l - 1].image;
                    if (image[0] != '\0') run->decoration_images[l] = tbox_image_cache_get(images, tbox_string_view_from_cstr(image));
                }
                if (run->style->has_border_image && tbox_style_border_image_of(run->style)->image[0] != '\0')
                    run->decoration_border_image = tbox_image_cache_get(images, tbox_string_view_from_cstr(tbox_style_border_image_of(run->style)->image));
            }
            box->has_transform = false;
            double m[6], offset[6];
            const double *motion = NULL;
            if (tbox_style_offset_path_of(style)->kind != TBOX_STYLE_OFFSET_PATH_NONE) {
                tbox_style_clip_path outline = { .kind = TBOX_STYLE_CLIP_PATH_INSET };
                tbox_rect reference = tbox_layout_motion_reference(box, viewport, &outline);
                tbox_rect motion_box = box->border_box;
                if (style->layout_frame != TBOX_STYLE_FRAME_NONE) { /* sampled upright, like the transform */
                    motion_box = tbox_layout_frame_upright(style, box->border_box, box->border_box);
                    reference  = tbox_layout_frame_upright(style, box->border_box, reference);
                }
                if (tbox_style_motion_matrix(style, motion_box, reference, &outline, arena, offset)) motion = offset;
            }
            bool transformed = style->layout_frame != TBOX_STYLE_FRAME_NONE ? tbox_layout_frame_transform(style, box->border_box, motion, m) :
                tbox_style_transform_matrix(style, box->border_box.x, box->border_box.y, box->border_box.width, box->border_box.height, motion, m);
            if (transformed) {
                if (m[0] == 1.0 && m[1] == 0.0 && m[2] == 0.0 && m[3] == 1.0) {
                    tbox_layout_translate(box, m[4], m[5]);
                } else {
                    box->has_transform = true;
                    memcpy(box->transform, m, sizeof(m));
                }
            }
        }
        tbox_layout_finish(box->first_child, images, arena, viewport);
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
        unsigned char generated = styles->items[i].style->generated;
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
    tbox_layout_finish(box, images, arena, viewport_rect);
    return box;
}
