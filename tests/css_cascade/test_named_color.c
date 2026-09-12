#include <tbox/css_cascade.h>

#include <string.h>

#include "test_support.h"

static tbox_string_view view_cstr(const char *s) {
    return tbox_string_view_make(s, strlen(s));
}

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

int tbox_test_css_cascade_named_color_run(void) {
    int failures = 0;

    /* 1: exact-case lookup resolves to the documented RGB value, fully opaque. */
    {
        tbox_css_rgba color;
        TBOX_TEST_ASSERT(tbox_css_named_color_find(view_cstr("CornflowerBlue"), &color));
        TBOX_TEST_ASSERT(color.r == 0x64 && color.g == 0x95 && color.b == 0xED && color.a == 0xFF);
    }

    /* 2: matching is ASCII case-insensitive, both directions. */
    {
        tbox_css_rgba lower, upper;
        TBOX_TEST_ASSERT(tbox_css_named_color_find(view_cstr("cornflowerblue"), &lower));
        TBOX_TEST_ASSERT(tbox_css_named_color_find(view_cstr("CORNFLOWERBLUE"), &upper));
        TBOX_TEST_ASSERT(lower.r == upper.r && lower.g == upper.g && lower.b == upper.b);
    }

    /* 3: an unknown name fails and leaves *out_color untouched. */
    {
        tbox_css_rgba color = { 1, 2, 3, 4 };
        TBOX_TEST_ASSERT(!tbox_css_named_color_find(view_cstr("notacolor"), &color));
        TBOX_TEST_ASSERT(color.r == 1 && color.g == 2 && color.b == 3 && color.a == 4);
    }

    /* 4: out_color == NULL is allowed, for a plain membership test. */
    {
        TBOX_TEST_ASSERT(tbox_css_named_color_find(view_cstr("Red"), NULL));
        TBOX_TEST_ASSERT(!tbox_css_named_color_find(view_cstr("notacolor"), NULL));
    }

    /* 5: name.data == NULL is rejected rather than dereferenced. */
    {
        tbox_string_view null_view = { .data = NULL, .size = 0 };
        TBOX_TEST_ASSERT(!tbox_css_named_color_find(null_view, NULL));
    }

    /* 6: reverse lookup round-trips a color back to its keyword. */
    {
        tbox_css_rgba color = { 0x64, 0x95, 0xED, 0xFF };
        TBOX_TEST_ASSERT(text_eq(tbox_css_named_color_name(color), "CornflowerBlue"));
    }

    /* 7: a color with no matching keyword yields an empty view. */
    {
        tbox_css_rgba color   = { 1, 2, 3, 0xFF };
        tbox_string_view name = tbox_css_named_color_name(color);
        TBOX_TEST_ASSERT(name.size == 0);
    }

    /* 8: RGB values aliased by two keywords resolve to the first one listed. */
    {
        tbox_css_rgba cyan_value    = { 0x00, 0xFF, 0xFF, 0xFF };
        tbox_css_rgba magenta_value = { 0xFF, 0x00, 0xFF, 0xFF };
        TBOX_TEST_ASSERT(text_eq(tbox_css_named_color_name(cyan_value), "Aqua"));
        TBOX_TEST_ASSERT(text_eq(tbox_css_named_color_name(magenta_value), "Fuchsia"));
    }

    /* 9: a matching RGB with non-opaque alpha never matches -- every keyword is opaque. */
    {
        tbox_css_rgba translucent_cornflower_blue = { 0x64, 0x95, 0xED, 0x80 };
        tbox_string_view name                     = tbox_css_named_color_name(translucent_cornflower_blue);
        TBOX_TEST_ASSERT(name.size == 0);
    }

    /* 10: find/name round-trip for every one of the 140 keywords. */
    {
        static const char *const all_names[] = {
            "AliceBlue",
            "AntiqueWhite",
            "Aqua",
            "Aquamarine",
            "Azure",
            "Beige",
            "Bisque",
            "Black",
            "BlanchedAlmond",
            "Blue",
            "BlueViolet",
            "Brown",
            "BurlyWood",
            "CadetBlue",
            "Chartreuse",
            "Chocolate",
            "Coral",
            "CornflowerBlue",
            "Cornsilk",
            "Crimson",
            "Cyan",
            "DarkBlue",
            "DarkCyan",
            "DarkGoldenRod",
            "DarkGray",
            "DarkGreen",
            "DarkKhaki",
            "DarkMagenta",
            "DarkOliveGreen",
            "DarkOrange",
            "DarkOrchid",
            "DarkRed",
            "DarkSalmon",
            "DarkSeaGreen",
            "DarkSlateBlue",
            "DarkSlateGray",
            "DarkTurquoise",
            "DarkViolet",
            "DeepPink",
            "DeepSkyBlue",
            "DimGray",
            "DodgerBlue",
            "FireBrick",
            "FloralWhite",
            "ForestGreen",
            "Fuchsia",
            "Gainsboro",
            "GhostWhite",
            "Gold",
            "GoldenRod",
            "Gray",
            "Green",
            "GreenYellow",
            "HoneyDew",
            "HotPink",
            "IndianRed",
            "Indigo",
            "Ivory",
            "Khaki",
            "Lavender",
            "LavenderBlush",
            "LawnGreen",
            "LemonChiffon",
            "LightBlue",
            "LightCoral",
            "LightCyan",
            "LightGoldenRodYellow",
            "LightGray",
            "LightGreen",
            "LightPink",
            "LightSalmon",
            "LightSeaGreen",
            "LightSkyBlue",
            "LightSlateGray",
            "LightSteelBlue",
            "LightYellow",
            "Lime",
            "LimeGreen",
            "Linen",
            "Magenta",
            "Maroon",
            "MediumAquaMarine",
            "MediumBlue",
            "MediumOrchid",
            "MediumPurple",
            "MediumSeaGreen",
            "MediumSlateBlue",
            "MediumSpringGreen",
            "MediumTurquoise",
            "MediumVioletRed",
            "MidnightBlue",
            "MintCream",
            "MistyRose",
            "Moccasin",
            "NavajoWhite",
            "Navy",
            "OldLace",
            "Olive",
            "OliveDrab",
            "Orange",
            "OrangeRed",
            "Orchid",
            "PaleGoldenRod",
            "PaleGreen",
            "PaleTurquoise",
            "PaleVioletRed",
            "PapayaWhip",
            "PeachPuff",
            "Peru",
            "Pink",
            "Plum",
            "PowderBlue",
            "Purple",
            "Red",
            "RosyBrown",
            "RoyalBlue",
            "SaddleBrown",
            "Salmon",
            "SandyBrown",
            "SeaGreen",
            "SeaShell",
            "Sienna",
            "Silver",
            "SkyBlue",
            "SlateBlue",
            "SlateGray",
            "Snow",
            "SpringGreen",
            "SteelBlue",
            "Tan",
            "Teal",
            "Thistle",
            "Tomato",
            "Turquoise",
            "Violet",
            "Wheat",
            "White",
            "WhiteSmoke",
            "Yellow",
            "YellowGreen",
        };
        size_t all_names_count = sizeof(all_names) / sizeof(all_names[0]);
        TBOX_TEST_ASSERT(all_names_count == 140);

        for (size_t i = 0; i < all_names_count; i++) {
            tbox_css_rgba color;
            TBOX_TEST_ASSERT_MSG(tbox_css_named_color_find(view_cstr(all_names[i]), &color), all_names[i]);
            TBOX_TEST_ASSERT_MSG(color.a == 0xFF, all_names[i]);
            tbox_string_view name = tbox_css_named_color_name(color);
            TBOX_TEST_ASSERT_MSG(name.size > 0, all_names[i]);

            /* Round-tripping the *found* color back through find() must land
             * on that same color -- a stand-in for "the name is a real CSS
             * keyword", since aliased colors (Aqua/Cyan, Fuchsia/Magenta)
             * mean tbox_css_named_color_name(color) doesn't always return
             * `all_names[i]` itself. */
            tbox_css_rgba roundtrip;
            TBOX_TEST_ASSERT_MSG(tbox_css_named_color_find(name, &roundtrip), all_names[i]);
            TBOX_TEST_ASSERT_MSG(roundtrip.r == color.r && roundtrip.g == color.g && roundtrip.b == color.b && roundtrip.a == color.a, all_names[i]);
        }
    }

    return failures;
}
