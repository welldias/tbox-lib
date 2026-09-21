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

/* NOVO v3: same as tbox_app_create, but html_path/css_path are paths to
 * files read fully into memory (a local helper in tbox_app.c, same
 * read-whole-file-into-a-malloc'd-buffer shape already used by
 * tests/context/test_context.c's read_file()/example/css_cascade_origins.c's
 * -- see ARCHITECTURE.md's "Application -- leitura de arquivo externo")
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

/* NOVO v3: same as tbox_app_create_from_files, plus an explicit
 * tbox_ua_style_config this app's tbox_context is opened with -- same
 * relationship tbox_app_create_with_config has to tbox_app_create. Fails
 * under the same conditions as tbox_app_create_from_files. */
tbox_app *tbox_app_create_from_files_with_config(const char *html_path, const char *css_path, int32_t width, int32_t height, tbox_ua_style_config config);

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
 * success. Needs no Wayland connection/compositor at RUNTIME (unlike every
 * tbox_app_create* function, it never calls tbox_backend_wayland_open) --
 * but it is still only COMPILED when TBOX_WAYLAND_FOUND (alongside
 * TBOX_FONTCONFIG_FOUND), same as the rest of this file: src/CMakeLists.txt
 * excludes src/app/tbox_app.c from the build entirely without both, a
 * file-level (not function-level) gate this function inherited rather than
 * one it actually needs. Splitting screenshot support into its own
 * always-compiled file to lift that build-time requirement is possible
 * future work, not done here. */
bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path);

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
 * tbox_context_dispatch_click if one was pending, reads the backend's
 * current pointer position via tbox_backend_wayland_pointer_position and
 * feeds it to tbox_context_update_hover (NOVO v3 -- Interatividade
 * Avançada: real ":hover" support; see <tbox/context.h>), and separately
 * compares the backend's current tbox_backend_wayland_size against the size
 * observed on the previous tick to detect a resize. tbox_context keeps no
 * "dirty" flag of its own (tbox_context_dispatch_click/tbox_context_update_hover
 * communicate only through their own bool return) -- so tbox_app is where
 * all three signals (dispatch_click's return value, update_hover's return
 * value, and the resize comparison) are combined into this tick's
 * "recompute or not" decision. If any signal fired, redoes the whole
 * compute pipeline and presents it (tbox_context_run_frame +
 * tbox_backend_wayland_present) -- same "recompute everything" policy as v0,
 * just with more possible triggers now. A no-op if app == NULL. */
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
