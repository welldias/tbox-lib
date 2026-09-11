#include <tbox/css_cascade.h>

#include <tbox/css_selector.h>

#include <stdlib.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* CSS2.1 "white space": space, tab, line feed, carriage return, form feed --
 * same definition tbox_css_selector_match.c uses. */
static bool tbox_css_cascade_is_space(char byte) {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f';
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
    static const int rank[3][2] = {
        /* USER_AGENT */ { 0, 5 },
        /* USER       */
        { 1, 4 },
        /* AUTHOR     */
        { 2, 3 },
    };
    return rank[origin][important ? 1 : 0];
}

static bool tbox_css_cascade_wins_or_ties(const tbox_css_resolved_declaration *candidate, const tbox_css_resolved_declaration *existing) {
    int candidate_rank = tbox_css_cascade_rank(candidate->origin, candidate->important);
    int existing_rank  = tbox_css_cascade_rank(existing->origin, existing->important);
    if (candidate_rank != existing_rank) {
        return candidate_rank > existing_rank;
    }
    return tbox_css_cascade_specificity_compare(candidate->specificity, existing->specificity) >= 0;
}

tbox_css_computed_style tbox_css_cascade_resolve(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node) {
    tbox_css_computed_style result = { .items = NULL, .count = 0, .reserved_ = NULL };
    if (node == NULL) {
        return result;
    }

    tbox_arena scratch = tbox_arena_create(0);

    tbox_vector winners;
    tbox_vector_init(&winners, &scratch, sizeof(tbox_css_resolved_declaration), 0);

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

                tbox_css_resolved_declaration *existing = NULL;
                size_t winner_count                     = tbox_vector_length(&winners);
                for (size_t w = 0; w < winner_count; w++) {
                    tbox_css_resolved_declaration *item = tbox_vector_at(&winners, w);
                    if (tbox_string_view_equal(item->property, candidate.property)) {
                        existing = item;
                        break;
                    }
                }

                if (existing == NULL) {
                    *(tbox_css_resolved_declaration *)tbox_vector_push(&winners) = candidate;
                } else if (tbox_css_cascade_wins_or_ties(&candidate, existing)) {
                    *existing = candidate;
                }
            }
        }
    }

    size_t count = tbox_vector_length(&winners);
    if (count > 0) {
        tbox_arena *out_arena = malloc(sizeof(tbox_arena));
        *out_arena            = tbox_arena_create(0);
        result.items          = tbox_arena_alloc(out_arena, count * sizeof(tbox_css_resolved_declaration));
        for (size_t i = 0; i < count; i++) {
            result.items[i] = *(const tbox_css_resolved_declaration *)tbox_vector_at_const(&winners, i);
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
