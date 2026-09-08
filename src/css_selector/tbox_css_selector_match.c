#include "tbox_css_selector_match.h"

#include <string.h>

#include "base/tbox_string.h"

static const tbox_html_attribute *tbox_css_selector_find_attribute(const tbox_html_node *node, tbox_string_view name) {
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

static const tbox_html_node *tbox_css_selector_prev_element_sibling(const tbox_html_node *node) {
    for (const tbox_html_node *sibling = node->prev_sibling; sibling != NULL; sibling = sibling->prev_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT) {
            return sibling;
        }
    }
    return NULL;
}

static const tbox_html_node *tbox_css_selector_next_element_sibling(const tbox_html_node *node) {
    for (const tbox_html_node *sibling = node->next_sibling; sibling != NULL; sibling = sibling->next_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT) {
            return sibling;
        }
    }
    return NULL;
}

/* CSS2.1 "white space": space, tab, line feed, carriage return, form feed. */
static bool tbox_css_selector_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

/* True if `token` appears as one whole whitespace-separated item of `list`
 * (used for CLASS and the '~=' attribute operator). Comparison is byte-exact
 * (case-sensitive), per CSS2.1. */
static bool tbox_css_selector_token_list_contains(tbox_string_view list, tbox_string_view token) {
    size_t i = 0;
    while (i < list.size) {
        while (i < list.size && tbox_css_selector_is_space(list.data[i])) {
            i++;
        }
        size_t start = i;
        while (i < list.size && !tbox_css_selector_is_space(list.data[i])) {
            i++;
        }
        if (i > start) {
            tbox_string_view item = tbox_string_view_make(list.data + start, i - start);
            if (tbox_string_view_equal(item, token)) {
                return true;
            }
        }
    }
    return false;
}

/* '|=' : value equals `prefix`, or value equals prefix followed by '-'. */
static bool tbox_css_selector_dash_match(tbox_string_view value, tbox_string_view prefix) {
    if (tbox_string_view_equal(value, prefix)) {
        return true;
    }
    return value.size > prefix.size && memcmp(value.data, prefix.data, prefix.size) == 0 && value.data[prefix.size] == '-';
}

static bool tbox_css_selector_matches_simple_selector(const tbox_css_simple_selector *item, const tbox_html_node *node) {
    switch (item->kind) {
    case TBOX_CSS_SIMPLE_SELECTOR_TYPE:
        return node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_ascii_ci(node->element.tag_name, item->name);

    case TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL:
        return node->type == TBOX_HTML_NODE_ELEMENT;

    case TBOX_CSS_SIMPLE_SELECTOR_ID: {
        const tbox_html_attribute *id = tbox_css_selector_find_attribute(node, tbox_string_view_make("id", 2));
        return id != NULL && tbox_string_view_equal(id->value, item->name);
    }

    case TBOX_CSS_SIMPLE_SELECTOR_CLASS: {
        const tbox_html_attribute *class_attr = tbox_css_selector_find_attribute(node, tbox_string_view_make("class", 5));
        return class_attr != NULL && tbox_css_selector_token_list_contains(class_attr->value, item->name);
    }

    case TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE: {
        const tbox_html_attribute *attribute = tbox_css_selector_find_attribute(node, item->name);
        if (attribute == NULL) {
            return false;
        }
        switch (item->attribute_operator) {
        case TBOX_CSS_ATTR_EXISTS:
            return true;
        case TBOX_CSS_ATTR_EQUALS:
            return tbox_string_view_equal(attribute->value, item->attribute_value);
        case TBOX_CSS_ATTR_INCLUDES:
            return tbox_css_selector_token_list_contains(attribute->value, item->attribute_value);
        case TBOX_CSS_ATTR_DASHMATCH:
            return tbox_css_selector_dash_match(attribute->value, item->attribute_value);
        }
        return false;
    }

    case TBOX_CSS_SIMPLE_SELECTOR_PSEUDO:
        /* Only the structural pseudo-classes computable from tree shape
         * alone are supported; see <tbox/css_selector.h>'s top comment for
         * the rationale. Everything else (dynamic state, generated content)
         * never matches. */
        if (tbox_string_view_equal_cstr(item->name, "first-child")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && tbox_css_selector_prev_element_sibling(node) == NULL;
        }
        if (tbox_string_view_equal_cstr(item->name, "last-child")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && tbox_css_selector_next_element_sibling(node) == NULL;
        }
        return false;
    }

    return false;
}

static bool tbox_css_selector_matches_compound(const tbox_css_simple_selector *items, size_t start, size_t end, const tbox_html_node *node) {
    for (size_t i = start; i < end; i++) {
        if (!tbox_css_selector_matches_simple_selector(&items[i], node)) {
            return false;
        }
    }
    return true;
}

/* Tests whether `node` satisfies items[0..count) as a selector suffix: the
 * rightmost compound (items[compound_start..count)) must match `node`
 * itself; if a compound precedes it, its connecting combinator says where to
 * look for a node satisfying items[0..compound_start) -- CHILD only checks
 * node->parent, ADJACENT_SIBLING only the immediately preceding element
 * sibling, and DESCENDANT backtracks over every ancestor in turn (matching
 * one ancestor to the previous compound does not guarantee the rest of the
 * chain resolves from there, so this must keep trying ancestors rather than
 * stopping at the first one that matches). */
static bool tbox_css_selector_matches_suffix(const tbox_css_simple_selector *items, size_t count, const tbox_html_node *node) {
    if (count == 0) {
        return true;
    }
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }

    size_t compound_start = count - 1;
    while (compound_start > 0 && items[compound_start].combinator_before == TBOX_CSS_COMBINATOR_NONE) {
        compound_start--;
    }

    if (!tbox_css_selector_matches_compound(items, compound_start, count, node)) {
        return false;
    }
    if (compound_start == 0) {
        return true;
    }

    switch (items[compound_start].combinator_before) {
    case TBOX_CSS_COMBINATOR_CHILD:
        return tbox_css_selector_matches_suffix(items, compound_start, node->parent);

    case TBOX_CSS_COMBINATOR_ADJACENT_SIBLING:
        return tbox_css_selector_matches_suffix(items, compound_start, tbox_css_selector_prev_element_sibling(node));

    case TBOX_CSS_COMBINATOR_DESCENDANT:
        for (const tbox_html_node *ancestor = node->parent; ancestor != NULL; ancestor = ancestor->parent) {
            if (tbox_css_selector_matches_suffix(items, compound_start, ancestor)) {
                return true;
            }
        }
        return false;

    case TBOX_CSS_COMBINATOR_NONE:
        break; /* unreachable: compound_start > 0 implies a combinator ended the scan above */
    }
    return false;
}

bool tbox_css_selector_matches(const tbox_css_selector *selector, const tbox_html_node *node) {
    if (selector == NULL || node == NULL) {
        return false;
    }
    return tbox_css_selector_matches_suffix(selector->simple_selectors, selector->simple_selector_count, node);
}

static void tbox_css_selector_collect_recursive(const tbox_html_node *node, const tbox_css_selector *selectors, size_t selector_count, tbox_vector *out) {
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == TBOX_HTML_NODE_ELEMENT) {
            for (size_t i = 0; i < selector_count; i++) {
                if (tbox_css_selector_matches(&selectors[i], child)) {
                    *(const tbox_html_node **)tbox_vector_push(out) = child;
                    break;
                }
            }
        }
        tbox_css_selector_collect_recursive(child, selectors, selector_count, out);
    }
}

void tbox_css_selector_collect_matching_nodes(const tbox_html_node *root, const tbox_css_selector *selectors, size_t selector_count, tbox_vector *out) {
    if (root == NULL) {
        return;
    }
    tbox_css_selector_collect_recursive(root, selectors, selector_count, out);
}
