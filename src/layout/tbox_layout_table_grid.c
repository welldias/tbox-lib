#include "tbox_layout_internal.h"
#include <string.h>
#include <stdio.h>

/* The extended table path keeps a small, explicit cell grid. The direct-row path in tbox_layout_table.c remains in use for plain tables. */
#define TBOX_TABLE_MAX_COLUMNS 1000
typedef struct tbox_table_row {
    const tbox_html_node *node, *group;
    size_t group_id;
    tbox_layout_box *box;
    double height, y;
} tbox_table_row;

typedef struct tbox_table_cell {
    const tbox_html_node *node;
    tbox_layout_box *box;
    size_t row, column, rows, columns;
} tbox_table_cell;

typedef struct tbox_table_column {
    const tbox_html_node *node, *group;
    double min_width, width, x;
} tbox_table_column;

bool tbox_layout_tag(const tbox_html_node *node, const char *name) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
        tbox_string_view_equal_cstr(node->element.tag_name, name);
}

static bool tbox_layout_table_section(const tbox_html_node *node) {
    return tbox_layout_tag(node, "thead") || tbox_layout_tag(node, "tbody") ||
        tbox_layout_tag(node, "tfoot");
}

bool tbox_layout_table_cell_node(const tbox_html_node *node) {
    return tbox_layout_tag(node, "td") || tbox_layout_tag(node, "th");
}

static size_t tbox_layout_table_span(const tbox_html_node *node, const char *name, bool zero_valid) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    if (attribute == NULL || attribute->value.size == 0) return 1;
    size_t value = 0;
    for (size_t i = 0; i < attribute->value.size; i++) {
        char c = attribute->value.data[i];
        if (c < '0' || c > '9') return 1;
        value = value * 10 + (size_t)(c - '0');
        if (value > TBOX_TABLE_MAX_COLUMNS) return TBOX_TABLE_MAX_COLUMNS;
    }
    return value == 0 && !zero_valid ? 1 : value;
}

static double tbox_layout_table_css_width(const tbox_style *style, double base) {
    double width = 0.0;
    if (style->width.kind == TBOX_STYLE_LENGTH_PX) width = style->width.value;
    if (style->width.kind == TBOX_STYLE_LENGTH_PERCENT) {
        width = base * style->width.value / 100.0;
    }
    width = tbox_layout_constrain_width(style, width, base, 0.0);
    return width > 0.0 ? width : 0.0;
}

static void tbox_layout_table_append(tbox_layout_box *parent, tbox_layout_box *child) {
    child->parent = parent;
    if (parent->last_child != NULL) parent->last_child->next_sibling = child;
    else parent->first_child = child;
    parent->last_child = child;
}

static tbox_layout_box *tbox_layout_table_box(tbox_arena *arena, const tbox_html_node *node,
                                               const tbox_style_table *styles, tbox_rect rect) {
    tbox_layout_box *box = tbox_arena_alloc_zero(arena, sizeof(*box));
    box->node = node;
    box->style = tbox_layout_style_or_default(styles, node);
    box->margin_box = box->border_box = box->padding_box = box->content_box = rect;
    return box;
}

void tbox_layout_table_shift_y(tbox_layout_box *box, double amount) {
    tbox_layout_translate(box, 0.0, amount);
}

bool tbox_layout_table_extended(const tbox_html_node *table, const tbox_style_table *styles) {
    const tbox_style *style = tbox_layout_style_or_default(styles, table);
    if (style->border_collapse || style->border_spacing_x > 0.0 || style->border_spacing_y > 0.0) return true;
    for (const tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling) {
        if (tbox_layout_table_section(child) || tbox_layout_tag(child, "caption") ||
            tbox_layout_tag(child, "col") || tbox_layout_tag(child, "colgroup")) return true;
        if (!tbox_layout_tag(child, "tr")) continue;
        if (tbox_layout_style_or_default(styles, child)->display == TBOX_STYLE_DISPLAY_NONE) return true;
        for (const tbox_html_node *cell = child->first_child; cell != NULL; cell = cell->next_sibling)
            if (tbox_layout_table_cell_node(cell) &&
                (tbox_html_node_get_attribute(cell, tbox_string_view_make("colspan", 7)) != NULL ||
                 tbox_html_node_get_attribute(cell, tbox_string_view_make("rowspan", 7)) != NULL ||
                 tbox_layout_style_or_default(styles, cell)->display == TBOX_STYLE_DISPLAY_NONE ||
                 tbox_layout_style_or_default(styles, cell)->width.kind != TBOX_STYLE_LENGTH_AUTO)) return true;
    }
    return false;
}

/* Collapsed borders: the widest `side` (0 top, 1 right, 2 bottom, 3 left)
 * among the boxes meeting at an edge wins; later calls win ties. */
static void tbox_layout_table_choose_border(const tbox_style *style, size_t side, double *width, tbox_css_rgba *color) {
    double side_width = tbox_style_border_side_width(style, side);
    if (style != NULL && side_width >= *width && side_width > 0.0) {
        *width = side_width;
        *color = tbox_style_border_side_color(style, side);
    }
}

double tbox_layout_table_longest_word(const tbox_font_face *face, tbox_string_view text, double letter_spacing) {
    double longest = 0.0;
    for (size_t start = 0; start < text.size;) {
        while (start < text.size && (text.data[start] == ' ' || text.data[start] == '\t' ||
            text.data[start] == '\n' || text.data[start] == '\r')) start++;
        size_t end = start;
        while (end < text.size && text.data[end] != ' ' && text.data[end] != '\t' &&
            text.data[end] != '\n' && text.data[end] != '\r') end++;
        if (end > start) {
            double width = tbox_font_measure_text_spaced(face, tbox_string_view_make(text.data + start, end - start), letter_spacing);
            if (width > longest) longest = width;
        }
        start = end;
    }
    return longest;
}

double tbox_layout_build_table_extended(tbox_arena *arena, const tbox_html_node *table,
    const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images,
    double x, double y, double *table_width, tbox_layout_box *table_box,
    tbox_layout_positioned_context positioned_context) {
    const tbox_style *table_style = table_box->style;
    bool collapse = table_style->border_collapse;
    double sx = collapse ? 0.0 : table_style->border_spacing_x;
    double sy = collapse ? 0.0 : table_style->border_spacing_y;
    tbox_vector rows, cells;
    tbox_vector_init(&rows, arena, sizeof(tbox_table_row), 0);
    tbox_vector_init(&cells, arena, sizeof(tbox_table_cell), 0);
    tbox_table_column *columns = tbox_arena_alloc_zero(arena,
        sizeof(*columns) * TBOX_TABLE_MAX_COLUMNS);
    if (columns == NULL) return 0.0;
    size_t declared = 0, group_id = 0;
    const tbox_html_node *caption = NULL;
    for (const tbox_html_node *child = table->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT) continue;
        if (tbox_layout_style_or_default(styles, child)->display == TBOX_STYLE_DISPLAY_NONE) continue;
        if (tbox_layout_tag(child, "caption")) {
            if (caption == NULL) caption = child;
        } else if (tbox_layout_tag(child, "col") && declared < TBOX_TABLE_MAX_COLUMNS) {
            size_t span = tbox_layout_table_span(child, "span", false);
            for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                columns[declared++] = (tbox_table_column){.node = child};
        } else if (tbox_layout_tag(child, "colgroup")) {
            size_t before = declared;
            for (const tbox_html_node *col = child->first_child; col != NULL; col = col->next_sibling) {
                if (!tbox_layout_tag(col, "col") ||
                    tbox_layout_style_or_default(styles, col)->display == TBOX_STYLE_DISPLAY_NONE) continue;
                size_t span = tbox_layout_table_span(col, "span", false);
                for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                    columns[declared++] = (tbox_table_column){.node = col, .group = child};
            }
            if (declared == before) {
                size_t span = tbox_layout_table_span(child, "span", false);
                for (size_t i = 0; i < span && declared < TBOX_TABLE_MAX_COLUMNS; i++)
                    columns[declared++] = (tbox_table_column){.group = child};
            }
        } else if (tbox_layout_tag(child, "tr")) {
            *(tbox_table_row *)tbox_vector_push(&rows) =
                (tbox_table_row){.node = child, .group_id = group_id};
        } else if (tbox_layout_table_section(child)) {
            group_id++;
            for (const tbox_html_node *row = child->first_child; row != NULL; row = row->next_sibling) {
                if (tbox_layout_tag(row, "tr") &&
                    tbox_layout_style_or_default(styles, row)->display != TBOX_STYLE_DISPLAY_NONE)
                    *(tbox_table_row *)tbox_vector_push(&rows) =
                        (tbox_table_row){.node = row, .group = child, .group_id = group_id};
            }
            group_id++;
        }
    }
    size_t occupied_until[TBOX_TABLE_MAX_COLUMNS] = {0};
    size_t column_count = declared;
    size_t previous_group = SIZE_MAX;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        if (row->group_id != previous_group) {
            memset(occupied_until, 0, sizeof(occupied_until));
            previous_group = row->group_id;
        }
        size_t group_end = r + 1;
        while (group_end < rows.length &&
            ((tbox_table_row *)tbox_vector_at(&rows, group_end))->group_id == row->group_id) group_end++;
        size_t cursor = 0;
        for (const tbox_html_node *cell = row->node->first_child; cell != NULL; cell = cell->next_sibling) {
            if (!tbox_layout_table_cell_node(cell) ||
                tbox_layout_style_or_default(styles, cell)->display == TBOX_STYLE_DISPLAY_NONE) continue;
            size_t span = tbox_layout_table_span(cell, "colspan", false);
            while (cursor < TBOX_TABLE_MAX_COLUMNS) {
                if (cursor + span > TBOX_TABLE_MAX_COLUMNS) span = TBOX_TABLE_MAX_COLUMNS - cursor;
                bool free = span > 0;
                for (size_t i = 0; i < span; i++)
                    if (occupied_until[cursor + i] > r) { free = false; break; }
                if (free) break;
                cursor++;
            }
            if (cursor >= TBOX_TABLE_MAX_COLUMNS || span == 0) break;
            size_t rowspan = tbox_layout_table_span(cell, "rowspan", true);
            if (rowspan == 0 || rowspan > group_end - r) rowspan = group_end - r;
            for (size_t i = 0; i < span; i++) occupied_until[cursor + i] = r + rowspan;
            *(tbox_table_cell *)tbox_vector_push(&cells) =
                (tbox_table_cell){.node = cell, .row = r, .column = cursor,
                                  .rows = rowspan, .columns = span};
            if (cursor + span > column_count) column_count = cursor + span;
            cursor += span;
        }
    }
    for (size_t c = 0; c < column_count; c++) {
        tbox_table_column *col = &columns[c];
        if (col->group != NULL)
            col->min_width = tbox_layout_table_css_width(tbox_layout_style_or_default(styles, col->group), *table_width);
        if (col->node != NULL) {
            double width = tbox_layout_table_css_width(tbox_layout_style_or_default(styles, col->node), *table_width);
            if (width > 0.0) col->min_width = width;
        }
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        const tbox_style *style = tbox_layout_style_or_default(styles, cell->node);
        double css_width = tbox_layout_table_css_width(style, *table_width);
        double edges = tbox_layout_resolve_edge(style->padding[1], *table_width) +
            tbox_layout_resolve_edge(style->padding[3], *table_width) +
            tbox_style_border_side_width(style, 1) + tbox_style_border_side_width(style, 3);
        double minimum = 0.0;
        const tbox_font_face *face = tbox_font_face_cache_get(fonts,
            tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
            style->font_italic, style->font_size);
        if (face != NULL) {
            double natural = tbox_layout_table_longest_word(face,
                tbox_html_node_text_content(arena, cell->node), style->letter_spacing);
            minimum = natural;
        }
        minimum += edges;
        double css_outer = css_width +
            (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? 0.0 : edges);
        if (css_outer > minimum) minimum = css_outer;
        double current = sx * (cell->columns - 1);
        for (size_t c = cell->column; c < cell->column + cell->columns; c++) current += columns[c].min_width;
        if (minimum > current) {
            double each = (minimum - current) / cell->columns;
            for (size_t c = cell->column; c < cell->column + cell->columns; c++) columns[c].min_width += each;
        }
    }
    double minimum_total = sx * (column_count + 1);
    for (size_t c = 0; c < column_count; c++) minimum_total += columns[c].min_width;
    if (minimum_total > *table_width) *table_width = minimum_total;
    double extra = *table_width - minimum_total;
    for (size_t c = 0; c < column_count; c++) {
        columns[c].width = columns[c].min_width + (column_count > 0 ? extra / column_count : 0.0);
        columns[c].x = x + sx;
        if (c > 0) columns[c].x = columns[c - 1].x + columns[c - 1].width + sx;
    }
    tbox_layout_box *caption_box = NULL;
    double caption_height = 0.0;
    if (caption != NULL) {
        tbox_layout_containing_block container = {
            .x = x, .y = y, .width = *table_width, .height = 0.0, .height_definite = false};
        caption_box = tbox_layout_build_element(arena, caption, styles, fonts, images,
            container, y, positioned_context, NULL, 0);
        caption_height = caption_box->margin_box.height;
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        double width = sx * (cell->columns - 1);
        for (size_t c = cell->column; c < cell->column + cell->columns; c++) width += columns[c].width;
        tbox_layout_containing_block container = {
            .x = columns[cell->column].x, .y = 0.0, .width = width,
            .height = 0.0, .height_definite = false};
        /* The non-null marker asks build_element to fit the cell into the grid
         * even when CSS width supplied its earlier column constraint. */
        cell->box = tbox_layout_build_element(arena, cell->node, styles, fonts, images,
            container, 0.0, positioned_context, &width, 0);
        if (cell->rows == 1) {
            tbox_table_row *row = tbox_vector_at(&rows, cell->row);
            if (cell->box->margin_box.height > row->height) row->height = cell->box->margin_box.height;
        }
        if (collapse) cell->box->table_suppress_border = true;
    }
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        if (cell->rows == 1) continue;
        double available = sy * (cell->rows - 1);
        for (size_t r = cell->row; r < cell->row + cell->rows; r++)
            available += ((tbox_table_row *)tbox_vector_at(&rows, r))->height;
        if (cell->box->margin_box.height > available) {
            double increment = (cell->box->margin_box.height - available) / cell->rows;
            for (size_t r = cell->row; r < cell->row + cell->rows; r++)
                ((tbox_table_row *)tbox_vector_at(&rows, r))->height += increment;
        }
    }
    double grid_top = y + (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_TOP ?
        caption_height : 0.0);
    double cursor_y = grid_top + sy;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        row->y = cursor_y;
        row->box = tbox_layout_table_box(arena, row->node, styles,
            (tbox_rect){x + sx, cursor_y, *table_width - 2.0 * sx, row->height});
        if (collapse) row->box->table_suppress_border = true;
        cursor_y += row->height + sy;
    }
    double grid_height = rows.length > 0 ? cursor_y - grid_top : 0.0;
    for (size_t i = 0; i < cells.length; i++) {
        tbox_table_cell *cell = tbox_vector_at(&cells, i);
        tbox_table_row *row = tbox_vector_at(&rows, cell->row);
        double target = sy * (cell->rows - 1);
        for (size_t r = cell->row; r < cell->row + cell->rows; r++)
            target += ((tbox_table_row *)tbox_vector_at(&rows, r))->height;
        double growth = target - cell->box->margin_box.height;
        if (growth < 0.0) growth = 0.0;
        tbox_layout_table_shift_y(cell->box, row->y);
        const tbox_style *style = cell->box->style;
        double align = style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_BOTTOM ? growth :
            style->vertical_align == TBOX_STYLE_VERTICAL_ALIGN_MIDDLE ? growth / 2.0 : 0.0;
        for (size_t run = 0; run < cell->box->text_run_count; run++)
            cell->box->text_runs[run].rect.y += align;
        for (tbox_layout_box *child = cell->box->first_child; child != NULL; child = child->next_sibling)
            tbox_layout_table_shift_y(child, align);
        cell->box->margin_box.height += growth;
        cell->box->border_box.height += growth;
        cell->box->padding_box.height += growth;
        cell->box->content_box.height += growth;
        tbox_layout_table_append(row->box, cell->box);
    }
    /* Column backgrounds are laid out before rows so ordinary paint order
     * puts them beneath row and cell backgrounds. */
    for (size_t c = 0; c < column_count;) {
        const tbox_html_node *group = columns[c].group;
        size_t end = c + 1;
        while (group != NULL && end < column_count && columns[end].group == group) end++;
        tbox_layout_box *parent = table_box;
        if (group != NULL) {
            double width = columns[end - 1].x + columns[end - 1].width - columns[c].x;
            tbox_layout_box *group_box = tbox_layout_table_box(arena, group, styles,
                (tbox_rect){columns[c].x, grid_top, width, grid_height});
            group_box->table_suppress_border = true;
            tbox_layout_table_append(table_box, group_box);
            parent = group_box;
        }
        for (size_t index = c; index < end; index++) {
            if (columns[index].node == NULL) continue;
            tbox_layout_box *col_box = tbox_layout_table_box(arena, columns[index].node, styles,
                (tbox_rect){columns[index].x, grid_top, columns[index].width, grid_height});
            col_box->table_suppress_border = true;
            tbox_layout_table_append(parent, col_box);
        }
        c = end;
    }
    if (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_TOP)
        tbox_layout_table_append(table_box, caption_box);
    tbox_layout_box *group_box = NULL;
    const tbox_html_node *last_group = NULL;
    for (size_t r = 0; r < rows.length; r++) {
        tbox_table_row *row = tbox_vector_at(&rows, r);
        if (row->group != last_group) {
            group_box = NULL;
            last_group = row->group;
        }
        if (row->group != NULL && group_box == NULL) {
            group_box = tbox_layout_table_box(arena, row->group, styles,
                (tbox_rect){x + sx, row->y, *table_width - 2.0 * sx, 0.0});
            if (collapse) group_box->table_suppress_border = true;
            tbox_layout_table_append(table_box, group_box);
        }
        tbox_layout_table_append(group_box != NULL ? group_box : table_box, row->box);
        if (group_box != NULL) group_box->margin_box.height = group_box->border_box.height =
            group_box->padding_box.height = group_box->content_box.height =
            row->y + row->height - group_box->content_box.y;
    }
    if (caption_box != NULL && caption_box->style->caption_side == TBOX_STYLE_CAPTION_BOTTOM) {
        tbox_layout_table_shift_y(caption_box, grid_height);
        tbox_layout_table_append(table_box, caption_box);
    }
    if (collapse && rows.length > 0 && column_count > 0 &&
        rows.length <= SIZE_MAX / (column_count * sizeof(tbox_table_cell *))) {
        tbox_table_cell **grid = tbox_arena_alloc_zero(arena,
            sizeof(*grid) * rows.length * column_count);
        if (grid != NULL) {
            for (size_t i = 0; i < cells.length; i++) {
                tbox_table_cell *cell = tbox_vector_at(&cells, i);
                for (size_t r = cell->row; r < cell->row + cell->rows; r++)
                    for (size_t c = cell->column; c < cell->column + cell->columns; c++)
                        grid[r * column_count + c] = cell;
            }
            tbox_vector edges;
            tbox_vector_init(&edges, arena, sizeof(tbox_table_edge), 0);
            for (size_t r = 0; r < rows.length; r++) {
                const tbox_table_row *row = tbox_vector_at(&rows, r);
                for (size_t c = 0; c <= column_count; c++) {
                    tbox_table_cell *left = c > 0 ? grid[r * column_count + c - 1] : NULL;
                    tbox_table_cell *right = c < column_count ? grid[r * column_count + c] : NULL;
                    if (left != NULL && left == right) continue;
                    double width = 0.0;
                    tbox_css_rgba color = {0, 0, 0, 255};
                    if (c == 0 || c == column_count) {
                        size_t side = c == 0 ? 3 : 1;
                        tbox_layout_table_choose_border(table_style, side, &width, &color);
                        if (row->group != NULL)
                            tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                side, &width, &color);
                        tbox_layout_table_choose_border(row->box->style, side, &width, &color);
                    }
                    if (left != NULL) tbox_layout_table_choose_border(left->box->style, 1, &width, &color);
                    if (right != NULL) tbox_layout_table_choose_border(right->box->style, 3, &width, &color);
                    if (width <= 0.0) continue;
                    double boundary = c == column_count ?
                        columns[c - 1].x + columns[c - 1].width : columns[c].x;
                    *(tbox_table_edge *)tbox_vector_push(&edges) = (tbox_table_edge){
                        .rect = {boundary - width / 2.0, row->y, width, row->height}, .color = color};
                }
            }
            for (size_t r = 0; r <= rows.length; r++) {
                for (size_t c = 0; c < column_count; c++) {
                    tbox_table_cell *above = r > 0 ? grid[(r - 1) * column_count + c] : NULL;
                    tbox_table_cell *below = r < rows.length ? grid[r * column_count + c] : NULL;
                    if (above != NULL && above == below) continue;
                    double width = 0.0;
                    tbox_css_rgba color = {0, 0, 0, 255};
                    if (r == 0 || r == rows.length)
                        tbox_layout_table_choose_border(table_style, r == 0 ? 0 : 2, &width, &color);
                    if (r > 0) {
                        const tbox_table_row *row = tbox_vector_at(&rows, r - 1);
                        tbox_layout_table_choose_border(row->box->style, 2, &width, &color);
                        if (r == rows.length || row->group !=
                            ((tbox_table_row *)tbox_vector_at(&rows, r))->group)
                            if (row->group != NULL)
                                tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                    2, &width, &color);
                    }
                    if (r < rows.length) {
                        const tbox_table_row *row = tbox_vector_at(&rows, r);
                        tbox_layout_table_choose_border(row->box->style, 0, &width, &color);
                        if (r == 0 || row->group !=
                            ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->group)
                            if (row->group != NULL)
                                tbox_layout_table_choose_border(tbox_layout_style_or_default(styles, row->group),
                                    0, &width, &color);
                    }
                    if (above != NULL) tbox_layout_table_choose_border(above->box->style, 2, &width, &color);
                    if (below != NULL) tbox_layout_table_choose_border(below->box->style, 0, &width, &color);
                    if (width <= 0.0) continue;
                    double boundary = r == rows.length ?
                        ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->y +
                        ((tbox_table_row *)tbox_vector_at(&rows, r - 1))->height :
                        ((tbox_table_row *)tbox_vector_at(&rows, r))->y;
                    *(tbox_table_edge *)tbox_vector_push(&edges) = (tbox_table_edge){
                        .rect = {columns[c].x, boundary - width / 2.0, columns[c].width, width},
                        .color = color};
                }
            }
            table_box->table_suppress_border = true;
            table_box->table_edges = edges.data;
            table_box->table_edge_count = edges.length;
        }
    }
    return grid_height + caption_height;
}

