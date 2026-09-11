#ifndef TBOX_CSS_SELECTOR_MATCH_H
#define TBOX_CSS_SELECTOR_MATCH_H

#include <tbox/css_selector.h>

#include "base/tbox_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Walks every ELEMENT descendant of `root` (root itself excluded) in
 * document (pre-order) order, pushing (as `const tbox_html_node *`) each one
 * that satisfies at least one of `selectors[0..selector_count)` (comma-group
 * semantics: OR across the group). Used by both tbox_css_selector_query_evaluate
 * and the node-collection half of tbox_css_selector_match_stylesheet. */
void tbox_css_selector_collect_matching_nodes(const tbox_html_node *root, const tbox_css_selector *selectors, size_t selector_count, tbox_vector *out);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_SELECTOR_MATCH_H */
