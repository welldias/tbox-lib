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
        tbox_html_document *doc        = parse_html_cstr("<div><p>a</p><section><p>b</p></section></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "p");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "a"));
        TBOX_TEST_ASSERT(text_eq(set.items[1]->first_child->text.text, "b"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 2: class selector, case-sensitive, whitespace-separated token match. */
    {
        tbox_html_document *doc        = parse_html_cstr("<div class=\"a b\"></div><div class=\"ab\"></div><div class=\"A\"></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), ".a");

        TBOX_TEST_ASSERT(set.count == 1);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 3: id selector. */
    {
        tbox_html_document *doc        = parse_html_cstr("<div id=\"x\"></div><div id=\"y\"></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "#y");

        TBOX_TEST_ASSERT(set.count == 1);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 4: attribute operators -- exists, equals, includes, dashmatch. */
    {
        tbox_html_document *doc    = parse_html_cstr("<a href=\"x\"></a>"
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
        tbox_html_document *doc    = parse_html_cstr("<div><section><p>deep</p></section><p>direct</p></div>");
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
        tbox_html_document *doc        = parse_html_cstr("<div><p>x</p>text<!--c--><span>y</span></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "p + span");

        TBOX_TEST_ASSERT(set.count == 1);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "y"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 7: comma-separated group is OR across selectors, still in document order. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>before</p><h2>heading</h2><em>gap</em><p>after 1</p><!-- c --><p>after 2</p></div><p>outside</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set set = select_cstr(root, "div > h2 ~ p");
        TBOX_TEST_ASSERT(set.count == 2);
        if (set.count == 2) {
            TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "after 1"));
            TBOX_TEST_ASSERT(text_eq(set.items[1]->first_child->text.text, "after 2"));
        }
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "h2+p");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 8: comma-separated group is OR across selectors, still in document order. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p class='skip'>A</p><p id='chosen'>B</p><span>C</span><p data-skip>D</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const char *selectors[] = { "p:not(.skip)", "p:not(#chosen)", "p:not([data-skip])", "div > :not(p)", "p:not(*)" };
        const size_t expected[] = { 2, 2, 2, 1, 0 };
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_selector_node_set set = select_cstr(root, selectors[i]);
            TBOX_TEST_ASSERT(set.count == expected[i]);
            tbox_css_selector_node_set_destroy(&set);
        }
        tbox_css_selector_query *invalid = tbox_css_selector_compile("p:not(.skip.more)", strlen("p:not(.skip.more)"), NULL);
        TBOX_TEST_ASSERT(invalid == NULL);
        tbox_css_selector_query_destroy(invalid);
        tbox_html_document_destroy(doc);
    }

    /* 9: comma-separated group is OR across selectors, still in document order. */
    {
        tbox_html_document *doc        = parse_html_cstr("<div><p>p</p><span>s</span><em>e</em></div>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "span, p");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, "p"));
        TBOX_TEST_ASSERT(text_eq(set.items[1]->first_child->text.text, "s"));

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 8: :first-child / :last-child are structural and count only element siblings. */
    {
        tbox_html_document *doc    = parse_html_cstr("<ul>text<li>a</li><li>b</li><li>c</li></ul>");
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

    /* Language comes from the closest lang attribute. A more specific
     * subtag matches its base language, case-insensitively. */
    {
        tbox_html_document *doc = parse_html_cstr("<div lang='pt-BR'><p>A</p><section lang='en-US'><p>B</p></section>"
            "<p lang=''>C</p><p lang='english'>D</p></div><p>E</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set set = select_cstr(root, "p:lang(pt)");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->first_child->text.text, "A"));
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:lang(EN)");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->first_child->text.text, "B"));
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:lang(english)");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->first_child->text.text, "D"));
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:lang(eng)");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* :hover never matches while no hover context has been set (default
     * NULL). Unsupported pseudo-elements still never match. */
    {
        tbox_html_document *doc        = parse_html_cstr("<a>x</a>");
        tbox_css_selector_node_set set = select_cstr(tbox_html_document_root(doc), "a:hover");

        TBOX_TEST_ASSERT(set.count == 0);

        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 10: tbox_css_selector_matches tests a single node directly against a
     * selector already parsed from a loaded stylesheet. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div class=\"box\"></div><span></span>");
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
        tbox_html_document *doc    = parse_html_cstr("<div><p class=\"lead\">a</p><p>b</p></div>");
        const char *css            = "p { color: black; } .lead { font-weight: bold; }";
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
        size_t error_offset            = 0;
        const tbox_css_selector_query *query = tbox_css_selector_compile(">", strlen(">"), &error_offset);

        TBOX_TEST_ASSERT(query == NULL);
        TBOX_TEST_ASSERT(error_offset == 0);
    }

    /* 13: trailing garbage after a valid selector-group is also a syntax error. */
    {
        const tbox_css_selector_query *query = tbox_css_selector_compile("div}", strlen("div}"), NULL);
        TBOX_TEST_ASSERT(query == NULL);
    }

    /* 14: after tbox_css_selector_set_hover_context(node_x), a bare :hover
     * selector matches node_x and its ancestors. Resets the (global) hover
     * context back to NULL at the end so it doesn't leak into later tests. */
    {
        tbox_html_document *doc    = parse_html_cstr("<div><p>a</p><span>b</span></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *div  = root->first_child;
        const tbox_html_node *p    = div->first_child;

        tbox_css_selector_set_hover_context(p);

        tbox_css_selector_node_set set = select_cstr(root, ":hover");
        TBOX_TEST_ASSERT(set.count == 2); /* div and p, not span */
        TBOX_TEST_ASSERT(set.items[0] == div && set.items[1] == p);
        tbox_css_selector_node_set_destroy(&set);

        tbox_css_selector_set_hover_context(NULL);
        tbox_html_document_destroy(doc);
    }

    /* 15: a compound selector like button:hover matches only a <button>
     * that is ALSO the current hover target -- proves the AND semantics,
     * not an OR. Resets the hover context back to NULL at the end. */
    {
        tbox_html_document *doc         = parse_html_cstr("<div><button>b1</button><div>b2</div></div>");
        const tbox_html_node *root      = tbox_html_document_root(doc);
        const tbox_html_node *outer_div = root->first_child;
        const tbox_html_node *button    = outer_div->first_child;
        const tbox_html_node *inner_div = button->next_sibling;

        /* positive control: the hovered <button> matches. */
        tbox_css_selector_set_hover_context(button);
        tbox_css_selector_node_set hovered_button = select_cstr(root, "button:hover");
        TBOX_TEST_ASSERT(hovered_button.count == 1);
        TBOX_TEST_ASSERT(hovered_button.items[0] == button);
        tbox_css_selector_node_set_destroy(&hovered_button);

        /* a <button> that is NOT the hover target must not match. */
        tbox_css_selector_set_hover_context(inner_div);
        tbox_css_selector_node_set button_not_hovered = select_cstr(root, "button:hover");
        TBOX_TEST_ASSERT(button_not_hovered.count == 0);
        tbox_css_selector_node_set_destroy(&button_not_hovered);

        /* a hovered <div> (not a <button>) must not match "button:hover"
         * either -- same query as above, inner_div is still the hover
         * target and is not a <button>. */
        tbox_css_selector_node_set non_button_hovered = select_cstr(root, "button:hover");
        TBOX_TEST_ASSERT(non_button_hovered.count == 0);
        tbox_css_selector_node_set_destroy(&non_button_hovered);

        tbox_css_selector_set_hover_context(NULL);
        tbox_html_document_destroy(doc);
    }

    /* 16: tbox_css_selector_set_hover_context(NULL) after a real node was
     * set makes :hover stop matching that node again (simulates the
     * pointer leaving the element). */
    {
        tbox_html_document *doc    = parse_html_cstr("<p>a</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *p    = root->first_child;

        tbox_css_selector_set_hover_context(p);
        tbox_css_selector_node_set hovered = select_cstr(root, ":hover");
        TBOX_TEST_ASSERT(hovered.count == 1);
        tbox_css_selector_node_set_destroy(&hovered);

        tbox_css_selector_set_hover_context(NULL);
        tbox_css_selector_node_set after_leave = select_cstr(root, ":hover");
        TBOX_TEST_ASSERT(after_leave.count == 0);
        tbox_css_selector_node_set_destroy(&after_leave);

        tbox_html_document_destroy(doc);
    }

    /* HTML input type is an ASCII case-insensitive enumerated attribute;
     * ordinary attribute values remain case-sensitive. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><input type='CHECKBOX'><input type='button'></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *checkbox = root->first_child->first_child;
        tbox_css_selector_node_set set = select_cstr(root, "input[type=checkbox]");
        TBOX_TEST_ASSERT(set.count == 1 && set.items[0] == checkbox);
        tbox_css_selector_node_set_destroy(&set);
        tbox_css_selector_node_set checked = select_cstr(root, "input:checked");
        TBOX_TEST_ASSERT(checked.count == 0);
        tbox_css_selector_node_set_destroy(&checked);
        tbox_html_node_set_attribute(doc, (tbox_html_node *)checkbox,
                                     tbox_string_view_make("checked", 7), tbox_string_view_make(NULL, 0));
        checked = select_cstr(root, "input:checked");
        TBOX_TEST_ASSERT(checked.count == 1 && checked.items[0] == checkbox);
        tbox_css_selector_node_set_destroy(&checked);
        tbox_html_document_destroy(doc);
    }

    /* Structural and form-state pseudo-classes used by CSS stylesheets. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p id='only'></p><!-- comment --></div>"
                                                 "<div><p></p><p></p></div>"
                                                 "<div id='empty'><!-- comment --></div>"
                                                 "<div id='space'> </div>"
                                                 "<input disabled><input><button disabled>x</button><span disabled>x</span>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set set = select_cstr(root, "p:only-child");
        TBOX_TEST_ASSERT(set.count == 1);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "div:empty");
        TBOX_TEST_ASSERT(set.count == 1);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "input:disabled, button:disabled");
        TBOX_TEST_ASSERT(set.count == 2);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "input:enabled");
        TBOX_TEST_ASSERT(set.count == 1);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "span:disabled, span:enabled");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* Required and optional apply only to supported input, select, and
     * textarea controls. The input type comparison is ASCII insensitive. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><input required><input><input type='HIDDEN' required>"
            "<input type='range'><select required></select><select></select><textarea required></textarea>"
            "<textarea></textarea><button required>x</button><span required>x</span></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set set = select_cstr(root, ":required");
        TBOX_TEST_ASSERT(set.count == 3);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, ":optional");
        TBOX_TEST_ASSERT(set.count == 3);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "input[type=hidden]:required, input[type=range]:optional, button:required, span:required");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* Root and type-relative selectors ignore other element types and
     * non-element siblings while counting siblings. */
    {
        tbox_html_document *doc = parse_html_cstr("<!DOCTYPE html><html><body>"
            "<section><p id='a'>A</p><!-- x --><span>S</span><p id='b'>B</p><em>E</em><p id='c'>C</p></section>"
            "<section><h2>H</h2><p id='d'>D</p></section>"
            "</body></html>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set set = select_cstr(root, ":root");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->element.tag_name, "html"));
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section:first-of-type");
        TBOX_TEST_ASSERT(set.count == 1);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section:last-of-type");
        TBOX_TEST_ASSERT(set.count == 1);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section:only-of-type");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section p:first-of-type");
        TBOX_TEST_ASSERT(set.count == 2);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section p:last-of-type");
        TBOX_TEST_ASSERT(set.count == 2);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section p:only-of-type");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->first_child->text.text, "D"));
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "section span:only-of-type");
        TBOX_TEST_ASSERT(set.count == 1 && text_eq(set.items[0]->first_child->text.text, "S"));
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* nth selectors count element siblings, or only siblings of the same
     * type. Their an+b formulas also accept whitespace and negative a. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>A</p><!-- x --><span>S</span><p>B</p><p>C</p><em>E</em><p>D</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const char *selectors[] = {
            "div > :nth-child(odd)", "div > :nth-child(even)",
            "div > :nth-child(3)", "div > :nth-child(2n + 1)",
            "div > p:nth-of-type(2)", "div > p:nth-of-type(-n+2)",
            "div > p:nth-of-type(n+3)", "div > p:nth-of-type(2n)",
            "div > :nth-child(0)", "div > :nth-child(2n+)",
        };
        const size_t expected[] = { 3, 3, 1, 3, 1, 2, 2, 2, 0, 0 };
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_selector_node_set set = select_cstr(root, selectors[i]);
            TBOX_TEST_ASSERT(set.count == expected[i]);
            tbox_css_selector_node_set_destroy(&set);
        }
        tbox_html_document_destroy(doc);
    }

    /* Last variants count from the end, ignoring comments and counting
     * only the same tag when requested. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><p>A</p><span>S</span><p>B</p><!-- x --><p>C</p><em>E</em><p>D</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const char *selectors[] = {
            "div > :nth-last-child(1)", "div > :nth-last-child(2)",
            "div > :nth-last-child(odd)", "div > :nth-last-child(2n + 1)",
            "div > p:nth-last-of-type(2)", "div > p:nth-last-of-type(-n+2)",
            "div > p:nth-last-of-type(2n)", "div > :nth-last-child(0)",
        };
        const size_t expected[] = { 1, 1, 3, 3, 1, 2, 2, 0 };
        const char *first_text[] = { "D", "E", "S", "S", "C", "C", "A", NULL };
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_selector_node_set set = select_cstr(root, selectors[i]);
            TBOX_TEST_ASSERT(set.count == expected[i]);
            if (set.count > 0) TBOX_TEST_ASSERT(text_eq(set.items[0]->first_child->text.text, first_text[i]));
            tbox_css_selector_node_set_destroy(&set);
        }
        tbox_html_document_destroy(doc);
    }

    /* :focus-within follows the current focus target up the ancestor chain. */
    {
        tbox_html_document *doc = parse_html_cstr("<div id='form'><fieldset><input></fieldset><p>other</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *form = root->first_child;
        const tbox_html_node *fieldset = form->first_child;
        const tbox_html_node *input = fieldset->first_child;
        tbox_css_selector_set_focus_context(input);
        tbox_css_selector_node_set set = select_cstr(root, ":focus-within");
        TBOX_TEST_ASSERT(set.count == 3);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:focus-within");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_css_selector_set_focus_context(NULL);
        set = select_cstr(root, ":focus-within");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* No visited-state store exists, so every eligible href matches both
     * :link and :any-link. An anchor without href does not. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><a href='/one'>One</a><a>Two</a><area href='/map'><link href='/css'><span href='/no'>No</span></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_css_selector_node_set links = select_cstr(root, ":link");
        tbox_css_selector_node_set any = select_cstr(root, ":any-link");
        TBOX_TEST_ASSERT(links.count == 3 && any.count == 3);
        tbox_css_selector_node_set_destroy(&links);
        tbox_css_selector_node_set_destroy(&any);
        tbox_html_document_destroy(doc);
    }

    /* Prefix, suffix and substring attribute operators match byte-exact
     * values; an empty search string never matches. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><a data-path='docs/start.html'>A</a><a data-path='images/icon.png'>B</a>"
            "<a data-path='docs/guide.png'>C</a><a data-path=''>D</a></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const char *selectors[] = { "[data-path^=docs]", "[data-path$='.png']", "[data-path*='guide']", "[data-path*='']" };
        const size_t expected[] = { 2, 2, 1, 0 };
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_selector_node_set set = select_cstr(root, selectors[i]);
            TBOX_TEST_ASSERT(set.count == expected[i]);
            tbox_css_selector_node_set_destroy(&set);
        }
        tbox_html_document_destroy(doc);
    }

    /* Hover applies to the hit element and each element ancestor. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><section><span>x</span></section><p>y</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *div = root->first_child;
        const tbox_html_node *section = div->first_child;
        const tbox_html_node *span = section->first_child;
        tbox_css_selector_set_hover_context(span);
        tbox_css_selector_node_set set = select_cstr(root, ":hover");
        TBOX_TEST_ASSERT(set.count == 3);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:hover");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_css_selector_set_hover_context(NULL);
        tbox_html_document_destroy(doc);
    }

    /* `i` folds ASCII; `s` explicitly preserves case, including on HTML
     * enumerated attributes that otherwise compare case-insensitively. */
    {
        tbox_html_document *doc = parse_html_cstr("<div data-name='Alpha-BETA' data-tags='FOO Bar'><input type='TEXT'></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const char *selectors[] = {
            "[data-name='alpha-beta' i]", "[data-tags~=foo i]", "[data-name|=alpha i]",
            "[data-name^=alpha i]", "[data-name$=beta i]", "[data-name*=HA-be i]",
            "[data-name='alpha-beta']", "[data-name^=alpha]", "[data-name='alpha-beta' s]",
            "[data-name='Alpha-BETA' s]", "input[type='text']", "input[type='text' s]", "input[type='TEXT' s]"
        };
        const size_t expected[] = { 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 0, 1 };
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
            tbox_css_selector_node_set set = select_cstr(root, selectors[i]);
            TBOX_TEST_ASSERT(set.count == expected[i]);
            tbox_css_selector_node_set_destroy(&set);
        }
        tbox_html_document_destroy(doc);
    }

    /* :active covers the pressed element and its ancestors until release. */
    {
        tbox_html_document *doc = parse_html_cstr("<div><button><span>x</span></button><p>y</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *span = root->first_child->first_child->first_child;
        tbox_css_selector_set_active_context(span);
        tbox_css_selector_node_set set = select_cstr(root, ":active");
        TBOX_TEST_ASSERT(set.count == 3);
        tbox_css_selector_node_set_destroy(&set);
        set = select_cstr(root, "p:active");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_css_selector_set_active_context(NULL);
        set = select_cstr(root, ":active");
        TBOX_TEST_ASSERT(set.count == 0);
        tbox_css_selector_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
