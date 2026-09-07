#include "base/tbox_string.h"

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

    return failures;
}
