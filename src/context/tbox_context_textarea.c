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
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
            if (field != NULL && face != NULL && field->length < SIZE_MAX / sizeof(tbox_text_line) - 1) {
                size_t maximum = field->length + 1;
                tbox_text_line *lines = tbox_arena_alloc(&ctx->frame_arena, maximum * sizeof(*lines));
                tbox_layout_text_run *runs = tbox_arena_alloc(&ctx->frame_arena, maximum * sizeof(*runs));
                if (lines != NULL && runs != NULL) {
                    double line_height = tbox_context_style_line_height(style, face);
                    size_t count = 0, start = 0, at = 0;
                    double width = 0.0;
                    while (at < field->length) {
                        size_t next = tbox_utf8_next(field->value, field->length, at);
                        if (next == at) next = at + 1;
                        if (field->value[at] == '\n') {
                            lines[count++] = (tbox_text_line){start, at, width};
                            start = next;
                            width = 0.0;
                        } else {
                            double next_width = tbox_font_measure_text_spaced(face,
                                tbox_string_view_make(field->value + start, next - start), style->letter_spacing);
                            if (next_width > box->content_box.width && at > start) {
                                lines[count++] = (tbox_text_line){start, at, width};
                                start = at;
                                next_width = tbox_font_measure_text_spaced(face,
                                    tbox_string_view_make(field->value + start, next - start), style->letter_spacing);
                            }
                            width = next_width;
                        }
                        at = next;
                    }
                    lines[count++] = (tbox_text_line){start, field->length, width};
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
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
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
                                double left = box->content_box.x + tbox_font_measure_text_spaced(face,
                                    tbox_string_view_make(field->value + line_start, a - line_start), style->letter_spacing);
                                double right = box->content_box.x + tbox_font_measure_text_spaced(face,
                                    tbox_string_view_make(field->value + line_start, b - line_start), style->letter_spacing);
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
                    double x = box->content_box.x + tbox_font_measure_text_spaced(face,
                        tbox_string_view_make(field->value + field->lines[row].start, offset), style->letter_spacing);
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
