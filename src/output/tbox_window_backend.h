#ifndef TBOX_WINDOW_BACKEND_H
#define TBOX_WINDOW_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/input.h>
#include <tbox/render.h>

/* Private platform boundary used by Application. Native window, keymap and
 * compositor types must not appear above this interface. */
typedef struct tbox_window_backend tbox_window_backend;

tbox_window_backend *tbox_window_backend_open(int32_t width, int32_t height, const char *title);
void tbox_window_backend_destroy(tbox_window_backend *window);
bool tbox_window_backend_poll(tbox_window_backend *window, int timeout_ms);
bool tbox_window_backend_should_close(const tbox_window_backend *window);
void tbox_window_backend_size(const tbox_window_backend *window, int32_t *width, int32_t *height);
bool tbox_window_backend_take_event(tbox_window_backend *window, tbox_input_event *event);
bool tbox_window_backend_pointer_position(const tbox_window_backend *window, double *x, double *y);
bool tbox_window_backend_clipboard_copy(tbox_window_backend *window, const char *text, size_t length, uint32_t serial);
bool tbox_window_backend_clipboard_paste(tbox_window_backend *window);
void tbox_window_backend_present(tbox_window_backend *window, const tbox_display_list *list);

#endif
