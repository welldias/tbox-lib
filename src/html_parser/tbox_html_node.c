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
    return node;
}

void tbox_html_node_append_child(tbox_html_node *parent, tbox_html_node *child) {
    child->parent       = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child != NULL) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
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

static tbox_string_view tbox_html_node_copy_string(tbox_arena *arena, tbox_string_view view) {
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
static tbox_string_view tbox_html_node_copy_string_lower(tbox_arena *arena, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, view.size);
    tbox_string_builder_append_view_lower_ascii(&builder, view);
    return tbox_string_builder_finish(&builder);
}

void tbox_html_node_set_attribute(tbox_html_document *document, tbox_html_node *node, tbox_string_view name, tbox_string_view value) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return;
    }

    for (size_t i = 0; i < node->element.attribute_count; i++) {
        tbox_html_attribute *existing = &node->element.attributes[i];
        if (tbox_string_view_equal_ascii_ci(existing->name, name)) {
            existing->value = tbox_html_node_copy_string(&document->arena, value);
            return;
        }
    }

    size_t new_count                = node->element.attribute_count + 1;
    tbox_html_attribute *attributes = tbox_arena_alloc(&document->arena, new_count * sizeof(tbox_html_attribute));
    for (size_t i = 0; i < node->element.attribute_count; i++) {
        attributes[i] = node->element.attributes[i];
    }

    tbox_html_attribute *added = &attributes[node->element.attribute_count];
    added->name                = tbox_html_node_copy_string_lower(&document->arena, name);
    added->value               = tbox_html_node_copy_string(&document->arena, value);

    node->element.attributes      = attributes;
    node->element.attribute_count = new_count;
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
