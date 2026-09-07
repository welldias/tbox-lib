#ifndef TBOX_HTML_PARSER_H
#define TBOX_HTML_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbox_html_node_type {
    TBOX_HTML_NODE_DOCUMENT,
    TBOX_HTML_NODE_ELEMENT,
    TBOX_HTML_NODE_TEXT,
    TBOX_HTML_NODE_COMMENT,
    TBOX_HTML_NODE_DOCTYPE,
} tbox_html_node_type;

/* name is lowercase ASCII, copied into the owning document's arena. value is
 * preserved verbatim from the source; an empty (size == 0) value means the
 * attribute was written without a value (a boolean attribute). */
typedef struct tbox_html_attribute {
    tbox_string_view name;
    tbox_string_view value;
} tbox_html_attribute;

typedef struct tbox_html_node {
    tbox_html_node_type type;

    struct tbox_html_node *parent;
    struct tbox_html_node *first_child;
    struct tbox_html_node *last_child;
    struct tbox_html_node *next_sibling;
    struct tbox_html_node *prev_sibling;

    union {
        /* Valid when type == TBOX_HTML_NODE_ELEMENT. */
        struct {
            tbox_string_view tag_name;
            tbox_html_attribute *attributes;
            size_t attribute_count;
            bool self_closing;
        } element;
        /* Valid when type is TEXT, COMMENT or DOCTYPE. For DOCTYPE this
         * holds only the doctype name (e.g. "html"). */
        struct {
            tbox_string_view text;
        } text;
    };
} tbox_html_node;

/* Opaque: owns the arena backing every node/string reachable from its root. */
typedef struct tbox_html_document tbox_html_document;

/* Parses `length` bytes of UTF-8 HTML5 starting at `input` (need not be
 * NUL-terminated) and returns a document tree allocated entirely inside an
 * arena owned by the returned document. Malformed HTML is tolerated (the
 * parser never fails on bad markup); returns NULL only on allocation
 * failure. */
tbox_html_document *tbox_html_parse(const char *input, size_t length);

const tbox_html_node *tbox_html_document_root(const tbox_html_document *document);

/* Frees the document's arena and everything allocated from it. */
void tbox_html_document_destroy(tbox_html_document *document);

/* Allocates a detached, zeroed node of the given type from the document's
 * arena. */
tbox_html_node *tbox_html_node_create(tbox_html_document *document, tbox_html_node_type type);

/* Appends `child` as the new last child of `parent`, wiring up
 * parent/first_child/last_child/next_sibling/prev_sibling. */
void tbox_html_node_append_child(tbox_html_node *parent, tbox_html_node *child);

/* Detaches `node` from its tree, relinking parent/sibling pointers around it.
 * `node` itself keeps its own subtree intact (its first_child/last_child are
 * untouched), so it can be re-attached elsewhere via tbox_html_node_append_child.
 * The node's memory stays valid in the document's arena (no individual free,
 * same pattern as every allocation in tbox) until the document is destroyed.
 * Safe to call on a node with no parent (the document root, or an already
 * detached node): a no-op. */
void tbox_html_node_remove(tbox_html_node *node);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_H */
