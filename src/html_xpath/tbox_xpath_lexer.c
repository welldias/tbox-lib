#include "tbox_xpath_lexer.h"

static bool tbox_xpath_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool tbox_xpath_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool tbox_xpath_is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool tbox_xpath_is_name_char(char c) {
    return tbox_xpath_is_name_start(c) || tbox_xpath_is_digit(c) || c == '-';
}

void tbox_xpath_lexer_init(tbox_xpath_lexer *lexer, const char *input, size_t length) {
    lexer->input    = input;
    lexer->length   = length;
    lexer->position = 0;
}

static tbox_xpath_token tbox_xpath_make_token(tbox_xpath_token_type type, const char *data, size_t size, size_t offset) {
    return (tbox_xpath_token){ .type = type, .text = tbox_string_view_make(data, size), .offset = offset };
}

tbox_xpath_token tbox_xpath_lexer_next(tbox_xpath_lexer *lexer) {
    while (lexer->position < lexer->length && tbox_xpath_is_space(lexer->input[lexer->position])) {
        lexer->position++;
    }

    size_t start = lexer->position;
    if (start >= lexer->length) {
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_EOF, lexer->input + start, 0, start);
    }

    char c = lexer->input[start];

    if (c == '/') {
        if (start + 1 < lexer->length && lexer->input[start + 1] == '/') {
            lexer->position = start + 2;
            return tbox_xpath_make_token(TBOX_XPATH_TOKEN_SLASH_SLASH, lexer->input + start, 2, start);
        }
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_SLASH, lexer->input + start, 1, start);
    }

    if (c == '.') {
        if (start + 1 < lexer->length && lexer->input[start + 1] == '.') {
            lexer->position = start + 2;
            return tbox_xpath_make_token(TBOX_XPATH_TOKEN_DOT_DOT, lexer->input + start, 2, start);
        }
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_DOT, lexer->input + start, 1, start);
    }

    if (c == '@') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_AT, lexer->input + start, 1, start);
    }
    if (c == '*') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_STAR, lexer->input + start, 1, start);
    }
    if (c == '[') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_LBRACKET, lexer->input + start, 1, start);
    }
    if (c == ']') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_RBRACKET, lexer->input + start, 1, start);
    }
    if (c == '(') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_LPAREN, lexer->input + start, 1, start);
    }
    if (c == ')') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_RPAREN, lexer->input + start, 1, start);
    }
    if (c == '=') {
        lexer->position = start + 1;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_EQUALS, lexer->input + start, 1, start);
    }

    if (c == '"' || c == '\'') {
        char quote = c;
        size_t pos = start + 1;
        while (pos < lexer->length && lexer->input[pos] != quote) {
            pos++;
        }
        if (pos >= lexer->length) {
            lexer->position = pos;
            return tbox_xpath_make_token(TBOX_XPATH_TOKEN_INVALID, lexer->input + start, pos - start, start);
        }
        tbox_xpath_token token = tbox_xpath_make_token(TBOX_XPATH_TOKEN_STRING, lexer->input + start + 1, pos - (start + 1), start);
        lexer->position        = pos + 1;
        return token;
    }

    if (tbox_xpath_is_digit(c)) {
        size_t pos = start;
        while (pos < lexer->length && tbox_xpath_is_digit(lexer->input[pos])) {
            pos++;
        }
        lexer->position = pos;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_NUMBER, lexer->input + start, pos - start, start);
    }

    if (tbox_xpath_is_name_start(c)) {
        size_t pos = start;
        while (pos < lexer->length && tbox_xpath_is_name_char(lexer->input[pos])) {
            pos++;
        }
        lexer->position = pos;
        return tbox_xpath_make_token(TBOX_XPATH_TOKEN_NAME, lexer->input + start, pos - start, start);
    }

    lexer->position = start + 1;
    return tbox_xpath_make_token(TBOX_XPATH_TOKEN_INVALID, lexer->input + start, 1, start);
}
