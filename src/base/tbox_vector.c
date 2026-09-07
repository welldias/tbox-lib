#include "tbox_vector.h"

#include <assert.h>
#include <string.h>

#define TBOX_VECTOR_DEFAULT_CAPACITY ((size_t)4)

void tbox_vector_init(tbox_vector *vector, tbox_arena *arena, size_t element_size, size_t initial_capacity) {
    vector->arena        = arena;
    vector->data         = NULL;
    vector->element_size = element_size;
    vector->length       = 0;
    vector->capacity     = 0;

    if (initial_capacity > 0) {
        vector->data     = tbox_arena_alloc(arena, element_size * initial_capacity);
        vector->capacity = initial_capacity;
    }
}

static void tbox_vector_grow(tbox_vector *vector) {
    size_t new_capacity = vector->capacity == 0 ? TBOX_VECTOR_DEFAULT_CAPACITY : vector->capacity * 2;
    void *new_data      = tbox_arena_alloc(vector->arena, vector->element_size * new_capacity);

    if (vector->length > 0) {
        memcpy(new_data, vector->data, vector->length * vector->element_size);
    }

    vector->data     = new_data;
    vector->capacity = new_capacity;
}

void *tbox_vector_push(tbox_vector *vector) {
    if (vector->length == vector->capacity) {
        tbox_vector_grow(vector);
    }

    unsigned char *slot = (unsigned char *)vector->data + vector->length * vector->element_size;
    vector->length++;
    return slot;
}

void *tbox_vector_at(tbox_vector *vector, size_t index) {
    assert(index < vector->length);
    return (unsigned char *)vector->data + index * vector->element_size;
}

const void *tbox_vector_at_const(const tbox_vector *vector, size_t index) {
    assert(index < vector->length);
    return (const unsigned char *)vector->data + index * vector->element_size;
}

size_t tbox_vector_length(const tbox_vector *vector) {
    return vector->length;
}
