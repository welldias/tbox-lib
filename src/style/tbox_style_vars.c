#include "style/tbox_style_internal.h"

#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"

/* One element's own custom properties (`--name: value`), values already
 * free of var(); inherited ones are reached through `parent`. */
typedef struct tbox_style_custom_property {
    tbox_string_view name, value;
} tbox_style_custom_property;

struct tbox_style_custom_properties {
    const tbox_style_custom_properties *parent;
    size_t count;
    tbox_style_custom_property items[];
};

#define TBOX_STYLE_VAR_MAX_DEPTH 16

static bool tbox_style_vars_is_custom(tbox_string_view property) {
    return property.size > 2 && property.data[0] == '-' && property.data[1] == '-';
}

static bool tbox_style_vars_mentions_var(tbox_string_view value) {
    for (size_t i = 0; i + 4 <= value.size; i++)
        if (tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data + i, 4), tbox_string_view_from_cstr("var(")))
            return true;
    return false;
}

bool tbox_style_vars_present(const tbox_css_computed_style *computed) {
    if (computed == NULL)
        return false;
    for (size_t i = 0; i < computed->count; i++)
        if (tbox_style_vars_is_custom(computed->items[i].property) || tbox_style_vars_mentions_var(computed->items[i].value))
            return true;
    return false;
}

static bool tbox_style_vars_substitute_text(tbox_arena *arena, tbox_string_view text, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out);

/* The value of custom property `name` (its own var() references
 * substituted): this element's declaration first, then the inherited
 * chain. False when undefined, or on a reference cycle (depth). */
static bool tbox_style_vars_lookup(tbox_arena *arena, tbox_string_view name, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out) {
    if (depth > TBOX_STYLE_VAR_MAX_DEPTH)
        return false;
    for (size_t i = 0; i < computed->count; i++)
        if (tbox_string_view_equal_ascii_ci(computed->items[i].property, name))
            return tbox_style_vars_substitute_text(arena, computed->items[i].value, computed, inherited, depth + 1, out);
    for (const tbox_style_custom_properties *set = inherited; set != NULL; set = set->parent)
        for (size_t i = 0; i < set->count; i++)
            if (tbox_string_view_equal_ascii_ci(set->items[i].name, name)) {
                *out = set->items[i].value;
                return true;
            }
    return false;
}

/* Replaces every `var(--name[, fallback])` in `text`. A reference with no
 * value and no fallback makes the whole value invalid (false). */
static bool tbox_style_vars_substitute_text(tbox_arena *arena, tbox_string_view text, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out) {
    if (!tbox_style_vars_mentions_var(text)) {
        *out = text;
        return true;
    }
    if (depth > TBOX_STYLE_VAR_MAX_DEPTH)
        return false;
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, text.size + 16);
    size_t i = 0;
    while (i < text.size) {
        if (i + 4 <= text.size && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data + i, 4), tbox_string_view_from_cstr("var("))) {
            /* Find the matching ')' and the first top-level ','. */
            size_t j = i + 4, comma = 0;
            int nesting = 1;
            while (j < text.size && nesting > 0) {
                if (text.data[j] == '(')
                    nesting++;
                else if (text.data[j] == ')')
                    nesting--;
                else if (text.data[j] == ',' && nesting == 1 && comma == 0)
                    comma = j;
                if (nesting > 0)
                    j++;
            }
            if (j >= text.size)
                return false; /* unterminated var( */
            size_t name_end        = comma != 0 ? comma : j;
            tbox_string_view name  = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + i + 4, name_end - i - 4));
            tbox_string_view value;
            if (!tbox_style_vars_is_custom(name))
                return false;
            if (!tbox_style_vars_lookup(arena, name, computed, inherited, depth, &value)) {
                if (comma == 0)
                    return false;
                if (!tbox_style_vars_substitute_text(arena, tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + comma + 1, j - comma - 1)), computed, inherited, depth + 1, &value))
                    return false;
            }
            tbox_string_builder_append_view(&builder, value);
            i = j + 1;
            continue;
        }
        tbox_string_builder_append_byte(&builder, text.data[i]);
        i++;
    }
    *out = tbox_string_builder_finish(&builder);
    return true;
}

tbox_css_computed_style tbox_style_vars_substitute(const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, tbox_arena *arena, const tbox_style_custom_properties **out_own, bool keep_own) {
    tbox_css_computed_style result = { NULL, 0, NULL };
    *out_own                       = inherited;
    if (computed == NULL || computed->count == 0)
        return result;
    result.items = (tbox_css_resolved_declaration *)tbox_arena_alloc(arena, computed->count * sizeof(tbox_css_resolved_declaration));
    if (result.items == NULL)
        return result;

    size_t own = 0;
    for (size_t i = 0; i < computed->count; i++)
        if (tbox_style_vars_is_custom(computed->items[i].property))
            own++;
    tbox_style_custom_properties *set = NULL;
    if (own > 0 && keep_own) {
        set = (tbox_style_custom_properties *)tbox_arena_alloc(arena, sizeof(tbox_style_custom_properties) + own * sizeof(tbox_style_custom_property));
        if (set != NULL) {
            set->parent = inherited;
            set->count  = 0;
        }
    }

    for (size_t i = 0; i < computed->count; i++) {
        const tbox_css_resolved_declaration *decl = &computed->items[i];
        tbox_string_view value;
        bool valid = tbox_style_vars_substitute_text(arena, decl->value, computed, inherited, 0, &value);
        if (tbox_style_vars_is_custom(decl->property)) {
            /* Custom properties keep their substituted text for children;
             * an invalid one is simply undefined. */
            if (valid && set != NULL) {
                tbox_string_builder copy;
                tbox_string_builder_init(&copy, arena, value.size + 1);
                tbox_string_builder_append_view(&copy, tbox_string_view_trim_ascii_space(value));
                tbox_string_builder name;
                tbox_string_builder_init(&name, arena, decl->property.size + 1);
                tbox_string_builder_append_view(&name, decl->property);
                set->items[set->count++] = (tbox_style_custom_property){ tbox_string_builder_finish(&name), tbox_string_builder_finish(&copy) };
            }
            continue;
        }
        result.items[result.count]       = *decl;
        /* Invalid at computed-value time: the property behaves as unset. */
        result.items[result.count].value = valid ? value : tbox_string_view_from_cstr("unset");
        result.count++;
    }
    if (set != NULL && set->count > 0)
        *out_own = set;
    return result;
}

/* ---- light-dark() ---- */

static const char tbox_style_light_dark_name[] = "light-dark(";

/* Offset of the next `light-dark(` in `text` from `from`, or text.size. */
static size_t tbox_style_light_dark_find(tbox_string_view text, size_t from) {
    size_t length = sizeof(tbox_style_light_dark_name) - 1;
    for (size_t i = from; i + length <= text.size; i++) {
        bool boundary = i == 0 || !((text.data[i - 1] >= 'a' && text.data[i - 1] <= 'z') || (text.data[i - 1] >= 'A' && text.data[i - 1] <= 'Z') || text.data[i - 1] == '-');
        if (boundary && tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data + i, length), tbox_string_view_from_cstr(tbox_style_light_dark_name)))
            return i;
    }
    return text.size;
}

bool tbox_style_light_dark_present(const tbox_css_computed_style *computed) {
    if (computed == NULL)
        return false;
    for (size_t i = 0; i < computed->count; i++)
        if (tbox_style_light_dark_find(computed->items[i].value, 0) < computed->items[i].value.size)
            return true;
    return false;
}

/* `text` with its light-dark() calls replaced, innermost arguments
 * included (the chosen argument is substituted again). */
static bool tbox_style_light_dark_text(tbox_arena *arena, tbox_string_view text, bool dark, int depth, tbox_string_view *out) {
    size_t at = tbox_style_light_dark_find(text, 0);
    if (at == text.size) {
        *out = text;
        return true;
    }
    if (depth > TBOX_STYLE_VAR_MAX_DEPTH)
        return false;
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, text.size);
    size_t done = 0;
    while (at < text.size) {
        size_t open = at + sizeof(tbox_style_light_dark_name) - 1, comma = 0, close = 0;
        int nesting = 0;
        for (size_t i = open; i < text.size && close == 0; i++) {
            char c = text.data[i];
            if (c == '(') nesting++;
            else if (c == ')' && nesting > 0) nesting--;
            else if (c == ')') close = i;
            else if (c == ',' && nesting == 0) {
                if (comma != 0) return false; /* more than two arguments */
                comma = i;
            }
        }
        if (close == 0 || comma == 0)
            return false;
        tbox_string_view chosen = dark ? tbox_string_view_make(text.data + comma + 1, close - comma - 1) : tbox_string_view_make(text.data + open, comma - open);
        chosen                  = tbox_string_view_trim_ascii_space(chosen);
        if (chosen.size == 0 || !tbox_style_light_dark_text(arena, chosen, dark, depth + 1, &chosen))
            return false;
        tbox_string_builder_append_view(&builder, tbox_string_view_make(text.data + done, at - done));
        tbox_string_builder_append_view(&builder, chosen);
        done = close + 1;
        at   = tbox_style_light_dark_find(text, done);
    }
    tbox_string_builder_append_view(&builder, tbox_string_view_make(text.data + done, text.size - done));
    *out = tbox_string_builder_finish(&builder);
    return true;
}

tbox_css_computed_style tbox_style_light_dark_substitute(const tbox_css_computed_style *computed, bool dark, tbox_arena *arena) {
    tbox_css_computed_style result = { NULL, 0, NULL };
    if (computed == NULL || computed->count == 0)
        return result;
    result.items = (tbox_css_resolved_declaration *)tbox_arena_alloc(arena, computed->count * sizeof(tbox_css_resolved_declaration));
    if (result.items == NULL)
        return result;
    for (size_t i = 0; i < computed->count; i++) {
        tbox_string_view value;
        result.items[i]       = computed->items[i];
        result.items[i].value = tbox_style_light_dark_text(arena, computed->items[i].value, dark, 0, &value) ? value : tbox_string_view_from_cstr("unset");
    }
    result.count = computed->count;
    return result;
}
