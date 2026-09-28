#include "tbox_context_internal.h"

static const tbox_html_node *tbox_context_nearest_form(const tbox_html_node *node);
static tbox_string_view tbox_context_copy_default(tbox_context *ctx, tbox_string_view value);
static const tbox_html_node *tbox_context_radio_form(const tbox_html_node *node);
static bool tbox_context_radio_same_group(const tbox_html_node *a, const tbox_html_node *b);
static bool tbox_context_radio_clear_peers(tbox_html_node *node, const tbox_html_node *chosen);

static const tbox_html_node *tbox_context_nearest_form(const tbox_html_node *node) {
    for (node = node != NULL ? node->parent : NULL; node != NULL; node = node->parent)
        if (node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(node->element.tag_name, "form")) return node;
    return NULL;
}

static tbox_string_view tbox_context_copy_default(tbox_context *ctx, tbox_string_view value) {
    if (value.size == 0) return tbox_string_view_make(NULL, 0);
    char *copy = tbox_arena_alloc(&ctx->handler_arena, value.size);
    if (copy == NULL) return tbox_string_view_make(NULL, 0);
    memcpy(copy, value.data, value.size);
    return tbox_string_view_make(copy, value.size);
}

void tbox_context_capture_form_defaults(tbox_context *ctx, const tbox_html_node *node) {
    for (; node != NULL; node = node->next_sibling) {
        if (node->type == TBOX_HTML_NODE_ELEMENT) {
            bool input = tbox_string_view_equal_cstr(node->element.tag_name, "input");
            bool textarea = tbox_string_view_equal_cstr(node->element.tag_name, "textarea");
            bool option = tbox_string_view_equal_cstr(node->element.tag_name, "option");
            const tbox_html_node *form = tbox_context_nearest_form(node);
            if (form != NULL && (input || textarea || option)) {
                tbox_form_default *entry = tbox_arena_alloc_zero(&ctx->handler_arena, sizeof(*entry));
                if (entry != NULL) {
                    entry->node = node;
                    entry->form = form;
                    const tbox_html_attribute *value = input ?
                        tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5)) : NULL;
                    if (textarea) entry->value = tbox_context_copy_default(ctx,
                        tbox_html_node_text_content(&ctx->handler_arena, node));
                    else if (value != NULL) entry->value = tbox_context_copy_default(ctx, value->value);
                    entry->checked = input && tbox_html_node_get_attribute(node,
                        tbox_string_view_make("checked", 7)) != NULL;
                    entry->selected = option && tbox_html_node_get_attribute(node,
                        tbox_string_view_make("selected", 8)) != NULL;
                    entry->next = ctx->form_defaults;
                    ctx->form_defaults = entry;
                }
            }
        }
        tbox_context_capture_form_defaults(ctx, node->first_child);
    }
}

bool tbox_context_reset_form(tbox_context *ctx, const tbox_html_node *reset) {
    const tbox_html_node *form = tbox_context_nearest_form(reset);
    if (form == NULL) return false;
    for (tbox_form_default *entry = ctx->form_defaults; entry != NULL; entry = entry->next) {
        if (entry->form != form || !tbox_context_node_attached(ctx, entry->node) ||
            tbox_context_nearest_form(entry->node) != form) continue;
        tbox_html_node *node = (tbox_html_node *)entry->node;
        if (tbox_string_view_equal_cstr(node->element.tag_name, "input")) {
            tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("value", 5), entry->value);
            if (entry->checked)
                tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("checked", 7),
                    tbox_string_view_make(NULL, 0));
            else tbox_html_node_remove_attribute(node, tbox_string_view_make("checked", 7));
        } else if (tbox_string_view_equal_cstr(node->element.tag_name, "textarea"))
            tbox_html_node_set_text_content(ctx->document, node, entry->value);
        else if (entry->selected)
            tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("selected", 8),
                tbox_string_view_make(NULL, 0));
        else tbox_html_node_remove_attribute(node, tbox_string_view_make("selected", 8));
    }
    for (tbox_select_field *field = ctx->select_fields; field != NULL; field = field->next)
        if (tbox_context_nearest_form(field->node) == form) field->dirty = false;
    for (tbox_file_field *field = ctx->file_fields; field != NULL; field = field->next)
        if (tbox_context_nearest_form(field->node) == form) {
            free(field->path);
            field->path = NULL;
        }
    ctx->open_select = NULL;
    ctx->open_color = NULL;
    ctx->open_date = NULL;
    tbox_context_file_close(ctx);
    return true;
}

bool tbox_context_submit_form(tbox_context *ctx, const tbox_html_node *activator,
                                     const tbox_html_node *submitter) {
    const tbox_html_node *form = tbox_context_nearest_form(activator);
    if (form == NULL) return false;
    if (ctx->submit_handler != NULL)
        ctx->submit_handler(ctx, (tbox_html_node *)form, (tbox_html_node *)submitter,
            ctx->submit_userdata);
    return true;
}

bool tbox_context_is_button_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("button", 6));
}

bool tbox_context_is_reset_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5));
}

bool tbox_context_is_submit_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("submit", 6));
}

bool tbox_context_is_image_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("image", 5));
}

bool tbox_context_is_checkbox_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8));
}

bool tbox_context_is_radio_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("radio", 5));
}

static const tbox_html_node *tbox_context_radio_form(const tbox_html_node *node) {
    for (node = node->parent; node != NULL; node = node->parent)
        if (node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(node->element.tag_name, "form")) return node;
    return NULL;
}

static bool tbox_context_radio_same_group(const tbox_html_node *a, const tbox_html_node *b) {
    if (a == b || !tbox_context_is_radio_input(a) || !tbox_context_is_radio_input(b)) return false;
    const tbox_html_attribute *a_name = tbox_html_node_get_attribute(a, tbox_string_view_make("name", 4));
    const tbox_html_attribute *b_name = tbox_html_node_get_attribute(b, tbox_string_view_make("name", 4));
    return a_name != NULL && b_name != NULL && a_name->value.size > 0 &&
        tbox_string_view_equal(a_name->value, b_name->value) &&
        tbox_context_radio_form(a) == tbox_context_radio_form(b);
}

static bool tbox_context_radio_clear_peers(tbox_html_node *node, const tbox_html_node *chosen) {
    bool changed = false;
    for (; node != NULL; node = node->next_sibling) {
        if (tbox_context_radio_same_group(node, chosen) &&
            tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL) {
            tbox_html_node_remove_attribute(node, tbox_string_view_make("checked", 7));
            changed = true;
        }
        changed = tbox_context_radio_clear_peers(node->first_child, chosen) || changed;
    }
    return changed;
}

bool tbox_context_radio_select(tbox_context *ctx, const tbox_html_node *node) {
    bool changed = tbox_context_radio_clear_peers(
        (tbox_html_node *)tbox_html_document_root(ctx->document), node);
    if (tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) == NULL) {
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
            tbox_string_view_make("checked", 7), tbox_string_view_make(NULL, 0));
        changed = true;
    }
    return changed;
}

void tbox_context_toggle_checkbox(tbox_context *ctx, const tbox_html_node *node) {
    tbox_string_view checked = tbox_string_view_make("checked", 7);
    if (tbox_html_node_get_attribute(node, checked) != NULL)
        tbox_html_node_remove_attribute((tbox_html_node *)node, checked);
    else
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node, checked,
                                     tbox_string_view_make(NULL, 0));
}
