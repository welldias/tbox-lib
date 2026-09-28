#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* Walks `node`'s children (TEXT children are no longer always
 * ignored, see below), skipping any ELEMENT whose resolved style->display
 * == TBOX_STYLE_DISPLAY_NONE entirely -- no box, no recursion into its
 * subtree, no contribution to the height sum returned.
 *
 * adjacent siblings now collapse their touching margins (CSS2.1
 * 8.3.1's sibling case -- see ARCHITECTURE.md) instead of always summing
 * them. In place of a single running `cursor_y`, this tracks `border_bottom`
 * (the y just past the last positioned sibling's OWN border_box -- not its
 * margin_box: its bottom margin hasn't been "spent" yet, so it stays free to
 * collapse with the next sibling's top margin) and `pending_margin_bottom`
 * (that unspent margin; 0.0 before the first child, which is why the first
 * child's own top margin never collapses with anything -- out of scope here,
 * unchanged since v0-v3). Before building each child, its OWN top margin is
 * resolved (against `children_container.width`, the same base every margin
 * already resolves against) so the gap can be decided ahead of the call: when
 * both the pending bottom margin and this child's top margin are >= 0, the
 * gap collapses to `max(pending_margin_bottom, child_margin_top)`; otherwise
 * (either one negative -- CSS2.1's full negative-margin algorithm is out of
 * scope, see ARCHITECTURE.md) the pair falls back to today's behavior, a
 * plain sum of the two. Once the child is built, `border_bottom`/
 * `pending_margin_bottom` are refreshed for the next iteration -- deriving
 * `border_bottom` as `cursor_y + child_box->margin_box.height -
 * child_margin_bottom` rather than reading `child_box->border_box.y`
 * directly: a `position: relative` child's border_box/margin_box carry its
 * visual offset (see below), but `cursor_y` (the static flow position this
 * child was placed at) and `margin_box.height` (unaffected by an x/y-only
 * offset) are not, so this keeps margin collapsing computed entirely in the
 * same unshifted flow coordinate space v0-v3 always used -- a
 * `position: relative` sibling still never disturbs where the next sibling
 * lands. The last child's pending bottom margin never collapses with
 * anything after it (parent/last-child collapsing is out of scope), so it's
 * added in full to the returned total.
 *
 * `positioned_context` (see tbox_layout_positioned_context above)
 * is threaded through so descendants deeper in the recursion know what
 * `absolute`/`fixed` boxes must position against. Before building each
 * child, `child_style->position` is now ALSO peeked (same pattern as
 * `margin[0]` above): `ABSOLUTE`/`FIXED` children are built against a
 * containing block carved out of `positioned_context.nearest_ancestor`
 * (ABSOLUTE) or `positioned_context.viewport` (FIXED) instead of
 * `children_container` -- and, critically, take NO part in flow at all:
 * `border_bottom`/`pending_margin_bottom` are left untouched by them (they
 * never collapse margins with any sibling, in or out of flow) and their
 * height is not added to the returned total (they never count toward the
 * parent's auto-height) -- exactly CSS2.1's rule that out-of-flow boxes do
 * not participate in the block formatting context they're removed from.
 * They are still linked into `parent_box->first_child`/`last_child`/
 * `next_sibling` normally: they remain children of the same DOM parent in
 * the layout tree, just with different geometry (see ARCHITECTURE.md). The
 * `cursor_y` passed to tbox_layout_build_element for them is whatever
 * `border_bottom` currently holds -- a valid double, required by the
 * function's signature, but never actually used for their geometry since
 * `style->position != STATIC/RELATIVE/STICKY` there takes the
 * `container.x`/`container.y`-based path instead.
 *
 * gains `container_style` -- `node`'s OWN already-resolved style
 * (the caller, tbox_layout_build_element, already has this as its local
 * `style`), used exclusively to synthesize an eventual anonymous box's style
 * (see tbox_layout_build_anonymous_box above), never for `node`'s ELEMENT
 * children (those keep resolving their OWN style via `styles`, unchanged).
 * Before deciding whether a child is skipped/out-of-flow/in-flow-block (the
 * three pre-v14 cases, all unchanged in behavior), each child is first
 * checked for whether it TRIGGERS a loose-inline-content sequence
 * (tbox_layout_is_inline_run_trigger: non-whitespace TEXT, or an in-flow
 * ELEMENT with display:inline -- see ARCHITECTURE.md's v14 "Algoritmo").
 * When it does, tbox_layout_inline_run_end finds the end of that sequence
 * (the first in-flow non-inline ELEMENT, the first out-of-flow ELEMENT, or
 * NULL), tbox_layout_build_anonymous_box builds ONE box for the whole
 * [run_start, run_end) range, and the outer loop resumes at `run_end` --
 * never `child->next_sibling` -- so the entire sequence is consumed in one
 * step. The anonymous box is linked into `parent_box->first_child`/
 * `last_child`/`next_sibling` exactly like any other child box (preserving
 * document order against real block siblings around it) and advances
 * `border_bottom` by its own height with NO margin contribution (it has
 * none, see ARCHITECTURE.md's "Escopo") -- `pending_margin_bottom` is
 * cleared afterwards, the same way a real block's own (here: zero) bottom
 * margin already clears it for the next sibling. A child that does NOT
 * trigger a sequence falls through to the pre-v14 behavior unchanged:
 * whitespace-only TEXT and display:none ELEMENTs are skipped with no box;
 * out-of-flow and in-flow-block ELEMENTs build via tbox_layout_build_element
 * exactly as before. */
double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box, tbox_layout_positioned_context positioned_context, const tbox_style *container_style) {
    double border_bottom         = start_y;
    double pending_margin_bottom = 0.0;
    tbox_layout_box *previous    = NULL;

    /* `<script>`/`<style>` are HTML5 "raw text" elements -- the
     * HTML parser already treats them specially, tokenizing their content
     * as opaque text rather than markup (see
     * src/html_parser/tbox_html_tree_builder.c). Their one TEXT child (CSS/
     * script source) must never become visible page content -- browsers
     * never render it, and tbox_context_collect_style_elements (Context
     * layer) already reads `<style>`'s raw text straight from the DOM for
     * the cascade, independently of the Layout Tree. Before v14 this held
     * "for free" (loose TEXT was ignored everywhere); the inline-run
     * detection below would otherwise now wrap that source text in an
     * anonymous box like any other loose text. Scoped to exactly these two
     * tags -- every other element keeps the new v14 behavior unchanged. */
    bool is_raw_text_container = tbox_string_view_equal_cstr(node->element.tag_name, "script") || tbox_string_view_equal_cstr(node->element.tag_name, "style");

    for (const tbox_html_node *child = node->first_child; child != NULL;) {
        if (child->type != TBOX_HTML_NODE_ELEMENT && child->type != TBOX_HTML_NODE_TEXT) {
            /* COMMENT/DOCTYPE: never a box, never a sequence trigger. */
            child = child->next_sibling;
            continue;
        }

        if (!is_raw_text_container && tbox_layout_is_inline_run_trigger(arena, child, styles)) {
            const tbox_html_node *run_start = child;
            const tbox_html_node *run_end   = tbox_layout_inline_run_end(run_start, styles);

            double cursor_y            = border_bottom + pending_margin_bottom;
            tbox_layout_box *child_box = tbox_layout_build_anonymous_box(arena, run_start, run_end, container_style, styles, fonts, images, children_container.x, cursor_y, children_container.width, &positioned_context);
            child_box->parent          = parent_box;
            if (previous == NULL) {
                parent_box->first_child = child_box;
            } else {
                previous->next_sibling = child_box;
            }
            parent_box->last_child = child_box;
            previous               = child_box;

            border_bottom         = cursor_y + child_box->margin_box.height;
            pending_margin_bottom = 0.0;

            child = run_end;
            continue;
        }

        if (child->type == TBOX_HTML_NODE_TEXT) {
            /* Whitespace-only TEXT that never triggered a sequence above:
             * contributes nothing -- unchanged since before v14. */
            child = child->next_sibling;
            continue;
        }

        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        if (tbox_layout_is_hidden_input(child) || child_style->display == TBOX_STYLE_DISPLAY_NONE) {
            child = child->next_sibling;
            continue;
        }

        if (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE || child_style->position == TBOX_STYLE_POSITION_FIXED) {
            tbox_rect basis                                    = (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE) ? positioned_context.nearest_ancestor : positioned_context.viewport;
            tbox_layout_containing_block out_of_flow_container = {
                .x               = basis.x,
                .y               = basis.y,
                .width           = basis.width,
                .height          = basis.height,
                .height_definite = true, /* a concrete rect -- always definite, see tbox_layout_positioned_context */
            };

            tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, images, out_of_flow_container, border_bottom, positioned_context, NULL, 0);
            child_box->parent          = parent_box;
            if (previous == NULL) {
                parent_box->first_child = child_box;
            } else {
                previous->next_sibling = child_box;
            }
            parent_box->last_child = child_box;
            previous               = child_box;

            /* Deliberately NOT touching border_bottom/pending_margin_bottom:
             * an out-of-flow child never collapses margins with, or advances
             * the cursor for, any sibling -- see doc comment above. */
            child = child->next_sibling;
            continue;
        }

        double child_margin_top = tbox_layout_resolve_edge(child_style->margin[0], children_container.width);

        double cursor_y;
        if (pending_margin_bottom >= 0.0 && child_margin_top >= 0.0) {
            double gap = pending_margin_bottom > child_margin_top ? pending_margin_bottom : child_margin_top;
            cursor_y   = border_bottom + gap - child_margin_top;
        } else {
            cursor_y = border_bottom + pending_margin_bottom;
        }

        tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, fonts, images, children_container, cursor_y, positioned_context, NULL, 0);
        child_box->parent          = parent_box;
        if (previous == NULL) {
            parent_box->first_child = child_box;
        } else {
            previous->next_sibling = child_box;
        }
        parent_box->last_child = child_box;
        previous               = child_box;

        double child_margin_bottom = tbox_layout_resolve_edge(child_style->margin[2], children_container.width);
        border_bottom              = cursor_y + child_box->margin_box.height - child_margin_bottom;
        pending_margin_bottom      = child_margin_bottom;

        child = child->next_sibling;
    }

    return (border_bottom - start_y) + pending_margin_bottom;
}

/* builds ONE anonymous block box (`box->node == NULL`, a
 * convention documented since v2 and already handled safely by the Context
 * layer's hit-test, see src/context/tbox_context.c) covering the sibling
 * range [run_start, run_end) -- a contiguous sequence of loose inline
 * content directly inside a block container (see ARCHITECTURE.md's v14
 * "Escopo" for the full rationale). Reuses the exact same inline formatting
 * mechanism a real text-tag element already uses
 * (tbox_layout_build_text_runs, generalized to accept a sibling range and a
 * NULL `node` earlier in this file) rather than a parallel implementation.
 *
 * The synthesized style starts from tbox_layout_default_style (every
 * NON-inheritable property at its CSS2.1 initial value -- display:block,
 * zero margin/padding/border, transparent background, no position -- so the
 * anonymous box paints nothing of its own and never double-paints the
 * container's background/border, see ARCHITECTURE.md) and then copies ONLY
 * the inherited fields needed by anonymous text and pointer hit testing
 * from `container_style` (see
 * include/tbox/style.h's "inheritable" comments on each field): `color`,
 * `font_family` (the whole fixed buffer, via memcpy -- not a pointer),
 * and every other field style.h marks inheritable (text layout, text
 * painting, table and form-control properties) except `text_indent`, which
 * only indents the block's own first line, not each anonymous box. This is
 * exactly what a real, undeclared child element would resolve to against
 * this same parent, computed here without calling back into the Style
 * layer (which already ran and has no entry point for "resolve a style with
 * no node").
 *
 * The returned box has no margin/padding/border of its own (see
 * ARCHITECTURE.md's "Escopo" -- CSS2.1 anonymous boxes never contribute a
 * box model of their own), so its four rects (content/padding/border/margin
 * box) are all identical: `{content_x, cursor_y, available_width, height}`,
 * `height` coming straight out of tbox_layout_build_text_runs. */

tbox_layout_box *tbox_layout_build_anonymous_box(tbox_arena *arena, const tbox_html_node *run_start, const tbox_html_node *run_end, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double cursor_y, double available_width, const tbox_layout_positioned_context *context) {
    tbox_style anon = tbox_layout_default_style;
    anon.color      = container_style->color;
    memcpy(anon.font_family, container_style->font_family, sizeof(anon.font_family));
    anon.font_weight_bold         = container_style->font_weight_bold;
    anon.font_italic              = container_style->font_italic;
    anon.font_size                = container_style->font_size;
    anon.text_align               = container_style->text_align;
    anon.overflow_wrap_break_word = container_style->overflow_wrap_break_word;
    anon.pointer_events_none      = container_style->pointer_events_none;
    anon.visibility_hidden        = container_style->visibility_hidden;
    anon.white_space              = container_style->white_space;
    anon.word_break_all           = container_style->word_break_all;
    anon.text_transform           = container_style->text_transform;
    anon.list_style_type          = container_style->list_style_type;
    anon.word_spacing             = container_style->word_spacing;
    anon.letter_spacing           = container_style->letter_spacing;
    anon.line_height_kind         = container_style->line_height_kind;
    anon.line_height_value        = container_style->line_height_value;
    anon.text_underline_offset    = container_style->text_underline_offset;
    anon.text_shadow_offset_x     = container_style->text_shadow_offset_x;
    anon.text_shadow_offset_y     = container_style->text_shadow_offset_y;
    anon.text_shadow_blur         = container_style->text_shadow_blur;
    anon.text_shadow_color        = container_style->text_shadow_color;
    anon.caption_side             = container_style->caption_side;
    anon.border_collapse          = container_style->border_collapse;
    anon.border_spacing_x         = container_style->border_spacing_x;
    anon.border_spacing_y         = container_style->border_spacing_y;
    anon.accent_color             = container_style->accent_color;
    anon.caret_color              = container_style->caret_color;

    /* `box->style` is a pointer that must outlive this call -- unlike `anon`
     * itself (a local), the synthesized style needs arena-backed storage,
     * same lifetime as every other tbox_style this Layout Tree build
     * produces. */
    tbox_style *anon_style = (tbox_style *)tbox_arena_alloc(arena, sizeof(tbox_style));
    *anon_style            = anon;

    /* Zero-initialized, same pattern as tbox_layout_build_element: parent/
     * first_child/last_child/next_sibling all start NULL, only overwritten
     * by the caller (tbox_layout_build_children) for the sibling links. */
    tbox_layout_box *box = (tbox_layout_box *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_box));
    box->node            = NULL;
    box->style           = anon_style;

    double height = tbox_layout_build_text_runs(arena, NULL, run_start, run_end, anon_style, styles, fonts, images, content_x, cursor_y, available_width, box, context);

    tbox_rect rect   = { content_x, cursor_y, available_width, height };
    box->content_box = rect;
    box->padding_box = rect;
    box->border_box  = rect;
    box->margin_box  = rect;

    return box;
}

/* true when `child` alone would START a new anonymous inline-box
 * sequence inside tbox_layout_build_children (see ARCHITECTURE.md's v14
 * "Algoritmo de tbox_layout_build_children"): non-whitespace-only TEXT, or
 * an ELEMENT whose resolved style is display:inline AND in flow (not
 * absolute/fixed). Whitespace-only TEXT and any other ELEMENT
 * (display:none/block, or out-of-flow) never trigger a sequence on their
 * own -- even though whitespace TEXT and display:none still EXTEND an
 * already-triggered sequence, see tbox_layout_inline_run_end below. */

/* scans forward from `run_start` (itself already known to be an
 * inline-run trigger, see tbox_layout_is_inline_run_trigger above) for the
 * first sibling that must NOT be consumed by the sequence -- the first
 * in-flow ELEMENT with display != inline, the first out-of-flow ELEMENT, or
 * the end of the sibling list (NULL, meaning the sequence runs to the last
 * child). TEXT of ANY content (including whitespace-only -- preserves
 * spacing between words/inline elements), ELEMENT display:none, and
 * COMMENT/DOCTYPE nodes are all transparent and extend the sequence without
 * ever starting or ending it on their own. */

bool tbox_layout_is_inline_run_trigger(tbox_arena *arena, const tbox_html_node *child, const tbox_style_table *styles) {
    if (child->type == TBOX_HTML_NODE_TEXT) {
        tbox_string_view collapsed = tbox_string_collapse_whitespace(arena, child->text.text);
        return collapsed.size > 0;
    }

    if (child->type == TBOX_HTML_NODE_ELEMENT) {
        if (tbox_layout_is_hidden_input(child))
            return false;
        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        bool is_out_of_flow           = (child_style->position == TBOX_STYLE_POSITION_ABSOLUTE || child_style->position == TBOX_STYLE_POSITION_FIXED);
        return (child_style->display == TBOX_STYLE_DISPLAY_INLINE || child_style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK || child_style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) && !is_out_of_flow;
    }

    return false;
}

const tbox_html_node *tbox_layout_inline_run_end(const tbox_html_node *run_start, const tbox_style_table *styles) {
    const tbox_html_node *node;
    for (node = run_start; node != NULL; node = node->next_sibling) {
        if (node->type != TBOX_HTML_NODE_ELEMENT) {
            continue; /* TEXT (any content), COMMENT, DOCTYPE: transparent */
        }

        if (tbox_layout_is_hidden_input(node))
            continue;

        const tbox_style *node_style = tbox_layout_style_or_default(styles, node);
        if (node_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue; /* transparent, same as display:none anywhere else */
        }

        bool is_out_of_flow = (node_style->position == TBOX_STYLE_POSITION_ABSOLUTE || node_style->position == TBOX_STYLE_POSITION_FIXED);
        if (is_out_of_flow) {
            break; /* terminates, NOT consumed -- built via the out-of-flow path instead */
        }

        if (node_style->display == TBOX_STYLE_DISPLAY_INLINE || node_style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK || node_style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX) {
            continue; /* in-flow inline or inline-block: extends the sequence */
        }

        break; /* in-flow block terminates, NOT consumed */
    }
    return node;
}
