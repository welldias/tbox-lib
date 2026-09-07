#include "tbox_list.h"

void tbox_list_init(tbox_list *list) {
    list->sentinel.next = &list->sentinel;
    list->sentinel.prev = &list->sentinel;
}

bool tbox_list_empty(const tbox_list *list) {
    return list->sentinel.next == &list->sentinel;
}

static void tbox_list_insert_between(tbox_list_node *node, tbox_list_node *before, tbox_list_node *after) {
    node->prev   = before;
    node->next   = after;
    before->next = node;
    after->prev  = node;
}

void tbox_list_push_back(tbox_list *list, tbox_list_node *node) {
    tbox_list_insert_between(node, list->sentinel.prev, &list->sentinel);
}

void tbox_list_push_front(tbox_list *list, tbox_list_node *node) {
    tbox_list_insert_between(node, &list->sentinel, list->sentinel.next);
}

void tbox_list_remove(tbox_list_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next       = node;
    node->prev       = node;
}

tbox_list_node *tbox_list_front(const tbox_list *list) {
    return tbox_list_empty(list) ? NULL : list->sentinel.next;
}

tbox_list_node *tbox_list_back(const tbox_list *list) {
    return tbox_list_empty(list) ? NULL : list->sentinel.prev;
}
