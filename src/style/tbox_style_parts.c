#include <tbox/style.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

/* Pseudo-element names, indexed by tbox_style_part. */
static const char *const tbox_style_part_names[TBOX_STYLE_PART_COUNT] = {
    [TBOX_STYLE_PART_SLIDER_TRACK]         = "slider-track",
    [TBOX_STYLE_PART_SLIDER_FILL]          = "slider-fill",
    [TBOX_STYLE_PART_SLIDER_THUMB]         = "slider-thumb",
    [TBOX_STYLE_PART_PICKER_ICON]          = "picker-icon",
    [TBOX_STYLE_PART_CHECKMARK]            = "checkmark",
    [TBOX_STYLE_PART_FILE_SELECTOR_BUTTON] = "file-selector-button",
    [TBOX_STYLE_PART_CLEAR_ICON]           = "clear-icon",
    [TBOX_STYLE_PART_STEP_CONTROL]         = "step-control",
    [TBOX_STYLE_PART_STEP_UP]              = "step-up",
    [TBOX_STYLE_PART_STEP_DOWN]            = "step-down",
    [TBOX_STYLE_PART_COLOR_SWATCH]         = "color-swatch",
    [TBOX_STYLE_PART_SELECTION]            = "selection",
};

#define TBOX_STYLE_PART_BIT(part) (1u << (part))

static bool tbox_style_type_is(tbox_string_view type, const char *name) {
    return tbox_string_view_equal_ascii_ci(type, tbox_string_view_from_cstr(name));
}

/* The parts `node` has, one bit per tbox_style_part: only the ones the
 * control actually paints. An input without a type, or with a text-like
 * one, edits text and so has ::selection. */
static unsigned tbox_style_parts_of(const tbox_html_node *node) {
    tbox_string_view tag = node->element.tag_name;
    if (tbox_string_view_equal_cstr(tag, "progress") || tbox_string_view_equal_cstr(tag, "meter"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_TRACK) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_FILL);
    if (tbox_string_view_equal_cstr(tag, "select"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_PICKER_ICON);
    if (tbox_string_view_equal_cstr(tag, "textarea"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION);
    if (!tbox_string_view_equal_cstr(tag, "input"))
        return 0;
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("type"));
    if (attribute == NULL)
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION);
    tbox_string_view type = attribute->value;
    if (tbox_style_type_is(type, "range"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_TRACK) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_FILL) |
               TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_THUMB);
    if (tbox_style_type_is(type, "checkbox") || tbox_style_type_is(type, "radio"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_CHECKMARK);
    if (tbox_style_type_is(type, "file"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_FILE_SELECTOR_BUTTON);
    if (tbox_style_type_is(type, "color"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_COLOR_SWATCH);
    if (tbox_style_type_is(type, "search"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_CLEAR_ICON) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION);
    if (tbox_style_type_is(type, "number"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_STEP_CONTROL) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_STEP_UP) |
               TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_STEP_DOWN) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION);
    if (tbox_style_type_is(type, "text") || tbox_style_type_is(type, "email") || tbox_style_type_is(type, "password") ||
        tbox_style_type_is(type, "tel") || tbox_style_type_is(type, "url"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION);
    return 0;
}

/* Borders per side (none = 0 width) and radii per corner: a single
 * `border-radius` length fills every corner. */
static void tbox_style_part_copy(const tbox_style *from, tbox_style_part_style *to) {
    to->width            = from->width;
    to->height           = from->height;
    to->color            = from->color;
    to->background_color = from->background_color;
    bool separate        = false;
    for (size_t i = 0; i < 4; i++) {
        to->border_widths[i] = tbox_style_border_side_width(from, i);
        to->border_colors[i] = tbox_style_border_side_color(from, i);
        if (from->border_radius_corners[i] > 0.0 || from->border_radius_percent[i] > 0.0)
            separate = true;
    }
    for (size_t i = 0; i < 4; i++) {
        to->border_radii[i]         = separate ? from->border_radius_corners[i] : from->border_radius;
        to->border_radii_percent[i] = separate ? from->border_radius_percent[i] : 0.0;
    }
}

void tbox_style_resolve_parts(const tbox_html_node *node, tbox_style *style, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height, bool reduced_motion, tbox_arena *arena) {
    unsigned parts = tbox_style_parts_of(node);
    for (size_t part = 0; part < TBOX_STYLE_PART_COUNT; part++) {
        if ((parts & TBOX_STYLE_PART_BIT(part)) == 0)
            continue;
        tbox_style_part_style *out = (tbox_style_part_style *)tbox_arena_alloc(arena, sizeof(*out));
        if (out == NULL)
            continue;
        tbox_css_computed_style computed = tbox_css_cascade_resolve_pseudo_element_in_viewport(sources, source_count, node, tbox_style_part_names[part], viewport_width, viewport_height, reduced_motion);
        tbox_style resolved              = tbox_style_resolve_in_viewport(NULL, style, &computed, viewport_width, viewport_height);
        tbox_style_part_copy(&resolved, out);
        out->color_declared      = tbox_css_computed_style_find(&computed, tbox_string_view_from_cstr("color")) != NULL;
        out->background_declared = tbox_css_computed_style_find(&computed, tbox_string_view_from_cstr("background-color")) != NULL ||
                                   tbox_css_computed_style_find(&computed, tbox_string_view_from_cstr("background")) != NULL;
        tbox_css_computed_style_destroy(&computed);
        style->parts[part] = out;
    }
}

const tbox_style_part_style *tbox_style_part_find(const tbox_style *style, tbox_style_part part) {
    if (style == NULL || (unsigned)part >= TBOX_STYLE_PART_COUNT)
        return NULL;
    return style->parts[part];
}
