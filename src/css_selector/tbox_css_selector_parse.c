#include "tbox_css_selector_parse.h"

#include <stdlib.h>

#include "css_parser/tbox_css_parser.h"
#include "tbox_css_selector_query.h"

tbox_css_selector_query *tbox_css_selector_parser_compile(const char *text, size_t length, size_t *out_error_offset) {
    tbox_css_selector_query *query = malloc(sizeof(tbox_css_selector_query));
    if (query == NULL) {
        return NULL;
    }

    query->arena          = tbox_arena_create(0);
    query->selectors      = NULL;
    query->selector_count = 0;

    if (!tbox_css_parser_parse_standalone_selector_group(text, length, &query->arena, &query->selectors, &query->selector_count, out_error_offset)) {
        tbox_arena_destroy(&query->arena);
        free(query);
        return NULL;
    }

    return query;
}
