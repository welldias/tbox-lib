#ifndef TBOX_BASE_LIST_H
#define TBOX_BASE_LIST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Intrusive doubly linked list node. Meant to be embedded by value inside
 * the struct it links; the list never allocates or frees nodes itself. */
typedef struct tbox_list_node {
    struct tbox_list_node *next;
    struct tbox_list_node *prev;
} tbox_list_node;

typedef struct tbox_list {
    tbox_list_node sentinel;
} tbox_list;

void tbox_list_init(tbox_list *list);
bool tbox_list_empty(const tbox_list *list);
void tbox_list_push_back(tbox_list *list, tbox_list_node *node);
void tbox_list_push_front(tbox_list *list, tbox_list_node *node);

/* Unlinks node from whatever list it is currently part of. */
void tbox_list_remove(tbox_list_node *node);

tbox_list_node *tbox_list_front(const tbox_list *list);
tbox_list_node *tbox_list_back(const tbox_list *list);

#define TBOX_LIST_ENTRY(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

#define TBOX_LIST_FOR_EACH(cursor, list) for (tbox_list_node *cursor = (list)->sentinel.next; cursor != &(list)->sentinel; cursor = cursor->next)

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_LIST_H */
