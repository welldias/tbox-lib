#ifndef TBOX_LAYOUT_PASSWORD_H
#define TBOX_LAYOUT_PASSWORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tbox/font.h>
#include <tbox/html_parser.h>
#include <tbox/string_view.h>

#include "base/tbox_arena.h"

/* Password masking, shared by layout (the drawn value) and context (caret,
 * hit testing, selection) so both always measure the same string. */

bool tbox_layout_is_password_input(const tbox_html_node *node);

/* The mark drawn for each codepoint: U+2022 when `face` has it, '*'
 * otherwise -- as a codepoint and as its UTF-8 text. */
uint32_t tbox_layout_password_codepoint(const tbox_font_face *face);
tbox_string_view tbox_layout_password_glyph(const tbox_font_face *face);

/* `text` with every codepoint replaced by the mark, built in `arena`;
 * empty on allocation failure. */
tbox_string_view tbox_layout_password_mask(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text);

/* Width of tbox_layout_password_mask(text), bit-identical to
 * tbox_font_measure_text_spaced of the mask, without building the string
 * (a masked tbox_text_measurer; see src/font/tbox_font_measure.h). */
double tbox_layout_password_width(const tbox_font_face *face, tbox_string_view text, double letter_spacing);

#endif /* TBOX_LAYOUT_PASSWORD_H */
