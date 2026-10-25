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

/* Moves focus away (closing its popup) when it is on `node` or inside it. */
static void tbox_context_blur_within(tbox_context *ctx, const tbox_html_node *node) {
    for (const tbox_html_node *at = ctx->focused_node; at != NULL; at = at->parent)
        if (at == node) {
            tbox_context_set_focus(ctx, NULL);
            return;
        }
}

void tbox_context_control_set_disabled(tbox_context *ctx, tbox_html_node *control, bool disabled) {
    if (ctx == NULL || control == NULL) return;
    tbox_html_node_set_bool_attribute(ctx->document, control, tbox_string_view_make("disabled", 8), disabled);
    if (disabled) tbox_context_blur_within(ctx, control);
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

/* Generic elements: classes, `hidden`, and items instantiated from a
 * <template>. Options keep their choice in the context, so <select> and
 * <optgroup> are left to the tbox_context_select_* functions. */

static bool tbox_context_is_option_container(const tbox_html_node *node) {
    return tbox_context_is_select(node) || tbox_context_is_element(node, "optgroup");
}

static bool tbox_context_is_option_item(const tbox_html_node *node) {
    return tbox_context_is_element(node, "option") || tbox_context_is_element(node, "optgroup");
}

/* An element of the context's document: what every mutation below needs. */
static bool tbox_context_owns_element(const tbox_context *ctx, const tbox_html_node *node) {
    return ctx != NULL && node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && node->owner_document == ctx->document;
}

tbox_html_node *tbox_context_query_in(const tbox_context *ctx, const tbox_html_node *root, const char *selector, size_t length) {
    if (!tbox_context_owns_element(ctx, root)) return NULL;
    tbox_css_selector_query *query = tbox_css_selector_compile(selector, length, NULL);
    tbox_html_node *found          = (tbox_html_node *)tbox_css_selector_query_first(query, root);
    tbox_css_selector_query_destroy(query);
    return found;
}

bool tbox_context_control_has_class(const tbox_html_node *node, tbox_string_view name) {
    return tbox_html_node_has_class(node, name);
}

bool tbox_context_control_set_class(tbox_context *ctx, tbox_html_node *node, tbox_string_view name, bool on) {
    if (!tbox_context_owns_element(ctx, node) || name.size == 0) return false;
    for (size_t i = 0; i < name.size; i++)
        if (tbox_ascii_is_space(name.data[i])) return false;
    if (on)
        tbox_html_node_add_class(ctx->document, node, name);
    else
        tbox_html_node_remove_class(ctx->document, node, name);
    return true;
}

bool tbox_context_control_hidden(const tbox_html_node *node) {
    return node != NULL && tbox_html_node_has_attribute(node, tbox_string_view_make("hidden", 6));
}

void tbox_context_control_set_hidden(tbox_context *ctx, tbox_html_node *node, bool hidden) {
    if (!tbox_context_owns_element(ctx, node)) return;
    tbox_html_node_set_bool_attribute(ctx->document, node, tbox_string_view_make("hidden", 6), hidden);
    if (hidden) tbox_context_blur_within(ctx, node);
}

/* The element child of `parent` at `index`, or NULL past the end. */
static tbox_html_node *tbox_context_element_child_at(const tbox_html_node *parent, size_t index) {
    for (tbox_html_node *child = tbox_html_node_first_element_child(parent); child != NULL;
         child = tbox_html_node_next_element_sibling(child))
        if (index-- == 0) return child;
    return NULL;
}

tbox_html_node *tbox_context_control_instantiate(tbox_context *ctx, const tbox_html_node *templ, tbox_html_node *parent,
                                                 size_t before_index) {
    if (!tbox_context_owns_element(ctx, templ) || !tbox_context_is_element(templ, "template") ||
        !tbox_context_owns_element(ctx, parent) || tbox_context_is_option_container(parent))
        return NULL;
    /* A template inside `parent` would see its own copies while cloning. */
    for (const tbox_html_node *at = parent; at != NULL; at = at->parent)
        if (at == templ) return NULL;
    tbox_html_node *before = tbox_context_element_child_at(parent, before_index);
    tbox_html_node *first  = NULL;
    for (const tbox_html_node *child = tbox_html_node_first_element_child(templ); child != NULL;
         child = tbox_html_node_next_element_sibling(child)) {
        tbox_html_node *copy = tbox_html_node_clone(ctx->document, child, true);
        if (copy == NULL) break;
        tbox_html_node_insert_before(parent, copy, before);
        if (first == NULL) first = copy;
    }
    return first;
}

bool tbox_context_control_remove(tbox_context *ctx, tbox_html_node *node) {
    if (!tbox_context_owns_element(ctx, node) || node->parent == NULL || node->parent->type != TBOX_HTML_NODE_ELEMENT ||
        tbox_context_is_option_item(node))
        return false;
    tbox_context_blur_within(ctx, node);
    tbox_html_node_remove(node);
    return true;
}

bool tbox_context_control_clear(tbox_context *ctx, tbox_html_node *container) {
    if (!tbox_context_owns_element(ctx, container) || tbox_context_is_option_container(container)) return false;
    for (const tbox_html_node *child = container->first_child; child != NULL; child = child->next_sibling)
        tbox_context_blur_within(ctx, child);
    tbox_html_node_remove_children(container);
    return true;
}

/* Tables: plain tree walks in HTMLTableElement.rows order (see
 * <tbox/controls.h>), nothing kept in the context. */

static bool tbox_context_is_cell(const tbox_html_node *node) {
    return tbox_context_is_element(node, "td") || tbox_context_is_element(node, "th");
}

/* Where a child of the table puts its rows: 0 <thead>, 1 <tbody> or a
 * direct <tr>, 2 <tfoot>, -1 none. */
static int tbox_context_table_section_order(const tbox_html_node *child) {
    if (tbox_context_is_element(child, "thead")) return 0;
    if (tbox_context_is_element(child, "tbody") || tbox_context_is_element(child, "tr")) return 1;
    if (tbox_context_is_element(child, "tfoot")) return 2;
    return -1;
}

static tbox_html_node *tbox_context_first_child_element(const tbox_html_node *parent, const char *tag) {
    for (tbox_html_node *child = parent->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_element(child, tag)) return child;
    return NULL;
}

/* The first row of a section of `order`, scanning the table's children
 * from `child` on. */
static tbox_html_node *tbox_context_table_first_row_from(tbox_html_node *child, int order) {
    for (; child != NULL; child = child->next_sibling) {
        if (tbox_context_table_section_order(child) != order) continue;
        if (tbox_context_is_element(child, "tr")) return child;
        tbox_html_node *row = tbox_context_first_child_element(child, "tr");
        if (row != NULL) return row;
    }
    return NULL;
}

/* The row after `row` (NULL: the first row), or NULL after the last. */
static tbox_html_node *tbox_context_table_next_row(const tbox_html_node *table, const tbox_html_node *row) {
    int order = 0;
    if (row != NULL) {
        if (row->parent != table)
            for (tbox_html_node *next = row->next_sibling; next != NULL; next = next->next_sibling)
                if (tbox_context_is_element(next, "tr")) return next;
        const tbox_html_node *child = row->parent != table ? row->parent : row;
        order                       = tbox_context_table_section_order(child);
        tbox_html_node *next        = tbox_context_table_first_row_from(child->next_sibling, order);
        if (next != NULL) return next;
        order++;
    }
    for (; order <= 2; order++) {
        tbox_html_node *next = tbox_context_table_first_row_from(table->first_child, order);
        if (next != NULL) return next;
    }
    return NULL;
}

/* The table a row belongs to, or NULL. */
static tbox_html_node *tbox_context_row_table(const tbox_html_node *row) {
    if (!tbox_context_is_element(row, "tr") || row->parent == NULL) return NULL;
    tbox_html_node *parent = row->parent;
    if (tbox_context_is_element(parent, "table")) return parent;
    int order = tbox_context_table_section_order(parent);
    if (order < 0 || tbox_context_is_element(parent, "tr")) return NULL;
    return tbox_context_is_element(parent->parent, "table") ? parent->parent : NULL;
}

size_t tbox_context_table_row_count(const tbox_html_node *table) {
    if (!tbox_context_is_element(table, "table")) return 0;
    size_t count = 0;
    for (const tbox_html_node *row = tbox_context_table_next_row(table, NULL); row != NULL;
         row = tbox_context_table_next_row(table, row))
        count++;
    return count;
}

tbox_html_node *tbox_context_table_row_at(const tbox_html_node *table, size_t index) {
    if (!tbox_context_is_element(table, "table")) return NULL;
    tbox_html_node *row = tbox_context_table_next_row(table, NULL);
    for (size_t i = 0; row != NULL && i < index; i++) row = tbox_context_table_next_row(table, row);
    return row;
}

static tbox_html_node *tbox_context_table_new_cell(tbox_context *ctx, tbox_string_view text) {
    tbox_html_node *cell = tbox_html_element_create(ctx->document, tbox_string_view_make("td", 2));
    if (cell != NULL && text.size > 0) tbox_html_node_append_child(cell, tbox_html_text_create(ctx->document, text));
    return cell;
}

tbox_html_node *tbox_context_table_insert_row(tbox_context *ctx, tbox_html_node *table, size_t before_index,
                                              const tbox_string_view *cells, size_t count) {
    if (ctx == NULL || !tbox_context_is_element(table, "table")) return NULL;
    tbox_html_node *row = tbox_html_element_create(ctx->document, tbox_string_view_make("tr", 2));
    if (row == NULL) return NULL;
    for (size_t i = 0; cells != NULL && i < count; i++)
        tbox_html_node_append_child(row, tbox_context_table_new_cell(ctx, cells[i]));
    tbox_html_node *before = tbox_context_table_row_at(table, before_index);
    if (before != NULL) {
        tbox_html_node_insert_before(before->parent, row, before);
        return row;
    }
    tbox_html_node *body = NULL;
    for (tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_element(child, "tbody")) body = child;
    tbox_html_node_append_child(body != NULL ? body : table, row);
    return row;
}

bool tbox_context_table_remove_row(tbox_context *ctx, tbox_html_node *table, size_t index) {
    if (ctx == NULL) return false;
    tbox_html_node *row = tbox_context_table_row_at(table, index);
    if (row == NULL) return false;
    tbox_html_node_remove(row);
    return true;
}

static void tbox_context_remove_rows(tbox_html_node *parent) {
    for (tbox_html_node *child = parent->first_child, *next; child != NULL; child = next) {
        next = child->next_sibling;
        if (tbox_context_is_element(child, "tr")) tbox_html_node_remove(child);
    }
}

void tbox_context_table_clear_body(tbox_context *ctx, tbox_html_node *table) {
    if (ctx == NULL || !tbox_context_is_element(table, "table")) return;
    tbox_context_remove_rows(table);
    for (tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_element(child, "tbody")) tbox_context_remove_rows(child);
}

tbox_html_node *tbox_context_table_head(const tbox_html_node *table) {
    return tbox_context_is_element(table, "table") ? tbox_context_first_child_element(table, "thead") : NULL;
}

tbox_html_node *tbox_context_table_foot(const tbox_html_node *table) {
    return tbox_context_is_element(table, "table") ? tbox_context_first_child_element(table, "tfoot") : NULL;
}

size_t tbox_context_table_cell_count(const tbox_html_node *row) {
    if (!tbox_context_is_element(row, "tr")) return 0;
    size_t count = 0;
    for (const tbox_html_node *child = row->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_cell(child)) count++;
    return count;
}

tbox_html_node *tbox_context_table_cell_at(const tbox_html_node *row, size_t index) {
    if (!tbox_context_is_element(row, "tr")) return NULL;
    for (tbox_html_node *child = row->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_cell(child) && index-- == 0) return child;
    return NULL;
}

tbox_html_node *tbox_context_table_insert_cell(tbox_context *ctx, tbox_html_node *row, size_t before_index,
                                               tbox_string_view text) {
    if (ctx == NULL || !tbox_context_is_element(row, "tr")) return NULL;
    tbox_html_node *cell = tbox_context_table_new_cell(ctx, text);
    if (cell == NULL) return NULL;
    tbox_html_node_insert_before(row, cell, tbox_context_table_cell_at(row, before_index));
    return cell;
}

bool tbox_context_table_remove_cell(tbox_context *ctx, tbox_html_node *row, size_t index) {
    if (ctx == NULL) return false;
    tbox_html_node *cell = tbox_context_table_cell_at(row, index);
    if (cell == NULL) return false;
    tbox_html_node_remove(cell);
    return true;
}

tbox_string_view tbox_context_table_cell_text(tbox_context *ctx, const tbox_html_node *table, size_t row, size_t column) {
    const tbox_html_node *cell = tbox_context_table_cell_at(tbox_context_table_row_at(table, row), column);
    return ctx != NULL && cell != NULL ? tbox_context_node_text(ctx, cell) : tbox_string_view_make(NULL, 0);
}

bool tbox_context_table_set_cell_text(tbox_context *ctx, tbox_html_node *table, size_t row, size_t column,
                                      tbox_string_view text) {
    tbox_html_node *cell = tbox_context_table_cell_at(tbox_context_table_row_at(table, row), column);
    if (ctx == NULL || cell == NULL) return false;
    tbox_html_node_set_text_content(ctx->document, cell, text);
    return true;
}

tbox_html_node *tbox_context_table_row_of(const tbox_html_node *node) {
    for (; node != NULL && !tbox_context_is_element(node, "table"); node = node->parent)
        if (tbox_context_is_element(node, "tr")) return (tbox_html_node *)node;
    return NULL;
}

tbox_html_node *tbox_context_table_cell_of(const tbox_html_node *node) {
    for (; node != NULL && !tbox_context_is_element(node, "tr") && !tbox_context_is_element(node, "table");
         node = node->parent)
        if (tbox_context_is_cell(node)) return (tbox_html_node *)node;
    return NULL;
}

ptrdiff_t tbox_context_table_row_index(const tbox_html_node *row) {
    const tbox_html_node *table = tbox_context_row_table(row);
    if (table == NULL) return -1;
    ptrdiff_t index = 0;
    for (const tbox_html_node *it = tbox_context_table_next_row(table, NULL); it != NULL;
         it = tbox_context_table_next_row(table, it), index++)
        if (it == row) return index;
    return -1;
}

ptrdiff_t tbox_context_table_cell_index(const tbox_html_node *cell) {
    if (!tbox_context_is_cell(cell) || !tbox_context_is_element(cell->parent, "tr")) return -1;
    ptrdiff_t index = 0;
    for (const tbox_html_node *child = cell->parent->first_child; child != cell; child = child->next_sibling)
        if (tbox_context_is_cell(child)) index++;
    return index;
}
