#include "tbox_style_grid.h"
#include "base/tbox_string.h"

#include <math.h>
#include <string.h>

static bool tbox_grid_is(tbox_string_view text, const char *word) {
    return tbox_string_view_equal_ascii_ci(tbox_string_view_trim_ascii_space(text), tbox_string_view_from_cstr(word));
}

/* A token preserves balanced functions, line-name groups and quoted rows. */
static bool tbox_grid_token(tbox_string_view *text, tbox_string_view *out) {
    *text = tbox_string_view_trim_ascii_space(*text);
    if (text->size == 0)
        return false;
    size_t i   = 0;
    int depth  = 0;
    char quote = 0;
    for (; i < text->size; i++) {
        char c = text->data[i];
        if (quote != 0) {
            if (c == quote) {
                quote = 0;
                i++;
                break;
            }
            if (c == '\\')
                return false;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '(' || c == '[') {
            depth++;
        } else if (c == ')' || c == ']') {
            if (--depth < 0)
                return false;
            if (depth == 0 && c == ']') {
                i++;
                break;
            }
        } else if (depth == 0 && tbox_ascii_is_space(c)) {
            break;
        }
    }
    if (depth != 0 || quote != 0 || i == 0)
        return false;
    *out  = tbox_string_view_make(text->data, i);
    *text = tbox_string_view_make(text->data + i, text->size - i);
    return true;
}

static bool tbox_grid_split(tbox_string_view text, char delimiter, tbox_string_view *a, tbox_string_view *b) {
    int depth  = 0;
    char quote = 0;
    for (size_t i = 0; i < text.size; i++) {
        char c = text.data[i];
        if (quote != 0) {
            if (c == quote)
                quote = 0;
        } else if (c == '\'' || c == '"')
            quote = c;
        else if (c == '(' || c == '[')
            depth++;
        else if (c == ')' || c == ']')
            depth--;
        else if (c == delimiter && depth == 0) {
            *a = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data, i));
            *b = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + i + 1, text.size - i - 1));
            return true;
        }
    }
    return false;
}

static bool tbox_grid_ident(tbox_string_view name) {
    if (name.size == 0 || name.size >= TBOX_STYLE_GRID_NAME_SIZE || tbox_grid_is(name, "auto") || tbox_grid_is(name, "span") || tbox_grid_is(name, "none") || tbox_grid_is(name, "initial") || tbox_grid_is(name, "inherit") || tbox_grid_is(name, "unset") || tbox_grid_is(name, "revert"))
        return false;
    for (size_t i = 0; i < name.size; i++) {
        unsigned char c = (unsigned char)name.data[i];
        if (!(((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) || c == '_' || c == '-' || c >= 128 || (i > 0 && (c >= '0' && c <= '9'))))
            return false;
    }
    return !(name.size == 1 && name.data[0] == '-');
}

static bool tbox_grid_integer(tbox_string_view text, int *out) {
    text = tbox_string_view_trim_ascii_space(text);
    if (text.size == 0)
        return false;
    size_t i = text.data[0] == '+' || text.data[0] == '-' ? 1 : 0;
    if (i == text.size)
        return false;
    for (; i < text.size; i++)
        if (!(text.data[i] >= '0' && text.data[i] <= '9'))
            return false;
    double value;
    if (!tbox_style_parse_number(text, &value) || value < -100000 || value > 100000)
        return false;
    *out = (int)value;
    return true;
}

static bool tbox_grid_function(tbox_string_view text, const char *name, tbox_string_view *body) {
    size_t n = strlen(name);
    if (text.size <= n + 1 || text.data[n] != '(' || text.data[text.size - 1] != ')' || !tbox_string_view_equal_ascii_ci(tbox_string_view_make(text.data, n), tbox_string_view_from_cstr(name)))
        return false;
    *body = tbox_string_view_trim_ascii_space(tbox_string_view_make(text.data + n + 1, text.size - n - 2));
    return true;
}

static bool tbox_grid_breadth_parse(tbox_string_view text, const tbox_style_units *units, bool flex, tbox_grid_breadth *out) {
    *out = (tbox_grid_breadth){ 0 };
    if (tbox_grid_is(text, "auto"))
        return true;
    if (tbox_grid_is(text, "min-content")) {
        out->kind = TBOX_GRID_MIN_CONTENT;
        return true;
    }
    if (tbox_grid_is(text, "max-content")) {
        out->kind = TBOX_GRID_MAX_CONTENT;
        return true;
    }
    if (text.size > 2 && tbox_grid_is(tbox_string_view_make(text.data + text.size - 2, 2), "fr")) {
        if (!flex || !tbox_style_parse_number(tbox_string_view_make(text.data, text.size - 2), &out->flex) || !isfinite(out->flex) || out->flex < 0)
            return false;
        out->kind = TBOX_GRID_FR;
        return true;
    }
    if (!tbox_style_parse_spacing_length(text, units, &out->length) || out->length.kind == TBOX_STYLE_LENGTH_AUTO || out->length.value < 0)
        return false;
    out->kind = TBOX_GRID_LENGTH;
    return true;
}

static bool tbox_grid_track_parse(tbox_string_view text, const tbox_style_units *units, tbox_grid_track *out) {
    *out = (tbox_grid_track){ 0 };
    tbox_string_view body, a, b;
    if (tbox_grid_function(text, "minmax", &body)) {
        return tbox_grid_split(body, ',', &a, &b) && tbox_grid_breadth_parse(a, units, false, &out->min) && tbox_grid_breadth_parse(b, units, true, &out->max);
    }
    if (tbox_grid_function(text, "fit-content", &body)) {
        if (!tbox_grid_breadth_parse(body, units, false, &out->max) || out->max.kind != TBOX_GRID_LENGTH)
            return false;
        out->max.kind = TBOX_GRID_FIT_CONTENT;
        return true;
    }
    if (!tbox_grid_breadth_parse(text, units, true, &out->max))
        return false;
    if (out->max.kind != TBOX_GRID_FR)
        out->min = out->max;
    return true;
}

static bool tbox_grid_append_list(tbox_grid_track_list *out, const tbox_grid_track_list *pattern, int repetitions, bool automatic, bool fit) {
    if (repetitions <= 0 || pattern->count == 0 || repetitions > (TBOX_GRID_MAX_TRACKS - out->count) / pattern->count || pattern->name_count * repetitions > TBOX_GRID_MAX_NAMES - out->name_count)
        return false;
    for (int n = 0; n < repetitions; n++) {
        for (int i = 0; i < pattern->name_count; i++) {
            tbox_grid_line_name name = pattern->names[i];
            name.line += out->count;
            name.repeated                 = automatic;
            out->names[out->name_count++] = name;
        }
        for (int i = 0; i < pattern->count; i++) {
            tbox_grid_track track     = pattern->tracks[i];
            track.auto_repeat         = automatic;
            track.auto_fit            = fit;
            out->tracks[out->count++] = track;
        }
    }
    return true;
}

static bool tbox_grid_list_parse(tbox_string_view text, const tbox_style_units *units, bool implicit, bool nested, tbox_grid_track_list *out) {
    memset(out, 0, sizeof(*out));
    tbox_string_view token;
    while (tbox_string_view_trim_ascii_space(text).size != 0) {
        if (!tbox_grid_token(&text, &token))
            return false;
        if (token.data[0] == '[') {
            if (implicit || token.data[token.size - 1] != ']')
                return false;
            tbox_string_view names = tbox_string_view_make(token.data + 1, token.size - 2), name;
            while (tbox_string_view_trim_ascii_space(names).size != 0) {
                if (!tbox_grid_token(&names, &name) || !tbox_grid_ident(name) || out->name_count == TBOX_GRID_MAX_NAMES)
                    return false;
                out->names[out->name_count++] = (tbox_grid_line_name){ name, out->count, false };
            }
            continue;
        }
        tbox_string_view body;
        if (tbox_grid_function(token, "repeat", &body)) {
            if (implicit || nested)
                return false;
            tbox_string_view a, b;
            if (!tbox_grid_split(body, ',', &a, &b))
                return false;
            bool automatic = tbox_grid_is(a, "auto-fill") || tbox_grid_is(a, "auto-fit");
            int n          = 1;
            if (!automatic && (!tbox_grid_integer(a, &n) || n <= 0))
                return false;
            tbox_grid_track_list pattern;
            if (!tbox_grid_list_parse(b, units, false, true, &pattern))
                return false;
            if (automatic) {
                for (int i = 0; i < out->count; i++)
                    if (out->tracks[i].auto_repeat)
                        return false;
                for (int i = 0; i < pattern.count; i++)
                    if (pattern.tracks[i].min.kind != TBOX_GRID_LENGTH && pattern.tracks[i].max.kind != TBOX_GRID_LENGTH)
                        return false;
            }
            if (!tbox_grid_append_list(out, &pattern, n, automatic, tbox_grid_is(a, "auto-fit")))
                return false;
        } else {
            if (out->count == TBOX_GRID_MAX_TRACKS || !tbox_grid_track_parse(token, units, &out->tracks[out->count]))
                return false;
            out->count++;
        }
    }
    return out->count > 0;
}

bool tbox_style_grid_parse_tracks(tbox_string_view text, const tbox_style_units *units, double available, double gap, bool definite, bool implicit, tbox_grid_track_list *out) {
    text = tbox_string_view_trim_ascii_space(text);
    memset(out, 0, sizeof(*out));
    if (text.size == 0 || (!implicit && tbox_grid_is(text, "none")))
        return true;
    if (!tbox_grid_list_parse(text, units, implicit, false, out))
        return false;
    int start = -1, end = -1;
    double fixed = 0, pattern_size = 0;
    for (int i = 0; i < out->count; i++) {
        const tbox_grid_track *track = &out->tracks[i];
        if (track->auto_repeat) {
            if (start < 0)
                start = i;
            end = i + 1;
        }
    }
    if (start < 0)
        return true;
    for (int i = 0; i < out->count; i++) {
        const tbox_grid_track *track = &out->tracks[i];
        if (track->min.kind != TBOX_GRID_LENGTH && track->max.kind != TBOX_GRID_LENGTH)
            return false;
        tbox_grid_breadth basis = track->max.kind == TBOX_GRID_LENGTH ? track->max : track->min;
        double size             = tbox_style_length_resolve(basis.length, definite ? available : 0);
        if (track->min.kind == TBOX_GRID_LENGTH)
            size = fmax(size, tbox_style_length_resolve(track->min.length, definite ? available : 0));
        size = fmax(1, size);
        if (track->auto_repeat)
            pattern_size += size;
        else
            fixed += size;
    }
    int pattern_count  = end - start;
    double repeats     = definite ? floor((available - fixed - gap * (out->count - pattern_count - 1)) / (pattern_size + gap * pattern_count)) : 1;
    int max_repeat     = 1 + (TBOX_GRID_MAX_TRACKS - out->count) / pattern_count;
    int n              = repeats > max_repeat ? max_repeat : repeats > 1 ? (int)repeats : 1;
    int repeated_names = 0;
    for (int i = 0; i < out->name_count; i++)
        if (out->names[i].repeated)
            repeated_names++;
    if (repeated_names > 0) {
        int name_limit = 1 + (TBOX_GRID_MAX_NAMES - out->name_count) / repeated_names;
        if (n > name_limit)
            n = name_limit;
    }
    if (n == 1)
        return true;
    tbox_grid_track_list original = *out;
    out->count = out->name_count = 0;
    for (int i = 0; i <= original.count; i++) {
        int copies = i >= start && i < end ? n : 1;
        /* Copy the whole pattern at its first boundary. */
        if (i > start && i < end)
            continue;
        if (i == start) {
            for (int k = 0; k < original.name_count; k++)
                if (original.names[k].line == i && !original.names[k].repeated) {
                    if (out->name_count == TBOX_GRID_MAX_NAMES)
                        return false;
                    out->names[out->name_count++] = (tbox_grid_line_name){ original.names[k].name, out->count, false };
                }
            tbox_grid_track_list pattern = { 0 };
            pattern.count                = pattern_count;
            memcpy(pattern.tracks, original.tracks + start, sizeof(tbox_grid_track) * (size_t)pattern_count);
            for (int k = 0; k < original.name_count; k++)
                if (original.names[k].repeated) {
                    tbox_grid_line_name name = original.names[k];
                    name.line -= start;
                    pattern.names[pattern.name_count++] = name;
                }
            if (!tbox_grid_append_list(out, &pattern, copies, true, original.tracks[start].auto_fit))
                return false;
        } else {
            for (int k = 0; k < original.name_count; k++)
                if (original.names[k].line == i && !original.names[k].repeated) {
                    if (out->name_count == TBOX_GRID_MAX_NAMES)
                        return false;
                    out->names[out->name_count++] = (tbox_grid_line_name){ original.names[k].name, out->count, false };
                }
            if (i < original.count)
                out->tracks[out->count++] = original.tracks[i];
        }
    }
    return true;
}

bool tbox_style_grid_parse_areas(tbox_string_view text, tbox_grid_areas *out) {
    memset(out, 0, sizeof(*out));
    text = tbox_string_view_trim_ascii_space(text);
    if (text.size == 0 || tbox_grid_is(text, "none"))
        return true;
    int cells[TBOX_GRID_MAX_AREAS] = { 0 };
    tbox_string_view row;
    while (text.size != 0) {
        if (!tbox_grid_token(&text, &row) || (row.data[0] != '"' && row.data[0] != '\'') || row.size < 2 || row.data[row.size - 1] != row.data[0] || out->rows == TBOX_GRID_MAX_TRACKS)
            return false;
        tbox_string_view content = tbox_string_view_make(row.data + 1, row.size - 2);
        int column               = 0;
        while ((content = tbox_string_view_trim_ascii_space(content)).size != 0) {
            size_t len = 0;
            bool dot   = content.data[0] == '.';
            while (len < content.size && !tbox_ascii_is_space(content.data[len]) && (content.data[len] == '.') == dot)
                len++;
            tbox_string_view name = tbox_string_view_make(content.data, len);
            content               = tbox_string_view_make(content.data + len, content.size - len);
            if (!dot) {
                if (!tbox_grid_ident(name))
                    return false;
                int a = 0;
                while (a < out->count && !tbox_string_view_equal(name, out->areas[a].name))
                    a++;
                if (a == out->count) {
                    if (out->count == TBOX_GRID_MAX_AREAS)
                        return false;
                    out->areas[out->count++] = (tbox_grid_area){ name, out->rows, column, 1, 1 };
                }
                tbox_grid_area *area = &out->areas[a];
                if (column < area->column)
                    return false;
                area->rows = out->rows - area->row + 1;
                if (column - area->column + 1 > area->columns)
                    area->columns = column - area->column + 1;
                cells[a]++;
            }
            if (++column > TBOX_GRID_MAX_TRACKS)
                return false;
        }
        if (column == 0 || (out->rows > 0 && column != out->columns))
            return false;
        out->columns = column;
        out->rows++;
        text = tbox_string_view_trim_ascii_space(text);
    }
    for (int i = 0; i < out->count; i++)
        if (cells[i] != out->areas[i].rows * out->areas[i].columns)
            return false;
    return true;
}

static bool tbox_grid_copy(char *dest, size_t capacity, tbox_string_view text) {
    text = tbox_string_view_trim_ascii_space(text);
    if (text.size >= capacity)
        return false;
    tbox_utf8_copy_truncated(dest, capacity, text);
    return true;
}

static bool tbox_grid_append(char *dest, size_t capacity, tbox_string_view text) {
    size_t n = strlen(dest);
    if (text.size + n + 2 > capacity)
        return false;
    if (n > 0)
        dest[n++] = ' ';
    return tbox_grid_copy(dest + n, capacity - n, text);
}

static bool tbox_grid_line_parse(tbox_string_view text, tbox_style_grid_line *out) {
    *out = (tbox_style_grid_line){ 0 };
    if (tbox_grid_is(text, "auto"))
        return true;
    tbox_string_view token;
    bool any = false;
    while ((text = tbox_string_view_trim_ascii_space(text)).size > 0) {
        if (!tbox_grid_token(&text, &token))
            return false;
        int number;
        if (tbox_grid_is(token, "span")) {
            if (out->span)
                return false;
            out->span = true;
        } else if (tbox_grid_integer(token, &number)) {
            if (number == 0 || out->number != 0)
                return false;
            out->number = number;
        } else if (tbox_grid_ident(token) && out->name[0] == '\0') {
            tbox_grid_copy(out->name, sizeof(out->name), token);
        } else
            return false;
        any = true;
    }
    if (out->span && out->number < 0)
        return false;
    if (out->span && out->number == 0 && out->name[0] == '\0')
        return false;
    return any;
}

static bool tbox_grid_flow_parse(tbox_string_view text, bool *column, bool *dense) {
    *column = *dense = false;
    bool axis = false, any = false;
    tbox_string_view token;
    while ((text = tbox_string_view_trim_ascii_space(text)).size > 0) {
        if (!tbox_grid_token(&text, &token))
            return false;
        if (tbox_grid_is(token, "dense") && !*dense)
            *dense = true;
        else if (!axis && (tbox_grid_is(token, "row") || tbox_grid_is(token, "column"))) {
            axis    = true;
            *column = tbox_grid_is(token, "column");
        } else
            return false;
        any = true;
    }
    return any;
}

/* Apply declarations from low to high cascade priority. Each declaration
 * is parsed into a temporary style first, so malformed shorthands are atomic. */
static bool tbox_grid_template_parse(tbox_string_view text, const tbox_style_units *units, tbox_style *style) {
    style->grid_template_rows[0] = style->grid_template_columns[0] = style->grid_template_areas[0] = '\0';
    if (tbox_grid_is(text, "none"))
        return true;
    tbox_string_view rows = text, columns = { 0 };
    bool slash = tbox_grid_split(text, '/', &rows, &columns);
    tbox_grid_track_list tracks;
    if (slash && (!tbox_style_grid_parse_tracks(columns, units, 0, 0, false, false, &tracks) || !tbox_grid_copy(style->grid_template_columns, sizeof(style->grid_template_columns), columns)))
        return false;
    bool quoted = false;
    for (size_t i = 0; i < rows.size; i++)
        if (rows.data[i] == '"' || rows.data[i] == '\'')
            quoted = true;
    if (!quoted)
        return slash && tbox_style_grid_parse_tracks(rows, units, 0, 0, false, false, &tracks) && tbox_grid_copy(style->grid_template_rows, sizeof(style->grid_template_rows), rows);
    tbox_string_view token;
    bool needs_size = false;
    while ((rows = tbox_string_view_trim_ascii_space(rows)).size > 0) {
        if (!tbox_grid_token(&rows, &token))
            return false;
        if (token.data[0] == '"' || token.data[0] == '\'') {
            if (needs_size && !tbox_grid_append(style->grid_template_rows, sizeof(style->grid_template_rows), tbox_string_view_from_cstr("auto")))
                return false;
            if (!tbox_grid_append(style->grid_template_areas, sizeof(style->grid_template_areas), token))
                return false;
            needs_size = true;
        } else if (token.data[0] == '[') {
            if (needs_size) {
                if (!tbox_grid_append(style->grid_template_rows, sizeof(style->grid_template_rows), tbox_string_view_from_cstr("auto")))
                    return false;
                needs_size = false;
            }
            if (!tbox_grid_append(style->grid_template_rows, sizeof(style->grid_template_rows), token))
                return false;
        } else {
            tbox_grid_track track;
            if (!needs_size || !tbox_grid_track_parse(token, units, &track) || !tbox_grid_append(style->grid_template_rows, sizeof(style->grid_template_rows), token))
                return false;
            needs_size = false;
        }
    }
    if (needs_size && !tbox_grid_append(style->grid_template_rows, sizeof(style->grid_template_rows), tbox_string_view_from_cstr("auto")))
        return false;
    tbox_grid_areas areas;
    if (!tbox_style_grid_parse_areas(tbox_string_view_from_cstr(style->grid_template_areas), &areas) || !tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(style->grid_template_rows), units, 0, 0, false, false, &tracks) || tracks.count != areas.rows)
        return false;
    if (slash && (!tbox_style_grid_parse_tracks(columns, units, 0, 0, false, false, &tracks) || tracks.count != areas.columns))
        return false;
    return true;
}

static bool tbox_grid_shorthand_parse(tbox_string_view text, const tbox_style_units *units, tbox_style *style) {
    style->grid_auto_columns[0] = style->grid_auto_rows[0] = '\0';
    style->grid_auto_flow_column = style->grid_auto_flow_dense = false;
    if (tbox_grid_template_parse(text, units, style))
        return true;
    tbox_string_view a, b;
    if (!tbox_grid_split(text, '/', &a, &b))
        return false;
    tbox_string_view halves[] = { a, b };
    for (int axis = 0; axis < 2; axis++) {
        tbox_string_view rest = halves[axis], token;
        bool flow = false, dense = false;
        char sizes[256] = { 0 };
        while ((rest = tbox_string_view_trim_ascii_space(rest)).size > 0) {
            if (!tbox_grid_token(&rest, &token))
                return false;
            if (tbox_grid_is(token, "auto-flow") && !flow && sizes[0] == '\0')
                flow = true;
            else if (tbox_grid_is(token, "dense") && !dense && sizes[0] == '\0')
                dense = true;
            else if (!tbox_grid_append(sizes, sizeof(sizes), token))
                return false;
        }
        if (!flow)
            continue;
        tbox_grid_track_list list;
        if (!tbox_style_grid_parse_tracks(tbox_string_view_from_cstr(sizes), units, 0, 0, false, true, &list) || !tbox_style_grid_parse_tracks(halves[1 - axis], units, 0, 0, false, false, &list))
            return false;
        style->grid_template_rows[0] = style->grid_template_columns[0] = style->grid_template_areas[0] = '\0';
        style->grid_auto_flow_column                                                                   = axis == 1;
        style->grid_auto_flow_dense                                                                    = dense;
        return tbox_grid_copy(axis == 0 ? style->grid_auto_rows : style->grid_auto_columns, sizeof(style->grid_auto_rows), tbox_string_view_from_cstr(sizes)) && tbox_grid_copy(axis == 0 ? style->grid_template_columns : style->grid_template_rows, sizeof(style->grid_template_rows), halves[1 - axis]);
    }
    return false;
}

static bool tbox_grid_apply(const tbox_css_resolved_declaration *decl, const tbox_style_units *units, tbox_style *style) {
    tbox_string_view name = decl->property, text = tbox_string_view_trim_ascii_space(decl->value);
    if (tbox_grid_is(name, "grid-template"))
        return tbox_grid_template_parse(text, units, style);
    if (tbox_grid_is(name, "grid"))
        return tbox_grid_shorthand_parse(text, units, style);
    static const char *const lists[] = { "grid-template-columns", "grid-template-rows", "grid-auto-columns", "grid-auto-rows" };
    char *dest[]                     = { style->grid_template_columns, style->grid_template_rows, style->grid_auto_columns, style->grid_auto_rows };
    for (int i = 0; i < 4; i++)
        if (tbox_grid_is(name, lists[i])) {
            tbox_grid_track_list tracks;
            return tbox_style_grid_parse_tracks(text, units, 0, 0, false, i >= 2, &tracks) && tbox_grid_copy(dest[i], i >= 2 ? sizeof(style->grid_auto_rows) : sizeof(style->grid_template_rows), text);
        }
    if (tbox_grid_is(name, "grid-template-areas")) {
        tbox_grid_areas areas;
        return tbox_style_grid_parse_areas(text, &areas) && tbox_grid_copy(style->grid_template_areas, sizeof(style->grid_template_areas), text);
    }
    if (tbox_grid_is(name, "grid-auto-flow"))
        return tbox_grid_flow_parse(text, &style->grid_auto_flow_column, &style->grid_auto_flow_dense);
    static const char *const lines[] = { "grid-row-start", "grid-column-start", "grid-row-end", "grid-column-end" };
    tbox_style_grid_line *line[]     = { &style->grid_row_start, &style->grid_column_start, &style->grid_row_end, &style->grid_column_end };
    for (int i = 0; i < 4; i++)
        if (tbox_grid_is(name, lines[i]))
            return tbox_grid_line_parse(text, line[i]);
    int count                      = tbox_grid_is(name, "grid-area") ? 4 : 2;
    tbox_style_grid_line values[4] = { 0 };
    tbox_string_view rest          = text, a, b;
    int n                          = 0;
    while (tbox_grid_split(rest, '/', &a, &b)) {
        if (n >= count - 1 || !tbox_grid_line_parse(a, &values[n++]))
            return false;
        rest = b;
    }
    if (!tbox_grid_line_parse(rest, &values[n++]))
        return false;
    for (int i = n; i < count; i++) {
        int source = count == 4 ? (i == 2 ? 0 : i == 3 ? 1 : 0) : 0;
        if (values[source].number == 0 && !values[source].span && values[source].name[0] != '\0')
            values[i] = values[source];
    }
    if (count == 4)
        for (int i = 0; i < 4; i++)
            *line[i] = values[i];
    else {
        int axis        = tbox_grid_is(name, "grid-column") ? 1 : 0;
        *line[axis]     = values[0];
        *line[axis + 2] = values[1];
    }
    return true;
}

void tbox_style_resolve_grid(const tbox_css_computed_style *computed, const tbox_style_units *units, tbox_style *style) {
    for (int i = 0; i < 4; i++)
        style->grid_units[i] = (tbox_style_grid_units){ units->font_size, units->root_font_size, units->viewport_width, units->viewport_height };
    static const char *const properties[] = { "grid", "grid-template", "grid-template-columns", "grid-template-rows", "grid-template-areas", "grid-auto-columns", "grid-auto-rows", "grid-auto-flow", "grid-area", "grid-row", "grid-column", "grid-row-start", "grid-row-end", "grid-column-start", "grid-column-end" };
    const tbox_css_resolved_declaration *decls[sizeof(properties) / sizeof(properties[0])];
    size_t count = 0;
    for (size_t i = 0; i < sizeof(properties) / sizeof(properties[0]); i++) {
        const tbox_css_resolved_declaration *decl = tbox_css_computed_style_find(computed, tbox_string_view_from_cstr(properties[i]));
        if (decl == NULL)
            continue;
        size_t j = count++;
        while (j > 0 && tbox_css_cascade_priority_compare(decls[j - 1], decl) > 0) {
            decls[j] = decls[j - 1];
            j--;
        }
        decls[j] = decl;
    }
    for (size_t i = 0; i < count; i++) {
        tbox_style candidate = *style;
        if (tbox_grid_apply(decls[i], units, &candidate))
            *style = candidate;
    }
}
