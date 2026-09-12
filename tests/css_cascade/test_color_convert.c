#include <tbox/css_cascade.h>

#include <string.h>

#include "test_support.h"

static tbox_string_view view_cstr(const char *s) {
    return tbox_string_view_make(s, strlen(s));
}

static bool rgba_eq(tbox_css_rgba x, tbox_css_rgba y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

int tbox_test_css_cascade_color_convert_run(void) {
    int failures = 0;

    /* 1: 6-digit hex, opaque. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#6495ED"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 0x64, 0x95, 0xED, 0xFF }));
    }

    /* 2: 6-digit hex is lowercase/uppercase-insensitive. */
    {
        tbox_css_rgba lower, upper;
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#6495ed"), &lower));
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#6495ED"), &upper));
        TBOX_TEST_ASSERT(rgba_eq(lower, upper));
    }

    /* 3: 8-digit hex carries an explicit alpha byte. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#6495ED80"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 0x64, 0x95, 0xED, 0x80 }));
    }

    /* 4: 3-digit shorthand duplicates each nibble, alpha defaults to opaque. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#0af"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 0x00, 0xAA, 0xFF, 0xFF }));
    }

    /* 5: 4-digit shorthand duplicates each nibble, including alpha. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#0af8"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 0x00, 0xAA, 0xFF, 0x88 }));
    }

    /* 6: missing '#', wrong digit count, and non-hex characters are all rejected,
     * leaving *out_color untouched. */
    {
        tbox_css_rgba color   = { 1, 2, 3, 4 };
        tbox_css_rgba unmoved = color;
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("6495ED"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("#6495E"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("#6495EDF"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("#64G5ED"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("#"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, unmoved));
    }

    /* 7: hex.data == NULL is rejected rather than dereferenced. */
    {
        tbox_string_view null_view = { .data = NULL, .size = 0 };
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(null_view, NULL));
    }

    /* 8: out_color == NULL is allowed, for a plain validity check. */
    {
        TBOX_TEST_ASSERT(tbox_css_hex_to_rgba(view_cstr("#000"), NULL));
        TBOX_TEST_ASSERT(!tbox_css_hex_to_rgba(view_cstr("bogus"), NULL));
    }

    /* 9: pure red/green/blue/black/white/gray, the textbook HSL corners. */
    {
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, 1.0 }), (tbox_css_rgba){ 0xFF, 0x00, 0x00, 0xFF }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 120, 1.0, 0.5, 1.0 }), (tbox_css_rgba){ 0x00, 0xFF, 0x00, 0xFF }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 240, 1.0, 0.5, 1.0 }), (tbox_css_rgba){ 0x00, 0x00, 0xFF, 0xFF }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 0.0, 0.0, 1.0 }), (tbox_css_rgba){ 0x00, 0x00, 0x00, 0xFF }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 0.0, 1.0, 1.0 }), (tbox_css_rgba){ 0xFF, 0xFF, 0xFF, 0xFF }));
        TBOX_TEST_ASSERT(rgba_eq(tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 0.0, 0.5, 1.0 }), (tbox_css_rgba){ 0x80, 0x80, 0x80, 0xFF }));
    }

    /* 10: alpha passes through, scaled to a byte. */
    {
        tbox_css_rgba color = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, 0.5 });
        TBOX_TEST_ASSERT(color.r == 0xFF && color.g == 0x00 && color.b == 0x00 && color.a == 0x80);
    }

    /* 11: hue wraps modulo 360, including negative hues. */
    {
        tbox_css_rgba at_0        = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, 1.0 });
        tbox_css_rgba at_360      = tbox_css_hsla_to_rgba((tbox_css_hsla){ 360, 1.0, 0.5, 1.0 });
        tbox_css_rgba at_neg_360  = tbox_css_hsla_to_rgba((tbox_css_hsla){ -360, 1.0, 0.5, 1.0 });
        tbox_css_rgba blue_270    = tbox_css_hsla_to_rgba((tbox_css_hsla){ 270, 1.0, 0.5, 1.0 });
        tbox_css_rgba blue_neg_90 = tbox_css_hsla_to_rgba((tbox_css_hsla){ -90, 1.0, 0.5, 1.0 });
        TBOX_TEST_ASSERT(rgba_eq(at_0, at_360));
        TBOX_TEST_ASSERT(rgba_eq(at_0, at_neg_360));
        TBOX_TEST_ASSERT(rgba_eq(blue_270, blue_neg_90));
    }

    /* 12: out-of-range s/l/a clamp into [0, 1] rather than producing garbage. */
    {
        tbox_css_rgba over  = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.5, 0.5, 1.0 });
        tbox_css_rgba clamp = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, 1.0 });
        TBOX_TEST_ASSERT(rgba_eq(over, clamp));

        tbox_css_rgba negative_alpha = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, -1.0 });
        TBOX_TEST_ASSERT(negative_alpha.a == 0x00);
    }

    /* 13: tbox_css_hsla_to_rgba's output round-trips through tbox_css_named_color_name
     * for a color known to be one of the 140 keywords (Red). */
    {
        tbox_css_rgba red = tbox_css_hsla_to_rgba((tbox_css_hsla){ 0, 1.0, 0.5, 1.0 });
        TBOX_TEST_ASSERT(tbox_css_named_color_name(red).size == 3);
    }

    return failures;
}
