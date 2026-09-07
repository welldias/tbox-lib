#include <tbox/html_parser.h>

#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

static tbox_html_document *parse_cstr(const char *html) {
    return tbox_html_parse(html, strlen(html));
}

int tbox_test_html_parser_tree_run(void) {
    int failures = 0;

    /* 1: simple element with text. */
    {
        tbox_html_document *doc  = parse_cstr("<p>Hello</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        TBOX_TEST_ASSERT(root->type == TBOX_HTML_NODE_DOCUMENT);

        const tbox_html_node *p = root->first_child;
        TBOX_TEST_ASSERT(p != NULL && p->type == TBOX_HTML_NODE_ELEMENT);
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(p->next_sibling == NULL);

        const tbox_html_node *text = p->first_child;
        TBOX_TEST_ASSERT(text != NULL && text->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(text->text.text, "Hello"));
        tbox_html_document_destroy(doc);
    }

    /* 2: void element in the middle doesn't get pushed / doesn't get
     * children. */
    {
        tbox_html_document *doc  = parse_cstr("<p>a<br>b</p>");
        const tbox_html_node *p   = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));

        const tbox_html_node *a = p->first_child;
        TBOX_TEST_ASSERT(a != NULL && a->type == TBOX_HTML_NODE_TEXT && text_eq(a->text.text, "a"));

        const tbox_html_node *br = a->next_sibling;
        TBOX_TEST_ASSERT(br != NULL && br->type == TBOX_HTML_NODE_ELEMENT);
        TBOX_TEST_ASSERT(text_eq(br->element.tag_name, "br"));
        TBOX_TEST_ASSERT(br->first_child == NULL);

        const tbox_html_node *b = br->next_sibling;
        TBOX_TEST_ASSERT(b != NULL && b->type == TBOX_HTML_NODE_TEXT && text_eq(b->text.text, "b"));
        TBOX_TEST_ASSERT(b->next_sibling == NULL);
        TBOX_TEST_ASSERT(p->last_child == b);
        tbox_html_document_destroy(doc);
    }

    /* 3: siblings link both ways. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p><p>y</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *p1  = div->first_child;
        const tbox_html_node *p2  = div->last_child;
        TBOX_TEST_ASSERT(p1 != p2);
        TBOX_TEST_ASSERT(p1->next_sibling == p2);
        TBOX_TEST_ASSERT(p2->prev_sibling == p1);
        TBOX_TEST_ASSERT(text_eq(p1->first_child->text.text, "x"));
        TBOX_TEST_ASSERT(text_eq(p2->first_child->text.text, "y"));
        tbox_html_document_destroy(doc);
    }

    /* 4: multiple attributes. */
    {
        tbox_html_document *doc = parse_cstr("<a href=\"http://x\" class=\"y z\">link</a>");
        const tbox_html_node *a  = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(a->element.attribute_count == 2);
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].name, "href"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].value, "http://x"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[1].name, "class"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[1].value, "y z"));
        tbox_html_document_destroy(doc);
    }

    /* 5: doctype + comment + element as root-level siblings, in order. */
    {
        tbox_html_document *doc = parse_cstr("<!DOCTYPE html><!--c--><p></p>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        const tbox_html_node *doctype = root->first_child;
        TBOX_TEST_ASSERT(doctype->type == TBOX_HTML_NODE_DOCTYPE && text_eq(doctype->text.text, "html"));

        const tbox_html_node *comment = doctype->next_sibling;
        TBOX_TEST_ASSERT(comment->type == TBOX_HTML_NODE_COMMENT && text_eq(comment->text.text, "c"));

        const tbox_html_node *p = comment->next_sibling;
        TBOX_TEST_ASSERT(p->type == TBOX_HTML_NODE_ELEMENT && text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(p->first_child == NULL);
        TBOX_TEST_ASSERT(p == root->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 6: unclosed tags at EOF still produce the correct nesting. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>text");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        const tbox_html_node *p = div->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "text"));
        tbox_html_document_destroy(doc);
    }

    /* 7: a closing tag implicitly closes unclosed ancestors. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>text</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        TBOX_TEST_ASSERT(div->next_sibling == NULL);
        const tbox_html_node *p = div->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "text"));
        tbox_html_document_destroy(doc);
    }

    /* 8: an orphan end tag is ignored. */
    {
        tbox_html_document *doc = parse_cstr("<p>text</b></p>");
        const tbox_html_node *p  = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(p->first_child != NULL);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "text"));
        TBOX_TEST_ASSERT(p->first_child == p->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 9: <style> content is raw text, not tokenized as markup. */
    {
        tbox_html_document *doc = parse_cstr("<style>body { color: red; } </style><p>ok</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *style = root->first_child;
        TBOX_TEST_ASSERT(text_eq(style->element.tag_name, "style"));
        TBOX_TEST_ASSERT(style->first_child != NULL && style->first_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(style->first_child->text.text, "body { color: red; } "));

        const tbox_html_node *p = style->next_sibling;
        TBOX_TEST_ASSERT(p != NULL && text_eq(p->element.tag_name, "p"));
        tbox_html_document_destroy(doc);
    }

    /* 10: tag names are normalized to lowercase; closing works across case. */
    {
        tbox_html_document *doc = parse_cstr("<DIV><P>x</p></DIV>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        const tbox_html_node *p = div->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "x"));
        tbox_html_document_destroy(doc);
    }

    /* 11: self-closing syntax on an arbitrary (non-void) tag is honored. */
    {
        tbox_html_document *doc = parse_cstr("<custom-tag/>after");
        const tbox_html_node *root = tbox_html_document_root(doc);
        const tbox_html_node *custom = root->first_child;
        TBOX_TEST_ASSERT(text_eq(custom->element.tag_name, "custom-tag"));
        TBOX_TEST_ASSERT(custom->element.self_closing);
        TBOX_TEST_ASSERT(custom->first_child == NULL);

        const tbox_html_node *after = custom->next_sibling;
        TBOX_TEST_ASSERT(after != NULL && after->type == TBOX_HTML_NODE_TEXT && text_eq(after->text.text, "after"));
        TBOX_TEST_ASSERT(after->parent == root);
        tbox_html_document_destroy(doc);
    }

    /* 12: destroying a parsed document doesn't crash (checked under ASan in
     * CI/local sanitizer builds). */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p></div>");
        tbox_html_document_destroy(doc);
    }

    /* 13: empty and whitespace-only input. */
    {
        tbox_html_document *empty_doc = parse_cstr("");
        TBOX_TEST_ASSERT(tbox_html_document_root(empty_doc)->first_child == NULL);
        tbox_html_document_destroy(empty_doc);

        tbox_html_document *space_doc = parse_cstr("   ");
        const tbox_html_node *only_child = tbox_html_document_root(space_doc)->first_child;
        TBOX_TEST_ASSERT(only_child != NULL && only_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(only_child->text.text, "   "));
        tbox_html_document_destroy(space_doc);
    }

    /* 14: removing a middle child preserves the siblings around it. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p><span>y</span><b>z</b></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p          = (tbox_html_node *)div->first_child;
        tbox_html_node *span       = (tbox_html_node *)p->next_sibling;
        tbox_html_node *b          = (tbox_html_node *)span->next_sibling;

        tbox_html_node_remove(span);

        TBOX_TEST_ASSERT(p->next_sibling == b);
        TBOX_TEST_ASSERT(b->prev_sibling == p);
        TBOX_TEST_ASSERT(div->first_child == p);
        TBOX_TEST_ASSERT(div->last_child == b);
        TBOX_TEST_ASSERT(span->parent == NULL && span->next_sibling == NULL && span->prev_sibling == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 15: removing the first child. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p><span>y</span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p          = (tbox_html_node *)div->first_child;
        tbox_html_node *span       = (tbox_html_node *)p->next_sibling;

        tbox_html_node_remove(p);

        TBOX_TEST_ASSERT(div->first_child == span);
        TBOX_TEST_ASSERT(span->prev_sibling == NULL);
        TBOX_TEST_ASSERT(div->last_child == span);
        tbox_html_document_destroy(doc);
    }

    /* 16: removing the last child. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p><span>y</span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p          = (tbox_html_node *)div->first_child;
        tbox_html_node *span       = (tbox_html_node *)p->next_sibling;

        tbox_html_node_remove(span);

        TBOX_TEST_ASSERT(div->last_child == p);
        TBOX_TEST_ASSERT(p->next_sibling == NULL);
        TBOX_TEST_ASSERT(div->first_child == p);
        tbox_html_document_destroy(doc);
    }

    /* 17: removing an only child. */
    {
        tbox_html_document *doc = parse_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p          = (tbox_html_node *)div->first_child;

        tbox_html_node_remove(p);

        TBOX_TEST_ASSERT(div->first_child == NULL);
        TBOX_TEST_ASSERT(div->last_child == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 18: a removed node can be re-attached under another parent. */
    {
        tbox_html_document *doc = parse_cstr("<div></div><section></section>");
        tbox_html_node *root      = (tbox_html_node *)tbox_html_document_root(doc);
        tbox_html_node *div1      = (tbox_html_node *)root->first_child;
        tbox_html_node *div2      = (tbox_html_node *)root->last_child;
        tbox_html_node *p         = tbox_html_node_create(doc, TBOX_HTML_NODE_ELEMENT);
        p->element.tag_name       = tbox_string_view_make("p", 1);
        tbox_html_node_append_child(div1, p);

        tbox_html_node_remove(p);
        TBOX_TEST_ASSERT(div1->first_child == NULL);

        tbox_html_node_append_child(div2, p);
        TBOX_TEST_ASSERT(p->parent == div2);
        TBOX_TEST_ASSERT(div2->first_child == p);
        tbox_html_document_destroy(doc);
    }

    return failures;
}
