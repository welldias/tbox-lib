#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tbox/tbox.h>

#include "base/tbox_string.h"
#include "context/tbox_context_internal.h"
#include "test_support.h"

static const tbox_layout_box *tbox_positioned_find(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_html_attribute *a = box->node != NULL ? tbox_html_node_get_attribute(box->node, tbox_string_view_from_cstr("id")) : NULL;
        if (a != NULL && tbox_string_view_equal_cstr(a->value, id)) return box;
        const tbox_layout_box *found = tbox_positioned_find(box->first_child, id);
        if (found != NULL) return found;
    }
    return NULL;
}

int tbox_test_inline_positioned_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *html = "<body><div id='host'><p id='line'>Left<span><span id='floating' class='floating'>Floating</span></span>Right</p>"
        "<p>Before<span id='fixed' class='fixed'>Fixed</span>After</p>"
        "<table><thead><tr><th><span id='hidden' class='hidden'>Options</span></th></tr></thead></table>"
        "</div></body>";
    const char *css = "body{margin:0;font:14px/20px sans-serif}#host{position:relative;width:200px;height:160px;margin:10px}"
        "p{margin:0}.floating{position:absolute;left:20px;top:24px;width:70px;height:20px;color:green}"
        ".fixed{position:fixed;left:260px;top:10px;color:blue}"
        ".hidden{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip-path:inset(50%);white-space:nowrap;border:0;color:red}";
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 320, 200, &list);
    const tbox_layout_box *host = tbox_positioned_find(ctx->root, "host");
    const tbox_layout_box *line = tbox_positioned_find(ctx->root, "line");
    const tbox_layout_box *floating = tbox_positioned_find(ctx->root, "floating");
    const tbox_layout_box *fixed = tbox_positioned_find(ctx->root, "fixed");
    const tbox_layout_box *hidden = tbox_positioned_find(ctx->root, "hidden");
    TBOX_TEST_REQUIRE(host != NULL && line != NULL && floating != NULL && fixed != NULL && hidden != NULL);
    TBOX_TEST_REQUIRE(line->text_run_count > 0);
    TBOX_TEST_ASSERT(line->content_box.height == 20.0);
    for (size_t i = 0; i < line->text_run_count; i++)
        TBOX_TEST_ASSERT(!tbox_string_view_equal_cstr(line->text_runs[i].text, "Floating"));
    TBOX_TEST_ASSERT(fabs(floating->border_box.x - host->padding_box.x - 20.0) < 1e-6);
    TBOX_TEST_ASSERT(fabs(floating->border_box.y - host->padding_box.y - 24.0) < 1e-6);
    TBOX_TEST_ASSERT(fixed->border_box.x == 260.0 && fixed->border_box.y == 10.0);
    TBOX_TEST_ASSERT(hidden->border_box.width == 1.0 && hidden->border_box.height == 1.0);
    /* Render may discard empty clipped operations entirely. */
    bool clipped = true;
    for (size_t i = 0; i < list.count; i++) {
        const tbox_paint_op *op = &list.items[i];
        if (op->kind == TBOX_PAINT_TEXT_RUN && tbox_string_view_equal_cstr(op->text, "Options"))
            clipped = clipped && ((op->has_clip && (op->clip.width <= 0.0 || op->clip.height <= 0.0)) ||
                (op->has_rounded_clip && (op->rounded_clip.width <= 0.0 || op->rounded_clip.height <= 0.0)));
    }
    TBOX_TEST_ASSERT(clipped);
    uint32_t *pixels = calloc(320 * 200, sizeof(*pixels));
    TBOX_TEST_REQUIRE(pixels != NULL);
    tbox_raster_display_list(pixels, 320, 200, &list);
    bool red_ink = false;
    for (size_t i = 0; i < 320 * 200; i++)
        if (((pixels[i] >> 16) & 255) > 200 && ((pixels[i] >> 8) & 255) < 60 && (pixels[i] & 255) < 60)
            red_ink = true;
    TBOX_TEST_ASSERT(!red_ink);
    free(pixels);
    tbox_context_close(ctx);
    return failures;
}
