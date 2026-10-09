#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* Builds and positions the box for one ELEMENT `node` (already known to not
 * be display:none -- the caller checks that before recursing, see
 * tbox_layout_build_children and tbox_layout_build) against `container`
 * (its parent's, or the viewport's, content box -- or, for an
 * `absolute`/`fixed` box, the positioned containing block tbox_layout_build_children
 * already carved out for it) with its margin_box's top edge at `cursor_y`
 * (flow boxes only -- an `absolute`/`fixed` box ignores `cursor_y` entirely
 * and positions itself against `container.x`/`container.y` instead, see
 * below). `positioned_context`  is what this box's OWN descendants,
 * if any, will use to position themselves if they turn out to be
 * `absolute`/`fixed` -- see tbox_layout_positioned_context above. */
tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count) {
    return tbox_layout_build_element_sized(arena, node, styles, fonts, images, container, cursor_y, positioned_context, row_column_widths, row_column_count, NULL);
}

tbox_layout_box *tbox_layout_build_element_sized(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count, const tbox_layout_forced_size *forced) {
    const tbox_style *style                  = tbox_layout_style_or_default(styles, node);
    tbox_layout_classification kind          = tbox_layout_classify(node, style, row_column_widths, row_column_count);
    bool is_text_tag                         = kind.text;
    bool is_image_input                      = kind.image;
    bool is_table                            = kind.table;
    bool is_table_row                        = kind.table_row;
    bool is_flex                             = kind.flex;
    tbox_layout_replaced_image image_content = { 0 };
    if (is_image_input)
        image_content = tbox_layout_image_prepare(arena, node, style, fonts, images);

    /* RELATIVE/ABSOLUTE/FIXED/STICKY all count as "positioned" for
     * being a containing block (see tbox_layout_positioned_context), but
     * only ABSOLUTE/FIXED are actually placed by resolving left/right/top/
     * bottom against `container` instead of flowing at `cursor_y` -- RELATIVE
     * and STICKY (== RELATIVE, see ARCHITECTURE.md's scope) still flow
     * normally and only shift visually, unchanged since v4. */
    bool is_out_of_flow = (style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED);
    /* Zero-initialized: text_runs/text_run_count/parent/first_child/
     * last_child/next_sibling all start at their empty/NULL default and are
     * only overwritten where this function (or tbox_layout_build_children,
     * for the sibling links) has something to put there. */
    tbox_layout_box *box = (tbox_layout_box *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_box));
    box->node            = node;
    box->style           = style;

    double margin_top    = tbox_layout_resolve_edge(style->margin[0], container.width);
    double margin_right  = tbox_layout_resolve_edge(style->margin[1], container.width);
    double margin_bottom = tbox_layout_resolve_edge(style->margin[2], container.width);
    double margin_left   = tbox_layout_resolve_edge(style->margin[3], container.width);

    double padding_top    = tbox_layout_resolve_edge(style->padding[0], container.width);
    double padding_right  = tbox_layout_resolve_edge(style->padding[1], container.width);
    double padding_bottom = tbox_layout_resolve_edge(style->padding[2], container.width);
    double padding_left   = tbox_layout_resolve_edge(style->padding[3], container.width);

    /* `border` occupies space exactly like padding does, but only when
     * `border-style: solid` actually applies; a declared border-width/color
     * without `solid` (or with the initial `none`) occupies zero space,
     * same as not declaring `border` at all. */
    double border_top       = tbox_style_border_side_width(style, 0);
    double border_right     = tbox_style_border_side_width(style, 1);
    double border_bottom    = tbox_style_border_side_width(style, 2);
    double border_left      = tbox_style_border_side_width(style, 3);
    double horizontal_edges = padding_left + padding_right + border_left + border_right;
    double vertical_edges   = padding_top + padding_bottom + border_top + border_bottom;

    /* Width: the same rule for every node, text-tag or not -- see
     * ARCHITECTURE.md's clarification that measured text never resizes the
     * box (D4). Border counts against the available width in the AUTO
     * branch exactly like padding does. */
    double content_width;
    switch (style->width.kind) {
    case TBOX_STYLE_LENGTH_PX:
        content_width = style->width.value - (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? horizontal_edges : 0.0);
        break;
    case TBOX_STYLE_LENGTH_PERCENT:
        content_width = tbox_style_length_resolve(style->width, container.width) - (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? horizontal_edges : 0.0);
        break;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        content_width = container.width - margin_left - margin_right - horizontal_edges;
        /* An absolute/fixed box with both `left` and `right` stretches
         * between them (CSS2.1 10.3.7), instead of taking the whole width. */
        if (is_out_of_flow && style->offset[1].kind != TBOX_STYLE_LENGTH_AUTO && style->offset[3].kind != TBOX_STYLE_LENGTH_AUTO)
            content_width -= tbox_layout_resolve_edge(style->offset[1], container.width) + tbox_layout_resolve_edge(style->offset[3], container.width);
        if (is_image_input)
            content_width = tbox_layout_image_auto_width(&image_content, style, container, vertical_edges);
        if (tbox_layout_is_textarea(node))
            content_width = tbox_layout_textarea_auto_width(node, style, fonts, content_width);
        /* min-/max-/fit-content: the intrinsic widths instead of filling
         * the containing block (fit-content: the available width, kept
         * between the two). */
        if (style->width_keyword != TBOX_STYLE_SIZE_KEYWORD_NONE && !is_image_input && !tbox_layout_is_textarea(node) && !is_table_row) {
            tbox_layout_intrinsic outer = tbox_layout_intrinsic_outer(arena, node, styles, fonts, images);
            double outside              = margin_left + margin_right + horizontal_edges;
            double minimum = outer.min - outside, maximum = outer.max - outside;
            if (style->width_keyword == TBOX_STYLE_SIZE_KEYWORD_MIN_CONTENT)
                content_width = minimum;
            else if (style->width_keyword == TBOX_STYLE_SIZE_KEYWORD_MAX_CONTENT)
                content_width = maximum;
            else
                content_width = content_width > maximum ? maximum : content_width < minimum ? minimum : content_width;
        }
        break;
    }
    if (row_column_widths != NULL && row_column_count == 0 && tbox_layout_table_cell_node(node))
        content_width = container.width - margin_left - margin_right - padding_left - padding_right - border_left - border_right;
    if (content_width < 0.0)
        content_width = 0.0;
    /* Grid cells take their final width from the column algorithm, where
     * min-width is included as a column constraint. Clamping a cell alone
     * would leave gaps or overlap its neighbors. */
    if (!is_table_row && !tbox_layout_table_cell_node(node))
        content_width = tbox_layout_constrain_width(style, content_width, container.width, horizontal_edges);
    if (forced != NULL && forced->has_width)
        content_width = forced->width > horizontal_edges ? forced->width - horizontal_edges : 0.0;

    /* aspect-ratio (width / height, of box-sizing's box): an auto height
     * follows the width; an auto width follows a definite height. Content
     * taller than the ratio still grows the box unless it clips (CSS's
     * automatic minimum size). */
    bool ratio_height           = false;
    double ratio_content_height = 0.0;
    if (style->aspect_ratio > 0.0 && !is_image_input && !is_table && !is_table_row && !(forced != NULL && forced->has_height)) {
        bool border_box     = style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX;
        bool height_defined = style->height.kind == TBOX_STYLE_LENGTH_PX || (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite);
        if (!height_defined) {
            double outer         = (border_box ? content_width + horizontal_edges : content_width) / style->aspect_ratio;
            ratio_content_height = border_box ? outer - vertical_edges : outer;
            if (ratio_content_height < 0.0)
                ratio_content_height = 0.0;
            ratio_content_height = tbox_layout_constrain_height(style, ratio_content_height, container.height, container.height_definite, vertical_edges);
            ratio_height         = true;
        } else if (style->width.kind == TBOX_STYLE_LENGTH_AUTO && !(forced != NULL && forced->has_width) && !tbox_layout_table_cell_node(node)) {
            double outer  = tbox_style_length_resolve(style->height, container.height) * style->aspect_ratio;
            content_width = border_box ? outer - horizontal_edges : outer;
            if (content_width < 0.0)
                content_width = 0.0;
            content_width = tbox_layout_constrain_width(style, content_width, container.width, horizontal_edges);
        }
    }
    /* A stable scrollbar consumes a strip inside the existing padding box. */
    if (style->scrollbar_gutter_stable && style->scrollbar_width != TBOX_STYLE_SCROLLBAR_WIDTH_NONE &&
        style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE) {
        double gutter = style->scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_THIN ? 6.0 : 10.0;
        if (gutter > content_width) gutter = content_width;
        content_width -= gutter;
        padding_right += gutter;
        box->scrollbar_gutter = gutter;
    }
    bool ratio_grows = ratio_height && style->overflow_y == TBOX_STYLE_OVERFLOW_Y_VISIBLE;

    double content_x;
    double content_y;

    if (is_out_of_flow) {
        /* absolute/fixed geometry -- resolve the MARGIN BOX
         * position against `container` (already the right positioned
         * containing block by construction, see tbox_layout_build_children)
         * instead of flowing at cursor_y. See ARCHITECTURE.md "Layout Tree
         * -- geometria de absolute/fixed" and tbox_layout_resolve_absolute_edge
         * above for the full rationale. */

        /* Horizontal: never circular -- content_width above is already
         * resolved against container.width regardless of position, so
         * margin_box.width is known outright before positioning. */
        double border_box_width = content_width + horizontal_edges;
        double margin_box_width = border_box_width + margin_left + margin_right;
        double margin_box_x     = tbox_layout_resolve_absolute_edge(style->offset[3], style->offset[1], container.x, container.width, margin_box_width);
        double border_box_x     = margin_box_x + margin_left;
        content_x               = border_box_x + border_left + padding_left;

        /* Vertical: `top` non-AUTO resolves outright, no circularity (children
         * are laid out normally afterwards, and an AUTO content_height still
         * just sums them as usual). `top` AUTO but `bottom` non-AUTO needs
         * margin_box.height up front, which is only knowable ahead of the
         * children/text pass when style->height is itself definite (PX, or a
         * PERCENT against an already-definite container) AND this isn't a
         * text tag (its text layout is completed below, so the height is
         * not used for early absolute-position calculations).
         * Otherwise -- both AUTO, or the genuinely circular
         * top:auto+bottom:defined+height:auto case -- falls back to the
         * containing block's own origin, the simplification documented in
         * ARCHITECTURE.md's "Escopo deliberadamente contido" and "Layout
         * Tree -- geometria de absolute/fixed". */
        bool top_auto           = style->offset[0].kind == TBOX_STYLE_LENGTH_AUTO;
        bool bottom_auto        = style->offset[2].kind == TBOX_STYLE_LENGTH_AUTO;
        bool height_known_early = !is_text_tag && (style->height.kind == TBOX_STYLE_LENGTH_PX || (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite));

        double margin_box_y;
        if (!top_auto || bottom_auto || height_known_early) {
            double margin_box_height = 0.0; /* only read by tbox_layout_resolve_absolute_edge's opposite-side branch, taken below */
            if (top_auto && !bottom_auto && height_known_early) {
                double early_content_height = (style->height.kind == TBOX_STYLE_LENGTH_PX) ? style->height.value : tbox_style_length_resolve(style->height, container.height);
                if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                    early_content_height = early_content_height > vertical_edges ? early_content_height - vertical_edges : 0.0;
                early_content_height           = tbox_layout_constrain_height(style, early_content_height, container.height, container.height_definite, vertical_edges);
                double early_border_box_height = early_content_height + vertical_edges;
                margin_box_height              = early_border_box_height + margin_top + margin_bottom;
            }
            margin_box_y = tbox_layout_resolve_absolute_edge(style->offset[0], style->offset[2], container.y, container.height, margin_box_height);
        } else {
            margin_box_y = container.y; /* circular top:auto+bottom:defined+height:auto case -- see comment above */
        }

        double border_box_y = margin_box_y + margin_top;
        content_y           = border_box_y + border_top + padding_top;
    } else {
        content_x = container.x + margin_left + padding_left + border_left;
        content_y = cursor_y + margin_top + padding_top + border_top;
    }

    /* `position: relative` -- a pure visual-coordinate shift, no
     * new containing-block concept (confirmed in ARCHITECTURE.md). Applied
     * to content_x/content_y BEFORE anything below derives from them --
     * children_container (so the whole subtree shifts automatically) and
     * content_box/padding_box/border_box/margin_box (all built from
     * content_x/content_y further down). Deliberately NOT applied to
     * cursor_y/margin_box.height as seen by the sibling loop in
     * tbox_layout_build_children: those are computed independently of
     * content_x/content_y (cursor_y is the function's own parameter, and
     * margin_box.height only ever depends on content_height/padding/
     * border/margin, never x/y), so a `position: relative` box never
     * disturbs the normal flow of any other element, exactly as CSS
     * specifies. `position: sticky` takes this exact same branch --
     * ARCHITECTURE.md documents `sticky` as an exact synonym of `relative`
     * (no scrollport anywhere in the project for a "stuck" threshold to ever
     * cross), so it must resolve to IDENTICAL geometry given the same
     * offsets, not just similar. */
    /* sticky boxes are placed by Context once scroll offsets are known
     * (tbox_context_apply_sticky), not shifted here. */
    if (style->position == TBOX_STYLE_POSITION_RELATIVE) {
        double dx = tbox_layout_resolve_offset(style->offset[3], style->offset[1], container.width, true);
        double dy = tbox_layout_resolve_offset(style->offset[0], style->offset[2], container.height, container.height_definite);
        content_x += dx;
        content_y += dy;
    }

    /* Same stretch vertically (CSS2.1 10.6.4): auto height with both `top`
     * and `bottom` fills the containing block between them. */
    bool stretch_height     = is_out_of_flow && style->height.kind == TBOX_STYLE_LENGTH_AUTO && style->offset[0].kind != TBOX_STYLE_LENGTH_AUTO && style->offset[2].kind != TBOX_STYLE_LENGTH_AUTO && container.height_definite;
    double stretched_height = container.height - margin_top - margin_bottom - vertical_edges - tbox_layout_resolve_edge(style->offset[0], container.height) - tbox_layout_resolve_edge(style->offset[2], container.height);
    if (stretched_height < 0.0)
        stretched_height = 0.0;
    double content_height;
    if (is_image_input) {
        content_height = tbox_layout_image_height(&image_content, style, container, content_width, vertical_edges);
    } else if (is_flex) {
        bool height_definite = false;
        if (forced != NULL && forced->has_height) {
            content_height  = forced->height > vertical_edges ? forced->height - vertical_edges : 0.0;
            height_definite = true;
        } else if (style->height.kind == TBOX_STYLE_LENGTH_PX || (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)) {
            content_height = style->height.kind == TBOX_STYLE_LENGTH_PX ? style->height.value : tbox_style_length_resolve(style->height, container.height);
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
            height_definite = true;
        } else if (stretch_height) {
            content_height  = stretched_height;
            height_definite = true;
        } else if (ratio_height) {
            content_height  = ratio_content_height;
            height_definite = true;
        } else {
            content_height = 0.0;
        }
        /* A definite max-height caps a column container's main size. */
        if (height_definite)
            content_height = tbox_layout_constrain_height(style, content_height, container.height, container.height_definite, vertical_edges);
        tbox_layout_positioned_context context_for_children = positioned_context;
        if (style->position != TBOX_STYLE_POSITION_STATIC) {
            context_for_children.nearest_ancestor = (tbox_rect){ content_x - padding_left, content_y - padding_top, content_width + padding_left + padding_right, content_height + padding_top + padding_bottom };
        }
        double min_height = tbox_layout_constrain_height(style, 0.0, container.height, container.height_definite, vertical_edges);
        double max_height = tbox_layout_constrain_height(style, TBOX_LAYOUT_FLEX_INFINITE, container.height, container.height_definite, vertical_edges);
        double extent     = tbox_layout_build_flex(arena, node, style, styles, fonts, images, content_x, content_y, content_width, content_height, height_definite, min_height, max_height, box, context_for_children);
        if (!height_definite)
            content_height = extent;
    } else if (is_text_tag) {
        /* Text-tag leaf: no child boxes even though the DOM node may have
         * element descendants (e.g. <b> inside a <p>) -- those only
         * contribute words to this box's own text_runs (, real
         * inline formatting context -- see tbox_layout_build_text_runs),
         * never a box of their own. Height is the sum of the wrapped
         * lines' heights (or one face's line-height for empty text) -- see
         * tbox_layout_build_text_runs's doc comment. */
        tbox_layout_positioned_context context_for_children = positioned_context;
        if (style->position != TBOX_STYLE_POSITION_STATIC) /* height unknown yet, same simplification as blocks */
            context_for_children.nearest_ancestor = (tbox_rect){ content_x - padding_left, content_y - padding_top, content_width + padding_left + padding_right, padding_top + padding_bottom };
        content_height = tbox_layout_build_text_runs(arena, node, node->first_child, NULL, style, styles, fonts, images, content_x, content_y, content_width, box, &context_for_children);
        if (tbox_layout_is_textarea(node)) {
            content_height = tbox_layout_textarea_height(node, style, fonts, container, vertical_edges, content_height);
        } else if (style->height.kind == TBOX_STYLE_LENGTH_PX || (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)) {
            content_height = style->height.kind == TBOX_STYLE_LENGTH_PX ? style->height.value : tbox_style_length_resolve(style->height, container.height);
            if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
        } else if (ratio_height && (!ratio_grows || content_height < ratio_content_height)) {
            content_height = ratio_content_height;
        }
    } else if (is_table) {
        /* NOVO (table support): a <table>'s content_height is ALWAYS the
         * summed row heights, same "content always dictates height,
         * style->height is never consulted" posture is_text_tag already
         * has above -- no PX/PERCENT `height` branch, no positioned-context
         * threading for descendants (a `position: absolute` box inside a
         * table resolving against the table itself is out of scope). */
        content_height = tbox_layout_table_extended(node, styles) ? tbox_layout_build_table_extended(arena, node, styles, fonts, images, content_x, content_y, &content_width, box, positioned_context) : tbox_layout_build_table_children(arena, node, styles, fonts, images, content_x, content_y, &content_width, box, positioned_context);
    } else if (is_table_row) {
        /* NOVO (table support): same posture as the <table> branch above --
         * content always dictates a row's height. */
        content_height = tbox_layout_build_table_row_children(arena, node, styles, fonts, images, content_x, content_y, row_column_widths, row_column_count, box, positioned_context);
    } else {
        /* Container node: recurse into ELEMENT children first (their
         * containing block is this node's own content box), then decide
         * this node's own height. PX is definite outright; PERCENT is
         * definite only when the containing block's own height is (CSS2.1
         * 10.5 -- percentage height against an auto-height container
         * computes to auto, the common v0 case); anything else (AUTO, or
         * that PERCENT fallback) is shrink-to-fit: the sum of every child's
         * margin_box.height, which requires children to be laid out first. */
        bool height_definite = false;
        if (forced != NULL && forced->has_height) {
            content_height  = forced->height > vertical_edges ? forced->height - vertical_edges : 0.0;
            height_definite = true;
        } else
            switch (style->height.kind) {
            case TBOX_STYLE_LENGTH_PX:
                content_height = style->height.value;
                if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                    content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
                height_definite = true;
                break;
            case TBOX_STYLE_LENGTH_PERCENT:
                if (container.height_definite) {
                    content_height = tbox_style_length_resolve(style->height, container.height);
                    if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
                        content_height = content_height > vertical_edges ? content_height - vertical_edges : 0.0;
                    height_definite = true;
                } else {
                    content_height = 0.0; /* placeholder; replaced by the children sum below */
                }
                break;
            case TBOX_STYLE_LENGTH_AUTO:
            default:
                content_height = 0.0; /* placeholder; replaced by the children sum below */
                if (stretch_height) {
                    content_height  = stretched_height;
                    height_definite = true;
                }
                break;
            }
        if (ratio_height && !height_definite) {
            content_height  = ratio_content_height;
            height_definite = true;
        }

        /* decide what positioned_context THIS box's own descendants
         * see, per ARCHITECTURE.md "Layout Tree -- containing block
         * posicionado" -- any non-STATIC position (RELATIVE/ABSOLUTE/FIXED/
         * STICKY all count as "positioned" for this purpose, same as CSS)
         * makes this box's own padding_box the nearest_ancestor passed down
         * to tbox_layout_build_children; `viewport` never changes. Computed
         * here, before recursing into children, from content_x/content_y/
         * content_width/content_height as known AT THIS POINT -- for a
         * positioned box whose OWN height is AUTO, that means `content_height`
         * is still the 0.0 placeholder above (its real, children-summed
         * value isn't known until after tbox_layout_build_children returns,
         * which is too late: children need nearest_ancestor to build
         * themselves). A documented simplification, not a bug: an
         * absolute/fixed descendant resolving `bottom` against such an
         * auto-height ancestor sees a too-small containing block. Not
         * exercised by this version's minimum test cases (which use a
         * definite-height or default-flow positioned ancestor). */
        tbox_layout_positioned_context context_for_children = positioned_context;
        if (style->position != TBOX_STYLE_POSITION_STATIC) {
            tbox_rect padding_box_now = {
                .x      = content_x - padding_left,
                .y      = content_y - padding_top,
                .width  = content_width + padding_left + padding_right,
                .height = content_height + padding_top + padding_bottom,
            };
            context_for_children.nearest_ancestor = padding_box_now;
        }

        tbox_layout_containing_block children_container = {
            .x               = content_x,
            .y               = content_y,
            .width           = content_width,
            .height          = content_height,
            .height_definite = height_definite,
        };
        double children_total_height = tbox_layout_build_children(arena, node, styles, fonts, images, children_container, content_y, box, context_for_children, style);
        box->scroll_content_height   = children_total_height;
        if (!height_definite || (ratio_grows && children_total_height > content_height)) {
            content_height = children_total_height;
        }
    }

    if (stretch_height)
        content_height = stretched_height; /* text boxes too, not only blocks */
    content_height = tbox_layout_constrain_height(style, content_height, container.height, container.height_definite, vertical_edges);
    if (forced != NULL && forced->has_height)
        content_height = forced->height > vertical_edges ? forced->height - vertical_edges : 0.0;

    box->content_box.x      = content_x;
    box->content_box.y      = content_y;
    box->content_box.width  = content_width;
    box->content_box.height = content_height;

    /* padding_box is content_box grown back out by padding (note:
     * content_x/content_y already include the left/top border -- see above --
     * so subtracting only padding_left/padding_top here correctly lands on
     * the padding_box edge, between border and padding).
     * border_box is padding_box grown back out by each side's own border
     * width (0.0 when there's no effective border, preserving the v0-v3
     * identity border_box == padding_box exactly). */
    box->padding_box.x      = content_x - padding_left;
    box->padding_box.y      = content_y - padding_top;
    box->padding_box.width  = content_width + padding_left + padding_right;
    box->padding_box.height = content_height + padding_top + padding_bottom;

    box->border_box.x      = box->padding_box.x - border_left;
    box->border_box.y      = box->padding_box.y - border_top;
    box->border_box.width  = box->padding_box.width + border_left + border_right;
    box->border_box.height = box->padding_box.height + border_top + border_bottom;

    /* margin_box is border_box grown back out by margin -- its x/y land back
     * on (container.x, cursor_y) exactly for a flow box, per the geometry
     * above -- or, , on (margin_box_x, margin_box_y) as resolved by
     * tbox_layout_resolve_absolute_edge for an absolute/fixed box, by the
     * same border_box.x = margin_box.x + margin_left relation used
     * everywhere else in this file. */
    box->margin_box.x      = box->border_box.x - margin_left;
    box->margin_box.y      = box->border_box.y - margin_top;
    box->margin_box.width  = box->border_box.width + margin_left + margin_right;
    box->margin_box.height = box->border_box.height + margin_top + margin_bottom;

    if (is_image_input)
        tbox_layout_image_append_run(arena, &image_content, style, box);

    tbox_layout_build_checkbox_checkmark(arena, node, style, fonts, box);

    return box;
}
