#ifndef TBOX_FONT_SOURCE_INTERNAL_H
#define TBOX_FONT_SOURCE_INTERNAL_H

#include <tbox/font.h>

/* tbox_font_source is one small opaque public type backed by several very
 * different backends (Fontconfig, embedded bytes, and -- in the future --
 * one per platform). Rather than a tagged union in the public header (which
 * would force every backend's private state into <tbox/font.h>), each
 * backend implements this vtable and hands its own `self` payload to
 * tbox_font_source_create; tbox_font_source_resolve/_destroy (in
 * tbox_font_source.c) just dispatch through it. */
typedef struct tbox_font_source_vtable {
    bool (*resolve)(void *self, tbox_font_query query, const void **out_data, size_t *out_size);
    void (*destroy)(void *self); /* may be NULL if self needs no cleanup */
} tbox_font_source_vtable;

/* Wraps `vtable`/`self` into a tbox_font_source the public API can return.
 * Returns NULL only on allocation failure; on failure the caller still owns
 * `self` (this function never touches it when it fails). */
tbox_font_source *tbox_font_source_create(const tbox_font_source_vtable *vtable, void *self);

#endif /* TBOX_FONT_SOURCE_INTERNAL_H */
