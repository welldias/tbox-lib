#include <tbox/css_cascade.h>

#include <math.h>

#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"

/* `@container [<name>] <condition>` (CSS Containment 3), decided per
 * element against its tbox_css_query_context. Three-valued: a feature the
 * container can't answer (wrong container-type, not measured yet, unknown
 * name or function) is unknown, which `not` keeps and the rule treats as
 * false. Writing modes are horizontal: inline is width, block is height. */

typedef enum tbox_cq_result {
    TBOX_CQ_FALSE,
    TBOX_CQ_TRUE,
    TBOX_CQ_UNKNOWN,
} tbox_cq_result;

enum {
    TBOX_CQ_AXIS_INLINE = 1u << 0,
    TBOX_CQ_AXIS_BLOCK  = 1u << 1,
};

typedef struct tbox_cq_env {
    const tbox_css_query_container *container; /* for size features; NULL: unknown */
    const struct tbox_style_custom_properties *custom_properties; /* for style() */
    double viewport_width, viewport_height;
} tbox_cq_env;

static bool tbox_cq_is(tbox_string_view view, const char *text) {
    return tbox_string_view_equal_ascii_ci(view, tbox_string_view_from_cstr(text));
}

static bool tbox_cq_ident_byte(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (unsigned char)c >= 0x80;
}

static size_t tbox_cq_ident_length(tbox_string_view text, size_t at) {
    size_t end = at;
    while (end < text.size && tbox_cq_ident_byte(text.data[end]))
        end++;
    return end - at;
}

/* Index just past the ')' matching the '(' at `open`, or 0 when it never
 * closes. */
static size_t tbox_cq_close(tbox_string_view text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size; i++) {
        if (text.data[i] == '(')
            depth++;
        else if (text.data[i] == ')' && --depth == 0)
            return i + 1;
    }
    return 0;
}

static tbox_cq_result tbox_cq_not(tbox_cq_result value) {
    return value == TBOX_CQ_UNKNOWN ? TBOX_CQ_UNKNOWN : value == TBOX_CQ_TRUE ? TBOX_CQ_FALSE : TBOX_CQ_TRUE;
}

/* The size feature `name` (min-/max- already removed): its axes, and
 * 0 when it is not one. */
static unsigned tbox_cq_feature_axes(tbox_string_view name) {
    if (tbox_cq_is(name, "width") || tbox_cq_is(name, "inline-size"))
        return TBOX_CQ_AXIS_INLINE;
    if (tbox_cq_is(name, "height") || tbox_cq_is(name, "block-size"))
        return TBOX_CQ_AXIS_BLOCK;
    if (tbox_cq_is(name, "aspect-ratio") || tbox_cq_is(name, "orientation"))
        return TBOX_CQ_AXIS_INLINE | TBOX_CQ_AXIS_BLOCK;
    return 0;
}

static tbox_string_view tbox_cq_strip_prefix(tbox_string_view name, int *prefix) {
    *prefix = 0;
    if (name.size > 4 && tbox_cq_is(tbox_string_view_make(name.data, 4), "min-"))
        *prefix = 1;
    else if (name.size > 4 && tbox_cq_is(tbox_string_view_make(name.data, 4), "max-"))
        *prefix = -1;
    if (*prefix != 0 && tbox_cq_feature_axes(tbox_string_view_make(name.data + 4, name.size - 4)) != 0)
        return tbox_string_view_make(name.data + 4, name.size - 4);
    *prefix = 0;
    return name;
}

/* The axes every size feature of `condition` needs: a container must have
 * them all to be chosen. style() arguments are skipped. */
static unsigned tbox_cq_condition_axes(tbox_string_view condition) {
    unsigned axes = 0;
    for (size_t i = 0; i < condition.size;) {
        size_t length = tbox_cq_ident_length(condition, i);
        if (length == 0) {
            i++;
            continue;
        }
        tbox_string_view word = tbox_string_view_make(condition.data + i, length);
        if (i + length < condition.size && condition.data[i + length] == '(' && tbox_cq_is(word, "style")) {
            size_t end = tbox_cq_close(condition, i + length);
            i = end != 0 ? end : condition.size;
            continue;
        }
        int prefix;
        axes |= tbox_cq_feature_axes(tbox_cq_strip_prefix(word, &prefix));
        i += length;
    }
    return axes;
}

/* A length in px: px, em/rem (the container's), viewport and absolute
 * units, or a bare 0. */
static bool tbox_cq_length(tbox_string_view text, const tbox_cq_env *query_env, double *out) {
    double number;
    size_t used = tbox_number_scan(text, &number);
    if (used == 0 || !isfinite(number))
        return false;
    tbox_string_view unit = tbox_string_view_make(text.data + used, text.size - used);
    double vw = query_env->viewport_width / 100.0, vh = query_env->viewport_height / 100.0;
    const tbox_css_query_container *c = query_env->container;
    const struct {
        const char *name;
        double px;
    } units[] = {
        { "px", 1.0 }, { "em", c->font_size }, { "rem", c->root_font_size },
        { "ex", c->font_size * 0.5 }, { "ch", c->font_size * 0.5 },
        { "pt", 96.0 / 72.0 }, { "pc", 16.0 }, { "in", 96.0 }, { "cm", 96.0 / 2.54 },
        { "mm", 96.0 / 25.4 }, { "q", 96.0 / 101.6 },
        { "vw", vw }, { "vh", vh }, { "vmin", vw < vh ? vw : vh }, { "vmax", vw > vh ? vw : vh },
        { "svw", vw }, { "svh", vh }, { "lvw", vw }, { "lvh", vh }, { "dvw", vw }, { "dvh", vh },
    };
    if (unit.size == 0) {
        if (number != 0.0)
            return false;
        *out = 0.0;
        return true;
    }
    for (size_t i = 0; i < sizeof(units) / sizeof(units[0]); i++)
        if (tbox_cq_is(unit, units[i].name)) {
            *out = number * units[i].px;
            return true;
        }
    return false;
}

/* `16/9`, `16 / 9` or a single number. */
static bool tbox_cq_ratio(tbox_string_view text, double *out) {
    size_t slash = 0;
    while (slash < text.size && text.data[slash] != '/')
        slash++;
    double a, b = 1.0;
    if (!tbox_number_parse(tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data, slash)), &a))
        return false;
    if (slash < text.size && !tbox_number_parse(tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + slash + 1, text.size - slash - 1)), &b))
        return false;
    if (a < 0.0 || b <= 0.0)
        return false;
    *out = a / b;
    return true;
}

/* The container's value of size feature `name` into *actual, and how its
 * values are written (ratio or length). */
static tbox_cq_result tbox_cq_actual(tbox_string_view name, const tbox_cq_env *query_env, double *actual, bool *ratio) {
    const tbox_css_query_container *c = query_env->container;
    unsigned axes = tbox_cq_feature_axes(name);
    if (axes == 0 || c == NULL)
        return TBOX_CQ_UNKNOWN;
    if ((axes & TBOX_CQ_AXIS_INLINE) && (c->type == TBOX_CSS_CONTAINER_NORMAL || c->width < 0.0))
        return TBOX_CQ_UNKNOWN;
    if ((axes & TBOX_CQ_AXIS_BLOCK) && (c->type != TBOX_CSS_CONTAINER_SIZE || c->height < 0.0))
        return TBOX_CQ_UNKNOWN;
    *ratio = false;
    if (tbox_cq_is(name, "width") || tbox_cq_is(name, "inline-size"))
        *actual = c->width;
    else if (tbox_cq_is(name, "height") || tbox_cq_is(name, "block-size"))
        *actual = c->height;
    else {
        if (c->height <= 0.0)
            return TBOX_CQ_UNKNOWN;
        *actual = c->width / c->height;
        *ratio  = true;
    }
    return TBOX_CQ_TRUE;
}

static bool tbox_cq_value(tbox_string_view text, bool ratio, const tbox_cq_env *query_env, double *out) {
    text = tbox_string_view_trim_ascii_space(text);
    return ratio ? tbox_cq_ratio(text, out) : tbox_cq_length(text, query_env, out);
}

typedef enum tbox_cq_op {
    TBOX_CQ_OP_LT,
    TBOX_CQ_OP_LE,
    TBOX_CQ_OP_GT,
    TBOX_CQ_OP_GE,
    TBOX_CQ_OP_EQ,
} tbox_cq_op;

static bool tbox_cq_compare(double a, tbox_cq_op op, double b) {
    const double epsilon = 1e-6;
    switch (op) {
    case TBOX_CQ_OP_LT: return a < b - epsilon;
    case TBOX_CQ_OP_LE: return a <= b + epsilon;
    case TBOX_CQ_OP_GT: return a > b + epsilon;
    case TBOX_CQ_OP_GE: return a >= b - epsilon;
    case TBOX_CQ_OP_EQ: return fabs(a - b) <= epsilon;
    }
    return false;
}

/* `a op name` read as `name op' a`. */
static tbox_cq_op tbox_cq_flip(tbox_cq_op op) {
    switch (op) {
    case TBOX_CQ_OP_LT: return TBOX_CQ_OP_GT;
    case TBOX_CQ_OP_LE: return TBOX_CQ_OP_GE;
    case TBOX_CQ_OP_GT: return TBOX_CQ_OP_LT;
    case TBOX_CQ_OP_GE: return TBOX_CQ_OP_LE;
    case TBOX_CQ_OP_EQ: return TBOX_CQ_OP_EQ;
    }
    return op;
}

/* The inside of `( … )` that is a size feature: `name`, `name: value`
 * (with min-/max-), or a range `name op v`, `v op name`,
 * `v op name op v`. */
static tbox_cq_result tbox_cq_feature(tbox_string_view text, const tbox_cq_env *query_env) {
    text = tbox_string_view_trim_ascii_space(text);
    size_t colon = 0;
    while (colon < text.size && text.data[colon] != ':')
        colon++;
    double actual, target;
    bool ratio;
    if (colon < text.size) {
        int prefix;
        tbox_string_view name  = tbox_cq_strip_prefix(tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data, colon)), &prefix);
        tbox_string_view value = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + colon + 1, text.size - colon - 1));
        if (tbox_cq_is(name, "orientation")) {
            const tbox_css_query_container *c = query_env->container;
            if (prefix != 0 || c == NULL || c->type != TBOX_CSS_CONTAINER_SIZE || c->width < 0.0 || c->height < 0.0)
                return TBOX_CQ_UNKNOWN;
            if (tbox_cq_is(value, "portrait"))
                return c->height >= c->width ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
            if (tbox_cq_is(value, "landscape"))
                return c->width > c->height ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
            return TBOX_CQ_UNKNOWN;
        }
        tbox_cq_result known = tbox_cq_actual(name, query_env, &actual, &ratio);
        if (known != TBOX_CQ_TRUE)
            return known;
        if (!tbox_cq_value(value, ratio, query_env, &target))
            return TBOX_CQ_UNKNOWN;
        tbox_cq_op op = prefix > 0 ? TBOX_CQ_OP_GE : prefix < 0 ? TBOX_CQ_OP_LE : TBOX_CQ_OP_EQ;
        return tbox_cq_compare(actual, op, target) ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
    }

    /* Split on the comparison operators. */
    tbox_string_view parts[3];
    tbox_cq_op ops[2];
    size_t count = 0, start = 0;
    for (size_t i = 0; i < text.size; i++) {
        char c = text.data[i];
        if (c != '<' && c != '>' && c != '=')
            continue;
        if (count == 2)
            return TBOX_CQ_UNKNOWN;
        bool equals = c != '=' && i + 1 < text.size && text.data[i + 1] == '=';
        ops[count]  = c == '=' ? TBOX_CQ_OP_EQ : c == '<' ? (equals ? TBOX_CQ_OP_LE : TBOX_CQ_OP_LT) : (equals ? TBOX_CQ_OP_GE : TBOX_CQ_OP_GT);
        parts[count++] = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + start, i - start));
        i += equals ? 1 : 0;
        start = i + 1;
    }
    parts[count] = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + start, text.size - start));

    if (count == 0) {
        /* Boolean context: true unless the value is zero. */
        if (tbox_cq_is(text, "orientation")) {
            const tbox_css_query_container *c = query_env->container;
            return c != NULL && c->type == TBOX_CSS_CONTAINER_SIZE && c->width >= 0.0 && c->height >= 0.0 ? TBOX_CQ_TRUE : TBOX_CQ_UNKNOWN;
        }
        tbox_cq_result known = tbox_cq_actual(text, query_env, &actual, &ratio);
        if (known != TBOX_CQ_TRUE)
            return known;
        return actual != 0.0 ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
    }
    for (size_t i = 0; i <= count; i++)
        if (parts[i].size == 0)
            return TBOX_CQ_UNKNOWN;
    if (count == 1) {
        bool name_first = tbox_cq_feature_axes(parts[0]) != 0;
        tbox_string_view name = name_first ? parts[0] : parts[1];
        tbox_cq_result known  = tbox_cq_actual(name, query_env, &actual, &ratio);
        if (known != TBOX_CQ_TRUE)
            return known;
        if (!tbox_cq_value(name_first ? parts[1] : parts[0], ratio, query_env, &target))
            return TBOX_CQ_UNKNOWN;
        tbox_cq_op op = name_first ? ops[0] : tbox_cq_flip(ops[0]);
        return tbox_cq_compare(actual, op, target) ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
    }
    /* `a < name < b` or `a > name > b`: both operators the same way. */
    bool less = ops[0] == TBOX_CQ_OP_LT || ops[0] == TBOX_CQ_OP_LE;
    bool less_too = ops[1] == TBOX_CQ_OP_LT || ops[1] == TBOX_CQ_OP_LE;
    if (ops[0] == TBOX_CQ_OP_EQ || ops[1] == TBOX_CQ_OP_EQ || less != less_too)
        return TBOX_CQ_UNKNOWN;
    tbox_cq_result known = tbox_cq_actual(parts[1], query_env, &actual, &ratio);
    if (known != TBOX_CQ_TRUE)
        return known;
    double low, high;
    if (!tbox_cq_value(parts[0], ratio, query_env, &low) || !tbox_cq_value(parts[2], ratio, query_env, &high))
        return TBOX_CQ_UNKNOWN;
    return tbox_cq_compare(actual, tbox_cq_flip(ops[0]), low) && tbox_cq_compare(actual, ops[1], high) ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
}

/* `style(--name)` or `style(--name: value)`: the container's computed
 * custom property, compared as trimmed text. */
static tbox_cq_result tbox_cq_style(tbox_string_view text, const tbox_cq_env *query_env) {
    text = tbox_string_view_trim_ascii_space(text);
    size_t colon = 0;
    while (colon < text.size && text.data[colon] != ':')
        colon++;
    tbox_string_view name = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data, colon));
    if (name.size < 3 || name.data[0] != '-' || name.data[1] != '-' || tbox_cq_ident_length(name, 0) != name.size)
        return TBOX_CQ_UNKNOWN;
    tbox_string_view actual;
    bool defined = tbox_style_custom_property_get(query_env->custom_properties, name, &actual);
    if (colon == text.size)
        return defined ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
    tbox_string_view wanted = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + colon + 1, text.size - colon - 1));
    return defined && tbox_string_view_equal(tbox_string_view_trim_ascii_space(actual), wanted) ? TBOX_CQ_TRUE : TBOX_CQ_FALSE;
}

static tbox_cq_result tbox_cq_condition(tbox_string_view text, const tbox_cq_env *query_env, int depth);

/* One `( … )` or function term. */
static tbox_cq_result tbox_cq_term(tbox_string_view term, const tbox_cq_env *query_env, int depth) {
    if (term.data[0] != '(') {
        size_t length = tbox_cq_ident_length(term, 0);
        if (tbox_cq_is(tbox_string_view_make(term.data, length), "style"))
            return tbox_cq_style(tbox_string_view_make(term.data + length + 1, term.size - length - 2), query_env);
        return TBOX_CQ_UNKNOWN; /* <general-enclosed> */
    }
    tbox_string_view inner = tbox_string_view_trim_ascii_space(tbox_string_view_make(term.data + 1, term.size - 2));
    if (inner.size == 0)
        return TBOX_CQ_UNKNOWN;
    size_t length = tbox_cq_ident_length(inner, 0);
    bool nested = inner.data[0] == '(' || (length > 0 && length < inner.size && inner.data[length] == '(') ||
                  (length == 3 && tbox_cq_is(tbox_string_view_make(inner.data, 3), "not") && length < inner.size);
    return nested ? tbox_cq_condition(inner, query_env, depth + 1) : tbox_cq_feature(inner, query_env);
}

/* The term starting at text[*at]: `( … )` or `ident( … )`; 0-size when
 * there is none. Advances *at past it. */
static tbox_string_view tbox_cq_next_term(tbox_string_view text, size_t *at) {
    size_t start = *at, open = start;
    if (start < text.size && text.data[start] != '(')
        open = start + tbox_cq_ident_length(text, start);
    if (open >= text.size || text.data[open] != '(')
        return tbox_string_view_make(NULL, 0);
    size_t end = tbox_cq_close(text, open);
    if (end == 0)
        return tbox_string_view_make(NULL, 0);
    *at = end;
    return tbox_string_view_make(text.data + start, end - start);
}

static size_t tbox_cq_skip_space(tbox_string_view text, size_t at) {
    while (at < text.size && tbox_ascii_is_space(text.data[at]))
        at++;
    return at;
}

/* `not <term>`, or terms joined all by `and` or all by `or`. Malformed
 * text is false. */
static tbox_cq_result tbox_cq_condition(tbox_string_view text, const tbox_cq_env *query_env, int depth) {
    if (depth > 16)
        return TBOX_CQ_FALSE;
    text = tbox_string_view_trim_ascii_space(text);
    size_t at = 0;
    if (tbox_cq_ident_length(text, 0) == 3 && tbox_cq_is(tbox_string_view_make(text.data, 3), "not")) {
        at = tbox_cq_skip_space(text, 3);
        tbox_string_view term = tbox_cq_next_term(text, &at);
        if (term.size == 0 || tbox_cq_skip_space(text, at) != text.size)
            return TBOX_CQ_FALSE;
        return tbox_cq_not(tbox_cq_term(term, query_env, depth));
    }
    int joiner = 0; /* 1 and, 2 or */
    tbox_cq_result result = TBOX_CQ_FALSE;
    for (bool first = true;; first = false) {
        tbox_string_view term = tbox_cq_next_term(text, &at);
        if (term.size == 0)
            return TBOX_CQ_FALSE;
        tbox_cq_result value = tbox_cq_term(term, query_env, depth);
        if (first)
            result = value;
        else if (joiner == 1)
            result = result == TBOX_CQ_FALSE || value == TBOX_CQ_FALSE ? TBOX_CQ_FALSE : result == TBOX_CQ_UNKNOWN || value == TBOX_CQ_UNKNOWN ? TBOX_CQ_UNKNOWN : TBOX_CQ_TRUE;
        else
            result = result == TBOX_CQ_TRUE || value == TBOX_CQ_TRUE ? TBOX_CQ_TRUE : result == TBOX_CQ_UNKNOWN || value == TBOX_CQ_UNKNOWN ? TBOX_CQ_UNKNOWN : TBOX_CQ_FALSE;
        at = tbox_cq_skip_space(text, at);
        if (at == text.size)
            return result;
        size_t length = tbox_cq_ident_length(text, at);
        tbox_string_view word = tbox_string_view_make(text.data + at, length);
        int next = tbox_cq_is(word, "and") ? 1 : tbox_cq_is(word, "or") ? 2 : 0;
        if (next == 0 || (joiner != 0 && next != joiner))
            return TBOX_CQ_FALSE;
        joiner = next;
        at = tbox_cq_skip_space(text, at + length);
    }
}

static bool tbox_cq_has_name(tbox_string_view names, tbox_string_view name) {
    for (size_t i = 0; i < names.size;) {
        while (i < names.size && tbox_ascii_is_space(names.data[i]))
            i++;
        size_t start = i;
        while (i < names.size && !tbox_ascii_is_space(names.data[i]))
            i++;
        if (i > start && tbox_string_view_equal(tbox_string_view_make(names.data + start, i - start), name))
            return true;
    }
    return false;
}

/* One `[<name>] <condition>` of the prelude's comma list. */
static bool tbox_cq_one(tbox_string_view text, const tbox_css_query_context *query, double viewport_width, double viewport_height) {
    text = tbox_string_view_trim_ascii_space(text);
    tbox_string_view name = tbox_string_view_make(NULL, 0);
    size_t length = tbox_cq_ident_length(text, 0);
    if (length > 0 && (length == text.size || text.data[length] != '(')) {
        tbox_string_view word = tbox_string_view_make(text.data, length);
        if (!tbox_cq_is(word, "not") && !tbox_cq_is(word, "and") && !tbox_cq_is(word, "or")) {
            if (tbox_cq_is(word, "none") || tbox_cq_is(word, "default") || tbox_cq_is(word, "initial") || tbox_cq_is(word, "inherit") || tbox_cq_is(word, "unset"))
                return false;
            name = word;
            text = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + length, text.size - length));
        }
    }
    unsigned axes = tbox_cq_condition_axes(text);
    /* An unnamed style()-only query asks the parent element, which
     * query->custom_properties stands for; the rest pick a container. */
    const tbox_css_query_container *container = axes != 0 || name.size > 0 ? query->containers : NULL;
    for (; container != NULL; container = container->parent) {
        if (name.size > 0 && !tbox_cq_has_name(container->names, name))
            continue;
        if ((axes & TBOX_CQ_AXIS_INLINE) && container->type == TBOX_CSS_CONTAINER_NORMAL)
            continue;
        if ((axes & TBOX_CQ_AXIS_BLOCK) && container->type != TBOX_CSS_CONTAINER_SIZE)
            continue;
        break;
    }
    if (container == NULL && (axes != 0 || name.size > 0))
        return false;
    if (text.size == 0)
        return name.size > 0; /* `@container card { }`: any such container */
    tbox_cq_env query_env = {
        container,
        container != NULL ? container->custom_properties : query->custom_properties,
        viewport_width,
        viewport_height,
    };
    return tbox_cq_condition(text, &query_env, 0) == TBOX_CQ_TRUE;
}

bool tbox_css_cascade_container_matches(tbox_string_view prelude, const tbox_css_query_context *query, double viewport_width, double viewport_height) {
    if (query == NULL)
        return false;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i <= prelude.size; i++) {
        if (i < prelude.size && prelude.data[i] == '(')
            depth++;
        else if (i < prelude.size && prelude.data[i] == ')')
            depth--;
        else if (i == prelude.size || (prelude.data[i] == ',' && depth == 0)) {
            if (tbox_cq_one(tbox_string_view_make(prelude.data + start, i - start), query, viewport_width, viewport_height))
                return true;
            start = i + 1;
        }
    }
    return false;
}
