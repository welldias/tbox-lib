#ifndef TBOX_APP_H
#define TBOX_APP_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/context.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Public application API. A tbox_app owns one document context, font/image
 * caches, and a window selected by the private backend interface. The GUI
 * core is not thread-safe; call these functions from the application thread. */
typedef struct tbox_app tbox_app;

/* True when this build can create an interactive window. Screenshot
 * rendering is available independently of Wayland when Fontconfig exists. */
bool tbox_app_backend_available(void);

/* Creates an interactive window from NUL-terminated HTML/CSS strings.
 * Returns NULL on invalid input, resource failure, or when no window backend
 * is built. The current backend is Linux/Wayland. */
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
 * success. Needs Fontconfig at build time, but no Wayland dependency. When
 * Fontconfig is unavailable the symbol remains linkable and returns false. */
bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path);

/* Access to the internal tbox_context -- for registering click handlers via
 * tbox_context_on_click, at any point before or after the first
 * tbox_app_step. Returns NULL if app == NULL. */
tbox_context *tbox_app_context(tbox_app *app);

/* Called for each translated key event before the context handles it.
 * Return true to consume the key. The handler may load another document. */
typedef bool (*tbox_app_key_handler)(tbox_app *app, tbox_key_event event, void *userdata);
void tbox_app_on_key(tbox_app *app, tbox_app_key_handler handler, void *userdata);

/* Replaces the current document in the existing window. NULL css_path uses
 * only the document's embedded styles and the user-agent stylesheet. On
 * failure the current document remains active. Relative image paths use the
 * new HTML file's directory. */
bool tbox_app_load_from_files(tbox_app *app, const char *html_path, const char *css_path);

/* Request a frame after the caller mutates the document or application
 * state outside an input handler. The next tbox_app_step recomputes styles,
 * layout and painting. Multiple requests before a step coalesce. */
void tbox_app_request_redraw(tbox_app *app);

/* Pumps a nonblocking round of backend events, dispatches pointer and
 * translated keyboard input to the context, and recomputes a frame when
 * input, resize, or tbox_app_request_redraw changes visible state.
 * A no-op if app == NULL. */
void tbox_app_step(tbox_app *app);

/* True once the window should close or its backend connection is lost.
 * The current Wayland backend also closes on Escape. NULL is already closed. */
bool tbox_app_should_close(const tbox_app *app);

/* Destroys the backend, closes the context, destroys the font cache (NOVO
 * v2: was a single tbox_font_face; same order tbox_app_open used internally
 * before returning), and frees `app` itself. A no-op if app == NULL. */
void tbox_app_close(tbox_app *app);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_APP_H */
