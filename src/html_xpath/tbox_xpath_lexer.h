#ifndef TBOX_HTML_XPATH_LEXER_H
#define TBOX_HTML_XPATH_LEXER_H

#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbox_xpath_token_type {
    TBOX_XPATH_TOKEN_SLASH,
    TBOX_XPATH_TOKEN_SLASH_SLASH,
    TBOX_XPATH_TOKEN_DOT,
    TBOX_XPATH_TOKEN_DOT_DOT,
    TBOX_XPATH_TOKEN_AT,
    TBOX_XPATH_TOKEN_STAR,
    TBOX_XPATH_TOKEN_LBRACKET,
    TBOX_XPATH_TOKEN_RBRACKET,
    TBOX_XPATH_TOKEN_LPAREN,
    TBOX_XPATH_TOKEN_RPAREN,
    TBOX_XPATH_TOKEN_EQUALS,
    TBOX_XPATH_TOKEN_NAME,   /* [A-Za-z_][A-Za-z0-9_-]* */
    TBOX_XPATH_TOKEN_NUMBER, /* [0-9]+ */
    TBOX_XPATH_TOKEN_STRING, /* quotes stripped; no escape support */
    TBOX_XPATH_TOKEN_EOF,
    TBOX_XPATH_TOKEN_INVALID,
} tbox_xpath_token_type;

typedef struct tbox_xpath_token {
    tbox_xpath_token_type type;
    tbox_string_view text; /* raw view into the lexer's input buffer */
    size_t offset;         /* byte offset of this token's start, for error reporting */
} tbox_xpath_token;

typedef struct tbox_xpath_lexer {
    const char *input;
    size_t length;
    size_t position;
} tbox_xpath_lexer;

void tbox_xpath_lexer_init(tbox_xpath_lexer *lexer, const char *input, size_t length);
tbox_xpath_token tbox_xpath_lexer_next(tbox_xpath_lexer *lexer);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_XPATH_LEXER_H */
