#ifndef TBOX_TOOL_SUPPORT_H
#define TBOX_TOOL_SUPPORT_H

/* Shared by the measuring tools tbox_bench and tbox_frame_dump (see
 * docs/12-ferramentas-e-testes.md): file reading, the embedded font,
 * opening a document as tbox_app would, and a monotonic clock. Both are
 * single-threaded programs with their own main(). */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tbox/tbox.h>

/* The whole file, NUL-terminated, in malloc'd memory; NULL on failure. */
static inline char *tbox_tool_read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    char *data = NULL;
    if (fseek(file, 0, SEEK_END) == 0) {
        long size = ftell(file);
        if (size >= 0 && fseek(file, 0, SEEK_SET) == 0 && (data = malloc((size_t)size + 1)) != NULL) {
            *out_size        = fread(data, 1, (size_t)size, file);
            data[*out_size] = '\0';
        }
    }
    fclose(file);
    return data;
}

/* A font cache on the vendored Liberation Sans (regular for bold too), so
 * results do not depend on the fonts installed. NULL on failure; the
 * cache keeps its own copy of the bytes. */
static inline tbox_font_face_cache *tbox_tool_fonts(void) {
    size_t size;
    char *data = tbox_tool_read_file(TBOX_TOOL_FONT_PATH, &size);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", TBOX_TOOL_FONT_PATH);
        return NULL;
    }
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(data, size, data, size, NULL, NULL, NULL);
    free(data);
    return fonts;
}

/* `path`'s directory ("." when it has none) into `out`, for <link>,
 * <img> and @font-face URLs. */
static inline void tbox_tool_directory(const char *path, char *out, size_t out_size) {
    const char *slash = strrchr(path, '/');
    size_t length     = slash != NULL ? (size_t)(slash - path) : 0;
    if (slash == NULL)
        snprintf(out, out_size, ".");
    else
        snprintf(out, out_size, "%.*s", (int)length, path);
}

/* A context on `html`, with `css` as the application stylesheet (may be
 * NULL) and resources resolved against `base_dir`. */
static inline tbox_context *tbox_tool_open(const char *html, size_t html_size, const char *css, size_t css_size, const char *base_dir, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    tbox_context_options options = tbox_context_options_default();
    options.asset_base_dir       = base_dir;
    return tbox_context_open_with_options(html, html_size, css != NULL ? css : "", css != NULL ? css_size : 0, fonts, images, &options);
}

static inline double tbox_tool_now(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

/* "WxH" into the two sizes; false when malformed. */
static inline bool tbox_tool_parse_size(const char *text, int *width, int *height) {
    return sscanf(text, "%dx%d", width, height) == 2 && *width > 0 && *height > 0;
}

#endif /* TBOX_TOOL_SUPPORT_H */
