#include "tbox_font_source_internal.h"

#include <fontconfig/fontconfig.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Owns the bytes of whatever file the most recent resolve() call matched;
 * a fresh resolve() call replaces it (freeing the previous buffer) rather
 * than accumulating one buffer per call -- v0 only ever resolves once per
 * document (a single shared tbox_font_face, see ARCHITECTURE.md's "Fonte /
 * Texto" section), so this keeps at most one buffer alive at a time. */
typedef struct tbox_font_source_fontconfig {
    unsigned char *data;
    size_t size;
} tbox_font_source_fontconfig;

/* Reads the whole file at `path` into a malloc'd buffer. Returns false and
 * leaves *out_data / *out_size untouched on any I/O failure. */
static bool tbox_font_source_fontconfig_read_file(const char *path, unsigned char **out_data, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    unsigned char *buffer = malloc(size > 0 ? (size_t)size : 1);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        return false;
    }

    *out_data = buffer;
    *out_size = (size_t)size;
    return true;
}

/* tbox_font_query.family is a tbox_string_view (not necessarily
 * NUL-terminated), but Fontconfig's pattern API takes NUL-terminated
 * FcChar8 strings. Generic families are short ("sans-serif", "serif",
 * "monospace"), so a fixed-size stack buffer is enough; anything
 * implausibly long is truncated rather than rejected -- Fontconfig itself
 * tolerates an unrecognized family by falling back to its configured
 * default, so truncation degrades gracefully instead of failing outright. */
static const char *tbox_font_source_fontconfig_family_cstr(tbox_string_view family, char *buffer, size_t buffer_size) {
    if (family.size == 0) {
        return "sans-serif";
    }

    size_t n = family.size < buffer_size - 1 ? family.size : buffer_size - 1;
    memcpy(buffer, family.data, n);
    buffer[n] = '\0';
    return buffer;
}

static bool tbox_font_source_fontconfig_resolve(void *self, tbox_font_query query, const void **out_data, size_t *out_size) {
    tbox_font_source_fontconfig *fc = self;

    if (FcInit() == FcFalse) {
        return false;
    }

    char family_buffer[128];
    const char *family = tbox_font_source_fontconfig_family_cstr(query.family, family_buffer, sizeof(family_buffer));

    FcPattern *pattern = FcPatternCreate();
    if (pattern == NULL) {
        return false;
    }

    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(pattern, FC_WEIGHT, query.bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pattern, FC_SLANT, query.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);

    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern *matched = FcFontMatch(NULL, pattern, &result);
    FcPatternDestroy(pattern);
    if (matched == NULL) {
        return false;
    }

    bool resolved = false;
    FcChar8 *path = NULL;
    if (FcPatternGetString(matched, FC_FILE, 0, &path) == FcResultMatch && path != NULL) {
        unsigned char *data = NULL;
        size_t size = 0;
        if (tbox_font_source_fontconfig_read_file((const char *)path, &data, &size)) {
            free(fc->data);
            fc->data = data;
            fc->size = size;
            *out_data = fc->data;
            *out_size = fc->size;
            resolved = true;
        }
    }

    FcPatternDestroy(matched);
    return resolved;
}

static void tbox_font_source_fontconfig_destroy(void *self) {
    tbox_font_source_fontconfig *fc = self;
    free(fc->data);
    free(fc);
}

static const tbox_font_source_vtable tbox_font_source_fontconfig_vtable = {
    .resolve = tbox_font_source_fontconfig_resolve,
    .destroy = tbox_font_source_fontconfig_destroy,
};

tbox_font_source *tbox_font_source_fontconfig_create(void) {
    tbox_font_source_fontconfig *fc = malloc(sizeof(*fc));
    if (fc == NULL) {
        return NULL;
    }

    fc->data = NULL;
    fc->size = 0;

    tbox_font_source *source = tbox_font_source_create(&tbox_font_source_fontconfig_vtable, fc);
    if (source == NULL) {
        free(fc);
        return NULL;
    }

    return source;
}
