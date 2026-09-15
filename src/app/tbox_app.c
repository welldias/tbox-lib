#include <tbox/app.h>

#include <stdlib.h>
#include <string.h>

#include <tbox/context.h>
#include <tbox/font.h>
#include <tbox/output.h>
#include <tbox/string_view.h>

/* v0/v1's one and only font: a generic sans-serif, neither bold nor italic,
 * at the CSS2.1 initial font-size of 16px -- see ARCHITECTURE.md's "Fonte /
 * Texto" section. */
#define TBOX_APP_FONT_SIZE_PX 16.0

/* Non-blocking, per ARCHITECTURE.md's "loop não-bloqueante": each
 * tbox_app_step pumps at most this many milliseconds of Wayland events
 * before moving on to click/resize handling and returning -- see
 * tbox_backend_wayland_poll's doc comment ("0 never blocks"). tbox_app_step
 * is meant to be called every tick of a caller-driven loop (e.g. alongside a
 * future script engine's own task queue -- see ARCHITECTURE.md's "Fora de
 * escopo" about vsync), not to wait for something to happen. */
#define TBOX_APP_POLL_TIMEOUT_MS 0

struct tbox_app {
    tbox_font_source *font_source;
    tbox_font_face *font;
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

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    tbox_font_source *font_source = tbox_font_source_fontconfig_create();
    if (font_source == NULL) {
        return NULL;
    }

    tbox_font_query query = {
        .family = tbox_string_view_make("sans-serif", strlen("sans-serif")),
        .bold   = false,
        .italic = false,
    };

    const void *font_data = NULL;
    size_t font_size      = 0;
    if (!tbox_font_source_resolve(font_source, query, &font_data, &font_size)) {
        tbox_font_source_destroy(font_source);
        return NULL;
    }

    tbox_font_face *font = tbox_font_face_load(font_data, font_size, TBOX_APP_FONT_SIZE_PX);
    if (font == NULL) {
        tbox_font_source_destroy(font_source);
        return NULL;
    }

    /* NOTE: same lifetime requirement tbox_app_open documented --
     * FT_New_Memory_Face keeps a pointer into font_source's bytes rather
     * than copying them, so font_source must outlive font, not just this
     * call. Both are destroyed together in tbox_app_close/on failure here. */
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), font);
    if (ctx == NULL) {
        tbox_font_face_destroy(font);
        tbox_font_source_destroy(font_source);
        return NULL;
    }

    tbox_backend_wayland *backend = tbox_backend_wayland_open(width, height, NULL);
    if (backend == NULL) {
        tbox_context_close(ctx);
        tbox_font_face_destroy(font);
        tbox_font_source_destroy(font_source);
        return NULL;
    }

    tbox_app *app = (tbox_app *)malloc(sizeof(tbox_app));
    if (app == NULL) {
        tbox_backend_wayland_destroy(backend);
        tbox_context_close(ctx);
        tbox_font_face_destroy(font);
        tbox_font_source_destroy(font_source);
        return NULL;
    }

    app->font_source  = font_source;
    app->font         = font;
    app->ctx          = ctx;
    app->backend      = backend;
    app->last_width   = 0;
    app->last_height  = 0;
    app->closed       = false;

    return app;
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
    tbox_font_face_destroy(app->font);
    tbox_font_source_destroy(app->font_source);
    free(app);
}
