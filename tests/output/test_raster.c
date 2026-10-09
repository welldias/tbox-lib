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
        const char type[5] = { (char)header[4], (char)header[5], (char)header[6], (char)header[7], '\0' };

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
    const uint32_t pixels[6] = {
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

        tbox_paint_op ops[2] = { 0 };
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

/* NOVO (image support): tbox_raster_image, 1:1 -- a 2x2 opaque RGBA source
 * painted into a matching 2x2 dest_rect must copy each source pixel
 * exactly, with no scaling/blending involved (full alpha). */
static void tbox_test_raster_image_basic(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 10, height = 10;
    uint32_t background = tbox_test_raster_xrgb(200, 200, 200);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        /* 2x2 RGBA8, row-major: top-left red, top-right green, bottom-left
         * blue, bottom-right opaque black. */
        unsigned char source_pixels[2 * 2 * 4] = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   0, 0, 0, 255,
        };
        tbox_image image = { 2, 2, source_pixels };

        tbox_rect dest_rect = { 3, 3, 2, 2 };
        tbox_raster_image(pixels, width, height, dest_rect, &image);

        TBOX_TEST_ASSERT_MSG(pixels[3 * (size_t)width + 3] == tbox_test_raster_xrgb(255, 0, 0), "top-left source pixel must land at dest_rect's top-left");
        TBOX_TEST_ASSERT_MSG(pixels[3 * (size_t)width + 4] == tbox_test_raster_xrgb(0, 255, 0), "top-right source pixel must land at dest_rect's top-right");
        TBOX_TEST_ASSERT_MSG(pixels[4 * (size_t)width + 3] == tbox_test_raster_xrgb(0, 0, 255), "bottom-left source pixel must land at dest_rect's bottom-left");
        TBOX_TEST_ASSERT_MSG(pixels[4 * (size_t)width + 4] == tbox_test_raster_xrgb(0, 0, 0), "bottom-right source pixel must land at dest_rect's bottom-right");

        /* Strictly outside dest_rect: untouched. */
        TBOX_TEST_ASSERT(pixels[0] == background);
        TBOX_TEST_ASSERT(pixels[9 * (size_t)width + 9] == background);

        free(pixels);
    }

    *failures_ptr = failures;
}

/* Upscaling: the SAME 2x2 source painted into a 4x4 dest_rect goes through
 * stb_image_resize2's real (Mitchell/cubic, sRGB-aware) resampling, not a
 * flat nearest-neighbor block per source pixel -- the interior blends
 * between neighboring source colors. What's still exactly predictable
 * (edge-clamped resampling anchors here) is each of the FOUR CORNER pixels
 * of the destination, which must stay close to their one nearest source
 * pixel's color -- proving both that real scaling happened (dest is 4x4,
 * not silently left at 2x2 or some other size) and that source-to-dest
 * orientation/mapping is correct (top-left dest corner is red, not e.g.
 * bottom-right's black). */
static void tbox_test_raster_image_scaling(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 10, height = 10;
    uint32_t *pixels = tbox_test_raster_make_buffer(width, height, tbox_test_raster_xrgb(0, 0, 0));
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        unsigned char source_pixels[2 * 2 * 4] = {
            255, 0, 0, 255,   0, 255, 0, 255,
            0, 0, 255, 255,   0, 0, 0, 255,
        };
        tbox_image image = { 2, 2, source_pixels };

        tbox_rect dest_rect = { 0, 0, 4, 4 };
        tbox_raster_image(pixels, width, height, dest_rect, &image);

        const int tolerance = 30;
        uint32_t top_left     = pixels[0 * (size_t)width + 0];
        uint32_t top_right    = pixels[0 * (size_t)width + 3];
        uint32_t bottom_left  = pixels[3 * (size_t)width + 0];
        uint32_t bottom_right = pixels[3 * (size_t)width + 3];

        TBOX_TEST_ASSERT_MSG(abs((int)((top_left >> 16) & 0xFF) - 255) <= tolerance && abs((int)((top_left >> 8) & 0xFF) - 0) <= tolerance && abs((int)(top_left & 0xFF) - 0) <= tolerance, "top-left dest corner must stay close to red, the nearest source pixel");
        TBOX_TEST_ASSERT_MSG(abs((int)((top_right >> 16) & 0xFF) - 0) <= tolerance && abs((int)((top_right >> 8) & 0xFF) - 255) <= tolerance && abs((int)(top_right & 0xFF) - 0) <= tolerance, "top-right dest corner must stay close to green, the nearest source pixel");
        TBOX_TEST_ASSERT_MSG(abs((int)((bottom_left >> 16) & 0xFF) - 0) <= tolerance && abs((int)((bottom_left >> 8) & 0xFF) - 0) <= tolerance && abs((int)(bottom_left & 0xFF) - 255) <= tolerance, "bottom-left dest corner must stay close to blue, the nearest source pixel");
        TBOX_TEST_ASSERT_MSG(abs((int)((bottom_right >> 16) & 0xFF) - 0) <= tolerance && abs((int)((bottom_right >> 8) & 0xFF) - 0) <= tolerance && abs((int)(bottom_right & 0xFF) - 0) <= tolerance, "bottom-right dest corner must stay close to black, the nearest source pixel");

        /* Untouched outside the 4x4 dest_rect. */
        TBOX_TEST_ASSERT(pixels[9 * (size_t)width + 9] == tbox_test_raster_xrgb(0, 0, 0));

        free(pixels);
    }

    *failures_ptr = failures;
}

/* Pixelated enlargement maps each destination block to exactly one source
 * pixel, while preserving image op clipping and opacity. */
static void tbox_test_raster_image_pixelated(int *failures_ptr) {
    int failures = *failures_ptr;
    unsigned char source_pixels[2 * 2 * 4] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255,
    };
    tbox_image source = { 2, 2, source_pixels };
    tbox_paint_op op = { 0 };
    op.kind = TBOX_PAINT_IMAGE;
    op.rect = (tbox_rect){ 0, 0, 4, 4 };
    op.image = &source;
    op.image_pixelated = true;
    op.color.a = 255;
    tbox_display_list list = { &op, 1 };
    uint32_t pixels[4 * 4] = { 0 };
    tbox_raster_display_list(pixels, 4, 4, &list);
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            uint32_t expected = y < 2 ? (x < 2 ? tbox_test_raster_xrgb(255, 0, 0) : tbox_test_raster_xrgb(0, 255, 0)) :
                                        (x < 2 ? tbox_test_raster_xrgb(0, 0, 255) : tbox_test_raster_xrgb(255, 255, 0));
            TBOX_TEST_ASSERT(pixels[y * 4 + x] == expected);
        }
    }
    op.has_clip = true;
    op.clip = (tbox_rect){ 2, 0, 2, 4 };
    memset(pixels, 0, sizeof(pixels));
    tbox_raster_display_list(pixels, 4, 4, &list);
    TBOX_TEST_ASSERT(pixels[0] == 0 && pixels[2] == tbox_test_raster_xrgb(0, 255, 0));
    *failures_ptr = failures;
}

/* Alpha compositing: a fully-transparent source pixel must leave the
 * destination untouched (same "alpha == 0 is a no-op" contract
 * tbox_raster_fill_rect already has), and a half-transparent one must blend
 * with the "over" formula rather than overwrite. */
static void tbox_test_raster_image_alpha_blend(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 4, height = 1;
    uint32_t background = tbox_test_raster_xrgb(100, 100, 100);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        /* 2x1: fully transparent red, then half-alpha (128) opaque-red-ish
         * source over the same grey background. */
        unsigned char source_pixels[2 * 1 * 4] = {
            255, 0, 0, 0,
            255, 0, 0, 128,
        };
        tbox_image image = { 2, 1, source_pixels };

        tbox_rect dest_rect = { 0, 0, 2, 1 };
        tbox_raster_image(pixels, width, height, dest_rect, &image);

        TBOX_TEST_ASSERT_MSG(pixels[0] == background, "alpha == 0 source pixel must leave the destination untouched");

        unsigned char blended_r = (unsigned char)(255.0 * (128.0 / 255.0) + 100.0 * (1.0 - 128.0 / 255.0) + 0.5);
        unsigned char blended_g = (unsigned char)(0.0 * (128.0 / 255.0) + 100.0 * (1.0 - 128.0 / 255.0) + 0.5);
        uint32_t expected       = tbox_test_raster_xrgb(blended_r, blended_g, blended_g);
        TBOX_TEST_ASSERT_MSG(pixels[1] == expected, "half-alpha source pixel must alpha-blend over the destination, not overwrite it");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* A dest_rect partially/fully outside the buffer's bounds must clip cleanly
 * (no crash, no out-of-bounds write) -- same expectation tbox_raster_fill_rect
 * already has (tbox_test_raster_fill_rect_out_of_bounds above). */
static void tbox_test_raster_image_out_of_bounds(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 5, height = 5;
    uint32_t background = tbox_test_raster_xrgb(1, 2, 3);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        unsigned char source_pixels[1 * 1 * 4] = { 255, 255, 255, 255 };
        tbox_image image                       = { 1, 1, source_pixels };

        /* Entirely outside (past the right/bottom edge) -- must be a no-op,
         * not a crash. */
        tbox_rect far_rect = { 100, 100, 10, 10 };
        tbox_raster_image(pixels, width, height, far_rect, &image);
        TBOX_TEST_ASSERT(pixels[0] == background);

        /* Straddles the top-left corner -- only the visible portion paints. */
        tbox_rect corner_rect = { -1, -1, 3, 3 };
        tbox_raster_image(pixels, width, height, corner_rect, &image);
        TBOX_TEST_ASSERT_MSG(pixels[1 * (size_t)width + 1] == tbox_test_raster_xrgb(255, 255, 255), "the visible portion of a straddling dest_rect must still paint");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* NULL image, non-positive buffer dims, and non-positive dest_rect
 * width/height are all no-ops -- same "tolerate bad input silently"
 * contract every other tbox_raster_* function already has. */
static void tbox_test_raster_image_invalid_args(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 4, height = 4;
    uint32_t background = tbox_test_raster_xrgb(9, 9, 9);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        unsigned char source_pixels[1 * 1 * 4] = { 255, 255, 255, 255 };
        tbox_image image                       = { 1, 1, source_pixels };
        tbox_rect rect                         = { 0, 0, 4, 4 };

        tbox_raster_image(pixels, width, height, rect, NULL);
        tbox_raster_image(NULL, width, height, rect, &image);
        tbox_raster_image(pixels, 0, height, rect, &image);
        tbox_raster_image(pixels, width, 0, rect, &image);
        tbox_raster_image(pixels, width, height, (tbox_rect){ 0, 0, 0, 4 }, &image);
        tbox_raster_image(pixels, width, height, (tbox_rect){ 0, 0, 4, 0 }, &image);

        TBOX_TEST_ASSERT_MSG(pixels[0] == background, "every invalid-argument call above must be a no-op");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* NOVO (visual fidelity): tbox_raster_fill_rounded_rect -- corners are
 * actually cut (a pixel deep in a corner's RADIUSxRADIUS square, outside
 * its circle, stays untouched), while the "cross" region (center, straight
 * edges away from any corner) paints exactly like a plain rect fill. */
static void tbox_test_raster_rounded_rect_corners(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 20, height = 20;
    uint32_t background = tbox_test_raster_xrgb(0, 0, 0);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba white = { 255, 255, 255, 255 };
        tbox_rect rect       = { 0, 0, 20, 20 };
        tbox_raster_fill_rounded_rect(pixels, width, height, rect, 5.0, white);

        uint32_t painted = tbox_test_raster_xrgb(255, 255, 255);

        /* The extreme corner pixel is well outside the radius-5 circle
         * (inset (5,5) from that corner) -- must stay background. */
        TBOX_TEST_ASSERT_MSG(pixels[0 * (size_t)width + 0] == background, "the extreme top-left corner pixel must be cut by the radius");

        /* The dead center of the rect is always inside (the "cross"
         * region), far from every corner. */
        TBOX_TEST_ASSERT_MSG(pixels[10 * (size_t)width + 10] == painted, "the center must always be painted");

        /* The middle of the top edge (away from both top corners) is in
         * neither corner's x-band, so it must paint flat, same as a plain
         * rect fill would. */
        TBOX_TEST_ASSERT_MSG(pixels[0 * (size_t)width + 10] == painted, "the middle of a straight edge must paint flat, same as tbox_raster_fill_rect");

        /* A pixel exactly at the inset circle's own center (5,5) is
         * trivially inside that circle (distance 0) -- must paint. */
        TBOX_TEST_ASSERT_MSG(pixels[5 * (size_t)width + 5] == painted, "the corner circle's own center must be painted");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* radius <= 0.0 (after clamping) must produce BYTE-IDENTICAL output to
 * tbox_raster_fill_rect -- the documented degenerate case. */
static void tbox_test_raster_rounded_rect_zero_radius_matches_fill_rect(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 12, height = 12;
    uint32_t *via_rounded = tbox_test_raster_make_buffer(width, height, tbox_test_raster_xrgb(9, 9, 9));
    uint32_t *via_plain   = tbox_test_raster_make_buffer(width, height, tbox_test_raster_xrgb(9, 9, 9));
    TBOX_TEST_ASSERT(via_rounded != NULL && via_plain != NULL);

    if (via_rounded != NULL && via_plain != NULL) {
        tbox_css_rgba color = { 1, 2, 3, 200 };
        tbox_rect rect       = { 2, 2, 8, 8 };

        tbox_raster_fill_rounded_rect(via_rounded, width, height, rect, 0.0, color);
        tbox_raster_fill_rect(via_plain, width, height, rect, color);

        TBOX_TEST_ASSERT(memcmp(via_rounded, via_plain, sizeof(uint32_t) * (size_t)width * (size_t)height) == 0);

        free(via_rounded);
        free(via_plain);
    }

    *failures_ptr = failures;
}

/* A radius larger than half the smaller rect dimension must clamp (a
 * "stadium"/circle shape, never a broken self-intersecting one) -- checked
 * by confirming a corner pixel that a NAIVE (unclamped) radius would still
 * consider "inside a straight cross region" is correctly cut once the
 * radius is clamped down to fit. */
static void tbox_test_raster_rounded_rect_radius_clamped(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 40, height = 40;
    uint32_t background = tbox_test_raster_xrgb(0, 0, 0);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba white = { 255, 255, 255, 255 };
        /* rect is 20x10 -- max_radius clamps to min(20,10)/2 = 5, even
         * though 1000.0 was requested. */
        tbox_rect rect = { 0, 0, 20, 10 };
        tbox_raster_fill_rounded_rect(pixels, width, height, rect, 1000.0, white);

        uint32_t painted = tbox_test_raster_xrgb(255, 255, 255);

        /* Vertical center of the short (height=10) edge: with the radius
         * correctly clamped to 5 (== half the height), EVERY point along
         * x is within the corners' y-band, so this becomes a full
         * capsule/stadium shape -- the middle of the left edge (x=0,y=5)
         * sits exactly on the inset circle's own vertical center, always
         * inside. */
        TBOX_TEST_ASSERT_MSG(pixels[5 * (size_t)width + 0] == painted, "clamped radius must still paint the short edge's own vertical center");

        /* The far corners stay cut, exactly like the unclamped case. */
        TBOX_TEST_ASSERT_MSG(pixels[0 * (size_t)width + 0] == background, "the extreme corner must still be cut after clamping");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* A rect straddling the buffer's bounds must clip cleanly -- no crash, no
 * out-of-bounds write, same expectation every other tbox_raster_* function
 * already has. */
static void tbox_test_raster_rounded_rect_out_of_bounds(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 10, height = 10;
    uint32_t background = tbox_test_raster_xrgb(3, 3, 3);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba white = { 255, 255, 255, 255 };
        tbox_rect far_rect   = { 1000, 1000, 20, 20 };
        tbox_raster_fill_rounded_rect(pixels, width, height, far_rect, 4.0, white);
        TBOX_TEST_ASSERT(pixels[0] == background);

        tbox_rect straddling = { -5, -5, 15, 15 };
        tbox_raster_fill_rounded_rect(pixels, width, height, straddling, 3.0, white);
        TBOX_TEST_ASSERT_MSG(pixels[8 * (size_t)width + 8] == tbox_test_raster_xrgb(255, 255, 255), "the visible portion of a straddling rect must still paint (deep in the cross region, away from the clipped corner)");

        free(pixels);
    }

    *failures_ptr = failures;
}

/* NULL pixels, non-positive buffer dims, non-positive rect.width/height,
 * and color.a == 0 are all no-ops -- same "tolerate bad input silently"
 * contract every other tbox_raster_* function already has. */
static void tbox_test_raster_rounded_rect_invalid_args(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 8, height = 8;
    uint32_t background = tbox_test_raster_xrgb(5, 5, 5);
    uint32_t *pixels     = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);

    if (pixels != NULL) {
        tbox_css_rgba white           = { 255, 255, 255, 255 };
        tbox_css_rgba transparent     = { 255, 255, 255, 0 };
        tbox_rect rect                 = { 0, 0, 8, 8 };

        tbox_raster_fill_rounded_rect(NULL, width, height, rect, 2.0, white);
        tbox_raster_fill_rounded_rect(pixels, 0, height, rect, 2.0, white);
        tbox_raster_fill_rounded_rect(pixels, width, 0, rect, 2.0, white);
        tbox_raster_fill_rounded_rect(pixels, width, height, (tbox_rect){ 0, 0, 0, 8 }, 2.0, white);
        tbox_raster_fill_rounded_rect(pixels, width, height, (tbox_rect){ 0, 0, 8, 0 }, 2.0, white);
        tbox_raster_fill_rounded_rect(pixels, width, height, rect, 2.0, transparent);

        TBOX_TEST_ASSERT_MSG(pixels[0] == background, "every invalid-argument call above must be a no-op");

        free(pixels);
    }

    *failures_ptr = failures;
}

static void tbox_test_raster_individual_corners(int *failures_ptr) {
    int failures = *failures_ptr;
    const int32_t width = 20, height = 20;
    uint32_t background = tbox_test_raster_xrgb(0, 0, 0);
    uint32_t painted = tbox_test_raster_xrgb(255, 255, 255);
    uint32_t *pixels = tbox_test_raster_make_buffer(width, height, background);
    TBOX_TEST_ASSERT(pixels != NULL);
    if (pixels != NULL) {
        tbox_paint_op op = {0};
        op.kind = TBOX_PAINT_FILL_RECT;
        op.rect = (tbox_rect){0, 0, 20, 20};
        op.color = (tbox_css_rgba){255, 255, 255, 255};
        op.corner_radii[0] = 8.0;
        tbox_display_list list = {&op, 1};
        tbox_raster_display_list(pixels, width, height, &list);
        TBOX_TEST_ASSERT(pixels[0] == background);
        TBOX_TEST_ASSERT(pixels[19] == painted);
        TBOX_TEST_ASSERT(pixels[19 * (size_t)width] == painted);
        TBOX_TEST_ASSERT(pixels[19 * (size_t)width + 19] == painted);
        TBOX_TEST_ASSERT(pixels[8 * (size_t)width + 8] == painted);
        free(pixels);
    }
    *failures_ptr = failures;
}

static void tbox_test_raster_wavy_line(int *failures_ptr) {
    int failures = *failures_ptr;
    uint32_t pixels[8 * 8];
    for (size_t i = 0; i < 8u * 8u; i++) pixels[i] = 0xFFFFFFFFu;
    tbox_paint_op op = {0};
    op.kind = TBOX_PAINT_WAVY_LINE;
    op.rect = (tbox_rect){0, 1, 8, 1};
    op.color = (tbox_css_rgba){255, 0, 0, 255};
    tbox_display_list list = {&op, 1};
    tbox_raster_display_list(pixels, 8, 8, &list);
    TBOX_TEST_ASSERT(pixels[1 * 8 + 0] == 0xFFFF0000u);
    TBOX_TEST_ASSERT(pixels[2 * 8 + 2] == 0xFFFF0000u);
    TBOX_TEST_ASSERT(pixels[1 * 8 + 2] == 0xFFFFFFFFu);
    *failures_ptr = failures;
}

/* Linear and radial gradients: stop colors at the ends, interpolation in
 * between, repeating stops, and an elliptical rounded clip. */
static void tbox_test_raster_gradient(int *failures_ptr) {
    int failures = *failures_ptr;
    uint32_t pixels[20 * 10];
    for (size_t i = 0; i < 20u * 10u; i++) pixels[i] = 0xFFFFFFFFu;
    tbox_style_gradient gradient = { 0 };
    gradient.kind                = TBOX_STYLE_GRADIENT_LINEAR;
    gradient.angle               = 90.0; /* to right */
    gradient.stop_count          = 2;
    gradient.stops[0]            = (tbox_style_gradient_stop){ { 255, 0, 0, 255 }, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
    gradient.stops[1]            = (tbox_style_gradient_stop){ { 0, 0, 255, 255 }, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
    tbox_paint_op op             = { 0 };
    op.kind                      = TBOX_PAINT_GRADIENT;
    op.rect                      = (tbox_rect){ 0, 0, 20, 10 };
    op.color                     = (tbox_css_rgba){ 0, 0, 0, 255 };
    op.gradient                  = &gradient;
    tbox_display_list list       = { &op, 1 };
    tbox_raster_display_list(pixels, 20, 10, &list);
    uint32_t left = pixels[5 * 20 + 0], right = pixels[5 * 20 + 19], middle = pixels[5 * 20 + 10];
    TBOX_TEST_ASSERT(((left >> 16) & 0xFFu) > 230 && (left & 0xFFu) < 25);
    TBOX_TEST_ASSERT((right & 0xFFu) > 230 && ((right >> 16) & 0xFFu) < 25);
    TBOX_TEST_ASSERT(((middle >> 16) & 0xFFu) > 100 && ((middle >> 16) & 0xFFu) < 155 && (middle & 0xFFu) > 100 && (middle & 0xFFu) < 155);

    /* Repeating: 0-5px red, 5-10px blue, every 10px. */
    gradient.repeating = true;
    gradient.stop_count = 4;
    gradient.stops[0]   = (tbox_style_gradient_stop){ { 255, 0, 0, 255 }, { TBOX_STYLE_LENGTH_PX, 0.0, 0.0, 0, 0.0, 0.0 } };
    gradient.stops[1]   = (tbox_style_gradient_stop){ { 255, 0, 0, 255 }, { TBOX_STYLE_LENGTH_PX, 5.0, 0.0, 0, 0.0, 0.0 } };
    gradient.stops[2]   = (tbox_style_gradient_stop){ { 0, 0, 255, 255 }, { TBOX_STYLE_LENGTH_PX, 5.0, 0.0, 0, 0.0, 0.0 } };
    gradient.stops[3]   = (tbox_style_gradient_stop){ { 0, 0, 255, 255 }, { TBOX_STYLE_LENGTH_PX, 10.0, 0.0, 0, 0.0, 0.0 } };
    tbox_raster_display_list(pixels, 20, 10, &list);
    TBOX_TEST_ASSERT(pixels[2 * 20 + 2] == 0xFFFF0000u && pixels[2 * 20 + 12] == 0xFFFF0000u);
    TBOX_TEST_ASSERT(pixels[2 * 20 + 7] == 0xFF0000FFu && pixels[2 * 20 + 17] == 0xFF0000FFu);

    /* Radial: center color in the middle, last stop at the corners. */
    for (size_t i = 0; i < 20u * 10u; i++) pixels[i] = 0xFFFFFFFFu;
    gradient            = (tbox_style_gradient){ 0 };
    gradient.kind       = TBOX_STYLE_GRADIENT_RADIAL;
    gradient.center[0]  = gradient.center[1] = (tbox_style_length){ TBOX_STYLE_LENGTH_PERCENT, 50.0, 0.0, 0, 0.0, 0.0 };
    gradient.stop_count = 2;
    gradient.stops[0]   = (tbox_style_gradient_stop){ { 0, 255, 0, 255 }, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
    gradient.stops[1]   = (tbox_style_gradient_stop){ { 0, 0, 0, 255 }, { TBOX_STYLE_LENGTH_AUTO, 0.0, 0.0, 0, 0.0, 0.0 } };
    tbox_raster_display_list(pixels, 20, 10, &list);
    TBOX_TEST_ASSERT(((pixels[5 * 20 + 10] >> 8) & 0xFFu) > 230);
    TBOX_TEST_ASSERT(((pixels[0] >> 8) & 0xFFu) < 40);

    /* A rounded clip leaves the corner untouched. */
    for (size_t i = 0; i < 20u * 10u; i++) pixels[i] = 0xFFFFFFFFu;
    op.has_rounded_clip = true;
    op.rounded_clip     = op.rect;
    for (size_t i = 0; i < 4; i++) {
        op.rounded_clip_radii[i]   = 8.0;
        op.rounded_clip_radii_y[i] = 4.0;
    }
    tbox_raster_display_list(pixels, 20, 10, &list);
    TBOX_TEST_ASSERT(pixels[0] == 0xFFFFFFFFu && pixels[5 * 20 + 10] != 0xFFFFFFFFu);
    *failures_ptr = failures;
}

/* Elliptical corners: a pixel inside the horizontal radius but outside the
 * ellipse stays clear; large radii regions may overlap (leaf shape). */
static void tbox_test_raster_elliptical_corners(int *failures_ptr) {
    int failures = *failures_ptr;
    uint32_t pixels[40 * 20];
    for (size_t i = 0; i < 40u * 20u; i++) pixels[i] = 0xFFFFFFFFu;
    tbox_paint_op op = { 0 };
    op.kind          = TBOX_PAINT_FILL_RECT;
    op.rect          = (tbox_rect){ 0, 0, 40, 20 };
    op.color         = (tbox_css_rgba){ 255, 0, 0, 255 };
    op.elliptical    = true;
    op.corner_radii[0] = op.corner_radii[2] = 40.0;
    op.corner_radii_y[0] = op.corner_radii_y[2] = 20.0;
    tbox_display_list list = { &op, 1 };
    tbox_raster_display_list(pixels, 40, 20, &list);
    TBOX_TEST_ASSERT(pixels[0] == 0xFFFFFFFFu);              /* top-left corner cut */
    TBOX_TEST_ASSERT(pixels[19 * 40 + 39] == 0xFFFFFFFFu);   /* bottom-right cut too */
    TBOX_TEST_ASSERT(pixels[10 * 40 + 20] == 0xFFFF0000u);   /* the middle is filled */
    TBOX_TEST_ASSERT(pixels[0 * 40 + 39] == 0xFFFF0000u);    /* square top-right corner */
    *failures_ptr = failures;
}


/* ---- partial repaint (tbox_damage_tracker) ---- */

static tbox_paint_op tbox_test_damage_rect_op(double x, double y, double w, double h, tbox_css_rgba color) {
    tbox_paint_op op;
    memset(&op, 0, sizeof(op));
    op.kind  = TBOX_PAINT_FILL_RECT;
    op.rect  = (tbox_rect){ x, y, w, h };
    op.color = color;
    return op;
}

static bool tbox_test_damage_covers(const tbox_damage *damage, tbox_rect r) {
    /* Every pixel of `r` lies in some damage rect. */
    for (int32_t y = (int32_t)r.y; y < (int32_t)(r.y + r.height); y++)
        for (int32_t x = (int32_t)r.x; x < (int32_t)(r.x + r.width); x++) {
            bool inside = false;
            for (size_t i = 0; i < damage->count && !inside; i++) {
                tbox_rect d = damage->rects[i];
                inside      = x >= d.x && x < d.x + d.width && y >= d.y && y < d.y + d.height;
            }
            if (!inside)
                return false;
        }
    return true;
}

static double tbox_test_damage_area(const tbox_damage *damage) {
    double area = 0.0;
    for (size_t i = 0; i < damage->count; i++)
        area += damage->rects[i].width * damage->rects[i].height;
    return area;
}

static void tbox_test_raster_damage_tracker(int *failures_ptr) {
    int failures = *failures_ptr;

    const int32_t width = 200, height = 200;
    tbox_css_rgba red = { 255, 0, 0, 255 }, blue = { 0, 0, 255, 255 }, green = { 0, 128, 0, 255 };
    tbox_damage_tracker *tracker = tbox_damage_tracker_create();
    TBOX_TEST_ASSERT(tracker != NULL);
    if (tracker != NULL) {
        tbox_paint_op ops[3] = {
            tbox_test_damage_rect_op(10, 10, 20, 20, red),
            tbox_test_damage_rect_op(50, 50, 20, 20, blue),
            tbox_test_damage_rect_op(100, 100, 20, 20, green),
        };
        tbox_display_list list = { ops, 3 };

        tbox_damage damage = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(damage.full);

        damage = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full && damage.count == 0);

        /* Recolor: exactly that op's bounds. */
        ops[1].color = red;
        damage       = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full && damage.count == 1);
        TBOX_TEST_ASSERT(damage.count == 1 && damage.rects[0].x == 50.0 && damage.rects[0].y == 50.0 && damage.rects[0].width == 20.0 && damage.rects[0].height == 20.0);

        /* Move: old and new position. */
        ops[1].rect = (tbox_rect){ 140, 20, 20, 20 };
        damage      = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full);
        TBOX_TEST_ASSERT(tbox_test_damage_covers(&damage, (tbox_rect){ 50, 50, 20, 20 }));
        TBOX_TEST_ASSERT(tbox_test_damage_covers(&damage, (tbox_rect){ 140, 20, 20, 20 }));
        TBOX_TEST_ASSERT(tbox_test_damage_area(&damage) == 800.0);

        /* Remove the last op, then add it back. */
        list.count = 2;
        damage     = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full && damage.count == 1 && tbox_test_damage_covers(&damage, (tbox_rect){ 100, 100, 20, 20 }));
        list.count = 3;
        damage     = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full && damage.count == 1 && tbox_test_damage_covers(&damage, (tbox_rect){ 100, 100, 20, 20 }));

        /* Swapping the paint order of two overlapping ops damages both. */
        ops[1].rect = (tbox_rect){ 15, 15, 20, 20 };
        tbox_damage_tracker_update(tracker, &list, width, height);
        tbox_paint_op swap = ops[0];
        ops[0]             = ops[1];
        ops[1]             = swap;
        damage             = tbox_damage_tracker_update(tracker, &list, width, height);
        TBOX_TEST_ASSERT(!damage.full && tbox_test_damage_covers(&damage, (tbox_rect){ 10, 10, 25, 25 }));

        /* Invalidation and a size change force a full repaint. */
        tbox_damage_tracker_invalidate(tracker);
        TBOX_TEST_ASSERT(tbox_damage_tracker_update(tracker, &list, width, height).full);
        TBOX_TEST_ASSERT(tbox_damage_tracker_update(tracker, &list, width + 1, height).full);

        /* Changes covering most of the buffer fall back to full. */
        tbox_paint_op big = tbox_test_damage_rect_op(0, 0, 190, 190, red);
        tbox_display_list big_list = { &big, 1 };
        tbox_damage_tracker_update(tracker, &big_list, width, height);
        big.color = blue;
        TBOX_TEST_ASSERT(tbox_damage_tracker_update(tracker, &big_list, width, height).full);

        /* Many scattered changes stay within the rect budget. */
        tbox_paint_op grid[64];
        for (int i = 0; i < 64; i++)
            grid[i] = tbox_test_damage_rect_op((i % 8) * 25.0, (i / 8) * 25.0, 4, 4, red);
        tbox_display_list grid_list = { grid, 64 };
        tbox_damage_tracker_update(tracker, &grid_list, width, height);
        for (int i = 0; i < 64; i++)
            grid[i].color = blue;
        damage = tbox_damage_tracker_update(tracker, &grid_list, width, height);
        TBOX_TEST_ASSERT(damage.full || damage.count <= TBOX_DAMAGE_MAX_RECTS);
        for (int i = 0; i < 64 && !damage.full; i++)
            TBOX_TEST_ASSERT(tbox_test_damage_covers(&damage, grid[i].rect));

        TBOX_TEST_ASSERT(tbox_damage_tracker_update(NULL, &list, width, height).full);
        tbox_damage_tracker_destroy(tracker);
    }

    *failures_ptr = failures;
}

/* Repainting B's damage over a buffer holding A must equal painting B from
 * scratch, pixel for pixel. */
static void tbox_test_damage_expect_equivalent(int *failures_ptr, const tbox_display_list *a, const tbox_display_list *b, int32_t width, int32_t height, const char *label) {
    int failures             = *failures_ptr;
    uint32_t *partial        = tbox_test_raster_make_buffer(width, height, 0xFFFFFFFFu);
    uint32_t *full           = tbox_test_raster_make_buffer(width, height, 0xFFFFFFFFu);
    tbox_damage_tracker *tracker = tbox_damage_tracker_create();
    TBOX_TEST_ASSERT(partial != NULL && full != NULL && tracker != NULL);
    if (partial != NULL && full != NULL && tracker != NULL) {
        tbox_damage first = tbox_damage_tracker_update(tracker, a, width, height);
        tbox_raster_display_list_damaged(partial, width, height, a, &first);
        tbox_damage damage = tbox_damage_tracker_update(tracker, b, width, height);
        TBOX_TEST_ASSERT_MSG(!damage.full, label);
        tbox_raster_display_list_damaged(partial, width, height, b, &damage);
        tbox_raster_display_list(full, width, height, b);
        TBOX_TEST_ASSERT_MSG(memcmp(partial, full, sizeof(uint32_t) * (size_t)width * (size_t)height) == 0, label);
    }
    tbox_damage_tracker_destroy(tracker);
    free(partial);
    free(full);
    *failures_ptr = failures;
}

static void tbox_test_raster_damage_pixel_equivalence(int *failures_ptr, const void *font_data, size_t font_size) {
    int failures = *failures_ptr;

    tbox_font_face *face = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT(face != NULL);
    if (face != NULL) {
        const int32_t width = 240, height = 160;
        tbox_css_rgba red = { 220, 30, 30, 255 }, blue = { 20, 40, 200, 160 }, black = { 0, 0, 0, 255 };

        unsigned char image_pixels[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 128, 255, 255, 0, 255 };
        tbox_image image = { 2, 2, image_pixels };

        tbox_style_gradient gradient;
        memset(&gradient, 0, sizeof(gradient));
        gradient.kind       = TBOX_STYLE_GRADIENT_LINEAR;
        gradient.angle      = 90.0;
        gradient.stop_count = 2;
        gradient.stops[0]   = (tbox_style_gradient_stop){ { 255, 200, 0, 255 }, { TBOX_STYLE_LENGTH_PERCENT, 0.0, 0.0, 0, 0.0, 0.0 } };
        gradient.stops[1]   = (tbox_style_gradient_stop){ { 0, 120, 255, 255 }, { TBOX_STYLE_LENGTH_PERCENT, 100.0, 0.0, 0, 0.0, 0.0 } };

        enum { COUNT = 7 };
        tbox_paint_op a[COUNT];
        a[0]         = tbox_test_damage_rect_op(0.0, 0.0, 240.0, 160.0, (tbox_css_rgba){ 245, 245, 245, 255 });
        a[1]         = tbox_test_damage_rect_op(10.3, 12.7, 60.5, 30.2, red);
        a[2]         = tbox_test_damage_rect_op(40.0, 30.0, 50.0, 40.0, blue);
        a[2].radius  = 8.0;
        a[3]         = tbox_test_damage_rect_op(120.0, 10.0, 80.0, 50.0, black);
        a[3].kind    = TBOX_PAINT_FILL_RING;
        a[3].inner_rect = (tbox_rect){ 124.0, 14.0, 72.0, 42.0 };
        for (int i = 0; i < 4; i++)
            a[3].corner_radii[i] = 10.0, a[3].inner_corner_radii[i] = 6.0;
        a[4]          = tbox_test_damage_rect_op(20.0, 80.0, 0.0, 20.0, black);
        a[4].kind     = TBOX_PAINT_TEXT_RUN;
        a[4].text     = tbox_test_raster_view_from_cstr("Partial gjy");
        a[4].face     = face;
        a[4].rect.width = tbox_font_measure_text(face, a[4].text);
        a[4].rect.height = tbox_font_face_line_height(face);
        a[5]          = tbox_test_damage_rect_op(150.5, 90.5, 30.0, 30.0, (tbox_css_rgba){ 0, 0, 0, 200 });
        a[5].kind     = TBOX_PAINT_IMAGE;
        a[5].image    = &image;
        a[6]          = tbox_test_damage_rect_op(100.0, 120.0, 120.0, 30.0, (tbox_css_rgba){ 0, 0, 0, 255 });
        a[6].kind     = TBOX_PAINT_GRADIENT;
        a[6].gradient = &gradient;
        a[6].has_clip = true;
        a[6].clip     = (tbox_rect){ 110.4, 118.0, 90.0, 40.0 };
        tbox_display_list list_a = { a, COUNT };

        tbox_paint_op b[COUNT];
        tbox_display_list list_b = { b, COUNT };

        memcpy(b, a, sizeof(a));
        b[1].color = blue;
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "recolored fractional rect");

        memcpy(b, a, sizeof(a));
        b[2].rect.x += 13.0, b[2].rect.y -= 5.0;
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "moved rounded rect over others");

        memcpy(b, a, sizeof(a));
        b[4].text       = tbox_test_raster_view_from_cstr("Changed text");
        b[4].rect.width = tbox_font_measure_text(face, b[4].text);
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "changed text run");

        memcpy(b, a, sizeof(a));
        b[5].rect = (tbox_rect){ 60.2, 100.8, 18.0, 24.0 };
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "moved and resized image");

        memcpy(b, a, sizeof(a));
        b[6].clip.width -= 25.0;
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "narrowed gradient clip");

        memcpy(b, a, sizeof(a));
        b[3].color = red;
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "recolored ring");

        memcpy(b, a, sizeof(a));
        b[1] = a[2], b[2] = a[1];
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "swapped overlapping ops");

        list_b.count = COUNT - 1;
        memcpy(b, a, sizeof(a));
        tbox_test_damage_expect_equivalent(&failures, &list_a, &list_b, width, height, "removed last op");

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
    tbox_test_raster_image_basic(&failures);
    tbox_test_raster_image_scaling(&failures);
    tbox_test_raster_image_pixelated(&failures);
    tbox_test_raster_image_alpha_blend(&failures);
    tbox_test_raster_image_out_of_bounds(&failures);
    tbox_test_raster_image_invalid_args(&failures);
    tbox_test_raster_rounded_rect_corners(&failures);
    tbox_test_raster_rounded_rect_zero_radius_matches_fill_rect(&failures);
    tbox_test_raster_rounded_rect_radius_clamped(&failures);
    tbox_test_raster_rounded_rect_out_of_bounds(&failures);
    tbox_test_raster_rounded_rect_invalid_args(&failures);
    tbox_test_raster_individual_corners(&failures);
    tbox_test_raster_wavy_line(&failures);
    tbox_test_raster_gradient(&failures);
    tbox_test_raster_elliptical_corners(&failures);
    tbox_test_raster_write_png_round_trip(&failures);
    tbox_test_raster_write_png_invalid_args(&failures);
    tbox_test_raster_damage_tracker(&failures);

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");

    if (font_data != NULL) {
        tbox_test_raster_text_run_basic(&failures, font_data, font_size);
        tbox_test_raster_display_list_matches_direct_calls(&failures, font_data, font_size);
        tbox_test_raster_damage_pixel_equivalence(&failures, font_data, font_size);
        free(font_data);
    } else {
        failures++;
    }

    return failures;
}
