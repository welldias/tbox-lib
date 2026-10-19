#include "tbox_style_internal.h"

#include <string.h>

#include "base/tbox_string.h"

/* A group permits one keyword; the selected keyword supplies the feature.
 * Bits are stable only inside tbox_style, not part of the CSS API. */
static const struct {
    const char *name, *tag;
    unsigned property, group, value;
} tbox_style_ot_keywords[] = {
    { "common-ligatures",           "liga", 0, 0,  1 },
    { "no-common-ligatures",        "liga", 0, 0,  0 },
    { "discretionary-ligatures",    "dlig", 0, 1,  1 },
    { "no-discretionary-ligatures", "dlig", 0, 1,  0 },
    { "historical-ligatures",       "hlig", 0, 2,  1 },
    { "no-historical-ligatures",    "hlig", 0, 2,  0 },
    { "contextual",                 "calt", 0, 3,  1 },
    { "no-contextual",              "calt", 0, 3,  0 },
    { "lining-nums",                "lnum", 1, 4,  1 },
    { "oldstyle-nums",              "onum", 1, 4,  1 },
    { "proportional-nums",          "pnum", 1, 5,  1 },
    { "tabular-nums",               "tnum", 1, 5,  1 },
    { "diagonal-fractions",         "frac", 1, 6,  1 },
    { "stacked-fractions",          "afrc", 1, 6,  1 },
    { "ordinal",                    "ordn", 1, 7,  1 },
    { "slashed-zero",               "zero", 1, 8,  1 },
    { "jis78",                      "jp78", 2, 9,  1 },
    { "jis83",                      "jp83", 2, 9,  1 },
    { "jis90",                      "jp90", 2, 9,  1 },
    { "jis04",                      "jp04", 2, 9,  1 },
    { "simplified",                 "smpl", 2, 9,  1 },
    { "traditional",                "trad", 2, 9,  1 },
    { "full-width",                 "fwid", 2, 10, 1 },
    { "proportional-width",         "pwid", 2, 10, 1 },
    { "ruby",                       "ruby", 2, 11, 1 },
};

static uint32_t tbox_style_ot_tag(const char *s) {
    return (uint32_t)(unsigned char)s[0] << 24 | (uint32_t)(unsigned char)s[1] << 16 | (uint32_t)(unsigned char)s[2] << 8 | (uint32_t)(unsigned char)s[3];
}

/* CSS string, decoded into printable ASCII only (OpenType tags). */
static bool tbox_style_ot_string(tbox_string_view *input, char out[4], size_t *length) {
    tbox_string_view s = *input;
    if (s.size < 2 || (s.data[0] != '\'' && s.data[0] != '"'))
        return false;
    size_t at = 1, n = 0;
    while (at < s.size && s.data[at] != s.data[0]) {
        unsigned c = (unsigned char)s.data[at++];
        if (c == '\\') {
            if (at == s.size)
                return false;
            c = (unsigned char)s.data[at++];
            if (c == '\n' || c == '\r' || c == '\f') {
                if (c == '\r' && at < s.size && s.data[at] == '\n')
                    at++;
                continue;
            }
            unsigned hex = 0, digits = 0;
            at--;
            while (at < s.size && digits < 6) {
                unsigned d = (unsigned char)s.data[at];
                if (d >= '0' && d <= '9')
                    d -= '0';
                else if (d >= 'a' && d <= 'f')
                    d = d - 'a' + 10;
                else if (d >= 'A' && d <= 'F')
                    d = d - 'A' + 10;
                else
                    break;
                hex = hex * 16 + d;
                digits++;
                at++;
            }
            if (digits > 0) {
                c = hex;
                if (at < s.size && tbox_ascii_is_space(s.data[at])) {
                    char space = s.data[at++];
                    if (space == '\r' && at < s.size && s.data[at] == '\n')
                        at++;
                }
            } else
                c = (unsigned char)s.data[at++];
        }
        if (c < 0x20 || c > 0x7e || n == 4)
            return false;
        out[n++] = (char)c;
    }
    if (at == s.size)
        return false;
    *input  = tbox_string_view_trim_ascii_space(tbox_string_view_make(s.data + at + 1, s.size - at - 1));
    *length = n;
    return true;
}

static bool tbox_style_ot_settings(tbox_string_view s, tbox_font_feature_settings *out) {
    *out = (tbox_font_feature_settings){ 0 };
    if (tbox_style_is(s, "normal"))
        return true;
    while (s.size > 0) {
        char tag[4];
        size_t length;
        if (!tbox_style_ot_string(&s, tag, &length) || length != 4)
            return false;
        size_t end = 0;
        while (end < s.size && s.data[end] != ',')
            end++;
        tbox_string_view value = tbox_string_view_trim_ascii_space(tbox_string_view_make(s.data, end));
        double number          = 1;
        if (tbox_style_is(value, "off"))
            number = 0;
        else if (value.size != 0 && !tbox_style_is(value, "on")) {
            size_t at = value.data[0] == '+' || value.data[0] == '-' ? 1 : 0;
            if (at == value.size)
                return false;
            for (; at < value.size; at++)
                if (value.data[at] < '0' || value.data[at] > '9')
                    return false;
            if (!tbox_style_parse_number(value, &number) || number < 0 || number > UINT32_MAX)
                return false;
        }
        uint32_t key = tbox_style_ot_tag(tag);
        size_t i     = 0;
        while (i < out->count && out->items[i].tag != key)
            i++;
        if (i == out->count) {
            if (out->count == TBOX_FONT_MAX_FEATURE_SETTINGS)
                return false;
            out->count++;
        }
        out->items[i] = (tbox_font_feature){ key, (uint32_t)number };
        if (end == s.size)
            return true;
        s = tbox_string_view_trim_ascii_space(tbox_string_view_make(s.data + end + 1, s.size - end - 1));
        if (s.size == 0)
            return false;
    }
    return false;
}

static bool tbox_style_ot_ident(tbox_string_view s) {
    if (s.size == 0)
        return false;
    for (size_t i = 0; i < s.size; i++) {
        char c = s.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-' || (i > 0 && c >= '0' && c <= '9') || (unsigned char)c >= 128))
            return false;
    }
    return true;
}

static bool tbox_style_ot_alternate(tbox_string_view token, unsigned *seen) {
    static const char *const functions[] = { "stylistic", "styleset", "character-variant", "swash", "ornaments", "annotation" };
    if (tbox_style_is(token, "historical-forms")) {
        if (*seen & 1u)
            return false;
        *seen |= 1u;
        return true;
    }
    for (size_t i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
        tbox_string_view args;
        if (!tbox_style_function_args(token, functions[i], &args))
            continue;
        unsigned bit = 2u << i;
        if (*seen & bit)
            return false;
        tbox_string_view names[64];
        size_t count = tbox_style_split_top_level(args, ',', names, 64);
        if (count == 0 || count > 64 || ((i != 1 && i != 2) && count != 1))
            return false;
        for (size_t j = 0; j < count; j++)
            if (!tbox_style_ot_ident(tbox_string_view_trim_ascii_space(names[j])))
                return false;
        *seen |= bit;
        return true;
    }
    return false;
}

bool tbox_style_parse_opentype(const char *property, tbox_string_view value, tbox_style *style) {
    value = tbox_string_view_trim_ascii_space(value);
    if (strcmp(property, "font-feature-settings") == 0) {
        tbox_font_feature_settings settings;
        if (!tbox_style_ot_settings(value, &settings))
            return false;
        style->font_feature_settings = settings;
        return true;
    }
    if (strcmp(property, "font-language-override") == 0) {
        uint32_t language = 0;
        if (!tbox_style_is(value, "normal")) {
            char tag[4] = { ' ', ' ', ' ', ' ' };
            size_t length;
            if (!tbox_style_ot_string(&value, tag, &length) || length == 0 || value.size != 0)
                return false;
            language = tbox_style_ot_tag(tag);
        }
        style->font_language_override = language;
        return true;
    }
    if (strcmp(property, "font-palette") == 0) {
        if (tbox_style_is(value, "normal"))
            style->font_palette[0] = '\0';
        else if (tbox_style_is(value, "light") || tbox_style_is(value, "dark") || (value.size > 2 && value.data[0] == '-' && value.data[1] == '-' && tbox_style_ot_ident(value))) {
            if (value.size >= sizeof(style->font_palette))
                return false;
            tbox_utf8_copy_truncated(style->font_palette, sizeof(style->font_palette), value);
        } else
            return false;
        return true;
    }
    bool shorthand = strcmp(property, "font-variant") == 0;
    int prop       = strcmp(property, "font-variant-ligatures") == 0 ? 0 : strcmp(property, "font-variant-numeric") == 0 ? 1 : strcmp(property, "font-variant-east-asian") == 0 ? 2 : strcmp(property, "font-variant-alternates") == 0 ? 3 : strcmp(property, "font-variant-caps") == 0 ? 4 : strcmp(property, "font-variant-position") == 0 ? 5 : -1;
    if (!shorthand && prop < 0)
        return false;
    uint32_t bits[3] = { 0 };
    unsigned alternates = 0;
    bool caps                                 = false;
    tbox_style_font_variant_position position = TBOX_STYLE_FONT_VARIANT_POSITION_NORMAL;
    if (tbox_style_is(value, "none")) {
        if (!shorthand && prop != 0)
            return false;
        bits[0] = (1u << 1) | (1u << 3) | (1u << 5) | (1u << 7);
    } else if (!tbox_style_is(value, "normal")) {
        tbox_string_view tokens[64];
        size_t count = tbox_style_tokens(value, tokens, 64);
        if (count == 0 || count > 64)
            return false;
        unsigned groups = 0;
        for (size_t k = 0; k < count; k++) {
            bool found = false;
            for (size_t i = 0; i < sizeof(tbox_style_ot_keywords) / sizeof(tbox_style_ot_keywords[0]); i++) {
                if ((!shorthand && prop != (int)tbox_style_ot_keywords[i].property) || !tbox_style_is(tokens[k], tbox_style_ot_keywords[i].name))
                    continue;
                unsigned group = 1u << tbox_style_ot_keywords[i].group;
                if (groups & group)
                    return false;
                groups |= group;
                bits[tbox_style_ot_keywords[i].property] |= 1u << i;
                found = true;
                break;
            }
            if (found)
                continue;
            if ((shorthand || prop == 4) && (tbox_style_is(tokens[k], "small-caps") || tbox_style_is(tokens[k], "all-small-caps"))) {
                if (caps)
                    return false;
                caps = true;
            } else if ((shorthand || prop == 5) && (tbox_style_is(tokens[k], "sub") || tbox_style_is(tokens[k], "super"))) {
                if (position != TBOX_STYLE_FONT_VARIANT_POSITION_NORMAL)
                    return false;
                position = tbox_style_is(tokens[k], "sub") ? TBOX_STYLE_FONT_VARIANT_POSITION_SUB : TBOX_STYLE_FONT_VARIANT_POSITION_SUPER;
            } else if (!(shorthand || prop == 3) || !tbox_style_ot_alternate(tokens[k], &alternates))
                return false;
        }
    }
    if (shorthand || prop == 0)
        style->font_variant_ligatures = bits[0];
    if (shorthand || prop == 1)
        style->font_variant_numeric = bits[1];
    if (shorthand || prop == 2)
        style->font_variant_east_asian = bits[2];
    if (shorthand || prop == 3)
        style->font_variant_alternates = (alternates & 1u) != 0;
    if (shorthand || prop == 4)
        style->font_small_caps = caps;
    if (shorthand || prop == 5)
        style->font_variant_position = position;
    return true;
}

/* Discard invalid literal declarations before they can replace an earlier
 * valid value. var() is checked after substitution by the style resolver. */
bool tbox_style_opentype_declaration_valid(tbox_string_view property, tbox_string_view value) {
    static const char *const properties[] = { "font-feature-settings", "font-variant-ligatures", "font-variant-numeric", "font-variant-east-asian", "font-variant-alternates", "font-language-override", "font-palette", "font-variant" };
    const char *name                      = NULL;
    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++)
        if (tbox_string_view_equal_ascii_ci(property, tbox_string_view_from_cstr(properties[i])))
            name = properties[i];
    if (name == NULL)
        return true;
    if (tbox_style_is(value, "initial") || tbox_style_is(value, "inherit") || tbox_style_is(value, "unset") || tbox_style_is(value, "revert") || tbox_style_is(value, "revert-layer"))
        return true;
    for (size_t i = 0; i + 4 <= value.size; i++)
        if (tbox_string_view_equal_ascii_ci(tbox_string_view_make(value.data + i, 4), tbox_string_view_from_cstr("var(")))
            return true;
    tbox_style scratch = { 0 };
    return tbox_style_parse_opentype(name, value, &scratch);
}

static void tbox_style_ot_add(tbox_font_shaping_options *out, uint32_t tag, uint32_t value) {
    size_t i = 0;
    while (i < out->count && out->items[i].tag != tag)
        i++;
    if (i == out->count)
        out->count++;
    out->items[i] = (tbox_font_feature){ tag, value };
}

void tbox_style_shaping_options(const tbox_style *style, tbox_font_shaping_options *out) {
    *out          = (tbox_font_shaping_options){ 0 };
    /* Vertical text: sideways-* modes keep every glyph sideways, so only
     * vertical-rl and vertical-lr shape upright glyphs. */
    if (style->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_RL || style->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_LR) {
        out->vertical_frame       = style->writing_mode == TBOX_STYLE_WRITING_MODE_VERTICAL_RL ? TBOX_FONT_VERTICAL_ROTATED : TBOX_FONT_VERTICAL_TRANSPOSED;
        out->vertical_orientation = style->text_combine_upright ? TBOX_FONT_VERTICAL_COMBINE :
                                    style->text_orientation == TBOX_STYLE_TEXT_ORIENTATION_UPRIGHT ? TBOX_FONT_VERTICAL_UPRIGHT :
                                    style->text_orientation == TBOX_STYLE_TEXT_ORIENTATION_SIDEWAYS ? TBOX_FONT_VERTICAL_SIDEWAYS : TBOX_FONT_VERTICAL_MIXED;
    }
    uint32_t bits = style->font_variant_ligatures | style->font_variant_numeric | style->font_variant_east_asian;
    for (size_t i = 0; i < sizeof(tbox_style_ot_keywords) / sizeof(tbox_style_ot_keywords[0]); i++) {
        if (!(bits & (1u << i)))
            continue;
        unsigned group = tbox_style_ot_keywords[i].group;
        /* Explicitly disable alternative forms in the same exclusive group. */
        if (group >= 4)
            for (size_t j = 0; j < sizeof(tbox_style_ot_keywords) / sizeof(tbox_style_ot_keywords[0]); j++)
                if (j != i && tbox_style_ot_keywords[j].group == group)
                    tbox_style_ot_add(out, tbox_style_ot_tag(tbox_style_ot_keywords[j].tag), 0);
        tbox_style_ot_add(out, tbox_style_ot_tag(tbox_style_ot_keywords[i].tag), tbox_style_ot_keywords[i].value);
        if (group == 0)
            tbox_style_ot_add(out, tbox_style_ot_tag("clig"), tbox_style_ot_keywords[i].value);
    }
    if (style->font_variant_alternates)
        tbox_style_ot_add(out, tbox_style_ot_tag("hist"), 1);
    for (size_t i = 0; i < style->font_feature_settings.count; i++)
        tbox_style_ot_add(out, style->font_feature_settings.items[i].tag, style->font_feature_settings.items[i].value);
    out->language = style->font_language_override;
}

/* Resolve each field against both shorthands before copying it. A valid
 * font declaration resets even the settings it cannot express. */
void tbox_style_resolve_opentype(const tbox_css_computed_style *computed, const tbox_style *parent, const tbox_css_resolved_declaration *font, tbox_string_view font_variant, tbox_style *style) {
    static const struct {
        const char *name;
        size_t offset, size;
        bool variant;
    } fields[] = {
#define FIELD(name, member, variant) { name, offsetof(tbox_style, member), sizeof(((tbox_style *)0)->member), variant }
        FIELD("font-feature-settings", font_feature_settings, false),
        FIELD("font-language-override", font_language_override, false),
        FIELD("font-palette", font_palette, false),
        FIELD("font-variant-ligatures", font_variant_ligatures, true),
        FIELD("font-variant-numeric", font_variant_numeric, true),
        FIELD("font-variant-east-asian", font_variant_east_asian, true),
        FIELD("font-variant-alternates", font_variant_alternates, true),
        FIELD("font-variant-caps", font_small_caps, true),
        FIELD("font-variant-position", font_variant_position, true),
#undef FIELD
    };
    tbox_style variant                             = { 0 };
    const tbox_css_resolved_declaration *shorthand = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr("font-variant"));
    if (shorthand != NULL && !tbox_style_parse_opentype("font-variant", shorthand->value, &variant))
        shorthand = NULL;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        char *dst = (char *)style + fields[i].offset;
        if (parent != NULL)
            memcpy(dst, (const char *)parent + fields[i].offset, fields[i].size);
        else
            memset(dst, 0, fields[i].size);
        const tbox_css_resolved_declaration *winner = NULL;
        if (font != NULL) {
            memset(dst, 0, fields[i].size);
            if (strcmp(fields[i].name, "font-variant-caps") == 0)
                style->font_small_caps = tbox_style_is(font_variant, "small-caps");
            winner = font;
        }
        if (fields[i].variant && shorthand != NULL && (winner == NULL || tbox_css_cascade_priority_compare(shorthand, winner) > 0)) {
            memcpy(dst, (const char *)&variant + fields[i].offset, fields[i].size);
            winner = shorthand;
        }
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(fields[i].name));
        if (decl != NULL && (winner == NULL || tbox_css_cascade_priority_compare(decl, winner) > 0))
            tbox_style_parse_opentype(fields[i].name, decl->value, style);
    }
}
