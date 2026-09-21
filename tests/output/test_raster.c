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

/* Reads a big-endian uint32 from `data` (must have >= 4 bytes available). */
static uint32_t tbox_test_raster_read_u32_be(const unsigned char *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

/* Independent (separately transcribed, not reused from src/output/tbox_raster.c)
 * IEEE 802.3 CRC-32, so the round-trip test below cross-checks
 * tbox_raster_write_png's own CRC against a second implementation rather
 * than trivially agreeing with itself. */
static uint32_t tbox_test_raster_crc32(uint32_t crc, const unsigned char *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return crc;
}

/* Minimal validating PNG reader, deliberately only as capable as the writer
 * under test needs it to be: tbox_raster_write_png only ever emits IHDR then
 * exactly one IDAT then IEND, and its zlib stream only ever uses
 * uncompressed ("stored") DEFLATE blocks -- so "decoding" the pixel data
 * back out is just concatenating each stored block's literal bytes, no real
 * INFLATE needed. Asserts every chunk's CRC-32 along the way (via
 * tbox_test_raster_crc32 above) and the zlib stream's Adler-32 trailer (via
 * a second, equally independent implementation below), then writes the
 * decoded RGB scanlines (filter byte stripped, assumed 0/"None" -- the only
 * filter this writer ever emits) into `out_rgb` (caller-allocated,
 * width*height*3 bytes). Returns false on any structural mismatch (bad
 * signature, wrong IHDR fields, bad CRC/Adler-32, malformed block framing),
 * without necessarily filling `out_rgb`. */
static bool tbox_test_raster_read_png(const char *path, int32_t expected_width, int32_t expected_height, unsigned char *out_rgb) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    unsigned char signature[8];
    static const unsigned char expected_signature[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    bool ok                                           = fread(signature, 1, 8, file) == 8 && memcmp(signature, expected_signature, 8) == 0;

    unsigned char *idat   = NULL;
    size_t idat_size      = 0;
    bool saw_ihdr         = false;
    bool saw_iend         = false;

    while (ok && !saw_iend) {
        unsigned char header[8];
        if (fread(header, 1, 8, file) != 8) {
            ok = false;
            break;
        }
        uint32_t length = tbox_test_raster_read_u32_be(header);
        char type[5]    = { (char)header[4], (char)header[5], (char)header[6], (char)header[7], '\0' };

        unsigned char *data = length > 0 ? (unsigned char *)malloc(length) : NULL;
        if (length > 0 && (data == NULL || fread(data, 1, length, file) != length)) {
            free(data);
            ok = false;
            break;
        }

        unsigned char crc_bytes[4];
        uint32_t stored_crc;
        if (fread(crc_bytes, 1, 4, file) != 4) {
            free(data);
            ok = false;
            break;
        }
        stored_crc = tbox_test_raster_read_u32_be(crc_bytes);

        uint32_t computed_crc = tbox_test_raster_crc32(0xFFFFFFFFu, (const unsigned char *)header + 4, 4);
        if (length > 0) {
            computed_crc = tbox_test_raster_crc32(computed_crc, data, length);
        }
        computed_crc ^= 0xFFFFFFFFu;
        if (computed_crc != stored_crc) {
            free(data);
            ok = false;
            break;
        }

        if (strcmp(type, "IHDR") == 0) {
            saw_ihdr = length == 13 && (int32_t)tbox_test_raster_read_u32_be(data) == expected_width && (int32_t)tbox_test_raster_read_u32_be(data + 4) == expected_height && data[8] == 8 /* bit depth */ && data[9] == 2 /* color type: truecolor */;
            free(data);
        } else if (strcmp(type, "IDAT") == 0) {
            idat      = data; /* ownership transferred */
            idat_size = length;
        } else if (strcmp(type, "IEND") == 0) {
            saw_iend = true;
            free(data);
        } else {
            free(data);
        }
    }
    fclose(file);

    if (!ok || !saw_ihdr || !saw_iend || idat == NULL || idat_size < 6 /* 2-byte zlib header + 4-byte Adler-32 trailer, minimum */) {
        free(idat);
        return false;
    }

    /* zlib stream: skip the 2-byte header, walk stored DEFLATE blocks until
     * BFINAL, then verify the 4-byte Adler-32 trailer. */
    size_t row_bytes = 1 + (size_t)expected_width * 3;
    size_t raw_size  = row_bytes * (size_t)expected_height;
    unsigned char *raw = (unsigned char *)malloc(raw_size);
    if (raw == NULL) {
        free(idat);
        return false;
    }

    size_t pos       = 2;
    size_t raw_pos   = 0;
    bool final       = false;
    while (ok && !final && pos < idat_size) {
        if (pos + 5 > idat_size) {
            ok = false;
            break;
        }
        final              = (idat[pos] & 1u) != 0;
        uint16_t block_len = (uint16_t)(idat[pos + 1] | (idat[pos + 2] << 8));
        pos += 5; /* header byte + LEN + NLEN */
        if (pos + block_len > idat_size || raw_pos + block_len > raw_size) {
            ok = false;
            break;
        }
        memcpy(raw + raw_pos, idat + pos, block_len);
        raw_pos += block_len;
        pos += block_len;
    }
    ok = ok && raw_pos == raw_size && pos + 4 <= idat_size;

    if (ok) {
        uint32_t stored_adler = tbox_test_raster_read_u32_be(idat + pos);
        uint32_t a = 1, b = 0;
        for (size_t i = 0; i < raw_size; i++) {
            a = (a + raw[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        ok = ((b << 16) | a) == stored_adler;
    }

    if (ok) {
        for (int32_t y = 0; y < expected_height; y++) {
            const unsigned char *row = raw + (size_t)y * row_bytes;
            ok                       = ok && row[0] == 0; /* filter: None */
            memcpy(out_rgb + (size_t)y * (size_t)expected_width * 3, row + 1, (size_t)expected_width * 3);
        }
    }

    free(raw);
    free(idat);
    return ok;
}

static void tbox_test_raster_write_png_round_trip(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 3, height = 2;
    uint32_t pixels[6] = {
        tbox_test_raster_xrgb(255, 0, 0), tbox_test_raster_xrgb(0, 255, 0), tbox_test_raster_xrgb(0, 0, 255),
        tbox_test_raster_xrgb(255, 255, 0), tbox_test_raster_xrgb(0, 255, 255), tbox_test_raster_xrgb(17, 34, 51),
    };

    const char *path = "tbox_test_raster_write_png_round_trip.png";
    TBOX_TEST_ASSERT_MSG(tbox_raster_write_png(path, pixels, width, height), "tbox_raster_write_png must succeed for a small valid buffer");

    unsigned char decoded[6 * 3];
    bool decoded_ok = tbox_test_raster_read_png(path, width, height, decoded);
    TBOX_TEST_ASSERT_MSG(decoded_ok, "the written PNG must be structurally valid (signature, IHDR, CRC-32s, Adler-32) and decode back via stored DEFLATE blocks");

    if (decoded_ok) {
        for (int32_t i = 0; i < width * height; i++) {
            unsigned char expected_r = (unsigned char)((pixels[i] >> 16) & 0xFFu);
            unsigned char expected_g = (unsigned char)((pixels[i] >> 8) & 0xFFu);
            unsigned char expected_b = (unsigned char)(pixels[i] & 0xFFu);
            TBOX_TEST_ASSERT(decoded[i * 3 + 0] == expected_r);
            TBOX_TEST_ASSERT(decoded[i * 3 + 1] == expected_g);
            TBOX_TEST_ASSERT(decoded[i * 3 + 2] == expected_b);
        }
    }

    remove(path);

    *failures_ptr = failures;
}

static void tbox_test_raster_write_png_invalid_args(int *failures_ptr) {
    int failures = *failures_ptr;

    uint32_t pixel = tbox_test_raster_xrgb(1, 2, 3);
    TBOX_TEST_ASSERT(!tbox_raster_write_png(NULL, &pixel, 1, 1));
    TBOX_TEST_ASSERT(!tbox_raster_write_png("tbox_test_raster_write_png_invalid.png", NULL, 1, 1));
    TBOX_TEST_ASSERT(!tbox_raster_write_png("tbox_test_raster_write_png_invalid.png", &pixel, 0, 1));
    TBOX_TEST_ASSERT(!tbox_raster_write_png("tbox_test_raster_write_png_invalid.png", &pixel, 1, -1));

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
    tbox_test_raster_write_png_round_trip(&failures);
    tbox_test_raster_write_png_invalid_args(&failures);

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
