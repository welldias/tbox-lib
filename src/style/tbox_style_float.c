/* `float`, `clear` and `shape-outside` (CSS 2.1 section 9.5, CSS Shapes
 * 1). Layout places the floats (src/layout/tbox_layout_float.c); this
 * file only resolves the values. */

#include <string.h>

#include <tbox/style.h>

#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

static bool tbox_style_parse_float_side(tbox_string_view value, bool rtl, tbox_style_float *out) {
    if (tbox_style_is(value, "none"))
        *out = TBOX_STYLE_FLOAT_NONE;
    else if (tbox_style_is(value, "left"))
        *out = TBOX_STYLE_FLOAT_LEFT;
    else if (tbox_style_is(value, "right"))
        *out = TBOX_STYLE_FLOAT_RIGHT;
    else if (tbox_style_is(value, "inline-start"))
        *out = rtl ? TBOX_STYLE_FLOAT_RIGHT : TBOX_STYLE_FLOAT_LEFT;
    else if (tbox_style_is(value, "inline-end"))
        *out = rtl ? TBOX_STYLE_FLOAT_LEFT : TBOX_STYLE_FLOAT_RIGHT;
    else
        return false;
    return true;
}

static bool tbox_style_parse_clear(tbox_string_view value, bool rtl, tbox_style_clear *out) {
    if (tbox_style_is(value, "none"))
        *out = TBOX_STYLE_CLEAR_NONE;
    else if (tbox_style_is(value, "left"))
        *out = TBOX_STYLE_CLEAR_LEFT;
    else if (tbox_style_is(value, "right"))
        *out = TBOX_STYLE_CLEAR_RIGHT;
    else if (tbox_style_is(value, "both"))
        *out = TBOX_STYLE_CLEAR_BOTH;
    else if (tbox_style_is(value, "inline-start"))
        *out = rtl ? TBOX_STYLE_CLEAR_RIGHT : TBOX_STYLE_CLEAR_LEFT;
    else if (tbox_style_is(value, "inline-end"))
        *out = rtl ? TBOX_STYLE_CLEAR_LEFT : TBOX_STYLE_CLEAR_RIGHT;
    else
        return false;
    return true;
}

static bool tbox_style_parse_shape_box(tbox_string_view token, tbox_style_shape_box *out) {
    static const struct {
        const char *name;
        tbox_style_shape_box box;
    } boxes[] = {
        { "margin-box",  TBOX_STYLE_SHAPE_BOX_MARGIN  },
        { "border-box",  TBOX_STYLE_SHAPE_BOX_BORDER  },
        { "padding-box", TBOX_STYLE_SHAPE_BOX_PADDING },
        { "content-box", TBOX_STYLE_SHAPE_BOX_CONTENT },
    };
    for (size_t i = 0; i < sizeof(boxes) / sizeof(boxes[0]); i++)
        if (tbox_style_is(token, boxes[i].name)) {
            *out = boxes[i].box;
            return true;
        }
    return false;
}

/* none | <basic-shape> || <shape-box> | <image>. Only url() images: a
 * gradient as a shape is not supported (the declaration is ignored). */
static bool tbox_style_parse_shape_outside(tbox_string_view raw, const tbox_style_units *units, tbox_style_shape_outside *out) {
    tbox_style_shape_outside shape;
    memset(&shape, 0, sizeof(shape));
    raw = tbox_string_view_trim_ascii_space(raw);
    if (tbox_style_is(raw, "none")) {
        *out = shape;
        return true;
    }
    if (tbox_style_parse_url(raw, shape.image, sizeof(shape.image))) {
        shape.kind = TBOX_STYLE_SHAPE_OUTSIDE_IMAGE;
        *out       = shape;
        return true;
    }
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(raw, tokens, 3);
    if (count == 0 || count > 2)
        return false;
    bool have_box = false, have_shape = false;
    for (size_t i = 0; i < count; i++) {
        if (!have_box && tbox_style_parse_shape_box(tokens[i], &shape.box)) {
            have_box = true;
        } else if (!have_shape && tbox_style_parse_clip_path(tokens[i], units, &shape.shape) && shape.shape.kind != TBOX_STYLE_CLIP_PATH_NONE) {
            have_shape = true;
        } else {
            return false;
        }
    }
    shape.kind = have_shape ? TBOX_STYLE_SHAPE_OUTSIDE_SHAPE : TBOX_STYLE_SHAPE_OUTSIDE_BOX;
    *out       = shape;
    return true;
}

void tbox_style_resolve_float(const tbox_css_computed_style *computed, const tbox_style *parent_style, const tbox_style_units *units, tbox_style *style) {
    /* Logical values follow the containing block's direction. */
    bool rtl = parent_style != NULL ? parent_style->direction_rtl : style->direction_rtl;
    style->float_side = TBOX_STYLE_FLOAT_NONE;
    style->clear      = TBOX_STYLE_CLEAR_NONE;
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("float"));
    if (decl != NULL)
        tbox_style_parse_float_side(tbox_string_view_trim_ascii_space(decl->value), rtl, &style->float_side);
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("clear"));
    if (decl != NULL)
        tbox_style_parse_clear(tbox_string_view_trim_ascii_space(decl->value), rtl, &style->clear);

    memset(&style->shape_outside, 0, sizeof(style->shape_outside));
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("shape-outside"));
    if (decl != NULL)
        tbox_style_parse_shape_outside(decl->value, units, &style->shape_outside);

    style->shape_margin = (tbox_style_length){ TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 };
    decl                = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("shape-margin"));
    tbox_style_length margin;
    if (decl != NULL && tbox_style_parse_spacing_length(decl->value, units, &margin) && margin.kind != TBOX_STYLE_LENGTH_AUTO && margin.value >= 0.0)
        style->shape_margin = margin;

    style->shape_image_threshold = 0.0;
    decl                         = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("shape-image-threshold"));
    double threshold;
    if (decl != NULL && tbox_style_parse_number(tbox_string_view_trim_ascii_space(decl->value), &threshold))
        style->shape_image_threshold = threshold < 0.0 ? 0.0 : threshold > 1.0 ? 1.0 : threshold;
}

void tbox_style_finish_float(tbox_style *style, const tbox_style *parent_style) {
    /* `float` does not apply to absolutely positioned boxes nor to flex
     * flex or grid items (CSS 2.1 section 9.7, Flexbox section 4, Grid
     * section 6). */
    if (style->position == TBOX_STYLE_POSITION_ABSOLUTE || style->position == TBOX_STYLE_POSITION_FIXED)
        style->float_side = TBOX_STYLE_FLOAT_NONE;
    if (parent_style != NULL && (parent_style->display == TBOX_STYLE_DISPLAY_FLEX || parent_style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX ||
                                 parent_style->display == TBOX_STYLE_DISPLAY_GRID || parent_style->display == TBOX_STYLE_DISPLAY_INLINE_GRID))
        style->float_side = TBOX_STYLE_FLOAT_NONE;
    if (style->float_side == TBOX_STYLE_FLOAT_NONE)
        return;
    /* Blockification (CSS Display 3 section 2.7). */
    if (style->display == TBOX_STYLE_DISPLAY_INLINE || style->display == TBOX_STYLE_DISPLAY_INLINE_BLOCK)
        style->display = TBOX_STYLE_DISPLAY_BLOCK;
    else if (style->display == TBOX_STYLE_DISPLAY_INLINE_FLEX)
        style->display = TBOX_STYLE_DISPLAY_FLEX;
    else if (style->display == TBOX_STYLE_DISPLAY_INLINE_GRID)
        style->display = TBOX_STYLE_DISPLAY_GRID;
}
