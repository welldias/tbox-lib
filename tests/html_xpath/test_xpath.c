#include <tbox/html_xpath.h>

#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

static tbox_html_document *parse_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

static tbox_xpath_node_set select_cstr(const tbox_html_node *context, const char *expr) {
    return tbox_xpath_select(context, expr, strlen(expr));
}

int tbox_test_html_xpath_run(void) {
    int failures = 0;

    /* 1: simple absolute path. */
    {
        tbox_html_document *doc = parse_cstr("<html><body><p>x</p></body></html>");
        tbox_xpath_node_set set  = select_cstr(tbox_html_document_root(doc), "/html/body");

        TBOX_TEST_ASSERT(set.count == 1);
        TBOX_TEST_ASSERT(set.items[0].type == TBOX_XPATH_ITEM_NODE);
        TBOX_TEST_ASSERT(text_eq(set.items[0].node->element.tag_name, "body"));

        tbox_xpath_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 2: '//tag' matches descendants at any depth. */
    {
        tbox_html_document *doc = parse_cstr("<div><section><p>a</p></section><p>b</p></div>");
        tbox_xpath_node_set set  = select_cstr(tbox_html_document_root(doc), "//p");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0].node->first_child->text.text, "a"));
        TBOX_TEST_ASSERT(text_eq(set.items[1].node->first_child->text.text, "b"));

        tbox_xpath_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 3: '*' only matches ELEMENT children (excludes comments/text). */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p><!--c--><span>y</span></div>");
        tbox_xpath_node_set set  = select_cstr(tbox_html_document_root(doc), "div/*");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0].node->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(set.items[1].node->element.tag_name, "span"));

        tbox_xpath_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 4: attribute axis at the end of the path, including '@*' and boolean
     * attributes. */
    {
        tbox_html_document *doc = parse_cstr("<a href=\"http://x\" class=\"y\"></a><input disabled>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_xpath_node_set href_set = select_cstr(root, "//a/@href");
        TBOX_TEST_ASSERT(href_set.count == 1);
        TBOX_TEST_ASSERT(href_set.items[0].type == TBOX_XPATH_ITEM_ATTRIBUTE);
        TBOX_TEST_ASSERT(text_eq(href_set.items[0].attribute.attribute->value, "http://x"));
        tbox_xpath_node_set_destroy(&href_set);

        tbox_xpath_node_set all_attrs = select_cstr(root, "//a/@*");
        TBOX_TEST_ASSERT(all_attrs.count == 2);
        tbox_xpath_node_set_destroy(&all_attrs);

        tbox_xpath_node_set disabled_set = select_cstr(root, "//input/@disabled");
        TBOX_TEST_ASSERT(disabled_set.count == 1);
        TBOX_TEST_ASSERT(disabled_set.items[0].attribute.attribute->value.size == 0);
        tbox_xpath_node_set_destroy(&disabled_set);

        tbox_html_document_destroy(doc);
    }

    /* 5: positional predicate is per individual context node, not a global
     * index over the merged result set. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>1</p><p>2</p></div><div><p>3</p><p>4</p></div>");
        tbox_xpath_node_set set  = select_cstr(tbox_html_document_root(doc), "//div/p[1]");

        TBOX_TEST_ASSERT(set.count == 2);
        TBOX_TEST_ASSERT(text_eq(set.items[0].node->first_child->text.text, "1"));
        TBOX_TEST_ASSERT(text_eq(set.items[1].node->first_child->text.text, "3"));

        tbox_xpath_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 6: '[@attr]' existence and '[@attr=\"value\"]' equality predicates. */
    {
        tbox_html_document *doc = parse_cstr("<a href=\"x\" class=\"btn\"></a><a class=\"btn\"></a>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_xpath_node_set has_href = select_cstr(root, "//a[@href]");
        TBOX_TEST_ASSERT(has_href.count == 1);
        tbox_xpath_node_set_destroy(&has_href);

        tbox_xpath_node_set btn = select_cstr(root, "//a[@class='btn']");
        TBOX_TEST_ASSERT(btn.count == 2);
        tbox_xpath_node_set_destroy(&btn);

        tbox_xpath_node_set none = select_cstr(root, "//a[@class='outro']");
        TBOX_TEST_ASSERT(none.count == 0);
        tbox_xpath_node_set_destroy(&none);

        tbox_html_document_destroy(doc);
    }

    /* 7: text() only matches direct text children; './/text()' reaches
     * nested descendants too. */
    {
        tbox_html_document *doc = parse_cstr("<p>hello<b>world</b></p>");
        tbox_xpath_node_set p_set = select_cstr(tbox_html_document_root(doc), "//p");
        const tbox_html_node *p    = p_set.items[0].node;

        tbox_xpath_node_set direct = select_cstr(p, "text()");
        TBOX_TEST_ASSERT(direct.count == 1);
        TBOX_TEST_ASSERT(text_eq(direct.items[0].node->text.text, "hello"));
        tbox_xpath_node_set_destroy(&direct);

        tbox_xpath_node_set nested = select_cstr(p, ".//text()");
        TBOX_TEST_ASSERT(nested.count == 2);
        tbox_xpath_node_set_destroy(&nested);

        tbox_xpath_node_set_destroy(&p_set);
        tbox_html_document_destroy(doc);
    }

    /* 8: node() matches any child node type. */
    {
        tbox_html_document *doc = parse_cstr("<p>hi<b>x</b><!--c--></p>");
        tbox_xpath_node_set p_set = select_cstr(tbox_html_document_root(doc), "//p");
        const tbox_html_node *p    = p_set.items[0].node;

        tbox_xpath_node_set set = select_cstr(p, "node()");
        TBOX_TEST_ASSERT(set.count == 3);
        TBOX_TEST_ASSERT(set.items[0].node->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(set.items[1].node->type == TBOX_HTML_NODE_ELEMENT);
        TBOX_TEST_ASSERT(set.items[2].node->type == TBOX_HTML_NODE_COMMENT);

        tbox_xpath_node_set_destroy(&set);
        tbox_xpath_node_set_destroy(&p_set);
        tbox_html_document_destroy(doc);
    }

    /* 9: '.' (self) and '..' (parent), including '..' at the root. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p></div>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        tbox_xpath_node_set p_set  = select_cstr(root, "//p");
        const tbox_html_node *p     = p_set.items[0].node;

        tbox_xpath_node_set self_set = select_cstr(p, ".");
        TBOX_TEST_ASSERT(self_set.count == 1 && self_set.items[0].node == p);
        tbox_xpath_node_set_destroy(&self_set);

        tbox_xpath_node_set parent_set = select_cstr(p, "..");
        TBOX_TEST_ASSERT(parent_set.count == 1);
        TBOX_TEST_ASSERT(text_eq(parent_set.items[0].node->element.tag_name, "div"));
        tbox_xpath_node_set_destroy(&parent_set);

        tbox_xpath_node_set root_parent = select_cstr(root, "..");
        TBOX_TEST_ASSERT(root_parent.count == 0);
        tbox_xpath_node_set_destroy(&root_parent);

        tbox_xpath_node_set_destroy(&p_set);
        tbox_html_document_destroy(doc);
    }

    /* 10: a path with no matches yields an empty, safe-to-destroy node_set. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p></div>");
        tbox_xpath_node_set set  = select_cstr(tbox_html_document_root(doc), "//naoexiste");

        TBOX_TEST_ASSERT(set.count == 0);
        TBOX_TEST_ASSERT(set.items == NULL);

        tbox_xpath_node_set_destroy(&set);
        tbox_html_document_destroy(doc);
    }

    /* 11: syntax errors are rejected at compile time. */
    {
        size_t offset = 0;
        tbox_xpath_query *unterminated_bracket = tbox_xpath_compile("//div[", strlen("//div["), &offset);
        TBOX_TEST_ASSERT(unterminated_bracket == NULL);

        tbox_xpath_query *attr_not_last = tbox_xpath_compile("//a/@href/b", strlen("//a/@href/b"), NULL);
        TBOX_TEST_ASSERT(attr_not_last == NULL);
    }

    /* 12: round-trip -- locate with xpath, remove the node, re-evaluate and
     * see the updated tree. */
    {
        tbox_html_document *doc = parse_cstr("<ul><li>a</li><li>b</li><li>c</li></ul>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        tbox_xpath_node_set second = select_cstr(root, "//li[2]");
        TBOX_TEST_ASSERT(second.count == 1);
        TBOX_TEST_ASSERT(text_eq(second.items[0].node->first_child->text.text, "b"));

        tbox_html_node *li2 = (tbox_html_node *)second.items[0].node;
        tbox_html_node_remove(li2);
        tbox_xpath_node_set_destroy(&second);

        tbox_xpath_node_set remaining = select_cstr(root, "//li");
        TBOX_TEST_ASSERT(remaining.count == 2);
        TBOX_TEST_ASSERT(text_eq(remaining.items[0].node->first_child->text.text, "a"));
        TBOX_TEST_ASSERT(text_eq(remaining.items[1].node->first_child->text.text, "c"));
        tbox_xpath_node_set_destroy(&remaining);

        tbox_html_document_destroy(doc);
    }

    return failures;
}
