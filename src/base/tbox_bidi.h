#ifndef TBOX_BASE_BIDI_H
#define TBOX_BASE_BIDI_H

/* The Unicode Bidirectional Algorithm (UAX #9, Unicode 16.0), over
 * codepoints: the layout runs it per paragraph and reorders each line
 * (docs/09-texto-e-fontes.md, "Texto bidirecional"). No allocation
 * outside the calls themselves; the tables are `static const` data. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tbox_bidi_class {
    TBOX_BIDI_L,
    TBOX_BIDI_R,
    TBOX_BIDI_AL,
    TBOX_BIDI_EN,
    TBOX_BIDI_ES,
    TBOX_BIDI_ET,
    TBOX_BIDI_AN,
    TBOX_BIDI_CS,
    TBOX_BIDI_NSM,
    TBOX_BIDI_BN,
    TBOX_BIDI_B,
    TBOX_BIDI_S,
    TBOX_BIDI_WS,
    TBOX_BIDI_ON,
    TBOX_BIDI_LRE,
    TBOX_BIDI_LRO,
    TBOX_BIDI_RLE,
    TBOX_BIDI_RLO,
    TBOX_BIDI_PDF,
    TBOX_BIDI_LRI,
    TBOX_BIDI_RLI,
    TBOX_BIDI_FSI,
    TBOX_BIDI_PDI,
} tbox_bidi_class;

/* Explicit formatting characters the layout inserts for `unicode-bidi`. */
enum {
    TBOX_BIDI_CHAR_LRE = 0x202A,
    TBOX_BIDI_CHAR_RLE = 0x202B,
    TBOX_BIDI_CHAR_PDF = 0x202C,
    TBOX_BIDI_CHAR_LRO = 0x202D,
    TBOX_BIDI_CHAR_RLO = 0x202E,
    TBOX_BIDI_CHAR_LRI = 0x2066,
    TBOX_BIDI_CHAR_RLI = 0x2067,
    TBOX_BIDI_CHAR_FSI = 0x2068,
    TBOX_BIDI_CHAR_PDI = 0x2069,
};

/* Paragraph level to be found from the text (rules P2/P3). */
#define TBOX_BIDI_LEVEL_AUTO 0xFF

tbox_bidi_class tbox_bidi_class_of(uint32_t codepoint);

/* Bidi_Mirroring_Glyph of `codepoint`, or `codepoint` itself (rule L4). */
uint32_t tbox_bidi_mirror(uint32_t codepoint);

/* True for a class that makes text need the algorithm at all in a
 * left-to-right paragraph: R, AL, AN or an explicit formatting character. */
bool tbox_bidi_class_is_rtl_trigger(tbox_bidi_class bidi_class);

/* Rules P2/P3 over text[0..count): 1 if the first strong character
 * (skipping isolates) is R or AL, 0 if it is L, `fallback` if none. */
uint8_t tbox_bidi_first_strong_level(const uint32_t *text, size_t count, uint8_t fallback);

/* Resolves the embedding level of each of text[0..count) -- one
 * paragraph, rules P2-I2 -- into levels[0..count). `paragraph_level` is 0,
 * 1 or TBOX_BIDI_LEVEL_AUTO; the level used is returned. Characters
 * removed by X9 (embeddings, overrides, PDF, BN) take the level of the
 * character before them. False only on allocation failure (levels are
 * then all the paragraph level). */
bool tbox_bidi_resolve(const uint32_t *text, size_t count, uint8_t paragraph_level, uint8_t *levels, uint8_t *out_paragraph_level);

/* Rule L1 on one line text[start..end) of a resolved paragraph:
 * separators and the whitespace (and isolate/removed characters) before
 * them or at the end of the line return to the paragraph level. */
void tbox_bidi_reset_line(const uint32_t *text, size_t start, size_t end, uint8_t paragraph_level, uint8_t *levels);

/* Rule L2: order[v] = the index (0..count) shown at visual position v,
 * left to right, of items with levels[0..count). */
void tbox_bidi_reorder(const uint8_t *levels, size_t count, size_t *order);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_BASE_BIDI_H */
