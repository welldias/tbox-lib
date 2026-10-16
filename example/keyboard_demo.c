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
                              : id != NULL && id->value.size == 12 && memcmp(id->value.data, "input-button", 12) == 0
                                  ? "Input button activated"
                              : id != NULL && id->value.size == 12 && memcmp(id->value.data, "image-button", 12) == 0
                                  ? "Image button activated"
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

static void on_input(tbox_context *ctx, tbox_html_node *input, const char *value, size_t value_length, void *userdata) {
    (void)userdata;
    const tbox_html_attribute *id = tbox_html_node_get_attribute(input, tbox_string_view_make("id", 2));
    if (id != NULL && id->value.size == 16 && memcmp(id->value.data, "account-password", 16) == 0) {
        char message[40];
        int written = snprintf(message, sizeof(message), "Password length: %zu", value_length);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#password-status", 16);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
        return;
    }
    if (id != NULL && id->value.size == 14 && memcmp(id->value.data, "favorite-color", 14) == 0) {
        char message[32];
        int written = snprintf(message, sizeof(message), "Color: %.*s", (int)value_length, value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#color-status", 13);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
    }
    if (id != NULL && id->value.size == 12 && memcmp(id->value.data, "meeting-date", 12) == 0) {
        char message[32];
        int written = snprintf(message, sizeof(message), "Date: %.*s", (int)value_length, value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#date-status", 12);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
    }
    if (id != NULL && id->value.size == 12 && memcmp(id->value.data, "meeting-time", 12) == 0) {
        char message[40];
        int written = snprintf(message, sizeof(message), "Date and time: %.*s", (int)value_length, value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#time-status", 12);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
    }
    if (id != NULL && id->value.size == 13 && memcmp(id->value.data, "billing-month", 13) == 0) {
        char message[32];
        int written = snprintf(message, sizeof(message), "Month: %.*s", (int)value_length, value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#month-status", 13);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
    }
    if (id != NULL && id->value.size == 13 && memcmp(id->value.data, "contact-email", 13) == 0) {
        const char *message = tbox_context_email_valid(input) ? "Email: valid" : "Email: invalid";
        tbox_css_selector_node_set matches = tbox_css_selector_select(
            tbox_html_document_root(tbox_context_document(ctx)), "#email-status", 13);
        if (matches.count > 0)
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                tbox_string_view_make(message, strlen(message)));
        tbox_css_selector_node_set_destroy(&matches);
    }
    if (id != NULL && id->value.size == 8 && memcmp(id->value.data, "quantity", 8) == 0) {
        const char *message = tbox_context_number_valid(input) ? "Number: valid" : "Number: invalid";
        tbox_css_selector_node_set matches = tbox_css_selector_select(
            tbox_html_document_root(tbox_context_document(ctx)), "#number-status", 14);
        if (matches.count > 0)
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                tbox_string_view_make(message, strlen(message)));
        tbox_css_selector_node_set_destroy(&matches);
    }
    if (id != NULL && id->value.size == 11 && memcmp(id->value.data, "contact-url", 11) == 0) {
        const char *message = tbox_context_url_valid(input) ? "URL: valid" : "URL: invalid";
        tbox_css_selector_node_set matches = tbox_css_selector_select(
            tbox_html_document_root(tbox_context_document(ctx)), "#url-status", 11);
        if (matches.count > 0)
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                tbox_string_view_make(message, strlen(message)));
        tbox_css_selector_node_set_destroy(&matches);
    }
    if (id != NULL && id->value.size == 12 && memcmp(id->value.data, "volume-range", 12) == 0) {
        char message[32];
        int written = snprintf(message, sizeof(message), "Range: %.*s", (int)value_length, value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#range-status", 13);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
    }
    if (id != NULL && id->value.size == 10 && memcmp(id->value.data, "attachment", 10) == 0) {
        char message[160];
        int written = snprintf(message, sizeof(message), "File: %.*s",
            (int)(value_length < 140 ? value_length : 140), value);
        if (written > 0 && (size_t)written < sizeof(message)) {
            tbox_css_selector_node_set matches = tbox_css_selector_select(
                tbox_html_document_root(tbox_context_document(ctx)), "#file-status", 12);
            if (matches.count > 0)
                tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                    tbox_string_view_make(message, (size_t)written));
            tbox_css_selector_node_set_destroy(&matches);
        }
        tbox_string_view path = tbox_context_file_path(ctx, input);
        if (path.size > 0) printf("Selected path: %.*s\n", (int)path.size, path.data);
    }
    fputs("Input: ", stdout);
    fwrite(value, 1, value_length, stdout);
    fputc('\n', stdout);
}

static bool on_checkbox(tbox_context *ctx, tbox_html_node *checkbox, void *userdata) {
    (void)userdata;
    const bool checked = tbox_html_node_get_attribute(checkbox, tbox_string_view_make("checked", 7)) != NULL;
    const char *message = checked ? "Checkbox: checked" : "Checkbox: unchecked";
    tbox_css_selector_node_set matches = tbox_css_selector_select(
        tbox_html_document_root(tbox_context_document(ctx)), "#toggle-status", 14);
    if (matches.count > 0)
        tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                                        tbox_string_view_make(message, strlen(message)));
    tbox_css_selector_node_set_destroy(&matches);
    return true;
}

static bool on_radio(tbox_context *ctx, tbox_html_node *radio, void *userdata) {
    (void)userdata;
    const tbox_html_attribute *value = tbox_html_node_get_attribute(radio, tbox_string_view_make("value", 5));
    char message[40];
    int written = snprintf(message, sizeof(message), "Radio: %.*s",
        value != NULL ? (int)value->value.size : 0, value != NULL ? value->value.data : "");
    if (written > 0 && (size_t)written < sizeof(message)) {
        tbox_css_selector_node_set matches = tbox_css_selector_select(
            tbox_html_document_root(tbox_context_document(ctx)), "#radio-status", 13);
        if (matches.count > 0)
            tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
                tbox_string_view_make(message, (size_t)written));
        tbox_css_selector_node_set_destroy(&matches);
    }
    return true;
}

static void on_select(tbox_context *ctx, tbox_html_node *select, const char *value, size_t value_length, void *userdata) {
    (void)select;
    (void)userdata;
    char message[128];
    int written = snprintf(message, sizeof(message), "Selected: %.*s",
                           (int)(value_length < 100 ? value_length : 100), value != NULL ? value : "");
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

static void on_submit(tbox_context *ctx, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    (void)form;
    (void)userdata;
    const char *message = submitter != NULL ? "Submitted with button." : "Submitted with Enter.";
    tbox_css_selector_node_set matches = tbox_css_selector_select(
        tbox_html_document_root(tbox_context_document(ctx)), "#submit-status", 14);
    if (matches.count > 0)
        tbox_html_node_set_text_content(tbox_context_document(ctx), (tbox_html_node *)matches.items[0],
            tbox_string_view_make(message, strlen(message)));
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
    tbox_context_on_click(tbox_app_context(app), "button", 6, on_button, NULL, NULL);
    tbox_context_on_click(tbox_app_context(app), "input[type=button]", 18, on_button, NULL, NULL);
    tbox_context_on_click(tbox_app_context(app), "input[type=image]", 17, on_button, NULL, NULL);
    tbox_context_on_click(tbox_app_context(app), "input[type=checkbox]", 20, on_checkbox, NULL, NULL);
    tbox_context_on_click(tbox_app_context(app), "input[type=radio]", 17, on_radio, NULL, NULL);
    tbox_context_on_input(tbox_app_context(app), on_input, NULL, NULL);
    tbox_context_on_submit(tbox_app_context(app), on_submit, NULL, NULL);
    tbox_context_on_select(tbox_app_context(app), on_select, NULL, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
