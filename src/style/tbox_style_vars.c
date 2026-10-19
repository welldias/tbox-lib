#include "style/tbox_style_internal.h"

#include <string.h>
#include <math.h>
#include "base/tbox_number.h"
#include "css_parser/tbox_css_stylesheet.h"
#include "css_parser/tbox_css_tokenizer.h"

#include "base/tbox_arena.h"
#include "base/tbox_string.h"

static void tbox_style_registered_number(tbox_string_builder *b, double n, const char *unit) {
    char text[64];
    int count = tbox_number_format(text, sizeof(text), n, 15);
    if (count > 0 && (size_t)count < sizeof(text)) tbox_string_builder_append_view(b, tbox_string_view_make(text, (size_t)count));
    tbox_string_builder_append_view(b, tbox_string_view_from_cstr(unit));
}

bool tbox_style_registered_value(tbox_arena *arena, tbox_string_view syntax, tbox_string_view value,
    const tbox_style_units *units, tbox_string_view *out) {
    value = tbox_string_view_trim_ascii_space(value);
    if (tbox_string_view_equal_cstr(syntax, "*")) { *out = value; return true; }
    /* Relative initial values are outside this registration subset. */
    tbox_style_units initial = { 16, 16, 800, 600, false, -1.0, -1.0, 1.0, TBOX_STYLE_FRAME_NONE, 0.0, 0.0 };
    if (units == NULL) {
        static const char *const dependent[] = { "em", "rem", "ex", "ch", "vw", "vh", "vmin", "vmax", "svw", "svh", "svmin", "svmax", "lvw", "lvh", "lvmin", "lvmax", "dvw", "dvh", "dvmin", "dvmax", "currentcolor" };
        tbox_css_tokenizer tokenizer;
        tbox_css_token token;
        tbox_css_tokenizer_init(&tokenizer, value.data, value.size);
        do {
            tbox_css_tokenizer_next(&tokenizer, &token);
            if (token.type == TBOX_CSS_TOKEN_FUNCTION && tbox_style_is(token.text, "var")) return false;
            if (token.type == TBOX_CSS_TOKEN_IDENT)
                for (size_t i = 0; i < sizeof(dependent) / sizeof(dependent[0]); i++)
                    if (tbox_style_is(token.text, dependent[i])) return false;
        } while (token.type != TBOX_CSS_TOKEN_EOF);
        units = &initial;
    }
    tbox_string_builder b;
    tbox_string_builder_init(&b, arena, value.size + 32);
    double number;
    if (tbox_string_view_equal_cstr(syntax, "<color>")) {
        tbox_css_rgba color;
        if (!tbox_css_color_parse(value, &color)) return false;
        tbox_string_builder_append_view(&b, tbox_string_view_from_cstr("rgba("));
        tbox_style_registered_number(&b, color.r, ",");
        tbox_style_registered_number(&b, color.g, ",");
        tbox_style_registered_number(&b, color.b, ",");
        tbox_style_registered_number(&b, color.a / 255.0, ")");
    } else if (tbox_string_view_equal_cstr(syntax, "<number>") || tbox_string_view_equal_cstr(syntax, "<integer>")) {
        if (!tbox_number_parse(value, &number) &&
            (memchr(value.data, '%', value.size) != NULL || !tbox_style_parse_calc_number(value, units, &number))) return false;
        if (tbox_string_view_equal_cstr(syntax, "<integer>") && trunc(number) != number) return false;
        tbox_style_registered_number(&b, number, "");
    } else if (tbox_string_view_equal_cstr(syntax, "<angle>")) {
        if (!tbox_style_parse_angle(value, units, &number)) return false;
        tbox_style_registered_number(&b, number, "deg");
    } else if (tbox_string_view_equal_cstr(syntax, "<length>") || tbox_string_view_equal_cstr(syntax, "<percentage>") || tbox_string_view_equal_cstr(syntax, "<length-percentage>")) {
        tbox_style_length length;
        if (!tbox_style_parse_length(value, units, &length) || length.kind == TBOX_STYLE_LENGTH_AUTO || length.bounds != 0 || !isfinite(length.value) || !isfinite(length.px_offset)) return false;
        if (tbox_string_view_equal_cstr(syntax, "<length>") && length.kind != TBOX_STYLE_LENGTH_PX) return false;
        if (tbox_string_view_equal_cstr(syntax, "<percentage>") && (length.kind != TBOX_STYLE_LENGTH_PERCENT || length.px_offset != 0)) return false;
        if (length.kind == TBOX_STYLE_LENGTH_PX) tbox_style_registered_number(&b, length.value, "px");
        else if (length.px_offset == 0) tbox_style_registered_number(&b, length.value, "%");
        else {
            tbox_string_builder_append_view(&b, tbox_string_view_from_cstr("calc("));
            tbox_style_registered_number(&b, length.value, "% + ");
            tbox_style_registered_number(&b, length.px_offset, "px)");
        }
    } else return false;
    *out = tbox_string_builder_finish(&b);
    return true;
}

/* One element's own custom properties (`--name: value`), values already
 * free of var(); inherited ones are reached through `parent`. */
typedef struct tbox_style_custom_property {
    tbox_string_view name, value;
    bool valid;
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

static bool tbox_style_vars_substitute_text(tbox_arena *arena, const tbox_style_units *units, tbox_string_view text, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out);

/* The value of custom property `name` (its own var() references
 * substituted): this element's declaration first, then the inherited
 * chain. False when undefined, or on a reference cycle (depth). */
static bool tbox_style_vars_lookup(tbox_arena *arena, const tbox_style_units *units, tbox_string_view name, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out) {
    if (depth > TBOX_STYLE_VAR_MAX_DEPTH)
        return false;
    for (size_t i = 0; i < computed->count; i++) {
        const tbox_css_resolved_declaration *decl = &computed->items[i];
        if (!tbox_string_view_equal(decl->property, name)) continue;
        const tbox_css_property_rule *rule = decl->registration;
        if (rule == NULL)
            return tbox_style_vars_substitute_text(arena, units, decl->value, computed, inherited, depth + 1, out);
        bool inherit = tbox_style_is(decl->value, "inherit") ||
            (rule->inherits && (decl->registered_default || tbox_style_is(decl->value, "unset")));
        if (inherit) {
            for (const tbox_style_custom_properties *set = inherited; set != NULL; set = set->parent)
                for (size_t j = 0; j < set->count; j++)
                    if (tbox_string_view_equal(set->items[j].name, name)) { *out = set->items[j].value; return set->items[j].valid; }
        }
        tbox_string_view value;
        if (!decl->registered_default && !inherit && !tbox_style_is(decl->value, "initial") && !tbox_style_is(decl->value, "unset") &&
            tbox_style_vars_substitute_text(arena, units, decl->value, computed, inherited, depth + 1, &value) &&
            tbox_style_registered_value(arena, rule->syntax, value, units, out)) return true;
        *out = rule->initial;
        return out->size > 0;
    }
    for (const tbox_style_custom_properties *set = inherited; set != NULL; set = set->parent)
        for (size_t i = 0; i < set->count; i++)
            if (tbox_string_view_equal(set->items[i].name, name)) {
                *out = set->items[i].value;
                return set->items[i].valid;
            }
    return false;
}

bool tbox_style_custom_property_get(const tbox_style_custom_properties *set, tbox_string_view name, tbox_string_view *out) {
    for (; set != NULL; set = set->parent)
        for (size_t i = 0; i < set->count; i++)
            if (tbox_string_view_equal_ascii_ci(set->items[i].name, name)) {
                *out = set->items[i].value;
                return true;
            }
    return false;
}

/* Replaces every `var(--name[, fallback])` in `text`. A reference with no
 * value and no fallback makes the whole value invalid (false). */
static bool tbox_style_vars_substitute_text(tbox_arena *arena, const tbox_style_units *units, tbox_string_view text, const tbox_css_computed_style *computed, const tbox_style_custom_properties *inherited, int depth, tbox_string_view *out) {
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
            size_t name_end       = comma != 0 ? comma : j;
            tbox_string_view name = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + i + 4, name_end - i - 4));
            tbox_string_view value;
            if (!tbox_style_vars_is_custom(name))
                return false;
            if (!tbox_style_vars_lookup(arena, units, name, computed, inherited, depth, &value)) {
                if (comma == 0)
                    return false;
                if (!tbox_style_vars_substitute_text(arena, units, tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + comma + 1, j - comma - 1)), computed, inherited, depth + 1, &value))
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

tbox_css_computed_style tbox_style_vars_substitute(const tbox_css_computed_style *computed, const tbox_style_units *units, const tbox_style_custom_properties *inherited, tbox_arena *arena, const tbox_style_custom_properties **out_own, bool keep_own) {
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
        bool custom = tbox_style_vars_is_custom(decl->property);
        bool valid = custom ? tbox_style_vars_lookup(arena, units, decl->property, computed, inherited, 0, &value) :
            tbox_style_vars_substitute_text(arena, units, decl->value, computed, inherited, 0, &value);
        if (tbox_style_vars_is_custom(decl->property)) {
            /* Custom properties keep their substituted text for children;
             * an invalid one is simply undefined. */
            if ((valid || decl->registration != NULL) && set != NULL) {
                if (!valid) value = tbox_string_view_make(NULL, 0);
                tbox_string_builder copy;
                tbox_string_builder_init(&copy, arena, value.size + 1);
                tbox_string_builder_append_view(&copy, tbox_string_view_trim_ascii_space(value));
                tbox_string_builder name;
                tbox_string_builder_init(&name, arena, decl->property.size + 1);
                tbox_string_builder_append_view(&name, decl->property);
                set->items[set->count++] = (tbox_style_custom_property){ tbox_string_builder_finish(&name), tbox_string_builder_finish(&copy), valid };
            }
            continue;
        }
        result.items[result.count] = *decl;
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
            if (c == '(')
                nesting++;
            else if (c == ')' && nesting > 0)
                nesting--;
            else if (c == ')')
                close = i;
            else if (c == ',' && nesting == 0) {
                if (comma != 0)
                    return false; /* more than two arguments */
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
