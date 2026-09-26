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
    TBOX_STYLE_DISPLAY_INLINE_BLOCK, /* NOVO v15: an atomic box inside a line */
    TBOX_STYLE_DISPLAY_FLEX,         /* NOVO v16: block-level flex container */
    TBOX_STYLE_DISPLAY_INLINE_FLEX,  /* NOVO v16: flex container placed like an inline-block */
} tbox_style_display;

/* NOVO v16: flexbox. Every enum's zero value is the CSS initial value. */
typedef enum tbox_style_flex_direction {
    TBOX_STYLE_FLEX_DIRECTION_ROW, /* initial */
    TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE,
    TBOX_STYLE_FLEX_DIRECTION_COLUMN,
    TBOX_STYLE_FLEX_DIRECTION_COLUMN_REVERSE,
} tbox_style_flex_direction;

typedef enum tbox_style_flex_wrap {
    TBOX_STYLE_FLEX_WRAP_NOWRAP, /* initial */
    TBOX_STYLE_FLEX_WRAP_WRAP,
    TBOX_STYLE_FLEX_WRAP_WRAP_REVERSE,
} tbox_style_flex_wrap;

/* `justify-content` and `align-content`. NORMAL is the initial value: it
 * packs at the start for justify-content and stretches the lines for
 * align-content. `left`/`start` map to START, `right`/`end` to END. */
typedef enum tbox_style_flex_justify {
    TBOX_STYLE_FLEX_JUSTIFY_NORMAL, /* initial */
    TBOX_STYLE_FLEX_JUSTIFY_START,
    TBOX_STYLE_FLEX_JUSTIFY_END,
    TBOX_STYLE_FLEX_JUSTIFY_CENTER,
    TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN,
    TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND,
    TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY,
    TBOX_STYLE_FLEX_JUSTIFY_STRETCH,
} tbox_style_flex_justify;

/* `align-items` and `align-self`. NORMAL is the initial value of both:
 * `stretch` for align-items, `auto` (use the container's align-items) for
 * align-self. `start`/`self-start` map to START, `end`/`self-end` to END. */
typedef enum tbox_style_flex_align {
    TBOX_STYLE_FLEX_ALIGN_NORMAL, /* initial */
    TBOX_STYLE_FLEX_ALIGN_STRETCH,
    TBOX_STYLE_FLEX_ALIGN_START,
    TBOX_STYLE_FLEX_ALIGN_END,
    TBOX_STYLE_FLEX_ALIGN_CENTER,
    TBOX_STYLE_FLEX_ALIGN_BASELINE,
} tbox_style_flex_align;

typedef enum tbox_style_overflow_y {
    TBOX_STYLE_OVERFLOW_Y_VISIBLE,
    TBOX_STYLE_OVERFLOW_Y_AUTO,
    TBOX_STYLE_OVERFLOW_Y_HIDDEN,
} tbox_style_overflow_y;

typedef enum tbox_style_box_sizing {
    TBOX_STYLE_BOX_SIZING_CONTENT_BOX,
    TBOX_STYLE_BOX_SIZING_BORDER_BOX,
} tbox_style_box_sizing;

typedef enum tbox_style_line_height_kind {
    TBOX_STYLE_LINE_HEIGHT_NORMAL,
    TBOX_STYLE_LINE_HEIGHT_NUMBER,
    TBOX_STYLE_LINE_HEIGHT_PX,
} tbox_style_line_height_kind;

typedef enum tbox_style_text_overflow {
    TBOX_STYLE_TEXT_OVERFLOW_CLIP,
    TBOX_STYLE_TEXT_OVERFLOW_ELLIPSIS,
} tbox_style_text_overflow;

/* Border and outline styles. `none`/`hidden` and "no border declared" all
 * resolve to NONE. `groove`, `ridge`, `inset` and `outset` paint as SOLID
 * (no 3D shading). Rounded boxes paint every style as SOLID. */
typedef enum tbox_style_border_style {
    TBOX_STYLE_BORDER_STYLE_NONE, /* initial */
    TBOX_STYLE_BORDER_STYLE_SOLID,
    TBOX_STYLE_BORDER_STYLE_DASHED,
    TBOX_STYLE_BORDER_STYLE_DOTTED,
    TBOX_STYLE_BORDER_STYLE_DOUBLE,
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

/* NOVO v11: `text-align`. `start`/`end` map to LEFT/RIGHT (text is always
 * left-to-right). JUSTIFY stretches the gaps between words on every line
 * but the last one of a paragraph and lines ended by a forced break. `LEFT`
 * is the initial value and the enum's first/zero member. */
typedef enum tbox_style_text_align {
    TBOX_STYLE_TEXT_ALIGN_LEFT, /* initial */
    TBOX_STYLE_TEXT_ALIGN_CENTER,
    TBOX_STYLE_TEXT_ALIGN_RIGHT,
    TBOX_STYLE_TEXT_ALIGN_JUSTIFY,
} tbox_style_text_align;

/* `text-decoration` supports one line at a time; no multi-value
 * declarations or `blink` (see ARCHITECTURE.md's v13 "Fora de
 * escopo"). `NONE` is the initial value and the enum's first/zero member. */
typedef enum tbox_style_text_decoration {
    TBOX_STYLE_TEXT_DECORATION_NONE, /* initial */
    TBOX_STYLE_TEXT_DECORATION_UNDERLINE,
    TBOX_STYLE_TEXT_DECORATION_LINE_THROUGH,
    TBOX_STYLE_TEXT_DECORATION_OVERLINE,
} tbox_style_text_decoration;

/* Baseline/sub/super/text-top/text-bottom/<length> apply to inline text;
 * top/middle/bottom position content in table cells. */
typedef enum tbox_style_vertical_align {
    TBOX_STYLE_VERTICAL_ALIGN_BASELINE, /* initial */
    TBOX_STYLE_VERTICAL_ALIGN_SUB,
    TBOX_STYLE_VERTICAL_ALIGN_SUPER,
    TBOX_STYLE_VERTICAL_ALIGN_TOP,
    TBOX_STYLE_VERTICAL_ALIGN_MIDDLE,
    TBOX_STYLE_VERTICAL_ALIGN_BOTTOM,
    TBOX_STYLE_VERTICAL_ALIGN_TEXT_TOP,    /* top edge on the block's font ascent */
    TBOX_STYLE_VERTICAL_ALIGN_TEXT_BOTTOM, /* bottom edge on the block's font descent */
    TBOX_STYLE_VERTICAL_ALIGN_LENGTH,      /* raised by vertical_align_length */
} tbox_style_vertical_align;

/* `list-style-type` for an <li> inside <ul>/<ol>. AUTO (the zero value,
 * never produced by CSS) keeps the tag default -- disc for <ul>, decimal
 * for <ol> -- for callers resolving without the user-agent stylesheet. */
typedef enum tbox_style_list_style_type {
    TBOX_STYLE_LIST_STYLE_AUTO,
    TBOX_STYLE_LIST_STYLE_DISC, /* initial */
    TBOX_STYLE_LIST_STYLE_CIRCLE,
    TBOX_STYLE_LIST_STYLE_SQUARE,
    TBOX_STYLE_LIST_STYLE_DECIMAL,
    TBOX_STYLE_LIST_STYLE_LOWER_ALPHA,
    TBOX_STYLE_LIST_STYLE_UPPER_ALPHA,
    TBOX_STYLE_LIST_STYLE_LOWER_ROMAN,
    TBOX_STYLE_LIST_STYLE_UPPER_ROMAN,
    TBOX_STYLE_LIST_STYLE_NONE,
} tbox_style_list_style_type;

/* `white-space`. AUTO (the zero value, never produced by CSS) acts as
 * NORMAL except on a <pre> element, which keeps its verbatim behavior for
 * callers resolving without the user-agent stylesheet (that sheet sets
 * `pre { white-space: pre }` explicitly). */
typedef enum tbox_style_white_space {
    TBOX_STYLE_WHITE_SPACE_AUTO,
    TBOX_STYLE_WHITE_SPACE_NORMAL,
    TBOX_STYLE_WHITE_SPACE_NOWRAP,
    TBOX_STYLE_WHITE_SPACE_PRE,      /* spaces and newlines kept, no wrapping */
    TBOX_STYLE_WHITE_SPACE_PRE_WRAP, /* spaces and newlines kept, wraps */
    TBOX_STYLE_WHITE_SPACE_PRE_LINE, /* spaces collapse, newlines kept, wraps */
} tbox_style_white_space;

typedef enum tbox_style_text_transform {
    TBOX_STYLE_TEXT_TRANSFORM_NONE, /* initial */
    TBOX_STYLE_TEXT_TRANSFORM_UPPERCASE,
    TBOX_STYLE_TEXT_TRANSFORM_LOWERCASE,
    TBOX_STYLE_TEXT_TRANSFORM_CAPITALIZE,
} tbox_style_text_transform;

typedef enum tbox_style_caption_side {
    TBOX_STYLE_CAPTION_TOP,
    TBOX_STYLE_CAPTION_BOTTOM,
} tbox_style_caption_side;

typedef struct tbox_style {
    /* Initial value in v0 is TBOX_STYLE_DISPLAY_BLOCK, NOT CSS2.1's
     * spec-correct `inline` -- a deliberate v0 simplification, since there
     * is no user-agent stylesheet yet to make e.g. <span> default to
     * inline while <div> defaults to block; every element defaults to
     * block until `display` is taught to read a per-tag table. See
     * ARCHITECTURE.md's Style section. */
    tbox_style_display display;
    tbox_style_overflow_y overflow_y; /* initial: visible; auto scrolls, auto/hidden clip */
    tbox_style_box_sizing box_sizing; /* initial: content-box */
    bool visibility_hidden;           /* inheritable; hidden keeps layout */
    tbox_style_text_overflow text_overflow; /* clip or ellipsis; not inheritable */
    tbox_style_white_space white_space; /* inheritable; initial AUTO */
    bool overflow_wrap_break_word;    /* inheritable; normal by default */
    bool word_break_all;              /* inheritable; `word-break: break-all` */
    tbox_style_text_transform text_transform; /* inheritable; initial NONE */
    tbox_style_list_style_type list_style_type; /* inheritable */
    bool pointer_events_none;         /* inheritable; auto by default */
    tbox_style_length width, height; /* initial: AUTO */
    tbox_style_length min_width, max_width; /* AUTO means no constraint; em resolves to px */
    tbox_style_length min_height, max_height; /* AUTO means no constraint; em resolves to px */
    tbox_style_length margin[4];     /* top right bottom left; initial: 0px each */
    tbox_style_length padding[4];    /* top right bottom left; initial: 0px each */
    tbox_style_length text_indent;   /* inheritable; first line, px or %; initial 0px */
    double word_spacing;             /* inheritable; px; initial 0 */
    double letter_spacing;           /* inheritable; px; initial 0 */
    tbox_style_line_height_kind line_height_kind; /* inheritable */
    double line_height_value;        /* multiplier or absolute px */
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
    /* Only the bold/not-bold axis: bold/600-900 versus normal/100-500.
     * Inheritable like `color`; initial value (no parent): false. */
    bool font_weight_bold;
    /* NOVO v4: `border` shorthand (width + style + color, order-free, each
     * optional -- only `solid` is ever painted). Not inheritable, same
     * treatment as `width`/`background-color`: always cascade-or-initial,
     * never looks at the parent. See ARCHITECTURE.md's v4 Style section. */
    double border_width;                  /* px; initial 0.0; thin/medium/thick = 1/3/5px */
    tbox_style_border_style border_style; /* initial NONE */
    tbox_css_rgba border_color;           /* initial: current text color */
    /* Per-side borders (top, right, bottom, left), always filled by
     * tbox_style_resolve. When the four sides agree, border_per_side is
     * false and the uniform fields above carry the same values; otherwise
     * it is true and the uniform fields hold the top side. A hand-built
     * style may set only the uniform fields and leave border_per_side
     * false. Read borders through tbox_style_border_side_width/_color. */
    double border_widths[4];
    tbox_style_border_style border_styles[4];
    tbox_css_rgba border_colors[4];
    bool border_per_side;
    double outline_width;                 /* px; initial 3 (medium); style NONE means no paint */
    tbox_style_border_style outline_style; /* none or solid */
    tbox_css_rgba outline_color;          /* initial: current text color */
    double outline_offset;                /* px; may be negative; initial 0 */
    /* NOVO v4: `position: relative` + offsets. Not inheritable. */
    tbox_style_position position; /* initial STATIC */
    tbox_style_length offset[4];  /* top right bottom left; initial: AUTO, same type as margin/padding */
    /* NOVO v11: `text-align`. Inheritable, same mechanism as `color`/
     * `font_weight_bold` above (herda do pai já resolvido se não
     * declarado/reconhecido; `LEFT` -- o valor inicial -- sem pai). */
    tbox_style_text_align text_align;
    /* NOVO v12: `font-family`. Fixed buffer, NOT a tbox_string_view -- every
     * field of tbox_style is copied by value, pointing at no external memory;
     * a view would dangle for the synthetic `style=""` stylesheet (v9),
     * created and destroyed entirely inside tbox_css_cascade_resolve (see
     * ARCHITECTURE.md's v12 Style section). Inheritable, same mechanism as
     * `color` above. `""` = no override anywhere in the inheritance chain --
     * the initial value. */
    char font_family[64];
    /* NOVO v13: `font-style: italic`. Inheritable, same mechanism as
     * `font_weight_bold` above; initial value (no parent): false. */
    bool font_italic;
    /* NOVO v13: `text-decoration`. NOT inheritable (same posture as
     * `background_color`); initial value NONE. */
    tbox_style_text_decoration text_decoration;
    tbox_css_rgba text_decoration_color; /* initial: current text color */
    double text_decoration_thickness;    /* px; initial: 1 */
    /* Inheritable. AUTO keeps the default underline position; PX is the
     * distance from the baseline to the underline's top edge. */
    tbox_style_length text_underline_offset;
    /* NOVO v13: `vertical-align`. NOT inheritable; initial value BASELINE. */
    tbox_style_vertical_align vertical_align;
    /* Only meaningful for VERTICAL_ALIGN_LENGTH: PX, or PERCENT of the
     * element's own line-height, resolved by the Layout Tree. Positive
     * values raise the text. */
    tbox_style_length vertical_align_length;
    tbox_style_caption_side caption_side;
    bool border_collapse;
    double border_spacing_x, border_spacing_y;
    /* Circular corner radii in px. The scalar retains the old uniform value
     * for callers that build styles directly; shorthand and longhand CSS
     * resolve into border_radius_corners in clockwise order. Elliptical
     * radii remain unsupported. */
    double border_radius;
    double border_radius_corners[4]; /* top-left, top-right, bottom-right, bottom-left */
    /* Corners declared as a percentage (0 otherwise), resolved by Render
     * against the smaller side of the border box -- a circular stand-in for
     * CSS's elliptical percentage radii, exact for squares (50% = circle). */
    double border_radius_percent[4];
    /* NOVO (visual fidelity): `box-shadow: <offset-x> <offset-y>
     * [<blur-radius>] <color>` -- ONE shadow only (no comma-separated list,
     * no `inset`, no spread-radius -- see tbox_style_resolve_box_shadow).
     * NOT inheritable. box_shadow_color.a == 0 means "no shadow", same
     * "alpha 0 means absent" convention background_color already has --
     * that's also the initial value, so an undeclared box-shadow paints
     * nothing. */
    double box_shadow_offset_x, box_shadow_offset_y, box_shadow_blur; /* px; initial 0.0 */
    double box_shadow_spread;                                        /* px, may be negative; initial 0.0 */
    tbox_css_rgba box_shadow_color;                                  /* initial transparent */
    /* `text-shadow`: one shadow, `<x> <y> [<blur>] [<color>]`. Inheritable;
     * text_shadow_color.a == 0 means none (the initial value). */
    double text_shadow_offset_x, text_shadow_offset_y, text_shadow_blur; /* px */
    tbox_css_rgba text_shadow_color;
    /* `opacity`, 0..1 (numbers or percentages, clamped). NOT inheritable,
     * but Render multiplies it into the element's whole subtree. Initial
     * 1.0 -- a hand-built, zero-initialized tbox_style must set it, or the
     * element paints nothing. */
    double opacity;
    /* Form control colors, both inheritable. Alpha 0 means `auto` (the
     * initial value), which paints with the element's own `color` --
     * same "alpha 0 means absent" convention as box_shadow_color. */
    tbox_css_rgba accent_color; /* checked checkbox mark and radio dot */
    tbox_css_rgba caret_color;  /* text insertion caret */
    /* NOVO v16: flexbox, none inheritable. Container properties: */
    tbox_style_flex_direction flex_direction;
    tbox_style_flex_wrap flex_wrap;
    tbox_style_flex_justify justify_content, align_content;
    tbox_style_flex_align align_items;
    tbox_style_length row_gap, column_gap; /* PX or PERCENT; AUTO (`normal`) = 0 */
    /* Item properties. flex_basis AUTO means `auto` (and `content`). A
     * hand-built, zero-initialized style has flex-shrink 0, not CSS's 1. */
    tbox_style_flex_align align_self;
    double flex_grow, flex_shrink; /* initial 0 and 1 */
    tbox_style_length flex_basis;
    int order;
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
 * Scope (v0): `display` (`block`/`inline`/`none`, NOVO v15 `inline-block`, case-insensitive
 * keywords), `width`, `height` (`auto`, a bare number followed by `px`, or
 * a bare number followed by `%` -- no `em`/`rem`, those are out of scope
 * until the font/text layer exists; an unparsable value falls back to the
 * initial value, same as if the property were undeclared), `min-width` and
 * `max-width` (nonnegative px/em/%; `max-width: none` removes the limit), `margin`,
 * `padding` (CSS2.1 1/2/3/4-value shorthand and per-side longhands),
 * `min-height` and `max-height` (nonnegative px/em/%, with percentage heights
 * requiring a definite containing-block height), `color`,
 * `background-color`, `background: <color>`
 * (any syntax tbox_css_color_parse accepts, plus currentColor), `font-size` (a bare
 * number followed by `px` -- absolute --, `em` -- `parent_style->font_size
 * * number` --, or `%` -- `parent_style->font_size * number / 100`;
 * xx-small through xx-large use a fixed pixel scale, and smaller/larger
 * divide/multiply the parent size by 1.2; absent/unparsable values inherit
 * the parent's size, or use 16px with no parent), `font-weight` (`bold` selects bold;
 * `normal` and `100` through `500` select regular; `600` through `900` select
 * bold; absent inherits), `border`
 * (NOVO v4: width/style/color shorthand, order-free, each optional;
 * see tbox_style_border_style and ARCHITECTURE.md's v4 Style section for the
 * exact per-token classification; uniform `border-width`/`border-style`/
 * `border-color` longhands, now with 1-4 values, are supported with normal
 * cascade precedence, as are the per-side `border-top`/`-right`/`-bottom`/
 * `-left` shorthands and their `-width`/`-style`/`-color` longhands; styles
 * `solid`, `dashed`, `dotted`, `double`, `none`, `hidden`, with the 3D
 * styles painted solid), `position`
 * (NOVO v4: `static`/`relative`; NOVO v5: `absolute`/`fixed`/`sticky`, all
 * case-insensitive; any other value falls back to the initial value
 * `STATIC`, same posture as `display` since v0), `top`, `right`, `bottom`,
 * `left` (NOVO v4: same length parser as `width`/`margin` -- `auto`, px, or
 * `%` -- only meaningful when `position` is non-`static`, but always
 * resolved regardless of `position`), `text-align` (NOVO v11: `left`/
 * `center`/`right`/`justify`/`start`/`end`, case-insensitive; any other
 * value falls back to the same "not recognized =
 * inherits" treatment `font-weight` already gets, or the initial value
 * `LEFT` with no parent), `font-family` (NOVO v12: only the FIRST name of a
 * comma-separated list is used -- a full list is never kept for fallback --
 * a name in quotes (`"Courier New"`) is recognized with the quotes stripped;
 * inheritable, same mechanism as `color`, falling back to `""` -- no
 * override -- with no parent), `font-style` (`italic`, `oblique`, or `normal`;
 * absent inherits; `oblique` uses the italic face), same inheritance mechanism
 * as `font-weight`, or falls back to `false` with no parent), `text-decoration`
 * (`underline`/`line-through`/`overline`, case-insensitive; any other
 * value/absent falls back to the initial value `NONE`; NOT inheritable --
 * always cascade-or-initial, same posture as `background-color`),
 * `text-decoration-color` (a solid color) and `text-decoration-thickness`
 * (nonnegative px/em), `white-space` (`normal`, `nowrap`, `pre`,
 * `pre-wrap`, `pre-line`; inheritable -- the text box's own value decides
 * whether its lines wrap, each text node's value how its spaces and
 * newlines are kept),
 * `overflow-wrap` (`normal`/`break-word`, inheritable), `pointer-events`
 * (`auto`/`none`, inheritable, applies to pointer hit testing),
 * `word-spacing` (normal or px/em, inheritable), `text-indent` (px/em/%,
 * inheritable), `outline` (uniform width/solid/color) and its width/style/
 * color longhands, `outline-offset` (signed px/em), `currentColor` for
 * background, border, and outline colors, and `thin`/`medium`/`thick` border widths,
 * `overflow-y` (`visible`, `auto`, or `hidden`; auto clips and scrolls,
 * hidden only clips),
 * `vertical-align` (`sub`/`super` for inline text and `top`/`middle`/
 * `bottom` for table cells; NOT inheritable), `caption-side` (`top`/`bottom`),
 * `border-collapse` (`separate`/`collapse`), and nonnegative one/two-value
 * `border-spacing` in px/em (all three table properties inherit),
 * `inset` (1-4 values onto top/right/bottom/left) and the logical
 * `margin-`/`padding-`/`inset-` `block`/`inline` shorthands and
 * `-start`/`-end` longhands (left-to-right: block = top/bottom, inline =
 * left/right; normal cascade precedence against the physical properties),
 * `font-weight: bolder|lighter` (bold/regular), `overflow: clip` (as
 * `hidden`) and `scroll` (as `auto`), `border-style: hidden` (as `none`),
 * the `text-decoration` shorthand with line/color/thickness plus the
 * `text-decoration-line` longhand, `text-underline-offset` (px/em/%,
 * inheritable), `vertical-align: text-top|text-bottom|<length>|<percent>`,
 * inheritable `accent-color`/`caret-color`, `list-style-type` and the
 * `list-style` shorthand's type keyword (`disc`, `circle`, `square`,
 * `decimal`, `lower-alpha`/`-latin`, `upper-alpha`/`-latin`, `lower-roman`,
 * `upper-roman`, `none`; inheritable), `text-transform` (`uppercase`,
 * `lowercase`, `capitalize`, `none`; inheritable), one `text-shadow`
 * (inheritable), an optional `box-shadow` spread radius (a shadow color
 * defaults to currentColor), `word-break: break-all` (inheritable), and
 * `opacity` (0..1 or a percentage, clamped; not inheritable), and NOVO v16
 * flexbox: `display: flex|inline-flex`, `flex-direction`, `flex-wrap`,
 * `flex-flow`, `justify-content`, `align-items`, `align-self`,
 * `align-content`, `gap`/`row-gap`/`column-gap`, `flex-grow`,
 * `flex-shrink`, `flex-basis`, the `flex` shorthand and `order`.
 * Out of scope: `float`, flex/grid, `z-index`, `break-spaces`, `tab-size`. */
tbox_style tbox_style_resolve(const tbox_html_node *node, const tbox_style *parent_style, const tbox_css_computed_style *computed);

/* The painted border width of `side` (0 top, 1 right, 2 bottom, 3 left):
 * its width unless that side's style is NONE, 0 otherwise -- the space the
 * Layout Tree reserves and the strip Render paints. */
double tbox_style_border_side_width(const tbox_style *style, size_t side);

/* The border style of `side`, same side numbering. */
tbox_style_border_style tbox_style_border_side_style(const tbox_style *style, size_t side);

/* The border color of `side`, same side numbering. */
tbox_css_rgba tbox_style_border_side_color(const tbox_style *style, size_t side);

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
