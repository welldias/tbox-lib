#ifndef TBOX_APP_H
#define TBOX_APP_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/context.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * The public, top-level API for the "open a window and show this document,
 * interactively" use case (see ARCHITECTURE.md's "v1 -- Interatividade" ->
 * "Application (tbox_app) -- loop não-bloqueante" section this materializes).
 *
 * v1 replaces v0's single blocking tbox_app_open with a stepable handle:
 * tbox_app_create opens everything (same work tbox_app_open used to do
 * internally) and hands back a handle the caller advances one tick at a time
 * via tbox_app_step, instead of a single call that blocks until the window
 * closes. Accepted as a breaking change -- see ARCHITECTURE.md -- since
 * tbox_app_open had no real external consumer besides example/ yet.
 *
 * Opaque: owns a tbox_context plus a tbox_backend_wayland and the
 * tbox_font_face/tbox_font_source pair tbox_app_open used to own locally. */
typedef struct tbox_app tbox_app;

/* Same work tbox_app_open used to do (parse html/css -- both plain
 * NUL-terminated C strings, strlen() computed here --, resolve a single
 * document-wide sans-serif font via tbox_font_source_fontconfig at 16px,
 * open a width x height Wayland window) but returns a handle instead of
 * blocking. Returns NULL on the same failure conditions tbox_app_open
 * documented (font source/resolve/load failure, HTML/CSS parse failure --
 * extremely unlikely --, or the window itself failing to open), cleaning up
 * whatever had already been allocated first; never crashes either way.
 * Compiled only when both a real Wayland backend and real font discovery are
 * available at build time (TBOX_WAYLAND_FOUND and TBOX_FONTCONFIG_FOUND, see
 * src/CMakeLists.txt) -- with either missing, this declaration still exists,
 * but nothing implements it. */
tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height);

/* Access to the internal tbox_context -- for registering click handlers via
 * tbox_context_on_click, at any point before or after the first
 * tbox_app_step. Returns NULL if app == NULL. */
tbox_context *tbox_app_context(tbox_app *app);

/* One tick of the loop: pumps at most one short/non-blocking round of
 * Wayland events (tbox_backend_wayland_poll with a short timeout -- v1 keeps
 * polling every tick rather than blocking, since a click or resize can no
 * longer be the only thing worth waking up for once a caller may be driving
 * its own task queue alongside this loop; see ARCHITECTURE.md's "Fora de
 * escopo" about vsync), then drains at most one pending click via
 * tbox_backend_wayland_take_click and dispatches it through
 * tbox_context_dispatch_click if one was pending, and separately compares
 * the backend's current tbox_backend_wayland_size against the size observed
 * on the previous tick to detect a resize. tbox_context keeps no "dirty"
 * flag of its own (tbox_context_dispatch_click communicates only through its
 * bool return) -- so tbox_app is where the two signals (dispatch_click's
 * return value, and the resize comparison) are combined into this tick's
 * "recompute or not" decision. If either signal fired, redoes the whole
 * compute pipeline and presents it (tbox_context_run_frame +
 * tbox_backend_wayland_present) -- same "recompute everything" policy as v0,
 * just with a second possible trigger now. A no-op if app == NULL. */
void tbox_app_step(tbox_app *app);

/* True once the underlying window should close (compositor close request,
 * ESC, or a lost connection -- see tbox_backend_wayland_should_close/
 * tbox_backend_wayland_poll). NULL is treated as already closed (true). */
bool tbox_app_should_close(const tbox_app *app);

/* Destroys the backend, closes the context, destroys the loaded font (same
 * order tbox_app_open used internally before returning), and frees `app`
 * itself. A no-op if app == NULL. */
void tbox_app_close(tbox_app *app);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_APP_H */
