#include <tbox/app.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/context.h>
#include <tbox/font.h>
#include <tbox/image.h>
#include <tbox/output.h>
#include <tbox/string_view.h>

#include "output/tbox_window_backend.h"

/* Non-blocking, per ARCHITECTURE.md's "loop não-bloqueante": each
 * tbox_app_step pumps at most this many milliseconds of Wayland events
 * before moving on to click/resize handling and returning -- see
 * tbox_backend_wayland_poll's doc comment ("0 never blocks"). tbox_app_step
 * is meant to be called every tick of a caller-driven loop (e.g. alongside a
 * future script engine's own task queue -- see ARCHITECTURE.md's "Fora de
 * escopo" about vsync), not to wait for something to happen. */
#define TBOX_APP_POLL_TIMEOUT_MS 0

#ifndef TBOX_HAS_WINDOW_BACKEND
#define TBOX_HAS_WINDOW_BACKEND 0
#endif

/* Generous fixed size for tbox_app_dirname's output buffer -- truncated
 * (never overflowed) if an html_path's directory component is longer than
 * this, same truncate-not-reject posture as tbox_image_cache's own path
 * buffers (src/image/tbox_image.c). */
#define TBOX_APP_PATH_BUF_SIZE 4096

#if TBOX_HAS_WINDOW_BACKEND
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
    bool redraw_requested;
    const tbox_html_node *paste_target;
};
#endif

/* Resolves a "sans-serif" query at the given bold flag through a FRESH
 * tbox_font_source_fontconfig (created, resolved exactly once, and left for
 * the caller to destroy) -- see tbox_app_create_impl's doc comment below
 * for why every call site needs its own source rather than reusing one
 * across two resolves. Returns NULL on either the source's creation or the
 * resolve itself failing (nothing left allocated in that case); on success,
 * writes the resolved bytes to out_data/out_size (both pointers), matching
 * tbox_font_source_resolve's own out-param contract. */
static tbox_font_source *tbox_app_resolve_font_source(bool bold, const void **out_data, size_t *out_size) {
    tbox_font_source *source = tbox_font_source_fontconfig_create();
    if (source == NULL) {
        return NULL;
    }

    tbox_font_query query = {
        .family = tbox_string_view_make("sans-serif", strlen("sans-serif")),
        .bold   = bold,
        .italic = false,
    };

    if (!tbox_font_source_resolve(source, query, out_data, out_size)) {
        tbox_font_source_destroy(source);
        return NULL;
    }

    return source;
}

/* The cache copies returned bytes before another resolve. The shared source
 * remains alive until the cache is destroyed, so the callback's output is
 * valid during that copy and each resolved font is eventually released. */
static bool tbox_app_font_resolver(void *userdata, tbox_font_query query, const void **out_data, size_t *out_size) {
    tbox_font_source *source = userdata;
    return tbox_font_source_resolve(source, query, out_data, out_size);
}

/* NOVO v12: shared by tbox_app_create_impl and tbox_app_screenshot_from_files
 * -- both used to duplicate this exact "resolve regular, resolve bold,
 * tbox_font_face_cache_create" sequence independently (see
 * ARCHITECTURE.md's "Limpeza recomendada, junto"); now there is one copy.
 * Resolves a real bold face alongside the regular one exactly like before
 * (see the retained comment on tbox_app_resolve_font_source above for why
 * TWO tbox_font_source_fontconfig instances are used, one per bold/non-bold
 * query), builds the font cache with tbox_app_font_resolver wired in as its
 * on-demand resolver using a third, retained Fontconfig source. The first
 * two sources can be destroyed after the cache copies their bytes. The
 * caller owns the retained source and destroys it after the cache. */
static tbox_font_face_cache *tbox_app_build_font_cache(tbox_font_source **out_resolver_source) {
    const void *regular_data         = NULL;
    size_t regular_size              = 0;
    tbox_font_source *regular_source = tbox_app_resolve_font_source(false, &regular_data, &regular_size);
    if (regular_source == NULL) {
        return NULL;
    }

    const void *bold_data         = NULL;
    size_t bold_size              = 0;
    tbox_font_source *bold_source = tbox_app_resolve_font_source(true, &bold_data, &bold_size);
    if (bold_source == NULL) {
        tbox_font_source_destroy(regular_source);
        return NULL;
    }

    tbox_font_source *resolver_source = tbox_font_source_fontconfig_create();
    if (resolver_source == NULL) {
        tbox_font_source_destroy(bold_source);
        tbox_font_source_destroy(regular_source);
        return NULL;
    }

    tbox_font_face_cache *fonts = tbox_font_face_cache_create(regular_data, regular_size, bold_data, bold_size, tbox_app_font_resolver, resolver_source);
    /* Both sources' bytes are already copied into `fonts` above (or the
     * call failed and there is nothing left to copy from) -- neither source
     * is needed past this point, success or failure alike. */
    tbox_font_source_destroy(bold_source);
    tbox_font_source_destroy(regular_source);
    if (fonts == NULL) {
        tbox_font_source_destroy(resolver_source);
        return NULL;
    }
    *out_resolver_source = resolver_source;
    return fonts;
}

/* Writes `path`'s directory component (everything before the last '/') into
 * `out`, NUL-terminated and truncated if it doesn't fit `out_size` -- same
 * "truncate, never crash" posture as tbox_image_cache_get's own fixed
 * buffers (src/image/tbox_image.c). `path == NULL`, or no '/' anywhere in
 * it, writes an empty string, which tbox_image_cache_create treats
 * identically to a NULL base_dir (<tbox/image.h>: "use src as-is").
 * Deliberately no dirname()/libgen.h dependency -- manual last-'/' scan,
 * matching this file's existing "html_path/css_path used exactly as given,
 * no path-joining" posture everywhere else. */
static void tbox_app_dirname(const char *path, char *out, size_t out_size) {
    if (out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (path == NULL) {
        return;
    }

    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        return;
    }

    size_t len = (size_t)(last_slash - path);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, path, len);
    out[len] = '\0';
}

/* Shared by tbox_app_create/tbox_app_create_with_config (the same "thin
 * public wrapper over one real implementation" shape as
 * tbox_context_open/tbox_context_open_with_config): builds the font cache
 * (see tbox_app_build_font_cache above) and the image cache (`base_dir` --
 * see tbox_app_dirname above -- is NULL/empty for tbox_app_create/
 * _with_config's raw-HTML-string callers, since there is no HTML file to
 * derive one from; tbox_app_create_from_files_impl below passes the real
 * one), opens the tbox_context (with or without an explicit
 * tbox_ua_style_config, per `use_config`) and the Wayland window, and
 * allocates the tbox_app struct.
 *
 * Returns NULL on any failure, cleaning up whatever had already been
 * allocated first; never crashes either way. */
#if TBOX_HAS_WINDOW_BACKEND
static tbox_app *tbox_app_create_impl(const char *html, const char *css, const char *base_dir, int32_t width, int32_t height, bool use_config, tbox_ua_style_config config) {
    tbox_font_source *resolver_source = NULL;
    tbox_font_face_cache *fonts = tbox_app_build_font_cache(&resolver_source);
    if (fonts == NULL) {
        return NULL;
    }

    tbox_image_cache *images = tbox_image_cache_create(base_dir);
    if (images == NULL) {
        tbox_font_face_cache_destroy(fonts);
        tbox_font_source_destroy(resolver_source);
        return NULL;
    }

    tbox_context *ctx = use_config ? tbox_context_open_with_config(html, strlen(html), css, strlen(css), fonts, images, config) : tbox_context_open(html, strlen(html), css, strlen(css), fonts, images);
    if (ctx == NULL) {
        tbox_image_cache_destroy(images);
        tbox_font_face_cache_destroy(fonts);
        tbox_font_source_destroy(resolver_source);
        return NULL;
    }

    tbox_window_backend *backend = tbox_window_backend_open(width, height, NULL);
    if (backend == NULL) {
        tbox_context_close(ctx);
        tbox_image_cache_destroy(images);
        tbox_font_face_cache_destroy(fonts);
        tbox_font_source_destroy(resolver_source);
        return NULL;
    }

    tbox_app *app = (tbox_app *)malloc(sizeof(tbox_app));
    if (app == NULL) {
        tbox_window_backend_destroy(backend);
        tbox_context_close(ctx);
        tbox_image_cache_destroy(images);
        tbox_font_face_cache_destroy(fonts);
        tbox_font_source_destroy(resolver_source);
        return NULL;
    }

    app->fonts       = fonts;
    app->resolver_source = resolver_source;
    app->images      = images;
    app->ctx         = ctx;
    app->backend     = backend;
    app->last_width  = 0;
    app->last_height = 0;
    app->closed      = false;
    app->redraw_requested = false;
    app->paste_target = NULL;

    return app;
}

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    tbox_ua_style_config unused_config; /* never read: use_config == false below */
    memset(&unused_config, 0, sizeof(unused_config));
    return tbox_app_create_impl(html, css, NULL, width, height, false, unused_config);
}

tbox_app *tbox_app_create_with_config(const char *html, const char *css, int32_t width, int32_t height, tbox_ua_style_config config) {
    return tbox_app_create_impl(html, css, NULL, width, height, true, config);
}
#endif

/* NOVO v3: reads `path` fully into a malloc'd, NUL-terminated buffer -- same
 * read-whole-file shape as tests/context/test_context.c's read_file() and
 * example/css_cascade_origins.c's own copy (see ARCHITECTURE.md's
 * "Application -- leitura de arquivo externo" on this being a small helper
 * this codebase already tolerates being duplicated a few times, not yet a
 * shared Base utility), with one difference: those callers hand their
 * buffer's length to a function that takes an explicit size, but
 * tbox_app_create_impl below takes plain NUL-terminated C strings (it calls
 * strlen() on them itself), so this helper allocates one extra byte and
 * writes a terminating '\0' rather than returning a separate size. Returns
 * NULL on any failure (file doesn't exist/can't be opened, seek/tell
 * failure, allocation failure, or a short read); the file is always closed
 * either way. */
static char *tbox_app_read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

/* Shared by tbox_app_create_from_files/tbox_app_create_from_files_with_config
 * -- same "thin public wrapper over one shared impl" shape as
 * tbox_app_create_impl above. Reads html_path (required: NULL, or a read
 * failure, returns NULL immediately without touching css_path) and css_path
 * (NULL means "no author stylesheet", treated as an empty CSS string rather
 * than an error; a non-NULL css_path that fails to read IS an error) via
 * tbox_app_read_file(), then forwards the resulting NUL-terminated buffers
 * to tbox_app_create_impl exactly like tbox_app_create/_with_config already
 * do above. Both buffers are freed right after that call returns, success or
 * failure alike -- tbox_html_parse/tbox_css_parse (called inside
 * tbox_context_open/_with_config, called inside tbox_app_create_impl)
 * already copy whatever they need into their own document/stylesheet
 * arenas, so these buffers don't need to outlive that call. */
#if TBOX_HAS_WINDOW_BACKEND
static tbox_app *tbox_app_create_from_files_impl(const char *html_path, const char *css_path, int32_t width, int32_t height, bool use_config, tbox_ua_style_config config) {
    if (html_path == NULL) {
        return NULL;
    }

    char *html = tbox_app_read_file(html_path);
    if (html == NULL) {
        return NULL;
    }

    char *css = NULL;
    if (css_path != NULL) {
        css = tbox_app_read_file(css_path);
        if (css == NULL) {
            free(html);
            return NULL;
        }
    }

    char base_dir[TBOX_APP_PATH_BUF_SIZE];
    tbox_app_dirname(html_path, base_dir, sizeof(base_dir));

    tbox_app *app = tbox_app_create_impl(html, css != NULL ? css : "", base_dir, width, height, use_config, config);

    free(css);
    free(html);

    return app;
}

tbox_app *tbox_app_create_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height) {
    tbox_ua_style_config unused_config = { 0 };
    return tbox_app_create_from_files_impl(html_path, css_path, width, height, false, unused_config);
}

tbox_app *tbox_app_create_from_files_with_config(const char *html_path, const char *css_path, int32_t width, int32_t height, tbox_ua_style_config config) {
    return tbox_app_create_from_files_impl(html_path, css_path, width, height, true, config);
}
#endif

/* See <tbox/app.h>'s doc comment. Shares tbox_app_read_file/
 * tbox_app_build_font_cache with the tbox_app_create* family above -- the
 * only real difference is what happens after tbox_context_open succeeds: no
 * tbox_backend_wayland_open, no tbox_app struct, just one
 * tbox_context_run_frame + tbox_raster_display_list into a locally-owned
 * pixel buffer, written out via tbox_raster_write_png. Everything opened
 * along the way (font cache, context, pixel buffer) is torn down before
 * returning, success or failure alike -- there is no handle for a caller to
 * hold onto afterward, unlike tbox_app_create*. */
bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path) {
    if (html_path == NULL || png_path == NULL || width <= 0 || height <= 0) {
        return false;
    }

    char *html = tbox_app_read_file(html_path);
    if (html == NULL) {
        return false;
    }

    char *css = NULL;
    if (css_path != NULL) {
        css = tbox_app_read_file(css_path);
        if (css == NULL) {
            free(html);
            return false;
        }
    }

    char base_dir[TBOX_APP_PATH_BUF_SIZE];
    tbox_app_dirname(html_path, base_dir, sizeof(base_dir));

    bool ok = false;

    tbox_font_source *resolver_source = NULL;
    tbox_font_face_cache *fonts = tbox_app_build_font_cache(&resolver_source);
    if (fonts != NULL) {
        tbox_image_cache *images = tbox_image_cache_create(base_dir);
        if (images != NULL) {
            tbox_context *ctx = tbox_context_open(html, strlen(html), css != NULL ? css : "", css != NULL ? strlen(css) : 0, fonts, images);
            if (ctx != NULL) {
                tbox_display_list list;
                tbox_context_run_frame(ctx, (double)width, (double)height, &list);

                uint32_t *pixels = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)width * (size_t)height);
                if (pixels != NULL) {
                    /* Same "clear to opaque white first" v0's tbox_backend_wayland_present
                     * uses -- see its doc comment in <tbox/output.h> for why (no UA
                     * background-color default yet, white matches every real browser's
                     * canvas default more closely than showing nothing/black would). */
                    for (size_t i = 0; i < (size_t)width * (size_t)height; i++) {
                        pixels[i] = 0xFFFFFFFFu;
                    }
                    tbox_raster_display_list(pixels, width, height, &list);
                    ok = tbox_raster_write_png(png_path, pixels, width, height);
                    free(pixels);
                }

                tbox_context_close(ctx);
            }
            tbox_image_cache_destroy(images);
        }
        tbox_font_face_cache_destroy(fonts);
        tbox_font_source_destroy(resolver_source);
    }

    free(css);
    free(html);
    return ok;
}

#if TBOX_HAS_WINDOW_BACKEND
bool tbox_app_backend_available(void) {
    return true;
}

tbox_context *tbox_app_context(tbox_app *app) {
    if (app == NULL) {
        return NULL;
    }
    return app->ctx;
}

void tbox_app_request_redraw(tbox_app *app) {
    if (app != NULL) {
        app->redraw_requested = true;
    }
}

void tbox_app_step(tbox_app *app) {
    if (app == NULL) {
        return;
    }

    if (!tbox_window_backend_poll(app->backend, TBOX_APP_POLL_TIMEOUT_MS)) {
        /* Connection to the compositor is gone -- see the `closed` field's
         * comment above. There is nothing left to dispatch/redraw. */
        app->closed = true;
        return;
    }

    bool dirty = app->redraw_requested;
    app->redraw_requested = false;

    tbox_input_event event;
    while (tbox_window_backend_take_event(app->backend, &event)) {
        switch (event.kind) {
        case TBOX_INPUT_POINTER_CLICK: {
            if (tbox_context_scrollbar_press(app->ctx, event.data.click.x, event.data.click.y)) {
                dirty = true;
                break;
            }
            tbox_context_dispatch_click(app->ctx, event.data.click.x, event.data.click.y);
            if (event.data.click.double_click) {
                tbox_context_dispatch_key(app->ctx,
                    (tbox_key_event){TBOX_KEY_A, true, false, true});
            }
            /* Clicking inside an already focused text field may move its
             * cursor without changing focus or firing a click handler. */
            dirty = true;
            break;
        }
        case TBOX_INPUT_POINTER_DRAG:
            if (tbox_context_scrollbar_drag(app->ctx, event.data.drag.x, event.data.drag.y))
                dirty = true;
            else if (tbox_context_drag_select_at(app->ctx, event.data.drag.x, event.data.drag.y)) dirty = true;
            break;
        case TBOX_INPUT_POINTER_RELEASE:
            tbox_context_scrollbar_release(app->ctx);
            break;
        case TBOX_INPUT_POINTER_SCROLL:
            if (tbox_context_scroll(app->ctx, event.data.scroll.x, event.data.scroll.y,
                    event.data.scroll.delta_y)) dirty = true;
            break;
        case TBOX_INPUT_KEY:
            if (event.data.key.pressed && event.data.key.control &&
                (event.data.key.key == TBOX_KEY_C || event.data.key.key == TBOX_KEY_X)) {
                tbox_string_view selected = tbox_context_selected_text(app->ctx);
                if (selected.size > 0 && tbox_window_backend_clipboard_copy(app->backend,
                        selected.data, selected.size, event.serial) &&
                    event.data.key.key == TBOX_KEY_X) {
                    if (tbox_context_dispatch_key(app->ctx,
                            (tbox_key_event){TBOX_KEY_DELETE, true, false, false})) dirty = true;
                }
                break;
            }
            if (event.data.key.pressed && event.data.key.control && event.data.key.key == TBOX_KEY_V) {
                app->paste_target = tbox_window_backend_clipboard_paste(app->backend) ?
                    tbox_context_focused_node(app->ctx) : NULL;
                break;
            }
            if (tbox_context_dispatch_key(app->ctx, event.data.key)) {
                dirty = true;
            } else if (event.data.key.pressed && event.data.key.key == TBOX_KEY_ESCAPE) {
                app->closed = true;
            }
            break;
        case TBOX_INPUT_TEXT:
            if (tbox_context_dispatch_text(app->ctx,
                    tbox_string_view_make(event.data.text.utf8, event.data.text.length))) {
                dirty = true;
            }
            break;
        case TBOX_INPUT_PASTE:
            if (app->paste_target != NULL &&
                app->paste_target == tbox_context_focused_node(app->ctx)) {
                const tbox_html_node *target = app->paste_target;
                bool multiline = target->type == TBOX_HTML_NODE_ELEMENT &&
                    target->element.tag_name.size == 8 &&
                    memcmp(target->element.tag_name.data, "textarea", 8) == 0;
                size_t output = 0;
                for (size_t i = 0; i < event.data.paste.length; i++) {
                    char ch = event.data.paste.utf8[i];
                    if (multiline) {
                        if (ch == '\r') {
                            if (i + 1 < event.data.paste.length && event.data.paste.utf8[i + 1] == '\n') i++;
                            ch = '\n';
                        } else if (ch == '\0') ch = ' ';
                    } else if (ch == '\r' || ch == '\n' || ch == '\t' || ch == '\0') ch = ' ';
                    event.data.paste.utf8[output++] = ch;
                }
                if (tbox_context_dispatch_text(app->ctx,
                        tbox_string_view_make(event.data.paste.utf8, output))) dirty = true;
            }
            app->paste_target = NULL;
            free(event.data.paste.utf8);
            break;
        }
    }

    /* NOVO v3: pointer position, not just click -- :hover. Called every
     * tick unconditionally (no check for whether the pointer actually
     * moved since the last tick), same O(n) hit-test cost already accepted
     * elsewhere in this project; see tbox_context_update_hover's doc
     * comment in <tbox/context.h>. */
    double pointer_x          = 0.0;
    double pointer_y          = 0.0;
    bool has_pointer_position = tbox_window_backend_pointer_position(app->backend, &pointer_x, &pointer_y);
    if (tbox_context_update_hover(app->ctx, has_pointer_position, pointer_x, pointer_y)) {
        dirty = true;
    }

    int32_t current_width  = 0;
    int32_t current_height = 0;
    tbox_window_backend_size(app->backend, &current_width, &current_height);
    if (current_width != app->last_width || current_height != app->last_height) {
        app->last_width  = current_width;
        app->last_height = current_height;
        dirty            = true;
    }

    if (dirty) {
        tbox_display_list list;
        tbox_context_run_frame(app->ctx, (double)app->last_width, (double)app->last_height, &list);
        tbox_window_backend_present(app->backend, &list);
    }
}

bool tbox_app_should_close(const tbox_app *app) {
    if (app == NULL) {
        return true;
    }
    return app->closed || tbox_window_backend_should_close(app->backend);
}

void tbox_app_close(tbox_app *app) {
    if (app == NULL) {
        return;
    }

    tbox_window_backend_destroy(app->backend);
    tbox_context_close(app->ctx);
    tbox_image_cache_destroy(app->images);
    tbox_font_face_cache_destroy(app->fonts);
    tbox_font_source_destroy(app->resolver_source);
    free(app);
}
#else
bool tbox_app_backend_available(void) {
    return false;
}

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    (void)html; (void)css; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_with_config(const char *html, const char *css, int32_t width, int32_t height, tbox_ua_style_config config) {
    (void)html; (void)css; (void)width; (void)height; (void)config;
    return NULL;
}

tbox_app *tbox_app_create_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height) {
    (void)html_path; (void)css_path; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_from_files_with_config(const char *html_path, const char *css_path, int32_t width, int32_t height, tbox_ua_style_config config) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)config;
    return NULL;
}

tbox_context *tbox_app_context(tbox_app *app) { (void)app; return NULL; }
void tbox_app_request_redraw(tbox_app *app) { (void)app; }
void tbox_app_step(tbox_app *app) { (void)app; }
bool tbox_app_should_close(const tbox_app *app) { (void)app; return true; }
void tbox_app_close(tbox_app *app) { (void)app; }
#endif
