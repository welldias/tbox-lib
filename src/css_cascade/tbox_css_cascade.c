#include <tbox/css_cascade.h>

#include <tbox/css_selector.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* CSS2.1 "white space": space, tab, line feed, carriage return, form feed --
 * same definition tbox_css_selector_match.c uses. */
static bool tbox_css_cascade_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
}

/* Copies `view`'s bytes into `arena`, returning a NEW tbox_string_view that
 * owns memory of its own instead of aliasing whatever `view.data` pointed
 * at -- needed by tbox_css_cascade_resolve below, whose `property`/`value`
 * string views can otherwise alias a SYNTHETIC style="" stylesheet
 * (tbox_css_cascade_parse_inline_style's result) that gets destroyed
 * before the caller ever sees the returned tbox_css_computed_style, a
 * violation of this header's own documented aliasing contract ("exactly as
 * long as the stylesheet they came from does"). `view.size == 0` still
 * produces a valid, zero-length view (whatever `tbox_arena_alloc` returns
 * for a zero-size request, same as `tbox_string_view_make(NULL, 0)`
 * elsewhere in this project -- a NULL `data` with `size == 0` is a normal,
 * already-used empty view, never dereferenced). */
static tbox_string_view tbox_css_cascade_copy_view(tbox_arena *arena, tbox_string_view view) {
    char *copy = tbox_arena_alloc(arena, view.size);
    if (view.size > 0) {
        memcpy(copy, view.data, view.size);
    }
    return tbox_string_view_make(copy, view.size);
}

static bool tbox_css_cascade_is_known_pseudo_element(tbox_string_view name) {
    static const char *const pseudo_elements[] = { "before", "after", "first-line", "first-letter" };
    for (size_t i = 0; i < sizeof(pseudo_elements) / sizeof(pseudo_elements[0]); i++) {
        if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(pseudo_elements[i]))) {
            return true;
        }
    }
    return false;
}

tbox_css_specificity tbox_css_cascade_specificity(const tbox_css_selector *selector) {
    tbox_css_specificity spec = { 0, 0, 0 };
    if (selector == NULL) {
        return spec;
    }

    for (size_t i = 0; i < selector->simple_selector_count; i++) {
        const tbox_css_simple_selector *item = &selector->simple_selectors[i];
        switch (item->kind) {
        case TBOX_CSS_SIMPLE_SELECTOR_ID:
            spec.a++;
            break;
        case TBOX_CSS_SIMPLE_SELECTOR_CLASS:
        case TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE:
            spec.b++;
            break;
        case TBOX_CSS_SIMPLE_SELECTOR_PSEUDO:
            if (tbox_css_cascade_is_known_pseudo_element(item->name)) {
                spec.c++;
            } else {
                spec.b++;
            }
            break;
        case TBOX_CSS_SIMPLE_SELECTOR_TYPE:
            spec.c++;
            break;
        case TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL:
            break;
        }
    }
    return spec;
}

int tbox_css_cascade_specificity_compare(tbox_css_specificity x, tbox_css_specificity y) {
    if (x.a != y.a) {
        return x.a < y.a ? -1 : 1;
    }
    if (x.b != y.b) {
        return x.b < y.b ? -1 : 1;
    }
    if (x.c != y.c) {
        return x.c < y.c ? -1 : 1;
    }
    return 0;
}

tbox_string_view tbox_css_cascade_strip_important(tbox_string_view value, bool *out_important) {
    if (out_important != NULL) {
        *out_important = false;
    }

    static const char keyword[] = "important";
    const size_t keyword_length = sizeof(keyword) - 1;

    if (value.size < keyword_length) {
        return value;
    }

    size_t tail_start = value.size - keyword_length;
    for (size_t i = 0; i < keyword_length; i++) {
        char byte = value.data[tail_start + i];
        if (byte >= 'A' && byte <= 'Z') {
            byte = (char)(byte - 'A' + 'a');
        }
        if (byte != keyword[i]) {
            return value;
        }
    }

    size_t bang = tail_start;
    while (bang > 0 && tbox_css_cascade_is_space(value.data[bang - 1])) {
        bang--;
    }
    if (bang == 0 || value.data[bang - 1] != '!') {
        return value;
    }

    size_t result_size = bang - 1;
    while (result_size > 0 && tbox_css_cascade_is_space(value.data[result_size - 1])) {
        result_size--;
    }

    if (out_important != NULL) {
        *out_important = true;
    }
    return tbox_string_view_make(value.data, result_size);
}

/* Total order over (origin, important) per this header's documented
 * priority -- rank[origin][important ? 1 : 0]. */
static int tbox_css_cascade_rank(tbox_css_origin origin, bool important) {
    static const int rank[4][2] = {
        /* USER_AGENT    */ { 0, 7 },
        /* USER          */ { 1, 6 },
        /* AUTHOR        */ { 2, 4 },
        /* AUTHOR_INLINE */ { 3, 5 },
    };
    return rank[origin][important ? 1 : 0];
}

int tbox_css_cascade_priority_compare(const tbox_css_resolved_declaration *a,
                                      const tbox_css_resolved_declaration *b) {
    int rank_a = tbox_css_cascade_rank(a->origin, a->important);
    int rank_b = tbox_css_cascade_rank(b->origin, b->important);
    if (rank_a != rank_b) return rank_a > rank_b ? 1 : -1;
    int specificity = tbox_css_cascade_specificity_compare(a->specificity, b->specificity);
    if (specificity != 0) return specificity;
    return (a->source_order > b->source_order) - (a->source_order < b->source_order);
}

static bool tbox_css_cascade_wins_or_ties(const tbox_css_resolved_declaration *candidate, const tbox_css_resolved_declaration *existing) {
    return tbox_css_cascade_priority_compare(candidate, existing) >= 0;
}

/* Shared "does a winner already exist for this property? does the candidate
 * win or tie against it?" bookkeeping -- used both by the sources scan below
 * and by the style="" inline step, so the decision logic lives in exactly
 * one place. `winners` holds tbox_css_resolved_declaration items, one per
 * distinct property seen so far. */
static void tbox_css_cascade_offer(tbox_vector *winners, const tbox_css_resolved_declaration *candidate) {
    tbox_css_resolved_declaration *existing = NULL;
    size_t winner_count                     = tbox_vector_length(winners);
    for (size_t w = 0; w < winner_count; w++) {
        tbox_css_resolved_declaration *item = tbox_vector_at(winners, w);
        if (tbox_string_view_equal(item->property, candidate->property)) {
            existing = item;
            break;
        }
    }

    if (existing == NULL) {
        *(tbox_css_resolved_declaration *)tbox_vector_push(winners) = *candidate;
    } else if (tbox_css_cascade_wins_or_ties(candidate, existing)) {
        *existing = *candidate;
    }
}

/* Wraps `declarations` (the raw text of a node's style="" HTML attribute) in
 * a synthetic "* { ... }" ruleset -- the universal selector matches any
 * node, so every declaration in it applies unconditionally -- and parses
 * that via tbox_css_parse, exactly like any other CSS text. Returns NULL if
 * `declarations` is empty, or if tbox_css_parse itself fails (allocation
 * failure only -- propagated as-is, not treated specially). The scratch
 * buffer built here is freed before returning: tbox_css_parse already
 * copies everything it needs out of `input` before it returns (see
 * <tbox/css_parser.h>), so the buffer doesn't need to outlive this call. */
static tbox_css_stylesheet *tbox_css_cascade_parse_inline_style(tbox_string_view declarations) {
    if (declarations.size == 0) {
        return NULL;
    }

    static const char prefix[] = "* {";
    static const char suffix[] = "}";
    const size_t prefix_length = sizeof(prefix) - 1;
    const size_t suffix_length = sizeof(suffix) - 1;
    const size_t css_length    = prefix_length + declarations.size + suffix_length;

    char *buffer = malloc(css_length + 1);
    if (buffer == NULL) {
        return NULL;
    }

    memcpy(buffer, prefix, prefix_length);
    memcpy(buffer + prefix_length, declarations.data, declarations.size);
    memcpy(buffer + prefix_length + declarations.size, suffix, suffix_length);
    buffer[css_length] = '\0';

    tbox_css_stylesheet *result = tbox_css_parse(buffer, css_length);
    free(buffer);
    return result;
}

tbox_css_computed_style tbox_css_cascade_resolve(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node) {
    tbox_css_computed_style result = { .items = NULL, .count = 0, .reserved_ = NULL };
    if (node == NULL) {
        return result;
    }

    tbox_arena scratch = tbox_arena_create(0);

    tbox_vector winners;
    tbox_vector_init(&winners, &scratch, sizeof(tbox_css_resolved_declaration), 0);
    size_t source_order = 0;

    for (size_t src = 0; src < source_count; src++) {
        const tbox_css_stylesheet *stylesheet = sources[src].stylesheet;
        if (stylesheet == NULL) {
            continue;
        }
        tbox_css_origin origin = sources[src].origin;

        size_t ruleset_count             = tbox_css_stylesheet_ruleset_count(stylesheet);
        const tbox_css_ruleset *rulesets = tbox_css_stylesheet_rulesets(stylesheet);

        for (size_t r = 0; r < ruleset_count; r++) {
            const tbox_css_ruleset *ruleset = &rulesets[r];

            const tbox_css_selector *best_selector = NULL;
            tbox_css_specificity best_specificity  = { 0, 0, 0 };
            for (size_t s = 0; s < ruleset->selector_count; s++) {
                const tbox_css_selector *selector = &ruleset->selectors[s];
                if (!tbox_css_selector_matches(selector, node)) {
                    continue;
                }
                tbox_css_specificity specificity = tbox_css_cascade_specificity(selector);
                if (best_selector == NULL || tbox_css_cascade_specificity_compare(specificity, best_specificity) > 0) {
                    best_selector    = selector;
                    best_specificity = specificity;
                }
            }

            if (best_selector == NULL) {
                continue;
            }

            for (size_t d = 0; d < ruleset->declaration_count; d++) {
                const tbox_css_declaration *declaration = &ruleset->declarations[d];

                tbox_css_resolved_declaration candidate;
                candidate.ruleset     = ruleset;
                candidate.selector    = best_selector;
                candidate.origin      = origin;
                candidate.property    = declaration->property;
                candidate.value       = tbox_css_cascade_strip_important(declaration->value, &candidate.important);
                candidate.specificity = best_specificity;
                candidate.source_order = source_order++;

                tbox_css_cascade_offer(&winners, &candidate);
            }
        }
    }

    /* v9: style="" inline -- resolved here, inside tbox_css_cascade_resolve
     * itself, so every caller (Style layer, tbox_css_cascade_resolve_stylesheet,
     * tests) gets it automatically without knowing it exists. Runs after the
     * sources loop above (unchanged) so it can reuse `winners` as-is. */
    if (node != NULL) {
        const tbox_html_attribute *style_attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("style", 5));
        if (style_attribute != NULL && style_attribute->value.size > 0) {
            tbox_css_stylesheet *inline_sheet = tbox_css_cascade_parse_inline_style(style_attribute->value);
            if (inline_sheet != NULL) {
                size_t ruleset_count             = tbox_css_stylesheet_ruleset_count(inline_sheet);
                const tbox_css_ruleset *rulesets = tbox_css_stylesheet_rulesets(inline_sheet);

                for (size_t r = 0; r < ruleset_count; r++) {
                    const tbox_css_ruleset *ruleset = &rulesets[r];

                    for (size_t d = 0; d < ruleset->declaration_count; d++) {
                        const tbox_css_declaration *declaration = &ruleset->declarations[d];

                        tbox_css_resolved_declaration candidate;
                        candidate.ruleset     = ruleset;
                        candidate.selector    = ruleset->selector_count > 0 ? &ruleset->selectors[0] : NULL;
                        candidate.origin      = TBOX_CSS_ORIGIN_AUTHOR_INLINE;
                        /* Deep-copied into `scratch` (still alive -- only
                         * destroyed at the very end of this function, after
                         * `result.items` is itself built from `winners`)
                         * instead of left aliasing `inline_sheet`'s own
                         * memory: `inline_sheet` is destroyed a few lines
                         * below, right after this loop, well before
                         * `winners` is ever read back out -- copying HERE,
                         * before that destroy, is the only point at which
                         * the source bytes are still guaranteed valid. See
                         * tbox_css_cascade_copy_view's doc comment for the
                         * full bug this fixes (found via a report that
                         * `style="text-align:center;"` silently did nothing
                         * on <h1>/<h2>). */
                        candidate.property    = tbox_css_cascade_copy_view(&scratch, declaration->property);
                        candidate.value       = tbox_css_cascade_copy_view(&scratch, tbox_css_cascade_strip_important(declaration->value, &candidate.important));
                        candidate.specificity = (tbox_css_specificity){ 0, 0, 0 };
                        candidate.source_order = source_order++;

                        tbox_css_cascade_offer(&winners, &candidate);
                    }
                }
            }
            tbox_css_stylesheet_destroy(inline_sheet);
        }
    }

    size_t count          = tbox_vector_length(&winners);
    tbox_arena *out_arena = count > 0 ? malloc(sizeof(tbox_arena)) : NULL;
    if (out_arena != NULL) {
        *out_arena   = tbox_arena_create(0);
        result.items = tbox_arena_alloc(out_arena, count * sizeof(tbox_css_resolved_declaration));
        for (size_t i = 0; i < count; i++) {
            result.items[i] = *(const tbox_css_resolved_declaration *)tbox_vector_at_const(&winners, i);

            /* Second half of the fix for a real bug (found while
             * investigating a report that `style="text-align: center;"`
             * silently did nothing on <h1>/<h2>, worse on elements with
             * more UA-stylesheet declarations of their own): the inline-
             * style loop above already deep-copies AUTHOR_INLINE items'
             * `property`/`value` into `scratch` before `inline_sheet` is
             * destroyed (the actual use-after-free this whole fix is
             * about), but `scratch` itself is destroyed at the end of
             * THIS function, right before returning -- `winners` (and
             * anything still aliasing `scratch`) would be dangling the
             * instant the caller got `result` back. Copying every item
             * (not just inline-origin ones, cheap either way since these
             * strings are always short) into `out_arena` -- which lives
             * exactly as long as `result` does, per this header's
             * aliasing contract -- detaches `result` from `scratch`'s
             * lifetime entirely. */
            result.items[i].property = tbox_css_cascade_copy_view(out_arena, result.items[i].property);
            result.items[i].value    = tbox_css_cascade_copy_view(out_arena, result.items[i].value);
        }
        result.count     = count;
        result.reserved_ = out_arena;
    }

    tbox_arena_destroy(&scratch);
    return result;
}

tbox_css_computed_style tbox_css_cascade_resolve_stylesheet(const tbox_css_stylesheet *stylesheet, const tbox_html_node *node) {
    tbox_css_cascade_source source = { .stylesheet = stylesheet, .origin = TBOX_CSS_ORIGIN_AUTHOR };
    return tbox_css_cascade_resolve(&source, stylesheet != NULL ? 1 : 0, node);
}

void tbox_css_computed_style_destroy(tbox_css_computed_style *style) {
    if (style == NULL || style->reserved_ == NULL) {
        return;
    }
    tbox_arena *arena = style->reserved_;
    tbox_arena_destroy(arena);
    free(arena);
    style->items     = NULL;
    style->count     = 0;
    style->reserved_ = NULL;
}

const tbox_css_resolved_declaration *tbox_css_computed_style_find(const tbox_css_computed_style *style, tbox_string_view property) {
    if (style == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < style->count; i++) {
        if (tbox_string_view_equal_ascii_ci(style->items[i].property, property)) {
            return &style->items[i];
        }
    }
    return NULL;
}
