#include "tbox_context_internal.h"

typedef struct tbox_file_popup_geometry {
    tbox_rect rect, back;
    double row_height;
    size_t first, visible;
} tbox_file_popup_geometry;

static bool tbox_context_file_popup_geometry(tbox_context *ctx, tbox_file_popup_geometry *out);

static char *tbox_context_file_copy(const char *text);
static char *tbox_context_file_join(const char *directory, const char *name);
static char *tbox_context_file_parent(const char *directory);
static void tbox_context_file_free_entries(tbox_file_entry *entries, size_t count);
static int tbox_context_file_entry_compare(const void *left, const void *right);
static bool tbox_context_file_load_directory(tbox_context *ctx, const char *path);
static void tbox_context_file_push_text(tbox_vector *items, const tbox_font_face *face,
                                        tbox_rect rect, tbox_string_view value, tbox_css_rgba color,
                                        tbox_rect clip);

static char *tbox_context_file_copy(const char *text) {
    size_t length = strlen(text);
    if (length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

static char *tbox_context_file_join(const char *directory, const char *name) {
    size_t directory_length = strlen(directory), name_length = strlen(name);
    if (directory_length > SIZE_MAX - name_length - 2) return NULL;
    char *path = malloc(directory_length + name_length + 2);
    if (path == NULL) return NULL;
    memcpy(path, directory, directory_length);
    size_t at = directory_length;
    if (at == 0 || path[at - 1] != '/') path[at++] = '/';
    memcpy(path + at, name, name_length + 1);
    return path;
}

static char *tbox_context_file_parent(const char *directory) {
    size_t length = strlen(directory);
    while (length > 1 && directory[length - 1] == '/') length--;
    while (length > 1 && directory[length - 1] != '/') length--;
    if (length > 1) length--;
    char *parent = malloc(length + 1);
    if (parent == NULL) return NULL;
    memcpy(parent, directory, length);
    parent[length] = '\0';
    return parent;
}

static void tbox_context_file_free_entries(tbox_file_entry *entries, size_t count) {
    for (size_t i = 0; i < count; i++) free(entries[i].name);
    free(entries);
}

void tbox_context_file_close(tbox_context *ctx) {
    ctx->open_file = NULL;
    free(ctx->file_directory);
    ctx->file_directory = NULL;
    tbox_context_file_free_entries(ctx->file_entries, ctx->file_count);
    ctx->file_entries = NULL;
    ctx->file_count = ctx->file_first = ctx->file_highlight = 0;
}

static int tbox_context_file_entry_compare(const void *left, const void *right) {
    const tbox_file_entry *a = left, *b = right;
    if (a->directory != b->directory) return a->directory ? -1 : 1;
    if (strcmp(a->name, "..") == 0) return -1;
    if (strcmp(b->name, "..") == 0) return 1;
    bool a_hidden = a->name[0] == '.', b_hidden = b->name[0] == '.';
    if (a_hidden != b_hidden) return a_hidden ? 1 : -1;
    return strcmp(a->name, b->name);
}

static bool tbox_context_file_load_directory(tbox_context *ctx, const char *path) {
    DIR *directory = opendir(path);
    if (directory == NULL) return false;
    char *copy = tbox_context_file_copy(path);
    if (copy == NULL) { closedir(directory); return false; }
    tbox_file_entry *entries = NULL;
    size_t count = 0, capacity = 0;
    bool success = true;
    struct dirent *item;
    while ((item = readdir(directory)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || (strcmp(item->d_name, "..") == 0 && strcmp(path, "/") == 0))
            continue;
        char *full = tbox_context_file_join(path, item->d_name);
        if (full == NULL) { success = false; break; }
        struct stat info;
        bool visible = stat(full, &info) == 0 && (S_ISDIR(info.st_mode) || S_ISREG(info.st_mode));
        free(full);
        if (!visible) continue;
        if (count == capacity) {
            size_t next = capacity == 0 ? 32 : capacity * 2;
            if (next <= capacity || next > SIZE_MAX / sizeof(*entries)) { success = false; break; }
            tbox_file_entry *grown = realloc(entries, next * sizeof(*entries));
            if (grown == NULL) { success = false; break; }
            entries = grown;
            capacity = next;
        }
        char *name = tbox_context_file_copy(item->d_name);
        if (name == NULL) { success = false; break; }
        entries[count++] = (tbox_file_entry){name, S_ISDIR(info.st_mode)};
    }
    closedir(directory);
    if (!success) {
        free(copy);
        tbox_context_file_free_entries(entries, count);
        return false;
    }
    if (count > 1) qsort(entries, count, sizeof(*entries), tbox_context_file_entry_compare);
    free(ctx->file_directory);
    tbox_context_file_free_entries(ctx->file_entries, ctx->file_count);
    ctx->file_directory = copy;
    ctx->file_entries = entries;
    ctx->file_count = count;
    ctx->file_first = ctx->file_highlight = 0;
    return true;
}

bool tbox_context_file_open(tbox_context *ctx, const tbox_html_node *node) {
    char directory[4096];
    if (getcwd(directory, sizeof(directory)) == NULL ||
        !tbox_context_file_load_directory(ctx, directory)) return false;
    ctx->open_file = node;
    return true;
}

tbox_file_field *tbox_context_file_field(tbox_context *ctx, const tbox_html_node *node, bool create) {
    for (tbox_file_field *field = ctx->file_fields; field != NULL; field = field->next)
        if (field->node == node) return field;
    if (!create) return NULL;
    tbox_file_field *field = calloc(1, sizeof(*field));
    if (field == NULL) return NULL;
    field->node = node;
    field->next = ctx->file_fields;
    ctx->file_fields = field;
    return field;
}

bool tbox_context_file_activate(tbox_context *ctx, size_t index) {
    if (ctx->open_file == NULL || index >= ctx->file_count) return false;
    const tbox_file_entry *entry = &ctx->file_entries[index];
    char *path = strcmp(entry->name, "..") == 0 ?
        tbox_context_file_parent(ctx->file_directory) :
        tbox_context_file_join(ctx->file_directory, entry->name);
    if (path == NULL) return false;
    if (entry->directory) {
        bool changed = tbox_context_file_load_directory(ctx, path);
        free(path);
        return changed;
    }
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) { free(path); return false; }
    tbox_file_field *field = tbox_context_file_field(ctx, ctx->open_file, true);
    if (field == NULL) { free(path); return false; }
    const tbox_html_node *node = ctx->open_file;
    char *name = tbox_context_file_copy(entry->name);
    if (name == NULL) { free(path); return false; }
    free(field->path);
    field->path = path;
    tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)node,
        tbox_string_view_make("value", 5), tbox_string_view_from_cstr(name));
    tbox_context_file_close(ctx);
    if (ctx->input_handler != NULL) {
        const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
        if (value != NULL) ctx->input_handler(ctx, (tbox_html_node *)node, value->value, ctx->input_userdata);
    }
    free(name);
    return true;
}

static bool tbox_context_file_popup_geometry(tbox_context *ctx, tbox_file_popup_geometry *out) {
    if (ctx->open_file == NULL) return false;
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->open_file);
    if (box == NULL) return false;
    tbox_style root_style = tbox_context_control_style(ctx, ctx->open_file, NULL, NULL);
    tbox_style row_style = tbox_context_control_style(ctx, ctx->open_file, "row", NULL);
    tbox_style back_style = tbox_context_control_style(ctx, ctx->open_file, "back", NULL);
    double width = tbox_context_control_size(root_style.width, 320.0);
    double row_height = tbox_context_control_size(row_style.height, 28.0);
    double back_width = tbox_context_control_size(back_style.width, 26.0);
    double back_height = tbox_context_control_size(back_style.height, 26.0);
    if (width < back_width + 52.0) width = back_width + 52.0;
    double height = tbox_context_control_size(root_style.height, 260.0);
    double required_height = 40.0 + 7.0 * row_height + 24.0;
    if (height < required_height) height = required_height;
    double x = box->border_box.x;
    if (x + width > ctx->viewport_width) x = ctx->viewport_width - width;
    if (x < 0.0) x = 0.0;
    double below = ctx->viewport_height - (box->border_box.y + box->border_box.height);
    double above = box->border_box.y;
    double y = below < height && above > below ? box->border_box.y - height :
        box->border_box.y + box->border_box.height;
    size_t visible = ctx->file_count < 7 ? ctx->file_count : 7;
    size_t first = ctx->file_first;
    if (first + visible > ctx->file_count) first = ctx->file_count - visible;
    *out = (tbox_file_popup_geometry){
        .rect = {x, y, width, height}, .back = {x + 7.0, y + 7.0, back_width, back_height},
        .row_height = row_height, .first = first, .visible = visible,
    };
    return true;
}

bool tbox_context_file_popup_visible(tbox_context *ctx) {
    tbox_file_popup_geometry popup;
    return tbox_context_file_popup_geometry(ctx, &popup);
}

bool tbox_context_file_popup_contains(tbox_context *ctx, double x, double y) {
    tbox_file_popup_geometry popup;
    return tbox_context_file_popup_geometry(ctx, &popup) &&
        tbox_context_point_in_rect(popup.rect, x, y);
}

bool tbox_context_file_popup_scroll(tbox_context *ctx, double x, double y, double delta_y) {
    tbox_file_popup_geometry popup;
    if (!tbox_context_file_popup_geometry(ctx, &popup) ||
        !tbox_context_point_in_rect(popup.rect, x, y)) return false;
    if (delta_y > 0.0 && ctx->file_first + popup.visible < ctx->file_count) ctx->file_first++;
    if (delta_y < 0.0 && ctx->file_first > 0) ctx->file_first--;
    return true;
}

bool tbox_context_file_parent_directory(tbox_context *ctx) {
    if (ctx->file_directory == NULL || strcmp(ctx->file_directory, "/") == 0) return false;
    char *parent = tbox_context_file_parent(ctx->file_directory);
    if (parent == NULL) return false;
    bool changed = tbox_context_file_load_directory(ctx, parent);
    free(parent);
    return changed;
}

bool tbox_context_file_popup_click(tbox_context *ctx, double x, double y) {
    tbox_file_popup_geometry popup;
    if (!tbox_context_file_popup_geometry(ctx, &popup) ||
        !tbox_context_point_in_rect(popup.rect, x, y)) return false;
    if (tbox_context_point_in_rect(popup.back, x, y)) {
        tbox_context_file_parent_directory(ctx);
        return true;
    }
    double relative_y = y - popup.rect.y - 40.0;
    if (relative_y >= 0.0 && relative_y < popup.row_height * popup.visible) {
        size_t index = popup.first + (size_t)(relative_y / popup.row_height);
        tbox_context_file_activate(ctx, index);
    }
    return true;
}

static void tbox_context_file_push_text(tbox_vector *items, const tbox_font_face *face,
                                        tbox_rect rect, tbox_string_view value, tbox_css_rgba color,
                                        tbox_rect clip) {
    if (face == NULL || value.size == 0 || clip.width <= 0.0 || clip.height <= 0.0) return;
    tbox_paint_op *op = tbox_vector_push(items);
    *op = (tbox_paint_op){.kind = TBOX_PAINT_TEXT_RUN,
        .rect = {rect.x, rect.y + (rect.height - tbox_font_face_line_height(face)) / 2.0,
                 tbox_font_measure_text(face, value), tbox_font_face_line_height(face)},
        .color = color, .text = value, .face = face, .has_clip = true, .clip = clip};
}

void tbox_context_paint_file_controls(tbox_context *ctx, const tbox_layout_box *box,
                                              tbox_vector *items) {
    for (; box != NULL; box = box->next_sibling) {
        if (tbox_context_is_file_input(box->node) && box->style != NULL && !box->style->visibility_hidden) {
            tbox_rect clip = box->content_box;
            for (const tbox_layout_box *ancestor = box->parent; ancestor != NULL; ancestor = ancestor->parent)
                if (ancestor->style != NULL && ancestor->style->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE)
                    clip = tbox_context_rect_intersection(clip, ancestor->padding_box);
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts,
                tbox_string_view_from_cstr(box->style->font_family), false, false, 13.0);
            bool disabled = tbox_html_node_get_attribute(box->node, tbox_string_view_make("disabled", 8)) != NULL;
            double button_width = box->content_box.width < 88.0 ? box->content_box.width : 88.0;
            tbox_rect button = {box->content_box.x, box->content_box.y,
                button_width, box->content_box.height};
            tbox_context_push_fill(items, button, (tbox_css_rgba){118, 124, 134, 255}, true, clip);
            if (button.width > 2.0 && button.height > 2.0)
                tbox_context_push_fill(items, (tbox_rect){button.x + 1.0, button.y + 1.0,
                    button.width - 2.0, button.height - 2.0},
                    disabled ? (tbox_css_rgba){232, 232, 232, 255} : (tbox_css_rgba){240, 242, 246, 255},
                    true, clip);
            tbox_context_date_text(items, face, button, tbox_string_view_make("Choose file", 11),
                disabled ? (tbox_css_rgba){140, 140, 140, 255} : (tbox_css_rgba){25, 25, 25, 255});
            tbox_file_field *field = tbox_context_file_field(ctx, box->node, false);
            const char *name = field != NULL && field->path != NULL ? strrchr(field->path, '/') : NULL;
            tbox_string_view label = name != NULL ? tbox_string_view_from_cstr(name + 1) :
                tbox_string_view_make("No file chosen", 14);
            tbox_rect label_rect = {button.x + button.width + 8.0, button.y,
                box->content_box.width - button.width - 8.0, button.height};
            tbox_context_file_push_text(items, face, label_rect, label,
                disabled ? (tbox_css_rgba){140, 140, 140, 255} : box->style->color,
                tbox_context_rect_intersection(clip, label_rect));
        }
        tbox_context_paint_file_controls(ctx, box->first_child, items);
    }
}

void tbox_context_paint_file_popup(tbox_context *ctx, tbox_vector *items) {
    tbox_file_popup_geometry popup;
    if (!tbox_context_file_popup_geometry(ctx, &popup)) return;
    tbox_style root_style = tbox_context_control_style(ctx, ctx->open_file, NULL, NULL);
    tbox_context_paint_control_box(items, popup.rect, &root_style, false, (tbox_rect){0});
    tbox_style back_style = tbox_context_control_style(ctx, ctx->open_file, "back", NULL);
    tbox_context_paint_control_box(items, popup.back, &back_style, false, (tbox_rect){0});
    tbox_context_date_text(items, tbox_context_control_font(ctx, &back_style), popup.back,
        tbox_string_view_make("<", 1), back_style.color);
    if (ctx->file_directory != NULL) {
        tbox_style path_style = tbox_context_control_style(ctx, ctx->open_file, "path", NULL);
        tbox_rect header = {popup.rect.x + 42.0, popup.rect.y + 6.0, popup.rect.width - 52.0, 28.0};
        tbox_context_file_push_text(items, tbox_context_control_font(ctx, &path_style), header,
            tbox_string_view_from_cstr(ctx->file_directory), path_style.color, header);
    }
    tbox_style divider_style = tbox_context_control_style(ctx, ctx->open_file, "divider", NULL);
    tbox_context_paint_control_box(items, (tbox_rect){popup.rect.x + 8.0, popup.rect.y + 38.0,
        popup.rect.width - 16.0, 1.0}, &divider_style, false, (tbox_rect){0});
    for (size_t row = 0; row < popup.visible; row++) {
        size_t index = popup.first + row;
        const tbox_file_entry *entry = &ctx->file_entries[index];
        tbox_rect line = {popup.rect.x + 7.0, popup.rect.y + 40.0 + popup.row_height * row,
            popup.rect.width - 14.0, popup.row_height};
        bool highlighted = index == ctx->file_highlight;
        tbox_style row_style = tbox_context_control_style(ctx, ctx->open_file, "row",
            highlighted ? (entry->directory ? "is-directory is-selected" : "is-selected") :
            entry->directory ? "is-directory" : NULL);
        tbox_context_paint_control_box(items, line, &row_style, false, (tbox_rect){0});
        tbox_rect label = {line.x + 7.0, line.y, line.width - 14.0, line.height};
        tbox_context_file_push_text(items, tbox_context_control_font(ctx, &row_style), label,
            tbox_string_view_from_cstr(entry->name), row_style.color, label);
    }
    if (ctx->file_count == 0) {
        tbox_style empty_style = tbox_context_control_style(ctx, ctx->open_file, "empty", NULL);
        tbox_rect empty = {popup.rect.x + 15.0, popup.rect.y + 47.0, popup.rect.width - 30.0, 24.0};
        tbox_context_file_push_text(items, tbox_context_control_font(ctx, &empty_style), empty,
            tbox_string_view_make("Empty folder", 12), empty_style.color, empty);
    }
    tbox_style footer_style = tbox_context_control_style(ctx, ctx->open_file, "footer", NULL);
    tbox_rect footer = {popup.rect.x + 12.0, popup.rect.y + popup.rect.height - 22.0,
        popup.rect.width - 24.0, 18.0};
    tbox_context_file_push_text(items, tbox_context_control_font(ctx, &footer_style), footer,
        tbox_string_view_from_cstr("Enter: open or select  -  Esc: close"), footer_style.color, footer);
}

tbox_string_view tbox_context_file_path(tbox_context *ctx, const tbox_html_node *input) {
    if (ctx == NULL || !tbox_context_is_file_input(input) || !tbox_context_node_attached(ctx, input))
        return tbox_string_view_make(NULL, 0);
    tbox_file_field *field = tbox_context_file_field(ctx, input, false);
    if (field == NULL || field->path == NULL) return tbox_string_view_make(NULL, 0);
    const char *name = strrchr(field->path, '/');
    const tbox_html_attribute *value = tbox_html_node_get_attribute(input, tbox_string_view_make("value", 5));
    if (name == NULL || value == NULL ||
        !tbox_string_view_equal(value->value, tbox_string_view_from_cstr(name + 1)))
        return tbox_string_view_make(NULL, 0);
    return tbox_string_view_from_cstr(field->path);
}
