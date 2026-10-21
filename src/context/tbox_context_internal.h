#ifndef TBOX_CONTEXT_INTERNAL_H
#define TBOX_CONTEXT_INTERNAL_H

/* Estado e contratos compartilhados apenas pelos módulos de src/context/.
 * Registros públicos de eventos pertencem a <tbox/app.h>. */

#include <tbox/context.h>
#include "tbox_context_callbacks.h"
#include <tbox/css_parser.h>
#include <tbox/css_selector.h>
#include <tbox/html_parser.h>
#include <tbox/style.h>

#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "base/tbox_arena.h"
#include "base/tbox_number.h"
#include "base/tbox_release.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "context/tbox_context_hit_test.h"
#include "font/tbox_font_measure.h"
#include "layout/tbox_layout_bidi.h"
#include "layout/tbox_layout_password.h"

/* One tbox_context_bind_click registration: a compiled selector-group plus
 * the handler/userdata to fire when some ancestor of a clicked node
 * matches it. Lives in ctx->handlers (see below).
 *
 * Interatividade Avançada: `id` is the opaque handle
 * tbox_context_bind_click hands back (see ctx->next_handler_id below); `active`
 * is a tombstone flag -- tbox_context_unbind_click sets it to false and
 * destroys `query` (setting it to NULL) rather than physically removing the
 * slot from ctx->handlers, since tbox_vector has no removal primitive and
 * this is a small UI-sized array where a dead slot costs nothing measurable
 * (same "linear scan is fine" precedent already used elsewhere in this
 * module). tbox_context_dispatch_click skips any binding with active ==
 * false; tbox_context_close destroys whatever `query` is still non-NULL
 * (an unbound binding's query is already destroyed and NULLed, so it is
 * never double-destroyed there). */
typedef struct tbox_context_click_binding {
    tbox_css_selector_query *query;
    tbox_context_click_handler handler;
    void *userdata;
    tbox_destroy_notify destroy; /* released at unbind or close, see <tbox/callback.h> */
    int id;
    bool active;
} tbox_context_click_binding;

typedef struct tbox_text_field {
    const tbox_html_node *node;
    char *value;
    size_t length, capacity, cursor, anchor;
    double scroll_x, scroll_y;
    struct tbox_text_line *lines;
    size_t line_count;
    bool reveal;
    struct tbox_text_field *next;
} tbox_text_field;

/* A running transition of one property: from `start` to the state's
 * value, over [start_time, start_time + duration]. */
typedef struct tbox_transition_track {
    double start[TBOX_STYLE_ANIM_VALUE_SIZE];
    double start_time, duration;
    tbox_style_easing easing;
    bool active;
} tbox_transition_track;

typedef struct tbox_transition_state {
    const tbox_html_node *node;
    /* Last frame's target of every animatable property, to detect changes. */
    double values[TBOX_STYLE_ANIM_COUNT][TBOX_STYLE_ANIM_VALUE_SIZE];
    /* NULL until the element first starts a transition. */
    tbox_transition_track *tracks;
    bool hidden; /* display: none last frame: showing it again uses @starting-style */
    struct tbox_transition_state *next;
} tbox_transition_state;

/* One resolved keyframe: the animatable properties it names (bit mask
 * over tbox_style_animatable) and their values. */
typedef struct tbox_animation_frame {
    double offset;
    unsigned long long mask;
    double values[TBOX_STYLE_ANIM_COUNT][TBOX_STYLE_ANIM_VALUE_SIZE];
    bool has_easing; /* animation-timing-function inside the keyframe */
    tbox_style_easing easing;
} tbox_animation_frame;

/* A running `animation` of one element, matched across frames by name. */
typedef struct tbox_animation_state {
    const tbox_html_node *node;
    char name[64];
    double start_time;
    bool paused;
    double paused_at;
    const tbox_css_keyframes_rule *rule; /* the frames below were resolved from it */
    tbox_animation_frame *frames;
    size_t frame_count;
    bool seen; /* still named by the element's style this frame */
    struct tbox_animation_state *next;
} tbox_animation_state;

typedef struct tbox_text_line {
    size_t start, end;
    double width;
} tbox_text_line;

typedef struct tbox_select_field {
    const tbox_html_node *node;
    const tbox_html_node *selected;
    bool dirty;
    struct tbox_select_field *next;
} tbox_select_field;

typedef struct tbox_scroll_state {
    const tbox_html_node *node;
    double y;
    /* scroll-behavior: smooth -- y eases from `from_y` to `target_y`
     * starting at `start_time` (ctx->animation_time) while `animating`. */
    bool animating;
    double from_y, target_y, start_time;
    /* scroll-snap-type: a wheel/drag gesture left the container unsnapped;
     * tbox_context_apply_scroll snaps it once the gesture has been idle
     * for a moment after `snap_time`. `snap_origin_y` is where the gesture
     * began (for scroll-snap-stop: always). */
    bool snap_pending;
    double snap_time, snap_origin_y;
    struct tbox_scroll_state *next;
} tbox_scroll_state;

/* A `resize` the user dragged: the element's `width`/`height` (in its
 * box-sizing box) from then on, applied over its style every frame while
 * the axis stays resizable. */
typedef struct tbox_resize_state {
    const tbox_html_node *node;
    bool has_width, has_height;
    double width, height;
    struct tbox_resize_state *next;
} tbox_resize_state;

typedef struct tbox_date {
    int year, month, day;
} tbox_date;

typedef struct tbox_file_field {
    const tbox_html_node *node;
    char *path;
    struct tbox_file_field *next;
} tbox_file_field;

typedef struct tbox_file_entry {
    char *name;
    bool directory;
} tbox_file_entry;

typedef struct tbox_form_default {
    const tbox_html_node *node;
    const tbox_html_node *form;
    tbox_string_view value;
    bool checked;
    bool selected;
    struct tbox_form_default *next;
} tbox_form_default;

/* See <tbox/context.h> for why this is opaque rather than a plain/visible
 * struct like tbox_layout_box: frame_arena is a tbox_arena BY VALUE, and
 * tbox_arena's full definition lives in the internal src/base/tbox_arena.h,
 * not under include/tbox/. */
struct tbox_context {
    tbox_html_document *document;             /* owned: parsed in tbox_context_open, destroyed in tbox_context_close */
    tbox_css_stylesheet *stylesheet;          /* owned, same lifecycle; author-only (see tbox_context_run_frame) */
    tbox_css_stylesheet *ua_stylesheet;       /* owned: built from typed UA rules and config, TBOX_CSS_ORIGIN_USER_AGENT in the cascade */
    tbox_css_stylesheet *control_stylesheet;  /* owned: internal picker theme, never cascaded into document nodes */
    tbox_css_stylesheet *internal_stylesheet; /* owned; CSS from <style> and local <link>, in document order, AUTHOR origin */
    tbox_font_face_cache *fonts;              /* borrowed -- built/destroyed by the caller, never by tbox_context (was a single tbox_font_face) */
    tbox_image_cache *images;                 /* borrowed, same lifecycle stance as `fonts` above -- may be NULL ("no images", see <tbox/image.h>) */
    tbox_layout_box *root;                    /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;                   /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;                 /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_bind_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;                     /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
    int next_handler_id;                      /* monotonic counter for tbox_context_bind_click's returned handle -- never reused, even after tbox_context_unbind_click removes a binding */
    const tbox_html_node *hovered_node;       /* the node currently under the pointer, or NULL -- a plain struct field with its own lifetime, deliberately NOT part of frame_arena (must survive every tbox_context_run_frame's arena reset so tbox_context_update_hover can compare across frames; see <tbox/context.h>) */
    const tbox_html_node *active_node;        /* pointer-pressed node, retained until release for :active */
    const tbox_html_node *focused_node;
    bool focus_keyboard;
    tbox_text_field *text_fields;
    tbox_file_field *file_fields;
    tbox_select_field *select_fields;
    tbox_form_default *form_defaults;
    const tbox_html_node *open_select;
    const tbox_html_node *open_color;
    unsigned color_channel;
    const tbox_html_node *open_date;
    tbox_date date_cursor;
    int date_hour, date_minute;
    const tbox_html_node *open_file;
    char *file_directory;
    tbox_file_entry *file_entries;
    size_t file_count, file_first, file_highlight;
    bool has_file_input;
    bool has_number_input;
    bool has_range_input;
    bool has_gauge;
    bool has_search_input;
    const tbox_html_node *range_drag_node;
    const tbox_html_node *popup_highlight;
    size_t popup_first;
    double viewport_width;
    double viewport_height;
    tbox_context_select_handler select_handler;
    void *select_userdata;
    tbox_destroy_notify select_destroy;
    tbox_scroll_state *scroll_states;
    const tbox_html_node *scroll_drag_node;
    const tbox_html_node *scroll_thumb_hover; /* box whose scrollbar thumb is under the pointer (compared, never dereferenced) */
    double scroll_drag_grab_y;
    bool scroll_pointer_consumed;
    tbox_resize_state *resize_states;
    const tbox_html_node *resize_drag_node; /* whose resize handle the pointer holds */
    tbox_style_resize resize_drag_axes;
    bool resize_drag_rtl;
    double resize_grab_x, resize_grab_y, resize_start_width, resize_start_height;
    bool focus_scroll_pending;
    tbox_context_input_handler input_handler;
    void *input_userdata;
    tbox_destroy_notify input_destroy;
    tbox_context_submit_handler submit_handler;
    void *submit_userdata;
    tbox_destroy_notify submit_destroy;
    /* Userdata releases waiting for the application callbacks in progress
     * (see src/base/tbox_release.h); every handler call goes through
     * tbox_context_emit_* or the click dispatch, which bracket it. */
    tbox_release_list releases;
    tbox_style_table styles; /* last frame's styles, for focus visibility checks */
    tbox_transition_state *transitions;
    tbox_animation_state *animations;
    double animation_time;
    /* Earliest animation_time at which a frame would look different with no
     * new input (INFINITY: idle); see tbox_context_next_frame_time. */
    double next_frame_time;
    bool prefers_reduced_motion;
    bool has_starting_style; /* some author rule is inside @starting-style */
};

/* Asks for a frame no later than `seconds` (animation_time clock);
 * -INFINITY: the very next frame. */
static inline void tbox_context_wake_at(tbox_context *ctx, double seconds) {
    if (seconds < ctx->next_frame_time) ctx->next_frame_time = seconds;
}

/* Call the application's input/select/submit handler, if set, with the
 * userdata release bracket around it. */
void tbox_context_emit_input(tbox_context *ctx, const tbox_html_node *input, tbox_string_view value);
void tbox_context_emit_select(tbox_context *ctx, const tbox_html_node *select, tbox_string_view value);
void tbox_context_emit_submit(tbox_context *ctx, const tbox_html_node *form, const tbox_html_node *submitter);

void tbox_context_free_transitions(tbox_context *ctx);
void tbox_context_free_animations(tbox_context *ctx);
void tbox_context_apply_animations(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height);
void tbox_context_apply_transitions(tbox_context *ctx, const tbox_css_cascade_source *sources, size_t source_count, double viewport_width, double viewport_height);
void tbox_context_register_font_faces(tbox_font_face_cache *fonts, const tbox_css_stylesheet *sheet, const char *base_dir);

double tbox_context_style_line_height(const tbox_style *style, const tbox_font_face *face);
bool tbox_context_is_textarea(const tbox_html_node *node);
bool tbox_context_is_summary(const tbox_html_node *node);
bool tbox_context_toggle_details(tbox_context *ctx, const tbox_html_node *summary);
const tbox_html_node *tbox_context_image_area_at(const tbox_context *ctx, const tbox_layout_box *box, double x, double y);
bool tbox_context_is_text_control(const tbox_html_node *node);
bool tbox_context_is_select(const tbox_html_node *node);
bool tbox_context_is_color_input(const tbox_html_node *node);
tbox_css_rgba tbox_context_color_value(const tbox_html_node *node);
bool tbox_context_color_commit(tbox_context *ctx, const tbox_html_node *node, tbox_css_rgba color);
bool tbox_context_is_datetime_input(const tbox_html_node *node);
bool tbox_context_is_month_input(const tbox_html_node *node);
bool tbox_context_is_time_input(const tbox_html_node *node);
bool tbox_context_is_week_input(const tbox_html_node *node);
bool tbox_context_is_calendar_input(const tbox_html_node *node);
bool tbox_context_is_file_input(const tbox_html_node *node);
void tbox_context_file_close(tbox_context *ctx);
bool tbox_context_file_open(tbox_context *ctx, const tbox_html_node *node);
tbox_file_field *tbox_context_file_field(tbox_context *ctx, const tbox_html_node *node, bool create);
bool tbox_context_file_activate(tbox_context *ctx, size_t index);
int tbox_context_date_days(int year, int month);
bool tbox_context_date_parse(tbox_string_view value, tbox_date *out);
bool tbox_context_month_parse(tbox_string_view value, tbox_date *out);
bool tbox_context_datetime_parse(tbox_string_view value, tbox_date *date, int *hour, int *minute);
bool tbox_context_time_parse(tbox_string_view value, int *hour, int *minute);
int tbox_context_date_compare(tbox_date a, tbox_date b);
bool tbox_context_calendar_selection_allowed(const tbox_context *ctx);
void tbox_context_date_open(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_date_commit(tbox_context *ctx, const tbox_html_node *node, tbox_date date);
void tbox_context_sanitize_inputs(tbox_context *ctx, tbox_html_node *node);
void tbox_context_capture_form_defaults(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_reset_form(tbox_context *ctx, const tbox_html_node *reset);
bool tbox_context_submit_form(tbox_context *ctx, const tbox_html_node *activator, const tbox_html_node *submitter);
bool tbox_context_option_disabled(const tbox_html_node *option);
size_t tbox_context_option_count(const tbox_html_node *select);
tbox_select_field *tbox_context_select_field(tbox_context *ctx, const tbox_html_node *node);
/* Marks each <option> under `node` (and its siblings) with whether its
 * select has it chosen now (tbox_html_node.selectedness, `option:checked`). */
void tbox_context_sync_option_selectedness(tbox_context *ctx, const tbox_html_node *node);
/* Pointer at (x, y) over the open select list: highlights the enabled
 * option under it (*changed when that moved). False when not over the list. */
bool tbox_context_select_popup_hover(tbox_context *ctx, double x, double y, bool *changed);
const tbox_layout_box *tbox_context_find_box(const tbox_layout_box *box, const tbox_html_node *node);
void tbox_context_sync_select_boxes(tbox_context *ctx, tbox_layout_box *box);
bool tbox_context_color_popup_visible(const tbox_context *ctx);
bool tbox_context_color_popup_contains(const tbox_context *ctx, double x, double y);
bool tbox_context_color_choose_point(tbox_context *ctx, double x, double y);
bool tbox_context_date_popup_visible(const tbox_context *ctx);
bool tbox_context_date_popup_contains(const tbox_context *ctx, double x, double y);
bool tbox_context_date_shift_month(tbox_date *date, int direction);
bool tbox_context_date_shift_year(tbox_date *date, int direction);
bool tbox_context_date_shift_day(tbox_date *date, int delta);
bool tbox_context_week_monday(tbox_date *date);
bool tbox_context_week_parse(tbox_string_view value, tbox_date *out);
bool tbox_context_date_popup_click(tbox_context *ctx, double x, double y);
bool tbox_context_file_popup_visible(tbox_context *ctx);
bool tbox_context_file_popup_contains(tbox_context *ctx, double x, double y);
bool tbox_context_file_popup_scroll(tbox_context *ctx, double x, double y, double delta_y);
bool tbox_context_file_parent_directory(tbox_context *ctx);
bool tbox_context_file_popup_click(tbox_context *ctx, double x, double y);
bool tbox_context_select_popup_contains(tbox_context *ctx, double x, double y);
bool tbox_context_select_popup_scroll(tbox_context *ctx, double x, double y, double delta_y);
void tbox_context_popup_reveal_highlight(tbox_context *ctx);
void tbox_context_apply_scroll(tbox_context *ctx, tbox_layout_box *box);
void tbox_context_reveal_focused(tbox_context *ctx);
void tbox_context_apply_sticky(tbox_context *ctx, double viewport_width, double viewport_height);
tbox_rect tbox_context_rect_intersection(tbox_rect a, tbox_rect b);
void tbox_context_append_scrollbars(tbox_context *ctx, tbox_display_list *list);
/* `resize` (tbox_context_resize.c): the handle's rect on `box` (false when
 * it has none), the dragged sizes applied over the frame's styles, the
 * handles painted, and the pointer press/drag/release on them. */
bool tbox_context_resizer_rect(const tbox_layout_box *box, tbox_rect *out);
const tbox_layout_box *tbox_context_resizer_at(const tbox_context *ctx, double x, double y);
void tbox_context_apply_resize(tbox_context *ctx, tbox_style_table *styles);
void tbox_context_paint_resizers(tbox_context *ctx, const tbox_layout_box *box, tbox_vector *items);
bool tbox_context_resize_press(tbox_context *ctx, double x, double y);
bool tbox_context_resize_drag(tbox_context *ctx, double x, double y);
void tbox_context_free_resize_states(tbox_context *ctx);
/* The node whose scrollbar thumb is at (x, y), or NULL. */
const tbox_html_node *tbox_context_scrollbar_thumb_at(tbox_context *ctx, double x, double y);
void tbox_context_push_fill(tbox_vector *items, tbox_rect rect, tbox_css_rgba color, bool has_clip, tbox_rect clip);
void tbox_context_paint_select_arrows(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_number_steppers(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_search_clears(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_ranges(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_gauges(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_select_popup(tbox_context *ctx, tbox_vector *items);
void tbox_context_paint_color_popup(tbox_context *ctx, tbox_vector *items);
void tbox_context_date_text(tbox_vector *items, const tbox_font_face *face, tbox_rect rect, tbox_string_view value, tbox_css_rgba color);
void tbox_context_paint_date_popup(tbox_context *ctx, tbox_vector *items);
void tbox_context_paint_file_controls(tbox_context *ctx, const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_file_popup(tbox_context *ctx, tbox_vector *items);
void tbox_context_layout_textareas(tbox_context *ctx, tbox_layout_box *box);
void tbox_context_paint_text_input_caret(tbox_context *ctx, tbox_display_list *out_list);
void tbox_context_paint_textarea_caret(tbox_context *ctx, tbox_display_list *out_list);
bool tbox_context_point_in_rect(tbox_rect r, double x, double y);
/* Maps the screen point (*x, *y) into `box`'s untransformed coordinates
 * (those of its layout rects): through the inverse of every transform
 * from the root down to `box`, its own included. False when one of them
 * is singular (nothing there can be hit). */
bool tbox_context_box_point(const tbox_layout_box *box, double *x, double *y);
bool tbox_context_popup_click(tbox_context *ctx, double x, double y);
bool tbox_context_node_attached(const tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_is_text_input(const tbox_html_node *node);
bool tbox_context_is_search_input(const tbox_html_node *node);
/* Width of value[0..offset) as `node` draws it (masked for a password),
 * within the shaping of the whole value. */
double tbox_context_input_advance(const tbox_html_node *node, const tbox_font_face *face, tbox_string_view value, size_t offset, double letter_spacing);
bool tbox_context_is_number_input(const tbox_html_node *node);
/* The number input's ::step-control column, or a non-empty search input's
 * ::clear-icon column, at the content box's right edge (16px wide by
 * default). False when the input has none or is too small for it. */
bool tbox_context_input_control_rect(const tbox_layout_box *box, tbox_rect *out);
bool tbox_context_is_range_input(const tbox_html_node *node);
bool tbox_context_number_syntax(tbox_string_view value, bool complete);
bool tbox_context_number_attribute(const tbox_html_node *node, const char *name, double *out);
void tbox_context_range_limits(const tbox_html_node *node, double *min, double *max, double *step, bool *any_step);
void tbox_context_range_normalize(tbox_context *ctx, tbox_html_node *node);
bool tbox_context_range_commit(tbox_context *ctx, const tbox_html_node *node, double value);
bool tbox_context_range_choose_x(tbox_context *ctx, const tbox_html_node *node, double x);
bool tbox_context_is_button_input(const tbox_html_node *node);
bool tbox_context_is_reset_input(const tbox_html_node *node);
bool tbox_context_is_submit_input(const tbox_html_node *node);
bool tbox_context_is_image_input(const tbox_html_node *node);
bool tbox_context_is_checkbox_input(const tbox_html_node *node);
bool tbox_context_is_radio_input(const tbox_html_node *node);
bool tbox_context_radio_select(tbox_context *ctx, const tbox_html_node *node);
void tbox_context_toggle_checkbox(tbox_context *ctx, const tbox_html_node *node);
tbox_text_field *tbox_context_text_field(tbox_context *ctx, const tbox_html_node *node);
void tbox_context_sync_text_value(tbox_context *ctx, tbox_text_field *field);
bool tbox_context_clear_search(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_number_step(tbox_context *ctx, const tbox_html_node *node, int direction, bool large);
size_t tbox_context_cursor_at_x(const tbox_text_field *field, const tbox_layout_box *box, const tbox_font_face *face, double x);
size_t tbox_context_cursor_at_point(const tbox_text_field *field, const tbox_layout_box *box, const tbox_font_face *face, double x, double y);
bool tbox_context_focusable(const tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_set_focus(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_focus_visibility(tbox_context *ctx, bool keyboard);
bool tbox_context_move_focus(tbox_context *ctx, bool reverse);
bool tbox_context_dispatch_select_key(tbox_context *ctx, tbox_key key);

const char *tbox_context_default_control_css(void);
tbox_style tbox_context_control_style(const tbox_context *ctx, const tbox_html_node *owner, const char *part, const char *state);
double tbox_context_control_size(tbox_style_length length, double fallback);
void tbox_context_paint_control_box(tbox_vector *items, tbox_rect rect, const tbox_style *style, bool has_clip, tbox_rect clip);
/* Control parts (docs/10-formularios-e-controles.md, "Partes dos
 * controles"). tbox_context_part never returns NULL: a style without the
 * part gives an empty one (no size, colors undeclared). */
const tbox_style_part_style *tbox_context_part(const tbox_style *style, tbox_style_part part);
/* AUTO -> fallback; px or % of `base` otherwise. */
double tbox_context_part_size(tbox_style_length length, double base, double fallback);
/* accent-color, or the built-in blue for auto. */
tbox_css_rgba tbox_context_accent_color(const tbox_style *style);
/* `part`'s border and radii around `rect`, filled with its declared
 * background or else `background`. */
void tbox_context_paint_part(tbox_vector *items, tbox_rect rect, const tbox_style_part_style *part,
                             tbox_css_rgba background, bool has_clip, tbox_rect clip);
/* Intersection of the padding boxes of `box`'s clipping ancestors; false
 * when none clips. */
bool tbox_context_ancestor_clip(const tbox_layout_box *box, tbox_rect *clip);
const tbox_font_face *tbox_context_control_font(tbox_context *ctx, const tbox_style *style);

#endif /* TBOX_CONTEXT_INTERNAL_H */
