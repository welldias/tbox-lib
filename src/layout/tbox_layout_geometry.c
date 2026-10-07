#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* ---- Block formatting context (unchanged in spirit since v0, now
 * threading a tbox_font_face_cache instead of a single tbox_font_face) ---- */

/* A margin/padding component ("length"). Percentages on margin and padding
 * -- on every side, including top/bottom -- always resolve against the
 * containing block's WIDTH per CSS2.1 10.2/8.3 (not its height); that's why
 * every call site below passes the same `percent_base` regardless of which
 * edge it's resolving. AUTO is treated as 0: v0 doesn't implement CSS2.1's
 * auto-margin resolution (used for centering an over-constrained box, see
 * 10.3.3), and padding never has a meaningful "auto" in real CSS anyway. */
double tbox_layout_resolve_edge(tbox_style_length length, double percent_base) {
    switch (length.kind) {
    case TBOX_STYLE_LENGTH_PX:
        return length.value;
    case TBOX_STYLE_LENGTH_PERCENT:
        return tbox_style_length_resolve(length, percent_base);
    case TBOX_STYLE_LENGTH_AUTO:
    default:
        return 0.0;
    }
}

/* Constraints apply to content width; a minimum wins if it exceeds the
 * maximum. Percentages use the containing block's width. */
double tbox_layout_constrain_width(const tbox_style *style, double width, double base, double edges) {
    if (style->max_width.kind != TBOX_STYLE_LENGTH_AUTO) {
        double maximum = tbox_layout_resolve_edge(style->max_width, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            maximum -= edges;
        if (maximum < 0.0)
            maximum = 0.0;
        if (width > maximum)
            width = maximum;
    }
    if (style->min_width.kind != TBOX_STYLE_LENGTH_AUTO) {
        double minimum = tbox_layout_resolve_edge(style->min_width, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            minimum -= edges;
        if (minimum < 0.0)
            minimum = 0.0;
        if (width < minimum)
            width = minimum;
    }
    return width;
}

/* Height constraints use the containing block's height only when definite.
 * CSS min-height wins if it exceeds max-height, as with width constraints. */
double tbox_layout_constrain_height(const tbox_style *style, double height, double base, bool base_definite, double edges) {
    if (style->max_height.kind == TBOX_STYLE_LENGTH_PX || (style->max_height.kind == TBOX_STYLE_LENGTH_PERCENT && base_definite)) {
        double maximum = tbox_layout_resolve_edge(style->max_height, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            maximum -= edges;
        if (maximum < 0.0)
            maximum = 0.0;
        if (height > maximum)
            height = maximum;
    }
    if (style->min_height.kind == TBOX_STYLE_LENGTH_PX || (style->min_height.kind == TBOX_STYLE_LENGTH_PERCENT && base_definite)) {
        double minimum = tbox_layout_resolve_edge(style->min_height, base);
        if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX)
            minimum -= edges;
        if (minimum < 0.0)
            minimum = 0.0;
        if (height < minimum)
            height = minimum;
    }
    return height;
}

/* resolves one (primary, opposite) pair of `position: relative`
 * offsets per CSS2.1 9.4.3 -- a non-AUTO primary side wins outright; else a
 * non-AUTO opposite side wins, negated (moving by `-opposite` is exactly
 * equivalent to moving by `+primary` when only one side is given); else 0
 * (both AUTO, the common case: `position: relative` with no offset at all
 * moves nothing). Called once for (left, right) against the container's
 * width (always definite) and once for (top, bottom) against its height,
 * where `percent_base_definite` guards against resolving a `%` against an
 * indefinite (AUTO-height) container -- same guard `height: %` already uses
 * in tbox_layout_build_element below -- falling back to 0 instead of
 * producing a bogus/NaN offset. A PX side is never affected by
 * `percent_base_definite` since it doesn't depend on `percent_base` at all. */
double tbox_layout_resolve_offset(tbox_style_length primary, tbox_style_length opposite, double percent_base, bool percent_base_definite) {
    if (primary.kind != TBOX_STYLE_LENGTH_AUTO) {
        if (primary.kind == TBOX_STYLE_LENGTH_PERCENT && !percent_base_definite) {
            return 0.0;
        }
        return tbox_layout_resolve_edge(primary, percent_base);
    }

    if (opposite.kind != TBOX_STYLE_LENGTH_AUTO) {
        if (opposite.kind == TBOX_STYLE_LENGTH_PERCENT && !percent_base_definite) {
            return 0.0;
        }
        return -tbox_layout_resolve_edge(opposite, percent_base);
    }

    return 0.0;
}

/* resolves an `absolute`/`fixed` box's MARGIN BOX origin on one axis
 * -- a POSITION against the containing block's origin/size, not a delta like
 * tbox_layout_resolve_offset (v4, for `position: relative`) computes. `primary`
 * is `left`/`top`; `opposite` is `right`/`bottom`. Per CSS2.1 10.3.7/10.6.4: a
 * non-AUTO primary side wins outright (`container_origin + resolved(primary)`);
 * else a non-AUTO opposite side positions the box's FAR edge
 * (`container_origin + container_size - margin_box_size - resolved(opposite)`);
 * else (both AUTO) the box falls back to the containing block's own origin --
 * a deliberate simplification (CSS defines this as the element's "static
 * position", which this project does not compute -- see ARCHITECTURE.md "v5
 * -- Escopo deliberadamente contido" and "Layout Tree -- geometria de
 * absolute/fixed" for the vertical axis's additional circular top/bottom/
 * height case, which also lands on this same fallback). `margin_box_size`
 * must already be known by the caller before this is called -- for the
 * horizontal axis that means content_width/border_box.width/margin_box.width
 * are resolved first (never circular here); for the vertical axis, see the
 * caller in tbox_layout_build_element for how the circular case is avoided. */
double tbox_layout_resolve_absolute_edge(tbox_style_length primary, tbox_style_length opposite, double container_origin, double container_size, double margin_box_size) {
    if (primary.kind != TBOX_STYLE_LENGTH_AUTO) {
        return container_origin + tbox_layout_resolve_edge(primary, container_size);
    }

    if (opposite.kind != TBOX_STYLE_LENGTH_AUTO) {
        return container_origin + container_size - margin_box_size - tbox_layout_resolve_edge(opposite, container_size);
    }

    return container_origin;
}
