#ifndef TBOX_BASE_UNICODE_H
#define TBOX_BASE_UNICODE_H
#include <stdbool.h>
#include <stdint.h>
/* Unicode general categories for typographic initials and hanging punctuation. */
bool tbox_unicode_is_open_punctuation(uint32_t cp);
bool tbox_unicode_is_close_punctuation(uint32_t cp);
bool tbox_unicode_is_quote(uint32_t cp);
bool tbox_unicode_is_punctuation(uint32_t cp);
bool tbox_unicode_is_combining_mark(uint32_t cp);
#endif
