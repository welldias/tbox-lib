#include <tbox/render.h>

#include <stddef.h>

#include "base/tbox_arena.h"
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
    op->has_clip      = false;
}

/* NOVO (visual fidelity): same as tbox_render_push_fill_rect above, but
 * with a corner radius -- used only by the border-radius/box-shadow paths
 * below, never by the pre-existing background/border-strip/mark-highlight/
 * text-decoration call sites (which stay exactly as they were, unchanged,
 * at radius 0.0 via tbox_render_push_fill_rect). `radius` is clamped here
 * to at most half of `min(rect.width, rect.height)`, standard CSS
 * border-radius behavior -- callers never need to clamp it themselves. */
static void tbox_render_push_fill_rect_rounded(tbox_vector *items, tbox_rect rect, double radius, tbox_css_rgba color) {
    double max_radius = (rect.width < rect.height ? rect.width : rect.height) / 2.0;
    if (radius > max_radius) {
        radius = max_radius;
    }
    if (radius < 0.0) {
        radius = 0.0;
    }

    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
    op->radius        = radius;
    op->has_clip      = false;
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
 * rect, no loop overhead. `corner_radius` is the box's OWN border-radius
 * (a shadow of a rounded box is itself rounded), grown along with each
 * step so the corners stay proportionally rounded as the shadow expands. */
#define TBOX_RENDER_BOX_SHADOW_STEPS 6

static void tbox_render_push_box_shadow(tbox_vector *items, tbox_rect border_box, double corner_radius, double offset_x, double offset_y, double blur, tbox_css_rgba color) {
    tbox_rect base = { border_box.x + offset_x, border_box.y + offset_y, border_box.width, border_box.height };

    if (blur <= 0.0) {
        tbox_render_push_fill_rect_rounded(items, base, corner_radius, color);
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
        tbox_render_push_fill_rect_rounded(items, expanded, corner_radius + grow, step_color);
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
        /* NOVO (visual fidelity): box-shadow, painted BEFORE the box's own
         * background/border so paint order alone makes them correctly cover
         * the shadow wherever the two overlap -- no explicit clipping
         * needed, same reasoning as any other paint-order z-stack in this
         * pipeline. */
        if (box->style != NULL && box->style->box_shadow_color.a != 0) {
            tbox_render_push_box_shadow(items, box->border_box, box->style->border_radius, box->style->box_shadow_offset_x, box->style->box_shadow_offset_y, box->style->box_shadow_blur, box->style->box_shadow_color);
        }

        /* NOVO v4: border painting. Render Pipeline isn't handed the
         * already-computed value from Layout Tree, so it re-derives it here
         * from `style` alone -- same formula as tbox_layout_build_element
         * (Tarefa 2): only `solid` ever paints. */
        double effective_border = (box->style != NULL && box->style->border_style == TBOX_STYLE_BORDER_STYLE_SOLID) ? box->style->border_width : 0.0;
        double radius            = box->style != NULL ? box->style->border_radius : 0.0;

        if (radius > 0.0) {
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
                tbox_render_push_fill_rect_rounded(items, box->border_box, radius, box->style->border_color);

                double inner_radius = radius - effective_border;
                if (inner_radius < 0.0) {
                    inner_radius = 0.0;
                }
                tbox_render_push_fill_rect_rounded(items, box->padding_box, inner_radius, box->style->background_color);
            } else if (box->style->background_color.a != 0) {
                tbox_render_push_fill_rect_rounded(items, box->border_box, radius, box->style->background_color);
            }
        } else {
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

        for (size_t i = 0; i < box->text_run_count; i++) {
            const tbox_layout_text_run *run = &box->text_runs[i];

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
            op->image         = NULL;
            op->radius        = 0.0;
            bool is_select = box->node != NULL &&
                tbox_string_view_equal_cstr(box->node->element.tag_name, "select");
            op->has_clip = box->node != NULL &&
                (tbox_string_view_equal_cstr(box->node->element.tag_name, "input") || is_select ||
                 tbox_string_view_equal_cstr(box->node->element.tag_name, "textarea"));
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
                double line_y   = run->style->text_decoration == TBOX_STYLE_TEXT_DECORATION_UNDERLINE
                                     ? baseline + 2.0
                                     : baseline - tbox_font_face_ascent(run->font) * 0.3;
                tbox_render_push_fill_rect(items, (tbox_rect){ run->rect.x, line_y, run->rect.width, 1.0 }, run->style->color);
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
        if (box->style != NULL && box->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO) {
            child_clip = child_has_clip ? tbox_render_intersect(child_clip, box->padding_box) : box->padding_box;
            child_has_clip = true;
        }
        tbox_render_walk(box->first_child, items, child_has_clip, child_clip);
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
