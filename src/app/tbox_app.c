#include <tbox/app.h>

#include <string.h>

#include <tbox/context.h>
#include <tbox/font.h>
#include <tbox/output.h>
#include <tbox/string_view.h>

/* v0's one and only font: a generic sans-serif, neither bold nor italic, at
 * the CSS2.1 initial font-size of 16px -- see ARCHITECTURE.md's "Fonte /
 * Texto" section and <tbox/app.h>'s doc comment for why this is fixed
 * rather than configurable yet. */
#define TBOX_APP_FONT_SIZE_PX 16.0

bool tbox_app_open(const char *html, const char *css, int32_t width, int32_t height) {
    tbox_font_source *font_source = tbox_font_source_fontconfig_create();
    if (font_source == NULL) {
        return false;
    }

    tbox_font_query query = {
        .family = tbox_string_view_make("sans-serif", strlen("sans-serif")),
        .bold   = false,
        .italic = false,
    };

    const void *font_data = NULL;
    size_t font_size      = 0;
    if (!tbox_font_source_resolve(font_source, query, &font_data, &font_size)) {
        tbox_font_source_destroy(font_source);
        return false;
    }

    tbox_font_face *font = tbox_font_face_load(font_data, font_size, TBOX_APP_FONT_SIZE_PX);
    if (font == NULL) {
        tbox_font_source_destroy(font_source);
        return false;
    }

    /* NOTE: despite <tbox/font.h>'s tbox_font_source_resolve doc comment
     * ("font_data is copied/read eagerly by FreeType"), FT_New_Memory_Face
     * does NOT copy the bytes -- it keeps a pointer to them and parses
     * tables (e.g. the cmap) lazily, on first use, which can be well after
     * this call returns (verified by a real crash here: destroying
     * `font_source` right after tbox_font_face_load and then rendering a
     * frame segfaults inside FreeType's cmap lookup, reading freed memory).
     * So `font_source` has to outlive `font`, not just tbox_font_face_load
     * -- destroyed together with it below, once nothing will ever
     * rasterize another glyph from this face again. */
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), font);
    if (ctx == NULL) {
        tbox_font_face_destroy(font);
        tbox_font_source_destroy(font_source);
        return false;
    }

    tbox_backend_wayland *backend = tbox_backend_wayland_open(width, height, NULL);
    if (backend == NULL) {
        tbox_context_close(ctx);
        tbox_font_face_destroy(font);
        tbox_font_source_destroy(font_source);
        return false;
    }

    /* v0's simplest possible redraw policy (see ARCHITECTURE.md's
     * "Orchestration / Main Loop" section): recompute and present once for
     * the very first frame, and again only when the window's size changes
     * -- no other invalidation trigger exists yet. */
    bool first          = true;
    int32_t last_width  = 0;
    int32_t last_height = 0;

    while (!tbox_backend_wayland_should_close(backend)) {
        int32_t current_width  = 0;
        int32_t current_height = 0;
        tbox_backend_wayland_size(backend, &current_width, &current_height);

        if (first || current_width != last_width || current_height != last_height) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, (double)current_width, (double)current_height, &list);
            tbox_backend_wayland_present(backend, &list);

            last_width  = current_width;
            last_height = current_height;
            first       = false;
        }

        /* Blocks until the next event (resize, key press, close request,
         * ...) rather than busy-looping: there is nothing to redraw between
         * events under the policy above. A lost connection is treated the
         * same as a normal close -- the loop just exits. */
        if (!tbox_backend_wayland_poll(backend, -1)) {
            break;
        }
    }

    tbox_backend_wayland_destroy(backend);
    tbox_context_close(ctx);
    tbox_font_face_destroy(font);
    tbox_font_source_destroy(font_source);
    return true;
}
