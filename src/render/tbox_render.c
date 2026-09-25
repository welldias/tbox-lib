#include <tbox/render.h>

#include <tbox/css_cascade.h>

#include <stddef.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* Pushes one FILL_RECT of `color` covering `rect` onto `items` -- shared by
 * the background fill and the (NOVO v4) 4 border strips below, since none
 * of them carry text. `radius` always 0.0 here -- every caller of THIS
 * helper wants a plain rectangle; see tbox_render_push_fill_rect_rounded
 * below for the border-radius/box-shadow paths. */
static void tbox_render_push_fill_rect(tbox_vector *items, tbox_rect rect, tbox_css_rgba color) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
    op->radius        = 0.0;
    for (size_t i = 0; i < 4; i++) op->corner_radii[i] = 0.0;
    op->has_clip      = false;
}

static bool tbox_render_checked_checkbox(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL &&
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8)) &&
           tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

static bool tbox_render_checked_radio(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("radio", 5)) &&
        tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

static bool tbox_render_color_input(const tbox_html_node *node, tbox_css_rgba *out_color) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    if (type == NULL || !tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("color", 5)))
        return false;
    *out_color = (tbox_css_rgba){0, 0, 0, 255};
    const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    if (value != NULL && value->value.size == 7)
        tbox_css_hex_to_rgba(value->value, out_color);
    out_color->a = 255;
    return true;
}

/* NOVO (visual fidelity): same as tbox_render_push_fill_rect above, but
 * with a corner radius -- used only by the border-radius/box-shadow paths
 * below, never by the pre-existing background/border-strip/mark-highlight/
 * text-decoration call sites (which stay exactly as they were, unchanged,
 * at radius 0.0 via tbox_render_push_fill_rect). `radius` is clamped here
 * to at most half of `min(rect.width, rect.height)`, standard CSS
 * border-radius behavior -- callers never need to clamp it themselves. */
static void tbox_render_push_fill_rect_corners(tbox_vector *items, tbox_rect rect,
                                                const double corners[4], tbox_css_rgba color) {
    double radii[4];
    for (size_t i = 0; i < 4; i++) radii[i] = corners[i] > 0.0 ? corners[i] : 0.0;
    double scale = 1.0;
    const double sums[4] = {radii[0] + radii[1], radii[2] + radii[3],
                            radii[0] + radii[3], radii[1] + radii[2]};
    const double limits[4] = {rect.width, rect.width, rect.height, rect.height};
    for (size_t i = 0; i < 4; i++)
        if (sums[i] > 0.0 && limits[i] / sums[i] < scale) scale = limits[i] / sums[i];
    if (scale < 0.0) scale = 0.0;
    for (size_t i = 0; i < 4; i++) radii[i] *= scale;

    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
    op->radius        = radii[0] == radii[1] && radii[0] == radii[2] && radii[0] == radii[3]
        ? radii[0] : 0.0;
    for (size_t i = 0; i < 4; i++) op->corner_radii[i] = radii[i];
    op->has_clip      = false;
}

static void tbox_render_push_fill_rect_rounded(tbox_vector *items, tbox_rect rect,
                                                double radius, tbox_css_rgba color) {
    const double corners[4] = {radius, radius, radius, radius};
    tbox_render_push_fill_rect_corners(items, rect, corners, color);
}

static void tbox_render_style_corners(const tbox_style *style, double out[4]) {
    bool has_corner = false;
    for (size_t i = 0; i < 4; i++)
        if (style->border_radius_corners[i] > 0.0) has_corner = true;
    for (size_t i = 0; i < 4; i++)
        out[i] = has_corner ? style->border_radius_corners[i] : style->border_radius;
}

/* NOVO (visual fidelity): approximates `box-shadow`'s blur with a handful
 * of concentric rounded-rect fills instead of a real Gaussian/box blur --
 * this rasterizer has no blur infrastructure, and a real one is a
 * meaningfully bigger addition than this pair of properties otherwise
 * needs (same class of simplification as v13's sub/sup fixed-fraction
 * offsets, or v11's flat-gray <hr>). Paints TBOX_RENDER_BOX_SHADOW_STEPS
 * layers, largest/faintest FIRST shrinking down to the exact
 * (offset, un-grown) shadow rect LAST: since every layer is painted with
 * the standard "over" operator, the area under the base rect accumulates
 * contributions from ALL layers (converging close to the declared
 * `color`'s own alpha), while the area only reached by the larger outer
 * layers gets progressively fainter -- a soft-looking falloff from cheap,
 * repeated flat fills, not a real convolution. `blur <= 0.0` (the common,
 * simple-shadow case) skips all of this and pushes exactly one hard-edged
 * rect, no loop overhead. `corner_radii` are the box's own border radii
 * (a shadow of a rounded box is itself rounded), grown along with each
 * step so the corners stay proportionally rounded as the shadow expands. */
#define TBOX_RENDER_BOX_SHADOW_STEPS 6

static void tbox_render_push_box_shadow(tbox_vector *items, tbox_rect border_box, const double corner_radii[4], double offset_x, double offset_y, double blur, tbox_css_rgba color) {
    tbox_rect base = { border_box.x + offset_x, border_box.y + offset_y, border_box.width, border_box.height };

    if (blur <= 0.0) {
        tbox_render_push_fill_rect_corners(items, base, corner_radii, color);
        return;
    }

    tbox_css_rgba step_color = color;
    step_color.a             = (unsigned char)((double)color.a / (double)TBOX_RENDER_BOX_SHADOW_STEPS + 0.5);
    if (step_color.a == 0) {
        step_color.a = 1; /* never let rounding vanish a declared shadow entirely */
    }

    for (int step = TBOX_RENDER_BOX_SHADOW_STEPS; step >= 1; step--) {
        double grow         = blur * (double)(step - 1) / (double)(TBOX_RENDER_BOX_SHADOW_STEPS - 1);
        tbox_rect expanded  = { base.x - grow, base.y - grow, base.width + 2.0 * grow, base.height + 2.0 * grow };
        double grown[4];
        for (size_t i = 0; i < 4; i++) grown[i] = corner_radii[i] + grow;
        tbox_render_push_fill_rect_corners(items, expanded, grown, step_color);
    }
}

/* Pre-order walk over `box` and its first_child/next_sibling chain, pushing
 * paint ops onto `items` (see tbox_render_build_display_list). A box's own
 * FILL_RECT (if its background isn't transparent) always precedes its own
 * (NOVO v4) border FILL_RECTs (if `effective_border > 0`), which in turn
 * precede its own TEXT_RUN/IMAGE ops (NOVO v2/image support: one per
 * box->text_runs entry, in the order Layout Tree built them -- IMAGE for a
 * run whose `image` is non-NULL, TEXT_RUN otherwise), and all of that
 * precedes its children's ops -- see ARCHITECTURE.md's "Render Pipeline"
 * section. */
static tbox_rect tbox_render_intersect(tbox_rect a, tbox_rect b) {
    double x0 = a.x > b.x ? a.x : b.x;
    double y0 = a.y > b.y ? a.y : b.y;
    double x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    double y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    return (tbox_rect){x0, y0, x1 > x0 ? x1 - x0 : 0.0, y1 > y0 ? y1 - y0 : 0.0};
}

static void tbox_render_walk(const tbox_layout_box *box, tbox_vector *items, bool has_clip, tbox_rect clip) {
    for (; box != NULL; box = box->next_sibling) {
        size_t own_start = items->length;
        bool visible = box->style == NULL || !box->style->visibility_hidden;
        double corners[4] = {0.0, 0.0, 0.0, 0.0};
        if (box->style != NULL) tbox_render_style_corners(box->style, corners);
        /* NOVO (visual fidelity): box-shadow, painted BEFORE the box's own
         * background/border so paint order alone makes them correctly cover
         * the shadow wherever the two overlap -- no explicit clipping
         * needed, same reasoning as any other paint-order z-stack in this
         * pipeline. */
        if (visible && box->style != NULL && box->style->box_shadow_color.a != 0) {
            tbox_render_push_box_shadow(items, box->border_box, corners, box->style->box_shadow_offset_x, box->style->box_shadow_offset_y, box->style->box_shadow_blur, box->style->box_shadow_color);
        }

        /* NOVO v4: border painting. Render Pipeline isn't handed the
         * already-computed value from Layout Tree, so it re-derives it here
         * from `style` alone -- same formula as tbox_layout_build_element
         * (Tarefa 2): only `solid` ever paints. */
        double effective_border = (!box->table_suppress_border && box->style != NULL &&
            box->style->border_style == TBOX_STYLE_BORDER_STYLE_SOLID) ? box->style->border_width : 0.0;
        bool rounded = corners[0] > 0.0 || corners[1] > 0.0 || corners[2] > 0.0 || corners[3] > 0.0;

        if (visible && rounded) {
            /* NOVO (visual fidelity): border-radius. The 4-strip technique
             * below is geometrically incompatible with curved corners (its
             * strips meet at sharp 90-degree joins), so a box with a radius
             * uses a DIFFERENT technique instead -- one or two nested
             * rounded-rect fills:
             * - no border: one rounded rect at border_box, in
             *   background_color (nothing to paint if that's transparent).
             * - with border: one rounded rect at border_box in
             *   border_color (the outer edge), THEN one rounded rect at
             *   padding_box in background_color (the inner edge, radius
             *   shrunk by the border's own width, standard CSS inner-radius
             *   formula) painted on top, producing the visible "ring".
             *   KNOWN LIMITATION: with a fully transparent background_color,
             *   the inner rounded-rect paint is a no-op (this rasterizer
             *   has no real alpha-hole/clip-path capability -- same
             *   limitation every other simplification here already lives
             *   with), so the shape reads as a solid border-colored disc
             *   rather than a true see-through ring; a realistic bordered
             *   box (card/button) almost always has an actual background
             *   too, where this renders correctly. */
            if (effective_border > 0.0) {
                tbox_render_push_fill_rect_corners(items, box->border_box, corners, box->style->border_color);

                double inner[4];
                for (size_t i = 0; i < 4; i++)
                    inner[i] = corners[i] > effective_border ? corners[i] - effective_border : 0.0;
                tbox_render_push_fill_rect_corners(items, box->padding_box, inner, box->style->background_color);
            } else if (box->style->background_color.a != 0) {
                tbox_render_push_fill_rect_corners(items, box->border_box, corners, box->style->background_color);
            }
        } else if (visible) {
            if (box->style != NULL && box->style->background_color.a != 0) {
                tbox_render_push_fill_rect(items, box->border_box, box->style->background_color);
            }

            if (effective_border > 0.0) {
                tbox_css_rgba border_color = box->style->border_color;
                tbox_rect border_box       = box->border_box;
                tbox_rect padding_box      = box->padding_box;

                /* Top and bottom span the full border_box width (including
                 * corners); left and right span only the padding_box
                 * height, so the 4 corners are each covered exactly once. */
                tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, border_box.y, border_box.width, padding_box.y - border_box.y }, border_color);
                tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, padding_box.y + padding_box.height, border_box.width, (border_box.y + border_box.height) - (padding_box.y + padding_box.height) }, border_color);
                tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, padding_box.y, padding_box.x - border_box.x, padding_box.height }, border_color);
                tbox_render_push_fill_rect(items, (tbox_rect){ padding_box.x + padding_box.width, padding_box.y, (border_box.x + border_box.width) - (padding_box.x + padding_box.width), padding_box.height }, border_color);
            }
        }

        if (visible && box->style != NULL && box->style->outline_style == TBOX_STYLE_BORDER_STYLE_SOLID &&
            box->style->outline_width > 0.0) {
            double w = box->style->outline_width;
            tbox_rect b = box->border_box;
            tbox_css_rgba c = box->style->outline_color;
            double offset = box->style->outline_offset;
            double min_offset = -(b.width < b.height ? b.width : b.height) / 2.0;
            if (offset < min_offset) offset = min_offset;
            tbox_rect inner = {b.x - offset, b.y - offset,
                               b.width + 2.0 * offset, b.height + 2.0 * offset};
            tbox_render_push_fill_rect(items, (tbox_rect){inner.x - w, inner.y - w, inner.width + 2.0 * w, w}, c);
            tbox_render_push_fill_rect(items, (tbox_rect){inner.x - w, inner.y + inner.height, inner.width + 2.0 * w, w}, c);
            tbox_render_push_fill_rect(items, (tbox_rect){inner.x - w, inner.y, w, inner.height}, c);
            tbox_render_push_fill_rect(items, (tbox_rect){inner.x + inner.width, inner.y, w, inner.height}, c);
        }

        tbox_css_rgba input_color;
        if (visible && tbox_render_color_input(box->node, &input_color))
            tbox_render_push_fill_rect(items, box->content_box, input_color);

        if (visible && box->style != NULL && tbox_render_checked_radio(box->node)) {
            tbox_rect content = box->content_box;
            double side = content.width < content.height ? content.width : content.height;
            side *= 0.5;
            if (side > 0.0) {
                tbox_rect dot = {content.x + (content.width - side) / 2.0,
                                 content.y + (content.height - side) / 2.0, side, side};
                tbox_render_push_fill_rect_rounded(items, dot, side / 2.0, box->style->color);
            }
        }

        /* Layout supplies the U+2713 text run when a suitable font exists.
         * Keep the small geometric mark for embedded fonts without it. */
        if (visible && box->style != NULL && box->text_run_count == 0 && tbox_render_checked_checkbox(box->node)) {
            tbox_rect content = box->content_box;
            double side = content.width < content.height ? content.width : content.height;
            double unit = side / 8.0;
            if (unit > 0.0) {
                double x = content.x + (content.width - side) / 2.0;
                double y = content.y + (content.height - side) / 2.0;
                const unsigned char pixels[][2] = {{1, 4}, {2, 5}, {3, 6}, {4, 5}, {5, 4}, {6, 3}};
                for (size_t i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++)
                    tbox_render_push_fill_rect(items, (tbox_rect){x + pixels[i][0] * unit,
                        y + pixels[i][1] * unit, unit, unit}, box->style->color);
            }
        }

        size_t text_start = items->length;
        for (size_t i = 0; i < box->text_run_count; i++) {
            const tbox_layout_text_run *run = &box->text_runs[i];
            if (run->style != NULL && run->style->visibility_hidden) continue;

            /* NOVO (image support): an <img> word's run paints its decoded
             * pixels instead of text -- no background highlight, no
             * text-decoration line (neither applies to a replaced element
             * in this project's scope), just one IMAGE op straight into
             * `run->rect` (already the resolved destination size -- see
             * tbox_layout_push_image_word/tbox_layout_build_line_runs).
             * Skips the rest of this loop body entirely for this run. */
            if (run->image != NULL) {
                tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
                op->kind          = TBOX_PAINT_IMAGE;
                op->rect          = run->rect;
                op->color         = (tbox_css_rgba){ 0, 0, 0, 0 };
                op->text          = tbox_string_view_make(NULL, 0);
                op->face          = NULL;
                op->image         = run->image;
                op->radius        = 0.0;
                for (size_t j = 0; j < 4; j++) op->corner_radii[j] = 0.0;
                op->has_clip      = false;
                continue;
            }

            /* NOVO v13: <mark> highlight -- a FILL_RECT covering the run's
             * own rect (not the whole box/line), painted before its
             * TEXT_RUN so the glyphs draw on top of it. Same helper as the
             * box background/border FILL_RECTs above.
             *
             * `run->style != box->style` guards against double-painting: a
             * run's own direct text (no inline descendant in between, e.g.
             * `<h1 style="background-color:...">`) is built with
             * `run->style` pointing at the SAME tbox_style as `box->style`
             * (see tbox_layout_build_text_runs/tbox_layout_collect_words),
             * whose background_color the box FILL_RECT above already
             * painted across the whole border_box. Without this guard, that
             * identical color gets painted a second time over just the
             * run's rect, compositing its alpha on top of itself and
             * producing a visibly different (darker/more opaque) patch
             * behind the text than the rest of the box -- only an inline
             * descendant with its OWN resolved style (a distinct pointer,
             * e.g. `<mark>`) should get this highlight. */
            if (run->style != box->style && run->style->background_color.a != 0) {
                tbox_render_push_fill_rect(items, run->rect, run->style->background_color);
            }

            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            op->kind          = TBOX_PAINT_TEXT_RUN;
            op->rect          = run->rect;
            op->color         = run->style->color; /* NOVO v13: per-run color (run->style, never NULL), replacing the one shared box->style->color -- see ARCHITECTURE.md's "v13" section */
            op->text          = run->text;
            op->face          = run->font;
            op->letter_spacing = run->style->letter_spacing;
            op->image         = NULL;
            op->radius        = 0.0;
            for (size_t j = 0; j < 4; j++) op->corner_radii[j] = 0.0;
            bool is_select = box->node != NULL &&
                tbox_string_view_equal_cstr(box->node->element.tag_name, "select");
            op->has_clip = box->node != NULL &&
                (tbox_string_view_equal_cstr(box->node->element.tag_name, "input") || is_select ||
                 tbox_string_view_equal_cstr(box->node->element.tag_name, "textarea"));
            if (box->style != NULL && box->style->text_overflow == TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS &&
                box->style->white_space_nowrap && box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_HIDDEN)
                op->has_clip = true;
            if (op->has_clip) {
                op->clip = box->content_box;
                if (is_select) {
                    op->clip.width -= 18.0;
                    if (op->clip.width < 0.0) op->clip.width = 0.0;
                }
            }

            /* NOVO v13: <del>/<ins> decoration line -- a thin (1px)
             * FILL_RECT spanning the run's width, positioned off its
             * baseline (same baseline tbox_raster_text_run/Output Display
             * already computes: rect.y + ascent). UNDERLINE sits a little
             * below the baseline, LINE_THROUGH a little above it
             * (approximating x-height by a fraction of ascent -- see
             * ARCHITECTURE.md's "Fora de escopo"). Painted after the
             * TEXT_RUN, same color as the text. */
            if (run->style->text_decoration != TBOX_STYLE_TEXT_DECORATION_NONE) {
                double baseline = run->rect.y + tbox_font_face_ascent(run->font);
                double line_y = run->style->text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE
                    ? baseline + 2.0
                    : run->style->text_decoration == TBOX_STYLE_TEXT_DECORATION_OVERLINE
                        ? baseline - tbox_font_face_ascent(run->font)
                        : baseline - tbox_font_face_ascent(run->font) * 0.3;
                tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y, run->rect.width,
                    run->style->text_decoration_thickness }, run->style->text_decoration_color);
            }
        }

        if (box->style != NULL && box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE) {
            for (size_t i = text_start; i < items->length; i++) {
                tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
                op->clip = op->has_clip ? tbox_render_intersect(op->clip, box->padding_box) : box->padding_box;
                op->has_clip = true;
            }
        }

        for (size_t i = own_start; i < items->length; i++) {
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_at(items, i);
            if (has_clip) {
                op->clip = op->has_clip ? tbox_render_intersect(op->clip, clip) : clip;
                op->has_clip = true;
            }
        }
        bool child_has_clip = has_clip;
        tbox_rect child_clip = clip;
        if (box->style != NULL && box->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE) {
            child_clip = child_has_clip ? tbox_render_intersect(child_clip, box->padding_box) : box->padding_box;
            child_has_clip = true;
        }
        tbox_render_walk(box->first_child, items, child_has_clip, child_clip);
        for (size_t i = 0; visible && i < box->table_edge_count; i++) {
            size_t index = items->length;
            tbox_render_push_fill_rect(items, box->table_edges[i].rect, box->table_edges[i].color);
            if (has_clip) {
                tbox_paint_op *op = tbox_vector_at(items, index);
                op->has_clip = true;
                op->clip = clip;
            }
        }
    }
}

tbox_display_list tbox_render_build_display_list(tbox_arena *arena, const tbox_layout_box *root) {
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_paint_op), 0);

    tbox_render_walk(root, &items, false, (tbox_rect){0});

    tbox_display_list list;
    list.items = (tbox_paint_op *)items.data;
    list.count = items.length;
    return list;
}
