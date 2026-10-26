#include <tbox/font.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_STROKER_H

#include <limits.h>
#include <math.h>

#include <stdlib.h>
#include <string.h>

#include "base/tbox_string.h"
#include "tbox_font_face_internal.h"
#include "tbox_font_measure.h"

/* One process-wide FT_Library, initialized lazily on first use. tbox has no
 * internal locking anywhere else, so this isn't made thread-safe either --
 * matching the rest of the codebase. Never torn down with FT_Done_FreeType:
 * v0 has no notion of shutting the whole library down, and every
 * tbox_font_face created against it is destroyed independently via
 * tbox_font_face_destroy (FT_Done_Face), which doesn't require the parent
 * FT_Library to still be freed. */
static FT_Library tbox_font_ft_library             = NULL;
static bool        tbox_font_ft_library_init_tried = false;

static FT_Library tbox_font_ft_library_get(void) {
    if (!tbox_font_ft_library_init_tried) {
        tbox_font_ft_library_init_tried = true;
        if (FT_Init_FreeType(&tbox_font_ft_library) != 0) {
            tbox_font_ft_library = NULL;
        }
    }

    return tbox_font_ft_library;
}

tbox_font_face *tbox_font_face_load(const void *font_data, size_t size, double size_px) {
    FT_Library library = tbox_font_ft_library_get();
    if (library == NULL || font_data == NULL) {
        return NULL;
    }

    void *font_data_copy = malloc(size);
    if (font_data_copy == NULL) {
        return NULL;
    }
    memcpy(font_data_copy, font_data, size);

    FT_Face ft_face;
    if (FT_New_Memory_Face(library, (const FT_Byte *)font_data_copy, (FT_Long)size, 0, &ft_face) != 0) {
        free(font_data_copy);
        return NULL;
    }

    if (FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)(size_px + 0.5)) != 0) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        return NULL;
    }

    tbox_font_face *face = malloc(sizeof(*face));
    if (face == NULL) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        return NULL;
    }

    face->options           = (tbox_font_shaping_options){0};
    face->ft_face           = ft_face;
    face->stroke_glyph      = NULL;
    face->font_data_copy    = font_data_copy;
    face->font_data_size    = size;
    face->kerning           = false;
    face->kerning_requested = false;
    face->shaper            = NULL;
    face->runs               = NULL;
    face->run_slots          = 0;
    face->run_count          = 0;
    face->advance_pages      = NULL;
    face->advance_page_count = 0;
    if (!tbox_font_shaper_attach(face)) {
        FT_Done_Face(ft_face);
        free(font_data_copy);
        free(face);
        return NULL;
    }
    return face;
}

static void tbox_font_run_cache_clear(tbox_font_face *face);

void tbox_font_face_set_kerning(tbox_font_face *face, bool kerning) {
    if (face != NULL) {
        if (face->kerning_requested != kerning)
            tbox_font_run_cache_clear(face); /* the cached runs used the old kerning */
        face->kerning_requested = kerning;
        face->kerning           = kerning && FT_HAS_KERNING(face->ft_face);
    }
}

double tbox_font_face_kerning(const tbox_font_face *face, uint32_t previous, uint32_t codepoint) {
    if (face == NULL || !face->kerning || previous == 0)
        return 0.0;
    FT_UInt left = FT_Get_Char_Index(face->ft_face, previous), right = FT_Get_Char_Index(face->ft_face, codepoint);
    FT_Vector delta;
    if (left == 0 || right == 0 || FT_Get_Kerning(face->ft_face, left, right, FT_KERNING_DEFAULT, &delta) != 0)
        return 0.0;
    return delta.x / 64.0;
}

void tbox_font_face_destroy(tbox_font_face *face) {
    if (face == NULL) {
        return;
    }

    /* The shaper and FT_Done_Face must run first: they stop reading
     * font_data_copy only once they return. */
    tbox_font_run_cache_clear(face);
    free(face->runs);
    for (size_t i = 0; i < face->advance_page_count; i++)
        free(face->advance_pages[i]);
    free(face->advance_pages);
    tbox_font_shaper_detach(face);
    if (face->stroke_glyph != NULL) FT_Done_Glyph(face->stroke_glyph);
    FT_Done_Face(face->ft_face);
    free(face->font_data_copy);
    free(face);
}

double tbox_font_face_line_height(const tbox_font_face *face) {
    if (face == NULL) {
        return 0.0;
    }

    /* face->size->metrics.height is ascender - descender + line gap,
     * already scaled to the pixel size set by FT_Set_Pixel_Sizes, in 26.6
     * fixed-point. */
    return face->ft_face->size->metrics.height / 64.0;
}

double tbox_font_face_ascent(const tbox_font_face *face) {
    if (face == NULL) {
        return 0.0;
    }

    /* face->size->metrics.ascender is the distance from the baseline up to
     * the top of the line box, already scaled to the active pixel size, in
     * 26.6 fixed-point -- same conversion as tbox_font_face_line_height
     * above. */
    return face->ft_face->size->metrics.ascender / 64.0;
}

double tbox_font_face_x_height(const tbox_font_face *face) {
    if (face == NULL)
        return 0.0;
    const TT_OS2 *os2 = (const TT_OS2 *)FT_Get_Sfnt_Table(face->ft_face, FT_SFNT_OS2);
    if (os2 != NULL && os2->version >= 2 && os2->sxHeight > 0)
        return FT_MulFix(os2->sxHeight, face->ft_face->size->metrics.y_scale) / 64.0;
    if (FT_Load_Char(face->ft_face, 'x', FT_LOAD_DEFAULT) != 0)
        return 0.0;
    return face->ft_face->glyph->metrics.horiBearingY / 64.0;
}

double tbox_font_face_cap_height(const tbox_font_face *face) {
    if (face == NULL)
        return 0.0;
    const TT_OS2 *os2 = (const TT_OS2 *)FT_Get_Sfnt_Table(face->ft_face, FT_SFNT_OS2);
    if (os2 != NULL && os2->version >= 2 && os2->sCapHeight > 0)
        return FT_MulFix(os2->sCapHeight, face->ft_face->size->metrics.y_scale) / 64.0;
    if (FT_Load_Char(face->ft_face, 'H', FT_LOAD_DEFAULT) != 0)
        return 0.0;
    return face->ft_face->glyph->metrics.horiBearingY / 64.0;
}

bool tbox_font_face_has_glyph(const tbox_font_face *face, uint32_t codepoint) {
    return face != NULL && FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint) != 0;
}

/* Vertical_Orientation U (UAX #50), coarsely: the CJK, Hangul, Yi,
 * kana, fullwidth and symbol blocks whose characters stand upright in
 * vertical text with text-orientation: mixed. */
static bool tbox_font_upright_codepoint(uint32_t c) {
    static const uint32_t ranges[][2] = {
        { 0x1100, 0x11FF },   { 0x2E80, 0x2FFF },   { 0x3000, 0x303F },   { 0x3040, 0x31FF },
        { 0x3200, 0x4DBF },   { 0x4E00, 0x9FFF },   { 0xA000, 0xA4CF },   { 0xA960, 0xA97F },
        { 0xAC00, 0xD7FF },   { 0xF900, 0xFAFF },   { 0xFE10, 0xFE1F },   { 0xFE30, 0xFE4F },
        { 0xFF01, 0xFF60 },   { 0xFFE0, 0xFFE6 },   { 0x1F000, 0x1FAFF }, { 0x20000, 0x3FFFD },
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++)
        if (c >= ranges[i][0] && c <= ranges[i][1])
            return true;
    return false;
}

/* Turns horizontally shaped glyphs into the face's vertical layout
 * (docs/09-texto-e-fontes.md, "Escrita vertical"). The line is laid out in
 * its frame as if it were horizontal; upright glyphs advance by 1em,
 * centered on the central baseline, and are drawn turned back so
 * they stand upright on the screen. text-combine-upright squeezes the
 * whole text into one upright 1em square. In a transposed frame sideways
 * glyphs are mirrored about the central baseline, so the transposition
 * shows them turned clockwise like in vertical-rl. */
static void tbox_font_orient(const tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_glyph *glyphs, size_t count, double *width) {
    unsigned char frame = face->options.vertical_frame, orientation = face->options.vertical_orientation;
    if (frame == TBOX_FONT_VERTICAL_NONE || count == 0 || (frame == TBOX_FONT_VERTICAL_ROTATED && orientation == TBOX_FONT_VERTICAL_SIDEWAYS))
        return;
    bool rotated   = frame == TBOX_FONT_VERTICAL_ROTATED;
    double ascent  = face->ft_face->size->metrics.ascender / 64.0;
    double descent = -face->ft_face->size->metrics.descender / 64.0;
    double central = -(ascent - descent) / 2.0; /* from the alphabetic baseline */
    unsigned char upright_orient = rotated ? TBOX_GLYPH_ORIENT_ROTATE_CCW : TBOX_GLYPH_ORIENT_TRANSPOSE;
    /* Upright glyphs advance by 1em, with the alphabetic baseline at the
     * ascent's share of it (what Firefox synthesizes without vmtx). */
    double em       = (double)face->ft_face->size->metrics.y_ppem;
    double em_top   = ascent + descent > 0.0 ? em * ascent / (ascent + descent) : ascent;
    bool blank      = true; /* a space between combined words stays a space */
    for (size_t i = 0; i < text.size && blank; i++)
        blank = text.data[i] == ' ' || text.data[i] == '\t' || text.data[i] == '\n';
    if (orientation == TBOX_FONT_VERTICAL_COMBINE && blank) {
        if (rotated)
            return;
        orientation = TBOX_FONT_VERTICAL_SIDEWAYS;
    }
    if (orientation == TBOX_FONT_VERTICAL_COMBINE) {
        double total = *width;
        float scale  = total > em && total > 0.0 ? (float)(em / total) : 1.0f;
        double top   = em_top;
        for (size_t i = 0; i < count; i++) {
            double offset    = (glyphs[i].x - total / 2.0) * scale;
            glyphs[i].x      = top;
            glyphs[i].y      = rotated ? central - offset : central + offset;
            glyphs[i].advance = i == 0 ? em + letter_spacing : 0.0;
            glyphs[i].orient = upright_orient;
            glyphs[i].scale  = scale;
        }
        *width = em + letter_spacing;
        return;
    }
    double pen = 0.0, shaped_pen = 0.0;
    for (size_t i = 0; i < count; i++) {
        tbox_glyph *glyph = &glyphs[i];
        uint32_t codepoint = 0;
        if (glyph->cluster < text.size)
            tbox_utf8_decode(text.data, text.size, glyph->cluster, &codepoint);
        bool upright = orientation == TBOX_FONT_VERTICAL_UPRIGHT || (orientation == TBOX_FONT_VERTICAL_MIXED && tbox_font_upright_codepoint(codepoint));
        double advance = glyph->advance;
        if (upright) {
            bool cluster_end = i + 1 == count || glyphs[i + 1].cluster != glyph->cluster;
            double spacing   = cluster_end ? letter_spacing : 0.0;
            double h_advance = advance - spacing;
            glyph->x       = pen + em_top;
            glyph->y       = rotated ? central + h_advance / 2.0 : central - h_advance / 2.0;
            glyph->advance = em + spacing;
            glyph->orient  = upright_orient;
        } else {
            glyph->x = pen + (glyph->x - shaped_pen);
            if (!rotated) {
                glyph->y      = 2.0 * central - glyph->y;
                glyph->orient = TBOX_GLYPH_ORIENT_FLIP;
            }
        }
        shaped_pen += advance;
        pen += glyph->advance;
    }
    *width = pen;
}

/* tbox_font_shaper_run plus the vertical orientation: a vertical face
 * needs every glyph, even when the caller only wants the width. */
static size_t tbox_font_run_uncached(tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, tbox_glyph *out, size_t capacity, double *out_width) {
    if (face->options.vertical_frame == TBOX_FONT_VERTICAL_NONE)
        return tbox_font_shaper_run(face, text, letter_spacing, direction, out, capacity, out_width);
    size_t count = tbox_font_shaper_run(face, text, letter_spacing, direction, out, capacity, out_width);
    if (count <= capacity) {
        tbox_font_orient(face, text, letter_spacing, out, count, out_width);
        return count;
    }
    tbox_glyph *all = malloc(count * sizeof(*all));
    if (all == NULL)
        return count;
    tbox_font_shaper_run(face, text, letter_spacing, direction, all, count, out_width);
    tbox_font_orient(face, text, letter_spacing, all, count, out_width);
    if (capacity > 0)
        memcpy(out, all, capacity * sizeof(*all));
    free(all);
    return count;
}

/* The run cache (tbox_font_face.runs): a run's glyphs depend only on the
 * face and on (text, letter-spacing, direction), and HarfBuzz shaping was
 * most of the layout's cost. The table starts at MIN_SLOTS and doubles up
 * to MAX_SLOTS (MAX_SLOTS / 2 runs: a table of a few thousand rows has
 * that many different words, and each frame asks for all of them). Runs
 * longer than MAX_TEXT are not kept. */
#define TBOX_FONT_RUN_CACHE_MIN_SLOTS 2048
#define TBOX_FONT_RUN_CACHE_MAX_SLOTS 65536
#define TBOX_FONT_RUN_CACHE_MAX_TEXT  256

static void tbox_font_run_cache_clear(tbox_font_face *face) {
    if (face->runs == NULL)
        return;
    for (size_t i = 0; i < face->run_slots; i++) {
        free(face->runs[i].text);
        free(face->runs[i].glyphs);
    }
    memset(face->runs, 0, face->run_slots * sizeof(*face->runs));
    face->run_count = 0;
}

static uint64_t tbox_font_run_hash(tbox_string_view text, double letter_spacing, tbox_font_direction direction) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < text.size; i++) {
        hash ^= (unsigned char)text.data[i];
        hash *= 1099511628211ull;
    }
    uint64_t bits;
    memcpy(&bits, &letter_spacing, sizeof(bits));
    hash ^= bits ^ ((uint64_t)direction << 56);
    hash *= 1099511628211ull;
    return hash != 0 ? hash : 1;
}

/* The slot holding this run, or the empty slot where it would go. NULL
 * only when the table cannot be allocated. */
static tbox_font_run_entry *tbox_font_run_slot(tbox_font_face *face, uint64_t hash, tbox_string_view text, double letter_spacing, tbox_font_direction direction) {
    if (face->runs == NULL) {
        face->runs = calloc(TBOX_FONT_RUN_CACHE_MIN_SLOTS, sizeof(*face->runs));
        if (face->runs == NULL)
            return NULL;
        face->run_slots = TBOX_FONT_RUN_CACHE_MIN_SLOTS;
    }
    size_t mask = face->run_slots - 1;
    for (size_t i = hash & mask;; i = (i + 1) & mask) {
        tbox_font_run_entry *entry = &face->runs[i];
        if (entry->hash == 0 || (entry->hash == hash && entry->text_size == text.size && entry->letter_spacing == letter_spacing && entry->direction == direction && memcmp(entry->text, text.data, text.size) == 0))
            return entry;
    }
}

/* Moves the runs into a table of `slots` slots, keeping all of them, or
 * (`sweep`) only those asked for since the previous sweep, whose marks
 * it then clears. False, with nothing changed, when out of memory. */
static bool tbox_font_run_cache_rebuild(tbox_font_face *face, size_t slots, bool sweep) {
    tbox_font_run_entry *runs = calloc(slots, sizeof(*runs));
    if (runs == NULL)
        return false;
    size_t count = 0;
    for (size_t i = 0; i < face->run_slots; i++) {
        tbox_font_run_entry *entry = &face->runs[i];
        if (entry->hash == 0)
            continue;
        if (sweep && !entry->used) {
            free(entry->text);
            free(entry->glyphs);
            continue;
        }
        size_t slot = entry->hash & (slots - 1);
        while (runs[slot].hash != 0)
            slot = (slot + 1) & (slots - 1);
        runs[slot]      = *entry;
        runs[slot].used = !sweep && entry->used;
        count++;
    }
    free(face->runs);
    face->runs      = runs;
    face->run_slots = slots;
    face->run_count = count;
    return true;
}

/* Room for one more run: the table doubles while it can; at its largest,
 * the runs not asked for again since the previous sweep (`used` marks a
 * hit, not the insertion) go, so text shown every frame stays and text
 * seen once leaves. When nearly all of them stay (the working set does
 * not fit), it is emptied instead, so that a sweep does not come again a
 * few runs later. */
static void tbox_font_run_cache_make_room(tbox_font_face *face) {
    if (face->run_count + 1 <= face->run_slots / 2)
        return;
    if (face->run_slots < TBOX_FONT_RUN_CACHE_MAX_SLOTS && tbox_font_run_cache_rebuild(face, face->run_slots * 2, false))
        return;
    if (!tbox_font_run_cache_rebuild(face, face->run_slots, true) || face->run_count > face->run_slots * 3 / 8)
        tbox_font_run_cache_clear(face);
}

/* tbox_font_run_uncached through the face's run cache. */
static size_t tbox_font_run(tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, tbox_glyph *out, size_t capacity, double *out_width) {
    if (text.size == 0 || text.size > TBOX_FONT_RUN_CACHE_MAX_TEXT)
        return tbox_font_run_uncached(face, text, letter_spacing, direction, out, capacity, out_width);
    if (letter_spacing == 0.0)
        letter_spacing = 0.0; /* -0.0 hashes like 0.0 */
    uint64_t hash              = tbox_font_run_hash(text, letter_spacing, direction);
    tbox_font_run_entry *entry = tbox_font_run_slot(face, hash, text, letter_spacing, direction);
    if (entry != NULL && entry->hash != 0 && (entry->glyphs != NULL || capacity == 0)) {
        if (capacity > 0 && entry->count > 0)
            memcpy(out, entry->glyphs, (capacity < entry->count ? capacity : entry->count) * sizeof(*out));
        *out_width  = entry->width;
        entry->used = true;
        return entry->count;
    }
    double width;
    size_t count = tbox_font_run_uncached(face, text, letter_spacing, direction, out, capacity, &width);
    *out_width   = width;
    if (entry == NULL || (capacity > 0 && count > capacity))
        return count; /* the caller asks again with room for every glyph */
    tbox_glyph *glyphs = NULL;
    if (capacity > 0 && count > 0) {
        glyphs = malloc(count * sizeof(*glyphs));
        if (glyphs == NULL)
            return count;
        memcpy(glyphs, out, count * sizeof(*glyphs));
    }
    if (entry->hash == 0) {
        if (face->run_count + 1 > face->run_slots / 2) {
            tbox_font_run_cache_make_room(face);
            entry = tbox_font_run_slot(face, hash, text, letter_spacing, direction);
        }
        char *copy = malloc(text.size);
        if (copy == NULL) {
            free(glyphs);
            return count;
        }
        memcpy(copy, text.data, text.size);
        *entry = (tbox_font_run_entry){ .hash = hash, .text = copy, .text_size = text.size, .letter_spacing = letter_spacing, .direction = direction };
        face->run_count++;
    } else {
        entry->used = true;
    }
    /* A width-only entry gains its glyphs when a caller wants them. */
    free(entry->glyphs);
    entry->count  = count;
    entry->width  = width;
    entry->glyphs = glyphs;
    return count;
}

#define TBOX_FONT_ADVANCE_PAGE 256
#define TBOX_FONT_ADVANCE_NONE INT32_MIN

int32_t tbox_font_face_glyph_advance(tbox_font_face *face, uint32_t glyph) {
    FT_Face ft_face = face->ft_face;
    size_t page     = glyph / TBOX_FONT_ADVANCE_PAGE;
    if (face->advance_pages == NULL && ft_face->num_glyphs > 0) {
        size_t pages        = ((size_t)ft_face->num_glyphs + TBOX_FONT_ADVANCE_PAGE - 1) / TBOX_FONT_ADVANCE_PAGE;
        face->advance_pages = calloc(pages, sizeof(*face->advance_pages));
        if (face->advance_pages != NULL)
            face->advance_page_count = pages;
    }
    int32_t *advances = NULL;
    if (page < face->advance_page_count) {
        advances = face->advance_pages[page];
        if (advances == NULL) {
            advances = malloc(TBOX_FONT_ADVANCE_PAGE * sizeof(*advances));
            if (advances != NULL)
                for (size_t i = 0; i < TBOX_FONT_ADVANCE_PAGE; i++)
                    advances[i] = TBOX_FONT_ADVANCE_NONE;
            face->advance_pages[page] = advances;
        }
    }
    if (advances != NULL && advances[glyph % TBOX_FONT_ADVANCE_PAGE] != TBOX_FONT_ADVANCE_NONE)
        return advances[glyph % TBOX_FONT_ADVANCE_PAGE];
    FT_Pos advance = FT_Load_Glyph(ft_face, (FT_UInt)glyph, FT_LOAD_DEFAULT) == 0 ? ft_face->glyph->advance.x : 0;
    int32_t value  = advance > INT32_MIN && advance <= INT32_MAX ? (int32_t)advance : 0;
    if (advances != NULL)
        advances[glyph % TBOX_FONT_ADVANCE_PAGE] = value;
    return value;
}

/* Shapes `text` into a malloc'd array, growing it once if the shaper
 * reports more glyphs than the first guess. NULL (and *out_count 0) for
 * empty text, a NULL face or an allocation failure. */
static tbox_glyph *tbox_font_shape_malloc(const tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, size_t *out_count, double *out_width) {
    *out_count = 0;
    *out_width = 0.0;
    if (face == NULL || text.size == 0)
        return NULL;
    tbox_font_face *mutable_face = (tbox_font_face *)face; /* the shaper reuses per-face buffers */
    size_t capacity              = text.size + 8;
    tbox_glyph *glyphs           = malloc(capacity * sizeof(*glyphs));
    if (glyphs == NULL)
        return NULL;
    size_t count = tbox_font_run(mutable_face, text, letter_spacing, direction, glyphs, capacity, out_width);
    if (count > capacity) {
        tbox_glyph *grown = realloc(glyphs, count * sizeof(*glyphs));
        if (grown == NULL) {
            free(glyphs);
            return NULL;
        }
        glyphs = grown;
        tbox_font_run(mutable_face, text, letter_spacing, direction, glyphs, count, out_width);
    }
    *out_count = count;
    return glyphs;
}

/* prefix[i] = the width of the first i codepoints of `text` within its
 * shaping: each cluster's advance is split evenly among its codepoints,
 * the last one taking the exact remainder. */
static void tbox_text_measurer_fill(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, tbox_string_view text) {
    size_t glyph_count;
    double total;
    tbox_glyph *glyphs = tbox_font_shape_malloc(face, text, letter_spacing, TBOX_FONT_DIRECTION_AUTO, &glyph_count, &total);
    double *cluster_width = text.size > 0 ? calloc(text.size, sizeof(*cluster_width)) : NULL;
    bool *cluster_start   = text.size > 0 ? calloc(text.size, sizeof(*cluster_start)) : NULL;
    measurer->prefix      = malloc((measurer->count + 1) * sizeof(*measurer->prefix));
    if (measurer->prefix == NULL || (text.size > 0 && (cluster_width == NULL || cluster_start == NULL))) {
        free(measurer->prefix);
        measurer->prefix = NULL;
    } else {
        for (size_t g = 0; g < glyph_count; g++) {
            if (glyphs[g].cluster < text.size) {
                cluster_width[glyphs[g].cluster] += glyphs[g].advance;
                cluster_start[glyphs[g].cluster] = true;
            }
        }
        measurer->prefix[0] = 0.0;
        size_t index        = 0;
        double before       = 0.0;
        for (size_t at = 0; at < text.size;) {
            /* one cluster: its first codepoint and the ones that follow
             * until the next cluster start */
            size_t first   = index;
            double width   = cluster_width[at];
            size_t members = 0;
            do {
                at = tbox_utf8_next(text.data, text.size, at);
                members++;
            } while (at < text.size && !cluster_start[at]);
            for (size_t k = 1; k <= members; k++)
                measurer->prefix[first + k] = k == members ? before + width : before + width * (double)k / (double)members;
            index += members;
            before += width;
        }
        measurer->prefix[measurer->count] = glyph_count > 0 ? total : before;
    }
    free(cluster_width);
    free(cluster_start);
    free(glyphs);
}

void tbox_text_measurer_init(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length) {
    tbox_text_measurer_init_masked(measurer, face, letter_spacing, data, length, 0);
}

void tbox_text_measurer_init_masked(tbox_text_measurer *measurer, const tbox_font_face *face, double letter_spacing, const char *data, size_t length, uint32_t substitute) {
    *measurer = (tbox_text_measurer){ .data = data, .length = length };
    measurer->count = tbox_string_view_codepoint_count(tbox_string_view_make(data, length));
    if (face == NULL)
        return;
    if (substitute == 0) {
        tbox_text_measurer_fill(measurer, face, letter_spacing, tbox_string_view_make(data, length));
        return;
    }
    /* The mask: one `substitute` per codepoint of `data`. */
    char unit[4];
    size_t unit_size = tbox_utf8_encode(unit, sizeof(unit), substitute);
    char *mask       = unit_size > 0 && measurer->count <= SIZE_MAX / unit_size ? malloc(measurer->count * unit_size + 1) : NULL;
    if (mask == NULL)
        return;
    for (size_t i = 0; i < measurer->count; i++)
        memcpy(mask + i * unit_size, unit, unit_size);
    tbox_text_measurer_fill(measurer, face, letter_spacing, tbox_string_view_make(mask, measurer->count * unit_size));
    free(mask);
}

size_t tbox_text_measurer_step(tbox_text_measurer *measurer) {
    if (measurer->at >= measurer->length)
        return measurer->length;
    measurer->at = tbox_utf8_next(measurer->data, measurer->length, measurer->at);
    measurer->index++;
    measurer->width = measurer->prefix != NULL ? measurer->prefix[measurer->index] : 0.0;
    return measurer->at;
}

double tbox_text_measurer_total(const tbox_text_measurer *measurer) {
    return measurer->prefix != NULL ? measurer->prefix[measurer->count] : 0.0;
}

void tbox_text_measurer_finish(tbox_text_measurer *measurer) {
    free(measurer->prefix);
    measurer->prefix = NULL;
}

double tbox_text_prefix_width(const tbox_font_face *face, double letter_spacing, tbox_string_view text, size_t offset) {
    tbox_text_measurer measurer;
    tbox_text_measurer_init(&measurer, face, letter_spacing, text.data, text.size);
    while (measurer.at < offset && measurer.at < measurer.length)
        tbox_text_measurer_step(&measurer);
    double width = measurer.width;
    tbox_text_measurer_finish(&measurer);
    return width;
}

bool tbox_font_shape(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction,
                     double origin_x, uint32_t cluster_base, tbox_glyph **out_glyphs, size_t *out_count, double *out_width) {
    *out_glyphs = NULL;
    *out_count  = 0;
    if (out_width != NULL)
        *out_width = 0.0;
    if (face == NULL || text.size == 0)
        return true;
    tbox_font_face *mutable_face = (tbox_font_face *)face; /* the shaper reuses per-face buffers */
    size_t capacity              = text.size + 8;
    tbox_glyph *glyphs           = tbox_arena_alloc(arena, capacity * sizeof(*glyphs));
    if (glyphs == NULL)
        return false;
    double width;
    size_t count = tbox_font_run(mutable_face, text, letter_spacing, direction, glyphs, capacity, &width);
    if (count > capacity) {
        glyphs = tbox_arena_alloc(arena, count * sizeof(*glyphs));
        if (glyphs == NULL)
            return false;
        tbox_font_run(mutable_face, text, letter_spacing, direction, glyphs, count, &width);
    }
    for (size_t i = 0; i < count; i++) {
        glyphs[i].x += origin_x;
        glyphs[i].cluster += cluster_base;
    }
    *out_glyphs = glyphs;
    *out_count  = count;
    if (out_width != NULL)
        *out_width = width;
    return true;
}

void tbox_font_shape_into(tbox_arena *arena, const tbox_font_face *face, tbox_string_view text, double letter_spacing,
                          const tbox_glyph **out_glyphs, size_t *out_count) {
    tbox_glyph *glyphs;
    size_t count;
    if (!tbox_font_shape(arena, face, text, letter_spacing, TBOX_FONT_DIRECTION_AUTO, 0.0, 0, &glyphs, &count, NULL)) {
        glyphs = NULL;
        count  = 0;
    }
    *out_glyphs = glyphs;
    *out_count  = count;
}

double tbox_font_measure_text_spaced(const tbox_font_face *face, tbox_string_view text, double letter_spacing) {
    if (face == NULL || text.size == 0) {
        return 0.0;
    }
    double width;
    tbox_font_run((tbox_font_face *)face, text, letter_spacing, TBOX_FONT_DIRECTION_AUTO, NULL, 0, &width);
    return width;
}

double tbox_font_measure_text(const tbox_font_face *face, tbox_string_view text) {
    return tbox_font_measure_text_spaced(face, text, 0.0);
}

tbox_font_glyph_bitmap tbox_font_rasterize_glyph(tbox_font_face *face, uint32_t codepoint) {
    if (face == NULL) {
        return (tbox_font_glyph_bitmap){ 0 };
    }
    return tbox_font_rasterize_glyph_id(face, FT_Get_Char_Index(face->ft_face, (FT_ULong)codepoint));
}

bool tbox_font_run_ink_bounds(const tbox_font_face *face, const tbox_glyph *glyphs, size_t count, double *top, double *bottom) {
    bool found = false;
    for (size_t i = 0; face != NULL && glyphs != NULL && i < count; i++) {
        if (FT_Load_Glyph(face->ft_face, (FT_UInt)glyphs[i].id, FT_LOAD_DEFAULT) != 0)
            continue;
        const FT_Glyph_Metrics *metrics = &face->ft_face->glyph->metrics;
        if (metrics->height <= 0 || metrics->width <= 0)
            continue;
        double y0 = glyphs[i].y - metrics->horiBearingY / 64.0;
        double y1 = y0 + metrics->height / 64.0;
        if (!found || y0 < *top)
            *top = y0;
        if (!found || y1 > *bottom)
            *bottom = y1;
        found = true;
    }
    return found;
}

tbox_font_glyph_bitmap tbox_font_rasterize_glyph_id(tbox_font_face *face, uint32_t glyph_id) {
    tbox_font_glyph_bitmap bitmap = { 0 };
    if (face == NULL) {
        return bitmap;
    }

    if (FT_Load_Glyph(face->ft_face, (FT_UInt)glyph_id, FT_LOAD_DEFAULT) != 0) {
        return bitmap;
    }

    FT_GlyphSlot slot = face->ft_face->glyph;
    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        return bitmap;
    }

    bitmap.width     = (int)slot->bitmap.width;
    bitmap.height    = (int)slot->bitmap.rows;
    bitmap.bearing_x = slot->bitmap_left;
    bitmap.bearing_y = slot->bitmap_top;
    bitmap.advance   = slot->advance.x / 64.0;
    bitmap.alpha     = (bitmap.width > 0 && bitmap.height > 0) ? slot->bitmap.buffer : NULL;
    return bitmap;
}

/* Stroke only: FreeType builds both borders of the glyph outline, leaving
 * the interior unpainted. The radius is half the CSS stroke width. */
tbox_font_glyph_bitmap tbox_font_rasterize_glyph_stroke(tbox_font_face *face, uint32_t glyph_id, double width) {
    tbox_font_glyph_bitmap bitmap = {0};
    if (face == NULL) return bitmap;
    if (face->stroke_glyph != NULL) {
        FT_Done_Glyph(face->stroke_glyph);
        face->stroke_glyph = NULL;
    }
    if (!isfinite(width) || width <= 0.0 || width >= (double)LONG_MAX / 64.0) return bitmap;
    if (FT_Load_Glyph(face->ft_face, (FT_UInt)glyph_id, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0 ||
        face->ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return bitmap;
    double advance = face->ft_face->glyph->advance.x / 64.0;
    FT_Stroker stroker;
    if (FT_Stroker_New(face->ft_face->glyph->library, &stroker) != 0) return bitmap;
    FT_Fixed radius = (FT_Fixed)fmax(1.0, round(width * 32.0));
    FT_Stroker_Set(stroker, radius, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
    bool ok = FT_Get_Glyph(face->ft_face->glyph, &face->stroke_glyph) == 0 &&
        FT_Glyph_Stroke(&face->stroke_glyph, stroker, true) == 0 &&
        FT_Glyph_To_Bitmap(&face->stroke_glyph, FT_RENDER_MODE_NORMAL, NULL, true) == 0;
    FT_Stroker_Done(stroker);
    if (!ok) {
        if (face->stroke_glyph != NULL) FT_Done_Glyph(face->stroke_glyph);
        face->stroke_glyph = NULL;
        return bitmap;
    }
    FT_BitmapGlyph glyph = (FT_BitmapGlyph)face->stroke_glyph;
    bitmap.width = (int)glyph->bitmap.width;
    bitmap.height = (int)glyph->bitmap.rows;
    bitmap.bearing_x = glyph->left;
    bitmap.bearing_y = glyph->top;
    bitmap.advance = advance;
    bitmap.alpha = bitmap.width > 0 && bitmap.height > 0 ? glyph->bitmap.buffer : NULL;
    return bitmap;
}
