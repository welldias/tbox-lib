#include "tbox_release.h"

#include <stdlib.h>

static void tbox_release_run(tbox_release_list *list) {
    /* A destroy may itself release more (e.g. a binding object that owns
     * another), so drain until empty rather than over a snapshot. */
    while (list->count > 0) {
        tbox_release_entry entry = list->items[--list->count];
        entry.destroy(entry.userdata);
    }
}

void tbox_release_add(tbox_release_list *list, tbox_destroy_notify destroy, void *userdata) {
    if (destroy == NULL)
        return;
    if (list->depth == 0) {
        destroy(userdata);
        return;
    }
    if (list->count == list->capacity) {
        size_t capacity            = list->capacity == 0 ? 4 : list->capacity * 2;
        tbox_release_entry *items = realloc(list->items, capacity * sizeof(*items));
        /* Out of memory: leaking the userdata is safe, releasing it under
         * a running callback is not. */
        if (items == NULL)
            return;
        list->items    = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = (tbox_release_entry){ destroy, userdata };
}

void tbox_release_enter(tbox_release_list *list) {
    list->depth++;
}

void tbox_release_leave(tbox_release_list *list) {
    if (list->depth > 0 && --list->depth == 0)
        tbox_release_run(list);
}

void tbox_release_finish(tbox_release_list *list) {
    list->depth = 0;
    tbox_release_run(list);
    free(list->items);
    *list = (tbox_release_list){ 0 };
}
