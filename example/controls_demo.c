#include <stdio.h>
#include <string.h>
#include <tbox/tbox.h>

/* Driving controls from code: a list that grows and shrinks, a checkbox
 * that disables it, and a button whose caption follows the state. */
static const char *demo_html =
    "<body><h1>Controls from code</h1>"
    "<p><input id='name' placeholder='New fruit'> <button id='add'>Add</button> <button id='remove'>Remove selected</button></p>"
    "<p><select id='fruits'><option value='apple'>Apple</option><option value='pear'>Pear</option></select></p>"
    "<p><label><input id='lock' type='checkbox'> Lock the list</label></p>"
    "<p id='status'>2 fruits</p></body>";
static const char *demo_css = "body { margin: 24px; font-family: sans-serif; } select { min-width: 160px; }";

static tbox_html_node *by_id(tbox_app *app, const char *id) {
    return tbox_app_get_element_by_id(app, tbox_string_view_make(id, strlen(id)));
}

static void show_count(tbox_app *app) {
    char text[48];
    size_t count = tbox_select_option_count(by_id(app, "fruits"));
    snprintf(text, sizeof(text), "%zu fruit%s", count, count == 1 ? "" : "s");
    tbox_control_set_label(app, by_id(app, "status"), tbox_string_view_make(text, strlen(text)));
}

static bool on_add(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    (void)userdata;
    tbox_html_node *name = by_id(app, "name");
    tbox_string_view label = tbox_control_value(app, name);
    if (label.size == 0) return false;
    tbox_html_node *fruits = by_id(app, "fruits");
    tbox_select_add_option(app, fruits, tbox_string_view_make(NULL, 0), label, SIZE_MAX);
    tbox_select_set_selected_index(app, fruits, tbox_select_option_count(fruits) - 1);
    tbox_control_set_value(app, name, tbox_string_view_make("", 0));
    tbox_control_focus(app, name);
    show_count(app);
    return false;
}

static bool on_remove(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    (void)userdata;
    tbox_html_node *fruits = by_id(app, "fruits");
    ptrdiff_t index        = tbox_select_selected_index(app, fruits);
    if (index >= 0) tbox_select_remove_option(app, fruits, (size_t)index);
    show_count(app);
    return false;
}

static bool on_lock(tbox_app *app, tbox_html_node *lock, void *userdata) {
    (void)userdata;
    bool locked       = tbox_control_checked(lock);
    tbox_control_set_disabled(app, by_id(app, "fruits"), locked);
    tbox_control_set_disabled(app, by_id(app, "add"), locked);
    tbox_control_set_disabled(app, by_id(app, "remove"), locked);
    const char *caption = locked ? "Add (locked)" : "Add";
    tbox_control_set_label(app, by_id(app, "add"), tbox_string_view_make(caption, strlen(caption)));
    return true;
}

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create(demo_html, demo_css, 640, 360);
    if (app == NULL) return 1;
    tbox_app_on_click(app, "#add", 4, on_add, NULL, NULL);
    tbox_app_on_click(app, "#remove", 7, on_remove, NULL, NULL);
    tbox_app_on_click(app, "#lock", 5, on_lock, NULL, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
