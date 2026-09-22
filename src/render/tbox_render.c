#include <tbox/render.h>

#include <stddef.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* Pushes one FILL_RECT of `color` covering `rect` onto `items` -- shared by
 * the background fill and the (NOVO v4) 4 border strips below, since none
 * of them carry text. */
static void tbox_render_push_fill_rect(tbox_vector *items, tbox_rect rect, tbox_css_rgba color) {
    tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
    op->kind          = TBOX_PAINT_FILL_RECT;
    op->rect          = rect;
    op->color         = color;
    op->text          = tbox_string_view_make(NULL, 0);
    op->face          = NULL;
    op->image         = NULL;
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
static void tbox_render_walk(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->style != NULL && box->style->background_color.a != 0) {
            tbox_render_push_fill_rect(items, box->border_box, box->style->background_color);
        }

        /* NOVO v4: border painting. Render Pipeline isn't handed the
         * already-computed value from Layout Tree, so it re-derives it here
         * from `style` alone -- same formula as tbox_layout_build_element
         * (Tarefa 2): only `solid` ever paints. */
        double effective_border = (box->style != NULL && box->style->border_style == TBOX_STYLE_BORDER_STYLE_SOLID) ? box->style->border_width : 0.0;
        if (effective_border > 0.0) {
            tbox_css_rgba border_color = box->style->border_color;
            tbox_rect border_box       = box->border_box;
            tbox_rect padding_box      = box->padding_box;

            /* Top and bottom span the full border_box width (including
             * corners); left and right span only the padding_box height, so
             * the 4 corners are each covered exactly once. */
            tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, border_box.y, border_box.width, padding_box.y - border_box.y }, border_color);
            tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, padding_box.y + padding_box.height, border_box.width, (border_box.y + border_box.height) - (padding_box.y + padding_box.height) }, border_color);
            tbox_render_push_fill_rect(items, (tbox_rect){ border_box.x, padding_box.y, padding_box.x - border_box.x, padding_box.height }, border_color);
            tbox_render_push_fill_rect(items, (tbox_rect){ padding_box.x + padding_box.width, padding_box.y, (border_box.x + border_box.width) - (padding_box.x + padding_box.width), padding_box.height }, border_color);
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

        tbox_render_walk(box->first_child, items);
    }
}

tbox_display_list tbox_render_build_display_list(tbox_arena *arena, const tbox_layout_box *root) {
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_paint_op), 0);

    tbox_render_walk(root, &items);

    tbox_display_list list;
    list.items = (tbox_paint_op *)items.data;
    list.count = items.length;
    return list;
}
