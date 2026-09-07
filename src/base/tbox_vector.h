#ifndef TBOX_BASE_VECTOR_H
#define TBOX_BASE_VECTOR_H

#include <stddef.h>

#include "tbox_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A dynamic array of fixed-size elements backed by an arena. Growth doubles
 * capacity and copies old elements into a freshly arena-allocated buffer;
 * the previous buffer is abandoned in the arena. There is no free/shrink:
 * lifetime is tied to the arena. Not thread-safe. */
typedef struct tbox_vector {
    tbox_arena *arena;
    void *data;
    size_t element_size;
    size_t length;
    size_t capacity;
} tbox_vector;

/* initial_capacity == 0 uses a small built-in default. */
void tbox_vector_init(tbox_vector *vector, tbox_arena *arena, size_t element_size, size_t initial_capacity);

/* Grows the vector if needed and returns a pointer to the new, uninitialized
 * slot at the end; the caller fills it in (e.g. via assignment or memcpy). */
void *tbox_vector_push(tbox_vector *vector);

void *tbox_vector_at(tbox_vector *vector, size_t index);
const void *tbox_vector_at_const(const tbox_vector *vector, size_t index);
size_t tbox_vector_length(const tbox_vector *vector);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_VECTOR_H */
