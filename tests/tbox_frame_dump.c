/* tbox_frame_dump: a signature of every frame of a set of documents, to
 * check that a change leaves the result alone. Run it with the build
 * before and after the change and compare the two outputs (--compare).
 * See docs/12-ferramentas-e-testes.md ("Comparação entre builds"). */

#include "tbox_tool_support.h"

#include <stdint.h>

#include "context/tbox_context_internal.h"

/* FNV-1a over everything a frame shows. Pointers never enter it, and
 * doubles are hashed by value with -0.0 as 0.0, so two builds agree. */
typedef struct tbox_dump_hash {
    uint64_t value;
} tbox_dump_hash;

static void tbox_dump_bytes(tbox_dump_hash *hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash->value ^= bytes[i];
        hash->value *= 1099511628211ull;
    }
}

static void tbox_dump_double(tbox_dump_hash *hash, double value) {
    value += 0.0; /* -0.0 becomes 0.0 */
    tbox_dump_bytes(hash, &value, sizeof(value));
}

static void tbox_dump_rect(tbox_dump_hash *hash, tbox_rect rect) {
    tbox_dump_double(hash, rect.x);
    tbox_dump_double(hash, rect.y);
    tbox_dump_double(hash, rect.width);
    tbox_dump_double(hash, rect.height);
}

/* Layout: every box's border and content boxes, and every text run's
 * place and text. */
static void tbox_dump_boxes(tbox_dump_hash *hash, const tbox_layout_box *box) {
    for (; box != NULL; box = box->next_sibling) {
        tbox_dump_rect(hash, box->border_box);
        tbox_dump_rect(hash, box->content_box);
        for (size_t i = 0; i < box->text_run_count; i++) {
            tbox_dump_rect(hash, box->text_runs[i].rect);
            tbox_dump_bytes(hash, box->text_runs[i].text.data, box->text_runs[i].text.size);
        }
        tbox_dump_boxes(hash, box->first_child);
    }
}

/* One frame: layout, display list (kind, place, color of each op) and
 * the painted pixels. */
static uint64_t tbox_dump_frame(tbox_context *ctx, int width, int height, uint32_t *pixels) {
    tbox_display_list list;
    tbox_context_run_frame(ctx, width, height, &list);
    tbox_dump_hash hash = { 14695981039346656037ull };
    tbox_dump_boxes(&hash, ctx->root);
    for (size_t i = 0; i < list.count; i++) {
        const tbox_paint_op *op = &list.items[i];
        tbox_dump_bytes(&hash, &op->kind, sizeof(op->kind));
        tbox_dump_rect(&hash, op->rect);
        tbox_dump_bytes(&hash, &op->color, sizeof(op->color));
    }
    for (size_t i = 0; i < (size_t)width * (size_t)height; i++)
        pixels[i] = 0xFFFFFFFFu;
    tbox_raster_display_list(pixels, width, height, &list);
    tbox_dump_bytes(&hash, pixels, (size_t)width * (size_t)height * sizeof(uint32_t));
    return hash.value;
}

typedef struct tbox_dump_options {
    int width, height, hover_step, scroll_every;
    double scroll;
} tbox_dump_options;

/* Prints `path` and the signature of each frame: one with no pointer, then
 * one per hover point on a grid of `hover_step` px, scrolling the
 * viewport's center by `scroll` px every `scroll_every` frames. */
static bool tbox_dump_document(const char *path, const tbox_dump_options *options, tbox_font_face_cache *fonts, uint32_t *pixels) {
    size_t size;
    char *html = tbox_tool_read_file(path, &size);
    if (html == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    char base_dir[4096];
    tbox_tool_directory(path, base_dir, sizeof(base_dir));
    tbox_image_cache *images = tbox_image_cache_create(base_dir);
    tbox_context *ctx        = tbox_tool_open(html, size, NULL, 0, base_dir, fonts, images);
    free(html);
    if (ctx == NULL) {
        fprintf(stderr, "cannot open %s\n", path);
        tbox_image_cache_destroy(images);
        return false;
    }
    printf("%s %016llx", path, (unsigned long long)tbox_dump_frame(ctx, options->width, options->height, pixels));
    int frame = 1;
    for (int y = options->hover_step / 2; y < options->height; y += options->hover_step) {
        for (int x = options->hover_step / 2; x < options->width; x += options->hover_step) {
            tbox_context_update_hover(ctx, true, x, y);
            printf(" %016llx", (unsigned long long)tbox_dump_frame(ctx, options->width, options->height, pixels));
            if (options->scroll_every > 0 && ++frame % options->scroll_every == 0)
                tbox_context_scroll(ctx, options->width / 2.0, options->height / 2.0, options->scroll);
        }
    }
    printf("\n");
    tbox_context_close(ctx);
    tbox_image_cache_destroy(images);
    return true;
}

/* A dump's lines, each "path hash hash ...", read into memory. */
typedef struct tbox_dump_file {
    char *text;
    char **lines;
    size_t count;
} tbox_dump_file;

static bool tbox_dump_load(const char *path, tbox_dump_file *out) {
    size_t size;
    *out = (tbox_dump_file){ tbox_tool_read_file(path, &size), NULL, 0 };
    if (out->text == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return false;
    }
    size_t capacity = 1;
    for (size_t i = 0; i < size; i++)
        capacity += out->text[i] == '\n';
    out->lines = malloc(capacity * sizeof(char *));
    if (out->lines == NULL)
        return false;
    for (char *line = out->text; *line != '\0';) {
        char *end = strchr(line, '\n');
        if (end != NULL)
            *end = '\0';
        if (*line != '\0')
            out->lines[out->count++] = line;
        if (end == NULL)
            break;
        line = end + 1;
    }
    return true;
}

/* The path at the start of `line`, as a length. */
static size_t tbox_dump_path_length(const char *line) {
    const char *space = strchr(line, ' ');
    return space != NULL ? (size_t)(space - line) : strlen(line);
}

static const char *tbox_dump_find(const tbox_dump_file *file, const char *line) {
    size_t length = tbox_dump_path_length(line);
    for (size_t i = 0; i < file->count; i++)
        if (tbox_dump_path_length(file->lines[i]) == length && strncmp(file->lines[i], line, length) == 0)
            return file->lines[i];
    return NULL;
}

/* Reports each document whose frames differ, or that only one side has.
 * 0 when the two agree, 1 when they differ, 2 on error. */
static int tbox_dump_compare(const char *before_path, const char *after_path) {
    tbox_dump_file before, after;
    if (!tbox_dump_load(before_path, &before) || !tbox_dump_load(after_path, &after))
        return 2;
    size_t documents = 0, frames = 0, differing = 0;
    for (size_t i = 0; i < before.count; i++) {
        const char *line  = before.lines[i];
        const char *other = tbox_dump_find(&after, line);
        int path_length   = (int)tbox_dump_path_length(line);
        if (other == NULL) {
            printf("only in %s: %.*s\n", before_path, path_length, line);
            differing++;
            continue;
        }
        documents++;
        /* Frame by frame: tokens after the path, 17 characters apart. */
        const char *a = line + path_length, *b = other + path_length;
        size_t frame = 0;
        long first   = -1;
        while (*a != '\0' && *b != '\0') {
            if (strncmp(a, b, 17) != 0 && first < 0)
                first = (long)frame;
            a += strlen(a) >= 17 ? 17 : strlen(a);
            b += strlen(b) >= 17 ? 17 : strlen(b);
            frame++;
        }
        frames += frame;
        if (first >= 0 || *a != *b) {
            printf("differs: %.*s (first at frame %ld)\n", path_length, line, first >= 0 ? first : (long)frame);
            differing++;
        }
    }
    for (size_t i = 0; i < after.count; i++)
        if (tbox_dump_find(&before, after.lines[i]) == NULL) {
            printf("only in %s: %.*s\n", after_path, (int)tbox_dump_path_length(after.lines[i]), after.lines[i]);
            differing++;
        }
    printf("%zu documents, %zu frames compared: %s\n", documents, frames, differing == 0 ? "identical" : "DIFFERENT");
    free(before.lines);
    free(before.text);
    free(after.lines);
    free(after.text);
    return differing == 0 ? 0 : 1;
}

static void tbox_dump_usage(void) {
    fprintf(stderr,
        "usage: tbox_frame_dump [--size WxH] [--hover-step PX] [--scroll PX] [--scroll-every N] FILE.html... > OUT.txt\n"
        "       tbox_frame_dump --compare BEFORE.txt AFTER.txt\n"
        "  --size WxH        viewport (default 800x600)\n"
        "  --hover-step PX   grid of hover points, one frame each (default 80)\n"
        "  --scroll PX       scroll the viewport's center by PX ... (default 90)\n"
        "  --scroll-every N  ... every N frames; 0 never scrolls (default 9)\n");
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--compare") == 0)
        return tbox_dump_compare(argv[2], argv[3]);
    tbox_dump_options options = { 800, 600, 80, 9, 90.0 };
    int first                 = 1;
    for (; first < argc && argv[first][0] == '-'; first++) {
        if (strcmp(argv[first], "--size") == 0 && first + 1 < argc && tbox_tool_parse_size(argv[first + 1], &options.width, &options.height))
            first++;
        else if (strcmp(argv[first], "--hover-step") == 0 && first + 1 < argc && atoi(argv[first + 1]) > 0)
            options.hover_step = atoi(argv[++first]);
        else if (strcmp(argv[first], "--scroll") == 0 && first + 1 < argc)
            options.scroll = atof(argv[++first]);
        else if (strcmp(argv[first], "--scroll-every") == 0 && first + 1 < argc && atoi(argv[first + 1]) >= 0)
            options.scroll_every = atoi(argv[++first]);
        else {
            tbox_dump_usage();
            return 2;
        }
    }
    if (first == argc) {
        tbox_dump_usage();
        return 2;
    }
    tbox_font_face_cache *fonts = tbox_tool_fonts();
    uint32_t *pixels            = malloc((size_t)options.width * (size_t)options.height * sizeof(uint32_t));
    if (fonts == NULL || pixels == NULL)
        return 2;
    int status = 0;
    for (int i = first; i < argc; i++)
        if (!tbox_dump_document(argv[i], &options, fonts, pixels))
            status = 2;
    free(pixels);
    tbox_font_face_cache_destroy(fonts);
    return status;
}
