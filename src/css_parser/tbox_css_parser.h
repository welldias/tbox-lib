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
    tbox_arena *arena; /* everything copied/allocated by the parser lands here */
} tbox_css_parser;

void tbox_css_parser_init(tbox_css_parser *parser, const char *input, size_t length, tbox_arena *arena);

/* Consumes every token and fills out_rulesets/out_ruleset_count (arena-
 * allocated). Malformed rulesets/declarations/at-rules are skipped per
 * CSS2.1's Appendix G.1 error-recovery grammar (see tbox_css_parser.c's
 * top-of-file comment for the exact recovery algorithm). Unlike
 * tbox_xpath_parser (which hard-fails on the first syntax error via a
 * sticky has_error flag), this parser never fails on bad syntax -- there is
 * no error/has_error state at all; "failure" of an individual grammar
 * production just means "produce nothing here, and let the caller's
 * recovery logic resynchronize." */
void tbox_css_parser_run(tbox_css_parser *parser, tbox_css_ruleset **out_rulesets, size_t *out_ruleset_count);

/* Parses a standalone selector-group -- the same grammar as the
 * `selector_group` embedded in a ruleset (e.g. "div.foo > p, #bar[href]"),
 * but with no trailing declaration block. Used by tbox_css_selector_compile
 * to reuse this grammar without duplicating it. Unlike tbox_css_parser_run,
 * this hard-fails (like tbox_xpath_compile) on a syntax error or on
 * trailing garbage after the last selector: a selector typed on its own is
 * meant to be well-formed, not tolerated the way a whole stylesheet is.
 * Returns false, leaving out_selectors/out_count untouched, on failure;
 * if out_error_offset is non-NULL, receives the byte offset where parsing
 * stopped. */
bool tbox_css_parser_parse_standalone_selector_group(const char *input, size_t length, tbox_arena *arena,
                                                       tbox_css_selector **out_selectors, size_t *out_count,
                                                       size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_PARSER_H */
