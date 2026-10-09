#include <stdlib.h>

#include <tbox/css_parser.h>

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
    stylesheet->font_faces = NULL;
    stylesheet->font_face_count = 0;

    return stylesheet;
}

tbox_css_stylesheet *tbox_css_parse(const char *input, size_t length) {
    tbox_css_stylesheet *stylesheet = tbox_css_stylesheet_create_empty();
    if (stylesheet == NULL) {
        return NULL;
    }

    tbox_css_parser parser;
    tbox_css_parser_init(&parser, input, length, &stylesheet->arena);
    tbox_css_parser_run(&parser, &stylesheet->rulesets, &stylesheet->ruleset_count, &stylesheet->font_faces, &stylesheet->font_face_count);

    return stylesheet;
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

void tbox_css_stylesheet_destroy(tbox_css_stylesheet *stylesheet) {
    if (stylesheet == NULL) {
        return;
    }
    tbox_arena_destroy(&stylesheet->arena);
    free(stylesheet);
}
