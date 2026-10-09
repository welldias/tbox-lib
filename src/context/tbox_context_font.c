#include "tbox_context_internal.h"
#include <strings.h>

static tbox_string_view tbox_font_trim(tbox_string_view value) {
    while (value.size && isspace((unsigned char)value.data[0])) value.data++, value.size--;
    while (value.size && isspace((unsigned char)value.data[value.size - 1])) value.size--;
    return value;
}

static tbox_string_view tbox_font_descriptor(const tbox_css_font_face_rule *face, const char *name) {
    tbox_string_view found = {0};
    for (size_t i = 0; i < face->declaration_count; i++)
        if (tbox_string_view_equal_ascii_ci(face->declarations[i].property, tbox_string_view_from_cstr(name)))
            found = tbox_font_trim(face->declarations[i].value);
    return found;
}

static bool tbox_font_url(tbox_string_view src, char *out, size_t capacity) {
    size_t start = 0;
    while (start + 4 <= src.size && strncasecmp(src.data + start, "url(", 4) != 0) start++;
    if (start + 4 > src.size) return false;
    start += 4;
    while (start < src.size && isspace((unsigned char)src.data[start])) start++;
    char quote = 0;
    if (start < src.size && (src.data[start] == '\'' || src.data[start] == '"')) quote = src.data[start++];
    size_t end = start;
    while (end < src.size && (quote ? src.data[end] != quote : src.data[end] != ')')) end++;
    if (end == src.size) return false;
    tbox_string_view path = tbox_font_trim(tbox_string_view_make(src.data + start, end - start));
    if (path.size == 0 || path.size >= capacity || memchr(path.data, ':', path.size) != NULL) return false;
    memcpy(out, path.data, path.size);
    out[path.size] = '\0';
    return true;
}

static void tbox_context_register_font_face(tbox_font_face_cache *fonts, const tbox_css_font_face_rule *face, const char *base_dir) {
    if (fonts == NULL) return;
    tbox_string_view family = tbox_font_descriptor(face, "font-family");
    tbox_string_view src = tbox_font_descriptor(face, "src");
    if (family.size == 0 || src.size == 0) return;
    if ((family.data[0] == '"' || family.data[0] == '\'') && family.size >= 2 && family.data[family.size - 1] == family.data[0])
        family.data++, family.size -= 2;
    char url[1024];
    if (!tbox_font_url(src, url, sizeof(url))) return;
    char path[2048];
    if (url[0] == '/') snprintf(path, sizeof(path), "%s", url);
    else if (base_dir != NULL && base_dir[0] != '\0') snprintf(path, sizeof(path), "%s/%s", base_dir, url);
    else snprintf(path, sizeof(path), "%s", url);
    FILE *file = fopen(path, "rb");
    if (file == NULL) return;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return; }
    long length = ftell(file);
    if (length <= 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return; }
    void *bytes = malloc((size_t)length);
    if (bytes == NULL) { fclose(file); return; }
    bool read_ok = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (read_ok) {
        int weight = 400;
        tbox_string_view weight_text = tbox_font_descriptor(face, "font-weight");
        if (tbox_string_view_equal_ascii_ci(weight_text, tbox_string_view_from_cstr("bold"))) weight = 700;
        else if (weight_text.size == 3) {
            int parsed = (weight_text.data[0] - '0') * 100 + (weight_text.data[1] - '0') * 10 + weight_text.data[2] - '0';
            if (parsed >= 100 && parsed <= 900 && parsed % 100 == 0) weight = parsed;
        }
        tbox_string_view style = tbox_font_descriptor(face, "font-style");
        bool italic = tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("italic")) || tbox_string_view_equal_ascii_ci(style, tbox_string_view_from_cstr("oblique"));
        tbox_font_face_cache_register(fonts, family, weight, italic, bytes, (size_t)length);
    }
    free(bytes);
}

void tbox_context_register_font_faces(tbox_font_face_cache *fonts, const tbox_css_stylesheet *sheet, const char *base_dir) {
    size_t count = tbox_css_stylesheet_font_face_count(sheet);
    const tbox_css_font_face_rule *faces = tbox_css_stylesheet_font_faces(sheet);
    for (size_t i = 0; i < count; i++) tbox_context_register_font_face(fonts, &faces[i], base_dir);
}
