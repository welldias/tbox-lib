#include <tbox/tbox.h>

#include <stdio.h>
#include <string.h>

#ifndef TBOX_KEYBOARD_HTML_PATH
#error "TBOX_KEYBOARD_HTML_PATH is required"
#endif
#ifndef TBOX_KEYBOARD_CSS_PATH
#error "TBOX_KEYBOARD_CSS_PATH is required"
#endif

static bool on_button(tbox_context *ctx, tbox_html_node *button, void *userdata) {
    (void)userdata;
    const tbox_html_attribute *id = tbox_html_node_get_attribute(button, tbox_string_view_make("id", 2));
    const char *message = id != NULL && id->value.size == 6 && memcmp(id->value.data, "second", 6) == 0
                              ? "Second button activated"
                              : id != NULL && id->value.size >= 4 && memcmp(id->value.data, "row-", 4) == 0
                                  ? "List item activated"
                                  : "First button activated";
    tbox_css_selector_node_set matches = tbox_css_selector_select(
        tbox_html_document_root(tbox_context_document(ctx)), "#status", 7);
    if (matches.count > 0) {
        tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                                        tbox_string_view_make(message, strlen(message)));
    }
    tbox_css_selector_node_set_destroy(&matches);
    return true;
}

static void on_input(tbox_context *ctx, tbox_html_node *input, tbox_string_view value, void *userdata) {
    (void)ctx;
    (void)input;
    (void)userdata;
    fputs("Input: ", stdout);
    fwrite(value.data, 1, value.size, stdout);
    fputc('\n', stdout);
}

static void on_select(tbox_context *ctx, tbox_html_node *select, tbox_string_view value, void *userdata) {
    (void)select;
    (void)userdata;
    char message[128];
    int written = snprintf(message, sizeof(message), "Selected: %.*s",
                           (int)(value.size < 100 ? value.size : 100), value.data != NULL ? value.data : "");
    if (written < 0) return;
    tbox_css_selector_node_set matches = tbox_css_selector_select(
        tbox_html_document_root(tbox_context_document(ctx)), "#choice", 7);
    if (matches.count > 0) {
        size_t length = (size_t)written < sizeof(message) ? (size_t)written : sizeof(message) - 1;
        tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                                        tbox_string_view_make(message, length));
    }
    tbox_css_selector_node_set_destroy(&matches);
}

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create_from_files(TBOX_KEYBOARD_HTML_PATH, TBOX_KEYBOARD_CSS_PATH, 460, 800);
    if (app == NULL) {
        fputs("Could not create the window\n", stderr);
        return 1;
    }
    tbox_context_on_click(tbox_app_context(app), "button", 6, on_button, NULL);
    tbox_context_on_input(tbox_app_context(app), on_input, NULL);
    tbox_context_on_select(tbox_app_context(app), on_select, NULL);
    while (!tbox_app_should_close(app)) {
        tbox_app_step(app);
    }
    tbox_app_close(app);
    return 0;
}
