#ifndef TBOX_BACKEND_WAYLAND_H
#define TBOX_BACKEND_WAYLAND_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/input.h>
#include <tbox/render.h>

/* Native Wayland backend. Keep this interface private to Output; Application
 * uses tbox_window_backend instead. */
typedef struct tbox_backend_wayland tbox_backend_wayland;

tbox_backend_wayland *tbox_backend_wayland_open(int32_t width, int32_t height, const char *title);
void tbox_backend_wayland_destroy(tbox_backend_wayland *backend);
bool tbox_backend_wayland_poll(tbox_backend_wayland *backend, int timeout_ms);
bool tbox_backend_wayland_should_close(const tbox_backend_wayland *backend);
void tbox_backend_wayland_size(const tbox_backend_wayland *backend, int32_t *width, int32_t *height);
bool tbox_backend_wayland_take_event(tbox_backend_wayland *backend, tbox_input_event *event);
bool tbox_backend_wayland_pointer_position(const tbox_backend_wayland *backend, double *x, double *y);
void tbox_backend_wayland_present(tbox_backend_wayland *backend, const tbox_display_list *list);

#endif
