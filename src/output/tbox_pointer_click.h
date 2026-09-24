#ifndef TBOX_POINTER_CLICK_H
#define TBOX_POINTER_CLICK_H

#include <stdbool.h>
#include <stdint.h>

/* Wayland timestamps are uint32 milliseconds and may wrap. Requiring both a
 * short interval and nearby presses avoids selecting across distant clicks. */
static inline bool tbox_pointer_is_double_click(uint32_t time, uint32_t last_time,
    double x, double y, double last_x, double last_y) {
    double dx = x - last_x, dy = y - last_y;
    return last_time != 0 && (uint32_t)(time - last_time) <= 400 &&
        dx * dx + dy * dy <= 16.0;
}

#endif
