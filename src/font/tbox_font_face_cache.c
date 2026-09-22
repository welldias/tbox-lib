#include <tbox/font.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* One already-loaded face, keyed by (family, bold, italic, size_px) -- see
 * tbox_font_face_cache_get in <tbox/font.h>. family[0] == '\0' means the
 * default (empty) family -- the entries populated from regular_data/
 * bold_data, same as before this cache understood any other family. */
typedef struct tbox_font_face_cache_entry {
    char family[64];
    bool bold;
    bool italic;
    double size_px;
    tbox_font_face *face;
} tbox_font_face_cache_entry;

/* One family's resolved font bytes, keyed by (family, bold, italic) --
 * resolved at most once per triple via `resolver`, no matter how many
 * distinct size_px values tbox_font_face_cache_get later loads a
 * tbox_font_face for. `data`/`size` are copied into the cache's arena
 * (tbox_font_face_cache_copy_bytes), same as regular_data/bold_data. */
typedef struct tbox_font_face_cache_family_blob {
    char family[64];
    bool bold;
    bool italic;
    const void *data;
    size_t size;
} tbox_font_face_cache_family_blob;

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
    tbox_font_resolver_fn resolver;
    void *resolver_userdata;
    tbox_vector entries;      /* tbox_font_face_cache_entry, linear-scanned by _get. */
    tbox_vector family_blobs; /* tbox_font_face_cache_family_blob, linear-scanned by _get. */
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

tbox_font_face_cache *tbox_font_face_cache_create(const void *regular_data, size_t regular_size, const void *bold_data, size_t bold_size, tbox_font_resolver_fn resolver, void *resolver_userdata) {
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

    cache->resolver          = resolver;
    cache->resolver_userdata = resolver_userdata;

    tbox_vector_init(&cache->entries, &cache->arena, sizeof(tbox_font_face_cache_entry), 0);
    tbox_vector_init(&cache->family_blobs, &cache->arena, sizeof(tbox_font_face_cache_family_blob), 0);

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

const tbox_font_face *tbox_font_face_cache_get(tbox_font_face_cache *cache, tbox_string_view family, bool bold, bool italic, double size_px) {
    if (cache == NULL) {
        return NULL;
    }

    /* Truncate-not-reject, same posture as
     * tbox_font_source_fontconfig_family_cstr (src/font/tbox_font_source_fontconfig.c)
     * -- always NUL-terminated. */
    char family_buf[64];
    size_t family_len = family.size < sizeof(family_buf) - 1 ? family.size : sizeof(family_buf) - 1;
    if (family_len > 0) {
        memcpy(family_buf, family.data, family_len);
    }
    family_buf[family_len] = '\0';

    size_t entry_count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < entry_count; i++) {
        tbox_font_face_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        if (entry->bold == bold && entry->italic == italic && entry->size_px == size_px && strcmp(entry->family, family_buf) == 0) {
            return entry->face;
        }
    }

    const void *data = NULL;
    size_t size      = 0;

    if (family_buf[0] == '\0' && !italic) {
        /* Default family, non-italic -- exactly the original v0..v12
         * behavior, no resolver call: regular_data/bold_data were preloaded
         * eagerly for exactly this case. */
        data = bold ? cache->bold_data : cache->regular_data;
        size = bold ? cache->bold_size : cache->regular_size;
    } else {
        /* Non-default family, OR the default family with italic requested
         * (NOVO v13: regular_data/bold_data have no italic variant of their
         * own -- resolving one on demand, same mechanism as any other
         * family, is what lets `<i>`/`<em>` render truly slanted without a
         * `font-family` declared anywhere in the ancestor chain, the
         * scenario ARCHITECTURE.md's v13 "Fatia vertical" criterion
         * requires. An empty `family` view reaches the resolver unchanged --
         * tbox_font_source_fontconfig_family_cstr already treats an empty
         * family as "sans-serif", the exact same substitution
         * tbox_app_resolve_font_source's eager bootstrap already hardcodes
         * literally for regular_data/bold_data, so no Application-layer
         * change is needed for this to work end to end.)
         *
         * Reuse an already-resolved (family, bold, italic) blob if one
         * exists, otherwise resolve it once via `resolver` and cache the
         * bytes so later size_px values for this same (family, bold,
         * italic) never call the resolver again. */
        tbox_font_face_cache_family_blob *blob = NULL;
        size_t blob_count                      = tbox_vector_length(&cache->family_blobs);
        for (size_t i = 0; i < blob_count; i++) {
            tbox_font_face_cache_family_blob *candidate = tbox_vector_at(&cache->family_blobs, i);
            if (candidate->bold == bold && candidate->italic == italic && strcmp(candidate->family, family_buf) == 0) {
                blob = candidate;
                break;
            }
        }

        if (blob == NULL) {
            if (cache->resolver == NULL) {
                return NULL;
            }

            tbox_font_query query = {
                .family = family,
                .bold   = bold,
                .italic = italic,
            };

            const void *resolved_data = NULL;
            size_t resolved_size      = 0;
            if (!cache->resolver(cache->resolver_userdata, query, &resolved_data, &resolved_size)) {
                return NULL;
            }

            const void *copied_data = tbox_font_face_cache_copy_bytes(&cache->arena, resolved_data, resolved_size);
            if (copied_data == NULL) {
                return NULL;
            }

            tbox_font_face_cache_family_blob *new_blob = tbox_vector_push(&cache->family_blobs);
            memcpy(new_blob->family, family_buf, sizeof(family_buf));
            new_blob->bold   = bold;
            new_blob->italic = italic;
            new_blob->data   = copied_data;
            new_blob->size   = resolved_size;
            blob             = new_blob;
        }

        data = blob->data;
        size = blob->size;
    }

    tbox_font_face *face = tbox_font_face_load(data, size, size_px);
    if (face == NULL) {
        /* Nothing cached on failure -- a later retry with the same
         * parameters tries loading again rather than being stuck. */
        return NULL;
    }

    tbox_font_face_cache_entry *entry = tbox_vector_push(&cache->entries);
    memcpy(entry->family, family_buf, sizeof(family_buf));
    entry->bold    = bold;
    entry->italic  = italic;
    entry->size_px = size_px;
    entry->face    = face;
    return face;
}
