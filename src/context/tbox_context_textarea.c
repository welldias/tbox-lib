#include "tbox_context_internal.h"

bool tbox_context_is_textarea(const tbox_html_node *node) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
        tbox_string_view_equal_cstr(node->element.tag_name, "textarea");
}

void tbox_context_layout_textareas(tbox_context *ctx, tbox_layout_box *box) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_textarea(box->node) && box->style != NULL) {
            tbox_text_field *field = tbox_context_text_field(ctx, box->node);
            const tbox_style *style = box->style;
            const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
            if (field != NULL && face != NULL && field->length < SIZE_MAX / sizeof(tbox_text_line) - 1) {
                size_t maximum = field->length + 1;
                tbox_text_line *lines = tbox_arena_alloc(&ctx->frame_arena, maximum * sizeof(*lines));
                tbox_layout_text_run *runs = tbox_arena_alloc(&ctx->frame_arena, maximum * sizeof(*runs));
                if (lines != NULL && runs != NULL) {
                    double line_height = tbox_context_style_line_height(style, face);
                    /* Each paragraph (up to a '\n') is shaped from the start
                     * of the current line; a line breaks before the
                     * codepoint that would overflow it, and the rest of the
                     * paragraph is shaped again from there. */
                    size_t count = 0, start = 0;
                    for (;;) {
                        size_t end = start;
                        while (end < field->length && field->value[end] != '\n') end++;
                        size_t line_start = start;
                        tbox_text_measurer line;
                        tbox_text_measurer_init(&line, face, style->letter_spacing, field->value + line_start, end - line_start);
                        while (line.at < line.length) {
                            size_t before       = line.at;
                            double before_width = line.width;
                            tbox_text_measurer_step(&line);
                            if (line.width > box->content_box.width && before > 0) {
                                lines[count++] = (tbox_text_line){line_start, line_start + before, before_width};
                                line_start += before;
                                tbox_text_measurer_finish(&line);
                                tbox_text_measurer_init(&line, face, style->letter_spacing, field->value + line_start, end - line_start);
                            }
                        }
                        lines[count++] = (tbox_text_line){line_start, end, line.width};
                        tbox_text_measurer_finish(&line);
                        if (end >= field->length) break;
                        start = end + 1;
                    }
                    field->lines = lines;
                    field->line_count = count;
                    double max_scroll = count * line_height - box->content_box.height;
                    if (max_scroll < 0) max_scroll = 0;
                    if (field->scroll_y > max_scroll) field->scroll_y = max_scroll;
                    if (field->reveal && ctx->focused_node == box->node) {
                        size_t row = 0;
                        for (size_t i = 0; i < count; i++) {
                            if (field->cursor >= lines[i].start && field->cursor <= lines[i].end) row = i;
                        }
                        double top = row * line_height;
                        if (top < field->scroll_y) field->scroll_y = top;
                        if (top + line_height > field->scroll_y + box->content_box.height)
                            field->scroll_y = top + line_height - box->content_box.height;
                        if (field->scroll_y > max_scroll) field->scroll_y = max_scroll;
                        field->reveal = false;
                    }
                    size_t run_count = 0;
                    for (size_t i = 0; i < count; i++) {
                        if (lines[i].end == lines[i].start) continue;
                        runs[run_count++] = (tbox_layout_text_run){
                            .rect = {box->content_box.x, box->content_box.y + i * line_height - field->scroll_y,
                                     lines[i].width, line_height},
                            .text = tbox_string_view_make(field->value + lines[i].start, lines[i].end - lines[i].start),
                            .font = face, .style = style,
                        };
                        tbox_font_shape_into(&ctx->frame_arena, face, runs[run_count - 1].text, style->letter_spacing,
                                             &runs[run_count - 1].glyphs, &runs[run_count - 1].glyph_count);
                    }
                    box->text_runs = runs;
                    box->text_run_count = run_count;
                    box->scroll_content_height = count * line_height;
                }
            }
        }
        tbox_context_layout_textareas(ctx, box->first_child);
    }
}

void tbox_context_paint_textarea_caret(tbox_context *ctx, tbox_display_list *out_list) {
    if (tbox_context_is_textarea(ctx->focused_node)) {
        const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
        tbox_text_field *field = box != NULL ? tbox_context_text_field(ctx, box->node) : NULL;
        if (field != NULL && field->line_count > 0 && box->style != NULL && !box->style->visibility_hidden) {
            const tbox_style *style = box->style;
            const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
            if (face != NULL) {
                double line_height = tbox_context_style_line_height(style, face);
                tbox_rect clip = box->content_box;
                for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                    if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                        clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
                tbox_paint_op *items = tbox_arena_alloc(&ctx->frame_arena,
                    (out_list->count + field->line_count + 1) * sizeof(*items));
                if (items != NULL) {
                    size_t count = 0;
                    size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
                    size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
                    for (size_t i = 0; i < out_list->count; i++) {
                        tbox_paint_op op = out_list->items[i];
                        bool own_text = false;
                        if (op.kind == TBOX_PAINT_TEXT_RUN)
                            for (size_t run = 0; run < box->text_run_count; run++)
                                if (op.text.data == box->text_runs[run].text.data &&
                                    op.text.size == box->text_runs[run].text.size) own_text = true;
                        if (own_text) {
                            size_t line_start = (size_t)(op.text.data - field->value);
                            size_t line_end = line_start + op.text.size;
                            size_t a = start > line_start ? start : line_start;
                            size_t b = end < line_end ? end : line_end;
                            if (b > a) {
                                tbox_string_view text = tbox_string_view_make(field->value + line_start, line_end - line_start);
                                double left = box->content_box.x + tbox_text_prefix_width(face, style->letter_spacing, text, a - line_start);
                                double right = box->content_box.x + tbox_text_prefix_width(face, style->letter_spacing, text, b - line_start);
                                items[count++] = (tbox_paint_op){.kind = TBOX_PAINT_FILL_RECT,
                                    .rect = {left, op.rect.y, right - left, line_height},
                                    .color = {130, 175, 235, 255}, .has_clip = true, .clip = clip};
                            }
                        }
                        items[count++] = op;
                    }
                    size_t row = 0;
                    for (size_t i = 0; i < field->line_count; i++)
                        if (field->cursor >= field->lines[i].start && field->cursor <= field->lines[i].end) row = i;
                    size_t offset = field->cursor - field->lines[row].start;
                    tbox_string_view text = tbox_string_view_make(field->value + field->lines[row].start,
                                                                  field->lines[row].end - field->lines[row].start);
                    double x = box->content_box.x + tbox_text_prefix_width(face, style->letter_spacing, text, offset);
                    items[count++] = (tbox_paint_op){.kind = TBOX_PAINT_FILL_RECT,
                        .rect = {x, box->content_box.y + row * line_height - field->scroll_y, 1.0, line_height},
                        .color = style->caret_color.a != 0 ? style->caret_color : style->color, .has_clip = true, .clip = clip};
                    out_list->items = items;
                    out_list->count = count;
                }
            }
        }
    }
}
