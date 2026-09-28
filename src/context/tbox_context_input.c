#include "tbox_context_internal.h"

static bool tbox_context_is_date_input(const tbox_html_node *node);
static bool tbox_context_number_parse(tbox_string_view value, double *out);
static bool tbox_context_is_email_input(const tbox_html_node *node);
static bool tbox_context_email_ascii_alnum(unsigned char c);
static bool tbox_context_email_address_valid(tbox_string_view address);
static bool tbox_context_email_ascii_space(char c);

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

void tbox_context_sanitize_inputs(tbox_context *ctx, tbox_html_node *node) {
    for (; node != NULL; node = node->next_sibling) {
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
        tbox_context_sanitize_inputs(ctx, node->first_child);
    }
}

void tbox_context_paint_number_steppers(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_number_input(box->node) && box->style != NULL && !box->style->visibility_hidden &&
            box->content_box.width >= 20.0 && box->content_box.height >= 14.0) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            double x = box->content_box.x + box->content_box.width - 16.0;
            double y = box->content_box.y;
            double middle = y + box->content_box.height / 2.0;
            tbox_context_push_fill(items, (tbox_rect){x, y, 16.0, box->content_box.height},
                (tbox_css_rgba){238, 240, 243, 255}, true, clip);
            tbox_context_push_fill(items, (tbox_rect){x, y, 1.0, box->content_box.height},
                (tbox_css_rgba){175, 180, 188, 255}, true, clip);
            tbox_context_push_fill(items, (tbox_rect){x, middle, 16.0, 1.0},
                (tbox_css_rgba){175, 180, 188, 255}, true, clip);
            tbox_css_rgba arrow = box->style->color;
            for (int row = 0; row < 3; row++) {
                tbox_context_push_fill(items, (tbox_rect){x + 7.0 - row,
                    y + 3.0 + row, 2.0 + 2.0 * row, 1.0}, arrow, true, clip);
                tbox_context_push_fill(items, (tbox_rect){x + 5.0 + row,
                    middle + 3.0 + row, 6.0 - 2.0 * row, 1.0}, arrow, true, clip);
            }
        }
        tbox_context_paint_number_steppers(box->first_child, items);
    }
}

void tbox_context_paint_search_clears(const tbox_layout_box *box, tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_html_attribute *value = tbox_context_is_search_input(box->node) ?
            tbox_html_node_get_attribute(box->node, tbox_string_view_make("value", 5)) : NULL;
        if (value != NULL && value->value.size > 0 && box->style != NULL && !box->style->visibility_hidden &&
            box->content_box.width >= 20.0 && box->content_box.height >= 14.0) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            double x = box->content_box.x + box->content_box.width - 16.0;
            double y = box->content_box.y + box->content_box.height / 2.0 - 5.0;
            tbox_context_push_fill(items, (tbox_rect){x, box->content_box.y, 16.0,
                box->content_box.height}, (tbox_css_rgba){245, 246, 248, 255}, true, clip);
            for (int i = 0; i < 5; i++) {
                tbox_context_push_fill(items, (tbox_rect){x + 4.0 + i, y + i, 1.5, 1.5},
                    box->style->color, true, clip);
                tbox_context_push_fill(items, (tbox_rect){x + 8.0 - i, y + i, 1.5, 1.5},
                    box->style->color, true, clip);
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

bool tbox_context_is_password_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8));
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
    if (value.size == 0 || !tbox_context_number_syntax(value, true) || value.size == SIZE_MAX) return false;
    char *copy = malloc(value.size + 1);
    if (copy == NULL) return false;
    memcpy(copy, value.data, value.size);
    copy[value.size] = '\0';
    char *end;
    double parsed = strtod(copy, &end);
    bool valid = end == copy + value.size && isfinite(parsed);
    free(copy);
    if (valid && out != NULL) *out = parsed;
    return valid;
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

static bool tbox_context_email_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
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
        while (first < last && tbox_context_email_ascii_space(text.data[first])) first++;
        while (last > first && tbox_context_email_ascii_space(text.data[last - 1])) last--;
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
        if (c == '%' && (i + 2 >= value.size || !isxdigit((unsigned char)value.data[i + 1]) ||
                         !isxdigit((unsigned char)value.data[i + 2]))) return false;
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
    double min, max, step;
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
    int length = snprintf(buffer, sizeof(buffer), "%.15g", next);
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
