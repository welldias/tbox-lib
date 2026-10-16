#include "tbox_string.h"

#include <string.h>

#include "utf8.h"

tbox_string_view tbox_string_view_make(const char *data, size_t size) {
    return (tbox_string_view){ .data = data, .size = size };
}

bool tbox_string_view_empty(tbox_string_view view) {
    return view.size == 0;
}

bool tbox_string_view_equal(tbox_string_view a, tbox_string_view b) {
    if (a.size != b.size) {
        return false;
    }
    if (a.size == 0) {
        return true;
    }
    return memcmp(a.data, b.data, a.size) == 0;
}

bool tbox_string_view_equal_cstr(tbox_string_view view, const char *cstr) {
    return tbox_string_view_equal(view, tbox_string_view_from_cstr(cstr));
}

tbox_string_view tbox_string_view_from_cstr(const char *nul_terminated) {
    return tbox_string_view_make(nul_terminated, utf8size_lazy(nul_terminated));
}

size_t tbox_string_view_codepoint_count(tbox_string_view view) {
    size_t count = 0;
    for (size_t at = 0; at < view.size; at = tbox_utf8_next(view.data, view.size, at))
        count++;
    return count;
}

/* Length of the sequence at data[at] and whether it is well formed. A
 * malformed one spans its maximal subpart: the lead byte plus every
 * continuation byte that was still acceptable (at least one byte). */
static size_t tbox_utf8_scan(const char *data, size_t length, size_t at, uint32_t *out_codepoint, bool *out_valid) {
    const unsigned char *s = (const unsigned char *)data + at;
    size_t left            = length - at;
    unsigned char b        = s[0];
    *out_valid             = true;
    if (b < 0x80) {
        *out_codepoint = b;
        return 1;
    }
    size_t need;
    uint32_t codepoint;
    unsigned char lower = 0x80, upper = 0xBF;
    if (b >= 0xC2 && b <= 0xDF) {
        need      = 1;
        codepoint = b & 0x1Fu;
    } else if (b >= 0xE0 && b <= 0xEF) {
        need      = 2;
        codepoint = b & 0x0Fu;
        if (b == 0xE0) lower = 0xA0;
        if (b == 0xED) upper = 0x9F; /* no surrogates */
    } else if (b >= 0xF0 && b <= 0xF4) {
        need      = 3;
        codepoint = b & 0x07u;
        if (b == 0xF0) lower = 0x90;
        if (b == 0xF4) upper = 0x8F; /* <= U+10FFFF */
    } else {
        *out_valid     = false;
        *out_codepoint = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i <= need; i++) {
        if (i >= left || s[i] < lower || s[i] > upper) {
            *out_valid     = false;
            *out_codepoint = 0xFFFD;
            return i;
        }
        codepoint = (codepoint << 6) | (s[i] & 0x3Fu);
        lower     = 0x80;
        upper     = 0xBF;
    }
    *out_codepoint = codepoint;
    return need + 1;
}

size_t tbox_utf8_decode(const char *data, size_t length, size_t at, uint32_t *out_codepoint) {
    bool valid;
    return at + tbox_utf8_scan(data, length, at, out_codepoint, &valid);
}

size_t tbox_utf8_next(const char *data, size_t length, size_t at) {
    if (at >= length) return length;
    uint32_t codepoint;
    return tbox_utf8_decode(data, length, at, &codepoint);
}

size_t tbox_utf8_previous(const char *data, size_t at) {
    if (at == 0) return 0;
    /* Back over up to three continuation bytes to the lead byte; if that
     * sequence does not end exactly at `at` (malformed text), step one
     * byte, matching how tbox_utf8_next consumes stray bytes. */
    size_t start = at - 1;
    while (start > 0 && at - start < 4 && ((unsigned char)data[start] & 0xC0u) == 0x80u)
        start--;
    return tbox_utf8_next(data, at, start) == at ? start : at - 1;
}

bool tbox_string_view_valid_utf8(tbox_string_view view) {
    for (size_t at = 0; at < view.size;) {
        uint32_t codepoint;
        bool valid;
        at += tbox_utf8_scan(view.data, view.size, at, &codepoint, &valid);
        if (!valid) return false;
    }
    return true;
}

size_t tbox_utf8_encode(char *out, size_t capacity, uint32_t codepoint) {
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        codepoint = 0xFFFD;
    size_t size = utf8codepointsize((utf8_int32_t)codepoint);
    if (size > capacity)
        return 0;
    utf8catcodepoint(out, (utf8_int32_t)codepoint, size);
    return size;
}

size_t tbox_utf8_copy_truncated(char *out, size_t capacity, tbox_string_view text) {
    size_t length = 0;
    while (length < text.size) {
        size_t next = tbox_utf8_next(text.data, text.size, length);
        if (next > capacity - 1)
            break;
        length = next;
    }
    if (length > 0)
        memcpy(out, text.data, length);
    out[length] = '\0';
    return length;
}

uint32_t tbox_utf8_to_upper(uint32_t codepoint) {
    return (uint32_t)utf8uprcodepoint((utf8_int32_t)codepoint);
}

uint32_t tbox_utf8_to_lower(uint32_t codepoint) {
    return (uint32_t)utf8lwrcodepoint((utf8_int32_t)codepoint);
}

tbox_string_view tbox_utf8_sanitize(tbox_arena *arena, tbox_string_view text) {
    if (text.data == NULL || tbox_string_view_valid_utf8(text))
        return text;
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, text.size + 8);
    for (size_t at = 0; at < text.size;) {
        uint32_t codepoint;
        bool valid;
        size_t width = tbox_utf8_scan(text.data, text.size, at, &codepoint, &valid);
        if (valid)
            tbox_string_builder_append_view(&builder, tbox_string_view_make(text.data + at, width));
        else
            tbox_string_builder_append_view(&builder, tbox_string_view_make("\xEF\xBF\xBD", 3));
        at += width;
    }
    return tbox_string_builder_finish(&builder);
}

char tbox_ascii_to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool tbox_ascii_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool tbox_ascii_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

tbox_string_view tbox_string_view_trim_ascii_space(tbox_string_view view) {
    size_t start = 0, end = view.size;
    while (start < end && tbox_ascii_is_space(view.data[start]))
        start++;
    while (end > start && tbox_ascii_is_space(view.data[end - 1]))
        end--;
    return tbox_string_view_make(view.data + start, end - start);
}

bool tbox_string_view_starts_with_ascii_ci(tbox_string_view view, const char *prefix) {
    size_t length = strlen(prefix);
    return view.size >= length && tbox_string_view_equal_ascii_ci(tbox_string_view_make(view.data, length), tbox_string_view_make(prefix, length));
}

tbox_string_view tbox_string_copy_lower_ascii(tbox_arena *arena, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, view.size);
    tbox_string_builder_append_view_lower_ascii(&builder, view);
    return tbox_string_builder_finish(&builder);
}

bool tbox_string_view_equal_ascii_ci(tbox_string_view a, tbox_string_view b) {
    if (a.size != b.size) {
        return false;
    }
    for (size_t i = 0; i < a.size; i++) {
        if (tbox_ascii_to_lower(a.data[i]) != tbox_ascii_to_lower(b.data[i])) {
            return false;
        }
    }
    return true;
}

#define TBOX_STRING_BUILDER_DEFAULT_CAPACITY ((size_t)32)

void tbox_string_builder_init(tbox_string_builder *builder, tbox_arena *arena, size_t initial_capacity) {
    builder->arena    = arena;
    builder->length   = 0;
    builder->capacity = initial_capacity > 0 ? initial_capacity : TBOX_STRING_BUILDER_DEFAULT_CAPACITY;
    builder->data     = tbox_arena_alloc(arena, builder->capacity);
}

static void tbox_string_builder_reserve(tbox_string_builder *builder, size_t additional) {
    if (builder->length + additional <= builder->capacity) {
        return;
    }

    size_t new_capacity = builder->capacity == 0 ? TBOX_STRING_BUILDER_DEFAULT_CAPACITY : builder->capacity * 2;
    while (new_capacity < builder->length + additional) {
        new_capacity *= 2;
    }

    char *new_data = tbox_arena_alloc(builder->arena, new_capacity);
    if (builder->length > 0) {
        memcpy(new_data, builder->data, builder->length);
    }

    builder->data     = new_data;
    builder->capacity = new_capacity;
}

void tbox_string_builder_append_byte(tbox_string_builder *builder, char byte) {
    tbox_string_builder_reserve(builder, 1);
    builder->data[builder->length++] = byte;
}

void tbox_string_builder_append_view(tbox_string_builder *builder, tbox_string_view view) {
    if (view.size == 0) {
        return;
    }
    tbox_string_builder_reserve(builder, view.size);
    memcpy(builder->data + builder->length, view.data, view.size);
    builder->length += view.size;
}

void tbox_string_builder_append_view_lower_ascii(tbox_string_builder *builder, tbox_string_view view) {
    tbox_string_builder_reserve(builder, view.size);
    for (size_t i = 0; i < view.size; i++) {
        builder->data[builder->length++] = tbox_ascii_to_lower(view.data[i]);
    }
}

void tbox_string_builder_append_codepoint(tbox_string_builder *builder, uint32_t codepoint) {
    char buffer[4];
    size_t size = tbox_utf8_encode(buffer, sizeof(buffer), codepoint);
    tbox_string_builder_append_view(builder, tbox_string_view_make(buffer, size));
}

tbox_string_view tbox_string_builder_finish(const tbox_string_builder *builder) {
    return tbox_string_view_make(builder->data, builder->length);
}

tbox_string_view tbox_string_collapse_whitespace(tbox_arena *arena, tbox_string_view text) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, text.size);

    bool pending_space = false;
    for (size_t i = 0; i < text.size; i++) {
        char c = text.data[i];
        if (tbox_ascii_is_space(c) || c == '\v') { /* '\v' is collapsed too, not drawn */
            /* Defer emitting the collapsed space until we know there is more
             * non-whitespace content ahead, so trailing runs are dropped. */
            pending_space = true;
            continue;
        }

        if (pending_space && builder.length > 0) {
            tbox_string_builder_append_byte(&builder, ' ');
        }
        pending_space = false;
        tbox_string_builder_append_byte(&builder, c);
    }

    return tbox_string_builder_finish(&builder);
}
