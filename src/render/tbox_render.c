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
}

/* Pre-order walk over `box` and its first_child/next_sibling chain, pushing
 * paint ops onto `items` (see tbox_render_build_display_list). A box's own
 * FILL_RECT (if its background isn't transparent) always precedes its own
 * (NOVO v4) border FILL_RECTs (if `effective_border > 0`), which in turn
 * precede its own TEXT_RUN ops (NOVO v2: one per box->text_runs entry, in
 * the order Layout Tree built them), and all of that precedes its
 * children's ops -- see ARCHITECTURE.md's "Render Pipeline" section. */
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

        tbox_css_rgba text_color = box->style != NULL ? box->style->color : (tbox_css_rgba){ 0, 0, 0, 255 };
        for (size_t i = 0; i < box->text_run_count; i++) {
            const tbox_layout_text_run *run = &box->text_runs[i];

            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            op->kind          = TBOX_PAINT_TEXT_RUN;
            op->rect          = run->rect;
            op->color         = text_color; /* same color for every run of one box -- see ARCHITECTURE.md's "Fora de escopo" */
            op->text          = run->text;
            op->face          = run->font;
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
