#include <tbox/css_selector.h>

#include <stdlib.h>

#include "tbox_css_selector_match.h"
#include "tbox_css_selector_parse.h"
#include "tbox_css_selector_query.h"

tbox_css_selector_query *tbox_css_selector_compile(const char *text, size_t length, size_t *out_error_offset) {
    return tbox_css_selector_parser_compile(text, length, out_error_offset);
}

void tbox_css_selector_query_destroy(tbox_css_selector_query *query) {
    if (query == NULL) {
        return;
    }
    tbox_arena_destroy(&query->arena);
    free(query);
}

bool tbox_css_selector_query_matches(const tbox_css_selector_query *query, const tbox_html_node *node) {
    if (query == NULL || node == NULL) {
        return false;
    }
    for (size_t i = 0; i < query->selector_count; i++) {
        if (tbox_css_selector_matches(&query->selectors[i], node)) {
            return true;
        }
    }
    return false;
}

tbox_css_selector_node_set tbox_css_selector_query_evaluate(const tbox_css_selector_query *query, const tbox_html_node *root) {
    tbox_css_selector_node_set result = {.items = NULL, .count = 0, .reserved_ = NULL};
    if (query == NULL || root == NULL) {
        return result;
    }

    tbox_arena scratch = tbox_arena_create(0);

    tbox_vector matches;
    tbox_vector_init(&matches, &scratch, sizeof(const tbox_html_node *), 0);
    tbox_css_selector_collect_matching_nodes(root, query->selectors, query->selector_count, &matches);

    size_t count = tbox_vector_length(&matches);
    if (count > 0) {
        tbox_arena *out_arena = malloc(sizeof(tbox_arena));
        *out_arena  = tbox_arena_create(0);
        result.items = tbox_arena_alloc(out_arena, count * sizeof(const tbox_html_node *));
        for (size_t i = 0; i < count; i++) {
            result.items[i] = *(const tbox_html_node **)tbox_vector_at_const(&matches, i);
        }
        result.count     = count;
        result.reserved_ = out_arena;
    }

    tbox_arena_destroy(&scratch);
    return result;
}

tbox_css_selector_node_set tbox_css_selector_select(const tbox_html_node *root, const char *text, size_t length) {
    tbox_css_selector_query *query   = tbox_css_selector_compile(text, length, NULL);
    tbox_css_selector_node_set result = tbox_css_selector_query_evaluate(query, root);
    tbox_css_selector_query_destroy(query);
    return result;
}

void tbox_css_selector_node_set_destroy(tbox_css_selector_node_set *set) {
    if (set == NULL || set->reserved_ == NULL) {
        return;
    }
    tbox_arena *arena = set->reserved_;
    tbox_arena_destroy(arena);
    free(arena);
    set->items     = NULL;
    set->count     = 0;
    set->reserved_ = NULL;
}

tbox_css_selector_match_set tbox_css_selector_match_stylesheet(const tbox_css_stylesheet *stylesheet, const tbox_html_node *root) {
    tbox_css_selector_match_set result = {.items = NULL, .count = 0, .reserved_ = NULL};
    if (stylesheet == NULL || root == NULL) {
        return result;
    }

    tbox_arena scratch = tbox_arena_create(0);

    tbox_vector matches;
    tbox_vector_init(&matches, &scratch, sizeof(tbox_css_selector_match), 0);

    size_t ruleset_count               = tbox_css_stylesheet_ruleset_count(stylesheet);
    const tbox_css_ruleset *rulesets   = tbox_css_stylesheet_rulesets(stylesheet);

    for (size_t r = 0; r < ruleset_count; r++) {
        const tbox_css_ruleset *ruleset = &rulesets[r];

        for (size_t s = 0; s < ruleset->selector_count; s++) {
            const tbox_css_selector *selector = &ruleset->selectors[s];

            tbox_vector nodes;
            tbox_vector_init(&nodes, &scratch, sizeof(const tbox_html_node *), 0);
            tbox_css_selector_collect_matching_nodes(root, selector, 1, &nodes);

            size_t node_count = tbox_vector_length(&nodes);
            for (size_t n = 0; n < node_count; n++) {
                tbox_css_selector_match *match = tbox_vector_push(&matches);
                match->ruleset                 = ruleset;
                match->selector                = selector;
                match->node                    = *(const tbox_html_node **)tbox_vector_at_const(&nodes, n);
            }
        }
    }

    size_t count = tbox_vector_length(&matches);
    if (count > 0) {
        tbox_arena *out_arena = malloc(sizeof(tbox_arena));
        *out_arena  = tbox_arena_create(0);
        result.items = tbox_arena_alloc(out_arena, count * sizeof(tbox_css_selector_match));
        for (size_t i = 0; i < count; i++) {
            result.items[i] = *(const tbox_css_selector_match *)tbox_vector_at_const(&matches, i);
        }
        result.count     = count;
        result.reserved_ = out_arena;
    }

    tbox_arena_destroy(&scratch);
    return result;
}

void tbox_css_selector_match_set_destroy(tbox_css_selector_match_set *set) {
    if (set == NULL || set->reserved_ == NULL) {
        return;
    }
    tbox_arena *arena = set->reserved_;
    tbox_arena_destroy(arena);
    free(arena);
    set->items     = NULL;
    set->count     = 0;
    set->reserved_ = NULL;
}
