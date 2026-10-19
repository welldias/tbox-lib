#ifndef TBOX_LAYOUT_BIDI_H
#define TBOX_LAYOUT_BIDI_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/font.h>
#include <tbox/string_view.h>

/* Bidi shared by layout and context (docs/09-texto-e-fontes.md, "Texto
 * bidirecional"): the x of the caret before byte `offset` of a one-line
 * `text` drawn from x = 0 in a paragraph of direction `rtl` -- the visual
 * position of that logical offset, at the edge of the character after it
 * (or the end edge of the last one). Plain left-to-right text gives
 * tbox_text_prefix_width. */
double tbox_layout_bidi_caret_x(const tbox_font_face *face, double letter_spacing, tbox_string_view text, size_t offset, bool rtl);

#endif /* TBOX_LAYOUT_BIDI_H */
