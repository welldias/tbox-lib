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
 * tbox_font_face_cache tbox_app_open used to own locally (NOVO v2: a font
 * cache, not a single tbox_font_face -- see <tbox/font.h>). No
 * tbox_font_source field: NOVO v2, tbox_app_create resolves two separate
 * tbox_font_source_fontconfig instances (one per bold/non-bold query, see
 * tbox_app_create's own doc comment in tbox_app.c) but destroys both right
 * after their bytes are copied into the font cache -- tbox_font_face_cache_create
 * already defensively copies both byte blobs (see <tbox/font.h>), so
 * neither source needs to outlive that call, and tbox_app has nothing font-
 * source-shaped left to store for its own lifetime. */
typedef struct tbox_app tbox_app;

/* Same work tbox_app_open used to do (parse html/css -- both plain
 * NUL-terminated C strings, strlen() computed here --, resolve a real
 * sans-serif font at 16px and wrap it in a tbox_font_face_cache, open a
 * width x height Wayland window) but returns a handle instead of blocking.
 * NOVO v2: font resolution now really is two independent
 * tbox_font_source_fontconfig instances -- one queried {bold: false}, one
 * {bold: true} -- each resolved exactly once and destroyed right after (see
 * tbox_app_create's own doc comment in tbox_app.c for why two sources, not
 * one source resolved twice: tbox_font_source_resolve's contract
 * invalidates a source's previous resolve() result the moment the SAME
 * source resolves again). Returns NULL on the same failure conditions
 * tbox_app_open documented (font source/resolve/load failure, HTML/CSS
 * parse failure -- extremely unlikely --, or the window itself failing to
 * open), cleaning up whatever had already been allocated first; never
 * crashes either way.
 * Compiled only when both a real Wayland backend and real font discovery are
 * available at build time (TBOX_WAYLAND_FOUND and TBOX_FONTCONFIG_FOUND, see
 * src/CMakeLists.txt) -- with either missing, this declaration still exists,
 * but nothing implements it. */
tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height);

/* NOVO v2: same as tbox_app_create, plus an explicit tbox_ua_style_config
 * this app's tbox_context is opened with (via tbox_context_open_with_config
 * instead of tbox_context_open -- see ARCHITECTURE.md's "Application /
 * Orchestration -- fiação do cache de fontes e do config"). Application
 * does not interpret any field of `config` itself; it only forwards it to
 * Orchestration, which is the sole reader. Font resolution (two fontconfig
 * queries, one regular/one bold -- see tbox_app_create's own doc comment
 * above) is identical between the two functions; `config` only affects the
 * generated user-agent stylesheet, not font discovery. Fails under the
 * exact same conditions as tbox_app_create. */
tbox_app *tbox_app_create_with_config(const char *html, const char *css, int32_t width, int32_t height, tbox_ua_style_config config);

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

/* Destroys the backend, closes the context, destroys the font cache (NOVO
 * v2: was a single tbox_font_face; same order tbox_app_open used internally
 * before returning), and frees `app` itself. A no-op if app == NULL. */
void tbox_app_close(tbox_app *app);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_APP_H */
