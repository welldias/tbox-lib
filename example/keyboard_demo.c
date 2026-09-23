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

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create_from_files(TBOX_KEYBOARD_HTML_PATH, TBOX_KEYBOARD_CSS_PATH, 460, 260);
    if (app == NULL) {
        fputs("Could not create the window\n", stderr);
        return 1;
    }
    tbox_context_on_click(tbox_app_context(app), "button", 6, on_button, NULL);
    while (!tbox_app_should_close(app)) {
        tbox_app_step(app);
    }
    tbox_app_close(app);
    return 0;
}
