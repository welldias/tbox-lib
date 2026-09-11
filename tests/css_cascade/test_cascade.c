#include <tbox/css_cascade.h>

#include <stdio.h>
#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

static tbox_string_view view_cstr(const char *s) {
    return tbox_string_view_make(s, strlen(s));
}

static tbox_html_document *parse_html_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

static tbox_css_stylesheet *parse_css_cstr(const char *css) {
    return tbox_css_parse(css, strlen(css));
}

static const tbox_css_resolved_declaration *find_cstr(const tbox_css_computed_style *style, const char *property) {
    return tbox_css_computed_style_find(style, view_cstr(property));
}

int tbox_test_css_cascade_run(void) {
    int failures = 0;

    /* 1: distinct properties from different rulesets coexist in the result. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p class=\"lead\">x</p>");
        const tbox_html_node *p    = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("p { color: red; } .lead { font-weight: bold; }");

        tbox_css_computed_style style = tbox_css_cascade_resolve_stylesheet(sheet, p);
        TBOX_TEST_ASSERT(style.count == 2);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "red"));
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "font-weight")->value, "bold"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 2: same property, different specificity (ID beats type), in both source orders. */
    {
        tbox_html_document *doc = parse_html_cstr("<p id=\"x\">a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *sheet_a   = parse_css_cstr("p { color: red; } #x { color: blue; }");
        tbox_css_computed_style style_a = tbox_css_cascade_resolve_stylesheet(sheet_a, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style_a, "color")->value, "blue"));
        tbox_css_computed_style_destroy(&style_a);
        tbox_css_stylesheet_destroy(sheet_a);

        tbox_css_stylesheet *sheet_b   = parse_css_cstr("#x { color: blue; } p { color: red; }");
        tbox_css_computed_style style_b = tbox_css_cascade_resolve_stylesheet(sheet_b, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style_b, "color")->value, "blue"));
        tbox_css_computed_style_destroy(&style_b);
        tbox_css_stylesheet_destroy(sheet_b);

        tbox_html_document_destroy(doc);
    }

    /* 3: same specificity ties by source order, between rulesets and within one ruleset. */
    {
        tbox_html_document *doc = parse_html_cstr("<p>a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *between   = parse_css_cstr("p { color: red; } p { color: blue; }");
        tbox_css_computed_style style1 = tbox_css_cascade_resolve_stylesheet(between, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style1, "color")->value, "blue"));
        tbox_css_computed_style_destroy(&style1);
        tbox_css_stylesheet_destroy(between);

        tbox_css_stylesheet *within    = parse_css_cstr("p { color: red; color: green; }");
        tbox_css_computed_style style2 = tbox_css_cascade_resolve_stylesheet(within, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style2, "color")->value, "green"));
        tbox_css_computed_style_destroy(&style2);
        tbox_css_stylesheet_destroy(within);

        tbox_html_document_destroy(doc);
    }

    /* 4: !important beats higher specificity, whichever position it's declared in. */
    {
        tbox_html_document *doc = parse_html_cstr("<p id=\"x\">a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *sheet_a   = parse_css_cstr("#x { color: blue; } p { color: red !important; }");
        tbox_css_computed_style style_a = tbox_css_cascade_resolve_stylesheet(sheet_a, p);
        const tbox_css_resolved_declaration *color_a = find_cstr(&style_a, "color");
        TBOX_TEST_ASSERT(color_a != NULL && text_eq(color_a->value, "red") && color_a->important);
        tbox_css_computed_style_destroy(&style_a);
        tbox_css_stylesheet_destroy(sheet_a);

        tbox_css_stylesheet *sheet_b   = parse_css_cstr("p { color: red !important; color: blue; }");
        tbox_css_computed_style style_b = tbox_css_cascade_resolve_stylesheet(sheet_b, p);
        const tbox_css_resolved_declaration *color_b = find_cstr(&style_b, "color");
        TBOX_TEST_ASSERT(color_b != NULL && text_eq(color_b->value, "red") && color_b->important);
        tbox_css_computed_style_destroy(&style_b);
        tbox_css_stylesheet_destroy(sheet_b);

        tbox_html_document_destroy(doc);
    }

    /* 5: a comma-separated branch's own specificity decides, not the first branch of the group. */
    {
        tbox_html_document *doc    = parse_html_cstr("<h1 class=\"title\">a</h1>");
        const tbox_html_node *h1   = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("h1 { color: red; } span, h1.title { color: blue; }");

        tbox_css_computed_style style = tbox_css_cascade_resolve_stylesheet(sheet, h1);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "blue"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 6: :first-child/:last-child contribute to specificity bucket b. */
    {
        tbox_html_document *doc     = parse_html_cstr("<ul><li>a</li><li>b</li></ul>");
        const tbox_html_node *ul    = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *li1   = ul->first_child;
        const tbox_html_node *li2   = li1->next_sibling;
        tbox_css_stylesheet *sheet  = parse_css_cstr("li { color: black; } li:first-child { color: red; }");

        tbox_css_computed_style style1 = tbox_css_cascade_resolve_stylesheet(sheet, li1);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style1, "color")->value, "red"));
        tbox_css_computed_style_destroy(&style1);

        tbox_css_computed_style style2 = tbox_css_cascade_resolve_stylesheet(sheet, li2);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style2, "color")->value, "black"));
        tbox_css_computed_style_destroy(&style2);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 7: the universal selector contributes no specificity at all. */
    {
        tbox_html_document *doc     = parse_html_cstr("<p>a</p><span>b</span>");
        const tbox_html_node *p     = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *span  = p->next_sibling;
        tbox_css_stylesheet *sheet  = parse_css_cstr("* { color: black; } p { color: red; }");

        tbox_css_computed_style style_p = tbox_css_cascade_resolve_stylesheet(sheet, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style_p, "color")->value, "red"));
        tbox_css_computed_style_destroy(&style_p);

        tbox_css_computed_style style_span = tbox_css_cascade_resolve_stylesheet(sheet, span);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style_span, "color")->value, "black"));
        tbox_css_computed_style_destroy(&style_span);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 8: specificity sums the entire selector chain, not just the last compound. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div class=\"a\"><span id=\"b\">x</span></div>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *span = div->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("div.a > span#b { color: purple; } span { color: red; }");

        tbox_css_computed_style style = tbox_css_cascade_resolve_stylesheet(sheet, span);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "purple"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 9: tbox_css_cascade_specificity, one simple selector kind at a time. */
    {
        struct {
            const char *selector_text;
            unsigned int a, b, c;
        } cases[] = {
            {"div",           0, 0, 1},
            {"#id",           1, 0, 0},
            {".cls",          0, 1, 0},
            {"[attr]",        0, 1, 0},
            {":first-child",  0, 1, 0},
            {":hover",        0, 1, 0}, /* unknown pseudo -- classified as pseudo-class */
            {":before",       0, 0, 1}, /* known pseudo-element name */
            {"*",             0, 0, 0},
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            char css[64];
            snprintf(css, sizeof(css), "%s { color: red; }", cases[i].selector_text);
            tbox_css_stylesheet *sheet = parse_css_cstr(css);
            TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(sheet) == 1);

            const tbox_css_selector *selector = &tbox_css_stylesheet_rulesets(sheet)[0].selectors[0];
            tbox_css_specificity spec         = tbox_css_cascade_specificity(selector);
            TBOX_TEST_ASSERT(spec.a == cases[i].a && spec.b == cases[i].b && spec.c == cases[i].c);

            tbox_css_stylesheet_destroy(sheet);
        }

        /* combinator chain: contributions sum across every compound. */
        tbox_css_stylesheet *sheet        = parse_css_cstr("div.a > span#b { color: red; }");
        const tbox_css_selector *selector = &tbox_css_stylesheet_rulesets(sheet)[0].selectors[0];
        tbox_css_specificity spec         = tbox_css_cascade_specificity(selector);
        TBOX_TEST_ASSERT(spec.a == 1 && spec.b == 1 && spec.c == 2);
        tbox_css_stylesheet_destroy(sheet);

        TBOX_TEST_ASSERT(tbox_css_cascade_specificity(NULL).a == 0);
    }

    /* 10: tbox_css_cascade_strip_important. */
    {
        struct {
            const char *input;
            const char *expected_value;
            bool expected_important;
        } cases[] = {
            {"red",                  "red",           false},
            {"red !important",       "red",           true},
            {"red!important",        "red",           true},
            {"red ! IMPORTANT",      "red",           true},
            {"red   !   important",  "red",           true},
            {"veryimportant",        "veryimportant", false},
            {"!important",           "",              true},
            {"",                     "",              false},
        };

        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            bool important          = false;
            tbox_string_view result = tbox_css_cascade_strip_important(view_cstr(cases[i].input), &important);
            TBOX_TEST_ASSERT(text_eq(result, cases[i].expected_value));
            TBOX_TEST_ASSERT(important == cases[i].expected_important);
        }
    }

    /* 11: NULL-safety. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>a</p>");
        const tbox_html_node *p    = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("p { color: red; }");

        tbox_css_computed_style empty1 = tbox_css_cascade_resolve(NULL, 0, p);
        TBOX_TEST_ASSERT(empty1.count == 0);
        tbox_css_computed_style_destroy(&empty1);

        tbox_css_computed_style empty2 = tbox_css_cascade_resolve_stylesheet(NULL, p);
        TBOX_TEST_ASSERT(empty2.count == 0);
        tbox_css_computed_style_destroy(&empty2);

        tbox_css_cascade_source one_source = {.stylesheet = sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR};
        tbox_css_computed_style empty3     = tbox_css_cascade_resolve(&one_source, 1, NULL);
        TBOX_TEST_ASSERT(empty3.count == 0);
        tbox_css_computed_style_destroy(&empty3);

        tbox_css_cascade_source sources_with_null[] = {
            {.stylesheet = NULL,  .origin = TBOX_CSS_ORIGIN_AUTHOR},
            {.stylesheet = sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR},
        };
        tbox_css_computed_style style = tbox_css_cascade_resolve(sources_with_null, 2, p);
        TBOX_TEST_ASSERT(style.count == 1);
        tbox_css_computed_style_destroy(&style);

        TBOX_TEST_ASSERT(tbox_css_computed_style_find(NULL, view_cstr("color")) == NULL);

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 12: tbox_css_computed_style_find is ASCII case-insensitive. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>a</p>");
        const tbox_html_node *p    = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("p { color: red; }");

        tbox_css_computed_style style        = tbox_css_cascade_resolve_stylesheet(sheet, p);
        const tbox_css_resolved_declaration *found = tbox_css_computed_style_find(&style, view_cstr("COLOR"));
        TBOX_TEST_ASSERT(found != NULL && text_eq(found->value, "red"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 13: origin dominates specificity -- a low-specificity author rule beats a
     * high-specificity user-agent rule. */
    {
        tbox_html_document *doc = parse_html_cstr("<p id=\"x\">a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *ua_sheet     = parse_css_cstr("#x { color: blue; }");
        tbox_css_stylesheet *author_sheet = parse_css_cstr("* { color: red; }");

        tbox_css_cascade_source sources[] = {
            {.stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT},
            {.stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR},
        };
        tbox_css_computed_style style = tbox_css_cascade_resolve(sources, 2, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "red"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(ua_sheet);
        tbox_css_stylesheet_destroy(author_sheet);
        tbox_html_document_destroy(doc);
    }

    /* 14: author !important beats author normal; user !important beats author !important. */
    {
        tbox_html_document *doc = parse_html_cstr("<p id=\"x\">a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        {
            tbox_css_stylesheet *sheet     = parse_css_cstr("#x { color: blue; } p { color: red !important; }");
            tbox_css_computed_style style  = tbox_css_cascade_resolve_stylesheet(sheet, p);
            TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "red"));
            tbox_css_computed_style_destroy(&style);
            tbox_css_stylesheet_destroy(sheet);
        }

        {
            tbox_css_stylesheet *author_sheet = parse_css_cstr("p { color: red !important; }");
            tbox_css_stylesheet *user_sheet   = parse_css_cstr("p { color: green !important; }");
            tbox_css_cascade_source sources[] = {
                {.stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR},
                {.stylesheet = user_sheet,   .origin = TBOX_CSS_ORIGIN_USER},
            };
            tbox_css_computed_style style = tbox_css_cascade_resolve(sources, 2, p);
            TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "green"));
            tbox_css_computed_style_destroy(&style);
            tbox_css_stylesheet_destroy(author_sheet);
            tbox_css_stylesheet_destroy(user_sheet);
        }

        tbox_html_document_destroy(doc);
    }

    /* 15: user-agent !important beats everything, including user !important --
     * the CSS Cascading Level 4 extension this module adopts. */
    {
        tbox_html_document *doc = parse_html_cstr("<p>a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *ua_sheet     = parse_css_cstr("p { color: black !important; }");
        tbox_css_stylesheet *user_sheet   = parse_css_cstr("p { color: green !important; }");
        tbox_css_stylesheet *author_sheet = parse_css_cstr("p { color: red !important; }");

        tbox_css_cascade_source sources[] = {
            {.stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR},
            {.stylesheet = user_sheet,   .origin = TBOX_CSS_ORIGIN_USER},
            {.stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT},
        };
        tbox_css_computed_style style = tbox_css_cascade_resolve(sources, 3, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "black"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(ua_sheet);
        tbox_css_stylesheet_destroy(user_sheet);
        tbox_css_stylesheet_destroy(author_sheet);
        tbox_html_document_destroy(doc);
    }

    /* 16: multiple stylesheets of the same origin concatenate -- the one later
     * in the `sources` array wins a tie. */
    {
        tbox_html_document *doc = parse_html_cstr("<p>a</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_css_stylesheet *first_sheet  = parse_css_cstr("p { color: red; }");
        tbox_css_stylesheet *second_sheet = parse_css_cstr("p { color: blue; }");

        tbox_css_cascade_source sources[] = {
            {.stylesheet = first_sheet,  .origin = TBOX_CSS_ORIGIN_AUTHOR},
            {.stylesheet = second_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR},
        };
        tbox_css_computed_style style = tbox_css_cascade_resolve(sources, 2, p);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&style, "color")->value, "blue"));

        tbox_css_computed_style_destroy(&style);
        tbox_css_stylesheet_destroy(first_sheet);
        tbox_css_stylesheet_destroy(second_sheet);
        tbox_html_document_destroy(doc);
    }

    /* 17: tbox_css_cascade_resolve_stylesheet matches the general form with one AUTHOR source. */
    {
        tbox_html_document *doc    = parse_html_cstr("<p id=\"x\" class=\"lead\">a</p>");
        const tbox_html_node *p    = tbox_html_document_root(doc)->first_child;
        tbox_css_stylesheet *sheet = parse_css_cstr("p { color: red; } #x { color: blue; } .lead { font-weight: bold; }");

        tbox_css_computed_style via_convenience = tbox_css_cascade_resolve_stylesheet(sheet, p);

        tbox_css_cascade_source source        = {.stylesheet = sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR};
        tbox_css_computed_style via_general    = tbox_css_cascade_resolve(&source, 1, p);

        TBOX_TEST_ASSERT(via_convenience.count == via_general.count);
        TBOX_TEST_ASSERT(text_eq(find_cstr(&via_convenience, "color")->value, "blue"));
        TBOX_TEST_ASSERT(text_eq(find_cstr(&via_general, "color")->value, "blue"));
        TBOX_TEST_ASSERT(text_eq(find_cstr(&via_convenience, "font-weight")->value, "bold"));
        TBOX_TEST_ASSERT(text_eq(find_cstr(&via_general, "font-weight")->value, "bold"));

        tbox_css_computed_style_destroy(&via_convenience);
        tbox_css_computed_style_destroy(&via_general);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
