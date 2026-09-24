#ifndef TBOX_LAYOUT_H
#define TBOX_LAYOUT_H

#include <tbox/font.h>
#include <tbox/html_parser.h>
#include <tbox/image.h>
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
 * flexbox/tables -- but, as of v2, a REAL inline formatting context inside
 * the fixed text tags (h1-h6, p): see ARCHITECTURE.md's "Layout Tree"
 * section for the full v2 scope and rationale. */

typedef struct tbox_rect {
    double x, y, width, height;
} tbox_rect;

/* One contiguous, same-face, same-line run of text within a text-bearing
 * box -- see ARCHITECTURE.md's "Layout Tree" section ("Layout Tree --
 * inline formatting context real") for the full algorithm that produces
 * these (greedy word-wrap, run merging by face, per-line height). */
typedef struct tbox_layout_text_run {
    tbox_rect rect;             /* this run's absolute position/size, already placed on the right line */
    tbox_string_view text;      /* the longest contiguous sequence of words sharing the same resolved face AND style AND fitting on the same line */
    const tbox_font_face *font; /* tbox_font_face_cache_get(fonts, ..., ...) for the element that originated this run */
    /* NOVO v13: the SAME tbox_style that already decided `font` above
     * (font_family/font_weight_bold/font_italic/font_size) for this run --
     * never NULL, a pointer into the tbox_style_table the caller already
     * passed to tbox_layout_build (or tbox_layout_default_style's address,
     * same fallback `font` itself already relies on for a node missing from
     * `styles`). Lets Render Pipeline read run->style->color/background_color/
     * text_decoration per RUN instead of per box (see ARCHITECTURE.md's "v13
     * -- Layout Tree" section: `<mark>`/`<del>`/`<ins>`/`<sub>`/`<sup>` all
     * need this, and it also fixes color varying only by box, a pre-existing
     * debt since v2). Also read internally by the Layout Tree itself (see
     * src/layout/tbox_layout.c's tbox_layout_build_line_runs) BEFORE this
     * struct is even filled in, to decide `rect.y`'s extra `vertical_align`
     * offset for `sub`/`sup`. */
    const tbox_style *style;

    /* NOVO (image support): non-NULL for a run built from an <img> word
     * (tbox_layout_push_image_word in src/layout/tbox_layout.c) instead of
     * text -- `text`/`font` are then meaningless (empty/whatever the
     * surrounding text context's face happened to be, never read for an
     * image run), and `rect` is the image's own resolved destination
     * rectangle (already scaled from `image`'s intrinsic pixel dimensions
     * if CSS declared a different width/height) rather than sharing the
     * line's full height the way a text run's `rect.height` does. NULL for
     * an ordinary text run. */
    const tbox_image *image;
} tbox_layout_text_run;

typedef struct tbox_layout_box {
    const tbox_html_node *node; /* NULL for anonymous boxes (unused in v2: inline elements still don't get their own box, see ARCHITECTURE.md's "Fora de escopo") */
    const tbox_style *style;

    tbox_rect margin_box, border_box, padding_box, content_box;
    double scroll_content_height; /* laid-out children extent before viewport clipping */

    /* Text runs, for the Render Pipeline's TEXT_RUN paint ops (see
     * ARCHITECTURE.md's "Render Pipeline" section). Populated only for a
     * leaf box built from one of the fixed text tags (h1-h6, p) -- an empty
     * array (text_run_count == 0, text_runs NULL) for every other box,
     * including a <div> with text nodes inside (not shown, exactly like
     * v0/v1) and a text tag whose own text collapses to nothing. Multiple
     * runs happen once the text wraps onto more than one line, or once the
     * face changes mid-line (e.g. a <b> inside a <p>) -- see
     * tbox_layout_text_run above. */
    tbox_layout_text_run *text_runs;
    size_t text_run_count;

    struct tbox_layout_box *parent, *first_child, *last_child, *next_sibling;
} tbox_layout_box;

/* Builds the layout tree rooted at `root` (either a TBOX_HTML_NODE_DOCUMENT,
 * treated as transparent -- the single box built is for its first ELEMENT
 * child, typically <html> -- or an ELEMENT node passed directly, useful for
 * laying out one fragment in isolation) against `styles` (see
 * tbox_style_table, already resolved by the Style layer) and `fonts` (NOVO
 * v2: a cache of faces keyed by (bold, size_px) -- see <tbox/font.h>'s
 * tbox_font_face_cache -- used to resolve the right face for every
 * text-bearing box's own resolved style->font_weight_bold/font_size, rather
 * than a single face shared by the whole document like in v0/v1; Layout
 * Tree never loads a font itself, only looks one up in this cache) and
 * `images` (same shape, for `<img>` -- see <tbox/image.h>'s
 * tbox_image_cache; NULL is a valid "no images" value, same as a NULL font
 * resolver, so every `<img>` then simply contributes nothing, same as a
 * missing `src`). `viewport_width`/`viewport_height` become the root box's
 * containing block's content width/height, positioned at (0, 0).
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
tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double viewport_width, double viewport_height);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_LAYOUT_H */
