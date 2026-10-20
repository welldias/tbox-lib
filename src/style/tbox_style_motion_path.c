#include <float.h>
#include <math.h>
#include <string.h>

#include <tbox/layout.h>

#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include "style/tbox_style_motion.h"

#define TBOX_MOTION_PI 3.14159265358979323846
#define TBOX_MOTION_TOLERANCE 0.125
#define TBOX_MOTION_MAX_SEGMENTS 16384

typedef struct tbox_motion_point { double x, y; } tbox_motion_point;

static tbox_motion_point tbox_motion_mid(tbox_motion_point a, tbox_motion_point b) {
    return (tbox_motion_point){ (a.x + b.x) * 0.5, (a.y + b.y) * 0.5 };
}

static tbox_motion_point tbox_motion_direction(tbox_motion_point a, tbox_motion_point b, tbox_motion_point fallback) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double length = hypot(dx, dy);
    return length > 0.0 ? (tbox_motion_point){ dx / length, dy / length } : fallback;
}

static bool tbox_motion_segment_add(tbox_vector *segments, tbox_motion_point a, tbox_motion_point b, tbox_motion_point ta, tbox_motion_point tb, bool move) {
    if (segments == NULL) return true; /* syntax validation only */
    if (segments->length >= TBOX_MOTION_MAX_SEGMENTS) return false;
    double previous = segments->length ? ((tbox_motion_segment *)segments->data)[segments->length - 1].length : 0.0;
    double length = move ? 0.0 : hypot(b.x - a.x, b.y - a.y);
    if (!isfinite(length) || !isfinite(previous + length)) return false;
    tbox_motion_segment *s = tbox_vector_push(segments);
    if (s == NULL) return false;
    *s = (tbox_motion_segment){ a.x, a.y, b.x, b.y, previous + length, ta.x, ta.y, tb.x, tb.y };
    return true;
}

static bool tbox_motion_line(tbox_vector *segments, tbox_motion_point a, tbox_motion_point b) {
    tbox_motion_point tangent = tbox_motion_direction(a, b, (tbox_motion_point){ 1.0, 0.0 });
    return tbox_motion_segment_add(segments, a, b, tangent, tangent, false);
}

static bool tbox_motion_cubic(tbox_vector *segments, tbox_motion_point a, tbox_motion_point b, tbox_motion_point c, tbox_motion_point d, unsigned depth) {
    if (segments == NULL) return true;
    double chord = hypot(d.x - a.x, d.y - a.y);
    double polygon = hypot(b.x - a.x, b.y - a.y) + hypot(c.x - b.x, c.y - b.y) + hypot(d.x - c.x, d.y - c.y);
    double cross1 = fabs((b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x));
    double cross2 = fabs((c.x - a.x) * (d.y - a.y) - (c.y - a.y) * (d.x - a.x));
    if (!isfinite(polygon)) return false;
    if (polygon - chord <= TBOX_MOTION_TOLERANCE && fmax(cross1, cross2) <= TBOX_MOTION_TOLERANCE * chord) {
        tbox_motion_point fallback = tbox_motion_direction(a, d, (tbox_motion_point){ 1.0, 0.0 });
        tbox_motion_point ta = tbox_motion_direction(a, b, tbox_motion_direction(a, c, fallback));
        tbox_motion_point tb = tbox_motion_direction(c, d, tbox_motion_direction(b, d, fallback));
        return tbox_motion_segment_add(segments, a, d, ta, tb, false);
    }
    if (depth >= 16) return false;
    tbox_motion_point ab = tbox_motion_mid(a, b), bc = tbox_motion_mid(b, c), cd = tbox_motion_mid(c, d);
    tbox_motion_point abc = tbox_motion_mid(ab, bc), bcd = tbox_motion_mid(bc, cd), mid = tbox_motion_mid(abc, bcd);
    return tbox_motion_cubic(segments, a, ab, abc, mid, depth + 1) && tbox_motion_cubic(segments, mid, bcd, cd, d, depth + 1);
}

/* SVG endpoint arc -> cubic curves (SVG 2, implementation notes F.6).
 * The cubic approximation is itself subdivided with fixed flatness. */
static bool tbox_motion_arc(tbox_vector *segments, tbox_motion_point start, double rx, double ry, double rotation, bool large, bool sweep, tbox_motion_point end) {
    if (segments == NULL) return true;
    rx = fabs(rx), ry = fabs(ry);
    if (start.x == end.x && start.y == end.y) return true;
    if (rx == 0.0 || ry == 0.0) return tbox_motion_line(segments, start, end);
    double phi = fmod(rotation, 360.0) * TBOX_MOTION_PI / 180.0, cp = cos(phi), sp = sin(phi);
    double dx = (start.x - end.x) * 0.5, dy = (start.y - end.y) * 0.5;
    double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    double ratio = hypot(xp / rx, yp / ry);
    if (ratio > 1.0) rx *= ratio, ry *= ratio;
    double ux = xp / rx, uy = yp / ry, sum = ux * ux + uy * uy;
    double factor = sum > 0.0 ? sqrt(fmax(0.0, (1.0 - sum) / sum)) : 0.0;
    if (large == sweep) factor = -factor;
    double cxp = factor * rx * uy, cyp = -factor * ry * ux;
    double cx = cp * cxp - sp * cyp + (start.x + end.x) * 0.5;
    double cy = sp * cxp + cp * cyp + (start.y + end.y) * 0.5;
    double theta = atan2((yp - cyp) / ry, (xp - cxp) / rx);
    double finish = atan2((-yp - cyp) / ry, (-xp - cxp) / rx);
    double delta = fmod(finish - theta, 2.0 * TBOX_MOTION_PI);
    if (sweep && delta < 0.0) delta += 2.0 * TBOX_MOTION_PI;
    if (!sweep && delta > 0.0) delta -= 2.0 * TBOX_MOTION_PI;
    /* Quarter arcs have <0.0003 relative error. Reduce their span for
     * large radii so conversion error also stays below 1/32px. */
    double max_span = fmin(TBOX_MOTION_PI * 0.5, pow(0.03125 * 40000.0 / fmax(rx, ry), 1.0 / 6.0));
    if (!(max_span > 0.0) || !isfinite(delta) || !isfinite(cx) || !isfinite(cy)) return false;
    double needed = ceil(fabs(delta) / max_span);
    if (needed > TBOX_MOTION_MAX_SEGMENTS) return false;
    size_t n = needed > 1.0 ? (size_t)needed : 1;
    double step = delta / (double)n;
    for (size_t i = 0; i < n; i++) {
        double t0 = theta + step * (double)i, t1 = t0 + step, k = 4.0 / 3.0 * tan(step * 0.25);
        double x0 = rx * cos(t0), y0 = ry * sin(t0), x1 = rx * cos(t1), y1 = ry * sin(t1);
        double bx = x0 - k * rx * sin(t0), by = y0 + k * ry * cos(t0);
        double cx1 = x1 + k * rx * sin(t1), cy1 = y1 - k * ry * cos(t1);
        tbox_motion_point b = { cx + cp * bx - sp * by, cy + sp * bx + cp * by };
        tbox_motion_point c = { cx + cp * cx1 - sp * cy1, cy + sp * cx1 + cp * cy1 };
        tbox_motion_point d = i + 1 == n ? end : (tbox_motion_point){ cx + cp * x1 - sp * y1, cy + sp * x1 + cp * y1 };
        if (!tbox_motion_cubic(segments, start, b, c, d, 0)) return false;
        start = d;
    }
    return true;
}

typedef struct tbox_motion_cursor { const char *at, *end; } tbox_motion_cursor;

static void tbox_motion_space(tbox_motion_cursor *c) {
    while (c->at < c->end && tbox_ascii_is_space(*c->at)) c->at++;
}

static bool tbox_motion_number(tbox_motion_cursor *c, bool comma, bool flag, double *out) {
    tbox_motion_space(c);
    if (comma && c->at < c->end && *c->at == ',') {
        c->at++;
        tbox_motion_space(c);
    }
    if (flag) {
        if (c->at == c->end || (*c->at != '0' && *c->at != '1')) return false;
        *out = *c->at++ - '0';
        return true;
    }
    const char *begin = c->at, *p = begin;
    if (p < c->end && (*p == '-' || *p == '+')) p++;
    bool digit = false;
    while (p < c->end && (*p >= '0' && *p <= '9')) digit = true, p++;
    const char *dot = NULL;
    if (p < c->end && *p == '.') {
        dot = p++;
        while (p < c->end && (*p >= '0' && *p <= '9')) digit = true, p++;
    }
    if (!digit) return false;
    const char *exponent = p;
    if (p < c->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < c->end && (*p == '+' || *p == '-')) p++;
        const char *digits = p;
        while (p < c->end && (*p >= '0' && *p <= '9')) p++;
        if (p == digits) return false;
    }
    size_t length = (size_t)(p - begin);
    char normalized[TBOX_STYLE_MAX_OFFSET_PATH_DATA];
    if (length >= sizeof(normalized)) return false;
    /* SVG allows `1.` and `1.e2`; the base CSS number scanner doesn't. */
    size_t n = 0;
    for (const char *q = begin; q < p; q++)
        if (!(q == dot && q + 1 == exponent)) normalized[n++] = *q;
    if (!tbox_number_parse(tbox_string_view_make(normalized, n), out)) return false;
    c->at = p;
    return true;
}

bool tbox_style_motion_svg(tbox_string_view data, tbox_vector *segments, bool *closed) {
    tbox_motion_cursor cursor = { data.data, data.data + data.size };
    tbox_motion_point current = { 0 }, subpath = { 0 }, control = { 0 };
    char command = 0, previous = 0;
    bool first = true;
    *closed = false;
    tbox_motion_space(&cursor);
    if (cursor.at == cursor.end) return false;
    while (cursor.at < cursor.end) {
        bool explicit_command = ((*cursor.at >= 'A' && *cursor.at <= 'Z') || (*cursor.at >= 'a' && *cursor.at <= 'z'));
        if (explicit_command) command = *cursor.at++;
        if (!command) return false;
        bool relative = command >= 'a' && command <= 'z';
        char upper = relative ? (char)(command - 'a' + 'A') : command;
        if (first && upper != 'M') return false;
        first = false;
        if (upper == 'Z') {
            if (!tbox_motion_line(segments, current, subpath)) return false;
            current = subpath;
            *closed = true;
            command = 0;
            previous = 'Z';
            tbox_motion_space(&cursor);
            continue;
        }
        size_t n;
        switch (upper) {
        case 'M': case 'L': case 'T': n = 2; break;
        case 'H': case 'V': n = 1; break;
        case 'C': n = 6; break;
        case 'S': case 'Q': n = 4; break;
        case 'A': n = 7; break;
        default: return false;
        }
        double v[7] = { 0 };
        for (size_t i = 0; i < n; i++)
            if (!tbox_motion_number(&cursor, i != 0 || !explicit_command, upper == 'A' && (i == 3 || i == 4), &v[i])) return false;
        if (upper == 'A' && (v[0] < 0.0 || v[1] < 0.0)) return false;
        double ox = relative ? current.x : 0.0, oy = relative ? current.y : 0.0;
        tbox_motion_point end = { v[n == 1 ? 0 : n - 2] + ox, v[n - 1] + oy };
        if (upper == 'H') end = (tbox_motion_point){ v[0] + ox, current.y };
        if (upper == 'V') end = (tbox_motion_point){ current.x, v[0] + oy };
        tbox_motion_point b = { v[0] + ox, v[1] + oy }, c = { v[2] + ox, v[3] + oy };
        if (!isfinite(end.x) || !isfinite(end.y) || !isfinite(b.x) || !isfinite(b.y) || !isfinite(c.x) || !isfinite(c.y)) return false;
        switch (upper) {
        case 'M':
            subpath = end;
            if (!tbox_motion_segment_add(segments, end, end, (tbox_motion_point){ 1, 0 }, (tbox_motion_point){ 1, 0 }, true)) return false;
            command = relative ? 'l' : 'L';
            break;
        case 'H': case 'V': case 'L':
            if (!tbox_motion_line(segments, current, end)) return false;
            break;
        case 'S':
            c = b;
            b = previous == 'C' || previous == 'S' ? (tbox_motion_point){ 2 * current.x - control.x, 2 * current.y - control.y } : current;
            [[fallthrough]];
        case 'C':
            if (!tbox_motion_cubic(segments, current, b, c, end, 0)) return false;
            control = c;
            break;
        case 'T': case 'Q': {
            if (upper == 'T') b = previous == 'Q' || previous == 'T' ? (tbox_motion_point){ 2 * current.x - control.x, 2 * current.y - control.y } : current;
            control = b;
            tbox_motion_point c1 = { current.x + (b.x - current.x) * 2.0 / 3.0, current.y + (b.y - current.y) * 2.0 / 3.0 };
            tbox_motion_point c2 = { end.x + (b.x - end.x) * 2.0 / 3.0, end.y + (b.y - end.y) * 2.0 / 3.0 };
            if (!tbox_motion_cubic(segments, current, c1, c2, end, 0)) return false;
            break;
        }
        case 'A':
            if (!tbox_motion_arc(segments, current, v[0], v[1], v[2], v[3] != 0, v[4] != 0, end)) return false;
            break;
        default: break;
        }
        current = end;
        previous = upper;
        *closed = false;
        tbox_motion_space(&cursor);
    }
    return true;
}

static double tbox_motion_radius(tbox_style_length radius, double center, double extent, double percent_base) {
    if (radius.kind == TBOX_STYLE_LENGTH_AUTO)
        return radius.value < 0.0 ? fmax(fabs(center), fabs(extent - center)) : fmin(fabs(center), fabs(extent - center));
    return fmax(0.0, tbox_style_length_resolve(radius, percent_base));
}

static bool tbox_motion_shape(tbox_vector *segments, const tbox_style_clip_path *shape, double width, double height, tbox_motion_point center) {
    if (shape->kind == TBOX_STYLE_CLIP_PATH_POLYGON) {
        for (size_t i = 0; i < shape->point_count; i++) {
            size_t j = (i + 1) % shape->point_count;
            tbox_motion_point a = { tbox_style_length_resolve(shape->points[i][0], width), tbox_style_length_resolve(shape->points[i][1], height) };
            tbox_motion_point b = { tbox_style_length_resolve(shape->points[j][0], width), tbox_style_length_resolve(shape->points[j][1], height) };
            if (!tbox_motion_line(segments, a, b)) return false;
        }
        return true;
    }
    if (shape->kind == TBOX_STYLE_CLIP_PATH_CIRCLE || shape->kind == TBOX_STYLE_CLIP_PATH_ELLIPSE) {
        double rx, ry;
        if (shape->kind == TBOX_STYLE_CLIP_PATH_CIRCLE) {
            double extent_x = tbox_motion_radius(shape->radius[0], center.x, width, hypot(width, height) / sqrt(2.0));
            double extent_y = tbox_motion_radius(shape->radius[0], center.y, height, hypot(width, height) / sqrt(2.0));
            rx = ry = shape->radius[0].kind == TBOX_STYLE_LENGTH_AUTO && shape->radius[0].value >= 0.0 ? fmin(extent_x, extent_y) : fmax(extent_x, extent_y);
        } else {
            rx = tbox_motion_radius(shape->radius[0], center.x, width, width);
            ry = tbox_motion_radius(shape->radius[1], center.y, height, height);
        }
        for (size_t i = 0; i < 4; i++) {
            double a = (double)i * TBOX_MOTION_PI * 0.5, b = a + TBOX_MOTION_PI * 0.5;
            tbox_motion_point start = { center.x + rx * cos(a), center.y + ry * sin(a) };
            tbox_motion_point end = { center.x + rx * cos(b), center.y + ry * sin(b) };
            if (rx == 0.0 || ry == 0.0) {
                if (!tbox_motion_line(segments, start, end)) return false;
            } else if (!tbox_motion_arc(segments, start, rx, ry, 0.0, false, true, end)) return false;
        }
        return true;
    }
    double x0 = tbox_style_length_resolve(shape->inset[3], width), x1 = width - tbox_style_length_resolve(shape->inset[1], width);
    double y0 = tbox_style_length_resolve(shape->inset[0], height), y1 = height - tbox_style_length_resolve(shape->inset[2], height);
    /* Crossed insets collapse at the midpoint, as CSS Shapes specifies. */
    if (x1 < x0) x0 = x1 = (x0 + x1) * 0.5;
    if (y1 < y0) y0 = y1 = (y0 + y1) * 0.5;
    double rx[4], ry[4], scale = 1.0;
    for (size_t i = 0; i < 4; i++) {
        rx[i] = fmax(0.0, shape->round_h[i] + width * shape->round_h_percent[i] / 100.0);
        ry[i] = fmax(0.0, shape->round_v[i] + height * shape->round_v_percent[i] / 100.0);
        if (rx[i] == 0.0 || ry[i] == 0.0) rx[i] = ry[i] = 0.0;
    }
    for (size_t i = 0; i < 4; i++) {
        double sum = i % 2 == 0 ? rx[i] + rx[(i + 1) % 4] : ry[i] + ry[(i + 1) % 4];
        double extent = i % 2 == 0 ? x1 - x0 : y1 - y0;
        if (sum > 0.0) scale = fmin(scale, extent / sum);
    }
    for (size_t i = 0; i < 4; i++) rx[i] *= scale, ry[i] *= scale;
    tbox_motion_point starts[4] = { { x0 + rx[0], y0 }, { x1, y0 + ry[1] }, { x1 - rx[2], y1 }, { x0, y1 - ry[3] } };
    tbox_motion_point ends[4] = { { x1 - rx[1], y0 }, { x1, y1 - ry[2] }, { x0 + rx[3], y1 }, { x0, y0 + ry[0] } };
    for (size_t i = 0; i < 4; i++) {
        size_t corner = (i + 1) % 4;
        if (!tbox_motion_line(segments, starts[i], ends[i]) || !tbox_motion_arc(segments, ends[i], rx[corner], ry[corner], 0.0, false, true, starts[corner])) return false;
    }
    return true;
}

static tbox_motion_point tbox_motion_origin(const tbox_style *style, tbox_rect box, tbox_rect reference) {
    const tbox_style_offset_position *position = &style->offset_position;
    if (position->mode == 1) return (tbox_motion_point){ box.x - reference.x, box.y - reference.y };
    if (position->mode == 2) return (tbox_motion_point){ reference.width * 0.5, reference.height * 0.5 };
    return (tbox_motion_point){ tbox_style_length_resolve(position->position[0], reference.width), tbox_style_length_resolve(position->position[1], reference.height) };
}

static double tbox_motion_ray_length(const tbox_style_offset_path *path, tbox_motion_point origin, tbox_rect reference, tbox_motion_point direction) {
    const double sides[4] = { fabs(origin.x), fabs(origin.y), fabs(reference.width - origin.x), fabs(reference.height - origin.y) };
    double near = DBL_MAX, far = 0.0;
    for (size_t i = 0; i < 4; i++) {
        double distance = sides[i];
        if (path->value.ray.size == 1 || path->value.ray.size == 3)
            distance = hypot(i % 2 ? reference.width - origin.x : origin.x, i < 2 ? origin.y : reference.height - origin.y);
        near = fmin(near, distance), far = fmax(far, distance);
    }
    if (path->value.ray.size < 4) return path->value.ray.size < 2 ? near : far;
    if (origin.x <= 0 || origin.y <= 0 || origin.x >= reference.width || origin.y >= reference.height) return 0.0;
    double tx = direction.x > 0 ? (reference.width - origin.x) / direction.x : direction.x < 0 ? -origin.x / direction.x : DBL_MAX;
    double ty = direction.y > 0 ? (reference.height - origin.y) / direction.y : direction.y < 0 ? -origin.y / direction.y : DBL_MAX;
    return fmin(tx, ty);
}

bool tbox_style_motion_matrix(const tbox_style *style, tbox_rect box, tbox_rect reference, const tbox_style_clip_path *outline, tbox_arena *arena, double out[6]) {
    const double identity[6] = { 1, 0, 0, 1, 0, 0 };
    memcpy(out, identity, sizeof(identity));
    if (style == NULL || tbox_style_offset_path_of(style)->kind == TBOX_STYLE_OFFSET_PATH_NONE) return false;
    const tbox_style_offset_path *path = tbox_style_offset_path_of(style);
    tbox_motion_point point = { 0 }, tangent, origin = tbox_motion_origin(style, box, reference);
    if (path->kind == TBOX_STYLE_OFFSET_PATH_RAY) {
        double angle = fmod(path->value.ray.angle, 360.0) * TBOX_MOTION_PI / 180.0;
        tangent = (tbox_motion_point){ sin(angle), -cos(angle) };
        if (fmod(path->value.ray.angle, 90.0) == 0.0) tangent.x = round(tangent.x), tangent.y = round(tangent.y);
        if (path->has_position) origin = (tbox_motion_point){ tbox_style_length_resolve(path->value.ray.position[0], reference.width), tbox_style_length_resolve(path->value.ray.position[1], reference.height) };
        double length = tbox_motion_ray_length(path, origin, reference, tangent);
        if (path->value.ray.contain) length = fmax(0.0, length - fmax(box.width, box.height) * 0.5);
        double distance = tbox_style_length_resolve(style->offset_distance, length);
        if (path->value.ray.contain) distance = fmax(0.0, fmin(length, distance));
        point = (tbox_motion_point){ origin.x + distance * tangent.x, origin.y + distance * tangent.y };
    } else {
        tbox_vector segments;
        if (arena == NULL) return false;
        tbox_vector_init(&segments, arena, sizeof(tbox_motion_segment), 32);
        bool closed = true;
        if (path->kind == TBOX_STYLE_OFFSET_PATH_SVG) {
            if (!tbox_style_motion_svg(tbox_string_view_from_cstr(path->value.data), &segments, &closed)) return false;
        } else {
            tbox_style_clip_path shape = path->kind == TBOX_STYLE_OFFSET_PATH_BOX ?
                (outline != NULL ? *outline : (tbox_style_clip_path){ .kind = TBOX_STYLE_CLIP_PATH_INSET }) : path->value.shape;
            if (path->has_position) origin = (tbox_motion_point){ tbox_style_length_resolve(shape.center[0], reference.width), tbox_style_length_resolve(shape.center[1], reference.height) };
            if (!tbox_motion_shape(&segments, &shape, reference.width, reference.height, origin)) return false;
        }
        tbox_motion_segment *s = segments.data;
        if (!segments.length) return false;
        double length = s[segments.length - 1].length;
        double distance = tbox_style_length_resolve(style->offset_distance, length);
        if (closed && length > 0.0) {
            distance = fmod(distance, length);
            if (distance < 0.0) distance += length;
        } else {
            distance = fmax(0.0, fmin(length, distance));
        }
        size_t i = 0;
        double previous = 0.0;
        for (; i + 1 < segments.length; i++) {
            if (s[i].length > previous && s[i].length >= distance) break;
            previous = s[i].length;
        }
        double span = s[i].length - previous;
        double t = span > 0.0 ? (distance - previous) / span : 0.0;
        point = (tbox_motion_point){ s[i].x0 + (s[i].x1 - s[i].x0) * t, s[i].y0 + (s[i].y1 - s[i].y0) * t };
        tangent = (tbox_motion_point){ s[i].tx0 + (s[i].tx1 - s[i].tx0) * t, s[i].ty0 + (s[i].ty1 - s[i].ty0) * t };
    }
    point.x += reference.x, point.y += reference.y;
    double ox = box.x + tbox_style_length_resolve(style->transform_origin[0], box.width);
    double oy = box.y + tbox_style_length_resolve(style->transform_origin[1], box.height);
    double ax = ox, ay = oy;
    if (style->offset_anchor.mode == 0) {
        ax = box.x + tbox_style_length_resolve(style->offset_anchor.position[0], box.width);
        ay = box.y + tbox_style_length_resolve(style->offset_anchor.position[1], box.height);
    }
    double angle = style->offset_rotate.angle;
    if (style->offset_rotate.auto_rotate) angle += atan2(tangent.y, tangent.x) * 180.0 / TBOX_MOTION_PI;
    double radians = fmod(angle, 360.0) * TBOX_MOTION_PI / 180.0;
    double c = cos(radians), s = sin(radians);
    if (fmod(angle, 90.0) == 0.0) c = round(c), s = round(s);
    out[0] = c, out[1] = s, out[2] = -s, out[3] = c;
    out[4] = point.x - ox + c * (ox - ax) - s * (oy - ay);
    out[5] = point.y - oy + s * (ox - ax) + c * (oy - ay);
    for (size_t i = 0; i < 6; i++) {
        if (!isfinite(out[i])) {
            memcpy(out, identity, sizeof(identity));
            return false;
        }
    }
    return true;
}
