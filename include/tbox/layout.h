#ifndef TBOX_LAYOUT_H
#define TBOX_LAYOUT_H

#include <tbox/font.h>
#include <tbox/html_parser.h>
#include <tbox/string_view.h>
#include <tbox/style.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Builds a box tree from the DOM + resolved styles and computes each box's
 * geometry (position and size, in px relative to the viewport) per a
 * deliberately simplified CSS2.1 block formatting context: only normal
 * flow, block-level boxes stacked vertically, no floats/positioning/
 * flexbox/tables/inline formatting context. See ARCHITECTURE.md's "Layout
 * Tree" section for the full v0 scope and rationale. */

typedef struct tbox_rect {
    double x, y, width, height;
} tbox_rect;

typedef struct tbox_layout_box {
    const tbox_html_node *node; /* NULL for anonymous boxes (unused in v0: no loose text/inline wrappers yet) */
    const tbox_style *style;

    tbox_rect margin_box, border_box, padding_box, content_box;

    /* Text content and font, for the Render Pipeline's TEXT_RUN paint ops
     * (see ARCHITECTURE.md's "Render Pipeline" section). Populated only for
     * a leaf box built from one of the fixed text tags (h1-h6, p): `text` is
     * the node's tbox_html_node_text_content, already whitespace-collapsed
     * (tbox_string_collapse_whitespace), and `font` is the same
     * tbox_font_face passed into tbox_layout_build. Every other box
     * (including a <div> with text nodes inside -- not shown in v0) leaves
     * `text` empty (tbox_string_view_empty) and `font` NULL. */
    tbox_string_view text;
    const tbox_font_face *font;

    struct tbox_layout_box *parent, *first_child, *last_child, *next_sibling;
} tbox_layout_box;

/* Builds the layout tree rooted at `root` (either a TBOX_HTML_NODE_DOCUMENT,
 * treated as transparent -- the single box built is for its first ELEMENT
 * child, typically <html> -- or an ELEMENT node passed directly, useful for
 * laying out one fragment in isolation) against `styles` (see
 * tbox_style_table, already resolved by the Style layer) and `font` (a
 * single face used for every text box in v0 -- see ARCHITECTURE.md's
 * "Fonte / Texto" section; Layout Tree never loads a font itself).
 * `viewport_width`/`viewport_height` become the root box's containing
 * block's content width/height, positioned at (0, 0).
 *
 * A node whose resolved style->display == TBOX_STYLE_DISPLAY_NONE produces
 * no box at all: it is absent from the returned tree, contributes nothing
 * to its parent's auto-height sum, and its own descendants are never
 * visited. A node with no entry in `styles` (should not normally happen --
 * the Style layer resolves every ELEMENT node) falls back to v0's default
 * style (TBOX_STYLE_DISPLAY_BLOCK, every length AUTO/0px, as if the element
 * had no declarations at all) rather than crashing.
 *
 * `arena` is supplied by the caller (same pattern as tbox_style_resolve_tree
 * and tbox_render_build_display_list downstream) -- the returned tree has no
 * `_destroy` of its own; its lifetime is the arena's (see "Convenções" at
 * the top of ARCHITECTURE.md). Returns NULL only if `root` has no ELEMENT
 * to lay out (e.g. an empty document, or a DOCUMENT node with no ELEMENT
 * child). */
tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, const tbox_font_face *font, double viewport_width, double viewport_height);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_LAYOUT_H */
