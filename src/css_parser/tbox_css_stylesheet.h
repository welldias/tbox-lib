#ifndef TBOX_CSS_PARSER_STYLESHEET_H
#define TBOX_CSS_PARSER_STYLESHEET_H

#include <tbox/css_parser.h>

#include "base/tbox_arena.h"

typedef struct tbox_css_named_rule {
    tbox_string_view name;
    tbox_css_declaration *declarations;
    size_t declaration_count;
    const tbox_css_media_condition *media;
    struct tbox_css_named_rule *next;
} tbox_css_named_rule;

struct tbox_css_property_rule {
    tbox_string_view name, syntax, initial;
    bool inherits;
    const tbox_css_media_condition *media;
    struct tbox_css_property_rule *next;
};

struct tbox_css_stylesheet {
    tbox_arena arena;
    tbox_css_ruleset *rulesets;
    size_t ruleset_count;
    tbox_css_font_face_rule *font_faces;
    size_t font_face_count;
    tbox_css_font_palette_rule *font_palettes;
    size_t font_palette_count;
    tbox_css_keyframes_rule *keyframes;
    size_t keyframes_count;
    tbox_css_property_rule *properties;
    tbox_css_named_rule *counter_styles;
};

/* Type checking/computation shared by registration validation and var().
 * NULL units accepts only computationally independent initial values. */
struct tbox_style_units;
bool tbox_style_registered_value(tbox_arena *arena, tbox_string_view syntax,
    tbox_string_view value, const struct tbox_style_units *units, tbox_string_view *out);

/* Internal constructor for stylesheets assembled without parsing CSS text. */
tbox_css_stylesheet *tbox_css_stylesheet_create_empty(void);

#endif /* TBOX_CSS_PARSER_STYLESHEET_H */
