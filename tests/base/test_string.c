#include "base/tbox_string.h"

#include <string.h>

#include "base/tbox_arena.h"
#include "test_support.h"

int tbox_test_string_run(void) {
    int failures = 0;

    /* 1: equal. */
    {
        tbox_string_view a = tbox_string_view_make("hello", 5);
        tbox_string_view b = tbox_string_view_make("hello", 5);
        tbox_string_view c = tbox_string_view_make("hellp", 5);
        tbox_string_view d = tbox_string_view_make("hell", 4);
        TBOX_TEST_ASSERT(tbox_string_view_equal(a, b));
        TBOX_TEST_ASSERT(!tbox_string_view_equal(a, c));
        TBOX_TEST_ASSERT(!tbox_string_view_equal(a, d));
    }

    /* 2: from_cstr uses byte size, not codepoint count. */
    {
        tbox_string_view view = tbox_string_view_from_cstr("caf\xc3\xa9"); /* "café" */
        TBOX_TEST_ASSERT(view.size == 5);                                  /* c, a, f, 0xC3, 0xA9 */
    }

    /* 3: codepoint_count. */
    {
        tbox_string_view ascii = tbox_string_view_from_cstr("abcdef");
        TBOX_TEST_ASSERT(tbox_string_view_codepoint_count(ascii) == ascii.size);

        tbox_string_view multibyte = tbox_string_view_from_cstr("caf\xc3\xa9");
        TBOX_TEST_ASSERT(tbox_string_view_codepoint_count(multibyte) < multibyte.size);
        TBOX_TEST_ASSERT(tbox_string_view_codepoint_count(multibyte) == 4);
    }

    /* 4: valid_utf8. */
    {
        tbox_string_view valid = tbox_string_view_from_cstr("caf\xc3\xa9");
        TBOX_TEST_ASSERT(tbox_string_view_valid_utf8(valid));

        char invalid_bytes[] = {'a', (char)0xC3, 'b'}; /* truncated 2-byte sequence */
        tbox_string_view invalid = tbox_string_view_make(invalid_bytes, sizeof(invalid_bytes));
        TBOX_TEST_ASSERT(!tbox_string_view_valid_utf8(invalid));
    }

    /* 5: equal_ascii_ci. */
    {
        tbox_string_view a = tbox_string_view_from_cstr("Script");
        tbox_string_view b = tbox_string_view_from_cstr("SCRIPT");
        tbox_string_view c = tbox_string_view_from_cstr("scripts");
        TBOX_TEST_ASSERT(tbox_string_view_equal_ascii_ci(a, b));
        TBOX_TEST_ASSERT(!tbox_string_view_equal_ascii_ci(a, c));
    }

    /* 6: builder append_view + finish. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, &arena, 0);
        tbox_string_builder_append_view(&builder, tbox_string_view_from_cstr("Hello, "));
        tbox_string_builder_append_view(&builder, tbox_string_view_from_cstr("world!"));
        tbox_string_view result = tbox_string_builder_finish(&builder);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "Hello, world!"));
        tbox_arena_destroy(&arena);
    }

    /* 7: append_view_lower_ascii only folds ASCII, multi-byte bytes pass
     * through untouched. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, &arena, 0);
        tbox_string_builder_append_view_lower_ascii(&builder, tbox_string_view_from_cstr("CAF\xc3\x89")); /* "CAFÉ" */
        tbox_string_view result = tbox_string_builder_finish(&builder);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "caf\xc3\x89"));
        tbox_arena_destroy(&arena);
    }

    /* 8: builder growth preserves earlier content. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, &arena, 4);
        for (int i = 0; i < 100; i++) {
            tbox_string_builder_append_byte(&builder, 'a');
        }
        tbox_string_view result = tbox_string_builder_finish(&builder);
        TBOX_TEST_ASSERT(result.size == 100);
        bool all_a = true;
        for (size_t i = 0; i < result.size; i++) {
            if (result.data[i] != 'a') {
                all_a = false;
            }
        }
        TBOX_TEST_ASSERT(all_a);
        tbox_arena_destroy(&arena);
    }

    /* 9: append_codepoint handles 1..4 byte encodings. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, &arena, 0);
        tbox_string_builder_append_codepoint(&builder, 0x24);    /* '$'  (1 byte) */
        tbox_string_builder_append_codepoint(&builder, 0xA2);    /* '¢'  (2 bytes) */
        tbox_string_builder_append_codepoint(&builder, 0x20AC);  /* '€'  (3 bytes) */
        tbox_string_builder_append_codepoint(&builder, 0x10348); /* 𐍈    (4 bytes) */
        tbox_string_view result = tbox_string_builder_finish(&builder);
        TBOX_TEST_ASSERT(result.size == 1 + 2 + 3 + 4);
        tbox_arena_destroy(&arena);
    }

    /* 10: collapse_whitespace -- no extra whitespace is unchanged. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result = tbox_string_collapse_whitespace(&arena, tbox_string_view_from_cstr("hello world"));
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "hello world"));
        tbox_arena_destroy(&arena);
    }

    /* 11: collapse_whitespace -- multiple internal spaces collapse to one. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result =
            tbox_string_collapse_whitespace(&arena, tbox_string_view_from_cstr("hello   world"));
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "hello world"));
        tbox_arena_destroy(&arena);
    }

    /* 12: collapse_whitespace -- tabs and newlines are whitespace too. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result =
            tbox_string_collapse_whitespace(&arena, tbox_string_view_from_cstr("hello\t\nworld"));
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "hello world"));
        tbox_arena_destroy(&arena);
    }

    /* 13: collapse_whitespace -- leading/trailing whitespace is stripped. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result =
            tbox_string_collapse_whitespace(&arena, tbox_string_view_from_cstr("  hello world  "));
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(result, "hello world"));
        tbox_arena_destroy(&arena);
    }

    /* 14: collapse_whitespace -- empty string stays empty. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result = tbox_string_collapse_whitespace(&arena, tbox_string_view_make("", 0));
        TBOX_TEST_ASSERT(result.size == 0);
        tbox_arena_destroy(&arena);
    }

    /* 15: collapse_whitespace -- string that is only whitespace becomes empty. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_string_view result =
            tbox_string_collapse_whitespace(&arena, tbox_string_view_from_cstr("   \t\n  "));
        TBOX_TEST_ASSERT(result.size == 0);
        tbox_arena_destroy(&arena);
    }

    /* 16: tbox_utf8_decode -- valid sequences, then one U+FFFD per maximal
     * invalid subpart, never reading past the view. */
    {
        const char text[] = "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80";
        size_t size       = sizeof(text) - 1;
        uint32_t cp;
        size_t at = tbox_utf8_decode(text, size, 0, &cp);
        TBOX_TEST_ASSERT(at == 1 && cp == 'a');
        at = tbox_utf8_decode(text, size, at, &cp);
        TBOX_TEST_ASSERT(at == 3 && cp == 0xE9);
        at = tbox_utf8_decode(text, size, at, &cp);
        TBOX_TEST_ASSERT(at == 6 && cp == 0x20AC);
        at = tbox_utf8_decode(text, size, at, &cp);
        TBOX_TEST_ASSERT(at == 10 && cp == 0x1F600);

        /* Truncated "€" at the end of the view: E2 82 is one error. */
        TBOX_TEST_ASSERT(tbox_utf8_decode("\xe2\x82\xac", 2, 0, &cp) == 2 && cp == 0xFFFD);
        TBOX_TEST_ASSERT(tbox_utf8_decode("\x80", 1, 0, &cp) == 1 && cp == 0xFFFD);
        TBOX_TEST_ASSERT(tbox_utf8_decode("\xc0\xaf", 2, 0, &cp) == 1 && cp == 0xFFFD);  /* overlong */
        TBOX_TEST_ASSERT(tbox_utf8_decode("\xed\xa0\x80", 3, 0, &cp) == 1 && cp == 0xFFFD); /* surrogate */
        TBOX_TEST_ASSERT(tbox_utf8_decode("\xf4\x90\x80\x80", 4, 0, &cp) == 1 && cp == 0xFFFD); /* > U+10FFFF */
    }

    /* 17: tbox_utf8_next/previous always make progress, also on bad bytes. */
    {
        const char text[] = "a\xffz\xc3\xa9";
        size_t size       = sizeof(text) - 1;
        TBOX_TEST_ASSERT(tbox_utf8_next(text, size, 0) == 1);
        TBOX_TEST_ASSERT(tbox_utf8_next(text, size, 1) == 2);
        TBOX_TEST_ASSERT(tbox_utf8_next(text, size, 3) == 5);
        TBOX_TEST_ASSERT(tbox_utf8_next(text, size, 5) == 5);
        TBOX_TEST_ASSERT(tbox_utf8_previous(text, 5) == 3);
        TBOX_TEST_ASSERT(tbox_utf8_previous(text, 3) == 2);
        TBOX_TEST_ASSERT(tbox_utf8_previous(text, 2) == 1);
        TBOX_TEST_ASSERT(tbox_utf8_previous(text, 0) == 0);
        TBOX_TEST_ASSERT(tbox_string_view_codepoint_count(tbox_string_view_make(text, size)) == 4);
    }

    /* 18: tbox_utf8_sanitize -- valid text is returned as is (no copy);
     * malformed sequences become U+FFFD. */
    {
        tbox_arena arena       = tbox_arena_create(0);
        tbox_string_view valid = tbox_string_view_from_cstr("caf\xc3\xa9");
        TBOX_TEST_ASSERT(tbox_utf8_sanitize(&arena, valid).data == valid.data);
        tbox_string_view fixed = tbox_utf8_sanitize(&arena, tbox_string_view_make("a\xff" "b\xe2\x82", 5));
        TBOX_TEST_ASSERT(fixed.size == 8 && memcmp(fixed.data, "a\xef\xbf\xbd" "b\xef\xbf\xbd", 8) == 0);
        TBOX_TEST_ASSERT(tbox_string_view_valid_utf8(fixed));
        tbox_arena_destroy(&arena);
    }

    return failures;
}
