#ifndef TBOX_HTML_XPATH_H
#define TBOX_HTML_XPATH_H

#include <stddef.h>

#include <tbox/html_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking. A
 * compiled query, a node_set, or the tbox_html_document being queried must
 * not be shared across threads without external synchronization. */

/* Opaque: owns the arena backing the compiled query's AST. */
typedef struct tbox_xpath_query tbox_xpath_query;

typedef enum tbox_xpath_item_type {
    TBOX_XPATH_ITEM_NODE,      /* item.node; node->type says ELEMENT/TEXT/COMMENT/DOCTYPE/DOCUMENT */
    TBOX_XPATH_ITEM_ATTRIBUTE, /* item.attribute */
} tbox_xpath_item_type;

typedef struct tbox_xpath_item {
    tbox_xpath_item_type type;
    union {
        const tbox_html_node *node; /* valid when type == TBOX_XPATH_ITEM_NODE */
        struct {
            const tbox_html_node *owner;          /* the element the attribute belongs to */
            const tbox_html_attribute *attribute; /* stable pointer inside owner->element.attributes */
        } attribute; /* valid when type == TBOX_XPATH_ITEM_ATTRIBUTE */
    };
} tbox_xpath_item;

typedef struct tbox_xpath_node_set {
    tbox_xpath_item *items;
    size_t count;
    void *reserved_; /* private: owns the `items` storage; touched only by tbox_xpath_node_set_destroy */
} tbox_xpath_node_set;

/* Compiles `expr` (need not be NUL-terminated) into a reusable query. Every
 * piece of data referenced by the AST is copied into the query's own arena,
 * so `expr` does not need to outlive this call.
 *
 * Supports a pragmatic subset of XPath 1.0 location paths:
 *   /a/b, //a, a/b, *, @name, @*, text(), node(), ., ..,
 *   [n] (1-based, positional per individual context node),
 *   [@name], [@name='value']
 * Not supported (future extensions): functions (count(), contains()...),
 * named axes (parent::, following-sibling::...), boolean/numeric
 * expressions beyond '=', path union ('|'), namespaces. An '@' step must be
 * the last step of the path. A bare "/" or "//" (no step following) is a
 * syntax error in this subset.
 *
 * Returns NULL on syntax error; if out_error_offset is non-NULL, receives
 * the byte offset into `expr` where parsing failed. */
tbox_xpath_query *tbox_xpath_compile(const char *expr, size_t length, size_t *out_error_offset);

void tbox_xpath_query_destroy(tbox_xpath_query *query);

/* Evaluates `query` starting from `context_node`. An absolute path (leading
 * '/' or '//') resolves the document root by walking context_node->parent
 * up to the top; a relative path starts at context_node itself. query ==
 * NULL or context_node == NULL yields an empty node_set instead of
 * crashing (should not happen if the caller checks tbox_xpath_compile's
 * return value). */
tbox_xpath_node_set tbox_xpath_query_evaluate(const tbox_xpath_query *query, const tbox_html_node *context_node);

/* Convenience: compiles, evaluates and destroys the query internally.
 * Syntax errors are reported as an empty node_set (count == 0); use
 * tbox_xpath_compile directly when error diagnostics are needed. */
tbox_xpath_node_set tbox_xpath_select(const tbox_html_node *context_node, const char *expr, size_t length);

void tbox_xpath_node_set_destroy(tbox_xpath_node_set *set);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_XPATH_H */
