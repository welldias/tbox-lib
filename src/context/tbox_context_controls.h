#ifndef TBOX_CONTEXT_CONTROLS_H
#define TBOX_CONTEXT_CONTROLS_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/context.h>
#include <tbox/css_selector.h>

/* Private implementation of <tbox/controls.h> and of the tbox_app_query
 * lookups in <tbox/app.h>: the same functions on the context, used by the
 * app adapters (src/app/tbox_app_controls.c) and the context tests.
 * Semantics are documented in the public headers. */
tbox_html_node *tbox_context_get_element_by_id(tbox_context *ctx, tbox_string_view id);
tbox_html_node *tbox_context_query(tbox_context *ctx, const char *selector, size_t length);
tbox_css_selector_node_set tbox_context_query_all(tbox_context *ctx, const char *selector, size_t length);

tbox_string_view tbox_context_control_value(tbox_context *ctx, const tbox_html_node *control);
bool tbox_context_control_set_value(tbox_context *ctx, tbox_html_node *control, tbox_string_view value);
tbox_string_view tbox_context_control_label(tbox_context *ctx, const tbox_html_node *control);
bool tbox_context_control_set_label(tbox_context *ctx, tbox_html_node *control, tbox_string_view text);
bool tbox_context_control_disabled(const tbox_html_node *control);
void tbox_context_control_set_disabled(tbox_context *ctx, tbox_html_node *control, bool disabled);
bool tbox_context_control_focus(tbox_context *ctx, tbox_html_node *control);
void tbox_context_control_blur(tbox_context *ctx);
bool tbox_context_control_checked(const tbox_html_node *control);
bool tbox_context_control_set_checked(tbox_context *ctx, tbox_html_node *control, bool checked);
tbox_html_node *tbox_context_control_radio_checked(tbox_context *ctx, const tbox_html_node *radio);
size_t tbox_context_select_option_count(const tbox_html_node *select);
tbox_html_node *tbox_context_select_option_at(const tbox_html_node *select, size_t index);
ptrdiff_t tbox_context_select_selected_index(tbox_context *ctx, const tbox_html_node *select);
bool tbox_context_select_set_selected_index(tbox_context *ctx, tbox_html_node *select, size_t index);
tbox_html_node *tbox_context_select_add_option(tbox_context *ctx, tbox_html_node *select, tbox_string_view value,
                                               tbox_string_view label, size_t before_index);
bool tbox_context_select_remove_option(tbox_context *ctx, tbox_html_node *select, size_t index);
void tbox_context_select_clear(tbox_context *ctx, tbox_html_node *select);

#endif
