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

/* One of the 140 CSS extended color keywords (CSS Color Module Level 3
 * "Extended color keywords", a superset of CSS2.1's own 17 keywords), as its
 * resolved sRGB channels plus alpha (0 == fully transparent, 255 == fully
 * opaque). Every one of the 140 keywords is fully opaque (a == 255) -- none
 * of them carries built-in transparency -- but the field is here so this
 * type also fits colors with an alpha component, such as CSS3's
 * "transparent" keyword or an rgba()/hsla() value, without a second type. */
typedef struct tbox_css_rgba {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} tbox_css_rgba;

/* Looks up `name` among the 140 CSS extended color keywords (e.g.
 * "cornflowerblue", "CornflowerBlue", "CORNFLOWERBLUE" -- matching is ASCII
 * case-insensitive, per CSS2.1 keyword matching). On a match, writes the
 * keyword's RGB value (with a == 255, since every keyword is opaque) to
 * *out_color (when out_color != NULL) and returns true. Returns false,
 * leaving *out_color untouched, if `name` doesn't match any of the 140
 * keywords, or name.data == NULL. */
bool tbox_css_named_color_find(tbox_string_view name, tbox_css_rgba *out_color);

/* Reverse of tbox_css_named_color_find: returns the keyword for `color`'s
 * exact RGBA value (alpha included -- a color with a != 255 can never match,
 * since every keyword is opaque), or an empty view (size == 0) if no keyword
 * has that value. A handful of keyword pairs alias the same RGB value
 * (Aqua/Cyan are both #00FFFF, Fuchsia/Magenta are both #FF00FF); for those
 * this consistently returns whichever of the pair is declared first in
 * tbox_css_named_color.c. The returned view aliases static storage that
 * lives for the whole program -- never free it. */
tbox_string_view tbox_css_named_color_name(tbox_css_rgba color);

/* Parses a CSS2.1/CSS3 <hex-color> token -- '#' followed by exactly 3, 4, 6,
 * or 8 hex digits (0-9, a-f, A-F; case-insensitive) -- into *out_color (when
 * out_color != NULL), and returns true. The 3- and 4-digit forms are
 * shorthand: each digit is duplicated to make a byte (e.g. "#0af" is the
 * same color as "#00aaff"); 4 and 8 digits add an alpha channel as the last
 * component ("#0000" is fully transparent black), while 3 and 6 digits
 * leave a == 255 (fully opaque). Returns false, leaving *out_color
 * untouched, for anything else -- missing '#', a digit count other than 3/4/
 * 6/8, a non-hex character, or hex.data == NULL -- including a leading/
 * trailing '#' or whitespace that a caller hasn't already trimmed. */
bool tbox_css_hex_to_rgba(tbox_string_view hex, tbox_css_rgba *out_color);

/* HSL(A) color components as already-parsed numbers, for tbox_css_hsla_to_rgba
 * -- this does not parse the "hsla(...)" functional-notation text itself
 * (that belongs to tokenizing/parsing a CSS value, not this conversion), only
 * converts the four numbers a caller has already extracted from it.
 *   h: hue in degrees. Any real value is accepted; it is taken modulo 360
 *      (negative values wrap the same way CSS itself defines hue to), so
 *      e.g. -90 and 270 are equivalent.
 *   s, l, a: saturation, lightness, and alpha, each clamped into [0, 1] if
 *      outside that range (e.g. CSS "hsla(0, 150%, 50%, 1)" -- s == 1.5 --
 *      clamps to s == 1.0, matching how out-of-range CSS color components
 *      are commonly handled rather than rejected). */
typedef struct tbox_css_hsla {
    double h;
    double s;
    double l;
    double a;
} tbox_css_hsla;

/* Converts `hsla` to sRGBA via the standard CSS/SVG HSL-to-RGB algorithm
 * (CSS Color Module Level 3 section 4.2), rounding each of r/g/b to the
 * nearest byte and clamping `hsla.a` into [0, 1] before scaling it to a
 * byte. Pure function of its input; always succeeds. */
tbox_css_rgba tbox_css_hsla_to_rgba(tbox_css_hsla hsla);

/* Which of the CSS2.1/CSS3 color value syntaxes a <color> string uses, per
 * tbox_css_color_detect_format. TBOX_CSS_COLOR_FORMAT_UNKNOWN covers both "not
 * a color at all" and "looks like none of the other four shapes". Detecting
 * a format doesn't guarantee the value parses -- e.g. "#zz" is detected as
 * HEXA (it starts with '#') but tbox_css_hex_to_rgba still rejects it. */
typedef enum tbox_css_color_format {
    TBOX_CSS_COLOR_FORMAT_UNKNOWN = 0,
    TBOX_CSS_COLOR_FORMAT_HEXA,       /* "#rgb", "#rgba", "#rrggbb", "#rrggbbaa" */
    TBOX_CSS_COLOR_FORMAT_RGBA,       /* "rgb(...)" or "rgba(...)" */
    TBOX_CSS_COLOR_FORMAT_HSLA,       /* "hsl(...)" or "hsla(...)" */
    TBOX_CSS_COLOR_FORMAT_COLOR_NAME, /* one of the 140 keywords, e.g. "CornflowerBlue" */
} tbox_css_color_format;

/* Classifies `value` by shape alone, cheaply and without allocating:
 *   - starts with '#'                                -> HEXA
 *   - case-insensitively starts with "rgb(" / "rgba(" -> RGBA
 *   - case-insensitively starts with "hsl(" / "hsla(" -> HSLA
 *   - otherwise, an exact match (case-insensitive) against one of the 140
 *     CSS extended color keywords (via tbox_css_named_color_find)
 *                                                      -> COLOR_NAME
 *   - anything else, or value.size == 0                -> UNKNOWN
 * `value` must already have any leading/trailing whitespace trimmed by the
 * caller (as tbox_css_parse already does for tbox_css_declaration.value) --
 * this never trims it itself, matching tbox_css_hex_to_rgba's contract. */
tbox_css_color_format tbox_css_color_detect_format(tbox_string_view value);

/* Parses a CSS2.1/CSS3 functional rgb()/rgba() color: "rgb(R, G, B)" or
 * "rgba(R, G, B, A)" (the two names are accepted interchangeably regardless
 * of whether an alpha argument follows, matching how browsers treat them).
 * Each of R/G/B is a plain number or a percentage (0%-100%); either way it
 * is clamped into [0, 255] after scaling. A is a plain number (0-1) or a
 * percentage (0%-100%), clamped into [0, 1] and scaled to a byte. When A
 * is omitted, a == 255. Internal whitespace around commas/parens is
 * tolerated ("rgb( 10 , 20,30 )" is fine), but `value` itself must already
 * be trimmed of leading/trailing whitespace and have no other surrounding
 * text. Writes to *out_color (when out_color != NULL) and returns true on
 * success; returns false, leaving *out_color untouched, if `value` isn't
 * "rgb("/"rgba(" followed by 3 or 4 comma-separated components and a closing
 * ')', or any component fails to parse as a number/percentage. */
bool tbox_css_rgb_to_rgba(tbox_string_view value, tbox_css_rgba *out_color);

/* Parses a CSS2.1/CSS3 functional hsl()/hsla() color: "hsl(H, S%, L%)" or
 * "hsla(H, S%, L%, A)" (the two names are accepted interchangeably regardless
 * of whether an alpha argument follows). H is a plain number of degrees (see
 * tbox_css_hsla.h -- no unit suffix is accepted); S and L must each carry a
 * '%' suffix (that's required by the CSS grammar, unlike rgb()'s R/G/B).
 * A is a plain number (0-1) or a percentage (0%-100%); when omitted, a ==
 * 255. Converts via tbox_css_hsla_to_rgba, so H/S/L/A are wrapped/clamped
 * exactly as that function documents. Same whitespace contract as
 * tbox_css_rgb_to_rgba (internal whitespace tolerated, `value` itself must
 * already be trimmed). Writes to *out_color (when out_color != NULL) and
 * returns true on success; returns false, leaving *out_color untouched, if
 * `value` isn't "hsl("/"hsla(" followed by 3 or 4 comma-separated components
 * and a closing ')', H carries a '%' suffix, S or L is missing its '%'
 * suffix, or any component fails to parse. */
bool tbox_css_hsl_to_rgba(tbox_string_view value, tbox_css_rgba *out_color);

/* Parses any CSS2.1/CSS3 <color> value -- hex, rgb()/rgba(), hsl()/hsla(),
 * or one of the 140 named keywords -- into *out_color (when out_color !=
 * NULL), and returns true. Dispatches on tbox_css_color_detect_format(value)
 * to tbox_css_hex_to_rgba, tbox_css_rgb_to_rgba, tbox_css_hsl_to_rgba, or
 * tbox_css_named_color_find respectively, so see those for exactly what each
 * syntax accepts; `value` must already be trimmed the same way their
 * contracts require. Returns false, leaving *out_color untouched, if the
 * format can't be detected (TBOX_CSS_COLOR_FORMAT_UNKNOWN) or the detected
 * format's own parser rejects `value` (e.g. "#zz" is detected as HEXA but
 * still fails to parse). */
bool tbox_css_color_parse(tbox_string_view value, tbox_css_rgba *out_color);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CSS_CASCADE_H */
