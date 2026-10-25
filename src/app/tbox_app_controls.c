#include <tbox/app.h>
#include <tbox/controls.h>

#include "context/tbox_context_controls.h"

/* <tbox/controls.h> and the tbox_app_query lookups on the app's current
 * context. Only the public tbox_app_context is needed, so this file also
 * builds next to tbox_app_unavailable.c, where every call sees a NULL
 * context. */

tbox_html_node *tbox_app_get_element_by_id(tbox_app *app, tbox_string_view id) {
    return tbox_context_get_element_by_id(tbox_app_context(app), id);
}

tbox_html_node *tbox_app_query(tbox_app *app, const char *selector, size_t length) {
    return tbox_context_query(tbox_app_context(app), selector, length);
}

tbox_css_selector_node_set tbox_app_query_all(tbox_app *app, const char *selector, size_t length) {
    return tbox_context_query_all(tbox_app_context(app), selector, length);
}

tbox_html_node *tbox_app_query_in(tbox_app *app, const tbox_html_node *root, const char *selector, size_t length) {
    return tbox_context_query_in(tbox_app_context(app), root, selector, length);
}

tbox_string_view tbox_control_value(tbox_app *app, const tbox_html_node *control) {
    return tbox_context_control_value(tbox_app_context(app), control);
}

bool tbox_control_set_value(tbox_app *app, tbox_html_node *control, tbox_string_view value) {
    return tbox_context_control_set_value(tbox_app_context(app), control, value);
}

tbox_string_view tbox_control_label(tbox_app *app, const tbox_html_node *control) {
    return tbox_context_control_label(tbox_app_context(app), control);
}

bool tbox_control_set_label(tbox_app *app, tbox_html_node *control, tbox_string_view text) {
    return tbox_context_control_set_label(tbox_app_context(app), control, text);
}

bool tbox_control_disabled(const tbox_html_node *control) {
    return tbox_context_control_disabled(control);
}

void tbox_control_set_disabled(tbox_app *app, tbox_html_node *control, bool disabled) {
    tbox_context_control_set_disabled(tbox_app_context(app), control, disabled);
}

bool tbox_control_focus(tbox_app *app, tbox_html_node *control) {
    return tbox_context_control_focus(tbox_app_context(app), control);
}

void tbox_control_blur(tbox_app *app) {
    tbox_context_control_blur(tbox_app_context(app));
}

bool tbox_control_checked(const tbox_html_node *control) {
    return tbox_context_control_checked(control);
}

bool tbox_control_set_checked(tbox_app *app, tbox_html_node *control, bool checked) {
    return tbox_context_control_set_checked(tbox_app_context(app), control, checked);
}

tbox_html_node *tbox_control_radio_checked(tbox_app *app, const tbox_html_node *radio) {
    return tbox_context_control_radio_checked(tbox_app_context(app), radio);
}

bool tbox_control_has_class(const tbox_html_node *node, tbox_string_view name) {
    return tbox_context_control_has_class(node, name);
}

bool tbox_control_set_class(tbox_app *app, tbox_html_node *node, tbox_string_view name, bool on) {
    return tbox_context_control_set_class(tbox_app_context(app), node, name, on);
}

bool tbox_control_hidden(const tbox_html_node *node) {
    return tbox_context_control_hidden(node);
}

void tbox_control_set_hidden(tbox_app *app, tbox_html_node *node, bool hidden) {
    tbox_context_control_set_hidden(tbox_app_context(app), node, hidden);
}

tbox_html_node *tbox_control_instantiate(tbox_app *app, const tbox_html_node *templ, tbox_html_node *parent,
                                         size_t before_index) {
    return tbox_context_control_instantiate(tbox_app_context(app), templ, parent, before_index);
}

bool tbox_control_remove(tbox_app *app, tbox_html_node *node) {
    return tbox_context_control_remove(tbox_app_context(app), node);
}

bool tbox_control_clear(tbox_app *app, tbox_html_node *container) {
    return tbox_context_control_clear(tbox_app_context(app), container);
}

size_t tbox_select_option_count(const tbox_html_node *select) {
    return tbox_context_select_option_count(select);
}

tbox_html_node *tbox_select_option_at(const tbox_html_node *select, size_t index) {
    return tbox_context_select_option_at(select, index);
}

ptrdiff_t tbox_select_selected_index(tbox_app *app, const tbox_html_node *select) {
    return tbox_context_select_selected_index(tbox_app_context(app), select);
}

bool tbox_select_set_selected_index(tbox_app *app, tbox_html_node *select, size_t index) {
    return tbox_context_select_set_selected_index(tbox_app_context(app), select, index);
}

tbox_html_node *tbox_select_add_option(tbox_app *app, tbox_html_node *select, tbox_string_view value,
                                       tbox_string_view label, size_t before_index) {
    return tbox_context_select_add_option(tbox_app_context(app), select, value, label, before_index);
}

bool tbox_select_remove_option(tbox_app *app, tbox_html_node *select, size_t index) {
    return tbox_context_select_remove_option(tbox_app_context(app), select, index);
}

void tbox_select_clear(tbox_app *app, tbox_html_node *select) {
    tbox_context_select_clear(tbox_app_context(app), select);
}

size_t tbox_table_row_count(const tbox_html_node *table) {
    return tbox_context_table_row_count(table);
}

tbox_html_node *tbox_table_row_at(const tbox_html_node *table, size_t index) {
    return tbox_context_table_row_at(table, index);
}

tbox_html_node *tbox_table_insert_row(tbox_app *app, tbox_html_node *table, size_t before_index,
                                      const tbox_string_view *cells, size_t count) {
    return tbox_context_table_insert_row(tbox_app_context(app), table, before_index, cells, count);
}

bool tbox_table_remove_row(tbox_app *app, tbox_html_node *table, size_t index) {
    return tbox_context_table_remove_row(tbox_app_context(app), table, index);
}

void tbox_table_clear_body(tbox_app *app, tbox_html_node *table) {
    tbox_context_table_clear_body(tbox_app_context(app), table);
}

tbox_html_node *tbox_table_head(const tbox_html_node *table) {
    return tbox_context_table_head(table);
}

tbox_html_node *tbox_table_foot(const tbox_html_node *table) {
    return tbox_context_table_foot(table);
}

size_t tbox_table_cell_count(const tbox_html_node *row) {
    return tbox_context_table_cell_count(row);
}

tbox_html_node *tbox_table_cell_at(const tbox_html_node *row, size_t index) {
    return tbox_context_table_cell_at(row, index);
}

tbox_html_node *tbox_table_insert_cell(tbox_app *app, tbox_html_node *row, size_t before_index,
                                       tbox_string_view text) {
    return tbox_context_table_insert_cell(tbox_app_context(app), row, before_index, text);
}

bool tbox_table_remove_cell(tbox_app *app, tbox_html_node *row, size_t index) {
    return tbox_context_table_remove_cell(tbox_app_context(app), row, index);
}

tbox_string_view tbox_table_cell_text(tbox_app *app, const tbox_html_node *table, size_t row, size_t column) {
    return tbox_context_table_cell_text(tbox_app_context(app), table, row, column);
}

bool tbox_table_set_cell_text(tbox_app *app, tbox_html_node *table, size_t row, size_t column,
                              tbox_string_view text) {
    return tbox_context_table_set_cell_text(tbox_app_context(app), table, row, column, text);
}

tbox_html_node *tbox_table_row_of(const tbox_html_node *node) {
    return tbox_context_table_row_of(node);
}

tbox_html_node *tbox_table_cell_of(const tbox_html_node *node) {
    return tbox_context_table_cell_of(node);
}

ptrdiff_t tbox_table_row_index(const tbox_html_node *row) {
    return tbox_context_table_row_index(row);
}

ptrdiff_t tbox_table_cell_index(const tbox_html_node *cell) {
    return tbox_context_table_cell_index(cell);
}
