#include "tbox_context_internal.h"

static bool tbox_context_dispatch_click_node(tbox_context *ctx, const tbox_html_node *node);

int tbox_context_on_click(tbox_context *ctx, const char *selector, size_t selector_length, tbox_context_click_handler handler, void *userdata) {
    if (ctx == NULL || handler == NULL) {
        return -1;
    }

    /* Hard-fails on a syntax error (see <tbox/css_selector.h>) -- nothing
     * is registered in that case, matching the documented contract. */
    tbox_css_selector_query *query = tbox_css_selector_compile(selector, selector_length, NULL);
    if (query == NULL) {
        return -1;
    }

    tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_push(&ctx->handlers);
    binding->query                      = query;
    binding->handler                    = handler;
    binding->userdata                   = userdata;
    binding->active                     = true;
    binding->id                         = ctx->next_handler_id;
    ctx->next_handler_id++;
    return binding->id;
}

bool tbox_context_unbind_click(tbox_context *ctx, int binding) {
    if (ctx == NULL) {
        return false;
    }

    size_t handler_count = tbox_vector_length(&ctx->handlers);
    for (size_t i = 0; i < handler_count; i++) {
        tbox_context_click_binding *entry = (tbox_context_click_binding *)tbox_vector_at(&ctx->handlers, i);
        if (entry->active && entry->id == binding) {
            /* Tombstone rather than physically removing the slot -- see
             * tbox_context_click_binding's doc comment above for why. */
            tbox_css_selector_query_destroy(entry->query);
            entry->query  = NULL;
            entry->active = false;
            return true;
        }
    }

    return false;
}

static bool tbox_context_dispatch_click_node(tbox_context *ctx, const tbox_html_node *node) {
    tbox_css_selector_set_hover_context(ctx->hovered_node);
    tbox_css_selector_set_focus_context(ctx->focused_node);
    bool dispatched      = false;
    size_t handler_count = tbox_vector_length(&ctx->handlers);

    /* walk the ancestor chain ONCE, nearest to farthest -- real
     * bubbling order. At each level, test EVERY currently-active binding
     * (in registration order); every one that matches fires. A handler
     * returning false (stopPropagation) stops the ancestor walk
     * immediately, so no farther ancestor is even tested. */
    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent) {
        bool stop_propagation = false;

        for (size_t i = 0; i < handler_count; i++) {
            const tbox_context_click_binding *binding = (const tbox_context_click_binding *)tbox_vector_at_const(&ctx->handlers, i);
            if (!binding->active) {
                continue;
            }

            if (tbox_css_selector_query_matches(binding->query, ancestor)) {
                /* Non-const cast: tbox_layout_box::node is const (layout's
                 * own read-only view), but a click handler's whole point is
                 * to be able to mutate the tree (e.g.
                 * tbox_html_node_set_attribute) -- see
                 * tbox_context_click_handler's signature. */
                bool keep_propagating = binding->handler(ctx, (tbox_html_node *)ancestor, binding->userdata);
                dispatched            = true;

                if (!keep_propagating) {
                    stop_propagation = true;
                    break;
                }
            }
        }

        if (stop_propagation) {
            break;
        }
    }

    return dispatched;
}

bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return false;
    }
    ctx->range_drag_node                 = NULL;
    const tbox_html_node *previous_color = ctx->open_color;
    const tbox_html_node *previous_date  = ctx->open_date;
    const tbox_html_node *previous_file  = ctx->open_file;
    if (tbox_context_file_popup_visible(ctx)) {
        if (tbox_context_file_popup_contains(ctx, x, y))
            return tbox_context_file_popup_click(ctx, x, y);
        tbox_context_file_close(ctx);
    }
    if (tbox_context_date_popup_visible(ctx)) {
        if (tbox_context_date_popup_contains(ctx, x, y))
            return tbox_context_date_popup_click(ctx, x, y);
        ctx->open_date = NULL;
    }
    if (tbox_context_color_popup_visible(ctx)) {
        if (tbox_context_color_popup_contains(ctx, x, y)) {
            tbox_context_color_choose_point(ctx, x, y);
            return true;
        }
        ctx->open_color = NULL;
    }
    bool was_open = ctx->open_select != NULL;
    if (was_open && tbox_context_popup_click(ctx, x, y))
        return true;
    const tbox_html_node *previous_open = ctx->open_select;
    ctx->open_select                    = NULL;
    ctx->popup_highlight                = NULL;
    const tbox_layout_box *box          = tbox_context_hit_test(ctx, x, y);
    if (box == NULL) {
        tbox_context_set_focus(ctx, NULL);
        return false;
    }
    while (box != NULL && box->node == NULL) {
        box = box->parent;
    }
    if (box == NULL) {
        return false;
    }

    if (box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(box->node->element.tag_name, "input") && tbox_html_node_get_attribute(box->node, tbox_string_view_make("disabled", 8)) != NULL) {
        tbox_context_set_focus(ctx, NULL);
        return false;
    }

    const tbox_html_node *focus = NULL;
    for (const tbox_html_node *node = box->node; node != NULL; node = node->parent) {
        if (tbox_context_focusable(ctx, node)) {
            focus = node;
            break;
        }
    }
    tbox_context_set_focus(ctx, focus);
    bool clicked_color = tbox_context_is_color_input(focus);
    bool opened_color  = clicked_color && focus != previous_color;
    if (opened_color) {
        ctx->open_color    = focus;
        ctx->color_channel = 0;
    }
    bool clicked_date = tbox_context_is_calendar_input(focus);
    bool opened_date  = clicked_date && focus != previous_date;
    if (opened_date)
        tbox_context_date_open(ctx, focus);
    bool clicked_file = tbox_context_is_file_input(focus);
    bool opened_file  = clicked_file && focus != previous_file && tbox_context_file_open(ctx, focus);
    if (tbox_context_is_select(focus) && focus != previous_open && tbox_context_option_count(focus) > 0) {
        tbox_select_field *field = tbox_context_select_field(ctx, focus);
        ctx->open_select         = focus;
        ctx->popup_highlight     = field != NULL ? field->selected : NULL;
        if (ctx->popup_highlight != NULL && tbox_context_option_disabled(ctx->popup_highlight))
            ctx->popup_highlight = NULL;
        ctx->popup_first = 0;
        tbox_context_popup_reveal_highlight(ctx);
    }
    if (tbox_context_is_text_control(focus)) {
        tbox_text_field *field           = tbox_context_text_field(ctx, focus);
        const tbox_layout_box *input_box = tbox_context_find_box(ctx->root, focus);
        if (field != NULL && input_box != NULL && input_box->style != NULL) {
            const tbox_style *style    = input_box->style;
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts, tbox_string_view_from_cstr(style->font_family), style->font_weight_bold, style->font_italic, style->font_size);
            if (face != NULL) {
                size_t best   = tbox_context_is_textarea(focus) ? tbox_context_cursor_at_point(field, input_box, face, x, y) : tbox_context_cursor_at_x(field, input_box, face, x);
                field->cursor = best;
                field->anchor = best;
                field->reveal = true;
            }
        }
    }
    bool toggled = tbox_context_is_checkbox_input(box->node);
    if (toggled)
        tbox_context_toggle_checkbox(ctx, box->node);
    bool selected_radio = tbox_context_is_radio_input(box->node) && tbox_context_radio_select(ctx, box->node);
    bool reset_form     = tbox_context_is_reset_input(box->node) && tbox_context_reset_form(ctx, box->node);
    bool cleared_search = false;
    if (tbox_context_is_search_input(focus)) {
        const tbox_layout_box *search_box = tbox_context_find_box(ctx->root, focus);
        if (search_box != NULL && search_box->content_box.width >= 20.0 && search_box->content_box.height >= 14.0 && x >= search_box->content_box.x + search_box->content_box.width - 16.0 && x < search_box->content_box.x + search_box->content_box.width && y >= search_box->content_box.y && y < search_box->content_box.y + search_box->content_box.height)
            cleared_search = tbox_context_clear_search(ctx, focus);
    }
    bool changed_range = false;
    if (tbox_context_is_range_input(focus)) {
        ctx->range_drag_node = focus;
        changed_range        = tbox_context_range_choose_x(ctx, focus, x);
    }
    bool stepped = false;
    if (tbox_context_is_number_input(focus)) {
        const tbox_layout_box *number_box = tbox_context_find_box(ctx->root, focus);
        if (number_box != NULL && number_box->content_box.width >= 20.0 && number_box->content_box.height >= 14.0 && x >= number_box->content_box.x + number_box->content_box.width - 16.0 && x < number_box->content_box.x + number_box->content_box.width && y >= number_box->content_box.y && y < number_box->content_box.y + number_box->content_box.height)
            stepped = tbox_context_number_step(ctx, focus, y < number_box->content_box.y + number_box->content_box.height / 2.0 ? 1 : -1, false);
    }
    bool clicked   = tbox_context_dispatch_click_node(ctx, box->node);
    bool submitted = tbox_context_is_submit_input(box->node) && tbox_context_submit_form(ctx, box->node, box->node);
    return clicked || submitted || toggled || selected_radio || reset_form || cleared_search || changed_range || opened_color || (clicked_color && focus == previous_color) || clicked_date || opened_file || (clicked_file && focus == previous_file) || stepped;
}

bool tbox_context_dispatch_key(tbox_context *ctx, tbox_key_event event) {
    if (ctx == NULL || !event.pressed) {
        return false;
    }
    if (event.key == TBOX_KEY_TAB) {
        ctx->open_select = NULL;
        ctx->open_color  = NULL;
        ctx->open_date   = NULL;
        tbox_context_file_close(ctx);
        ctx->popup_highlight = NULL;
        return tbox_context_move_focus(ctx, event.shift);
    }
    if (tbox_context_is_file_input(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        if (event.key == TBOX_KEY_ESCAPE && ctx->open_file != NULL) {
            tbox_context_file_close(ctx);
            return true;
        }
        if (event.key == TBOX_KEY_ENTER || event.key == TBOX_KEY_SPACE) {
            if (ctx->open_file == NULL)
                return tbox_context_file_open(ctx, ctx->focused_node);
            return tbox_context_file_activate(ctx, ctx->file_highlight);
        }
        if (ctx->open_file != NULL) {
            if (event.key == TBOX_KEY_BACKSPACE)
                return tbox_context_file_parent_directory(ctx);
            size_t next = ctx->file_highlight;
            if (event.key == TBOX_KEY_UP && next > 0)
                next--;
            else if (event.key == TBOX_KEY_DOWN && next + 1 < ctx->file_count)
                next++;
            else if (event.key == TBOX_KEY_HOME)
                next = 0;
            else if (event.key == TBOX_KEY_END && ctx->file_count > 0)
                next = ctx->file_count - 1;
            if (next != ctx->file_highlight) {
                ctx->file_highlight = next;
                if (next < ctx->file_first)
                    ctx->file_first = next;
                if (next >= ctx->file_first + 7)
                    ctx->file_first = next + 1 - 7;
                return true;
            }
        }
        return false;
    }
    if (tbox_context_is_calendar_input(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        if (event.key == TBOX_KEY_ESCAPE && ctx->open_date != NULL) {
            ctx->open_date = NULL;
            return true;
        }
        if (event.key == TBOX_KEY_ENTER || event.key == TBOX_KEY_SPACE) {
            if (ctx->open_date == NULL) {
                tbox_context_date_open(ctx, ctx->focused_node);
                return true;
            }
            if (!tbox_context_calendar_selection_allowed(ctx))
                return false;
            const tbox_html_node *node = ctx->open_date;
            ctx->open_date             = NULL;
            tbox_context_date_commit(ctx, node, ctx->date_cursor);
            return true;
        }
        if (ctx->open_date != NULL) {
            if (tbox_context_is_time_input(ctx->open_date)) {
                int *part   = event.control ? &ctx->date_minute : &ctx->date_hour;
                int maximum = event.control ? 59 : 23;
                int next    = *part;
                if (event.key == TBOX_KEY_HOME)
                    next = 0;
                else if (event.key == TBOX_KEY_END)
                    next = maximum;
                else if (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_RIGHT)
                    next += event.shift ? 10 : 1;
                else if (event.key == TBOX_KEY_DOWN || event.key == TBOX_KEY_LEFT)
                    next -= event.shift ? 10 : 1;
                else
                    return false;
                if (next < 0)
                    next = 0;
                if (next > maximum)
                    next = maximum;
                bool changed = next != *part;
                *part        = next;
                return changed;
            }
            if (tbox_context_is_week_input(ctx->open_date)) {
                tbox_date next = ctx->date_cursor;
                bool changed   = false;
                if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT)
                    changed = tbox_context_date_shift_day(&next, event.key == TBOX_KEY_RIGHT ? 7 : -7);
                else if (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN)
                    changed = tbox_context_date_shift_day(&next, event.key == TBOX_KEY_DOWN ? 28 : -28);
                else if (event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
                    next.day = event.key == TBOX_KEY_HOME ? 1 : tbox_context_date_days(next.year, next.month);
                    changed  = tbox_context_week_monday(&next) && tbox_context_date_compare(next, ctx->date_cursor) != 0;
                }
                if (changed)
                    ctx->date_cursor = next;
                return changed;
            }
            if (tbox_context_is_month_input(ctx->open_date)) {
                tbox_date next = ctx->date_cursor;
                bool changed   = false;
                if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT) {
                    int direction = event.key == TBOX_KEY_RIGHT ? 1 : -1;
                    changed       = event.shift ? tbox_context_date_shift_year(&next, direction) : tbox_context_date_shift_month(&next, direction);
                } else if (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN) {
                    int direction = event.key == TBOX_KEY_DOWN ? 1 : -1;
                    for (int i = 0; i < 4; i++) {
                        if (!tbox_context_date_shift_month(&next, direction))
                            break;
                        changed = true;
                    }
                } else if (event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
                    next.month = event.key == TBOX_KEY_HOME ? 1 : 12;
                    changed    = next.month != ctx->date_cursor.month;
                }
                if (changed)
                    ctx->date_cursor = next;
                return changed;
            }
            if (tbox_context_is_datetime_input(ctx->open_date) && (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN || event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) && (event.shift || event.control)) {
                int *part      = event.control ? &ctx->date_minute : &ctx->date_hour;
                int maximum    = event.control ? 59 : 23;
                int next_value = *part;
                if (event.key == TBOX_KEY_HOME)
                    next_value = 0;
                else if (event.key == TBOX_KEY_END)
                    next_value = maximum;
                else
                    next_value += event.key == TBOX_KEY_UP ? 1 : -1;
                if (next_value < 0)
                    next_value = 0;
                if (next_value > maximum)
                    next_value = maximum;
                bool changed = next_value != *part;
                *part        = next_value;
                return changed;
            }
            tbox_date next = ctx->date_cursor;
            bool changed   = false;
            if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT) {
                int direction = event.key == TBOX_KEY_RIGHT ? 1 : -1;
                changed       = event.shift ? tbox_context_date_shift_month(&next, direction) : tbox_context_date_shift_day(&next, direction);
            } else if (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN)
                changed = tbox_context_date_shift_day(&next, event.key == TBOX_KEY_DOWN ? 7 : -7);
            else if (event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
                next.day = event.key == TBOX_KEY_HOME ? 1 : tbox_context_date_days(next.year, next.month);
                changed  = next.day != ctx->date_cursor.day;
            }
            if (changed)
                ctx->date_cursor = next;
            return changed;
        }
        return false;
    }
    if (tbox_context_is_color_input(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        if (event.key == TBOX_KEY_ENTER || event.key == TBOX_KEY_SPACE) {
            ctx->open_color = ctx->open_color == ctx->focused_node ? NULL : ctx->focused_node;
            return true;
        }
        if (event.key == TBOX_KEY_ESCAPE && ctx->open_color != NULL) {
            ctx->open_color = NULL;
            return true;
        }
        if (ctx->open_color != NULL) {
            if (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN) {
                unsigned old       = ctx->color_channel;
                ctx->color_channel = event.key == TBOX_KEY_UP ? (old + 2) % 3 : (old + 1) % 3;
                return true;
            }
            if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT || event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
                tbox_css_rgba color   = tbox_context_color_value(ctx->open_color);
                unsigned char current = ctx->color_channel == 0 ? color.r : ctx->color_channel == 1 ? color.g : color.b;
                int value             = current;
                if (event.key == TBOX_KEY_HOME)
                    value = 0;
                else if (event.key == TBOX_KEY_END)
                    value = 255;
                else
                    value += event.key == TBOX_KEY_RIGHT ? (event.shift ? 10 : 1) : -(event.shift ? 10 : 1);
                if (value < 0)
                    value = 0;
                if (value > 255)
                    value = 255;
                if (ctx->color_channel == 0)
                    color.r = (unsigned char)value;
                else if (ctx->color_channel == 1)
                    color.g = (unsigned char)value;
                else
                    color.b = (unsigned char)value;
                return tbox_context_color_commit(ctx, ctx->open_color, color);
            }
        }
        return false;
    }
    if (tbox_context_is_select(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node))
        return tbox_context_dispatch_select_key(ctx, event.key);
    if (tbox_context_is_range_input(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        double min, max, step, value;
        bool any_step;
        tbox_context_range_limits(ctx->focused_node, &min, &max, &step, &any_step);
        if (!tbox_context_number_attribute(ctx->focused_node, "value", &value))
            value = min;
        if (event.key == TBOX_KEY_HOME)
            value = min;
        else if (event.key == TBOX_KEY_END)
            value = max;
        else if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_DOWN)
            value -= step * (event.shift ? 10.0 : 1.0);
        else if (event.key == TBOX_KEY_RIGHT || event.key == TBOX_KEY_UP)
            value += step * (event.shift ? 10.0 : 1.0);
        else
            return false;
        return tbox_context_range_commit(ctx, ctx->focused_node, value);
    }
    if (tbox_context_is_number_input(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node) && (event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN))
        return tbox_context_number_step(ctx, ctx->focused_node, event.key == TBOX_KEY_UP ? 1 : -1, event.shift);
    if (tbox_context_is_text_control(ctx->focused_node) && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
        if (field == NULL)
            return false;
        if (tbox_context_is_search_input(ctx->focused_node) && event.key == TBOX_KEY_ESCAPE)
            return tbox_context_clear_search(ctx, ctx->focused_node);
        if (!tbox_context_is_textarea(ctx->focused_node) && event.key == TBOX_KEY_ENTER)
            return tbox_context_submit_form(ctx, ctx->focused_node, NULL);
        bool multiline = tbox_context_is_textarea(ctx->focused_node);
        if (multiline && event.key == TBOX_KEY_ENTER && !event.control)
            return tbox_context_dispatch_text(ctx, tbox_string_view_make("\n", 1));
        if (event.control && event.key == TBOX_KEY_A) {
            bool changed  = field->anchor != 0 || field->cursor != field->length;
            field->anchor = 0;
            field->cursor = field->length;
            field->reveal = true;
            return changed;
        }
        size_t start = field->cursor, end = field->cursor;
        size_t old_anchor = field->anchor;
        switch (event.key) {
        case TBOX_KEY_LEFT:
            if (!event.shift && field->anchor != field->cursor)
                field->cursor = field->anchor < field->cursor ? field->anchor : field->cursor;
            else
                field->cursor = tbox_utf8_previous(field->value, field->cursor);
            break;
        case TBOX_KEY_RIGHT:
            if (!event.shift && field->anchor != field->cursor)
                field->cursor = field->anchor > field->cursor ? field->anchor : field->cursor;
            else
                field->cursor = tbox_utf8_next(field->value, field->length, field->cursor);
            break;
        case TBOX_KEY_HOME:
            if (multiline) {
                while (field->cursor > 0 && field->value[field->cursor - 1] != '\n')
                    field->cursor--;
            } else
                field->cursor = 0;
            break;
        case TBOX_KEY_END:
            if (multiline) {
                while (field->cursor < field->length && field->value[field->cursor] != '\n')
                    field->cursor++;
            } else
                field->cursor = field->length;
            break;
        case TBOX_KEY_UP:
        case TBOX_KEY_DOWN: {
            if (!multiline)
                return false;
            const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
            if (box == NULL || box->style == NULL || field->line_count == 0)
                return false;
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts, tbox_string_view_from_cstr(box->style->font_family), box->style->font_weight_bold, box->style->font_italic, box->style->font_size);
            if (face == NULL)
                return false;
            size_t row = 0;
            for (size_t i = 0; i < field->line_count; i++)
                if (field->cursor >= field->lines[i].start && field->cursor <= field->lines[i].end)
                    row = i;
            double x      = box->content_box.x + tbox_font_measure_text_spaced(face, tbox_string_view_make(field->value + field->lines[row].start, field->cursor - field->lines[row].start), box->style->letter_spacing);
            double height = tbox_context_style_line_height(box->style, face);
            double y      = box->content_box.y + ((double)row + (event.key == TBOX_KEY_UP ? -0.5 : 1.5)) * height - field->scroll_y;
            field->cursor = tbox_context_cursor_at_point(field, box, face, x, y);
            break;
        }
        case TBOX_KEY_BACKSPACE:
            if (field->anchor != field->cursor) {
                start = field->anchor < field->cursor ? field->anchor : field->cursor;
                end   = field->anchor > field->cursor ? field->anchor : field->cursor;
            } else
                start = tbox_utf8_previous(field->value, field->cursor);
            break;
        case TBOX_KEY_DELETE:
            if (field->anchor != field->cursor) {
                start = field->anchor < field->cursor ? field->anchor : field->cursor;
                end   = field->anchor > field->cursor ? field->anchor : field->cursor;
            } else
                end = tbox_utf8_next(field->value, field->length, field->cursor);
            break;
        default:
            return false;
        }
        if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT || event.key == TBOX_KEY_UP || event.key == TBOX_KEY_DOWN || event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
            if (!event.shift)
                field->anchor = field->cursor;
            field->reveal = true;
            return field->cursor != start || field->anchor != old_anchor;
        }
        if (start == end)
            return false;
        memmove(field->value + start, field->value + end, field->length - end + 1);
        field->length -= end - start;
        field->cursor = field->anchor = start;
        field->reveal                 = true;
        tbox_context_sync_text_value(ctx, field);
        return true;
    }
    if ((event.key == TBOX_KEY_ENTER || event.key == TBOX_KEY_SPACE) && ctx->focused_node != NULL && tbox_context_node_attached(ctx, ctx->focused_node) && tbox_context_focusable(ctx, ctx->focused_node)) {
        tbox_string_view tag = ctx->focused_node->element.tag_name;
        if (tbox_context_is_reset_input(ctx->focused_node)) {
            bool reset   = tbox_context_reset_form(ctx, ctx->focused_node);
            bool clicked = tbox_context_dispatch_click_node(ctx, ctx->focused_node);
            return reset || clicked;
        }
        if (tbox_context_is_submit_input(ctx->focused_node)) {
            bool clicked   = tbox_context_dispatch_click_node(ctx, ctx->focused_node);
            bool submitted = tbox_context_submit_form(ctx, ctx->focused_node, ctx->focused_node);
            return clicked || submitted;
        }
        if (tbox_string_view_equal_cstr(tag, "button") || tbox_context_is_button_input(ctx->focused_node) || tbox_context_is_image_input(ctx->focused_node)) {
            return tbox_context_dispatch_click_node(ctx, ctx->focused_node);
        }
        if (event.key == TBOX_KEY_SPACE && tbox_context_is_checkbox_input(ctx->focused_node)) {
            tbox_context_toggle_checkbox(ctx, ctx->focused_node);
            tbox_context_dispatch_click_node(ctx, ctx->focused_node);
            return true;
        }
        if (event.key == TBOX_KEY_SPACE && tbox_context_is_radio_input(ctx->focused_node)) {
            bool changed = tbox_context_radio_select(ctx, ctx->focused_node);
            return tbox_context_dispatch_click_node(ctx, ctx->focused_node) || changed;
        }
    }
    return false;
}
