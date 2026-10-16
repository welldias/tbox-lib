#ifndef TBOX_BASE_NUMBER_H
#define TBOX_BASE_NUMBER_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/string_view.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Locale-independent decimal numbers. strtod and printf follow
 * LC_NUMERIC, so an application that calls setlocale(LC_ALL, "") in a
 * locale with a decimal comma would read CSS "1.5" as 1 and write "1,5".
 * Every number the library reads from or writes to markup/CSS goes
 * through here. */

/* Scans a CSS <number> at the start of `text`:
 * [+-]? (D+ (. D+)? | . D+) ([eE] [+-]? D+)?   (D = ASCII digit)
 * No leading whitespace, "inf", "nan" or hex forms. Returns the bytes
 * consumed and stores the value in *out (which may overflow to +-inf), or
 * returns 0 when `text` does not start with a number. The conversion is
 * exact for up to 15 significant digits and decimal exponents within
 * +-22 (every value markup realistically holds); beyond that it is within
 * a few ulps. */
size_t tbox_number_scan(tbox_string_view text, double *out);

/* True when the whole of `text` is one finite number per tbox_number_scan. */
bool tbox_number_parse(tbox_string_view text, double *out);

/* Like snprintf(out, capacity, "%.*g", precision, value), always with '.'
 * as the decimal separator. Returns the length the full result needs (as
 * snprintf), or -1 on an encoding error. */
int tbox_number_format(char *out, size_t capacity, double value, int precision);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_NUMBER_H */
