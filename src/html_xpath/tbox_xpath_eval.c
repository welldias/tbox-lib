#include "tbox_xpath_eval.h"

#include <stdlib.h>

#include "base/tbox_string.h"
#include "base/tbox_vector.h"

static bool tbox_xpath_node_matches_test(const tbox_html_node *node, tbox_xpath_node_test_kind test_kind, tbox_string_view test_name) {
    switch (test_kind) {
    case TBOX_XPATH_TEST_NAME:
        return node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_ascii_ci(node->element.tag_name, test_name);
    case TBOX_XPATH_TEST_WILDCARD:
        return node->type == TBOX_HTML_NODE_ELEMENT;
    case TBOX_XPATH_TEST_TEXT:
        return node->type == TBOX_HTML_NODE_TEXT;
    case TBOX_XPATH_TEST_NODE:
        return true;
    case TBOX_XPATH_TEST_NONE:
        return false;
    }
    return false;
}

static void tbox_xpath_push_node(tbox_vector *out, const tbox_html_node *node) {
    tbox_xpath_item *item = tbox_vector_push(out);
    item->type            = TBOX_XPATH_ITEM_NODE;
    item->node            = node;
}

/* Tests base's direct children (combinator == IDENTITY) or every proper
 * descendant of base (combinator == DESCENDANT_OR_SELF), always in document
 * (pre-order) order. For the descendant case this deliberately tests each
 * descendant directly rather than testing the children of every node in
 * {base} u descendants(base) separately: both approaches produce the same
 * *set* (every descendant of base is the child of exactly one node in that
 * set), but only recursing this way -- testing a child, then immediately
 * descending into it -- keeps the results in true document order. Draining
 * an ancestor's full child list before descending into any of them would
 * interleave shallow matches ahead of deeper matches that actually occur
 * earlier in the document. */
static void tbox_xpath_collect_child_axis(const tbox_html_node *base, tbox_xpath_combinator combinator, tbox_xpath_node_test_kind test_kind, tbox_string_view test_name, tbox_vector *out) {
    for (const tbox_html_node *child = base->first_child; child != NULL; child = child->next_sibling) {
        if (tbox_xpath_node_matches_test(child, test_kind, test_name)) {
            tbox_xpath_push_node(out, child);
        }
        if (combinator == TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF) {
            tbox_xpath_collect_child_axis(child, TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF, test_kind, test_name, out);
        }
    }
}

/* Collects `node` together with every one of its descendants, in document
 * (pre-order) order. Used for axes other than CHILD, where visiting each
 * self-or-descendant node once and handling it independently (attribute
 * lookup, self, parent) does not have the ordering pitfall described above. */
static void tbox_xpath_collect_self_and_descendants(const tbox_html_node *node, tbox_vector *out) {
    *(const tbox_html_node **)tbox_vector_push(out) = node;
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        tbox_xpath_collect_self_and_descendants(child, out);
    }
}

static void tbox_xpath_collect_non_child_axis(const tbox_html_node *search_node, tbox_xpath_axis axis, tbox_xpath_node_test_kind test_kind, tbox_string_view test_name, tbox_vector *out) {
    switch (axis) {
    case TBOX_XPATH_AXIS_ATTRIBUTE:
        if (search_node->type == TBOX_HTML_NODE_ELEMENT) {
            for (size_t i = 0; i < search_node->element.attribute_count; i++) {
                const tbox_html_attribute *attribute = &search_node->element.attributes[i];
                bool match                           = test_kind == TBOX_XPATH_TEST_WILDCARD || (test_kind == TBOX_XPATH_TEST_NAME && tbox_string_view_equal_ascii_ci(attribute->name, test_name));
                if (match) {
                    tbox_xpath_item *item     = tbox_vector_push(out);
                    item->type                = TBOX_XPATH_ITEM_ATTRIBUTE;
                    item->attribute.owner     = search_node;
                    item->attribute.attribute = attribute;
                }
            }
        }
        break;
    case TBOX_XPATH_AXIS_SELF:
        tbox_xpath_push_node(out, search_node);
        break;
    case TBOX_XPATH_AXIS_PARENT:
        if (search_node->parent != NULL) {
            tbox_xpath_push_node(out, search_node->parent);
        }
        break;
    case TBOX_XPATH_AXIS_CHILD:
        break; /* handled by tbox_xpath_collect_child_axis, never reached */
    }
}

static bool tbox_xpath_node_has_attribute(const tbox_html_node *node, const tbox_xpath_predicate *predicate) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }
    for (size_t i = 0; i < node->element.attribute_count; i++) {
        const tbox_html_attribute *attribute = &node->element.attributes[i];
        if (!tbox_string_view_equal_ascii_ci(attribute->name, predicate->attr_name)) {
            continue;
        }
        if (predicate->kind == TBOX_XPATH_PREDICATE_ATTR_EXISTS) {
            return true;
        }
        return tbox_string_view_equal(attribute->value, predicate->attr_value);
    }
    return false;
}

static void tbox_xpath_apply_predicate(const tbox_xpath_predicate *predicate, tbox_vector *candidates, tbox_arena *scratch) {
    tbox_vector filtered;
    tbox_vector_init(&filtered, scratch, sizeof(tbox_xpath_item), 0);

    size_t length = tbox_vector_length(candidates);

    if (predicate->kind == TBOX_XPATH_PREDICATE_POSITION) {
        if (predicate->position >= 1 && predicate->position <= length) {
            const tbox_xpath_item *item                     = tbox_vector_at_const(candidates, predicate->position - 1);
            *(tbox_xpath_item *)tbox_vector_push(&filtered) = *item;
        }
    } else {
        for (size_t i = 0; i < length; i++) {
            const tbox_xpath_item *item = tbox_vector_at_const(candidates, i);
            if (item->type == TBOX_XPATH_ITEM_NODE && tbox_xpath_node_has_attribute(item->node, predicate)) {
                *(tbox_xpath_item *)tbox_vector_push(&filtered) = *item;
            }
        }
    }

    *candidates = filtered;
}

static bool tbox_xpath_item_equal(const tbox_xpath_item *a, const tbox_xpath_item *b) {
    if (a->type != b->type) {
        return false;
    }
    if (a->type == TBOX_XPATH_ITEM_NODE) {
        return a->node == b->node;
    }
    return a->attribute.owner == b->attribute.owner && a->attribute.attribute == b->attribute.attribute;
}

static void tbox_xpath_push_unique(tbox_vector *set, const tbox_xpath_item *item) {
    size_t length = tbox_vector_length(set);
    for (size_t i = 0; i < length; i++) {
        if (tbox_xpath_item_equal(tbox_vector_at_const(set, i), item)) {
            return;
        }
    }
    *(tbox_xpath_item *)tbox_vector_push(set) = *item;
}

static void tbox_xpath_collect_step_candidates(const tbox_html_node *base, const tbox_xpath_step *step, tbox_vector *candidates, tbox_arena *scratch) {
    if (step->axis == TBOX_XPATH_AXIS_CHILD) {
        tbox_xpath_collect_child_axis(base, step->combinator_before, step->test_kind, step->test_name, candidates);
        return;
    }

    tbox_vector search_nodes;
    tbox_vector_init(&search_nodes, scratch, sizeof(const tbox_html_node *), 0);
    if (step->combinator_before == TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF) {
        tbox_xpath_collect_self_and_descendants(base, &search_nodes);
    } else {
        *(const tbox_html_node **)tbox_vector_push(&search_nodes) = base;
    }

    size_t search_count = tbox_vector_length(&search_nodes);
    for (size_t s = 0; s < search_count; s++) {
        const tbox_html_node *search_node = *(const tbox_html_node **)tbox_vector_at(&search_nodes, s);
        tbox_xpath_collect_non_child_axis(search_node, step->axis, step->test_kind, step->test_name, candidates);
    }
}

tbox_xpath_node_set tbox_xpath_eval_run(const tbox_xpath_query *query, const tbox_html_node *context_node) {
    tbox_xpath_node_set result = { .items = NULL, .count = 0, .reserved_ = NULL };
    if (query == NULL || context_node == NULL) {
        return result;
    }

    tbox_arena scratch = tbox_arena_create(0);

    const tbox_html_node *start = context_node;
    if (query->absolute) {
        while (start->parent != NULL) {
            start = start->parent;
        }
    }

    tbox_vector current;
    tbox_vector_init(&current, &scratch, sizeof(tbox_xpath_item), 0);
    tbox_xpath_push_node(&current, start);

    for (const tbox_xpath_step *step = query->first_step; step != NULL; step = step->next) {
        tbox_vector next;
        tbox_vector_init(&next, &scratch, sizeof(tbox_xpath_item), 0);

        size_t current_length = tbox_vector_length(&current);
        for (size_t i = 0; i < current_length; i++) {
            const tbox_xpath_item *item = tbox_vector_at_const(&current, i);
            const tbox_html_node *base  = item->node; /* attribute axis is always the last step */

            tbox_vector candidates;
            tbox_vector_init(&candidates, &scratch, sizeof(tbox_xpath_item), 0);
            tbox_xpath_collect_step_candidates(base, step, &candidates, &scratch);

            for (const tbox_xpath_predicate *predicate = step->predicates; predicate != NULL; predicate = predicate->next) {
                tbox_xpath_apply_predicate(predicate, &candidates, &scratch);
            }

            size_t candidate_count = tbox_vector_length(&candidates);
            for (size_t c = 0; c < candidate_count; c++) {
                tbox_xpath_push_unique(&next, tbox_vector_at_const(&candidates, c));
            }
        }

        current = next;
    }

    size_t count = tbox_vector_length(&current);
    if (count > 0) {
        tbox_arena *out_arena = malloc(sizeof(tbox_arena));
        *out_arena            = tbox_arena_create(0);
        result.items          = tbox_arena_alloc(out_arena, count * sizeof(tbox_xpath_item));
        for (size_t i = 0; i < count; i++) {
            result.items[i] = *(const tbox_xpath_item *)tbox_vector_at_const(&current, i);
        }
        result.count     = count;
        result.reserved_ = out_arena;
    }

    tbox_arena_destroy(&scratch);
    return result;
}
