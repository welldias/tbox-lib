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
