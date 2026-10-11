#include "style/tbox_style_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* Generated content for ::before/::after. tbox_style_resolve_tree feeds
 * every displayed element here in document order, which is what counters
 * and quote nesting need:
 *
 * - a counter instance lives from the element that created it (by
 *   counter-reset, or by an increment/set with no instance in scope) to
 *   the end of that element's parent, so each instance records the parent
 *   (`scope`) whose end drops it (tbox_style_generated_leave);
 * - quote depth runs through the whole document; the `quotes` list is
 *   inherited, swapped in and out around each subtree. */

typedef struct tbox_style_counter {
    tbox_string_view name;
    long value;
    const tbox_html_node *scope;
} tbox_style_counter;

typedef struct tbox_style_quotes {
    size_t pair_count;
    tbox_string_view open[8], close[8];
} tbox_style_quotes;

struct tbox_style_generated {
    tbox_arena *arena;
    bool wants[2];
    tbox_vector counters; /* tbox_style_counter, innermost last */
    const tbox_style_quotes *quotes;
    int quote_depth;
};

/* `auto`: English curly quotes, double then single. */
static const tbox_style_quotes tbox_style_default_quotes = {
    2,
    { { "\xE2\x80\x9C", 3 }, { "\xE2\x80\x98", 3 } },
    { { "\xE2\x80\x9D", 3 }, { "\xE2\x80\x99", 3 } },
};

static bool tbox_style_generated_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool tbox_style_generated_ends_in(const tbox_css_selector *selector, const char *pseudo) {
    if (selector->simple_selector_count == 0)
        return false;
    const tbox_css_simple_selector *last = &selector->simple_selectors[selector->simple_selector_count - 1];
    return last->kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && tbox_string_view_equal_cstr(last->name, pseudo);
}

tbox_style_generated *tbox_style_generated_create(tbox_arena *arena, const tbox_css_cascade_source *sources, size_t source_count) {
    bool wants[2] = { false, false };
    for (size_t s = 0; s < source_count; s++) {
        const tbox_css_stylesheet *sheet = sources[s].stylesheet;
        size_t count                     = tbox_css_stylesheet_ruleset_count(sheet);
        const tbox_css_ruleset *rulesets = tbox_css_stylesheet_rulesets(sheet);
        for (size_t r = 0; sheet != NULL && r < count; r++)
            for (size_t i = 0; i < rulesets[r].selector_count; i++) {
                wants[0] = wants[0] || tbox_style_generated_ends_in(&rulesets[r].selectors[i], "::before");
                wants[1] = wants[1] || tbox_style_generated_ends_in(&rulesets[r].selectors[i], "::after");
            }
    }
    if (!wants[0] && !wants[1])
        return NULL;
    tbox_style_generated *generated = (tbox_style_generated *)tbox_arena_alloc_zero(arena, sizeof(tbox_style_generated));
    if (generated == NULL)
        return NULL;
    generated->arena    = arena;
    generated->wants[0] = wants[0];
    generated->wants[1] = wants[1];
    generated->quotes   = &tbox_style_default_quotes;
    tbox_vector_init(&generated->counters, arena, sizeof(tbox_style_counter), 0);
    return generated;
}

bool tbox_style_generated_wants(const tbox_style_generated *generated, int which) {
    return generated != NULL && generated->wants[which != 0];
}

bool tbox_style_generated_allowed(const tbox_html_node *element) {
    static const char *const excluded[] = { "img", "input", "select", "textarea", "br", "hr", "iframe", "video", "audio", "canvas", "embed", "object", "meta", "link", "base", "area", "source", "col", "wbr", "progress", "meter" };
    if (element == NULL || element->type != TBOX_HTML_NODE_ELEMENT)
        return false;
    for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); i++)
        if (tbox_string_view_equal_cstr(element->element.tag_name, excluded[i]))
            return false;
    return true;
}

/* ---- tokens of a declaration value ---- */

static tbox_string_view tbox_style_generated_skip_space(tbox_string_view text) {
    while (text.size > 0 && tbox_style_generated_is_space(text.data[0]))
        text = tbox_string_view_make(text.data + 1, text.size - 1);
    return text;
}

/* A CSS identifier at the start of `text` (letters, digits, '-', '_',
 * non-ASCII). */
static size_t tbox_style_generated_ident_length(tbox_string_view text) {
    size_t n = 0;
    while (n < text.size) {
        unsigned char c = (unsigned char)text.data[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 0x80))
            break;
        n++;
    }
    return n;
}

static void tbox_style_generated_append_codepoint(tbox_string_builder *builder, unsigned long cp) {
    char buffer[4];
    size_t n;
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
    if (cp < 0x80) {
        buffer[0] = (char)cp, n = 1;
    } else if (cp < 0x800) {
        buffer[0] = (char)(0xC0 | (cp >> 6)), buffer[1] = (char)(0x80 | (cp & 0x3F)), n = 2;
    } else if (cp < 0x10000) {
        buffer[0] = (char)(0xE0 | (cp >> 12)), buffer[1] = (char)(0x80 | ((cp >> 6) & 0x3F)), buffer[2] = (char)(0x80 | (cp & 0x3F)), n = 3;
    } else {
        buffer[0] = (char)(0xF0 | (cp >> 18)), buffer[1] = (char)(0x80 | ((cp >> 12) & 0x3F)), buffer[2] = (char)(0x80 | ((cp >> 6) & 0x3F)), buffer[3] = (char)(0x80 | (cp & 0x3F)), n = 4;
    }
    tbox_string_builder_append_view(builder, tbox_string_view_make(buffer, n));
}

/* A quoted string at the start of `*text` (consumed), its CSS escapes
 * decoded (`\201C ` -> U+201C, `\"` -> `"`, an escaped newline vanishes)
 * into `builder`. False when `*text` doesn't start with a string. */
static bool tbox_style_generated_string(tbox_string_view *text, tbox_string_builder *builder) {
    if (text->size == 0 || (text->data[0] != '"' && text->data[0] != '\''))
        return false;
    char quote = text->data[0];
    size_t i   = 1;
    while (i < text->size && text->data[i] != quote) {
        char c = text->data[i];
        if (c != '\\' || i + 1 >= text->size) {
            tbox_string_builder_append_view(builder, tbox_string_view_make(text->data + i, 1));
            i++;
            continue;
        }
        i++;
        if (text->data[i] == '\n') {
            i++;
            continue;
        }
        unsigned long cp = 0;
        size_t digits    = 0;
        while (digits < 6 && i < text->size) {
            char h = text->data[i];
            int v  = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
            if (v < 0)
                break;
            cp = cp * 16 + (unsigned long)v;
            digits++, i++;
        }
        if (digits > 0) {
            if (i < text->size && tbox_style_generated_is_space(text->data[i]))
                i++;
            tbox_style_generated_append_codepoint(builder, cp);
        } else {
            tbox_string_builder_append_view(builder, tbox_string_view_make(text->data + i, 1));
            i++;
        }
    }
    *text = tbox_string_view_make(text->data + (i < text->size ? i + 1 : i), i < text->size ? text->size - i - 1 : 0);
    return true;
}

/* The arguments of a function whose name and '(' were consumed: up to the
 * matching ')', split at top-level commas (trimmed). */
static size_t tbox_style_generated_arguments(tbox_string_view *text, tbox_string_view *args, size_t max) {
    size_t count = 0, start = 0, i = 0;
    int depth    = 0;
    char quote   = 0;
    for (; i < text->size; i++) {
        char c = text->data[i];
        if (quote != 0) {
            if (c == '\\') i++;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(') depth++;
        else if (c == ')' && depth > 0) depth--;
        else if ((c == ')' || c == ',') && depth == 0) {
            tbox_string_view arg = tbox_string_view_make(text->data + start, i - start);
            arg                  = tbox_style_generated_skip_space(arg);
            while (arg.size > 0 && tbox_style_generated_is_space(arg.data[arg.size - 1]))
                arg.size--;
            if (count < max)
                args[count++] = arg;
            start = i + 1;
            if (c == ')')
                break;
        }
    }
    *text = tbox_string_view_make(text->data + (i < text->size ? i + 1 : i), i < text->size ? text->size - i - 1 : 0);
    return count;
}

/* ---- counters ---- */

static tbox_style_counter *tbox_style_generated_find(tbox_style_generated *generated, tbox_string_view name) {
    for (size_t i = generated->counters.length; i-- > 0;) {
        tbox_style_counter *counter = (tbox_style_counter *)tbox_vector_at(&generated->counters, i);
        if (tbox_string_view_equal(counter->name, name))
            return counter;
    }
    return NULL;
}

/* A new instance of `name`. One already created by a previous sibling
 * (same scope) is replaced, not nested (CSS Lists 3). */
static tbox_style_counter *tbox_style_generated_instantiate(tbox_style_generated *generated, tbox_string_view name, const tbox_html_node *scope, long value) {
    tbox_style_counter *existing = tbox_style_generated_find(generated, name);
    if (existing != NULL && existing->scope == scope) {
        existing->value = value;
        return existing;
    }
    tbox_style_counter *counter = (tbox_style_counter *)tbox_vector_push(&generated->counters);
    if (counter == NULL)
        return NULL;
    counter->name  = name;
    counter->value = value;
    counter->scope = scope;
    return counter;
}

/* `none | [<name> <integer>?]+`, `fallback` when the integer is omitted:
 * mode 0 resets (new instance), 1 increments, 2 sets -- the last two on
 * the innermost instance, instantiating one at `scope` when none is in
 * scope. */
static void tbox_style_generated_counter_list(tbox_style_generated *generated, const tbox_html_node *scope, tbox_string_view value, long fallback, int mode) {
    value = tbox_style_generated_skip_space(value);
    if (value.size == 4 && tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none")))
        return;
    while (value.size > 0) {
        size_t length = tbox_style_generated_ident_length(value);
        if (length == 0)
            return; /* malformed: stop */
        tbox_string_view name = tbox_string_view_make(value.data, length);
        value                 = tbox_style_generated_skip_space(tbox_string_view_make(value.data + length, value.size - length));
        long amount           = fallback;
        if (value.size > 0 && (value.data[0] == '-' || value.data[0] == '+' || (value.data[0] >= '0' && value.data[0] <= '9'))) {
            char number[24];
            size_t n = 0;
            while (n < value.size && n + 1 < sizeof(number) && !tbox_style_generated_is_space(value.data[n])) {
                number[n] = value.data[n];
                n++;
            }
            number[n] = '\0';
            char *end;
            amount = strtol(number, &end, 10);
            if (*end != '\0')
                return;
            value = tbox_style_generated_skip_space(tbox_string_view_make(value.data + n, value.size - n));
        }
        /* The name lives in the cascade's storage, freed after the walk
         * step: copy it into the arena. */
        tbox_string_builder copy;
        tbox_string_builder_init(&copy, generated->arena, name.size);
        tbox_string_builder_append_view(&copy, name);
        name = tbox_string_builder_finish(&copy);
        if (mode == 0) {
            tbox_style_generated_instantiate(generated, name, scope, amount);
        } else {
            tbox_style_counter *counter = tbox_style_generated_find(generated, name);
            if (counter == NULL)
                counter = tbox_style_generated_instantiate(generated, name, scope, 0);
            if (counter != NULL)
                counter->value = mode == 1 ? counter->value + amount : amount;
        }
    }
}

void tbox_style_generated_counters(tbox_style_generated *generated, const tbox_html_node *scope_parent, const tbox_css_computed_style *computed) {
    static const char *const properties[3] = { "counter-reset", "counter-increment", "counter-set" };
    static const long fallbacks[3]         = { 0, 1, 0 };
    for (int mode = 0; mode < 3; mode++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(properties[mode]));
        if (decl != NULL)
            tbox_style_generated_counter_list(generated, scope_parent, decl->value, fallbacks[mode], mode);
    }
}

void tbox_style_generated_leave(tbox_style_generated *generated, const tbox_html_node *element) {
    while (generated->counters.length > 0) {
        const tbox_style_counter *last = (const tbox_style_counter *)tbox_vector_at(&generated->counters, generated->counters.length - 1);
        if (last->scope != element)
            break;
        generated->counters.length--;
    }
}

/* `value` in counter style `style` (a list-style-type keyword). */
static void tbox_style_generated_format(tbox_string_builder *builder, long value, tbox_string_view style) {
    char buffer[64];
    if (style.size == 0 || tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("decimal"))) {
        snprintf(buffer, sizeof(buffer), "%ld", value);
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("decimal-leading-zero"))) {
        snprintf(buffer, sizeof(buffer), value >= 0 && value < 10 ? "0%ld" : "%ld", value);
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("none"))) {
        return;
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("disc"))) {
        tbox_string_builder_append_view(builder, tbox_string_view_from_cstr("\xE2\x80\xA2"));
        return;
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("circle"))) {
        tbox_string_builder_append_view(builder, tbox_string_view_from_cstr("\xE2\x97\xA6"));
        return;
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("square"))) {
        tbox_string_builder_append_view(builder, tbox_string_view_from_cstr("\xE2\x96\xAA"));
        return;
    } else if ((tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("lower-roman")) || tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("upper-roman"))) && value > 0 && value < 4000) {
        static const struct { int value; const char *text; } numerals[] = {
            { 1000, "m" }, { 900, "cm" }, { 500, "d" }, { 400, "cd" }, { 100, "c" }, { 90, "xc" },
            { 50, "l" }, { 40, "xl" }, { 10, "x" }, { 9, "ix" }, { 5, "v" }, { 4, "iv" }, { 1, "i" },
        };
        bool upper = style.data[0] == 'u' || style.data[0] == 'U';
        size_t n   = 0;
        long rest  = value;
        for (size_t i = 0; i < sizeof(numerals) / sizeof(numerals[0]); i++)
            for (; rest >= numerals[i].value; rest -= numerals[i].value)
                for (const char *c = numerals[i].text; *c != '\0' && n + 1 < sizeof(buffer); c++)
                    buffer[n++] = upper ? (char)(*c - 'a' + 'A') : *c;
        buffer[n] = '\0';
    } else if ((tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("lower-alpha")) || tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("lower-latin")) ||
                tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("upper-alpha")) || tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("upper-latin"))) && value > 0) {
        char base = style.data[0] == 'u' || style.data[0] == 'U' ? 'A' : 'a';
        char reversed[32];
        size_t n = 0;
        for (long rest = value; rest > 0 && n < sizeof(reversed); rest = (rest - 1) / 26)
            reversed[n++] = (char)(base + (rest - 1) % 26);
        for (size_t i = 0; i < n; i++)
            buffer[i] = reversed[n - 1 - i];
        buffer[n] = '\0';
    } else if (tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("lower-greek")) && value > 0) {
        /* alpha..omega, skipping final sigma (U+03C2). */
        unsigned long letters[24];
        for (int i = 0, cp = 0x3B1; i < 24; cp++)
            if (cp != 0x3C2) letters[i++] = (unsigned long)cp;
        unsigned long out[16];
        size_t n = 0;
        for (long rest = value; rest > 0 && n < 16; rest = (rest - 1) / 24)
            out[n++] = letters[(rest - 1) % 24];
        while (n > 0)
            tbox_style_generated_append_codepoint(builder, out[--n]);
        return;
    } else {
        snprintf(buffer, sizeof(buffer), "%ld", value); /* outside the style's range, or unknown */
    }
    tbox_string_builder_append_view(builder, tbox_string_view_from_cstr(buffer));
}

/* ---- quotes ---- */

const void *tbox_style_generated_quotes(tbox_style_generated *generated, const tbox_css_computed_style *computed) {
    const tbox_style_quotes *previous         = generated->quotes;
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("quotes"));
    if (decl == NULL)
        return previous;
    tbox_string_view value = tbox_style_generated_skip_space(decl->value);
    if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("auto"))) {
        generated->quotes = &tbox_style_default_quotes;
        return previous;
    }
    tbox_style_quotes *quotes = (tbox_style_quotes *)tbox_arena_alloc_zero(generated->arena, sizeof(tbox_style_quotes));
    if (quotes == NULL || tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("none"))) {
        if (quotes != NULL)
            generated->quotes = quotes; /* no pairs */
        return previous;
    }
    tbox_string_view strings[16];
    size_t count = 0;
    while (value.size > 0 && count < 16) {
        tbox_string_builder builder;
        tbox_string_builder_init(&builder, generated->arena, 8);
        if (!tbox_style_generated_string(&value, &builder))
            return previous; /* invalid: keep the inherited list */
        strings[count++] = tbox_string_builder_finish(&builder);
        value            = tbox_style_generated_skip_space(value);
    }
    if (count == 0 || count % 2 != 0)
        return previous;
    for (size_t i = 0; i < count / 2 && i < 8; i++) {
        quotes->open[i]  = strings[2 * i];
        quotes->close[i] = strings[2 * i + 1];
        quotes->pair_count++;
    }
    generated->quotes = quotes;
    return previous;
}

void tbox_style_generated_restore_quotes(tbox_style_generated *generated, const void *previous) {
    generated->quotes = (const tbox_style_quotes *)previous;
}

/* ---- content ---- */

bool tbox_style_generated_content(tbox_style_generated *generated, const tbox_html_node *element, const tbox_css_computed_style *computed, tbox_string_view *out) {
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("content"));
    if (decl == NULL)
        return false;
    tbox_string_view value = tbox_style_generated_skip_space(decl->value);
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, generated->arena, 16);
    bool any = false;
    while (value.size > 0) {
        value = tbox_style_generated_skip_space(value);
        if (value.size == 0 || value.data[0] == '/')
            break; /* "/ alt text" is for accessibility only */
        if (tbox_style_generated_string(&value, &builder)) {
            any = true;
            continue;
        }
        size_t length = tbox_style_generated_ident_length(value);
        if (length == 0)
            return false;
        tbox_string_view word = tbox_string_view_make(value.data, length);
        value                 = tbox_string_view_make(value.data + length, value.size - length);
        bool function         = value.size > 0 && value.data[0] == '(';
        if (!function) {
            const tbox_style_quotes *quotes = generated->quotes;
            bool open  = tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("open-quote"));
            bool close = tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("close-quote"));
            if (open || tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("no-open-quote"))) {
                if (open && quotes->pair_count > 0)
                    tbox_string_builder_append_view(&builder, quotes->open[(size_t)generated->quote_depth < quotes->pair_count ? (size_t)generated->quote_depth : quotes->pair_count - 1]);
                generated->quote_depth++;
            } else if (close || tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("no-close-quote"))) {
                if (generated->quote_depth > 0) {
                    generated->quote_depth--;
                    if (close && quotes->pair_count > 0)
                        tbox_string_builder_append_view(&builder, quotes->close[(size_t)generated->quote_depth < quotes->pair_count ? (size_t)generated->quote_depth : quotes->pair_count - 1]);
                }
            } else {
                return false; /* none, normal, or an unknown keyword */
            }
            any = true;
            continue;
        }
        value = tbox_string_view_make(value.data + 1, value.size - 1);
        tbox_string_view args[3];
        size_t count = tbox_style_generated_arguments(&value, args, 3);
        if (tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("attr")) && count >= 1) {
            /* attr(name), attr(name type, fallback): the attribute's text,
             * else the fallback string, else nothing. */
            size_t name_length = tbox_style_generated_ident_length(args[0]);
            const tbox_html_attribute *attribute = tbox_html_node_get_attribute(element, tbox_string_view_make(args[0].data, name_length));
            if (attribute != NULL)
                tbox_string_builder_append_view(&builder, attribute->value);
            else if (count >= 2)
                tbox_style_generated_string(&args[1], &builder);
        } else if (tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("counter")) && count >= 1) {
            /* A counter never instantiated reads as 0, as in browsers. */
            const tbox_style_counter *counter = tbox_style_generated_find(generated, args[0]);
            tbox_style_generated_format(&builder, counter != NULL ? counter->value : 0, count >= 2 ? args[1] : tbox_string_view_make(NULL, 0));
        } else if (tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("counters")) && count >= 2) {
            tbox_string_builder separator;
            tbox_string_builder_init(&separator, generated->arena, 4);
            tbox_style_generated_string(&args[1], &separator);
            tbox_string_view joiner = tbox_string_builder_finish(&separator);
            bool first              = true;
            for (size_t i = 0; i < generated->counters.length; i++) {
                const tbox_style_counter *counter = (const tbox_style_counter *)tbox_vector_at(&generated->counters, i);
                if (!tbox_string_view_equal(counter->name, args[0]))
                    continue;
                if (!first)
                    tbox_string_builder_append_view(&builder, joiner);
                tbox_style_generated_format(&builder, counter->value, count >= 3 ? args[2] : tbox_string_view_make(NULL, 0));
                first = false;
            }
            if (first)
                tbox_style_generated_format(&builder, 0, count >= 3 ? args[2] : tbox_string_view_make(NULL, 0));
        } else if (!tbox_string_view_equal_ascii_ci(word, tbox_string_view_from_cstr("url"))) {
            return false; /* an image function or something unknown */
        } else {
            continue; /* url(): images aren't generated; the rest still is */
        }
        any = true;
    }
    if (!any)
        return false;
    *out = tbox_string_builder_finish(&builder);
    return true;
}

tbox_html_node *tbox_style_generated_node(tbox_style_generated *generated, const tbox_html_node *element, int which, tbox_string_view text) {
    tbox_html_node *node = (tbox_html_node *)tbox_arena_alloc_zero(generated->arena, sizeof(tbox_html_node));
    if (node == NULL)
        return NULL;
    node->type                  = TBOX_HTML_NODE_ELEMENT;
    node->element.tag_name      = tbox_string_view_from_cstr(which == 0 ? "::before" : "::after");
    node->parent                = (tbox_html_node *)element;
    if (text.size > 0) {
        tbox_html_node *child = (tbox_html_node *)tbox_arena_alloc_zero(generated->arena, sizeof(tbox_html_node));
        if (child != NULL) {
            child->type      = TBOX_HTML_NODE_TEXT;
            child->text.text = text;
            child->parent    = node;
            node->first_child = node->last_child = child;
        }
    }
    return node;
}
