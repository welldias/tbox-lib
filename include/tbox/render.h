#ifndef TBOX_RENDER_H
#define TBOX_RENDER_H

#include <stdbool.h>
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
    TBOX_PAINT_FILL_RING, /* outer rounded rect minus inner rounded rect */
    TBOX_PAINT_WAVY_LINE, /* text-decoration-style: wavy */
    TBOX_PAINT_GRADIENT,  /* background gradient: one tile, see `gradient` */
} tbox_paint_op_kind;

typedef struct tbox_paint_op {
    tbox_paint_op_kind kind;

    /* FILL_RECT: the rectangle to fill. TEXT_RUN: only rect.x/rect.y are
     * meaningful (the content box's origin) -- rect.width/rect.height are
     * unused. IMAGE: the destination rectangle `image` is painted into
     * (the fitted image rectangle after object-fit/object-position; the
     * original CSS image box is retained as a clip when content overflows).
     * Output Display scales `image` to that rectangle. */
    tbox_rect rect;

    /* FILL_RECT: the background color. TEXT_RUN: the text color. IMAGE: only
     * `color.a` is used, as the image's opacity (255 = as decoded; an
     * `opacity` ancestor lowers it). */
    tbox_css_rgba color;

    /* TEXT_RUN only (left at their empty/NULL default for FILL_RECT/IMAGE):
     * one paint op per tbox_layout_text_run, not per text-bearing
     * box, since a box's text may now wrap onto multiple lines and/or mix
     * faces (see ARCHITECTURE.md's "Render Pipeline" section). */
    tbox_string_view text;
    const tbox_font_face *face; /* injected by whoever builds the display list, never loaded here */
    double letter_spacing;      /* TEXT_RUN only; px after each codepoint */

    /* IMAGE only (NULL for FILL_RECT/TEXT_RUN): the decoded image to
     * composite into `rect` -- see tbox_raster_image. */
    const tbox_image *image;
    bool image_pixelated; /* IMAGE only: nearest-neighbor enlargement */

    /* FILL_RECT only: radius keeps the original uniform value; corner_radii
     * carries individual clockwise radii. Both are zero for plain fills.
     * Rounded fills use circular corners, normalized to fit the rectangle. */
    double radius;
    double corner_radii[4]; /* top-left, top-right, bottom-right, bottom-left */

    /* FILL_RING only: pixels inside inner_rect/corners stay untouched. */
    tbox_rect inner_rect;
    double inner_corner_radii[4];

    /* FILL_RECT/FILL_RING: when `elliptical`, corner_radii (and
     * inner_corner_radii) are the horizontal radii and these the vertical
     * ones; otherwise every corner is circular. Already normalized. */
    bool elliptical;
    double corner_radii_y[4];
    double inner_corner_radii_y[4];

    /* GRADIENT only: painted across `rect` (one background tile), the
     * style that owns it outliving the display list (same arena). */
    const tbox_style_gradient *gradient;

    /* IMAGE/GRADIENT: a `filter` color matrix (see tbox_style.filter_matrix)
     * applied to each pixel; NULL for none. Solid-color ops have their
     * color transformed by Render instead. */
    const double *color_filter;

    /* Optional paint clip, applied to every op kind. Input text and the
     * descendants of overflow-y:auto blocks use it. */
    bool has_clip;
    tbox_rect clip;

    /* Optional rounded clip on top of `clip` (every op kind): background
     * images and inset shadows of a box with border-radius, and clip-path.
     * Horizontal/vertical radii per corner, already normalized. */
    bool has_rounded_clip;
    tbox_rect rounded_clip;
    double rounded_clip_radii[4], rounded_clip_radii_y[4];
} tbox_paint_op;

typedef struct tbox_display_list {
    tbox_paint_op *items;
    size_t count;
} tbox_display_list;

/* Builds the display list for `root`'s subtree (may be NULL, producing an
 * empty list), pre-order: for each box, first (NOVO, visual fidelity) if
 * its style's box_shadow_color is non-transparent, one or more FILL_RECTs
 * approximating a soft shadow behind border_box (see
 * tbox_render_push_box_shadow). A solid background fills the box selected by
 * background-clip; square borders use side fills, and rounded borders use a
 * ring fill over the background. Then one TEXT_RUN -- or (NOVO,
 * image support) one IMAGE, for a run whose `image` is non-NULL, i.e. built
 * from an `<img>` word -- per entry of box->text_runs, in the order Layout
 * Tree built them (already line-order, left-to-right/top-to-bottom) -- in
 * that order relative to the FILL_RECTs, since backgrounds and borders sit
 * under text/images -- and only then its first_child and the rest of the
 * next_sibling chain, recursively, in the same order. Background images
 * (IMAGE/GRADIENT tiles) and inset shadows sit between the background color
 * and the border. Positioned boxes with an explicit z-index leave tree
 * order: each stacking context (the root, a z-index box, or one with
 * opacity below 1) paints its negative z-index layers right after its own
 * border and the rest after its children, in increasing z-index. See
 * ARCHITECTURE.md's "Render Pipeline" and v18 sections (clipping of scroll
 * containers and input text via `clip` below).
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
