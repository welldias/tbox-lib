#ifndef TBOX_BASE_ARENA_H
#define TBOX_BASE_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbox_arena_block {
    struct tbox_arena_block *next;
    unsigned char *data;
    size_t capacity;
    size_t used;
} tbox_arena_block;

/* A region-based (bump-pointer) allocator: individual allocations are never
 * freed on their own, only the whole arena at once via tbox_arena_destroy.
 * Not thread-safe. */
typedef struct tbox_arena {
    tbox_arena_block *current;
    tbox_arena_block *first;
    size_t default_block_size;
    size_t total_capacity;
    size_t total_used;
} tbox_arena;

/* initial_block_size == 0 uses a built-in default. Eagerly allocates the
 * first block. Returns a zeroed arena (first == current == NULL) if that
 * first allocation fails. */
tbox_arena tbox_arena_create(size_t initial_block_size);

void *tbox_arena_alloc(tbox_arena *arena, size_t size);
void *tbox_arena_alloc_aligned(tbox_arena *arena, size_t size, size_t alignment);
void *tbox_arena_alloc_zero(tbox_arena *arena, size_t size);

/* Frees every block except the first, and rewinds the first block's bump
 * pointer to zero. Every pointer previously returned by this arena becomes
 * invalid. */
void tbox_arena_reset(tbox_arena *arena);

/* Frees every block. The arena must not be used again unless re-created. */
void tbox_arena_destroy(tbox_arena *arena);

size_t tbox_arena_bytes_allocated(const tbox_arena *arena);
size_t tbox_arena_bytes_used(const tbox_arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_ARENA_H */
