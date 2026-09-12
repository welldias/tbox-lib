#include <tbox/css_cascade.h>

#include <string.h>

#include "test_support.h"

static tbox_string_view view_cstr(const char *s) {
    return tbox_string_view_make(s, strlen(s));
}

static bool rgba_eq(tbox_css_rgba x, tbox_css_rgba y) {
    return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a;
}

int tbox_test_css_cascade_color_parse_run(void) {
    int failures = 0;

    /* 1: tbox_css_color_detect_format classifies each of the four shapes. */
    {
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("#6495ED")) == TBOX_CSS_COLOR_FORMAT_HEXA);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("rgb(100, 149, 237)")) == TBOX_CSS_COLOR_FORMAT_RGBA);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("RGBA(100, 149, 237, 0.5)")) == TBOX_CSS_COLOR_FORMAT_RGBA);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("hsl(219, 79%, 66%)")) == TBOX_CSS_COLOR_FORMAT_HSLA);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("HSLA(219, 79%, 66%, 0.5)")) == TBOX_CSS_COLOR_FORMAT_HSLA);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("CornflowerBlue")) == TBOX_CSS_COLOR_FORMAT_COLOR_NAME);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("cornflowerblue")) == TBOX_CSS_COLOR_FORMAT_COLOR_NAME);
    }

    /* 2: detect_format reports UNKNOWN for garbage and for an empty/NULL value. */
    {
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("notacolor")) == TBOX_CSS_COLOR_FORMAT_UNKNOWN);
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(view_cstr("")) == TBOX_CSS_COLOR_FORMAT_UNKNOWN);
        tbox_string_view null_view = { .data = NULL, .size = 0 };
        TBOX_TEST_ASSERT(tbox_css_color_detect_format(null_view) == TBOX_CSS_COLOR_FORMAT_UNKNOWN);
    }

    /* 3: tbox_css_rgb_to_rgba parses rgb() with plain integers, alpha defaulting to opaque. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgb(100, 149, 237)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 100, 149, 237, 255 }));
    }

    /* 4: tbox_css_rgb_to_rgba accepts rgba() with a numeric alpha, and percentages for R/G/B. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgba(100, 149, 237, 0.5)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 100, 149, 237, 128 }));

        tbox_css_rgba from_percent;
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgb(0%, 50%, 100%)"), &from_percent));
        TBOX_TEST_ASSERT(rgba_eq(from_percent, (tbox_css_rgba){ 0, 128, 255, 255 }));
    }

    /* 5: rgb()/rgba() are interchangeable regardless of argument count, and internal
     * whitespace around commas/parens is tolerated. */
    {
        tbox_css_rgba a, b;
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgba(10, 20, 30)"), &a));
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgb( 10 , 20,30 )"), &b));
        TBOX_TEST_ASSERT(rgba_eq(a, b));
        TBOX_TEST_ASSERT(rgba_eq(a, (tbox_css_rgba){ 10, 20, 30, 255 }));
    }

    /* 6: out-of-range R/G/B/A clamp instead of wrapping or failing. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_rgb_to_rgba(view_cstr("rgba(300, -10, 128, 2)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 255, 0, 128, 255 }));
    }

    /* 7: malformed rgb()/rgba() calls are rejected, leaving *out_color untouched. */
    {
        tbox_css_rgba color   = { 1, 2, 3, 4 };
        tbox_css_rgba unmoved = color;
        TBOX_TEST_ASSERT(!tbox_css_rgb_to_rgba(view_cstr("rgb(10, 20)"), &color));
        TBOX_TEST_ASSERT(!tbox_css_rgb_to_rgba(view_cstr("rgb(10, 20, 30, 0.5, 1)"), &color));
        TBOX_TEST_ASSERT(!tbox_css_rgb_to_rgba(view_cstr("rgb(10, 20, thirty)"), &color));
        TBOX_TEST_ASSERT(!tbox_css_rgb_to_rgba(view_cstr("rgb(10, 20, 30"), &color));
        TBOX_TEST_ASSERT(!tbox_css_rgb_to_rgba(view_cstr("hsl(10, 20%, 30%)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, unmoved));
    }

    /* 8: tbox_css_hsl_to_rgba parses hsl(), alpha defaulting to opaque. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hsl_to_rgba(view_cstr("hsl(0, 100%, 50%)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 255, 0, 0, 255 }));
    }

    /* 9: tbox_css_hsl_to_rgba accepts hsla() with a numeric or percentage alpha. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_hsl_to_rgba(view_cstr("hsla(0, 100%, 50%, 0.5)"), &color));
        TBOX_TEST_ASSERT(rgba_eq(color, (tbox_css_rgba){ 255, 0, 0, 128 }));

        tbox_css_rgba from_percent;
        TBOX_TEST_ASSERT(tbox_css_hsl_to_rgba(view_cstr("hsla(0, 100%, 50%, 50%)"), &from_percent));
        TBOX_TEST_ASSERT(rgba_eq(from_percent, (tbox_css_rgba){ 255, 0, 0, 128 }));
    }

    /* 10: hue must NOT carry '%'; saturation/lightness MUST carry '%'. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(!tbox_css_hsl_to_rgba(view_cstr("hsl(50%, 100%, 50%)"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hsl_to_rgba(view_cstr("hsl(0, 100, 50%)"), &color));
        TBOX_TEST_ASSERT(!tbox_css_hsl_to_rgba(view_cstr("hsl(0, 100%, 50)"), &color));
    }

    /* 11: tbox_css_color_parse dispatches to the right parser for every format. */
    {
        tbox_css_rgba hex, rgb, hsl, name;
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("#FF0000"), &hex));
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("rgb(255, 0, 0)"), &rgb));
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("hsl(0, 100%, 50%)"), &hsl));
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("Red"), &name));

        tbox_css_rgba expected = { 255, 0, 0, 255 };
        TBOX_TEST_ASSERT(rgba_eq(hex, expected));
        TBOX_TEST_ASSERT(rgba_eq(rgb, expected));
        TBOX_TEST_ASSERT(rgba_eq(hsl, expected));
        TBOX_TEST_ASSERT(rgba_eq(name, expected));
    }

    /* 12: tbox_css_color_parse fails for unknown text and for a detected-but-malformed value. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(!tbox_css_color_parse(view_cstr("notacolor"), &color));
        TBOX_TEST_ASSERT(!tbox_css_color_parse(view_cstr("#zz"), &color));
        TBOX_TEST_ASSERT(!tbox_css_color_parse(view_cstr("rgb(1, 2)"), &color));
    }

    return failures;
}
