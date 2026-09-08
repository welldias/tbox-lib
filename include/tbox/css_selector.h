#ifndef TBOX_CSS_SELECTOR_H
#define TBOX_CSS_SELECTOR_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/css_parser.h>
#include <tbox/html_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking. A
 * compiled query, a node_set/match_set, or the tbox_html_node tree being
 * queried must not be shared across threads without external
 * synchronization. */

/* Matches CSS2.1 selectors (tbox_css_selector, as produced by tbox_css_parse
 * or tbox_css_selector_compile below) against a tbox_html_node tree. Every
 * tbox_css_simple_selector_kind is supported except a reduced subset of
 * PSEUDO: only "first-child" and "last-child" are evaluated (both purely
 * structural, computed from prev_sibling/next_sibling); every other
 * pseudo-class or pseudo-element (:hover, :lang(), :before, ...) never
 * matches -- deliberate scope reduction, since this library has no concept
 * of dynamic UI state or generated content to evaluate them against. ID and
 * CLASS simple selectors compare case-sensitively (per CSS2.1); TYPE, the
 * "id"/"class" attribute lookup itself, and ATTRIBUTE names compare
 * case-insensitively (tag/attribute names are already lowercased by
 * tbox_html_parse); ATTRIBUTE and ID values compare byte-exact. */

/* Opaque: owns the arena backing the compiled selector-group's AST. */
typedef struct tbox_css_selector_query tbox_css_selector_query;

typedef struct tbox_css_selector_node_set {
    const tbox_html_node **items;
    size_t count;
    void *reserved_; /* private: owns the `items` storage; touched only by tbox_css_selector_node_set_destroy */
} tbox_css_selector_node_set;

/* Compiles `text` (need not be NUL-terminated) into a reusable query. Every
 * piece of data referenced by the AST is copied into the query's own arena,
 * so `text` does not need to outlive this call.
 *
 * Grammar: a standalone selector-group -- one or more comma-separated
 * selectors, each a left-to-right chain of type/universal/id/class/
 * attribute/pseudo simple selectors joined by descendant (' '), child
 * ('>') or adjacent-sibling ('+') combinators (the same grammar
 * tbox_css_parse uses for a ruleset's selector, documented in
 * <tbox/css_parser.h>). Unlike tbox_css_parse (which tolerates malformed
 * CSS), this hard-fails on a syntax error or trailing garbage -- a selector
 * typed on its own is expected to be well-formed, the same stance
 * tbox_xpath_compile takes.
 *
 * Returns NULL on syntax error; if out_error_offset is non-NULL, receives
 * the byte offset into `text` where parsing failed. */
tbox_css_selector_query *tbox_css_selector_compile(const char *text, size_t length, size_t *out_error_offset);

void tbox_css_selector_query_destroy(tbox_css_selector_query *query);

/* True if `node` itself (not a descendant) satisfies at least one selector
 * in the compiled group. query == NULL or node == NULL returns false. */
bool tbox_css_selector_query_matches(const tbox_css_selector_query *query, const tbox_html_node *node);

/* Finds every ELEMENT descendant of `root` (root itself excluded, matching
 * querySelectorAll semantics) that satisfies at least one selector in the
 * group, in document (pre-order) order. Combinators may walk ancestors and
 * siblings outside of `root`'s own subtree (e.g. a descendant combinator
 * can match through root's ancestors) -- `root` only bounds which nodes are
 * reported as matches, not which nodes combinators may inspect while
 * evaluating one. query == NULL or root == NULL yields an empty node_set. */
tbox_css_selector_node_set tbox_css_selector_query_evaluate(const tbox_css_selector_query *query, const tbox_html_node *root);

/* Convenience: compiles, evaluates and destroys the query internally.
 * Syntax errors are reported as an empty node_set (count == 0); use
 * tbox_css_selector_compile directly when error diagnostics are needed. */
tbox_css_selector_node_set tbox_css_selector_select(const tbox_html_node *root, const char *text, size_t length);

void tbox_css_selector_node_set_destroy(tbox_css_selector_node_set *set);

/* One (ruleset, matching selector branch, matching element) triple produced
 * by tbox_css_selector_match_stylesheet. `ruleset` and `selector` point
 * into the stylesheet that was matched (selector is the specific
 * comma-separated branch of ruleset->selectors that matched); both remain
 * valid exactly as long as the stylesheet does. */
typedef struct tbox_css_selector_match {
    const tbox_css_ruleset *ruleset;
    const tbox_css_selector *selector;
    const tbox_html_node *node;
} tbox_css_selector_match;

typedef struct tbox_css_selector_match_set {
    tbox_css_selector_match *items;
    size_t count;
    void *reserved_; /* private: owns the `items` storage; touched only by tbox_css_selector_match_set_destroy */
} tbox_css_selector_match_set;

/* For every ruleset in `stylesheet`, finds every ELEMENT descendant of
 * `root` that satisfies any of its selectors -- i.e. applies the stylesheet
 * to the tree, the way a browser's selector-matching phase would. Produces
 * one tbox_css_selector_match per (ruleset, matching selector branch,
 * matching element) triple, in ruleset source order, then selector-branch
 * order, then document order. stylesheet == NULL or root == NULL yields an
 * empty match_set. */
tbox_css_selector_match_set tbox_css_selector_match_stylesheet(const tbox_css_stylesheet *stylesheet, const tbox_html_node *root);

void tbox_css_selector_match_set_destroy(tbox_css_selector_match_set *set);

/* Lowest-level primitive both APIs above are built on: true if `node`
 * itself satisfies `selector` (one already-parsed selector, e.g. one
 * element of a tbox_css_ruleset.selectors array obtained from
 * tbox_css_stylesheet_rulesets, or one element of a compiled
 * tbox_css_selector_query). Does not search descendants. selector == NULL
 * or node == NULL returns false. */
bool tbox_css_selector_matches(const tbox_css_selector *selector, const tbox_html_node *node);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_SELECTOR_H */
