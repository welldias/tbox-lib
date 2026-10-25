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

size_t tbox_context_table_row_count(const tbox_html_node *table);
tbox_html_node *tbox_context_table_row_at(const tbox_html_node *table, size_t index);
tbox_html_node *tbox_context_table_insert_row(tbox_context *ctx, tbox_html_node *table, size_t before_index,
                                              const tbox_string_view *cells, size_t count);
bool tbox_context_table_remove_row(tbox_context *ctx, tbox_html_node *table, size_t index);
void tbox_context_table_clear_body(tbox_context *ctx, tbox_html_node *table);
tbox_html_node *tbox_context_table_head(const tbox_html_node *table);
tbox_html_node *tbox_context_table_foot(const tbox_html_node *table);
size_t tbox_context_table_cell_count(const tbox_html_node *row);
tbox_html_node *tbox_context_table_cell_at(const tbox_html_node *row, size_t index);
tbox_html_node *tbox_context_table_insert_cell(tbox_context *ctx, tbox_html_node *row, size_t before_index,
                                               tbox_string_view text);
bool tbox_context_table_remove_cell(tbox_context *ctx, tbox_html_node *row, size_t index);
tbox_string_view tbox_context_table_cell_text(tbox_context *ctx, const tbox_html_node *table, size_t row, size_t column);
bool tbox_context_table_set_cell_text(tbox_context *ctx, tbox_html_node *table, size_t row, size_t column,
                                      tbox_string_view text);
tbox_html_node *tbox_context_table_row_of(const tbox_html_node *node);
tbox_html_node *tbox_context_table_cell_of(const tbox_html_node *node);
ptrdiff_t tbox_context_table_row_index(const tbox_html_node *row);
ptrdiff_t tbox_context_table_cell_index(const tbox_html_node *cell);

#endif
