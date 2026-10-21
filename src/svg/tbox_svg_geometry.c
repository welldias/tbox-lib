#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "svg/tbox_svg_internal.h"
#include <math.h>
#include <string.h>

bool tbox_svg_tag(const tbox_html_node *node, const char *tag) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, tag);
}
tbox_string_view tbox_svg_attr(const tbox_html_node *node, const char *name) {
    const tbox_html_attribute *a = node != NULL ? tbox_html_node_get_attribute(node, tbox_string_view_from_cstr(name)) : NULL;
    return a != NULL ? a->value : (tbox_string_view){ 0 };
}
size_t tbox_svg_numbers(tbox_string_view text, double *out, size_t capacity) {
    size_t at = 0, count = 0;
    while (at < text.size) {
        while (at < text.size && tbox_ascii_is_space(text.data[at]))
            at++;
        if (at == text.size)
            break;
        if (count == capacity)
            return SIZE_MAX;
        size_t n = tbox_number_scan(tbox_string_view_make(text.data + at, text.size - at), &out[count]);
        if (n == 0 || !isfinite(out[count]))
            return SIZE_MAX;
        at += n;
        count++;
        while (at < text.size && tbox_ascii_is_space(text.data[at]))
            at++;
        if (at < text.size && text.data[at] == ',') {
            at++;
            while (at < text.size && tbox_ascii_is_space(text.data[at]))
                at++;
            if (at == text.size)
                return SIZE_MAX;
        }
    }
    return count;
}
void tbox_svg_matrix_multiply(double a[6], const double b[6]) {
    double r[6] = { a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3], a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5] };
    memcpy(a, r, sizeof(r));
}
bool tbox_svg_parse_transform(tbox_string_view text, double out[6]) {
    double m[6] = { 1, 0, 0, 1, 0, 0 };
    size_t at = 0, functions = 0;
    while (at < text.size) {
        while (at < text.size && tbox_ascii_is_space(text.data[at]))
            at++;
        if (at == text.size)
            break;
        size_t begin = at;
        while (at < text.size && ((text.data[at] >= 'a' && text.data[at] <= 'z') || (text.data[at] >= 'A' && text.data[at] <= 'Z')))
            at++;
        tbox_string_view name = tbox_string_view_make(text.data + begin, at - begin);
        while (at < text.size && tbox_ascii_is_space(text.data[at]))
            at++;
        if (at == text.size || text.data[at++] != '(' || ++functions > 32)
            return false;
        begin = at;
        while (at < text.size && text.data[at] != ')')
            at++;
        if (at == text.size)
            return false;
        double v[6] = { 0 }, n[6] = { 1, 0, 0, 1, 0, 0 };
        size_t count = tbox_svg_numbers(tbox_string_view_make(text.data + begin, at - begin), v, 6);
        at++;
        if (tbox_style_is(name, "matrix") && count == 6)
            memcpy(n, v, sizeof(n));
        else if (tbox_style_is(name, "translate") && (count == 1 || count == 2))
            n[4] = v[0], n[5] = v[1];
        else if (tbox_style_is(name, "scale") && (count == 1 || count == 2))
            n[0] = v[0], n[3] = count == 1 ? v[0] : v[1];
        else if (tbox_style_is(name, "rotate") && (count == 1 || count == 3)) {
            double angle = v[0] * 0.017453292519943295;
            n[0] = n[3] = cos(angle);
            n[1]        = sin(angle);
            n[2]        = -n[1];
            if (count == 3)
                n[4] = v[1] - n[0] * v[1] - n[2] * v[2], n[5] = v[2] - n[1] * v[1] - n[3] * v[2];
        } else if ((tbox_style_is(name, "skewx") || tbox_style_is(name, "skewy")) && count == 1) {
            double tangent = tan(v[0] * 0.017453292519943295);
            if (!isfinite(tangent))
                return false;
            n[tbox_style_is(name, "skewx") ? 2 : 1] = tangent;
        } else
            return false;
        tbox_svg_matrix_multiply(m, n);
        for (size_t i = 0; i < 6; i++)
            if (!isfinite(m[i]))
                return false;
        while (at < text.size && tbox_ascii_is_space(text.data[at]))
            at++;
        if (at < text.size && text.data[at] == ',') {
            at++;
            while (at < text.size && tbox_ascii_is_space(text.data[at]))
                at++;
            if (at == text.size)
                return false;
        }
    }
    memcpy(out, m, sizeof(m));
    return functions > 0;
}
bool tbox_svg_viewbox(const tbox_html_node *node, tbox_rect *out) {
    double v[4];
    if (tbox_svg_numbers(tbox_svg_attr(node, "viewbox"), v, 4) != 4 || v[2] < 0 || v[3] < 0)
        return false;
    *out = (tbox_rect){ v[0], v[1], v[2], v[3] };
    return true;
}
void tbox_svg_view_matrix(const tbox_html_node *node, tbox_rect viewport, double out[6]) {
    double m[6] = { 1, 0, 0, 1, viewport.x, viewport.y };
    tbox_rect vb;
    if (tbox_svg_viewbox(node, &vb)) {
        if (vb.width <= 0 || vb.height <= 0) {
            m[0] = m[3] = 0;
        } else {
            double sx = viewport.width / vb.width, sy = viewport.height / vb.height;
            tbox_string_view parts[3];
            size_t n  = tbox_style_tokens(tbox_svg_attr(node, "preserveaspectratio"), parts, 3);
            bool none = n > 0 && tbox_style_is(parts[0], "none");
            double ax = .5, ay = .5;
            if (n > 0 && parts[0].size == 8) {
                if (memcmp(parts[0].data, "xMin", 4) == 0)
                    ax = 0;
                if (memcmp(parts[0].data, "xMax", 4) == 0)
                    ax = 1;
                if (memcmp(parts[0].data + 4, "YMin", 4) == 0)
                    ay = 0;
                if (memcmp(parts[0].data + 4, "YMax", 4) == 0)
                    ay = 1;
            }
            if (!none)
                sx = sy = n > 1 && tbox_style_is(parts[1], "slice") ? fmax(sx, sy) : fmin(sx, sy);
            m[0] = sx;
            m[3] = sy;
            m[4] += (viewport.width - vb.width * sx) * ax - vb.x * sx;
            m[5] += (viewport.height - vb.height * sy) * ay - vb.y * sy;
        }
    }
    memcpy(out, m, sizeof(m));
}

static double tbox_svg_coordinate(const tbox_html_node *node, const char *name, double base, double font_size, double fallback) {
    tbox_string_view value = tbox_svg_attr(node, name);
    if (value.size == 0)
        return fallback;
    const tbox_style_units units = { .font_size = font_size, .root_font_size = font_size, .zoom = 1 };
    tbox_style_length length;
    double number;
    if (tbox_number_parse(value, &number))
        return number;
    if (tbox_style_parse_spacing_length(value, &units, &length) && length.kind != TBOX_STYLE_LENGTH_AUTO)
        return tbox_style_length_resolve(length, base);
    return fallback;
}
static bool tbox_svg_move(tbox_vector *segments, tbox_path_point p) {
    return tbox_path_segment_add(segments, p, p, (tbox_path_point){ 1, 0 }, (tbox_path_point){ 1, 0 }, true);
}
static bool tbox_svg_close(tbox_vector *segments, tbox_path_point a, tbox_path_point b) {
    if (!tbox_path_line(segments, a, b))
        return false;
    ((tbox_path_segment *)segments->data)[segments->length - 1].close = true;
    return true;
}
bool tbox_svg_shape(const tbox_html_node *node, tbox_vector *segments, double tolerance, double vw, double vh, double font_size) {
    if (tbox_svg_tag(node, "path")) {
        bool closed;
        tbox_string_view d = tbox_svg_attr(node, "d");
        return d.size > 0 && tbox_path_parse(d, segments, &closed, tolerance);
    }
    double x = tbox_svg_coordinate(node, "x", vw, font_size, 0), y = tbox_svg_coordinate(node, "y", vh, font_size, 0);
    if (tbox_svg_tag(node, "circle") || tbox_svg_tag(node, "ellipse")) {
        double cx = tbox_svg_coordinate(node, "cx", vw, font_size, 0), cy = tbox_svg_coordinate(node, "cy", vh, font_size, 0);
        double rx = tbox_svg_tag(node, "circle") ? tbox_svg_coordinate(node, "r", hypot(vw, vh) / sqrt(2), font_size, 0) : tbox_svg_coordinate(node, "rx", vw, font_size, 0);
        double ry = tbox_svg_tag(node, "circle") ? rx : tbox_svg_coordinate(node, "ry", vh, font_size, 0);
        if (!(rx > 0) || !(ry > 0))
            return false;
        tbox_path_point start = { cx + rx, cy }, mid = { cx - rx, cy };
        if (!tbox_svg_move(segments, start) || !tbox_path_arc(segments, start, rx, ry, 0, false, true, mid, tolerance) || !tbox_path_arc(segments, mid, rx, ry, 0, false, true, start, tolerance))
            return false;
        return tbox_svg_close(segments, start, start);
    }
    if (tbox_svg_tag(node, "rect")) {
        double w = tbox_svg_coordinate(node, "width", vw, font_size, 0), h = tbox_svg_coordinate(node, "height", vh, font_size, 0);
        if (!(w > 0) || !(h > 0))
            return false;
        double rx = tbox_svg_coordinate(node, "rx", vw, font_size, -1), ry = tbox_svg_coordinate(node, "ry", vh, font_size, -1);
        if (rx < 0)
            rx = fmax(0, ry);
        if (ry < 0)
            ry = rx;
        rx = fmin(rx, w * .5);
        ry = fmin(ry, h * .5);
        if (rx == 0 || ry == 0)
            rx = ry = 0;
        tbox_path_point starts[4] = {
            { x + rx,     y          },
            { x + w,      y + ry     },
            { x + w - rx, y + h      },
            { x,          y + h - ry }
        };
        tbox_path_point ends[4] = {
            { x + w - rx, y          },
            { x + w,      y + h - ry },
            { x + rx,     y + h      },
            { x,          y + ry     }
        };
        if (!tbox_svg_move(segments, starts[0]))
            return false;
        for (size_t i = 0; i < 4; i++) {
            size_t j = (i + 1) % 4;
            if (!tbox_path_line(segments, starts[i], ends[i]) || !tbox_path_arc(segments, ends[i], rx, ry, 0, false, true, starts[j], tolerance))
                return false;
        }
        return tbox_svg_close(segments, starts[0], starts[0]);
    }
    if (tbox_svg_tag(node, "line")) {
        tbox_path_point a = { tbox_svg_coordinate(node, "x1", vw, font_size, 0), tbox_svg_coordinate(node, "y1", vh, font_size, 0) };
        tbox_path_point b = { tbox_svg_coordinate(node, "x2", vw, font_size, 0), tbox_svg_coordinate(node, "y2", vh, font_size, 0) };
        return tbox_svg_move(segments, a) && tbox_path_line(segments, a, b);
    }
    if (tbox_svg_tag(node, "polyline") || tbox_svg_tag(node, "polygon")) {
        double v[2048];
        size_t count = tbox_svg_numbers(tbox_svg_attr(node, "points"), v, 2048);
        if (count == SIZE_MAX || count < 4 || count % 2)
            return false;
        tbox_path_point a = { v[0], v[1] }, first = a;
        if (!tbox_svg_move(segments, a))
            return false;
        for (size_t i = 2; i < count; i += 2) {
            tbox_path_point b = { v[i], v[i + 1] };
            if (!tbox_path_line(segments, a, b))
                return false;
            a = b;
        }
        return !tbox_svg_tag(node, "polygon") || tbox_svg_close(segments, a, first);
    }
    return false;
}
