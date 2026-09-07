#ifndef TBOX_HTML_PARSER_TOKEN_H
#define TBOX_HTML_PARSER_TOKEN_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbox_html_token_type {
    TBOX_HTML_TOKEN_EOF,
    TBOX_HTML_TOKEN_START_TAG,
    TBOX_HTML_TOKEN_END_TAG,
    TBOX_HTML_TOKEN_TEXT,
    TBOX_HTML_TOKEN_COMMENT,
    TBOX_HTML_TOKEN_DOCTYPE,
} tbox_html_token_type;

/* Raw (not normalized) views into the tokenizer's input buffer. Valid only
 * until the next call to tbox_html_tokenizer_next. */
typedef struct tbox_html_token_attribute {
    tbox_string_view name;
    tbox_string_view value; /* size == 0 for a valueless (boolean) attribute */
} tbox_html_token_attribute;

typedef struct tbox_html_token {
    tbox_html_token_type type;

    /* TEXT/COMMENT: content. DOCTYPE: name. START_TAG/END_TAG: tag name. */
    tbox_string_view text;

    /* Only meaningful for START_TAG. Backed by the tokenizer's internal
     * scratch buffer: valid only until the next tbox_html_tokenizer_next
     * call. */
    const tbox_html_token_attribute *attributes;
    size_t attribute_count;

    /* START_TAG only: true if the tag's source ended in "/>". */
    bool self_closing;
} tbox_html_token;

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_TOKEN_H */
