#include "tbox_context_internal.h"

static const tbox_html_node *tbox_context_named_map(const tbox_html_node *node, tbox_string_view name) {
    if (node == NULL) return NULL;
    if (node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "map")) {
        const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_from_cstr("name"));
        if (attribute != NULL && tbox_string_view_equal(attribute->value, name)) return node;
    }
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        const tbox_html_node *found = tbox_context_named_map(child, name);
        if (found != NULL) return found;
    }
    return NULL;
}

static size_t tbox_context_area_coords(tbox_string_view text, double *values, size_t capacity) {
    size_t count = 0, at = 0;
    while (at < text.size && count < capacity) {
        while (at < text.size && (text.data[at] == ',' || tbox_ascii_is_space(text.data[at]))) at++;
        if (at == text.size) break;
        double value;
        size_t used = tbox_number_scan(tbox_string_view_make(text.data + at, text.size - at), &value);
        if (used == 0 || !isfinite(value)) return 0;
        values[count++] = value;
        at += used;
        if (at < text.size && text.data[at] != ',' && !tbox_ascii_is_space(text.data[at])) return 0;
    }
    return count;
}

static bool tbox_context_area_contains(const tbox_html_node *area, double x, double y) {
    const tbox_html_attribute *shape = tbox_html_node_get_attribute(area, tbox_string_view_from_cstr("shape"));
    if (shape != NULL && tbox_string_view_equal_ascii_ci(shape->value, tbox_string_view_from_cstr("default"))) return true;
    const tbox_html_attribute *coords = tbox_html_node_get_attribute(area, tbox_string_view_from_cstr("coords"));
    if (coords == NULL) return false;
    double v[128];
    size_t count = tbox_context_area_coords(coords->value, v, 128);
    if (shape == NULL || tbox_string_view_equal_ascii_ci(shape->value, tbox_string_view_from_cstr("rect")))
        return count == 4 && x >= fmin(v[0], v[2]) && x <= fmax(v[0], v[2]) &&
            y >= fmin(v[1], v[3]) && y <= fmax(v[1], v[3]);
    if (tbox_string_view_equal_ascii_ci(shape->value, tbox_string_view_from_cstr("circle")))
        return count == 3 && v[2] >= 0.0 && (x - v[0]) * (x - v[0]) + (y - v[1]) * (y - v[1]) <= v[2] * v[2];
    if (tbox_string_view_equal_ascii_ci(shape->value, tbox_string_view_from_cstr("poly")) && count >= 6 && count % 2 == 0) {
        bool inside = false;
        size_t vertices = count / 2;
        for (size_t i = 0, j = vertices - 1; i < vertices; j = i++) {
            double xi = v[2 * i], yi = v[2 * i + 1];
            double xj = v[2 * j], yj = v[2 * j + 1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
        }
        return inside;
    }
    return false;
}

const tbox_html_node *tbox_context_run_element(const tbox_context *ctx, const tbox_layout_box *box, const tbox_layout_text_run *run) {
    const tbox_layout_box *scope = box;
    while (scope != NULL && scope->node == NULL)
        scope = scope->parent;
    if (scope != NULL && run->style == scope->style)
        return scope->node;
    for (size_t i = 0; i < ctx->styles.count; i++) {
        if (ctx->styles.items[i].style != run->style)
            continue;
        const tbox_html_node *node = ctx->styles.items[i].node;
        for (const tbox_html_node *up = node; up != NULL; up = up->parent)
            if (scope == NULL || up == scope->node)
                return node;
    }
    return NULL;
}

const tbox_html_node *tbox_context_image_area_at(const tbox_context *ctx, const tbox_layout_box *box, double x, double y) {
    if (ctx == NULL || box == NULL || !tbox_context_box_point(box, &x, &y)) return NULL;
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->image == NULL || run->image->width <= 0 || run->image->height <= 0 ||
            !tbox_context_point_in_rect(run->rect, x, y)) continue;
        const tbox_html_node *img = NULL;
        if (box->node != NULL && box->node->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_string_view_equal_cstr(box->node->element.tag_name, "img")) img = box->node;
        else {
            const tbox_html_node *element = tbox_context_run_element(ctx, box, run);
            if (element != NULL && element->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(element->element.tag_name, "img"))
                img = element;
        }
        if (img == NULL) continue;
        const tbox_html_attribute *usemap = tbox_html_node_get_attribute(img, tbox_string_view_from_cstr("usemap"));
        if (usemap == NULL || usemap->value.size < 2 || usemap->value.data[0] != '#') continue;
        const tbox_html_node *map = tbox_context_named_map(tbox_html_document_root(ctx->document),
            tbox_string_view_make(usemap->value.data + 1, usemap->value.size - 1));
        if (map == NULL) continue;
        double image_x = (x - run->rect.x) * run->image->width / run->rect.width;
        double image_y = (y - run->rect.y) * run->image->height / run->rect.height;
        for (const tbox_html_node *area = map->first_child; area != NULL; area = area->next_sibling)
            if (area->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(area->element.tag_name, "area") &&
                tbox_html_node_get_attribute(area, tbox_string_view_from_cstr("href")) != NULL &&
                tbox_context_area_contains(area, image_x, image_y)) return area;
    }
    return NULL;
}
