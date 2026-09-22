#include <tbox/image.h>

#include <stddef.h>
#include <string.h>

#include "test_support.h"

/* <tbox/string_view.h> (pulled in by <tbox/image.h>) only exposes
 * tbox_string_view_make -- same posture as tests/font/test_font.c's own
 * copy of this helper. */
static tbox_string_view tbox_test_image_view_from_cstr(const char *nul_terminated) {
    return tbox_string_view_make(nul_terminated, strlen(nul_terminated));
}

/* Real fixtures already on disk (see tests/assets/024.html, the maintainer's
 * own <img> regression fixture) -- black.png/yellow.png are both 200x200
 * RGBA PNGs, confirmed via PIL when this feature was designed; reused here
 * rather than a synthetic image so this test exercises the SAME stb_image
 * decode path the real 024.html/.png regression pair does. */
#ifndef TBOX_TEST_ASSETS_DIR
#define TBOX_TEST_ASSETS_DIR "tests/assets"
#endif

int tbox_test_image_run(void) {
    int failures = 0;

    /* 1: decoding a real PNG via a relative `src` joined against `base_dir`
     * succeeds, with the fixture's known intrinsic dimensions. */
    {
        tbox_image_cache *cache = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(cache != NULL);
        if (cache != NULL) {
            const tbox_image *image = tbox_image_cache_get(cache, tbox_test_image_view_from_cstr("black.png"));
            TBOX_TEST_ASSERT_MSG(image != NULL, "black.png should decode via base_dir-joined path");
            if (image != NULL) {
                TBOX_TEST_ASSERT_MSG(image->width == 200 && image->height == 200, "black.png is a known 200x200 fixture");
                TBOX_TEST_ASSERT(image->pixels != NULL);
            }
            tbox_image_cache_destroy(cache);
        }
    }

    /* 2: the SAME `src` on the SAME cache returns the identical pointer on a
     * second lookup -- decoded once, reused (see tbox_image_cache_get's own
     * doc comment), same "decode once" contract tbox_font_face_cache_get
     * already has for faces. */
    {
        tbox_image_cache *cache = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(cache != NULL);
        if (cache != NULL) {
            const tbox_image *first  = tbox_image_cache_get(cache, tbox_test_image_view_from_cstr("yellow.png"));
            const tbox_image *second = tbox_image_cache_get(cache, tbox_test_image_view_from_cstr("yellow.png"));
            TBOX_TEST_ASSERT_MSG(first != NULL && first == second, "repeated lookups of the same src must return the same cached tbox_image");
            tbox_image_cache_destroy(cache);
        }
    }

    /* 3: a missing/undecodable src returns NULL without crashing, and
     * nothing is cached on failure -- a later retry with the same src tries
     * again (same contract as tbox_font_face_cache_get's resolver-failure
     * case), proven here by two consecutive NULL results rather than a
     * crash on the second call. */
    {
        tbox_image_cache *cache = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(cache != NULL);
        if (cache != NULL) {
            const tbox_image *first  = tbox_image_cache_get(cache, tbox_test_image_view_from_cstr("notfound.png"));
            const tbox_image *second = tbox_image_cache_get(cache, tbox_test_image_view_from_cstr("notfound.png"));
            TBOX_TEST_ASSERT_MSG(first == NULL && second == NULL, "a missing src must return NULL every time, never crash");
            tbox_image_cache_destroy(cache);
        }
    }

    /* 4: a NULL base_dir falls back to using `src` exactly as given (no
     * joining) -- a relative src resolved against the CURRENT process
     * working directory still decodes when ctest happens to run from the
     * repo root (see tests/CMakeLists.txt), same as tbox_app_read_file's
     * existing no-op path handling for html_path/css_path with no base
     * directory concept at all. Kept tolerant of a different CWD (no
     * assertion on success), the only real assertion is "does not crash". */
    {
        tbox_image_cache *cache = tbox_image_cache_create(NULL);
        TBOX_TEST_ASSERT(cache != NULL);
        if (cache != NULL) {
            (void)tbox_image_cache_get(cache, tbox_test_image_view_from_cstr(TBOX_TEST_ASSETS_DIR "/black.png"));
            tbox_image_cache_destroy(cache);
        }
    }

    /* 5: NULL-safety -- tbox_image_cache_get(NULL, ...) and
     * tbox_image_cache_destroy(NULL) are no-ops, never crash, same
     * "everything tolerates NULL" posture the rest of tbox has. */
    {
        TBOX_TEST_ASSERT(tbox_image_cache_get(NULL, tbox_test_image_view_from_cstr("black.png")) == NULL);
        tbox_image_cache_destroy(NULL);
    }

    /* 6: an empty src is rejected (NULL) without crashing. */
    {
        tbox_image_cache *cache = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
        TBOX_TEST_ASSERT(cache != NULL);
        if (cache != NULL) {
            TBOX_TEST_ASSERT(tbox_image_cache_get(cache, tbox_string_view_make(NULL, 0)) == NULL);
            tbox_image_cache_destroy(cache);
        }
    }

    return failures;
}
