#ifndef TBOX_HTML_PARSER_DOCUMENT_H
#define TBOX_HTML_PARSER_DOCUMENT_H

#include <tbox/html_parser.h>

#include <stdint.h>

#include "base/tbox_arena.h"

struct tbox_html_document {
    tbox_arena arena;
    tbox_html_node *root;
    uint64_t revision; /* see tbox_html_document_revision */
};

/* Counts a change that affects rendering but goes around the DOM mutation
 * functions (a select's choice, focus, an edit written into a bound
 * view). NULL is a no-op. */
static inline void tbox_html_document_touch(tbox_html_document *document) {
    if (document != NULL) document->revision++;
}

#endif /* TBOX_HTML_PARSER_DOCUMENT_H */
