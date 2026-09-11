#ifndef TBOX_CSS_CASCADE_H
#define TBOX_CSS_CASCADE_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/css_parser.h>
#include <tbox/html_parser.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Resolves the CSS2.1 cascade (chapter 6.4) for a single tbox_html_node
 * against one or more tbox_css_stylesheet, deciding -- per property, not
 * per ruleset -- which single declaration among every applicable one wins.
 * Priority order, highest first:
 *   1. origin + !important (see tbox_css_origin below for the exact order;
 *      this library extends CSS2.1's five levels to six, per CSS Cascading
 *      Level 4, to give "user-agent origin + !important" a defined place --
 *      CSS2.1 itself leaves that combination unspecified).
 *   2. selector specificity (a, b, c) -- see tbox_css_cascade_specificity.
 *   3. source order: a stylesheet later in the `sources` array beats an
 *      earlier one; within one stylesheet, a ruleset later in
 *      tbox_css_stylesheet_rulesets beats an earlier one; within one
 *      ruleset, a later declaration beats an earlier one.
 * A declaration's origin is never inferred from the stylesheet's content --
 * tbox_css_stylesheet carries no origin of its own; the caller assigns one
 * per stylesheet via tbox_css_cascade_source when calling
 * tbox_css_cascade_resolve. Property/value validity is not checked here
 * either, matching tbox_css_parse's own stance (see <tbox/css_parser.h>). */

/* CSS2.1 selector specificity as the tuple (a, b, c):
 *   a: number of ID simple selectors.
 *   b: number of class, attribute, and pseudo-class simple selectors.
 *   c: number of type simple selectors and pseudo-element simple selectors.
 * Summed across every simple selector of the entire chain -- every compound
 * of the selector, not just the one that matched the node directly -- e.g.
 * "div.a > span#b" contributes c += 2 (div, span), b += 1 (.a), a += 1
 * (#b). Universal selectors ('*') contribute to none of the three. There is
 * no fourth "style attribute" bucket: tbox has no concept of an inline
 * `style` attribute overriding the cascade. */
typedef struct tbox_css_specificity {
    unsigned int a;
    unsigned int b;
    unsigned int c;
} tbox_css_specificity;

/* Computes the specificity of `selector`. CSS2.1 has no "::" syntax, so
 * tbox_css_simple_selector_kind's PSEUDO covers both pseudo-classes and
 * pseudo-elements (see <tbox/css_parser.h>); this function tells them apart
 * by name -- "before", "after", "first-line" and "first-letter"
 * (case-insensitive) count as pseudo-elements (bucket c); every other name
 * (including "first-child"/"last-child", the only ones
 * tbox_css_selector_matches currently implements) counts as a pseudo-class
 * (bucket b). This classifies by CSS2.1 semantics, independent of which
 * pseudo-classes/elements the matching engine actually supports today.
 * selector == NULL returns {0, 0, 0}. */
tbox_css_specificity tbox_css_cascade_specificity(const tbox_css_selector *selector);

/* Lexicographic comparison of (a, b, c): negative if x < y, zero if equal,
 * positive if x > y. */
int tbox_css_cascade_specificity_compare(tbox_css_specificity x, tbox_css_specificity y);

/* If `value` ends with a CSS2.1 "!important" marker (a '!', optionally
 * surrounded by whitespace, immediately followed by "important" in any
 * ASCII case, with nothing else after it), returns the leading portion of
 * `value` with that marker -- and any whitespace immediately before the '!'
 * -- trimmed off, and sets *out_important = true (when out_important !=
 * NULL). A '!' is required immediately before the (whitespace-skipped)
 * "important" keyword, so e.g. "very important" or "veryimportant" are left
 * unchanged (no '!' at the right place) rather than treated as important.
 * Otherwise returns `value` unchanged and sets *out_important = false.
 * Never allocates or copies: the returned view aliases the same bytes as
 * `value`. tbox_css_declaration.value is never pre-stripped of this by
 * tbox_css_parse (see <tbox/css_parser.h>) -- every caller that cares about
 * !important must call this itself; tbox_css_cascade_resolve already
 * does. */
tbox_string_view tbox_css_cascade_strip_important(tbox_string_view value, bool *out_important);

/* Where a stylesheet sits in the cascade (CSS2.1 chapter 6.4, extended per
 * CSS Cascading Level 4 for the origin+!important ordering below). Purely a
 * label the caller attaches per stylesheet via tbox_css_cascade_source --
 * tbox_css_stylesheet itself carries no origin. From lowest to highest
 * final priority once !important is folded in:
 *   user-agent normal < user normal < author normal
 *     < author !important < user !important < user-agent !important
 * (CSS2.1 defines only the first five of those six; placing "user-agent +
 * !important" at the very top is the CSS Cascading Level 4 refinement this
 * library follows, since CSS2.1 leaves that combination unspecified). */
typedef enum tbox_css_origin {
    TBOX_CSS_ORIGIN_USER_AGENT,
    TBOX_CSS_ORIGIN_USER,
    TBOX_CSS_ORIGIN_AUTHOR,
} tbox_css_origin;

/* One stylesheet paired with the origin it should be resolved as, for
 * tbox_css_cascade_resolve. stylesheet == NULL makes this entry contribute
 * nothing (skipped, not an error). */
typedef struct tbox_css_cascade_source {
    const tbox_css_stylesheet *stylesheet;
    tbox_css_origin origin;
} tbox_css_cascade_source;

/* One declaration that won the cascade for one property, on one node,
 * against one call's set of sources. `ruleset` points into whichever
 * stylesheet produced it; `selector` is whichever comma-separated branch of
 * ruleset->selectors actually matched the node and had the highest
 * specificity among the branches that did (all branches of one ruleset
 * share the same declarations, so only the winning branch's specificity is
 * kept -- see tbox_css_cascade_resolve). `property`/`value` are views into
 * that same stylesheet (value with any "!important" suffix already
 * stripped by tbox_css_cascade_strip_important). All of these remain valid
 * exactly as long as the stylesheet they came from does -- the same
 * aliasing contract as tbox_css_selector_match. */
typedef struct tbox_css_resolved_declaration {
    const tbox_css_ruleset *ruleset;
    const tbox_css_selector *selector;
    tbox_css_origin origin;
    tbox_string_view property;
    tbox_string_view value;
    bool important;
    tbox_css_specificity specificity;
} tbox_css_resolved_declaration;

typedef struct tbox_css_computed_style {
    tbox_css_resolved_declaration *items;
    size_t count;
    void *reserved_; /* private: owns the `items` storage; touched only by tbox_css_computed_style_destroy */
} tbox_css_computed_style;

/* Resolves the cascade of `sources` for `node` itself (not its
 * descendants): for every property that appears in at least one applicable
 * declaration (any declaration of any ruleset, in any source, that has a
 * selector branch matching `node`, tested via tbox_css_selector_matches),
 * keeps exactly the one declaration that wins per this header's priority
 * order. Items appear in the order each distinct property was first
 * encountered while scanning sources/rulesets/declarations in the order
 * documented above -- NOT sorted by property name, and NOT necessarily at
 * the winning declaration's own source position (a later declaration can
 * overwrite an earlier item in place without moving it). Entries of
 * `sources` with stylesheet == NULL are skipped. sources == NULL with
 * source_count == 0, or node == NULL, yields an empty computed_style (count
 * == 0, still safe to pass to tbox_css_computed_style_destroy). */
tbox_css_computed_style tbox_css_cascade_resolve(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node);

/* Convenience for the common case of a single stylesheet resolved as
 * TBOX_CSS_ORIGIN_AUTHOR -- equivalent to tbox_css_cascade_resolve with a
 * one-element sources array. stylesheet == NULL or node == NULL yields an
 * empty computed_style. */
tbox_css_computed_style tbox_css_cascade_resolve_stylesheet(const tbox_css_stylesheet *stylesheet, const tbox_html_node *node);

void tbox_css_computed_style_destroy(tbox_css_computed_style *style);

/* Linear scan for the resolved declaration of `property`, compared ASCII
 * case-insensitively (tbox_css_declaration.property is already lowercase
 * ASCII as produced by tbox_css_parse, but this is case-insensitive anyway
 * for caller convenience). Returns NULL if style == NULL or no resolved
 * declaration has that property. The returned pointer aliases into
 * style->items and remains valid exactly as long as `style` does. */
const tbox_css_resolved_declaration *tbox_css_computed_style_find(const tbox_css_computed_style *style, tbox_string_view property);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_CASCADE_H */
