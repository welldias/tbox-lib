/* The Unicode Bidirectional Algorithm (UAX #9, revision for Unicode 16.0):
 * explicit levels (X1-X10), weak types (W1-W7), paired brackets (N0),
 * neutrals (N1-N2), implicit levels (I1-I2) and the line rules L1/L2.
 * Mirroring (L4) is left to the shaper (tbox_bidi_mirror). Checked against
 * the UCD's BidiCharacterTest.txt; see tests/base/test_bidi.c. */
#include "base/tbox_bidi.h"

#include <stdlib.h>
#include <string.h>

#include "base/tbox_bidi_tables.h"

#define TBOX_BIDI_MAX_DEPTH 125
#define TBOX_BIDI_NONE      ((size_t)-1)

tbox_bidi_class tbox_bidi_class_of(uint32_t codepoint) {
    size_t low = 0, high = tbox_bidi_class_range_count;
    while (low < high) {
        size_t middle                     = low + (high - low) / 2;
        const tbox_bidi_class_range *item = &tbox_bidi_class_ranges[middle];
        if (codepoint < item->first)
            high = middle;
        else if (codepoint > item->last)
            low = middle + 1;
        else
            return item->bidi_class;
    }
    return TBOX_BIDI_L;
}

uint32_t tbox_bidi_mirror(uint32_t codepoint) {
    size_t low = 0, high = tbox_bidi_mirror_pair_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (codepoint < tbox_bidi_mirror_pairs[middle].codepoint)
            high = middle;
        else if (codepoint > tbox_bidi_mirror_pairs[middle].codepoint)
            low = middle + 1;
        else
            return tbox_bidi_mirror_pairs[middle].mirror;
    }
    return codepoint;
}

static const tbox_bidi_bracket *tbox_bidi_bracket_of(uint32_t codepoint) {
    size_t low = 0, high = tbox_bidi_bracket_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (codepoint < tbox_bidi_brackets[middle].codepoint)
            high = middle;
        else if (codepoint > tbox_bidi_brackets[middle].codepoint)
            low = middle + 1;
        else
            return &tbox_bidi_brackets[middle];
    }
    return NULL;
}

bool tbox_bidi_class_is_rtl_trigger(tbox_bidi_class bidi_class) {
    return bidi_class == TBOX_BIDI_R || bidi_class == TBOX_BIDI_AL || bidi_class == TBOX_BIDI_AN || bidi_class >= TBOX_BIDI_LRE;
}

static bool tbox_bidi_is_isolate_initiator(tbox_bidi_class c) {
    return c == TBOX_BIDI_LRI || c == TBOX_BIDI_RLI || c == TBOX_BIDI_FSI;
}

/* X9: embeddings, overrides, PDF and boundary neutrals. */
static bool tbox_bidi_is_removed(tbox_bidi_class c) {
    return c == TBOX_BIDI_LRE || c == TBOX_BIDI_RLE || c == TBOX_BIDI_LRO || c == TBOX_BIDI_RLO || c == TBOX_BIDI_PDF || c == TBOX_BIDI_BN;
}

static bool tbox_bidi_is_neutral_or_isolate(tbox_bidi_class c) {
    return c == TBOX_BIDI_B || c == TBOX_BIDI_S || c == TBOX_BIDI_WS || c == TBOX_BIDI_ON || tbox_bidi_is_isolate_initiator(c) || c == TBOX_BIDI_PDI;
}

/* P2/P3 over classes[start..end), skipping isolates; 0xFF when no strong
 * character is found. */
static uint8_t tbox_bidi_first_strong(const uint8_t *classes, size_t start, size_t end) {
    size_t depth = 0;
    for (size_t i = start; i < end; i++) {
        tbox_bidi_class c = (tbox_bidi_class)classes[i];
        if (tbox_bidi_is_isolate_initiator(c))
            depth++;
        else if (c == TBOX_BIDI_PDI) {
            if (depth > 0)
                depth--;
        } else if (c == TBOX_BIDI_B)
            break;
        else if (depth == 0 && c == TBOX_BIDI_L)
            return 0;
        else if (depth == 0 && (c == TBOX_BIDI_R || c == TBOX_BIDI_AL))
            return 1;
    }
    return 0xFF;
}

uint8_t tbox_bidi_first_strong_level(const uint32_t *text, size_t count, uint8_t fallback) {
    size_t depth = 0;
    for (size_t i = 0; i < count; i++) {
        tbox_bidi_class c = tbox_bidi_class_of(text[i]);
        if (tbox_bidi_is_isolate_initiator(c))
            depth++;
        else if (c == TBOX_BIDI_PDI) {
            if (depth > 0)
                depth--;
        } else if (c == TBOX_BIDI_B)
            break;
        else if (depth == 0 && c == TBOX_BIDI_L)
            return 0;
        else if (depth == 0 && (c == TBOX_BIDI_R || c == TBOX_BIDI_AL))
            return 1;
    }
    return fallback;
}

/* Strong direction for N0/N1: L, or R for R/AL/EN/AN; ON otherwise. */
static tbox_bidi_class tbox_bidi_strong(tbox_bidi_class c) {
    if (c == TBOX_BIDI_L)
        return TBOX_BIDI_L;
    if (c == TBOX_BIDI_R || c == TBOX_BIDI_AL || c == TBOX_BIDI_EN || c == TBOX_BIDI_AN)
        return TBOX_BIDI_R;
    return TBOX_BIDI_ON;
}

/* U+2329/U+232A are canonically equivalent to U+3008/U+3009 (BD16). */
static uint32_t tbox_bidi_canonical_bracket(uint32_t codepoint) {
    if (codepoint == 0x2329)
        return 0x3008;
    if (codepoint == 0x232A)
        return 0x3009;
    return codepoint;
}

typedef struct tbox_bidi_bracket_pair {
    size_t open, close; /* indices into the sequence */
} tbox_bidi_bracket_pair;

static int tbox_bidi_pair_compare(const void *a, const void *b) {
    size_t x = ((const tbox_bidi_bracket_pair *)a)->open, y = ((const tbox_bidi_bracket_pair *)b)->open;
    return x < y ? -1 : x > y;
}

typedef struct tbox_bidi_work {
    const uint32_t *text;
    const uint8_t *initial; /* original classes */
    uint8_t *types;         /* current classes */
    uint8_t *levels;
    const uint8_t *explicit_levels; /* levels after X1-X8, before W1-I2 */
    size_t *seq;            /* one isolating run sequence (text indices) */
    uint8_t *seq_types;
    tbox_bidi_bracket_pair *pairs;
    size_t count;
    uint8_t paragraph_level;
} tbox_bidi_work;

/* N0 for the sequence seq[0..m). */
static void tbox_bidi_resolve_brackets(tbox_bidi_work *w, size_t m, uint8_t level, tbox_bidi_class sos) {
    uint8_t *t = w->seq_types;
    struct {
        uint32_t pair;
        size_t at;
    } stack[63];
    size_t depth = 0, pair_count = 0;
    for (size_t k = 0; k < m; k++) {
        if (t[k] != TBOX_BIDI_ON)
            continue;
        uint32_t cp                       = w->text[w->seq[k]];
        const tbox_bidi_bracket *bracket = tbox_bidi_bracket_of(cp);
        if (bracket == NULL)
            continue;
        if (bracket->opening) {
            if (depth == 63)
                break;
            stack[depth].pair = tbox_bidi_canonical_bracket(bracket->pair);
            stack[depth].at   = k;
            depth++;
        } else {
            uint32_t self = tbox_bidi_canonical_bracket(cp);
            for (size_t s = depth; s > 0; s--) {
                if (stack[s - 1].pair == self) {
                    w->pairs[pair_count++] = (tbox_bidi_bracket_pair){ stack[s - 1].at, k };
                    depth                  = s - 1;
                    break;
                }
            }
        }
    }
    if (pair_count == 0)
        return;
    qsort(w->pairs, pair_count, sizeof(*w->pairs), tbox_bidi_pair_compare);

    tbox_bidi_class embedding = (level & 1) ? TBOX_BIDI_R : TBOX_BIDI_L;
    for (size_t p = 0; p < pair_count; p++) {
        size_t open = w->pairs[p].open, close = w->pairs[p].close;
        bool found_embedding = false, found_opposite = false;
        for (size_t k = open + 1; k < close; k++) {
            tbox_bidi_class s = tbox_bidi_strong((tbox_bidi_class)t[k]);
            if (s == embedding)
                found_embedding = true;
            else if (s != TBOX_BIDI_ON)
                found_opposite = true;
        }
        tbox_bidi_class resolved;
        if (found_embedding)
            resolved = embedding;
        else if (found_opposite) {
            tbox_bidi_class context = sos;
            for (size_t k = open; k > 0; k--) {
                tbox_bidi_class s = tbox_bidi_strong((tbox_bidi_class)t[k - 1]);
                if (s != TBOX_BIDI_ON) {
                    context = s;
                    break;
                }
            }
            resolved = context != embedding ? context : embedding;
        } else
            continue;
        t[open] = t[close] = (uint8_t)resolved;
        /* NSMs after a bracket follow it (they became ON in W1). */
        const size_t ends[2] = { open, close };
        for (size_t e = 0; e < 2; e++)
            for (size_t k = ends[e] + 1; k < m && w->initial[w->seq[k]] == TBOX_BIDI_NSM; k++)
                t[k] = (uint8_t)resolved;
    }
}

/* W1-I2 for the isolating run sequence seq[0..m). */
static void tbox_bidi_resolve_sequence(tbox_bidi_work *w, size_t m) {
    size_t *seq   = w->seq;
    uint8_t *t    = w->seq_types;
    uint8_t level = w->explicit_levels[seq[0]];
    for (size_t k = 0; k < m; k++)
        t[k] = w->types[seq[k]];

    /* sos/eos from the levels around the sequence (X10). */
    size_t first = seq[0], last = seq[m - 1];
    size_t before = first;
    while (before > 0 && tbox_bidi_is_removed((tbox_bidi_class)w->initial[before - 1]))
        before--;
    uint8_t previous_level = before > 0 ? w->explicit_levels[before - 1] : w->paragraph_level;
    uint8_t next_level     = w->paragraph_level;
    if (!tbox_bidi_is_isolate_initiator((tbox_bidi_class)w->types[last])) {
        size_t after = last + 1;
        while (after < w->count && tbox_bidi_is_removed((tbox_bidi_class)w->initial[after]))
            after++;
        if (after < w->count)
            next_level = w->explicit_levels[after];
    }
    tbox_bidi_class sos = ((previous_level > level ? previous_level : level) & 1) ? TBOX_BIDI_R : TBOX_BIDI_L;
    tbox_bidi_class eos = ((next_level > level ? next_level : level) & 1) ? TBOX_BIDI_R : TBOX_BIDI_L;

    /* W1 */
    for (size_t k = 0; k < m; k++) {
        if (t[k] != TBOX_BIDI_NSM)
            continue;
        if (k == 0)
            t[k] = (uint8_t)sos;
        else if (tbox_bidi_is_isolate_initiator((tbox_bidi_class)t[k - 1]) || t[k - 1] == TBOX_BIDI_PDI)
            t[k] = TBOX_BIDI_ON;
        else
            t[k] = t[k - 1];
    }
    /* W2, W3 */
    tbox_bidi_class last_strong = sos;
    for (size_t k = 0; k < m; k++) {
        if (t[k] == TBOX_BIDI_L || t[k] == TBOX_BIDI_R || t[k] == TBOX_BIDI_AL)
            last_strong = (tbox_bidi_class)t[k];
        else if (t[k] == TBOX_BIDI_EN && last_strong == TBOX_BIDI_AL)
            t[k] = TBOX_BIDI_AN;
    }
    for (size_t k = 0; k < m; k++)
        if (t[k] == TBOX_BIDI_AL)
            t[k] = TBOX_BIDI_R;
    /* W4 */
    for (size_t k = 1; k + 1 < m; k++) {
        if (t[k] == TBOX_BIDI_ES && t[k - 1] == TBOX_BIDI_EN && t[k + 1] == TBOX_BIDI_EN)
            t[k] = TBOX_BIDI_EN;
        else if (t[k] == TBOX_BIDI_CS && t[k - 1] == t[k + 1] && (t[k - 1] == TBOX_BIDI_EN || t[k - 1] == TBOX_BIDI_AN))
            t[k] = t[k - 1];
    }
    /* W5 */
    for (size_t k = 0; k < m;) {
        if (t[k] != TBOX_BIDI_ET) {
            k++;
            continue;
        }
        size_t end = k;
        while (end < m && t[end] == TBOX_BIDI_ET)
            end++;
        if ((k > 0 && t[k - 1] == TBOX_BIDI_EN) || (end < m && t[end] == TBOX_BIDI_EN))
            for (size_t j = k; j < end; j++)
                t[j] = TBOX_BIDI_EN;
        k = end;
    }
    /* W6 */
    for (size_t k = 0; k < m; k++)
        if (t[k] == TBOX_BIDI_ES || t[k] == TBOX_BIDI_ET || t[k] == TBOX_BIDI_CS)
            t[k] = TBOX_BIDI_ON;
    /* W7 */
    last_strong = sos;
    for (size_t k = 0; k < m; k++) {
        if (t[k] == TBOX_BIDI_L || t[k] == TBOX_BIDI_R)
            last_strong = (tbox_bidi_class)t[k];
        else if (t[k] == TBOX_BIDI_EN && last_strong == TBOX_BIDI_L)
            t[k] = TBOX_BIDI_L;
    }
    /* N0 */
    tbox_bidi_resolve_brackets(w, m, level, sos);
    /* N1, N2 */
    tbox_bidi_class embedding = (level & 1) ? TBOX_BIDI_R : TBOX_BIDI_L;
    for (size_t k = 0; k < m;) {
        if (!tbox_bidi_is_neutral_or_isolate((tbox_bidi_class)t[k])) {
            k++;
            continue;
        }
        size_t end = k;
        while (end < m && tbox_bidi_is_neutral_or_isolate((tbox_bidi_class)t[end]))
            end++;
        tbox_bidi_class leading  = k == 0 ? sos : tbox_bidi_strong((tbox_bidi_class)t[k - 1]);
        tbox_bidi_class trailing = end == m ? eos : tbox_bidi_strong((tbox_bidi_class)t[end]);
        tbox_bidi_class resolved = leading == trailing ? leading : embedding;
        for (size_t j = k; j < end; j++)
            t[j] = (uint8_t)resolved;
        k = end;
    }
    /* I1, I2 */
    for (size_t k = 0; k < m; k++) {
        uint8_t *slot = &w->levels[seq[k]];
        if ((*slot & 1) == 0) {
            if (t[k] == TBOX_BIDI_R)
                *slot += 1;
            else if (t[k] == TBOX_BIDI_AN || t[k] == TBOX_BIDI_EN)
                *slot += 2;
        } else if (t[k] == TBOX_BIDI_L || t[k] == TBOX_BIDI_EN || t[k] == TBOX_BIDI_AN)
            *slot += 1;
    }
}

bool tbox_bidi_resolve(const uint32_t *text, size_t count, uint8_t paragraph_level, uint8_t *levels, uint8_t *out_paragraph_level) {
    if (count == 0) {
        if (out_paragraph_level != NULL)
            *out_paragraph_level = paragraph_level == TBOX_BIDI_LEVEL_AUTO ? 0 : paragraph_level;
        return true;
    }
    tbox_bidi_work w = { 0 };
    w.text           = text;
    w.count          = count;
    w.levels         = levels;
    uint8_t *initial = malloc(count * 5);
    size_t *indices  = malloc(count * 4 * sizeof(size_t));
    w.pairs          = malloc((count / 2 + 1) * sizeof(*w.pairs));
    if (initial == NULL || indices == NULL || w.pairs == NULL) {
        uint8_t level = paragraph_level == TBOX_BIDI_LEVEL_AUTO ? 0 : paragraph_level;
        memset(levels, level, count);
        if (out_paragraph_level != NULL)
            *out_paragraph_level = level;
        free(initial);
        free(indices);
        free(w.pairs);
        return false;
    }
    w.initial          = initial;
    w.types            = initial + count;
    w.seq_types        = initial + 2 * count;
    uint8_t *pdi_matched = initial + 3 * count;
    uint8_t *explicit_levels = initial + 4 * count;
    w.explicit_levels        = explicit_levels;
    size_t *matching   = indices;             /* isolate initiator -> its PDI */
    size_t *run_of     = indices + count;     /* text index -> level run */
    size_t *run_starts = indices + 2 * count; /* level run -> first text index; reused below */
    w.seq              = indices + 3 * count;

    for (size_t i = 0; i < count; i++)
        initial[i] = w.types[i] = (uint8_t)tbox_bidi_class_of(text[i]);

    /* BD9: matching PDIs. */
    {
        size_t depth = 0;
        for (size_t i = 0; i < count; i++) {
            matching[i]    = TBOX_BIDI_NONE;
            pdi_matched[i] = 0;
            if (tbox_bidi_is_isolate_initiator((tbox_bidi_class)initial[i]))
                run_starts[depth++] = i; /* scratch stack */
            else if (initial[i] == TBOX_BIDI_PDI && depth > 0) {
                matching[run_starts[--depth]] = i;
                pdi_matched[i]                = 1;
            }
            else if (initial[i] == TBOX_BIDI_B)
                depth = 0;
        }
    }

    /* P2, P3 */
    if (paragraph_level == TBOX_BIDI_LEVEL_AUTO) {
        uint8_t found   = tbox_bidi_first_strong(initial, 0, count);
        paragraph_level = found == 0xFF ? 0 : found;
    }
    w.paragraph_level = paragraph_level;

    /* X1-X8 */
    struct {
        uint8_t level;
        uint8_t override; /* ON, L or R */
        bool isolate;
    } stack[TBOX_BIDI_MAX_DEPTH + 2];
    size_t top                 = 0;
    stack[0].level             = paragraph_level;
    stack[0].override          = TBOX_BIDI_ON;
    stack[0].isolate           = false;
    size_t overflow_isolates   = 0;
    size_t overflow_embeddings = 0;
    size_t valid_isolates      = 0;
    for (size_t i = 0; i < count; i++) {
        tbox_bidi_class c = (tbox_bidi_class)initial[i];
        switch (c) {
        case TBOX_BIDI_RLE:
        case TBOX_BIDI_LRE:
        case TBOX_BIDI_RLO:
        case TBOX_BIDI_LRO: {
            bool rtl      = c == TBOX_BIDI_RLE || c == TBOX_BIDI_RLO;
            uint8_t level = stack[top].level;
            uint8_t next  = rtl ? (uint8_t)((level + 1) | 1) : (uint8_t)((level + 2) & ~1);
            levels[i]     = level;
            if (next <= TBOX_BIDI_MAX_DEPTH && overflow_isolates == 0 && overflow_embeddings == 0) {
                top++;
                stack[top].level    = next;
                stack[top].override = c == TBOX_BIDI_RLO ? TBOX_BIDI_R : c == TBOX_BIDI_LRO ? TBOX_BIDI_L : TBOX_BIDI_ON;
                stack[top].isolate  = false;
            } else if (overflow_isolates == 0)
                overflow_embeddings++;
            break;
        }
        case TBOX_BIDI_RLI:
        case TBOX_BIDI_LRI:
        case TBOX_BIDI_FSI: {
            bool rtl = c == TBOX_BIDI_RLI;
            if (c == TBOX_BIDI_FSI)
                rtl = tbox_bidi_first_strong(initial, i + 1, matching[i] != TBOX_BIDI_NONE ? matching[i] : count) == 1;
            levels[i] = stack[top].level;
            if (stack[top].override != TBOX_BIDI_ON)
                w.types[i] = stack[top].override;
            uint8_t level = stack[top].level;
            uint8_t next  = rtl ? (uint8_t)((level + 1) | 1) : (uint8_t)((level + 2) & ~1);
            if (next <= TBOX_BIDI_MAX_DEPTH && overflow_isolates == 0 && overflow_embeddings == 0) {
                valid_isolates++;
                top++;
                stack[top].level    = next;
                stack[top].override = TBOX_BIDI_ON;
                stack[top].isolate  = true;
            } else
                overflow_isolates++;
            break;
        }
        case TBOX_BIDI_PDI:
            if (overflow_isolates > 0)
                overflow_isolates--;
            else if (valid_isolates > 0) {
                overflow_embeddings = 0;
                while (!stack[top].isolate)
                    top--;
                top--;
                valid_isolates--;
            }
            levels[i] = stack[top].level;
            if (stack[top].override != TBOX_BIDI_ON)
                w.types[i] = stack[top].override;
            break;
        case TBOX_BIDI_PDF:
            levels[i] = stack[top].level;
            if (overflow_isolates > 0) {
            } else if (overflow_embeddings > 0)
                overflow_embeddings--;
            else if (!stack[top].isolate && top > 0)
                top--;
            break;
        case TBOX_BIDI_B:
            levels[i] = paragraph_level;
            break;
        case TBOX_BIDI_BN:
            levels[i] = stack[top].level;
            break;
        default:
            levels[i] = stack[top].level;
            if (stack[top].override != TBOX_BIDI_ON)
                w.types[i] = stack[top].override;
            break;
        }
    }

    memcpy(explicit_levels, levels, count);

    /* X10: level runs over the characters X9 keeps, chained into
     * isolating run sequences through matching isolate initiator/PDI. */
    size_t run_count = 0;
    {
        bool have = false;
        uint8_t current = 0;
        for (size_t i = 0; i < count; i++) {
            run_of[i] = TBOX_BIDI_NONE;
            if (tbox_bidi_is_removed((tbox_bidi_class)initial[i]))
                continue;
            if (!have || levels[i] != current) {
                run_starts[run_count++] = i;
                current                 = levels[i];
                have                    = true;
            }
            run_of[i] = run_count - 1;
        }
    }
    for (size_t r = 0; r < run_count; r++) {
        size_t start = run_starts[r];
        /* a matched PDI continues the sequence of its initiator */
        if (pdi_matched[start])
            continue;
        size_t m   = 0;
        size_t run = r;
        for (;;) {
            size_t last = TBOX_BIDI_NONE;
            for (size_t i = run_starts[run]; i < count; i++) {
                if (run_of[i] == TBOX_BIDI_NONE)
                    continue;
                if (run_of[i] != run)
                    break;
                w.seq[m++] = i;
                last       = i;
            }
            if (last == TBOX_BIDI_NONE || !tbox_bidi_is_isolate_initiator((tbox_bidi_class)initial[last]) || matching[last] == TBOX_BIDI_NONE)
                break;
            run = run_of[matching[last]];
        }
        if (m > 0)
            tbox_bidi_resolve_sequence(&w, m);
    }

    /* Removed characters take the level of the one before them. */
    for (size_t i = 0; i < count; i++)
        if (tbox_bidi_is_removed((tbox_bidi_class)initial[i]))
            levels[i] = i > 0 ? levels[i - 1] : paragraph_level;

    if (out_paragraph_level != NULL)
        *out_paragraph_level = paragraph_level;
    free(initial);
    free(indices);
    free(w.pairs);
    return true;
}

void tbox_bidi_reset_line(const uint32_t *text, size_t start, size_t end, uint8_t paragraph_level, uint8_t *levels) {
    bool trailing = true;
    for (size_t i = end; i > start; i--) {
        tbox_bidi_class c = tbox_bidi_class_of(text[i - 1]);
        if (c == TBOX_BIDI_S || c == TBOX_BIDI_B) {
            levels[i - 1] = paragraph_level;
            trailing      = true;
        } else if (trailing && (c == TBOX_BIDI_WS || tbox_bidi_is_isolate_initiator(c) || c == TBOX_BIDI_PDI || tbox_bidi_is_removed(c)))
            levels[i - 1] = paragraph_level;
        else
            trailing = false;
    }
}

void tbox_bidi_reorder(const uint8_t *levels, size_t count, size_t *order) {
    uint8_t highest = 0, lowest_odd = 0xFF;
    for (size_t i = 0; i < count; i++) {
        order[i] = i;
        if (levels[i] > highest)
            highest = levels[i];
        if ((levels[i] & 1) && levels[i] < lowest_odd)
            lowest_odd = levels[i];
    }
    for (unsigned level = highest; level >= lowest_odd && level > 0; level--) {
        for (size_t i = 0; i < count;) {
            if (levels[order[i]] < level) {
                i++;
                continue;
            }
            size_t end = i;
            while (end < count && levels[order[end]] >= level)
                end++;
            for (size_t a = i, b = end - 1; a < b; a++, b--) {
                size_t swap = order[a];
                order[a]    = order[b];
                order[b]    = swap;
            }
            i = end;
        }
    }
}
