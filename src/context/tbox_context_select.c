#include "tbox_context_internal.h"

typedef struct tbox_popup_geometry {
    tbox_rect rect;
    double row_height;
    size_t first, visible, count;
} tbox_popup_geometry;

static bool tbox_context_popup_geometry(tbox_context *ctx, tbox_popup_geometry *out);

static bool tbox_context_is_option(const tbox_html_node *node);
static const tbox_html_node *tbox_context_option_at(const tbox_html_node *select, size_t index);
static size_t tbox_context_option_index(const tbox_html_node *select, const tbox_html_node *option);
static tbox_string_view tbox_context_option_text(tbox_context *ctx, const tbox_html_node *option);
static tbox_string_view tbox_context_option_label(tbox_context *ctx, const tbox_html_node *option);
static tbox_string_view tbox_context_option_value(tbox_context *ctx, const tbox_html_node *option);
static bool tbox_context_select_commit(tbox_context *ctx, const tbox_html_node *select,
                                       const tbox_html_node *option);
static const tbox_html_node *tbox_context_select_neighbor(const tbox_html_node *select,
                                                           const tbox_html_node *current, tbox_key key);

static bool tbox_context_is_option(const tbox_html_node *node) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
        tbox_string_view_equal_cstr(node->element.tag_name, "option");
}

bool tbox_context_option_disabled(const tbox_html_node *option) {
    return tbox_html_node_get_attribute(option, tbox_string_view_make("disabled", 8)) != NULL;
}

size_t tbox_context_option_count(const tbox_html_node *select) {
    size_t count = 0;
    for (const tbox_html_node *child = select->first_child; child != NULL; child = child->next_sibling)
        if (tbox_context_is_option(child)) count++;
    return count;
}

static const tbox_html_node *tbox_context_option_at(const tbox_html_node *select, size_t index) {
    for (const tbox_html_node *child = select->first_child; child != NULL; child = child->next_sibling) {
        if (!tbox_context_is_option(child)) continue;
        if (index-- == 0) return child;
    }
    return NULL;
}

static size_t tbox_context_option_index(const tbox_html_node *select, const tbox_html_node *option) {
    size_t index = 0;
    for (const tbox_html_node *child = select->first_child; child != NULL; child = child->next_sibling) {
        if (!tbox_context_is_option(child)) continue;
        if (child == option) return index;
        index++;
    }
    return index;
}

static tbox_string_view tbox_context_option_text(tbox_context *ctx, const tbox_html_node *option) {
    tbox_string_view text = tbox_html_node_text_content(&ctx->frame_arena, option);
    return tbox_string_collapse_whitespace(&ctx->frame_arena, text);
}

static tbox_string_view tbox_context_option_label(tbox_context *ctx, const tbox_html_node *option) {
    const tbox_html_attribute *label = tbox_html_node_get_attribute(option, tbox_string_view_make("label", 5));
    return label != NULL && label->value.size > 0 ? label->value : tbox_context_option_text(ctx, option);
}

tbox_select_field *tbox_context_select_field(tbox_context *ctx, const tbox_html_node *node) {
    if (!tbox_context_is_select(node)) return NULL;
    tbox_select_field *field = ctx->select_fields;
    while (field != NULL && field->node != node) field = field->next;
    if (field == NULL) {
        field = calloc(1, sizeof(*field));
        if (field == NULL) return NULL;
        field->node = node;
        field->next = ctx->select_fields;
        ctx->select_fields = field;
    }
    if (field->selected == NULL || field->selected->parent != node) field->dirty = false;
    if (!field->dirty) {
        const tbox_html_node *marked = NULL, *first_enabled = NULL;
        for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
            if (!tbox_context_is_option(child)) continue;
            if (first_enabled == NULL && !tbox_context_option_disabled(child)) first_enabled = child;
            if (tbox_html_node_get_attribute(child, tbox_string_view_make("selected", 8)) != NULL)
                marked = child;
        }
        field->selected = marked != NULL ? marked : first_enabled;
    }
    return field;
}

void tbox_context_sync_select_boxes(tbox_context *ctx, tbox_layout_box *box) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_select(box->node)) {
            tbox_select_field *field = tbox_context_select_field(ctx, box->node);
            if (field != NULL && field->selected != NULL && box->style != NULL) {
                tbox_string_view label = tbox_context_option_label(ctx, field->selected);
                const tbox_style *style = box->style;
                const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
                if (face != NULL && label.size > 0) {
                    tbox_layout_text_run *run = tbox_arena_alloc(&ctx->frame_arena, sizeof(*run));
                    if (run != NULL) {
                        *run = (tbox_layout_text_run){
                            .rect = {box->content_box.x, box->content_box.y,
                                     tbox_font_measure_text(face, label), tbox_font_face_line_height(face)},
                            .text = label, .font = face, .style = style,
                        };
                        box->text_runs = run;
                        box->text_run_count = 1;
                    }
                }
            }
        }
        tbox_context_sync_select_boxes(ctx, box->first_child);
    }
}

static tbox_string_view tbox_context_option_value(tbox_context *ctx, const tbox_html_node *option) {
    const tbox_html_attribute *value = tbox_html_node_get_attribute(option, tbox_string_view_make("value", 5));
    return value != NULL ? value->value : tbox_context_option_text(ctx, option);
}

static bool tbox_context_select_commit(tbox_context *ctx, const tbox_html_node *select,
                                       const tbox_html_node *option) {
    if (option == NULL || tbox_context_option_disabled(option)) return false;
    tbox_select_field *field = tbox_context_select_field(ctx, select);
    if (field == NULL || field->selected == option) return false;
    field->selected = option;
    field->dirty = true;
    if (ctx->select_handler != NULL) {
        tbox_string_view value = tbox_context_option_value(ctx, option);
        ctx->select_handler(ctx, (tbox_html_node *)select, value, ctx->select_userdata);
    }
    return true;
}

static bool tbox_context_popup_geometry(tbox_context *ctx, tbox_popup_geometry *out) {
    if (ctx->open_select == NULL) return false;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_select);
    if (box == NULL || box->style == NULL) return false;
    size_t count = tbox_context_option_count(ctx->open_select);
    if (count == 0) return false;
    const tbox_style *style = box->style;
    const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
    double row_height = face != NULL ? tbox_font_face_line_height(face) + 8.0 : 24.0;
    if (row_height < 24.0) row_height = 24.0;
    size_t desired = count < 6 ? count : 6;
    double below = ctx->viewport_height - (box->border_box.y + box->border_box.height);
    double above = box->border_box.y;
    bool place_above = below < row_height * (double)desired && above > below;
    double available = place_above ? above : below;
    size_t visible = available > 0.0 ? (size_t)(available / row_height) : 0;
    if (visible == 0) visible = 1;
    if (visible > desired) visible = desired;
    size_t first = ctx->popup_first;
    if (first + visible > count) first = count - visible;
    double height = row_height * (double)visible;
    double y = place_above ? box->border_box.y - height : box->border_box.y + box->border_box.height;
    *out = (tbox_popup_geometry){
        .rect = {box->border_box.x, y, box->border_box.width, height},
        .row_height = row_height, .first = first, .visible = visible, .count = count,
    };
    return true;
}

bool tbox_context_select_popup_contains(tbox_context *ctx, double x, double y) {
    tbox_popup_geometry popup;
    return tbox_context_popup_geometry(ctx, &popup) &&
        tbox_context_point_in_rect(popup.rect, x, y);
}

bool tbox_context_select_popup_scroll(tbox_context *ctx, double x, double y, double delta_y) {
    tbox_popup_geometry popup;
    if (!tbox_context_popup_geometry(ctx, &popup) ||
        !tbox_context_point_in_rect(popup.rect, x, y)) return false;
    if (delta_y > 0.0 && ctx->popup_first + popup.visible < popup.count) ctx->popup_first++;
    if (delta_y < 0.0 && ctx->popup_first > 0) ctx->popup_first--;
    return true;
}

void tbox_context_popup_reveal_highlight(tbox_context *ctx) {
    tbox_popup_geometry popup;
    if (!tbox_context_popup_geometry(ctx, &popup) || ctx->popup_highlight == NULL) return;
    size_t index = tbox_context_option_index(ctx->open_select, ctx->popup_highlight);
    if (index < ctx->popup_first) ctx->popup_first = index;
    else if (index >= ctx->popup_first + popup.visible) ctx->popup_first = index + 1 - popup.visible;
}

void tbox_context_paint_select_arrows(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_select(box->node) && box->style != NULL && !box->style->visibility_hidden) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            double x = box->content_box.x + box->content_box.width - 14.0;
            double y = box->content_box.y + (box->content_box.height - 6.0) / 2.0;
            for (int i = 0; i < 3; i++)
                tbox_context_push_fill(items, (tbox_rect){x + 2.0 * i, y + 2.0 * i,
                    10.0 - 4.0 * i, 2.0}, box->style->color, true, clip);
        }
        tbox_context_paint_select_arrows(box->first_child, items);
    }
}

void tbox_context_paint_select_popup(tbox_context *ctx, tbox_vector *items) {
    tbox_popup_geometry popup;
    if (!tbox_context_popup_geometry(ctx, &popup)) return;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_select);
    const tbox_style *style = box->style;
    const tbox_font_face *face = tbox_layout_style_face(ctx->fonts, style);
    tbox_context_push_fill(items, popup.rect, (tbox_css_rgba){105, 112, 122, 255}, false, (tbox_rect){0});
    tbox_rect inner = {popup.rect.x + 1.0, popup.rect.y + 1.0,
                       popup.rect.width - 2.0, popup.rect.height - 2.0};
    if (inner.width > 0.0 && inner.height > 0.0)
        tbox_context_push_fill(items, inner, (tbox_css_rgba){255, 255, 255, 255}, false, (tbox_rect){0});
    for (size_t i = 0; i < popup.visible; i++) {
        const tbox_html_node *option = tbox_context_option_at(ctx->open_select, popup.first + i);
        if (option == NULL) continue;
        tbox_rect row = {popup.rect.x + 1.0, popup.rect.y + popup.row_height * (double)i,
                         popup.rect.width - 2.0, popup.row_height};
        bool highlighted = option == ctx->popup_highlight && !tbox_context_option_disabled(option);
        if (highlighted)
            tbox_context_push_fill(items, row, (tbox_css_rgba){65, 115, 195, 255}, true, inner);
        if (face != NULL) {
            tbox_string_view label = tbox_context_option_label(ctx, option);
            if (label.size == 0) continue;
            tbox_paint_op *op = tbox_vector_push(items);
            *op = (tbox_paint_op){
                .kind = TBOX_PAINT_TEXT_RUN,
                .rect = {row.x + 7.0, row.y + (row.height - tbox_font_face_line_height(face)) / 2.0,
                         row.width - 14.0, tbox_font_face_line_height(face)},
                .color = tbox_context_option_disabled(option) ? (tbox_css_rgba){145, 145, 145, 255} :
                         highlighted ? (tbox_css_rgba){255, 255, 255, 255} : style->color,
                .text = label, .face = face, .has_clip = true,
                .clip = tbox_context_rect_intersection(row, inner),
            };
        }
    }
}

bool tbox_context_popup_click(tbox_context *ctx, double x, double y) {
    tbox_popup_geometry popup;
    if (!tbox_context_popup_geometry(ctx, &popup) ||
        !tbox_context_point_in_rect(popup.rect, x, y)) return false;
    size_t row = (size_t)((y - popup.rect.y) / popup.row_height);
    if (row < popup.visible) {
        const tbox_html_node *option = tbox_context_option_at(ctx->open_select, popup.first + row);
        if (option != NULL && !tbox_context_option_disabled(option)) {
            const tbox_html_node *select = ctx->open_select;
            ctx->open_select = NULL;
            ctx->popup_highlight = NULL;
            tbox_context_select_commit(ctx, select, option);
        }
    }
    return true;
}

static const tbox_html_node *tbox_context_select_neighbor(const tbox_html_node *select,
                                                           const tbox_html_node *current, tbox_key key) {
    size_t count = tbox_context_option_count(select);
    size_t index = current != NULL ? tbox_context_option_index(select, current) : count;
    if (key == TBOX_KEY_HOME || key == TBOX_KEY_END) {
        const tbox_html_node *candidate = NULL;
        for (size_t i = 0; i < count; i++) {
            const tbox_html_node *option = tbox_context_option_at(select, i);
            if (option == NULL || tbox_context_option_disabled(option)) continue;
            candidate = option;
            if (key == TBOX_KEY_HOME) break;
        }
        return candidate;
    }
    if (key == TBOX_KEY_DOWN) {
        for (size_t i = index < count ? index + 1 : 0; i < count; i++) {
            const tbox_html_node *option = tbox_context_option_at(select, i);
            if (option != NULL && !tbox_context_option_disabled(option)) return option;
        }
    } else if (key == TBOX_KEY_UP) {
        for (size_t i = index <= count ? index : count; i > 0; i--) {
            const tbox_html_node *option = tbox_context_option_at(select, i - 1);
            if (option != NULL && !tbox_context_option_disabled(option)) return option;
        }
    }
    return current;
}

bool tbox_context_dispatch_select_key(tbox_context *ctx, tbox_key key) {
    const tbox_html_node *select = ctx->focused_node;
    tbox_select_field *field = tbox_context_select_field(ctx, select);
    if (field == NULL) return false;
    bool open = ctx->open_select == select;
    if (key == TBOX_KEY_ESCAPE) {
        if (!open) return false;
        ctx->open_select = NULL;
        ctx->popup_highlight = NULL;
        return true;
    }
    if (key == TBOX_KEY_ENTER || key == TBOX_KEY_SPACE) {
        if (!open) {
            if (tbox_context_option_count(select) == 0) return false;
            ctx->open_select = select;
            ctx->popup_highlight = field->selected;
            if (ctx->popup_highlight != NULL && tbox_context_option_disabled(ctx->popup_highlight))
                ctx->popup_highlight = tbox_context_select_neighbor(select, NULL, TBOX_KEY_HOME);
            ctx->popup_first = 0;
            tbox_context_popup_reveal_highlight(ctx);
        } else {
            const tbox_html_node *chosen = ctx->popup_highlight;
            ctx->open_select = NULL;
            ctx->popup_highlight = NULL;
            tbox_context_select_commit(ctx, select, chosen);
        }
        return true;
    }
    if (key == TBOX_KEY_UP || key == TBOX_KEY_DOWN || key == TBOX_KEY_HOME || key == TBOX_KEY_END) {
        const tbox_html_node *current = open ? ctx->popup_highlight : field->selected;
        const tbox_html_node *next = tbox_context_select_neighbor(select, current, key);
        if (next == NULL || next == current) return false;
        if (open) {
            ctx->popup_highlight = next;
            tbox_context_popup_reveal_highlight(ctx);
            return true;
        }
        return tbox_context_select_commit(ctx, select, next);
    }
    return false;
}

tbox_string_view tbox_context_select_value(tbox_context *ctx, const tbox_html_node *select) {
    if (ctx == NULL || !tbox_context_is_select(select) || !tbox_context_node_attached(ctx, select))
        return tbox_string_view_make(NULL, 0);
    tbox_select_field *field = tbox_context_select_field(ctx, select);
    return field != NULL && field->selected != NULL ?
        tbox_context_option_value(ctx, field->selected) : tbox_string_view_make(NULL, 0);
}

bool tbox_context_select_set_value(tbox_context *ctx, const tbox_html_node *select, tbox_string_view value) {
    if (ctx == NULL || !tbox_context_is_select(select) || !tbox_context_node_attached(ctx, select))
        return false;
    tbox_select_field *field = tbox_context_select_field(ctx, select);
    if (field == NULL) return false;
    for (const tbox_html_node *option = select->first_child; option != NULL; option = option->next_sibling) {
        if (!tbox_context_is_option(option) ||
            !tbox_string_view_equal(tbox_context_option_value(ctx, option), value)) continue;
        if (field->selected == option) return false;
        field->selected = option;
        field->dirty = true;
        if (ctx->open_select == select) {
            ctx->popup_highlight = option;
            tbox_context_popup_reveal_highlight(ctx);
        }
        return true;
    }
    return false;
}

void tbox_context_on_select(tbox_context *ctx, tbox_context_select_handler handler, void *userdata) {
    if (ctx == NULL) return;
    ctx->select_handler = handler;
    ctx->select_userdata = userdata;
}
