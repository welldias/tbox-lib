#include <stdlib.h>

#include <tbox/css_parser.h>

#include "tbox_css_parser.h"
#include "tbox_css_stylesheet.h"

tbox_css_stylesheet *tbox_css_parse(const char *input, size_t length) {
    tbox_css_stylesheet *stylesheet = malloc(sizeof(tbox_css_stylesheet));
    if (stylesheet == NULL) {
        return NULL;
    }

    stylesheet->arena         = tbox_arena_create(0);
    stylesheet->rulesets      = NULL;
    stylesheet->ruleset_count = 0;

    tbox_css_parser parser;
    tbox_css_parser_init(&parser, input, length, &stylesheet->arena);
    tbox_css_parser_run(&parser, &stylesheet->rulesets, &stylesheet->ruleset_count);

    return stylesheet;
}

size_t tbox_css_stylesheet_ruleset_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet->ruleset_count;
}

const tbox_css_ruleset *tbox_css_stylesheet_rulesets(const tbox_css_stylesheet *stylesheet) {
    return stylesheet->rulesets;
}

void tbox_css_stylesheet_destroy(tbox_css_stylesheet *stylesheet) {
    if (stylesheet == NULL) {
        return;
    }
    tbox_arena_destroy(&stylesheet->arena);
    free(stylesheet);
}
