#include <tbox/output.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

/* Only tbox_raster_* is exercised here -- tbox_backend_wayland_* needs a
 * real Wayland compositor, which isn't available in CI (see
 * ARCHITECTURE.md's "Output Display" section and TASKS.md's Tarefa 8). */

static uint32_t tbox_test_raster_xrgb(unsigned char r, unsigned char g, unsigned char b) {
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t *tbox_test_raster_make_buffer(int32_t width, int32_t height, uint32_t fill) {
    uint32_t *pixels = malloc(sizeof(uint32_t) * (size_t)width * (size_t)height);
    if (pixels != NULL) {
        for (int32_t i = 0; i < width * height; i++) {
            pixels[i] = fill;
        }
    }
    return pixels;
}

static tbox_string_view tbox_test_raster_view_from_cstr(const char *nul_terminated) {
    return tbox_string_view_make(nul_terminated, strlen(nul_terminated));
}

#ifndef TBOX_TEST_LIBERATION_SANS_PATH
#define TBOX_TEST_LIBERATION_SANS_PATH "external/liberation-sans/LiberationSans-Regular.ttf"
#endif

/* Mirrors tests/font/test_font.c's / tests/layout/test_layout.c's
 * read_file() helper. */
static char *read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)size);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)size;
    return buffer;
}

static void tbox_test_raster_fill_rect_basic(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 20, height = 20;
    uint32_t background = tbox_test_raster_xrgb(10, 20, 30);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba red = { 255, 0, 0, 255 };
        tbox_rect rect     = { 5, 5, 8, 8 }; /* covers x/y in [5,13) */
        tbox_raster_fill_rect(pixels, width, height, rect, red);

        uint32_t expected = tbox_test_raster_xrgb(255, 0, 0);

        /* Strictly inside the rect: painted red. */
        TBOX_TEST_ASSERT(pixels[7 * (size_t)width + 7] == expected);
        TBOX_TEST_ASSERT(pixels[5 * (size_t)width + 5] == expected);
        TBOX_TEST_ASSERT(pixels[12 * (size_t)width + 12] == expected);

        /* Strictly outside: still the background. */
        TBOX_TEST_ASSERT(pixels[0] == background);
        TBOX_TEST_ASSERT(pixels[4 * (size_t)width + 4] == background);
        TBOX_TEST_ASSERT(pixels[13 * (size_t)width + 13] == background);
        TBOX_TEST_ASSERT(pixels[19 * (size_t)width + 19] == background);

        free(pixels);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_fill_rect_out_of_bounds(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 10, height = 10;
    uint32_t background = tbox_test_raster_xrgb(0, 0, 0);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba blue = { 0, 0, 255, 255 };

        /* Extends past every edge -- must clip, not crash or write OOB. */
        tbox_rect rect = { -5, -5, 20, 20 };
        tbox_raster_fill_rect(pixels, width, height, rect, blue);

        uint32_t expected = tbox_test_raster_xrgb(0, 0, 255);
        for (int32_t y = 0; y < height; y++) {
            for (int32_t x = 0; x < width; x++) {
                TBOX_TEST_ASSERT(pixels[(size_t)y * (size_t)width + (size_t)x] == expected);
            }
        }

        /* Entirely outside the buffer -- a no-op. */
        tbox_rect far_rect = { 1000, 1000, 5, 5 };
        tbox_raster_fill_rect(pixels, width, height, far_rect, blue);
        for (int32_t y = 0; y < height; y++) {
            for (int32_t x = 0; x < width; x++) {
                TBOX_TEST_ASSERT(pixels[(size_t)y * (size_t)width + (size_t)x] == expected);
            }
        }

        free(pixels);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_fill_rect_paint_order(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 20, height = 20;
    uint32_t *pixels = tbox_test_raster_make_buffer(width, height, tbox_test_raster_xrgb(0, 0, 0));
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba first_color  = { 255, 0, 0, 255 };
        tbox_css_rgba second_color = { 0, 255, 0, 255 };

        tbox_rect first_rect  = { 0, 0, 12, 12 };
        tbox_rect second_rect = { 6, 6, 12, 12 };

        tbox_raster_fill_rect(pixels, width, height, first_rect, first_color);
        tbox_raster_fill_rect(pixels, width, height, second_rect, second_color);

        uint32_t expected_first  = tbox_test_raster_xrgb(255, 0, 0);
        uint32_t expected_second = tbox_test_raster_xrgb(0, 255, 0);

        /* Only-first region: still red. */
        TBOX_TEST_ASSERT(pixels[2 * (size_t)width + 2] == expected_first);
        /* Overlap region: second (green) wins. */
        TBOX_TEST_ASSERT(pixels[8 * (size_t)width + 8] == expected_second);
        /* Only-second region: green. */
        TBOX_TEST_ASSERT(pixels[16 * (size_t)width + 16] == expected_second);

        free(pixels);
    }

    *failures_ptr = failures;
}

static unsigned char tbox_test_raster_expected_blend_channel(unsigned char src, unsigned char dst, double alpha) {
    double result = (double)src * alpha + (double)dst * (1.0 - alpha);
    return (unsigned char)(result + 0.5);
}

static void tbox_test_raster_fill_rect_alpha_blend(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 10, height = 10;
    unsigned char bg_r = 200, bg_g = 200, bg_b = 200;
    uint32_t background = tbox_test_raster_xrgb(bg_r, bg_g, bg_b);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba overlay = { 10, 20, 30, 128 };
        tbox_rect rect         = { 0, 0, width, height };
        tbox_raster_fill_rect(pixels, width, height, rect, overlay);

        double alpha            = overlay.a / 255.0;
        unsigned char expected_r = tbox_test_raster_expected_blend_channel(overlay.r, bg_r, alpha);
        unsigned char expected_g = tbox_test_raster_expected_blend_channel(overlay.g, bg_g, alpha);
        unsigned char expected_b = tbox_test_raster_expected_blend_channel(overlay.b, bg_b, alpha);

        uint32_t pixel   = pixels[5 * (size_t)width + 5];
        int actual_r     = (int)((pixel >> 16) & 0xFFu);
        int actual_g     = (int)((pixel >> 8) & 0xFFu);
        int actual_b     = (int)(pixel & 0xFFu);

        TBOX_TEST_ASSERT(abs(actual_r - (int)expected_r) <= 2);
        TBOX_TEST_ASSERT(abs(actual_g - (int)expected_g) <= 2);
        TBOX_TEST_ASSERT(abs(actual_b - (int)expected_b) <= 2);
        TBOX_TEST_ASSERT((pixel >> 24) == 0xFFu);

        free(pixels);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_fill_rect_fully_transparent_noop(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 8, height = 8;
    uint32_t background = tbox_test_raster_xrgb(1, 2, 3);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba transparent = { 255, 255, 255, 0 };
        tbox_rect rect             = { 0, 0, width, height };
        tbox_raster_fill_rect(pixels, width, height, rect, transparent);

        for (int32_t i = 0; i < width * height; i++) {
            TBOX_TEST_ASSERT(pixels[i] == background);
        }

        free(pixels);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_text_run_basic(int *failures_ptr, const void *font_data, size_t font_size) {
    int failures = *failures_ptr;

    tbox_font_face *face = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT(face != NULL);

    if (face != NULL) {
        const int32_t width = 120, height = 60;
        uint32_t background  = 0xFFFFFFFFu; /* opaque white */
        uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
        TBOX_TEST_ASSERT(pixels != NULL);

        if (pixels != NULL) {
            tbox_string_view text = tbox_test_raster_view_from_cstr("Hi!");
            tbox_css_rgba black    = { 0, 0, 0, 255 };
            tbox_rect origin       = { 5.0, 5.0, 0.0, 0.0 };

            tbox_raster_text_run(pixels, width, height, origin, text, face, black);

            double text_width  = tbox_font_measure_text(face, text);
            double line_height = tbox_font_face_line_height(face);
            const double pad   = 6.0;

            int32_t span_x0 = (int32_t)(origin.x - pad);
            int32_t span_x1 = (int32_t)(origin.x + text_width + pad);
            int32_t span_y0 = (int32_t)(origin.y - pad);
            int32_t span_y1 = (int32_t)(origin.y + line_height + pad);

            if (span_x0 < 0) span_x0 = 0;
            if (span_y0 < 0) span_y0 = 0;
            if (span_x1 > width) span_x1 = width;
            if (span_y1 > height) span_y1 = height;

            bool found_inside  = false;
            bool found_outside = false;

            for (int32_t y = 0; y < height; y++) {
                for (int32_t x = 0; x < width; x++) {
                    uint32_t pixel = pixels[(size_t)y * (size_t)width + (size_t)x];
                    bool inside_span = x >= span_x0 && x < span_x1 && y >= span_y0 && y < span_y1;
                    if (pixel != background) {
                        if (inside_span) {
                            found_inside = true;
                        } else {
                            found_outside = true;
                        }
                    }
                }
            }

            TBOX_TEST_ASSERT_MSG(found_inside, "expected some painted pixel within the text's expected span");
            TBOX_TEST_ASSERT_MSG(!found_outside, "expected no painted pixel outside the padded text span");

            free(pixels);
        }

        tbox_font_face_destroy(face);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_display_list_matches_direct_calls(int *failures_ptr, const void *font_data, size_t font_size) {
    int failures = *failures_ptr;

    tbox_font_face *face = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT(face != NULL);

    if (face != NULL) {
        const int32_t width = 60, height = 40;
        tbox_string_view text = tbox_test_raster_view_from_cstr("Ab");
        tbox_css_rgba bg_color = { 240, 240, 240, 255 };
        tbox_css_rgba fg_color = { 0, 0, 0, 255 };

        tbox_paint_op ops[2];
        ops[0].kind  = TBOX_PAINT_FILL_RECT;
        ops[0].rect  = (tbox_rect){ 0, 0, width, height };
        ops[0].color = bg_color;
        ops[0].text  = tbox_string_view_make(NULL, 0);
        ops[0].face  = NULL;

        ops[1].kind  = TBOX_PAINT_TEXT_RUN;
        ops[1].rect  = (tbox_rect){ 3, 3, 0, 0 };
        ops[1].color = fg_color;
        ops[1].text  = text;
        ops[1].face  = face;

        tbox_display_list list = { ops, 2 };

        uint32_t *via_list = tbox_test_raster_make_buffer(width, height, 0);
        uint32_t *via_direct = tbox_test_raster_make_buffer(width, height, 0);
        TBOX_TEST_ASSERT(via_list != NULL && via_direct != NULL);

        if (via_list != NULL && via_direct != NULL) {
            tbox_raster_display_list(via_list, width, height, &list);

            tbox_raster_fill_rect(via_direct, width, height, ops[0].rect, ops[0].color);
            tbox_raster_text_run(via_direct, width, height, ops[1].rect, ops[1].text, ops[1].face, ops[1].color);

            TBOX_TEST_ASSERT(memcmp(via_list, via_direct, sizeof(uint32_t) * (size_t)width * (size_t)height) == 0);
        }

        free(via_list);
        free(via_direct);
        tbox_font_face_destroy(face);
    }

    *failures_ptr = failures;
}

int tbox_test_output_raster_run(void) {
    int failures = 0;

    tbox_test_raster_fill_rect_basic(&failures);
    tbox_test_raster_fill_rect_out_of_bounds(&failures);
    tbox_test_raster_fill_rect_paint_order(&failures);
    tbox_test_raster_fill_rect_alpha_blend(&failures);
    tbox_test_raster_fill_rect_fully_transparent_noop(&failures);

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");

    if (font_data != NULL) {
        tbox_test_raster_text_run_basic(&failures, font_data, font_size);
        tbox_test_raster_display_list_matches_direct_calls(&failures, font_data, font_size);
        free(font_data);
    } else {
        failures++;
    }

    return failures;
}
