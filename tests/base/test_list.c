#include "base/tbox_list.h"

#include "test_support.h"

typedef struct tbox_test_item {
    int value;
    tbox_list_node node;
} tbox_test_item;

int tbox_test_list_run(void) {
    int failures = 0;

    /* 1: empty list. */
    {
        tbox_list list;
        tbox_list_init(&list);
        TBOX_TEST_ASSERT(tbox_list_empty(&list));
        TBOX_TEST_ASSERT(tbox_list_front(&list) == NULL);
        TBOX_TEST_ASSERT(tbox_list_back(&list) == NULL);
    }

    /* 2: push_back preserves insertion order. */
    {
        tbox_list list;
        tbox_list_init(&list);
        tbox_test_item items[4] = {{.value = 0}, {.value = 1}, {.value = 2}, {.value = 3}};
        for (int i = 0; i < 4; i++) {
            tbox_list_push_back(&list, &items[i].node);
        }

        int expected = 0;
        TBOX_LIST_FOR_EACH(cursor, &list) {
            tbox_test_item *item = TBOX_LIST_ENTRY(cursor, tbox_test_item, node);
            TBOX_TEST_ASSERT(item->value == expected);
            expected++;
        }
        TBOX_TEST_ASSERT(expected == 4);
        TBOX_TEST_ASSERT(TBOX_LIST_ENTRY(tbox_list_front(&list), tbox_test_item, node)->value == 0);
        TBOX_TEST_ASSERT(TBOX_LIST_ENTRY(tbox_list_back(&list), tbox_test_item, node)->value == 3);
    }

    /* 3: push_front preserves reverse insertion order. */
    {
        tbox_list list;
        tbox_list_init(&list);
        tbox_test_item items[3] = {{.value = 0}, {.value = 1}, {.value = 2}};
        for (int i = 0; i < 3; i++) {
            tbox_list_push_front(&list, &items[i].node);
        }

        int expected = 2;
        TBOX_LIST_FOR_EACH(cursor, &list) {
            tbox_test_item *item = TBOX_LIST_ENTRY(cursor, tbox_test_item, node);
            TBOX_TEST_ASSERT(item->value == expected);
            expected--;
        }
    }

    /* 4: removing a node from the middle preserves neighbors. */
    {
        tbox_list list;
        tbox_list_init(&list);
        tbox_test_item items[3] = {{.value = 0}, {.value = 1}, {.value = 2}};
        for (int i = 0; i < 3; i++) {
            tbox_list_push_back(&list, &items[i].node);
        }

        tbox_list_remove(&items[1].node);

        int values[2];
        int index = 0;
        TBOX_LIST_FOR_EACH(cursor, &list) {
            values[index++] = TBOX_LIST_ENTRY(cursor, tbox_test_item, node)->value;
        }
        TBOX_TEST_ASSERT(index == 2);
        TBOX_TEST_ASSERT(values[0] == 0);
        TBOX_TEST_ASSERT(values[1] == 2);
    }

    /* 5: removing every node empties the list. */
    {
        tbox_list list;
        tbox_list_init(&list);
        tbox_test_item items[3] = {{.value = 0}, {.value = 1}, {.value = 2}};
        for (int i = 0; i < 3; i++) {
            tbox_list_push_back(&list, &items[i].node);
        }
        for (int i = 0; i < 3; i++) {
            tbox_list_remove(&items[i].node);
        }
        TBOX_TEST_ASSERT(tbox_list_empty(&list));
    }

    /* 6: mixed push_front/push_back order. */
    {
        tbox_list list;
        tbox_list_init(&list);
        tbox_test_item a = {.value = 1};
        tbox_test_item b = {.value = 2};
        tbox_test_item c = {.value = 3};
        tbox_list_push_back(&list, &a.node);
        tbox_list_push_front(&list, &b.node);
        tbox_list_push_back(&list, &c.node);
        /* expected order: b, a, c */
        int expected[3] = {2, 1, 3};
        int index        = 0;
        TBOX_LIST_FOR_EACH(cursor, &list) {
            TBOX_TEST_ASSERT(TBOX_LIST_ENTRY(cursor, tbox_test_item, node)->value == expected[index]);
            index++;
        }
    }

    return failures;
}
