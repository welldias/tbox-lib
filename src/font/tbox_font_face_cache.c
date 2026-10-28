#include <tbox/font.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "tbox_font_face_internal.h"

/* One already-loaded face, keyed by (family, bold, italic, size_px) -- see
 * tbox_font_face_cache_get in <tbox/font.h>. family[0] == '\0' means the
 * default (empty) family -- the entries populated from regular_data/
 * bold_data, same as before this cache understood any other family. */
typedef struct tbox_font_face_cache_entry {
    char family[TBOX_FONT_FAMILY_CAPACITY];
    size_t family_size; /* strlen(family) */
    bool kerning;
    int weight;
    double stretch;
    bool italic;
    double size_px;
    tbox_font_face *face;
    unsigned generation;
    bool alias; /* a family list's key for a face another entry owns */
} tbox_font_face_cache_entry;

/* One family's resolved font bytes, keyed by (family, bold, italic) --
 * resolved at most once per triple via `resolver`, no matter how many
 * distinct size_px values tbox_font_face_cache_get later loads a
 * tbox_font_face for. `data`/`size` are copied into the cache's arena
 * (tbox_font_face_cache_copy_bytes), same as regular_data/bold_data. */
typedef struct tbox_font_face_cache_family_blob {
    char family[TBOX_FONT_FAMILY_CAPACITY];
    int weight;
    double stretch;
    bool italic;
    const void *data;
    size_t size;
    bool registered;
    bool missing; /* the resolver found no such family: data is NULL */
} tbox_font_face_cache_family_blob;

/* Own lifetime (real _destroy, no caller arena), same as tbox_css_stylesheet
 * (see src/css_parser/tbox_css_stylesheet.c) -- the internal tbox_arena
 * below is purely an implementation detail for the copied byte buffers and
 * the entries vector's backing storage, not the caller-arena pattern
 * docs/dev/convencoes.md warns not to mix with a dedicated
 * _destroy. */
struct tbox_font_face_cache {
    tbox_arena arena;
    const void *regular_data;
    size_t regular_size;
    const void *bold_data;
    size_t bold_size;
    tbox_font_resolver_fn resolver;
    void *resolver_userdata;
    tbox_destroy_notify resolver_destroy;
    tbox_vector entries;      /* tbox_font_face_cache_entry, linear-scanned by _get. */
    tbox_vector family_blobs; /* tbox_font_face_cache_family_blob, linear-scanned by _get. */
    unsigned generation;
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

tbox_font_face_cache *tbox_font_face_cache_create(const void *regular_data, size_t regular_size, const void *bold_data, size_t bold_size, tbox_font_resolver_fn resolver, void *resolver_userdata, tbox_destroy_notify resolver_destroy) {
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
    cache->resolver_destroy  = resolver_destroy;

    tbox_vector_init(&cache->entries, &cache->arena, sizeof(tbox_font_face_cache_entry), 0);
    tbox_vector_init(&cache->family_blobs, &cache->arena, sizeof(tbox_font_face_cache_family_blob), 0);
    cache->generation = 0;

    return cache;
}

bool tbox_font_face_cache_register(tbox_font_face_cache *cache, tbox_string_view family, int weight, bool italic, const void *data, size_t size) {
    if (cache == NULL || family.size == 0 || family.size >= TBOX_FONT_FAMILY_CAPACITY || data == NULL || size == 0) return false;
    const void *copy = tbox_font_face_cache_copy_bytes(&cache->arena, data, size);
    if (copy == NULL) return false;
    tbox_font_face_cache_family_blob *blob = tbox_vector_push(&cache->family_blobs);
    if (blob == NULL) return false;
    memset(blob, 0, sizeof(*blob));
    memcpy(blob->family, family.data, family.size);
    blob->weight = weight > 0 ? weight : 400;
    blob->stretch = 100.0;
    blob->italic = italic;
    blob->data = copy;
    blob->size = size;
    blob->registered = true;
    cache->generation++;
    return true;
}

void tbox_font_face_cache_destroy(tbox_font_face_cache *cache) {
    if (cache == NULL) {
        return;
    }
    if (cache->resolver_destroy != NULL)
        cache->resolver_destroy(cache->resolver_userdata);

    size_t count = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < count; i++) {
        tbox_font_face_cache_entry *entry = tbox_vector_at(&cache->entries, i);
        if (!entry->alias)
            tbox_font_face_destroy(entry->face);
    }

    /* Frees the copied regular_data/bold_data buffers and the entries
     * vector's backing storage too -- both live in this same arena. */
    tbox_arena_destroy(&cache->arena);
    free(cache);
}

const tbox_font_face *tbox_font_face_cache_get(tbox_font_face_cache *cache, tbox_string_view family, bool bold, bool italic, double size_px) {
    return tbox_font_face_cache_get_styled(cache, family, bold ? 700 : 400, 100.0, italic, size_px);
}

const tbox_font_face *tbox_font_face_cache_get_styled(tbox_font_face_cache *cache, tbox_string_view family, int weight, double stretch, bool italic, double size_px) {
    return tbox_font_face_cache_get_kerned(cache, family, weight, stretch, italic, size_px, true);
}

const tbox_font_face *tbox_font_face_cache_get_kerned(tbox_font_face_cache *cache, tbox_string_view family, int weight, double stretch, bool italic, double size_px, bool kerning) {
    return tbox_font_face_cache_get_shaped(cache, family, weight, stretch, italic, size_px, kerning, NULL);
}

static bool tbox_font_options_equal(const tbox_font_shaping_options *a, const tbox_font_shaping_options *b) {
    if (a->language != b->language || a->count != b->count) return false;
    if (a->vertical_frame != b->vertical_frame || a->vertical_orientation != b->vertical_orientation) return false;
    for (size_t i = 0; i < a->count; i++)
        if (a->items[i].tag != b->items[i].tag || a->items[i].value != b->items[i].value) return false;
    return true;
}

static tbox_font_face_cache_entry *tbox_font_face_cache_find(tbox_font_face_cache *cache, tbox_string_view family, int weight, double stretch, bool italic, double size_px, bool kerning, const tbox_font_shaping_options *options) {
    tbox_font_face_cache_entry *entries = cache->entries.data;
    size_t entry_count                  = tbox_vector_length(&cache->entries);
    for (size_t i = 0; i < entry_count; i++) {
        tbox_font_face_cache_entry *entry = &entries[i];
        if (entry->generation == cache->generation && entry->weight == weight && entry->stretch == stretch && entry->italic == italic && entry->kerning == kerning && entry->size_px == size_px &&
            entry->family_size == family.size && (family.size == 0 || memcmp(entry->family, family.data, family.size) == 0) && tbox_font_options_equal(&entry->face->options, options)) {
            return entry;
        }
    }
    return NULL;
}

static void tbox_font_face_cache_add(tbox_font_face_cache *cache, const char *family, int weight, double stretch, bool italic, double size_px, bool kerning, tbox_font_face *face, bool alias) {
    tbox_font_face_cache_entry *entry = tbox_vector_push(&cache->entries);
    entry->kerning                    = kerning;
    entry->family_size = tbox_utf8_copy_truncated(entry->family, sizeof(entry->family), tbox_string_view_from_cstr(family));
    entry->weight     = weight;
    entry->stretch    = stretch;
    entry->italic     = italic;
    entry->size_px    = size_px;
    entry->face       = face;
    entry->generation = cache->generation;
    entry->alias      = alias;
}

/* One family name (or "" for the default family); NULL when the resolver
 * has no such family. */
static const tbox_font_face *tbox_font_face_cache_get_one(tbox_font_face_cache *cache, const char *family_buf, int weight, double stretch, bool italic, double size_px, bool kerning, const tbox_font_shaping_options *options) {
    bool bold                         = weight >= 600;
    tbox_string_view family           = tbox_string_view_from_cstr(family_buf);
    const tbox_font_face_cache_entry *found = tbox_font_face_cache_find(cache, family, weight, stretch, italic, size_px, kerning, options);
    if (found != NULL)
        return found->face;

    const void *data = NULL;
    size_t size      = 0;

    bool preloaded = family_buf[0] == '\0' && !italic && (weight == 400 || weight == 700) && stretch == 100.0;
    if (preloaded) {
        /* Default family, non-italic -- exactly the original v0..v12
         * behavior, no resolver call: regular_data/bold_data were preloaded
         * eagerly for exactly this case. */
        data = bold ? cache->bold_data : cache->regular_data;
        size = bold ? cache->bold_size : cache->regular_size;
    } else {
        /* Non-default family, OR the default family with italic requested
         * (regular_data/bold_data have no italic variant of their
         * own -- resolving one on demand, same mechanism as any other
         * family, is what lets `<i>`/`<em>` render truly slanted without a
         * `font-family` declared anywhere in the ancestor chain, the
         * scenario docs/dev/historico.md's v13 "Fatia vertical" criterion
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
        const tbox_font_face_cache_family_blob *blob = NULL;
        size_t blob_count                            = tbox_vector_length(&cache->family_blobs);
        for (size_t i = blob_count; i > 0; i--) {
            const tbox_font_face_cache_family_blob *candidate = tbox_vector_at(&cache->family_blobs, i - 1);
            if (candidate->weight == weight && candidate->stretch == stretch && candidate->italic == italic && strcmp(candidate->family, family_buf) == 0) {
                blob = candidate;
                break;
            }
        }

        /* The default family at another weight/stretch without a working
         * resolver still gets its closest preloaded face. */
        bool default_upright = family_buf[0] == '\0' && !italic;
        if (blob == NULL) {
            tbox_font_query query = {
                .family  = family,
                .bold    = bold,
                .italic  = italic,
                .weight  = weight,
                .stretch = stretch,
            };

            const void *resolved_data = NULL;
            size_t resolved_size      = 0;
            if (cache->resolver == NULL || !cache->resolver(cache->resolver_userdata, &query, &resolved_data, &resolved_size)) {
                if (!default_upright) {
                    /* Remembered, so the resolver isn't asked again. */
                    tbox_font_face_cache_family_blob *missing = tbox_vector_push(&cache->family_blobs);
                    memset(missing, 0, sizeof(*missing));
                    tbox_utf8_copy_truncated(missing->family, sizeof(missing->family), family);
                    missing->weight  = weight;
                    missing->stretch = stretch;
                    missing->italic  = italic;
                    missing->missing = true;
                    return NULL;
                }
                resolved_data = bold ? cache->bold_data : cache->regular_data;
                resolved_size = bold ? cache->bold_size : cache->regular_size;
            }

            const void *copied_data = tbox_font_face_cache_copy_bytes(&cache->arena, resolved_data, resolved_size);
            if (copied_data == NULL) {
                return NULL;
            }

            tbox_font_face_cache_family_blob *new_blob = tbox_vector_push(&cache->family_blobs);
            memset(new_blob, 0, sizeof(*new_blob));
            tbox_utf8_copy_truncated(new_blob->family, sizeof(new_blob->family), family);
            new_blob->weight  = weight;
            new_blob->stretch = stretch;
            new_blob->italic  = italic;
            new_blob->data   = copied_data;
            new_blob->size   = resolved_size;
            new_blob->registered = false;
            blob             = new_blob;
        }

        if (blob->missing)
            return NULL;
        data = blob->data;
        size = blob->size;
    }

    tbox_font_face *face = tbox_font_face_load(data, size, size_px);
    if (face == NULL) {
        /* Nothing cached on failure -- a later retry with the same
         * parameters tries loading again rather than being stuck. */
        return NULL;
    }

    face->options = *options;
    tbox_font_face_set_kerning(face, kerning);
    tbox_font_face_cache_add(cache, family_buf, weight, stretch, italic, size_px, kerning, face, false);
    return face;
}

const tbox_font_face *tbox_font_face_cache_get_shaped(tbox_font_face_cache *cache, tbox_string_view family, int weight, double stretch, bool italic, double size_px, bool kerning, const tbox_font_shaping_options *options) {
    static const tbox_font_shaping_options defaults = {0};
    if (options == NULL) options = &defaults;
    if (options->count > TBOX_FONT_MAX_FEATURES) return NULL;
    if (cache == NULL) {
        return NULL;
    }
    if (weight <= 0)
        weight = 400;
    if (stretch <= 0.0)
        stretch = 100.0;

    /* A list that fits is its own key: the copy below would keep every
     * byte. Hits, the common case, skip the copy. */
    const tbox_font_face_cache_entry *found = family.size < TBOX_FONT_FAMILY_CAPACITY ? tbox_font_face_cache_find(cache, family, weight, stretch, italic, size_px, kerning, options) : NULL;
    if (found != NULL)
        return found->face;
    /* Truncate-not-reject, same posture as
     * tbox_font_source_fontconfig_family_cstr (src/font/tbox_font_source_fontconfig.c)
     * -- always NUL-terminated. */
    char list[TBOX_FONT_FAMILY_CAPACITY];
    tbox_string_view key = tbox_string_view_make(list, tbox_utf8_copy_truncated(list, sizeof(list), family));
    found                = tbox_font_face_cache_find(cache, key, weight, stretch, italic, size_px, kerning, options);
    if (found != NULL)
        return found->face;

    /* `family` is a font-family list ("a, 'b c', serif"): the first name
     * the resolver really has wins, else the default family. */
    const tbox_font_face *face = NULL;
    for (tbox_string_view rest = tbox_string_view_from_cstr(list); face == NULL && rest.size > 0;) {
        size_t comma = 0;
        while (comma < rest.size && rest.data[comma] != ',')
            comma++;
        tbox_string_view name = tbox_string_view_trim_ascii_space(tbox_string_view_make(rest.data, comma));
        if (name.size >= 2 && (name.data[0] == '"' || name.data[0] == '\'') && name.data[name.size - 1] == name.data[0])
            name = tbox_string_view_make(name.data + 1, name.size - 2);
        rest = comma < rest.size ? tbox_string_view_make(rest.data + comma + 1, rest.size - comma - 1) : tbox_string_view_make(rest.data + rest.size, 0);
        if (name.size == 0)
            continue;
        char one[TBOX_FONT_FAMILY_CAPACITY];
        tbox_utf8_copy_truncated(one, sizeof(one), name);
        face = tbox_font_face_cache_get_one(cache, one, weight, stretch, italic, size_px, kerning, options);
    }
    if (face == NULL)
        face = tbox_font_face_cache_get_one(cache, "", weight, stretch, italic, size_px, kerning, options);
    if (face != NULL && tbox_font_face_cache_find(cache, key, weight, stretch, italic, size_px, kerning, options) == NULL)
        tbox_font_face_cache_add(cache, list, weight, stretch, italic, size_px, kerning, (tbox_font_face *)face, true);
    return face;
}
