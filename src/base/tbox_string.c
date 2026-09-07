#include "tbox_string.h"

#include <string.h>

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
    return utf8nlen(view.data, view.size);
}

bool tbox_string_view_valid_utf8(tbox_string_view view) {
    return utf8nvalid(view.data, view.size) == NULL;
}

static char tbox_ascii_to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
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

void tbox_string_builder_append_codepoint(tbox_string_builder *builder, utf8_int32_t codepoint) {
    size_t codepoint_size = utf8codepointsize(codepoint);
    tbox_string_builder_reserve(builder, codepoint_size);
    utf8catcodepoint(builder->data + builder->length, codepoint, codepoint_size);
    builder->length += codepoint_size;
}

tbox_string_view tbox_string_builder_finish(const tbox_string_builder *builder) {
    return tbox_string_view_make(builder->data, builder->length);
}
