/* CSS Inline 3 initial-letter and CSS Text 3 hanging-punctuation. */
#include "base/tbox_string.h"
#include "tbox_style_internal.h"
#include <limits.h>
#include <math.h>

bool tbox_style_parse_initial_letter(tbox_string_view value, double *size, int *sink) {
    if (tbox_style_is(value, "normal")) {
        *size = 0.0;
        *sink = 0;
        return true;
    }
    tbox_string_view tokens[3];
    size_t count = tbox_style_tokens(value, tokens, 3);
    double number;
    if (count < 1 || count > 2 || !tbox_style_parse_number(tokens[0], &number) || !isfinite(number) || number < 1.0 || number > INT_MAX)
        return false;
    int lines = (int)floor(number);
    if (count == 2) {
        if (tbox_style_is(tokens[1], "raise"))
            lines = 1;
        else if (!tbox_style_is(tokens[1], "drop")) {
            tbox_string_view integer = tokens[1];
            size_t at                = integer.size > 0 && (integer.data[0] == '+' || integer.data[0] == '-') ? 1 : 0;
            if (at == integer.size)
                return false;
            for (; at < integer.size; at++)
                if (integer.data[at] < '0' || integer.data[at] > '9')
                    return false;
            double parsed;
            if (!tbox_style_parse_number(integer, &parsed) || !isfinite(parsed) || parsed < 1.0 || parsed > INT_MAX)
                return false;
            lines = (int)parsed;
        }
    }
    *size = number;
    *sink = lines;
    return true;
}

bool tbox_style_parse_hanging_punctuation(tbox_string_view value, unsigned *bits) {
    if (tbox_style_is(value, "none")) {
        *bits = 0;
        return true;
    }
    tbox_string_view tokens[4];
    size_t count    = tbox_style_tokens(value, tokens, 4);
    unsigned parsed = 0;
    if (count < 1 || count > 3)
        return false;
    for (size_t i = 0; i < count; i++) {
        unsigned bit;
        if (tbox_style_is(tokens[i], "first"))
            bit = TBOX_STYLE_HANG_FIRST;
        else if (tbox_style_is(tokens[i], "last"))
            bit = TBOX_STYLE_HANG_LAST;
        else if (tbox_style_is(tokens[i], "force-end"))
            bit = TBOX_STYLE_HANG_FORCE_END;
        else if (tbox_style_is(tokens[i], "allow-end"))
            bit = TBOX_STYLE_HANG_ALLOW_END;
        else
            return false;
        if ((parsed & bit) != 0 || ((bit & (TBOX_STYLE_HANG_FORCE_END | TBOX_STYLE_HANG_ALLOW_END)) != 0 && (parsed & (TBOX_STYLE_HANG_FORCE_END | TBOX_STYLE_HANG_ALLOW_END)) != 0))
            return false;
        parsed |= bit;
    }
    *bits = parsed;
    return true;
}

bool tbox_style_inline_declaration_valid(tbox_string_view property, tbox_string_view value) {
    bool initial = tbox_style_is(property, "initial-letter");
    if (!initial && !tbox_style_is(property, "hanging-punctuation"))
        return true;
    if (tbox_style_is(value, "initial") || tbox_style_is(value, "inherit") || tbox_style_is(value, "unset") || tbox_style_is(value, "revert") || tbox_style_is(value, "revert-layer"))
        return true;
    for (size_t i = 0; i + 4 <= value.size; i++)
        if (tbox_style_is(tbox_string_view_make(value.data + i, 4), "var("))
            return true;
    double size;
    int sink;
    unsigned bits;
    return initial ? tbox_style_parse_initial_letter(value, &size, &sink) : tbox_style_parse_hanging_punctuation(value, &bits);
}

void tbox_style_resolve_inline(const tbox_css_computed_style *computed, const tbox_style *parent, tbox_style *style) {
    style->hanging_punctuation                = parent != NULL ? parent->hanging_punctuation : 0;
    const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("initial-letter"));
    if (decl != NULL)
        tbox_style_parse_initial_letter(decl->value, &style->initial_letter_size, &style->initial_letter_sink);
    decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("hanging-punctuation"));
    if (decl != NULL)
        tbox_style_parse_hanging_punctuation(decl->value, &style->hanging_punctuation);
}
