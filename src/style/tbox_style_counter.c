#include "tbox_style_internal.h"

#include <limits.h>
#include <string.h>

#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "css_parser/tbox_css_stylesheet.h"
#include "css_parser/tbox_css_tokenizer.h"

/* The frame registry copies active descriptor records, newest first,
 * independently of selector specificity and of the stylesheet lifetime. */
struct tbox_style_counter_styles {
    const tbox_css_named_rule *rule;
    const tbox_style_counter_styles *next;
};

typedef enum tbox_counter_system {
    TBOX_COUNTER_SYMBOLIC, TBOX_COUNTER_CYCLIC, TBOX_COUNTER_NUMERIC,
    TBOX_COUNTER_ALPHABETIC, TBOX_COUNTER_ADDITIVE, TBOX_COUNTER_FIXED,
    TBOX_COUNTER_EXTENDS, TBOX_COUNTER_BUILTIN
} tbox_counter_system;

typedef struct tbox_counter_definition {
    tbox_counter_system system;
    tbox_string_view symbols[64], additive[64];
    unsigned long weights[64];
    size_t symbol_count, additive_count;
    long first;
    tbox_string_view prefix, suffix, negative[2], pad, fallback, extends;
    size_t pad_width, range_count;
    long ranges[16][2];
} tbox_counter_definition;

static tbox_string_view tbox_counter_descriptor(const tbox_css_named_rule *rule, const char *name) {
    for (size_t i = rule->declaration_count; i-- > 0;)
        if (tbox_string_view_equal_cstr(rule->declarations[i].property, name)) return rule->declarations[i].value;
    return tbox_string_view_make(NULL, 0);
}

bool tbox_style_counter_type_valid(tbox_string_view raw) {
    if (raw.size == 0 || raw.size >= 256) return false;
    tbox_css_tokenizer tokenizer;
    tbox_css_token token, end;
    tbox_css_tokenizer_init(&tokenizer, raw.data, raw.size);
    tbox_css_tokenizer_next(&tokenizer, &token);
    tbox_css_tokenizer_next(&tokenizer, &end);
    if ((token.type == TBOX_CSS_TOKEN_IDENT || token.type == TBOX_CSS_TOKEN_STRING) && end.type == TBOX_CSS_TOKEN_EOF) return true;
    tbox_string_view args;
    if (!tbox_style_function_args(raw, "symbols", &args)) return false;
    tbox_css_tokenizer_init(&tokenizer, args.data, args.size);
    size_t symbols = 0, minimum = 1;
    bool first = true;
    for (;;) {
        tbox_css_tokenizer_next(&tokenizer, &token);
        if (token.type == TBOX_CSS_TOKEN_S) continue;
        if (token.type == TBOX_CSS_TOKEN_EOF) break;
        if (first && token.type == TBOX_CSS_TOKEN_IDENT) {
            if (tbox_style_is(token.text, "numeric") || tbox_style_is(token.text, "alphabetic")) minimum = 2;
            else if (!tbox_style_is(token.text, "cyclic") && !tbox_style_is(token.text, "symbolic") && !tbox_style_is(token.text, "fixed")) return false;
        } else if (token.type == TBOX_CSS_TOKEN_STRING) symbols++;
        else return false;
        first = false;
    }
    return symbols >= minimum && symbols <= 63;
}

static bool tbox_counter_integer(tbox_string_view text, long *out) {
    double n;
    /* Limit descriptors to exactly representable integers; values supplied
     * by the counter machinery itself may use the entire long range. */
    if (!tbox_number_parse(text, &n) || n < -2147483648.0 || n > 2147483647.0) return false;
    *out = (long)n;
    return (double)*out == n;
}

static bool tbox_counter_symbol(tbox_arena *arena, tbox_string_view token, tbox_string_view *out) {
    if (token.size == 0) return false;
    if (token.data[0] == '"' || token.data[0] == '\'') {
        if (token.size < 2 || token.data[token.size - 1] != token.data[0]) return false;
        tbox_string_builder b;
        tbox_string_builder_init(&b, arena, token.size);
        if (!tbox_style_parse_content_string(&token, &b) || token.size != 0) return false;
        *out = tbox_string_builder_finish(&b);
        return true;
    }
    tbox_css_tokenizer tokenizer;
    tbox_css_token symbol, end;
    tbox_css_tokenizer_init(&tokenizer, token.data, token.size);
    tbox_css_tokenizer_next(&tokenizer, &symbol);
    tbox_css_tokenizer_next(&tokenizer, &end);
    if (symbol.type != TBOX_CSS_TOKEN_IDENT || end.type != TBOX_CSS_TOKEN_EOF) return false;
    *out = token;
    return true;
}

static bool tbox_counter_system_parse(tbox_string_view value, tbox_counter_definition *d) {
    tbox_string_view tokens[2];
    size_t count = tbox_style_tokens(value, tokens, 2);
    if (count == 0) return false;
    static const char *const names[] = { "symbolic", "cyclic", "numeric", "alphabetic", "additive", "fixed", "extends" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!tbox_style_is(tokens[0], names[i])) continue;
        d->system = (tbox_counter_system)i;
        if (i == TBOX_COUNTER_EXTENDS) {
            if (count != 2) return false;
            d->extends = tokens[1];
        } else if (i == TBOX_COUNTER_FIXED) {
            if (count == 2 && !tbox_counter_integer(tokens[1], &d->first)) return false;
        } else if (count != 1) return false;
        return true;
    }
    return false;
}

static bool tbox_counter_definition_get(tbox_arena *arena, const tbox_style_counter_styles *styles,
    tbox_string_view name, tbox_counter_definition *out, unsigned depth);

static bool tbox_counter_definition_parse(tbox_arena *arena, const tbox_style_counter_styles *styles,
    const tbox_css_named_rule *rule, tbox_counter_definition *out, unsigned depth) {
    tbox_counter_definition d = { .first = 1, .suffix = { ". ", 2 }, .negative = { { "-", 1 }, { NULL, 0 } }, .fallback = { "decimal", 7 } };
    tbox_string_view value = tbox_counter_descriptor(rule, "system");
    if (value.size != 0 && !tbox_counter_system_parse(value, &d)) return false;
    bool extending = d.system == TBOX_COUNTER_EXTENDS;
    if (extending && !tbox_counter_definition_get(arena, styles, d.extends, &d, depth + 1))
        tbox_counter_definition_get(arena, NULL, tbox_string_view_from_cstr("decimal"), &d, 0);
    tbox_string_view tokens[64];
    if (!extending) {
        value = tbox_counter_descriptor(rule, "symbols");
        if (value.size > 0) {
            size_t n = tbox_style_tokens(value, tokens, 64);
            for (size_t i = 0; i < n; i++)
                if (!tbox_counter_symbol(arena, tokens[i], &d.symbols[i])) return false;
            d.symbol_count = n;
        }
        value = tbox_counter_descriptor(rule, "additive-symbols");
        if (value.size > 0) {
            tbox_string_view pairs[64];
            size_t count = tbox_style_split_top_level(value, ',', pairs, 64);
            for (size_t i = 0; i < count; i++) {
                long weight;
                if (tbox_style_tokens(pairs[i], tokens, 2) != 2 || !tbox_counter_integer(tokens[0], &weight) || weight < 0 ||
                    !tbox_counter_symbol(arena, tokens[1], &d.additive[i]) || (i > 0 && (unsigned long)weight >= d.weights[i - 1])) return false;
                d.weights[i] = (unsigned long)weight;
            }
            d.additive_count = count;
        }
        size_t minimum = d.system == TBOX_COUNTER_NUMERIC || d.system == TBOX_COUNTER_ALPHABETIC ? 2 : 1;
        if (d.system == TBOX_COUNTER_ADDITIVE ? d.additive_count == 0 : d.symbol_count < minimum) return false;
    }
    value = tbox_counter_descriptor(rule, "prefix");
    tbox_string_view symbol;
    if (value.size && tbox_counter_symbol(arena, value, &symbol)) d.prefix = symbol;
    value = tbox_counter_descriptor(rule, "suffix");
    if (value.size && tbox_counter_symbol(arena, value, &symbol)) d.suffix = symbol;
    value = tbox_counter_descriptor(rule, "negative");
    if (value.size) {
        size_t n = tbox_style_tokens(value, tokens, 2);
        tbox_string_view a, b = {0};
        if (n > 0 && tbox_counter_symbol(arena, tokens[0], &a) && (n == 1 || tbox_counter_symbol(arena, tokens[1], &b))) {
            d.negative[0] = a; d.negative[1] = b;
        }
    }
    value = tbox_counter_descriptor(rule, "pad");
    if (value.size) {
        long width;
        if (tbox_style_tokens(value, tokens, 2) == 2 && tbox_counter_integer(tokens[0], &width) && width >= 0 && width <= 4096 && tbox_counter_symbol(arena, tokens[1], &symbol)) {
            d.pad_width = (size_t)width; d.pad = symbol;
        }
    }
    value = tbox_counter_descriptor(rule, "fallback");
    if (value.size && tbox_style_tokens(value, tokens, 2) == 1 && tokens[0].data[0] != '"' && tokens[0].data[0] != '\'') d.fallback = tokens[0];
    value = tbox_counter_descriptor(rule, "range");
    if (tbox_style_is(value, "auto")) d.range_count = 0;
    else if (value.size) {
        tbox_string_view pairs[16];
        size_t count = tbox_style_split_top_level(value, ',', pairs, 16);
        long ranges[16][2];
        bool valid = count > 0;
        for (size_t i = 0; valid && i < count; i++) {
            valid = tbox_style_tokens(pairs[i], tokens, 2) == 2;
            for (size_t j = 0; valid && j < 2; j++) {
                if (tbox_style_is(tokens[j], "infinite")) ranges[i][j] = j == 0 ? LONG_MIN : LONG_MAX;
                else valid = tbox_counter_integer(tokens[j], &ranges[i][j]);
            }
            valid = valid && ranges[i][0] <= ranges[i][1];
        }
        if (valid) { memcpy(d.ranges, ranges, count * sizeof(ranges[0])); d.range_count = count; }
    }
    *out = d;
    return true;
}

static bool tbox_counter_definition_get(tbox_arena *arena, const tbox_style_counter_styles *styles,
    tbox_string_view name, tbox_counter_definition *out, unsigned depth) {
    if (depth > 16) return false;
    for (const tbox_style_counter_styles *s = styles; s != NULL; s = s->next)
        if (tbox_string_view_equal(s->rule->name, name) && tbox_counter_definition_parse(arena, styles, s->rule, out, depth)) return true;
    /* Builtins can be extended without duplicating their representations. */
    static const char *const builtins[] = { "decimal", "decimal-leading-zero", "lower-roman", "upper-roman", "lower-alpha", "upper-alpha", "lower-latin", "upper-latin", "lower-greek", "disc", "circle", "square", "none" };
    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++)
        if (tbox_style_is(name, builtins[i])) {
            *out = (tbox_counter_definition){ .system = TBOX_COUNTER_BUILTIN, .extends = name, .suffix = { ". ", 2 }, .negative = { { "-", 1 }, { NULL, 0 } }, .fallback = { "decimal", 7 } };
            return true;
        }
    return false;
}

static tbox_string_view tbox_counter_copy(tbox_arena *arena, tbox_string_view text) {
    char *copy = tbox_arena_alloc(arena, text.size);
    if (text.size > 0) memcpy(copy, text.data, text.size);
    return tbox_string_view_make(copy, text.size);
}

tbox_style_counter_styles *tbox_style_counter_styles_create(tbox_arena *arena, const tbox_css_cascade_source *sources,
    size_t count, double width, double height, bool reduced_motion) {
    tbox_style_counter_styles *head = NULL;
    for (size_t i = 0; i < count; i++) {
        if (sources[i].stylesheet == NULL) continue;
        tbox_style_counter_styles *first = NULL, *last = NULL;
        for (const tbox_css_named_rule *rule = sources[i].stylesheet->counter_styles; rule != NULL; rule = rule->next) {
            if (!tbox_css_cascade_media_matches(rule->media, width, height, reduced_motion)) continue;
            tbox_style_counter_styles *entry = tbox_arena_alloc_zero(arena, sizeof(*entry));
            tbox_css_named_rule *copy = tbox_arena_alloc_zero(arena, sizeof(*copy));
            copy->name = tbox_counter_copy(arena, rule->name);
            copy->declaration_count = rule->declaration_count;
            copy->declarations = tbox_arena_alloc(arena, rule->declaration_count * sizeof(*copy->declarations));
            for (size_t j = 0; j < rule->declaration_count; j++) {
                copy->declarations[j].property = tbox_counter_copy(arena, rule->declarations[j].property);
                copy->declarations[j].value = tbox_counter_copy(arena, rule->declarations[j].value);
            }
            entry->rule = copy;
            if (last != NULL) last->next = entry;
            else first = entry;
            last = entry;
        }
        if (last != NULL) { last->next = head; head = first; }
    }
    return head;
}

static bool tbox_counter_representation(tbox_string_builder *b, const tbox_counter_definition *d, long value) {
    if (d->system == TBOX_COUNTER_BUILTIN) { tbox_style_counter_builtin(b, value, d->extends); return true; }
    /* Cyclic and fixed use the signed value directly; other systems use
     * its magnitude and add the negative descriptor around the result. */
    if (d->system == TBOX_COUNTER_CYCLIC) {
        long count = (long)d->symbol_count;
        long index = value % count;
        index = (index + count - 1) % count;
        tbox_string_builder_append_view(b, d->symbols[index]);
        return true;
    }
    if (d->system == TBOX_COUNTER_FIXED) {
        if (value < d->first || (unsigned long)value - (unsigned long)d->first >= d->symbol_count) return false;
        tbox_string_builder_append_view(b, d->symbols[(unsigned long)value - (unsigned long)d->first]);
        return true;
    }
    unsigned long n = value < 0 ? 0ul - (unsigned long)value : (unsigned long)value;
    if (d->system == TBOX_COUNTER_ADDITIVE) {
        if (n == 0) {
            for (size_t i = 0; i < d->additive_count; i++) if (d->weights[i] == 0) { tbox_string_builder_append_view(b, d->additive[i]); return true; }
            return false;
        }
        size_t total = 0;
        for (size_t i = 0; i < d->additive_count && n > 0; i++) {
            unsigned long weight = d->weights[i];
            if (weight == 0) continue;
            unsigned long repeats = n / weight;
            if (repeats > 4096 - total) return false;
            for (unsigned long j = 0; j < repeats; j++) tbox_string_builder_append_view(b, d->additive[i]);
            total += repeats;
            n %= weight;
        }
        return n == 0;
    }
    if (d->system == TBOX_COUNTER_SYMBOLIC) {
        if (n == 0 || (n - 1) / d->symbol_count >= 4096) return false;
        tbox_string_view symbol = d->symbols[(n - 1) % d->symbol_count];
        for (unsigned long i = 0; i <= (n - 1) / d->symbol_count; i++) tbox_string_builder_append_view(b, symbol);
        return true;
    }
    if (d->system == TBOX_COUNTER_ALPHABETIC && n == 0) return false;
    size_t digits[sizeof(long) * CHAR_BIT + 1], used = 0;
    do {
        if (d->system == TBOX_COUNTER_ALPHABETIC) n--;
        digits[used++] = n % d->symbol_count;
        n /= d->symbol_count;
    } while (n > 0);
    while (used > 0) tbox_string_builder_append_view(b, d->symbols[digits[--used]]);
    return true;
}

static tbox_string_view tbox_counter_format_impl(tbox_arena *arena, const tbox_style_counter_styles *styles,
    tbox_string_view name, long value, bool marker, unsigned depth) {
    tbox_string_builder b;
    tbox_string_builder_init(&b, arena, 32);
    tbox_counter_definition d;
    if (depth > 16) name = tbox_string_view_from_cstr("decimal");
    if (name.size > 0 && (name.data[0] == '"' || name.data[0] == '\'')) {
        tbox_style_parse_content_string(&name, &b);
        return tbox_string_builder_finish(&b);
    }
    tbox_string_view args;
    if (tbox_style_function_args(name, "symbols", &args)) {
        d = (tbox_counter_definition){ .first = 1, .suffix = { " ", 1 }, .negative = { { "-", 1 }, { NULL, 0 } }, .fallback = { "decimal", 7 } };
        tbox_string_view tokens[64];
        size_t count = tbox_style_tokens(args, tokens, 64), start = 0;
        if (count > 0 && tokens[0].data[0] != '"' && tokens[0].data[0] != '\'') {
            if (!tbox_counter_system_parse(tokens[0], &d) || d.system == TBOX_COUNTER_ADDITIVE || d.system == TBOX_COUNTER_EXTENDS) count = 0;
            start = 1;
        }
        for (size_t i = start; i < count; i++) {
            if (!tbox_counter_symbol(arena, tokens[i], &d.symbols[d.symbol_count])) { d.symbol_count = 0; break; }
            d.symbol_count++;
        }
        size_t minimum = d.system == TBOX_COUNTER_NUMERIC || d.system == TBOX_COUNTER_ALPHABETIC ? 2 : 1;
        if (d.symbol_count < minimum) return tbox_counter_format_impl(arena, styles, d.fallback, value, marker, depth + 1);
    } else if (!tbox_counter_definition_get(arena, styles, name, &d, 0) || (d.system == TBOX_COUNTER_BUILTIN && tbox_string_view_equal(d.extends, name))) {
        tbox_style_counter_builtin(&b, value, name);
        if (marker) tbox_string_builder_append_view(&b, tbox_string_view_from_cstr(". "));
        return tbox_string_builder_finish(&b);
    }
    bool in_range = d.range_count == 0;
    for (size_t i = 0; i < d.range_count; i++) if (value >= d.ranges[i][0] && value <= d.ranges[i][1]) in_range = true;
    if (d.range_count == 0 && (d.system == TBOX_COUNTER_SYMBOLIC || d.system == TBOX_COUNTER_ALPHABETIC)) in_range = value > 0;
    if (d.range_count == 0 && d.system == TBOX_COUNTER_ADDITIVE) in_range = value >= 0;
    tbox_string_builder core;
    tbox_string_builder_init(&core, arena, 32);
    if (!in_range || !tbox_counter_representation(&core, &d, value)) {
        /* Fallback generates the representation; marker decoration belongs
         * to the original style, even through a chain of fallbacks. */
        if (marker) tbox_string_builder_append_view(&b, d.prefix);
        tbox_string_builder_append_view(&b, tbox_counter_format_impl(arena, styles, depth >= 16 ? tbox_string_view_from_cstr("decimal") : d.fallback, value, false, depth + 1));
        if (marker) tbox_string_builder_append_view(&b, d.suffix);
        return tbox_string_builder_finish(&b);
    }
    tbox_string_view representation = tbox_string_builder_finish(&core);
    bool negative = value < 0 && d.system != TBOX_COUNTER_CYCLIC && d.system != TBOX_COUNTER_FIXED && d.system != TBOX_COUNTER_BUILTIN;
    size_t width = tbox_string_view_codepoint_count(representation);
    if (negative) width += tbox_string_view_codepoint_count(d.negative[0]) + tbox_string_view_codepoint_count(d.negative[1]);
    if (marker) tbox_string_builder_append_view(&b, d.prefix);
    if (negative) tbox_string_builder_append_view(&b, d.negative[0]);
    for (; width < d.pad_width; width++) tbox_string_builder_append_view(&b, d.pad);
    tbox_string_builder_append_view(&b, representation);
    if (negative) tbox_string_builder_append_view(&b, d.negative[1]);
    if (marker) tbox_string_builder_append_view(&b, d.suffix);
    return tbox_string_builder_finish(&b);
}

tbox_string_view tbox_style_counter_format(tbox_arena *arena, const tbox_style_counter_styles *styles,
    tbox_string_view name, long value, bool marker) {
    return tbox_counter_format_impl(arena, styles, name, value, marker, 0);
}
