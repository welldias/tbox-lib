#include "tbox_font_source_internal.h"

#include <stdlib.h>

struct tbox_font_source {
    const tbox_font_source_vtable *vtable;
    void *self;
};

tbox_font_source *tbox_font_source_create(const tbox_font_source_vtable *vtable, void *self) {
    tbox_font_source *source = malloc(sizeof(*source));
    if (source == NULL) {
        return NULL;
    }

    source->vtable = vtable;
    source->self   = self;
    return source;
}

bool tbox_font_source_resolve(tbox_font_source *source, tbox_font_query query, const void **out_data, size_t *out_size) {
    if (source == NULL || out_data == NULL || out_size == NULL) {
        return false;
    }

    return source->vtable->resolve(source->self, query, out_data, out_size);
}

void tbox_font_source_destroy(tbox_font_source *source) {
    if (source == NULL) {
        return;
    }

    if (source->vtable->destroy != NULL) {
        source->vtable->destroy(source->self);
    }

    free(source);
}
