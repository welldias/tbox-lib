#include "html_parser/tbox_html_tokenizer.h"

#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

int tbox_test_html_parser_tokenizer_run(void) {
    int failures = 0;

    /* 1: plain text. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "hello";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT);
        TBOX_TEST_ASSERT(text_eq(token.text, "hello"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_EOF);
        tbox_arena_destroy(&arena);
    }

    /* 2: simple void-looking tag, no attributes. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<br>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG);
        TBOX_TEST_ASSERT(text_eq(token.text, "br"));
        TBOX_TEST_ASSERT(token.attribute_count == 0);
        TBOX_TEST_ASSERT(!token.self_closing);
        tbox_arena_destroy(&arena);
    }

    /* 3: open/text/close. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<p>hi</p>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG && text_eq(token.text, "p"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT && text_eq(token.text, "hi"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_END_TAG && text_eq(token.text, "p"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_EOF);
        tbox_arena_destroy(&arena);
    }

    /* 4: attributes with double/single/unquoted/boolean forms. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<a href=\"x\" target='y' disabled foo=bar>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG);
        TBOX_TEST_ASSERT(token.attribute_count == 4);
        TBOX_TEST_ASSERT(text_eq(token.attributes[0].name, "href") && text_eq(token.attributes[0].value, "x"));
        TBOX_TEST_ASSERT(text_eq(token.attributes[1].name, "target") && text_eq(token.attributes[1].value, "y"));
        TBOX_TEST_ASSERT(text_eq(token.attributes[2].name, "disabled") && token.attributes[2].value.size == 0);
        TBOX_TEST_ASSERT(text_eq(token.attributes[3].name, "foo") && text_eq(token.attributes[3].value, "bar"));
        tbox_arena_destroy(&arena);
    }

    /* 5: self-closing tag. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<img src=\"a.png\"/>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG);
        TBOX_TEST_ASSERT(token.self_closing);
        tbox_arena_destroy(&arena);
    }

    /* 6: comment, raw content preserved verbatim. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<!-- a -- b -->";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_COMMENT);
        TBOX_TEST_ASSERT(text_eq(token.text, " a -- b "));
        tbox_arena_destroy(&arena);
    }

    /* 7: doctype, case-insensitive keyword, verbatim name. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<!DOCTYPE html><!doctype HTML>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_DOCTYPE && text_eq(token.text, "html"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_DOCTYPE && text_eq(token.text, "HTML"));
        tbox_arena_destroy(&arena);
    }

    /* 8: raw text mode for <script>. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<script>if (a < b) { }</script>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG && text_eq(token.text, "script"));
        tbox_html_tokenizer_enter_raw_text(&tokenizer, token.text);

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT);
        TBOX_TEST_ASSERT(text_eq(token.text, "if (a < b) { }"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_END_TAG && text_eq(token.text, "script"));
        tbox_arena_destroy(&arena);
    }

    /* 9: false positive close tag inside raw text. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<script>a</scriptX>b</script>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token); /* start tag */
        tbox_html_tokenizer_enter_raw_text(&tokenizer, token.text);

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT);
        TBOX_TEST_ASSERT(text_eq(token.text, "a</scriptX>b"));

        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_END_TAG && text_eq(token.text, "script"));
        tbox_arena_destroy(&arena);
    }

    /* 10: interleaved text/tags. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "a<b>c</b>d";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT && text_eq(token.text, "a"));
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_START_TAG && text_eq(token.text, "b"));
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT && text_eq(token.text, "c"));
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_END_TAG && text_eq(token.text, "b"));
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_TEXT && text_eq(token.text, "d"));
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_EOF);
        tbox_arena_destroy(&arena);
    }

    /* 11: empty input. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        tbox_html_tokenizer_init(&tokenizer, "", 0, &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_EOF);
        tbox_arena_destroy(&arena);
    }

    /* 12: UTF-8 multi-byte attribute value. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<p data-x=\"caf\xc3\xa9\">";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(token.attributes[0].value, "caf\xc3\xa9"));
        tbox_arena_destroy(&arena);
    }

    /* 13: irregular whitespace around '=' and '>'. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<a  href = \"x\" >";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(token.attributes[0].name, "href") && text_eq(token.attributes[0].value, "x"));
        tbox_arena_destroy(&arena);
    }

    /* 14: malformed declaration falls back to a bogus comment. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_html_tokenizer tokenizer;
        const char *src = "<!weird>";
        tbox_html_tokenizer_init(&tokenizer, src, strlen(src), &arena);

        tbox_html_token token;
        tbox_html_tokenizer_next(&tokenizer, &token);
        TBOX_TEST_ASSERT(token.type == TBOX_HTML_TOKEN_COMMENT);
        TBOX_TEST_ASSERT(text_eq(token.text, "weird"));
        tbox_arena_destroy(&arena);
    }

    return failures;
}
