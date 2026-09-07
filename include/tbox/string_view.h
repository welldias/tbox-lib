#ifndef TBOX_STRING_VIEW_H
#define TBOX_STRING_VIEW_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A non-owning view over a span of bytes. `data` is NOT guaranteed to be
 * NUL-terminated: `size` is always the authoritative length in bytes. */
typedef struct tbox_string_view {
    const char *data;
    size_t size;
} tbox_string_view;

tbox_string_view tbox_string_view_make(const char *data, size_t size);
bool tbox_string_view_empty(tbox_string_view view);
bool tbox_string_view_equal(tbox_string_view a, tbox_string_view b);
bool tbox_string_view_equal_cstr(tbox_string_view view, const char *cstr);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_STRING_VIEW_H */
