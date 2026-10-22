/* Compile the actual app loop against an instance-owned simulated backend.
 * No compositor, native globals, installed fonts or timing sleeps required. */
#include "app/tbox_app_internal.h"
#include "context/tbox_context_internal.h"
#include "test_support.h"
#include <stdlib.h>
#include <string.h>
#include <tbox/tbox.h>

struct tbox_window_backend {
    int32_t width, height;
    bool close, connected, ready, accept_present;
    unsigned presents, polls;
    int last_timeout;
    tbox_input_event events[16];
    size_t count;
};
tbox_window_backend *tbox_window_backend_open(int32_t width, int32_t height, const char *title) {
    (void)title;
    tbox_window_backend *b = calloc(1, sizeof(*b));
    if (b != NULL) {
        b->width     = width;
        b->height    = height;
        b->connected = b->ready = b->accept_present = true;
    }
    return b;
}
void tbox_window_backend_destroy(tbox_window_backend *b) {
    free(b);
}
bool tbox_window_backend_poll(tbox_window_backend *b, int timeout) {
    b->polls++;
    b->last_timeout = timeout;
    return b->connected;
}
void tbox_window_backend_wake(tbox_window_backend *b) {
    (void)b;
}
bool tbox_window_backend_frame_ready(const tbox_window_backend *b) {
    return b != NULL && b->ready;
}
bool tbox_window_backend_should_close(const tbox_window_backend *b) {
    return b == NULL || b->close;
}
void tbox_window_backend_size(const tbox_window_backend *b, int32_t *w, int32_t *h) {
    *w = b->width;
    *h = b->height;
}
bool tbox_window_backend_take_event(tbox_window_backend *b, tbox_input_event *event) {
    if (b->count == 0)
        return false;
    *event = b->events[0];
    b->count--;
    memmove(b->events, b->events + 1, b->count * sizeof(*event));
    return true;
}
bool tbox_window_backend_pointer_position(const tbox_window_backend *b, double *x, double *y) {
    (void)b;
    (void)x;
    (void)y;
    return false;
}
bool tbox_window_backend_clipboard_copy(tbox_window_backend *b, const char *text, size_t n, uint32_t serial) {
    (void)b;
    (void)text;
    (void)n;
    (void)serial;
    return false;
}
bool tbox_window_backend_clipboard_paste(tbox_window_backend *b) {
    (void)b;
    return false;
}
bool tbox_window_backend_present(tbox_window_backend *b, const tbox_display_list *list) {
    (void)list;
    b->presents++;
    return b->accept_present;
}
void tbox_window_backend_set_cursor(tbox_window_backend *b, tbox_style_cursor cursor) {
    (void)b;
    (void)cursor;
}
void tbox_window_backend_set_debug_damage(tbox_window_backend *b, bool enabled) {
    (void)b;
    (void)enabled;
}

static tbox_app *tbox_test_app_with_html(const char *html) {
    tbox_context_options options = tbox_context_options_default();
    tbox_context *ctx            = tbox_context_open_with_options(html, strlen(html), "", 0, NULL, NULL, &options);
    tbox_window_backend *backend = tbox_window_backend_open(320, 200, NULL);
    if (ctx == NULL || backend == NULL) {
        tbox_context_close(ctx);
        tbox_window_backend_destroy(backend);
        return NULL;
    }
    tbox_app *app = tbox_app_adopt(ctx, NULL, NULL, NULL, backend, &options);
    if (app == NULL) {
        tbox_context_close(ctx);
        tbox_window_backend_destroy(backend);
    }
    return app;
}
static tbox_app *tbox_test_app(void) {
    return tbox_test_app_with_html("<body><div id='root'></div></body>");
}
typedef struct lifecycle_probe {
    int loads, shows, hides, unloads, resizes, errors, destroys;
    int order[32];
    size_t count;
    bool context_on_unload, recursive_close, close_on_load, close_on_resize, close_on_show;
    bool replace_load, destroyed_in_callback, reload_on_load, close_next_task;
    tbox_app_resize_event resize;
} lifecycle_probe;
static void probe_destroy(void *userdata) {
    ((lifecycle_probe *)userdata)->destroys++;
}
static void check_release_and_close(tbox_app *app, void *userdata) {
    lifecycle_probe *p = userdata;
    if (p->destroys != 1)
        p->errors++;
    tbox_app_close(app);
}
static void probe_load(tbox_app *app, void *userdata) {
    lifecycle_probe *p = userdata;
    p->loads++;
    p->order[p->count++] = 1;
    if (p->replace_load) {
        int before = p->destroys;
        tbox_app_on_load(app, probe_load, p, probe_destroy);
        p->destroyed_in_callback = p->destroys != before;
        p->replace_load          = false;
    }
    /* Loop callbacks cannot recursively dispatch themselves. */
    unsigned polls = app->backend->polls;
    tbox_app_step(app);
    tbox_app_wait(app, 0);
    tbox_app_run(app);
    if (app->backend->polls != polls)
        p->errors++;
    if (p->reload_on_load) {
        p->reload_on_load = false;
        if (!tbox_app_load_from_files(app, TBOX_TEST_ASSETS_DIR "/svg-paths.html", NULL))
            p->errors++;
    }
    if (p->close_next_task) {
        p->close_next_task = false;
        if (!tbox_app_post(app, check_release_and_close, p, NULL))
            p->errors++;
    }
    if (p->close_on_load)
        tbox_app_close(app);
}
static void probe_unload(tbox_app *app, void *userdata) {
    lifecycle_probe *p = userdata;
    p->unloads++;
    p->order[p->count++] = 4;
    p->context_on_unload = tbox_app_context(app) != NULL && tbox_context_document(tbox_app_context(app)) != NULL;
    if (p->recursive_close)
        tbox_app_close(app);
}
static void probe_show(tbox_app *app, void *userdata) {
    lifecycle_probe *p = userdata;
    p->shows++;
    p->order[p->count++] = 2;
    if (p->close_on_show)
        tbox_app_close(app);
}
static void probe_hide(tbox_app *app, void *userdata) {
    (void)app;
    lifecycle_probe *p = userdata;
    p->hides++;
    p->order[p->count++] = 3;
}
static void probe_resize(tbox_app *app, const tbox_app_resize_event *event, void *userdata) {
    lifecycle_probe *p = userdata;
    p->resizes++;
    p->resize = *event;
    if (p->close_on_resize)
        tbox_app_close(app);
}
static void probe_error(tbox_app *app, const tbox_app_error_event *event, void *userdata) {
    (void)app;
    (void)event;
    ((lifecycle_probe *)userdata)->errors++;
}
static void push_focus(tbox_app *app, bool focused) {
    app->backend->events[app->backend->count++] = (tbox_input_event){ .kind = TBOX_INPUT_WINDOW_FOCUS, .data.window_focus = { focused } };
}
static void bind_probe(tbox_app *app, lifecycle_probe *p) {
    tbox_app_on_load(app, probe_load, p, probe_destroy);
    tbox_app_on_unload(app, probe_unload, p, probe_destroy);
    tbox_app_on_pageshow(app, probe_show, p, probe_destroy);
    tbox_app_on_pagehide(app, probe_hide, p, probe_destroy);
    tbox_app_on_resize(app, probe_resize, p, probe_destroy);
    tbox_app_on_error(app, probe_error, p, probe_destroy);
}
static int test_lifecycle(void) {
    int failures      = 0;
    lifecycle_probe p = { 0 };
    tbox_app *app     = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    push_focus(app, true);
    TBOX_TEST_ASSERT(p.loads == 0 && p.shows == 0);
    app->backend->accept_present = false;
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 0 && p.shows == 0); /* no load for a refused frame */
    app->backend->accept_present = true;
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 1 && p.shows == 1 && p.resizes == 0);
    TBOX_TEST_ASSERT(p.order[0] == 1 && p.order[1] == 2);
    push_focus(app, true);
    push_focus(app, false);
    push_focus(app, false);
    push_focus(app, true);
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 1 && p.hides == 1 && p.shows == 2);
    TBOX_TEST_ASSERT(p.order[2] == 3 && p.order[3] == 2);
    app->backend->width  = 500;
    app->backend->height = 300;
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.resizes == 1 && p.resize.previous_width == 320 && p.resize.previous_height == 200 && p.resize.width == 500 && p.resize.height == 300);
    TBOX_TEST_ASSERT(app->ctx->root != NULL && app->ctx->root->content_box.width <= 500);
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.resizes == 1 && p.loads == 1);
    TBOX_TEST_ASSERT(!tbox_app_load_from_files(app, "/this-file-does-not-exist-tbox.html", NULL));
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.errors == 0 && p.loads == 1);
    TBOX_TEST_ASSERT(tbox_app_load_from_files(app, TBOX_TEST_ASSETS_DIR "/svg-paths.html", NULL));
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 2 && p.unloads == 0 && p.shows == 2);
    app->backend->close = true;
    tbox_app_step(app);
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.context_on_unload && p.errors == 0);
    TBOX_TEST_ASSERT(!tbox_app_load_from_files(app, TBOX_TEST_ASSETS_DIR "/svg-paths.html", NULL));
    tbox_app_close(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.destroys == 6);
    return failures;
}
static int test_reentrancy(void) {
    int failures      = 0;
    lifecycle_probe p = { .replace_load = true, .reload_on_load = true };
    tbox_app *app     = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 1 && !p.destroyed_in_callback && p.destroys == 1 && p.errors == 0);
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.loads == 2);
    p.recursive_close = true;
    tbox_app_close(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.context_on_unload && p.destroys == 7);
    p   = (lifecycle_probe){ .replace_load = true, .close_next_task = true };
    app = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    tbox_app_run(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.destroys == 7 && p.errors == 0);
    /* Destruction from load must wait until both step and run stop using app. */
    for (int run = 0; run < 2; run++) {
        lifecycle_probe q = { .close_on_load = true, .recursive_close = true };
        app               = tbox_test_app();
        TBOX_TEST_REQUIRE(app != NULL);
        bind_probe(app, &q);
        if (run)
            tbox_app_run(app);
        else
            tbox_app_step(app);
        TBOX_TEST_ASSERT(q.loads == 1 && q.unloads == 1 && q.context_on_unload && q.destroys == 6 && q.errors == 0);
    }
    return failures;
}
static int test_close_paths(void) {
    int failures = 0;
    for (int kind = 0; kind < 4; kind++) {
        lifecycle_probe p = { 0 };
        tbox_app *app     = tbox_test_app();
        TBOX_TEST_REQUIRE(app != NULL);
        bind_probe(app, &p);
        if (kind == 0) {
            tbox_app_close(app);
            TBOX_TEST_ASSERT(p.loads == 0 && p.unloads == 1 && p.destroys == 6);
            continue;
        }
        tbox_app_step(app);
        if (kind == 1)
            app->backend->connected = false;
        if (kind == 2)
            app->backend->events[app->backend->count++] = (tbox_input_event){
                .kind = TBOX_INPUT_KEY, .data.key = { .key = TBOX_KEY_ESCAPE, .pressed = true }
            };
        if (kind == 3)
            app->backend->close = true;
        tbox_app_run(app);
        TBOX_TEST_ASSERT(p.unloads == 1 && p.context_on_unload && p.errors == 0);
        tbox_app_close(app);
        TBOX_TEST_ASSERT(p.unloads == 1 && p.destroys == 6);
    }
    /* Every new callback can close safely before the loop accesses resources. */
    for (int kind = 0; kind < 2; kind++) {
        lifecycle_probe p = { 0 };
        tbox_app *app     = tbox_test_app();
        TBOX_TEST_REQUIRE(app != NULL);
        bind_probe(app, &p);
        tbox_app_step(app);
        if (kind == 0) {
            p.close_on_resize   = true;
            app->backend->width = 400;
        } else {
            p.close_on_show = true;
            push_focus(app, true);
        }
        tbox_app_step(app);
        TBOX_TEST_ASSERT(p.unloads == 1 && p.destroys == 6);
    }
    return failures;
}
typedef struct task_probe {
    int calls, destroys;
    bool close;
} task_probe;
static void close_task(tbox_app *app, void *userdata) {
    task_probe *p = userdata;
    p->calls++;
    if (p->close)
        tbox_app_close(app);
}
static void task_destroy(void *userdata) {
    ((task_probe *)userdata)->destroys++;
}
static bool close_key(tbox_app *app, const tbox_key_event *event, void *userdata) {
    (void)event;
    (void)userdata;
    tbox_app_close(app);
    return false; /* default handling must also stop after destruction request */
}
static int test_existing_callbacks(void) {
    int failures      = 0;
    lifecycle_probe p = { 0 };
    task_probe first = { .close = true }, second = { 0 };
    tbox_app *app = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    TBOX_TEST_ASSERT(tbox_app_post(app, close_task, &first, task_destroy));
    TBOX_TEST_ASSERT(tbox_app_post(app, close_task, &second, task_destroy));
    tbox_app_step(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.context_on_unload && p.destroys == 6 && p.loads == 0);
    TBOX_TEST_ASSERT(first.calls == 1 && second.calls == 0 && first.destroys == 1 && second.destroys == 1);
    p   = (lifecycle_probe){ 0 };
    app = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    tbox_app_on_key(app, close_key, NULL, NULL);
    app->backend->events[app->backend->count++] = (tbox_input_event){
        .kind = TBOX_INPUT_KEY, .data.key = { .key = TBOX_KEY_ENTER, .pressed = true }
    };
    tbox_app_run(app);
    TBOX_TEST_ASSERT(p.unloads == 1 && p.context_on_unload && p.destroys == 6 && p.loads == 0);
    return failures;
}
static int test_independent_windows(void) {
    int failures        = 0;
    lifecycle_probe one = { 0 }, two = { 0 };
    tbox_app *a = tbox_test_app(), *b = tbox_test_app();
    TBOX_TEST_REQUIRE(a != NULL && b != NULL);
    bind_probe(a, &one);
    bind_probe(b, &two);
    push_focus(a, true);
    tbox_app_step(a);
    tbox_app_step(b);
    TBOX_TEST_ASSERT(one.loads == 1 && two.loads == 1 && one.shows == 1 && two.shows == 0);
    push_focus(a, false);
    push_focus(b, true);
    tbox_app_step(a);
    tbox_app_step(b);
    TBOX_TEST_ASSERT(one.hides == 1 && two.shows == 1 && two.hides == 0);
    tbox_app_close(a);
    TBOX_TEST_ASSERT(one.unloads == 1 && two.unloads == 0);
    tbox_app_close(b);
    TBOX_TEST_ASSERT(two.unloads == 1);
    return failures;
}
static int test_cleanup(void) {
    int failures      = 0;
    lifecycle_probe p = { 0 };
    tbox_app *app     = tbox_test_app();
    TBOX_TEST_REQUIRE(app != NULL);
    bind_probe(app, &p);
    tbox_app_on_load(app, probe_load, &p, probe_destroy);
    TBOX_TEST_ASSERT(p.destroys == 1);
    tbox_app_on_load(app, NULL, NULL, NULL);
    tbox_app_on_unload(app, NULL, NULL, NULL);
    tbox_app_on_pageshow(app, NULL, NULL, NULL);
    tbox_app_on_pagehide(app, NULL, NULL, NULL);
    tbox_app_on_resize(app, NULL, NULL, NULL);
    tbox_app_on_error(app, NULL, NULL, NULL);
    TBOX_TEST_ASSERT(p.destroys == 7);
    tbox_app_step(app);
    tbox_app_close(app);
    TBOX_TEST_ASSERT(p.count == 0 && p.destroys == 7);
    tbox_app_on_load(NULL, probe_load, &p, probe_destroy);
    tbox_app_on_unload(NULL, probe_unload, &p, probe_destroy);
    tbox_app_on_pageshow(NULL, probe_show, &p, probe_destroy);
    tbox_app_on_pagehide(NULL, probe_hide, &p, probe_destroy);
    tbox_app_on_resize(NULL, probe_resize, &p, probe_destroy);
    tbox_app_on_error(NULL, probe_error, &p, probe_destroy);
    TBOX_TEST_ASSERT(p.destroys == 7);
    return failures;
}
typedef struct document_probe {
    tbox_app *expected_app;
    int clicks, inputs, selects, submits, destroys, binding;
    bool wrong_app, remove_self, clear_input, close, close_on_destroy, premature_destroy;
    tbox_html_node *node, *form, *submitter;
    char value[32];
    size_t value_length;
} document_probe;
static void document_destroy(void *userdata) {
    document_probe *p = userdata;
    p->destroys++;
    if (p->close_on_destroy)
        tbox_app_close(p->expected_app);
}
static void document_record(tbox_app *app, tbox_html_node *node, document_probe *p) {
    p->wrong_app |= app != p->expected_app || tbox_app_context(app) == NULL;
    p->node = node;
}
static bool document_click(tbox_app *app, tbox_html_node *node, void *userdata) {
    document_probe *p = userdata;
    document_record(app, node, p);
    p->clicks++;
    if (p->remove_self) {
        tbox_app_unbind_click(app, p->binding);
        p->premature_destroy |= p->destroys != 0;
    }
    if (p->close)
        tbox_app_close(app);
    return false;
}
static void document_input(tbox_app *app, tbox_html_node *node, const char *value, size_t length, void *userdata) {
    document_probe *p = userdata;
    document_record(app, node, p);
    p->inputs++;
    p->value_length = length;
    if (length < sizeof(p->value))
        memcpy(p->value, value, length);
    if (p->clear_input) {
        tbox_app_on_input(app, NULL, p, document_destroy);
        p->premature_destroy |= p->destroys != 0;
    }
    if (p->close)
        tbox_app_close(app);
}
static void document_select(tbox_app *app, tbox_html_node *node, const char *value, size_t length, void *userdata) {
    document_probe *p = userdata;
    document_record(app, node, p);
    p->selects++;
    p->value_length = length;
    if (length < sizeof(p->value))
        memcpy(p->value, value, length);
}
static void document_submit(tbox_app *app, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    document_probe *p = userdata;
    document_record(app, form, p);
    p->submits++;
    p->form      = form;
    p->submitter = submitter;
}
static void document_key(tbox_app *app, tbox_key key) {
    app->backend->events[app->backend->count++] = (tbox_input_event){
        .kind = TBOX_INPUT_KEY, .data.key = { .key = key, .pressed = true }
    };
    tbox_app_step(app);
}
static tbox_app *document_app(void) {
    return tbox_test_app_with_html("<body><form><input type='range' value='5' min='0' max='10'>"
                                   "<select><option value='a'>A</option><option value='b'>B</option></select>"
                                   "<input type='submit' value='Send'></form></body>");
}
static int test_document_events(void) {
    int failures  = 0;
    tbox_app *app = document_app();
    TBOX_TEST_REQUIRE(app != NULL);
    document_probe click  = { .expected_app = app, .remove_self = true };
    document_probe input  = { .expected_app = app, .clear_input = true };
    document_probe select = { .expected_app = app }, submit = { .expected_app = app };
    tbox_app_step(app);
    click.binding = tbox_app_on_click(app, "input[type=submit]", 18, document_click, &click, document_destroy);
    TBOX_TEST_ASSERT(click.binding >= 0);
    tbox_app_on_input(app, document_input, &input, document_destroy);
    tbox_app_on_select(app, document_select, &select, document_destroy);
    tbox_app_on_submit(app, document_submit, &submit, document_destroy);
    document_key(app, TBOX_KEY_TAB);
    document_key(app, TBOX_KEY_RIGHT);
    TBOX_TEST_ASSERT(input.inputs == 1 && input.value_length == 1 && input.value[0] == '6');
    TBOX_TEST_ASSERT(input.destroys == 1 && !input.premature_destroy && !input.wrong_app);
    document_key(app, TBOX_KEY_RIGHT);
    TBOX_TEST_ASSERT(input.inputs == 1);
    document_key(app, TBOX_KEY_TAB);
    document_key(app, TBOX_KEY_DOWN);
    TBOX_TEST_ASSERT(select.selects == 1 && select.value_length == 1 && select.value[0] == 'b' && !select.wrong_app);
    TBOX_TEST_ASSERT(tbox_control_set_value(app, (tbox_html_node *)select.node, tbox_string_view_make("a", 1)));
    TBOX_TEST_ASSERT(select.selects == 1);
    document_key(app, TBOX_KEY_TAB);
    document_key(app, TBOX_KEY_ENTER);
    TBOX_TEST_ASSERT(submit.submits == 1 && submit.form != NULL && submit.submitter != NULL && !submit.wrong_app);
    TBOX_TEST_ASSERT(click.clicks == 1 && click.node == submit.submitter && click.destroys == 1 && !click.premature_destroy && !click.wrong_app);
    TBOX_TEST_ASSERT(!tbox_app_unbind_click(app, click.binding));
    document_key(app, TBOX_KEY_ENTER);
    TBOX_TEST_ASSERT(click.clicks == 1 && submit.submits == 2);
    tbox_app_on_select(app, NULL, &select, document_destroy);
    TBOX_TEST_ASSERT(select.destroys == 1);
    tbox_app_on_submit(app, document_submit, &submit, document_destroy);
    TBOX_TEST_ASSERT(submit.destroys == 1);
    tbox_app_close(app);
    TBOX_TEST_ASSERT(submit.destroys == 2 && select.destroys == 1 && click.destroys == 1 && input.destroys == 1);
    return failures;
}
static int test_document_registration_lifetime(void) {
    int failures  = 0;
    tbox_app *app = document_app();
    TBOX_TEST_REQUIRE(app != NULL);
    document_probe p = { .expected_app = app };
    TBOX_TEST_ASSERT(tbox_app_on_click(NULL, "*", 1, document_click, &p, document_destroy) == -1);
    TBOX_TEST_ASSERT(!tbox_app_unbind_click(NULL, 0));
    tbox_app_on_input(NULL, document_input, &p, document_destroy);
    tbox_app_on_select(NULL, document_select, &p, document_destroy);
    tbox_app_on_submit(NULL, document_submit, &p, document_destroy);
    TBOX_TEST_ASSERT(tbox_app_on_click(app, "[", 1, document_click, &p, document_destroy) == -1);
    TBOX_TEST_ASSERT(tbox_app_on_click(app, "*", 1, NULL, &p, document_destroy) == -1);
    TBOX_TEST_ASSERT(p.destroys == 0);
    TBOX_TEST_ASSERT(tbox_app_on_click(app, "*", 1, document_click, &p, document_destroy) >= 0);
    tbox_app_on_input(app, document_input, &p, document_destroy);
    tbox_app_on_select(app, document_select, &p, document_destroy);
    tbox_app_on_submit(app, document_submit, &p, document_destroy);
    TBOX_TEST_ASSERT(!tbox_app_load_from_files(app, "/missing/tbox-document.html", NULL));
    TBOX_TEST_ASSERT(p.destroys == 0);
    TBOX_TEST_ASSERT(tbox_app_load_from_files(app, TBOX_TEST_ASSETS_DIR "/svg-paths.html", NULL));
    TBOX_TEST_ASSERT(p.destroys == 4);
    tbox_app_close(app);
    TBOX_TEST_ASSERT(p.destroys == 4);
    /* Closing from a control callback is deferred until dispatch unwinds. */
    app = document_app();
    TBOX_TEST_REQUIRE(app != NULL);
    p = (document_probe){ .expected_app = app, .close = true };
    tbox_app_on_input(app, document_input, &p, document_destroy);
    tbox_app_step(app);
    document_key(app, TBOX_KEY_TAB);
    document_key(app, TBOX_KEY_RIGHT);
    TBOX_TEST_ASSERT(p.inputs == 1 && p.destroys == 1 && !p.wrong_app);
    /* A destroy callback can close the app while a slot is being cleared. */
    app = document_app();
    TBOX_TEST_REQUIRE(app != NULL);
    p = (document_probe){ .expected_app = app, .close_on_destroy = true };
    tbox_app_on_input(app, document_input, &p, document_destroy);
    tbox_app_on_input(app, NULL, NULL, NULL);
    TBOX_TEST_ASSERT(p.destroys == 1);
    /* Reload also releases document userdata while keeping app alive. */
    app = document_app();
    TBOX_TEST_REQUIRE(app != NULL);
    p = (document_probe){ .expected_app = app, .close_on_destroy = true };
    tbox_app_on_input(app, document_input, &p, document_destroy);
    TBOX_TEST_ASSERT(tbox_app_load_from_files(app, TBOX_TEST_ASSETS_DIR "/svg-paths.html", NULL));
    TBOX_TEST_ASSERT(p.destroys == 1);
    return failures;
}
/* A DOM change made outside any handler needs no request_redraw: the loop
 * neither sleeps nor skips the frame. An idle loop stays idle. */
static int test_dom_change_redraws(void) {
    int failures  = 0;
    tbox_app *app = tbox_test_app_with_html("<body><p id='label'>a</p><input id='field'></body>");
    TBOX_TEST_REQUIRE(app != NULL);
    tbox_app_step(app);
    tbox_app_step(app);
    unsigned presents = app->backend->presents;
    tbox_app_wait(app, -1);
    TBOX_TEST_ASSERT(app->backend->presents == presents && app->backend->last_timeout == -1);

    tbox_context *ctx = tbox_app_context(app);
    tbox_html_node *label = tbox_app_get_element_by_id(app, tbox_string_view_make("label", 5));
    TBOX_TEST_REQUIRE(label != NULL);
    TBOX_TEST_ASSERT(tbox_app_query(app, "p", 1) == label && tbox_app_query(app, "[", 1) == NULL);
    TBOX_TEST_ASSERT(tbox_app_query(NULL, "p", 1) == NULL);
    TBOX_TEST_ASSERT(tbox_app_get_element_by_id(NULL, tbox_string_view_make("label", 5)) == NULL);
    tbox_css_selector_node_set all = tbox_app_query_all(app, "p, input", 8);
    TBOX_TEST_ASSERT(all.count == 2 && all.items[0] == label);
    tbox_css_selector_node_set_destroy(&all);
    tbox_html_node_set_text_content(tbox_context_document(ctx), label, tbox_string_view_make("b", 1));
    tbox_app_wait(app, -1);
    TBOX_TEST_ASSERT(app->backend->last_timeout == 0 && app->backend->presents == presents + 1);
    tbox_app_wait(app, -1); /* the post-present hover recheck */
    tbox_app_wait(app, -1);
    TBOX_TEST_ASSERT(app->backend->presents == presents + 1 && app->backend->last_timeout == -1);

    /* Controls the frame normalizes or binds to editing buffers settle
     * after one frame instead of bumping the revision every time. */
    tbox_app_close(app);
    app = tbox_test_app_with_html("<body><form><input id='field' value='x'><input type='number' value='zz'>"
        "<input type='range' value='500'><input type='date' value='bad'><input type='file' value='f'>"
        "<input type='checkbox' checked><textarea>t</textarea><select><option>1</option></select>"
        "<progress value='1'></progress></form></body>");
    TBOX_TEST_REQUIRE(app != NULL);
    for (int i = 0; i < 4; i++) tbox_app_wait(app, -1);
    presents = app->backend->presents;
    tbox_app_wait(app, -1);
    TBOX_TEST_ASSERT(app->backend->presents == presents && app->backend->last_timeout == -1);
    ctx = tbox_app_context(app);

    /* Control state outside the DOM (focus) counts too. */
    TBOX_TEST_ASSERT(tbox_control_focus(app, tbox_app_get_element_by_id(app, tbox_string_view_make("field", 5))));
    tbox_app_wait(app, -1);
    TBOX_TEST_ASSERT(app->backend->presents == presents + 1);
    tbox_app_close(app);
    return failures;
}
int main(void) {
    return test_dom_change_redraws() + test_document_events() + test_document_registration_lifetime() + test_lifecycle() + test_reentrancy() + test_close_paths() + test_cleanup() + test_existing_callbacks() + test_independent_windows() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
