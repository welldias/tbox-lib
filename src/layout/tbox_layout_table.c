#include "tbox_layout_internal.h"
#include <string.h>
#include <stdio.h>

/* Original direct-row table path, retained for the existing simple tables.
 * The extended grid path in tbox_layout_table_grid.c handles sections, spans, captions and cols. */

/* Counts `table_node`'s columns: the MAX, across every direct <tr> child,
 * of that row's own direct <th>/<td> element-child count. A ragged table
 * (rows with fewer cells than the widest one) is tolerated -- those rows
 * simply leave their trailing columns empty, never an error. */
static size_t tbox_layout_table_column_count(const tbox_html_node *table_node) {
    size_t max_count = 0;

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        size_t count = 0;
        for (const tbox_html_node *cell = row->first_child; cell != NULL; cell = cell->next_sibling) {
            if (cell->type == TBOX_HTML_NODE_ELEMENT && (tbox_string_view_equal_cstr(cell->element.tag_name, "td") || tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
                count++;
            }
        }
        if (count > max_count) {
            max_count = count;
        }
    }

    return max_count;
}

double tbox_layout_table_longest_word(const tbox_font_face *face, tbox_string_view text, double letter_spacing);
void tbox_layout_table_shift_y(tbox_layout_box *box, double amount);

/* Approximate auto table layout with the minimum (longest word) and maximum
 * (unwrapped text) width of each column. Interpolating between those limits
 * lets a long, wrapping cell use available room without taking it from
 * columns whose contents already fit. */
static void tbox_layout_table_compute_column_widths(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, size_t column_count, double *content_width, double *out_widths) {
    double *min_widths = tbox_arena_alloc_zero(arena, sizeof(double) * column_count);
    for (size_t i = 0; i < column_count; i++) {
        out_widths[i] = 0.0;
    }

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        size_t column_index = 0;
        for (const tbox_html_node *cell = row->first_child; cell != NULL; cell = cell->next_sibling) {
            if (cell->type != TBOX_HTML_NODE_ELEMENT || (!tbox_string_view_equal_cstr(cell->element.tag_name, "td") && !tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
                continue;
            }
            if (column_index >= column_count) {
                break;
            }

            const tbox_style *cell_style = tbox_layout_style_or_default(styles, cell);
            double padding_left          = tbox_layout_resolve_edge(cell_style->padding[3], *content_width);
            double padding_right         = tbox_layout_resolve_edge(cell_style->padding[1], *content_width);
            double border_x              = tbox_style_border_side_width(cell_style, 1) + tbox_style_border_side_width(cell_style, 3);

            double text_width          = 0.0;
            double word_width          = 0.0;
            tbox_vector words;
            tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
            tbox_layout_collect_words(arena, cell->first_child, NULL, cell_style, styles, fonts, images, *content_width, &words);
            if (words.length > 0) {
                double line_width = 0.0;
                bool first_word = true;
                for (size_t i = 0; i < words.length; i++) {
                    const tbox_layout_word *word = tbox_vector_at(&words, i);
                    if (word->hard_break) {
                        if (line_width > text_width) text_width = line_width;
                        line_width = 0.0;
                        first_word = true;
                        continue;
                    }
                    if (!first_word) line_width += word->space_width;
                    line_width += word->width;
                    if (word->width > word_width) word_width = word->width;
                    first_word = false;
                }
                if (line_width > text_width) text_width = line_width;
            } else {
                const tbox_font_face *face = tbox_layout_style_face(fonts, cell_style);
                tbox_string_view text = tbox_html_node_text_content(arena, cell);
                if (face != NULL) {
                    text_width = tbox_font_measure_text_spaced(face, text, cell_style->letter_spacing);
                    word_width = tbox_layout_table_longest_word(face, text, cell_style->letter_spacing);
                }
            }

            double edges = padding_left + padding_right + border_x;
            double natural_width = text_width + edges;
            double minimum_width = word_width + edges;
            if (cell_style->min_width.kind != TBOX_STYLE_LENGTH_AUTO) {
                double css_min = tbox_layout_resolve_edge(cell_style->min_width, *content_width) +
                    (cell_style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? 0.0 : edges);
                if (minimum_width < css_min) minimum_width = css_min;
                if (natural_width < css_min) natural_width = css_min;
            }
            if (natural_width > out_widths[column_index]) {
                out_widths[column_index] = natural_width;
            }
            if (minimum_width > min_widths[column_index]) {
                min_widths[column_index] = minimum_width;
            }

            column_index++;
        }
    }

    double total_natural = 0.0;
    double total_minimum = 0.0;
    for (size_t i = 0; i < column_count; i++) {
        total_natural += out_widths[i];
        total_minimum += min_widths[i];
    }
    if (*content_width < total_minimum) *content_width = total_minimum;

    if (total_natural <= 0.0) {
        double equal_share = *content_width / (double)column_count;
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] = equal_share;
        }
        return;
    }

    if (*content_width >= total_minimum && *content_width < total_natural) {
        double fraction = (*content_width - total_minimum) / (total_natural - total_minimum);
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] = min_widths[i] + fraction * (out_widths[i] - min_widths[i]);
        }
    } else {
        double scale = *content_width / total_natural;
        for (size_t i = 0; i < column_count; i++) {
            out_widths[i] *= scale;
        }
    }
}

/* Lays out one <tr>'s direct <th>/<td> children SIDE BY SIDE (not stacked
 * -- unlike every other container in this file, a table row is a
 * horizontal formatting context) at x-offsets derived from
 * `column_widths`. Each cell is built via tbox_layout_build_element itself
 * -- cells are text tags (see tbox_layout_is_text_tag), so this is the
 * EXACT same per-box machinery (margin/padding/border/content resolution,
 * then tbox_layout_build_text_runs for wrapped text) every other text tag
 * already uses, just called with `container.width = column_widths[i]`
 * instead of the row's own full width. Returns the row's own content
 * height: the MAX of every cell's margin_box.height. All cell borders and
 * backgrounds then extend to that height, as in a browser table row. */
double tbox_layout_build_table_row_children(tbox_arena *arena, const tbox_html_node *row_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, const double *column_widths, size_t column_count, tbox_layout_box *row_box, tbox_layout_positioned_context positioned_context) {
    double max_height          = 0.0;
    double cursor_x            = content_x;
    size_t column_index        = 0;
    tbox_layout_box *previous  = NULL;

    for (const tbox_html_node *cell = row_node->first_child; cell != NULL; cell = cell->next_sibling) {
        if (cell->type != TBOX_HTML_NODE_ELEMENT || (!tbox_string_view_equal_cstr(cell->element.tag_name, "td") && !tbox_string_view_equal_cstr(cell->element.tag_name, "th"))) {
            continue;
        }
        if (column_index >= column_count) {
            break; /* defensive only -- column_count is already the max across every row, so this row can never exceed it */
        }

        const tbox_style *cell_style = tbox_layout_style_or_default(styles, cell);
        if (cell_style->display == TBOX_STYLE_DISPLAY_NONE) {
            cursor_x += column_widths[column_index];
            column_index++;
            continue;
        }

        tbox_layout_containing_block cell_container = {
            .x               = cursor_x,
            .y               = content_y,
            .width           = column_widths[column_index],
            .height          = 0.0,
            .height_definite = false,
        };

        tbox_layout_box *cell_box = tbox_layout_build_element(arena, cell, styles, fonts, images, cell_container, content_y, positioned_context, NULL, 0);
        cell_box->parent          = row_box;
        if (previous == NULL) {
            row_box->first_child = cell_box;
        } else {
            previous->next_sibling = cell_box;
        }
        row_box->last_child = cell_box;
        previous            = cell_box;

        if (cell_box->margin_box.height > max_height) {
            max_height = cell_box->margin_box.height;
        }

        cursor_x += column_widths[column_index];
        column_index++;
    }

    for (tbox_layout_box *cell_box = row_box->first_child; cell_box != NULL; cell_box = cell_box->next_sibling) {
        double growth = max_height - cell_box->margin_box.height;
        if (growth <= 0.0) continue;
        const tbox_style *style = cell_box->style;
        double align = style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BOTTOM ? growth :
            style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE ? growth / 2.0 : 0.0;
        for (size_t run = 0; run < cell_box->text_run_count; run++)
            cell_box->text_runs[run].rect.y += align;
        for (tbox_layout_box *child = cell_box->first_child; child != NULL; child = child->next_sibling)
            tbox_layout_table_shift_y(child, align);
        cell_box->margin_box.height += growth;
        cell_box->border_box.height += growth;
        cell_box->padding_box.height += growth;
        cell_box->content_box.height += growth;
    }

    return max_height;
}

/* Lays out `table_node`'s direct <tr> children STACKED vertically (like any
 * other block-level sibling sequence, but bespoke here rather than reusing
 * tbox_layout_build_children -- a table row never collapses margins with
 * anything and never triggers the v14 loose-inline-content mechanism,
 * neither of which apply inside a table). Computes `column_count`/
 * `column_widths` ONCE (tbox_layout_table_column_count/
 * tbox_layout_table_compute_column_widths above) before building any row,
 * since every row's cells need the SAME column widths to stay aligned.
 * Each row's own box is built via tbox_layout_build_element too, passing
 * `column_widths`/`column_count` through its trailing parameters so IT
 * takes the "table row" branch (see that function's dispatch). Returns the
 * table's own content height: the sum of every row's margin_box.height. */
double tbox_layout_build_table_children(tbox_arena *arena, const tbox_html_node *table_node, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double *content_width, tbox_layout_box *table_box, tbox_layout_positioned_context positioned_context) {
    size_t column_count = tbox_layout_table_column_count(table_node);
    if (column_count == 0) {
        return 0.0;
    }

    double *column_widths = (double *)tbox_arena_alloc(arena, sizeof(double) * column_count);
    tbox_layout_table_compute_column_widths(arena, table_node, styles, fonts, images, column_count, content_width, column_widths);

    double cursor_y           = content_y;
    tbox_layout_box *previous = NULL;

    for (const tbox_html_node *row = table_node->first_child; row != NULL; row = row->next_sibling) {
        if (row->type != TBOX_HTML_NODE_ELEMENT || !tbox_string_view_equal_cstr(row->element.tag_name, "tr")) {
            continue;
        }

        const tbox_style *row_style = tbox_layout_style_or_default(styles, row);
        if (row_style->display == TBOX_STYLE_DISPLAY_NONE) {
            continue;
        }

        tbox_layout_containing_block row_container = {
            .x               = content_x,
            .y               = content_y,
            .width           = *content_width,
            .height          = 0.0,
            .height_definite = false,
        };

        tbox_layout_box *row_box = tbox_layout_build_element(arena, row, styles, fonts, images, row_container, cursor_y, positioned_context, column_widths, column_count);
        row_box->parent          = table_box;
        if (previous == NULL) {
            table_box->first_child = row_box;
        } else {
            previous->next_sibling = row_box;
        }
        table_box->last_child = row_box;
        previous              = row_box;

        cursor_y += row_box->margin_box.height;
    }

    return cursor_y - content_y;
}

