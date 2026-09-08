#ifndef TBOX_CSS_SELECTOR_PARSE_H
#define TBOX_CSS_SELECTOR_PARSE_H

#include <tbox/css_selector.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Implements tbox_css_selector_compile by reusing the css_parser module's
 * selector-group grammar (tbox_css_parser_parse_standalone_selector_group)
 * instead of duplicating it. */
tbox_css_selector_query *tbox_css_selector_parser_compile(const char *text, size_t length, size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_SELECTOR_PARSE_H */
