#include <tbox/layout.h>

#include <stddef.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"

/* v0's fallback style for an ELEMENT node missing from `styles` -- should
 * not normally happen (the Style layer resolves every ELEMENT node), but
 * matches tbox_style_resolve's own v0 initial values exactly (display
 * BLOCK, width/height AUTO, margin/padding 0px, color opaque black,
 * background transparent) rather than crashing on a defensive gap. */
static const tbox_style tbox_layout_default_style = {
    .display = TBOX_STYLE_DISPLAY_BLOCK,
    .width   = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .height  = { TBOX_STYLE_LENGTH_AUTO, 0.0 },
    .margin  = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .padding = {
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
        { TBOX_STYLE_LENGTH_PX, 0.0 },
    },
    .color            = { 0, 0, 0, 255 },
    .background_color = { 0, 0, 0, 0 },
};

static const tbox_style *tbox_layout_style_or_default(const tbox_style_table *styles, const tbox_html_node *node) {
    const tbox_style *style = tbox_style_table_find(styles, node);
    return style != NULL ? style : &tbox_layout_default_style;
}

/* The fixed tag-name list that gets text-box treatment in v0 (see
 * ARCHITECTURE.md's "Layout Tree" section) -- checked by plain byte-exact
 * comparison since tbox_html_node tag names are already lowercase ASCII. */
static bool tbox_layout_is_text_tag(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }

    static const char *const text_tags[] = { "h1", "h2", "h3", "h4", "h5", "h6", "p" };
    tbox_string_view tag_name            = node->element.tag_name;
    for (size_t i = 0; i < sizeof(text_tags) / sizeof(text_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, text_tags[i])) {
            return true;
        }
    }
    return false;
}

/* A margin/padding component ("length"). Percentages on margin and padding
 * -- on every side, including top/bottom -- always resolve against the
 * containing block's WIDTH per CSS2.1 10.2/8.3 (not its height); that's why
 * every call site below passes the same `percent_base` regardless of which
 * edge it's resolving. AUTO is treated as 0: v0 doesn't implement CSS2.1's
 * auto-margin resolution (used for centering an over-constrained box, see
 * 10.3.3), and padding never has a meaningful "auto" in real CSS anyway. */
static double tbox_layout_resolve_edge(tbox_style_length length, double percent_base) {
    switch (length.kind) {
    case TBOX_STYLE_LENGTH_PX:
        return length.value;
    case TBOX_STYLE_LENGTH_PERCENT:
        return length.value / 100.0 * percent_base;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        return 0.0;
    }
}

/* The containing block a box is laid out against: the parent's (or the
 * viewport's, for the root) content-box x/width, plus its content height --
 * `height_definite` says whether that height came from an explicit value
 * (PX, or PERCENT against an already-definite container) rather than from
 * AUTO/shrink-to-fit, which is what CSS2.1 10.5 needs to decide whether a
 * child's own `height: %` resolves or itself falls back to AUTO. There is
 * no `y` here: the vertical position within the containing block is instead
 * threaded through explicitly as a running cursor (see
 * tbox_layout_build_children), since siblings advance it independently of
 * anything about the containing block itself. */
typedef struct tbox_layout_containing_block {
    double x;
    double width;
    double height;
    bool height_definite;
} tbox_layout_containing_block;

static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, const tbox_font_face *font, tbox_layout_containing_block container, double cursor_y);

/* Walks `node`'s ELEMENT children (TEXT/COMMENT/DOCTYPE children never get
 * their own box in v0, see ARCHITECTURE.md), skipping any whose resolved
 * style->display == TBOX_STYLE_DISPLAY_NONE entirely -- no box, no
 * recursion into its subtree, no contribution to the height sum returned.
 * Stacks the rest vertically starting at `start_y` (no margin collapsing:
 * each box's own margin is applied independently -- see the module doc
 * comment), linking them onto `parent_box`'s first_child/last_child/
 * next_sibling. Returns the sum of every built child's margin_box.height,
 * for the parent's own AUTO/shrink-to-fit height. */
static double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, const tbox_font_face *font, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box) {
    double cursor_y           = start_y;
    double total_height       = 0.0;
    tbox_layout_box *previous = NULL;

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT) {
            continue;
        }

        const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
        if (child_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue;
        }

        tbox_layout_box *child_box = tbox_layout_build_element(arena, child, styles, font, children_container, cursor_y);
        child_box->parent          = parent_box;
        if (previous == NULL) {
            parent_box->first_child = child_box;
        } else {
            previous->next_sibling = child_box;
        }
        parent_box->last_child = child_box;
        previous               = child_box;

        cursor_y += child_box->margin_box.height;
        total_height += child_box->margin_box.height;
    }

    return total_height;
}

/* Builds and positions the box for one ELEMENT `node` (already known to not
 * be display:none -- the caller checks that before recursing, see
 * tbox_layout_build_children and tbox_layout_build) against `container`
 * (its parent's, or the viewport's, content box) with its margin_box's top
 * edge at `cursor_y`. */
static tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, const tbox_font_face *font, tbox_layout_containing_block container, double cursor_y) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);

    /* Zero-initialized: text/font/parent/first_child/last_child/next_sibling
     * all start at their empty/NULL v0 default and are only overwritten
     * where this function (or tbox_layout_build_children, for the sibling
     * links) has something to put there. */
    tbox_layout_box *box = (tbox_layout_box *)tbox_arena_alloc_zero(arena, sizeof(tbox_layout_box));
    box->node            = node;
    box->style           = style;

    double margin_top    = tbox_layout_resolve_edge(style->margin[0], container.width);
    double margin_right  = tbox_layout_resolve_edge(style->margin[1], container.width);
    double margin_bottom = tbox_layout_resolve_edge(style->margin[2], container.width);
    double margin_left   = tbox_layout_resolve_edge(style->margin[3], container.width);

    double padding_top    = tbox_layout_resolve_edge(style->padding[0], container.width);
    double padding_right  = tbox_layout_resolve_edge(style->padding[1], container.width);
    double padding_bottom = tbox_layout_resolve_edge(style->padding[2], container.width);
    double padding_left   = tbox_layout_resolve_edge(style->padding[3], container.width);

    /* Width: the same rule for every node, text-tag or not -- see
     * ARCHITECTURE.md's clarification that measured text never resizes the
     * box (D4). There is no border in v0 (always 0), so border/padding box
     * width differ from content width only by padding. */
    double content_width;
    switch (style->width.kind) {
    case TBOX_STYLE_LENGTH_PX:
        content_width = style->width.value;
        break;
    case TBOX_STYLE_LENGTH_PERCENT:
        content_width = style->width.value / 100.0 * container.width;
        break;
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        content_width = container.width - margin_left - margin_right - padding_left - padding_right;
        break;
    }

    double content_x = container.x + margin_left + padding_left;
    double content_y = cursor_y + margin_top + padding_top;

    double content_height;
    if (tbox_layout_is_text_tag(node)) {
        /* Text-tag leaf: no children even though the DOM node may have
         * element descendants (e.g. <b> inside a <p>) -- those are folded
         * into text_content already. Height is always the font's line
         * height, even for empty (post-collapse) text -- v0 deliberately
         * doesn't special-case that to 0 (see ARCHITECTURE.md). */
        box->text      = tbox_string_collapse_whitespace(arena, tbox_html_node_text_content(arena, node));
        box->font      = font;
        content_height = tbox_font_face_line_height(font);
    } else {
        /* Container node: recurse into ELEMENT children first (their
         * containing block is this node's own content box), then decide
         * this node's own height. PX is definite outright; PERCENT is
         * definite only when the containing block's own height is (CSS2.1
         * 10.5 -- percentage height against an auto-height container
         * computes to auto, the common v0 case); anything else (AUTO, or
         * that PERCENT fallback) is shrink-to-fit: the sum of every child's
         * margin_box.height, which requires children to be laid out first. */
        bool height_definite = false;
        switch (style->height.kind) {
        case TBOX_STYLE_LENGTH_PX:
            content_height  = style->height.value;
            height_definite = true;
            break;
        case TBOX_STYLE_LENGTH_PERCENT:
            if (container.height_definite) {
                content_height  = style->height.value / 100.0 * container.height;
                height_definite = true;
            } else {
                content_height = 0.0; /* placeholder; replaced by the children sum below */
            }
            break;
        case TBOX_STYLE_LENGTH_AUTO:
        default:
            content_height = 0.0; /* placeholder; replaced by the children sum below */
            break;
        }

        tbox_layout_containing_block children_container = {
            .x               = content_x,
            .width           = content_width,
            .height          = content_height,
            .height_definite = height_definite,
        };
        double children_total_height = tbox_layout_build_children(arena, node, styles, font, children_container, content_y, box);
        if (!height_definite) {
            content_height = children_total_height;
        }
    }

    box->content_box.x      = content_x;
    box->content_box.y      = content_y;
    box->content_box.width  = content_width;
    box->content_box.height = content_height;

    /* padding_box/border_box are identical in v0 (border is always 0
     * width); both are content_box grown back out by padding. */
    box->padding_box.x      = content_x - padding_left;
    box->padding_box.y      = content_y - padding_top;
    box->padding_box.width  = content_width + padding_left + padding_right;
    box->padding_box.height = content_height + padding_top + padding_bottom;
    box->border_box         = box->padding_box;

    /* margin_box is border_box grown back out by margin -- its x/y land
     * back on (container.x, cursor_y) exactly, per the geometry above. */
    box->margin_box.x      = box->border_box.x - margin_left;
    box->margin_box.y      = box->border_box.y - margin_top;
    box->margin_box.width  = box->border_box.width + margin_left + margin_right;
    box->margin_box.height = box->border_box.height + margin_top + margin_bottom;

    return box;
}

tbox_layout_box *tbox_layout_build(tbox_arena *arena, const tbox_html_node *root, const tbox_style_table *styles, const tbox_font_face *font, double viewport_width, double viewport_height) {
    if (root == NULL) {
        return NULL;
    }

    /* A DOCUMENT node is transparent -- it is never itself styled (the
     * Style layer skips it, same as TEXT/COMMENT/DOCTYPE) -- so build the
     * one box for its first ELEMENT child instead (typically <html> in a
     * full document). A caller may also pass an ELEMENT node directly, to
     * lay out one fragment in isolation. Anything else (a bare TEXT/
     * COMMENT/DOCTYPE root) has nothing to lay out. */
    const tbox_html_node *element = NULL;
    if (root->type == TBOX_HTML_NODE_DOCUMENT) {
        for (const tbox_html_node *child = root->first_child; child != NULL; child = child->next_sibling) {
            if (child->type == TBOX_HTML_NODE_ELEMENT) {
                element = child;
                break;
            }
        }
    } else if (root->type == TBOX_HTML_NODE_ELEMENT) {
        element = root;
    }

    if (element == NULL) {
        return NULL;
    }

    if (tbox_layout_style_or_default(styles, element)->display == TBOX_STYLE_DISPLAY_NONE) {
        return NULL;
    }

    tbox_layout_containing_block viewport = {
        .x               = 0.0,
        .width           = viewport_width,
        .height          = viewport_height,
        .height_definite = true, /* the viewport's height is always a concrete number */
    };
    return tbox_layout_build_element(arena, element, styles, font, viewport, 0.0);
}
