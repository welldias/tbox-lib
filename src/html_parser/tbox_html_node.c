#include "tbox_html_node.h"

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
