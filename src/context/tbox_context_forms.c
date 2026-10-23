#include "tbox_context_internal.h"

static const tbox_html_node *tbox_context_form_owner(const tbox_html_node *node);
static tbox_string_view tbox_context_copy_default(tbox_context *ctx, tbox_string_view value);
static bool tbox_context_radio_same_group(const tbox_html_node *a, const tbox_html_node *b);
static bool tbox_context_radio_clear_peers(tbox_html_node *node, const tbox_html_node *chosen);

static bool tbox_context_is_form(const tbox_html_node *node) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "form");
}

/* HTML's form owner: a `form` attribute names the <form> by id (no owner
 * when the id is missing or not a form); otherwise the nearest ancestor
 * <form>. An option belongs to its select's form. */
static const tbox_html_node *tbox_context_form_owner(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) return NULL;
    if (tbox_string_view_equal_cstr(node->element.tag_name, "option"))
        for (const tbox_html_node *at = node->parent; at != NULL; at = at->parent)
            if (tbox_context_is_select(at)) {
                node = at;
                break;
            }
    const tbox_html_attribute *form = tbox_html_node_get_attribute(node, tbox_string_view_make("form", 4));
    if (form != NULL) {
        if (form->value.size == 0) return NULL;
        const tbox_html_node *root = node;
        while (root->parent != NULL) root = root->parent;
        const tbox_html_node *owner = tbox_context_find_id((tbox_html_node *)root, form->value);
        return tbox_context_is_form(owner) ? owner : NULL;
    }
    for (node = node->parent; node != NULL; node = node->parent)
        if (tbox_context_is_form(node)) return node;
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
            const tbox_html_node *form = tbox_context_form_owner(node);
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
    const tbox_html_node *form = tbox_context_form_owner(reset);
    if (form == NULL) return false;
    for (tbox_form_default *entry = ctx->form_defaults; entry != NULL; entry = entry->next) {
        if (entry->form != form || !tbox_context_node_attached(ctx, entry->node) ||
            tbox_context_form_owner(entry->node) != form) continue;
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
        if (tbox_context_form_owner(field->node) == form) field->dirty = false;
    for (tbox_file_field *field = ctx->file_fields; field != NULL; field = field->next)
        if (tbox_context_form_owner(field->node) == form) {
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
    const tbox_html_node *form = tbox_context_form_owner(activator);
    if (form == NULL) return false;
    tbox_context_emit_submit(ctx, form, submitter);
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

typedef enum tbox_button_type {
    TBOX_BUTTON_TYPE_NONE, /* not a <button> */
    TBOX_BUTTON_TYPE_SUBMIT,
    TBOX_BUTTON_TYPE_RESET,
    TBOX_BUTTON_TYPE_BUTTON
} tbox_button_type;

/* A missing or unknown `type` makes a <button> a submit button, as in HTML. */
static tbox_button_type tbox_context_button_type(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "button")) return TBOX_BUTTON_TYPE_NONE;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    if (type == NULL) return TBOX_BUTTON_TYPE_SUBMIT;
    if (tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5))) return TBOX_BUTTON_TYPE_RESET;
    if (tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("button", 6))) return TBOX_BUTTON_TYPE_BUTTON;
    return TBOX_BUTTON_TYPE_SUBMIT;
}

bool tbox_context_is_submit_button(const tbox_html_node *node) {
    return tbox_context_is_submit_input(node) || tbox_context_button_type(node) == TBOX_BUTTON_TYPE_SUBMIT;
}

bool tbox_context_is_reset_button(const tbox_html_node *node) {
    return tbox_context_is_reset_input(node) || tbox_context_button_type(node) == TBOX_BUTTON_TYPE_RESET;
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

static bool tbox_context_radio_same_group(const tbox_html_node *a, const tbox_html_node *b) {
    if (a == b || !tbox_context_is_radio_input(a) || !tbox_context_is_radio_input(b)) return false;
    const tbox_html_attribute *a_name = tbox_html_node_get_attribute(a, tbox_string_view_make("name", 4));
    const tbox_html_attribute *b_name = tbox_html_node_get_attribute(b, tbox_string_view_make("name", 4));
    return a_name != NULL && b_name != NULL && a_name->value.size > 0 &&
        tbox_string_view_equal(a_name->value, b_name->value) &&
        tbox_context_form_owner(a) == tbox_context_form_owner(b);
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

static const tbox_html_node *tbox_context_radio_find_checked(const tbox_html_node *node, const tbox_html_node *chosen) {
    for (; node != NULL; node = node->next_sibling) {
        if (tbox_context_radio_same_group(node, chosen) &&
            tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL) return node;
        const tbox_html_node *found = tbox_context_radio_find_checked(node->first_child, chosen);
        if (found != NULL) return found;
    }
    return NULL;
}

const tbox_html_node *tbox_context_radio_checked_peer(tbox_context *ctx, const tbox_html_node *node) {
    return tbox_context_radio_find_checked(tbox_html_document_root(ctx->document), node);
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
