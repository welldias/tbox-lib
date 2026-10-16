#ifndef TBOX_BASE_STRING_H
#define TBOX_BASE_STRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/string_view.h>

#include "tbox_arena.h"

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

/* Every UTF-8 operation of the library lives here; tbox_string.c is the
 * only file that includes the vendored utf8.h.
 *
 * The single UTF-8 decoder of the library: every loop that walks text
 * codepoint by codepoint goes through these, never through utf8.h's
 * utf8codepoint (which reads past the end of a view) or ad-hoc lead-byte
 * scans. A malformed sequence decodes as one U+FFFD per maximal invalid
 * subpart (WHATWG Encoding), so text already passed through
 * tbox_utf8_sanitize decodes identically.
 *
 * Decodes the codepoint starting at data[at] (at < length) into
 * *out_codepoint and returns the offset just past it -- always > at. */
size_t tbox_utf8_decode(const char *data, size_t length, size_t at, uint32_t *out_codepoint);

/* Offset of the codepoint after the one at `at`; `length` if at >= length. */
size_t tbox_utf8_next(const char *data, size_t length, size_t at);

/* Offset of the codepoint that ends at `at`; 0 if at == 0. */
size_t tbox_utf8_previous(const char *data, size_t at);

/* Returns `text` itself when it is valid UTF-8; otherwise a copy in
 * `arena` with every malformed sequence replaced by U+FFFD. Entry points
 * that accept text from outside (parsers, DOM setters, typed or pasted
 * text) call this, so everything downstream may assume valid UTF-8. */
tbox_string_view tbox_utf8_sanitize(tbox_arena *arena, tbox_string_view text);

/* Writes `codepoint` as UTF-8 into out[0..capacity) and returns the bytes
 * written (1..4), or 0 if it does not fit. Surrogates and values above
 * U+10FFFF are written as U+FFFD. */
size_t tbox_utf8_encode(char *out, size_t capacity, uint32_t codepoint);

/* Copies the longest prefix of `text` that fits in out[0..capacity - 1)
 * and ends on a codepoint boundary, NUL-terminates it and returns its
 * byte length. Fixed-size fields (tbox_style strings, path buffers) use
 * this so a cut never leaves half a UTF-8 sequence. `capacity` must be
 * >= 1. */
size_t tbox_utf8_copy_truncated(char *out, size_t capacity, tbox_string_view text);

/* Simple (one-to-one) case mappings from utf8.h: no multi-codepoint
 * expansions such as German sharp s. A codepoint without a mapping is
 * returned unchanged. */
uint32_t tbox_utf8_to_upper(uint32_t codepoint);
uint32_t tbox_utf8_to_lower(uint32_t codepoint);

/* ASCII helpers for markup and CSS syntax. They replace <ctype.h>, whose
 * answers depend on the C locale (isspace also accepts '\v'). */

/* ASCII whitespace as HTML and CSS define it (WHATWG Infra): TAB, LF, FF,
 * CR and SPACE. XPath's grammar has its own set (no FF). */
bool tbox_ascii_is_space(char c);
bool tbox_ascii_is_hex_digit(char c);
char tbox_ascii_to_lower(char c);

/* `view` without leading and trailing tbox_ascii_is_space bytes. */
tbox_string_view tbox_string_view_trim_ascii_space(tbox_string_view view);

/* True if `view` starts with `prefix`, ASCII case-insensitively. */
bool tbox_string_view_starts_with_ascii_ci(tbox_string_view view, const char *prefix);

/* Copy of `view` in `arena` with 'A'-'Z' folded to lowercase. */
tbox_string_view tbox_string_copy_lower_ascii(tbox_arena *arena, tbox_string_view view);

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

/* Appends `codepoint` encoded by tbox_utf8_encode (invalid values become
 * U+FFFD). */
void tbox_string_builder_append_codepoint(tbox_string_builder *builder, uint32_t codepoint);

/* Packages the builder's current contents as a view. The backing bytes stay
 * owned by the arena; no copy is made. */
tbox_string_view tbox_string_builder_finish(const tbox_string_builder *builder);

/* Collapses runs of ASCII whitespace (space, tab, '\n', '\r', form feed,
 * vertical tab) into a single ' ' and trims leading/trailing whitespace --
 * equivalent to CSS `white-space: normal` text collapsing. Builds a fresh
 * copy in `arena`; `text` is a non-owning view and is never modified. */
tbox_string_view tbox_string_collapse_whitespace(tbox_arena *arena, tbox_string_view text);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_STRING_H */
