#ifndef TBOX_CSS_SELECTOR_QUERY_H
#define TBOX_CSS_SELECTOR_QUERY_H

#include <tbox/css_selector.h>

#include "base/tbox_arena.h"

struct tbox_css_selector_query {
    tbox_arena arena;
    tbox_css_selector *selectors;
    size_t selector_count;
};

#endif /* TBOX_CSS_SELECTOR_QUERY_H */
