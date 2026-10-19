#include <tbox/layout.h>
#include <tbox/render.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "render/tbox_render_internal.h"
#include "test_support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct tbox_grid_expected {
    const char *id;
    double x, y, width, height;
} tbox_grid_expected;

static const tbox_layout_box *tbox_grid_find_box(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        const tbox_html_attribute *attribute = box->node != NULL ? tbox_html_node_get_attribute(box->node, tbox_string_view_from_cstr("id")) : NULL;
        if (attribute != NULL && tbox_string_view_equal_cstr(attribute->value, id))
            return box;
        const tbox_layout_box *found = tbox_grid_find_box(box->first_child, id);
        if (found != NULL)
            return found;
    }
    return NULL;
}

static int tbox_grid_check(tbox_font_face_cache *fonts, const char *html, const char *css, const tbox_grid_expected *expected, size_t count) {
    int failures                   = 0;
    tbox_html_document *doc        = tbox_html_parse(html, strlen(html));
    tbox_css_stylesheet *sheet     = tbox_css_parse(css, strlen(css));
    tbox_arena arena               = tbox_arena_create(0);
    tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
    tbox_style_table styles        = tbox_style_resolve_tree_in_viewport(&arena, tbox_html_document_root(doc), &source, 1, 800, 600);
    const tbox_layout_box *root    = tbox_layout_build(&arena, tbox_html_document_root(doc), &styles, fonts, NULL, 800, 600);
    for (size_t i = 0; i < count; i++) {
        const tbox_layout_box *box = tbox_grid_find_box(root, expected[i].id);
        TBOX_TEST_ASSERT(box != NULL);
        if (box == NULL)
            continue;
        const tbox_rect *rect = &box->border_box;
        bool matches          = fabs(rect->x - expected[i].x) < 1e-6 && fabs(rect->y - expected[i].y) < 1e-6 && fabs(rect->width - expected[i].width) < 1e-6 && fabs(rect->height - expected[i].height) < 1e-6;
        if (!matches)
            fprintf(stderr, "grid #%s: got %.3f %.3f %.3f %.3f; expected %.3f %.3f %.3f %.3f\n", expected[i].id, rect->x, rect->y, rect->width, rect->height, expected[i].x, expected[i].y, expected[i].width, expected[i].height);
        TBOX_TEST_ASSERT(matches);
    }
    const tbox_layout_box *top = tbox_grid_find_box(root, "ztop");
    if (top != NULL) {
        size_t paint_count;
        tbox_render_painted_box *paint = tbox_render_paint_order(root, &paint_count);
        TBOX_TEST_ASSERT(paint != NULL && paint_count > 0);
        if (paint != NULL && paint_count > 0)
            TBOX_TEST_ASSERT(paint[paint_count - 1].box == top);
        free(paint);
    }
    const tbox_layout_box *bl_a = tbox_grid_find_box(root, "bl_a"), *bl_b = tbox_grid_find_box(root, "bl_b");
    if (bl_a != NULL && bl_b != NULL) {
        TBOX_TEST_ASSERT(bl_a->text_run_count > 0 && bl_b->text_run_count > 0);
        if (bl_a->text_run_count > 0 && bl_b->text_run_count > 0) {
            const tbox_layout_text_run *a = &bl_a->text_runs[0], *b = &bl_b->text_runs[0];
            TBOX_TEST_ASSERT(fabs(a->rect.y + tbox_font_face_ascent(a->font) - b->rect.y - tbox_font_face_ascent(b->font)) < 1e-6);
        }
    }
    /* The display-list path must accept the resulting box tree too. */
    tbox_display_list display = tbox_render_build_display_list(&arena, root);
    TBOX_TEST_ASSERT(display.count < 10000);
    tbox_arena_destroy(&arena);
    tbox_css_stylesheet_destroy(sheet);
    tbox_html_document_destroy(doc);
    return failures;
}

int tbox_test_grid_run(tbox_font_face_cache *fonts) {
    int failures     = 0;
    const char *html = "<div id='g'><div id='a'></div><div id='b'></div><div id='c'></div><div id='d'></div></div>";
    {
        const tbox_grid_expected expected[] = {
            { "g", 0,                    0,  400,       50 },
            { "a", 0,                    0,  100,       20 },
            { "b", 110,                  0,  280.0 / 3, 20 },
            { "c", 110 + 280.0 / 3 + 10, 0,  560.0 / 3, 20 },
            { "d", 0,                    30, 100,       20 }
        };
        failures += tbox_grid_check(fonts, html, "#g {display:grid;width:400px;grid-template-columns:100px 1fr 2fr;gap:10px} #g > div {height:20px}", expected, 5);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0,  100, 30 },
            { "b", 110, 0,  210, 70 },
            { "c", 0,   40, 100, 30 },
            { "d", 220, 80, 100, 30 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:320px;grid-template-columns:repeat(3,1fr);grid-auto-rows:30px;gap:10px}#b{grid-column:2 / -1;grid-row:span 2}#d{grid-column:-2;grid-row:3}", expected, 4);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0,  150, 20 },
            { "b", 160, 0,  150, 20 },
            { "c", 0,   30, 150, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:310px;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:10px}#g>div{height:20px}", expected, 3);
    }
    {
        const char *two                = "<div id='g'><div id='a'></div><div id='b'></div></div>";
        const tbox_grid_expected fit[] = {
            { "a", 0,   0, 200, 20 },
            { "b", 210, 0, 200, 20 }
        };
        const tbox_grid_expected fill[] = {
            { "a", 0,   0, 130, 20 },
            { "b", 140, 0, 130, 20 }
        };
        failures += tbox_grid_check(fonts, two, "#g{display:grid;width:410px;grid-template-columns:repeat(auto-fit,minmax(100px,1fr));gap:10px}#g>div{height:20px}", fit, 2);
        failures += tbox_grid_check(fonts, two, "#g{display:grid;width:410px;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));gap:10px}#g>div{height:20px}", fill, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,   300, 30 },
            { "b", 0,  40,  60,  50 },
            { "c", 70, 40,  230, 50 },
            { "d", 0,  100, 300, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:300px;gap:10px;grid-template:'head head' 30px 'side main' 50px 'foot foot' 20px / 60px 1fr}#a{grid-area:head}#b{grid-area:side}#c{grid-area:main}#d{grid-area:foot}", expected, 4);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,  80, 20 },
            { "b", 0,  30, 80, 40 },
            { "c", 90, 0,  80, 20 },
            { "d", 90, 30, 80, 40 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:300px;grid:20px 40px / auto-flow 80px;gap:10px}", expected, 4);
    }
    {
        const tbox_grid_expected dense[] = {
            { "a", 0,   0,  210, 20 },
            { "b", 0,   30, 210, 20 },
            { "c", 220, 0,  100, 20 }
        };
        const tbox_grid_expected sparse[] = {
            { "c", 220, 30, 100, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:320px;grid-template-columns:repeat(3,100px);grid-auto-rows:20px;gap:10px;grid-auto-flow:row dense}#a,#b{grid-column:span 2}", dense, 3);
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:320px;grid-template-columns:repeat(3,100px);grid-auto-rows:20px;gap:10px}#a,#b{grid-column:span 2}", sparse, 1);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 30,  20, 40, 20 },
            { "b", 160, 0,  40, 20 },
            { "c", 260, 40, 40, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:300px;grid-template-columns:repeat(3,100px);grid-auto-rows:60px;place-items:center}#g>div{width:40px;height:20px}#b{place-self:start end}#c{justify-self:end;align-self:end}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0, 50, 20 },
            { "b", 125, 0, 50, 20 },
            { "c", 250, 0, 50, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:300px;grid-template-columns:repeat(3,50px);justify-content:space-between;grid-auto-rows:20px}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 280, 0,  20,  20 },
            { "b", 90,  30, 210, 20 },
            { "c", 0,   60, 80,  20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:300px;grid-template-columns:[start a] 80px [b] 100px [a] 100px [end];grid-auto-rows:20px;gap:10px}#a{grid-column:a 2 / end;width:20px;justify-self:end}#b{grid-column:b / end}#c{grid-column:start / b}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 40,  0, 50, 20 },
            { "b", 0,   0, 30, 20 },
            { "c", 100, 0, 50, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:150px;grid-template-columns:50px 50px;grid-auto-columns:30px;grid-auto-rows:20px;gap:10px}#a{grid-column:1;grid-row:1}#b{grid-column:-4;grid-row:1}#c{grid-column:2;grid-row:1}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 110, 0, 100, 20 },
            { "b", 0,   0, 100, 20 },
            { "c", 220, 0, 100, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:320px;grid-template-columns:repeat(3,1fr);grid-auto-rows:20px;gap:10px}#b{order:-1}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 20,  10, 80,  30 },
            { "b", 110, 0,  100, 50 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;position:relative;width:210px;grid-template-columns:100px 100px;grid-template-rows:50px;gap:10px}#a{position:absolute;grid-area:1/1/2/2;left:20px;right:0;top:10px;height:30px}#b{grid-column:2}#c,#d{display:none}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0, 100, 20 },
            { "b", 110, 0, 100, 20 },
            { "g", 0,   0, 320, 50 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:320px;grid-template-columns:repeat(3,1fr)!important;grid:20px / 10px;gap:10px}#g>div{height:20px}", expected, 3);
    }

    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0, 100, 20 },
            { "b", 100, 0, 50,  20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:150px;grid-template-columns:minmax(100px,1fr) 1fr;grid-auto-rows:20px}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,  50,  50 },
            { "b", 60, 0,  140, 50 },
            { "c", 0,  60, 50,  40 },
            { "d", 60, 60, 140, 40 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:200px;height:100px;grid-template-columns:25% 1fr;grid-template-rows:50% 1fr;gap:10px}", expected, 4);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0, 0,  100, 20 },
            { "b", 0, 20, 100, 30 },
            { "c", 0, 50, 100, 20 },
            { "d", 0, 70, 100, 30 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:100px;grid-template-columns:1fr;grid-auto-rows:20px 30px}", expected, 4);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0, 0,  100, 50  },
            { "b", 0, 50, 100, 50  },
            { "g", 0, 0,  100, 100 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'></div><div id='b'></div></div>", "#g{display:grid;width:100px;min-height:100px;grid-template-rows:1fr 1fr}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0, 0,  100, 20 },
            { "b", 0, 24, 100, 20 },
            { "g", 0, 0,  100, 40 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'></div><div id='b'></div></div>", "#g{display:grid;width:100px;grid-auto-rows:20px;row-gap:10%}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0, 0,  100, 20 },
            { "b", 0, 20, 100, 20 },
            { "g", 0, 0,  100, 40 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'></div><div id='b'></div></div>", "#g{display:grid;width:100px;grid-template-rows:50% 50%}#g>div{height:20px}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0, 0,  100, 20 },
            { "d", 0, 60, 100, 20 },
            { "g", 0, 0,  100, 50 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:100px;max-height:50px;grid-auto-rows:20px}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 120, 0, 20,  20 },
            { "b", 0,   0, 110, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:200px;direction:rtl;grid-template-columns:80px 110px;gap:10px;grid-auto-rows:20px}#a{width:20px;justify-self:end}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0, 60,  20 },
            { "b", 70, 0, 130, 20 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'><span></span> <span></span> <span></span> <span></span></div><div id='b'></div></div>", "#g{display:grid;width:200px;grid-template-columns:fit-content(60px) 1fr;gap:10px}#g>div{height:20px}span{display:inline-block;width:20px;height:10px}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0, 100, 20 },
            { "b", 50, 0, 150, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:200px;grid-template-columns:minmax(auto,50px) 1fr;grid-auto-rows:20px}#a{width:100px}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "g", 0,  0, 110, 20 },
            { "a", 0,  0, 50,  20 },
            { "b", 60, 0, 50,  20 }
        };
        failures += tbox_grid_check(fonts, "<div><div id='g'><div id='a'></div><div id='b'></div></div></div>", "#g{display:inline-grid;grid-template-columns:repeat(2,50px);gap:10px;grid-auto-rows:20px}", expected, 3);
    }
    {
        const tbox_font_face *face          = tbox_font_face_cache_get(fonts, tbox_string_view_make(NULL, 0), false, false, 16);
        double maximum                      = 40 + tbox_font_measure_text(face, tbox_string_view_from_cstr(" "));
        const tbox_grid_expected expected[] = {
            { "a", 0,            0, 20,            20 },
            { "b", 30,           0, maximum,       20 },
            { "c", 40 + maximum, 0, 260 - maximum, 20 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'><span></span> <span></span></div><div id='b'><span></span> <span></span></div><div id='c'></div></div>", "#g{display:grid;width:300px;grid-template-columns:min-content max-content 1fr;gap:10px}#g>div{height:20px}span{display:inline-block;width:20px;height:10px}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,  80, 20 },
            { "b", 40, 20, 80, 20 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'></div><div id='b'></div></div>", "#g{display:grid;width:120px;grid-template-columns:repeat(3,[x] 40px) [x];grid-auto-rows:20px}#a{grid-column:x / span 2 x}#b{grid-column:span 2 x / -1}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0, 40, 20 },
            { "b", 40, 0, 40, 20 }
        };
        failures += tbox_grid_check(fonts, "<div style='font-size:20px;grid-template-columns:2em 2em'><div id='g'><div id='a'></div><div id='b'></div></div></div>", "#g{display:grid;width:80px;font-size:10px;grid-template-columns:inherit;grid-auto-rows:20px}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 40,  0, 20, 20 },
            { "b", 115, 0, 70, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:200px;grid-template-columns:100px 100px;grid-auto-rows:20px}#a{width:20px;margin-left:auto;margin-right:auto}#b{margin:0 15px}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "ztop", 0, 0, 100, 50 },
            { "b",    0, 0, 100, 50 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='ztop'></div><div id='b'></div></div>", "#g{display:grid;width:100px;grid-template:50px / 100px}#g>div{grid-area:1/1}#ztop{z-index:2;background:blue}#b{z-index:1;background:red}", expected, 2);
    }
    failures += tbox_grid_check(fonts, "<div id='g'><p id='bl_a'>A</p><p id='bl_b'>B</p></div>", "#g{display:grid;width:200px;grid-template-columns:100px 100px;align-items:baseline}p{margin:0}#bl_a{font-size:24px}#bl_b{font-size:12px}", NULL, 0);
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0, 80, 20 },
            { "b", 80, 0, 40, 20 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'></div><div id='b'></div></div>", "#g{display:grid;width:120px;grid-template-columns:[main-start] 40px 40px [main-end] 40px;grid-auto-rows:20px}#a{grid-column:main}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,   0, 100, 20 },
            { "b", 100, 0, 100, 20 }
        };
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'><div style='width:300px'></div></div><div id='b'></div></div>", "#g{display:grid;width:200px;grid-template-columns:1fr 1fr;grid-auto-rows:20px}#a{min-width:0}", expected, 2);
        failures += tbox_grid_check(fonts, "<div id='g'><div id='a'><div style='width:300px'></div></div><div id='b'></div></div>", "#g{display:grid;width:200px;grid-template-columns:1fr 1fr;grid-auto-rows:20px}#a{overflow:hidden}", expected, 2);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,  50, 50 },
            { "b", 60, 0,  50, 50 },
            { "c", 0,  60, 50, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:200px;grid-template-rows:repeat(3,20px);grid-auto-columns:50px;gap:10px;grid-auto-flow:column dense}#a,#b{grid-row:span 2}", expected, 3);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  30, 50, 20 },
            { "b", 60, 0,  50, 20 },
            { "c", 0,  0,  50, 20 },
            { "d", 60, 30, 50, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:110px;grid-template:20px 20px / 50px 50px;gap:10px}#a{grid-row:2}#b{grid-area:1/2}", expected, 4);
    }
    {
        const tbox_grid_expected expected[] = {
            { "a", 0,  0,  50, 20 },
            { "b", 50, 0,  50, 20 },
            { "c", 0,  20, 50, 20 }
        };
        failures += tbox_grid_check(fonts, html, "#g{display:grid;width:100px;grid:auto-flow dense 20px / repeat(2,50px)}", expected, 3);
    }
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(display: grid) and (grid-template-columns: repeat(3, minmax(10px, 1fr)))")));
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(display: inline-grid) and (justify-self: center)")));
    return failures;
}
