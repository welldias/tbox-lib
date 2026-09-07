#include "tbox_arena.h"

#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TBOX_ARENA_DEFAULT_BLOCK_SIZE ((size_t)(64 * 1024))

static tbox_arena_block *tbox_arena_block_create(size_t capacity) {
    tbox_arena_block *block = malloc(sizeof(tbox_arena_block));
    if (block == NULL) {
        return NULL;
    }

    block->data = malloc(capacity);
    if (block->data == NULL) {
        free(block);
        return NULL;
    }

    block->next     = NULL;
    block->capacity = capacity;
    block->used     = 0;
    return block;
}

tbox_arena tbox_arena_create(size_t initial_block_size) {
    tbox_arena arena = {
        .current            = NULL,
        .first              = NULL,
        .default_block_size = initial_block_size != 0 ? initial_block_size : TBOX_ARENA_DEFAULT_BLOCK_SIZE,
        .total_capacity     = 0,
        .total_used         = 0,
    };

    tbox_arena_block *block = tbox_arena_block_create(arena.default_block_size);
    if (block == NULL) {
        return arena;
    }

    arena.current        = block;
    arena.first          = block;
    arena.total_capacity = block->capacity;
    return arena;
}

void *tbox_arena_alloc_aligned(tbox_arena *arena, size_t size, size_t alignment) {
    if (arena->current == NULL) {
        return NULL;
    }

    if (size == 0) {
        size = 1;
    }

    tbox_arena_block *block = arena->current;
    uintptr_t cursor        = (uintptr_t)(block->data + block->used);
    size_t misalignment     = (size_t)(cursor % alignment);
    size_t padding          = misalignment != 0 ? alignment - misalignment : 0;

    if (block->used + padding + size > block->capacity) {
        size_t new_capacity = arena->default_block_size;
        size_t worst_case   = size + alignment - 1;
        if (worst_case > new_capacity) {
            new_capacity = worst_case;
        }

        tbox_arena_block *new_block = tbox_arena_block_create(new_capacity);
        if (new_block == NULL) {
            return NULL;
        }

        new_block->next = arena->current;
        arena->current  = new_block;
        arena->total_capacity += new_block->capacity;

        block        = new_block;
        cursor       = (uintptr_t)(block->data + block->used);
        misalignment = (size_t)(cursor % alignment);
        padding      = misalignment != 0 ? alignment - misalignment : 0;
    }

    unsigned char *result = block->data + block->used + padding;
    block->used += padding + size;
    arena->total_used += size;
    return result;
}

void *tbox_arena_alloc(tbox_arena *arena, size_t size) {
    return tbox_arena_alloc_aligned(arena, size, alignof(max_align_t));
}

void *tbox_arena_alloc_zero(tbox_arena *arena, size_t size) {
    void *ptr = tbox_arena_alloc(arena, size);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void tbox_arena_reset(tbox_arena *arena) {
    tbox_arena_block *block = arena->current;
    while (block != NULL && block != arena->first) {
        tbox_arena_block *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }

    arena->current = arena->first;
    if (arena->first != NULL) {
        arena->first->used = 0;
    }
    arena->total_capacity = arena->first != NULL ? arena->first->capacity : 0;
    arena->total_used     = 0;
}

void tbox_arena_destroy(tbox_arena *arena) {
    tbox_arena_block *block = arena->current;
    while (block != NULL) {
        tbox_arena_block *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }

    arena->current        = NULL;
    arena->first          = NULL;
    arena->total_capacity = 0;
    arena->total_used     = 0;
}

size_t tbox_arena_bytes_allocated(const tbox_arena *arena) {
    return arena->total_capacity;
}

size_t tbox_arena_bytes_used(const tbox_arena *arena) {
    return arena->total_used;
}
