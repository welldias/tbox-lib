#include "base/tbox_bidi.h"

#include <string.h>

#include "test_support.h"

/* Resolves `text` and compares levels with `expected` (one per codepoint;
 * removed characters are skipped with 'x'). */
static bool levels_equal(const uint32_t *text, size_t count, uint8_t paragraph, const char *expected, uint8_t expected_paragraph) {
    uint8_t levels[32];
    uint8_t used = 0xFF;
    if (!tbox_bidi_resolve(text, count, paragraph, levels, &used) || used != expected_paragraph)
        return false;
    tbox_bidi_reset_line(text, 0, count, used, levels);
    for (size_t i = 0; i < count; i++)
        if (expected[i] != 'x' && levels[i] != (uint8_t)(expected[i] - '0'))
            return false;
    return strlen(expected) == count;
}

int tbox_test_bidi_run(void) {
    int failures = 0;

    /* 1: classes and mirrors from the generated tables. */
    {
        TBOX_TEST_ASSERT(tbox_bidi_class_of('a') == TBOX_BIDI_L);
        TBOX_TEST_ASSERT(tbox_bidi_class_of(0x05D0) == TBOX_BIDI_R);  /* alef */
        TBOX_TEST_ASSERT(tbox_bidi_class_of(0x0627) == TBOX_BIDI_AL); /* arabic alef */
        TBOX_TEST_ASSERT(tbox_bidi_class_of('7') == TBOX_BIDI_EN);
        TBOX_TEST_ASSERT(tbox_bidi_class_of(0x0663) == TBOX_BIDI_AN);
        TBOX_TEST_ASSERT(tbox_bidi_class_of(' ') == TBOX_BIDI_WS);
        TBOX_TEST_ASSERT(tbox_bidi_class_of('(') == TBOX_BIDI_ON);
        TBOX_TEST_ASSERT(tbox_bidi_class_of(0x05FF) == TBOX_BIDI_R); /* unassigned, Hebrew block default */
        TBOX_TEST_ASSERT(tbox_bidi_class_of(TBOX_BIDI_CHAR_RLI) == TBOX_BIDI_RLI);
        TBOX_TEST_ASSERT(tbox_bidi_class_of(0x10FFFF) == TBOX_BIDI_BN);
        TBOX_TEST_ASSERT(tbox_bidi_mirror('(') == ')' && tbox_bidi_mirror(']') == '[');
        TBOX_TEST_ASSERT(tbox_bidi_mirror('<') == '>' && tbox_bidi_mirror(0x00AB) == 0x00BB);
        TBOX_TEST_ASSERT(tbox_bidi_mirror('a') == 'a');
    }

    /* 2: implicit levels, numbers and neutrals. */
    {
        const uint32_t hebrew_number[] = { 0x05D0, 0x05D1, ' ', '1', '2', ' ', 'a', 'b' };
        TBOX_TEST_ASSERT(levels_equal(hebrew_number, 8, 0, "11122000", 0));
        TBOX_TEST_ASSERT(levels_equal(hebrew_number, 8, 1, "11122122", 1));
        TBOX_TEST_ASSERT(levels_equal(hebrew_number, 8, TBOX_BIDI_LEVEL_AUTO, "11122122", 1));
        const uint32_t arabic_digits[] = { 0x0627, ' ', '1', '2' }; /* W2: EN after AL is AN */
        TBOX_TEST_ASSERT(levels_equal(arabic_digits, 4, 0, "1122", 0));
        const uint32_t trailing[] = { 'a', ' ', 0x05D0, ' ', ' ' }; /* L1: trailing spaces */
        TBOX_TEST_ASSERT(levels_equal(trailing, 5, 0, "00100", 0));
    }

    /* 3: N0 brackets, explicit embeddings, overrides and isolates. */
    {
        const uint32_t brackets[] = { 'a', '(', 'b', ')', 0x05D0 }; /* rtl paragraph */
        TBOX_TEST_ASSERT(levels_equal(brackets, 5, 1, "22221", 1));
        const uint32_t brackets_rtl[] = { 0x05D0, '(', 'b', ')', 'c' }; /* ltr paragraph: pair takes e */
        TBOX_TEST_ASSERT(levels_equal(brackets_rtl, 5, 0, "10000", 0));
        const uint32_t override[] = { TBOX_BIDI_CHAR_RLO, 'a', 'b', TBOX_BIDI_CHAR_PDF, 'c' };
        TBOX_TEST_ASSERT(levels_equal(override, 5, 0, "x11x0", 0));
        const uint32_t embed[] = { 'a', TBOX_BIDI_CHAR_RLE, 'b', TBOX_BIDI_CHAR_PDF };
        TBOX_TEST_ASSERT(levels_equal(embed, 4, 0, "0x2x", 0));
        /* An isolate keeps the number after it out of the Hebrew run. */
        const uint32_t normal[]  = { 'u', ' ', 0x05D0, ' ', '5' };
        const uint32_t isolate[] = { 'u', ' ', TBOX_BIDI_CHAR_RLI, 0x05D0, TBOX_BIDI_CHAR_PDI, ' ', '5' };
        TBOX_TEST_ASSERT(levels_equal(normal, 5, 0, "00112", 0));
        TBOX_TEST_ASSERT(levels_equal(isolate, 7, 0, "0001000", 0));
        const uint32_t first_strong[] = { TBOX_BIDI_CHAR_FSI, ' ', 0x05D0, TBOX_BIDI_CHAR_PDI, 'a' };
        TBOX_TEST_ASSERT(tbox_bidi_first_strong_level(first_strong, 5, 7) == 0); /* the isolate is skipped */
        TBOX_TEST_ASSERT(tbox_bidi_first_strong_level(first_strong + 1, 2, 7) == 1);
        TBOX_TEST_ASSERT(tbox_bidi_first_strong_level(first_strong + 1, 1, 7) == 7);
        TBOX_TEST_ASSERT(levels_equal(first_strong, 5, 0, "01100", 0));
    }

    /* 4: L2 visual order. */
    {
        const uint8_t levels[] = { 0, 1, 1, 2, 2, 1, 0 };
        size_t order[7];
        tbox_bidi_reorder(levels, 7, order);
        const size_t expected[] = { 0, 5, 3, 4, 2, 1, 6 };
        TBOX_TEST_ASSERT(memcmp(order, expected, sizeof(expected)) == 0);
        const uint8_t flat[] = { 0, 0 };
        tbox_bidi_reorder(flat, 2, order);
        TBOX_TEST_ASSERT(order[0] == 0 && order[1] == 1);
        tbox_bidi_reorder(NULL, 0, order); /* empty is fine */
    }

    return failures;
}
