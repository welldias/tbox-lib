#include <tbox/render.h>

#include <stddef.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* Pre-order walk over `box` and its first_child/next_sibling chain, pushing
 * paint ops onto `items` (see tbox_render_build_display_list). A box's own
 * FILL_RECT (if its background isn't transparent) always precedes its own
 * TEXT_RUN (if it has text), and both precede its children's ops -- see
 * ARCHITECTURE.md's "Render Pipeline" section. */
static void tbox_render_walk(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->style != NULL && box->style->background_color.a != 0) {
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            op->kind          = TBOX_PAINT_FILL_RECT;
            op->rect          = box->border_box;
            op->color         = box->style->background_color;
            op->text          = tbox_string_view_make(NULL, 0);
            op->face          = NULL;
        }

        if (!tbox_string_view_empty(box->text)) {
            tbox_paint_op *op = (tbox_paint_op *)tbox_vector_push(items);
            op->kind          = TBOX_PAINT_TEXT_RUN;
            op->rect.x        = box->content_box.x;
            op->rect.y        = box->content_box.y;
            op->rect.width    = 0.0;
            op->rect.height   = 0.0;
            op->color         = box->style != NULL ? box->style->color : (tbox_css_rgba){ 0, 0, 0, 255 };
            op->text          = box->text;
            op->face          = box->font;
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
