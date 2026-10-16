#ifndef TBOX_RELEASE_H
#define TBOX_RELEASE_H

#include <stddef.h>

#include <tbox/callback.h>

/* Userdata releases that must wait for the callbacks in progress: a
 * handler that unbinds or replaces itself still runs with its userdata,
 * so tbox_release_add only queues while some callback is on the stack
 * (depth > 0) and the outermost tbox_release_leave runs the queue.
 * Zero-initialized is empty. Plain malloc, not an arena: entries come and
 * go one by one for the owner's whole lifetime. */
typedef struct tbox_release_entry {
    tbox_destroy_notify destroy;
    void *userdata;
} tbox_release_entry;

typedef struct tbox_release_list {
    tbox_release_entry *items;
    size_t count;
    size_t capacity;
    unsigned depth;
} tbox_release_list;

/* Calls destroy(userdata) now, or after the outermost callback returns.
 * A NULL destroy is a no-op. */
void tbox_release_add(tbox_release_list *list, tbox_destroy_notify destroy, void *userdata);

/* Brackets a callback invocation. */
void tbox_release_enter(tbox_release_list *list);
void tbox_release_leave(tbox_release_list *list);

/* Runs whatever is still queued and frees the list (owner teardown). */
void tbox_release_finish(tbox_release_list *list);

#endif
