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
 * Display's job (see docs/dev/historico.md's "Render Pipeline" and "Output
 * Display" sections, including the rationale for keeping them separate). */

typedef enum tbox_paint_op_kind {
    TBOX_PAINT_FILL_RECT,
    TBOX_PAINT_TEXT_RUN,
    TBOX_PAINT_IMAGE, /* NOVO (image support): one per tbox_layout_text_run whose `image` is non-NULL -- see tbox_render_build_display_list */
    TBOX_PAINT_FILL_RING, /* outer rounded rect minus inner rounded rect */
    TBOX_PAINT_WAVY_LINE, /* text-decoration-style: wavy */
    TBOX_PAINT_GRADIENT,  /* background gradient: one tile, see `gradient` */
    /* `backdrop-filter`: `filters` applied to the pixels already painted
     * inside `rect` (with corner_radii/corner_radii_y as a FILL_RECT's) in
     * the current buffer -- the screen or the nearest layer. */
    TBOX_PAINT_BACKDROP_FILTER,
    /* A compositing layer (docs/08-layout-e-pintura.md): the ops up to the
     * matching LAYER_END paint into a transparent buffer, which LAYER_END
     * composites: `filters` (may be NULL), the mask, color.a as opacity,
     * `blend_mode` (mix-blend-mode), clipped to this op's clip and
     * rounded_clip. `rect` is unused. Layers nest. */
    TBOX_PAINT_LAYER_BEGIN,
    /* Inside a layer: the ops up to its LAYER_END paint the mask (alpha,
     * or luminance when `mask_luminance`) instead of the content. */
    TBOX_PAINT_LAYER_MASK,
    TBOX_PAINT_LAYER_END,
} tbox_paint_op_kind;

/* Resolved polygon vertices, local to polygon_clip_origin. Borrowed from
 * the frame arena, with the same lifetime as the display list. */
typedef struct tbox_paint_polygon {
    double points[TBOX_STYLE_MAX_POLYGON_POINTS][2];
    size_t count;
    bool evenodd;
} tbox_paint_polygon;

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
     * faces (see docs/dev/historico.md's "Render Pipeline" section). */
    tbox_string_view text;
    const tbox_font_face *face; /* injected by whoever builds the display list, never loaded here */
    double letter_spacing;      /* TEXT_RUN only; px after each codepoint */
    /* TEXT_RUN: the positioned glyphs Output paints (docs/08-layout-e-pintura.md,
     * "Texto como glifos posicionados"), x relative to rect.x and y to the
     * baseline; borrowed from the frame arena like the rest of the list.
     * NULL/0 makes Output shape `text` itself with the same shaper. */
    const tbox_glyph *glyphs;
    size_t glyph_count;

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

    /* IMAGE/GRADIENT: `background-blend-mode`, blending each pixel with
     * what is already painted under it; NORMAL for plain compositing.
     * LAYER_BEGIN: the layer's `mix-blend-mode`. */
    tbox_style_blend_mode blend_mode;

    /* LAYER_BEGIN/BACKDROP_FILTER: the filter list to run (the style that
     * owns it outlives the display list); NULL for none. */
    const tbox_style_filter_list *filters;
    bool mask_luminance; /* LAYER_MASK */

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

    const tbox_paint_polygon *polygon_clip; /* NULL for none; also LAYER_BEGIN */
    double polygon_clip_origin[2];
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
 * docs/dev/historico.md's "Render Pipeline" and v18 sections (clipping of scroll
 * containers and input text via `clip` below).
 *
 * A run's `font` and `glyphs` are copied into the TEXT_RUN op as they
 * are; Render only shapes text of its own (a `text-emphasis` string mark)
 * and never rasterizes -- Output Display paints the glyphs.
 *
 * `arena` is supplied by the caller (same per-frame arena as
 * tbox_style_resolve_tree and tbox_layout_build upstream) -- the returned
 * tbox_display_list has no `_destroy` of its own; its lifetime is the
 * arena's (see "Convenções" in docs/dev/convencoes.md). */
tbox_display_list tbox_render_build_display_list(tbox_arena *arena, const tbox_layout_box *root);

/* Uses the actual window dimensions for background-attachment: fixed.
 * The original entry point uses the root border box as a fallback viewport. */
tbox_display_list tbox_render_build_display_list_in_viewport(tbox_arena *arena, const tbox_layout_box *root, double viewport_width, double viewport_height);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_RENDER_H */
