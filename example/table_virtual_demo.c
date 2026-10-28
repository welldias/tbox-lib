/* Needed for nanosleep/struct timespec under the project's strict C23. */
#define _POSIX_C_SOURCE 200809L

#include <tbox/tbox.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A virtual table of a million rows (tbox_table_virtualize): only the rows
 * in view are in the document. The handler marks the rows it is asked for
 * as loading and queues the request; a worker thread "fetches" them (a
 * short sleep stands for a database) and posts the result to the UI
 * thread, which fills the rows still in view. Click a row to see its
 * index; the button jumps to the middle of the table. */

#define DEMO_ROWS 1000000

static const char *demo_html =
    "<body><h1>Virtual table</h1>"
    "<p><button id='middle'>Go to row 500 000</button> <span id='status'>Scroll the table.</span></p>"
    "<div id='scroll'><table id='people'>"
    "<colgroup><col style='width: 90px'><col><col><col style='width: 90px'></colgroup>"
    "<thead><tr><th>#</th><th>Name</th><th>E-mail</th><th>Status</th></tr></thead>"
    "</table></div>"
    "<template id='row'><tr class='loading'><td></td><td></td><td></td><td></td></tr></template></body>";
static const char *demo_css =
    "body { margin: 24px; font-family: sans-serif; }"
    "#scroll { height: 360px; overflow-y: auto; border: 1px solid #c8c8cc; }"
    "table { width: 100%; border-collapse: collapse; table-layout: fixed; }"
    "th, td { border-bottom: 1px solid #e4e4e8; padding: 4px 10px; text-align: left; white-space: nowrap; overflow: hidden; }"
    "thead th { background: #eef0f4; }"
    "tbody tr:nth-child(even) { background: #f8f8fa; }"
    "tbody tr:hover { background: #cfe0ff; }"
    "tr.loading td { color: #999; }";

/* One request for rows [first, first + count), from the handler to the
 * worker and back to the UI thread. */
typedef struct request {
    size_t first, count;
    struct request *next;
} request;

typedef struct demo {
    tbox_app *app;
    tbox_html_node *table;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    request *queue; /* guarded by lock */
    atomic_bool stop;
} demo;

static tbox_html_node *by_id(tbox_app *app, const char *id) {
    return tbox_app_get_element_by_id(app, tbox_string_view_make(id, strlen(id)));
}

static void set_cell(tbox_html_node *row, size_t column, const char *text) {
    size_t index = 0;
    for (tbox_html_node *cell = tbox_html_node_first_element_child(row); cell != NULL; cell = tbox_html_node_next_element_sibling(cell), index++)
        if (index == column) {
            tbox_html_node_set_text_content(cell->owner_document, cell, tbox_string_view_make(text, strlen(text)));
            return;
        }
}

/* UI thread, from the worker: fill the rows of `userdata` still in view. */
static void deliver(tbox_app *app, void *userdata) {
    const request *job    = userdata;
    tbox_html_node *table = by_id(app, "people");
    for (size_t i = job->first; i < job->first + job->count; i++) {
        tbox_html_node *row = tbox_table_virtual_row(app, table, i);
        if (row == NULL)
            continue; /* scrolled away meanwhile: asked for again if it comes back */
        char text[64];
        snprintf(text, sizeof(text), "%zu", i + 1);
        set_cell(row, 0, text);
        snprintf(text, sizeof(text), "Person %zu", i + 1);
        set_cell(row, 1, text);
        snprintf(text, sizeof(text), "person%zu@example.com", i + 1);
        set_cell(row, 2, text);
        set_cell(row, 3, i % 3 == 0 ? "inactive" : "active");
        tbox_control_set_class(app, row, tbox_string_view_make("loading", 7), false);
    }
}

/* UI thread, from tbox: the rows coming into view. They show "..." until
 * the worker's answer arrives. */
static void on_rows(tbox_app *app, tbox_html_node *table, size_t first, tbox_html_node *const *rows, size_t count, void *userdata) {
    (void)table;
    demo *self = userdata;
    for (size_t i = 0; i < count; i++) {
        tbox_control_set_class(app, rows[i], tbox_string_view_make("loading", 7), true);
        set_cell(rows[i], 0, "...");
        set_cell(rows[i], 1, "");
        set_cell(rows[i], 2, "");
        set_cell(rows[i], 3, "");
    }
    request *job = malloc(sizeof(*job));
    if (job == NULL)
        return;
    *job = (request){ first, count, NULL };
    pthread_mutex_lock(&self->lock);
    request **tail = &self->queue;
    while (*tail != NULL)
        tail = &(*tail)->next;
    *tail = job;
    pthread_cond_signal(&self->wake);
    pthread_mutex_unlock(&self->lock);
}

static void *fetch(void *arg) {
    demo *self = arg;
    for (;;) {
        pthread_mutex_lock(&self->lock);
        while (self->queue == NULL && !atomic_load(&self->stop))
            pthread_cond_wait(&self->wake, &self->lock);
        request *job = self->queue;
        if (job != NULL)
            self->queue = job->next;
        pthread_mutex_unlock(&self->lock);
        if (job == NULL)
            return NULL; /* stopping, nothing left */
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 120L * 1000L * 1000L };
        nanosleep(&delay, NULL);
        if (atomic_load(&self->stop) || !tbox_app_post(self->app, deliver, job, free))
            free(job);
    }
}

static bool on_row_click(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)userdata;
    ptrdiff_t index = tbox_table_virtual_index(app, tbox_table_row_of(node));
    char text[64];
    snprintf(text, sizeof(text), index >= 0 ? "Row %td" : "No row", index + 1);
    tbox_control_set_label(app, by_id(app, "status"), tbox_string_view_make(text, strlen(text)));
    return false;
}

static bool on_middle(tbox_app *app, tbox_html_node *node, void *userdata) {
    (void)node;
    demo *self = userdata;
    tbox_table_scroll_to_row(app, self->table, DEMO_ROWS / 2 - 1);
    return false;
}

int main(void) {
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        return 1;
    }
    tbox_app *app = tbox_app_create(demo_html, demo_css, 760, 520);
    if (app == NULL)
        return 1;
    demo self = { .app = app, .table = by_id(app, "people") };
    pthread_mutex_init(&self.lock, NULL);
    pthread_cond_init(&self.wake, NULL);
    atomic_init(&self.stop, false);
    pthread_t thread;
    if (pthread_create(&thread, NULL, fetch, &self) != 0 ||
        !tbox_table_virtualize(app, self.table, by_id(app, "row"), DEMO_ROWS, on_rows, &self, NULL)) {
        tbox_app_close(app);
        return 1;
    }
    tbox_app_on_click(app, "#people tbody td", 16, on_row_click, &self, NULL);
    tbox_app_on_click(app, "#middle", 7, on_middle, &self, NULL);
    tbox_app_run(app);

    /* Every poster must be done before tbox_app_close. */
    pthread_mutex_lock(&self.lock);
    atomic_store(&self.stop, true);
    pthread_cond_signal(&self.wake);
    pthread_mutex_unlock(&self.lock);
    pthread_join(thread, NULL);
    for (request *job = self.queue; job != NULL;) {
        request *next = job->next;
        free(job);
        job = next;
    }
    tbox_app_close(app);
    pthread_cond_destroy(&self.wake);
    pthread_mutex_destroy(&self.lock);
    return 0;
}
