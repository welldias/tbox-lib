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
typedef tbox_path_point tbox_motion_point;
#define tbox_motion_segment_add tbox_path_segment_add
#define tbox_motion_line tbox_path_line
#define tbox_motion_arc(v,a,rx,ry,r,l,s,b) tbox_path_arc(v,a,rx,ry,r,l,s,b,TBOX_MOTION_TOLERANCE)
bool tbox_style_motion_svg(tbox_string_view data, tbox_vector *segments, bool *closed) {
    return tbox_path_parse(data, segments, closed, TBOX_MOTION_TOLERANCE);
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
