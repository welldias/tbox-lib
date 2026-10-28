#ifndef TBOX_HTML_PARSER_H
#define TBOX_HTML_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque here: tbox_html_node_text_content only needs a pointer to it. Full
 * definition lives in Base (src/base/tbox_arena.h); the region-based
 * allocator every tbox structure is built on (see ownership convention in
 * docs/dev/convencoes.md). */
typedef struct tbox_arena tbox_arena;

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

typedef struct tbox_html_document tbox_html_document;

typedef struct tbox_html_node {
    tbox_html_node_type type;

    /* The document whose arena holds this node (set at creation, never
     * changes, also while detached). */
    tbox_html_document *owner_document;

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
            /* <option> only: chosen in its <select> right now. Kept in sync
             * by tbox_context before each style resolution (`option:checked`
             * reads it); the `selected` attribute is just the initial choice,
             * which a form reset restores. */
            bool selectedness;
            /* Runtime :focus-visible state, maintained by tbox_context.
             * Not an HTML attribute; false on newly parsed/created nodes. */
            bool focus_visible;
        } element;
        /* Valid when type is TEXT, COMMENT or DOCTYPE. For DOCTYPE this
         * holds only the doctype name (e.g. "html"). */
        struct {
            tbox_string_view text;
            /* Private: the buffer tbox_html_node_set_text_content stored
             * `text` in and its size, so a later call can rewrite it in
             * place while `text` still points at it. NULL otherwise. */
            char *buffer_;
            size_t capacity_;
        } text;
    };
} tbox_html_node;

/* Opaque: owns the arena backing every node/string reachable from its
 * root. Declared above, before tbox_html_node. */

/* Parses `length` bytes of UTF-8 HTML5 starting at `input` (need not be
 * NUL-terminated) and returns a document tree allocated entirely inside an
 * arena owned by the returned document. Malformed HTML is tolerated (the
 * parser never fails on bad markup), and so is malformed UTF-8: each
 * invalid sequence becomes U+FFFD. Returns NULL only on allocation
 * failure. */
tbox_html_document *tbox_html_parse(const char *input, size_t length);

const tbox_html_node *tbox_html_document_root(const tbox_html_document *document);

/* Grows by one or more on every change to the tree, attributes or text
 * made through this header's mutation functions (and on control state
 * changed through <tbox/controls.h>). Never decreases. tbox_app compares it
 * with the revision of its last drawn frame, so a DOM mutation needs no
 * tbox_app_request_redraw. 0 for NULL. */
uint64_t tbox_html_document_revision(const tbox_html_document *document);

/* Frees the document's arena and everything allocated from it. */
void tbox_html_document_destroy(tbox_html_document *document);

/* Allocates a detached, zeroed node of the given type from the document's
 * arena. */
tbox_html_node *tbox_html_node_create(tbox_html_document *document, tbox_html_node_type type);

/* A detached ELEMENT named `tag_name` (copied, ASCII-lowercased), with no
 * attributes or children. NULL when `document` is NULL or the name is
 * empty. */
tbox_html_node *tbox_html_element_create(tbox_html_document *document, tbox_string_view tag_name);

/* A detached TEXT node holding a copy of `text` (malformed UTF-8 becomes
 * U+FFFD). NULL when `document` is NULL. */
tbox_html_node *tbox_html_text_create(tbox_html_document *document, tbox_string_view text);

/* A detached copy of `node` allocated in `document`: tag, attributes and
 * text are copied; with `deep`, the whole subtree too. Runtime state
 * (selectedness, focus_visible) is not copied. NULL for a DOCUMENT node. */
tbox_html_node *tbox_html_node_clone(tbox_html_document *document, const tbox_html_node *node, bool deep);

/* Appends `child` as the new last child of `parent`, wiring up
 * parent/first_child/last_child/next_sibling/prev_sibling. Same rules as
 * tbox_html_node_insert_before with a NULL reference. */
void tbox_html_node_append_child(tbox_html_node *parent, tbox_html_node *child);

/* Inserts `child` into `parent` right before `reference` (a child of
 * `parent`), or last when `reference` is NULL. A `child` that is already in
 * a tree is moved (detached first), like the DOM's insertBefore. No-op when
 * either pointer is NULL, `reference` isn't a child of `parent`, or `child`
 * is `parent` or one of its ancestors. */
void tbox_html_node_insert_before(tbox_html_node *parent, tbox_html_node *child, tbox_html_node *reference);

/* Inserts `child` as the first child of `parent`; same rules as
 * tbox_html_node_insert_before. */
void tbox_html_node_prepend_child(tbox_html_node *parent, tbox_html_node *child);

/* Puts `new_node` where `old_node` is and detaches `old_node`. No-op when
 * `old_node` has no parent or the insertion would be invalid. */
void tbox_html_node_replace(tbox_html_node *old_node, tbox_html_node *new_node);

/* Detaches every child of `node` (each keeps its own subtree and can be
 * re-inserted). The memory stays in the document's arena. */
void tbox_html_node_remove_children(tbox_html_node *node);

/* Element-only navigation: skip TEXT/COMMENT/DOCTYPE siblings. NULL/0 for a
 * NULL node. */
tbox_html_node *tbox_html_node_first_element_child(const tbox_html_node *node);
tbox_html_node *tbox_html_node_next_element_sibling(const tbox_html_node *node);
size_t tbox_html_node_element_child_count(const tbox_html_node *node);

/* Detaches `node` from its tree, relinking parent/sibling pointers around it.
 * `node` itself keeps its own subtree intact (its first_child/last_child are
 * untouched), so it can be re-attached elsewhere via tbox_html_node_append_child.
 * The node's memory stays valid in the document's arena (no individual free,
 * same pattern as every allocation in tbox) until the document is destroyed.
 * Safe to call on a node with no parent (the document root, or an already
 * detached node): a no-op. */
void tbox_html_node_remove(tbox_html_node *node);

/* Equivalent to DOM's `textContent`: walks `node`'s descendants in document
 * order and concatenates the text of every TEXT node found, ignoring the
 * nested element structure entirely (ELEMENT nodes are recursed into but
 * contribute no text of their own; COMMENT and DOCTYPE nodes, and their
 * subtrees, are skipped). E.g. `<p>oi <b>mundo</b></p>` yields "oi mundo".
 * If `node` itself is a TEXT node, its own text is returned as-is (no
 * descendants to walk); if it's a COMMENT or DOCTYPE, the result is empty,
 * matching how those types are skipped everywhere else in this function.
 * The typical caller passes an ELEMENT (e.g. a `<p>` or `<h1>`).
 *
 * The result is built with a tbox_string_builder backed by `arena`, which
 * need not be (and typically isn't) the same arena that owns `node`'s
 * document -- the returned view stays valid for as long as `arena` does,
 * independent of the source document's lifetime. */
tbox_string_view tbox_html_node_text_content(tbox_arena *arena, const tbox_html_node *node);

/* Sets (or replaces, if `name` already exists -- case-insensitive
 * comparison, same convention as tag/attribute names used elsewhere in the
 * lib) an attribute of `node`. `name`/`value` are copied into `document`'s
 * arena (the same arena tbox_html_node_create allocates from) -- the caller
 * doesn't need to keep the original buffers alive after the call returns.
 * No `_destroy` of its own: same arena as `document`, no individual free
 * (see "Convenções" in docs/dev/convencoes.md). If `node` didn't
 * already have `name`, a new attributes array (size count+1) is allocated
 * and the existing entries copied into it -- the old array (if any) is left
 * orphaned in the arena; long-lived mutation grows the arena, an accepted
 * trade-off (see docs/dev/historico.md's "Árvore pública de mutação" design debt)
 * not addressed preemptively here. Malformed UTF-8 in `value` becomes
 * U+FFFD. A no-op if `node` isn't an ELEMENT. */
void tbox_html_node_set_attribute(tbox_html_document *document, tbox_html_node *node, tbox_string_view name, tbox_string_view value);

/* Removes an attribute by case-insensitive name. Returns true only when an
 * attribute was present. The document arena retains its storage until the
 * document is destroyed, as with other DOM mutations. */
bool tbox_html_node_remove_attribute(tbox_html_node *node, tbox_string_view name);

/* True when `node` has attribute `name` (case-insensitive). */
bool tbox_html_node_has_attribute(const tbox_html_node *node, tbox_string_view name);

/* Boolean attributes (`checked`, `disabled`, `hidden`...): `on` adds the
 * attribute with an empty value if missing, `!on` removes it. */
void tbox_html_node_set_bool_attribute(tbox_html_document *document, tbox_html_node *node, tbox_string_view name, bool on);

/* The `class` attribute as a token list (classList). Names compare
 * case-sensitively, as in `.name` selectors; a name that is empty or
 * contains ASCII whitespace is rejected (false / no-op). add keeps the
 * existing order and appends; remove drops every occurrence; toggle
 * returns whether the class is present afterwards. */
bool tbox_html_node_has_class(const tbox_html_node *node, tbox_string_view name);
void tbox_html_node_add_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name);
void tbox_html_node_remove_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name);
bool tbox_html_node_toggle_class(tbox_html_document *document, tbox_html_node *node, tbox_string_view name);

/* Linear search over node->element.attributes for `name`
 * (case-insensitive). Returns NULL if `node` isn't an ELEMENT or the
 * attribute doesn't exist -- same return pattern as
 * tbox_style_table_find/tbox_context_hit_test. */
const tbox_html_attribute *tbox_html_node_get_attribute(const tbox_html_node *node, tbox_string_view name);

/* Equivalent to DOM's `textContent` setter: detaches ALL of `node`'s current
 * children (as tbox_html_node_remove_children; their memory stays in
 * `document`'s arena -- no individual free, same trade-off
 * tbox_html_node_set_attribute already accepts for its old attributes
 * array), then creates exactly one new
 * TEXT-type child (tbox_html_node_create with TBOX_HTML_NODE_TEXT) whose
 * text is `text` copied into `document`'s arena -- the caller doesn't need
 * to keep the original buffer alive after the call returns; malformed UTF-8
 * becomes U+FFFD -- and appends
 * it via tbox_html_node_append_child. A no-op if `node` isn't an ELEMENT
 * (mutation-of-a-single-TEXT-node-in-place, e.g. editing one word inside
 * mixed text with a nested `<b>`, is out of scope -- this is "replace
 * everything", same granularity as the real DOM textContent). The Layout
 * Tree (v2) already reads text by walking direct children on every
 * relayout with no cache, so the next tbox_context_run_frame after this
 * call reflects the new text without any change to the Layout Tree. */
void tbox_html_node_set_text_content(tbox_html_document *document, tbox_html_node *node, tbox_string_view text);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_H */
