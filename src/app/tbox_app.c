#include <tbox/app.h>

#include <stdlib.h>
#include <string.h>

#include <tbox/context.h>
#include <tbox/font.h>
#include <tbox/output.h>
#include <tbox/string_view.h>

/* Non-blocking, per ARCHITECTURE.md's "loop não-bloqueante": each
 * tbox_app_step pumps at most this many milliseconds of Wayland events
 * before moving on to click/resize handling and returning -- see
 * tbox_backend_wayland_poll's doc comment ("0 never blocks"). tbox_app_step
 * is meant to be called every tick of a caller-driven loop (e.g. alongside a
 * future script engine's own task queue -- see ARCHITECTURE.md's "Fora de
 * escopo" about vsync), not to wait for something to happen. */
#define TBOX_APP_POLL_TIMEOUT_MS 0

struct tbox_app {
    tbox_font_face_cache *fonts;
    tbox_context *ctx;
    tbox_backend_wayland *backend;

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
};

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

/* Shared by tbox_app_create/tbox_app_create_with_config (the same "thin
 * public wrapper over one real implementation" shape as
 * tbox_context_open/tbox_context_open_with_config): resolves a real bold
 * face alongside the regular one, builds the font cache, opens the
 * tbox_context (with or without an explicit tbox_ua_style_config, per
 * `use_config`) and the Wayland window, and allocates the tbox_app struct.
 *
 * Why TWO tbox_font_source_fontconfig instances (one per bold/non-bold
 * query) rather than one source resolved twice: tbox_font_source_resolve's
 * documented contract (<tbox/font.h>) invalidates the previous resolve's
 * returned pointer the moment the SAME source resolves again -- resolving
 * {bold:false} then {bold:true} on one source would invalidate the first
 * result before both could be handed to tbox_font_face_cache_create. Two
 * independent sources sidestep that entirely, at the cost of one extra
 * FcFontMatch call, which is irrelevant. Both sources are destroyed right
 * after tbox_font_face_cache_create returns -- it already copies both byte
 * blobs defensively (same defense tbox_font_face_load itself already makes
 * for a single face, see <tbox/font.h>), so neither source needs to outlive
 * that call, let alone tbox_app's whole lifetime; tbox_app therefore has no
 * tbox_font_source field of its own.
 *
 * Returns NULL on any failure, cleaning up whatever had already been
 * allocated first; never crashes either way. */
static tbox_app *tbox_app_create_impl(const char *html, const char *css, int32_t width, int32_t height, bool use_config, tbox_ua_style_config config) {
    const void *regular_data = NULL;
    size_t regular_size      = 0;
    tbox_font_source *regular_source = tbox_app_resolve_font_source(false, &regular_data, &regular_size);
    if (regular_source == NULL) {
        return NULL;
    }

    const void *bold_data = NULL;
    size_t bold_size      = 0;
    tbox_font_source *bold_source = tbox_app_resolve_font_source(true, &bold_data, &bold_size);
    if (bold_source == NULL) {
        tbox_font_source_destroy(regular_source);
        return NULL;
    }

    tbox_font_face_cache *fonts = tbox_font_face_cache_create(regular_data, regular_size, bold_data, bold_size);
    /* Both sources' bytes are already copied into `fonts` above (or the
     * call failed and there is nothing left to copy from) -- neither source
     * is needed past this point, success or failure alike. */
    tbox_font_source_destroy(bold_source);
    tbox_font_source_destroy(regular_source);
    if (fonts == NULL) {
        return NULL;
    }

    tbox_context *ctx = use_config
        ? tbox_context_open_with_config(html, strlen(html), css, strlen(css), fonts, config)
        : tbox_context_open(html, strlen(html), css, strlen(css), fonts);
    if (ctx == NULL) {
        tbox_font_face_cache_destroy(fonts);
        return NULL;
    }

    tbox_backend_wayland *backend = tbox_backend_wayland_open(width, height, NULL);
    if (backend == NULL) {
        tbox_context_close(ctx);
        tbox_font_face_cache_destroy(fonts);
        return NULL;
    }

    tbox_app *app = (tbox_app *)malloc(sizeof(tbox_app));
    if (app == NULL) {
        tbox_backend_wayland_destroy(backend);
        tbox_context_close(ctx);
        tbox_font_face_cache_destroy(fonts);
        return NULL;
    }

    app->fonts       = fonts;
    app->ctx         = ctx;
    app->backend     = backend;
    app->last_width  = 0;
    app->last_height = 0;
    app->closed      = false;

    return app;
}

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    tbox_ua_style_config unused_config; /* never read: use_config == false below */
    memset(&unused_config, 0, sizeof(unused_config));
    return tbox_app_create_impl(html, css, width, height, false, unused_config);
}

tbox_app *tbox_app_create_with_config(const char *html, const char *css, int32_t width, int32_t height, tbox_ua_style_config config) {
    return tbox_app_create_impl(html, css, width, height, true, config);
}

tbox_context *tbox_app_context(tbox_app *app) {
    if (app == NULL) {
        return NULL;
    }
    return app->ctx;
}

void tbox_app_step(tbox_app *app) {
    if (app == NULL) {
        return;
    }

    if (!tbox_backend_wayland_poll(app->backend, TBOX_APP_POLL_TIMEOUT_MS)) {
        /* Connection to the compositor is gone -- see the `closed` field's
         * comment above. There is nothing left to dispatch/redraw. */
        app->closed = true;
        return;
    }

    bool dirty = false;

    double click_x = 0.0;
    double click_y = 0.0;
    if (tbox_backend_wayland_take_click(app->backend, &click_x, &click_y)) {
        if (tbox_context_dispatch_click(app->ctx, click_x, click_y)) {
            dirty = true;
        }
    }

    int32_t current_width  = 0;
    int32_t current_height = 0;
    tbox_backend_wayland_size(app->backend, &current_width, &current_height);
    if (current_width != app->last_width || current_height != app->last_height) {
        app->last_width  = current_width;
        app->last_height = current_height;
        dirty            = true;
    }

    if (dirty) {
        tbox_display_list list;
        tbox_context_run_frame(app->ctx, (double)app->last_width, (double)app->last_height, &list);
        tbox_backend_wayland_present(app->backend, &list);
    }
}

bool tbox_app_should_close(const tbox_app *app) {
    if (app == NULL) {
        return true;
    }
    return app->closed || tbox_backend_wayland_should_close(app->backend);
}

void tbox_app_close(tbox_app *app) {
    if (app == NULL) {
        return;
    }

    tbox_backend_wayland_destroy(app->backend);
    tbox_context_close(app->ctx);
    tbox_font_face_cache_destroy(app->fonts);
    free(app);
}
