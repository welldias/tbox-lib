/* `@supports` conditions (CSS Conditional 3 §6), evaluated once, at parse
 * time: what tbox supports never changes while it runs. A declaration is
 * supported when tbox resolves the property, not when it accepts that
 * exact value -- except for the few values below that would mislead
 * feature detection (3D functions in `transform`...). */

#include <stdbool.h>
#include <string.h>

#include <tbox/css_parser.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "tbox_css_parser.h"
#include "style/tbox_style_internal.h"

/* Every property tbox resolves (docs/referencia/css-suportado.md), plus
 * the print-only ones (break-*, orphans...), which correctly do nothing
 * on a screen. The 3D ones tbox accepts as no-ops (perspective,
 * transform-style, backface-visibility) stay out: answering true would
 * send pages down their 3D path. SVG-only marker* and mask-type also
 * stay out, since the basic SVG subset does not draw markers/masks. Keep
 * in step with that table when a
 * property changes status. */
static const char *const tbox_css_supported_properties[] = {
    "fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin",
    "stroke-miterlimit", "stroke-dasharray", "stroke-dashoffset",
    "fill-rule", "fill-opacity", "stroke-opacity",
    "paint-order",
    "-webkit-text-stroke",
    "-webkit-text-stroke-width",
    "-webkit-text-stroke-color",
    "accent-color",
    "align-content",
    "align-items",
    "align-self",
    "all",
    "animation",
    "animation-delay",
    "animation-direction",
    "animation-duration",
    "animation-fill-mode",
    "animation-iteration-count",
    "animation-name",
    "animation-play-state",
    "animation-timing-function",
    "aspect-ratio",
    "backdrop-filter",
    "background",
    "background-attachment",
    "background-blend-mode",
    "background-clip",
    "background-color",
    "background-image",
    "background-origin",
    "background-position",
    "background-position-x",
    "background-position-y",
    "background-repeat",
    "background-size",
    "block-size",
    "border",
    "border-block",
    "border-block-color",
    "border-block-end",
    "border-block-end-color",
    "border-block-end-style",
    "border-block-end-width",
    "border-block-start",
    "border-block-start-color",
    "border-block-start-style",
    "border-block-start-width",
    "border-block-style",
    "border-block-width",
    "border-bottom",
    "border-bottom-color",
    "border-bottom-left-radius",
    "border-bottom-right-radius",
    "border-bottom-style",
    "border-bottom-width",
    "border-collapse",
    "border-color",
    "border-end-end-radius",
    "border-end-start-radius",
    "border-image",
    "border-image-outset",
    "border-image-repeat",
    "border-image-slice",
    "border-image-source",
    "border-image-width",
    "border-inline",
    "border-inline-color",
    "border-inline-end",
    "border-inline-end-color",
    "border-inline-end-style",
    "border-inline-end-width",
    "border-inline-start",
    "border-inline-start-color",
    "border-inline-start-style",
    "border-inline-start-width",
    "border-inline-style",
    "border-inline-width",
    "border-left",
    "border-left-color",
    "border-left-style",
    "border-left-width",
    "border-radius",
    "border-right",
    "border-right-color",
    "border-right-style",
    "border-right-width",
    "border-spacing",
    "border-start-end-radius",
    "border-start-start-radius",
    "border-style",
    "border-top",
    "border-top-color",
    "border-top-left-radius",
    "border-top-right-radius",
    "border-top-style",
    "border-top-width",
    "border-width",
    "bottom",
    "box-reflect",
    "-webkit-box-reflect",
    "box-decoration-break",
    "-webkit-box-decoration-break",
    "mask-composite",
    "-webkit-mask",
    "-webkit-mask-image",
    "-webkit-mask-size",
    "-webkit-mask-position",
    "-webkit-mask-repeat",
    "-webkit-mask-mode",
    "-webkit-mask-origin",
    "-webkit-mask-clip",
    "-webkit-mask-composite",
    "box-shadow",
    "box-sizing",
    "break-after",
    "break-before",
    "break-inside",
    "caption-side",
    "caret-color",
    "clear",
    "clip",
    "clip-path",
    "color",
    "color-scheme",
    "column-count",
    "column-fill",
    "column-gap",
    "column-rule",
    "column-rule-color",
    "column-rule-style",
    "column-rule-width",
    "column-span",
    "column-width",
    "columns",
    "container",
    "container-name",
    "container-type",
    "content",
    "counter-increment",
    "counter-reset",
    "counter-set",
    "cursor",
    "direction",
    "display",
    "empty-cells",
    "filter",
    "flex",
    "flex-basis",
    "flex-direction",
    "flex-flow",
    "flex-grow",
    "flex-shrink",
    "flex-wrap",
    "float",
    "font",
    "font-family",
    "font-kerning",
    "font-size",
    "font-size-adjust",
    "font-stretch",
    "font-style",
    "font-synthesis",
    "font-feature-settings",
    "font-variant-ligatures",
    "font-variant-numeric",
    "font-variant-east-asian",
    "font-variant-alternates",
    "font-language-override",
    "font-palette",
    "font-variant",
    "font-variant-caps",
    "font-variant-position",
    "font-weight",
    "gap",
    "grid",
    "grid-template",
    "grid-template-columns",
    "grid-template-rows",
    "grid-template-areas",
    "grid-auto-columns",
    "grid-auto-rows",
    "grid-auto-flow",
    "grid-row",
    "grid-column",
    "grid-area",
    "grid-row-start",
    "grid-row-end",
    "grid-column-start",
    "grid-column-end",
    "justify-items",
    "justify-self",
    "height",
    "hanging-punctuation",
    "initial-letter",
    "hyphenate-character",
    "hyphens",
    "image-rendering",
    "inline-size",
    "inset",
    "inset-block",
    "inset-block-end",
    "inset-block-start",
    "inset-inline",
    "inset-inline-end",
    "inset-inline-start",
    "isolation",
    "justify-content",
    "left",
    "letter-spacing",
    "line-break",
    "line-clamp",
    "-webkit-line-clamp",
    "line-height",
    "list-style",
    "list-style-image",
    "list-style-position",
    "list-style-type",
    "margin",
    "margin-block",
    "margin-block-end",
    "margin-block-start",
    "margin-bottom",
    "margin-inline",
    "margin-inline-end",
    "margin-inline-start",
    "margin-left",
    "margin-right",
    "margin-top",
    "mask",
    "mask-clip",
    "mask-image",
    "mask-mode",
    "mask-origin",
    "mask-position",
    "mask-repeat",
    "mask-size",
    "max-block-size",
    "max-height",
    "max-inline-size",
    "max-width",
    "min-block-size",
    "min-height",
    "min-inline-size",
    "min-width",
    "mix-blend-mode",
    "object-fit",
    "object-position",
    "offset",
    "offset-path",
    "offset-distance",
    "offset-position",
    "offset-anchor",
    "offset-rotate",
    "opacity",
    "order",
    "orphans",
    "outline",
    "outline-color",
    "outline-offset",
    "outline-style",
    "outline-width",
    "overflow",
    "overflow-anchor",
    "overflow-wrap",
    "overflow-x",
    "overflow-y",
    "overscroll-behavior",
    "overscroll-behavior-block",
    "overscroll-behavior-inline",
    "overscroll-behavior-x",
    "overscroll-behavior-y",
    "padding",
    "padding-block",
    "padding-block-end",
    "padding-block-start",
    "padding-bottom",
    "padding-inline",
    "padding-inline-end",
    "padding-inline-start",
    "padding-left",
    "padding-right",
    "padding-top",
    "page-break-after",
    "page-break-before",
    "page-break-inside",
    "place-content",
    "place-items",
    "place-self",
    "pointer-events",
    "position",
    "quotes",
    "resize",
    "right",
    "rotate",
    "row-gap",
    "scale",
    "scroll-behavior",
    "scroll-margin",
    "scroll-margin-block",
    "scroll-margin-block-end",
    "scroll-margin-block-start",
    "scroll-margin-bottom",
    "scroll-margin-inline",
    "scroll-margin-inline-end",
    "scroll-margin-inline-start",
    "scroll-margin-left",
    "scroll-margin-right",
    "scroll-margin-top",
    "scroll-padding",
    "scroll-padding-block",
    "scroll-padding-block-end",
    "scroll-padding-block-start",
    "scroll-padding-bottom",
    "scroll-padding-inline",
    "scroll-padding-inline-end",
    "scroll-padding-inline-start",
    "scroll-padding-left",
    "scroll-padding-right",
    "scroll-padding-top",
    "scroll-snap-align",
    "scroll-snap-stop",
    "scroll-snap-type",
    "scrollbar-color",
    "scrollbar-gutter",
    "scrollbar-width",
    "shape-image-threshold",
    "shape-margin",
    "shape-outside",
    "tab-size",
    "table-layout",
    "text-align",
    "text-align-last",
    "text-combine-upright",
    "text-decoration",
    "text-decoration-color",
    "text-decoration-line",
    "text-decoration-style",
    "text-decoration-thickness",
    "text-emphasis",
    "text-emphasis-color",
    "text-emphasis-position",
    "text-emphasis-style",
    "text-indent",
    "text-justify",
    "text-orientation",
    "text-overflow",
    "text-shadow",
    "text-transform",
    "text-underline-offset",
    "text-underline-position",
    "text-wrap",
    "text-wrap-mode",
    "text-wrap-style",
    "top",
    "transform",
    "transform-origin",
    "transition",
    "transition-delay",
    "transition-duration",
    "transition-property",
    "transition-timing-function",
    "translate",
    "unicode-bidi",
    "user-select",
    "vertical-align",
    "visibility",
    "white-space",
    "white-space-collapse",
    "widows",
    "width",
    "word-break",
    "word-spacing",
    "word-wrap",
    "writing-mode",
    "z-index",
    "zoom",
};

/* Values accepted by tbox's `display`. */
static const char *const tbox_css_supported_displays[] = {
    "block",
    "inline",
    "none",
    "inline-block",
    "flex",
    "inline-flex",
    "grid",
    "inline-grid",
    "flow-root",
    "list-item",
    "-webkit-box",
    "block flow-root",
    "inherit",
    "initial",
    "unset",
    "revert",
};

/* Function names that make a value unsupported, per property. */
typedef struct tbox_css_unsupported_function {
    const char *property, *function;
} tbox_css_unsupported_function;

static const tbox_css_unsupported_function tbox_css_unsupported_functions[] = {
    /* 2D transforms are real; the 3D functions are only flattened, so a
     * page testing for them must take its 2D path. A name matches as a
     * prefix (`rotateX` also rejects `rotateXyz(`). */
    { "transform",       "rotateX"     },
    { "transform",       "rotateY"     },
    { "transform",       "rotate3d"    },
    { "transform",       "translateZ"  },
    { "transform",       "translate3d" },
    { "transform",       "scaleZ"      },
    { "transform",       "scale3d"     },
    { "transform",       "matrix3d"    },
    { "transform",       "perspective" },
    { "filter",          "url"         },
    { "backdrop-filter", "url"         },
    { "backdrop-filter", "drop-shadow" },
};

typedef struct tbox_css_supports_cursor {
    const char *at, *end;
    bool valid;
} tbox_css_supports_cursor;

static void tbox_css_supports_skip(tbox_css_supports_cursor *c) {
    while (c->at < c->end && tbox_ascii_is_space(*c->at))
        c->at++;
}

static tbox_string_view tbox_css_supports_trim(const char *start, const char *end) {
    return tbox_string_view_trim_ascii_space(tbox_string_view_make(start, (size_t)(end - start)));
}

/* Case-insensitive `word` at the cursor, followed by a space or '('. */
static bool tbox_css_supports_keyword(const tbox_css_supports_cursor *c, const char *word) {
    size_t length = strlen(word);
    if ((size_t)(c->end - c->at) <= length || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(c->at, length), tbox_string_view_from_cstr(word)))
        return false;
    char next = c->at[length];
    return tbox_ascii_is_space(next) || next == '(';
}

/* The matching ')' for the '(' just before `from`, or NULL. */
static const char *tbox_css_supports_close(const char *from, const char *end) {
    int depth  = 1;
    char quote = 0;
    for (const char *p = from; p < end; p++) {
        if (quote != 0) {
            if (*p == '\\' && p + 1 < end)
                p++;
            else if (*p == quote)
                quote = 0;
        } else if (*p == '"' || *p == '\'')
            quote = *p;
        else if (*p == '(')
            depth++;
        else if (*p == ')' && --depth == 0)
            return p;
    }
    return NULL;
}

/* Pseudo-classes and pseudo-elements the selector matcher knows
 * (src/css_selector/tbox_css_selector_match.c); the parser accepts any. */
static const char *const tbox_css_supported_pseudos[] = {
    "not",
    "lang",
    "first-child",
    "last-child",
    "only-child",
    "root",
    "scope",
    "first-of-type",
    "last-of-type",
    "only-of-type",
    "nth-child",
    "nth-of-type",
    "nth-last-child",
    "nth-last-of-type",
    "empty",
    "disabled",
    "enabled",
    "required",
    "optional",
    "hover",
    "active",
    "focus",
    "focus-visible",
    "focus-within",
    "link",
    "any-link",
    "checked",
    "::before",
    "::after",
    "::marker",
    "::first-line",
    "::first-letter",
    "::placeholder",
};

static bool tbox_css_supports_selector(tbox_string_view text) {
    tbox_arena arena             = tbox_arena_create(0);
    tbox_css_selector *selectors = NULL;
    size_t count = 0, error = 0;
    bool ok = text.size > 0 && tbox_css_parser_parse_standalone_selector_group(text.data, text.size, &arena, &selectors, &count, &error);
    for (size_t i = 0; ok && i < count; i++)
        for (size_t j = 0; ok && j < selectors[i].simple_selector_count; j++) {
            const tbox_css_simple_selector *simple = &selectors[i].simple_selectors[j];
            if (simple->kind != TBOX_CSS_SIMPLE_SELECTOR_PSEUDO)
                continue;
            bool known = false;
            for (size_t k = 0; k < sizeof(tbox_css_supported_pseudos) / sizeof(tbox_css_supported_pseudos[0]) && !known; k++)
                known = tbox_string_view_equal_ascii_ci(simple->name, tbox_string_view_from_cstr(tbox_css_supported_pseudos[k]));
            ok = known;
        }
    tbox_arena_destroy(&arena);
    return ok;
}

static bool tbox_css_supports_ident_char(char c) {
    return c == '-' || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* Some function in `value` has a name starting with `function`
 * (`rotate` also catches rotateZ()). */
static bool tbox_css_supports_uses_function(tbox_string_view value, const char *function) {
    size_t length = strlen(function);
    for (size_t i = 0; i + length < value.size; i++) {
        if (i > 0 && tbox_css_supports_ident_char(value.data[i - 1]))
            continue;
        if (!tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data + i, length), tbox_string_view_from_cstr(function)))
            continue;
        size_t j = i + length;
        while (j < value.size && tbox_css_supports_ident_char(value.data[j]))
            j++;
        if (j < value.size && value.data[j] == '(')
            return true;
    }
    return false;
}

static bool tbox_css_supports_declaration(tbox_string_view name, tbox_string_view value) {
    if (name.size == 0 || value.size == 0)
        return false;
    if (name.size > 2 && name.data[0] == '-' && name.data[1] == '-')
        return true; /* custom property: any value */
    bool known = false;
    for (size_t i = 0; i < sizeof(tbox_css_supported_properties) / sizeof(tbox_css_supported_properties[0]) && !known; i++)
        known = tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(tbox_css_supported_properties[i]));
    if (!known)
        return false;
    if (!tbox_style_paint_declaration_valid(name, value)) return false;
    if (!tbox_style_svg_declaration_valid(name, value)) return false;
    if (!tbox_style_motion_declaration_valid(name, value))
        return false;
    tbox_string_view composition = name;
    if (tbox_string_view_starts_with_ascii_ci(composition, "-webkit-")) composition.data += 8, composition.size -= 8;
    if (tbox_string_view_equal_ascii_ci(composition, tbox_string_view_from_cstr("box-reflect")) ||
        tbox_string_view_equal_ascii_ci(composition, tbox_string_view_from_cstr("box-decoration-break")) ||
        tbox_string_view_equal_ascii_ci(composition, tbox_string_view_from_cstr("mask")) ||
        tbox_string_view_starts_with_ascii_ci(composition, "mask-"))
        return tbox_style_composition_declaration_valid(name, value);
    if (!tbox_style_inline_declaration_valid(name, value))
        return false;
    static const char *const opentype[] = {
        "font-feature-settings", "font-variant-ligatures", "font-variant-numeric",
        "font-variant-east-asian", "font-variant-alternates", "font-language-override",
        "font-palette", "font-variant"
    };
    for (size_t i = 0; i < sizeof(opentype) / sizeof(opentype[0]); i++) {
        if (!tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(opentype[i]))) continue;
        return tbox_style_opentype_declaration_valid(name, value);
    }
    if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr("display"))) {
        for (size_t i = 0; i < sizeof(tbox_css_supported_displays) / sizeof(tbox_css_supported_displays[0]); i++)
            if (tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr(tbox_css_supported_displays[i])))
                return true;
        return false;
    }
    /* text-combine-upright: digits <n> is not supported. */
    if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr("text-combine-upright")) && tbox_string_view_starts_with_ascii_ci(value, "digits"))
        return false;
    /* zoom: reset (the old user-zoom keyword) is not supported. */
    if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr("zoom")) && tbox_string_view_equal_ascii_ci(value, tbox_string_view_from_cstr("reset")))
        return false;
    for (size_t i = 0; i < sizeof(tbox_css_unsupported_functions) / sizeof(tbox_css_unsupported_functions[0]); i++)
        if (tbox_string_view_equal_ascii_ci(name, tbox_string_view_from_cstr(tbox_css_unsupported_functions[i].property)) && tbox_css_supports_uses_function(value, tbox_css_unsupported_functions[i].function))
            return false;
    return true;
}

static bool tbox_css_supports_condition_at(tbox_css_supports_cursor *c);

/* <supports-in-parens>: `( <condition> )`, `( <declaration> )`,
 * `selector( ... )`, or any other function / parenthesized text, which is
 * false (<general-enclosed>). */
static bool tbox_css_supports_in_parens(tbox_css_supports_cursor *c) {
    tbox_css_supports_skip(c);
    if (c->at >= c->end) {
        c->valid = false;
        return false;
    }
    const char *open = c->at;
    while (open < c->end && *open != '(' && !tbox_ascii_is_space(*open))
        open++;
    if (open >= c->end || *open != '(') {
        c->valid = false;
        return false;
    }
    const char *close = tbox_css_supports_close(open + 1, c->end);
    if (close == NULL) {
        c->valid = false;
        return false;
    }
    tbox_string_view function = tbox_string_view_make(c->at, (size_t)(open - c->at));
    tbox_string_view inner    = tbox_css_supports_trim(open + 1, close);
    c->at                     = close + 1;
    if (function.size > 0)
        return tbox_string_view_equal_ascii_ci(function, tbox_string_view_from_cstr("selector")) && tbox_css_supports_selector(inner);
    tbox_css_supports_cursor nested = { inner.data, inner.data + inner.size, true };
    if (inner.size > 0 && (inner.data[0] == '(' || tbox_css_supports_keyword(&nested, "not"))) {
        bool result = tbox_css_supports_condition_at(&nested);
        tbox_css_supports_skip(&nested);
        return nested.valid && nested.at == nested.end && result;
    }
    const char *colon = memchr(inner.data, ':', inner.size);
    if (colon == NULL)
        return false;
    return tbox_css_supports_declaration(tbox_css_supports_trim(inner.data, colon), tbox_css_supports_trim(colon + 1, inner.data + inner.size));
}

/* not <in-parens> | <in-parens> [ and <in-parens> ]* | <in-parens> [ or <in-parens> ]* */
static bool tbox_css_supports_condition_at(tbox_css_supports_cursor *c) {
    tbox_css_supports_skip(c);
    if (tbox_css_supports_keyword(c, "not")) {
        c->at += 3;
        return !tbox_css_supports_in_parens(c);
    }
    bool result = tbox_css_supports_in_parens(c);
    int joiner  = 0; /* 1 and, 2 or */
    for (;;) {
        tbox_css_supports_skip(c);
        if (c->at >= c->end || !c->valid)
            break;
        int next = tbox_css_supports_keyword(c, "and") ? 1 : tbox_css_supports_keyword(c, "or") ? 2 : 0;
        if (next == 0 || (joiner != 0 && next != joiner)) {
            c->valid = false; /* mixing and/or needs parentheses */
            break;
        }
        joiner = next;
        c->at += next == 1 ? 3 : 2;
        bool operand = tbox_css_supports_in_parens(c);
        result       = next == 1 ? result && operand : result || operand;
    }
    return result;
}

const char *tbox_css_supported_property(size_t index) {
    return index < sizeof(tbox_css_supported_properties) / sizeof(tbox_css_supported_properties[0]) ? tbox_css_supported_properties[index] : NULL;
}

bool tbox_css_supports(tbox_string_view condition) {
    tbox_css_supports_cursor c = { condition.data, condition.data + condition.size, condition.data != NULL };
    bool result                = c.valid && tbox_css_supports_condition_at(&c);
    tbox_css_supports_skip(&c);
    return c.valid && c.at == c.end && result;
}
