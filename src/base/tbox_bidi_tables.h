#ifndef TBOX_BASE_BIDI_TABLES_H
#define TBOX_BASE_BIDI_TABLES_H

/* The Unicode data behind src/base/tbox_bidi.c (generated, see
 * tbox_bidi_tables.c). Internal to that file. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "base/tbox_bidi.h"

typedef struct tbox_bidi_class_range {
    uint32_t first, last;
    tbox_bidi_class bidi_class;
} tbox_bidi_class_range;

typedef struct tbox_bidi_pair {
    uint32_t codepoint, mirror;
} tbox_bidi_pair;

typedef struct tbox_bidi_bracket {
    uint32_t codepoint, pair;
    bool opening;
} tbox_bidi_bracket;

extern const tbox_bidi_class_range tbox_bidi_class_ranges[];
extern const size_t tbox_bidi_class_range_count;
extern const tbox_bidi_pair tbox_bidi_mirror_pairs[];
extern const size_t tbox_bidi_mirror_pair_count;
extern const tbox_bidi_bracket tbox_bidi_brackets[];
extern const size_t tbox_bidi_bracket_count;

#endif /* TBOX_BASE_BIDI_TABLES_H */
