/* The HarfBuzz shaper (docs/09-texto-e-fontes.md, "Shaping"): GSUB/GPOS --
 * ligatures, contextual forms, positioned marks, OpenType kerning.
 * Compiled only when HarfBuzz is found and TBOX_ENABLE_HARFBUZZ is ON.
 *
 * The font is read from the bytes the face already owns, without hb-ft.
 * A sub-font overrides advances with FreeType's hinted values, matching
 * the raster's bitmaps, while inheriting HarfBuzz's OpenType functions. */
#include "tbox_font_face_internal.h"

#include <hb.h>
#include <hb-ot.h>
#include <stdlib.h>

typedef struct tbox_font_harfbuzz {
    hb_blob_t *blob;
    hb_face_t *face;
    hb_font_t *parent; /* HarfBuzz's own OpenType functions, at the face's ppem */
    hb_font_t *font;   /* `parent` with FreeType advances */
    hb_font_funcs_t *funcs;
    hb_buffer_t *buffer; /* reused by every shaping call on this face */
} tbox_font_harfbuzz;

static hb_position_t tbox_font_harfbuzz_advance(hb_font_t *font, void *font_data, hb_codepoint_t glyph, void *user_data) {
    (void)font;
    (void)user_data;
    return (hb_position_t)tbox_font_face_glyph_advance((tbox_font_face *)font_data, (uint32_t)glyph); /* 26.6, the font's scale below */
}

static void tbox_font_harfbuzz_free(tbox_font_harfbuzz *hb) {
    if (hb == NULL)
        return;
    hb_buffer_destroy(hb->buffer);
    hb_font_destroy(hb->font);
    hb_font_funcs_destroy(hb->funcs);
    hb_font_destroy(hb->parent);
    hb_face_destroy(hb->face);
    hb_blob_destroy(hb->blob);
    free(hb);
}

bool tbox_font_shaper_attach(tbox_font_face *face) {
    face->shaper           = NULL;
    tbox_font_harfbuzz *hb = calloc(1, sizeof(*hb));
    if (hb == NULL)
        return false;
    hb->blob   = hb_blob_create((const char *)face->font_data_copy, (unsigned int)face->font_data_size, HB_MEMORY_MODE_READONLY, NULL, NULL);
    hb->face   = hb_face_create(hb->blob, 0);
    hb->parent = hb_font_create(hb->face);
    /* Positions in 26.6 pixels, like FreeType's. */
    unsigned int ppem = face->ft_face->size->metrics.x_ppem;
    hb_font_set_scale(hb->parent, (int)ppem * 64, (int)ppem * 64);
    hb_font_set_ppem(hb->parent, ppem, ppem);
    hb->funcs = hb_font_funcs_create();
    hb_font_funcs_set_glyph_h_advance_func(hb->funcs, tbox_font_harfbuzz_advance, NULL, NULL);
    hb_font_funcs_make_immutable(hb->funcs);
    hb->font = hb_font_create_sub_font(hb->parent);
    hb_font_set_funcs(hb->font, hb->funcs, face, NULL);
    hb->buffer = hb_buffer_create();
    if (!hb_buffer_allocation_successful(hb->buffer) || hb_face_get_glyph_count(hb->face) == 0) {
        tbox_font_harfbuzz_free(hb);
        return false;
    }
    face->shaper = hb;
    return true;
}

void tbox_font_shaper_detach(tbox_font_face *face) {
    tbox_font_harfbuzz_free((tbox_font_harfbuzz *)face->shaper);
    face->shaper = NULL;
}

static hb_feature_t tbox_font_harfbuzz_off(hb_tag_t tag) {
    return (hb_feature_t){ .tag = tag, .value = 0, .start = HB_FEATURE_GLOBAL_START, .end = HB_FEATURE_GLOBAL_END };
}

size_t tbox_font_shaper_run(tbox_font_face *face, tbox_string_view text, double letter_spacing, tbox_font_direction direction, tbox_glyph *out, size_t capacity, double *out_width) {
    tbox_font_harfbuzz *hb = (tbox_font_harfbuzz *)face->shaper;
    *out_width             = 0.0;
    if (hb == NULL || text.size == 0)
        return 0;

    hb_buffer_clear_contents(hb->buffer);
    hb_buffer_add_utf8(hb->buffer, text.data, (int)text.size, 0, (int)text.size);
    if (face->options.language != 0)
        hb_buffer_set_language(hb->buffer, hb_ot_tag_to_language(face->options.language));
    hb_buffer_guess_segment_properties(hb->buffer);
    /* A direction from the bidi levels wins over the guess; HarfBuzz then
     * mirrors brackets in RTL (L4) and returns glyphs in visual order. */
    if (direction != TBOX_FONT_DIRECTION_AUTO)
        hb_buffer_set_direction(hb->buffer, direction == TBOX_FONT_DIRECTION_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);

    /* font-kerning: none drops `kern`; a nonzero letter-spacing drops the
     * optional ligatures (CSS Text 3, letter-spacing). */
    hb_feature_t features[TBOX_FONT_MAX_FEATURES + 4];
    unsigned int feature_count = 0;
    if (!face->kerning_requested)
        features[feature_count++] = tbox_font_harfbuzz_off(HB_TAG('k', 'e', 'r', 'n'));
    /* CSS variants, then explicit feature settings; these override kerning.
     * Letter spacing disables optional ligatures after explicit settings. */
    for (size_t i = 0; i < face->options.count; i++)
        features[feature_count++] = (hb_feature_t){ face->options.items[i].tag, face->options.items[i].value,
                                                   HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END };
    if (letter_spacing != 0.0) {
        features[feature_count++] = tbox_font_harfbuzz_off(HB_TAG('l', 'i', 'g', 'a'));
        features[feature_count++] = tbox_font_harfbuzz_off(HB_TAG('c', 'l', 'i', 'g'));
        features[feature_count++] = tbox_font_harfbuzz_off(HB_TAG('d', 'l', 'i', 'g'));
    }
    hb_shape(hb->font, hb->buffer, features, feature_count);

    unsigned int length;
    const hb_glyph_info_t *info         = hb_buffer_get_glyph_infos(hb->buffer, &length);
    const hb_glyph_position_t *position = hb_buffer_get_glyph_positions(hb->buffer, NULL);
    double pen                          = 0.0;
    for (unsigned int i = 0; i < length; i++) {
        double advance = position[i].x_advance / 64.0;
        /* letter-spacing once per cluster, after its last glyph */
        if (i + 1 == length || info[i + 1].cluster != info[i].cluster)
            advance += letter_spacing;
        if (i < capacity)
            out[i] = (tbox_glyph){
                .id      = info[i].codepoint,
                .cluster = info[i].cluster,
                .x       = pen + position[i].x_offset / 64.0,
                .y       = -position[i].y_offset / 64.0, /* HarfBuzz y grows upward */
                .advance = advance,
            };
        pen += advance;
    }
    *out_width = pen;
    return length;
}
