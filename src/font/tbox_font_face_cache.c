#include <tbox/font.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* One already-loaded face, keyed by (bold, size_px) -- see
 * tbox_font_face_cache_get in <tbox/font.h>. */
typedef struct tbox_font_face_cache_entry {
    bool bold;
    double size_px;
    tbox_font_face *face;
} tbox_font_face_cache_entry;

/* Own lifetime (real _destroy, no caller arena), same as tbox_css_stylesheet
 * (see src/css_parser/tbox_css_stylesheet.c) -- the internal tbox_arena
 * below is purely an implementation detail for the copied byte buffers and
 * the entries vector's backing storage, not the caller-arena pattern
 * ARCHITECTURE.md's "Convenções" warns not to mix with a dedicated
 * _destroy. */
struct tbox_font_face_cache {
    tbox_arena arena;
    const void *regular_data;
    size_t regular_size;
    const void *bold_data;
    size_t bold_size;
    tbox_vector entries; /* tbox_font_face_cache_entry, linear-scanned by _get. */
};

static void *tbox_font_face_cache_copy_bytes(tbox_arena *arena, const void *data, size_t size) {
    void *copy = tbox_arena_alloc(arena, size);
    if (copy == NULL) {
        return NULL;
    }

    if (size > 0) {
        memcpy(copy, data, size);
    }
    return copy;
}

tbox_font_face_cache *tbox_font_face_cache_create(const void *regular_data, size_t regular_size, const void *bold_data, size_t bold_size) {
    tbox_font_face_cache *cache = malloc(sizeof(*cache));
    if (cache == NULL) {
        return NULL;
    }

    cache->arena = tbox_arena_create(0);
    if (cache->arena.current == NULL) {
        free(cache);
        return NULL;
    }

    cache->regular_data = tbox_font_face_cache_copy_bytes(&cache->arena, regular_data, regular_size);
    cache->bold_data    = tbox_font_face_cache_copy_bytes(&cache->arena, bold_data, bold_size);
    if (cache->regular_data == NULL || cache->bold_data == NULL) {
        tbox_arena_destroy(&cache->arena);
        free(cache);
        return NULL;
    }
    cache->regular_size = regular_size;
    cache->bold_size    = bold_size;

    tbox_vector_init(&cache->entries, &cache->arena, sizeof(tbox_font_face_cache_entry), 0);

    return cache;
}

void tbox_font_face_cache_destroy(tbox_font_face_cache *cache) {
    if (cache == NULL) {
        return;
    }

    size_t count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < count; i++) {
        tbox_font_face_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        tbox_font_face_destroy(entry->face);
    }

    /* Frees the copied regular_data/bold_data buffers and the entries
     * vector's backing storage too -- both live in this same arena. */
    tbox_arena_destroy(&cache->arena);
    free(cache);
}

const tbox_font_face *tbox_font_face_cache_get(tbox_font_face_cache *cache, bool bold, double size_px) {
    if (cache == NULL) {
        return NULL;
    }

    size_t count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < count; i++) {
        tbox_font_face_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        if (entry->bold == bold && entry->size_px == size_px) {
            return entry->face;
        }
    }

    const void *data = bold ? cache->bold_data : cache->regular_data;
    size_t size      = bold ? cache->bold_size : cache->regular_size;

    tbox_font_face *face = tbox_font_face_load(data, size, size_px);
    if (face == NULL) {
        /* Nothing cached on failure -- a later retry with the same
         * parameters tries loading again rather than being stuck. */
        return NULL;
    }

    tbox_font_face_cache_entry *entry = tbox_vector_push(&cache->entries);
    entry->bold                       = bold;
    entry->size_px                    = size_px;
    entry->face                       = face;
    return face;
}
