#include <tbox/context.h>
#include <tbox/output.h>
#include <tbox/render.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "test_support.h"

typedef struct tbox_test_composition_frame {
    tbox_arena arena;
    tbox_html_document *doc;
    tbox_css_stylesheet *sheet;
    tbox_layout_box *box;
    tbox_display_list list;
} tbox_test_composition_frame;

static tbox_test_composition_frame tbox_test_composition_open(const char *html, const char *css, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    tbox_test_composition_frame frame = { 0 };
    frame.arena = tbox_arena_create(0);
    frame.doc = tbox_html_parse(html, strlen(html));
    frame.sheet = tbox_css_parse(css, strlen(css));
    tbox_css_cascade_source source = { frame.sheet, TBOX_CSS_ORIGIN_AUTHOR };
    const tbox_html_node *root = tbox_html_document_root(frame.doc);
    tbox_style_table table = tbox_style_resolve_tree(&frame.arena, root, &source, 1);
    frame.box = tbox_layout_build(&frame.arena, root, &table, fonts, images, 160, 120);
    frame.list = tbox_render_build_display_list_in_viewport(&frame.arena, frame.box, 160, 120);
    return frame;
}

static void tbox_test_composition_close(tbox_test_composition_frame *frame) {
    tbox_arena_destroy(&frame->arena);
    tbox_css_stylesheet_destroy(frame->sheet);
    tbox_html_document_destroy(frame->doc);
}

static void tbox_test_composition_paint(uint32_t pixels[160 * 120], const tbox_display_list *list) {
    for (size_t i = 0; i < 160 * 120; i++) pixels[i] = 0xFFFFFFFFu;
    tbox_raster_display_list(pixels, 160, 120, list);
}

static bool tbox_test_composition_damage_contains(const tbox_damage *damage, double x, double y) {
    if (damage->full) return true;
    for (size_t i = 0; i < damage->count; i++) {
        tbox_rect r = damage->rects[i];
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return true;
    }
    return false;
}

int tbox_test_composition_run(const void *font_data, size_t font_size) {
    int failures = 0;
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
    tbox_image_cache *images = tbox_image_cache_create(TBOX_TEST_ASSETS_DIR);
    TBOX_TEST_REQUIRE(fonts != NULL && images != NULL);
    uint32_t pixels[160 * 120], full[160 * 120];
    static const char *const operators[] = { "add", "subtract", "intersect", "exclude", "source-over", "source-out", "source-in", "xor" };
    static const bool kept[4][4] = {
        { true, true, true, false }, { true, false, false, false },
        { false, true, false, false }, { true, false, true, false }
    };
    /* End-to-end coverage for every standard and legacy operator, using
     * both decoded images and gradients. Includes damaged/full parity. */
    for (size_t op = 0; op < 8; op++) {
        for (int image = 0; image < 2; image++) {
            char css[900];
            const char *prefix = op < 4 ? "" : "-webkit-";
            snprintf(css, sizeof(css), "div { margin:10px; width:40px; height:20px; background:red;"
                " %smask-image:%s,linear-gradient(black,black); %smask-size:20px 20px;"
                " %smask-position:0 0,10px 0; %smask-repeat:no-repeat; %smask-composite:%s; }",
                prefix, image ? "url(black.png)" : "linear-gradient(black,black)", prefix, prefix, prefix, prefix, operators[op]);
            tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>", css, fonts, images);
            TBOX_TEST_REQUIRE(frame.box != NULL && frame.box->style != NULL);
            TBOX_TEST_ASSERT(frame.box->style->mask_layer_count == 2);
            TBOX_TEST_ASSERT(frame.box->style->mask_layers[0].composite == (tbox_style_mask_composite)(op % 4));
            tbox_test_composition_paint(pixels, &frame.list);
            for (size_t x = 0; x < 4; x++)
                TBOX_TEST_ASSERT(pixels[15 * 160 + 15 + 10 * x] == (kept[op % 4][x] ? 0xFFFF0000u : 0xFFFFFFFFu));
            tbox_damage_tracker *tracker = tbox_damage_tracker_create();
            TBOX_TEST_REQUIRE(tracker != NULL);
            tbox_damage_tracker_update(tracker, &frame.list, 160, 120);
            for (size_t i = 0; i < frame.list.count; i++)
                if (frame.list.items[i].kind == TBOX_PAINT_LAYER_MASK)
                    frame.list.items[i].mask_composite = op % 4 == 3 ? TBOX_STYLE_MASK_INTERSECT : TBOX_STYLE_MASK_EXCLUDE;
            tbox_damage damage = tbox_damage_tracker_update(tracker, &frame.list, 160, 120);
            TBOX_TEST_ASSERT(tbox_test_composition_damage_contains(&damage, 25, 15));
            tbox_raster_display_list_damaged(pixels, 160, 120, &frame.list, &damage);
            tbox_test_composition_paint(full, &frame.list);
            TBOX_TEST_ASSERT(memcmp(pixels, full, sizeof(pixels)) == 0);
            tbox_damage_tracker_destroy(tracker);
            tbox_test_composition_close(&frame);
        }
    }
    /* Continuous alpha, rather than boolean set operations. */
    for (size_t op = 0; op < 4; op++) {
        char css[700];
        snprintf(css, sizeof(css), "div { width:40px;height:20px;background:red;mask-image:"
            "linear-gradient(rgba(0,0,0,.5),rgba(0,0,0,.5)),linear-gradient(rgba(0,0,0,.5),rgba(0,0,0,.5));mask-composite:%s; }", operators[op]);
        tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>", css, fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        const int green[] = { 64, 191, 191, 128 };
        TBOX_TEST_ASSERT(abs((int)((pixels[5 * 160 + 5] >> 8) & 255u) - green[op]) <= 2);
        tbox_test_composition_close(&frame);
    }
    /* A transparent `none` stays in the list; independent mode lists
     * convert each layer before compositing. Invalid lists are ignored. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>",
            "div{width:40px;height:20px;background:red;mask-image:none,linear-gradient(white,white),linear-gradient(black,black);"
            "mask-mode:alpha,luminance;mask-composite:intersect,add;mask-size:20px,30px;mask-repeat:no-repeat;}", fonts, images);
        TBOX_TEST_ASSERT(frame.box->style->mask_layer_count == 3);
        TBOX_TEST_ASSERT(!frame.box->style->mask_layers[2].luminance);
        TBOX_TEST_ASSERT(frame.box->style->mask_layers[2].source.size[0].value == 20);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[5 * 160 + 5] == 0xFFFFFFFFu);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<div></div>",
            "div{width:40px;height:20px;background:red;mask:linear-gradient(black,black) no-repeat 0 0 / 20px 20px subtract,"
            "linear-gradient(black,black) no-repeat 10px 0 / 20px 20px;mask-composite:bogus;mask-mode:alpha,bogus;}", fonts, images);
        TBOX_TEST_ASSERT(frame.box->style->mask_layers[0].composite == TBOX_STYLE_MASK_SUBTRACT);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[5 * 160 + 5] == 0xFFFF0000u && pixels[5 * 160 + 15] == 0xFFFFFFFFu);
        tbox_test_composition_close(&frame);
    }
    /* Per-layer origin/clip: the first source fills padding, the second
     * fills content; subtract removes precisely that inner rectangle. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>",
            "div{margin:10px;width:30px;height:20px;padding:5px;border:5px solid red;background:red;"
            "mask-image:linear-gradient(white,white),linear-gradient(white,white);"
            "mask-origin:padding-box,content-box;mask-clip:padding-box,content-box;"
            "mask-repeat:no-repeat;mask-composite:subtract;}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[12 * 160 + 12] == 0xFFFFFFFFu);
        TBOX_TEST_ASSERT(pixels[17 * 160 + 17] == 0xFFFF0000u);
        TBOX_TEST_ASSERT(pixels[25 * 160 + 25] == 0xFFFFFFFFu);
        tbox_test_composition_close(&frame);
    }
    /* Cascade precedence across aliases and reset through the shorthand. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>",
            "div{mask-image:linear-gradient(black,black),linear-gradient(white,white);"
            "-webkit-mask-composite:xor!important;mask-composite:subtract;"
            "-webkit-box-reflect:below 4px;box-reflect:right 10%;"
            "-webkit-box-decoration-break:clone;box-decoration-break:slice;}", fonts, images);
        TBOX_TEST_ASSERT(frame.box->style->mask_layers[0].composite == TBOX_STYLE_MASK_EXCLUDE);
        TBOX_TEST_ASSERT(frame.box->style->box_reflect.direction == TBOX_STYLE_REFLECT_RIGHT);
        TBOX_TEST_ASSERT(frame.box->style->box_reflect.offset.kind == TBOX_STYLE_LENGTH_PERCENT);
        TBOX_TEST_ASSERT(!frame.box->style->box_decoration_clone);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<div></div>",
            "div{mask-composite:exclude;mask-size:1px;mask:linear-gradient(white,white);box-reflect:below banana;box-decoration-break:banana;}", fonts, images);
        TBOX_TEST_ASSERT(frame.box->style->mask_layers[0].composite == TBOX_STYLE_MASK_ADD);
        TBOX_TEST_ASSERT(frame.box->style->mask_layers[0].source.size[0].kind == TBOX_STYLE_LENGTH_AUTO);
        TBOX_TEST_ASSERT(frame.box->style->box_reflect.direction == TBOX_STYLE_REFLECT_NONE && !frame.box->style->box_decoration_clone);
        tbox_test_composition_close(&frame);
    }
    /* Invalid declarations preserve earlier valid values. CSS-wide
     * keywords copy only the requested longhand; ordinary values do not inherit. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<div><p></p></div>",
            "div{mask-image:linear-gradient(white,white),linear-gradient(black,black);mask-composite:exclude;box-reflect:below 4px;box-decoration-break:clone;}"
            "p{mask-image:inherit;mask-size:10px;mask-composite:inherit;box-reflect:inherit;box-decoration-break:inherit;mask-composite:banana;box-reflect:banana;box-decoration-break:banana;}", fonts, images);
        TBOX_TEST_REQUIRE(frame.box->first_child != NULL);
        const tbox_style *child = frame.box->first_child->style;
        TBOX_TEST_ASSERT(child->mask_layer_count == 2 && child->has_mask);
        TBOX_TEST_ASSERT(child->mask_layers[0].composite == TBOX_STYLE_MASK_EXCLUDE);
        TBOX_TEST_ASSERT(child->mask_layers[0].source.size[0].value == 10);
        TBOX_TEST_ASSERT(child->box_reflect.direction == TBOX_STYLE_REFLECT_BELOW && child->box_decoration_clone);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<div><p></p></div>",
            "div{box-reflect:below;box-decoration-break:clone;}p{width:10px;height:10px;background:blue;}", fonts, images);
        TBOX_TEST_ASSERT(frame.box->first_child->style->box_reflect.direction == TBOX_STYLE_REFLECT_NONE);
        TBOX_TEST_ASSERT(!frame.box->first_child->style->box_decoration_clone);
        tbox_test_composition_close(&frame);
    }
    /* Reflections keep layout geometry and mirror the already-rendered
     * child, in all directions. A changed source damages both copies. */
    static const char *const directions[] = { "above", "below", "left", "right" };
    const int rx[] = { 45, 45, 33, 79 }, ry[] = { 33, 79, 45, 45 };
    for (size_t d = 0; d < 4; d++) {
        char css[500];
        snprintf(css, sizeof(css), "div{margin:40px;width:20px;height:20px;background:red;box-reflect:%s 4px;}"
            "i{display:block;width:10px;height:10px;background:blue;}", directions[d]);
        tbox_test_composition_frame frame = tbox_test_composition_open("<div><i></i></div>", css, fonts, images);
        TBOX_TEST_ASSERT(frame.box->border_box.x == 40 && frame.box->border_box.y == 40 && frame.box->border_box.height == 20);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[45 * 160 + 45] == 0xFF0000FFu);
        TBOX_TEST_ASSERT(pixels[ry[d] * 160 + rx[d]] == 0xFF0000FFu);
        tbox_damage_tracker *tracker = tbox_damage_tracker_create();
        TBOX_TEST_REQUIRE(tracker != NULL);
        tbox_damage_tracker_update(tracker, &frame.list, 160, 120);
        for (size_t i = 0; i < frame.list.count; i++)
            if (frame.list.items[i].kind == TBOX_PAINT_FILL_RECT && frame.list.items[i].color.b == 255)
                frame.list.items[i].color = (tbox_css_rgba){ 0, 128, 0, 255 };
        tbox_damage damage = tbox_damage_tracker_update(tracker, &frame.list, 160, 120);
        TBOX_TEST_ASSERT(tbox_test_composition_damage_contains(&damage, rx[d], ry[d]));
        TBOX_TEST_ASSERT(tbox_test_composition_damage_contains(&damage, 45, 45));
        tbox_raster_display_list_damaged(pixels, 160, 120, &frame.list, &damage);
        tbox_test_composition_paint(full, &frame.list);
        TBOX_TEST_ASSERT(memcmp(pixels, full, sizeof(pixels)) == 0);
        tbox_damage_tracker_destroy(tracker);
        tbox_test_composition_close(&frame);
    }
    /* A nested reflected element owns positioned descendants too. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<section><div><i></i></div></section>",
            "section{width:100px;height:100px;}div{margin:10px;position:relative;width:20px;height:20px;box-reflect:below 4px;}"
            "i{position:absolute;left:0;top:0;width:10px;height:10px;background:blue;}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[15 * 160 + 15] == 0xFF0000FFu && pixels[49 * 160 + 15] == 0xFF0000FFu);
        tbox_test_composition_close(&frame);
    }
    /* Reflected gradient fades toward its far edge; URL masks, ancestor
     * clipping, transforms and negative offsets use the same group path. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<div></div>",
            "div{margin:10px;width:40px;height:20px;background:red;-webkit-box-reflect:below 4px linear-gradient(transparent,rgba(0,0,0,.4));}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        unsigned near = (pixels[35 * 160 + 15] >> 8) & 255u, far = (pixels[51 * 160 + 15] >> 8) & 255u;
        TBOX_TEST_ASSERT(near > 145 && near < far && far > 230);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<div></div>",
            "div{margin:10px;width:40px;height:20px;background:red;box-reflect:below 4px url(black.png);transform:scale(1.5);transform-origin:top left;}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[55 * 160 + 15] == 0xFFFF0000u);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<section><div></div></section>",
            "section{width:50px;height:25px;overflow:hidden;}div{width:40px;height:20px;background:red;box-reflect:below 0;}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[22 * 160 + 5] == 0xFFFF0000u && pixels[27 * 160 + 5] == 0xFFFFFFFFu);
        tbox_test_composition_close(&frame);
        frame = tbox_test_composition_open("<div><i></i></div>",
            "div{margin:10px;width:20px;height:20px;background:red;box-reflect:below -20px;}i{display:block;width:20px;height:10px;background:blue;}", fonts, images);
        tbox_test_composition_paint(pixels, &frame.list);
        TBOX_TEST_ASSERT(pixels[15 * 160 + 15] == 0xFF0000FFu && pixels[25 * 160 + 15] == 0xFFFF0000u);
        tbox_test_composition_close(&frame);
    }
    /* Each clone fragment has its own rounded border, padding, restarted
     * gradient and box shadow. Slice suppresses the two internal ends. */
    for (int clone = 0; clone < 2; clone++) {
        char css[600];
        snprintf(css, sizeof(css), "p{margin:10px;width:90px;font-size:16px;line-height:32px;}"
            "span{display:inline;padding:2px 6px;border:2px solid blue;border-radius:8px;"
            "background:linear-gradient(red,yellow);box-shadow:2px 2px 0 green;%sbox-decoration-break:%s;}",
            clone ? "-webkit-" : "", clone ? "clone" : "slice");
        tbox_test_composition_frame frame = tbox_test_composition_open("<p><span>word word word word word word</span></p>", css, fonts, images);
        TBOX_TEST_REQUIRE(frame.box != NULL && frame.box->text_run_count >= 3);
        const tbox_layout_text_run *runs = frame.box->text_runs;
        TBOX_TEST_ASSERT(runs[0].rect.x == 18);
        TBOX_TEST_ASSERT(runs[1].rect.x == (clone ? 18 : 10));
        size_t gradients = 0, rings = 0, shadows = 0;
        for (size_t i = 0; i < frame.list.count; i++) {
            const tbox_paint_op *op = &frame.list.items[i];
            if (op->kind == TBOX_PAINT_GRADIENT) gradients++;
            if (op->kind == TBOX_PAINT_FILL_RECT && op->color.g == 128 && op->color.r == 0) shadows++;
            if (op->kind == TBOX_PAINT_FILL_RING && op->color.b == 255) {
                rings++;
                if (clone && rings == 2) {
                    TBOX_TEST_ASSERT(op->corner_radii[0] == (clone ? 8 : 0));
                    TBOX_TEST_ASSERT(op->corner_radii[1] == (clone ? 8 : 0));
                    TBOX_TEST_ASSERT(op->rect.x == 10);
                }
            }
        }
        TBOX_TEST_ASSERT(gradients >= frame.box->text_run_count && shadows == frame.box->text_run_count);
        TBOX_TEST_ASSERT(rings == (clone ? frame.box->text_run_count : 2));
        tbox_test_composition_paint(pixels, &frame.list);
        int y = (int)(runs[1].rect.y + runs[1].rect.height / 2);
        TBOX_TEST_ASSERT(pixels[y * 160 + 10] == (clone ? 0xFF0000FFu : pixels[y * 160 + 11]));
        tbox_test_composition_close(&frame);
    }
    /* Clone reserves the end padding before distributing justified gaps. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<p><span>word word word word word word</span></p>",
            "p{margin:10px;width:100px;font-size:16px;text-align:justify;}span{display:inline;box-decoration-break:clone;padding:0 6px;border:2px solid blue;background-image:url(black.png);}", fonts, images);
        TBOX_TEST_REQUIRE(frame.box->text_run_count >= 2);
        const tbox_layout_text_run *run = &frame.box->text_runs[0];
        TBOX_TEST_ASSERT(fabs(run->rect.x + run->rect.width + 8 - 110) < 0.01);
        TBOX_TEST_ASSERT(run->decoration_images[0] != NULL);
        tbox_test_composition_close(&frame);
    }
    /* Reordered RTL lines reserve decoration at visual fragment ends,
     * while retaining logical text and the reversed glyph clusters. */
    for (int clone = 0; clone < 2; clone++) {
        char css[500];
        snprintf(css, sizeof(css), "p{margin:10px;width:90px;font-size:16px;line-height:32px;text-align:left;}"
            "bdo{display:inline;padding:0 6px;border:2px solid blue;border-radius:8px;background:red;box-decoration-break:%s;}", clone ? "clone" : "slice");
        tbox_test_composition_frame frame = tbox_test_composition_open("<p><bdo dir=rtl>word word word word word word</bdo></p>", css, fonts, images);
        TBOX_TEST_REQUIRE(frame.box != NULL && frame.box->text_run_count == 3);
        const tbox_layout_text_run *runs = frame.box->text_runs;
        TBOX_TEST_ASSERT(runs[0].glyph_count > 1);
        if (runs[0].glyph_count > 1)
            TBOX_TEST_ASSERT(runs[0].glyphs[0].cluster > runs[0].glyphs[runs[0].glyph_count - 1].cluster);
        TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(runs[0].text, "word word"));
        TBOX_TEST_ASSERT(runs[0].rect.x == (clone ? 18 : 10));
        TBOX_TEST_ASSERT(runs[1].rect.x == (clone ? 18 : 10));
        TBOX_TEST_ASSERT(runs[2].rect.x == 18); /* slice's final RTL fragment keeps the left end */
        tbox_test_composition_paint(pixels, &frame.list);
        for (size_t r = 0; r < 3; r++) {
            int y = (int)(runs[r].rect.y + runs[r].rect.height / 2);
            TBOX_TEST_ASSERT((pixels[y * 160 + 10] == 0xFF0000FFu) == (clone || r == 2));
        }
        tbox_test_composition_close(&frame);
    }
    /* A leading float is not a fragment boundary. Both LTR and reordered
     * RTL text keep their padding within the narrowed first two lines. */
    for (int rtl = 0; rtl < 2; rtl++) {
        for (int clone = 0; clone < 2; clone++) {
            char css[600];
            snprintf(css, sizeof(css), "p{margin:0;width:150px;font-size:16px;line-height:32px;text-align:justify;}"
                "i{float:left;width:60px;height:64px;background:green;}"
                "span,bdo{display:inline;padding:0 6px;border:2px solid blue;border-radius:8px;background:red;box-decoration-break:%s;}", clone ? "clone" : "slice");
            const char *html = rtl ? "<p><i></i><bdo dir=rtl>word word word word word word</bdo></p>" :
                "<p><i></i><span>word word word word word word</span></p>";
            tbox_test_composition_frame frame = tbox_test_composition_open(html, css, fonts, images);
            TBOX_TEST_REQUIRE(frame.box != NULL && frame.box->first_child != NULL && frame.box->text_run_count == 3);
            const tbox_layout_text_run *runs = frame.box->text_runs;
            TBOX_TEST_ASSERT(frame.box->first_child->border_box.x == 0 && frame.box->first_child->border_box.width == 60);
            TBOX_TEST_ASSERT(runs[0].rect.x == (clone || !rtl ? 68 : 60));
            TBOX_TEST_ASSERT(runs[1].rect.x == (clone ? 68 : 60));
            TBOX_TEST_ASSERT(runs[2].rect.x == (clone || rtl ? 8 : 0));
            TBOX_TEST_ASSERT(fabs(runs[0].rect.x + runs[0].rect.width + (clone || rtl ? 8 : 0) - 150) < 0.01);
            TBOX_TEST_ASSERT(fabs(runs[1].rect.x + runs[1].rect.width + (clone ? 8 : 0) - 150) < 0.01);
            TBOX_TEST_ASSERT(runs[2].line_y >= 64);
            tbox_test_composition_paint(pixels, &frame.list);
            int y = (int)(runs[1].rect.y + runs[1].rect.height / 2);
            TBOX_TEST_ASSERT((pixels[y * 160 + 60] == 0xFF0000FFu) == (clone != 0));
            tbox_test_composition_close(&frame);
        }
    }
    /* Word alone would fit in 40px, but its padding/border do not: the
     * line must descend below the 40px-high float before it is placed. */
    {
        tbox_test_composition_frame frame = tbox_test_composition_open("<p><i></i><span>word</span></p>",
            "p{margin:0;width:100px;font-size:16px;line-height:32px;}i{float:left;width:60px;height:40px;}"
            "span{display:inline;box-decoration-break:clone;padding:0 6px;border:2px solid blue;}", fonts, images);
        TBOX_TEST_REQUIRE(frame.box != NULL && frame.box->text_run_count == 1);
        TBOX_TEST_ASSERT(frame.box->text_runs[0].line_y >= 40 && frame.box->text_runs[0].rect.x == 8);
        tbox_test_composition_close(&frame);
    }
    /* Feature detection agrees with the values the new parsers accept. */
    static const char *const supported[] = {
        "(mask-composite: add, subtract, intersect, exclude)", "(-webkit-mask-composite: source-over, source-out, source-in, xor)",
        "(mask: radial-gradient(black,transparent),linear-gradient(black,white))", "(mask-origin: padding-box, content-box)",
        "(-webkit-mask-clip: no-clip)", "(mask: linear-gradient(white,white) no-clip content-box)", "(box-reflect: below 0)", "(box-reflect: below 4px linear-gradient(transparent,black))", "(-webkit-box-reflect: right 10% url(black.png))",
        "(box-decoration-break: clone)", "(-webkit-box-decoration-break: slice)"
    };
    static const char *const unsupported[] = {
        "(mask-composite: source-over)", "(-webkit-mask-composite: subtract)", "(mask-composite: add,banana)",
        "(mask-clip: banana)", "(mask-mode: alpha,banana)", "(box-reflect: upside 4px)", "(box-reflect: below auto)",
        "(box-reflect: below 4px banana)", "(box-decoration-break: banana)"
    };
    for (size_t i = 0; i < sizeof(supported) / sizeof(supported[0]); i++)
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr(supported[i])));
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++)
        TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr(unsupported[i])));
    tbox_image_cache_destroy(images);
    tbox_font_face_cache_destroy(fonts);
    return failures;
}
