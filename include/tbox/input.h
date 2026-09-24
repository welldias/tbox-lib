#ifndef TBOX_INPUT_H
#define TBOX_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    TBOX_KEY_BACKSPACE,
    TBOX_KEY_DELETE,
    TBOX_KEY_LEFT,
    TBOX_KEY_RIGHT,
    TBOX_KEY_UP,
    TBOX_KEY_DOWN,
    TBOX_KEY_HOME,
    TBOX_KEY_END,
    TBOX_KEY_A,
    TBOX_KEY_C,
    TBOX_KEY_V,
    TBOX_KEY_X,
} tbox_key;

typedef struct tbox_key_event {
    tbox_key key;
    bool pressed;
    bool shift;
    bool control; /* used for shortcuts such as Ctrl+A */
} tbox_key_event;

typedef enum tbox_input_event_kind {
    TBOX_INPUT_POINTER_CLICK,
    TBOX_INPUT_POINTER_DRAG,
    TBOX_INPUT_POINTER_RELEASE,
    TBOX_INPUT_POINTER_SCROLL,
    TBOX_INPUT_KEY,
    TBOX_INPUT_TEXT,
    TBOX_INPUT_PASTE,
} tbox_input_event_kind;

/* Backend queue event. TEXT carries one committed UTF-8 chunk inline so
 * platform adapters do not expose native input objects or transfer ownership
 * of temporary compositor buffers to the application layer. Longer commits
 * can be split at codepoint boundaries. PASTE owns a heap buffer passed to
 * the event consumer, which must free it after dispatch. */
typedef struct tbox_input_event {
    tbox_input_event_kind kind;
    uint32_t serial; /* native input serial, used by the Wayland clipboard */
    union {
        struct { double x, y; bool double_click; } click;
        struct { double x, y; } drag;
        struct { double x, y, delta_y; } scroll;
        tbox_key_event key;
        struct { char utf8[32]; size_t length; } text;
        struct { char *utf8; size_t length; } paste; /* receiver frees utf8 */
    } data;
} tbox_input_event;

#ifdef __cplusplus
}
#endif

#endif /* TBOX_INPUT_H */
