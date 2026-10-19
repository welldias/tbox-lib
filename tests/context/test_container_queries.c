/* Container queries: `@container`, container-type/-name/container, cq*
 * units and size containment, end to end through tbox_context. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/context.h>
#include <tbox/css_cascade.h>
#include <tbox/css_parser.h>
#include <tbox/layout.h>

#include "base/tbox_string.h"
#include "test_support.h"

static char *cq_read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = size > 0 ? malloc((size_t)size) : NULL;
    if (buffer != NULL && fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        buffer = NULL;
    }
    fclose(file);
    *out_size = buffer != NULL ? (size_t)size : 0;
    return buffer;
}

static bool cq_has_id(const tbox_html_node *node, const char *id) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT)
        return false;
    const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("id", 2));
    return attribute != NULL && attribute->value.size == strlen(id) && memcmp(attribute->value.data, id, attribute->value.size) == 0;
}

static const tbox_layout_box *cq_find(const tbox_layout_box *box, const char *id) {
    for (; box != NULL; box = box->next_sibling) {
        if (cq_has_id(box->node, id))
            return box;
        const tbox_layout_box *child = cq_find(box->first_child, id);
        if (child != NULL)
            return child;
    }
    return NULL;
}

static const tbox_layout_box *cq_box(const tbox_context *ctx, const char *id) {
    const tbox_layout_box *root = tbox_context_hit_test(ctx, 1.0, 1.0);
    while (root != NULL && root->parent != NULL)
        root = root->parent;
    return cq_find(root, id);
}

static bool cq_matches(const char *prelude, const tbox_css_query_container *containers) {
    tbox_css_query_context query = { containers, NULL };
    return tbox_css_cascade_container_matches(tbox_string_view_from_cstr(prelude), &query, 800.0, 600.0);
}

int tbox_test_container_queries_run(void);

int tbox_test_container_queries_run(void) {
    int failures = 0;

    /* Parsing: the rules inside @container keep their prelude, flagged. */
    {
        const char *css = "@media screen { @container card (width > 10px) { p { color: red } } } @container (min-width: 5px) { a { color: blue } }";
        tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(sheet) == 2);
        const tbox_css_ruleset *rules = tbox_css_stylesheet_rulesets(sheet);
        if (rules != NULL && tbox_css_stylesheet_ruleset_count(sheet) == 2) {
            TBOX_TEST_ASSERT(rules[0].media != NULL && rules[0].media->container);
            TBOX_TEST_ASSERT(rules[0].media->parent != NULL && !rules[0].media->parent->container);
            TBOX_TEST_ASSERT(rules[1].media != NULL && rules[1].media->container && rules[1].media->parent == NULL);
        }
        tbox_css_stylesheet_destroy(sheet);
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(container-type: inline-size)")));
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("(container: a / size)")));
    }

    /* The condition evaluator against hand-built containers. */
    {
        tbox_css_query_container outer = { NULL, tbox_string_view_from_cstr("panel side"), TBOX_CSS_CONTAINER_SIZE, 400.0, 200.0, 20.0, 16.0, NULL };
        tbox_css_query_container inner = { &outer, tbox_string_view_from_cstr(""), TBOX_CSS_CONTAINER_INLINE_SIZE, 150.0, 90.0, 10.0, 16.0, NULL };
        TBOX_TEST_ASSERT(cq_matches("(width: 150px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(min-width: 100px)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(max-width: 100px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(100px <= width < 200px)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(150px < width <= 200px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(width > 14em)", &inner));   /* the container's em: 140px */
        TBOX_TEST_ASSERT(!cq_matches("(width > 16em)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(200px > inline-size)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(width)", &inner));
        /* height needs a size container: skips the inline-size one. */
        TBOX_TEST_ASSERT(cq_matches("(height: 200px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(orientation: landscape) and (aspect-ratio: 2)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(aspect-ratio > 3/2)", &inner));
        TBOX_TEST_ASSERT(cq_matches("panel (width > 300px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("side (width > 300px)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("nope (width > 0px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("panel", &inner));
        TBOX_TEST_ASSERT(cq_matches("not (width > 200px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(width > 200px) or (width < 160px)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(width > 100px) and (width > 200px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("((width > 100px) and (width < 200px))", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(width > 100px) and (width < 200px) or (width > 0px)", &inner));
        TBOX_TEST_ASSERT(cq_matches("(width > 900px), (width < 160px)", &inner));
        /* Unknown stays unknown under not; garbage is false. */
        TBOX_TEST_ASSERT(!cq_matches("not (frobs > 1)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(width > 10qux)", &inner));
        TBOX_TEST_ASSERT(!cq_matches("width > 10px", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(width > 10px", &inner));
        TBOX_TEST_ASSERT(!cq_matches("(width > 0px)", NULL));
        /* Not laid out yet: unknown. */
        tbox_css_query_container unmeasured = { NULL, tbox_string_view_from_cstr(""), TBOX_CSS_CONTAINER_INLINE_SIZE, -1.0, -1.0, 16.0, 16.0, NULL };
        TBOX_TEST_ASSERT(!cq_matches("(width >= 0px)", &unmeasured));
        TBOX_TEST_ASSERT(!cq_matches("not (width >= 0px)", &unmeasured));
    }

    size_t font_size = 0;
    char *font_data  = cq_read_file(TBOX_TEST_LIBERATION_SANS_PATH, &font_size);
    TBOX_TEST_ASSERT(font_data != NULL);
    if (font_data == NULL)
        return failures;
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font_data, font_size, font_data, font_size, NULL, NULL, NULL);
    TBOX_TEST_ASSERT(fonts != NULL);

    /* Size queries, cq units, names across an unnamed container, style()
     * and resize, end to end. */
    {
        const char *html =
            "<body><div id='outer'><div id='inner'><div id='a'></div><div id='b'></div><div id='q'></div><div id='s'></div></div></div>"
            "<div id='nobox'></div></body>";
        const char *css =
            "body { margin: 0 } #outer { width: 60%; container: panel / inline-size; --tone: dark }"
            "#inner { width: 200px; container-type: inline-size; padding: 5px }"
            "#a, #b, #q, #s, #nobox { height: 10px; width: 10px }"
            "@container (width > 150px) { #a { width: 50px } }"
            "@container panel (width > 400px) { #b { width: 70px } }"
            "@container style(--tone: dark) { #s { width: 30px } }"
            "#q { width: 25cqi; height: 10cqw }"
            "#nobox { width: 10cqi }";
        tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            const tbox_layout_box *a = cq_box(ctx, "a"), *b = cq_box(ctx, "b"), *q = cq_box(ctx, "q"), *s = cq_box(ctx, "s"), *nobox = cq_box(ctx, "nobox");
            TBOX_TEST_ASSERT(a != NULL && a->border_box.width == 50.0);
            TBOX_TEST_ASSERT(b != NULL && b->border_box.width == 70.0); /* panel is 480px */
            TBOX_TEST_ASSERT(q != NULL && q->border_box.width == 50.0 && q->border_box.height == 20.0);
            /* style() without a name asks the parent (#inner), which
             * inherits --tone from #outer. */
            TBOX_TEST_ASSERT(s != NULL && s->border_box.width == 30.0);
            /* No container: cqi falls back to the small viewport. */
            TBOX_TEST_ASSERT(nobox != NULL && nobox->border_box.width == 80.0);
            const tbox_layout_box *outer = cq_box(ctx, "outer");
            TBOX_TEST_ASSERT(outer != NULL && outer->style->container_type == TBOX_CSS_CONTAINER_INLINE_SIZE && strcmp(outer->style->container_name, "panel") == 0);

            /* A narrower window: the panel drops to 360px. */
            tbox_context_run_frame(ctx, 600.0, 600.0, &list);
            b = cq_box(ctx, "b");
            nobox = cq_box(ctx, "nobox");
            TBOX_TEST_ASSERT(b != NULL && b->border_box.width == 10.0);
            TBOX_TEST_ASSERT(nobox != NULL && nobox->border_box.width == 60.0);
            tbox_context_close(ctx);
        }
    }

    /* Size containment: an inline-size container's shrink-to-fit width
     * ignores its content; a size container's auto height does too. */
    {
        const char *html = "<body><div id='wrap'><div id='ib'><div id='wide'></div></div></div><div id='sized'><div id='tall'></div></div><div id='plain'><div id='tall2'></div></div></body>";
        const char *css =
            "body { margin: 0 } #ib { display: inline-block; container-type: inline-size; padding: 4px }"
            "#wide { width: 300px; height: 10px }"
            "#sized { container-type: size; padding: 3px; min-height: 5px } #tall, #tall2 { height: 100px }";
        tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            const tbox_layout_box *ib = cq_box(ctx, "ib"), *sized = cq_box(ctx, "sized"), *plain = cq_box(ctx, "plain");
            TBOX_TEST_ASSERT(ib != NULL && ib->border_box.width == 8.0);
            TBOX_TEST_ASSERT(sized != NULL && sized->content_box.height == 5.0 && sized->border_box.height == 11.0);
            TBOX_TEST_ASSERT(plain != NULL && plain->content_box.height == 100.0);
            tbox_context_close(ctx);
        }
    }

    /* Shorthand vs longhands, `none`, invalid names, and no containers:
     * nothing in @container applies. */
    {
        const char *html = "<body><div id='x'><div id='y'></div></div><div id='z'></div></body>";
        const char *css =
            "body { margin: 0 } #x { container: a b / size; container-type: inline-size; height: 50px } #z { container-name: none; container-type: bogus }"
            "@container (width > 0px) { #z { width: 5px } }"
            "@container b (width > 0px) { #y { width: 7px } }";
        tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
        TBOX_TEST_ASSERT(ctx != NULL);
        if (ctx != NULL) {
            tbox_display_list list;
            tbox_context_run_frame(ctx, 800.0, 600.0, &list);
            const tbox_layout_box *x = cq_box(ctx, "x"), *y = cq_box(ctx, "y"), *z = cq_box(ctx, "z");
            TBOX_TEST_ASSERT(x != NULL && x->style->container_type == TBOX_CSS_CONTAINER_INLINE_SIZE && strcmp(x->style->container_name, "a b") == 0);
            TBOX_TEST_ASSERT(y != NULL && y->border_box.width == 7.0);
            TBOX_TEST_ASSERT(z != NULL && z->style->container_type == TBOX_CSS_CONTAINER_NORMAL && z->style->container_name[0] == '\0' && z->border_box.width > 5.0);
            tbox_context_close(ctx);
        }
    }

    tbox_font_face_cache_destroy(fonts);
    free(font_data);
    return failures;
}
