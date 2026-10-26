#ifndef TBOX_CSS_PARSER_PARSER_H
#define TBOX_CSS_PARSER_PARSER_H

#include <tbox/css_parser.h>

#include "tbox_css_stylesheet.h"
#include "tbox_css_tokenizer.h"
#include "base/tbox_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbox_css_parser {
    tbox_css_tokenizer tokenizer;
    tbox_css_token current;
    tbox_arena *arena; /* everything copied/allocated by the parser lands here */
    /* Cascade layers seen so far (tbox_css_layer_node, in order of first
     * mention); NULL outside tbox_css_parser_run. */
    tbox_vector *layers;
    const tbox_css_scope *scope;
    tbox_css_property_rule *properties;
    tbox_css_named_rule *counter_styles;
    /* `@keyframes` rules (tbox_css_keyframes_rule) collected while parsing;
     * NULL outside tbox_css_parser_run, which leaves the result in
     * keyframes_out (arena-allocated, NULL when there is none). */
    tbox_vector *palettes;
    tbox_css_font_palette_rule *palettes_out;
    size_t palettes_out_count;
    tbox_vector *keyframes;
    tbox_css_keyframes_rule *keyframes_out;
    size_t keyframes_out_count;
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
void tbox_css_parser_run(tbox_css_parser *parser, tbox_css_ruleset **out_rulesets, size_t *out_ruleset_count, tbox_css_font_face_rule **out_faces, size_t *out_face_count);

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
bool tbox_css_parser_parse_standalone_selector_group(const char *input, size_t length, tbox_arena *arena, tbox_css_selector **out_selectors, size_t *out_count, size_t *out_error_offset);

/* The `index`th property @supports accepts (tbox_css_supports.c), NULL
 * past the last one. */
const char *tbox_css_supported_property(size_t index);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_PARSER_H */
