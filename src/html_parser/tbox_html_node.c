#include "tbox_html_node.h"

#include "base/tbox_string.h"
#include "tbox_html_document.h"

static const char *const tbox_html_void_elements[] = {
    "area",
    "base",
    "br",
    "col",
    "embed",
    "hr",
    "img",
    "input",
    "link",
    "meta",
    "param",
    "source",
    "track",
    "wbr",
};

bool tbox_html_is_void_element(tbox_string_view tag_name) {
    size_t count = sizeof(tbox_html_void_elements) / sizeof(tbox_html_void_elements[0]);
    for (size_t i = 0; i < count; i++) {
        if (tbox_string_view_equal_cstr(tag_name, tbox_html_void_elements[i])) {
            return true;
        }
    }
    return false;
}

tbox_html_node *tbox_html_node_create(tbox_html_document *document, tbox_html_node_type type) {
    tbox_html_node *node = tbox_arena_alloc_zero(&document->arena, sizeof(tbox_html_node));
    node->type           = type;
    node->owner_document = document;
    return node;
}

static tbox_string_view tbox_html_node_copy_string(tbox_arena *arena, tbox_string_view view);

tbox_html_node *tbox_html_element_create(tbox_html_document *document, tbox_string_view tag_name) {
    if (document == NULL || tag_name.size == 0) return NULL;
    tbox_html_node *node   = tbox_html_node_create(document, TBOX_HTML_NODE_ELEMENT);
    node->element.tag_name = tbox_string_copy_lower_ascii(&document->arena, tag_name);
    node->element.self_closing = tbox_html_is_void_element(node->element.tag_name);
    return node;
}

tbox_html_node *tbox_html_text_create(tbox_html_document *document, tbox_string_view text) {
    if (document == NULL) return NULL;
    tbox_html_node *node = tbox_html_node_create(document, TBOX_HTML_NODE_TEXT);
    node->text.text      = tbox_html_node_copy_string(&document->arena, text);
    return node;
}

/* Strings are copied, not shared: a text field binds its node's `value`
 * attribute to its own editing buffer (src/context/tbox_context_text.c),
 * which a clone must not alias. */
tbox_html_node *tbox_html_node_clone(tbox_html_document *document, const tbox_html_node *node, bool deep) {
    if (document == NULL || node == NULL || node->type == TBOX_HTML_NODE_DOCUMENT) return NULL;
    tbox_html_node *copy = tbox_html_node_create(document, node->type);
    if (node->type == TBOX_HTML_NODE_ELEMENT) {
        copy->element.tag_name     = tbox_html_node_copy_string(&document->arena, node->element.tag_name);
        copy->element.self_closing = node->element.self_closing;
        size_t count               = node->element.attribute_count;
        if (count > 0) {
            copy->element.attributes = tbox_arena_alloc(&document->arena, count * sizeof(tbox_html_attribute));
            for (size_t i = 0; i < count; i++) {
                copy->element.attributes[i].name  = tbox_html_node_copy_string(&document->arena, node->element.attributes[i].name);
                copy->element.attributes[i].value = tbox_html_node_copy_string(&document->arena, node->element.attributes[i].value);
            }
            copy->element.attribute_count = count;
        }
    } else {
        copy->text.text = tbox_html_node_copy_string(&document->arena, node->text.text);
    }
    if (deep)
        for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
            tbox_html_node_insert_before(copy, tbox_html_node_clone(document, child, true), NULL);
    return copy;
}

/* True when `child` may be inserted under `parent`: both exist, and `child`
 * is neither `parent` nor one of its ancestors (that would make a cycle). */
static bool tbox_html_node_can_insert(const tbox_html_node *parent, const tbox_html_node *child) {
    if (parent == NULL || child == NULL) return false;
    for (const tbox_html_node *at = parent; at != NULL; at = at->parent)
        if (at == child) return false;
    return true;
}

void tbox_html_node_append_child(tbox_html_node *parent, tbox_html_node *child) {
    tbox_html_node_insert_before(parent, child, NULL);
}

void tbox_html_node_insert_before(tbox_html_node *parent, tbox_html_node *child, tbox_html_node *reference) {
    if (!tbox_html_node_can_insert(parent, child) || child == reference) return;
    if (reference != NULL && reference->parent != parent) return;
    tbox_html_node_remove(child);

    tbox_html_node *prev = reference != NULL ? reference->prev_sibling : parent->last_child;
    child->parent        = parent;
    child->prev_sibling  = prev;
    child->next_sibling  = reference;
    if (prev != NULL) prev->next_sibling = child;
    else parent->first_child = child;
    if (reference != NULL) reference->prev_sibling = child;
    else parent->last_child = child;
    tbox_html_document_touch(parent->owner_document);
}

void tbox_html_node_prepend_child(tbox_html_node *parent, tbox_html_node *child) {
    if (parent == NULL) return;
    tbox_html_node_insert_before(parent, child, parent->first_child);
}

void tbox_html_node_replace(tbox_html_node *old_node, tbox_html_node *new_node) {
    if (old_node == NULL || old_node == new_node || old_node->parent == NULL) return;
    tbox_html_node *parent = old_node->parent;
    if (!tbox_html_node_can_insert(parent, new_node)) return;
    tbox_html_node_insert_before(parent, new_node, old_node);
    tbox_html_node_remove(old_node);
}

void tbox_html_node_remove_children(tbox_html_node *node) {
    if (node == NULL) return;
    while (node->first_child != NULL) tbox_html_node_remove(node->first_child);
}

tbox_html_node *tbox_html_node_first_element_child(const tbox_html_node *node) {
    if (node == NULL) return NULL;
    tbox_html_node *child = node->first_child;
    while (child != NULL && child->type != TBOX_HTML_NODE_ELEMENT) child = child->next_sibling;
    return child;
}

tbox_html_node *tbox_html_node_next_element_sibling(const tbox_html_node *node) {
    if (node == NULL) return NULL;
    tbox_html_node *sibling = node->next_sibling;
    while (sibling != NULL && sibling->type != TBOX_HTML_NODE_ELEMENT) sibling = sibling->next_sibling;
    return sibling;
}

size_t tbox_html_node_element_child_count(const tbox_html_node *node) {
    size_t count = 0;
    for (const tbox_html_node *child = tbox_html_node_first_element_child(node); child != NULL;
         child = tbox_html_node_next_element_sibling(child))
        count++;
    return count;
}

void tbox_html_node_remove(tbox_html_node *node) {
    if (node == NULL) {
        return;
    }

    tbox_html_node *parent = node->parent;
    tbox_html_node *prev   = node->prev_sibling;
    tbox_html_node *next   = node->next_sibling;

    if (prev != NULL) {
        prev->next_sibling = next;
    } else if (parent != NULL) {
        parent->first_child = next;
    }

    if (next != NULL) {
        next->prev_sibling = prev;
    } else if (parent != NULL) {
        parent->last_child = prev;
    }

    if (parent != NULL) tbox_html_document_touch(parent->owner_document);
    node->parent       = NULL;
    node->prev_sibling = NULL;
    node->next_sibling = NULL;
}

/* Appends the text of every TEXT descendant of `node` (not `node` itself) to
 * `builder`, in document order, recursing through ELEMENT children and
 * skipping COMMENT/DOCTYPE children (and their subtrees) entirely. */
static void tbox_html_node_append_descendant_text(const tbox_html_node *node, tbox_string_builder *builder) {
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        switch (child->type) {
        case TBOX_HTML_NODE_TEXT:
            tbox_string_builder_append_view(builder, child->text.text);
            break;
        case TBOX_HTML_NODE_ELEMENT:
            tbox_html_node_append_descendant_text(child, builder);
            break;
        case TBOX_HTML_NODE_DOCUMENT:
        case TBOX_HTML_NODE_COMMENT:
        case TBOX_HTML_NODE_DOCTYPE:
            break;
        }
    }
}

tbox_string_view tbox_html_node_text_content(tbox_arena *arena, const tbox_html_node *node) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, 0);

    if (node->type == TBOX_HTML_NODE_TEXT) {
        tbox_string_builder_append_view(&builder, node->text.text);
    } else {
        tbox_html_node_append_descendant_text(node, &builder);
    }

    return tbox_string_builder_finish(&builder);
}

/* Copies text set through the DOM API; malformed UTF-8 becomes U+FFFD,
 * the same invariant tbox_html_parse keeps for parsed text. */
static tbox_string_view tbox_html_node_copy_string(tbox_arena *arena, tbox_string_view view) {
    tbox_string_view sanitized = tbox_utf8_sanitize(arena, view);
    if (sanitized.data != view.data)
        return sanitized;
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, view.size);
    tbox_string_builder_append_view(&builder, view);
    return tbox_string_builder_finish(&builder);
}

/* Attribute names are always ASCII-lowercase, same invariant the parser
 * maintains for every attribute it produces (see tbox_html_attribute's
 * doc comment in html_parser.h) -- so an attribute added via
 * tbox_html_node_set_attribute is indistinguishable from one that came out
 * of the parser. */
void tbox_html_node_set_attribute(tbox_html_document *document, tbox_html_node *node, tbox_string_view name, tbox_string_view value) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return;
    }

    for (size_t i = 0; i < node->element.attribute_count; i++) {
        tbox_html_attribute *existing = &node->element.attributes[i];
        if (tbox_string_view_equal_ascii_ci(existing->name, name)) {
            existing->value = tbox_html_node_copy_string(&document->arena, value);
            tbox_html_document_touch(node->owner_document);
            return;
        }
    }

    size_t new_count                = node->element.attribute_count + 1;
    tbox_html_attribute *attributes = tbox_arena_alloc(&document->arena, new_count * sizeof(tbox_html_attribute));
    for (size_t i = 0; i < node->element.attribute_count; i++) {
        attributes[i] = node->element.attributes[i];
    }

    tbox_html_attribute *added = &attributes[node->element.attribute_count];
    added->name                = tbox_string_copy_lower_ascii(&document->arena, name);
    added->value               = tbox_html_node_copy_string(&document->arena, value);

    node->element.attributes      = attributes;
    node->element.attribute_count = new_count;
    tbox_html_document_touch(node->owner_document);
}

bool tbox_html_node_remove_attribute(tbox_html_node *node, tbox_string_view name) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) return false;
    for (size_t i = 0; i < node->element.attribute_count; i++) {
        if (!tbox_string_view_equal_ascii_ci(node->element.attributes[i].name, name)) continue;
        for (size_t j = i + 1; j < node->element.attribute_count; j++)
            node->element.attributes[j - 1] = node->element.attributes[j];
        node->element.attribute_count--;
        tbox_html_document_touch(node->owner_document);
        return true;
    }
    return false;
}

void tbox_html_node_set_text_content(tbox_html_document *document, tbox_html_node *node, tbox_string_view text) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return;
    }

    /* Detached one by one (not by zeroing first_child/last_child) so each
     * old child has no stale parent/sibling pointers: the application may
     * keep and re-insert it. */
    tbox_html_node_remove_children(node);

    tbox_html_node *text_node = tbox_html_node_create(document, TBOX_HTML_NODE_TEXT);
    text_node->text.text      = tbox_html_node_copy_string(&document->arena, text);
    tbox_html_node_append_child(node, text_node);
}

const tbox_html_attribute *tbox_html_node_get_attribute(const tbox_html_node *node, tbox_string_view name) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return NULL;
    }

    for (size_t i = 0; i < node->element.attribute_count; i++) {
        const tbox_html_attribute *attribute = &node->element.attributes[i];
        if (tbox_string_view_equal_ascii_ci(attribute->name, name)) {
            return attribute;
        }
    }
    return NULL;
}

bool tbox_html_node_has_attribute(const tbox_html_node *node, tbox_string_view name) {
    return node != NULL && tbox_html_node_get_attribute(node, name) != NULL;
}

static bool tbox_html_node_is_tag(const tbox_html_node *node, const char *tag) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, tag);
}

static bool tbox_html_node_has_disabled(const tbox_html_node *node) {
    return tbox_html_node_get_attribute(node, tbox_string_view_make("disabled", 8)) != NULL;
}

/* True when `child` is the first <legend> element child of `fieldset`. */
static bool tbox_html_node_is_first_legend(const tbox_html_node *fieldset, const tbox_html_node *child) {
    for (const tbox_html_node *at = fieldset->first_child; at != NULL; at = at->next_sibling)
        if (tbox_html_node_is_tag(at, "legend")) return at == child;
    return false;
}

bool tbox_html_node_disabled(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) return false;
    if (tbox_html_node_is_tag(node, "optgroup")) return tbox_html_node_has_disabled(node);
    if (tbox_html_node_is_tag(node, "option"))
        return tbox_html_node_has_disabled(node) ||
            (tbox_html_node_is_tag(node->parent, "optgroup") && tbox_html_node_has_disabled(node->parent));
    if (!tbox_html_node_is_tag(node, "button") && !tbox_html_node_is_tag(node, "input") &&
        !tbox_html_node_is_tag(node, "select") && !tbox_html_node_is_tag(node, "textarea") &&
        !tbox_html_node_is_tag(node, "fieldset")) return false;
    if (tbox_html_node_has_disabled(node)) return true;
    for (const tbox_html_node *child = node, *ancestor = node->parent; ancestor != NULL;
         child = ancestor, ancestor = ancestor->parent)
        if (tbox_html_node_is_tag(ancestor, "fieldset") && tbox_html_node_has_disabled(ancestor) &&
            !tbox_html_node_is_first_legend(ancestor, child)) return true;
    return false;
}

void tbox_html_node_set_bool_attribute(tbox_html_document *document, tbox_html_node *node, tbox_string_view name, bool on) {
    if (document == NULL || node == NULL) return;
    if (!on) tbox_html_node_remove_attribute(node, name);
    else if (!tbox_html_node_has_attribute(node, name)) tbox_html_node_set_attribute(document, node, name, tbox_string_view_make(NULL, 0));
}

/* Byte offset of the whitespace-separated token `name` inside `list`, or
 * list.size when absent. Class names compare case-sensitively, like the
 * class selector does. */
static size_t tbox_html_class_find(tbox_string_view list, tbox_string_view name) {
    size_t at = 0;
    while (at < list.size) {
        while (at < list.size && tbox_ascii_is_space(list.data[at])) at++;
        size_t start = at;
        while (at < list.size && !tbox_ascii_is_space(list.data[at])) at++;
        if (at > start && tbox_string_view_equal(tbox_string_view_make(list.data + start, at - start), name)) return start;
    }
    return list.size;
}

static bool tbox_html_class_valid(tbox_string_view name) {
    if (name.size == 0) return false;
    for (size_t i = 0; i < name.size; i++)
        if (tbox_ascii_is_space(name.data[i])) return false;
    return true;
}

static tbox_string_view tbox_html_node_class_list(const tbox_html_node *node) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("class", 5));
    return attribute != NULL ? attribute->value : tbox_string_view_make(NULL, 0);
}

bool tbox_html_node_has_class(const tbox_html_node *node, tbox_string_view name) {
    if (node == NULL || !tbox_html_class_valid(name)) return false;
    tbox_string_view list = tbox_html_node_class_list(node);
    return tbox_html_class_find(list, name) < list.size;
}

void tbox_html_node_add_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name) {
    if (document == NULL || node == NULL || node->type != TBOX_HTML_NODE_ELEMENT || !tbox_html_class_valid(name)) return;
    tbox_string_view list = tbox_html_node_class_list(node);
    if (tbox_html_class_find(list, name) < list.size) return;
    tbox_string_view trimmed = tbox_string_view_trim_ascii_space(list);
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, &document->arena, trimmed.size + 1 + name.size);
    tbox_string_builder_append_view(&builder, trimmed);
    if (trimmed.size > 0) tbox_string_builder_append_byte(&builder, ' ');
    tbox_string_builder_append_view(&builder, name);
    tbox_html_node_set_attribute(document, node, tbox_string_view_make("class", 5), tbox_string_builder_finish(&builder));
}

void tbox_html_node_remove_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name) {
    if (document == NULL || node == NULL || !tbox_html_class_valid(name)) return;
    tbox_string_view list = tbox_html_node_class_list(node);
    if (tbox_html_class_find(list, name) == list.size) return;
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, &document->arena, list.size);
    size_t at = 0;
    while (at < list.size) {
        while (at < list.size && tbox_ascii_is_space(list.data[at])) at++;
        size_t start = at;
        while (at < list.size && !tbox_ascii_is_space(list.data[at])) at++;
        tbox_string_view token = tbox_string_view_make(list.data + start, at - start);
        if (token.size == 0 || tbox_string_view_equal(token, name)) continue;
        if (builder.length > 0) tbox_string_builder_append_byte(&builder, ' ');
        tbox_string_builder_append_view(&builder, token);
    }
    tbox_html_node_set_attribute(document, node, tbox_string_view_make("class", 5), tbox_string_builder_finish(&builder));
}

bool tbox_html_node_toggle_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name) {
    if (tbox_html_node_has_class(node, name)) {
        tbox_html_node_remove_class(document, node, name);
        return false;
    }
    tbox_html_node_add_class(document, node, name);
    return tbox_html_node_has_class(node, name);
}
