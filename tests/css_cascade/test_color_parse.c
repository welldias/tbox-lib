#include <tbox/css_cascade.h>

#include <stdio.h>
#include <stdlib.h>
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

    /* Hue routes select visibly different arcs. Expected colors are
     * analytic OKLCH midpoints; tolerate the existing RGBA8 input rounding. */
    {
        static const struct { const char *method, *expected; } cases[] = {
            { "shorter", "oklch(0.65 0.08 60)" },
            { "longer", "oklch(0.65 0.08 240)" },
            { "increasing", "oklch(0.65 0.08 60)" },
            { "decreasing", "oklch(0.65 0.08 240)" },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char text[256];
            snprintf(text, sizeof(text), "color-mix(in oklch %s hue, oklch(0.65 0.08 30), oklch(0.65 0.08 90))", cases[i].method);
            tbox_css_rgba actual, expected;
            TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr(text), &actual));
            TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr(cases[i].expected), &expected));
            TBOX_TEST_ASSERT_MSG(abs((int)actual.r - expected.r) <= 3 && abs((int)actual.g - expected.g) <= 3 && abs((int)actual.b - expected.b) <= 3, cases[i].method);
            TBOX_TEST_ASSERT(actual.a == 255);
        }
        tbox_css_rgba actual, expected;
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("color-mix(in oklch longer hue, oklch(0.65 0.08 30), oklch(0.65 0.08 30))"), &actual));
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("oklch(0.65 0.08 210)"), &expected));
        TBOX_TEST_ASSERT(abs((int)actual.r - expected.r) <= 3 && abs((int)actual.g - expected.g) <= 3 && abs((int)actual.b - expected.b) <= 3);
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("color-mix(in oklch increasing hue, oklch(0.65 0.08 30 / 50%), oklch(0.65 0.08 90))"), &actual));
        TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr("oklch(0.65 0.08 60 / 75%)"), &expected));
        TBOX_TEST_ASSERT(abs((int)actual.r - expected.r) <= 3 && abs((int)actual.g - expected.g) <= 3 && abs((int)actual.b - expected.b) <= 3 && abs((int)actual.a - expected.a) <= 1);
        static const char *const invalid[] = {
            "color-mix(in oklch sideways hue, red, blue)",
            "color-mix(in oklch longer, red, blue)",
            "color-mix(in oklch longer hue extra, red, blue)",
            "color-mix(in srgb longer hue, red, blue)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            actual = (tbox_css_rgba){ 1, 2, 3, 4 };
            TBOX_TEST_ASSERT(!tbox_css_color_parse(view_cstr(invalid[i]), &actual));
            TBOX_TEST_ASSERT(rgba_eq(actual, (tbox_css_rgba){ 1, 2, 3, 4 }));
        }
    }

    /* Explicit sRGB and linear sRGB use different transfer curves. */
    {
        static const struct { const char *css; tbox_css_rgba expected; } cases[] = {
            { "color(srgb 1 0.5 0 / 25%)", { 255, 128, 0, 64 } },
            { "color( srgb 100% 50% 0% )", { 255, 128, 0, 255 } },
            { "COLOR(SRGB-LINEAR 0.5 0 1 / .5)", { 188, 0, 255, 128 } },
            { "color(srgb-linear 0.21404114048223255 0 0)", { 128, 0, 0, 255 } },
            { "color(srgb-linear 50% 0% 100%)", { 188, 0, 255, 255 } },
            { "color(srgb -1 2 0.5 / 2)", { 0, 255, 128, 255 } },
            { "color(srgb-linear -1 2 0 / -1)", { 0, 255, 0, 0 } },
            { "color(srgb none 0.5 1 / none)", { 0, 128, 255, 0 } },
            { "color-mix(in srgb, color(srgb 1 0 0), color(srgb-linear 0 0 1))", { 128, 0, 128, 255 } },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_css_rgba actual = { 0, 0, 0, 0 };
            TBOX_TEST_ASSERT_MSG(tbox_css_color_parse(view_cstr(cases[i].css), &actual), cases[i].css);
            TBOX_TEST_ASSERT_MSG(rgba_eq(actual, cases[i].expected), cases[i].css);
        }
        static const char *const invalid[] = {
            "color(display-p3 1 0 0)", "color(srgb 1 0)", "color(srgb 1 0 0 1)",
            "color(srgb 1, 0, 0)", "color(srgb 1 0 0 /)", "color(srgb 1 0 0 / .5 / .2)",
            "color(srgb 1px 0 0)", "color(srgb NaN 0 0)", "color(srgb 1e999 0 0)",
            "color(srgb 1 0 0 / NaN)", "color(srgb 1 0 0 / infinity)",
            "color(from red srgb r g b)", "color(srgb 1 0 0) extra",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            tbox_css_rgba actual = { 1, 2, 3, 4 };
            TBOX_TEST_ASSERT_MSG(!tbox_css_color_parse(view_cstr(invalid[i]), &actual), invalid[i]);
            TBOX_TEST_ASSERT(rgba_eq(actual, (tbox_css_rgba){ 1, 2, 3, 4 }));
        }
    }

    /* Known spaces retain their behavior; weights are bounded separately
     * before normalization, allowing a sum above 100% but never zero. */
    {
        static const struct { const char *value; tbox_css_rgba expected; } cases[] = {
            { "color-mix(in srgb, red 80%, blue 80%)", {128, 0, 128, 255} },
            { "color-mix(in srgb, red 25%, blue 25%)", {128, 0, 128, 128} },
            { "color-mix(in srgb, 25% red, blue)", {64, 0, 191, 255} },
            { "color-mix(in srgb, red, 100% blue)", {0, 0, 255, 255} },
            { "color-mix(IN\tSRGB, red 0%, blue)", {0, 0, 255, 255} },
            { "color-mix(in\nsrgb, red 100%, blue 0%)", {255, 0, 0, 255} },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_css_rgba actual = {0, 0, 0, 0};
            TBOX_TEST_ASSERT_MSG(tbox_css_color_parse(view_cstr(cases[i].value), &actual), cases[i].value);
            TBOX_TEST_ASSERT_MSG(rgba_eq(actual, cases[i].expected), cases[i].value);
        }
        static const char *const invalid[] = {
            "color-mix(in misspelled, red, blue)", "color-mix(in --custom, red, blue)",
            "color-mix(in , red, blue)", "color-mix(insrgb, red, blue)",
            "color-mix(in srgb, red -1%, blue)", "color-mix(in srgb, -1% red, blue)",
            "color-mix(in srgb, red, blue 101%)", "color-mix(in srgb, 101% red, blue)",
            "color-mix(in srgb, red NaN%, blue)", "color-mix(in srgb, 1e999% red, blue)",
            "color-mix(in srgb, red 0%, blue 0%)", "color-mix(in srgb, red 20% 30%, blue)",
        };
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
            tbox_css_rgba actual = {1, 2, 3, 4};
            TBOX_TEST_ASSERT_MSG(!tbox_css_color_parse(view_cstr(invalid[i]), &actual), invalid[i]);
            TBOX_TEST_ASSERT(rgba_eq(actual, (tbox_css_rgba){1, 2, 3, 4}));
        }
        /* Recognized approximations are preserved by the validation. */
        static const char *const spaces[] = {"lab", "lch", "hsl", "hwb", "display-p3", "xyz-d50", "rec2020"};
        for (size_t i = 0; i < sizeof(spaces) / sizeof(spaces[0]); i++) {
            char css[128];
            snprintf(css, sizeof(css), "color-mix(in %s, red, blue)", spaces[i]);
            TBOX_TEST_ASSERT(tbox_css_color_parse(view_cstr(css), NULL));
        }
    }

    return failures;
}
