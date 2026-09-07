#ifndef TBOX_CSS_PARSER_TOKEN_H
#define TBOX_CSS_PARSER_TOKEN_H

#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A deliberately trimmed subset of the CSS2.1 core tokenizer (spec Appendix
 * G.2) -- everything needed to parse rulesets/selectors/declarations, plus
 * enough to safely skip at-rules and malformed constructs even for token
 * kinds this module doesn't otherwise interpret. Operator composition
 * ('~=', '|=') is NOT a token kind: the tokenizer emits TILDE/PIPE/EQUALS as
 * independent one-byte tokens, and the parser composes them -- this keeps
 * the tokenizer free of multi-token lookahead. */
typedef enum tbox_css_token_type {
    TBOX_CSS_TOKEN_EOF,
    TBOX_CSS_TOKEN_S,          /* a run of whitespace and/or comments, collapsed into one token */
    TBOX_CSS_TOKEN_IDENT,
    TBOX_CSS_TOKEN_FUNCTION,   /* ident immediately followed by '(' (the '(' is consumed too); text = ident only */
    TBOX_CSS_TOKEN_URL,        /* url(...) per the CSS2.1 URI grammar, consumed atomically */
    TBOX_CSS_TOKEN_STRING,     /* quotes stripped; escapes preserved raw, not decoded */
    TBOX_CSS_TOKEN_HASH,       /* '#' name; text excludes '#' */
    TBOX_CSS_TOKEN_NUMBER,     /* digit+ ['.' digit+]? or '.' digit+; no sign, no unit fusion */
    TBOX_CSS_TOKEN_AT_KEYWORD, /* '@' ident; text excludes '@' */
    TBOX_CSS_TOKEN_CDO,        /* "<!--" */
    TBOX_CSS_TOKEN_CDC,        /* "-->" */
    TBOX_CSS_TOKEN_COLON,
    TBOX_CSS_TOKEN_SEMICOLON,
    TBOX_CSS_TOKEN_LBRACE,
    TBOX_CSS_TOKEN_RBRACE,
    TBOX_CSS_TOKEN_LPAREN,
    TBOX_CSS_TOKEN_RPAREN,
    TBOX_CSS_TOKEN_LBRACKET,
    TBOX_CSS_TOKEN_RBRACKET,
    TBOX_CSS_TOKEN_DOT,
    TBOX_CSS_TOKEN_COMMA,
    TBOX_CSS_TOKEN_GT,     /* '>' */
    TBOX_CSS_TOKEN_PLUS,   /* '+' */
    TBOX_CSS_TOKEN_TILDE,  /* '~' -- only meaningful inside [attr~=val] */
    TBOX_CSS_TOKEN_STAR,   /* '*' */
    TBOX_CSS_TOKEN_EQUALS, /* '=' */
    TBOX_CSS_TOKEN_PIPE,   /* '|' -- only meaningful inside [attr|=val] */
    TBOX_CSS_TOKEN_DELIM,  /* catch-all: any other single byte */
} tbox_css_token_type;

typedef struct tbox_css_token {
    tbox_css_token_type type;
    tbox_string_view text; /* raw view into the tokenizer's input buffer; meaning depends on type (see enum comments) */
    size_t offset;         /* byte offset of this token's first source byte */
} tbox_css_token;

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_TOKEN_H */
