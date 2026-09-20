#include "tbox_html_entities.h"

#include <stdbool.h>
#include <stddef.h>

#include "base/tbox_string.h"

typedef struct tbox_html_entity {
    const char *name;
    int codepoint;
} tbox_html_entity;

/* Fixed set of ~23 common named entities -- see ARCHITECTURE.md's v6
 * "Escopo" for the exact list and rationale (not the full ~2000-entry HTML5
 * named character reference table). */
static const tbox_html_entity tbox_html_entities[] = {
    {"amp", 0x26},    {"lt", 0x3C},     {"gt", 0x3E},     {"quot", 0x22},   {"apos", 0x27},  {"nbsp", 0xA0},
    {"copy", 0xA9},   {"reg", 0xAE},    {"trade", 0x2122}, {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026},
    {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"euro", 0x20AC}, {"pound", 0xA3},
    {"yen", 0xA5},    {"cent", 0xA2},   {"sect", 0xA7},   {"para", 0xB6},   {"middot", 0xB7}, {"deg", 0xB0},
};

/* `start` points right after "&#". On success, *out_end is the index right
 * after the consumed ';' and *out_codepoint is the (already-substituted, if
 * needed) codepoint. */
static bool tbox_html_parse_numeric_reference(tbox_string_view text, size_t start, size_t *out_end, int *out_codepoint) {
    size_t i    = start;
    bool is_hex = false;
    if (i < text.size && (text.data[i] == 'x' || text.data[i] == 'X')) {
        is_hex = true;
        i++;
    }

    size_t digit_start        = i;
    unsigned long value       = 0;
    bool overflowed           = false;
    while (i < text.size) {
        char c = text.data[i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (is_hex && c >= 'a' && c <= 'f') {
            digit = 10 + (c - 'a');
        } else if (is_hex && c >= 'A' && c <= 'F') {
            digit = 10 + (c - 'A');
        } else {
            break;
        }

        if (value > 0x10FFFFul) {
            overflowed = true;
        } else {
            value = value * (unsigned long)(is_hex ? 16 : 10) + (unsigned long)digit;
        }
        i++;
    }

    if (i == digit_start) {
        return false; /* no digits */
    }
    if (i >= text.size || text.data[i] != ';') {
        return false; /* missing trailing ';' */
    }

    *out_end = i + 1;
    if (overflowed || value > 0x10FFFFul || value == 0 || (value >= 0xD800ul && value <= 0xDFFFul)) {
        *out_codepoint = 0xFFFD;
    } else {
        *out_codepoint = (int)value;
    }
    return true;
}

/* `start` points right after "&". On success, *out_end is the index right
 * after the consumed ';' and *out_codepoint is the entity's codepoint. */
static bool tbox_html_parse_named_reference(tbox_string_view text, size_t start, size_t *out_end, int *out_codepoint) {
    size_t i = start;
    while (i < text.size && ((text.data[i] >= 'a' && text.data[i] <= 'z') || (text.data[i] >= 'A' && text.data[i] <= 'Z'))) {
        i++;
    }
    if (i == start) {
        return false; /* no letters */
    }
    if (i >= text.size || text.data[i] != ';') {
        return false; /* missing trailing ';' */
    }

    tbox_string_view name = tbox_string_view_make(text.data + start, i - start);
    for (size_t k = 0; k < sizeof(tbox_html_entities) / sizeof(tbox_html_entities[0]); k++) {
        if (tbox_string_view_equal_cstr(name, tbox_html_entities[k].name)) {
            *out_codepoint = tbox_html_entities[k].codepoint;
            *out_end       = i + 1;
            return true;
        }
    }
    return false;
}

tbox_string_view tbox_html_decode_entities(tbox_arena *arena, tbox_string_view text) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, text.size);

    size_t pos = 0;
    while (pos < text.size) {
        size_t amp = pos;
        while (amp < text.size && text.data[amp] != '&') {
            amp++;
        }
        if (amp > pos) {
            tbox_string_builder_append_view(&builder, tbox_string_view_make(text.data + pos, amp - pos));
        }
        if (amp >= text.size) {
            break;
        }

        bool handled = false;
        size_t end;
        int codepoint;
        if (amp + 1 < text.size && text.data[amp + 1] == '#') {
            if (tbox_html_parse_numeric_reference(text, amp + 2, &end, &codepoint)) {
                tbox_string_builder_append_codepoint(&builder, codepoint);
                pos     = end;
                handled = true;
            }
        } else if (tbox_html_parse_named_reference(text, amp + 1, &end, &codepoint)) {
            tbox_string_builder_append_codepoint(&builder, codepoint);
            pos     = end;
            handled = true;
        }

        if (!handled) {
            tbox_string_builder_append_byte(&builder, '&');
            pos = amp + 1;
        }
    }

    return tbox_string_builder_finish(&builder);
}
