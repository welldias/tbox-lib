#ifndef TBOX_LAYOUT_INTERNAL_H
#define TBOX_LAYOUT_INTERNAL_H

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "html_parser/tbox_html_entities.h"
#include <stddef.h>
#include <stdint.h>
#include <tbox/image.h>
#include <tbox/layout.h>

typedef struct tbox_layout_containing_block {
    double x;
    double y;
    double width;
    double height;
    bool height_definite;
} tbox_layout_containing_block;

typedef struct tbox_layout_positioned_context {
    tbox_rect nearest_ancestor;
    tbox_rect viewport;
} tbox_layout_positioned_context;

typedef struct tbox_layout_forced_size {
    bool has_width, has_height;
    double width, height;
} tbox_layout_forced_size;

/* One word from the flat, document-order (word, face) sequence a
 * text-bearing box's direct children contribute (see
 * tbox_layout_collect_words) -- `width`/`space_width` are pre-measured
 * (via tbox_font_measure_text, against `face`) so the greedy wrap pass
 * below never re-measures the same word twice. */

typedef struct tbox_layout_word {
    tbox_string_view text;
    const tbox_font_face *face;
    /* the SAME tbox_style that already decided `face` above (and
     * ultimately becomes tbox_layout_text_run.style once this word is
     * merged into a run, see tbox_layout_build_line_runs below) -- populated
     * at the exact same call sites that already populate `face`, no new
     * "which style" decision. Never NULL: every pusher of a
     * tbox_layout_word (tbox_layout_push_words, tbox_layout_push_hard_break)
     * requires a non-NULL style, same as `face` already effectively required
     * a non-NULL cache lookup to have produced a word at all. */
    const tbox_style *style;
    double width;         /* tbox_font_measure_text(face, text), OR (image word) the resolved CSS/attribute content width */
    double space_width;   /* tbox_font_measure_text(face, " ") -- the gap this word's face would render before it */
    bool no_space_before; /* continuation of an overflow-wrap split word */

    /* An <img> word instead of a text word -- see tbox_layout_push_image_word.
     * NULL for every ordinary word (text or hard-break). `face`/`style`
     * above are still populated for an image word (the surrounding text
     * context's own face/style, purely for `space_width` measurement and
     * run-merge-boundary comparisons -- see tbox_layout_build_line_runs),
     * but `image`/`image_height` are what Layout/Render actually use to
     * size and paint it: `image` is the DECODED image (intrinsic pixel
     * dimensions + pixel data, owned by the tbox_image_cache), `width`
     * above and `image_height` are the RESOLVED CSS content box size for
     * this particular <img> (may differ from `image`'s own intrinsic
     * dimensions -- e.g. an explicit `style="width:600px;height:500px"` on
     * a smaller source image -- Output Display scales when painting, see
     * tbox_raster_image). */
    const tbox_image *image;
    double image_height;

    /* true for an entry pushed by tbox_layout_push_hard_break --
     * a forced line break (<br>, or the boundary between two physical
     * lines inside <pre>), not a real word. `text`/`width`/`space_width`
     * are all zeroed for such an entry (see tbox_layout_push_hard_break);
     * `face` is still populated, kept only as a line-height fallback for a
     * blank line between two consecutive hard breaks (see
     * tbox_layout_break_lines). Every function that pushes a REAL word
     * (tbox_layout_push_words, tbox_layout_collect_preformatted_words) sets
     * this explicitly to false -- never left to tbox_vector's zero-init,
     * per ARCHITECTURE.md's "v11" section. */
    bool hard_break;

    /* an `inline-block` placed as one unbreakable word. `atomic`
     * is its box, already built at the origin and moved onto its line by
     * tbox_layout_build_line_runs; `width`/`image_height` are its margin
     * box size and `atomic_ascent` its baseline, measured from the margin
     * box top. NULL for every other word. */
    struct tbox_layout_box *atomic;
    double atomic_ascent;
    const tbox_html_node *atomic_node; /* the inline-block element, for a rebuild in place */
    double atomic_containing_width;    /* the width it was built against */
    /* min-content width when it differs from `width` (an
     * inline-block measured for intrinsic sizing); see
     * tbox_layout_measure_words. */
    bool has_min_width;
    double min_width;
} tbox_layout_word;

/* A half-open range [start, end) into a tbox_layout_word array, one greedy
 * line's worth of words, plus that line's own height (max line-height among
 * the faces actually used by words in this range -- see ARCHITECTURE.md;
 * degenerates to "the one face's line-height" when every word on the line
 * shares a face, the common v2 case). */

typedef struct tbox_layout_line {
    size_t start, end;
    double height;
    /* the max tbox_font_face_ascent among the faces used by words
     * in [start, end) -- same "max across the line" loop that already
     * computes `height` above (tbox_font_face_line_height), just with
     * tbox_font_face_ascent instead; see tbox_layout_break_lines's three
     * line-closing points. Used by tbox_layout_build_line_runs to align
     * every run's own baseline with this line's dominant one instead of
     * `line_y` (the line's TOP) directly -- see ARCHITECTURE.md's "v13 --
     * Layout Tree" section for why this is needed now (the first time this
     * project has more than one font size on the same line). */
    double ascent;
} tbox_layout_line;

typedef struct tbox_layout_inline_state {
    bool has_content;    /* a word was pushed since the start or the last hard break */
    bool trailing_space; /* the source text since that word ended in whitespace */
} tbox_layout_inline_state;

typedef struct tbox_layout_intrinsic {
    double min, max;
} tbox_layout_intrinsic;

typedef struct tbox_layout_classification {
    bool text, image, table, table_row, flex;
} tbox_layout_classification;

typedef struct tbox_layout_replaced_image {
    const tbox_image *image;
    tbox_string_view fallback_text;
    const tbox_font_face *fallback_face;
} tbox_layout_replaced_image;

tbox_layout_replaced_image tbox_layout_image_prepare(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_image_cache *images);
double tbox_layout_image_auto_width(const tbox_layout_replaced_image *content, const tbox_style *style, tbox_layout_containing_block container, double vertical_edges);
double tbox_layout_image_height(const tbox_layout_replaced_image *content, const tbox_style *style, tbox_layout_containing_block container, double content_width, double vertical_edges);
void tbox_layout_image_append_run(tbox_arena *arena, const tbox_layout_replaced_image *content, const tbox_style *style, tbox_layout_box *box);

double tbox_layout_textarea_auto_width(const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, double fallback_width);
double tbox_layout_textarea_height(const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_layout_containing_block container, double vertical_edges, double text_height);

tbox_layout_classification tbox_layout_classify(const tbox_html_node *node, const tbox_style *style, const double *row_column_widths, size_t row_column_count);

#define TBOX_LAYOUT_FLEX_INFINITE 1e300

extern const tbox_style tbox_layout_default_style;

void tbox_layout_break_lines(const tbox_layout_word *words, size_t word_count, double available_width, double first_line_indent, bool no_wrap, const tbox_font_face *block_face, tbox_vector *lines);
tbox_layout_box *tbox_layout_build_anonymous_box(tbox_arena *arena, const tbox_html_node *run_start, const tbox_html_node *run_end, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double cursor_y, double available_width, const tbox_layout_positioned_context *context);
void tbox_layout_build_checkbox_checkmark(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_layout_box *box);
double tbox_layout_build_children(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block children_container, double start_y, tbox_layout_box *parent_box, tbox_layout_positioned_context positioned_context, const tbox_style *container_style);
tbox_layout_box *tbox_layout_build_element(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count);
tbox_layout_box *tbox_layout_build_element_sized(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_layout_containing_block container, double cursor_y, tbox_layout_positioned_context positioned_context, const double *row_column_widths, size_t row_column_count, const tbox_layout_forced_size *forced);
double tbox_layout_build_flex(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double content_width, double content_height, bool height_definite, double min_height, double max_height, tbox_layout_box *box, tbox_layout_positioned_context context);
double tbox_layout_build_line_runs(tbox_arena *arena, const tbox_layout_word *words, const tbox_layout_line *line, double line_y, double content_x, const tbox_font_face *block_face, double justify_gap, tbox_vector *runs);
double tbox_layout_build_table_children(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double *content_width, tbox_layout_box *table_box, tbox_layout_positioned_context positioned_context);
double tbox_layout_build_table_extended(tbox_arena *arena, const tbox_html_node *table, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double x, double y, double *table_width, tbox_layout_box *table_box, tbox_layout_positioned_context positioned_context);
double tbox_layout_build_table_row_children(tbox_arena *arena, const tbox_html_node *row_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, const double *column_widths, size_t column_count, tbox_layout_box *row_box, tbox_layout_positioned_context positioned_context);
double tbox_layout_build_text_runs(tbox_arena *arena, const tbox_html_node *node, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double available_width, tbox_layout_box *box, const tbox_layout_positioned_context *context);
void tbox_layout_collect_preformatted_words(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words);
void tbox_layout_collect_words(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, tbox_vector *words);
void tbox_layout_collect_words_in(tbox_arena *arena, const tbox_html_node *first_sibling, const tbox_html_node *end_exclusive, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, bool inside_inline, bool measure_only, tbox_layout_inline_state *state, tbox_vector *words);
double tbox_layout_constrain_height(const tbox_style *style, double height, double base, bool base_definite, double edges);
double tbox_layout_constrain_width(const tbox_style *style, double width, double base, double edges);
void tbox_layout_ellipsize_line(tbox_vector *runs, size_t first, double content_x, double available_width, const tbox_style *style, const tbox_font_face *face, const tbox_layout_line *line, double line_y);
bool tbox_layout_has_escaping_positioned(const tbox_html_node *node, const tbox_style_table *styles, bool contained);
const tbox_html_node *tbox_layout_inline_run_end(const tbox_html_node *run_start, const tbox_style_table *styles);
bool tbox_layout_input_is_text_tag(const tbox_html_node *node);
void tbox_layout_input_push_value_word(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words);
tbox_layout_intrinsic tbox_layout_intrinsic_content(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images);
tbox_layout_intrinsic tbox_layout_intrinsic_outer(tbox_arena *arena, const tbox_html_node *node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images);
bool tbox_layout_is_hidden_input(const tbox_html_node *node);
bool tbox_layout_is_replaced_image(const tbox_html_node *node);
bool tbox_layout_is_inline_run_trigger(tbox_arena *arena, const tbox_html_node *child, const tbox_style_table *styles);
bool tbox_layout_is_select(const tbox_html_node *node);
bool tbox_layout_is_text_tag(const tbox_html_node *node);
bool tbox_layout_is_textarea(const tbox_html_node *node);
tbox_layout_intrinsic tbox_layout_measure_words(const tbox_layout_word *words, size_t count, bool no_wrap);
tbox_layout_word *tbox_layout_new_word(tbox_vector *words);
void tbox_layout_push_atomic_word(tbox_arena *arena, const tbox_html_node *node, const tbox_style *node_style, const tbox_font_face *context_face, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double containing_width, bool measure_only, tbox_vector *words);
void tbox_layout_push_hard_break(tbox_vector *words, const tbox_font_face *face, const tbox_style *style);
void tbox_layout_push_image_word(tbox_arena *arena, const tbox_html_node *img_node, const tbox_style *img_style, const tbox_font_face *context_face, tbox_image_cache *images, double containing_width, tbox_vector *words);
void tbox_layout_push_list_marker(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words);
void tbox_layout_push_text_words(tbox_arena *arena, tbox_vector *words, tbox_string_view raw, const tbox_font_face *face, const tbox_style *style);
void tbox_layout_push_words(tbox_vector *words, tbox_string_view collapsed, const tbox_font_face *face, const tbox_style *style);
double tbox_layout_resolve_absolute_edge(tbox_style_length primary, tbox_style_length opposite, double container_origin, double container_size, double margin_box_size);
double tbox_layout_resolve_edge(tbox_style_length length, double percent_base);
double tbox_layout_resolve_offset(tbox_style_length primary, tbox_style_length opposite, double percent_base, bool percent_base_definite);
void tbox_layout_split_overlong_words(tbox_arena *arena, tbox_vector *words, double available_width, double first_indent);
double tbox_layout_style_line_height(const tbox_style *style, const tbox_font_face *face);
const tbox_style *tbox_layout_style_or_default(const tbox_style_table *styles, const tbox_html_node *node);
bool tbox_layout_table_cell_node(const tbox_html_node *node);
bool tbox_layout_table_extended(const tbox_html_node *table, const tbox_style_table *styles);
double tbox_layout_table_longest_word(const tbox_font_face *face, tbox_string_view text, double letter_spacing);
void tbox_layout_table_shift_y(tbox_layout_box *box, double amount);
bool tbox_layout_tag(const tbox_html_node *node, const char *name);
unsigned tbox_layout_textarea_size(const tbox_html_node *node, const char *name, unsigned fallback);
tbox_string_view tbox_layout_transform_text(tbox_arena *arena, tbox_string_view text, tbox_style_text_transform transform);
void tbox_layout_translate(tbox_layout_box *box, double dx, double dy);
bool tbox_layout_white_space_nowrap(const tbox_style *style);

#endif /* TBOX_LAYOUT_INTERNAL_H */
