#ifndef TBOX_APP_INTERNAL_H
#define TBOX_APP_INTERNAL_H
#include "base/tbox_mutex.h"
#include "base/tbox_release.h"
#include "output/tbox_window_backend.h"
#include <tbox/app.h>

typedef enum tbox_app_event_kind {
    TBOX_APP_EVENT_LOAD,
    TBOX_APP_EVENT_UNLOAD,
    TBOX_APP_EVENT_PAGEHIDE,
    TBOX_APP_EVENT_PAGESHOW,
    TBOX_APP_EVENT_RESIZE,
    TBOX_APP_EVENT_ERROR,
    TBOX_APP_EVENT_COUNT
} tbox_app_event_kind;
typedef struct tbox_app_event_binding {
    union {
        tbox_app_event_handler simple;
        tbox_app_resize_handler resize;
        tbox_app_error_handler error;
    } callback;
    void *userdata;
    tbox_destroy_notify destroy;
} tbox_app_event_binding;

/* One tbox_app_post call waiting for the UI thread. */
typedef struct tbox_app_task_entry {
    tbox_app_task task;
    void *userdata;
    tbox_destroy_notify destroy;
    struct tbox_app_task_entry *next;
} tbox_app_task_entry;

struct tbox_app {
    tbox_font_face_cache *fonts;
    tbox_font_source *resolver_source;
    tbox_image_cache *images;
    tbox_context *ctx;
    tbox_window_backend *backend;

    /* Last size observed by tbox_app_step, used to detect a resize (see
     * tbox_app_step). Deliberately initialized to 0/0 in tbox_app_create,
     * not to the window's actual initial size: since a real window's size is
     * always positive, the very first tbox_app_step call always sees a
     * "change" against this sentinel, which doubles as the trigger for the
     * initial frame -- same first-frame behavior tbox_app_open used to get
     * from an explicit `first` flag, without needing one here. */
    int32_t last_width;
    int32_t last_height;

    /* tbox_backend_wayland_should_close deliberately does NOT become true
     * when tbox_backend_wayland_poll reports a lost compositor connection
     * (see output.h's doc comment on tbox_backend_wayland_poll) -- v0's
     * blocking tbox_app_open handled that itself by breaking out of its
     * loop when poll returned false. tbox_app_step/_should_close reproduce
     * that same "treat a lost connection as a close request" behavior here,
     * via this flag, so a step-based caller's `while
     * (!tbox_app_should_close(app))` loop still terminates instead of
     * spinning forever once the connection is gone. */
    bool closed;
    bool destroy_requested, destroying;
    unsigned call_depth;
    bool load_pending, unload_emitted;
    bool focused, reported_focus;
    tbox_app_event_binding events[TBOX_APP_EVENT_COUNT];
    bool redraw_requested;
    /* tbox_html_document_revision right after the last run_frame: a
     * different value means the DOM changed and needs a frame. */
    uint64_t drawn_revision;
    const tbox_html_node *paste_target;
    tbox_context_options options;
    char *control_css_copy;
    tbox_app_key_handler key_handler;
    void *key_handler_userdata;
    tbox_destroy_notify key_handler_destroy;
    /* Userdata releases waiting for an event handler or a task in progress
     * (see src/base/tbox_release.h). */
    tbox_release_list releases;

    /* After a frame the layout may have moved under a still pointer: the
     * next wait re-tests :hover before sleeping. */
    bool hover_recheck;

    /* tbox_app_post's queue, the only state touched from other threads. */
    tbox_mutex tasks_lock;
    bool tasks_closed; /* protected by tasks_lock */
    tbox_app_task_entry *tasks_head;
    tbox_app_task_entry *tasks_tail;
};
/* Shared operation guard: defer app teardown and userdata releases until
 * the enclosing operation returns. leave may destroy app. */
void tbox_app_enter(tbox_app *app);
void tbox_app_leave(tbox_app *app);

/* Event helpers called under the operation guard by the app loop. */
bool tbox_app_emit(tbox_app *app, tbox_app_event_kind kind, const tbox_app_resize_event *resize);
bool tbox_app_emit_key(tbox_app *app, const tbox_key_event *event);
void tbox_app_notify_unload(tbox_app *app);
bool tbox_app_notify_focus(tbox_app *app);
/* Release key/lifecycle bindings during app teardown, before releases finish. */
void tbox_app_events_release(tbox_app *app);

/* Shares construction with headless backend integration tests. Resources
 * transfer only on success; callers clean them up if allocation fails. */
tbox_app *tbox_app_adopt(tbox_context *ctx, tbox_font_face_cache *fonts, tbox_font_source *resolver_source, tbox_image_cache *images, tbox_window_backend *backend, const tbox_context_options *options);
#endif
