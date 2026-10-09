#ifndef TBOX_CONTEXT_INTERNAL_H
#define TBOX_CONTEXT_INTERNAL_H

/* Estado e contratos compartilhados apenas pelos módulos de src/context/.
 * A interface para aplicações permanece em <tbox/context.h>. */

#include <tbox/context.h>
#include <tbox/css_parser.h>
#include <tbox/css_selector.h>
#include <tbox/html_parser.h>
#include <tbox/style.h>

#include <ctype.h>
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
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "context/tbox_context_hit_test.h"

/* One tbox_context_on_click registration: a compiled selector-group plus
 * the handler/userdata to fire when some ancestor of a clicked node
 * matches it. Lives in ctx->handlers (see below).
 *
 * Interatividade Avançada: `id` is the opaque handle
 * tbox_context_on_click hands back (see ctx->next_handler_id below); `active`
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

typedef struct tbox_transition_track {
    double start[4], target[4];
    double start_time, duration;
    unsigned char timing;
    bool active;
} tbox_transition_track;

typedef struct tbox_transition_state {
    const tbox_html_node *node;
    tbox_transition_track tracks[3];
    struct tbox_transition_state *next;
} tbox_transition_state;

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
    struct tbox_scroll_state *next;
} tbox_scroll_state;

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
    tbox_css_stylesheet *internal_stylesheet; /* owned, same lifecycle as `stylesheet` -- NULL se o documento não tem nenhum <style>; concatenação de todo <style> encontrado na árvore, mesma origem TBOX_CSS_ORIGIN_AUTHOR que `stylesheet` em tbox_context_run_frame */
    tbox_font_face_cache *fonts;              /* borrowed -- built/destroyed by the caller, never by tbox_context (was a single tbox_font_face) */
    tbox_image_cache *images;                 /* borrowed, same lifecycle stance as `fonts` above -- may be NULL ("no images", see <tbox/image.h>) */
    tbox_layout_box *root;                    /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;                   /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;                 /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_on_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;                     /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
    int next_handler_id;                      /* monotonic counter for tbox_context_on_click's returned handle -- never reused, even after tbox_context_unbind_click removes a binding */
    const tbox_html_node *hovered_node;       /* the node currently under the pointer, or NULL -- a plain struct field with its own lifetime, deliberately NOT part of frame_arena (must survive every tbox_context_run_frame's arena reset so tbox_context_update_hover can compare across frames; see <tbox/context.h>) */
    const tbox_html_node *active_node;        /* pointer-pressed node, retained until release for :active */
    const tbox_html_node *focused_node;
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
    bool has_search_input;
    const tbox_html_node *range_drag_node;
    const tbox_html_node *popup_highlight;
    size_t popup_first;
    double viewport_width;
    double viewport_height;
    tbox_context_select_handler select_handler;
    void *select_userdata;
    tbox_scroll_state *scroll_states;
    const tbox_html_node *scroll_drag_node;
    double scroll_drag_grab_y;
    bool scroll_pointer_consumed;
    bool focus_scroll_pending;
    tbox_context_input_handler input_handler;
    void *input_userdata;
    tbox_context_submit_handler submit_handler;
    void *submit_userdata;
    tbox_style_table styles; /* last frame's styles, for focus visibility checks */
    tbox_transition_state *transitions;
    double animation_time;
    bool animations_active;
    bool prefers_reduced_motion;
};

void tbox_context_apply_transitions(tbox_context *ctx);
void tbox_context_register_font_faces(tbox_font_face_cache *fonts, const tbox_css_stylesheet *sheet, const char *base_dir);

double tbox_context_style_line_height(const tbox_style *style, const tbox_font_face *face);
bool tbox_context_is_textarea(const tbox_html_node *node);
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
bool tbox_context_calendar_selection_allowed(tbox_context *ctx);
void tbox_context_date_open(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_date_commit(tbox_context *ctx, const tbox_html_node *node, tbox_date date);
void tbox_context_sanitize_inputs(tbox_context *ctx, tbox_html_node *node);
void tbox_context_capture_form_defaults(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_reset_form(tbox_context *ctx, const tbox_html_node *reset);
bool tbox_context_submit_form(tbox_context *ctx, const tbox_html_node *activator, const tbox_html_node *submitter);
bool tbox_context_option_disabled(const tbox_html_node *option);
size_t tbox_context_option_count(const tbox_html_node *select);
tbox_select_field *tbox_context_select_field(tbox_context *ctx, const tbox_html_node *node);
const tbox_layout_box *tbox_context_find_box(const tbox_layout_box *box, const tbox_html_node *node);
void tbox_context_sync_select_boxes(tbox_context *ctx, tbox_layout_box *box);
bool tbox_context_color_popup_visible(tbox_context *ctx);
bool tbox_context_color_popup_contains(tbox_context *ctx, double x, double y);
bool tbox_context_color_choose_point(tbox_context *ctx, double x, double y);
bool tbox_context_date_popup_visible(tbox_context *ctx);
bool tbox_context_date_popup_contains(tbox_context *ctx, double x, double y);
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
void tbox_context_push_fill(tbox_vector *items, tbox_rect rect, tbox_css_rgba color, bool has_clip, tbox_rect clip);
void tbox_context_paint_select_arrows(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_number_steppers(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_search_clears(const tbox_layout_box *box, tbox_vector *items);
void tbox_context_paint_ranges(const tbox_layout_box *box, tbox_vector *items);
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
bool tbox_context_popup_click(tbox_context *ctx, double x, double y);
bool tbox_context_node_attached(const tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_is_text_input(const tbox_html_node *node);
bool tbox_context_is_search_input(const tbox_html_node *node);
bool tbox_context_is_password_input(const tbox_html_node *node);
double tbox_context_input_advance(const tbox_html_node *node, const tbox_font_face *face, tbox_string_view text, double letter_spacing);
bool tbox_context_is_number_input(const tbox_html_node *node);
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
size_t tbox_utf8_next(const char *data, size_t length, size_t at);
size_t tbox_utf8_previous(const char *data, size_t at);
size_t tbox_context_cursor_at_x(const tbox_text_field *field, const tbox_layout_box *box, const tbox_font_face *face, double x);
size_t tbox_context_cursor_at_point(const tbox_text_field *field, const tbox_layout_box *box, const tbox_font_face *face, double x, double y);
bool tbox_context_focusable(const tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_set_focus(tbox_context *ctx, const tbox_html_node *node);
bool tbox_context_move_focus(tbox_context *ctx, bool reverse);
bool tbox_context_dispatch_select_key(tbox_context *ctx, tbox_key key);

const char *tbox_context_default_control_css(void);
tbox_style tbox_context_control_style(tbox_context *ctx, const tbox_html_node *owner, const char *part, const char *state);
double tbox_context_control_size(tbox_style_length length, double fallback);
void tbox_context_paint_control_box(tbox_vector *items, tbox_rect rect, const tbox_style *style, bool has_clip, tbox_rect clip);
const tbox_font_face *tbox_context_control_font(tbox_context *ctx, const tbox_style *style);

#endif /* TBOX_CONTEXT_INTERNAL_H */
