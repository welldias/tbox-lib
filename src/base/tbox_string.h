#ifndef TBOX_BASE_STRING_H
#define TBOX_BASE_STRING_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/string_view.h>

#include "tbox_arena.h"
#include "utf8.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wraps a NUL-terminated C string as a view. Uses utf8size_lazy (byte count),
 * never utf8len (which counts codepoints, not bytes). */
tbox_string_view tbox_string_view_from_cstr(const char *nul_terminated);

/* Number of UTF-8 codepoints in view (<= view.size). */
size_t tbox_string_view_codepoint_count(tbox_string_view view);

/* True if view is well-formed UTF-8. */
bool tbox_string_view_valid_utf8(tbox_string_view view);

/* Byte-wise, ASCII-only case-insensitive comparison (bytes >= 0x80 are
 * compared verbatim, never case-folded). Suitable for HTML tag/attribute
 * names. */
bool tbox_string_view_equal_ascii_ci(tbox_string_view a, tbox_string_view b);

/* Grows inside an arena; there is no free/shrink, lifetime is tied to the
 * arena. Not thread-safe. */
typedef struct tbox_string_builder {
    tbox_arena *arena;
    char *data;
    size_t length;
    size_t capacity;
} tbox_string_builder;

void tbox_string_builder_init(tbox_string_builder *builder, tbox_arena *arena, size_t initial_capacity);
void tbox_string_builder_append_byte(tbox_string_builder *builder, char byte);
void tbox_string_builder_append_view(tbox_string_builder *builder, tbox_string_view view);

/* Like append_view, but folds ASCII 'A'-'Z' to lowercase. Bytes >= 0x80
 * (UTF-8 lead/continuation bytes) never overlap that range, so multi-byte
 * codepoints pass through untouched without needing to be decoded. */
void tbox_string_builder_append_view_lower_ascii(tbox_string_builder *builder, tbox_string_view view);

void tbox_string_builder_append_codepoint(tbox_string_builder *builder, utf8_int32_t codepoint);

/* Packages the builder's current contents as a view. The backing bytes stay
 * owned by the arena; no copy is made. */
tbox_string_view tbox_string_builder_finish(const tbox_string_builder *builder);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_STRING_H */
