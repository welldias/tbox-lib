#ifndef TBOX_HTML_PARSER_TREE_BUILDER_H
#define TBOX_HTML_PARSER_TREE_BUILDER_H

#include <tbox/html_parser.h>

#include "base/tbox_vector.h"
#include "tbox_html_document.h"
#include "tbox_html_tokenizer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tbox_html_tree_builder {
    tbox_html_document *document;
    tbox_html_tokenizer tokenizer;
    tbox_vector open_elements; /* tbox_html_node* stack; top = last element */
} tbox_html_tree_builder;

void tbox_html_tree_builder_init(tbox_html_tree_builder *builder, const char *input, size_t length, tbox_html_document *document);

/* Consumes every token from the tokenizer and returns the document's root
 * (DOCUMENT) node. */
tbox_html_node *tbox_html_tree_builder_run(tbox_html_tree_builder *builder);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_TREE_BUILDER_H */
