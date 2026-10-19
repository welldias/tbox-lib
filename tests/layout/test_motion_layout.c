#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/context.h>
#include <tbox/output.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "test_support.h"

static const tbox_layout_box *tbox_motion_find(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_html_attribute *a = box->node ? tbox_html_node_get_attribute(box->node, tbox_string_view_from_cstr("id")) : NULL;
        if (a && tbox_string_view_equal_cstr(a->value, id)) return box;
        const tbox_layout_box *found = tbox_motion_find(box->first_child, id);
        if (found) return found;
    }
    return NULL;
}

static int tbox_motion_layout_check(tbox_font_face_cache *fonts, const char *html, const char *css, double x, double y) {
    int failures = 0;
    tbox_html_document *doc = tbox_html_parse(html, strlen(html));
    tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));
    tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
    tbox_arena arena = tbox_arena_create(0);
    tbox_style_table styles = tbox_style_resolve_tree_in_viewport(&arena, tbox_html_document_root(doc), &source, 1, 400, 300);
    const tbox_layout_box *root = tbox_layout_build(&arena, tbox_html_document_root(doc), &styles, fonts, NULL, 400, 300);
    const tbox_layout_box *box = tbox_motion_find(root, "b");
    TBOX_TEST_REQUIRE(box != NULL);
    if (fabs(box->border_box.x - x) > .001 || fabs(box->border_box.y - y) > .001)
        fprintf(stderr, "motion layout: got %.3f %.3f; expected %.3f %.3f: %s\n", box->border_box.x, box->border_box.y, x, y, css);
    TBOX_TEST_ASSERT(!box->has_transform);
    TBOX_TEST_ASSERT(fabs(box->border_box.x - x) < .001 && fabs(box->border_box.y - y) < .001);
    tbox_arena_destroy(&arena);
    tbox_css_stylesheet_destroy(sheet);
    tbox_html_document_destroy(doc);
    return failures;
}

int tbox_test_motion_layout_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *html = "<div id='c'><div><div id='b'></div></div></div>";
    const char *base = "#c{position:relative;margin:20px;width:200px;height:160px;padding:10px;border:5px solid black}"
                       "#b{position:absolute;left:0;top:0;width:40px;height:20px;offset-distance:50%;offset-rotate:0deg}";
    const char *const boxes[] = { "border-box", "padding-box", "content-box", "margin-box" };
    const double expected[][2] = { { 50, 10 }, { 55, 15 }, { 65, 25 }, { 30, -10 } };
    for (size_t i = 0; i < 4; i++) {
        char css[512];
        snprintf(css, sizeof(css), "%s#b{offset-path:path('M0 0 H100') %s}", base, boxes[i]);
        failures += tbox_motion_layout_check(fonts, html, css, expected[i][0], expected[i][1]);
    }
    failures += tbox_motion_layout_check(fonts, html,
        "#c{position:relative;margin:20px;width:200px;height:160px;border-radius:20px}"
        "#b{position:absolute;left:0;top:0;width:40px;height:20px;offset-path:border-box;offset-rotate:0deg}", 20, 10);
    failures += tbox_motion_layout_check(fonts, html,
        "#c{margin:20px;width:200px;height:160px}"
        "#b{position:fixed;left:0;top:0;width:40px;height:20px;offset-path:path('M100 100');offset-rotate:0deg}", 80, 90);
    failures += tbox_motion_layout_check(fonts, html,
        "#c{margin:20px;width:200px;height:160px;translate:30px 0}"
        "#b{position:fixed;left:0;top:0;width:40px;height:20px;offset-path:path('M100 100');offset-rotate:0deg}", 130, 110);
    /* The following sibling retains its flow position when b is moved. */
    failures += tbox_motion_layout_check(fonts, "<div><div id='move'></div><div id='b'></div></div>",
        "#move{width:40px;height:20px;offset-path:path('M150 150');offset-rotate:0deg}#b{width:40px;height:20px}", 0, 20);

    /* End-to-end: the layer, inverse hit-test and raster agree about a
     * rotated element. Its child travels with the layer as well. */
    const char *css = "#c{position:relative;width:300px;height:200px;background:white}"
                      "#b{position:absolute;left:0;top:0;width:40px;height:20px;background:red;"
                      "offset-path:path('M100 100 V180');offset-rotate:auto}"
                      "#child{width:8px;height:8px;background:blue}";
    const char *content = "<div id='c'><div id='b'><div id='child'></div></div></div>";
    tbox_context *ctx = tbox_context_open(content, strlen(content), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 300, 200, &list);
    const tbox_layout_box *hit = tbox_context_hit_test(ctx, 100, 100);
    TBOX_TEST_REQUIRE(hit != NULL);
    const tbox_html_attribute *id = tbox_html_node_get_attribute(hit->node, tbox_string_view_from_cstr("id"));
    TBOX_TEST_ASSERT(id != NULL && tbox_string_view_equal_cstr(id->value, "b"));
    TBOX_TEST_ASSERT(hit->has_transform && fabs(hit->transform[1] - 1.0) < 1e-9);
    bool layer = false;
    for (size_t i = 0; i < list.count; i++)
        layer |= list.items[i].kind == TBOX_PAINT_LAYER_BEGIN && list.items[i].has_transform;
    TBOX_TEST_ASSERT(layer);
    uint32_t *pixels = calloc(300 * 200, sizeof(uint32_t));
    TBOX_TEST_REQUIRE(pixels != NULL);
    tbox_raster_display_list(pixels, 300, 200, &list);
    TBOX_TEST_ASSERT(pixels[100 * 300 + 100] == 0xffff0000u);
    TBOX_TEST_ASSERT(pixels[82 * 300 + 108] == 0xff0000ffu);
    TBOX_TEST_ASSERT(pixels[100 * 300 + 120] == 0xffffffffu);
    free(pixels);
    tbox_context_close(ctx);

    /* Transitions and @keyframes actually drive layout every frame, and
     * the new frame doesn't borrow the previous frame's sample table. */
    content = "<div id='c'><div id='b'></div></div>";
    css = "#c{position:relative;width:300px;height:200px}"
          "#b{position:absolute;width:40px;height:20px;offset-path:path('M50 100 H250');offset-rotate:0deg;transition:offset-distance 2s linear}"
          "#b.go{offset-distance:100%}";
    ctx = tbox_context_open(content, strlen(content), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_context_set_animation_time(ctx, 10);
    tbox_context_run_frame(ctx, 300, 200, &list);
    tbox_html_node *node = tbox_html_document_root(tbox_context_document(ctx))->first_child->first_child;
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, tbox_string_view_from_cstr("class"), tbox_string_view_from_cstr("go"));
    tbox_context_run_frame(ctx, 300, 200, &list);
    tbox_context_set_animation_time(ctx, 11);
    tbox_context_run_frame(ctx, 300, 200, &list);
    hit = tbox_context_hit_test(ctx, 150, 100);
    TBOX_TEST_ASSERT(hit != NULL && hit->node == node && fabs(hit->border_box.x - 130) < .001);
    tbox_context_set_animation_time(ctx, 12);
    tbox_context_run_frame(ctx, 300, 200, &list);
    hit = tbox_context_hit_test(ctx, 250, 100);
    TBOX_TEST_ASSERT(hit != NULL && hit->node == node);
    tbox_context_close(ctx);

    css = "#c{position:relative;width:300px;height:200px}"
          "#b{position:absolute;width:40px;height:20px;offset-path:path('M50 100 H250');animation:move 2s linear forwards}"
          "@keyframes move{from{offset-distance:0%;offset-rotate:0deg}to{offset-distance:100%;offset-rotate:180deg}}";
    ctx = tbox_context_open(content, strlen(content), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_context_set_animation_time(ctx, 10);
    tbox_context_run_frame(ctx, 300, 200, &list);
    tbox_context_set_animation_time(ctx, 11);
    tbox_context_run_frame(ctx, 300, 200, &list);
    hit = tbox_context_hit_test(ctx, 150, 100);
    TBOX_TEST_REQUIRE(hit != NULL);
    id = tbox_html_node_get_attribute(hit->node, tbox_string_view_from_cstr("id"));
    TBOX_TEST_ASSERT(id != NULL && tbox_string_view_equal_cstr(id->value, "b"));
    TBOX_TEST_ASSERT(hit->has_transform && fabs(hit->transform[1] - 1) < 1e-9);
    tbox_context_close(ctx);
    return failures;
}
