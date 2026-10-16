#include <stdlib.h>

#include <tbox/html_parser.h>

#include "base/tbox_string.h"
#include "tbox_html_document.h"
#include "tbox_html_tree_builder.h"

tbox_html_document *tbox_html_parse(const char *input, size_t length) {
    tbox_html_document *document = malloc(sizeof(tbox_html_document));
    if (document == NULL) {
        return NULL;
    }

    document->arena = tbox_arena_create(0);
    document->root  = NULL;

    /* Malformed UTF-8 becomes U+FFFD once, here, so every later stage
     * (layout, measurement, editing) sees valid text. */
    tbox_string_view source = tbox_utf8_sanitize(&document->arena, tbox_string_view_make(input, length));
    tbox_html_tree_builder builder;
    tbox_html_tree_builder_init(&builder, source.data, source.size, document);
    document->root = tbox_html_tree_builder_run(&builder);

    return document;
}

const tbox_html_node *tbox_html_document_root(const tbox_html_document *document) {
    return document->root;
}

void tbox_html_document_destroy(tbox_html_document *document) {
    if (document == NULL) {
        return;
    }
    tbox_arena_destroy(&document->arena);
    free(document);
}
