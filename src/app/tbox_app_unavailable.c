#include <tbox/app.h>

#include <stddef.h>

/* Font discovery is unavailable in this build. Keep the public application
 * symbols linkable so callers can check capabilities at runtime. */
bool tbox_app_backend_available(void) { return false; }

tbox_app *tbox_app_create(const char *html, const char *css, int32_t width, int32_t height) {
    (void)html; (void)css; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_with_config(const char *html, const char *css, int32_t width, int32_t height, tbox_ua_style_config config) {
    (void)html; (void)css; (void)width; (void)height; (void)config;
    return NULL;
}

tbox_app *tbox_app_create_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height) {
    (void)html_path; (void)css_path; (void)width; (void)height;
    return NULL;
}

tbox_app *tbox_app_create_from_files_with_config(const char *html_path, const char *css_path, int32_t width, int32_t height, tbox_ua_style_config config) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)config;
    return NULL;
}

bool tbox_app_screenshot_from_files(const char *html_path, const char *css_path, int32_t width, int32_t height, const char *png_path) {
    (void)html_path; (void)css_path; (void)width; (void)height; (void)png_path;
    return false;
}

tbox_context *tbox_app_context(tbox_app *app) { (void)app; return NULL; }
void tbox_app_request_redraw(tbox_app *app) { (void)app; }
void tbox_app_step(tbox_app *app) { (void)app; }
bool tbox_app_should_close(const tbox_app *app) { (void)app; return true; }
void tbox_app_close(tbox_app *app) { (void)app; }
