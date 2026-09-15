#include <tbox/font.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_support.h"

/* <tbox/string_view.h> (pulled in by <tbox/font.h>) only exposes
 * tbox_string_view_make; tbox_string_view_from_cstr lives in Base's
 * internal header, which this test deliberately doesn't include -- font.h's
 * public API is all it needs. */
static tbox_string_view tbox_test_font_view_from_cstr(const char *nul_terminated) {
    return tbox_string_view_make(nul_terminated, strlen(nul_terminated));
}

#ifndef TBOX_TEST_LIBERATION_SANS_PATH
#define TBOX_TEST_LIBERATION_SANS_PATH "external/liberation-sans/LiberationSans-Regular.ttf"
#endif

/* Reads the whole file into a malloc'd buffer (not NUL-terminated). Caller
 * frees the buffer with free(). Returns NULL and leaves *out_size untouched
 * on any I/O failure. Mirrors example/css_cascade_origins.c's read_file(). */
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

int tbox_test_font_run(void) {
    int failures = 0;

    size_t font_size = 0;
    char *font_data  = read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT_MSG(font_data != NULL, "failed to read vendored LiberationSans-Regular.ttf");
    if (font_data == NULL) {
        return failures + 1;
    }

    /* 1: tbox_font_face_load at 16px succeeds. */
    tbox_font_face *face = tbox_font_face_load(font_data, font_size, 16.0);
    TBOX_TEST_ASSERT(face != NULL);

    if (face != NULL) {
        /* 2: measuring an empty string is 0. */
        double empty_width = tbox_font_measure_text(face, tbox_string_view_make("", 0));
        TBOX_TEST_ASSERT(empty_width == 0.0);

        /* 3: measuring a longer string is wider than measuring a prefix of
         * it (relative comparison -- exact glyph metrics can vary by
         * FreeType version). */
        tbox_string_view prefix = tbox_test_font_view_from_cstr("Hello");
        tbox_string_view full   = tbox_test_font_view_from_cstr("Hello, world!");
        double prefix_width     = tbox_font_measure_text(face, prefix);
        double full_width       = tbox_font_measure_text(face, full);
        TBOX_TEST_ASSERT(prefix_width > 0.0);
        TBOX_TEST_ASSERT(full_width > prefix_width);

        /* 4: line_height is > 0. */
        double line_height = tbox_font_face_line_height(face);
        TBOX_TEST_ASSERT(line_height > 0.0);

        /* 5: rasterizing a common ASCII glyph produces width/height > 0. */
        tbox_font_glyph_bitmap glyph = tbox_font_rasterize_glyph(face, 'A');
        TBOX_TEST_ASSERT(glyph.width > 0);
        TBOX_TEST_ASSERT(glyph.height > 0);
        TBOX_TEST_ASSERT(glyph.alpha != NULL);

        tbox_font_face_destroy(face);
    }

    /* 6: tbox_font_source_embedded_create + resolve returns the same bytes
     * given to it, regardless of the query's family/bold/italic. */
    tbox_font_source *source = tbox_font_source_embedded_create(font_data, font_size);
    TBOX_TEST_ASSERT(source != NULL);

    if (source != NULL) {
        const void *resolved_data = NULL;
        size_t resolved_size      = 0;

        tbox_font_query serif_bold_italic = {
            .family = tbox_test_font_view_from_cstr("serif"),
            .bold   = true,
            .italic = true,
        };
        TBOX_TEST_ASSERT(tbox_font_source_resolve(source, serif_bold_italic, &resolved_data, &resolved_size));
        TBOX_TEST_ASSERT(resolved_size == font_size);
        TBOX_TEST_ASSERT(resolved_data != NULL);
        TBOX_TEST_ASSERT(memcmp(resolved_data, font_data, font_size) == 0);

        tbox_font_query sans_plain = {
            .family = tbox_test_font_view_from_cstr("sans-serif"),
            .bold   = false,
            .italic = false,
        };
        const void *resolved_data_2 = NULL;
        size_t resolved_size_2      = 0;
        TBOX_TEST_ASSERT(tbox_font_source_resolve(source, sans_plain, &resolved_data_2, &resolved_size_2));
        TBOX_TEST_ASSERT(resolved_size_2 == font_size);
        TBOX_TEST_ASSERT(resolved_data_2 == resolved_data);

        tbox_font_source_destroy(source);
    }

    /* 7: tbox_font_face_cache_create with valid regular+bold bytes does not
     * return NULL. Only one vendored font file exists, so the same bytes are
     * passed for both the "regular" and "bold" arguments -- the cache stores
     * two independent copies regardless, and these tests only need to prove
     * the cache mechanics (miss/hit, distinct keys), not that regular and
     * bold actually render differently. */
    tbox_font_face_cache *cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size);
    TBOX_TEST_ASSERT(cache != NULL);

    if (cache != NULL) {
        /* 8: _get(regular, 16) does not return NULL. */
        const tbox_font_face *regular_16 = tbox_font_face_cache_get(cache, false, 16.0);
        TBOX_TEST_ASSERT(regular_16 != NULL);

        /* 9: a second _get(regular, 16) returns the exact same pointer --
         * cache hit, does not reload. */
        const tbox_font_face *regular_16_again = tbox_font_face_cache_get(cache, false, 16.0);
        TBOX_TEST_ASSERT(regular_16_again == regular_16);

        /* 10: _get(bold, 16) returns a pointer DIFFERENT from
         * _get(regular, 16), even though both happen to load from the same
         * underlying bytes in this test, since they're cached under
         * different keys. */
        const tbox_font_face *bold_16 = tbox_font_face_cache_get(cache, true, 16.0);
        TBOX_TEST_ASSERT(bold_16 != NULL);
        TBOX_TEST_ASSERT(bold_16 != regular_16);

        /* 11: _get(regular, 32) returns a pointer different from
         * _get(regular, 16) -- different size_px, different cache key. */
        const tbox_font_face *regular_32 = tbox_font_face_cache_get(cache, false, 32.0);
        TBOX_TEST_ASSERT(regular_32 != NULL);
        TBOX_TEST_ASSERT(regular_32 != regular_16);

        /* 12: _get on a NULL cache returns NULL instead of crashing. */
        TBOX_TEST_ASSERT(tbox_font_face_cache_get(NULL, false, 16.0) == NULL);

        /* 13: _destroy does not crash. */
        tbox_font_face_cache_destroy(cache);
    }

    /* 14: _destroy(NULL) does not crash. */
    tbox_font_face_cache_destroy(NULL);

    free(font_data);

    return failures;
}
