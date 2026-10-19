#include <tbox/css_cascade.h>
#include <tbox/css_selector.h>

#include <string.h>

#include "base/tbox_string.h"
#include "test_support.h"

int tbox_test_css_scope_run(void) {
    int failures = 0;
    const char *html = "<div class='outer'><div class='inner'><p id='x'></p><section class='limit'><p id='y'></p></section></div><p id='z'></p></div>";
    tbox_html_document *doc = tbox_html_parse(html, strlen(html));
    const tbox_html_node *outer = tbox_html_document_root(doc)->first_child;
    const tbox_html_node *inner = outer->first_child;
    const tbox_html_node *x = inner->first_child;
    const tbox_html_node *y = x->next_sibling->first_child;
    const tbox_html_node *z = outer->last_child;
    static const struct { const char *css, *x, *y, *z; } cases[] = {
        { "@scope (.outer) to (.limit) { p { color:red; } } p { color:black; }", "red", "black", "red" },
        { "@scope (.inner) { p { color:blue; } } @scope (.outer) { p { color:red; } } p { color:black; }", "blue", "blue", "red" },
        { "@scope (.inner) { p { color:blue; } } #x { color:green; }", "green", "blue", NULL },
        { "@scope (.inner) { p { color:blue; } } p { color:red!important; }", "red", "red", "red" },
        { "@scope (.inner) { :scope > p { color:blue; } }", "blue", NULL, NULL },
        { "@scope (.outer) to (.limit) { @scope (.inner) { p { color:blue; } } }", "blue", NULL, NULL },
        { "@scope (.outer) { @scope (:scope > .inner) to (:scope > .limit) { p { color:blue; } } }", "blue", NULL, NULL },
        { "@scope (.outer) { @scope { :scope > p { color:blue; } } }", NULL, NULL, "blue" },
        { "@scope (.outer, .inner) { :scope > p { color:blue; } }", "blue", NULL, "blue" },
        { "@scope (.outer) { p { color:red; } p { color:blue; } }", "blue", "blue", "blue" },
        { "@layer a,b; @layer b { p { color:green; } } @layer a { @scope (.inner) { p { color:blue; } } }", "green", "green", "green" },
        { "@media screen { @scope (.outer) { @layer a { p { color:blue; } } } }", "blue", "blue", "blue" },
        { "@scope (.outer) { @media (min-width:900px) { p { color:blue; } } }", NULL, NULL, NULL },
        { "@scope { p { color:blue; } }", "blue", "blue", "blue" },
        { "@scope (.outer) broken { p { color:red; } } p { color:green; }", "green", "green", "green" },
        { "@scope (.outer) to (.outer) { p { color:blue; } }", "blue", "blue", "blue" },
        { "@scope (.inner) to (.outer :scope .limit) { p { color:blue; } }", "blue", NULL, NULL },
        { "@scope (.inner) { .outer :scope > p { color:blue; } }", "blue", NULL, NULL },
        { "@scope (.outer::before) { p { color:red; } } p { color:green; }", "green", "green", "green" },
        { "@scope (.outer >) { p { color:red; } } p { color:green; }", "green", "green", "green" },
    };
    const tbox_html_node *nodes[] = { x, y, z };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        tbox_css_stylesheet *sheet = tbox_css_parse(cases[i].css, strlen(cases[i].css));
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        const char *expected[] = { cases[i].x, cases[i].y, cases[i].z };
        for (size_t j = 0; j < 3; j++) {
            tbox_css_computed_style style = tbox_css_cascade_resolve_in_viewport(&source, 1, nodes[j], 800, 600, false, NULL);
            const tbox_css_resolved_declaration *color = tbox_css_computed_style_find(&style, tbox_string_view_from_cstr("color"));
            TBOX_TEST_ASSERT_MSG(expected[j] == NULL ? color == NULL : color != NULL && tbox_string_view_equal_cstr(color->value, expected[j]), cases[i].css);
            tbox_css_computed_style_destroy(&style);
        }
        tbox_css_stylesheet_destroy(sheet);
    }
    {
        const char *text = "@scope (.inner) to (.limit) { :scope > p { color:blue; } }";
        tbox_css_stylesheet *sheet = tbox_css_parse(text, strlen(text));
        tbox_css_selector_match_set matches = tbox_css_selector_match_stylesheet(sheet, tbox_html_document_root(doc));
        TBOX_TEST_ASSERT(matches.count == 1 && matches.items[0].node == x);
        tbox_css_selector_match_set_destroy(&matches);
        tbox_css_stylesheet_destroy(sheet);
        TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("selector(:scope > p)")));
        TBOX_TEST_ASSERT(!tbox_css_supports(tbox_string_view_from_cstr("at-rule(@scope)")));
    }
    tbox_html_document_destroy(doc);
    return failures;
}
