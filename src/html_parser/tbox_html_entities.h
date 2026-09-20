#ifndef TBOX_HTML_PARSER_ENTITIES_H
#define TBOX_HTML_PARSER_ENTITIES_H

#include <tbox/string_view.h>

#include "base/tbox_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Copies `text` into `arena`, decoding any numeric character reference
 * (`&#NNN;`/`&#xHHH;`/`&#XHHH;`, any Unicode codepoint) or named reference (a
 * fixed set of ~23 common entities -- see ARCHITECTURE.md's v6 "Escopo")
 * found in `text`. A reference that isn't recognized (name outside the
 * table, numeric with no digits, or either kind missing the trailing ';')
 * is left as literal text, consuming nothing beyond the '&' itself -- same
 * "unrecognized = pass through" posture as the rest of the tolerant parser.
 * A numeric codepoint that decodes to 0, a surrogate (0xD800-0xDFFF), or a
 * value > 0x10FFFF becomes the Unicode replacement character (U+FFFD)
 * instead of producing invalid UTF-8. */
tbox_string_view tbox_html_decode_entities(tbox_arena *arena, tbox_string_view text);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_HTML_PARSER_ENTITIES_H */
