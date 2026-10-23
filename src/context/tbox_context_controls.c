#include "tbox_context_internal.h"

static bool tbox_context_is_element(const tbox_html_node *node, const char *tag) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, tag);
}

/* Buttons whose caption is their `value` attribute. */
static bool tbox_context_is_caption_input(const tbox_html_node *node) {
    return tbox_context_is_button_input(node) || tbox_context_is_submit_input(node) || tbox_context_is_reset_input(node);
}

static tbox_string_view tbox_context_attribute_value(const tbox_html_node *node, const char *name) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL ? attribute->value : tbox_string_view_make(NULL, 0);
}

/* A single text child is read in place (a textarea's is the live editing
 * buffer); anything else is concatenated into the frame arena. */
static tbox_string_view tbox_context_node_text(tbox_context *ctx, const tbox_html_node *node) {
    if (node->first_child == NULL) return tbox_string_view_make(NULL, 0);
    if (node->first_child == node->last_child && node->first_child->type == TBOX_HTML_NODE_TEXT)
        return node->first_child->text.text;
    return tbox_html_node_text_content(&ctx->frame_arena, node);
}

tbox_string_view tbox_context_control_value(tbox_context *ctx, const tbox_html_node *control) {
    tbox_string_view empty = tbox_string_view_make(NULL, 0);
    if (ctx == NULL || control == NULL || control->type != TBOX_HTML_NODE_ELEMENT) return empty;
    if (tbox_context_is_select(control)) return tbox_context_select_value(ctx, control);
    if (tbox_context_is_textarea(control) || tbox_context_is_element(control, "output"))
        return tbox_context_node_text(ctx, control);
    if (tbox_context_is_element(control, "input")) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(control, tbox_string_view_make("value", 5));
        if (value == NULL && (tbox_context_is_checkbox_input(control) || tbox_context_is_radio_input(control)))
            return tbox_string_view_make("on", 2);
        return value != NULL ? value->value : empty;
    }
    if (tbox_context_is_element(control, "button") || tbox_context_is_element(control, "progress") ||
        tbox_context_is_element(control, "meter") || tbox_context_is_element(control, "option"))
        return tbox_context_attribute_value(control, "value");
    return empty;
}

/* A text field already editing `node` re-reads the new DOM value now
 * (caret to the end), instead of at its next use. */
static void tbox_context_resync_text_field(tbox_context *ctx, const tbox_html_node *node) {
    for (tbox_text_field *field = ctx->text_fields; field != NULL; field = field->next)
        if (field->node == node) {
            tbox_context_text_field(ctx, node);
            return;
        }
}

bool tbox_context_control_set_value(tbox_context *ctx, tbox_html_node *control, tbox_string_view value) {
    if (ctx == NULL || control == NULL || control->type != TBOX_HTML_NODE_ELEMENT) return false;
    if (tbox_context_is_select(control)) return tbox_context_select_set_value(ctx, control, value);
    if (tbox_context_is_textarea(control) || tbox_context_is_element(control, "output")) {
        tbox_html_node_set_text_content(ctx->document, control, value);
        tbox_context_resync_text_field(ctx, control);
        return true;
    }
    bool input = tbox_context_is_element(control, "input");
    if (!input && !tbox_context_is_element(control, "button") && !tbox_context_is_element(control, "progress") &&
        !tbox_context_is_element(control, "meter") && !tbox_context_is_element(control, "option"))
        return false;
    if (tbox_context_is_file_input(control) && value.size > 0) return false;
    tbox_html_node_set_attribute(ctx->document, control, tbox_string_view_make("value", 5), value);
    if (input) {
        tbox_context_sanitize_input(ctx, control);
        tbox_context_resync_text_field(ctx, control);
    }
    return true;
}

tbox_string_view tbox_context_control_label(tbox_context *ctx, const tbox_html_node *control) {
    tbox_string_view empty = tbox_string_view_make(NULL, 0);
    if (ctx == NULL || control == NULL || control->type != TBOX_HTML_NODE_ELEMENT) return empty;
    if (tbox_context_is_caption_input(control)) return tbox_context_attribute_value(control, "value");
    if (tbox_context_is_element(control, "input") || tbox_context_is_select(control) || tbox_context_is_textarea(control))
        return empty;
    if (tbox_context_is_element(control, "option")) return tbox_context_option_label(ctx, control);
    return tbox_context_node_text(ctx, control);
}

bool tbox_context_control_set_label(tbox_context *ctx, tbox_html_node *control, tbox_string_view text) {
    if (ctx == NULL || control == NULL || control->type != TBOX_HTML_NODE_ELEMENT) return false;
    if (tbox_context_is_caption_input(control)) {
        tbox_html_node_set_attribute(ctx->document, control, tbox_string_view_make("value", 5), text);
        return true;
    }
    if (tbox_context_is_element(control, "input") || tbox_context_is_select(control) || tbox_context_is_textarea(control))
        return false;
    const tbox_html_attribute *label = tbox_context_is_element(control, "option") ?
        tbox_html_node_get_attribute(control, tbox_string_view_make("label", 5)) : NULL;
    if (label != NULL && label->value.size > 0)
        tbox_html_node_set_attribute(ctx->document, control, tbox_string_view_make("label", 5), text);
    else
        tbox_html_node_set_text_content(ctx->document, control, text);
    return true;
}

bool tbox_context_control_disabled(const tbox_html_node *control) {
    return tbox_html_node_disabled(control);
}

void tbox_context_control_set_disabled(tbox_context *ctx, tbox_html_node *control, bool disabled) {
    if (ctx == NULL || control == NULL) return;
    tbox_html_node_set_bool_attribute(ctx->document, control, tbox_string_view_make("disabled", 8), disabled);
    if (!disabled || ctx->focused_node == NULL) return;
    for (const tbox_html_node *at = ctx->focused_node; at != NULL; at = at->parent)
        if (at == control) {
            tbox_context_set_focus(ctx, NULL);
            return;
        }
}

bool tbox_context_control_focus(tbox_context *ctx, tbox_html_node *control) {
    if (ctx == NULL || control == NULL || !tbox_context_node_attached(ctx, control) ||
        !tbox_context_focusable(ctx, control)) return false;
    if (ctx->focused_node != control) ctx->focus_scroll_pending = true;
    tbox_context_set_focus(ctx, control);
    return true;
}

void tbox_context_control_blur(tbox_context *ctx) {
    if (ctx != NULL) tbox_context_set_focus(ctx, NULL);
}

bool tbox_context_control_checked(const tbox_html_node *control) {
    return (tbox_context_is_checkbox_input(control) || tbox_context_is_radio_input(control)) &&
        tbox_html_node_has_attribute(control, tbox_string_view_make("checked", 7));
}

bool tbox_context_control_set_checked(tbox_context *ctx, tbox_html_node *control, bool checked) {
    if (ctx == NULL) return false;
    if (tbox_context_is_radio_input(control) && checked) {
        tbox_context_radio_select(ctx, control);
        return true;
    }
    if (!tbox_context_is_checkbox_input(control) && !tbox_context_is_radio_input(control)) return false;
    tbox_html_node_set_bool_attribute(ctx->document, control, tbox_string_view_make("checked", 7), checked);
    return true;
}

tbox_html_node *tbox_context_control_radio_checked(tbox_context *ctx, const tbox_html_node *radio) {
    if (ctx == NULL || !tbox_context_is_radio_input(radio)) return NULL;
    if (tbox_context_control_checked(radio)) return (tbox_html_node *)radio;
    return (tbox_html_node *)tbox_context_radio_checked_peer(ctx, radio);
}
