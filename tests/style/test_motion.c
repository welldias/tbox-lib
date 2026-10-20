#include <math.h>
#include <stdio.h>
#include <string.h>

#include <tbox/layout.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "test_support.h"

/* Out-of-line style groups (tbox_style_masks_of, ...) of the styles this
 * file resolves live here, for the whole run. */
static tbox_arena style_arena;

static tbox_style tbox_motion_resolve(const char *css, const tbox_style *parent) {
    tbox_html_document *doc = tbox_html_parse("<div></div>", 11);
    tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));
    tbox_css_computed_style computed = tbox_css_cascade_resolve_stylesheet(sheet, tbox_html_document_root(doc)->first_child);
    tbox_style style = tbox_style_resolve_in_viewport(&style_arena, tbox_html_document_root(doc)->first_child, parent, &computed, 800, 600);
    tbox_css_computed_style_destroy(&computed);
    tbox_css_stylesheet_destroy(sheet);
    tbox_html_document_destroy(doc);
    return style;
}

static int tbox_motion_check(const char *css, double x, double y, double angle, double tolerance) {
    int failures = 0;
    tbox_style style = tbox_motion_resolve(css, NULL);
    tbox_arena arena = tbox_arena_create(0);
    const tbox_rect box = { 30, 40, 40, 20 }, reference = { 0, 0, 400, 200 };
    double offset[6], matrix[6];
    TBOX_TEST_REQUIRE(tbox_style_motion_matrix(&style, box, reference, NULL, &arena, offset));
    tbox_style_transform_matrix(&style, box.x, box.y, box.width, box.height, offset, matrix);
    double actual_x = matrix[0] * 50 + matrix[2] * 50 + matrix[4];
    double actual_y = matrix[1] * 50 + matrix[3] * 50 + matrix[5];
    double actual_angle = atan2(matrix[1], matrix[0]) * 180.0 / 3.14159265358979323846;
    if (fabs(actual_x - x) > tolerance || fabs(actual_y - y) > tolerance || fabs(remainder(actual_angle - angle, 360)) > tolerance)
        fprintf(stderr, "motion: got %.6f %.6f %.6f, expected %.6f %.6f %.6f: %s\n", actual_x, actual_y, actual_angle, x, y, angle, css);
    TBOX_TEST_ASSERT(fabs(actual_x - x) <= tolerance);
    TBOX_TEST_ASSERT(fabs(actual_y - y) <= tolerance);
    TBOX_TEST_ASSERT(fabs(remainder(actual_angle - angle, 360)) <= tolerance);
    tbox_arena_destroy(&arena);
    return failures;
}

int tbox_test_motion_run(void) {
    style_arena = tbox_arena_create(0);
    int failures = 0;
    tbox_style initial = tbox_motion_resolve("div{}", NULL);
    TBOX_TEST_ASSERT(tbox_style_offset_path_of(&initial)->kind == TBOX_STYLE_OFFSET_PATH_NONE && initial.offset_position.mode == 2 && initial.offset_anchor.mode == 1 && initial.offset_rotate.auto_rotate);
    TBOX_TEST_ASSERT(!tbox_style_has_transform(&initial));

    /* All SVG commands, relative/absolute, implicit lineto after moveto,
     * compact numbers/arc flags and SVG's trailing decimal dot. */
    static const struct { const char *path; double x, y; } endpoints[] = {
        { "M 0 0 L 20 10 H 40 V 30 l 10 20 h 10 v 10", 60, 60 },
        { "m 10 20 30 40 10 20", 50, 80 },
        { "M 0 0 C 0 100 100 100 100 0 S 200 -100 200 0", 200, 0 },
        { "m 0 0 c 0 100 100 100 100 0 s 100 -100 100 0", 200, 0 },
        { "M 0 0 Q 50 100 100 0 T 200 0", 200, 0 },
        { "m 0 0 q 50 100 100 0 t 100 0", 200, 0 },
        { "M100 0 A100 100 0 010 100", 0, 100 },
        { "m100 0 a100 100 0 01-100 100", 0, 100 },
        { "M1.e2-.5L.5.5", .5, .5 },
        { "M0 0 L50 0 M100 100 L150 100", 150, 100 },
        { "M17 23", 17, 23 },
        { "M0 0 A0 10 0 0 0 50 0", 50, 0 },
    };
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        char css[512];
        snprintf(css, sizeof(css), "div{offset-path:path('%s');offset-distance:100%%;offset-rotate:0deg}", endpoints[i].path);
        failures += tbox_motion_check(css, endpoints[i].x, endpoints[i].y, 0, 1e-6);
    }
    failures += tbox_motion_check("div{offset-path:path('\\4d 10 20 H100');offset-distance:100%;offset-rotate:0deg}", 100, 20, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M20 80 C120 0 220 160 320 80');offset-distance:50%;offset-rotate:auto}", 170, 80, atan2(120, 300) * 180 / 3.14159265358979323846, .05);
    failures += tbox_motion_check("div{offset-path:path('M20 80 C120 0 220 160 320 80');offset-distance:0%;offset-rotate:auto}", 20, 80, atan2(-80, 100) * 180 / 3.14159265358979323846, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M20 80 C120 0 220 160 320 80');offset-distance:100%;offset-rotate:auto}", 320, 80, atan2(-80, 100) * 180 / 3.14159265358979323846, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M0 0 C0 100 100 100 100 0 S200 -100 200 0');offset-distance:75%;offset-rotate:0deg}", 150, -75, 0, .05);
    failures += tbox_motion_check("div{offset-path:path('M0 0 Q50 100 100 0 T200 0');offset-distance:75%;offset-rotate:auto}", 150, -50, 0, .05);
    failures += tbox_motion_check("div{offset-path:path('M100 0 A100 100 0 0 1 0 100');offset-distance:50%;offset-rotate:auto}", 70.710678, 70.710678, 135, .05);
    failures += tbox_motion_check("div{offset-path:path('M0 0 H100 V100 H0 Z');offset-distance:-25%;offset-rotate:0deg}", 0, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M0 0 H100 V100 H0 z');offset-distance:150%;offset-rotate:0deg}", 100, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M10 20 L110 20');offset-distance:200%;offset-rotate:reverse 30deg}", 110, 20, 210, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M10 20 L110 20');offset-distance:-10px;offset-rotate:0deg}", 10, 20, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M0 0 L100 0');offset-distance:calc(25% + 10px);offset-rotate:0deg}", 35, 0, 0, 1e-6);

    failures += tbox_motion_check("div{offset-path:circle(60px at center);offset-distance:25%;offset-rotate:0deg}", 200, 160, 0, .05);
    failures += tbox_motion_check("div{offset-path:ellipse(100px 50px at center);offset-distance:50%;offset-rotate:0deg}", 100, 100, 0, .05);
    failures += tbox_motion_check("div{offset-path:circle(20px);offset-position:auto;offset-distance:25%;offset-rotate:0deg}", 30, 60, 0, .05);
    failures += tbox_motion_check("div{offset-path:circle(20px);offset-position:25% 10px;offset-rotate:0deg}", 120, 10, 0, .05);
    failures += tbox_motion_check("div{offset-path:circle(20px at 50px 60px);offset-position:25% 10px;offset-rotate:0deg}", 70, 60, 0, .05);
    failures += tbox_motion_check("div{offset-path:inset(10px 20px);offset-distance:25%;offset-rotate:0deg}", 290, 10, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:xywh(10px 20px 100px 100px);offset-distance:25%;offset-rotate:0deg}", 110, 20, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:rect(20px 110px 120px 10px);offset-distance:50%;offset-rotate:0deg}", 110, 120, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:polygon(0 0,100px 0,100px 100px,0 100px);offset-distance:75%;offset-rotate:0deg}", 0, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:inset(0 round 20px);offset-distance:0%;offset-rotate:auto}", 20, 0, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:content-box;offset-distance:25%;offset-rotate:0deg}", 300, 0, 0, 1e-6);

    failures += tbox_motion_check("div{offset-path:ray(45deg closest-side);offset-distance:50%;offset-rotate:auto}", 200 + 50 / sqrt(2), 100 - 50 / sqrt(2), -45, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg closest-corner);offset-distance:100%;offset-rotate:0deg}", 200 + hypot(200,100), 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg farthest-side at 20px 30px);offset-distance:100%;offset-rotate:0deg}", 400, 30, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg farthest-corner at 20px 30px);offset-distance:100%;offset-rotate:0deg}", 20 + hypot(380,170), 30, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(at 20px 30px 90deg sides);offset-distance:100%;offset-rotate:0deg}", 400, 30, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg sides);offset-distance:100%;offset-rotate:0deg}", 400, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg closest-side contain);offset-distance:200%;offset-rotate:0deg}", 280, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg closest-side);offset-distance:-10px;offset-rotate:0deg}", 190, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg closest-side);offset-distance:200%;offset-rotate:0deg}", 400, 100, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:ray(90deg closest-side);offset-position:auto;offset-distance:50%;offset-rotate:0deg}", 45, 40, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M100 100 L200 100');offset-anchor:top left;offset-rotate:0deg}", 120, 110, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M100 100 L200 100');offset-anchor:top left;offset-rotate:90deg}", 90, 120, 90, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M100 100 L200 100');transform-origin:top left;offset-rotate:0deg}", 120, 110, 0, 1e-6);
    failures += tbox_motion_check("div{offset-path:path('M100 100 L200 100');offset-distance:50%;offset-rotate:0deg;translate:10px 20px;scale:2;transform:translateX(5px)}", 270, 170, 0, 1e-6);

    /* Invalid declarations preserve previous winners, including a valid
     * duplicate of the same property. Parsing leaves no borrowed strings. */
    static const char *const invalid[] = {
        "path('L0 0')", "path('M0')", "path('M0 0 C1 2')", "path('M0 0 A1 2 3 2 0 4 5')",
        "path('M0 0 L1e999 2')", "path('M0 0 Z 10 20')", "path('M0,0,')", "path('M0 0 R1 2')",
        "path('M0 0 A-1 1 0 0 1 1 1')", "path('')", "ray()", "ray(45deg contain contain)",
        "ray(45deg closest-side sides)", "ray(45deg at)", "none border-box", "url('#p')", "circle(-1px)", "circle(10px) content-box margin-box"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char css[512];
        snprintf(css, sizeof(css), "div{offset-path:path('M0 0 L100 0');offset-path:%s;offset-distance:25%%;offset-rotate:0deg}", invalid[i]);
        failures += tbox_motion_check(css, 25, 0, 0, 1e-6);
    }
    tbox_style style = tbox_motion_resolve("div{offset-distance:20px!important;offset:top left path('M0 0 L100 0') 50% auto 30deg / top left;offset-anchor:center}", NULL);
    TBOX_TEST_ASSERT(style.offset_distance.value == 20 && style.offset_distance.kind == TBOX_STYLE_LENGTH_PX);
    TBOX_TEST_ASSERT(tbox_style_offset_path_of(&style)->kind == TBOX_STYLE_OFFSET_PATH_SVG && style.offset_position.mode == 0);
    TBOX_TEST_ASSERT(style.offset_rotate.auto_rotate && style.offset_rotate.angle == 30);
    TBOX_TEST_ASSERT(style.offset_anchor.mode == 0 && style.offset_anchor.position[0].value == 50);
    style = tbox_motion_resolve("div{offset-anchor:top left;offset-rotate:80deg;offset-distance:10px;offset:ray(0deg);}", NULL);
    TBOX_TEST_ASSERT(style.offset_anchor.mode == 1 && style.offset_distance.value == 0 && style.offset_rotate.auto_rotate && style.offset_rotate.angle == 0);
    tbox_style parent = style;
    style = tbox_motion_resolve("div{offset:inherit;offset-distance:25%}", &parent);
    TBOX_TEST_ASSERT(tbox_style_offset_path_of(&style)->kind == TBOX_STYLE_OFFSET_PATH_RAY && style.offset_distance.value == 25);
    style = tbox_motion_resolve("div{offset:unset}", &parent);
    TBOX_TEST_ASSERT(tbox_style_offset_path_of(&style)->kind == TBOX_STYLE_OFFSET_PATH_NONE && style.offset_position.mode == 2);
    style = tbox_motion_resolve("div{--p:path('M0 0 L100 0');offset-path:var(--p);offset-distance:1em}", NULL);
    TBOX_TEST_ASSERT(tbox_style_offset_path_of(&style)->kind == TBOX_STYLE_OFFSET_PATH_SVG && style.offset_distance.value == 16);
    style = tbox_motion_resolve("div{width:10px}@supports (offset-path:ray(45deg)){div{width:20px}}@supports (offset-path:url('#p')){div{width:30px}}@supports (offset-path:ray(bogus)){div{width:40px}}", NULL);
    TBOX_TEST_ASSERT(style.width.value == 20);

    /* Animation values interpolate px + %, normalize reverse to auto +
     * 180deg and keep auto/fixed-rotation changes discrete. */
    tbox_style from = tbox_motion_resolve("div{offset-distance:10px;offset-rotate:auto 10deg;offset-anchor:10px 20px;offset-position:20% 30%}", NULL);
    tbox_style to = tbox_motion_resolve("div{offset-distance:50%;offset-rotate:reverse 30deg;offset-anchor:50% 60%;offset-position:40% 50%}", NULL);
    const tbox_style_animatable properties[] = { TBOX_STYLE_ANIM_OFFSET_DISTANCE, TBOX_STYLE_ANIM_OFFSET_ROTATE, TBOX_STYLE_ANIM_OFFSET_ANCHOR_X, TBOX_STYLE_ANIM_OFFSET_ANCHOR_Y, TBOX_STYLE_ANIM_OFFSET_POSITION_X, TBOX_STYLE_ANIM_OFFSET_POSITION_Y };
    for (size_t i = 0; i < 6; i++) {
        double a[TBOX_STYLE_ANIM_VALUE_SIZE], b[TBOX_STYLE_ANIM_VALUE_SIZE], mixed[TBOX_STYLE_ANIM_VALUE_SIZE];
        tbox_style_animatable_read(&from, properties[i], a);
        tbox_style_animatable_read(&to, properties[i], b);
        TBOX_TEST_ASSERT(tbox_style_animatable_interpolable(properties[i], a, b));
        tbox_style_animatable_mix(properties[i], a, b, .5, mixed);
        tbox_style_animatable_write(&initial, properties[i], mixed);
    }
    TBOX_TEST_ASSERT(tbox_style_length_resolve(initial.offset_distance, 200) == 55);
    TBOX_TEST_ASSERT(initial.offset_rotate.auto_rotate && initial.offset_rotate.angle == 110);
    TBOX_TEST_ASSERT(tbox_style_length_resolve(initial.offset_anchor.position[0], 40) == 15);
    TBOX_TEST_ASSERT(initial.offset_position.position[0].value == 30);
    double a[TBOX_STYLE_ANIM_VALUE_SIZE], b[TBOX_STYLE_ANIM_VALUE_SIZE];
    tbox_style_animatable_read(&from, TBOX_STYLE_ANIM_OFFSET_ROTATE, a);
    to.offset_rotate.auto_rotate = false;
    tbox_style_animatable_read(&to, TBOX_STYLE_ANIM_OFFSET_ROTATE, b);
    TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_OFFSET_ROTATE, a, b));
    /* Bounded expressions retain their entire value during discrete
     * keyframe selection, instead of losing their px bounds. */
    to = tbox_motion_resolve("div{offset-anchor:clamp(20px,50%,100px) 0;offset-distance:min(50%,100px)}", NULL);
    tbox_style_animatable_read(&to, TBOX_STYLE_ANIM_OFFSET_ANCHOR_X, b);
    tbox_style_animatable_read(&from, TBOX_STYLE_ANIM_OFFSET_ANCHOR_X, a);
    TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_OFFSET_ANCHOR_X, a, b));
    tbox_style_animatable_write(&initial, TBOX_STYLE_ANIM_OFFSET_ANCHOR_X, b);
    TBOX_TEST_ASSERT(tbox_style_length_resolve(initial.offset_anchor.position[0], 10) == 20);
    TBOX_TEST_ASSERT(tbox_style_length_resolve(initial.offset_anchor.position[0], 1000) == 100);
    tbox_style_animatable_read(&to, TBOX_STYLE_ANIM_OFFSET_DISTANCE, b);
    tbox_style_animatable_read(&from, TBOX_STYLE_ANIM_OFFSET_DISTANCE, a);
    TBOX_TEST_ASSERT(!tbox_style_animatable_interpolable(TBOX_STYLE_ANIM_OFFSET_DISTANCE, a, b));
    tbox_style_animatable_write(&initial, TBOX_STYLE_ANIM_OFFSET_DISTANCE, b);
    TBOX_TEST_ASSERT(tbox_style_length_resolve(initial.offset_distance, 1000) == 100);
    TBOX_TEST_ASSERT(tbox_style_animatable_mask(tbox_string_view_from_cstr("offset")) != 0);
    tbox_arena_destroy(&style_arena);
    return failures;
}
