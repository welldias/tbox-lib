#ifndef TBOX_CSS_PARSER_TOKENIZER_H
#define TBOX_CSS_PARSER_TOKENIZER_H

#include <stdbool.h>
#include <stddef.h>

#include "tbox_css_token.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-oriented CSS2.1 core tokenizer. Like tbox_html_tokenizer, every
 * structural delimiter is ASCII and bytes >= 0x80 are scanned verbatim as
 * ordinary identifier/string/URL content, so token boundaries never require
 * decoding UTF-8. Pull-based: call tbox_css_tokenizer_next repeatedly until
 * it yields TBOX_CSS_TOKEN_EOF. Unlike the HTML tokenizer, this one needs no
 * arena -- CSS tokens carry no variable-length side array. */
typedef struct tbox_css_tokenizer {
    const char *input;
    size_t length;
    size_t position;
} tbox_css_tokenizer;

void tbox_css_tokenizer_init(tbox_css_tokenizer *tokenizer, const char *input, size_t length);

/* Always returns true and fills *out_token; the caller stops once
 * out_token->type == TBOX_CSS_TOKEN_EOF. */
bool tbox_css_tokenizer_next(tbox_css_tokenizer *tokenizer, tbox_css_token *out_token);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_TOKENIZER_H */
