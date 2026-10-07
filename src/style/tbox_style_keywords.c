#include "style/tbox_style_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"

/* One tbox_style member (or array element) a property sets. */
typedef struct tbox_style_field {
    size_t offset, size;
} tbox_style_field;

#define TBOX_STYLE_FIELD(member) { offsetof(tbox_style, member), sizeof(((tbox_style *)0)->member) }
#define TBOX_STYLE_MAX_FIELDS 12

typedef struct tbox_style_property_fields {
    const char *name;
    bool inherited;
    tbox_style_field fields[TBOX_STYLE_MAX_FIELDS]; /* ends at the first zero size */
} tbox_style_property_fields;

/* Every property with a fixed set of fields. Box-side families (margin,
 * padding, inset, border) are matched by tbox_style_side_fields instead. */
static const tbox_style_property_fields tbox_style_properties[] = {
    { "display",                    false, { TBOX_STYLE_FIELD(display), TBOX_STYLE_FIELD(display_list_item) } },
    { "overflow",                   false, { TBOX_STYLE_FIELD(overflow_x), TBOX_STYLE_FIELD(overflow_y) } },
    { "overflow-x",                 false, { TBOX_STYLE_FIELD(overflow_x) } },
    { "overflow-y",                 false, { TBOX_STYLE_FIELD(overflow_y) } },
    { "box-sizing",                 false, { TBOX_STYLE_FIELD(box_sizing) } },
    { "visibility",                 true,  { TBOX_STYLE_FIELD(visibility_hidden), TBOX_STYLE_FIELD(visibility_collapse) } },
    { "text-overflow",              false, { TBOX_STYLE_FIELD(text_overflow), TBOX_STYLE_FIELD(text_overflow_string) } },
    { "white-space",                true,  { TBOX_STYLE_FIELD(white_space) } },
    { "white-space-collapse",       true,  { TBOX_STYLE_FIELD(white_space) } },
    { "text-wrap-mode",             true,  { TBOX_STYLE_FIELD(white_space) } },
    { "text-wrap",                  true,  { TBOX_STYLE_FIELD(white_space), TBOX_STYLE_FIELD(text_wrap_balance) } },
    { "tab-size",                   true,  { TBOX_STYLE_FIELD(tab_size) } },
    { "overflow-wrap",              true,  { TBOX_STYLE_FIELD(overflow_wrap_break_word), TBOX_STYLE_FIELD(overflow_wrap_anywhere) } },
    { "word-wrap",                  true,  { TBOX_STYLE_FIELD(overflow_wrap_break_word), TBOX_STYLE_FIELD(overflow_wrap_anywhere) } },
    { "word-break",                 true,  { TBOX_STYLE_FIELD(word_break_all), TBOX_STYLE_FIELD(word_break_keep_all) } },
    { "text-transform",             true,  { TBOX_STYLE_FIELD(text_transform) } },
    { "hyphens",                    true,  { TBOX_STYLE_FIELD(hyphens_none) } },
    { "list-style",                 true,  { TBOX_STYLE_FIELD(list_style_type), TBOX_STYLE_FIELD(list_style_inside), TBOX_STYLE_FIELD(list_style_image) } },
    { "list-style-image",           true,  { TBOX_STYLE_FIELD(list_style_image) } },
    { "list-style-type",            true,  { TBOX_STYLE_FIELD(list_style_type) } },
    { "list-style-position",        true,  { TBOX_STYLE_FIELD(list_style_inside) } },
    { "pointer-events",             true,  { TBOX_STYLE_FIELD(pointer_events_none) } },
    { "user-select",                false, { TBOX_STYLE_FIELD(user_select) } },
    { "cursor",                     true,  { TBOX_STYLE_FIELD(cursor) } },
    { "width",                      false, { TBOX_STYLE_FIELD(width), TBOX_STYLE_FIELD(width_keyword) } },
    { "inline-size",                false, { TBOX_STYLE_FIELD(width), TBOX_STYLE_FIELD(width_keyword) } },
    { "height",                     false, { TBOX_STYLE_FIELD(height), TBOX_STYLE_FIELD(height_keyword) } },
    { "block-size",                 false, { TBOX_STYLE_FIELD(height), TBOX_STYLE_FIELD(height_keyword) } },
    { "min-width",                  false, { TBOX_STYLE_FIELD(min_width) } },
    { "min-inline-size",            false, { TBOX_STYLE_FIELD(min_width) } },
    { "max-width",                  false, { TBOX_STYLE_FIELD(max_width) } },
    { "max-inline-size",            false, { TBOX_STYLE_FIELD(max_width) } },
    { "min-height",                 false, { TBOX_STYLE_FIELD(min_height) } },
    { "min-block-size",             false, { TBOX_STYLE_FIELD(min_height) } },
    { "max-height",                 false, { TBOX_STYLE_FIELD(max_height) } },
    { "max-block-size",             false, { TBOX_STYLE_FIELD(max_height) } },
    { "aspect-ratio",               false, { TBOX_STYLE_FIELD(aspect_ratio) } },
    { "text-indent",                true,  { TBOX_STYLE_FIELD(text_indent), TBOX_STYLE_FIELD(text_indent_hanging), TBOX_STYLE_FIELD(text_indent_each_line) } },
    { "word-spacing",               true,  { TBOX_STYLE_FIELD(word_spacing) } },
    { "letter-spacing",             true,  { TBOX_STYLE_FIELD(letter_spacing) } },
    { "line-height",                true,  { TBOX_STYLE_FIELD(line_height_kind), TBOX_STYLE_FIELD(line_height_value) } },
    { "color",                      true,  { TBOX_STYLE_FIELD(color) } },
    { "background-color",           false, { TBOX_STYLE_FIELD(background_color) } },
    { "background",                 false, { TBOX_STYLE_FIELD(background_color), TBOX_STYLE_FIELD(background_image), TBOX_STYLE_FIELD(background_gradient), TBOX_STYLE_FIELD(background_size), TBOX_STYLE_FIELD(background_size_kind), TBOX_STYLE_FIELD(background_position), TBOX_STYLE_FIELD(background_repeat_x), TBOX_STYLE_FIELD(background_repeat_y), TBOX_STYLE_FIELD(background_clip), TBOX_STYLE_FIELD(background_origin), TBOX_STYLE_FIELD(background_layers), TBOX_STYLE_FIELD(background_layer_count) } },
    { "background-image",           false, { TBOX_STYLE_FIELD(background_image), TBOX_STYLE_FIELD(background_gradient), TBOX_STYLE_FIELD(background_layers), TBOX_STYLE_FIELD(background_layer_count) } },
    { "background-size",            false, { TBOX_STYLE_FIELD(background_size), TBOX_STYLE_FIELD(background_size_kind) } },
    { "background-position",        false, { TBOX_STYLE_FIELD(background_position) } },
    { "background-position-x",      false, { TBOX_STYLE_FIELD(background_position[0]) } },
    { "background-position-y",      false, { TBOX_STYLE_FIELD(background_position[1]) } },
    { "background-repeat",          false, { TBOX_STYLE_FIELD(background_repeat_x), TBOX_STYLE_FIELD(background_repeat_y) } },
    { "background-clip",            false, { TBOX_STYLE_FIELD(background_clip) } },
    { "background-origin",          false, { TBOX_STYLE_FIELD(background_origin) } },
    { "font-size",                  true,  { TBOX_STYLE_FIELD(font_size) } },
    { "font-weight",                true,  { TBOX_STYLE_FIELD(font_weight), TBOX_STYLE_FIELD(font_weight_bold) } },
    { "font-family",                true,  { TBOX_STYLE_FIELD(font_family) } },
    { "font-style",                 true,  { TBOX_STYLE_FIELD(font_italic) } },
    { "font-stretch",               true,  { TBOX_STYLE_FIELD(font_stretch) } },
    { "font-variant",               true,  { TBOX_STYLE_FIELD(font_small_caps) } },
    { "font-variant-caps",          true,  { TBOX_STYLE_FIELD(font_small_caps) } },
    { "font-kerning",               true,  { TBOX_STYLE_FIELD(font_kerning_none) } },
    { "font",                       true,  { TBOX_STYLE_FIELD(font_size), TBOX_STYLE_FIELD(font_weight), TBOX_STYLE_FIELD(font_weight_bold), TBOX_STYLE_FIELD(font_family), TBOX_STYLE_FIELD(font_italic), TBOX_STYLE_FIELD(font_stretch), TBOX_STYLE_FIELD(font_small_caps), TBOX_STYLE_FIELD(line_height_kind), TBOX_STYLE_FIELD(line_height_value) } },
    { "outline",                    false, { TBOX_STYLE_FIELD(outline_width), TBOX_STYLE_FIELD(outline_style), TBOX_STYLE_FIELD(outline_color) } },
    { "outline-width",              false, { TBOX_STYLE_FIELD(outline_width) } },
    { "outline-style",              false, { TBOX_STYLE_FIELD(outline_style) } },
    { "outline-color",              false, { TBOX_STYLE_FIELD(outline_color) } },
    { "outline-offset",             false, { TBOX_STYLE_FIELD(outline_offset) } },
    { "position",                   false, { TBOX_STYLE_FIELD(position) } },
    { "z-index",                    false, { TBOX_STYLE_FIELD(z_index), TBOX_STYLE_FIELD(z_index_auto) } },
    { "text-align",                 true,  { TBOX_STYLE_FIELD(text_align) } },
    { "text-align-last",            true,  { TBOX_STYLE_FIELD(text_align_last) } },
    { "text-decoration",            false, { TBOX_STYLE_FIELD(text_decoration), TBOX_STYLE_FIELD(text_decoration_lines), TBOX_STYLE_FIELD(text_decoration_style), TBOX_STYLE_FIELD(text_decoration_color), TBOX_STYLE_FIELD(text_decoration_thickness) } },
    { "text-decoration-line",       false, { TBOX_STYLE_FIELD(text_decoration), TBOX_STYLE_FIELD(text_decoration_lines) } },
    { "text-decoration-style",      false, { TBOX_STYLE_FIELD(text_decoration_style) } },
    { "text-decoration-color",      false, { TBOX_STYLE_FIELD(text_decoration_color) } },
    { "text-decoration-thickness",  false, { TBOX_STYLE_FIELD(text_decoration_thickness) } },
    { "text-underline-offset",      true,  { TBOX_STYLE_FIELD(text_underline_offset) } },
    { "text-underline-position",    true,  { TBOX_STYLE_FIELD(text_underline_position_under) } },
    { "vertical-align",             false, { TBOX_STYLE_FIELD(vertical_align), TBOX_STYLE_FIELD(vertical_align_length) } },
    { "caption-side",               true,  { TBOX_STYLE_FIELD(caption_side) } },
    { "border-collapse",            true,  { TBOX_STYLE_FIELD(border_collapse) } },
    { "border-spacing",             true,  { TBOX_STYLE_FIELD(border_spacing_x), TBOX_STYLE_FIELD(border_spacing_y) } },
    { "empty-cells",                true,  { TBOX_STYLE_FIELD(empty_cells_hide) } },
    { "table-layout",               false, { TBOX_STYLE_FIELD(table_layout_fixed) } },
    { "border-radius",              false, { TBOX_STYLE_FIELD(border_radius_corners), TBOX_STYLE_FIELD(border_radius_percent), TBOX_STYLE_FIELD(border_radius_vertical), TBOX_STYLE_FIELD(border_radius_vertical_percent) } },
    { "border-top-left-radius",     false, { TBOX_STYLE_FIELD(border_radius_corners[0]), TBOX_STYLE_FIELD(border_radius_percent[0]), TBOX_STYLE_FIELD(border_radius_vertical[0]), TBOX_STYLE_FIELD(border_radius_vertical_percent[0]) } },
    { "border-top-right-radius",    false, { TBOX_STYLE_FIELD(border_radius_corners[1]), TBOX_STYLE_FIELD(border_radius_percent[1]), TBOX_STYLE_FIELD(border_radius_vertical[1]), TBOX_STYLE_FIELD(border_radius_vertical_percent[1]) } },
    { "border-bottom-right-radius", false, { TBOX_STYLE_FIELD(border_radius_corners[2]), TBOX_STYLE_FIELD(border_radius_percent[2]), TBOX_STYLE_FIELD(border_radius_vertical[2]), TBOX_STYLE_FIELD(border_radius_vertical_percent[2]) } },
    { "border-bottom-left-radius",  false, { TBOX_STYLE_FIELD(border_radius_corners[3]), TBOX_STYLE_FIELD(border_radius_percent[3]), TBOX_STYLE_FIELD(border_radius_vertical[3]), TBOX_STYLE_FIELD(border_radius_vertical_percent[3]) } },
    { "box-shadow",                 false, { TBOX_STYLE_FIELD(box_shadow_offset_x), TBOX_STYLE_FIELD(box_shadow_offset_y), TBOX_STYLE_FIELD(box_shadow_blur), TBOX_STYLE_FIELD(box_shadow_spread), TBOX_STYLE_FIELD(box_shadow_color), TBOX_STYLE_FIELD(box_shadow_inset), TBOX_STYLE_FIELD(box_shadows), TBOX_STYLE_FIELD(box_shadow_count) } },
    { "text-shadow",                true,  { TBOX_STYLE_FIELD(text_shadow_offset_x), TBOX_STYLE_FIELD(text_shadow_offset_y), TBOX_STYLE_FIELD(text_shadow_blur), TBOX_STYLE_FIELD(text_shadow_color), TBOX_STYLE_FIELD(text_shadows), TBOX_STYLE_FIELD(text_shadow_count) } },
    { "opacity",                    false, { TBOX_STYLE_FIELD(opacity) } },
    { "filter",                     false, { TBOX_STYLE_FIELD(has_filter), TBOX_STYLE_FIELD(filter_matrix) } },
    { "clip-path",                  false, { TBOX_STYLE_FIELD(clip_path) } },
    { "transform",                  false, { TBOX_STYLE_FIELD(translate_x), TBOX_STYLE_FIELD(translate_y) } },
    { "translate",                  false, { TBOX_STYLE_FIELD(translate_x), TBOX_STYLE_FIELD(translate_y) } },
    { "object-fit",                 false, { TBOX_STYLE_FIELD(object_fit) } },
    { "object-position",            false, { TBOX_STYLE_FIELD(object_position) } },
    { "image-rendering",            true,  { TBOX_STYLE_FIELD(image_rendering_pixelated) } },
    { "accent-color",               true,  { TBOX_STYLE_FIELD(accent_color) } },
    { "caret-color",                true,  { TBOX_STYLE_FIELD(caret_color) } },
    { "scrollbar-color",            true,  { TBOX_STYLE_FIELD(scrollbar_thumb_color), TBOX_STYLE_FIELD(scrollbar_track_color) } },
    { "scrollbar-width",            false, { TBOX_STYLE_FIELD(scrollbar_width) } },
    { "line-clamp",                 false, { TBOX_STYLE_FIELD(line_clamp) } },
    { "-webkit-line-clamp",         false, { TBOX_STYLE_FIELD(line_clamp) } },
    { "flex-direction",             false, { TBOX_STYLE_FIELD(flex_direction) } },
    { "flex-wrap",                  false, { TBOX_STYLE_FIELD(flex_wrap) } },
    { "flex-flow",                  false, { TBOX_STYLE_FIELD(flex_direction), TBOX_STYLE_FIELD(flex_wrap) } },
    { "justify-content",            false, { TBOX_STYLE_FIELD(justify_content) } },
    { "align-content",              false, { TBOX_STYLE_FIELD(align_content) } },
    { "align-items",                false, { TBOX_STYLE_FIELD(align_items) } },
    { "align-self",                 false, { TBOX_STYLE_FIELD(align_self) } },
    { "place-content",              false, { TBOX_STYLE_FIELD(align_content), TBOX_STYLE_FIELD(justify_content) } },
    { "place-items",                false, { TBOX_STYLE_FIELD(align_items) } },
    { "place-self",                 false, { TBOX_STYLE_FIELD(align_self) } },
    { "gap",                        false, { TBOX_STYLE_FIELD(row_gap), TBOX_STYLE_FIELD(column_gap) } },
    { "row-gap",                    false, { TBOX_STYLE_FIELD(row_gap) } },
    { "column-gap",                 false, { TBOX_STYLE_FIELD(column_gap) } },
    { "flex",                       false, { TBOX_STYLE_FIELD(flex_grow), TBOX_STYLE_FIELD(flex_shrink), TBOX_STYLE_FIELD(flex_basis) } },
    { "flex-grow",                  false, { TBOX_STYLE_FIELD(flex_grow) } },
    { "flex-shrink",                false, { TBOX_STYLE_FIELD(flex_shrink) } },
    { "flex-basis",                 false, { TBOX_STYLE_FIELD(flex_basis) } },
    { "order",                      false, { TBOX_STYLE_FIELD(order) } },
};

/* Box-side suffixes in the engine's horizontal left-to-right writing mode,
 * as side indices (top, right, bottom, left). */
static const struct {
    const char *suffix;
    int sides[2];
    size_t count;
} tbox_style_side_suffixes[] = {
    { "-top",          { 0, 0 }, 1 },
    { "-right",        { 1, 0 }, 1 },
    { "-bottom",       { 2, 0 }, 1 },
    { "-left",         { 3, 0 }, 1 },
    { "-block",        { 0, 2 }, 2 },
    { "-inline",       { 3, 1 }, 2 },
    { "-block-start",  { 0, 0 }, 1 },
    { "-block-end",    { 2, 0 }, 1 },
    { "-inline-start", { 3, 0 }, 1 },
    { "-inline-end",   { 1, 0 }, 1 },
};

static bool tbox_style_equal_ci(tbox_string_view a, const char *b) {
    return tbox_string_view_equal_ascii_ci(a, tbox_string_view_from_cstr(b));
}

static bool tbox_style_has_prefix_ci(tbox_string_view text, const char *prefix, tbox_string_view *rest) {
    size_t length = strlen(prefix);
    if (text.size < length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data, length), tbox_string_view_make(prefix, length)))
        return false;
    *rest = tbox_string_view_make(text.data + length, text.size - length);
    return true;
}

/* Side indices for "" (all four sides) or one of the suffixes above. */
static size_t tbox_style_sides(tbox_string_view suffix, int out[4]) {
    if (suffix.size == 0) {
        for (int i = 0; i < 4; i++)
            out[i] = i;
        return 4;
    }
    for (size_t i = 0; i < sizeof(tbox_style_side_suffixes) / sizeof(tbox_style_side_suffixes[0]); i++) {
        if (tbox_style_equal_ci(suffix, tbox_style_side_suffixes[i].suffix)) {
            for (size_t j = 0; j < tbox_style_side_suffixes[i].count; j++)
                out[j] = tbox_style_side_suffixes[i].sides[j];
            return tbox_style_side_suffixes[i].count;
        }
    }
    return 0;
}

/* margin*, padding*, inset*, top/right/bottom/left and border* (except
 * the radius/collapse/spacing properties in the table above). */
static size_t tbox_style_side_fields(tbox_string_view property, tbox_style_field out[TBOX_STYLE_MAX_FIELDS]) {
    static const struct {
        const char *prefix;
        size_t offset;
    } families[] = {
        { "margin",  offsetof(tbox_style, margin)  },
        { "padding", offsetof(tbox_style, padding) },
        { "inset",   offsetof(tbox_style, offset)  },
    };
    int sides[4];
    size_t side_count;
    tbox_string_view rest;
    for (size_t f = 0; f < sizeof(families) / sizeof(families[0]); f++) {
        if (tbox_style_has_prefix_ci(property, families[f].prefix, &rest) && (side_count = tbox_style_sides(rest, sides)) > 0) {
            for (size_t i = 0; i < side_count; i++)
                out[i] = (tbox_style_field){ families[f].offset + (size_t)sides[i] * sizeof(tbox_style_length), sizeof(tbox_style_length) };
            return side_count;
        }
    }
    static const char *const physical[4] = { "top", "right", "bottom", "left" };
    for (size_t i = 0; i < 4; i++)
        if (tbox_style_equal_ci(property, physical[i])) {
            out[0] = (tbox_style_field){ offsetof(tbox_style, offset) + i * sizeof(tbox_style_length), sizeof(tbox_style_length) };
            return 1;
        }

    if (!tbox_style_has_prefix_ci(property, "border", &rest))
        return 0;
    /* An optional -width/-style/-color part at the end, sides before it. */
    static const struct {
        const char *suffix;
        size_t offset, size;
    } parts[] = {
        { "-width", offsetof(tbox_style, border_widths), sizeof(double)                  },
        { "-style", offsetof(tbox_style, border_styles), sizeof(tbox_style_border_style) },
        { "-color", offsetof(tbox_style, border_colors), sizeof(tbox_css_rgba)           },
    };
    size_t first_part = 0, part_count = 3;
    for (size_t p = 0; p < 3; p++) {
        size_t length = strlen(parts[p].suffix);
        if (rest.size >= length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(rest.data + rest.size - length, length), tbox_string_view_make(parts[p].suffix, length))) {
            first_part = p;
            part_count = 1;
            rest       = tbox_string_view_make(rest.data, rest.size - length);
            break;
        }
    }
    side_count = tbox_style_sides(rest, sides);
    size_t count = 0;
    for (size_t p = first_part; p < first_part + part_count; p++)
        for (size_t i = 0; i < side_count; i++)
            out[count++] = (tbox_style_field){ parts[p].offset + (size_t)sides[i] * parts[p].size, parts[p].size };
    return count;
}

/* The fields `property` sets and whether it inherits; 0 fields when the
 * property is unknown. */
static size_t tbox_style_property_lookup(tbox_string_view property, tbox_style_field out[TBOX_STYLE_MAX_FIELDS], bool *inherited) {
    *inherited = false;
    for (size_t i = 0; i < sizeof(tbox_style_properties) / sizeof(tbox_style_properties[0]); i++) {
        if (tbox_style_equal_ci(property, tbox_style_properties[i].name)) {
            *inherited   = tbox_style_properties[i].inherited;
            size_t count = 0;
            while (count < TBOX_STYLE_MAX_FIELDS && tbox_style_properties[i].fields[count].size > 0) {
                out[count] = tbox_style_properties[i].fields[count];
                count++;
            }
            return count;
        }
    }
    return tbox_style_side_fields(property, out);
}

typedef enum tbox_style_keyword_kind {
    TBOX_STYLE_KEYWORD_NONE,
    TBOX_STYLE_KEYWORD_INHERIT,
    TBOX_STYLE_KEYWORD_INITIAL,
    TBOX_STYLE_KEYWORD_UNSET,
} tbox_style_keyword_kind;

static tbox_style_keyword_kind tbox_style_keyword_of(tbox_string_view value) {
    size_t start = 0, end = value.size;
    while (start < end && (value.data[start] == ' ' || value.data[start] == '\t' || value.data[start] == '\n' || value.data[start] == '\r' || value.data[start] == '\f'))
        start++;
    while (end > start && (value.data[end - 1] == ' ' || value.data[end - 1] == '\t' || value.data[end - 1] == '\n' || value.data[end - 1] == '\r' || value.data[end - 1] == '\f'))
        end--;
    tbox_string_view word = tbox_string_view_make(value.data + start, end - start);
    if (tbox_style_equal_ci(word, "inherit"))
        return TBOX_STYLE_KEYWORD_INHERIT;
    if (tbox_style_equal_ci(word, "initial"))
        return TBOX_STYLE_KEYWORD_INITIAL;
    if (tbox_style_equal_ci(word, "unset") || tbox_style_equal_ci(word, "revert") || tbox_style_equal_ci(word, "revert-layer"))
        return TBOX_STYLE_KEYWORD_UNSET; /* no user stylesheet: revert acts as unset for author rules */
    return TBOX_STYLE_KEYWORD_NONE;
}

bool tbox_style_keywords_present(const tbox_css_computed_style *computed) {
    if (computed == NULL)
        return false;
    for (size_t i = 0; i < computed->count; i++)
        if (tbox_style_keyword_of(computed->items[i].value) != TBOX_STYLE_KEYWORD_NONE)
            return true;
    return false;
}

tbox_css_computed_style tbox_style_keywords_strip(const tbox_css_computed_style *computed) {
    tbox_css_computed_style stripped = { NULL, 0, NULL };
    if (computed == NULL || computed->count == 0)
        return stripped;
    stripped.items = (tbox_css_resolved_declaration *)malloc(computed->count * sizeof(tbox_css_resolved_declaration));
    if (stripped.items == NULL)
        return stripped;
    for (size_t i = 0; i < computed->count; i++) {
        const tbox_css_resolved_declaration *decl = &computed->items[i];
        tbox_style_keyword_kind kind              = tbox_style_keyword_of(decl->value);
        if (kind == TBOX_STYLE_KEYWORD_NONE) {
            stripped.items[stripped.count++] = *decl;
        } else if (kind == TBOX_STYLE_KEYWORD_INITIAL && tbox_style_equal_ci(decl->property, "font-size")) {
            stripped.items[stripped.count]       = *decl;
            stripped.items[stripped.count].value = tbox_string_view_from_cstr("medium");
            stripped.count++;
        }
    }
    return stripped;
}

void tbox_style_keywords_release(tbox_css_computed_style *stripped) {
    if (stripped == NULL)
        return;
    free(stripped->items);
    stripped->items = NULL;
    stripped->count = 0;
}

static bool tbox_style_fields_overlap(tbox_style_field a, tbox_style_field b) {
    return a.offset < b.offset + b.size && b.offset < a.offset + a.size;
}

/* True when an ordinary (non-keyword) declaration beating `keyword` also
 * sets `field`. */
static bool tbox_style_field_overridden(const tbox_css_computed_style *computed, const tbox_css_resolved_declaration *keyword, tbox_style_field field) {
    for (size_t i = 0; i < computed->count; i++) {
        const tbox_css_resolved_declaration *other = &computed->items[i];
        if (other == keyword || tbox_style_keyword_of(other->value) != TBOX_STYLE_KEYWORD_NONE || tbox_css_cascade_priority_compare(other, keyword) <= 0)
            continue;
        tbox_style_field fields[TBOX_STYLE_MAX_FIELDS];
        bool inherited;
        size_t count = tbox_style_property_lookup(other->property, fields, &inherited);
        for (size_t f = 0; f < count; f++)
            if (tbox_style_fields_overlap(fields[f], field))
                return true;
    }
    return false;
}

void tbox_style_keywords_apply(tbox_style *style, const tbox_style *parent, const tbox_style *initial, const tbox_css_computed_style *computed) {
    if (style == NULL || initial == NULL || computed == NULL)
        return;

    /* Lower-priority keywords first, so a higher one overwrites them. */
    const tbox_css_resolved_declaration *order[64];
    size_t count = 0;
    for (size_t i = 0; i < computed->count && count < sizeof(order) / sizeof(order[0]); i++)
        if (tbox_style_keyword_of(computed->items[i].value) != TBOX_STYLE_KEYWORD_NONE)
            order[count++] = &computed->items[i];
    for (size_t i = 1; i < count; i++)
        for (size_t j = i; j > 0 && tbox_css_cascade_priority_compare(order[j - 1], order[j]) > 0; j--) {
            const tbox_css_resolved_declaration *swap = order[j];
            order[j]                                  = order[j - 1];
            order[j - 1]                              = swap;
        }

    bool touched_borders = false, touched_radius = false;
    for (size_t k = 0; k < count; k++) {
        const tbox_css_resolved_declaration *decl = order[k];
        tbox_style_field fields[TBOX_STYLE_MAX_FIELDS];
        bool inherited;
        size_t field_count = tbox_style_property_lookup(decl->property, fields, &inherited);
        if (field_count == 0)
            continue;
        tbox_style_keyword_kind kind = tbox_style_keyword_of(decl->value);
        bool from_parent             = parent != NULL && (kind == TBOX_STYLE_KEYWORD_INHERIT || (kind == TBOX_STYLE_KEYWORD_UNSET && inherited));
        const tbox_style *source     = from_parent ? parent : initial;
        for (size_t f = 0; f < field_count; f++) {
            if (tbox_style_field_overridden(computed, decl, fields[f]))
                continue;
            memcpy((char *)style + fields[f].offset, (const char *)source + fields[f].offset, fields[f].size);
            if (fields[f].offset >= offsetof(tbox_style, border_widths) && fields[f].offset < offsetof(tbox_style, border_per_side))
                touched_borders = true;
            if (fields[f].offset >= offsetof(tbox_style, border_radius_corners) && fields[f].offset < offsetof(tbox_style, border_radius_corners) + sizeof(style->border_radius_corners))
                touched_radius = true;
        }
    }

    /* Keep the derived uniform fields in step with the per-side arrays. */
    if (touched_borders) {
        style->border_per_side = false;
        for (size_t i = 1; i < 4; i++)
            if (style->border_widths[i] != style->border_widths[0] || style->border_styles[i] != style->border_styles[0] ||
                memcmp(&style->border_colors[i], &style->border_colors[0], sizeof(tbox_css_rgba)) != 0)
                style->border_per_side = true;
        style->border_width = style->border_widths[0];
        style->border_style = style->border_styles[0];
        style->border_color = style->border_colors[0];
    }
    if (touched_radius) {
        const double *c      = style->border_radius_corners;
        style->border_radius = c[0] == c[1] && c[0] == c[2] && c[0] == c[3] ? c[0] : 0.0;
    }
}
