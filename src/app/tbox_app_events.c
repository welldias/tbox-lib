#include <tbox/app.h>

#ifndef TBOX_HAS_WINDOW_BACKEND
#define TBOX_HAS_WINDOW_BACKEND 0
#endif

#if TBOX_HAS_WINDOW_BACKEND
#include "tbox_app_internal.h"
#include "context/tbox_context_callbacks.h"
#include <stdlib.h>

/* The context owns adapters along with its document bindings. No adapter is
 * accessed after invoking user code: it may have removed its own binding. */
typedef struct tbox_app_document_binding {
    tbox_app *app;
    union {
        tbox_app_click_handler click;
        tbox_app_input_handler input;
        tbox_app_select_handler select;
        tbox_app_submit_handler submit;
    } callback;
    void *userdata;
    tbox_destroy_notify destroy;
} tbox_app_document_binding;

static void tbox_app_document_binding_destroy(void *userdata) {
    tbox_app_document_binding *binding = userdata;
    if (binding->destroy != NULL)
        binding->destroy(binding->userdata);
    free(binding);
}

static tbox_app_document_binding *tbox_app_document_binding_create(tbox_app *app, void *userdata, tbox_destroy_notify destroy) {
    tbox_app_document_binding *binding = calloc(1, sizeof(*binding));
    if (binding != NULL) {
        binding->app      = app;
        binding->userdata = userdata;
        binding->destroy  = destroy;
    }
    return binding;
}

static bool tbox_app_document_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)ctx;
    tbox_app_document_binding *binding = userdata;
    tbox_app *app                      = binding->app;
    if (tbox_app_should_close(app))
        return false;
    bool propagate = binding->callback.click(app, node, binding->userdata);
    return !tbox_app_should_close(app) && propagate;
}

int tbox_app_on_click(tbox_app *app, const char *selector, size_t selector_length, tbox_app_click_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (app == NULL || app->destroying || app->ctx == NULL || handler == NULL)
        return -1;
    tbox_app_document_binding *binding = tbox_app_document_binding_create(app, userdata, destroy);
    if (binding == NULL)
        return -1;
    binding->callback.click = handler;
    int id                  = tbox_context_bind_click(app->ctx, selector, selector_length, tbox_app_document_click, binding, tbox_app_document_binding_destroy);
    if (id < 0)
        free(binding); /* registration failed: caller still owns userdata */
    return id;
}

bool tbox_app_unbind_click(tbox_app *app, int binding) {
    if (app == NULL || app->destroying || app->ctx == NULL)
        return false;
    tbox_app_enter(app);
    bool removed = tbox_context_unbind_click(app->ctx, binding);
    tbox_app_leave(app);
    return removed;
}

static void tbox_app_document_input(tbox_context *ctx, tbox_html_node *node, const char *value, size_t value_length, void *userdata) {
    (void)ctx;
    tbox_app_document_binding *binding = userdata;
    if (!tbox_app_should_close(binding->app))
        binding->callback.input(binding->app, node, value, value_length, binding->userdata);
}

void tbox_app_on_input(tbox_app *app, tbox_app_input_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (app == NULL || app->destroying || app->ctx == NULL)
        return;
    tbox_app_document_binding *binding = NULL;
    if (handler != NULL) {
        binding = tbox_app_document_binding_create(app, userdata, destroy);
        if (binding == NULL)
            return;
        binding->callback.input = handler;
    }
    tbox_app_enter(app);
    tbox_context_set_input_handler(app->ctx, handler != NULL ? tbox_app_document_input : NULL, binding, handler != NULL ? tbox_app_document_binding_destroy : NULL);
    tbox_app_leave(app);
}

static void tbox_app_document_select(tbox_context *ctx, tbox_html_node *node, const char *value, size_t value_length, void *userdata) {
    (void)ctx;
    tbox_app_document_binding *binding = userdata;
    if (!tbox_app_should_close(binding->app))
        binding->callback.select(binding->app, node, value, value_length, binding->userdata);
}

void tbox_app_on_select(tbox_app *app, tbox_app_select_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (app == NULL || app->destroying || app->ctx == NULL)
        return;
    tbox_app_document_binding *binding = NULL;
    if (handler != NULL) {
        binding = tbox_app_document_binding_create(app, userdata, destroy);
        if (binding == NULL)
            return;
        binding->callback.select = handler;
    }
    tbox_app_enter(app);
    tbox_context_set_select_handler(app->ctx, handler != NULL ? tbox_app_document_select : NULL, binding, handler != NULL ? tbox_app_document_binding_destroy : NULL);
    tbox_app_leave(app);
}

static void tbox_app_document_submit(tbox_context *ctx, tbox_html_node *form, tbox_html_node *submitter, void *userdata) {
    (void)ctx;
    tbox_app_document_binding *binding = userdata;
    if (!tbox_app_should_close(binding->app))
        binding->callback.submit(binding->app, form, submitter, binding->userdata);
}

void tbox_app_on_submit(tbox_app *app, tbox_app_submit_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (app == NULL || app->destroying || app->ctx == NULL)
        return;
    tbox_app_document_binding *binding = NULL;
    if (handler != NULL) {
        binding = tbox_app_document_binding_create(app, userdata, destroy);
        if (binding == NULL)
            return;
        binding->callback.submit = handler;
    }
    tbox_app_enter(app);
    tbox_context_set_submit_handler(app->ctx, handler != NULL ? tbox_app_document_submit : NULL, binding, handler != NULL ? tbox_app_document_binding_destroy : NULL);
    tbox_app_leave(app);
}

static void tbox_app_set_event(tbox_app *app, tbox_app_event_kind kind, const tbox_app_event_binding *binding) {
    if (app == NULL || app->destroying)
        return;
    tbox_app_enter(app);
    tbox_app_event_binding previous = app->events[kind];
    app->events[kind]               = *binding;
    tbox_release_add(&app->releases, previous.destroy, previous.userdata);
    tbox_app_leave(app);
}
#define TBOX_APP_SIMPLE_EVENT(name, kind)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      \
    void tbox_app_on_##name(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      \
        tbox_app_event_binding binding = { .callback.simple = handler, .userdata = handler != NULL ? userdata : NULL, .destroy = handler != NULL ? destroy : NULL };                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
        tbox_app_set_event(app, kind, &binding);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               \
    }
TBOX_APP_SIMPLE_EVENT(load, TBOX_APP_EVENT_LOAD)
TBOX_APP_SIMPLE_EVENT(unload, TBOX_APP_EVENT_UNLOAD)
TBOX_APP_SIMPLE_EVENT(pagehide, TBOX_APP_EVENT_PAGEHIDE)
TBOX_APP_SIMPLE_EVENT(pageshow, TBOX_APP_EVENT_PAGESHOW)
#undef TBOX_APP_SIMPLE_EVENT
void tbox_app_on_resize(tbox_app *app, tbox_app_resize_handler handler, void *userdata, tbox_destroy_notify destroy) {
    tbox_app_event_binding binding = { .callback.resize = handler, .userdata = handler != NULL ? userdata : NULL, .destroy = handler != NULL ? destroy : NULL };
    tbox_app_set_event(app, TBOX_APP_EVENT_RESIZE, &binding);
}
void tbox_app_on_error(tbox_app *app, tbox_app_error_handler handler, void *userdata, tbox_destroy_notify destroy) {
    tbox_app_event_binding binding = { .callback.error = handler, .userdata = handler != NULL ? userdata : NULL, .destroy = handler != NULL ? destroy : NULL };
    tbox_app_set_event(app, TBOX_APP_EVENT_ERROR, &binding);
}
bool tbox_app_emit(tbox_app *app, tbox_app_event_kind kind, const tbox_app_resize_event *resize) {
    if (kind == TBOX_APP_EVENT_ERROR)
        return false; /* registration only */
    tbox_app_event_binding binding = app->events[kind];
    if (kind == TBOX_APP_EVENT_RESIZE) {
        if (binding.callback.resize == NULL)
            return false;
        binding.callback.resize(app, resize, binding.userdata);
    } else {
        if (binding.callback.simple == NULL)
            return false;
        binding.callback.simple(app, binding.userdata);
    }
    return true;
}
void tbox_app_notify_unload(tbox_app *app) {
    if (app->unload_emitted)
        return;
    app->unload_emitted = true;
    app->closed         = true;
    app->load_pending   = false;
    tbox_app_emit(app, TBOX_APP_EVENT_UNLOAD, NULL);
}
bool tbox_app_notify_focus(tbox_app *app) {
    if (app->load_pending || tbox_app_should_close(app) || app->focused == app->reported_focus)
        return false;
    app->reported_focus = app->focused;
    return tbox_app_emit(app, app->focused ? TBOX_APP_EVENT_PAGESHOW : TBOX_APP_EVENT_PAGEHIDE, NULL);
}

void tbox_app_on_key(tbox_app *app, tbox_app_key_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (app == NULL || app->destroying)
        return;
    tbox_app_enter(app);
    tbox_release_add(&app->releases, app->key_handler_destroy, app->key_handler_userdata);
    app->key_handler          = handler;
    app->key_handler_userdata = userdata;
    app->key_handler_destroy  = destroy;
    tbox_app_leave(app);
}

bool tbox_app_emit_key(tbox_app *app, const tbox_key_event *event) {
    if (app->key_handler == NULL)
        return false;
    tbox_release_enter(&app->releases);
    bool consumed = app->key_handler(app, event, app->key_handler_userdata);
    tbox_release_leave(&app->releases);
    return consumed;
}

void tbox_app_events_release(tbox_app *app) {
    tbox_release_add(&app->releases, app->key_handler_destroy, app->key_handler_userdata);
    for (size_t i = 0; i < TBOX_APP_EVENT_COUNT; i++)
        tbox_release_add(&app->releases, app->events[i].destroy, app->events[i].userdata);
}
#else
void tbox_app_on_key(tbox_app *app, tbox_app_key_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app;
    (void)handler;
    (void)userdata;
    (void)destroy;
}
void tbox_app_on_load(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_unload(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_pagehide(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_pageshow(tbox_app *app, tbox_app_event_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_resize(tbox_app *app, tbox_app_resize_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }
void tbox_app_on_error(tbox_app *app, tbox_app_error_handler handler, void *userdata, tbox_destroy_notify destroy) { (void)app; (void)handler; (void)userdata; (void)destroy; }

int tbox_app_on_click(tbox_app *app, const char *selector, size_t selector_length, tbox_app_click_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app;
    (void)selector;
    (void)selector_length;
    (void)handler;
    (void)userdata;
    (void)destroy;
    return -1;
}
bool tbox_app_unbind_click(tbox_app *app, int binding) {
    (void)app;
    (void)binding;
    return false;
}
void tbox_app_on_input(tbox_app *app, tbox_app_input_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app;
    (void)handler;
    (void)userdata;
    (void)destroy;
}
void tbox_app_on_select(tbox_app *app, tbox_app_select_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app;
    (void)handler;
    (void)userdata;
    (void)destroy;
}
void tbox_app_on_submit(tbox_app *app, tbox_app_submit_handler handler, void *userdata, tbox_destroy_notify destroy) {
    (void)app;
    (void)handler;
    (void)userdata;
    (void)destroy;
}
#endif
