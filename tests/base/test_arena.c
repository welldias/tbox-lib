#include "base/tbox_arena.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "test_support.h"

int tbox_test_arena_run(void) {
    int failures = 0;

    /* 1: create + single allocation is writable/readable. */
    {
        tbox_arena arena = tbox_arena_create(0);
        int *value        = tbox_arena_alloc(&arena, sizeof(int));
        TBOX_TEST_ASSERT(value != NULL);
        *value = 42;
        TBOX_TEST_ASSERT(*value == 42);
        tbox_arena_destroy(&arena);
    }

    /* 2: multiple small allocations don't overlap. */
    {
        tbox_arena arena = tbox_arena_create(0);
        unsigned char *blocks[8];
        for (int i = 0; i < 8; i++) {
            blocks[i] = tbox_arena_alloc(&arena, 16);
            memset(blocks[i], i, 16);
        }
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 16; j++) {
                TBOX_TEST_ASSERT(blocks[i][j] == (unsigned char)i);
            }
        }
        tbox_arena_destroy(&arena);
    }

    /* 3: default alignment is at least alignof(max_align_t). */
    {
        tbox_arena arena = tbox_arena_create(0);
        for (int i = 0; i < 16; i++) {
            tbox_arena_alloc(&arena, (size_t)(i + 1));
            void *ptr = tbox_arena_alloc(&arena, sizeof(max_align_t));
            TBOX_TEST_ASSERT((uintptr_t)ptr % alignof(max_align_t) == 0);
        }
        tbox_arena_destroy(&arena);
    }

    /* 4: small block size forces growth across multiple blocks, all
     * allocations stay valid. */
    {
        tbox_arena arena = tbox_arena_create(64);
        unsigned char *ptrs[64];
        for (int i = 0; i < 64; i++) {
            ptrs[i] = tbox_arena_alloc(&arena, 32);
            memset(ptrs[i], i, 32);
        }
        for (int i = 0; i < 64; i++) {
            for (int j = 0; j < 32; j++) {
                TBOX_TEST_ASSERT(ptrs[i][j] == (unsigned char)i);
            }
        }
        TBOX_TEST_ASSERT(tbox_arena_bytes_allocated(&arena) > 64);
        tbox_arena_destroy(&arena);
    }

    /* 5: an allocation larger than the default block size succeeds. */
    {
        tbox_arena arena = tbox_arena_create(64);
        void *big         = tbox_arena_alloc(&arena, 4096);
        TBOX_TEST_ASSERT(big != NULL);
        tbox_arena_destroy(&arena);
    }

    /* 6: reset rewinds usage and keeps the arena usable. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_arena_alloc(&arena, 128);
        tbox_arena_reset(&arena);
        TBOX_TEST_ASSERT(tbox_arena_bytes_used(&arena) == 0);
        void *ptr = tbox_arena_alloc(&arena, 8);
        TBOX_TEST_ASSERT(ptr != NULL);
        tbox_arena_destroy(&arena);
    }

    /* 7: alloc_zero returns zeroed memory. */
    {
        tbox_arena arena  = tbox_arena_create(0);
        unsigned char *ptr = tbox_arena_alloc_zero(&arena, 64);
        bool all_zero      = true;
        for (int i = 0; i < 64; i++) {
            if (ptr[i] != 0) {
                all_zero = false;
            }
        }
        TBOX_TEST_ASSERT(all_zero);
        tbox_arena_destroy(&arena);
    }

    /* 8: size == 0 still returns a usable pointer. */
    {
        tbox_arena arena = tbox_arena_create(0);
        void *ptr         = tbox_arena_alloc(&arena, 0);
        TBOX_TEST_ASSERT(ptr != NULL);
        tbox_arena_destroy(&arena);
    }

    return failures;
}
