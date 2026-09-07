#include "css_parser/tbox_css_tokenizer.h"

#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

int tbox_test_css_parser_tokenizer_run(void) {
    int failures = 0;

    /* 1: empty input. */
    {
        tbox_css_tokenizer tokenizer;
        tbox_css_tokenizer_init(&tokenizer, "", 0);

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EOF);
    }

    /* 2: plain ident. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "div";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "div"));

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EOF);
    }

    /* 3: whitespace collapses to one S token. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "  \t\n  div";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "div"));
    }

    /* 4: comments behave exactly like whitespace. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "/* c */div";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "div"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "/* only */";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);
        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EOF);
    }

    /* 5: a no-space comment still separates tokens (proves it always yields
     * an S, preventing accidental token fusion). */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "a/**/b";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "a"));

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "b"));
    }

    /* 6: HASH, and the fallback to DELIM when no ident-chars follow. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "#foo";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_HASH && text_eq(token.text, "foo"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "# ";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DELIM && text_eq(token.text, "#"));
        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);
    }

    /* 7: '.' vs. a leading-dot number. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = ".foo";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DOT);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "foo"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = ".5";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_NUMBER && text_eq(token.text, ".5"));

        tbox_css_tokenizer tokenizer3;
        const char *src3 = ". 5";
        tbox_css_tokenizer_init(&tokenizer3, src3, strlen(src3));

        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DOT);
        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);
        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_NUMBER && text_eq(token.text, "5"));
    }

    /* 8: strings, both quote styles, embedded escaped quote preserved raw. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "\"a\\\"b\"";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_STRING && text_eq(token.text, "a\\\"b"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "'x'";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_STRING && text_eq(token.text, "x"));
    }

    /* 9: an unterminated string runs safely to EOF (no hang/OOB). */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "\"abc";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_STRING);
        TBOX_TEST_ASSERT(tokenizer.position == tokenizer.length);

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EOF);
    }

    /* 10: FUNCTION, and that tokenizing resumes correctly after its
     * embedded '('. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "lang(en)";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_FUNCTION && text_eq(token.text, "lang"));

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "en"));

        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_RPAREN);
    }

    /* 11: url() atomic token -- unquoted, quoted, and whitespace-trimmed
     * forms. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "url(a.png)";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_URL && text_eq(token.text, "a.png"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "url(\"a b.png\")";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_URL && text_eq(token.text, "a b.png"));

        tbox_css_tokenizer tokenizer3;
        const char *src3 = "url( a.png )";
        tbox_css_tokenizer_init(&tokenizer3, src3, strlen(src3));

        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_URL && text_eq(token.text, "a.png"));
    }

    /* 12: AT_KEYWORD, and the fallback when '@' isn't followed by an
     * ident-start. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "@media";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_AT_KEYWORD && text_eq(token.text, "media"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "@ ";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DELIM && text_eq(token.text, "@"));
        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);
    }

    /* 13: CDO/CDC, and near-misses falling back to DELIM. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "<!--";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_CDO);

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "-->";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_CDC);

        tbox_css_tokenizer tokenizer3;
        const char *src3 = "<-";
        tbox_css_tokenizer_init(&tokenizer3, src3, strlen(src3));

        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DELIM && text_eq(token.text, "<"));
        tbox_css_tokenizer_next(&tokenizer3, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_DELIM && text_eq(token.text, "-"));
    }

    /* 14: every single-char punctuation token in one sweep. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "{ } ( ) [ ] : ; , > + ~ * = |";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token_type expected[] = {
            TBOX_CSS_TOKEN_LBRACE, TBOX_CSS_TOKEN_RBRACE, TBOX_CSS_TOKEN_LPAREN, TBOX_CSS_TOKEN_RPAREN,
            TBOX_CSS_TOKEN_LBRACKET, TBOX_CSS_TOKEN_RBRACKET, TBOX_CSS_TOKEN_COLON, TBOX_CSS_TOKEN_SEMICOLON,
            TBOX_CSS_TOKEN_COMMA, TBOX_CSS_TOKEN_GT, TBOX_CSS_TOKEN_PLUS, TBOX_CSS_TOKEN_TILDE,
            TBOX_CSS_TOKEN_STAR, TBOX_CSS_TOKEN_EQUALS, TBOX_CSS_TOKEN_PIPE,
        };

        tbox_css_token token;
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_tokenizer_next(&tokenizer, &token);
            TBOX_TEST_ASSERT(token.type == expected[i]);
            if (i + 1 < sizeof(expected) / sizeof(expected[0])) {
                tbox_css_tokenizer_next(&tokenizer, &token);
                TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S);
            }
        }
    }

    /* 15: attribute-selector-shaped input tokenizes as independent pieces
     * (operator composition is the parser's job, not the tokenizer's). */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "[href~=\"x\"]";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_LBRACKET);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "href"));
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_TILDE);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EQUALS);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_STRING && text_eq(token.text, "x"));
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_RBRACKET);
    }

    /* 16: numbers don't fuse with a trailing unit. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "10px";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_NUMBER && text_eq(token.text, "10"));
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "px"));

        tbox_css_tokenizer tokenizer2;
        const char *src2 = "3.14";
        tbox_css_tokenizer_init(&tokenizer2, src2, strlen(src2));

        tbox_css_tokenizer_next(&tokenizer2, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_NUMBER && text_eq(token.text, "3.14"));
    }

    /* 17: token offsets are correct -- the parser's raw-value slicing
     * depends on this. */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "  a:b";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_S && token.offset == 0);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && token.offset == 2);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_COLON && token.offset == 3);
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && token.offset == 4);
    }

    /* 18: a backslash escape keeps an identifier from terminating early
     * (the documented "structural only" escape simplification). */
    {
        tbox_css_tokenizer tokenizer;
        const char *src = "a\\:b";
        tbox_css_tokenizer_init(&tokenizer, src, strlen(src));

        tbox_css_token token;
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_IDENT && text_eq(token.text, "a\\:b"));
        tbox_css_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_CSS_TOKEN_EOF);
    }

    return failures;
}
