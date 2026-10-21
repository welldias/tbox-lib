#include <stdio.h>
#include <string.h>
#include <tbox/tbox.h>

/* Lifecycle handlers are owned by the application and survive document
 * replacement. Register them before entering the loop to receive load. */
static void show_status(tbox_app *app, const char *text) {
    tbox_html_document *doc          = tbox_context_document(tbox_app_context(app));
    tbox_css_selector_node_set nodes = tbox_css_selector_select(tbox_html_document_root(doc), "#status", 7);
    if (nodes.count > 0)
        tbox_html_node_set_text_content(doc, (tbox_html_node *)nodes.items[0], tbox_string_view_make(text, strlen(text)));
    tbox_css_selector_node_set_destroy(&nodes);
}
static void loaded(tbox_app *app, void *userdata) {
    (void)userdata;
    show_status(app, "Page loaded. Switch windows or resize this window.");
    puts("load");
}
static void hidden(tbox_app *app, void *userdata) {
    (void)userdata;
    show_status(app, "Window lost keyboard focus.");
    puts("pagehide");
}
static void shown(tbox_app *app, void *userdata) {
    (void)userdata;
    show_status(app, "Window has keyboard focus.");
    puts("pageshow");
}
static void unloading(tbox_app *app, void *userdata) {
    (void)app;
    (void)userdata;
    puts("unload: window is closing");
}
static void resized(tbox_app *app, const tbox_app_resize_event *event, void *userdata) {
    (void)userdata;
    char text[128];
    snprintf(text, sizeof(text), "Resized from %d x %d to %d x %d.", (int)event->previous_width, (int)event->previous_height, (int)event->width, (int)event->height);
    show_status(app, text);
    puts(text);
}
static void error(tbox_app *app, const tbox_app_error_event *event, void *userdata) {
    (void)app;
    (void)userdata;
    /* Reserved callback: tbox does not emit errors yet. */
    fprintf(stderr, "error: %.*s\n", (int)event->message.size, event->message.data);
}
int main(void) {
    const char *html = "<body><h1>Window lifecycle</h1><p id='status'>Waiting for load...</p><p>Press Escape to close.</p></body>";
    const char *css  = "body{margin:24px;font:16px/24px sans-serif;background:#f5f7fb;color:#24304a}h1{font-size:24px}";
    tbox_app *app    = tbox_app_create(html, css, 640, 240);
    if (app == NULL) {
        fputs("Could not open a window; this example needs Fontconfig and Wayland.\n", stderr);
        return 1;
    }
    tbox_app_on_load(app, loaded, NULL, NULL);
    tbox_app_on_unload(app, unloading, NULL, NULL);
    tbox_app_on_pagehide(app, hidden, NULL, NULL);
    tbox_app_on_pageshow(app, shown, NULL, NULL);
    tbox_app_on_resize(app, resized, NULL, NULL);
    tbox_app_on_error(app, error, NULL, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
