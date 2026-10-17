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
/* Waits up to `timeout_ms` (-1: no limit, 0: never blocks) for compositor
 * events, a wake, clipboard pipe progress or the next key repeat, whichever
 * comes first, then dispatches what arrived. False once the connection to
 * the compositor is lost. */
bool tbox_backend_wayland_poll(tbox_backend_wayland *backend, int timeout_ms);
/* Interrupts a poll in progress (or makes the next one return at once).
 * The only function here safe to call from any thread. */
void tbox_backend_wayland_wake(const tbox_backend_wayland *backend);
/* False while the compositor has not asked for the next frame yet (the
 * last present's wl_surface.frame is pending) or still holds both buffers.
 * Painting only when true paces frames to the display and pauses them while
 * the window is hidden. */
bool tbox_backend_wayland_frame_ready(const tbox_backend_wayland *backend);
bool tbox_backend_wayland_should_close(const tbox_backend_wayland *backend);
void tbox_backend_wayland_size(const tbox_backend_wayland *backend, int32_t *width, int32_t *height);
bool tbox_backend_wayland_take_event(tbox_backend_wayland *backend, tbox_input_event *event);
bool tbox_backend_wayland_pointer_position(const tbox_backend_wayland *backend, double *x, double *y);
bool tbox_backend_wayland_clipboard_copy(tbox_backend_wayland *backend, const char *text, size_t length, uint32_t serial);
bool tbox_backend_wayland_clipboard_paste(tbox_backend_wayland *backend);
/* Repaints and reports to the compositor only the regions that changed
 * (see tbox_damage_tracker), into whichever of the two buffers the
 * compositor has released; when nothing changed it commits without a new
 * buffer, only to keep the frame callback going. False when both buffers
 * are still held by the compositor: nothing was painted, the changes are
 * kept, and the caller should present again once frame_ready/an event
 * says so. */
bool tbox_backend_wayland_present(tbox_backend_wayland *backend, const tbox_display_list *list);
/* Outlines every repainted region in magenta, for inspecting partial
 * repaints. Forces one full repaint. */
void tbox_backend_wayland_set_debug_damage(tbox_backend_wayland *backend, bool enabled);
/* Shows the themed pointer cursor for `cursor` over the window (a no-op
 * without wayland-cursor). AUTO shows the default arrow. */
void tbox_backend_wayland_set_cursor(tbox_backend_wayland *backend, tbox_style_cursor cursor);

#endif
