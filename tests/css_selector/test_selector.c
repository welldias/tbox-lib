#include <tbox/css_selector.h>

#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

static tbox_html_document *parse_html_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

static tbox_css_selector_node_set select_cstr(const tbox_html_node *root, const char *text) {
    return tbox_css_selector_select(root, text, strlen(text));
}

int tbox_test_css_selector_run(void) {
    int failures = 0;

    /* 1: type selector matches every descendant with that tag, root itself excluded. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>a</p><section><p>b</p></section></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "p");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "a"));
        TBOX_TEST_ASSERT(text_eq(set.items[1]->first_child->text.text, "b"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 2: class selector, case-sensitive, whitespace-separated token match. */
    {
        tbox_html_document *doc = parse_html_cstr("<div class=\"a b\"></div><div class=\"ab\"></div><div class=\"A\"></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), ".a");

        TBOX_TEST_ASSERT(set.count == 1);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 3: id selector. */
    {
        tbox_html_document *doc = parse_html_cstr("<div id=\"x\"></div><div id=\"y\"></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "#y");

        TBOX_TEST_ASSERT(set.count == 1);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 4: attribute operators -- exists, equals, includes, dashmatch. */
    {
        tbox_html_document *doc = parse_html_cstr(
            "<a href=\"x\"></a>"
            "<b lang=\"en\"></b>"
            "<c class=\"foo bar\"></c>"
            "<d lang=\"en-US\"></d>"
            "<e lang=\"english\"></e>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_css_selector_node_set exists = select_cstr(root, "[href]");
        TBOX_TEST_ASSERT(exists.count == 1);
        tbox_css_selector_node_set_destroy(&exists);

        tbox_css_selector_node_set equals = select_cstr(root, "[lang=en]");
        TBOX_TEST_ASSERT(equals.count == 1);
        tbox_css_selector_node_set_destroy(&equals);

        tbox_css_selector_node_set includes = select_cstr(root, "[class~=bar]");
        TBOX_TEST_ASSERT(includes.count == 1);
        tbox_css_selector_node_set_destroy(&includes);

        tbox_css_selector_node_set dashmatch = select_cstr(root, "[lang|=en]");
        TBOX_TEST_ASSERT(dashmatch.count == 2); /* "en" and "en-US", not "english" */
        tbox_css_selector_node_set_destroy(&dashmatch);

        tbox_html_document_destroy(doc);
    }

    /* 5: descendant combinator matches at any depth; child combinator only
     * matches direct children. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><section><p>deep</p></section><p>direct</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_css_selector_node_set descendant = select_cstr(root, "div p");
        TBOX_TEST_ASSERT(descendant.count == 2);
        tbox_css_selector_node_set_destroy(&descendant);

        tbox_css_selector_node_set child = select_cstr(root, "div > p");
        TBOX_TEST_ASSERT(child.count == 1);
        TBOX_TEST_ASSERT(text_eq(child.items[0]->first_child->text.text, "direct"));
        tbox_css_selector_node_set_destroy(&child);

        tbox_html_document_destroy(doc);
    }

    /* 6: adjacent sibling combinator skips non-element siblings (text/comments). */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>x</p>text<!--c--><span>y</span></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "p + span");

        TBOX_TEST_ASSERT(set.count == 1);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "y"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 7: comma-separated group is OR across selectors, still in document order. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>p</p><span>s</span><em>e</em></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "span, p");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "p"));
        TBOX_TEST_ASSERT(text_eq(set.items[1]->first_child->text.text, "s"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 8: :first-child / :last-child are structural and count only element siblings. */
    {
        tbox_html_document *doc = parse_html_cstr("<ul>text<li>a</li><li>b</li><li>c</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_css_selector_node_set first = select_cstr(root, "li:first-child");
        TBOX_TEST_ASSERT(first.count == 1);
        TBOX_TEST_ASSERT(text_eq(first.items[0]->first_child->text.text, "a"));
        tbox_css_selector_node_set_destroy(&first);

        tbox_css_selector_node_set last = select_cstr(root, "li:last-child");
        TBOX_TEST_ASSERT(last.count == 1);
        TBOX_TEST_ASSERT(text_eq(last.items[0]->first_child->text.text, "c"));
        tbox_css_selector_node_set_destroy(&last);

        tbox_html_document_destroy(doc);
    }

    /* 9: an unsupported pseudo-class (e.g. :hover) never matches. */
    {
        tbox_html_document *doc = parse_html_cstr("<a>x</a>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "a:hover");

        TBOX_TEST_ASSERT(set.count == 0);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 10: tbox_css_selector_matches tests a single node directly against a
     * selector already parsed from a loaded stylesheet. */
    {
        tbox_html_document *doc = parse_html_cstr("<div class=\"box\"></div><span></span>");
        const tbox_html_node *div  = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *span = div->next_sibling;

        tbox_css_stylesheet *sheet = tbox_css_parse(".box { color: red; }", strlen(".box { color: red; }"));
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(sheet) == 1);
        const tbox_css_selector *selector = &tbox_css_stylesheet_rulesets(sheet)[0].selectors[0];

        TBOX_TEST_ASSERT(tbox_css_selector_matches(selector, div));
        TBOX_TEST_ASSERT(!tbox_css_selector_matches(selector, span));

        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 11: tbox_css_selector_match_stylesheet applies every ruleset of a
     * loaded stylesheet to a loaded HTML tree end to end. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p class=\"lead\">a</p><p>b</p></div>");
        const char *css = "p { color: black; } .lead { font-weight: bold; }";
        tbox_css_stylesheet *sheet = tbox_css_parse(css, strlen(css));

        tbox_css_selector_match_set matches = tbox_css_selector_match_stylesheet(sheet, tbox_html_document_root(doc));

        TBOX_TEST_ASSERT(matches.count == 3); /* "p" matches both <p>, ".lead" matches one */

        size_t lead_matches = 0;
        for (size_t i = 0; i < matches.count; i++) {
            if (text_eq(matches.items[i].ruleset->declarations[0].property, "font-weight")) {
                lead_matches++;
                TBOX_TEST_ASSERT(text_eq(matches.items[i].node->element.tag_name, "p"));
            }
        }
        TBOX_TEST_ASSERT(lead_matches == 1);

        tbox_css_selector_match_set_destroy(&matches);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }

    /* 12: tbox_css_selector_compile reports a syntax error via NULL + offset. */
    {
        size_t error_offset = 0;
        tbox_css_selector_query *query = tbox_css_selector_compile(">", strlen(">"), &error_offset);

        TBOX_TEST_ASSERT(query == NULL);
        TBOX_TEST_ASSERT(error_offset == 0);
    }

    /* 13: trailing garbage after a valid selector-group is also a syntax error. */
    {
        tbox_css_selector_query *query = tbox_css_selector_compile("div}", strlen("div}"), NULL);
        TBOX_TEST_ASSERT(query == NULL);
    }

    return failures;
}
