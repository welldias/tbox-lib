#include "tbox_window_backend.h"

#include <stdlib.h>

#include <tbox/output.h>

#ifndef TBOX_HAS_WAYLAND
#define TBOX_HAS_WAYLAND 0
#endif

#if TBOX_HAS_WAYLAND
#include "tbox_backend_wayland.h"
#endif

typedef struct tbox_window_backend_ops {
    void (*destroy)(void *impl);
    bool (*poll)(void *impl, int timeout_ms);
    bool (*should_close)(const void *impl);
    void (*size)(const void *impl, int32_t *width, int32_t *height);
    bool (*take_event)(void *impl, tbox_input_event *event);
    bool (*pointer_position)(const void *impl, double *x, double *y);
    void (*present)(void *impl, const tbox_display_list *list);
} tbox_window_backend_ops;

struct tbox_window_backend {
    void *impl;
    const tbox_window_backend_ops *ops;
};

#if TBOX_HAS_WAYLAND
static void wayland_destroy(void *impl) { tbox_backend_wayland_destroy(impl); }
static bool wayland_poll(void *impl, int timeout_ms) { return tbox_backend_wayland_poll(impl, timeout_ms); }
static bool wayland_should_close(const void *impl) { return tbox_backend_wayland_should_close(impl); }
static void wayland_size(const void *impl, int32_t *width, int32_t *height) { tbox_backend_wayland_size(impl, width, height); }
static bool wayland_take_event(void *impl, tbox_input_event *event) { return tbox_backend_wayland_take_event(impl, event); }
static bool wayland_pointer_position(const void *impl, double *x, double *y) { return tbox_backend_wayland_pointer_position(impl, x, y); }
static void wayland_present(void *impl, const tbox_display_list *list) { tbox_backend_wayland_present(impl, list); }

static const tbox_window_backend_ops wayland_ops = {
    wayland_destroy, wayland_poll, wayland_should_close, wayland_size,
    wayland_take_event, wayland_pointer_position, wayland_present
};
#endif

tbox_window_backend *tbox_window_backend_open(int32_t width, int32_t height, const char *title) {
#if TBOX_HAS_WAYLAND
    tbox_backend_wayland *impl = tbox_backend_wayland_open(width, height, title);
    if (impl == NULL) {
        return NULL;
    }
    tbox_window_backend *window = malloc(sizeof(*window));
    if (window == NULL) {
        tbox_backend_wayland_destroy(impl);
        return NULL;
    }
    window->impl = impl;
    window->ops = &wayland_ops;
    return window;
#else
    (void)width; (void)height; (void)title;
    return NULL;
#endif
}

void tbox_window_backend_destroy(tbox_window_backend *window) {
    if (window != NULL) {
        window->ops->destroy(window->impl);
        free(window);
    }
}

bool tbox_window_backend_poll(tbox_window_backend *window, int timeout_ms) {
    return window != NULL && window->ops->poll(window->impl, timeout_ms);
}

bool tbox_window_backend_should_close(const tbox_window_backend *window) {
    return window == NULL || window->ops->should_close(window->impl);
}

void tbox_window_backend_size(const tbox_window_backend *window, int32_t *width, int32_t *height) {
    if (window != NULL) {
        window->ops->size(window->impl, width, height);
    } else {
        if (width != NULL) *width = 0;
        if (height != NULL) *height = 0;
    }
}

bool tbox_window_backend_take_event(tbox_window_backend *window, tbox_input_event *event) {
    return window != NULL && window->ops->take_event(window->impl, event);
}

bool tbox_window_backend_pointer_position(const tbox_window_backend *window, double *x, double *y) {
    return window != NULL && window->ops->pointer_position(window->impl, x, y);
}

void tbox_window_backend_present(tbox_window_backend *window, const tbox_display_list *list) {
    if (window != NULL) window->ops->present(window->impl, list);
}
