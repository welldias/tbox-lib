#ifndef TBOX_CSS_SELECTOR_MATCH_H
#define TBOX_CSS_SELECTOR_MATCH_H

#include <tbox/css_selector.h>

#include "base/tbox_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Explicit scoping root for :scope and ancestor matching. */
bool tbox_css_selector_matches_scoped(const tbox_css_selector *selector, const tbox_html_node *node, const tbox_html_node *root);

/* tbox_css_selector_matches_scoped that also sets *contextual (when not
 * NULL; never cleared) if matching read anything about `node` beyond its
 * and its ancestors' tags and attributes: its or an ancestor's position
 * among siblings, its children, runtime state (:hover, :focus, :checked,
 * :disabled...) or one node's identity. A match that read nothing else
 * comes out the same for every element with the same tags and attributes
 * up its ancestor chain. */
bool tbox_css_selector_matches_traced(const tbox_css_selector *selector, const tbox_html_node *node, const tbox_html_node *root, bool *contextual);

/* `selector` within `scope` (NULL: unscoped); `contextual` as in
 * tbox_css_selector_matches_traced, NULL to ignore. */
bool tbox_css_selector_scope_matches(const tbox_css_scope *scope, const tbox_html_node *node, const tbox_css_selector *selector, size_t *hops, bool *contextual);

/* Walks every ELEMENT descendant of `root` (root itself excluded) in
 * document (pre-order) order, pushing (as `const tbox_html_node *`) each one
 * that satisfies at least one of `selectors[0..selector_count)` (comma-group
 * semantics: OR across the group). Used by both tbox_css_selector_query_evaluate
 * and the node-collection half of tbox_css_selector_match_stylesheet. */
void tbox_css_selector_collect_matching_nodes(const tbox_html_node *root, const tbox_css_selector *selectors, size_t selector_count, const tbox_css_scope *scope, tbox_vector *out);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_SELECTOR_MATCH_H */
