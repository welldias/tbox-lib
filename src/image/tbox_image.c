#include <tbox/image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* stb_image is third-party, vendored code (external/stb_image/, see its own
 * README.md) -- not held to this project's own -Wall -Wextra -Wpedantic
 * -Werror bar. Its implementation is pulled in exactly once, here, guarded
 * by pragmas so its own warnings (unused static decoders for formats this
 * project never triggers, signed/unsigned comparisons, ...) never fail this
 * project's build. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* Truncate-not-reject key size, same posture as
 * tbox_font_face_cache_entry.family (src/font/tbox_font_face_cache.c) --
 * generous for any realistic `src` value (a relative filename/path), never
 * rejected outright. */
#define TBOX_IMAGE_CACHE_SRC_BUF_SIZE 256
#define TBOX_IMAGE_CACHE_PATH_BUF_SIZE 4096

typedef struct tbox_image_cache_entry {
    char src[TBOX_IMAGE_CACHE_SRC_BUF_SIZE];
    tbox_image image; /* .pixels is stb_image-owned (STBI_MALLOC/free), freed via stbi_image_free at cache destroy */
} tbox_image_cache_entry;

/* Own lifetime (real _destroy, no caller arena), same pattern as
 * tbox_font_face_cache -- the internal tbox_arena is purely an
 * implementation detail for `entries`' backing storage and the copied
 * `base_dir` string, not the caller-arena pattern ARCHITECTURE.md's
 * "Convenções" warns not to mix with a dedicated _destroy. */
struct tbox_image_cache {
    tbox_arena arena;
    const char *base_dir; /* NULL means "no base_dir, use src as-is" */
    tbox_vector entries;  /* tbox_image_cache_entry, linear-scanned by _get */
};

tbox_image_cache *tbox_image_cache_create(const char *base_dir) {
    tbox_image_cache *cache = malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }

    cache->arena = tbox_arena_create(0);
    if (cache->arena.current == NULL) {
        free(cache);
        return NULL;
    }

    cache->base_dir = NULL;
    if (base_dir != NULL && base_dir[0] != '\0') {
        size_t len = strlen(base_dir);
        char *copy = (char *)tbox_arena_alloc(&cache->arena, len + 1);
        if (copy == NULL) {
            tbox_arena_destroy(&cache->arena);
            free(cache);
            return NULL;
        }
        memcpy(copy, base_dir, len + 1);
        cache->base_dir = copy;
    }

    tbox_vector_init(&cache->entries, &cache->arena, sizeof(tbox_image_cache_entry), 0);

    return cache;
}

void tbox_image_cache_destroy(tbox_image_cache *cache) {
    if (cache == NULL) {
        return;
    }

    size_t count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < count; i++) {
        tbox_image_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        stbi_image_free((void *)entry->image.pixels);
    }

    tbox_arena_destroy(&cache->arena);
    free(cache);
}

/* Joins `base_dir`/`src` into `out_path` (unless `src` is already absolute,
 * i.e. starts with '/', or `base_dir` is NULL) -- plain string concatenation
 * with a '/' separator, no normalization (no "..", no symlink resolution):
 * the same "no libgen/dirname dependency, no path-cleverness" posture
 * tbox_app_read_file already has for html_path/css_path (see
 * src/app/tbox_app.c). Returns false (nothing written past truncation) if
 * the joined path doesn't fit `out_path_size`. */
static bool tbox_image_cache_resolve_path(const char *base_dir, const char *src, char *out_path, size_t out_path_size) {
    bool is_absolute = src[0] == '/';
    int written;
    if (base_dir != NULL && !is_absolute) {
        written = snprintf(out_path, out_path_size, "%s/%s", base_dir, src);
    } else {
        written = snprintf(out_path, out_path_size, "%s", src);
    }
    return written >= 0 && (size_t)written < out_path_size;
}

const tbox_image *tbox_image_cache_get(tbox_image_cache *cache, tbox_string_view src) {
    if (cache == NULL || src.size == 0) {
        return NULL;
    }

    char src_buf[TBOX_IMAGE_CACHE_SRC_BUF_SIZE];
    size_t src_len = src.size < sizeof(src_buf) - 1 ? src.size : sizeof(src_buf) - 1;
    memcpy(src_buf, src.data, src_len);
    src_buf[src_len] = '\0';

    size_t entry_count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < entry_count; i++) {
        tbox_image_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        if (strcmp(entry->src, src_buf) == 0) {
            return &entry->image;
        }
    }

    char path[TBOX_IMAGE_CACHE_PATH_BUF_SIZE];
    if (!tbox_image_cache_resolve_path(cache->base_dir, src_buf, path, sizeof(path))) {
        return NULL;
    }

    int width, height, source_channels;
    const unsigned char *pixels = stbi_load(path, &width, &height, &source_channels, 4);
    if (pixels == NULL) {
        /* Nothing cached on failure -- a later retry with the same `src`
         * tries again rather than being permanently stuck, same contract as
         * tbox_font_face_cache_get's resolver-failure case. */
        return NULL;
    }

    tbox_image_cache_entry *entry = tbox_vector_push(&cache->entries);
    memcpy(entry->src, src_buf, sizeof(src_buf));
    entry->image.width  = width;
    entry->image.height = height;
    entry->image.pixels = pixels;
    return &entry->image;
}
