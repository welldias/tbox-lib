/* tbox_bench: how long a frame takes and how much memory it uses, on a
 * synthetic table of N rows or on a page. See docs/12-ferramentas-e-testes.md
 * ("Medição de desempenho"). Not part of ctest: timings depend on the
 * machine and on the build type. */

#include "tbox_tool_support.h"

#include "context/tbox_context_controls.h"
#include "context/tbox_context_internal.h"
#include "html_parser/tbox_html_document.h"

/* A table like an application's: header, zebra rows, a hover rule, some
 * selected rows and a bold first column, inside a scroll container. */
static char *tbox_bench_table(int rows, int viewport_height, bool virtual_rows, size_t *out_size) {
    size_t capacity = (size_t)(virtual_rows ? 0 : rows) * 200 + 2048;
    char *html      = malloc(capacity);
    if (html == NULL)
        return NULL;
    size_t n = (size_t)snprintf(html, capacity,
        "<html><head><style>"
        "body { margin: 0; font-family: sans-serif; }"
        "#scroll { height: %dpx; overflow-y: auto; }"
        "table { border-collapse: collapse; }"
        "td, th { border: 1px solid #ccc; padding: 4px 8px; }"
        "tbody tr:nth-child(even) { background: #f4f4f4; }"
        "tbody tr:hover { background: #cfe0ff; }"
        "tr.selected { background: #ffd; }"
        "td:first-child { font-weight: bold; text-align: right; }"
        "</style></head><body><div id='scroll'><table>"
        "<thead><tr><th>Id</th><th>Name</th><th>E-mail</th><th>Status</th><th>Value</th></tr></thead><tbody>",
        viewport_height > 40 ? viewport_height - 40 : viewport_height);
    for (int i = 0; i < rows && !virtual_rows; i++)
        n += (size_t)snprintf(html + n, capacity - n,
            "<tr%s><td>%d</td><td>Person number %d</td><td>person%d@example.com</td><td>%s</td><td>%d.%02d</td></tr>",
            i % 7 == 3 ? " class='selected'" : "", i, i, i, i % 2 ? "active" : "inactive", i * 7, i % 100);
    n += (size_t)snprintf(html + n, capacity - n, "</tbody></table></div>"
        "<template id='row'><tr><td></td><td></td><td></td><td></td><td></td></tr></template></body></html>");
    *out_size = n;
    return html;
}

/* Resident memory in MB from /proc (Linux), or a negative value where
 * there is none. */
static double tbox_bench_rss_mb(void) {
    FILE *file = fopen("/proc/self/statm", "r");
    long pages = 0, resident = 0;
    if (file == NULL)
        return -1.0;
    int read = fscanf(file, "%ld %ld", &pages, &resident);
    fclose(file);
    return read == 2 ? (double)resident * 4096.0 / (1024.0 * 1024.0) : -1.0;
}

/* --virtual-table: fills each row as --table writes it. */
static void tbox_bench_rows(tbox_context *ctx, tbox_html_node *table, size_t first, tbox_html_node *const *rows, size_t count, void *userdata) {
    (void)table;
    (void)userdata;
    for (size_t r = 0; r < count; r++) {
        size_t i = first + r;
        char text[5][64];
        int sizes[5] = {
            snprintf(text[0], sizeof(text[0]), "%zu", i), snprintf(text[1], sizeof(text[1]), "Person number %zu", i),
            snprintf(text[2], sizeof(text[2]), "person%zu@example.com", i), snprintf(text[3], sizeof(text[3]), "%s", i % 2 ? "active" : "inactive"),
            snprintf(text[4], sizeof(text[4]), "%zu.%02zu", i * 7, i % 100),
        };
        size_t c = 0;
        for (tbox_html_node *cell = tbox_html_node_first_element_child(rows[r]); cell != NULL && c < 5; cell = tbox_html_node_next_element_sibling(cell), c++)
            tbox_html_node_set_text_content(ctx->document, cell, tbox_string_view_make(text[c], (size_t)sizes[c]));
    }
}

static size_t tbox_bench_count_boxes(const tbox_layout_box *box) {
    size_t count = 0;
    for (; box != NULL; box = box->next_sibling)
        count += 1 + tbox_bench_count_boxes(box->first_child);
    return count;
}

static void tbox_bench_usage(void) {
    fprintf(stderr,
        "usage: tbox_bench [--frames N] [--size WxH] [--scroll PX] (--table ROWS | --virtual-table ROWS | PAGE.html [PAGE.css])\n"
        "  --table ROWS  synthetic table of ROWS rows in a scroll container\n"
        "  --virtual-table ROWS  the same table, virtual (tbox_table_virtualize)\n"
        "  --frames N    measured frames, after one warm-up frame (default 20)\n"
        "  --size WxH    viewport (default 1280x800)\n"
        "  --scroll PX   scroll the viewport's center by PX between frames (default 0)\n");
}

int main(int argc, char **argv) {
    int frames = 20, width = 1280, height = 800, rows = -1;
    bool virtual_rows = false;
    double scroll       = 0.0;
    const char *page    = NULL, *page_css = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!tbox_tool_parse_size(argv[++i], &width, &height)) {
                tbox_bench_usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--scroll") == 0 && i + 1 < argc)
            scroll = atof(argv[++i]);
        else if (strcmp(argv[i], "--table") == 0 && i + 1 < argc)
            rows = atoi(argv[++i]);
        else if (strcmp(argv[i], "--virtual-table") == 0 && i + 1 < argc) {
            rows         = atoi(argv[++i]);
            virtual_rows = true;
        }
        else if (argv[i][0] == '-') {
            tbox_bench_usage();
            return 2;
        } else if (page == NULL)
            page = argv[i];
        else
            page_css = argv[i];
    }
    if ((rows < 0) == (page == NULL) || frames < 1) {
        tbox_bench_usage();
        return 2;
    }

    size_t html_size = 0, css_size = 0;
    char *html = NULL, *css = NULL;
    char base_dir[4096] = ".";
    if (page == NULL) {
        html = tbox_bench_table(rows, height, virtual_rows, &html_size);
    } else {
        html = tbox_tool_read_file(page, &html_size);
        tbox_tool_directory(page, base_dir, sizeof(base_dir));
    }
    if (page_css != NULL)
        css = tbox_tool_read_file(page_css, &css_size);
    if (html == NULL || (page_css != NULL && css == NULL)) {
        fprintf(stderr, "cannot read the page\n");
        return 2;
    }
    tbox_font_face_cache *fonts = tbox_tool_fonts();
    tbox_image_cache *images    = tbox_image_cache_create(base_dir);
    double rss_before           = tbox_bench_rss_mb();
    tbox_context *ctx           = fonts != NULL ? tbox_tool_open(html, html_size, css, css_size, base_dir, fonts, images) : NULL;
    if (ctx == NULL) {
        fprintf(stderr, "cannot open the page\n");
        return 2;
    }

    if (virtual_rows) {
        tbox_html_node *table = tbox_context_query(ctx, "table", 5), *row = tbox_context_query(ctx, "#row", 4);
        if (!tbox_context_table_virtualize(ctx, table, row, (size_t)rows, tbox_bench_rows, NULL, NULL)) {
            fprintf(stderr, "cannot make the table virtual\n");
            return 2;
        }
    }
    size_t document_start = 0;

    uint32_t *pixels             = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    tbox_damage_tracker *tracker = tbox_damage_tracker_create();
    double frame_time = 0.0, diff_time = 0.0, raster_time = 0.0;
    size_t ops = 0;
    tbox_display_list list = { 0 };
    for (int f = 0; f <= frames; f++) {
        double start = tbox_tool_now();
        tbox_context_run_frame(ctx, width, height, &list);
        double built = tbox_tool_now();
        tbox_damage damage = tbox_damage_tracker_update(tracker, &list, width, height);
        double diffed      = tbox_tool_now();
        tbox_raster_display_list_damaged(pixels, width, height, &list, &damage);
        double painted = tbox_tool_now();
        if (f == 1)
            document_start = tbox_arena_bytes_used(&ctx->document->arena);
        if (f > 0) { /* frame 0 warms the caches up */
            frame_time += built - start;
            diff_time += diffed - built;
            raster_time += painted - diffed;
        }
        ops = list.count;
        if (scroll != 0.0)
            tbox_context_scroll(ctx, width / 2.0, height / 2.0, scroll);
    }
    /* A full repaint of the last frame, whatever the damage. */
    double full_start = tbox_tool_now();
    for (int f = 0; f < frames; f++)
        tbox_raster_display_list(pixels, width, height, &list);
    double full_time   = tbox_tool_now() - full_start;
    size_t frame_arena = tbox_arena_bytes_allocated(&ctx->frame_arena);
    size_t elements    = ctx->styles.count;
    size_t boxes       = tbox_bench_count_boxes(ctx->root);

    /* The stages apart, in an arena of their own: the frame's style,
     * layout and display list without what run_frame adds around them
     * (transitions, scroll, controls), so they sum to a bit less. */
    tbox_css_cascade_source sources[3] = {
        { ctx->ua_stylesheet, TBOX_CSS_ORIGIN_USER_AGENT },
        { ctx->stylesheet, TBOX_CSS_ORIGIN_AUTHOR },
        { ctx->internal_stylesheet, TBOX_CSS_ORIGIN_AUTHOR },
    };
    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    double style_time = 0.0, layout_time = 0.0, list_time = 0.0;
    size_t style_bytes = 0, layout_bytes = 0, list_bytes = 0;
    for (int f = 0; f < frames; f++) {
        tbox_arena arena       = tbox_arena_create(0);
        double start           = tbox_tool_now();
        tbox_style_table table = tbox_style_resolve_tree_in_viewport_with_preferences(&arena, root, sources, 3, width, height, false, NULL, 0, NULL);
        double styled          = tbox_tool_now();
        size_t after_style     = tbox_arena_bytes_used(&arena);
        tbox_layout_box *boxes_root = tbox_layout_build(&arena, root, &table, fonts, images, width, height);
        double laid            = tbox_tool_now();
        size_t after_layout    = tbox_arena_bytes_used(&arena);
        tbox_render_build_display_list_in_viewport(&arena, boxes_root, width, height);
        double listed = tbox_tool_now();
        style_time += styled - start;
        layout_time += laid - styled;
        list_time += listed - laid;
        style_bytes  = after_style;
        layout_bytes = after_layout - after_style;
        list_bytes   = tbox_arena_bytes_used(&arena) - after_layout;
        tbox_arena_destroy(&arena);
    }

    const double mb = 1024.0 * 1024.0;
    printf("%s, %dx%d, %d frames%s\n", page == NULL ? (virtual_rows ? "virtual table" : "table") : page, width, height, frames, scroll != 0.0 ? ", scrolling" : "");
    if (page == NULL)
        printf("  rows            %d\n", rows);
    printf("  elements        %zu (%zu boxes, %zu ops)\n", elements, boxes, ops);
    printf("  frame           %8.2f ms   (tbox_context_run_frame)\n", frame_time / frames * 1e3);
    printf("    style         %8.2f ms   %7.1f MB\n", style_time / frames * 1e3, (double)style_bytes / mb);
    printf("    layout        %8.2f ms   %7.1f MB\n", layout_time / frames * 1e3, (double)layout_bytes / mb);
    printf("    display list  %8.2f ms   %7.1f MB\n", list_time / frames * 1e3, (double)list_bytes / mb);
    printf("  damage diff     %8.2f ms\n", diff_time / frames * 1e3);
    printf("  raster          %8.2f ms   (damaged regions only; %.2f ms for the whole viewport)\n", raster_time / frames * 1e3, full_time / frames * 1e3);
    printf("  frame arena     %8.1f MB\n", (double)frame_arena / mb);
    printf("  document arena  %8.1f MB   (%+.1f KB over the measured frames)\n", (double)tbox_arena_bytes_used(&ctx->document->arena) / mb,
           ((double)tbox_arena_bytes_used(&ctx->document->arena) - (double)document_start) / 1024.0);
    double rss = tbox_bench_rss_mb();
    if (rss >= 0.0)
        printf("  resident        %8.1f MB   (%.1f MB before opening the page)\n", rss, rss_before);

    tbox_damage_tracker_destroy(tracker);
    free(pixels);
    tbox_context_close(ctx);
    tbox_image_cache_destroy(images);
    tbox_font_face_cache_destroy(fonts);
    free(css);
    free(html);
    return 0;
}
