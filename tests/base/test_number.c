#include "base/tbox_number.h"

#include <locale.h>
#include <math.h>
#include <string.h>

#include "style/tbox_style_internal.h"
#include "test_support.h"

static bool scan_equals(const char *text, size_t expected_used, double expected) {
    double value = -12345.0;
    size_t used  = tbox_number_scan(tbox_string_view_make(text, strlen(text)), &value);
    return used == expected_used && (expected_used == 0 || value == expected);
}

static bool format_equals(double value, int precision, const char *expected) {
    char buffer[64];
    int length = tbox_number_format(buffer, sizeof(buffer), value, precision);
    return length == (int)strlen(expected) && strcmp(buffer, expected) == 0;
}

int tbox_test_number_run(void) {
    int failures = 0;

    /* 1: CSS <number> grammar; the exact fast path matches C literals. */
    {
        TBOX_TEST_ASSERT(scan_equals("10", 2, 10.0));
        TBOX_TEST_ASSERT(scan_equals("-3.5", 4, -3.5));
        TBOX_TEST_ASSERT(scan_equals("+.5", 3, 0.5));
        TBOX_TEST_ASSERT(scan_equals("0.1", 3, 0.1));
        TBOX_TEST_ASSERT(scan_equals("1e3", 3, 1000.0));
        TBOX_TEST_ASSERT(scan_equals("2.5E-2", 6, 0.025));
        TBOX_TEST_ASSERT(scan_equals("1.5em", 3, 1.5));   /* 'e' without digits is a unit */
        TBOX_TEST_ASSERT(scan_equals("3.px", 1, 3.0));    /* "3." is not a number */
        TBOX_TEST_ASSERT(scan_equals("0.000001", 8, 0.000001));
        double big = 0.0; /* beyond 19 digits: within a few ulps */
        TBOX_TEST_ASSERT(tbox_number_scan(tbox_string_view_make("123456789012345678901234", 24), &big) == 24 &&
                         fabs(big - 123456789012345678901234.0) <= 123456789012345678901234.0 * 1e-15);
        TBOX_TEST_ASSERT(scan_equals("", 0, 0.0));
        TBOX_TEST_ASSERT(scan_equals(".", 0, 0.0));
        TBOX_TEST_ASSERT(scan_equals("-", 0, 0.0));
        TBOX_TEST_ASSERT(scan_equals("inf", 0, 0.0));
        TBOX_TEST_ASSERT(scan_equals("nan", 0, 0.0));
        TBOX_TEST_ASSERT(scan_equals(" 1", 0, 0.0));
        double value;
        TBOX_TEST_ASSERT(tbox_number_parse(tbox_string_view_make("42.25", 5), &value) && value == 42.25);
        TBOX_TEST_ASSERT(!tbox_number_parse(tbox_string_view_make("42px", 4), &value));
        TBOX_TEST_ASSERT(!tbox_number_parse(tbox_string_view_make("1e999", 5), &value)); /* not finite */
    }

    /* 2: formatting is %.*g with '.' always. */
    {
        TBOX_TEST_ASSERT(format_equals(1.5, 15, "1.5"));
        TBOX_TEST_ASSERT(format_equals(-0.25, 6, "-0.25"));
        TBOX_TEST_ASSERT(format_equals(100.0, 6, "100"));
        TBOX_TEST_ASSERT(format_equals(1e-7, 6, "1e-07"));
        char small[4];
        TBOX_TEST_ASSERT(tbox_number_format(small, sizeof(small), 1.25, 6) == 4 && strcmp(small, "1.2") == 0);
    }

    /* 3: a decimal-comma LC_NUMERIC changes neither reading nor writing.
     * Skipped when the machine has no such locale installed. */
    {
        const char *previous = setlocale(LC_NUMERIC, NULL);
        char saved[64];
        strncpy(saved, previous != NULL ? previous : "C", sizeof(saved) - 1);
        saved[sizeof(saved) - 1] = '\0';
        if (setlocale(LC_NUMERIC, "pt_BR.UTF-8") != NULL || setlocale(LC_NUMERIC, "de_DE.UTF-8") != NULL) {
            double value = 0.0;
            TBOX_TEST_ASSERT(tbox_number_parse(tbox_string_view_make("1.5", 3), &value) && value == 1.5);
            TBOX_TEST_ASSERT(tbox_style_parse_number(tbox_string_view_make("0.75", 4), &value) && value == 0.75);
            TBOX_TEST_ASSERT(format_equals(1.5, 15, "1.5"));
            TBOX_TEST_ASSERT(format_equals(0.5, 17, "0.5"));
        }
        setlocale(LC_NUMERIC, saved);
    }

    return failures;
}
