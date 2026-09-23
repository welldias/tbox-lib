#ifndef TBOX_INPUT_H
#define TBOX_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logical keys used by the GUI core. Backends translate native key codes.
 * Text composition is a separate input channel. */
typedef enum tbox_key {
    TBOX_KEY_UNKNOWN,
    TBOX_KEY_TAB,
    TBOX_KEY_ENTER,
    TBOX_KEY_SPACE,
    TBOX_KEY_ESCAPE,
} tbox_key;

typedef struct tbox_key_event {
    tbox_key key;
    bool pressed;
    bool shift;
} tbox_key_event;

typedef enum tbox_input_event_kind {
    TBOX_INPUT_POINTER_CLICK,
    TBOX_INPUT_KEY,
} tbox_input_event_kind;

typedef struct tbox_input_event {
    tbox_input_event_kind kind;
    union {
        struct { double x, y; } click;
        tbox_key_event key;
    } data;
} tbox_input_event;

#ifdef __cplusplus
}
#endif

#endif /* TBOX_INPUT_H */
