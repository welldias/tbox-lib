#ifndef TBOX_CONTEXT_CALLBACKS_H
#define TBOX_CONTEXT_CALLBACKS_H

#include <tbox/context.h>

/* Private hooks used by app adapters and context tests. Applications register
 * callbacks through <tbox/app.h>; the context owns their dispatch lifetime. */
typedef bool (*tbox_context_click_handler)(tbox_context *ctx, tbox_html_node *node, void *userdata);
typedef void (*tbox_context_input_handler)(tbox_context *ctx, tbox_html_node *input, const char *value, size_t value_length, void *userdata);
typedef void (*tbox_context_select_handler)(tbox_context *ctx, tbox_html_node *select, const char *value, size_t value_length, void *userdata);
typedef void (*tbox_context_submit_handler)(tbox_context *ctx, tbox_html_node *form, tbox_html_node *submitter, void *userdata);

int tbox_context_bind_click(tbox_context *ctx, const char *selector, size_t selector_length, tbox_context_click_handler handler, void *userdata, tbox_destroy_notify destroy);
bool tbox_context_unbind_click(tbox_context *ctx, int binding);
void tbox_context_set_input_handler(tbox_context *ctx, tbox_context_input_handler handler, void *userdata, tbox_destroy_notify destroy);
void tbox_context_set_select_handler(tbox_context *ctx, tbox_context_select_handler handler, void *userdata, tbox_destroy_notify destroy);
void tbox_context_set_submit_handler(tbox_context *ctx, tbox_context_submit_handler handler, void *userdata, tbox_destroy_notify destroy);

#endif
