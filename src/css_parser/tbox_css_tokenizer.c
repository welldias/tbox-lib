#include "tbox_css_tokenizer.h"

#include "base/tbox_string.h"

static bool tbox_css_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbox_css_is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* nmstart, minus the backslash-escape alternative (escapes are handled
 * structurally at each call site instead -- see the tokenizer's top-of-file
 * simplification notes). */
static bool tbox_css_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (unsigned char)c >= 0x80;
}

/* nmchar, same caveat as tbox_css_is_ident_start. */
static bool tbox_css_is_ident_char(char c) {
    return tbox_css_is_ident_start(c) || tbox_css_is_digit(c) || c == '-';
}

/* True if an `ident` production (CSS2.1: -?{nmstart}{nmchar}*) begins at
 * t->input[pos]: either an nmstart directly, or a single '-' followed by an
 * nmstart. A second leading '-' (as in "--foo", CSS3 custom-property
 * syntax) does NOT match -- this is the deliberate CSS2.1-only-grammar
 * simplification documented in the tokenizer header. */
static bool tbox_css_ident_starts_at(const tbox_css_tokenizer *t, size_t pos) {
    if (pos >= t->length) {
        return false;
    }
    char c = t->input[pos];
    if (tbox_css_is_ident_start(c)) {
        return true;
    }
    return c == '-' && pos + 1 < t->length && tbox_css_is_ident_start(t->input[pos + 1]);
}

static bool tbox_css_tokenizer_starts_with(const tbox_css_tokenizer *t, size_t pos, const char *literal, size_t literal_len) {
    if (pos + literal_len > t->length) {
        return false;
    }
    for (size_t i = 0; i < literal_len; i++) {
        if (t->input[pos + i] != literal[i]) {
            return false;
        }
    }
    return true;
}

static void tbox_css_token_init(tbox_css_token *out, tbox_css_token_type type, tbox_string_view text, size_t offset) {
    out->type   = type;
    out->text   = text;
    out->offset = offset;
}

/* Caller must have already confirmed (via tbox_css_ident_starts_at) that an
 * ident begins at t->position. Advances past the whole ident, consuming any
 * '\' + byte pair as two raw bytes without decoding it (see the tokenizer's
 * "no escape-sequence decoding" simplification). */
static void tbox_css_tokenizer_consume_ident(tbox_css_tokenizer *t) {
    if (t->input[t->position] == '-') {
        t->position++;
    }
    t->position++; /* the nmstart byte itself */

    while (t->position < t->length) {
        char c = t->input[t->position];
        if (c == '\\' && t->position + 1 < t->length) {
            t->position += 2;
            continue;
        }
        if (tbox_css_is_ident_char(c)) {
            t->position++;
            continue;
        }
        break;
    }
}

/* Current token is "url(" -- t->position sits on the '(' (not yet
 * consumed). `start` is the offset of the "url" ident that preceded it, and
 * becomes the emitted token's offset. Implements the CSS2.1 URI production
 * directly: optional quoted string body, or raw bytes up to an unescaped
 * ')'/whitespace, with optional surrounding S. */
static bool tbox_css_tokenizer_scan_url(tbox_css_tokenizer *t, tbox_css_token *out, size_t start) {
    t->position++; /* '(' */
    while (t->position < t->length && tbox_css_is_space(t->input[t->position])) {
        t->position++;
    }

    size_t content_start;
    size_t content_end;

    if (t->position < t->length && (t->input[t->position] == '"' || t->input[t->position] == '\'')) {
        char quote = t->input[t->position];
        t->position++;
        content_start = t->position;
        while (t->position < t->length && t->input[t->position] != quote) {
            if (t->input[t->position] == '\\' && t->position + 1 < t->length) {
                t->position += 2;
                continue;
            }
            t->position++;
        }
        content_end = t->position;
        if (t->position < t->length) {
            t->position++; /* closing quote */
        }
    } else {
        content_start = t->position;
        while (t->position < t->length && t->input[t->position] != ')' && !tbox_css_is_space(t->input[t->position])) {
            if (t->input[t->position] == '\\' && t->position + 1 < t->length) {
                t->position += 2;
                continue;
            }
            t->position++;
        }
        content_end = t->position;
    }

    while (t->position < t->length && tbox_css_is_space(t->input[t->position])) {
        t->position++;
    }
    if (t->position < t->length && t->input[t->position] == ')') {
        t->position++;
    }

    tbox_css_token_init(out, TBOX_CSS_TOKEN_URL, tbox_string_view_make(t->input + content_start, content_end - content_start), start);
    return true;
}

static bool tbox_css_tokenizer_scan_ident_or_function(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t start = t->position;
    tbox_css_tokenizer_consume_ident(t);
    tbox_string_view name = tbox_string_view_make(t->input + start, t->position - start);

    if (t->position < t->length && t->input[t->position] == '(') {
        if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_make("url", 3))) {
            return tbox_css_tokenizer_scan_url(t, out, start);
        }
        t->position++;
        tbox_css_token_init(out, TBOX_CSS_TOKEN_FUNCTION, name, start);
        return true;
    }

    tbox_css_token_init(out, TBOX_CSS_TOKEN_IDENT, name, start);
    return true;
}

/* Quote-delimited. '\' + any byte is consumed as two literal bytes and kept
 * verbatim in the token's text (not decoded, not stripped). Stops at the
 * matching unescaped quote, an unescaped raw newline (a lexical error per
 * spec -- "bad string" -- but treated the same as an ordinary terminator
 * here), or EOF; the run never crosses those boundaries either way. */
static bool tbox_css_tokenizer_scan_string(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t start = t->position;
    char quote   = t->input[t->position];
    t->position++;
    size_t content_start = t->position;

    while (t->position < t->length) {
        char c = t->input[t->position];
        if (c == quote || c == '\n') {
            break;
        }
        if (c == '\\' && t->position + 1 < t->length) {
            t->position += 2;
            continue;
        }
        t->position++;
    }

    size_t content_end = t->position;
    if (t->position < t->length && t->input[t->position] == quote) {
        t->position++;
    }

    tbox_css_token_init(out, TBOX_CSS_TOKEN_STRING, tbox_string_view_make(t->input + content_start, content_end - content_start), start);
    return true;
}

static bool tbox_css_tokenizer_scan_hash(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t start = t->position;
    t->position++; /* '#' */
    size_t name_start = t->position;

    while (t->position < t->length) {
        char c = t->input[t->position];
        if (c == '\\' && t->position + 1 < t->length) {
            t->position += 2;
            continue;
        }
        if (tbox_css_is_ident_char(c)) {
            t->position++;
            continue;
        }
        break;
    }

    if (t->position == name_start) {
        tbox_css_token_init(out, TBOX_CSS_TOKEN_DELIM, tbox_string_view_make(t->input + start, 1), start);
        return true;
    }

    tbox_css_token_init(out, TBOX_CSS_TOKEN_HASH, tbox_string_view_make(t->input + name_start, t->position - name_start), start);
    return true;
}

/* Handles both digit+['.'digit+]? and the leading-dot form .digit+. A bare
 * '.' not followed by a digit is emitted as TBOX_CSS_TOKEN_DOT (the class
 * selector delimiter) instead. No sign, no unit fusion -- see the
 * tokenizer's simplification notes. */
static bool tbox_css_tokenizer_scan_number_or_dot(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t start = t->position;

    if (t->input[t->position] == '.') {
        if (t->position + 1 < t->length && tbox_css_is_digit(t->input[t->position + 1])) {
            t->position++;
            while (t->position < t->length && tbox_css_is_digit(t->input[t->position])) {
                t->position++;
            }
            tbox_css_token_init(out, TBOX_CSS_TOKEN_NUMBER, tbox_string_view_make(t->input + start, t->position - start), start);
            return true;
        }
        t->position++;
        tbox_css_token_init(out, TBOX_CSS_TOKEN_DOT, tbox_string_view_make(t->input + start, 1), start);
        return true;
    }

    while (t->position < t->length && tbox_css_is_digit(t->input[t->position])) {
        t->position++;
    }
    if (t->position < t->length && t->input[t->position] == '.' && t->position + 1 < t->length && tbox_css_is_digit(t->input[t->position + 1])) {
        t->position++;
        while (t->position < t->length && tbox_css_is_digit(t->input[t->position])) {
            t->position++;
        }
    }

    tbox_css_token_init(out, TBOX_CSS_TOKEN_NUMBER, tbox_string_view_make(t->input + start, t->position - start), start);
    return true;
}

static bool tbox_css_tokenizer_scan_at_keyword(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t start = t->position;

    if (!tbox_css_ident_starts_at(t, t->position + 1)) {
        t->position++;
        tbox_css_token_init(out, TBOX_CSS_TOKEN_DELIM, tbox_string_view_make(t->input + start, 1), start);
        return true;
    }

    t->position++; /* '@' */
    size_t name_start = t->position;
    tbox_css_tokenizer_consume_ident(t);
    tbox_css_token_init(out, TBOX_CSS_TOKEN_AT_KEYWORD, tbox_string_view_make(t->input + name_start, t->position - name_start), start);
    return true;
}

/* Swallows a run of whitespace and/or comment blocks (delimited by a slash
 * followed by a star, and a star followed by a slash -- interchangeable
 * with whitespace everywhere in CSS2.1). An unterminated comment consumes
 * to EOF rather than erroring. Returns whether it consumed anything. */
static bool tbox_css_tokenizer_skip_insignificant(tbox_css_tokenizer *t) {
    size_t start = t->position;

    for (;;) {
        while (t->position < t->length && tbox_css_is_space(t->input[t->position])) {
            t->position++;
        }

        if (t->position + 1 < t->length && t->input[t->position] == '/' && t->input[t->position + 1] == '*') {
            t->position += 2;
            while (t->position < t->length && !(t->input[t->position] == '*' && t->position + 1 < t->length && t->input[t->position + 1] == '/')) {
                t->position++;
            }
            t->position = t->position < t->length ? t->position + 2 : t->length;
            continue;
        }

        break;
    }

    return t->position > start;
}

static bool tbox_css_tokenizer_next_impl(tbox_css_tokenizer *t, tbox_css_token *out) {
    size_t run_start = t->position;
    if (tbox_css_tokenizer_skip_insignificant(t)) {
        tbox_css_token_init(out, TBOX_CSS_TOKEN_S, tbox_string_view_make(t->input + run_start, t->position - run_start), run_start);
        return true;
    }

    size_t start = t->position;
    if (start >= t->length) {
        tbox_css_token_init(out, TBOX_CSS_TOKEN_EOF, tbox_string_view_make(NULL, 0), start);
        return true;
    }

    char c = t->input[start];

    if (tbox_css_ident_starts_at(t, start)) {
        return tbox_css_tokenizer_scan_ident_or_function(t, out);
    }
    if (c == '"' || c == '\'') {
        return tbox_css_tokenizer_scan_string(t, out);
    }
    if (c == '#') {
        return tbox_css_tokenizer_scan_hash(t, out);
    }
    if (tbox_css_is_digit(c) || c == '.') {
        return tbox_css_tokenizer_scan_number_or_dot(t, out);
    }
    if (c == '@') {
        return tbox_css_tokenizer_scan_at_keyword(t, out);
    }
    if (c == '<') {
        if (tbox_css_tokenizer_starts_with(t, start, "<!--", 4)) {
            t->position += 4;
            tbox_css_token_init(out, TBOX_CSS_TOKEN_CDO, tbox_string_view_make(t->input + start, 4), start);
            return true;
        }
        t->position++;
        tbox_css_token_init(out, TBOX_CSS_TOKEN_DELIM, tbox_string_view_make(t->input + start, 1), start);
        return true;
    }
    if (c == '-') {
        if (tbox_css_tokenizer_starts_with(t, start, "-->", 3)) {
            t->position += 3;
            tbox_css_token_init(out, TBOX_CSS_TOKEN_CDC, tbox_string_view_make(t->input + start, 3), start);
            return true;
        }
        t->position++;
        tbox_css_token_init(out, TBOX_CSS_TOKEN_DELIM, tbox_string_view_make(t->input + start, 1), start);
        return true;
    }

    tbox_css_token_type simple_type;
    switch (c) {
    case ':':
        simple_type = TBOX_CSS_TOKEN_COLON;
        break;
    case ';':
        simple_type = TBOX_CSS_TOKEN_SEMICOLON;
        break;
    case '{':
        simple_type = TBOX_CSS_TOKEN_LBRACE;
        break;
    case '}':
        simple_type = TBOX_CSS_TOKEN_RBRACE;
        break;
    case '(':
        simple_type = TBOX_CSS_TOKEN_LPAREN;
        break;
    case ')':
        simple_type = TBOX_CSS_TOKEN_RPAREN;
        break;
    case '[':
        simple_type = TBOX_CSS_TOKEN_LBRACKET;
        break;
    case ']':
        simple_type = TBOX_CSS_TOKEN_RBRACKET;
        break;
    case ',':
        simple_type = TBOX_CSS_TOKEN_COMMA;
        break;
    case '>':
        simple_type = TBOX_CSS_TOKEN_GT;
        break;
    case '+':
        simple_type = TBOX_CSS_TOKEN_PLUS;
        break;
    case '~':
        simple_type = TBOX_CSS_TOKEN_TILDE;
        break;
    case '*':
        simple_type = TBOX_CSS_TOKEN_STAR;
        break;
    case '=':
        simple_type = TBOX_CSS_TOKEN_EQUALS;
        break;
    case '|':
        simple_type = TBOX_CSS_TOKEN_PIPE;
        break;
    default:
        simple_type = TBOX_CSS_TOKEN_DELIM;
        break;
    }

    t->position++;
    tbox_css_token_init(out, simple_type, tbox_string_view_make(t->input + start, 1), start);
    return true;
}

void tbox_css_tokenizer_init(tbox_css_tokenizer *tokenizer, const char *input, size_t length) {
    tokenizer->input    = input;
    tokenizer->length   = length;
    tokenizer->position = 0;
}

bool tbox_css_tokenizer_next(tbox_css_tokenizer *tokenizer, tbox_css_token *out_token) {
    return tbox_css_tokenizer_next_impl(tokenizer, out_token);
}
