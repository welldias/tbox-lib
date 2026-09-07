#ifndef TBOX_CSS_PARSER_H
#define TBOX_CSS_PARSER_H

#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking. */

/* CSS2.1's own grammar calls a fused run of these one "simple_selector"; here
 * each qualifier is its own list item, joined via
 * tbox_css_simple_selector.combinator_before (see that field's comment). */
typedef enum tbox_css_simple_selector_kind {
    TBOX_CSS_SIMPLE_SELECTOR_TYPE,      /* element_name, e.g. "div"; name is lowercase ASCII */
    TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL, /* '*'; name is empty */
    TBOX_CSS_SIMPLE_SELECTOR_ID,        /* '#' name; name preserved verbatim (case-sensitive) */
    TBOX_CSS_SIMPLE_SELECTOR_CLASS,     /* '.' name; name preserved verbatim (case-sensitive) */
    TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE, /* '[' name [ operator value ] ']' */
    TBOX_CSS_SIMPLE_SELECTOR_PSEUDO,    /* ':' name [ '(' argument ')' ]. CSS2.1 has no '::'
                                          * syntax, so pseudo-classes (:hover, :first-child) and
                                          * pseudo-elements (:before, :after) are lexically
                                          * identical and share this one kind. */
} tbox_css_simple_selector_kind;

typedef enum tbox_css_attribute_operator {
    TBOX_CSS_ATTR_EXISTS,    /* [attr] */
    TBOX_CSS_ATTR_EQUALS,    /* [attr=val] */
    TBOX_CSS_ATTR_INCLUDES,  /* [attr~=val]  (val is one of a space-separated list) */
    TBOX_CSS_ATTR_DASHMATCH, /* [attr|=val]  (val, or val followed by '-', is a case-sensitive prefix) */
} tbox_css_attribute_operator;

/* How a simple selector attaches to the one immediately before it within the
 * same tbox_css_selector.simple_selectors run.
 *   NONE:             fused into the same CSS2.1 "compound" as the previous
 *                      item -- no combinator/whitespace separated them in the
 *                      source (e.g. in "div#a.b:hover", the '#a', '.b' and
 *                      ':hover' items all have combinator_before == NONE).
 *                      The first item of every tbox_css_selector always has
 *                      combinator_before == NONE (nothing precedes it).
 *   DESCENDANT:        ' '  (whitespace, no explicit combinator symbol)
 *   CHILD:             '>'
 *   ADJACENT_SIBLING:  '+'
 * A future selector-matching engine walks simple_selectors left to right,
 * starting a new compound (AND-group) every time combinator_before != NONE,
 * treating a compound with no TYPE/UNIVERSAL item as an implicit universal,
 * exactly as CSS2.1 defines it. */
typedef enum tbox_css_combinator {
    TBOX_CSS_COMBINATOR_NONE,
    TBOX_CSS_COMBINATOR_DESCENDANT,
    TBOX_CSS_COMBINATOR_CHILD,
    TBOX_CSS_COMBINATOR_ADJACENT_SIBLING,
} tbox_css_combinator;

/* Every tbox_string_view below is copied into the owning tbox_css_stylesheet's
 * arena; none of them point into the caller's original `input` buffer passed
 * to tbox_css_parse, so `input` may be freed immediately after that call
 * returns (same contract as tbox_html_parse). */
typedef struct tbox_css_simple_selector {
    tbox_css_simple_selector_kind kind;
    tbox_css_combinator combinator_before;

    /* TYPE: tag name (lowercased). ID/CLASS: name after '#'/'.' (verbatim).
     * ATTRIBUTE: attribute name (lowercased). PSEUDO: name without the
     * leading ':' (lowercased). UNIVERSAL: empty (size == 0). */
    tbox_string_view name;

    tbox_css_attribute_operator attribute_operator; /* meaningful for ATTRIBUTE only */
    tbox_string_view attribute_value;                /* ATTRIBUTE + EQUALS/INCLUDES/DASHMATCH only; empty for EXISTS */
    tbox_string_view pseudo_argument;                /* PSEUDO functional form only (":lang(en)" -> "en"); empty otherwise */
} tbox_css_simple_selector;

/* One selector: a left-to-right chain of simple selectors. Corresponds to one
 * item of a comma-separated selector group -- e.g. in "div.a, p > span",
 * "div.a" and "p > span" are each one tbox_css_selector. */
typedef struct tbox_css_selector {
    tbox_css_simple_selector *simple_selectors;
    size_t simple_selector_count; /* always >= 1 */
} tbox_css_selector;

typedef struct tbox_css_declaration {
    tbox_string_view property; /* lowercase ASCII, e.g. "color" */
    tbox_string_view value;    /* raw, unparsed text between ':' and the terminating ';' or
                                 * '}' -- leading/trailing whitespace and comments trimmed,
                                 * everything else (internal whitespace, commas, quoted
                                 * strings, nested parens) preserved byte-for-byte. May be
                                 * empty (size == 0), e.g. for "color: ;". Not validated or
                                 * tokenized further -- that is future work. */
} tbox_css_declaration;

typedef struct tbox_css_ruleset {
    tbox_css_selector *selectors;
    size_t selector_count; /* always >= 1 */
    tbox_css_declaration *declarations;
    size_t declaration_count; /* may be 0, e.g. "p { }" */
} tbox_css_ruleset;

/* Opaque: owns the arena backing every ruleset/selector/declaration/string
 * reachable from it. */
typedef struct tbox_css_stylesheet tbox_css_stylesheet;

/* Parses `length` bytes of CSS 2.1 starting at `input` (need not be
 * NUL-terminated -- suitable both for a whole .css file and for text
 * extracted from an HTML <style> element).
 *
 * Grammar coverage: rulesets (selector-group '{' declaration-list '}'),
 * structured selectors (type/universal/id/class/attribute/pseudo-class-or
 * -element, joined by descendant/child/adjacent-sibling combinators), and
 * declarations (property + raw value text). At-rules (@media, @import,
 * @charset, @font-face, @page, ...) are recognized structurally just enough
 * to be skipped whole (their block, if any, or up to the next ';'); nothing
 * about their contents is stored. Property/value validity is not checked at
 * all in this version (future work); declaration values are stored
 * verbatim, unparsed.
 *
 * Malformed CSS is tolerated per CSS2.1's Appendix G.1 error-recovery rules
 * (a broken declaration is skipped up to its ';' or the block's '}'; a
 * broken ruleset -- including an invalid selector -- is skipped up to and
 * including its '{...}' block, or up to the next ';'/EOF if it never reaches
 * a block); the parser never fails on bad CSS. Returns NULL only on
 * allocation failure. */
tbox_css_stylesheet *tbox_css_parse(const char *input, size_t length);

size_t tbox_css_stylesheet_ruleset_count(const tbox_css_stylesheet *stylesheet);

/* Array of tbox_css_stylesheet_ruleset_count(stylesheet) elements, in source
 * order. NULL if ruleset_count == 0. */
const tbox_css_ruleset *tbox_css_stylesheet_rulesets(const tbox_css_stylesheet *stylesheet);

/* Frees the stylesheet's arena and everything allocated from it. */
void tbox_css_stylesheet_destroy(tbox_css_stylesheet *stylesheet);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_PARSER_H */
