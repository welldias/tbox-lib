/* Vertical writing modes (docs/08-layout-e-pintura.md, "Escrita vertical").
 *
 * A box whose writing mode asks for another frame than the one it is laid
 * out in (tbox_style.layout_frame) starts a frame change: its subtree is
 * laid out as if it were horizontal, in the frame's own coordinates, with
 * every style turned into the frame (physical sides and sizes permuted),
 * and an anonymous box with a transform carries the result back. Layout of
 * horizontal documents never gets here: the check is one comparison. */
#include "tbox_layout_internal.h"

#include <string.h>

/* The linear part of `frame` (frame coordinates to the screen), as
 * x' = m[0] x + m[2] y, y' = m[1] x + m[3] y. */
static void tbox_layout_frame_matrix(tbox_style_frame frame, double m[4]) {
    static const double matrices[4][4] = {
        { 1, 0, 0, 1 },  /* NONE */
        { 0, 1, -1, 0 }, /* ROTATE_CW: (x, y) -> (-y, x) */
        { 0, 1, 1, 0 },  /* TRANSPOSE: (x, y) -> (y, x) */
        { 0, -1, 1, 0 }, /* ROTATE_CCW: (x, y) -> (y, -x) */
    };
    memcpy(m, matrices[(unsigned)frame < 4 ? frame : 0], 4 * sizeof(double));
}

bool tbox_layout_frame_change(const tbox_style *style) {
    return tbox_style_writing_mode_frame(style->writing_mode) != style->layout_frame;
}

/* `physical` (a style from the Style layer, in screen terms) turned into
 * `frame`'s coordinates: side i of the frame takes the physical side it
 * lands on, and since every vertical frame swaps the axes, widths become
 * heights. Backgrounds, shadows and transforms keep their screen values
 * (the transform is turned in tbox_layout_frame_transform). */
void tbox_layout_frame_remap(tbox_style *out, const tbox_style *physical, tbox_style_frame frame) {
    *out              = *physical;
    out->layout_frame = frame;
    if (frame == TBOX_STYLE_FRAME_NONE)
        return;
    for (size_t i = 0; i < 4; i++) {
        size_t p                = tbox_style_frame_side(frame, i);
        out->margin[i]          = physical->margin[p];
        out->padding[i]         = physical->padding[p];
        out->offset[i]          = physical->offset[p];
        out->scroll_margin[i]   = physical->scroll_margin[p];
        out->scroll_padding[i]  = physical->scroll_padding[p];
        out->border_widths[i]   = physical->border_per_side ? physical->border_widths[p] : physical->border_width;
        out->border_styles[i]   = tbox_style_border_side_style(physical, p);
        out->border_colors[i]   = tbox_style_border_side_color(physical, p);
    }
    out->border_per_side = true;
    out->border_width    = out->border_widths[0];
    out->border_style    = out->border_styles[0];
    out->border_color    = out->border_colors[0];
    /* corners: each frame corner is where its two sides land */
    static const size_t corner_sides[4][2] = { { 0, 3 }, { 0, 1 }, { 2, 1 }, { 2, 3 } };
    for (size_t c = 0; c < 4; c++) {
        size_t a = tbox_style_frame_side(frame, corner_sides[c][0]), b = tbox_style_frame_side(frame, corner_sides[c][1]);
        size_t pc = c;
        for (size_t k = 0; k < 4; k++)
            if ((corner_sides[k][0] == a && corner_sides[k][1] == b) || (corner_sides[k][0] == b && corner_sides[k][1] == a))
                pc = k;
        out->border_radius_corners[c]          = physical->border_radius_vertical[pc];
        out->border_radius_vertical[c]         = physical->border_radius_corners[pc];
        out->border_radius_percent[c]          = physical->border_radius_vertical_percent[pc];
        out->border_radius_vertical_percent[c] = physical->border_radius_percent[pc];
    }
    out->width          = physical->height;
    out->height         = physical->width;
    out->width_keyword  = physical->height_keyword;
    out->height_keyword = physical->width_keyword;
    out->min_width      = physical->min_height;
    out->max_width      = physical->max_height;
    out->min_height     = physical->min_width;
    out->max_height     = physical->max_width;
    if (physical->aspect_ratio > 0.0)
        out->aspect_ratio = 1.0 / physical->aspect_ratio;
}

static const tbox_style_table *tbox_layout_root_table(const tbox_style_table *styles) {
    while (styles->fallback != NULL)
        styles = styles->fallback;
    return styles;
}

static size_t tbox_layout_count_elements(const tbox_html_node *node) {
    size_t count = node->type == TBOX_HTML_NODE_ELEMENT ? 1 : 0;
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
        count += tbox_layout_count_elements(child);
    return count;
}

static void tbox_layout_fill_frame_styles(const tbox_html_node *node, const tbox_style_table *root, tbox_style_frame frame, tbox_style_table *out) {
    if (node->type == TBOX_HTML_NODE_ELEMENT) {
        const tbox_style *physical = tbox_style_table_find(root, node);
        if (physical != NULL) {
            tbox_style_entry *entry = &out->items[out->count++];
            entry->node             = node;
            tbox_layout_frame_remap(&entry->style, physical, frame);
        }
    }
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
        tbox_layout_fill_frame_styles(child, root, frame, out);
}

/* The styles of `node`'s subtree turned into `frame`, over `styles`. */
static tbox_style_table *tbox_layout_frame_styles(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_style_frame frame) {
    tbox_style_table *table = tbox_arena_alloc_zero(arena, sizeof(*table));
    size_t count            = tbox_layout_count_elements(node);
    if (table == NULL)
        return NULL;
    table->items = count > 0 ? tbox_arena_alloc(arena, count * sizeof(tbox_style_entry)) : NULL;
    if (count > 0 && table->items == NULL)
        return NULL;
    table->fallback = styles;
    tbox_layout_fill_frame_styles(node, tbox_layout_root_table(styles), frame, table);
    return table;
}

static tbox_rect tbox_layout_map_rect(const double m[6], tbox_rect r) {
    const double xs[2] = { r.x, r.x + r.width }, ys[2] = { r.y, r.y + r.height };
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            double x = m[0] * xs[i] + m[2] * ys[j] + m[4], y = m[1] * xs[i] + m[3] * ys[j] + m[5];
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    return (tbox_rect){ x0, y0, x1 - x0, y1 - y0 };
}

tbox_layout_box *tbox_layout_build_orthogonal(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const tbox_layout_forced_size *forced) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);
    tbox_style_frame target = tbox_style_writing_mode_frame(style->writing_mode);
    tbox_style_table *frame_styles = tbox_layout_frame_styles(arena, node, styles, target);
    if (frame_styles == NULL)
        return NULL;
    /* d: the target frame's coordinates in the current frame's */
    double outer[4], inner[4], d[4];
    tbox_layout_frame_matrix(style->layout_frame, outer);
    tbox_layout_frame_matrix(target, inner);
    d[0] = outer[0] * inner[0] + outer[1] * inner[1];
    d[1] = outer[2] * inner[0] + outer[3] * inner[1];
    d[2] = outer[0] * inner[2] + outer[1] * inner[3];
    d[3] = outer[2] * inner[2] + outer[3] * inner[3];
    bool swap = d[0] == 0.0;

    /* The subtree's own style in the target frame, laid out as a static
     * box at the frame's origin: the current frame places it below. Its
     * transform is applied on the current frame's side (tbox_layout_finish). */
    tbox_style *inside = NULL;
    for (size_t i = 0; i < frame_styles->count; i++)
        if (frame_styles->items[i].node == node)
            inside = &frame_styles->items[i].style;
    if (inside == NULL)
        return NULL;
    if (inside->position != TBOX_STYLE_POSITION_STATIC)
        inside->position = TBOX_STYLE_POSITION_RELATIVE; /* still the containing block of its absolute descendants */
    for (size_t i = 0; i < 4; i++)
        inside->offset[i] = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_AUTO };

    /* CSS Writing Modes 3, 7.3: an orthogonal box's inline size comes
     * from the containing block's height when definite, else the
     * viewport's, and is shrink-to-fit within it. */
    double available = swap ? (container.height_definite ? container.height : positioned_context.viewport.height) : container.width;
    if (!(available > 0.0))
        available = swap ? 1e6 : 0.0; /* no viewport (intrinsic sizing): max-content */
    tbox_layout_containing_block inner_container = {
        .x = 0.0, .y = 0.0, .width = available,
        .height = swap ? container.width : container.height,
        .height_definite = swap ? true : container.height_definite,
    };
    tbox_layout_forced_size inner_forced = { 0 };
    if (forced != NULL) {
        inner_forced.has_width  = swap ? forced->has_height : forced->has_width;
        inner_forced.width      = swap ? forced->height : forced->width;
        inner_forced.has_height = swap ? forced->has_width : forced->has_height;
        inner_forced.height     = swap ? forced->width : forced->height;
    }
    /* The root fills the viewport across the lines, so vertical-rl
     * starts at its right edge (the initial containing block takes the
     * root's writing mode). */
    bool root = node->parent == NULL || node->parent->type == TBOX_HTML_NODE_DOCUMENT;
    if (root && swap && !inner_forced.has_height && inside->height.kind == TBOX_STYLE_LENGTH_AUTO) {
        double across = container.width - tbox_layout_resolve_edge(style->margin[1], container.width) - tbox_layout_resolve_edge(style->margin[3], container.width);
        inner_forced.has_height = true;
        inner_forced.height     = across > 0.0 ? across : 0.0;
    }
    if (swap && !root && !inner_forced.has_width && inside->width.kind == TBOX_STYLE_LENGTH_AUTO && inside->width_keyword == TBOX_STYLE_SIZE_KEYWORD_NONE) {
        tbox_layout_intrinsic intrinsic = tbox_layout_intrinsic_outer(arena, node, frame_styles, fonts, images);
        double margins = tbox_layout_resolve_edge(inside->margin[1], available) + tbox_layout_resolve_edge(inside->margin[3], available);
        double width   = available < intrinsic.max ? available : intrinsic.max;
        if (width < intrinsic.min)
            width = intrinsic.min;
        inner_forced.has_width = true;
        inner_forced.width     = width - margins > 0.0 ? width - margins : 0.0;
    }
    tbox_rect origin = { 0.0, 0.0, inner_container.width, inner_container.height };
    tbox_layout_positioned_context inner_context = { origin, origin, NULL };
    tbox_layout_box *content = tbox_layout_build_element_sized(arena, node, frame_styles, fonts, images, inner_container, 0.0, inner_context, NULL, 0,
                                                               inner_forced.has_width || inner_forced.has_height ? &inner_forced : NULL);
    if (content == NULL)
        return NULL;

    /* Place the border box in the current frame like a block would be. */
    double width  = swap ? content->border_box.height : content->border_box.width;
    double height = swap ? content->border_box.width : content->border_box.height;
    double margin[4];
    for (size_t i = 0; i < 4; i++)
        margin[i] = tbox_layout_resolve_edge(style->margin[i], container.width);
    double x = container.x, y = cursor_y;
    if (style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED) {
        x = tbox_layout_resolve_absolute_edge(style->offset[3], style->offset[1], container.x, container.width, width + margin[1] + margin[3]);
        y = tbox_layout_resolve_absolute_edge(style->offset[0], style->offset[2], container.y, container.height, height + margin[0] + margin[2]);
    } else if (style->position == TBOX_STYLE_POSITION_RELATIVE) {
        x += tbox_layout_resolve_offset(style->offset[3], style->offset[1], container.width, true);
        y += tbox_layout_resolve_offset(style->offset[0], style->offset[2], container.height, container.height_definite);
    }
    double m[6] = { d[0], d[1], d[2], d[3], 0.0, 0.0 };
    tbox_rect mapped = tbox_layout_map_rect(m, content->border_box);
    m[4] = x + margin[3] - mapped.x;
    m[5] = y + margin[0] - mapped.y;

    tbox_layout_box *box = tbox_arena_alloc_zero(arena, sizeof(*box));
    if (box == NULL)
        return NULL;
    box->node        = node;
    box->style       = style;
    box->border_box  = tbox_layout_map_rect(m, content->border_box);
    box->padding_box = tbox_layout_map_rect(m, content->padding_box);
    box->content_box = tbox_layout_map_rect(m, content->content_box);
    box->margin_box  = (tbox_rect){ box->border_box.x - margin[3], box->border_box.y - margin[0],
                                    box->border_box.width + margin[1] + margin[3], box->border_box.height + margin[0] + margin[2] };
    box->scroll_content_height = box->content_box.height;

    /* The laid-out subtree hangs, in its own coordinates, under an
     * anonymous box with the frame's transform; the box itself paints its
     * decoration on the screen side. */
    content->style         = NULL;
    content->has_transform = true;
    memcpy(content->transform, m, sizeof(m));
    content->parent    = box;
    box->first_child   = content;
    box->last_child    = content;
    return box;
}

/* `style`'s transform for a box laid out in `style->layout_frame`: the
 * screen matrix (computed on the box turned upright) carried into the
 * frame's coordinates. `offset` is the Motion Path term, sampled with
 * the box and its reference turned upright (tbox_layout_frame_upright;
 * NULL for none). False when there is none. */
bool tbox_layout_frame_transform(const tbox_style *style, tbox_rect border_box, const double offset[6], double out[6]) {
    double f[4];
    tbox_layout_frame_matrix(style->layout_frame, f);
    bool swap = f[0] == 0.0;
    double w = swap ? border_box.height : border_box.width, h = swap ? border_box.width : border_box.height;
    double t[6];
    if (!tbox_style_transform_matrix(style, 0.0, 0.0, w, h, offset, t))
        return false;
    /* L(p) = F (p - c) + (w, h) / 2 maps the frame box onto the upright
     * one; the result is L^-1 T L, with L^-1(q) = F^T (q - (w, h) / 2) + c. */
    double cx = border_box.x + border_box.width / 2.0, cy = border_box.y + border_box.height / 2.0;
    double l[6]  = { f[0], f[1], f[2], f[3], 0.0, 0.0 };
    l[4]         = w / 2.0 - (f[0] * cx + f[2] * cy);
    l[5]         = h / 2.0 - (f[1] * cx + f[3] * cy);
    double li[6] = { f[0], f[2], f[1], f[3], 0.0, 0.0 };
    li[4]        = cx - (li[0] * w / 2.0 + li[2] * h / 2.0);
    li[5]        = cy - (li[1] * w / 2.0 + li[3] * h / 2.0);
    const double tl[6] = {
        t[0] * l[0] + t[2] * l[1], t[1] * l[0] + t[3] * l[1],
        t[0] * l[2] + t[2] * l[3], t[1] * l[2] + t[3] * l[3],
        t[0] * l[4] + t[2] * l[5] + t[4], t[1] * l[4] + t[3] * l[5] + t[5],
    };
    out[0] = li[0] * tl[0] + li[2] * tl[1];
    out[1] = li[1] * tl[0] + li[3] * tl[1];
    out[2] = li[0] * tl[2] + li[2] * tl[3];
    out[3] = li[1] * tl[2] + li[3] * tl[3];
    out[4] = li[0] * tl[4] + li[2] * tl[5] + li[4];
    out[5] = li[1] * tl[4] + li[3] * tl[5] + li[5];
    return true;
}

/* `rect` (in the frame of `style`) in the upright coordinates
 * tbox_layout_frame_transform computes a box's transform in: the box's
 * border box `border_box` turned to screen orientation, at the origin. */
tbox_rect tbox_layout_frame_upright(const tbox_style *style, tbox_rect border_box, tbox_rect rect) {
    double f[4];
    tbox_layout_frame_matrix(style->layout_frame, f);
    bool swap = f[0] == 0.0;
    double w = swap ? border_box.height : border_box.width, h = swap ? border_box.width : border_box.height;
    double cx = border_box.x + border_box.width / 2.0, cy = border_box.y + border_box.height / 2.0;
    double l[6] = { f[0], f[1], f[2], f[3], w / 2.0 - (f[0] * cx + f[2] * cy), h / 2.0 - (f[1] * cx + f[3] * cy) };
    return tbox_layout_map_rect(l, rect);
}
