#ifndef TBOX_HTML_PARSER_DOCUMENT_H
#define TBOX_HTML_PARSER_DOCUMENT_H

#include <tbox/html_parser.h>

#include "base/tbox_arena.h"

struct tbox_html_document {
    tbox_arena arena;
    tbox_html_node *root;
};

#endif /* TBOX_HTML_PARSER_DOCUMENT_H */
