#define _GNU_SOURCE

#include <tbox/output.h>

#include "tbox_backend_wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include "xdg-shell-client-protocol.h"
#include "output/tbox_key_repeat.h"
#include "output/tbox_pointer_click.h"

/* Linux/Wayland window backend, evolved from example/tbox_wayland.c's
 * tbox_wayland_app + main()/run_event_loop(): same registry/compositor/shm/
 * seat/keyboard/xdg_wm_base/xdg_surface/xdg_toplevel setup and the same
 * prepare_read/poll/read_events event-loop pattern, restructured behind the
 * opaque tbox_backend_wayland API declared in <tbox/output.h> instead of a
 * monolithic main(). draw_and_attach's hardcoded checkerboard is replaced by
 * tbox_raster_display_list (src/output/tbox_raster.c) painting a real
 * tbox_display_list.
 *
 * Unlike the rest of src/, this file intentionally never calls into
 * <tbox/debug.h> (tbox_log/tbox_env_bool/tbox_env_long) -- that header's own
 * doc comment says "the library itself never calls any of these" (it's meant
 * for example programs and other command-line tools built on tbox), and this
 * is now library code, not an example, even though it started life as one.
 * Failures are reported the same way the rest of libtbox reports them: a
 * NULL/false return, nothing printed. */

typedef struct tbox_clipboard_source {
    struct tbox_backend_wayland *backend;
    struct wl_data_source *proxy;
    char *text;
    size_t length;
    struct tbox_clipboard_source *next;
} tbox_clipboard_source;

typedef struct tbox_clipboard_offer {
    struct wl_data_offer *proxy;
    char *mime_utf8, *mime_plain;
    struct tbox_clipboard_offer *next;
} tbox_clipboard_offer;

typedef struct tbox_clipboard_write {
    int fd;
    char *text;
    size_t length, offset;
    struct tbox_clipboard_write *next;
} tbox_clipboard_write;

struct tbox_backend_wayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wl_data_device_manager *data_manager;
    struct wl_data_device *data_device;
    tbox_clipboard_source *sources, *own_source;
    tbox_clipboard_offer *offers, *selected_offer;
    tbox_clipboard_write *writes;
    int paste_fd;
    char *paste_buffer;
    size_t paste_length, paste_capacity;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    int shm_fd;
    void *shm_data;
    size_t shm_size;
    struct wl_buffer *buffer;
    int32_t buffer_width; /* size the current shm buffer/pool was allocated at */
    int32_t buffer_height;

    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;

    int32_t repeat_rate;  /* characters per second, from wl_keyboard.repeat_info */
    int32_t repeat_delay; /* milliseconds until the first repeat */
    bool repeat_active;
    xkb_keycode_t repeat_keycode;
    uint64_t repeat_next_ns;
    tbox_input_event repeat_key_event;
    tbox_input_event repeat_text_event;
    bool repeat_has_key;
    bool repeat_has_text;
    bool repeat_composed_text;

    int32_t width;
    int32_t height;
    int32_t pending_width; /* from the latest xdg_toplevel::configure, applied on the next ack */
    int32_t pending_height;

    bool configured;
    bool should_close;

    /* Events are queued in compositor delivery order. Pointer position is
     * tracked separately for hover and click coordinates. */
    bool pointer_has_focus;
    double pointer_x;
    double pointer_y;
    bool pointer_pressed;
    uint32_t last_click_time;
    double last_click_x, last_click_y;
    tbox_input_event *events;
    size_t event_count;
    size_t event_capacity;
};

static bool tbox_backend_wayland_push_event(tbox_backend_wayland *backend, tbox_input_event event) {
    if (backend->event_count == backend->event_capacity) {
        size_t capacity = backend->event_capacity == 0 ? 8 : backend->event_capacity * 2;
        tbox_input_event *events = realloc(backend->events, capacity * sizeof(*events));
        if (events == NULL) {
            return false;
        }
        backend->events = events;
        backend->event_capacity = capacity;
    }
    backend->events[backend->event_count++] = event;
    return true;
}

/* A clipboard receiver can close its pipe at any time. Block SIGPIPE around
 * this write so a cancelled paste cannot terminate the application. */
static ssize_t tbox_backend_wayland_pipe_write(int fd, const char *data, size_t length) {
    sigset_t mask, old_mask, pending;
    sigemptyset(&mask);
    sigaddset(&mask, SIGPIPE);
    sigpending(&pending);
    bool already_pending = sigismember(&pending, SIGPIPE) == 1;
    if (sigprocmask(SIG_BLOCK, &mask, &old_mask) != 0) return -1;
    ssize_t result = write(fd, data, length);
    int saved_errno = errno;
    if (result < 0 && saved_errno == EPIPE && !already_pending) {
        struct timespec zero = {0};
        sigtimedwait(&mask, NULL, &zero);
    }
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
    errno = saved_errno;
    return result;
}

static uint64_t tbox_backend_wayland_now_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void tbox_backend_wayland_repeat_tick(tbox_backend_wayland *backend) {
    if (!backend->repeat_active || backend->repeat_rate <= 0) return;
    uint64_t now = tbox_backend_wayland_now_ns();
    if (now == 0) return;
    /* Catch up briefly after a slow frame, without flooding the event queue
     * if the application was paused for a long time. */
    int due = tbox_key_repeat_due(now, &backend->repeat_next_ns, backend->repeat_rate);
    for (int i = 0; i < due; i++) {
        if (backend->repeat_has_key) tbox_backend_wayland_push_event(backend, backend->repeat_key_event);
        if (backend->repeat_has_text) tbox_backend_wayland_push_event(backend, backend->repeat_text_event);
    }
}

#define TBOX_CLIPBOARD_MAX_BYTES (16u * 1024u * 1024u)
#define TBOX_CLIPBOARD_MIME_UTF8 "text/plain;charset=utf-8"
#define TBOX_CLIPBOARD_MIME_PLAIN "text/plain"

static void tbox_backend_wayland_clipboard_source_target(void *data, struct wl_data_source *proxy, const char *mime) {
    (void)data; (void)proxy; (void)mime;
}

static void tbox_backend_wayland_clipboard_source_send(void *data, struct wl_data_source *proxy,
                                                       const char *mime, int32_t fd) {
    (void)proxy;
    tbox_clipboard_source *source = data;
    if (mime == NULL ||
        (strcasecmp(mime, TBOX_CLIPBOARD_MIME_UTF8) != 0 && strcasecmp(mime, TBOX_CLIPBOARD_MIME_PLAIN) != 0)) {
        close(fd);
        return;
    }
    tbox_clipboard_write *write = calloc(1, sizeof(*write));
    if (write == NULL) { close(fd); return; }
    write->text = malloc(source->length == 0 ? 1 : source->length);
    if (write->text == NULL) { free(write); close(fd); return; }
    if (source->length > 0) memcpy(write->text, source->text, source->length);
    write->fd = fd;
    write->length = source->length;
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        free(write->text);
        free(write);
        close(fd);
        return;
    }
    write->next = source->backend->writes;
    source->backend->writes = write;
}

static void tbox_backend_wayland_clipboard_source_cancelled(void *data, struct wl_data_source *proxy) {
    tbox_clipboard_source *source = data;
    tbox_backend_wayland *backend = source->backend;
    tbox_clipboard_source **slot = &backend->sources;
    while (*slot != NULL && *slot != source) slot = &(*slot)->next;
    if (*slot == source) *slot = source->next;
    if (backend->own_source == source) backend->own_source = NULL;
    wl_data_source_destroy(proxy);
    free(source->text);
    free(source);
}

static void tbox_backend_wayland_clipboard_source_dnd_done(void *data, struct wl_data_source *proxy) {
    (void)data; (void)proxy;
}
static void tbox_backend_wayland_clipboard_source_action(void *data, struct wl_data_source *proxy, uint32_t action) {
    (void)data; (void)proxy; (void)action;
}

static const struct wl_data_source_listener tbox_backend_wayland_clipboard_source_listener = {
    .target = tbox_backend_wayland_clipboard_source_target,
    .send = tbox_backend_wayland_clipboard_source_send,
    .cancelled = tbox_backend_wayland_clipboard_source_cancelled,
    .dnd_drop_performed = tbox_backend_wayland_clipboard_source_dnd_done,
    .dnd_finished = tbox_backend_wayland_clipboard_source_dnd_done,
    .action = tbox_backend_wayland_clipboard_source_action,
};

static void tbox_backend_wayland_clipboard_offer_mime(void *data, struct wl_data_offer *proxy, const char *mime) {
    (void)proxy;
    tbox_clipboard_offer *offer = data;
    if (strcasecmp(mime, TBOX_CLIPBOARD_MIME_UTF8) == 0 && offer->mime_utf8 == NULL)
        offer->mime_utf8 = strdup(mime);
    if (strcasecmp(mime, TBOX_CLIPBOARD_MIME_PLAIN) == 0 && offer->mime_plain == NULL)
        offer->mime_plain = strdup(mime);
}

static void tbox_backend_wayland_clipboard_offer_actions(void *data, struct wl_data_offer *proxy, uint32_t actions) {
    (void)data; (void)proxy; (void)actions;
}

static const struct wl_data_offer_listener tbox_backend_wayland_clipboard_offer_listener = {
    .offer = tbox_backend_wayland_clipboard_offer_mime,
    .source_actions = tbox_backend_wayland_clipboard_offer_actions,
    .action = tbox_backend_wayland_clipboard_offer_actions,
};

static void tbox_backend_wayland_clipboard_data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *proxy) {
    (void)device;
    tbox_backend_wayland *backend = data;
    tbox_clipboard_offer *offer = calloc(1, sizeof(*offer));
    if (offer == NULL) { wl_data_offer_destroy(proxy); return; }
    offer->proxy = proxy;
    offer->next = backend->offers;
    backend->offers = offer;
    wl_data_offer_add_listener(proxy, &tbox_backend_wayland_clipboard_offer_listener, offer);
}

static void tbox_backend_wayland_clipboard_selection(void *data, struct wl_data_device *device, struct wl_data_offer *proxy) {
    (void)device;
    tbox_backend_wayland *backend = data;
    tbox_clipboard_offer *selected = NULL;
    for (tbox_clipboard_offer *offer = backend->offers; offer != NULL; offer = offer->next)
        if (offer->proxy == proxy) selected = offer;
    backend->selected_offer = selected;
    tbox_clipboard_offer **slot = &backend->offers;
    while (*slot != NULL) {
        tbox_clipboard_offer *offer = *slot;
        if (offer == selected) { slot = &offer->next; continue; }
        *slot = offer->next;
        wl_data_offer_destroy(offer->proxy);
        free(offer->mime_utf8);
        free(offer->mime_plain);
        free(offer);
    }
}

static void tbox_backend_wayland_clipboard_drag_enter(void *data, struct wl_data_device *device,
    uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer) {
    (void)data; (void)device; (void)serial; (void)surface; (void)x; (void)y; (void)offer;
}
static void tbox_backend_wayland_clipboard_drag_leave(void *data, struct wl_data_device *device) {
    (void)data; (void)device;
}
static void tbox_backend_wayland_clipboard_drag_motion(void *data, struct wl_data_device *device,
    uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)data; (void)device; (void)time; (void)x; (void)y;
}
static void tbox_backend_wayland_clipboard_drag_drop(void *data, struct wl_data_device *device) {
    (void)data; (void)device;
}

static const struct wl_data_device_listener tbox_backend_wayland_clipboard_device_listener = {
    .data_offer = tbox_backend_wayland_clipboard_data_offer,
    .enter = tbox_backend_wayland_clipboard_drag_enter,
    .leave = tbox_backend_wayland_clipboard_drag_leave,
    .motion = tbox_backend_wayland_clipboard_drag_motion,
    .drop = tbox_backend_wayland_clipboard_drag_drop,
    .selection = tbox_backend_wayland_clipboard_selection,
};

bool tbox_backend_wayland_clipboard_copy(tbox_backend_wayland *backend, const char *text, size_t length, uint32_t serial) {
    if (backend == NULL || backend->data_manager == NULL || backend->data_device == NULL ||
        text == NULL || length == 0 || serial == 0) return false;
    tbox_clipboard_source *source = calloc(1, sizeof(*source));
    if (source == NULL) return false;
    source->text = malloc(length);
    if (source->text == NULL) { free(source); return false; }
    memcpy(source->text, text, length);
    source->length = length;
    source->backend = backend;
    source->proxy = wl_data_device_manager_create_data_source(backend->data_manager);
    if (source->proxy == NULL) { free(source->text); free(source); return false; }
    wl_data_source_add_listener(source->proxy, &tbox_backend_wayland_clipboard_source_listener, source);
    wl_data_source_offer(source->proxy, TBOX_CLIPBOARD_MIME_UTF8);
    wl_data_source_offer(source->proxy, TBOX_CLIPBOARD_MIME_PLAIN);
    source->next = backend->sources;
    backend->sources = source;
    backend->own_source = source;
    wl_data_device_set_selection(backend->data_device, source->proxy, serial);
    return true;
}

bool tbox_backend_wayland_clipboard_paste(tbox_backend_wayland *backend) {
    if (backend == NULL || backend->data_device == NULL) return false;
    size_t kept = 0;
    for (size_t i = 0; i < backend->event_count; i++) {
        if (backend->events[i].kind == TBOX_INPUT_PASTE)
            free(backend->events[i].data.paste.utf8);
        else backend->events[kept++] = backend->events[i];
    }
    backend->event_count = kept;
    if (backend->paste_fd >= 0) {
        close(backend->paste_fd);
        backend->paste_fd = -1;
        free(backend->paste_buffer);
        backend->paste_buffer = NULL;
        backend->paste_length = backend->paste_capacity = 0;
    }
    if (backend->own_source != NULL) {
        tbox_clipboard_source *source = backend->own_source;
        char *copy = malloc(source->length == 0 ? 1 : source->length);
        if (copy == NULL) return false;
        if (source->length > 0) memcpy(copy, source->text, source->length);
        tbox_input_event event = { .kind = TBOX_INPUT_PASTE,
            .data.paste = { copy, source->length } };
        if (!tbox_backend_wayland_push_event(backend, event)) { free(copy); return false; }
        return true;
    }
    tbox_clipboard_offer *offer = backend->selected_offer;
    if (offer == NULL || (offer->mime_utf8 == NULL && offer->mime_plain == NULL)) return false;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return false;
    int read_flags = fcntl(fds[0], F_GETFL);
    if (read_flags < 0 || fcntl(fds[0], F_SETFL, read_flags | O_NONBLOCK) < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    wl_data_offer_receive(offer->proxy,
        offer->mime_utf8 != NULL ? offer->mime_utf8 : offer->mime_plain, fds[1]);
    close(fds[1]);
    backend->paste_fd = fds[0];
    return true;
}

static void tbox_backend_wayland_clipboard_pump(tbox_backend_wayland *backend) {
    tbox_clipboard_write **slot = &backend->writes;
    while (*slot != NULL) {
        tbox_clipboard_write *pending = *slot;
        bool done = pending->offset == pending->length;
        while (!done) {
            size_t amount = pending->length - pending->offset;
            if (amount > 65536) amount = 65536;
            ssize_t sent = tbox_backend_wayland_pipe_write(pending->fd,
                pending->text + pending->offset, amount);
            if (sent > 0) { pending->offset += (size_t)sent; done = pending->offset == pending->length; continue; }
            if (sent < 0 && errno == EINTR) continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            done = true;
        }
        if (done) {
            close(pending->fd);
            *slot = pending->next;
            free(pending->text);
            free(pending);
        } else slot = &pending->next;
    }
    if (backend->paste_fd < 0) return;
    char chunk[4096];
    for (;;) {
        ssize_t count = read(backend->paste_fd, chunk, sizeof(chunk));
        if (count > 0) {
            size_t needed = backend->paste_length + (size_t)count;
            if (needed > TBOX_CLIPBOARD_MAX_BYTES) break;
            if (needed > backend->paste_capacity) {
                size_t capacity = backend->paste_capacity == 0 ? 4096 : backend->paste_capacity;
                while (capacity < needed) capacity *= 2;
                char *buffer = realloc(backend->paste_buffer, capacity);
                if (buffer == NULL) break;
                backend->paste_buffer = buffer;
                backend->paste_capacity = capacity;
            }
            memcpy(backend->paste_buffer + backend->paste_length, chunk, (size_t)count);
            backend->paste_length = needed;
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (count == 0 && backend->paste_length > 0) {
            tbox_input_event event = { .kind = TBOX_INPUT_PASTE,
                .data.paste = {backend->paste_buffer, backend->paste_length} };
            if (tbox_backend_wayland_push_event(backend, event)) backend->paste_buffer = NULL;
        }
        break;
    }
    close(backend->paste_fd);
    backend->paste_fd = -1;
    free(backend->paste_buffer);
    backend->paste_buffer = NULL;
    backend->paste_length = backend->paste_capacity = 0;
}

/* --- xdg_wm_base: must pong every ping or the compositor may consider the
 * client unresponsive and kill it. --- */
static void tbox_backend_wayland_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener tbox_backend_wayland_wm_base_listener = {
    .ping = tbox_backend_wayland_wm_base_ping,
};

/* --- wl_keyboard: map native key symbols into tbox's logical keys. --- */
static void tbox_backend_wayland_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    (void)keyboard;
    tbox_backend_wayland *backend = data;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    void *map_source = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map_source == MAP_FAILED) {
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_string(backend->xkb_context, map_source, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_source, size);
    if (keymap == NULL) {
        return;
    }

    if (backend->xkb_state != NULL) {
        xkb_state_unref(backend->xkb_state);
    }
    if (backend->xkb_keymap != NULL) {
        xkb_keymap_unref(backend->xkb_keymap);
    }
    backend->xkb_keymap = keymap;
    backend->xkb_state  = xkb_state_new(keymap);
    backend->repeat_active = false;
}

static void tbox_backend_wayland_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard;
    (void)time;
    tbox_backend_wayland *backend = data;

    if (backend->xkb_state == NULL) {
        return;
    }

    xkb_keycode_t keycode = (xkb_keycode_t)(key + 8); /* evdev-to-xkb keycode offset */
    xkb_keysym_t sym      = xkb_state_key_get_one_sym(backend->xkb_state, keycode);
    bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    bool control = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool alt = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool key_repeats = xkb_keymap_key_repeats(backend->xkb_keymap, keycode) != 0;
    if (!pressed && backend->repeat_active && backend->repeat_keycode == keycode)
        backend->repeat_active = false;

    tbox_key logical_key = TBOX_KEY_UNKNOWN;
    switch (sym) {
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: logical_key = TBOX_KEY_TAB; break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: logical_key = TBOX_KEY_ENTER; break;
    case XKB_KEY_space: logical_key = TBOX_KEY_SPACE; break;
    case XKB_KEY_Escape: logical_key = TBOX_KEY_ESCAPE; break;
    case XKB_KEY_BackSpace: logical_key = TBOX_KEY_BACKSPACE; break;
    case XKB_KEY_Delete: logical_key = TBOX_KEY_DELETE; break;
    case XKB_KEY_Left: logical_key = TBOX_KEY_LEFT; break;
    case XKB_KEY_Right: logical_key = TBOX_KEY_RIGHT; break;
    case XKB_KEY_Up: logical_key = TBOX_KEY_UP; break;
    case XKB_KEY_Down: logical_key = TBOX_KEY_DOWN; break;
    case XKB_KEY_Home: logical_key = TBOX_KEY_HOME; break;
    case XKB_KEY_End: logical_key = TBOX_KEY_END; break;
    case XKB_KEY_a:
    case XKB_KEY_A: logical_key = TBOX_KEY_A; break;
    case XKB_KEY_c:
    case XKB_KEY_C: logical_key = TBOX_KEY_C; break;
    case XKB_KEY_v:
    case XKB_KEY_V: logical_key = TBOX_KEY_V; break;
    case XKB_KEY_x:
    case XKB_KEY_X: logical_key = TBOX_KEY_X; break;
    default: break;
    }
    if (pressed && (key_repeats || logical_key != TBOX_KEY_UNKNOWN)) {
        backend->repeat_active = false;
        backend->repeat_has_key = false;
        backend->repeat_has_text = false;
        backend->repeat_composed_text = false;
    }
    bool shift = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (logical_key != TBOX_KEY_UNKNOWN) {
        tbox_input_event key_event = {
            .kind = TBOX_INPUT_KEY,
            .serial = serial,
            .data.key = {logical_key, pressed, shift || sym == XKB_KEY_ISO_Left_Tab, control},
        };
        tbox_backend_wayland_push_event(backend, key_event);
        if (pressed && logical_key != TBOX_KEY_TAB && logical_key != TBOX_KEY_ENTER &&
            logical_key != TBOX_KEY_ESCAPE && logical_key != TBOX_KEY_SPACE &&
            !(control && logical_key == TBOX_KEY_A)) {
            backend->repeat_key_event = key_event;
            backend->repeat_has_key = true;
        }
    }
    if (pressed) {
        if (control || alt) return;
        tbox_input_event text_event = { .kind = TBOX_INPUT_TEXT };
        int length;
        if (backend->compose_state != NULL &&
            xkb_compose_state_feed(backend->compose_state, sym) == XKB_COMPOSE_FEED_ACCEPTED) {
            enum xkb_compose_status status = xkb_compose_state_get_status(backend->compose_state);
            if (status == XKB_COMPOSE_COMPOSING) return;
            if (status == XKB_COMPOSE_CANCELLED) {
                xkb_compose_state_reset(backend->compose_state);
                return;
            }
            if (status == XKB_COMPOSE_COMPOSED) {
                length = xkb_compose_state_get_utf8(backend->compose_state,
                    text_event.data.text.utf8, sizeof(text_event.data.text.utf8));
                backend->repeat_composed_text = true;
                xkb_compose_state_reset(backend->compose_state);
            } else {
                length = xkb_state_key_get_utf8(backend->xkb_state, keycode,
                    text_event.data.text.utf8, sizeof(text_event.data.text.utf8));
            }
        } else {
            length = xkb_state_key_get_utf8(backend->xkb_state, keycode,
                text_event.data.text.utf8, sizeof(text_event.data.text.utf8));
        }
        if (length > 0 && (size_t)length < sizeof(text_event.data.text.utf8) &&
            (unsigned char)text_event.data.text.utf8[0] >= 0x20 &&
            (unsigned char)text_event.data.text.utf8[0] != 0x7f) {
            text_event.data.text.length = (size_t)length;
            tbox_backend_wayland_push_event(backend, text_event);
            backend->repeat_text_event = text_event;
            backend->repeat_has_text = true;
        }
        if (key_repeats && backend->repeat_rate > 0 &&
            (backend->repeat_has_key || backend->repeat_has_text)) {
            uint64_t now = tbox_backend_wayland_now_ns();
            if (now != 0) {
                backend->repeat_keycode = keycode;
                backend->repeat_next_ns = now + (uint64_t)backend->repeat_delay * 1000000u;
                backend->repeat_active = true;
            }
        }
    }
}

static void tbox_backend_wayland_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    (void)keyboard;
    (void)serial;
    tbox_backend_wayland *backend = data;
    if (backend->xkb_state == NULL) {
        return;
    }
    xkb_state_update_mask(backend->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    if (!backend->repeat_active) return;
    bool control = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
    bool alt = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (control || alt) {
        backend->repeat_active = false;
        return;
    }
    bool shift = xkb_state_mod_name_is_active(backend->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    if (backend->repeat_has_key) backend->repeat_key_event.data.key.shift = shift;
    if (backend->repeat_has_text && !backend->repeat_composed_text) {
        tbox_input_event *event = &backend->repeat_text_event;
        int length = xkb_state_key_get_utf8(backend->xkb_state, backend->repeat_keycode,
            event->data.text.utf8, sizeof(event->data.text.utf8));
        if (length > 0 && (size_t)length < sizeof(event->data.text.utf8))
            event->data.text.length = (size_t)length;
    }
}

/* Keyboard focus boundaries also end any local repeat. */
static void tbox_backend_wayland_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    tbox_backend_wayland *backend = data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    backend->repeat_active = false;
}

static void tbox_backend_wayland_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    tbox_backend_wayland *backend = data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    backend->repeat_active = false;
    if (backend->data_device != NULL)
        tbox_backend_wayland_clipboard_selection(backend, backend->data_device, NULL);
    if (backend->compose_state != NULL) xkb_compose_state_reset(backend->compose_state);
}

static void tbox_backend_wayland_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    tbox_backend_wayland *backend = data;
    (void)keyboard;
    if (rate < 0 || delay < 0) return;
    backend->repeat_rate = rate;
    backend->repeat_delay = delay;
    if (rate == 0) {
        backend->repeat_active = false;
    } else if (backend->repeat_active) {
        uint64_t interval = 1000000000u / (uint64_t)rate;
        if (interval < 1000000u) interval = 1000000u;
        backend->repeat_next_ns = tbox_backend_wayland_now_ns() + interval;
    }
}

static const struct wl_keyboard_listener tbox_backend_wayland_keyboard_listener = {
    .keymap      = tbox_backend_wayland_keyboard_keymap,
    .enter       = tbox_backend_wayland_keyboard_enter,
    .leave       = tbox_backend_wayland_keyboard_leave,
    .key         = tbox_backend_wayland_keyboard_key,
    .modifiers   = tbox_backend_wayland_keyboard_modifiers,
    .repeat_info = tbox_backend_wayland_keyboard_repeat_info,
};

/* --- wl_pointer: enter/leave track which surface (if any) has pointer
 * focus, motion keeps the current surface-local position up to date, and
 * button queues a click on press and motion queues drags while the primary
 * button is held -- consumed in order with keyboard
 * events by tbox_backend_wayland_take_event. Bound at the same wl_seat version as the
 * keyboard (see tbox_backend_wayland_seat_capabilities and the registry
 * bind below), so every event up to version 7 needs a real (even if no-op)
 * handler here, same reasoning as the keyboard listener's comment above;
 * axis_value120 (v8) and axis_relative_direction (v9) are not reachable at
 * that version and are left out. */
static void tbox_backend_wayland_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)serial;
    tbox_backend_wayland *backend = data;

    if (surface == backend->surface) {
        backend->pointer_has_focus = true;
    }
    backend->pointer_x = wl_fixed_to_double(surface_x);
    backend->pointer_y = wl_fixed_to_double(surface_y);
}

static void tbox_backend_wayland_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    tbox_backend_wayland *backend = data;

    if (surface == backend->surface) {
        backend->pointer_has_focus = false;
        if (backend->pointer_pressed) {
            tbox_backend_wayland_push_event(backend, (tbox_input_event){ .kind = TBOX_INPUT_POINTER_RELEASE });
        }
        backend->pointer_pressed = false;
    }
}

static void tbox_backend_wayland_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)time;
    tbox_backend_wayland *backend = data;

    backend->pointer_x = wl_fixed_to_double(surface_x);
    backend->pointer_y = wl_fixed_to_double(surface_y);
    if (backend->pointer_pressed) {
        tbox_backend_wayland_push_event(backend, (tbox_input_event){
            .kind = TBOX_INPUT_POINTER_DRAG,
            .data.drag = {backend->pointer_x, backend->pointer_y},
        });
    }
}

static void tbox_backend_wayland_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    (void)serial;
    tbox_backend_wayland *backend = data;

    if (button != BTN_LEFT) {
        return;
    }
    if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
        backend->pointer_pressed = false;
        tbox_backend_wayland_push_event(backend, (tbox_input_event){ .kind = TBOX_INPUT_POINTER_RELEASE });
        return;
    }
    if (!backend->pointer_has_focus) return;
    backend->pointer_pressed = true;
    bool double_click = tbox_pointer_is_double_click(time, backend->last_click_time,
        backend->pointer_x, backend->pointer_y,
        backend->last_click_x, backend->last_click_y);
    backend->last_click_time = double_click ? 0 : time;
    backend->last_click_x = backend->pointer_x;
    backend->last_click_y = backend->pointer_y;

    tbox_backend_wayland_push_event(backend, (tbox_input_event){
        .kind = TBOX_INPUT_POINTER_CLICK,
        .data.click = {backend->pointer_x, backend->pointer_y, double_click},
    });
}

static void tbox_backend_wayland_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)pointer;
    (void)time;
    tbox_backend_wayland *backend = data;
    if (backend->pointer_has_focus && axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        tbox_backend_wayland_push_event(backend, (tbox_input_event){
            .kind = TBOX_INPUT_POINTER_SCROLL,
            .data.scroll = {backend->pointer_x, backend->pointer_y, wl_fixed_to_double(value)},
        });
    }
}

static void tbox_backend_wayland_pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data;
    (void)pointer;
}

static void tbox_backend_wayland_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
    (void)data;
    (void)pointer;
    (void)axis_source;
}

static void tbox_backend_wayland_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void tbox_backend_wayland_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener tbox_backend_wayland_pointer_listener = {
    .enter         = tbox_backend_wayland_pointer_enter,
    .leave         = tbox_backend_wayland_pointer_leave,
    .motion        = tbox_backend_wayland_pointer_motion,
    .button        = tbox_backend_wayland_pointer_button,
    .axis          = tbox_backend_wayland_pointer_axis,
    .frame         = tbox_backend_wayland_pointer_frame,
    .axis_source   = tbox_backend_wayland_pointer_axis_source,
    .axis_stop     = tbox_backend_wayland_pointer_axis_stop,
    .axis_discrete = tbox_backend_wayland_pointer_axis_discrete,
};

/* --- wl_seat: bind the keyboard/pointer once we know they're available. --- */
static void tbox_backend_wayland_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    tbox_backend_wayland *backend = data;

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && backend->keyboard == NULL) {
        backend->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(backend->keyboard, &tbox_backend_wayland_keyboard_listener, backend);
    }

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && backend->pointer == NULL) {
        backend->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(backend->pointer, &tbox_backend_wayland_pointer_listener, backend);
    }
}

/* Sent since wl_seat version 2 -- required whenever we bind above version 1
 * (see the keyboard listener comment above for why this can't be NULL). */
static void tbox_backend_wayland_seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener tbox_backend_wayland_seat_listener = {
    .capabilities = tbox_backend_wayland_seat_capabilities,
    .name         = tbox_backend_wayland_seat_name,
};

/* --- xdg_surface: the configure/ack_configure handshake required before any
 * attached buffer is actually displayed. Unlike the example this evolved
 * from, this does NOT draw/commit immediately -- there is no display list to
 * paint yet, only tbox_backend_wayland_present() has one. It is fine for the
 * matching wl_surface.commit to happen later, whenever the caller next
 * presents a frame; the ack_configure request alone satisfies the
 * protocol's requirement to acknowledge the configure event. --- */
static void tbox_backend_wayland_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    tbox_backend_wayland *backend = data;

    xdg_surface_ack_configure(xdg_surface, serial);

    /* width/height == 0 means "the compositor has no opinion, pick your own
     * size" (e.g. a plain floating window manager on the very first
     * configure) -- keep whatever backend->width/height already holds (the
     * caller's requested size on the first call) in that case. A tiling
     * compositor like Sway instead sends the actual tile size it allocated. */
    int32_t new_width       = backend->pending_width > 0 ? backend->pending_width : backend->width;
    int32_t new_height      = backend->pending_height > 0 ? backend->pending_height : backend->height;
    backend->pending_width  = 0;
    backend->pending_height = 0;

    backend->width      = new_width;
    backend->height     = new_height;
    backend->configured = true;
}

static const struct xdg_surface_listener tbox_backend_wayland_surface_listener = {
    .configure = tbox_backend_wayland_surface_configure,
};

/* --- xdg_toplevel: the compositor's own close request (titlebar "x", or
 * equivalent gesture). --- */
static void tbox_backend_wayland_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    (void)toplevel;
    (void)states;
    tbox_backend_wayland *backend = data;

    /* Only records the suggestion -- applied together with every other
     * pending configure state when xdg_surface::configure is ack'd, per the
     * protocol's batching rules. */
    if (width > 0) {
        backend->pending_width = width;
    }
    if (height > 0) {
        backend->pending_height = height;
    }
}

static void tbox_backend_wayland_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    (void)toplevel;
    tbox_backend_wayland *backend = data;
    backend->should_close         = true;
}

static const struct xdg_toplevel_listener tbox_backend_wayland_toplevel_listener = {
    .configure = tbox_backend_wayland_toplevel_configure,
    .close     = tbox_backend_wayland_toplevel_close,
};

/* --- wl_registry: bind every global this backend needs. --- */
static void tbox_backend_wayland_registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    tbox_backend_wayland *backend = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        backend->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        backend->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        backend->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(backend->wm_base, &tbox_backend_wayland_wm_base_listener, backend);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        backend->seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(backend->seat, &tbox_backend_wayland_seat_listener, backend);
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        uint32_t bind_version = version < 3 ? version : 3;
        backend->data_manager = wl_registry_bind(registry, name,
            &wl_data_device_manager_interface, bind_version);
    }
}

static void tbox_backend_wayland_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener tbox_backend_wayland_registry_listener = {
    .global        = tbox_backend_wayland_registry_global,
    .global_remove = tbox_backend_wayland_registry_global_remove,
};

void tbox_backend_wayland_destroy(tbox_backend_wayland *backend) {
    if (backend == NULL) {
        return;
    }

    if (backend->paste_fd >= 0) close(backend->paste_fd);
    free(backend->paste_buffer);
    for (tbox_clipboard_write *pending = backend->writes; pending != NULL;) {
        tbox_clipboard_write *next = pending->next;
        close(pending->fd);
        free(pending->text);
        free(pending);
        pending = next;
    }
    for (tbox_clipboard_source *source = backend->sources; source != NULL;) {
        tbox_clipboard_source *next = source->next;
        wl_data_source_destroy(source->proxy);
        free(source->text);
        free(source);
        source = next;
    }
    for (tbox_clipboard_offer *offer = backend->offers; offer != NULL;) {
        tbox_clipboard_offer *next = offer->next;
        wl_data_offer_destroy(offer->proxy);
        free(offer->mime_utf8);
        free(offer->mime_plain);
        free(offer);
        offer = next;
    }
    if (backend->data_device != NULL) {
        if (wl_data_device_get_version(backend->data_device) >= 2)
            wl_data_device_release(backend->data_device);
        else wl_data_device_destroy(backend->data_device);
    }
    if (backend->data_manager != NULL) wl_data_device_manager_destroy(backend->data_manager);
    for (size_t i = 0; i < backend->event_count; i++) {
        if (backend->events[i].kind == TBOX_INPUT_PASTE)
            free(backend->events[i].data.paste.utf8);
    }

    if (backend->keyboard != NULL) {
        wl_keyboard_destroy(backend->keyboard);
    }
    if (backend->pointer != NULL) {
        wl_pointer_destroy(backend->pointer);
    }
    if (backend->seat != NULL) {
        wl_seat_destroy(backend->seat);
    }
    if (backend->shm_data != NULL) {
        munmap(backend->shm_data, backend->shm_size);
    }
    if (backend->shm_fd != -1) {
        close(backend->shm_fd);
    }
    if (backend->buffer != NULL) {
        wl_buffer_destroy(backend->buffer);
    }
    if (backend->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(backend->xdg_toplevel);
    }
    if (backend->xdg_surface != NULL) {
        xdg_surface_destroy(backend->xdg_surface);
    }
    if (backend->surface != NULL) {
        wl_surface_destroy(backend->surface);
    }
    if (backend->wm_base != NULL) {
        xdg_wm_base_destroy(backend->wm_base);
    }
    if (backend->shm != NULL) {
        wl_shm_destroy(backend->shm);
    }
    if (backend->compositor != NULL) {
        wl_compositor_destroy(backend->compositor);
    }
    if (backend->xkb_state != NULL) {
        xkb_state_unref(backend->xkb_state);
    }
    if (backend->compose_state != NULL) {
        xkb_compose_state_unref(backend->compose_state);
    }
    if (backend->compose_table != NULL) {
        xkb_compose_table_unref(backend->compose_table);
    }
    if (backend->xkb_keymap != NULL) {
        xkb_keymap_unref(backend->xkb_keymap);
    }
    if (backend->xkb_context != NULL) {
        xkb_context_unref(backend->xkb_context);
    }
    if (backend->registry != NULL) {
        wl_registry_destroy(backend->registry);
    }
    if (backend->display != NULL) {
        wl_display_disconnect(backend->display);
    }

    free(backend->events);
    free(backend);
}

bool tbox_backend_wayland_poll(tbox_backend_wayland *backend, int timeout_ms) {
    if (backend == NULL || backend->display == NULL) {
        return false;
    }

    /* The prepare_read/poll/read_events dance (rather than the simpler
     * blocking wl_display_dispatch) specifically so an external timeout can
     * be layered on top of it -- see wl_display_prepare_read(3) for why this
     * three-step sequence, rather than reading directly, is the correct way
     * to combine libwayland with an external poll() loop. */
    while (wl_display_prepare_read(backend->display) != 0) {
        if (wl_display_dispatch_pending(backend->display) == -1) {
            return false;
        }
    }

    if (wl_display_flush(backend->display) == -1 && errno != EAGAIN) {
        wl_display_cancel_read(backend->display);
        return false;
    }

    struct pollfd pfd = { .fd = wl_display_get_fd(backend->display), .events = POLLIN };
    int ready         = poll(&pfd, 1, timeout_ms);

    if (ready < 0) {
        wl_display_cancel_read(backend->display);
        /* EINTR is not a real failure -- nothing to read yet, try again on
         * the caller's next tbox_backend_wayland_poll call. */
        return errno == EINTR;
    }

    if (ready == 0) {
        /* Timed out with nothing pending. */
        wl_display_cancel_read(backend->display);
        tbox_backend_wayland_repeat_tick(backend);
        tbox_backend_wayland_clipboard_pump(backend);
        return true;
    }

    if (pfd.revents & POLLIN) {
        if (wl_display_read_events(backend->display) == -1) {
            return false;
        }
    } else {
        wl_display_cancel_read(backend->display);
    }

    if (wl_display_dispatch_pending(backend->display) == -1) {
        return false;
    }

    tbox_backend_wayland_repeat_tick(backend);
    tbox_backend_wayland_clipboard_pump(backend);

    return true;
}

bool tbox_backend_wayland_should_close(const tbox_backend_wayland *backend) {
    return backend == NULL || backend->should_close;
}

void tbox_backend_wayland_size(const tbox_backend_wayland *backend, int32_t *out_width, int32_t *out_height) {
    int32_t width  = 0;
    int32_t height = 0;
    if (backend != NULL) {
        width  = backend->width;
        height = backend->height;
    }
    if (out_width != NULL) {
        *out_width = width;
    }
    if (out_height != NULL) {
        *out_height = height;
    }
}

bool tbox_backend_wayland_take_event(tbox_backend_wayland *backend, tbox_input_event *out_event) {
    if (backend == NULL || out_event == NULL || backend->event_count == 0) {
        return false;
    }
    *out_event = backend->events[0];
    backend->event_count--;
    memmove(backend->events, backend->events + 1, backend->event_count * sizeof(*backend->events));
    return true;
}

bool tbox_backend_wayland_pointer_position(const tbox_backend_wayland *backend, double *out_x, double *out_y) {
    if (backend == NULL || out_x == NULL || out_y == NULL) {
        return false;
    }
    if (!backend->pointer_has_focus) {
        return false;
    }

    *out_x = backend->pointer_x;
    *out_y = backend->pointer_y;
    return true;
}

tbox_backend_wayland *tbox_backend_wayland_open(int32_t width, int32_t height, const char *title) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    tbox_backend_wayland *backend = calloc(1, sizeof(*backend));
    if (backend == NULL) {
        return NULL;
    }
    backend->shm_fd = -1;
    backend->paste_fd = -1;
    backend->width  = width;
    backend->height = height;

    backend->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (backend->xkb_context == NULL) {
        tbox_backend_wayland_destroy(backend);
        return NULL;
    }
    const char *locale = getenv("LC_ALL");
    if (locale == NULL || *locale == '\0') locale = getenv("LC_CTYPE");
    if (locale == NULL || *locale == '\0') locale = getenv("LANG");
    if (locale == NULL || *locale == '\0') locale = "C";
    backend->compose_table = xkb_compose_table_new_from_locale(
        backend->xkb_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (backend->compose_table != NULL) {
        backend->compose_state = xkb_compose_state_new(
            backend->compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
    }

    backend->display = wl_display_connect(NULL);
    if (backend->display == NULL) {
        tbox_backend_wayland_destroy(backend);
        return NULL;
    }

    backend->registry = wl_display_get_registry(backend->display);
    wl_registry_add_listener(backend->registry, &tbox_backend_wayland_registry_listener, backend);
    wl_display_roundtrip(backend->display);

    if (backend->data_manager != NULL && backend->seat != NULL) {
        backend->data_device = wl_data_device_manager_get_data_device(backend->data_manager, backend->seat);
        if (backend->data_device != NULL)
            wl_data_device_add_listener(backend->data_device,
                &tbox_backend_wayland_clipboard_device_listener, backend);
    }

    if (backend->compositor == NULL || backend->shm == NULL || backend->wm_base == NULL) {
        tbox_backend_wayland_destroy(backend);
        return NULL;
    }

    backend->surface     = wl_compositor_create_surface(backend->compositor);
    backend->xdg_surface = xdg_wm_base_get_xdg_surface(backend->wm_base, backend->surface);
    xdg_surface_add_listener(backend->xdg_surface, &tbox_backend_wayland_surface_listener, backend);
    backend->xdg_toplevel = xdg_surface_get_toplevel(backend->xdg_surface);
    xdg_toplevel_add_listener(backend->xdg_toplevel, &tbox_backend_wayland_toplevel_listener, backend);
    xdg_toplevel_set_title(backend->xdg_toplevel, title != NULL ? title : "tbox");
    xdg_toplevel_set_app_id(backend->xdg_toplevel, title != NULL ? title : "tbox");
    wl_surface_commit(backend->surface);

    /* Block until the compositor's initial xdg_surface::configure is
     * received and ack'd, so tbox_backend_wayland_size() is meaningful the
     * moment this call returns, rather than the caller having to poll first
     * just to find out what size actually got negotiated (see this
     * function's doc comment in <tbox/output.h>). */
    while (!backend->configured && !backend->should_close) {
        if (!tbox_backend_wayland_poll(backend, -1)) {
            break;
        }
    }

    if (!backend->configured) {
        tbox_backend_wayland_destroy(backend);
        return NULL;
    }

    return backend;
}

/* (Re)allocates backend->buffer/shm_data to match backend->width/height, if
 * it doesn't already. Mirrors example/tbox_wayland.c's draw_and_attach's
 * buffer-allocation half (the checkerboard-drawing half is gone -- callers
 * paint via tbox_raster_display_list instead). Returns false, leaving the
 * previous buffer (if any) untouched, on any allocation failure. */
static bool tbox_backend_wayland_ensure_buffer(tbox_backend_wayland *backend) {
    if (backend->buffer != NULL && backend->buffer_width == backend->width && backend->buffer_height == backend->height) {
        return true;
    }

    const int32_t stride   = backend->width * 4;
    const size_t pool_size = (size_t)stride * (size_t)backend->height;

    int fd = memfd_create("tbox_backend_wayland-shm", MFD_CLOEXEC);
    if (fd == -1) {
        return false;
    }
    if (ftruncate(fd, (off_t)pool_size) == -1) {
        close(fd);
        return false;
    }

    void *data = mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(backend->shm, fd, (int32_t)pool_size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, backend->width, backend->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    /* Replace whatever buffer/mapping backed the previous size, if any -- the
     * underlying shared memory the compositor is (or was) reading stays
     * valid as long as it holds its own fd/mapping, so it's safe to drop
     * ours as soon as the new one is ready. */
    if (backend->buffer != NULL) {
        wl_buffer_destroy(backend->buffer);
    }
    if (backend->shm_data != NULL) {
        munmap(backend->shm_data, backend->shm_size);
    }
    if (backend->shm_fd != -1) {
        close(backend->shm_fd);
    }

    backend->shm_fd        = fd;
    backend->shm_data      = data;
    backend->shm_size      = pool_size;
    backend->buffer        = buffer;
    backend->buffer_width  = backend->width;
    backend->buffer_height = backend->height;
    return true;
}

void tbox_backend_wayland_present(tbox_backend_wayland *backend, const tbox_display_list *list) {
    if (backend == NULL || backend->surface == NULL || backend->shm == NULL) {
        return;
    }
    if (backend->width <= 0 || backend->height <= 0) {
        return;
    }
    if (!tbox_backend_wayland_ensure_buffer(backend)) {
        return;
    }

    uint32_t *pixels   = backend->shm_data;
    size_t pixel_count = (size_t)backend->width * (size_t)backend->height;

    /* Clear to opaque white first: tbox_raster_* alpha-blends rather than
     * overwriting, so a stale previous frame (or uninitialized shm memory on
     * the very first present) would otherwise show through wherever the
     * display list has no fully-opaque coverage. White matches every
     * browser's real default canvas background -- closer to expected v0
     * output than showing nothing/black, given v0 has no UA stylesheet to
     * otherwise supply one (see ARCHITECTURE.md's "Orchestration / Main
     * Loop" section). */
    for (size_t i = 0; i < pixel_count; i++) {
        pixels[i] = 0xFFFFFFFFu;
    }

    tbox_raster_display_list(pixels, backend->width, backend->height, list);

    wl_surface_attach(backend->surface, backend->buffer, 0, 0);
    wl_surface_damage_buffer(backend->surface, 0, 0, backend->width, backend->height);
    wl_surface_commit(backend->surface);
}
