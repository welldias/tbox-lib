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

/* The rulesets that may match an element, by the rightmost compound of
 * their selectors: its id, else one of its classes, else its tag, else
 * any element (`universal`). A ruleset is in the bucket of each of its
 * selectors; one in none of an element's buckets has no selector that can
 * match it. Indices into the stylesheet's `rulesets`, ascending. */
typedef struct tbox_css_rule_bucket {
    bool used;                          /* false: an empty slot */
    tbox_css_simple_selector_kind kind; /* TYPE, ID or CLASS */
    tbox_string_view name;              /* TYPE: compared ASCII case-insensitively */
    size_t *rulesets;
    size_t count;
    size_t last; /* while building: the last ruleset added + 1 */
} tbox_css_rule_bucket;

typedef struct tbox_css_rule_index {
    tbox_css_rule_bucket *buckets; /* open addressing over mask + 1 slots; NULL: not built */
    size_t mask;
    size_t *universal;
    size_t universal_count;
} tbox_css_rule_index;

/* The rulesets of a stylesheet with a selector naming one pseudo-element,
 * and a rule index over them: what the cascade of that pseudo-element
 * looks at. */
typedef struct tbox_css_pseudo_element_rules {
    tbox_string_view name; /* without its "::" ("before", "marker") */
    size_t *rulesets;      /* indices into the stylesheet's rulesets, in order */
    size_t ruleset_count;
    tbox_css_rule_index index; /* zero when it could not be built */
} tbox_css_pseudo_element_rules;

struct tbox_css_stylesheet {
    tbox_arena arena;
    tbox_css_ruleset *rulesets;
    size_t ruleset_count;
    /* Indices into `rulesets`, in order, of those with a selector that
     * names a pseudo-element: the only ones the cascade of a
     * pseudo-element has to look at. */
    size_t *pseudo_element_rulesets;
    size_t pseudo_element_ruleset_count;
    /* Each pseudo-element those selectors name, once, with its own
     * rulesets and rule index (tbox_css_stylesheet_pseudo_element_rules):
     * an element asked for one no rule can fit needs no cascade. */
    tbox_css_pseudo_element_rules *pseudo_elements;
    size_t pseudo_element_count;
    /* Over every ruleset, for the cascade of an element. */
    tbox_css_rule_index element_index;
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

/* Fills pseudo_element_rulesets and the two rule indexes once `rulesets`
 * is final. False when out of memory. */
bool tbox_css_stylesheet_index_rules(tbox_css_stylesheet *stylesheet);

/* Whether a selector of `stylesheet` names the pseudo-element `pseudo`
 * ("before", without "::"). */
bool tbox_css_stylesheet_names_pseudo_element(const tbox_css_stylesheet *stylesheet, const char *pseudo);

/* The rulesets naming `pseudo` ("before") in `stylesheet`, NULL when none
 * does. */
const tbox_css_pseudo_element_rules *tbox_css_stylesheet_pseudo_element_rules(const tbox_css_stylesheet *stylesheet, tbox_string_view pseudo);

/* The bucket of (`kind`, `name`) in `index`, NULL when there is none. */
const tbox_css_rule_bucket *tbox_css_rule_index_find(const tbox_css_rule_index *index, tbox_css_simple_selector_kind kind, tbox_string_view name);

#endif /* TBOX_CSS_PARSER_STYLESHEET_H */
