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

#endif /* TBOX_STYLE_INTERNAL_H */
