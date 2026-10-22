#include "tbox_context_internal.h"

static const tbox_html_node *tbox_context_next_node(const tbox_html_node *root, const tbox_html_node *node);

bool tbox_context_is_summary(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "summary") ||
        node->parent == NULL || node->parent->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->parent->element.tag_name, "details")) return false;
    for (const tbox_html_node *child = node->parent->first_child; child != NULL; child = child->next_sibling)
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "summary"))
            return child == node;
    return false;
}

bool tbox_context_toggle_details(tbox_context *ctx, const tbox_html_node *summary) {
    if (ctx == NULL || !tbox_context_is_summary(summary)) return false;
    tbox_html_node *details = summary->parent;
    if (!tbox_html_node_remove_attribute(details, tbox_string_view_make("open", 4)))
        tbox_html_node_set_attribute(ctx->document, details, tbox_string_view_make("open", 4), tbox_string_view_make(NULL, 0));
    return true;
}

bool tbox_context_update_hover(tbox_context *ctx, bool has_position, double x, double y) {
    if (ctx == NULL) {
        return false;
    }

    /* Over the open select list, the pointer moves its highlight. */
    bool highlight_changed = false;
    if (has_position && tbox_context_select_popup_hover(ctx, x, y, &highlight_changed))
        return highlight_changed;

    /* A scrollbar thumb under the pointer paints its :hover variant. */
    const tbox_html_node *thumb = has_position ? tbox_context_scrollbar_thumb_at(ctx, x, y) : NULL;
    bool thumb_changed          = thumb != ctx->scroll_thumb_hover;
    ctx->scroll_thumb_hover     = thumb;

    /* `has_position == false` (pointer left the window) or nothing under
     * the point both mean "nothing hovered" -- new_hovered stays NULL in
     * either case. */
    const tbox_html_node *new_hovered = NULL;
    if (has_position) {
        const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
        if (box != NULL) {
            new_hovered = box->node;
        }
    }

    if (new_hovered == ctx->hovered_node) {
        return thumb_changed;
    }

    ctx->hovered_node = new_hovered;
    return true;
}

bool tbox_context_set_active_at(tbox_context *ctx, bool pressed, double x, double y) {
    if (ctx == NULL) return false;
    bool visibility_changed = pressed && tbox_context_focus_visibility(ctx, false);
    const tbox_html_node *node = NULL;
    if (pressed) {
        const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
        while (box != NULL && box->node == NULL) box = box->parent;
        if (box != NULL && box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT)
            node = box->node;
    }
    if (ctx->active_node == node) return visibility_changed;
    ctx->active_node = node;
    return true;
}

static const tbox_html_node *tbox_context_next_node(const tbox_html_node *root, const tbox_html_node *node) {
    if (node->first_child != NULL) {
        return node->first_child;
    }
    while (node != root) {
        if (node->next_sibling != NULL) {
            return node->next_sibling;
        }
        node = node->parent;
    }
    return NULL;
}

bool tbox_context_focusable(const tbox_context *ctx, const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }

    tbox_string_view tag = node->element.tag_name;
    bool control = tbox_string_view_equal_cstr(tag, "button") || tbox_context_is_button_input(node) ||
                   tbox_context_is_reset_input(node) || tbox_context_is_submit_input(node) ||
                   tbox_context_is_image_input(node) ||
                   tbox_context_is_checkbox_input(node) || tbox_context_is_radio_input(node) ||
                   tbox_context_is_color_input(node) || tbox_context_is_range_input(node) ||
                   tbox_context_is_calendar_input(node) || tbox_context_is_file_input(node) ||
                   tbox_context_is_text_control(node) ||
                   tbox_context_is_select(node) || tbox_context_is_summary(node) ||
                   (tbox_string_view_equal_cstr(tag, "area") &&
                    tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("href")) != NULL);
    if (!control || tbox_html_node_get_attribute(node, tbox_string_view_make("disabled", 8)) != NULL) {
        return false;
    }

    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent) {
        if (ancestor->type != TBOX_HTML_NODE_ELEMENT) {
            continue;
        }
        if (tbox_html_node_get_attribute(ancestor, tbox_string_view_make("hidden", 6)) != NULL) {
            return false;
        }
        if (ancestor != node && tbox_string_view_equal_cstr(ancestor->element.tag_name, "details") &&
            tbox_html_node_get_attribute(ancestor, tbox_string_view_make("open", 4)) == NULL) {
            const tbox_html_node *direct = node;
            while (direct->parent != ancestor) direct = direct->parent;
            if (!tbox_context_is_summary(direct)) return false;
        }
        const tbox_style *style = tbox_style_table_find(&ctx->styles, ancestor);
        if (style != NULL && style->display == TBOX_STYLE_DISPLAY_NONE) {
            return false;
        }
    }
    const tbox_style *own_style = tbox_style_table_find(&ctx->styles, node);
    if (own_style != NULL && own_style->visibility_hidden) return false;
    return true;
}

bool tbox_context_focus_visibility(tbox_context *ctx, bool keyboard) {
    if (ctx == NULL) return false;
    ctx->focus_keyboard = keyboard;
    if (ctx->focused_node == NULL) return false;
    tbox_html_node *node = (tbox_html_node *)ctx->focused_node;
    bool visible = keyboard || tbox_context_is_text_control(node);
    bool changed = node->element.focus_visible != visible;
    node->element.focus_visible = visible;
    return changed;
}

bool tbox_context_set_focus(tbox_context *ctx, const tbox_html_node *node) {
    if (ctx->focused_node == node) {
        return tbox_context_focus_visibility(ctx, ctx->focus_keyboard);
    }
    if (ctx->focused_node != NULL)
        ((tbox_html_node *)ctx->focused_node)->element.focus_visible = false;
    ctx->focused_node = node;
    tbox_html_document_touch(ctx->document);
    tbox_context_focus_visibility(ctx, ctx->focus_keyboard);
    if (ctx->open_select != node) {
        ctx->open_select = NULL;
        ctx->popup_highlight = NULL;
    }
    if (ctx->open_color != node) ctx->open_color = NULL;
    if (ctx->open_date != node) ctx->open_date = NULL;
    if (ctx->open_file != node) tbox_context_file_close(ctx);
    tbox_css_selector_set_focus_context(node);
    return true;
}

bool tbox_context_move_focus(tbox_context *ctx, bool reverse) {
    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    const tbox_html_node *current = ctx->focused_node;
    if (current != NULL && (!tbox_context_node_attached(ctx, current) || !tbox_context_focusable(ctx, current))) {
        current = NULL;
    }

    const tbox_html_node *first = NULL;
    const tbox_html_node *last = NULL;
    const tbox_html_node *before = NULL;
    const tbox_html_node *after = NULL;
    bool seen_current = false;
    for (const tbox_html_node *node = root; node != NULL; node = tbox_context_next_node(root, node)) {
        if (!tbox_context_focusable(ctx, node)) {
            continue;
        }
        if (first == NULL) first = node;
        last = node;
        if (node == current) {
            seen_current = true;
        } else if (!seen_current) {
            before = node;
        } else if (after == NULL) {
            after = node;
        }
    }

    const tbox_html_node *next = reverse ? (current == NULL ? last : (before != NULL ? before : last))
                                         : (current == NULL ? first : (after != NULL ? after : first));
    bool changed = tbox_context_set_focus(ctx, next);
    if (changed) ctx->focus_scroll_pending = true;
    return changed;
}

const tbox_html_node *tbox_context_focused_node(const tbox_context *ctx) {
    return ctx == NULL || ctx->focused_node == NULL || !tbox_context_node_attached(ctx, ctx->focused_node)
               ? NULL : ctx->focused_node;
}
