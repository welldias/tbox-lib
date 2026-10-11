#ifndef TBOX_STYLE_INTERNAL_H
#define TBOX_STYLE_INTERNAL_H

#include <stdbool.h>

#include <tbox/style.h>

/* CSS-wide keywords (`inherit`, `initial`, `unset`), see
 * tbox_style_keywords.c. Resolution runs in two passes: the declarations
 * carrying a keyword are taken out of the cascade result
 * (tbox_style_keywords_strip), the remaining ones resolve normally, and
 * then each keyword copies its property's fields from the parent's or the
 * initial style (tbox_style_keywords_apply). */

/* True when some declaration in `computed` is a CSS-wide keyword. */
bool tbox_style_keywords_present(const tbox_css_computed_style *computed);

/* `computed` without its keyword declarations, except `font-size`, which
 * the em-based properties depend on during the main pass: `initial`
 * becomes `medium`, while `inherit`/`unset` are just dropped (font-size is
 * inherited anyway). The result borrows every string from `computed`;
 * release it with tbox_style_keywords_release, not
 * tbox_css_computed_style_destroy. */
tbox_css_computed_style tbox_style_keywords_strip(const tbox_css_computed_style *computed);
void tbox_style_keywords_release(tbox_css_computed_style *stripped);

/* Copies the fields of every keyword declaration's property into `style`:
 * from `parent` for `inherit` (and `unset` on an inherited property), from
 * `initial` otherwise -- `initial` too when there is no parent. A field is
 * left alone when an ordinary declaration of higher cascade priority also
 * sets it (e.g. `margin: inherit` under a more specific `margin-top`). */
void tbox_style_keywords_apply(tbox_style *style, const tbox_style *parent, const tbox_style *initial, const tbox_css_computed_style *computed);

/* CSS custom properties (tbox_style_vars.c). */
typedef struct tbox_arena tbox_arena;

/* True when `computed` declares a custom property or uses var(). */
bool tbox_style_vars_present(const tbox_css_computed_style *computed);

/* `computed` with every var() replaced -- by this element's own `--x`
 * declarations, then `inherited` -- and the custom declarations removed; a
 * declaration whose var() can't be resolved (no value, no fallback, or a
 * cycle) becomes `unset`. Everything lives in `arena`. `*out_own` receives
 * the element's custom-property set for its children (its own
 * declarations chained to `inherited`) when `keep_own`, else `inherited`. */
tbox_css_computed_style tbox_style_vars_substitute(const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, tbox_arena *arena, const tbox_style_custom_properties **out_own, bool keep_own);

/* True when some declaration in `computed` uses light-dark(). */
bool tbox_style_light_dark_present(const tbox_css_computed_style *computed);

/* `computed` with every light-dark(<light>, <dark>) replaced by one of its
 * arguments (`dark` picks the second); a malformed one makes its
 * declaration `unset`. Everything lives in `arena`. */
tbox_css_computed_style tbox_style_light_dark_substitute(const tbox_css_computed_style *computed, bool dark, tbox_arena *arena);

/* Generated content -- ::before/::after, counters, quotes
 * (tbox_style_generated.c). One state per tbox_style_resolve_tree walk,
 * fed in document order. */
typedef struct tbox_style_generated tbox_style_generated;

/* NULL (nothing to do) when no rule in `sources` targets ::before or
 * ::after; else a state in `arena`. */
tbox_style_generated *tbox_style_generated_create(tbox_arena *arena, const tbox_css_cascade_source *sources, size_t source_count);

/* Whether a rule ends in ::before (`which` 0) or ::after (1). */
bool tbox_style_generated_wants(const tbox_style_generated *generated, int which);

/* Whether `element` can have ::before/::after boxes (not a replaced or
 * void element, nor a form control drawn by tbox). */
bool tbox_style_generated_allowed(const tbox_html_node *element);

/* Applies counter-reset, counter-increment and counter-set (in that order)
 * declared in `computed`; counters a reset creates are scoped to the
 * children of `scope_parent` from here on. */
void tbox_style_generated_counters(tbox_style_generated *generated, const tbox_html_node *scope_parent, const tbox_css_computed_style *computed);

/* `quotes` declared in `computed` becomes the current list; the previous
 * one is returned for tbox_style_generated_restore_quotes. */
const void *tbox_style_generated_quotes(tbox_style_generated *generated, const tbox_css_computed_style *computed);
void tbox_style_generated_restore_quotes(tbox_style_generated *generated, const void *previous);

/* The text of `content` in `computed` (strings, attr(), counter(),
 * counters(), quotes). False when it generates no box (none, normal,
 * missing, or nothing usable such as a lone url()). */
bool tbox_style_generated_content(tbox_style_generated *generated, const tbox_html_node *element, const tbox_css_computed_style *computed, tbox_string_view *out);

/* A detached synthetic element `::before`/`::after` (`which` 0/1) whose
 * parent pointer is `element`, with one TEXT child holding `text` (none
 * when empty). tbox_layout_build links it in while it lays out. */
tbox_html_node *tbox_style_generated_node(tbox_style_generated *generated, const tbox_html_node *element, int which, tbox_string_view text);

/* Leaving `element`: drops the counters its children created. */
void tbox_style_generated_leave(tbox_style_generated *generated, const tbox_html_node *element);

#endif /* TBOX_STYLE_INTERNAL_H */
