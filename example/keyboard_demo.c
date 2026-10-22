#include <tbox/tbox.h>

#include <stdio.h>
#include <string.h>

#ifndef TBOX_KEYBOARD_HTML_PATH
#error "TBOX_KEYBOARD_HTML_PATH is required"
#endif
#ifndef TBOX_KEYBOARD_CSS_PATH
#error "TBOX_KEYBOARD_CSS_PATH is required"
#endif

/* Every control in keyboard_demo.html with its default look; the only
 * application code is a log line showing the last event. */

static tbox_string_view text(const char *cstr) {
    return tbox_string_view_make(cstr, strlen(cstr));
}

static tbox_string_view attribute(const tbox_html_node *node, const char *name) {
    const tbox_html_attribute *found = tbox_html_node_get_attribute(node, text(name));
    return found != NULL ? found->value : tbox_string_view_make("", 0);
}

/* Writes "event #id: detail" (or "event: detail" without an id) into #log. */
static void log_event(tbox_app *app, const char *event, const tbox_html_node *node, tbox_string_view detail) {
    tbox_string_view id = attribute(node, "id");
    char line[200];
    int length = snprintf(line, sizeof(line), "%s%s%.*s: %.*s", event, id.size > 0 ? " #" : "", (int)id.size, id.data,
                          (int)(detail.size < 120 ? detail.size : 120), detail.data);
    if (length < 0) return;
    size_t size = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
    tbox_control_set_label(app, tbox_app_query(app, "#log", 4), tbox_string_view_make(line, size));
}

static bool on_click(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_string_view type = attribute(node, "type");
    if (tbox_string_view_equal_cstr(type, "checkbox"))
        log_event(app, "click", node, text(tbox_control_checked(node) ? "checked" : "unchecked"));
    else if (tbox_string_view_equal_cstr(type, "radio"))
        log_event(app, "click", node, tbox_control_value(app, node));
    else
        log_event(app, "click", node, tbox_control_label(app, node));
    return true;
}

static bool is_valid(const tbox_html_node *input, tbox_string_view type) {
    if (tbox_string_view_equal_cstr(type, "email")) return tbox_context_email_valid(input);
    if (tbox_string_view_equal_cstr(type, "url")) return tbox_context_url_valid(input);
    if (tbox_string_view_equal_cstr(type, "number")) return tbox_context_number_valid(input);
    return true;
}

static void on_input(tbox_app *app, tbox_html_node *input, const char *value, size_t value_length, void *userdata) {
    (void)userdata;
    tbox_string_view type = attribute(input, "type");
    if (tbox_string_view_equal_cstr(type, "password")) {
        log_event(app, "input", input, text("(hidden)"));
        return;
    }
    if (tbox_string_view_equal_cstr(type, "file")) {
        log_event(app, "input", input, tbox_context_file_path(tbox_app_context(app), input));
        return;
    }
    char detail[160];
    int length = snprintf(detail, sizeof(detail), "%.*s%s", (int)(value_length < 120 ? value_length : 120), value,
                          is_valid(input, type) ? "" : "  (invalid)");
    if (length > 0) log_event(app, "input", input, text(detail));
}

static void on_select(tbox_app *app, tbox_html_node *select, const char *value, size_t value_length, void *userdata) {
    (void)userdata;
    log_event(app, "select", select, tbox_string_view_make(value, value_length));
}

static void on_submit(tbox_app *app, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    (void)userdata;
    log_event(app, "submit", form, text(submitter != NULL ? "with the button" : "with Enter"));
}

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create_from_files(TBOX_KEYBOARD_HTML_PATH, TBOX_KEYBOARD_CSS_PATH, 640, 760);
    if (app == NULL) {
        fputs("Could not create the window\n", stderr);
        return 1;
    }
    const char *clickable = "button, input[type=button], input[type=image], input[type=checkbox], input[type=radio]";
    tbox_app_on_click(app, clickable, strlen(clickable), on_click, NULL, NULL);
    tbox_app_on_input(app, on_input, NULL, NULL);
    tbox_app_on_select(app, on_select, NULL, NULL);
    tbox_app_on_submit(app, on_submit, NULL, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
