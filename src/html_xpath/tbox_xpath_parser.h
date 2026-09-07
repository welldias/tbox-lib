#ifndef TBOX_HTML_XPATH_PARSER_H
#define TBOX_HTML_XPATH_PARSER_H

#include <stddef.h>

#include "tbox_xpath_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses `expr` into a newly allocated tbox_xpath_query (owning its own
 * arena). Returns NULL on syntax error; if out_error_offset is non-NULL,
 * *out_error_offset receives the byte offset where parsing failed. */
tbox_xpath_query *tbox_xpath_parser_parse(const char *expr, size_t length, size_t *out_error_offset);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_XPATH_PARSER_H */
