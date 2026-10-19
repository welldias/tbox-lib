#include "tbox_context_internal.h"

static void tbox_context_bind_text_value(tbox_context *ctx, tbox_text_field *field);

/* A measurer for `node`'s displayed value: masked for a password input,
 * so the caret lines up with the drawn marks. */
static void tbox_context_input_measurer(tbox_text_measurer *measurer, const tbox_html_node *node,
                                        const tbox_font_face *face, double letter_spacing, tbox_string_view value) {
    if (tbox_layout_is_password_input(node))
        tbox_text_measurer_init_masked(measurer, face, letter_spacing, value.data, value.size, tbox_layout_password_codepoint(face));
    else
        tbox_text_measurer_init(measurer, face, letter_spacing, value.data, value.size);
}

double tbox_context_input_advance(const tbox_html_node *node, const tbox_font_face *face,
                                  tbox_string_view value, size_t offset, double letter_spacing) {
    tbox_text_measurer measurer;
    tbox_context_input_measurer(&measurer, node, face, letter_spacing, value);
    while (measurer.at < offset && measurer.at < measurer.length)
        tbox_text_measurer_step(&measurer);
    double width = measurer.width;
    tbox_text_measurer_finish(&measurer);
    return width;
}

static void tbox_context_bind_text_value(tbox_context *ctx, tbox_text_field *field) {
    if (tbox_context_is_textarea(field->node)) {
        tbox_html_node *node = (tbox_html_node *)field->node;
        if (node->first_child == NULL || node->first_child->type != TBOX_HTML_NODE_TEXT ||
            node->first_child != node->last_child)
            tbox_html_node_set_text_content(ctx->document, node, tbox_string_view_make("", 0));
        node->first_child->text.text = tbox_string_view_make(field->value, field->length);
        return;
    }
    /* Keep the DOM view tied to the field's owned buffer. In particular,
     * bind the initial HTML value before a cursor-only key event arrives. */
    tbox_html_attribute *attribute = (tbox_html_attribute *)tbox_html_node_get_attribute(
        field->node, tbox_string_view_make("value", 5));
    if (attribute == NULL) {
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)field->node,
                                     tbox_string_view_make("value", 5),
                                     tbox_string_view_make(field->value, field->length));
        attribute = (tbox_html_attribute *)tbox_html_node_get_attribute(field->node, tbox_string_view_make("value", 5));
    }
    if (attribute != NULL) attribute->value = tbox_string_view_make(field->value, field->length);
}

tbox_text_field *tbox_context_text_field(tbox_context *ctx, const tbox_html_node *node) {
    for (tbox_text_field *field = ctx->text_fields; field != NULL; field = field->next) {
        if (field->node == node) {
            const tbox_html_attribute *attribute = tbox_context_is_textarea(node) ? NULL :
                tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
            bool external_text = tbox_context_is_textarea(node) &&
                (node->first_child == NULL || node->first_child->type != TBOX_HTML_NODE_TEXT ||
                 node->first_child->text.text.data != field->value);
            if ((attribute != NULL && attribute->value.data != field->value) || external_text) {
                tbox_string_view source = external_text ? tbox_html_node_text_content(&ctx->frame_arena, node) : attribute->value;
                if (source.size == SIZE_MAX) return NULL;
                char *copy = realloc(field->value, source.size + 1);
                if (copy == NULL) return NULL;
                field->value = copy;
                field->capacity = source.size + 1;
                if (source.size > 0) {
                    memcpy(field->value, source.data, source.size);
                }
                field->length = field->cursor = field->anchor = source.size;
                field->scroll_x = field->scroll_y = 0.0;
                field->value[field->length] = '\0';
                tbox_context_bind_text_value(ctx, field);
            }
            return field;
        }
    }
    const tbox_html_attribute *initial = tbox_context_is_textarea(node) ? NULL :
        tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    tbox_string_view initial_text = tbox_context_is_textarea(node) ?
        tbox_html_node_text_content(&ctx->frame_arena, node) :
        (initial != NULL ? initial->value : tbox_string_view_make(NULL, 0));
    size_t length = initial_text.size;
    if (length == SIZE_MAX) return NULL;
    tbox_text_field *field = calloc(1, sizeof(*field));
    if (field == NULL) return NULL;
    field->value = malloc(length + 1);
    if (field->value == NULL) {
        free(field);
        return NULL;
    }
    if (length > 0) memcpy(field->value, initial_text.data, length);
    field->value[length] = '\0';
    field->node = node;
    field->length = field->cursor = field->anchor = length;
    field->capacity = length + 1;
    field->reveal = true;
    field->next = ctx->text_fields;
    ctx->text_fields = field;
    tbox_context_bind_text_value(ctx, field);
    return field;
}

void tbox_context_sync_text_value(tbox_context *ctx, tbox_text_field *field) {
    /* The field owns its reusable buffer. The DOM view points at it so
     * layout and application code see the live value without an arena
     * allocation for every keystroke. */
    tbox_context_bind_text_value(ctx, field);
    tbox_context_emit_input(ctx, field->node, tbox_string_view_make(field->value, field->length));
}

size_t tbox_context_cursor_at_x(const tbox_text_field *field, const tbox_layout_box *box,
                                       const tbox_font_face *face, double x) {
    double relative_x = x - box->content_box.x + field->scroll_x;
    tbox_text_measurer measurer;
    tbox_context_input_measurer(&measurer, field->node, face, box->style->letter_spacing,
                                tbox_string_view_make(field->value, field->length));
    size_t found = field->length;
    while (measurer.at < measurer.length) {
        size_t at       = measurer.at;
        double previous = measurer.width;
        size_t next     = tbox_text_measurer_step(&measurer);
        if (relative_x < measurer.width) {
            found = relative_x - previous < measurer.width - relative_x ? at : next;
            break;
        }
    }
    tbox_text_measurer_finish(&measurer);
    return found;
}

size_t tbox_context_cursor_at_point(const tbox_text_field *field, const tbox_layout_box *box,
                                           const tbox_font_face *face, double x, double y) {
    if (field->line_count == 0) return 0;
    double height = tbox_context_style_line_height(box->style, face);
    if (height <= 0.0) return 0;
    double relative_y = y - box->content_box.y + field->scroll_y;
    size_t row = relative_y <= 0.0 ? 0 : (size_t)(relative_y / height);
    if (row >= field->line_count) row = field->line_count - 1;
    tbox_text_line line = field->lines[row];
    double relative_x = x - box->content_box.x;
    const char *text  = field->value + line.start;
    size_t length     = line.end - line.start;
    tbox_text_measurer measurer;
    tbox_text_measurer_init(&measurer, face, box->style->letter_spacing, text, length);
    size_t found = line.end;
    while (measurer.at < measurer.length) {
        size_t at       = measurer.at;
        double previous = measurer.width;
        size_t next     = tbox_text_measurer_step(&measurer);
        if (relative_x < measurer.width) {
            found = line.start + (relative_x - previous < measurer.width - relative_x ? at : next);
            break;
        }
    }
    tbox_text_measurer_finish(&measurer);
    return found;
}

bool tbox_context_drag_select(tbox_context *ctx, double x) {
    if (ctx == NULL || !tbox_context_is_text_control(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node)) return false;
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
    if (field == NULL || box == NULL || box->style == NULL) return false;
    const tbox_style *style = box->style;
    const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
    if (face == NULL) return false;
    size_t cursor = tbox_context_cursor_at_x(field, box, face, x);
    if (cursor == field->cursor) return false;
    field->cursor = cursor;
    return true;
}

bool tbox_context_drag_select_at(tbox_context *ctx, double x, double y) {
    if (ctx == NULL || !tbox_context_is_text_control(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node)) return false;
    if (!tbox_context_is_textarea(ctx->focused_node)) return tbox_context_drag_select(ctx, x);
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
    if (field == NULL || box == NULL || box->style == NULL) return false;
    const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, box->style);
    if (face == NULL) return false;
    size_t cursor = tbox_context_cursor_at_point(field, box, face, x, y);
    if (cursor == field->cursor) return false;
    field->cursor = cursor;
    field->reveal = true;
    return true;
}

tbox_string_view tbox_context_selected_text(tbox_context *ctx) {
    if (ctx == NULL || !tbox_context_is_text_control(ctx->focused_node) ||
        tbox_layout_is_password_input(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node))
        return tbox_string_view_make(NULL, 0);
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    if (field == NULL || field->anchor == field->cursor)
        return tbox_string_view_make(NULL, 0);
    size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
    size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
    return tbox_string_view_make(field->value + start, end - start);
}

bool tbox_context_dispatch_text(tbox_context *ctx, tbox_string_view text) {
    if (ctx == NULL || !tbox_context_is_text_control(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node) ||
        text.data == NULL || text.size == 0 || !tbox_string_view_valid_utf8(text)) return false;
    for (size_t i = 0; i < text.size;) {
        size_t next = tbox_utf8_next(text.data, text.size, i);
        if (((unsigned char)text.data[i] < 0x20 &&
            !(tbox_context_is_textarea(ctx->focused_node) &&
              (text.data[i] == '\n' || text.data[i] == '\t'))) ||
            (unsigned char)text.data[i] == 0x7f ||
            (next == i + 2 && (unsigned char)text.data[i] == 0xc2 &&
             (unsigned char)text.data[i + 1] >= 0x80 &&
             (unsigned char)text.data[i + 1] <= 0x9f)) return false;
        i = next;
    }
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    if (field == NULL) return false;
    size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
    size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
    size_t remaining = field->length - (end - start);
    if (text.size > SIZE_MAX - remaining - 1) return false;
    size_t needed = remaining + text.size + 1;
    if (tbox_context_is_number_input(ctx->focused_node)) {
        char *proposal = malloc(needed);
        if (proposal == NULL) return false;
        memcpy(proposal, field->value, start);
        memcpy(proposal + start, text.data, text.size);
        memcpy(proposal + start + text.size, field->value + end, field->length - end);
        bool accepted = tbox_context_number_syntax(
            tbox_string_view_make(proposal, needed - 1), false);
        free(proposal);
        if (!accepted) return false;
    }
    if (needed > field->capacity) {
        size_t capacity = field->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        char *value = realloc(field->value, capacity);
        if (value == NULL) return false;
        field->value = value;
        field->capacity = capacity;
    }
    memmove(field->value + start + text.size,
            field->value + end, field->length - end + 1);
    memcpy(field->value + start, text.data, text.size);
    field->length = remaining + text.size;
    field->cursor = field->anchor = start + text.size;
    field->reveal = true;
    tbox_context_sync_text_value(ctx, field);
    return true;
}

void tbox_context_paint_text_input_caret(tbox_context *ctx, tbox_display_list *out_list) {
    if (tbox_context_is_text_input(ctx->focused_node)) {
        const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
        tbox_text_field *field = box != NULL ? tbox_context_text_field(ctx, ctx->focused_node) : NULL;
        if (field != NULL && box->style != NULL && !box->style->visibility_hidden && box->content_box.width > 0) {
            const tbox_style *style = box->style;
            tbox_rect input_clip = box->content_box;
            tbox_rect control;
            double control_width = tbox_context_input_control_rect(box, &control) ? control.width : 0.0;
            input_clip.width -= control_width;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    input_clip = tbox_context_rect_intersection(input_clip, ancestor->padding_box);
            const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
            if (face != NULL) {
                tbox_string_view value = tbox_string_view_make(field->value, field->length);
                /* The caret goes where its logical offset is drawn after
                 * bidi reordering; an rtl field's text may also be
                 * aligned away from the content box's left edge. */
                double cursor_x = tbox_layout_is_password_input(box->node) ?
                    tbox_context_input_advance(box->node, face, value, field->cursor, style->letter_spacing) :
                    tbox_layout_bidi_caret_x(face, style->letter_spacing, value, field->cursor, style->direction_rtl);
                if (style->direction_rtl && box->text_run_count > 0)
                    cursor_x += box->text_runs[0].rect.x - box->content_box.x;
                double visible_width = box->content_box.width - control_width - 1.0;
                if (visible_width < 0.0) visible_width = 0.0;
                if (cursor_x < field->scroll_x) field->scroll_x = cursor_x;
                if (cursor_x > field->scroll_x + visible_width)
                    field->scroll_x = cursor_x - visible_width;
                double text_width = tbox_context_input_advance(box->node, face, value, field->length, style->letter_spacing);
                double max_scroll = text_width - visible_width;
                if (max_scroll < 0.0) max_scroll = 0.0;
                if (field->scroll_x > max_scroll) field->scroll_x = max_scroll;
                double x = box->content_box.x + cursor_x - field->scroll_x;
                double height = tbox_context_style_line_height(style, face);
                if (height > box->content_box.height) height = box->content_box.height;
                /* ::selection: its background behind the selected text and,
                 * when it declares a color, the text again in that color
                 * clipped to the selection. */
                const tbox_style_part_style *selection = tbox_context_part(style, TBOX_STYLE_PART_SELECTION);
                tbox_paint_op *items = tbox_arena_alloc(&ctx->frame_arena,
                    (out_list->count + 3) * sizeof(*items));
                if (items != NULL) {
                    size_t count = 0;
                    for (size_t i = 0; i < out_list->count; i++) {
                        tbox_paint_op op = out_list->items[i];
                        bool input_text = op.kind == TBOX_PAINT_TEXT_RUN &&
                            op.has_clip &&
                            op.text.data != NULL && box->text_run_count > 0 &&
                            op.text.data == box->text_runs[0].text.data;
                        bool selected_text = false;
                        tbox_rect selected = {0};
                        if (input_text) {
                            op.rect.x -= field->scroll_x;
                            op.clip = input_clip;
                            if (field->anchor != field->cursor) {
                                size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
                                size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
                                double left = box->content_box.x - field->scroll_x +
                                    tbox_context_input_advance(box->node, face, value, start, style->letter_spacing);
                                double right = box->content_box.x - field->scroll_x +
                                    tbox_context_input_advance(box->node, face, value, end, style->letter_spacing);
                                if (left < box->content_box.x) left = box->content_box.x;
                                if (right > box->content_box.x + box->content_box.width - control_width)
                                    right = box->content_box.x + box->content_box.width - control_width;
                                if (right > left) {
                                    selected = (tbox_rect){ left, box->content_box.y, right - left, height };
                                    selected_text = selection->color_declared;
                                    items[count++] = (tbox_paint_op){
                                        .kind = TBOX_PAINT_FILL_RECT,
                                        .rect = selected,
                                        .color = selection->background_declared ? selection->background_color :
                                                 (tbox_css_rgba){ 130, 175, 235, 255 },
                                        .has_clip = true, .clip = input_clip,
                                    };
                                }
                            }
                        }
                        items[count++] = op;
                        if (selected_text) {
                            op.color = selection->color;
                            op.clip  = tbox_context_rect_intersection(input_clip, selected);
                            items[count++] = op;
                        }
                    }
                    double caret_width = box->content_box.width < 1.0 ? box->content_box.width : 1.0;
                    items[count++] = (tbox_paint_op){
                        .kind = TBOX_PAINT_FILL_RECT,
                        .rect = { x, box->content_box.y, caret_width, height },
                        .color = style->caret_color.a != 0 ? style->caret_color : style->color,
                        .has_clip = true, .clip = input_clip,
                    };
                    out_list->items = items;
                    out_list->count = count;
                }
            }
        }
    }
}
