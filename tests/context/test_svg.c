#include "base/tbox_path.h"
#include "base/tbox_string.h"
#include "context/tbox_context_internal.h"
#include "svg/tbox_svg_internal.h"
#include "test_support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <tbox/tbox.h>

static tbox_html_node *tbox_svg_test_node(tbox_context *ctx, const char *selector) {
    tbox_css_selector_node_set set = tbox_css_selector_select(tbox_html_document_root(ctx->document), selector, strlen(selector));
    tbox_html_node *node           = set.count == 1 ? (tbox_html_node *)set.items[0] : NULL;
    tbox_css_selector_node_set_destroy(&set);
    return node;
}
static void tbox_svg_test_pixels(uint32_t *pixels, const tbox_display_list *list) {
    for (size_t i = 0; i < 240 * 160; i++)
        pixels[i] = 0xffffffff;
    tbox_raster_display_list(pixels, 240, 160, list);
}
static int tbox_svg_test_geometry(void) {
    int failures     = 0;
    tbox_arena arena = tbox_arena_create(0);
    tbox_vector segments;
    tbox_vector_init(&segments, &arena, sizeof(tbox_path_segment), 0);
    bool closed = false;
    TBOX_TEST_ASSERT(tbox_path_parse(tbox_string_view_from_cstr("M2 3h10v8z m20 0l5 0"), &segments, &closed, .05));
    TBOX_TEST_REQUIRE(segments.length == 6);
    const tbox_path_segment *s = segments.data;
    TBOX_TEST_ASSERT(s[0].move && s[3].close && s[4].move && !closed);
    TBOX_TEST_ASSERT(s[5].x1 == 27 && s[5].y1 == 3);
    segments.length = 0;
    TBOX_TEST_ASSERT(tbox_path_parse(tbox_string_view_from_cstr("M10 58a50 50 0 0 1 100 0"), &segments, &closed, .02));
    s = segments.data;
    TBOX_TEST_REQUIRE(segments.length > 20);
    TBOX_TEST_ASSERT(fabs(s[segments.length - 1].length - 157.0796) < .1);
    TBOX_TEST_ASSERT(s[segments.length - 1].x1 == 110 && !closed);
    segments.length = 0;
    TBOX_TEST_ASSERT(!tbox_path_parse(tbox_string_view_from_cstr("M0 0A2 2 0 2 0 10 10"), &segments, &closed, .05));
    double matrix[6];
    TBOX_TEST_ASSERT(tbox_svg_parse_transform(tbox_string_view_from_cstr("translate(10 20) scale(.5 2)"), matrix));
    TBOX_TEST_ASSERT(matrix[0] == .5 && matrix[3] == 2 && matrix[4] == 10 && matrix[5] == 20);
    TBOX_TEST_ASSERT(!tbox_svg_parse_transform(tbox_string_view_from_cstr("scale(2,)"), matrix));
    tbox_arena_destroy(&arena);
    return failures;
}
int tbox_test_svg_run(tbox_font_face_cache *fonts) {
    int failures      = tbox_svg_test_geometry();
    const char *html  = "<body><svg style='position:absolute;width:0;height:0'><defs>"
                        "<symbol id='tile' viewBox='0 0 10 10'><rect width='10' height='10' fill='currentColor'/></symbol>"
                        "<symbol id='hole' viewBox='0 0 10 10'><path d='M0 0h10v10H0zM3 3h4v4H3z' fill='currentColor' fill-rule='evenodd'/></symbol>"
                        "<g id='cycle'><use href='#cycle'/></g></defs></svg>"
                        "<div class='row'><svg id='red' width='40' height='40' color='red'><use href='#tile'/></svg>"
                        "<svg width='40' height='40' color='blue'><use id='reference' href='#tile'/></svg>"
                        "<svg width='40' height='40' color='green'><use href='#hole'/></svg>"
                        "<svg width='40' height='40'><use href='#missing'/><use href='#cycle'/></svg></div>"
                        "<svg id='paint' width='40' height='40' viewBox='0 0 10 10'><rect id='layer' width='10' height='10' fill='red'/></svg>"
                        "<svg width='40' height='40' viewBox='0 0 10 10'><g fill='red'><rect id='inherit' width='10' height='10' fill='blue'/></g></svg></body>";
    const char *css   = "body{margin:0}svg{display:block}.row{display:flex;gap:0}@layer test{#layer{fill:blue}}#inherit{fill:inherit}";
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 240, 160, &list);
    uint32_t *pixels = malloc(240 * 160 * sizeof(*pixels)), *expected = malloc(240 * 160 * sizeof(*expected));
    TBOX_TEST_REQUIRE(pixels != NULL && expected != NULL);
    tbox_svg_test_pixels(pixels, &list);
    TBOX_TEST_ASSERT(pixels[20 * 240 + 20] == 0xffff0000);
    TBOX_TEST_ASSERT(pixels[20 * 240 + 60] == 0xff0000ff);
    TBOX_TEST_ASSERT(pixels[20 * 240 + 100] == 0xffffffff); /* evenodd hole */
    TBOX_TEST_ASSERT(pixels[5 * 240 + 85] == 0xff008000);
    TBOX_TEST_ASSERT(pixels[20 * 240 + 140] == 0xffffffff); /* missing/cyclic refs */
    TBOX_TEST_ASSERT(pixels[60 * 240 + 20] == 0xff0000ff);  /* @layer beats attribute */
    TBOX_TEST_ASSERT(pixels[100 * 240 + 20] == 0xffff0000); /* CSS-wide group field */
    /* Stable visual hashes across rebuilt frame arenas. */
    tbox_damage_tracker *tracker = tbox_damage_tracker_create();
    TBOX_TEST_REQUIRE(tracker != NULL);
    tbox_damage first = tbox_damage_tracker_update(tracker, &list, 240, 160);
    TBOX_TEST_ASSERT(first.full);
    tbox_context_run_frame(ctx, 240, 160, &list);
    tbox_damage unchanged = tbox_damage_tracker_update(tracker, &list, 240, 160);
    TBOX_TEST_ASSERT(!unchanged.full && unchanged.count == 0);
    /* Changing href must repaint the old solid tile into a ring. */
    tbox_html_node *reference = tbox_svg_test_node(ctx, "#reference");
    TBOX_TEST_REQUIRE(reference != NULL);
    tbox_html_node_set_attribute(ctx->document, reference, tbox_string_view_from_cstr("href"), tbox_string_view_from_cstr("#hole"));
    tbox_context_run_frame(ctx, 240, 160, &list);
    tbox_damage changed = tbox_damage_tracker_update(tracker, &list, 240, 160);
    TBOX_TEST_ASSERT(changed.full || changed.count > 0);
    tbox_raster_display_list_damaged(pixels, 240, 160, &list, &changed);
    tbox_svg_test_pixels(expected, &list);
    TBOX_TEST_ASSERT(memcmp(pixels, expected, 240 * 160 * sizeof(*pixels)) == 0);
    TBOX_TEST_ASSERT(pixels[20 * 240 + 60] == 0xffffffff);
    tbox_damage_tracker_destroy(tracker);
    tbox_context_close(ctx);
    /* Group opacity composites overlapping children once. Transformed SVG
     * lives inside an HTML compositing layer with a non-zero buffer origin. */
    html = "<body><svg width='100' height='100' viewBox='0 0 100 100'><g transform='translate(20 10)' opacity='.5' fill='red'>"
           "<rect width='40' height='40'/><rect x='20' width='40' height='40'/></g></svg></body>";
    css  = "body{margin:0}svg{margin:10px;transform:translate(5px,5px)}";
    ctx  = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_context_run_frame(ctx, 240, 160, &list);
    tbox_svg_test_pixels(pixels, &list);
    TBOX_TEST_ASSERT(pixels[35 * 240 + 45] == pixels[35 * 240 + 65]);
    TBOX_TEST_ASSERT((pixels[35 * 240 + 45] & 0xffffff) == 0xff7f7f);
    tbox_context_close(ctx);
    /* SVG paints without any font cache. Invalid geometry and references
     * neither leak into HTML text layout nor stop valid sibling shapes. */
    html = "<body><svg width='100%' height='60' viewBox='0 0 120 60'>"
           "<path d='M0 0A2 2 0 2 0 10 10' fill='red'/><circle r='-5' fill='red'/>"
           "<svg width='30' height='30' viewBox='0 0 0 10'><rect width='100' height='100' fill='red'/></svg>"
           "<rect x='40' y='10' width='30' height='30' fill='blue'/></svg></body>";
    css  = "body{margin:0}";
    ctx  = tbox_context_open(html, strlen(html), css, strlen(css), NULL, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_context_run_frame(ctx, 240, 160, &list);
    tbox_svg_test_pixels(pixels, &list);
    TBOX_TEST_ASSERT(pixels[25 * 240 + 115] == 0xff0000ff);
    TBOX_TEST_ASSERT(pixels[10 * 240 + 10] == 0xffffffff);
    tbox_context_close(ctx);
    free(pixels);
    free(expected);
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(fill:currentColor)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(stroke-width:-1)")));
    TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("(stroke-dasharray:1,,2)")));
    return failures;
}
