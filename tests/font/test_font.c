#include <tbox/font.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "font/tbox_font_measure.h"
#include "test_support.h"
#include "style/tbox_style_internal.h"

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
static void tbox_test_font_release(void *userdata) {
    (*(int *)userdata)++;
}

static bool tbox_test_font_resolver(void *userdata, const tbox_font_query *query, const void **out_data, size_t *out_size) {
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
        TBOX_TEST_ASSERT(tbox_font_face_has_glyph(face, 'A'));
        TBOX_TEST_ASSERT(!tbox_font_face_has_glyph(face, 0x2713));

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

        /* 5b: the face's run cache answers like a fresh face: glyphs asked
         * after a width-only measure, a kerning change, runs past the
         * cache's text limit, and after the cache empties itself. */
        tbox_font_face *fresh = tbox_font_face_load(font_data, font_size, 16.0);
        TBOX_TEST_ASSERT(fresh != NULL);
        if (fresh != NULL) {
            tbox_string_view word = tbox_test_font_view_from_cstr("AVATAR Wave");
            double width          = tbox_font_measure_text(face, word);
            tbox_arena arena      = tbox_arena_create(0);
            tbox_glyph *cached, *shaped;
            size_t cached_count, shaped_count;
            double cached_width, shaped_width;
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, face, word, 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &cached, &cached_count, &cached_width));
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, fresh, word, 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &shaped, &shaped_count, &shaped_width));
            TBOX_TEST_ASSERT(cached_width == width && cached_width == shaped_width && cached_count == shaped_count);
            TBOX_TEST_ASSERT(cached_count > 0 && memcmp(cached, shaped, cached_count * sizeof(*cached)) == 0);

            tbox_font_face_set_kerning(face, true);
            tbox_font_face_set_kerning(fresh, true);
            TBOX_TEST_ASSERT(tbox_font_measure_text(face, word) == tbox_font_measure_text(fresh, word));
            tbox_font_face_set_kerning(face, false);
            TBOX_TEST_ASSERT(tbox_font_measure_text(face, word) == width);
            tbox_font_face_set_kerning(fresh, false);

            char long_text[600];
            for (size_t i = 0; i < sizeof(long_text); i++)
                long_text[i] = (char)('a' + i % 26);
            tbox_string_view long_view = tbox_string_view_make(long_text, sizeof(long_text));
            TBOX_TEST_ASSERT(tbox_font_measure_text(face, long_view) == tbox_font_measure_text(fresh, long_view));

            char distinct[16];
            for (int i = 0; i < 3000; i++) {
                int length = snprintf(distinct, sizeof(distinct), "w%d", i);
                tbox_font_measure_text(face, tbox_string_view_make(distinct, (size_t)length));
            }
            TBOX_TEST_ASSERT(tbox_font_measure_text(face, word) == width);

            /* Past the cache's largest table: the table grows, then sweeps
             * the runs nobody asked for. A run asked for all along, one
             * moved by every rebuild and one swept out still shape as on
             * a fresh face. */
            tbox_string_view early = tbox_test_font_view_from_cstr("w7");
            for (int i = 0; i < 70000; i++) {
                int length = snprintf(distinct, sizeof(distinct), "x%d", i);
                tbox_font_measure_text(face, tbox_string_view_make(distinct, (size_t)length));
                if (i % 1000 == 0) {
                    TBOX_TEST_ASSERT(tbox_font_shape(&arena, face, word, 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &cached, &cached_count, &cached_width));
                    TBOX_TEST_ASSERT(cached_count == shaped_count && memcmp(cached, shaped, cached_count * sizeof(*cached)) == 0);
                }
            }
            tbox_string_view checks[] = { word, early, tbox_test_font_view_from_cstr("x69999") };
            for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
                TBOX_TEST_ASSERT(tbox_font_shape(&arena, face, checks[i], 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &cached, &cached_count, &cached_width));
                TBOX_TEST_ASSERT(tbox_font_shape(&arena, fresh, checks[i], 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &shaped, &shaped_count, &shaped_width));
                TBOX_TEST_ASSERT(cached_width == shaped_width && cached_count == shaped_count);
                TBOX_TEST_ASSERT(cached_count > 0 && memcmp(cached, shaped, cached_count * sizeof(*cached)) == 0);
            }
            tbox_arena_destroy(&arena);
            tbox_font_face_destroy(fresh);
        }

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
        TBOX_TEST_ASSERT(resolved_data != NULL && memcmp(resolved_data, font_data, font_size) == 0);

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
    tbox_font_face_cache *cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
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

        /* 11b:  -- an empty family with italic=true no longer takes
         * the default (regular_data/bold_data) fast path (those have no
         * italic variant of their own): it falls through to the same
         * on-demand resolution as any other family, so on a cache with
         * resolver == NULL (this one), it returns NULL instead of silently
         * handing back the non-italic default face. */
        const tbox_font_face *regular_16_italic = tbox_font_face_cache_get(cache, no_family, false, true, 16.0);
        TBOX_TEST_ASSERT(regular_16_italic == NULL);

        /* 11c: the prefix measurer shapes the whole text once; prefixes
         * never shrink, the last one is exactly the measured width, and a
         * cluster of several codepoints (a ligature, when the shaper makes
         * one) has its width split evenly. A masked one measures the mask;
         * a NULL face measures 0. */
        if (regular_16 != NULL) {
            const char text[] = "AVA To\xc3\xa9\xe2\x82\xac W. office";
            size_t size       = sizeof(text) - 1;
            tbox_text_measurer measurer;
            tbox_text_measurer_init(&measurer, regular_16, 0.0, text, size);
            bool monotonic = true;
            double before  = 0.0;
            size_t steps   = 0;
            while (measurer.at < size) {
                tbox_text_measurer_step(&measurer);
                monotonic = monotonic && measurer.width >= before;
                before    = measurer.width;
                steps++;
            }
            TBOX_TEST_ASSERT(monotonic && steps == measurer.count && steps == 18);
            TBOX_TEST_ASSERT(measurer.width == tbox_font_measure_text_spaced(regular_16, tbox_string_view_make(text, size), 0.0));
            TBOX_TEST_ASSERT(tbox_text_measurer_total(&measurer) == measurer.width);
            tbox_text_measurer_finish(&measurer);

            tbox_arena arena = tbox_arena_create(0);
            tbox_glyph *glyphs;
            size_t glyph_count;
            tbox_string_view fi = tbox_string_view_make("fi", 2);
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, regular_16, fi, 0.0, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &glyph_count, NULL));
            if (glyph_count == 1) { /* the face (and shaper) made a ligature */
                tbox_text_measurer_init(&measurer, regular_16, 0.0, fi.data, fi.size);
                tbox_text_measurer_step(&measurer);
                TBOX_TEST_ASSERT(measurer.width == glyphs[0].advance / 2.0);
                tbox_text_measurer_step(&measurer);
                TBOX_TEST_ASSERT(measurer.width == glyphs[0].advance);
                tbox_text_measurer_finish(&measurer);
            }
            /* RTL (a bidi level): visual order, clusters descending, and
             * brackets mirrored (L4) -- "a(" draws as ")a". */
            tbox_glyph *close_glyph, *rtl;
            size_t close_count, rtl_count;
            double ltr_width, rtl_width;
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, regular_16, tbox_string_view_make(")", 1), 0.0, TBOX_FONT_DIRECTION_LTR, 0.0, 0, &close_glyph, &close_count, NULL));
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, regular_16, tbox_string_view_make("a(", 2), 0.0, TBOX_FONT_DIRECTION_RTL, 0.0, 0, &rtl, &rtl_count, &rtl_width));
            TBOX_TEST_ASSERT(close_count == 1 && rtl_count == 2 && rtl[0].id == close_glyph[0].id && rtl[0].cluster == 1 && rtl[1].cluster == 0 && rtl[0].x < rtl[1].x);
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, regular_16, tbox_string_view_make("a(", 2), 0.0, TBOX_FONT_DIRECTION_LTR, 0.0, 0, &glyphs, &glyph_count, &ltr_width));
            TBOX_TEST_ASSERT(glyph_count == 2 && glyphs[0].cluster == 0 && (ltr_width - rtl_width < 0.5 && rtl_width - ltr_width < 0.5));
            tbox_arena_destroy(&arena);

            tbox_text_measurer masked;
            tbox_text_measurer_init_masked(&masked, regular_16, 0.75, text, size, '*');
            TBOX_TEST_ASSERT(tbox_text_measurer_total(&masked) == tbox_font_measure_text_spaced(regular_16, tbox_string_view_make("******************", 18), 0.75));
            tbox_text_measurer_finish(&masked);

            tbox_text_measurer none;
            tbox_text_measurer_init(&none, NULL, 0.75, text, size);
            TBOX_TEST_ASSERT(tbox_text_measurer_step(&none) == 1 && none.width == 0.0);
            tbox_text_measurer_finish(&none);
        }

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
    tbox_font_face_cache *resolving_cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, tbox_test_font_resolver, &resolver_state, NULL);
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

        /* 18b:  -- an empty family WITH italic=true, on a cache with
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

    /* the resolver's userdata is released once, by the cache's destroy. */
    {
        int released                 = 0;
        tbox_font_face_cache *owning = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, &released, tbox_test_font_release);
        TBOX_TEST_ASSERT(owning != NULL && released == 0);
        tbox_font_face_cache_destroy(owning);
        TBOX_TEST_ASSERT(released == 1);
    }

    /* 19: a cache with resolver == NULL falls back to the default family
     * for a non-empty one, without crashing; so does a list of unknown
     * names, and a known name later in a list wins. */
    tbox_font_face_cache *no_resolver_cache = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
    TBOX_TEST_ASSERT(no_resolver_cache != NULL);

    if (no_resolver_cache != NULL) {
        tbox_string_view family = tbox_test_font_view_from_cstr("Alguma Familia");
        const tbox_font_face *fallback = tbox_font_face_cache_get(no_resolver_cache, tbox_test_font_view_from_cstr(""), false, false, 16.0);
        TBOX_TEST_ASSERT(fallback != NULL && tbox_font_face_cache_get(no_resolver_cache, family, false, false, 16.0) == fallback);
        TBOX_TEST_ASSERT(tbox_font_face_cache_get(no_resolver_cache, tbox_test_font_view_from_cstr("A, 'B C'"), false, false, 16.0) == fallback);
        TBOX_TEST_ASSERT(tbox_font_face_cache_register(no_resolver_cache, tbox_test_font_view_from_cstr("Local"), 400, false, font_data, font_size));
        const tbox_font_face *local = tbox_font_face_cache_get(no_resolver_cache, tbox_test_font_view_from_cstr("Missing, \"Local\", serif"), false, false, 16.0);
        TBOX_TEST_ASSERT(local != NULL && local != fallback);

        tbox_font_face_cache_destroy(no_resolver_cache);
    }

    /* Real OpenType behavior with a small, licensed fixture font. Cache
     * identity, measurement and prefix carets must all see the selection. */
    {
        size_t ot_size = 0;
        char *ot_data = read_file(TBOX_TEST_ASSETS_DIR "/opentype-sans.ttf", &ot_size);
        TBOX_TEST_REQUIRE(ot_data != NULL);
        tbox_font_face_cache *ot_cache = tbox_font_face_cache_create(ot_data, ot_size, ot_data, ot_size, NULL, NULL, NULL);
        free(ot_data);
        TBOX_TEST_REQUIRE(ot_cache != NULL);
        tbox_string_view family = tbox_string_view_make(NULL, 0);
        const tbox_font_face *normal = tbox_font_face_cache_get(ot_cache, family, false, false, 24.0);
        tbox_style selection              = {0};
        tbox_style_font_settings settings = {0};
        selection.font_settings           = &settings;
        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-feature-settings", tbox_test_font_view_from_cstr("'liga' 0"), &selection, &settings));
        tbox_font_shaping_options options;
        tbox_style_shaping_options(&selection, &options);
        const tbox_font_face *no_liga = tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options);
        TBOX_TEST_REQUIRE(normal != NULL && no_liga != NULL);
        TBOX_TEST_ASSERT(normal != no_liga);
        TBOX_TEST_ASSERT(no_liga == tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options));
        options.language = 0x54524b20u;
        const tbox_font_face *turkish = tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options);
        TBOX_TEST_ASSERT(turkish != NULL && turkish != no_liga);
        tbox_arena arena = tbox_arena_create(0);
        tbox_glyph *before = NULL, *after = NULL;
        size_t before_count = 0, after_count = 0;
        double width = 0;
        tbox_string_view office = tbox_test_font_view_from_cstr("office");
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, normal, office, 0, TBOX_FONT_DIRECTION_AUTO, 0, 0, &before, &before_count, NULL));
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, no_liga, office, 0, TBOX_FONT_DIRECTION_AUTO, 0, 0, &after, &after_count, &width));
        TBOX_TEST_ASSERT(after_count == 6 && width == tbox_font_measure_text(no_liga, office));
        TBOX_TEST_ASSERT(before_count == (TBOX_TEST_COMPLEX_SHAPING ? 4u : 6u));
        tbox_text_measurer measurer;
        tbox_text_measurer_init(&measurer, no_liga, 0, office.data, office.size);
        while (measurer.at < office.size) tbox_text_measurer_step(&measurer);
        TBOX_TEST_ASSERT(measurer.width == width);
        tbox_text_measurer_finish(&measurer);
        /* Letter spacing still disables optional ligatures. */
        TBOX_TEST_ASSERT(tbox_font_shape(&arena, normal, office, 1, TBOX_FONT_DIRECTION_AUTO, 0, 0, &after, &after_count, NULL));
        TBOX_TEST_ASSERT(after_count == 6);

        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-feature-settings", tbox_test_font_view_from_cstr("normal"), &selection, &settings));
        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-variant-numeric", tbox_test_font_view_from_cstr("proportional-nums"), &selection, &settings));
        tbox_style_shaping_options(&selection, &options);
        const tbox_font_face *proportional = tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options);
        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-variant-numeric", tbox_test_font_view_from_cstr("tabular-nums"), &selection, &settings));
        tbox_style_shaping_options(&selection, &options);
        const tbox_font_face *tabular = tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options);
        TBOX_TEST_REQUIRE(proportional != NULL && tabular != NULL);
        tbox_string_view ones = tbox_test_font_view_from_cstr("1111");
        double p_width = tbox_font_measure_text(proportional, ones);
        double t_width = tbox_font_measure_text(tabular, ones);
        TBOX_TEST_ASSERT(TBOX_TEST_COMPLEX_SHAPING ? p_width < t_width : p_width == t_width);
        tbox_text_measurer_init(&measurer, tabular, 0, ones.data, ones.size);
        while (measurer.at < ones.size) tbox_text_measurer_step(&measurer);
        TBOX_TEST_ASSERT(measurer.width == t_width);
        tbox_text_measurer_finish(&measurer);
        static const struct { const char *property, *value, *text; } changes[] = {
            { "font-variant-numeric", "oldstyle-nums", "1234567890" },
            { "font-variant-numeric", "diagonal-fractions", "1/2" },
            { "font-feature-settings", "'smcp'", "abc" },
        };
        for (size_t i = 0; i < sizeof(changes) / sizeof(changes[0]); i++) {
            selection               = (tbox_style){0};
            settings                = (tbox_style_font_settings){0};
            selection.font_settings = &settings;
            TBOX_TEST_ASSERT(tbox_style_parse_opentype(changes[i].property, tbox_test_font_view_from_cstr(changes[i].value), &selection, &settings));
            tbox_style_shaping_options(&selection, &options);
            const tbox_font_face *changed = tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options);
            TBOX_TEST_REQUIRE(changed != NULL);
            tbox_string_view text = tbox_test_font_view_from_cstr(changes[i].text);
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, normal, text, 0, TBOX_FONT_DIRECTION_AUTO, 0, 0, &before, &before_count, NULL));
            TBOX_TEST_ASSERT(tbox_font_shape(&arena, changed, text, 0, TBOX_FONT_DIRECTION_AUTO, 0, 0, &after, &after_count, NULL));
            bool different = before_count != after_count;
            for (size_t j = 0; j < before_count && j < after_count; j++)
                different |= before[j].id != after[j].id;
            TBOX_TEST_ASSERT_MSG(different == (bool)TBOX_TEST_COMPLEX_SHAPING, changes[i].value);
        }
        /* Same options are still cached after many intervening lookups. */
        TBOX_TEST_ASSERT(tbox_style_parse_opentype("font-feature-settings", tbox_test_font_view_from_cstr("'liga' 0"), &selection, &settings));
        selection.font_variant_numeric = 0;
        tbox_style_shaping_options(&selection, &options);
        TBOX_TEST_ASSERT(no_liga == tbox_font_face_cache_get_shaped(ot_cache, family, 400, 100, false, 24, true, &options));
        tbox_arena_destroy(&arena);
        tbox_font_face_cache_destroy(ot_cache);
    }

    free(font_data);

    return failures;
}
