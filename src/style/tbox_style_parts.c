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
    [TBOX_STYLE_PART_PICKER]               = "picker(select)",
    [TBOX_STYLE_PART_CHECKMARK]            = "checkmark",
    [TBOX_STYLE_PART_FILE_SELECTOR_BUTTON] = "file-selector-button",
    [TBOX_STYLE_PART_CLEAR_ICON]           = "clear-icon",
    [TBOX_STYLE_PART_STEP_CONTROL]         = "step-control",
    [TBOX_STYLE_PART_STEP_UP]              = "step-up",
    [TBOX_STYLE_PART_STEP_DOWN]            = "step-down",
    [TBOX_STYLE_PART_COLOR_SWATCH]         = "color-swatch",
    [TBOX_STYLE_PART_SELECTION]            = "selection",
    [TBOX_STYLE_PART_SCROLLBAR_TRACK]      = "-tbox-scrollbar-track",
    [TBOX_STYLE_PART_SCROLLBAR_THUMB]      = "-tbox-scrollbar-thumb",
    [TBOX_STYLE_PART_SCROLLBAR_THUMB_HOVER]  = "-tbox-scrollbar-thumb",
    [TBOX_STYLE_PART_SCROLLBAR_THUMB_ACTIVE] = "-tbox-scrollbar-thumb",
    [TBOX_STYLE_PART_RESIZER]                = "-tbox-resizer",
};

/* The user-action state each part is resolved in (tbox_css_part_state). */
static const unsigned tbox_style_part_states[TBOX_STYLE_PART_COUNT] = {
    [TBOX_STYLE_PART_SCROLLBAR_THUMB_HOVER]  = TBOX_CSS_PART_STATE_HOVER,
    [TBOX_STYLE_PART_SCROLLBAR_THUMB_ACTIVE] = TBOX_CSS_PART_STATE_HOVER | TBOX_CSS_PART_STATE_ACTIVE,
};

#define TBOX_STYLE_PART_BIT(part) (1u << (part))

static bool tbox_style_type_is(tbox_string_view type, const char *name) {
    return tbox_string_view_equal_ascii_ci(type, tbox_string_view_from_cstr(name));
}

/* The parts `node` has, one bit per tbox_style_part: only the ones the
 * control actually paints. An input without a type, or with a text-like
 * one, edits text and so has ::selection. */
static unsigned tbox_style_control_parts_of(const tbox_html_node *node) {
    tbox_string_view tag = node->element.tag_name;
    if (tbox_string_view_equal_cstr(tag, "progress") || tbox_string_view_equal_cstr(tag, "meter"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_TRACK) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SLIDER_FILL);
    if (tbox_string_view_equal_cstr(tag, "select"))
        return TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_PICKER_ICON) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_PICKER);
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

/* A box that may scroll (and so show a scrollbar) has the scrollbar parts
 * too, whatever element it is. */
static unsigned tbox_style_parts_of(const tbox_html_node *node, const tbox_style *style) {
    unsigned parts = tbox_style_control_parts_of(node);
    if (style->overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO && style->scrollbar_width != TBOX_STYLE_SCROLLBAR_WIDTH_NONE)
        parts |= TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SCROLLBAR_TRACK) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SCROLLBAR_THUMB) |
                 TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SCROLLBAR_THUMB_HOVER) | TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SCROLLBAR_THUMB_ACTIVE);
    /* A box the user may resize has the handle. */
    if (tbox_style_resize_allowed(style, tbox_string_view_equal_cstr(node->element.tag_name, "textarea")) != TBOX_STYLE_RESIZE_NONE)
        parts |= TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_RESIZER);
    return parts;
}

/* Borders per side (none = 0 width) and radii per corner: a single
 * `border-radius` length fills every corner. */
static void tbox_style_part_copy(const tbox_style *from, tbox_style_part_style *to) {
    to->width            = from->width;
    to->height           = from->height;
    to->min_height       = from->min_height;
    for (size_t i = 0; i < 4; i++)
        to->margin[i] = from->margin[i];
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
    unsigned parts = tbox_style_parts_of(node, style);
    const tbox_html_attribute *placeholder = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("placeholder"));
    if ((parts & TBOX_STYLE_PART_BIT(TBOX_STYLE_PART_SELECTION)) != 0 && placeholder != NULL && placeholder->value.size > 0) {
        tbox_css_computed_style computed = tbox_css_cascade_resolve_pseudo_element_in_viewport(sources, source_count, node,
            "placeholder", 0, viewport_width, viewport_height, reduced_motion,
            &(tbox_css_query_context){ style->query_container, style->custom_properties });
        tbox_style *text = tbox_arena_alloc(arena, sizeof(*text));
        if (text != NULL) {
            *text = tbox_style_resolve_in_viewport(NULL, style, &computed, viewport_width, viewport_height);
            if (tbox_css_computed_style_find(&computed, tbox_string_view_from_cstr("color")) == NULL)
                text->color = (tbox_css_rgba){117, 117, 117, 255};
            /* This is text ink, not an opacity group around the control. */
            text->color.a = text->visibility_hidden ? 0 : (uint8_t)(text->color.a * text->opacity + 0.5);
            text->opacity = 1.0;
            style->placeholder_style = text;
        }
        tbox_css_computed_style_destroy(&computed);
    }
    for (size_t part = 0; part < TBOX_STYLE_PART_COUNT; part++) {
        if ((parts & TBOX_STYLE_PART_BIT(part)) == 0)
            continue;
        tbox_style_part_style *out = (tbox_style_part_style *)tbox_arena_alloc(arena, sizeof(*out));
        if (out == NULL)
            continue;
        tbox_css_computed_style computed = tbox_css_cascade_resolve_pseudo_element_in_viewport(sources, source_count, node, tbox_style_part_names[part], tbox_style_part_states[part], viewport_width, viewport_height, reduced_motion, &(tbox_css_query_context){ style->query_container, style->custom_properties });
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

double tbox_style_scrollbar_size(const tbox_style *style, double base) {
    if (style == NULL || style->scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_NONE)
        return 0.0;
    double fallback = style->scrollbar_width == TBOX_STYLE_SCROLLBAR_WIDTH_THIN ? 6.0 : 10.0;
    const tbox_style_part_style *track = tbox_style_part_find(style, TBOX_STYLE_PART_SCROLLBAR_TRACK);
    if (track == NULL || track->width.kind == TBOX_STYLE_LENGTH_AUTO)
        return fallback;
    double width = tbox_style_length_resolve(track->width, base);
    return width >= 0.0 ? width : fallback;
}
