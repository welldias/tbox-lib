#ifndef TBOX_HTML_XPATH_EVAL_H
#define TBOX_HTML_XPATH_EVAL_H

#include "tbox_xpath_query.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Evaluates `query` from `context_node`. query == NULL or context_node ==
 * NULL yields an empty node_set. The returned node_set's `items` array (if
 * any) is malloc'd/arena-backed independently of `query`; the caller owns it
 * via tbox_xpath_node_set_destroy. */
tbox_xpath_node_set tbox_xpath_eval_run(const tbox_xpath_query *query, const tbox_html_node *context_node);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_XPATH_EVAL_H */
