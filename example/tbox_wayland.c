#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <tbox/tbox.h>

#include "xdg-shell-client-protocol.h"

/* Linux-only Wayland client: opens a fixed-size window via xdg-shell, fills
 * it with a checkerboard (colored via tbox_css_named_color_find, to prove
 * tbox links correctly), and closes on either ESC or the compositor's own
 * close request. No X11, no Windows.
 *
 * This is meant to be reused/extended a lot (font rendering is next), so it
 * keeps a couple of debugging aids permanently, built on tbox's own
 * <tbox/debug.h> (see there for details) and both opt-in via env vars read
 * once at startup:
 *   TBOX_WAYLAND_DEBUG=1           -- verbose stderr logging (prefixed
 *                                      "[tbox_wayland]") of every Wayland/
 *                                      xkbcommon lifecycle event this demo
 *                                      handles: globals bound, configure
 *                                      events and the size they settle on,
 *                                      key presses, close requests, etc.
 *   TBOX_WAYLAND_CLOSE_DELAY_MS=N  -- automatically closes the window N
 *                                      milliseconds after startup, instead
 *                                      of waiting for ESC/compositor close.
 *                                      Meant for scripted smoke-testing
 *                                      (e.g. in CI, or a quick manual check)
 *                                      without needing an external
 *                                      `timeout ... | pkill ...`. */

#define TBOX_WAYLAND_WIDTH 640
#define TBOX_WAYLAND_HEIGHT 480
#define TBOX_WAYLAND_CHECKER_SIZE 32

/* Bundles every Wayland/xkbcommon/FreeType resource this demo owns, so one
 * cleanup() can tear all of it down uniformly from both the success path
 * and every early-error return. */
typedef struct tbox_wayland_app {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    int shm_fd;
    void *shm_data;
    size_t shm_size;
    struct wl_buffer *buffer;

    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;

    FT_Library ft_library;
    bool ft_initialized;

    int32_t width;
    int32_t height;
    int32_t pending_width; /* from the latest xdg_toplevel::configure, applied on the next ack */
    int32_t pending_height;

    bool configured;
    bool should_close;
} tbox_wayland_app;

static void draw_and_attach(tbox_wayland_app *app);

/* --- xdg_wm_base: must pong every ping or the compositor may consider the
 * client unresponsive and kill it. --- */
static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    tbox_log("xdg_wm_base ping (serial=%u), ponging", serial);
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

/* --- wl_keyboard: only what's needed to detect ESC. --- */
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    (void)keyboard;
    tbox_wayland_app *app = data;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    void *map_source = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_source == MAP_FAILED) {
        fprintf(stderr, "failed to mmap keyboard keymap\n");
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(app->xkb_context, map_source, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_source, size);
    if (keymap == NULL) {
        fprintf(stderr, "failed to compile keyboard keymap\n");
        return;
    }

    if (app->xkb_state != NULL) {
        xkb_state_unref(app->xkb_state);
    }
    if (app->xkb_keymap != NULL) {
        xkb_keymap_unref(app->xkb_keymap);
    }
    app->xkb_keymap = keymap;
    app->xkb_state  = xkb_state_new(keymap);
    tbox_log("keyboard keymap compiled (%u bytes)", size);
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard;
    (void)serial;
    (void)time;
    tbox_wayland_app *app = data;

    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || app->xkb_state == NULL) {
        return;
    }

    xkb_keycode_t keycode = (xkb_keycode_t)(key + 8); /* evdev-to-xkb keycode offset */
    xkb_keysym_t sym      = xkb_state_key_get_one_sym(app->xkb_state, keycode);

    if (tbox_log_is_enabled()) {
        char sym_name[64];
        xkb_keysym_get_name(sym, sym_name, sizeof(sym_name));
        tbox_log("key pressed: keycode=%u sym=%s", key, sym_name);
    }

    if (sym == XKB_KEY_Escape) {
        tbox_log("ESC pressed, closing");
        app->should_close = true;
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void)keyboard;
    (void)serial;
    tbox_wayland_app *app = data;
    if (app->xkb_state == NULL) {
        return;
    }
    xkb_state_update_mask(app->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

/* enter/leave/repeat_info carry nothing this demo needs, but libwayland
 * aborts the process if it dispatches an event whose listener slot is NULL
 * for the version we bound the keyboard at -- every opcode up to that
 * version needs a real (even if no-op) handler. */
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    tbox_log("keyboard focus gained");
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    tbox_log("keyboard focus lost");
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = keyboard_keymap,
    .enter       = keyboard_enter,
    .leave       = keyboard_leave,
    .key         = keyboard_key,
    .modifiers   = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

/* --- wl_seat: bind the keyboard once we know it's available. --- */
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    tbox_wayland_app *app = data;
    tbox_log("seat capabilities: 0x%x%s", capabilities, (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) ? " (keyboard)" : "");

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && app->keyboard == NULL) {
        app->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app->keyboard, &keyboard_listener, app);
    }
}

/* Sent since wl_seat version 2 -- required whenever we bind above version 1
 * (see the keyboard listener comment above for why this can't be NULL). */
static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    tbox_log("seat name: %s", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name         = seat_name,
};

/* --- xdg_surface: the configure/ack_configure/commit handshake required
 * before any attached buffer is actually displayed. --- */
static void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    tbox_wayland_app *app = data;

    xdg_surface_ack_configure(xdg_surface, serial);

    /* width/height == 0 means "the compositor has no opinion, pick your own
     * size" (e.g. a plain floating window manager on the very first
     * configure) -- keep whatever app->width/height already holds (the
     * TBOX_WAYLAND_WIDTH/HEIGHT defaults on the first call) in that case.
     * A tiling compositor like Sway instead sends the actual tile size it
     * allocated, which is why this can't just always use a fixed 640x480:
     * the buffer has to match, or it only covers part of the window. */
    int32_t new_width   = app->pending_width > 0 ? app->pending_width : app->width;
    int32_t new_height  = app->pending_height > 0 ? app->pending_height : app->height;
    app->pending_width  = 0;
    app->pending_height = 0;

    bool size_changed = !app->configured || new_width != app->width || new_height != app->height;
    app->width        = new_width;
    app->height       = new_height;
    app->configured   = true;

    tbox_log("xdg_surface configure (serial=%u): %dx%d%s", serial, new_width, new_height, size_changed ? " (size changed)" : "");

    if (size_changed) {
        draw_and_attach(app);
    } else {
        wl_surface_commit(app->surface);
    }
}

static const struct xdg_surface_listener surface_listener = {
    .configure = surface_configure,
};

/* --- xdg_toplevel: the compositor's own close request (titlebar "x", or
 * equivalent gesture). --- */
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    (void)toplevel;
    (void)states;
    tbox_wayland_app *app = data;
    tbox_log("xdg_toplevel configure: suggested %dx%d", width, height);

    /* Only records the suggestion -- applied together with every other
     * pending configure state when xdg_surface::configure is ack'd, per the
     * protocol's batching rules. */
    if (width > 0) {
        app->pending_width = width;
    }
    if (height > 0) {
        app->pending_height = height;
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    tbox_wayland_app *app = data;
    tbox_log("xdg_toplevel close requested by compositor");
    app->should_close = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close     = toplevel_close,
};

/* --- wl_registry: bind every global this demo needs. --- */
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    tbox_wayland_app *app = data;
    tbox_log("registry global: %s v%u (name=%u)", interface, version, name);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global        = registry_global,
    .global_remove = registry_global_remove,
};

/* Looks up a CSS named color via tbox and packs it into a WL_SHM_FORMAT_XRGB8888
 * pixel (native-endian 0xXXRRGGBB), scaling each channel by `scale` (used to
 * derive a second, darker tone for the checkerboard). */
static uint32_t tbox_wayland_pixel_from_named_color(const char *name, double scale) {
    tbox_css_rgba color   = { 0, 0, 0, 255 };
    tbox_string_view view = tbox_string_view_make(name, strlen(name));
    if (!tbox_css_named_color_find(view, &color)) {
        fprintf(stderr, "failed to look up named color \"%s\"\n", name);
    }

    unsigned int r = (unsigned int)(color.r * scale);
    unsigned int g = (unsigned int)(color.g * scale);
    unsigned int b = (unsigned int)(color.b * scale);

    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

/* Allocates a wl_shm-backed buffer sized app->width x app->height, fills it
 * with a checkerboard pattern, and attaches/commits it. Called once after
 * the first xdg_surface::configure, and again every time it reports a new
 * size, so the buffer always covers the whole window instead of leaving
 * whatever area a tiling compositor allocated beyond the original fixed
 * size unpainted. */
static void draw_and_attach(tbox_wayland_app *app) {
    const int32_t width    = app->width;
    const int32_t height   = app->height;
    const int32_t stride   = width * 4;
    const size_t pool_size = (size_t)stride * (size_t)height;

    tbox_log("draw_and_attach: allocating %dx%d buffer (%zu bytes)", width, height, pool_size);

    int fd = memfd_create("tbox_wayland-shm", MFD_CLOEXEC);
    if (fd == -1) {
        fprintf(stderr, "failed to create shm backing file\n");
        return;
    }
    if (ftruncate(fd, (off_t)pool_size) == -1) {
        fprintf(stderr, "failed to size shm backing file\n");
        close(fd);
        return;
    }

    void *data = mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "failed to mmap shm backing file\n");
        close(fd);
        return;
    }

    uint32_t light = tbox_wayland_pixel_from_named_color("CornflowerBlue", 1.0);
    uint32_t dark  = tbox_wayland_pixel_from_named_color("CornflowerBlue", 0.5);

    uint32_t *pixels = data;
    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < width; x++) {
            bool light_square                             = ((x / TBOX_WAYLAND_CHECKER_SIZE) + (y / TBOX_WAYLAND_CHECKER_SIZE)) % 2 == 0;
            pixels[(size_t)y * (size_t)width + (size_t)x] = light_square ? light : dark;
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, (int32_t)pool_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    /* Replace whatever buffer/mapping backed the previous size, if any --
     * the underlying shared memory the compositor is (or was) reading stays
     * valid as long as it holds its own fd/mapping, so it's safe to drop
     * ours as soon as the new one is ready. */
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->shm_data != NULL) {
        munmap(app->shm_data, app->shm_size);
    }
    if (app->shm_fd != -1) {
        close(app->shm_fd);
    }

    app->shm_fd   = fd;
    app->shm_data = data;
    app->shm_size = pool_size;
    app->buffer   = buffer;

    wl_surface_attach(app->surface, buffer, 0, 0);
    wl_surface_damage_buffer(app->surface, 0, 0, width, height);
    wl_surface_commit(app->surface);
}

static void cleanup(tbox_wayland_app *app) {
    if (app->keyboard != NULL) {
        wl_keyboard_destroy(app->keyboard);
    }
    if (app->seat != NULL) {
        wl_seat_destroy(app->seat);
    }
    if (app->shm_data != NULL) {
        munmap(app->shm_data, app->shm_size);
    }
    if (app->shm_fd != -1) {
        close(app->shm_fd);
    }
    if (app->buffer != NULL) {
        wl_buffer_destroy(app->buffer);
    }
    if (app->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(app->xdg_toplevel);
    }
    if (app->xdg_surface != NULL) {
        xdg_surface_destroy(app->xdg_surface);
    }
    if (app->surface != NULL) {
        wl_surface_destroy(app->surface);
    }
    if (app->wm_base != NULL) {
        xdg_wm_base_destroy(app->wm_base);
    }
    if (app->shm != NULL) {
        wl_shm_destroy(app->shm);
    }
    if (app->compositor != NULL) {
        wl_compositor_destroy(app->compositor);
    }
    if (app->xkb_state != NULL) {
        xkb_state_unref(app->xkb_state);
    }
    if (app->xkb_keymap != NULL) {
        xkb_keymap_unref(app->xkb_keymap);
    }
    if (app->xkb_context != NULL) {
        xkb_context_unref(app->xkb_context);
    }
    if (app->ft_initialized) {
        FT_Done_FreeType(app->ft_library);
    }
    if (app->registry != NULL) {
        wl_registry_destroy(app->registry);
    }
    if (app->display != NULL) {
        wl_display_disconnect(app->display);
    }
}

/* Milliseconds elapsed since `start` (CLOCK_MONOTONIC). */
static long tbox_wayland_elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long seconds_diff = (long)(now.tv_sec - start->tv_sec);
    long nanos_diff   = now.tv_nsec - start->tv_nsec;
    return seconds_diff * 1000L + nanos_diff / 1000000L;
}

/* Runs the event loop until app->should_close, or -- when close_delay_ms > 0
 * -- until that many milliseconds have elapsed since this call started.
 * Uses the prepare_read/poll/read_events pattern (rather than the simpler
 * blocking wl_display_dispatch) specifically so a timeout can be layered on
 * top of it: see the wl_display_prepare_read(3) documentation for why this
 * three-step dance (as opposed to reading directly) is the correct way to
 * combine libwayland with an external poll() loop. */
static void run_event_loop(tbox_wayland_app *app, long close_delay_ms) {
    int display_fd = wl_display_get_fd(app->display);

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    if (close_delay_ms > 0) {
        tbox_log("auto-close armed: closing after %ld ms", close_delay_ms);
    }

    while (!app->should_close) {
        while (wl_display_prepare_read(app->display) != 0) {
            if (wl_display_dispatch_pending(app->display) == -1) {
                fprintf(stderr, "connection to Wayland compositor lost\n");
                return;
            }
        }

        if (wl_display_flush(app->display) == -1 && errno != EAGAIN) {
            fprintf(stderr, "failed to flush Wayland display\n");
            wl_display_cancel_read(app->display);
            return;
        }

        int timeout_ms = -1;
        if (close_delay_ms > 0) {
            long remaining_ms = close_delay_ms - tbox_wayland_elapsed_ms(&start_time);
            if (remaining_ms <= 0) {
                wl_display_cancel_read(app->display);
                tbox_log("auto-close delay elapsed, closing");
                app->should_close = true;
                return;
            }
            timeout_ms = (int)remaining_ms;
        }

        struct pollfd pfd = { .fd = display_fd, .events = POLLIN };
        int ready         = poll(&pfd, 1, timeout_ms);
        if (ready < 0) {
            wl_display_cancel_read(app->display);
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "poll on Wayland display failed\n");
            return;
        } else if (ready == 0) {
            /* Timed out (only possible when close_delay_ms > 0) -- loop
             * around, where the elapsed-time check above will fire. */
            wl_display_cancel_read(app->display);
            continue;
        } else if (pfd.revents & POLLIN) {
            if (wl_display_read_events(app->display) == -1) {
                fprintf(stderr, "failed to read Wayland events\n");
                return;
            }
        } else {
            wl_display_cancel_read(app->display);
        }

        if (wl_display_dispatch_pending(app->display) == -1) {
            fprintf(stderr, "connection to Wayland compositor lost\n");
            return;
        }
    }
}

int main(void) {
    tbox_log_init("tbox_wayland", tbox_env_bool("TBOX_WAYLAND_DEBUG"));
    long close_delay_ms = tbox_env_long("TBOX_WAYLAND_CLOSE_DELAY_MS", 0);

    tbox_wayland_app app = {
        .shm_fd = -1,
        .width  = TBOX_WAYLAND_WIDTH,
        .height = TBOX_WAYLAND_HEIGHT,
    };

    /* Smoke-tests linking against the static FreeType built by this
     * example's CMakeLists.txt -- no font is loaded yet. */
    if (FT_Init_FreeType(&app.ft_library) != 0) {
        fprintf(stderr, "failed to initialize FreeType\n");
        return 1;
    }
    app.ft_initialized = true;
    tbox_log("FreeType initialized");

    app.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (app.xkb_context == NULL) {
        fprintf(stderr, "failed to create xkbcommon context\n");
        cleanup(&app);
        return 1;
    }

    app.display = wl_display_connect(NULL);
    if (app.display == NULL) {
        fprintf(stderr, "failed to connect to Wayland display\n");
        cleanup(&app);
        return 1;
    }
    tbox_log("connected to Wayland display");

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display);

    if (app.compositor == NULL || app.shm == NULL || app.wm_base == NULL) {
        fprintf(stderr, "compositor does not support the required Wayland globals\n");
        cleanup(&app);
        return 1;
    }

    app.surface     = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &toplevel_listener, &app);
    xdg_toplevel_set_title(app.xdg_toplevel, "tbox_wayland");
    xdg_toplevel_set_app_id(app.xdg_toplevel, "tbox_wayland");
    wl_surface_commit(app.surface);

    run_event_loop(&app, close_delay_ms);

    tbox_log("shutting down");
    cleanup(&app);
    return 0;
}
