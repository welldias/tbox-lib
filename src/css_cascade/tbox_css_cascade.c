#include <tbox/css_cascade.h>

#include <tbox/css_selector.h>

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "base/tbox_arena.h"
#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "style/tbox_style_internal.h"
#include "css_parser/tbox_css_stylesheet.h"
#include "css_selector/tbox_css_selector_match.h"

static bool tbox_css_media_equals(tbox_string_view view, const char *text) {
    return tbox_string_view_equal_ascii_ci(view, tbox_string_view_from_cstr(text));
}

/* A non-negative number with an optional unit suffix into `out`/`unit`. */
static bool tbox_css_media_number(tbox_string_view value, double *out, char unit[8]) {
    size_t used = tbox_number_scan(value, out);
    if (used == 0 || !isfinite(*out) || *out < 0.0 || value.size - used >= 8) return false;
    memcpy(unit, value.data + used, value.size - used);
    unit[value.size - used] = '\0';
    return true;
}

/* `16/9`, `16 / 9` or a single number, as width / height. */
static bool tbox_css_media_ratio(tbox_string_view value, double *out) {
    size_t slash = 0;
    while (slash < value.size && value.data[slash] != '/') slash++;
    double a, b = 1.0;
    char unit[8];
    if (!tbox_css_media_number(tbox_string_view_trim_ascii_space(tbox_string_view_make(value.data, slash)), &a, unit) || unit[0] != '\0') return false;
    if (slash < value.size &&
        (!tbox_css_media_number(tbox_string_view_trim_ascii_space(tbox_string_view_make(value.data + slash + 1, value.size - slash - 1)), &b, unit) || unit[0] != '\0' || b == 0.0))
        return false;
    *out = a / b;
    return true;
}

/* One `(feature)` or `(feature: value)`. The environment is a desktop
 * window: light color scheme, a mouse that hovers, 1dppx, 8 bits per
 * color channel, no scripting, no forced colors, no contrast preference. */
static bool tbox_css_media_feature(tbox_string_view clause, double width, double height, bool reduced_motion) {
    clause = tbox_string_view_trim_ascii_space(clause);
    if (clause.size < 3 || clause.data[0] != '(' || clause.data[clause.size - 1] != ')') return false;
    clause.data++, clause.size -= 2;
    clause = tbox_string_view_trim_ascii_space(clause);
    size_t colon = 0;
    while (colon < clause.size && clause.data[colon] != ':') colon++;
    tbox_string_view name = tbox_string_view_trim_ascii_space(tbox_string_view_make(clause.data, colon));
    if (colon == clause.size) {
        /* Boolean context: true unless the feature's value would be none/0. */
        if (tbox_css_media_equals(name, "prefers-reduced-motion")) return reduced_motion;
        return tbox_css_media_equals(name, "width") || tbox_css_media_equals(name, "height") ||
               tbox_css_media_equals(name, "color") || tbox_css_media_equals(name, "hover") ||
               tbox_css_media_equals(name, "any-hover") || tbox_css_media_equals(name, "pointer") ||
               tbox_css_media_equals(name, "any-pointer") || tbox_css_media_equals(name, "orientation") ||
               tbox_css_media_equals(name, "aspect-ratio") || tbox_css_media_equals(name, "resolution") ||
               tbox_css_media_equals(name, "prefers-color-scheme");
    }
    tbox_string_view value = tbox_string_view_trim_ascii_space(tbox_string_view_make(clause.data + colon + 1, clause.size - colon - 1));
    static const struct {
        const char *name, *value;
    } discrete[] = {
        { "prefers-color-scheme", "light" },
        { "prefers-contrast", "no-preference" },
        { "forced-colors", "none" },
        { "hover", "hover" },
        { "any-hover", "hover" },
        { "pointer", "fine" },
        { "any-pointer", "fine" },
        { "scripting", "none" },
        { "update", "fast" },
        { "inverted-colors", "none" },
    };
    for (size_t i = 0; i < sizeof(discrete) / sizeof(discrete[0]); i++)
        if (tbox_css_media_equals(name, discrete[i].name)) return tbox_css_media_equals(value, discrete[i].value);
    if (tbox_css_media_equals(name, "prefers-reduced-motion")) {
        if (tbox_css_media_equals(value, "reduce")) return reduced_motion;
        if (tbox_css_media_equals(value, "no-preference")) return !reduced_motion;
        return false;
    }
    if (tbox_css_media_equals(name, "orientation")) {
        if (tbox_css_media_equals(value, "landscape")) return width > height;
        if (tbox_css_media_equals(value, "portrait")) return height >= width;
        return false;
    }
    bool min = false, max = false;
    if (name.size > 4 && tbox_css_media_equals(tbox_string_view_make(name.data, 4), "min-")) {
        name.data += 4, name.size -= 4, min = true;
    } else if (name.size > 4 && tbox_css_media_equals(tbox_string_view_make(name.data, 4), "max-")) {
        name.data += 4, name.size -= 4, max = true;
    }
    double actual, target;
    char unit[8];
    if (tbox_css_media_equals(name, "aspect-ratio")) {
        if (!tbox_css_media_ratio(value, &target)) return false;
        actual = width / height;
    } else if (tbox_css_media_equals(name, "resolution")) {
        if (!tbox_css_media_number(value, &target, unit)) return false;
        if (strcmp(unit, "dpi") == 0) target /= 96.0;
        else if (strcmp(unit, "dpcm") == 0) target *= 2.54 / 96.0;
        else if (strcmp(unit, "dppx") != 0 && strcmp(unit, "x") != 0) return false;
        actual = 1.0;
    } else if (tbox_css_media_equals(name, "color") || tbox_css_media_equals(name, "monochrome")) {
        if (!tbox_css_media_number(value, &target, unit) || unit[0] != '\0') return false;
        actual = tbox_css_media_equals(name, "color") ? 8.0 : 0.0;
    } else if (tbox_css_media_equals(name, "width") || tbox_css_media_equals(name, "height")) {
        actual = tbox_css_media_equals(name, "width") ? width : height;
        if (!tbox_css_media_number(value, &target, unit)) return false;
        if (strcmp(unit, "px") == 0 || unit[0] == '\0') { /* CSS px */ }
        else if (strcmp(unit, "em") == 0 || strcmp(unit, "rem") == 0) target *= 16.0;
        else return false;
    } else {
        return false;
    }
    /* Ratios and resolutions are compared with a little tolerance. */
    double epsilon = 1e-6;
    return min ? actual >= target - epsilon : max ? actual <= target + epsilon : fabs(actual - target) <= epsilon;
}

static bool tbox_css_media_one(tbox_string_view query, double width, double height, bool reduced_motion) {
    query = tbox_string_view_trim_ascii_space(query);
    bool negate = false;
    if (query.size > 4 && tbox_css_media_equals(tbox_string_view_make(query.data, 4), "not ")) {
        query.data += 4, query.size -= 4, negate = true;
    } else if (query.size > 5 && tbox_css_media_equals(tbox_string_view_make(query.data, 5), "only ")) {
        query.data += 5, query.size -= 5;
    }
    bool result = true, saw = false;
    while (query.size) {
        query = tbox_string_view_trim_ascii_space(query);
        if (!query.size) break;
        size_t end = 0;
        if (query.data[0] == '(') {
            int depth = 0;
            do {
                if (query.data[end] == '(') depth++;
                else if (query.data[end] == ')') depth--;
                end++;
            } while (end < query.size && depth > 0);
            if (depth != 0) return false;
            result &= tbox_css_media_feature(tbox_string_view_make(query.data, end), width, height, reduced_motion);
        } else {
            while (end < query.size && !tbox_ascii_is_space(query.data[end])) end++;
            tbox_string_view token = tbox_string_view_make(query.data, end);
            if (tbox_css_media_equals(token, "and")) { query.data += end, query.size -= end; continue; }
            result &= tbox_css_media_equals(token, "all") || tbox_css_media_equals(token, "screen");
        }
        saw = true;
        query.data += end, query.size -= end;
    }
    return saw && (negate ? !result : result);
}

static bool tbox_css_media_matches(const tbox_css_media_condition *media, double width, double height, bool reduced_motion, const tbox_css_query_context *query) {
    for (; media != NULL; media = media->parent) {
        if (media->container) {
            if (!tbox_css_cascade_container_matches(media->query, query, width, height)) return false;
            continue;
        }
        if (width <= 0.0 || height <= 0.0) return false;
        bool matched = false;
        tbox_string_view list = media->query;
        while (list.size) {
            size_t end = 0;
            int depth = 0;
            while (end < list.size) {
                if (list.data[end] == '(') depth++;
                else if (list.data[end] == ')') depth--;
                else if (list.data[end] == ',' && depth == 0) break;
                end++;
            }
            matched |= tbox_css_media_one(tbox_string_view_make(list.data, end), width, height, reduced_motion);
            list.data += end + (end < list.size);
            list.size -= end + (end < list.size);
        }
        if (!matched) return false;
    }
    return true;
}

bool tbox_css_cascade_media_matches(const tbox_css_media_condition *media, double viewport_width, double viewport_height, bool reduced_motion) {
    return tbox_css_media_matches(media, viewport_width, viewport_height, reduced_motion, NULL);
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

/* Pseudo-elements are kept with their "::" (the parser also turns CSS2's
 * :before/:after/:first-line/:first-letter into that form); they weigh
 * like a type selector. */
static bool tbox_css_cascade_is_known_pseudo_element(tbox_string_view name) {
    if (name.size > 2 && name.data[0] == ':' && name.data[1] == ':')
        return true;
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
            if (tbox_string_view_equal_cstr(item->name, "not") && item->negated_selector != NULL) {
                const tbox_css_simple_selector *nested = item->negated_selector;
                if (nested->kind == TBOX_CSS_SIMPLE_SELECTOR_ID) spec.a++;
                else if (nested->kind == TBOX_CSS_SIMPLE_SELECTOR_CLASS || nested->kind == TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE) spec.b++;
                else if (nested->kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE) spec.c++;
            } else if (tbox_css_cascade_is_known_pseudo_element(item->name)) {
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
    while (bang > 0 && tbox_ascii_is_space(value.data[bang - 1])) {
        bang--;
    }
    if (bang == 0 || value.data[bang - 1] != '!') {
        return value;
    }

    size_t result_size = bang - 1;
    while (result_size > 0 && tbox_ascii_is_space(value.data[result_size - 1])) {
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
    /* Cascade layers: for normal declarations a rule outside every layer
     * (0) beats any layer and a later layer beats an earlier one; for
     * !important the order reverses. */
    unsigned layer_a = a->ruleset != NULL ? a->ruleset->layer : 0, layer_b = b->ruleset != NULL ? b->ruleset->layer : 0;
    if (layer_a != layer_b) {
        int later = layer_a == 0 ? 1 : layer_b == 0 ? -1 : layer_a > layer_b ? 1 : -1;
        return a->important ? -later : later;
    }
    int specificity = tbox_css_cascade_specificity_compare(a->specificity, b->specificity);
    if (specificity != 0) return specificity;
    if (a->scope_hops != b->scope_hops) {
        if (a->scope_hops == 0) return -1;
        if (b->scope_hops == 0) return 1;
        return a->scope_hops < b->scope_hops ? 1 : -1;
    }
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
    if (!tbox_style_opentype_declaration_valid(candidate->property, candidate->value) ||
        !tbox_style_paint_declaration_valid(candidate->property, candidate->value) ||
        !tbox_style_motion_declaration_valid(candidate->property, candidate->value)) return;
    if (!tbox_style_composition_declaration_valid(candidate->property, candidate->value)) return;
    if (!tbox_style_inline_declaration_valid(candidate->property, candidate->value)) return;
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

/* Whether `selector` targets the pseudo-element `pseudo` ("marker") in
 * `state`: its pseudo-element is `pseudo`, followed by nothing or only by
 * :hover/:active that `state` has. *element_count receives the length of
 * the element part before the pseudo-element. */
static bool tbox_css_cascade_targets_pseudo_element(const tbox_css_selector *selector, const char *pseudo, unsigned state, size_t *element_count) {
    size_t count = selector->simple_selector_count, at = count;
    for (size_t i = 0; i < count && at == count; i++) {
        const tbox_css_simple_selector *item = &selector->simple_selectors[i];
        if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && item->name.size >= 3 && item->name.data[0] == ':' && item->name.data[1] == ':')
            at = i;
    }
    if (at == count)
        return false;
    tbox_string_view name = selector->simple_selectors[at].name;
    if (!tbox_string_view_equal_cstr(tbox_string_view_make(name.data + 2, name.size - 2), pseudo))
        return false;
    for (size_t i = at + 1; i < count; i++) {
        const tbox_css_simple_selector *item = &selector->simple_selectors[i];
        if (item->kind != TBOX_CSS_SIMPLE_SELECTOR_PSEUDO || item->combinator_before != TBOX_CSS_COMBINATOR_NONE)
            return false;
        if (tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_from_cstr("hover"))) {
            if ((state & TBOX_CSS_PART_STATE_HOVER) == 0) return false;
        } else if (tbox_string_view_equal_ascii_ci(item->name, tbox_string_view_from_cstr("active"))) {
            if ((state & TBOX_CSS_PART_STATE_ACTIVE) == 0) return false;
        } else {
            return false;
        }
    }
    *element_count = at;
    return true;
}

static tbox_css_computed_style tbox_css_cascade_resolve_impl(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, const char *pseudo, unsigned state, double viewport_width, double viewport_height, bool reduced_motion, bool starting, const tbox_css_query_context *query);

tbox_css_computed_style tbox_css_cascade_resolve(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node) {
    return tbox_css_cascade_resolve_impl(sources, source_count, node, NULL, 0, 0.0, 0.0, false, false, NULL);
}

tbox_css_computed_style tbox_css_cascade_resolve_pseudo_element(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, const char *pseudo) {
    return tbox_css_cascade_resolve_impl(sources, source_count, node, pseudo, 0, 0.0, 0.0, false, false, NULL);
}

tbox_css_computed_style tbox_css_cascade_resolve_in_viewport(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, double viewport_width, double viewport_height, bool reduced_motion, const tbox_css_query_context *query) {
    return tbox_css_cascade_resolve_impl(sources, source_count, node, NULL, 0, viewport_width, viewport_height, reduced_motion, false, query);
}

tbox_css_computed_style tbox_css_cascade_resolve_pseudo_element_in_viewport(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, const char *pseudo, unsigned state, double viewport_width, double viewport_height, bool reduced_motion, const tbox_css_query_context *query) {
    return tbox_css_cascade_resolve_impl(sources, source_count, node, pseudo, state, viewport_width, viewport_height, reduced_motion, false, query);
}

tbox_css_computed_style tbox_css_cascade_resolve_starting_style_in_viewport(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, double viewport_width, double viewport_height, bool reduced_motion, const tbox_css_query_context *query) {
    return tbox_css_cascade_resolve_impl(sources, source_count, node, NULL, 0, viewport_width, viewport_height, reduced_motion, true, query);
}

static tbox_css_computed_style tbox_css_cascade_resolve_impl(const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, const char *pseudo, unsigned state, double viewport_width, double viewport_height, bool reduced_motion, bool starting, const tbox_css_query_context *query) {
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
            if (ruleset->media != NULL && !tbox_css_media_matches(ruleset->media, viewport_width, viewport_height, reduced_motion, query)) continue;
            if (ruleset->starting_style && !starting) continue;

            const tbox_css_selector *best_selector = NULL;
            tbox_css_specificity best_specificity  = { 0, 0, 0 };
            size_t best_hops = 0;
            for (size_t s = 0; s < ruleset->selector_count; s++) {
                const tbox_css_selector *selector = &ruleset->selectors[s];
                size_t hops = 0;
                if (pseudo == NULL) {
                    if (!tbox_css_selector_scope_matches(ruleset->scope, node, selector, &hops))
                        continue;
                } else {
                    /* The selector before its pseudo-element must match
                     * the element (a bare "::marker" matches any). */
                    size_t element_count;
                    if (!tbox_css_cascade_targets_pseudo_element(selector, pseudo, state, &element_count))
                        continue;
                    tbox_css_selector element = { selector->simple_selectors, element_count };
                    if (!tbox_css_selector_scope_matches(ruleset->scope, node, &element, &hops))
                        continue;
                }
                tbox_css_specificity specificity = tbox_css_cascade_specificity(selector);
                if (best_selector == NULL || tbox_css_cascade_specificity_compare(specificity, best_specificity) > 0 ||
                    (tbox_css_cascade_specificity_compare(specificity, best_specificity) == 0 && hops > 0 && (best_hops == 0 || hops < best_hops))) {
                    best_selector    = selector;
                    best_specificity = specificity;
                    best_hops = hops;
                }
            }

            if (best_selector == NULL) {
                continue;
            }

            for (size_t d = 0; d < ruleset->declaration_count; d++) {
                const tbox_css_declaration *declaration = &ruleset->declarations[d];

                tbox_css_resolved_declaration candidate = {0};
                candidate.ruleset     = ruleset;
                candidate.selector    = best_selector;
                candidate.origin      = origin;
                candidate.property    = declaration->property;
                candidate.value       = tbox_css_cascade_strip_important(declaration->value, &candidate.important);
                candidate.specificity = best_specificity;
                candidate.scope_hops = best_hops;
                candidate.source_order = source_order++;

                tbox_css_cascade_offer(&winners, &candidate);
            }
        }
    }

    /* v9: style="" inline -- resolved here, inside tbox_css_cascade_resolve
     * itself, so every caller (Style layer, tbox_css_cascade_resolve_stylesheet,
     * tests) gets it automatically without knowing it exists. Runs after the
     * sources loop above (unchanged) so it can reuse `winners` as-is. */
    if (pseudo == NULL) {
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

                        tbox_css_resolved_declaration candidate = {0};
                        /* ruleset/selector stay NULL (from the `{0}`): both
                         * would point into `inline_sheet`, destroyed below. */
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

    /* Registered defaults must exist even on an element with no matching
     * rules, so inherits:false can stop the inherited variable chain. */
    for (size_t src = 0; src < source_count; src++) {
        const tbox_css_stylesheet *sheet = sources[src].stylesheet;
        if (sheet == NULL) continue;
        for (const tbox_css_property_rule *rule = sheet->properties; rule != NULL; rule = rule->next) {
            if (!tbox_css_media_matches(rule->media, viewport_width, viewport_height, reduced_motion, NULL)) continue;
            bool later = false;
            for (const tbox_css_property_rule *r = sheet->properties; r != rule; r = r->next)
                if (tbox_string_view_equal(r->name, rule->name) && tbox_css_media_matches(r->media, viewport_width, viewport_height, reduced_motion, NULL)) later = true;
            if (later) continue;
            tbox_css_resolved_declaration *decl = NULL;
            for (size_t i = 0; i < winners.length; i++) {
                tbox_css_resolved_declaration *d = tbox_vector_at(&winners, i);
                if (tbox_string_view_equal(d->property, rule->name)) { decl = d; break; }
            }
            if (decl == NULL) {
                decl = tbox_vector_push(&winners);
                *decl = (tbox_css_resolved_declaration){ .property = rule->name, .registered_default = true };
            }
            decl->registration = rule;
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
