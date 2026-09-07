#ifndef TBOX_HTML_PARSER_TOKENIZER_H
#define TBOX_HTML_PARSER_TOKENIZER_H

#include <stddef.h>

#include "base/tbox_vector.h"
#include "tbox_html_token.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Byte-oriented HTML5 tokenizer: every structural delimiter (<, >, =, quotes,
 * /, whitespace) is ASCII, and UTF-8 continuation/lead bytes are always >=
 * 0x80, so token boundaries can be found by scanning raw bytes without ever
 * decoding codepoints. Pull-based: call tbox_html_tokenizer_next repeatedly
 * until it yields a TBOX_HTML_TOKEN_EOF token. */
typedef struct tbox_html_tokenizer {
    const char *input;
    size_t length;
    size_t position;

    /* Scratch storage for the current START_TAG's attributes, reused (reset
     * to length 0) at the start of every start tag rather than reallocated. */
    tbox_vector pending_attributes;

    bool in_raw_text;
    tbox_string_view raw_text_end_tag_name;
} tbox_html_tokenizer;

/* `arena` backs only the tokenizer's internal attribute scratch buffer, never
 * the input bytes themselves (input is referenced, not copied or owned). */
void tbox_html_tokenizer_init(tbox_html_tokenizer *tokenizer, const char *input, size_t length, tbox_arena *arena);

/* Always returns true and fills *out_token; the caller stops once
 * out_token->type == TBOX_HTML_TOKEN_EOF. */
bool tbox_html_tokenizer_next(tbox_html_tokenizer *tokenizer, tbox_html_token *out_token);

/* Switches the tokenizer into raw-text mode: everything up to (but not
 * including) a matching "</tag_name" close sequence is emitted as a single
 * TEXT token, ignoring '<' otherwise. Used by the tree builder right after a
 * <script> or <style> start tag. */
void tbox_html_tokenizer_enter_raw_text(tbox_html_tokenizer *tokenizer, tbox_string_view tag_name);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_TOKENIZER_H */
