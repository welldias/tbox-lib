/* Needed for nanosleep/struct timespec under the project's strict C23. */
#define _POSIX_C_SOURCE 200809L

#include <tbox/tbox.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* tbox_app_post: a worker thread does slow work and hands each progress
 * step to the UI thread, the only one that touches the document. The loop
 * is tbox_app_run, which sleeps between posts and only wakes per display
 * frame while the bar's width transition runs. Closing the window early
 * stops the worker; one tbox_app_step after the join runs (and frees)
 * whatever it posted meanwhile. */

static const char *demo_html = "<body><h1>Worker thread</h1><div id='track'><div id='bar'></div></div><p id='label'>starting...</p></body>";
static const char *demo_css  = "body { margin: 24px; font-family: sans-serif; }"
                               "#track { width: 300px; height: 16px; background: #ddd; }"
                               "#bar { width: 0px; height: 16px; background: #3a7; transition: width 0.3s ease-out; }";

typedef struct worker {
    tbox_app *app;
    atomic_bool stop;
} worker;

static tbox_html_node *find_node(tbox_context *ctx, const char *selector) {
    tbox_css_selector_node_set set = tbox_css_selector_select(tbox_html_document_root(tbox_context_document(ctx)), selector, strlen(selector));
    tbox_html_node *node           = set.count > 0 ? (tbox_html_node *)set.items[0] : NULL;
    tbox_css_selector_node_set_destroy(&set);
    return node;
}

/* Runs on the UI thread, inside tbox_app_run. */
static void show_progress(tbox_app *app, void *userdata) {
    int *percent              = userdata;
    tbox_context *ctx         = tbox_app_context(app);
    tbox_html_document *doc   = tbox_context_document(ctx);
    tbox_html_node *bar       = find_node(ctx, "#bar");
    tbox_html_node *label     = find_node(ctx, "#label");
    char style[48], text[48];
    snprintf(style, sizeof(style), "width: %dpx", *percent * 3);
    if (*percent < 100)
        snprintf(text, sizeof(text), "%d%%", *percent);
    else
        snprintf(text, sizeof(text), "done");
    if (bar != NULL)
        tbox_html_node_set_attribute(doc, bar, tbox_string_view_make("style", 5), tbox_string_view_make(style, strlen(style)));
    if (label != NULL)
        tbox_html_node_set_text_content(doc, label, tbox_string_view_make(text, strlen(text)));
    free(percent);
}

static void *work(void *arg) {
    worker *self = arg;
    for (int step = 1; step <= 10 && !atomic_load(&self->stop); step++) {
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 400L * 1000L * 1000L };
        nanosleep(&delay, NULL);
        int *percent = malloc(sizeof(*percent));
        if (percent == NULL)
            continue;
        *percent = step * 10;
        if (!tbox_app_post(self->app, show_progress, percent))
            free(percent);
    }
    return NULL;
}

int main(void) {
    tbox_app *app = tbox_app_create(demo_html, demo_css, 360, 160);
    if (app == NULL) {
        fputs("Could not create the example window.\n", stderr);
        return 1;
    }
    worker self = { .app = app };
    atomic_init(&self.stop, false);
    pthread_t thread;
    if (pthread_create(&thread, NULL, work, &self) != 0) {
        tbox_app_close(app);
        return 1;
    }
    tbox_app_run(app);
    atomic_store(&self.stop, true);
    pthread_join(thread, NULL);
    tbox_app_step(app); /* runs what the worker posted after the window closed */
    tbox_app_close(app);
    return 0;
}
