#ifndef TBOX_APP_H
#define TBOX_APP_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/context.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public application API. A tbox_app owns one document context, font/image
 * caches, and a window selected by the private backend interface.
 *
 * Threads: every function here, and everything reached through
 * tbox_app_context, belongs to the UI thread -- the one that created the
 * app (on macOS it will have to be the process's main thread). The only
 * exception is tbox_app_post, which any thread may call to hand work to the
 * UI thread. */
typedef struct tbox_app tbox_app;

/* True when this build can create an interactive window. Screenshot
 * rendering is available independently of Wayland when Fontconfig exists. */
bool tbox_app_backend_available(void);

/* Creates an interactive window from NUL-terminated HTML/CSS strings.
 * Returns NULL on invalid input, resource failure, or when no window backend
 * is built. The current backend is Linux/Wayland. */
tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height);

/* Same as tbox_app_create, opening the context with `options` (user-agent
 * style config, picker theme, resource directories, reduced motion; see
 * tbox_context_open_with_options). NULL is tbox_context_options_default().
 * Neither `options` nor its strings need outlive the call: the app copies
 * control_css so a later tbox_app_load_from_files keeps the theme. */
tbox_app *tbox_app_create_with_options(const char *html, const char *css, int32_t width, int32_t height, const tbox_context_options *options);

/* same as tbox_app_create, but html_path/css_path are paths to
 * files read fully into memory (a local helper in tbox_app.c, same
 * read-whole-file-into-a-malloc'd-buffer shape already used by
 * tests/context/test_context.c's read_file()/example/css_cascade_origins.c's
 * -- see docs/dev/historico.md's "Application -- leitura de arquivo externo")
 * rather than caller-supplied strings. html_path is required: NULL, or a
 * failure to read it (missing file, can't be opened, ...), returns NULL
 * immediately without touching css_path. css_path == NULL is NOT an error --
 * it means "no author stylesheet", treated exactly like passing "" to
 * tbox_app_create (the document still gets the UA stylesheet); a non-NULL
 * css_path that fails to read IS an error (NULL). Both file buffers are
 * freed right after tbox_app_create's underlying work (parsing html/css into
 * the new tbox_context) completes, success or failure alike -- the parsers
 * already copy whatever they need into their own document/stylesheet arenas,
 * so the file buffers don't need to outlive that call. Fails under the same
 * remaining conditions as tbox_app_create otherwise (font source/resolve/
 * load failure, or the window failing to open). */
tbox_app *tbox_app_create_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height);

/* File-based counterpart of tbox_app_create_with_options. */
tbox_app *tbox_app_create_from_files_with_options(const char *html_path, const char *css_path, int32_t width, int32_t height, const tbox_context_options *options);

/* Development/testing tool: renders html_path/css_path into an offscreen
 * width x height buffer and writes it to png_path -- WITHOUT ever opening a
 * Wayland window/backend (no tbox_backend_wayland_* call anywhere in this
 * function). Same file-loading (html_path required, css_path == NULL means
 * "no author stylesheet") and font resolution as tbox_app_create_from_files,
 * but tbox_context_run_frame + tbox_raster_display_list + tbox_raster_write_png
 * (see <tbox/output.h>) stand in for "open a window and present frames" --
 * one deterministic frame, rendered exactly like the interactive app would
 * render its first frame at this viewport size, with no compositor, no
 * screenshot tool (grim/similar), and no window-manager coordination
 * (floating/moving/finding the right window) involved at any point. Useful
 * for automated visual verification in headless/CI environments, or any
 * environment where driving a real compositor is inconvenient or unreliable.
 * Returns false if html_path fails to load, css_path is non-NULL and fails
 * to load, font resolution fails, or png_path can't be written; true on
 * success. Needs Fontconfig at build time, but no Wayland dependency. When
 * Fontconfig is unavailable the symbol remains linkable and returns false. */
bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path);
/* Offscreen counterpart taking context options (NULL: defaults). */
bool tbox_app_screenshot_from_files_with_options(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path, const tbox_context_options *options);

/* Access to the internal context for DOM access, layout and control state.
 * Event registrations use tbox_app_on_*. Returns NULL if app == NULL. */
tbox_context *tbox_app_context(tbox_app *app);

/* Document event callbacks run on the UI thread and receive this app.
 * The app looks up its current context internally when registering. Use
 * tbox_app_context(app) inside a handler for DOM/control operations.
 * Registrations belong to the current document: successful load_from_files
 * removes them and releases their userdata. Register again in on_load for
 * the new document. NULL app is a no-op and never transfers ownership.
 * destroy (optional) releases accepted userdata exactly once, after an
 * active handler returns. These callbacks must not synchronously replace
 * the document; use tbox_app_post to schedule load_from_files instead. */
typedef bool (*tbox_app_click_handler)(tbox_app *app, tbox_html_node *node, void *userdata);
typedef void (*tbox_app_input_handler)(tbox_app *app, tbox_html_node *input, const char *value, size_t value_length, void *userdata);
typedef void (*tbox_app_select_handler)(tbox_app *app, tbox_html_node *select, const char *value, size_t value_length, void *userdata);
typedef void (*tbox_app_submit_handler)(tbox_app *app, tbox_html_node *form, tbox_html_node *submitter, void *userdata);

/* Delegated click: each matching ancestor receives the handler, in binding
 * order at each level. true continues bubbling; false stops propagation.
 * Returns a document-scoped id, never reused within that document, or -1
 * for invalid selector, NULL app/handler, or allocation failure (userdata
 * remains the caller's). Enabled buttons also activate by Enter/Space. */
int tbox_app_on_click(tbox_app *app, const char *selector, size_t selector_length, tbox_app_click_handler handler, void *userdata, tbox_destroy_notify destroy);
/* Removes an active binding and releases userdata after a running handler
 * returns. false for NULL app or unknown/inactive id. */
bool tbox_app_unbind_click(tbox_app *app, int binding);

/* Single slots: replace the old handler or clear with NULL. Clearing does
 * not take ownership of the new userdata. If allocating the adapter fails,
 * the old handler remains and ownership of the new userdata stays with
 * the caller. Values/nodes are borrowed for the duration of the callback. */
/* User editing or a picker changed an input/textarea (file: file name).
 * value_length bytes, not necessarily NUL-terminated. */
void tbox_app_on_input(tbox_app *app, tbox_app_input_handler handler, void *userdata, tbox_destroy_notify destroy);
/* User selected another option. value is its value attribute, or collapsed
 * text when no attribute exists. Programmatic changes do not emit select. */
void tbox_app_on_select(tbox_app *app, tbox_app_select_handler handler, void *userdata, tbox_destroy_notify destroy);
/* A submit control or Enter submitted the form. submitter is NULL for
 * Enter in a field. No automatic navigation/network request occurs. */
void tbox_app_on_submit(tbox_app *app, tbox_app_submit_handler handler, void *userdata, tbox_destroy_notify destroy);

/* Called for each translated key event before the context handles it.
 * Return true to consume the key. The handler may load another document.
 * `event` is valid only during the call. Setting a handler replaces the
 * previous one (NULL clears it); the previous `destroy` (may be NULL) is
 * called for the previous userdata then, and the current one at
 * tbox_app_close (see <tbox/callback.h>). */
typedef bool (*tbox_app_key_handler)(tbox_app *app, const tbox_key_event *event, void *userdata);
void tbox_app_on_key(tbox_app *app, tbox_app_key_handler handler, void *userdata, tbox_destroy_notify destroy);

/* Window/document lifecycle callbacks, on the UI thread. Each registration
 * is a single slot: replace it or pass NULL to clear it. destroy releases
 * the previous userdata after any callback using it returns, and the last
 * userdata when the app closes. NULL app is a no-op (ownership unchanged).
 * These app-level registrations survive tbox_app_load_from_files.
 * They register C callbacks; HTML onload="..." etc. do not execute script. */
typedef void (*tbox_app_event_handler)(tbox_app *app, void *userdata);

/* Once per document, after its first computed frame is accepted by the
 * backend. Deferred to step/wait/run, so register after create and before
 * entering the loop. Registering after load does not replay it. Successful
 * document replacement schedules another load; failures do not. */
void tbox_app_on_load(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy);

/* Once when closing the window: native request, unconsumed Escape, backend
 * disconnection or explicit close. Context/resources are still available.
 * Notification only: cannot cancel closing. Replacing a document is not
 * a window close and does not emit unload. */
void tbox_app_on_unload(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy);

/* In tbox these names mean window keyboard focus loss/gain, independently
 * of pointer hover. They do not model browser navigation/page caching.
 * Initial focus is delivered after load; duplicate states are ignored. */
void tbox_app_on_pagehide(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy);
void tbox_app_on_pageshow(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy);

typedef struct tbox_app_resize_event {
    int32_t previous_width, previous_height;
    int32_t width, height; /* window content dimensions in layout pixels */
} tbox_app_resize_event;
typedef void (*tbox_app_resize_handler)(tbox_app *app, const tbox_app_resize_event *event, void *userdata);
/* After a change to an already observed window size, before rebuilding the
 * frame. Initial size is not a resize. Changes within one backend poll may
 * coalesce to its final size. event is borrowed for the callback only. */
void tbox_app_on_resize(tbox_app *app, tbox_app_resize_handler handler, void *userdata, tbox_destroy_notify destroy);

typedef struct tbox_app_error_event {
    tbox_string_view message; /* borrowed for the callback only */
} tbox_app_error_event;
typedef void (*tbox_app_error_handler)(tbox_app *app, const tbox_app_error_event *event, void *userdata);
/* Reserved notification: registration/cleanup work, but NO error is emitted
 * yet (including invalid markup, load failures and backend disconnection).
 * Existing error returns remain the way to detect failures. */
void tbox_app_on_error(tbox_app *app, tbox_app_error_handler handler, void *userdata, tbox_destroy_notify destroy);

/* Replaces the current document in the existing window. NULL css_path uses
 * only the document's embedded styles and the user-agent stylesheet. On
 * failure the current document remains active. Relative image paths use the
 * new HTML file's directory. */
bool tbox_app_load_from_files(tbox_app *app, const char *html_path, const char *css_path);

/* Request a frame after the caller mutates the document or application
 * state outside an input handler. The next tbox_app_step recomputes styles,
 * layout and painting. Multiple requests before a step coalesce. */
void tbox_app_request_redraw(tbox_app *app);

/* Debug aid: outlines, in magenta, every region repainted by each frame
 * (frames only repaint what changed since the previous one). The library
 * never reads the environment itself; example programs map an environment
 * variable such as TBOX_DEBUG_DAMAGE onto this. A no-op if app == NULL. */
void tbox_app_set_debug_damage(tbox_app *app, bool enabled);

/* One round of the loop: waits up to `max_wait_ms` for something to do
 * (-1: as long as it takes; 0: never blocks), then dispatches pointer and
 * keyboard input to the context, runs the tasks posted with tbox_app_post,
 * and recomputes a frame when input, resize, a task, an animation or
 * tbox_app_request_redraw changes visible state. It returns early, without
 * waiting the whole `max_wait_ms`, as soon as an event arrives, a task is
 * posted, or the library itself needs to run (an animation frame, the end
 * of a transition-delay, a scroll snap, a key repeat). Frames are paced by
 * the compositor: while an animation runs, each one waits for the display
 * to ask for it, and a hidden window stops animating. Callers with their
 * own loop pass their own next deadline; tbox_app_run passes -1.
 * A no-op if app == NULL. */
void tbox_app_wait(tbox_app *app, int32_t max_wait_ms);

/* tbox_app_wait(app, 0): one round that never blocks. */
void tbox_app_step(tbox_app *app);

/* Runs tbox_app_wait(app, -1) until tbox_app_should_close: sleeps while
 * nothing happens. A no-op if app == NULL. */
void tbox_app_run(tbox_app *app);

/* Work handed to the UI thread: runs there, inside a later tbox_app_wait,
 * in posting order, and marks the frame dirty (a task usually mutates the
 * document, so no tbox_app_request_redraw is needed). */
typedef void (*tbox_app_task)(tbox_app *app, void *userdata);

/* Queues `task` and wakes a tbox_app_wait in progress. Safe to call from
 * any thread -- the only function in this API that is -- including from a
 * task or handler on the UI thread itself. `destroy` (may be NULL) is
 * called once on the UI thread for `userdata` when the library is done
 * with it: right after the task ran, or when tbox_app_close drops a task
 * that never ran (see <tbox/callback.h>). Returns false -- nothing queued,
 * destroy not called, userdata still the caller's -- if app or task is
 * NULL or memory runs out. Every poster must be done before tbox_app_close.
 * Once the window is closed, tbox_app_wait/tbox_app_step only run queued
 * tasks, so after joining the posting threads one tbox_app_step delivers
 * whatever is left. */
bool tbox_app_post(tbox_app *app, tbox_app_task task, void *userdata, tbox_destroy_notify destroy);

/* True once the window should close or its backend connection is lost.
 * The current Wayland backend also closes on Escape. NULL is already closed. */
bool tbox_app_should_close(const tbox_app *app);

/* Destroys the backend, closes the context, destroys the font cache (NOVO
 * v2: was a single tbox_font_face; same order tbox_app_open used internally
 * before returning), and frees `app` itself. A no-op if app == NULL. */
/* During a callback, teardown waits until the enclosing loop operation
 * returns. unload runs once while the context is valid. Calling close
 * recursively from unload is safe. The handle must not be used after the
 * outer operation returns if a callback requested its destruction. Nested
 * step/wait/run calls during callbacks are ignored. Join posting threads
 * before calling close, including from a callback. */
void tbox_app_close(tbox_app *app);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_APP_H */
