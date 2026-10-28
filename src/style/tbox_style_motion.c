#include <math.h>
#include <stddef.h>
#include <string.h>

#include "base/tbox_string.h"
#include "base/tbox_arena.h"
#include "style/tbox_style_motion.h"

static const char *const tbox_motion_properties[] = {
    "offset-path", "offset-distance", "offset-position", "offset-anchor", "offset-rotate", "offset"
};

/* The motion longhands, parsed apart from the style (whose offset_path
 * lives out of line). */
typedef struct tbox_motion_values {
    tbox_style_offset_path offset_path;
    tbox_style_length offset_distance;
    tbox_style_offset_position offset_position, offset_anchor;
    tbox_style_offset_rotate offset_rotate;
} tbox_motion_values;

static void tbox_motion_initial(tbox_motion_values *values) {
    memset(&values->offset_path, 0, sizeof(values->offset_path));
    values->offset_distance = (tbox_style_length){ .kind = TBOX_STYLE_LENGTH_PX };
    values->offset_position = (tbox_style_offset_position){ .mode = 2 };
    values->offset_anchor = (tbox_style_offset_position){ .mode = 1 };
    values->offset_rotate = (tbox_style_offset_rotate){ .auto_rotate = true };
}

static bool tbox_motion_position(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, bool anchor, tbox_style_offset_position *out) {
    tbox_style_offset_position p = { 0 };
    if (count == 1 && tbox_style_is(tokens[0], "auto"))
        p.mode = 1;
    else if (!anchor && count == 1 && tbox_style_is(tokens[0], "normal"))
        p.mode = 2;
    else if (!tbox_style_parse_position(tokens, count, units, p.position))
        return false;
    *out = p;
    return true;
}

static bool tbox_motion_rotate(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_offset_rotate *out) {
    if (count == 0 || count > 2)
        return false;
    tbox_style_offset_rotate r = { 0 };
    bool have_auto = false, have_angle = false, reverse = false;
    for (size_t i = 0; i < count; i++) {
        if (tbox_style_is(tokens[i], "auto") || tbox_style_is(tokens[i], "reverse")) {
            if (have_auto) return false;
            have_auto = r.auto_rotate = true;
            reverse = tbox_style_is(tokens[i], "reverse");
        } else if (!have_angle && tbox_style_parse_angle(tokens[i], units, &r.angle)) {
            have_angle = true;
        } else {
            return false;
        }
    }
    r.angle += reverse ? 180.0 : 0.0;
    *out = r;
    return true;
}

static bool tbox_motion_box(tbox_string_view token, tbox_style_coord_box *out) {
    static const char *const names[] = { "border-box", "padding-box", "content-box", "margin-box" };
    for (size_t i = 0; i < 4; i++) {
        if (tbox_style_is(token, names[i])) {
            *out = (tbox_style_coord_box)i;
            return true;
        }
    }
    /* In a CSS box, SVG coordinate boxes map to the corresponding CSS box. */
    if (tbox_style_is(token, "fill-box")) {
        *out = TBOX_STYLE_COORD_CONTENT_BOX;
        return true;
    }
    if (tbox_style_is(token, "stroke-box") || tbox_style_is(token, "view-box")) {
        *out = TBOX_STYLE_COORD_BORDER_BOX;
        return true;
    }
    return false;
}

static bool tbox_motion_path(const tbox_string_view *tokens, size_t count, const tbox_style_units *units, tbox_style_offset_path *out) {
    if (count == 0 || count > 2)
        return false;
    tbox_style_offset_path p = { 0 };
    if (count == 1 && tbox_style_is(tokens[0], "none")) {
        *out = p;
        return true;
    }
    size_t function = 0;
    if (tbox_motion_box(tokens[0], &p.coord_box)) {
        p.kind = TBOX_STYLE_OFFSET_PATH_BOX;
        if (count == 1) {
            *out = p;
            return true;
        }
        function = 1;
    } else if (count == 2 && !tbox_motion_box(tokens[1], &p.coord_box)) {
        return false;
    }
    tbox_string_view args;
    if (tbox_style_function_args(tokens[function], "path", &args)) {
        tbox_string_view parts[2];
        size_t n = tbox_style_split_top_level(args, ',', parts, 2);
        if (n == 2 && !tbox_style_is(parts[0], "nonzero") && !tbox_style_is(parts[0], "evenodd"))
            return false;
        if (n == 0) return false;
        tbox_string_view data = tbox_string_view_trim_ascii_space(parts[n - 1]);
        if (data.size < 2 || (data.data[0] != '"' && data.data[0] != '\'') || data.data[data.size - 1] != data.data[0])
            return false;
        if (memchr(data.data, '\\', data.size) != NULL) {
            tbox_arena scratch = tbox_arena_create(0);
            tbox_string_builder decoded;
            tbox_string_builder_init(&decoded, &scratch, data.size);
            bool valid = tbox_style_parse_content_string(&data, &decoded) && data.size == 0 && decoded.length < sizeof(p.value.data);
            if (valid) {
                memcpy(p.value.data, decoded.data, decoded.length);
                p.value.data[decoded.length] = '\0';
            }
            tbox_arena_destroy(&scratch);
            if (!valid) return false;
        } else {
            data = tbox_string_view_make(data.data + 1, data.size - 2);
            if (data.size >= sizeof(p.value.data)) return false;
            memcpy(p.value.data, data.data, data.size);
            p.value.data[data.size] = '\0';
        }
        bool closed;
        if (!tbox_style_motion_svg(tbox_string_view_from_cstr(p.value.data), NULL, &closed)) return false;
        p.kind = TBOX_STYLE_OFFSET_PATH_SVG;
    } else if (tbox_style_function_args(tokens[function], "ray", &args)) {
        tbox_string_view parts[12];
        size_t n = tbox_style_tokens(args, parts, 12);
        bool have_angle = false, have_size = false;
        if (n == 0) return false;
        for (size_t i = 0; i < n; i++) {
            static const char *const sizes[] = { "closest-side", "closest-corner", "farthest-side", "farthest-corner", "sides" };
            if (tbox_style_is(parts[i], "at")) {
                if (p.has_position) return false;
                size_t position_count = n - i - 1;
                if (position_count > 4) position_count = 4;
                while (position_count > 0 && !tbox_style_parse_position(parts + i + 1, position_count, units, p.value.ray.position)) position_count--;
                if (position_count == 0) return false;
                p.has_position = true;
                i += position_count;
                continue;
            }
            if (tbox_style_is(parts[i], "contain")) {
                if (p.value.ray.contain) return false;
                p.value.ray.contain = true;
                continue;
            }
            bool size = false;
            for (size_t k = 0; k < 5; k++) {
                if (!tbox_style_is(parts[i], sizes[k])) continue;
                if (have_size) return false;
                p.value.ray.size = (unsigned char)k;
                have_size = size = true;
                break;
            }
            if (size) continue;
            if (have_angle || !tbox_style_parse_angle(parts[i], units, &p.value.ray.angle))
                return false;
            have_angle = true;
        }
        if (!have_angle) return false;
        p.kind = TBOX_STYLE_OFFSET_PATH_RAY;
    } else if (tbox_style_parse_clip_path(tokens[function], units, &p.value.shape) && p.value.shape.kind != TBOX_STYLE_CLIP_PATH_NONE) {
        p.kind = TBOX_STYLE_OFFSET_PATH_SHAPE;
        /* Only an explicit `at` prevents offset-position supplying the center. */
        if (tbox_style_function_args(tokens[function], "circle", &args) || tbox_style_function_args(tokens[function], "ellipse", &args)) {
            tbox_string_view parts[14];
            size_t n = tbox_style_tokens(args, parts, 14);
            for (size_t i = 0; i < n; i++)
                p.has_position |= tbox_style_is(parts[i], "at");
        }
    } else {
        return false;
    }
    *out = p;
    return true;
}

static bool tbox_motion_distance(tbox_string_view raw, const tbox_style_units *units, tbox_style_length *out) {
    return tbox_style_parse_spacing_length(raw, units, out) && out->kind != TBOX_STYLE_LENGTH_AUTO && isfinite(out->value) && isfinite(out->px_offset);
}

static bool tbox_motion_shorthand(tbox_string_view raw, const tbox_style_units *units, tbox_motion_values *out) {
    tbox_string_view halves[3], tokens[16];
    size_t halves_count = tbox_style_split_top_level(raw, '/', halves, 3);
    if (halves_count == 0 || halves_count > 2) return false;
    size_t n = tbox_style_tokens(halves[0], tokens, 16);
    if (n == 0) return false;
    tbox_motion_initial(out);
    if (halves_count == 2) {
        tbox_string_view anchor[4];
        size_t count = tbox_style_tokens(halves[1], anchor, 4);
        if (!tbox_motion_position(anchor, count, units, true, &out->offset_anchor)) return false;
    }
    /* Position may precede the path. Try every legal prefix; path() stays
     * one token even when its quoted data includes spaces and commas. */
    for (size_t prefix = 0; prefix <= 4 && prefix <= n; prefix++) {
        tbox_style_offset_position position = { .mode = 2 };
        if (prefix > 0 && !tbox_motion_position(tokens, prefix, units, false, &position)) continue;
        if (prefix == n) {
            out->offset_position = position;
            return true;
        }
        for (size_t path_count = 1; path_count <= 2 && prefix + path_count <= n; path_count++) {
            tbox_style_offset_path path;
            if (!tbox_motion_path(tokens + prefix, path_count, units, &path)) continue;
            size_t start = prefix + path_count;
            /* Distance and rotate are unordered after the path. */
            for (size_t distance_at = start; distance_at <= n; distance_at++) {
                bool have_distance = distance_at < n;
                tbox_style_length distance = { .kind = TBOX_STYLE_LENGTH_PX };
                if (have_distance && !tbox_motion_distance(tokens[distance_at], units, &distance)) continue;
                tbox_string_view rotate[3];
                size_t rotate_count = 0;
                for (size_t j = start; j < n; j++) {
                    if (have_distance && j == distance_at) continue;
                    if (rotate_count == 3) break;
                    rotate[rotate_count++] = tokens[j];
                }
                tbox_style_offset_rotate rotation = { .auto_rotate = true };
                if (rotate_count && !tbox_motion_rotate(rotate, rotate_count, units, &rotation)) continue;
                out->offset_position = position;
                out->offset_path = path;
                out->offset_distance = distance;
                out->offset_rotate = rotation;
                return true;
            }
        }
    }
    return false;
}

static bool tbox_motion_parse(size_t which, tbox_string_view raw, const tbox_style_units *units, tbox_motion_values *out) {
    tbox_string_view tokens[8];
    size_t count = tbox_style_tokens(raw, tokens, 8);
    switch (which) {
    case 0: return tbox_motion_path(tokens, count, units, &out->offset_path);
    case 1: return count == 1 && tbox_motion_distance(tokens[0], units, &out->offset_distance);
    case 2: return tbox_motion_position(tokens, count, units, false, &out->offset_position);
    case 3: return tbox_motion_position(tokens, count, units, true, &out->offset_anchor);
    case 4: return tbox_motion_rotate(tokens, count, units, &out->offset_rotate);
    default: return tbox_motion_shorthand(raw, units, out);
    }
}

bool tbox_style_motion_declaration_valid(tbox_string_view property, tbox_string_view value) {
    size_t which = 0;
    while (which < 6 && !tbox_style_is(property, tbox_motion_properties[which])) which++;
    if (which == 6) return true;
    if (tbox_style_is(value, "initial") || tbox_style_is(value, "inherit") || tbox_style_is(value, "unset") || tbox_style_is(value, "revert") || tbox_style_is(value, "revert-layer")) return true;
    /* Variables are validated after substitution, in the style resolver. */
    for (size_t i = 0; i + 4 <= value.size; i++)
        if (tbox_style_is(tbox_string_view_make(value.data + i, 4), "var(")) return true;
    const tbox_style_units units = { .font_size = 16.0, .root_font_size = 16.0, .viewport_width = 800.0, .viewport_height = 600.0, .container_inline = -1.0, .container_block = -1.0, .zoom = 1.0 };
    tbox_motion_values parsed;
    return tbox_motion_parse(which, value, &units, &parsed);
}

void tbox_style_resolve_motion(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style, tbox_arena *arena) {
    const tbox_css_resolved_declaration *decls[6];
    size_t kinds[6], count = 0;
    for (size_t k = 0; k < 6; k++) {
        const tbox_css_resolved_declaration *d = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(tbox_motion_properties[k]));
        if (d == NULL) continue;
        size_t at = count++;
        while (at > 0 && tbox_css_cascade_priority_compare(decls[at - 1], d) > 0) {
            decls[at] = decls[at - 1];
            kinds[at] = kinds[at - 1];
            at--;
        }
        decls[at] = d;
        kinds[at] = k;
    }
    tbox_motion_values values;
    tbox_motion_initial(&values);
    bool path_set = false;
    for (size_t i = 0; i < count; i++) {
        /* Parse into a temporary: an invalid shorthand must not reset
         * any of the previously resolved longhands. */
        tbox_motion_values parsed;
        if (!tbox_motion_parse(kinds[i], decls[i]->value, units, &parsed)) continue;
        switch (kinds[i]) {
        case 0: values.offset_path = parsed.offset_path, path_set = true; break;
        case 1: values.offset_distance = parsed.offset_distance; break;
        case 2: values.offset_position = parsed.offset_position; break;
        case 3: values.offset_anchor = parsed.offset_anchor; break;
        case 4: values.offset_rotate = parsed.offset_rotate; break;
        default:
            values   = parsed;
            path_set = true;
            break;
        }
    }
    style->offset_distance = values.offset_distance;
    /* Undeclared, they stay the group's initial values (no copy). */
    if (count > 0) {
        tbox_style_group_set(style, TBOX_STYLE_GROUP_TRANSFORMS, arena, offsetof(tbox_style_transforms, offset_position), &values.offset_position, sizeof(values.offset_position));
        tbox_style_group_set(style, TBOX_STYLE_GROUP_TRANSFORMS, arena, offsetof(tbox_style_transforms, offset_anchor), &values.offset_anchor, sizeof(values.offset_anchor));
    }
    style->offset_rotate   = values.offset_rotate;
    style->offset_path     = NULL; /* initial: none */
    if (path_set) {
        tbox_style_offset_path *path = tbox_style_group_copy(arena, &values.offset_path, sizeof(*path));
        if (path != NULL)
            style->offset_path = path;
    }
}
