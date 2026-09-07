#ifndef TBOX_CSS_PARSER_PARSER_H
#define TBOX_CSS_PARSER_PARSER_H

#include <tbox/css_parser.h>

#include "tbox_css_stylesheet.h"
#include "tbox_css_tokenizer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbox_css_parser {
    tbox_css_tokenizer tokenizer;
    tbox_css_token current;
    tbox_css_stylesheet *stylesheet; /* owns the arena everything is copied/allocated into */
} tbox_css_parser;

void tbox_css_parser_init(tbox_css_parser *parser, const char *input, size_t length, tbox_css_stylesheet *stylesheet);

/* Consumes every token and fills stylesheet->rulesets/ruleset_count.
 * Malformed rulesets/declarations/at-rules are skipped per CSS2.1's Appendix
 * G.1 error-recovery grammar (see tbox_css_parser.c's top-of-file comment
 * for the exact recovery algorithm). Unlike tbox_xpath_parser (which hard-
 * fails on the first syntax error via a sticky has_error flag), this parser
 * never fails on bad syntax -- there is no error/has_error state at all;
 * "failure" of an individual grammar production just means "produce nothing
 * here, and let the caller's recovery logic resynchronize." */
void tbox_css_parser_run(tbox_css_parser *parser);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_PARSER_H */
