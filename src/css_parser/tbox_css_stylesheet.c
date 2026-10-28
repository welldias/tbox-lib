#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/css_parser.h>

#include "base/tbox_string.h"
#include "tbox_css_parser.h"
#include "tbox_css_stylesheet.h"

tbox_css_stylesheet *tbox_css_stylesheet_create_empty(void) {
    tbox_css_stylesheet *stylesheet = malloc(sizeof(tbox_css_stylesheet));
    if (stylesheet == NULL) {
        return NULL;
    }

    stylesheet->arena         = tbox_arena_create(0);
    if (stylesheet->arena.first == NULL) {
        free(stylesheet);
        return NULL;
    }
    stylesheet->rulesets      = NULL;
    stylesheet->ruleset_count = 0;
    stylesheet->pseudo_element_rulesets      = NULL;
    stylesheet->pseudo_element_ruleset_count = 0;
    stylesheet->pseudo_elements              = NULL;
    stylesheet->pseudo_element_count         = 0;
    stylesheet->element_index                = (tbox_css_rule_index){ 0 };
    stylesheet->font_palettes = NULL;
    stylesheet->font_palette_count = 0;
    stylesheet->font_faces = NULL;
    stylesheet->font_face_count = 0;
    stylesheet->keyframes       = NULL;
    stylesheet->keyframes_count = 0;
    stylesheet->properties = NULL;
    stylesheet->counter_styles = NULL;

    return stylesheet;
}

tbox_css_stylesheet *tbox_css_parse(const char *input, size_t length) {
    tbox_css_stylesheet *stylesheet = tbox_css_stylesheet_create_empty();
    if (stylesheet == NULL) {
        return NULL;
    }

    /* Strings such as `content` and `text-overflow` reach the screen:
     * malformed UTF-8 becomes U+FFFD here, as in tbox_html_parse. */
    tbox_string_view source = tbox_utf8_sanitize(&stylesheet->arena, tbox_string_view_make(input, length));
    tbox_css_parser parser;
    tbox_css_parser_init(&parser, source.data, source.size, &stylesheet->arena);
    tbox_css_parser_run(&parser, &stylesheet->rulesets, &stylesheet->ruleset_count, &stylesheet->font_faces, &stylesheet->font_face_count);
    stylesheet->font_palettes = parser.palettes_out;
    stylesheet->font_palette_count = parser.palettes_out_count;
    stylesheet->properties = parser.properties;
    stylesheet->counter_styles = parser.counter_styles;
    stylesheet->keyframes       = parser.keyframes_out;
    stylesheet->keyframes_count = parser.keyframes_out_count;
    if (!tbox_css_stylesheet_index_rules(stylesheet)) {
        tbox_css_stylesheet_destroy(stylesheet);
        return NULL;
    }

    return stylesheet;
}

/* A selector names a pseudo-element when one of its simple selectors is a
 * "::name" pseudo (the cascade's own test, see
 * tbox_css_cascade_targets_pseudo_element). */
static bool tbox_css_ruleset_has_pseudo_element(const tbox_css_ruleset *ruleset) {
    for (size_t s = 0; s < ruleset->selector_count; s++) {
        const tbox_css_selector *selector = &ruleset->selectors[s];
        for (size_t i = 0; i < selector->simple_selector_count; i++) {
            const tbox_css_simple_selector *item = &selector->simple_selectors[i];
            if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && item->name.size >= 3 && item->name.data[0] == ':' && item->name.data[1] == ':')
                return true;
        }
    }
    return false;
}

/* The pseudo-element `selector` names, as the cascade reads it: its first
 * "::" simple selector, without the colons. */
static bool tbox_css_selector_pseudo_element_name(const tbox_css_selector *selector, tbox_string_view *out) {
    for (size_t i = 0; i < selector->simple_selector_count; i++) {
        tbox_string_view name = selector->simple_selectors[i].name;
        if (selector->simple_selectors[i].kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && name.size >= 3 && name.data[0] == ':' && name.data[1] == ':') {
            *out = tbox_string_view_make(name.data + 2, name.size - 2);
            return true;
        }
    }
    return false;
}

static bool tbox_css_stylesheet_index_pseudo_elements(tbox_css_stylesheet *stylesheet) {
    size_t count = 0;
    for (size_t r = 0; r < stylesheet->ruleset_count; r++)
        if (tbox_css_ruleset_has_pseudo_element(&stylesheet->rulesets[r]))
            count++;
    stylesheet->pseudo_element_rulesets      = NULL;
    stylesheet->pseudo_element_ruleset_count = 0;
    stylesheet->pseudo_elements              = NULL;
    stylesheet->pseudo_element_count         = 0;
    if (count == 0)
        return true;
    stylesheet->pseudo_element_rulesets = tbox_arena_alloc(&stylesheet->arena, count * sizeof(size_t));
    if (stylesheet->pseudo_element_rulesets == NULL)
        return false;
    for (size_t r = 0; r < stylesheet->ruleset_count; r++)
        if (tbox_css_ruleset_has_pseudo_element(&stylesheet->rulesets[r]))
            stylesheet->pseudo_element_rulesets[stylesheet->pseudo_element_ruleset_count++] = r;
    /* The names, as the cascade reads them: the first "::" simple
     * selector of each selector. At most one per selector. */
    size_t selectors = 0;
    for (size_t p = 0; p < stylesheet->pseudo_element_ruleset_count; p++)
        selectors += stylesheet->rulesets[stylesheet->pseudo_element_rulesets[p]].selector_count;
    stylesheet->pseudo_elements = selectors > 0 ? tbox_arena_alloc_zero(&stylesheet->arena, selectors * sizeof(*stylesheet->pseudo_elements)) : NULL;
    if (selectors > 0 && stylesheet->pseudo_elements == NULL)
        return false;
    for (size_t p = 0; p < stylesheet->pseudo_element_ruleset_count; p++) {
        const tbox_css_ruleset *ruleset = &stylesheet->rulesets[stylesheet->pseudo_element_rulesets[p]];
        for (size_t s = 0; s < ruleset->selector_count; s++) {
            tbox_string_view name;
            if (!tbox_css_selector_pseudo_element_name(&ruleset->selectors[s], &name))
                continue;
            bool known = false;
            for (size_t n = 0; n < stylesheet->pseudo_element_count && !known; n++)
                known = tbox_string_view_equal(stylesheet->pseudo_elements[n].name, name);
            if (!known)
                stylesheet->pseudo_elements[stylesheet->pseudo_element_count++].name = name;
        }
    }
    /* Each name's rulesets: those with a selector naming it. */
    for (size_t n = 0; n < stylesheet->pseudo_element_count; n++) {
        tbox_css_pseudo_element_rules *rules = &stylesheet->pseudo_elements[n];
        rules->rulesets                      = tbox_arena_alloc(&stylesheet->arena, stylesheet->pseudo_element_ruleset_count * sizeof(size_t));
        if (rules->rulesets == NULL)
            return false;
        for (size_t p = 0; p < stylesheet->pseudo_element_ruleset_count; p++) {
            const tbox_css_ruleset *ruleset = &stylesheet->rulesets[stylesheet->pseudo_element_rulesets[p]];
            bool names                      = false;
            for (size_t s = 0; s < ruleset->selector_count && !names; s++) {
                tbox_string_view name;
                names = tbox_css_selector_pseudo_element_name(&ruleset->selectors[s], &name) && tbox_string_view_equal(name, rules->name);
            }
            if (names)
                rules->rulesets[rules->ruleset_count++] = stylesheet->pseudo_element_rulesets[p];
        }
    }
    return true;
}

const tbox_css_pseudo_element_rules *tbox_css_stylesheet_pseudo_element_rules(const tbox_css_stylesheet *stylesheet, tbox_string_view pseudo) {
    for (size_t n = 0; stylesheet != NULL && n < stylesheet->pseudo_element_count; n++) {
        tbox_string_view name = stylesheet->pseudo_elements[n].name;
        /* Size and first byte first: this runs for every pseudo-element
         * of every element. */
        if (name.size == pseudo.size && name.size > 0 && name.data[0] == pseudo.data[0] && memcmp(name.data, pseudo.data, name.size) == 0)
            return &stylesheet->pseudo_elements[n];
    }
    return NULL;
}

bool tbox_css_stylesheet_names_pseudo_element(const tbox_css_stylesheet *stylesheet, const char *pseudo) {
    return tbox_css_stylesheet_pseudo_element_rules(stylesheet, tbox_string_view_from_cstr(pseudo)) != NULL;
}

/* The rule index key of `selector`: its rightmost compound's id, else its
 * first class, else its tag; UNIVERSAL when it has none. Simple selectors
 * inside :is()/:not() and the like are not looked at: a compound needing
 * them still needs its own id, class or tag. */
static tbox_css_simple_selector_kind tbox_css_selector_index_key(const tbox_css_selector *selector, tbox_string_view *name) {
    size_t start = selector->simple_selector_count - 1;
    while (start > 0 && selector->simple_selectors[start].combinator_before == TBOX_CSS_COMBINATOR_NONE)
        start--;
    const tbox_css_simple_selector *id = NULL, *class_item = NULL, *type = NULL;
    for (size_t i = start; i < selector->simple_selector_count; i++) {
        const tbox_css_simple_selector *item = &selector->simple_selectors[i];
        if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_ID && id == NULL)
            id = item;
        else if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_CLASS && class_item == NULL)
            class_item = item;
        else if (item->kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE && type == NULL)
            type = item;
    }
    const tbox_css_simple_selector *key = id != NULL ? id : class_item != NULL ? class_item : type;
    *name = key != NULL ? key->name : tbox_string_view_make(NULL, 0);
    return key != NULL ? key->kind : TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL;
}

static size_t tbox_css_rule_index_hash(tbox_css_simple_selector_kind kind, tbox_string_view name) {
    uint64_t hash = 14695981039346656037ull ^ (uint64_t)kind;
    for (size_t i = 0; i < name.size; i++) {
        unsigned char c = (unsigned char)name.data[i];
        if (kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE)
            c = (unsigned char)tbox_ascii_to_lower((char)c);
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return (size_t)hash;
}

static bool tbox_css_rule_bucket_is(const tbox_css_rule_bucket *bucket, tbox_css_simple_selector_kind kind, tbox_string_view name) {
    return bucket->kind == kind && (kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE ? tbox_string_view_equal_ascii_ci(bucket->name, name) : tbox_string_view_equal(bucket->name, name));
}

static tbox_css_rule_bucket *tbox_css_rule_index_slot(const tbox_css_rule_index *index, tbox_css_simple_selector_kind kind, tbox_string_view name) {
    for (size_t slot = tbox_css_rule_index_hash(kind, name) & index->mask;; slot = (slot + 1) & index->mask) {
        tbox_css_rule_bucket *bucket = &index->buckets[slot];
        if (!bucket->used || tbox_css_rule_bucket_is(bucket, kind, name))
            return bucket;
    }
}

const tbox_css_rule_bucket *tbox_css_rule_index_find(const tbox_css_rule_index *index, tbox_css_simple_selector_kind kind, tbox_string_view name) {
    if (index->buckets == NULL)
        return NULL;
    const tbox_css_rule_bucket *bucket = tbox_css_rule_index_slot(index, kind, name);
    return bucket->used ? bucket : NULL;
}

/* Builds `index` over the rulesets `list` names (all of them when NULL),
 * in two passes: count each bucket, then fill it. */
static bool tbox_css_rule_index_build(tbox_css_stylesheet *stylesheet, const size_t *list, size_t count, tbox_css_rule_index *index) {
    *index           = (tbox_css_rule_index){ 0 };
    size_t selectors = 0;
    for (size_t k = 0; k < count; k++)
        selectors += stylesheet->rulesets[list != NULL ? list[k] : k].selector_count;
    size_t capacity = 16;
    while (capacity < selectors * 2 && capacity <= SIZE_MAX / 4 / sizeof(tbox_css_rule_bucket))
        capacity *= 2;
    index->buckets = tbox_arena_alloc_zero(&stylesheet->arena, capacity * sizeof(tbox_css_rule_bucket));
    if (index->buckets == NULL)
        return false;
    index->mask = capacity - 1;
    size_t universal_last = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t k = 0; k < count; k++) {
            size_t r                         = list != NULL ? list[k] : k;
            const tbox_css_ruleset *ruleset  = &stylesheet->rulesets[r];
            for (size_t s = 0; s < ruleset->selector_count; s++) {
                tbox_string_view name;
                tbox_css_simple_selector_kind kind = tbox_css_selector_index_key(&ruleset->selectors[s], &name);
                size_t *target = NULL, *target_count, *last;
                if (kind == TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL) {
                    target       = index->universal;
                    target_count = &index->universal_count;
                    last         = &universal_last;
                } else {
                    tbox_css_rule_bucket *bucket = tbox_css_rule_index_slot(index, kind, name);
                    if (!bucket->used)
                        *bucket = (tbox_css_rule_bucket){ true, kind, name, NULL, 0, 0 };
                    target       = bucket->rulesets;
                    target_count = &bucket->count;
                    last         = &bucket->last;
                }
                if (*last == r + 1)
                    continue; /* two selectors of one ruleset in the same bucket */
                *last = r + 1;
                if (pass == 1)
                    target[*target_count] = r;
                (*target_count)++;
            }
        }
        if (pass == 1)
            break;
        /* Storage for the counts, which the fill pass recounts. */
        index->universal = index->universal_count > 0 ? tbox_arena_alloc(&stylesheet->arena, index->universal_count * sizeof(size_t)) : NULL;
        if (index->universal_count > 0 && index->universal == NULL)
            return false;
        index->universal_count = 0;
        universal_last         = 0;
        for (size_t slot = 0; slot < capacity; slot++) {
            tbox_css_rule_bucket *bucket = &index->buckets[slot];
            if (!bucket->used)
                continue;
            bucket->rulesets = tbox_arena_alloc(&stylesheet->arena, bucket->count * sizeof(size_t));
            if (bucket->rulesets == NULL)
                return false;
            bucket->count = 0;
            bucket->last  = 0;
        }
    }
    return true;
}

bool tbox_css_stylesheet_index_rules(tbox_css_stylesheet *stylesheet) {
    if (!tbox_css_stylesheet_index_pseudo_elements(stylesheet))
        return false;
    /* Without an index the cascade looks at every ruleset of the list. */
    if (!tbox_css_rule_index_build(stylesheet, NULL, stylesheet->ruleset_count, &stylesheet->element_index))
        stylesheet->element_index = (tbox_css_rule_index){ 0 };
    for (size_t n = 0; n < stylesheet->pseudo_element_count; n++) {
        tbox_css_pseudo_element_rules *rules = &stylesheet->pseudo_elements[n];
        if (!tbox_css_rule_index_build(stylesheet, rules->rulesets, rules->ruleset_count, &rules->index))
            rules->index = (tbox_css_rule_index){ 0 };
    }
    return true;
}

size_t tbox_css_stylesheet_ruleset_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->ruleset_count : 0;
}

const tbox_css_ruleset *tbox_css_stylesheet_rulesets(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->rulesets : NULL;
}

size_t tbox_css_stylesheet_font_face_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_face_count : 0;
}

const tbox_css_font_face_rule *tbox_css_stylesheet_font_faces(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_faces : NULL;
}

size_t tbox_css_stylesheet_keyframes_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->keyframes_count : 0;
}

const tbox_css_keyframes_rule *tbox_css_stylesheet_keyframes(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->keyframes : NULL;
}

void tbox_css_stylesheet_destroy(tbox_css_stylesheet *stylesheet) {
    if (stylesheet == NULL) {
        return;
    }
    tbox_arena_destroy(&stylesheet->arena);
    free(stylesheet);
}

size_t tbox_css_stylesheet_font_palette_count(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_palette_count : 0;
}

const tbox_css_font_palette_rule *tbox_css_stylesheet_font_palettes(const tbox_css_stylesheet *stylesheet) {
    return stylesheet != NULL ? stylesheet->font_palettes : NULL;
}
