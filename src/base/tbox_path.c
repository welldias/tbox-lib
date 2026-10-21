#include "base/tbox_path.h"
#include "base/tbox_number.h"
#include "base/tbox_string.h"
#include <math.h>

#define TBOX_PATH_PI 3.14159265358979323846
#define TBOX_PATH_MAX_SEGMENTS 16384

static tbox_path_point tbox_path_mid(tbox_path_point a, tbox_path_point b) {
    return (tbox_path_point){ (a.x + b.x) * 0.5, (a.y + b.y) * 0.5 };
}

static tbox_path_point tbox_path_direction(tbox_path_point a, tbox_path_point b, tbox_path_point fallback) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double length = hypot(dx, dy);
    return length > 0.0 ? (tbox_path_point){ dx / length, dy / length } : fallback;
}

bool tbox_path_segment_add(tbox_vector *segments, tbox_path_point a, tbox_path_point b, tbox_path_point ta, tbox_path_point tb, bool move) {
    if (segments == NULL)
        return true; /* syntax validation only */
    if (segments->length >= TBOX_PATH_MAX_SEGMENTS)
        return false;
    double previous = segments->length ? ((tbox_path_segment *)segments->data)[segments->length - 1].length : 0.0;
    double length   = move ? 0.0 : hypot(b.x - a.x, b.y - a.y);
    if (!isfinite(length) || !isfinite(previous + length))
        return false;
    tbox_path_segment *s = tbox_vector_push(segments);
    if (s == NULL)
        return false;
    *s = (tbox_path_segment){ a.x, a.y, b.x, b.y, previous + length, ta.x, ta.y, tb.x, tb.y, move, false };
    return true;
}

bool tbox_path_line(tbox_vector *segments, tbox_path_point a, tbox_path_point b) {
    tbox_path_point tangent = tbox_path_direction(a, b, (tbox_path_point){ 1.0, 0.0 });
    return tbox_path_segment_add(segments, a, b, tangent, tangent, false);
}

bool tbox_path_cubic(tbox_vector *segments, tbox_path_point a, tbox_path_point b, tbox_path_point c, tbox_path_point d, unsigned depth, double tolerance) {
    if (segments == NULL)
        return true;
    double chord   = hypot(d.x - a.x, d.y - a.y);
    double polygon = hypot(b.x - a.x, b.y - a.y) + hypot(c.x - b.x, c.y - b.y) + hypot(d.x - c.x, d.y - c.y);
    double cross1  = fabs((b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x));
    double cross2  = fabs((c.x - a.x) * (d.y - a.y) - (c.y - a.y) * (d.x - a.x));
    if (!isfinite(polygon))
        return false;
    if (polygon - chord <= tolerance && fmax(cross1, cross2) <= tolerance * chord) {
        tbox_path_point fallback = tbox_path_direction(a, d, (tbox_path_point){ 1.0, 0.0 });
        tbox_path_point ta       = tbox_path_direction(a, b, tbox_path_direction(a, c, fallback));
        tbox_path_point tb       = tbox_path_direction(c, d, tbox_path_direction(b, d, fallback));
        return tbox_path_segment_add(segments, a, d, ta, tb, false);
    }
    if (depth >= 16)
        return false;
    tbox_path_point ab = tbox_path_mid(a, b), bc = tbox_path_mid(b, c), cd = tbox_path_mid(c, d);
    tbox_path_point abc = tbox_path_mid(ab, bc), bcd = tbox_path_mid(bc, cd), mid = tbox_path_mid(abc, bcd);
    return tbox_path_cubic(segments, a, ab, abc, mid, depth + 1, tolerance) && tbox_path_cubic(segments, mid, bcd, cd, d, depth + 1, tolerance);
}

/* SVG endpoint arc -> cubic curves (SVG 2, implementation notes F.6).
 * The cubic approximation is itself subdivided with fixed flatness. */
bool tbox_path_arc(tbox_vector *segments, tbox_path_point start, double rx, double ry, double rotation, bool large, bool sweep, tbox_path_point end, double tolerance) {
    if (segments == NULL)
        return true;
    rx = fabs(rx), ry = fabs(ry);
    if (start.x == end.x && start.y == end.y)
        return true;
    if (rx == 0.0 || ry == 0.0)
        return tbox_path_line(segments, start, end);
    double phi = fmod(rotation, 360.0) * TBOX_PATH_PI / 180.0, cp = cos(phi), sp = sin(phi);
    double dx = (start.x - end.x) * 0.5, dy = (start.y - end.y) * 0.5;
    double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
    double ratio = hypot(xp / rx, yp / ry);
    if (ratio > 1.0)
        rx *= ratio, ry *= ratio;
    double ux = xp / rx, uy = yp / ry, sum = ux * ux + uy * uy;
    double factor = sum > 0.0 ? sqrt(fmax(0.0, (1.0 - sum) / sum)) : 0.0;
    if (large == sweep)
        factor = -factor;
    double cxp = factor * rx * uy, cyp = -factor * ry * ux;
    double cx     = cp * cxp - sp * cyp + (start.x + end.x) * 0.5;
    double cy     = sp * cxp + cp * cyp + (start.y + end.y) * 0.5;
    double theta  = atan2((yp - cyp) / ry, (xp - cxp) / rx);
    double finish = atan2((-yp - cyp) / ry, (-xp - cxp) / rx);
    double delta  = fmod(finish - theta, 2.0 * TBOX_PATH_PI);
    if (sweep && delta < 0.0)
        delta += 2.0 * TBOX_PATH_PI;
    if (!sweep && delta > 0.0)
        delta -= 2.0 * TBOX_PATH_PI;
    /* Quarter arcs have <0.0003 relative error. Reduce their span for
     * large radii so conversion error also stays below 1/32px. */
    double max_span = fmin(TBOX_PATH_PI * 0.5, pow(0.03125 * 40000.0 / fmax(rx, ry), 1.0 / 6.0));
    if (!(max_span > 0.0) || !isfinite(delta) || !isfinite(cx) || !isfinite(cy))
        return false;
    double needed = ceil(fabs(delta) / max_span);
    if (needed > TBOX_PATH_MAX_SEGMENTS)
        return false;
    size_t n    = needed > 1.0 ? (size_t)needed : 1;
    double step = delta / (double)n;
    for (size_t i = 0; i < n; i++) {
        double t0 = theta + step * (double)i, t1 = t0 + step, k = 4.0 / 3.0 * tan(step * 0.25);
        double x0 = rx * cos(t0), y0 = ry * sin(t0), x1 = rx * cos(t1), y1 = ry * sin(t1);
        double bx = x0 - k * rx * sin(t0), by = y0 + k * ry * cos(t0);
        double cx1 = x1 + k * rx * sin(t1), cy1 = y1 - k * ry * cos(t1);
        tbox_path_point b = { cx + cp * bx - sp * by, cy + sp * bx + cp * by };
        tbox_path_point c = { cx + cp * cx1 - sp * cy1, cy + sp * cx1 + cp * cy1 };
        tbox_path_point d = i + 1 == n ? end : (tbox_path_point){ cx + cp * x1 - sp * y1, cy + sp * x1 + cp * y1 };
        if (!tbox_path_cubic(segments, start, b, c, d, 0, tolerance))
            return false;
        start = d;
    }
    return true;
}

typedef struct tbox_path_cursor {
    const char *at, *end;
} tbox_path_cursor;

static void tbox_path_space(tbox_path_cursor *c) {
    while (c->at < c->end && tbox_ascii_is_space(*c->at))
        c->at++;
}

static bool tbox_path_number(tbox_path_cursor *c, bool comma, bool flag, double *out) {
    tbox_path_space(c);
    if (comma && c->at < c->end && *c->at == ',') {
        c->at++;
        tbox_path_space(c);
    }
    if (flag) {
        if (c->at == c->end || (*c->at != '0' && *c->at != '1'))
            return false;
        *out = *c->at++ - '0';
        return true;
    }
    const char *begin = c->at, *p = begin;
    if (p < c->end && (*p == '-' || *p == '+'))
        p++;
    bool digit = false;
    while (p < c->end && (*p >= '0' && *p <= '9'))
        digit = true, p++;
    const char *dot = NULL;
    if (p < c->end && *p == '.') {
        dot = p++;
        while (p < c->end && (*p >= '0' && *p <= '9'))
            digit = true, p++;
    }
    if (!digit)
        return false;
    const char *exponent = p;
    if (p < c->end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < c->end && (*p == '+' || *p == '-'))
            p++;
        const char *digits = p;
        while (p < c->end && (*p >= '0' && *p <= '9'))
            p++;
        if (p == digits)
            return false;
    }
    size_t length = (size_t)(p - begin);
    char normalized[4096];
    if (length >= sizeof(normalized))
        return false;
    /* SVG allows `1.` and `1.e2`; the base CSS number scanner doesn't. */
    size_t n = 0;
    for (const char *q = begin; q < p; q++)
        if (!(q == dot && q + 1 == exponent))
            normalized[n++] = *q;
    if (!tbox_number_parse(tbox_string_view_make(normalized, n), out))
        return false;
    c->at = p;
    return true;
}

bool tbox_path_parse(tbox_string_view data, tbox_vector *segments, bool *closed, double tolerance) {
    if (closed == NULL || data.data == NULL || data.size == 0 || !isfinite(tolerance) || tolerance <= 0)
        return false;
    tbox_path_cursor cursor = { data.data, data.data + data.size };
    tbox_path_point current = { 0 }, subpath = { 0 }, control = { 0 };
    char command = 0, previous = 0;
    bool first = true;
    *closed    = false;
    tbox_path_space(&cursor);
    if (cursor.at == cursor.end)
        return false;
    while (cursor.at < cursor.end) {
        bool explicit_command = ((*cursor.at >= 'A' && *cursor.at <= 'Z') || (*cursor.at >= 'a' && *cursor.at <= 'z'));
        if (explicit_command)
            command = *cursor.at++;
        if (!command)
            return false;
        bool relative = command >= 'a' && command <= 'z';
        char upper    = relative ? (char)(command - 'a' + 'A') : command;
        if (first && upper != 'M')
            return false;
        first = false;
        if (upper == 'Z') {
            if (!tbox_path_line(segments, current, subpath))
                return false;
            if (segments != NULL && segments->length)
                ((tbox_path_segment *)segments->data)[segments->length - 1].close = true;
            current  = subpath;
            *closed  = true;
            command  = 0;
            previous = 'Z';
            tbox_path_space(&cursor);
            continue;
        }
        size_t n;
        switch (upper) {
        case 'M':
        case 'L':
        case 'T':
            n = 2;
            break;
        case 'H':
        case 'V':
            n = 1;
            break;
        case 'C':
            n = 6;
            break;
        case 'S':
        case 'Q':
            n = 4;
            break;
        case 'A':
            n = 7;
            break;
        default:
            return false;
        }
        double v[7] = { 0 };
        for (size_t i = 0; i < n; i++)
            if (!tbox_path_number(&cursor, i != 0 || !explicit_command, upper == 'A' && (i == 3 || i == 4), &v[i]))
                return false;
        if (upper == 'A' && (v[0] < 0.0 || v[1] < 0.0))
            return false;
        double ox = relative ? current.x : 0.0, oy = relative ? current.y : 0.0;
        tbox_path_point end = { v[n == 1 ? 0 : n - 2] + ox, v[n - 1] + oy };
        if (upper == 'H')
            end = (tbox_path_point){ v[0] + ox, current.y };
        if (upper == 'V')
            end = (tbox_path_point){ current.x, v[0] + oy };
        tbox_path_point b = { v[0] + ox, v[1] + oy }, c = { v[2] + ox, v[3] + oy };
        if (!isfinite(end.x) || !isfinite(end.y) || !isfinite(b.x) || !isfinite(b.y) || !isfinite(c.x) || !isfinite(c.y))
            return false;
        switch (upper) {
        case 'M':
            subpath = end;
            if (!tbox_path_segment_add(segments, end, end, (tbox_path_point){ 1, 0 }, (tbox_path_point){ 1, 0 }, true))
                return false;
            command = relative ? 'l' : 'L';
            break;
        case 'H':
        case 'V':
        case 'L':
            if (!tbox_path_line(segments, current, end))
                return false;
            break;
        case 'S':
            c = b;
            b = previous == 'C' || previous == 'S' ? (tbox_path_point){ 2 * current.x - control.x, 2 * current.y - control.y } : current;
            [[fallthrough]];
        case 'C':
            if (!tbox_path_cubic(segments, current, b, c, end, 0, tolerance))
                return false;
            control = c;
            break;
        case 'T':
        case 'Q': {
            if (upper == 'T')
                b = previous == 'Q' || previous == 'T' ? (tbox_path_point){ 2 * current.x - control.x, 2 * current.y - control.y } : current;
            control            = b;
            tbox_path_point c1 = { current.x + (b.x - current.x) * 2.0 / 3.0, current.y + (b.y - current.y) * 2.0 / 3.0 };
            tbox_path_point c2 = { end.x + (b.x - end.x) * 2.0 / 3.0, end.y + (b.y - end.y) * 2.0 / 3.0 };
            if (!tbox_path_cubic(segments, current, c1, c2, end, 0, tolerance))
                return false;
            break;
        }
        case 'A':
            if (!tbox_path_arc(segments, current, v[0], v[1], v[2], v[3] != 0, v[4] != 0, end, tolerance))
                return false;
            break;
        default:
            break;
        }
        current  = end;
        previous = upper;
        *closed  = false;
        tbox_path_space(&cursor);
    }
    return true;
}
