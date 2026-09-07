#include "base/tbox_vector.h"

#include <stdbool.h>

#include "base/tbox_arena.h"
#include "test_support.h"

typedef struct tbox_test_point {
    int x;
    int y;
    int z;
} tbox_test_point;

int tbox_test_vector_run(void) {
    int failures = 0;

    /* 1: push + read back ints. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_vector vector;
        tbox_vector_init(&vector, &arena, sizeof(int), 0);

        for (int i = 0; i < 10; i++) {
            int *slot = tbox_vector_push(&vector);
            *slot     = i * 10;
        }

        TBOX_TEST_ASSERT(tbox_vector_length(&vector) == 10);
        for (int i = 0; i < 10; i++) {
            TBOX_TEST_ASSERT(*(int *)tbox_vector_at(&vector, (size_t)i) == i * 10);
        }
        tbox_arena_destroy(&arena);
    }

    /* 2: growth across several doublings preserves earlier data. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_vector vector;
        tbox_vector_init(&vector, &arena, sizeof(int), 1);

        for (int i = 0; i < 200; i++) {
            int *slot = tbox_vector_push(&vector);
            *slot     = i;
        }

        TBOX_TEST_ASSERT(tbox_vector_length(&vector) == 200);
        bool ok = true;
        for (int i = 0; i < 200; i++) {
            if (*(int *)tbox_vector_at(&vector, (size_t)i) != i) {
                ok = false;
            }
        }
        TBOX_TEST_ASSERT(ok);
        tbox_arena_destroy(&arena);
    }

    /* 3: struct element with padding is copied correctly. */
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_vector vector;
        tbox_vector_init(&vector, &arena, sizeof(tbox_test_point), 0);

        for (int i = 0; i < 20; i++) {
            tbox_test_point *slot = tbox_vector_push(&vector);
            slot->x                = i;
            slot->y                = i * 2;
            slot->z                = i * 3;
        }

        bool ok = true;
        for (int i = 0; i < 20; i++) {
            const tbox_test_point *p = tbox_vector_at_const(&vector, (size_t)i);
            if (p->x != i || p->y != i * 2 || p->z != i * 3) {
                ok = false;
            }
        }
        TBOX_TEST_ASSERT(ok);
        tbox_arena_destroy(&arena);
    }

    /* 4: vector growth forces the backing arena to grow blocks too. */
    {
        tbox_arena arena = tbox_arena_create(32);
        tbox_vector vector;
        tbox_vector_init(&vector, &arena, sizeof(int), 0);
        for (int i = 0; i < 100; i++) {
            int *slot = tbox_vector_push(&vector);
            *slot     = i;
        }
        TBOX_TEST_ASSERT(tbox_vector_length(&vector) == 100);
        TBOX_TEST_ASSERT(*(int *)tbox_vector_at(&vector, 99) == 99);
        tbox_arena_destroy(&arena);
    }

    return failures;
}
