#ifndef TBOX_CALLBACK_H
#define TBOX_CALLBACK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Releases a `userdata` the library stored next to a callback. Every API
 * that keeps a callback's userdata also takes one of these (NULL: nothing
 * to release) and calls it exactly once, on the UI thread, when it stops
 * using that userdata: the binding is removed or replaced, a posted task
 * ran or was dropped, or the owner (context, app, font cache) is closed.
 * A callback running when its own registration goes away keeps its
 * userdata until it returns; the release happens after. A registration
 * that fails never calls it: the userdata stays the caller's.
 *
 * Meant for language bindings: a garbage-collected closure handed to C is
 * pinned by a handle in `userdata`, and this is where the binding unpins it. */
typedef void (*tbox_destroy_notify)(void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CALLBACK_H */
