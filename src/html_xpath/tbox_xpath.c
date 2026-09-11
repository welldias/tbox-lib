#include <tbox/html_xpath.h>

#include <stdlib.h>

#include "tbox_xpath_eval.h"
#include "tbox_xpath_parser.h"

tbox_xpath_query *tbox_xpath_compile(const char *expr, size_t length, size_t *out_error_offset) {
    return tbox_xpath_parser_parse(expr, length, out_error_offset);
}

void tbox_xpath_query_destroy(tbox_xpath_query *query) {
    if (query == NULL) {
        return;
    }
    tbox_arena_destroy(&query->arena);
    free(query);
}

tbox_xpath_node_set tbox_xpath_query_evaluate(const tbox_xpath_query *query, const tbox_html_node *context_node) {
    return tbox_xpath_eval_run(query, context_node);
}

tbox_xpath_node_set tbox_xpath_select(const tbox_html_node *context_node, const char *expr, size_t length) {
    tbox_xpath_query *query    = tbox_xpath_compile(expr, length, NULL);
    tbox_xpath_node_set result = tbox_xpath_query_evaluate(query, context_node);
    tbox_xpath_query_destroy(query);
    return result;
}

void tbox_xpath_node_set_destroy(tbox_xpath_node_set *set) {
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
