#include "tbox_context_internal.h"

static bool tbox_context_is_date_input(const tbox_html_node *node);
static bool tbox_context_number_parse(tbox_string_view value, double *out);
static bool tbox_context_is_email_input(const tbox_html_node *node);
static bool tbox_context_email_ascii_alnum(unsigned char c);
static bool tbox_context_email_address_valid(tbox_string_view address);

bool tbox_context_is_text_control(const tbox_html_node *node) {
    return tbox_context_is_text_input(node) || tbox_context_is_textarea(node);
}

bool tbox_context_is_select(const tbox_html_node *node) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT &&
        tbox_string_view_equal_cstr(node->element.tag_name, "select");
}

bool tbox_context_is_color_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("color", 5));
}

static bool tbox_context_is_date_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("date", 4));
}

bool tbox_context_is_datetime_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL &&
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("datetime-local", 14));
}

bool tbox_context_is_month_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("month", 5));
}

bool tbox_context_is_time_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("time", 4));
}

bool tbox_context_is_week_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("week", 4));
}

bool tbox_context_is_calendar_input(const tbox_html_node *node) {
    return tbox_context_is_date_input(node) || tbox_context_is_datetime_input(node) ||
        tbox_context_is_month_input(node) || tbox_context_is_time_input(node) ||
        tbox_context_is_week_input(node);
}

bool tbox_context_is_file_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("file", 4));
}

/* Drops a value the control can't hold (bad date/number, file name with
 * no chosen path), clamps range, and notes which control kinds exist. */
void tbox_context_sanitize_input(tbox_context *ctx, tbox_html_node *node) {
    if (node->type == TBOX_HTML_NODE_ELEMENT &&
        (tbox_string_view_equal_cstr(node->element.tag_name, "progress") ||
         tbox_string_view_equal_cstr(node->element.tag_name, "meter")))
        ctx->has_gauge = true;
    if (tbox_context_is_calendar_input(node)) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        tbox_date date;
        int hour, minute;
        bool valid = value != NULL && (tbox_context_is_datetime_input(node) ?
            tbox_context_datetime_parse(value->value, &date, &hour, &minute) :
            tbox_context_is_time_input(node) ? tbox_context_time_parse(value->value, &hour, &minute) :
            tbox_context_is_week_input(node) ? tbox_context_week_parse(value->value, &date) :
            tbox_context_is_month_input(node) ? tbox_context_month_parse(value->value, &date) :
            tbox_context_date_parse(value->value, &date));
        if (value != NULL && value->value.size > 0 && !valid)
            tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("value", 5),
                tbox_string_view_make("", 0));
    }
    if (tbox_context_is_number_input(node)) {
        ctx->has_number_input = true;
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        double parsed;
        if (value != NULL && value->value.size > 0 &&
            !tbox_context_number_parse(value->value, &parsed))
            tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("value", 5),
                tbox_string_view_make("", 0));
    }
    if (tbox_context_is_range_input(node)) {
        ctx->has_range_input = true;
        tbox_context_range_normalize(ctx, node);
    }
    if (tbox_context_is_search_input(node)) ctx->has_search_input = true;
    if (tbox_context_is_file_input(node)) {
        ctx->has_file_input = true;
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        tbox_file_field *field = tbox_context_file_field(ctx, node, false);
        const char *name = field != NULL && field->path != NULL ? strrchr(field->path, '/') : NULL;
        if (name != NULL) name++;
        if (field != NULL && field->path != NULL &&
            (value == NULL || name == NULL || !tbox_string_view_equal(value->value,
                tbox_string_view_from_cstr(name)))) {
            free(field->path);
            field->path = NULL;
        }
        if (value != NULL && value->value.size > 0 && (field == NULL || field->path == NULL))
            tbox_html_node_set_attribute(ctx->document, node, tbox_string_view_make("value", 5),
                tbox_string_view_make("", 0));
    }
}

void tbox_context_sanitize_inputs(tbox_context *ctx, tbox_html_node *node) {
    for (; node != NULL; node = node->next_sibling) {
        tbox_context_sanitize_input(ctx, node);
        tbox_context_sanitize_inputs(ctx, node->first_child);
    }
}

bool tbox_context_input_control_rect(const tbox_layout_box *box, tbox_rect *out) {
    if (box == NULL || box->style == NULL) return false;
    tbox_style_part part;
    if (tbox_context_is_number_input(box->node)) {
        part = TBOX_STYLE_PART_STEP_CONTROL;
    } else {
        const tbox_html_attribute *value = tbox_context_is_search_input(box->node) ?
            tbox_html_node_get_attribute(box->node, tbox_string_view_make("value", 5)) : NULL;
        if (value == NULL || value->value.size == 0) return false;
        part = TBOX_STYLE_PART_CLEAR_ICON;
    }
    double width = tbox_context_part_size(tbox_context_part(box->style, part)->width, box->content_box.width, 16.0);
    if (width <= 0.0 || box->content_box.width < width + 4.0 || box->content_box.height < 14.0) return false;
    *out = (tbox_rect){box->content_box.x + box->content_box.width - width, box->content_box.y,
                       width, box->content_box.height};
    return true;
}

/* ::step-control is the column (its left border also draws the divider
 * between the halves); ::step-up and ::step-down are the halves, whose
 * `color` paints the arrows (the input's color by default). */
void tbox_context_paint_number_steppers(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_rect column;
        if (tbox_context_is_number_input(box->node) && !box->style->visibility_hidden &&
            tbox_context_input_control_rect(box, &column)) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            const tbox_style_part_style *control = tbox_context_part(box->style, TBOX_STYLE_PART_STEP_CONTROL);
            const tbox_style_part_style *up      = tbox_context_part(box->style, TBOX_STYLE_PART_STEP_UP);
            const tbox_style_part_style *down    = tbox_context_part(box->style, TBOX_STYLE_PART_STEP_DOWN);
            tbox_context_paint_part(items, column, control, (tbox_css_rgba){238, 240, 243, 255}, true, clip);
            double inner_x = column.x + control->border_widths[3];
            double inner_w = column.width - control->border_widths[3];
            double middle  = column.y + column.height / 2.0;
            tbox_context_paint_part(items, (tbox_rect){inner_x, column.y, inner_w, middle - column.y}, up,
                (tbox_css_rgba){0, 0, 0, 0}, true, clip);
            tbox_context_paint_part(items, (tbox_rect){inner_x, middle, inner_w, column.y + column.height - middle}, down,
                (tbox_css_rgba){0, 0, 0, 0}, true, clip);
            if (control->border_widths[3] > 0.0)
                tbox_context_push_fill(items, (tbox_rect){inner_x, middle, inner_w, 1.0}, control->border_colors[3], true, clip);
            tbox_css_rgba up_color   = up->color_declared ? up->color : box->style->color;
            tbox_css_rgba down_color = down->color_declared ? down->color : box->style->color;
            double center = column.x + column.width / 2.0;
            for (int row = 0; row < 3; row++) {
                tbox_context_push_fill(items, (tbox_rect){center - 1.0 - row,
                    column.y + 3.0 + row, 2.0 + 2.0 * row, 1.0}, up_color, true, clip);
                tbox_context_push_fill(items, (tbox_rect){center - 3.0 + row,
                    middle + 3.0 + row, 6.0 - 2.0 * row, 1.0}, down_color, true, clip);
            }
        }
        tbox_context_paint_number_steppers(box->first_child, items);
    }
}

/* ::clear-icon: the column behind the X, which takes its `color` (the
 * input's color by default). */
void tbox_context_paint_search_clears(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_rect column;
        if (tbox_context_is_search_input(box->node) && !box->style->visibility_hidden &&
            tbox_context_input_control_rect(box, &column)) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            const tbox_style_part_style *icon = tbox_context_part(box->style, TBOX_STYLE_PART_CLEAR_ICON);
            tbox_context_paint_part(items, column, icon, (tbox_css_rgba){245, 246, 248, 255}, true, clip);
            tbox_css_rgba color = icon->color_declared ? icon->color : box->style->color;
            double x = column.x + (column.width - 16.0) / 2.0;
            double y = column.y + column.height / 2.0 - 5.0;
            for (int i = 0; i < 5; i++) {
                tbox_context_push_fill(items, (tbox_rect){x + 4.0 + i, y + i, 1.5, 1.5}, color, true, clip);
                tbox_context_push_fill(items, (tbox_rect){x + 8.0 - i, y + i, 1.5, 1.5}, color, true, clip);
            }
        }
        tbox_context_paint_search_clears(box->first_child, items);
    }
}

bool tbox_context_is_text_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) {
        return false;
    }
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type == NULL || tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("text", 4)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("email", 5)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("number", 6)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("search", 6)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("tel", 3)) ||
        tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("url", 3));
}

bool tbox_context_is_search_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("search", 6));
}

bool tbox_context_is_number_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("number", 6));
}

bool tbox_context_is_range_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("range", 5));
}

/* Accepts an editable prefix such as "-", "1.", or "2e-" while typing.
 * Complete values are converted only after the grammar reaches a number. */
bool tbox_context_number_syntax(tbox_string_view value, bool complete) {
    if (value.data == NULL && value.size > 0) return false;
    size_t at = 0;
    if (at < value.size && value.data[at] == '-') at++;
    bool digits = false;
    bool decimal = false;
    bool fraction_digits = false;
    while (at < value.size && value.data[at] >= '0' && value.data[at] <= '9') {
        at++;
        digits = true;
    }
    if (at < value.size && value.data[at] == '.') {
        decimal = true;
        at++;
        while (at < value.size && value.data[at] >= '0' && value.data[at] <= '9') {
            at++;
            digits = true;
            fraction_digits = true;
        }
    }
    if (complete && decimal && !fraction_digits) return false;
    if (at < value.size && (value.data[at] == 'e' || value.data[at] == 'E')) {
        if (!digits) return false;
        at++;
        if (at < value.size && (value.data[at] == '+' || value.data[at] == '-')) at++;
        bool exponent_digits = false;
        while (at < value.size && value.data[at] >= '0' && value.data[at] <= '9') {
            at++;
            exponent_digits = true;
        }
        if (complete && !exponent_digits) return false;
    }
    return at == value.size && (!complete || digits);
}

static bool tbox_context_number_parse(tbox_string_view value, double *out) {
    if (value.size == 0 || !tbox_context_number_syntax(value, true)) return false;
    double parsed;
    if (!tbox_number_parse(value, &parsed)) return false;
    if (out != NULL) *out = parsed;
    return true;
}

bool tbox_context_number_attribute(const tbox_html_node *node, const char *name, double *out) {
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name));
    return attribute != NULL && tbox_context_number_parse(attribute->value, out);
}

bool tbox_context_number_valid(const tbox_html_node *input) {
    if (!tbox_context_is_number_input(input)) return false;
    const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
    if (value == NULL || value->value.size == 0)
        return tbox_html_node_get_attribute(input, tbox_string_view_make("required", 8)) == NULL;
    double number;
    if (!tbox_context_number_parse(value->value, &number)) return false;
    double min, max;
    bool has_min = tbox_context_number_attribute(input, "min", &min);
    bool has_max = tbox_context_number_attribute(input, "max", &max);
    if ((has_min && number < min) || (has_max && number > max)) return false;
    const tbox_html_attribute *step_attr = tbox_html_node_get_attribute(input, tbox_string_view_make("step", 4));
    if (step_attr != NULL && tbox_string_view_equal_ascii_ci(step_attr->value, tbox_string_view_make("any", 3)))
        return true;
    double step = 1.0;
    double parsed_step;
    if (tbox_context_number_attribute(input, "step", &parsed_step) && parsed_step > 0.0) step = parsed_step;
    double quotient = (number - (has_min ? min : 0.0)) / step;
    return isfinite(quotient) && fabs(quotient - round(quotient)) <= 1e-9;
}

static bool tbox_context_is_email_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("email", 5));
}

static bool tbox_context_email_ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool tbox_context_email_address_valid(tbox_string_view address) {
    size_t at = 0;
    while (at < address.size && address.data[at] != '@') at++;
    if (at == 0 || at + 1 >= address.size) return false;
    static const char local_punctuation[] = "!#$%&'*+/=?^_`{|}~-";
    for (size_t i = 0; i < at; i++) {
        unsigned char c = (unsigned char)address.data[i];
        if (c == '.') {
            if (i == 0 || i + 1 == at || address.data[i - 1] == '.') return false;
        } else if (!tbox_context_email_ascii_alnum(c) && (c == 0 || strchr(local_punctuation, c) == NULL))
            return false;
    }
    size_t label_start = at + 1;
    for (size_t i = label_start; i <= address.size; i++) {
        if (i == address.size || address.data[i] == '.') {
            if (i == label_start || i - label_start > 63 ||
                !tbox_context_email_ascii_alnum((unsigned char)address.data[i - 1])) return false;
            label_start = i + 1;
        } else if (!tbox_context_email_ascii_alnum((unsigned char)address.data[i]) && address.data[i] != '-')
            return false;
        else if (i == label_start && address.data[i] == '-') return false;
    }
    return true;
}

bool tbox_context_email_valid(const tbox_html_node *input) {
    if (!tbox_context_is_email_input(input)) return false;
    const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
    if (value == NULL || value->value.size == 0)
        return tbox_html_node_get_attribute(input, tbox_string_view_make("required", 8)) == NULL;
    bool multiple = tbox_html_node_get_attribute(input, tbox_string_view_make("multiple", 8)) != NULL;
    tbox_string_view text = value->value;
    if (text.data == NULL) return false;
    size_t start = 0;
    while (start < text.size) {
        size_t end = start;
        while (end < text.size && (!multiple || text.data[end] != ',')) end++;
        size_t first = start, last = end;
        while (first < last && tbox_ascii_is_space(text.data[first])) first++;
        while (last > first && tbox_ascii_is_space(text.data[last - 1])) last--;
        if (!tbox_context_email_address_valid(tbox_string_view_make(text.data + first, last - first))) return false;
        if (end == text.size) break;
        start = end + 1;
        if (start == text.size) return false;
    }
    return true;
}

bool tbox_context_url_valid(const tbox_html_node *input) {
    if (input == NULL || input->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(input->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(input, tbox_string_view_make("type", 4));
    if (type == NULL || !tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("url", 3)))
        return false;
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
    if (attribute == NULL || attribute->value.size == 0)
        return tbox_html_node_get_attribute(input, tbox_string_view_make("required", 8)) == NULL;
    tbox_string_view value = attribute->value;
    if (value.data == NULL || value.size < 3 ||
        !((value.data[0] >= 'a' && value.data[0] <= 'z') ||
          (value.data[0] >= 'A' && value.data[0] <= 'Z'))) return false;
    size_t colon = 1;
    while (colon < value.size && value.data[colon] != ':') {
        unsigned char c = (unsigned char)value.data[colon];
        if (!tbox_context_email_ascii_alnum(c) && c != '+' && c != '-' && c != '.') return false;
        colon++;
    }
    if (colon + 1 >= value.size) return false;
    for (size_t i = colon + 1; i < value.size; i++) {
        unsigned char c = (unsigned char)value.data[i];
        if (c <= 0x20 || c == 0x7f) return false;
        if (c == '%' && (i + 2 >= value.size || !tbox_ascii_is_hex_digit(value.data[i + 1]) ||
                         !tbox_ascii_is_hex_digit(value.data[i + 2]))) return false;
    }
    tbox_string_view scheme = tbox_string_view_make(value.data, colon);
    bool web_scheme = tbox_string_view_equal_ascii_ci(scheme, tbox_string_view_make("http", 4)) ||
        tbox_string_view_equal_ascii_ci(scheme, tbox_string_view_make("https", 5));
    if (web_scheme) {
        if (colon + 3 >= value.size || value.data[colon + 1] != '/' || value.data[colon + 2] != '/')
            return false;
        size_t host = colon + 3;
        if (value.data[host] == '/' || value.data[host] == '?' || value.data[host] == '#') return false;
    }
    return true;
}

bool tbox_context_clear_search(tbox_context *ctx, const tbox_html_node *node) {
    tbox_text_field *field = tbox_context_text_field(ctx, node);
    if (field == NULL || field->length == 0) return false;
    field->value[0] = '\0';
    field->length = field->cursor = field->anchor = 0;
    field->scroll_x = 0.0;
    field->reveal = true;
    tbox_context_sync_text_value(ctx, field);
    return true;
}

bool tbox_context_number_step(tbox_context *ctx, const tbox_html_node *node, int direction, bool large) {
    tbox_text_field *field = tbox_context_text_field(ctx, node);
    if (field == NULL) return false;
    /* Initialized although only read behind has_min/has_max: optimized
     * builds can't prove that (-Wmaybe-uninitialized). */
    double min = 0.0, max = 0.0, step;
    bool has_min = tbox_context_number_attribute(node, "min", &min);
    bool has_max = tbox_context_number_attribute(node, "max", &max);
    if (!tbox_context_number_attribute(node, "step", &step) || step <= 0.0) step = 1.0;
    if (large) step *= 10.0;
    if (!isfinite(step) || step <= 0.0) return false;
    double current;
    bool has_current = tbox_context_number_parse(tbox_string_view_make(field->value, field->length), &current);
    double base = has_min ? min : 0.0;
    double next;
    if (!has_current) next = direction > 0 ? (has_min ? min : step) : (has_max ? max : -step);
    else {
        double units = (current - base) / step;
        if (!isfinite(units)) return false;
        double target = direction > 0 ? floor(units + 1e-9) + 1.0 : ceil(units - 1e-9) - 1.0;
        next = base + target * step;
    }
    if (!isfinite(next) || (has_min && next < min - 1e-9) ||
        (has_max && next > max + 1e-9)) return false;
    if (has_current && next == current) return false;
    char buffer[64];
    int length = tbox_number_format(buffer, sizeof(buffer), next, 15);
    if (length <= 0 || (size_t)length >= sizeof(buffer)) return false;
    if (field->capacity < (size_t)length + 1) {
        char *value = realloc(field->value, (size_t)length + 1);
        if (value == NULL) return false;
        field->value = value;
        field->capacity = (size_t)length + 1;
    }
    memcpy(field->value, buffer, (size_t)length + 1);
    field->length = field->cursor = field->anchor = (size_t)length;
    field->reveal = true;
    tbox_context_sync_text_value(ctx, field);
    return true;
}
