#include "tbox_css_selector_match.h"

#include <stdint.h>
#include <string.h>

#include "base/tbox_string.h"

/* Current hover target for the "hover" PSEUDO, set by
 * tbox_css_selector_set_hover_context. The one global/static mutable state
 * this file introduces (see <tbox/css_selector.h> and ARCHITECTURE.md's
 * "`:hover` -- pseudo-classe dinâmica de verdade"): a single, non-partitioned
 * mouse pointer justifies "what's hovered now" being process-global rather
 * than threaded through every selector-matching signature. */
static const tbox_html_node *tbox_css_selector_hovered_node = NULL;
static const tbox_html_node *tbox_css_selector_focused_node = NULL;
static const tbox_html_node *tbox_css_selector_active_node = NULL;

void tbox_css_selector_set_hover_context(const tbox_html_node *hovered) {
    tbox_css_selector_hovered_node = hovered;
}

void tbox_css_selector_set_focus_context(const tbox_html_node *focused) {
    tbox_css_selector_focused_node = focused;
}

void tbox_css_selector_set_active_context(const tbox_html_node *active) {
    tbox_css_selector_active_node = active;
}

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

static bool tbox_css_selector_has_same_type_sibling(const tbox_html_node *node, bool previous) {
    for (const tbox_html_node *sibling = previous ? node->prev_sibling : node->next_sibling;
         sibling != NULL;
         sibling = previous ? sibling->prev_sibling : sibling->next_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_ascii_ci(sibling->element.tag_name, node->element.tag_name))
            return true;
    }
    return false;
}

/* Parse the useful CSS an+b forms without allocating: odd/even, an+b and
 * a positive index. Reject malformed expressions instead of matching them. */
static bool tbox_css_selector_parse_nth(tbox_string_view argument, int64_t *a, int64_t *b) {
    char value[96];
    size_t length = 0;
    for (size_t i = 0; i < argument.size; i++) {
        char ch = argument.data[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f') continue;
        if (length + 1 >= sizeof(value)) return false;
        value[length++] = ch >= 'A' && ch <= 'Z' ? (char)(ch + ('a' - 'A')) : ch;
    }
    value[length] = '\0';
    if (strcmp(value, "odd") == 0) { *a = 2; *b = 1; return true; }
    if (strcmp(value, "even") == 0) { *a = 2; *b = 0; return true; }
    if (length == 0) return false;

    size_t n_pos = 0;
    while (n_pos < length && value[n_pos] != 'n') n_pos++;
    size_t cursor = 0;
    int64_t sign = 1;
    if (value[cursor] == '+' || value[cursor] == '-') {
        if (value[cursor] == '-') sign = -1;
        cursor++;
    }
    int64_t number = 0;
    size_t digits = cursor;
    size_t limit = n_pos < length ? n_pos : length;
    while (cursor < limit && value[cursor] >= '0' && value[cursor] <= '9') {
        int64_t digit = value[cursor++] - '0';
        if (number > (INT32_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    if (cursor != limit) return false;
    if (n_pos == length) {
        if (digits == cursor) return false;
        *a = 0;
        *b = sign * number;
        return true;
    }
    *a = sign * (digits == cursor ? 1 : number);
    cursor = n_pos + 1;
    if (cursor == length) { *b = 0; return true; }
    if (value[cursor] != '+' && value[cursor] != '-') return false;
    sign = value[cursor++] == '-' ? -1 : 1;
    if (cursor == length) return false;
    number = 0;
    for (; cursor < length; cursor++) {
        if (value[cursor] < '0' || value[cursor] > '9') return false;
        int64_t digit = value[cursor] - '0';
        if (number > (INT32_MAX - digit) / 10) return false;
        number = number * 10 + digit;
    }
    *b = sign * number;
    return true;
}

static bool tbox_css_selector_matches_nth(const tbox_html_node *node, tbox_string_view argument, bool of_type, bool from_end) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
    int64_t a, b;
    if (!tbox_css_selector_parse_nth(argument, &a, &b)) return false;
    int64_t index = 1;
    for (const tbox_html_node *sibling = from_end ? node->next_sibling : node->prev_sibling;
         sibling != NULL;
         sibling = from_end ? sibling->next_sibling : sibling->prev_sibling) {
        if (sibling->type == TBOX_HTML_NODE_ELEMENT &&
            (!of_type || tbox_string_view_equal_ascii_ci(sibling->element.tag_name, node->element.tag_name)))
            index++;
    }
    if (a == 0) return index == b;
    int64_t difference = index - b;
    return difference % a == 0 && difference / a >= 0;
}

/* CSS2.1 "white space": space, tab, line feed, carriage return, form feed. */
static bool tbox_css_selector_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

/* True if `token` appears as one whole whitespace-separated item of `list`
 * (used for CLASS and the '~=' attribute operator). Comparison is byte-exact
 * (case-sensitive), per CSS2.1. */
static bool tbox_css_selector_value_equal(tbox_string_view a, tbox_string_view b, bool insensitive) {
    return insensitive ? tbox_string_view_equal_ascii_ci(a, b) : tbox_string_view_equal(a, b);
}

static bool tbox_css_selector_prefix_equal(tbox_string_view value, tbox_string_view prefix, bool insensitive) {
    return value.size >= prefix.size && tbox_css_selector_value_equal(tbox_string_view_make(value.data, prefix.size), prefix, insensitive);
}

static bool tbox_css_selector_token_list_contains_case(tbox_string_view list, tbox_string_view token, bool insensitive) {
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
            if (tbox_css_selector_value_equal(item, token, insensitive)) {
                return true;
            }
        }
    }
    return false;
}

static bool tbox_css_selector_token_list_contains(tbox_string_view list, tbox_string_view token) {
    return tbox_css_selector_token_list_contains_case(list, token, false);
}

/* '|=' : value equals `prefix`, or value equals prefix followed by '-'. */
static bool tbox_css_selector_dash_match(tbox_string_view value, tbox_string_view prefix, bool insensitive) {
    if (tbox_css_selector_value_equal(value, prefix, insensitive)) {
        return true;
    }
    return value.size > prefix.size && tbox_css_selector_prefix_equal(value, prefix, insensitive) && value.data[prefix.size] == '-';
}

static bool tbox_css_selector_accepts_required(const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
    tbox_string_view tag = node->element.tag_name;
    if (tbox_string_view_equal_cstr(tag, "select") || tbox_string_view_equal_cstr(tag, "textarea")) return true;
    if (!tbox_string_view_equal_cstr(tag, "input")) return false;
    const tbox_html_attribute *type = tbox_css_selector_find_attribute(node, tbox_string_view_make("type", 4));
    if (type == NULL) return true;
    const char *excluded[] = { "hidden", "button", "submit", "reset", "image", "range", "color" };
    for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_from_cstr(excluded[i]))) return false;
    }
    return true;
}

static bool tbox_css_selector_matches_lang(const tbox_html_node *node, tbox_string_view requested) {
    if (node->type != TBOX_HTML_NODE_ELEMENT || requested.size == 0) return false;
    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent) {
        if (ancestor->type != TBOX_HTML_NODE_ELEMENT) continue;
        const tbox_html_attribute *lang = tbox_css_selector_find_attribute(ancestor, tbox_string_view_make("lang", 4));
        if (lang == NULL) continue;
        if (tbox_string_view_equal_ascii_ci(lang->value, requested)) return true;
        return lang->value.size > requested.size && lang->value.data[requested.size] == '-' &&
               tbox_string_view_equal_ascii_ci(tbox_string_view_make(lang->value.data, requested.size), requested);
    }
    return false;
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
            if (tbox_string_view_equal_cstr(node->element.tag_name, "input") &&
                tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_make("type", 4)) && !item->attribute_case_sensitive)
                return tbox_string_view_equal_ascii_ci(attribute->value, item->attribute_value);
            return tbox_css_selector_value_equal(attribute->value, item->attribute_value, item->attribute_case_insensitive);
        case TBOX_CSS_ATTR_INCLUDES:
            return tbox_css_selector_token_list_contains_case(attribute->value, item->attribute_value, item->attribute_case_insensitive);
        case TBOX_CSS_ATTR_DASHMATCH:
            return tbox_css_selector_dash_match(attribute->value, item->attribute_value, item->attribute_case_insensitive);
        case TBOX_CSS_ATTR_PREFIX:
            return item->attribute_value.size > 0 && tbox_css_selector_prefix_equal(attribute->value, item->attribute_value, item->attribute_case_insensitive);
        case TBOX_CSS_ATTR_SUFFIX:
            return item->attribute_value.size > 0 && attribute->value.size >= item->attribute_value.size &&
                   tbox_css_selector_value_equal(tbox_string_view_make(attribute->value.data + attribute->value.size - item->attribute_value.size, item->attribute_value.size), item->attribute_value, item->attribute_case_insensitive);
        case TBOX_CSS_ATTR_SUBSTRING:
            if (item->attribute_value.size == 0 || item->attribute_value.size > attribute->value.size) return false;
            for (size_t i = 0; i <= attribute->value.size - item->attribute_value.size; i++)
                if (tbox_css_selector_value_equal(tbox_string_view_make(attribute->value.data + i, item->attribute_value.size), item->attribute_value, item->attribute_case_insensitive)) return true;
            return false;
        }
        return false;
    }

    case TBOX_CSS_SIMPLE_SELECTOR_PSEUDO:
        /* Structural selectors and the context's current hover/focus nodes
         * are supported here. Other pseudo-classes never match. */
        if (tbox_string_view_equal_cstr(item->name, "not")) {
            return item->negated_selector != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
                   !tbox_css_selector_matches_simple_selector(item->negated_selector, node);
        }
        if (tbox_string_view_equal_cstr(item->name, "lang"))
            return tbox_css_selector_matches_lang(node, item->pseudo_argument);
        if (tbox_string_view_equal_cstr(item->name, "first-child")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && tbox_css_selector_prev_element_sibling(node) == NULL;
        }
        if (tbox_string_view_equal_cstr(item->name, "last-child")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && tbox_css_selector_next_element_sibling(node) == NULL;
        }
        if (tbox_string_view_equal_cstr(item->name, "only-child")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && tbox_css_selector_prev_element_sibling(node) == NULL && tbox_css_selector_next_element_sibling(node) == NULL;
        }
        if (tbox_string_view_equal_cstr(item->name, "root")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && node->parent != NULL &&
                   node->parent->type == TBOX_HTML_NODE_DOCUMENT &&
                   tbox_css_selector_prev_element_sibling(node) == NULL && tbox_css_selector_next_element_sibling(node) == NULL;
        }
        if (tbox_string_view_equal_cstr(item->name, "first-of-type")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && !tbox_css_selector_has_same_type_sibling(node, true);
        }
        if (tbox_string_view_equal_cstr(item->name, "last-of-type")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && !tbox_css_selector_has_same_type_sibling(node, false);
        }
        if (tbox_string_view_equal_cstr(item->name, "only-of-type")) {
            return node->type == TBOX_HTML_NODE_ELEMENT && !tbox_css_selector_has_same_type_sibling(node, true) &&
                   !tbox_css_selector_has_same_type_sibling(node, false);
        }
        if (tbox_string_view_equal_cstr(item->name, "nth-child"))
            return tbox_css_selector_matches_nth(node, item->pseudo_argument, false, false);
        if (tbox_string_view_equal_cstr(item->name, "nth-of-type"))
            return tbox_css_selector_matches_nth(node, item->pseudo_argument, true, false);
        if (tbox_string_view_equal_cstr(item->name, "nth-last-child"))
            return tbox_css_selector_matches_nth(node, item->pseudo_argument, false, true);
        if (tbox_string_view_equal_cstr(item->name, "nth-last-of-type"))
            return tbox_css_selector_matches_nth(node, item->pseudo_argument, true, true);
        if (tbox_string_view_equal_cstr(item->name, "empty")) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling)
                if (child->type == TBOX_HTML_NODE_ELEMENT || child->type == TBOX_HTML_NODE_TEXT) return false;
            return true;
        }
        if (tbox_string_view_equal_cstr(item->name, "disabled") || tbox_string_view_equal_cstr(item->name, "enabled")) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            tbox_string_view tag = node->element.tag_name;
            bool control = tbox_string_view_equal_cstr(tag, "button") || tbox_string_view_equal_cstr(tag, "input") ||
                           tbox_string_view_equal_cstr(tag, "select") || tbox_string_view_equal_cstr(tag, "textarea") ||
                           tbox_string_view_equal_cstr(tag, "option") || tbox_string_view_equal_cstr(tag, "optgroup") ||
                           tbox_string_view_equal_cstr(tag, "fieldset");
            if (!control) return false;
            bool disabled = tbox_css_selector_find_attribute(node, tbox_string_view_make("disabled", 8)) != NULL;
            return tbox_string_view_equal_cstr(item->name, "disabled") ? disabled : !disabled;
        }
        if (tbox_string_view_equal_cstr(item->name, "required") || tbox_string_view_equal_cstr(item->name, "optional")) {
            if (!tbox_css_selector_accepts_required(node)) return false;
            bool required = tbox_css_selector_find_attribute(node, tbox_string_view_make("required", 8)) != NULL;
            return tbox_string_view_equal_cstr(item->name, "required") ? required : !required;
        }
        if (tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_make("hover", 5))) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            for (const tbox_html_node *ancestor = tbox_css_selector_hovered_node; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor == node) return true;
            return false;
        }
        if (tbox_string_view_equal_cstr(item->name, "active")) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            for (const tbox_html_node *ancestor = tbox_css_selector_active_node; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor == node) return true;
            return false;
        }
        if (tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_make("focus", 5))) {
            return node == tbox_css_selector_focused_node;
        }
        if (tbox_string_view_equal_cstr(item->name, "focus-within")) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            for (const tbox_html_node *ancestor = tbox_css_selector_focused_node; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor == node) return true;
            return false;
        }
        if (tbox_string_view_equal_cstr(item->name, "link") || tbox_string_view_equal_cstr(item->name, "any-link")) {
            if (node->type != TBOX_HTML_NODE_ELEMENT) return false;
            tbox_string_view tag = node->element.tag_name;
            return (tbox_string_view_equal_cstr(tag, "a") || tbox_string_view_equal_cstr(tag, "area") ||
                    tbox_string_view_equal_cstr(tag, "link")) &&
                   tbox_css_selector_find_attribute(node, tbox_string_view_make("href", 4)) != NULL;
        }
        if (tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_make("checked", 7))) {
            if (node->type != TBOX_HTML_NODE_ELEMENT ||
                !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
            const tbox_html_attribute *type = tbox_css_selector_find_attribute(node, tbox_string_view_make("type", 4));
            return type != NULL &&
                   (tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8)) ||
                    tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("radio", 5))) &&
                   tbox_css_selector_find_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
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
 * sibling, GENERAL_SIBLING every preceding element sibling, and DESCENDANT
 * backtracks over every ancestor in turn (matching
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

    case TBOX_CSS_COMBINATOR_GENERAL_SIBLING:
        for (const tbox_html_node *sibling = tbox_css_selector_prev_element_sibling(node); sibling != NULL;
             sibling = tbox_css_selector_prev_element_sibling(sibling)) {
            if (tbox_css_selector_matches_suffix(items, compound_start, sibling)) return true;
        }
        return false;

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
