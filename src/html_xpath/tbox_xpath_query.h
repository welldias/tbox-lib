#ifndef TBOX_HTML_XPATH_QUERY_H
#define TBOX_HTML_XPATH_QUERY_H

#include <stdbool.h>

#include <tbox/html_xpath.h>

#include "base/tbox_arena.h"
#include "tbox_xpath_ast.h"

struct tbox_xpath_query {
    tbox_arena arena;
    bool absolute; /* true if the expression started with '/' or '//' */
    tbox_xpath_step *first_step;
};

#endif /* TBOX_HTML_XPATH_QUERY_H */
