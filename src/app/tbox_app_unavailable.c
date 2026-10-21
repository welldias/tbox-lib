#include <tbox/app.h>

#include <stddef.h>

/* Font discovery is unavailable in this build. Keep the public application
 * symbols linkable so callers can check capabilities at runtime. */
bool tbox_app_backend_available(void) { return false; }

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    (void)html; (void)css; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_with_options(const char *html, const char *css, int32_t width, int32_t height, const tbox_context_options *options) {
    (void)html; (void)css; (void)width; (void)height; (void)options;
    return NULL;
}

tbox_app *tbox_app_create_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height) {
    (void)html_path; (void)css_path; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_from_files_with_options(const char *html_path, const char *css_path, int32_t width, int32_t height, const tbox_context_options *options) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)options;
    return NULL;
}

bool tbox_app_load_from_files(tbox_app *app, const char *html_path, const char *css_path) {
    (void)app; (void)html_path; (void)css_path;
    return false;
}

void tbox_app_on_key(tbox_app *app, tbox_app_key_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app; (void)handler; (void)userdata; (void)destroy;
}

bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)png_path;
    return false;
}
bool tbox_app_screenshot_from_files_with_options(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path, const tbox_context_options *options) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)png_path; (void)options;
    return false;
}

tbox_context *tbox_app_context(tbox_app *app) { (void)app; return NULL; }
void tbox_app_request_redraw(tbox_app *app) { (void)app; }
void tbox_app_set_debug_damage(tbox_app *app, bool enabled) { (void)app; (void)enabled; }
void tbox_app_step(tbox_app *app) { (void)app; }
void tbox_app_wait(tbox_app *app, int32_t max_wait_ms) { (void)app; (void)max_wait_ms; }
void tbox_app_run(tbox_app *app) { (void)app; }
bool tbox_app_post(tbox_app *app, tbox_app_task task, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)task; (void)userdata; (void)destroy; return false; }
bool tbox_app_should_close(const tbox_app *app) { (void)app; return true; }
void tbox_app_close(tbox_app *app) { (void)app; }

void tbox_app_on_load(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_unload(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_pagehide(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_pageshow(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_resize(tbox_app *app, tbox_app_resize_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_error(tbox_app *app, tbox_app_error_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }

int tbox_app_on_click(tbox_app *app, const char *selector, size_t selector_length, tbox_app_click_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app; (void)selector; (void)selector_length; (void)handler; (void)userdata; (void)destroy;
    return -1;
}
bool tbox_app_unbind_click(tbox_app *app, int binding) {
    (void)app; (void)binding;
    return false;
}
void tbox_app_on_input(tbox_app *app, tbox_app_input_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app; (void)handler; (void)userdata; (void)destroy;
}
void tbox_app_on_select(tbox_app *app, tbox_app_select_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app; (void)handler; (void)userdata; (void)destroy;
}
void tbox_app_on_submit(tbox_app *app, tbox_app_submit_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app; (void)handler; (void)userdata; (void)destroy;
}
