#ifndef TBOX_STYLE_H
#define TBOX_STYLE_H

#include <stddef.h>

#include <tbox/css_cascade.h>
#include <tbox/html_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque forward declaration: tbox_arena is defined in the internal
 * src/base/tbox_arena.h, not under include/tbox/ -- this header only ever
 * takes a pointer to it (tbox_style_resolve_tree below), never dereferences
 * it, the same way tbox_html_document stays opaque across the public API.
 * The .c file that implements tbox_style_resolve_tree includes the internal
 * header for the full definition. */
typedef struct tbox_arena tbox_arena;

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Bridges tbox_css_computed_style (plain text: property/value pairs, no
 * unit parsing -- see <tbox/css_cascade.h>) to typed, complete values the
 * Layout Tree can consume directly. Every property in "Scope" below always
 * has a value on the resulting tbox_style, coming from either the cascade
 * (a declaration won for that property), inheritance (inheritable
 * properties copy the parent's resolved value when nothing was declared),
 * or the property's CSS2.1 initial value (non-inheritable properties, or
 * inheritable ones with no parent). */

typedef enum tbox_style_length_kind {
    TBOX_STYLE_LENGTH_AUTO,
    TBOX_STYLE_LENGTH_PX,      /* already absolute: this v0 supports only bare "px" values, no em/rem/pt (see Scope) */
    TBOX_STYLE_LENGTH_PERCENT, /* resolved against the containing block only later, by the Layout Tree */
} tbox_style_length_kind;

typedef struct tbox_style_length {
    tbox_style_length_kind kind;
    double value; /* px or percent, per kind; meaningless (0) when kind == AUTO */
} tbox_style_length;

typedef enum tbox_style_display {
    TBOX_STYLE_DISPLAY_BLOCK,
    TBOX_STYLE_DISPLAY_INLINE,
    TBOX_STYLE_DISPLAY_NONE,
} tbox_style_display;

/* NOVO v4: only `solid` is ever painted (see Render Pipeline); `none` and
 * any unsupported keyword both resolve here, indistinguishable from each
 * other and from "no border declared" -- see ARCHITECTURE.md's v4 Style
 * section. */
typedef enum tbox_style_border_style {
    TBOX_STYLE_BORDER_STYLE_NONE, /* initial */
    TBOX_STYLE_BORDER_STYLE_SOLID,
} tbox_style_border_style;

/* NOVO v4: the visual-offset axis of `position: relative`. NOVO v5:
 * `absolute`/`fixed`/`sticky` -- see ARCHITECTURE.md's v5 Style section.
 * `STICKY` is treated as an exact synonym of `RELATIVE` everywhere outside
 * the Style layer (Layout Tree, Render Pipeline, Orchestration); here it is
 * just one more enum value the Style layer recognizes. */
typedef enum tbox_style_position {
    TBOX_STYLE_POSITION_STATIC, /* initial */
    TBOX_STYLE_POSITION_RELATIVE,
    TBOX_STYLE_POSITION_ABSOLUTE, /* NOVO v5 */
    TBOX_STYLE_POSITION_FIXED,    /* NOVO v5 */
    TBOX_STYLE_POSITION_STICKY,   /* NOVO v5 -- treated as RELATIVE outside the Style layer, see above */
} tbox_style_position;

/* NOVO v11: `text-align`'s three supported values -- `justify` is out of
 * scope (see ARCHITECTURE.md's v11 Style section). `LEFT` is the initial
 * value and, not coincidentally, the enum's first/zero member. */
typedef enum tbox_style_text_align {
    TBOX_STYLE_TEXT_ALIGN_LEFT, /* initial */
    TBOX_STYLE_TEXT_ALIGN_CENTER,
    TBOX_STYLE_TEXT_ALIGN_RIGHT,
} tbox_style_text_align;

typedef struct tbox_style {
    /* Initial value in v0 is TBOX_STYLE_DISPLAY_BLOCK, NOT CSS2.1's
     * spec-correct `inline` -- a deliberate v0 simplification, since there
     * is no user-agent stylesheet yet to make e.g. <span> default to
     * inline while <div> defaults to block; every element defaults to
     * block until `display` is taught to read a per-tag table. See
     * ARCHITECTURE.md's Style section. */
    tbox_style_display display;
    tbox_style_length width, height; /* initial: AUTO */
    tbox_style_length margin[4];     /* top right bottom left; initial: 0px each */
    tbox_style_length padding[4];    /* top right bottom left; initial: 0px each */
    tbox_css_rgba color;             /* inheritable; initial (no parent): opaque black */
    tbox_css_rgba background_color;  /* not inheritable; initial: transparent, i.e. {0, 0, 0, 0} */
    /* NOVO v2: always absolute px, never a tbox_style_length -- unlike
     * width/height (PERCENT deferred to the Layout Tree, whose containing
     * block doesn't exist yet at Style-resolve time), font-size in em/%
     * resolves against the parent's already-resolved font_size right here,
     * since tbox_style_resolve_tree's top-down walk guarantees that value
     * is available (same ordering that already supports `color`
     * inheritance). Initial value (no parent): 16px, the same default
     * already used by Fonte/Texto since v0. */
    double font_size;
    /* NOVO v2: only the "bold"/not-bold axis -- see ARCHITECTURE.md's Style
     * section for what's out of scope (numeric 100-900, bolder/lighter).
     * Inheritable like `color`; initial value (no parent): false. */
    bool font_weight_bold;
    /* NOVO v4: `border` shorthand (width + style + color, order-free, each
     * optional -- only `solid` is ever painted). Not inheritable, same
     * treatment as `width`/`background-color`: always cascade-or-initial,
     * never looks at the parent. See ARCHITECTURE.md's v4 Style section. */
    double border_width;                  /* px; initial 0.0 -- no thin/medium/thick */
    tbox_style_border_style border_style; /* initial NONE */
    tbox_css_rgba border_color;           /* initial: opaque black (no currentColor) */
    /* NOVO v4: `position: relative` + offsets. Not inheritable. */
    tbox_style_position position; /* initial STATIC */
    tbox_style_length offset[4];  /* top right bottom left; initial: AUTO, same type as margin/padding */
    /* NOVO v11: `text-align`. Inheritable, same mechanism as `color`/
     * `font_weight_bold` above (herda do pai já resolvido se não
     * declarado/reconhecido; `LEFT` -- o valor inicial -- sem pai). */
    tbox_style_text_align text_align;
    /* Grows by supported property; see "Scope" below for what v0 covers. */
} tbox_style;

/* Resolves `node`'s own tbox_style from `computed` (its already-cascaded
 * text declarations -- see tbox_css_cascade_resolve_stylesheet). Per-node,
 * same granularity as tbox_css_cascade_resolve: it does not walk `node`'s
 * children (see tbox_style_resolve_tree for that). `parent_style` is
 * `node`'s parent's already-resolved style, or NULL for the root (or any
 * node whose parent has no style, e.g. a node not reached by
 * tbox_style_resolve_tree's own walk) -- only used for inheritable
 * properties (currently just `color`); NULL falls back to that property's
 * own initial value instead of inheriting.
 *
 * Scope (v0): `display` (`block`/`inline`/`none`, case-insensitive
 * keywords), `width`, `height` (`auto`, a bare number followed by `px`, or
 * a bare number followed by `%` -- no `em`/`rem`, those are out of scope
 * until the font/text layer exists; an unparsable value falls back to the
 * initial value, same as if the property were undeclared), `margin`,
 * `padding` (CSS2.1 1/2/3/4-value shorthand only -- longhands like
 * `margin-top` are out of scope in v0), `color`, `background-color`
 * (any syntax tbox_css_color_parse accepts), `font-size` (NOVO v2: a bare
 * number followed by `px` -- absolute --, `em` -- `parent_style->font_size
 * * number` --, or `%` -- `parent_style->font_size * number / 100`; an
 * absent/unparsable value, or any CSS2.1 keyword like `medium`/`larger`
 * -- out of scope --, inherits `parent_style->font_size`, or falls back to
 * 16px with no parent), `font-weight` (NOVO v2: only the exact
 * case-insensitive keyword `bold` sets `font_weight_bold = true`; anything
 * else -- absent, `normal`, a numeric 100-900, `bolder`/`lighter` -- out of
 * scope --, inherits `parent_style->font_weight_bold`, same inheritance
 * mechanism as `color`, or falls back to `false` with no parent), `border`
 * (NOVO v4: shorthand only -- width/style/color, order-free, each optional;
 * see tbox_style_border_style and ARCHITECTURE.md's v4 Style section for the
 * exact per-token classification; `border-top`/`-right`/`-bottom`/`-left`
 * and the longhands `border-width`/`border-style`/`border-color` are out of
 * scope, as is any `border-style` besides `solid`/`none`), `position`
 * (NOVO v4: `static`/`relative`; NOVO v5: `absolute`/`fixed`/`sticky`, all
 * case-insensitive; any other value falls back to the initial value
 * `STATIC`, same posture as `display` since v0), `top`, `right`, `bottom`,
 * `left` (NOVO v4: same length parser as `width`/`margin` -- `auto`, px, or
 * `%` -- only meaningful when `position` is non-`static`, but always
 * resolved regardless of `position`), `text-align` (NOVO v11: `left`/
 * `center`/`right`, case-insensitive; any other value -- including
 * `justify`, out of scope -- falls back to the same "not recognized =
 * inherits" treatment `font-weight` already gets, or the initial value
 * `LEFT` with no parent). Out of scope: `float`, flex/grid, `z-index`,
 * `white-space` (v0 always behaves as `white-space: normal`), `font-style`
 * (italic). */
tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed);

typedef struct tbox_style_entry {
    const tbox_html_node *node;
    tbox_style style;
} tbox_style_entry;

typedef struct tbox_style_table {
    tbox_style_entry *items;
    size_t count;
} tbox_style_table;

/* `arena` is supplied by the caller (same pattern as tbox_layout_build and
 * tbox_render_build_display_list downstream) -- tbox_style_table has no
 * `_destroy` of its own; its lifetime is the arena's (see "Convenções" at
 * the top of ARCHITECTURE.md). Walks `root`'s tree top-down (a parent is
 * always resolved before its children, since inheritance depends on it),
 * running tbox_css_cascade_resolve(sources, source_count, node) followed by
 * tbox_style_resolve on every TBOX_HTML_NODE_ELEMENT node (TEXT/COMMENT/
 * DOCTYPE/DOCUMENT nodes have no style of their own and are skipped, though
 * still walked through so their ELEMENT descendants are reached). Each
 * intermediate tbox_css_computed_style is destroyed as soon as
 * tbox_style_resolve has consumed it -- its lifetime is not the table's.
 *
 * NOVO v2: takes a `tbox_css_cascade_source` array instead of a single
 * `tbox_css_stylesheet*` (mirrors tbox_css_cascade_resolve's own signature)
 * so a user-agent stylesheet can be layered under the author one -- see
 * ARCHITECTURE.md's Style section. A single-source, AUTHOR-origin caller
 * passes `source_count == 1`. */
tbox_style_table tbox_style_resolve_tree(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count);

/* Linear scan for `node`'s entry, same pattern as tbox_css_computed_style_find
 * and tbox_css_selector_match -- acceptable for UI-sized trees (tens to a
 * few thousand nodes); if O(n) per lookup ever becomes a measured
 * bottleneck, revisit alongside the dirty-tracking debt (only the
 * implementation would gain an auxiliary index, not this signature).
 * Returns NULL if table == NULL, node == NULL, or no entry has that node. */
const tbox_style *tbox_style_table_find(const tbox_style_table *table, const tbox_html_node *node);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_STYLE_H */
