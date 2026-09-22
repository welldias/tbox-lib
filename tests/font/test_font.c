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

/* Test-only resolver state: the vendored font bytes to always resolve to
 * (regardless of the family/bold/italic requested) plus a call counter.
 * Lives as a plain local (stack) variable in tbox_test_font_run, passed to
 * tbox_font_face_cache_create as resolver_userdata -- never static/global,
 * per the project's "no new global/static mutable state" rule. */
typedef struct tbox_test_font_resolver_state {
    const void *font_data;
    size_t font_size;
    int calls;
} tbox_test_font_resolver_state;

/* Always resolves to the same vendored bytes already used by the rest of
 * this file, no matter which family/bold/italic is requested -- only
 * increments state->calls so tests can assert the cache does/doesn't
 * re-resolve. */
static bool tbox_test_font_resolver(void *userdata, tbox_font_query query, const void **out_data, size_t *out_size) {
    (void)query;
    tbox_test_font_resolver_state *state = userdata;
    state->calls++;
    *out_data = state->font_data;
    *out_size = state->font_size;
    return true;
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
    tbox_font_face_cache *cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL);
    TBOX_TEST_ASSERT(cache != NULL);

    if (cache != NULL) {
        tbox_string_view no_family = tbox_string_view_make(NULL, 0);

        /* 8: _get(regular, 16) does not return NULL. */
        const tbox_font_face *regular_16 = tbox_font_face_cache_get(cache, no_family, false, false, 16.0);
        TBOX_TEST_ASSERT(regular_16 != NULL);

        /* 9: a second _get(regular, 16) returns the exact same pointer --
         * cache hit, does not reload. */
        const tbox_font_face *regular_16_again = tbox_font_face_cache_get(cache, no_family, false, false, 16.0);
        TBOX_TEST_ASSERT(regular_16_again == regular_16);

        /* 10: _get(bold, 16) returns a pointer DIFFERENT from
         * _get(regular, 16), even though both happen to load from the same
         * underlying bytes in this test, since they're cached under
         * different keys. */
        const tbox_font_face *bold_16 = tbox_font_face_cache_get(cache, no_family, true, false, 16.0);
        TBOX_TEST_ASSERT(bold_16 != NULL);
        TBOX_TEST_ASSERT(bold_16 != regular_16);

        /* 11: _get(regular, 32) returns a pointer different from
         * _get(regular, 16) -- different size_px, different cache key. */
        const tbox_font_face *regular_32 = tbox_font_face_cache_get(cache, no_family, false, false, 32.0);
        TBOX_TEST_ASSERT(regular_32 != NULL);
        TBOX_TEST_ASSERT(regular_32 != regular_16);

        /* 11b: NOVO v13 -- an empty family with italic=true no longer takes
         * the default (regular_data/bold_data) fast path (those have no
         * italic variant of their own): it falls through to the same
         * on-demand resolution as any other family, so on a cache with
         * resolver == NULL (this one), it returns NULL instead of silently
         * handing back the non-italic default face. */
        const tbox_font_face *regular_16_italic = tbox_font_face_cache_get(cache, no_family, false, true, 16.0);
        TBOX_TEST_ASSERT(regular_16_italic == NULL);

        /* 12: _get on a NULL cache returns NULL instead of crashing. */
        TBOX_TEST_ASSERT(tbox_font_face_cache_get(NULL, no_family, false, false, 16.0) == NULL);

        /* 13: _destroy does not crash. */
        tbox_font_face_cache_destroy(cache);
    }

    /* 14: _destroy(NULL) does not crash. */
    tbox_font_face_cache_destroy(NULL);

    /* 15: a cache built with a resolver resolves a non-empty family on
     * demand -- _get("Alguma Familia", regular, 16) does not return NULL,
     * and the resolver was called exactly once. */
    tbox_test_font_resolver_state resolver_state = {
        .font_data = font_data,
        .font_size = font_size,
        .calls     = 0,
    };
    tbox_font_face_cache *resolving_cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, tbox_test_font_resolver, &resolver_state);
    TBOX_TEST_ASSERT(resolving_cache != NULL);

    if (resolving_cache != NULL) {
        tbox_string_view family = tbox_test_font_view_from_cstr("Alguma Familia");

        const tbox_font_face *family_16 = tbox_font_face_cache_get(resolving_cache, family, false, false, 16.0);
        TBOX_TEST_ASSERT(family_16 != NULL);
        TBOX_TEST_ASSERT(resolver_state.calls == 1);

        /* 16: a second identical _get returns the SAME pointer and does NOT
         * call the resolver again -- cache hit on (family, bold, italic,
         * size_px). */
        const tbox_font_face *family_16_again = tbox_font_face_cache_get(resolving_cache, family, false, false, 16.0);
        TBOX_TEST_ASSERT(family_16_again == family_16);
        TBOX_TEST_ASSERT(resolver_state.calls == 1);

        /* 17: the same family/weight at a DIFFERENT size_px returns a
         * DIFFERENT pointer (a new tbox_font_face was loaded for the new
         * size), but the resolver is still NOT called again -- proof the
         * family's resolved blob was reused from family_blobs instead of
         * re-resolving. */
        const tbox_font_face *family_32 = tbox_font_face_cache_get(resolving_cache, family, false, false, 32.0);
        TBOX_TEST_ASSERT(family_32 != NULL);
        TBOX_TEST_ASSERT(family_32 != family_16);
        TBOX_TEST_ASSERT(resolver_state.calls == 1);

        /* 17b: _get(family, bold=false, italic=true, 16) returns a pointer
         * DIFFERENT from _get(family, bold=false, italic=false, 16) -- same
         * family/weight/size, but italic is part of the cache key too, so
         * this is a fresh resolve (a distinct (family, bold, italic) blob). */
        const tbox_font_face *family_16_italic = tbox_font_face_cache_get(resolving_cache, family, false, true, 16.0);
        TBOX_TEST_ASSERT(family_16_italic != NULL);
        TBOX_TEST_ASSERT(family_16_italic != family_16);
        TBOX_TEST_ASSERT(resolver_state.calls == 2);

        /* 17c: a second call with the exact same (family, bold, italic,
         * size_px) as 17b returns the SAME pointer -- cache hit, no new
         * resolver call. */
        const tbox_font_face *family_16_italic_again = tbox_font_face_cache_get(resolving_cache, family, false, true, 16.0);
        TBOX_TEST_ASSERT(family_16_italic_again == family_16_italic);
        TBOX_TEST_ASSERT(resolver_state.calls == 2);

        /* 18: regression -- an empty family, NON-italic, even on a cache
         * with a resolver configured, still uses the default path and never
         * invokes the resolver. */
        const tbox_font_face *default_16 = tbox_font_face_cache_get(resolving_cache, tbox_string_view_make(NULL, 0), false, false, 16.0);
        TBOX_TEST_ASSERT(default_16 != NULL);
        TBOX_TEST_ASSERT(resolver_state.calls == 2);

        /* 18b: NOVO v13 -- an empty family WITH italic=true, on a cache with
         * a resolver configured, DOES invoke the resolver (empty `family` is
         * passed through unchanged -- see tbox_font_face_cache_get's doc
         * comment) and returns a face DIFFERENT from default_16 (a distinct
         * (family="", bold=false, italic=true) cache key) -- this is what
         * lets `<i>`/`<em>` resolve a real italic face without any
         * `font-family` declared anywhere in the ancestor chain. */
        const tbox_font_face *default_16_italic = tbox_font_face_cache_get(resolving_cache, tbox_string_view_make(NULL, 0), false, true, 16.0);
        TBOX_TEST_ASSERT(default_16_italic != NULL);
        TBOX_TEST_ASSERT(default_16_italic != default_16);
        TBOX_TEST_ASSERT(resolver_state.calls == 3);

        /* 18c: a second identical call returns the SAME pointer and does NOT
         * call the resolver again -- cache hit, same as any other family. */
        const tbox_font_face *default_16_italic_again = tbox_font_face_cache_get(resolving_cache, tbox_string_view_make(NULL, 0), false, true, 16.0);
        TBOX_TEST_ASSERT(default_16_italic_again == default_16_italic);
        TBOX_TEST_ASSERT(resolver_state.calls == 3);

        tbox_font_face_cache_destroy(resolving_cache);
    }

    /* 19: a cache with resolver == NULL returns NULL for a non-empty family,
     * without crashing. */
    tbox_font_face_cache *no_resolver_cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL);
    TBOX_TEST_ASSERT(no_resolver_cache != NULL);

    if (no_resolver_cache != NULL) {
        tbox_string_view family = tbox_test_font_view_from_cstr("Alguma Familia");
        TBOX_TEST_ASSERT(tbox_font_face_cache_get(no_resolver_cache, family, false, false, 16.0) == NULL);

        tbox_font_face_cache_destroy(no_resolver_cache);
    }

    free(font_data);

    return failures;
}
