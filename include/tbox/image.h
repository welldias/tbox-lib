#ifndef TBOX_IMAGE_H
#define TBOX_IMAGE_H

#include <stdint.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * A decoded raster image (`<img src="...">`'s payload), owned by whichever
 * tbox_image_cache produced it -- never constructed or freed directly by a
 * caller. Decoding is via stb_image (vendored at external/stb_image/,
 * src/image/tbox_image.c), so any format it understands (PNG/JPEG/BMP/GIF/
 * ...) works; format detection is stb_image's own (from the file's magic
 * bytes, not the `src` extension). */
typedef struct tbox_image {
    int32_t width, height;
    const unsigned char *pixels; /* RGBA8, width*height*4 bytes, row-major, top-to-bottom */
} tbox_image;

/* Opaque: decodes-and-caches images by their `src` string, keyed exactly as
 * given (not the resolved file path) -- same "decode once, reuse" shape as
 * tbox_font_face_cache (<tbox/font.h>). Own lifetime (real _destroy, no
 * caller arena): loaded once per tbox_app_create (or
 * tbox_app_screenshot_from_files) call, persists across every Layout Tree
 * rebuild that call's tbox_context does afterward. */
typedef struct tbox_image_cache tbox_image_cache;

/* `base_dir`, if non-NULL and non-empty, is the directory a non-absolute
 * `src` (tbox_image_cache_get below) resolves against -- typically the HTML
 * file's own directory (see tbox_app_create_from_files_impl in
 * src/app/tbox_app.c), matching how a real browser resolves a local file's
 * relative `<img src>`. Copied into memory the cache owns; the caller's
 * buffer need not outlive this call. NULL/empty means "use `src` exactly as
 * given" -- the same posture tbox_app_read_file already has for html_path/
 * css_path today (no directory joining). Returns NULL only on allocation
 * failure. */
tbox_image_cache *tbox_image_cache_create(const char *base_dir);

/* Frees every decoded tbox_image this cache produced, plus the cache's own
 * copied base_dir and the cache struct itself. A no-op if cache == NULL. */
void tbox_image_cache_destroy(tbox_image_cache *cache);

/* Looks up `src` (an <img>'s raw src="" attribute value, e.g. "foo.png" or
 * "/abs/foo.png") in the cache; on a miss, joins it against `base_dir`
 * (skipped when `src` already starts with '/', or when the cache has no
 * base_dir) and decodes the resulting file via stb_image, caching the
 * result keyed by the UNJOINED `src` string -- the same `src` on the same
 * cache always returns the same tbox_image, even across many <img> tags
 * referencing the same file (decoded once, same motivation as
 * tbox_font_face_cache_get). Returns NULL if the file can't be opened or
 * stb_image can't decode it as an image -- nothing is cached on failure, so
 * a later retry with the same `src` tries again rather than being
 * permanently stuck (same contract as tbox_font_face_cache_get's resolver-
 * failure case). The returned pointer stays valid for `cache`'s whole
 * lifetime. */
const tbox_image *tbox_image_cache_get(tbox_image_cache *cache, tbox_string_view src);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_IMAGE_H */
