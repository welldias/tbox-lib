#ifndef TBOX_CSS_PARSER_STYLESHEET_H
#define TBOX_CSS_PARSER_STYLESHEET_H

#include <tbox/css_parser.h>

#include "base/tbox_arena.h"

struct tbox_css_stylesheet {
    tbox_arena arena;
    tbox_css_ruleset *rulesets;
    size_t ruleset_count;
};

#endif /* TBOX_CSS_PARSER_STYLESHEET_H */
