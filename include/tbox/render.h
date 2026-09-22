#ifndef TBOX_RENDER_H
#define TBOX_RENDER_H

#include <stddef.h>

#include <tbox/font.h>
#include <tbox/image.h>
#include <tbox/layout.h>
#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Walks a Layout Tree (see <tbox/layout.h>) and produces a display list -- an
 * ordered sequence of abstract, backend-agnostic paint commands. Render
 * Pipeline knows nothing about Wayland or a pixel format; that is Output
 * Display's job (see ARCHITECTURE.md's "Render Pipeline" and "Output
 * Display" sections, including the rationale for keeping them separate). */

typedef enum tbox_paint_op_kind {
    TBOX_PAINT_FILL_RECT,
    TBOX_PAINT_TEXT_RUN,
    TBOX_PAINT_IMAGE, /* NOVO (image support): one per tbox_layout_text_run whose `image` is non-NULL -- see tbox_render_build_display_list */
} tbox_paint_op_kind;

typedef struct tbox_paint_op {
    tbox_paint_op_kind kind;

    /* FILL_RECT: the rectangle to fill. TEXT_RUN: only rect.x/rect.y are
     * meaningful (the content box's origin) -- rect.width/rect.height are
     * unused. IMAGE: the destination rectangle `image` is painted into
     * (already the resolved CSS content box size, which Output Display
     * scales `image`'s own intrinsic pixel dimensions to fit -- see
     * tbox_raster_image). */
    tbox_rect rect;

    /* FILL_RECT: the background color. TEXT_RUN: the text color. Unused for
     * IMAGE (an image paints its own decoded pixels, not a solid color). */
    tbox_css_rgba color;

    /* TEXT_RUN only (left at their empty/NULL default for FILL_RECT/IMAGE):
     * NOVO v2 -- one paint op per tbox_layout_text_run, not per text-bearing
     * box, since a box's text may now wrap onto multiple lines and/or mix
     * faces (see ARCHITECTURE.md's "Render Pipeline" section). */
    tbox_string_view text;
    const tbox_font_face *face; /* injected by whoever builds the display list, never loaded here */

    /* IMAGE only (NULL for FILL_RECT/TEXT_RUN): the decoded image to
     * composite into `rect` -- see tbox_raster_image. */
    const tbox_image *image;
} tbox_paint_op;

typedef struct tbox_display_list {
    tbox_paint_op *items;
    size_t count;
} tbox_display_list;

/* Builds the display list for `root`'s subtree (may be NULL, producing an
 * empty list), pre-order: for each box, first (if its style's
 * background_color is non-transparent) a FILL_RECT over its border_box,
 * then (NOVO v4) if `effective_border > 0` (re-derived here from
 * `style->border_style`/`border_width`, same formula as Layout Tree's
 * Tarefa 2 -- only `solid` ever paints) up to 4 more FILL_RECTs in
 * `style->border_color`, one per side, each covering the strip between
 * `border_box` and `padding_box` (top/bottom span the full border_box
 * width including corners; left/right span only the padding_box height),
 * then (NOVO v2) one TEXT_RUN -- or (NOVO, image support) one IMAGE, for a
 * run whose `image` is non-NULL, i.e. built from an `<img>` word -- per
 * entry of box->text_runs, in the order Layout Tree built them (already
 * line-order, left-to-right/top-to-bottom) -- in that order relative to the
 * FILL_RECTs, since backgrounds and borders sit under text/images -- and
 * only then its first_child and the rest of the next_sibling chain,
 * recursively, in the same order. See
 * ARCHITECTURE.md's "Render Pipeline" section (no stacking contexts,
 * clipping).
 *
 * This never calls into <tbox/font.h>: a run's `font` pointer is only
 * copied into the resulting paint op's `face`, never dereferenced -- Output
 * Display is the one that rasterizes glyphs.
 *
 * `arena` is supplied by the caller (same per-frame arena as
 * tbox_style_resolve_tree and tbox_layout_build upstream) -- the returned
 * tbox_display_list has no `_destroy` of its own; its lifetime is the
 * arena's (see "Convenções" at the top of ARCHITECTURE.md). */
tbox_display_list tbox_render_build_display_list(tbox_arena *arena, const tbox_layout_box *root);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_RENDER_H */
