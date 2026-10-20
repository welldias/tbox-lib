#include <stdlib.h>

#include <tbox/css_parser.h>

#include "base/tbox_string.h"
#include "tbox_css_parser.h"
#include "tbox_css_stylesheet.h"

tbox_css_stylesheet *tbox_css_stylesheet_create_empty(void) {
    tbox_css_stylesheet *stylesheet = malloc(sizeof(tbox_css_stylesheet));
    if (stylesheet == NULL) {
        return NULL;
    }

    stylesheet->arena         = tbox_arena_create(0);
    if (stylesheet->arena.first == NULL) {
        free(stylesheet);
        return NULL;
    }
    stylesheet->rulesets      = NULL;
    stylesheet->ruleset_count = 0;
    stylesheet->pseudo_element_rulesets      = NULL;
    stylesheet->pseudo_element_ruleset_count = 0;
    stylesheet->font_palettes = NULL;
    stylesheet->font_palette_count = 0;
    stylesheet->font_faces = NULL;
    stylesheet->font_face_count = 0;
    stylesheet->keyframes       = NULL;
    stylesheet->keyframes_count = 0;
    stylesheet->properties = NULL;
    stylesheet->counter_styles = NULL;

    return stylesheet;
}

tbox_css_stylesheet *tbox_css_parse(const char *input, size_t length) {
    tbox_css_stylesheet *stylesheet = tbox_css_stylesheet_create_empty();
    if (stylesheet == NULL) {
        return NULL;
    }

    /* Strings such as `content` and `text-overflow` reach the screen:
     * malformed UTF-8 becomes U+FFFD here, as in tbox_html_parse. */
    tbox_string_view source = tbox_utf8_sanitize(&stylesheet->arena, tbox_string_view_make(input, length));
    tbox_css_parser parser;
    tbox_css_parser_init(&parser, source.data, source.size, &stylesheet->arena);
    tbox_css_parser_run(&parser, &stylesheet->rulesets, &stylesheet->ruleset_count, &stylesheet->font_faces, &stylesheet->font_face_count);
    stylesheet->font_palettes = parser.palettes_out;
    stylesheet->font_palette_count = parser.palettes_out_count;
    stylesheet->properties = parser.properties;
    stylesheet->counter_styles = parser.counter_styles;
    stylesheet->keyframes       = parser.keyframes_out;
    stylesheet->keyframes_count = parser.keyframes_out_count;
    if (!tbox_css_stylesheet_index_pseudo_elements(stylesheet)) {
        tbox_css_stylesheet_destroy(stylesheet);
        return NULL;
    }

    return stylesheet;
}

/* A selector names a pseudo-element when one of its simple selectors is a
 * "::name" pseudo (the cascade's own test, see
 * tbox_css_cascade_targets_pseudo_element). */
static bool tbox_css_ruleset_has_pseudo_element(const tbox_css_ruleset *ruleset) {
    for (size_t s = 0; s < ruleset->selector_count; s++) {
        const tbox_css_selector *selector = &ruleset->selectors[s];
        for (size_t i = 0; i < selector->simple_selector_count; i++) {
            const tbox_css_simple_selector *item = &selector->simple_selectors[i];
            if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && item->name.size >= 3 && item->name.data[0] == ':' && item->name.data[1] == ':')
                return true;
        }
    }
    return false;
}

bool tbox_css_stylesheet_index_pseudo_elements(tbox_css_stylesheet *stylesheet) {
    size_t count = 0;
    for (size_t r = 0; r < stylesheet->ruleset_count; r++)
        if (tbox_css_ruleset_has_pseudo_element(&stylesheet->rulesets[r]))
            count++;
    stylesheet->pseudo_element_rulesets      = NULL;
    stylesheet->pseudo_element_ruleset_count = 0;
    if (count == 0)
        return true;
    stylesheet->pseudo_element_rulesets = tbox_arena_alloc(&stylesheet->arena, count * sizeof(size_t));
    if (stylesheet->pseudo_element_rulesets == NULL)
        return false;
    for (size_t r = 0; r < stylesheet->ruleset_count; r++)
        if (tbox_css_ruleset_has_pseudo_element(&stylesheet->rulesets[r]))
            stylesheet->pseudo_element_rulesets[stylesheet->pseudo_element_ruleset_count++] = r;
    return true;
}

size_t tbox_css_stylesheet_ruleset_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->ruleset_count : 0;
}

const tbox_css_ruleset *tbox_css_stylesheet_rulesets(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->rulesets : NULL;
}

size_t tbox_css_stylesheet_font_face_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_face_count : 0;
}

const tbox_css_font_face_rule *tbox_css_stylesheet_font_faces(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_faces : NULL;
}

size_t tbox_css_stylesheet_keyframes_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->keyframes_count : 0;
}

const tbox_css_keyframes_rule *tbox_css_stylesheet_keyframes(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->keyframes : NULL;
}

void tbox_css_stylesheet_destroy(tbox_css_stylesheet *stylesheet) {
    if (stylesheet == NULL) {
        return;
    }
    tbox_arena_destroy(&stylesheet->arena);
    free(stylesheet);
}

size_t tbox_css_stylesheet_font_palette_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_palette_count : 0;
}

const tbox_css_font_palette_rule *tbox_css_stylesheet_font_palettes(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_palettes : NULL;
}
