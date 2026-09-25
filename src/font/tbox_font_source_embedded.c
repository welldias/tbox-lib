#include "tbox_font_source_internal.h"

#include <stdlib.h>
#include <string.h>

/* Owns a copy of the bytes it was constructed with; resolve() always
 * returns those same bytes, ignoring `query` entirely -- see
 * tbox_font_source_embedded_create in <tbox/font.h>. */
typedef struct tbox_font_source_embedded {
    unsigned char *data;
    size_t size;
} tbox_font_source_embedded;

static bool tbox_font_source_embedded_resolve(void *self, tbox_font_query query, const void **out_data, size_t *out_size) {
    (void)query;

    const tbox_font_source_embedded *embedded = self;
    *out_data = embedded->data;
    *out_size = embedded->size;
    return true;
}

static void tbox_font_source_embedded_destroy(void *self) {
    tbox_font_source_embedded *embedded = self;
    free(embedded->data);
    free(embedded);
}

static const tbox_font_source_vtable tbox_font_source_embedded_vtable = {
    .resolve = tbox_font_source_embedded_resolve,
    .destroy = tbox_font_source_embedded_destroy,
};

tbox_font_source *tbox_font_source_embedded_create(const void *font_data, size_t size) {
    tbox_font_source_embedded *embedded = malloc(sizeof(*embedded));
    if (embedded == NULL) {
        return NULL;
    }

    /* malloc(0) is allowed to return NULL even on success; allocate at
     * least one byte so an empty font (never a real use case, but not this
     * function's job to reject) doesn't look like an allocation failure. */
    embedded->data = malloc(size > 0 ? size : 1);
    if (embedded->data == NULL) {
        free(embedded);
        return NULL;
    }

    if (size > 0) {
        memcpy(embedded->data, font_data, size);
    }
    embedded->size = size;

    tbox_font_source *source = tbox_font_source_create(&tbox_font_source_embedded_vtable, embedded);
    if (source == NULL) {
        free(embedded->data);
        free(embedded);
        return NULL;
    }

    return source;
}
