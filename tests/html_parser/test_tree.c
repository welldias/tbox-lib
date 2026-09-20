#include <tbox/html_parser.h>

#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
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
        tbox_html_document *doc    = parse_cstr("<p>Hello</p>");
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
        tbox_html_document *doc = parse_cstr("<p>a<br>b</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;
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
        tbox_html_document *doc   = parse_cstr("<div><p>x</p><p>y</p></div>");
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
        const tbox_html_node *a = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(a->element.attribute_count == 2);
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].name, "href"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].value, "http://x"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[1].name, "class"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[1].value, "y z"));
        tbox_html_document_destroy(doc);
    }

    /* 5: doctype + comment + element as root-level siblings, in order. */
    {
        tbox_html_document *doc    = parse_cstr("<!DOCTYPE html><!--c--><p></p>");
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
        tbox_html_document *doc   = parse_cstr("<div><p>text");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        const tbox_html_node *p = div->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "text"));
        tbox_html_document_destroy(doc);
    }

    /* 7: a closing tag implicitly closes unclosed ancestors. */
    {
        tbox_html_document *doc   = parse_cstr("<div><p>text</div>");
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
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(p->first_child != NULL);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "text"));
        TBOX_TEST_ASSERT(p->first_child == p->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 9: <style> content is raw text, not tokenized as markup. */
    {
        tbox_html_document *doc     = parse_cstr("<style>body { color: red; } </style><p>ok</p>");
        const tbox_html_node *root  = tbox_html_document_root(doc);
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
        tbox_html_document *doc   = parse_cstr("<DIV><P>x</p></DIV>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        const tbox_html_node *p = div->first_child;
        TBOX_TEST_ASSERT(text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "x"));
        tbox_html_document_destroy(doc);
    }

    /* 11: self-closing syntax on an arbitrary (non-void) tag is honored. */
    {
        tbox_html_document *doc      = parse_cstr("<custom-tag/>after");
        const tbox_html_node *root   = tbox_html_document_root(doc);
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

        tbox_html_document *space_doc    = parse_cstr("   ");
        const tbox_html_node *only_child = tbox_html_document_root(space_doc)->first_child;
        TBOX_TEST_ASSERT(only_child != NULL && only_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(only_child->text.text, "   "));
        tbox_html_document_destroy(space_doc);
    }

    /* 14: removing a middle child preserves the siblings around it. */
    {
        tbox_html_document *doc   = parse_cstr("<div><p>x</p><span>y</span><b>z</b></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p         = (tbox_html_node *)div->first_child;
        tbox_html_node *span      = (tbox_html_node *)p->next_sibling;
        tbox_html_node *b         = (tbox_html_node *)span->next_sibling;

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
        tbox_html_document *doc   = parse_cstr("<div><p>x</p><span>y</span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p         = (tbox_html_node *)div->first_child;
        tbox_html_node *span      = (tbox_html_node *)p->next_sibling;

        tbox_html_node_remove(p);

        TBOX_TEST_ASSERT(div->first_child == span);
        TBOX_TEST_ASSERT(span->prev_sibling == NULL);
        TBOX_TEST_ASSERT(div->last_child == span);
        tbox_html_document_destroy(doc);
    }

    /* 16: removing the last child. */
    {
        tbox_html_document *doc   = parse_cstr("<div><p>x</p><span>y</span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p         = (tbox_html_node *)div->first_child;
        tbox_html_node *span      = (tbox_html_node *)p->next_sibling;

        tbox_html_node_remove(span);

        TBOX_TEST_ASSERT(div->last_child == p);
        TBOX_TEST_ASSERT(p->next_sibling == NULL);
        TBOX_TEST_ASSERT(div->first_child == p);
        tbox_html_document_destroy(doc);
    }

    /* 17: removing an only child. */
    {
        tbox_html_document *doc   = parse_cstr("<div><p>x</p></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        tbox_html_node *p         = (tbox_html_node *)div->first_child;

        tbox_html_node_remove(p);

        TBOX_TEST_ASSERT(div->first_child == NULL);
        TBOX_TEST_ASSERT(div->last_child == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 18: a removed node can be re-attached under another parent. */
    {
        tbox_html_document *doc = parse_cstr("<div></div><section></section>");
        tbox_html_node *root    = (tbox_html_node *)tbox_html_document_root(doc);
        tbox_html_node *div1    = (tbox_html_node *)root->first_child;
        tbox_html_node *div2    = (tbox_html_node *)root->last_child;
        tbox_html_node *p       = tbox_html_node_create(doc, TBOX_HTML_NODE_ELEMENT);
        p->element.tag_name     = tbox_string_view_make("p", 1);
        tbox_html_node_append_child(div1, p);

        tbox_html_node_remove(p);
        TBOX_TEST_ASSERT(div1->first_child == NULL);

        tbox_html_node_append_child(div2, p);
        TBOX_TEST_ASSERT(p->parent == div2);
        TBOX_TEST_ASSERT(div2->first_child == p);
        tbox_html_document_destroy(doc);
    }

    /* 19: text_content on a simple element with direct text. */
    {
        tbox_html_document *doc = parse_cstr("<p>Hello</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        TBOX_TEST_ASSERT(text_eq(content, "Hello"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 20: text_content concatenates across a nested element in the middle,
     * dropping the element structure -- the ARCHITECTURE.md example. */
    {
        tbox_html_document *doc = parse_cstr("<p>oi <b>mundo</b></p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        TBOX_TEST_ASSERT(text_eq(content, "oi mundo"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 21: text_content walks multiple levels of nesting. */
    {
        tbox_html_document *doc   = parse_cstr("<div>a<span>b<em>c</em>d</span>e</div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, div);
        TBOX_TEST_ASSERT(text_eq(content, "abcde"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 22: text_content on a node with no text descendants returns an empty
     * view. */
    {
        tbox_html_document *doc   = parse_cstr("<div><span></span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, div);
        TBOX_TEST_ASSERT(content.size == 0);
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 23: a comment in the middle of the content is skipped, its text never
     * contributes. */
    {
        tbox_html_document *doc = parse_cstr("<p>oi <!--nope--> mundo</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        TBOX_TEST_ASSERT(text_eq(content, "oi  mundo"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 24: result stays valid independent of the source document's own
     * arena -- built entirely from the caller-supplied arena. */
    {
        tbox_html_document *doc = parse_cstr("<p>oi <b>mundo</b></p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        tbox_html_document_destroy(doc);
        TBOX_TEST_ASSERT(text_eq(content, "oi mundo"));
        tbox_arena_destroy(&arena);
    }

    /* 25: set_attribute on a node with no attributes creates the first
     * one. */
    {
        tbox_html_document *doc = parse_cstr("<div></div>");
        tbox_html_node *div     = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(div->element.attribute_count == 0);

        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("class"), tbox_string_view_from_cstr("box"));

        TBOX_TEST_ASSERT(div->element.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].name, "class"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].value, "box"));
        tbox_html_document_destroy(doc);
    }

    /* 26: set_attribute with an already-existing name replaces the value
     * in place, without duplicating the entry. */
    {
        tbox_html_document *doc = parse_cstr("<div class=\"off\"></div>");
        tbox_html_node *div     = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(div->element.attribute_count == 1);

        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("class"), tbox_string_view_from_cstr("on"));

        TBOX_TEST_ASSERT(div->element.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].name, "class"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].value, "on"));
        tbox_html_document_destroy(doc);
    }

    /* 27: set_attribute called repeatedly with different names accumulates
     * all of them. */
    {
        tbox_html_document *doc = parse_cstr("<div></div>");
        tbox_html_node *div     = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("id"), tbox_string_view_from_cstr("a"));
        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("class"), tbox_string_view_from_cstr("b"));
        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("data-x"), tbox_string_view_from_cstr("c"));

        TBOX_TEST_ASSERT(div->element.attribute_count == 3);
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].name, "id"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].value, "a"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[1].name, "class"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[1].value, "b"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[2].name, "data-x"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[2].value, "c"));
        tbox_html_document_destroy(doc);
    }

    /* 28: set_attribute name matching is case-insensitive -- setting
     * "CLASS" replaces the existing lowercase "class" attribute rather than
     * adding a second one. */
    {
        tbox_html_document *doc = parse_cstr("<div class=\"off\"></div>");
        tbox_html_node *div     = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("CLASS"), tbox_string_view_from_cstr("on"));

        TBOX_TEST_ASSERT(div->element.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].name, "class"));
        TBOX_TEST_ASSERT(text_eq(div->element.attributes[0].value, "on"));
        tbox_html_document_destroy(doc);
    }

    /* 29: get_attribute finds the right attribute with case-insensitive
     * name comparison. */
    {
        tbox_html_document *doc = parse_cstr("<a href=\"http://x\" class=\"y z\">link</a>");
        const tbox_html_node *a = tbox_html_document_root(doc)->first_child;

        const tbox_html_attribute *href = tbox_html_node_get_attribute(a, tbox_string_view_from_cstr("href"));
        TBOX_TEST_ASSERT(href != NULL && text_eq(href->value, "http://x"));

        const tbox_html_attribute *href_ci = tbox_html_node_get_attribute(a, tbox_string_view_from_cstr("HREF"));
        TBOX_TEST_ASSERT(href_ci != NULL && text_eq(href_ci->value, "http://x"));

        const tbox_html_attribute *class_attr = tbox_html_node_get_attribute(a, tbox_string_view_from_cstr("class"));
        TBOX_TEST_ASSERT(class_attr != NULL && text_eq(class_attr->value, "y z"));
        tbox_html_document_destroy(doc);
    }

    /* 30: get_attribute returns NULL for a missing name. */
    {
        tbox_html_document *doc   = parse_cstr("<div class=\"box\"></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(tbox_html_node_get_attribute(div, tbox_string_view_from_cstr("id")) == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 31: get_attribute returns NULL for a non-ELEMENT node, even when the
     * name would otherwise match something. */
    {
        tbox_html_document *doc    = parse_cstr("<p>Hello</p>");
        const tbox_html_node *p    = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *text = p->first_child;
        TBOX_TEST_ASSERT(text->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(tbox_html_node_get_attribute(text, tbox_string_view_from_cstr("class")) == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 32: set_attribute on a non-ELEMENT node is a no-op (doesn't crash,
     * doesn't touch the union's other members). */
    {
        tbox_html_document *doc = parse_cstr("<p>Hello</p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        tbox_html_node *text    = (tbox_html_node *)p->first_child;
        TBOX_TEST_ASSERT(text->type == TBOX_HTML_NODE_TEXT);

        tbox_html_node_set_attribute(doc, text, tbox_string_view_from_cstr("class"), tbox_string_view_from_cstr("box"));

        TBOX_TEST_ASSERT(text_eq(text->text.text, "Hello"));
        tbox_html_document_destroy(doc);
    }

    /* 33: an attribute set via set_attribute looks exactly like a parsed
     * one from every other angle -- reachable through
     * node->element.attributes/attribute_count and through get_attribute,
     * with no special-casing anywhere else. */
    {
        tbox_html_document *doc = parse_cstr("<div class=\"off\"></div>");
        tbox_html_node *div     = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_attribute(doc, div, tbox_string_view_from_cstr("data-count"), tbox_string_view_from_cstr("1"));

        TBOX_TEST_ASSERT(div->element.attribute_count == 2);
        const tbox_html_attribute *found = tbox_html_node_get_attribute(div, tbox_string_view_from_cstr("data-count"));
        TBOX_TEST_ASSERT(found == &div->element.attributes[1]);
        TBOX_TEST_ASSERT(text_eq(found->name, "data-count"));
        TBOX_TEST_ASSERT(text_eq(found->value, "1"));
        tbox_html_document_destroy(doc);
    }

    /* 34: set_text_content on an ELEMENT with no children creates a single
     * TEXT child with the given text. */
    {
        tbox_html_document *doc = parse_cstr("<p></p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(p->first_child == NULL);

        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr("hi"));

        TBOX_TEST_ASSERT(p->first_child != NULL && p->first_child == p->last_child);
        TBOX_TEST_ASSERT(p->first_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "hi"));
        TBOX_TEST_ASSERT(p->first_child->parent == p);
        tbox_html_document_destroy(doc);
    }

    /* 35: set_text_content on an ELEMENT with existing mixed TEXT/ELEMENT
     * children (matching <p>oi <b>mundo</b></p>'s shape) replaces ALL of
     * them with the single new TEXT node -- text_content afterwards
     * returns only the new text, nothing from before. */
    {
        tbox_html_document *doc = parse_cstr("<p>oi <b>mundo</b></p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr("bye"));

        TBOX_TEST_ASSERT(p->first_child != NULL && p->first_child == p->last_child);
        TBOX_TEST_ASSERT(p->first_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "bye"));

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        TBOX_TEST_ASSERT(text_eq(content, "bye"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 36: calling set_text_content twice in a row replaces the text again
     * rather than accumulating -- the second call's result has only the
     * second text. */
    {
        tbox_html_document *doc = parse_cstr("<p>original</p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr("first"));
        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr("second"));

        TBOX_TEST_ASSERT(p->first_child != NULL && p->first_child == p->last_child);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "second"));

        tbox_arena arena         = tbox_arena_create(0);
        tbox_string_view content = tbox_html_node_text_content(&arena, p);
        TBOX_TEST_ASSERT(text_eq(content, "second"));
        tbox_arena_destroy(&arena);
        tbox_html_document_destroy(doc);
    }

    /* 37: set_text_content is a no-op on a TEXT node -- doesn't crash,
     * doesn't corrupt the union's text.text member. */
    {
        tbox_html_document *doc = parse_cstr("<p>Hello</p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        tbox_html_node *text    = (tbox_html_node *)p->first_child;
        TBOX_TEST_ASSERT(text->type == TBOX_HTML_NODE_TEXT);

        tbox_html_node_set_text_content(doc, text, tbox_string_view_from_cstr("nope"));

        TBOX_TEST_ASSERT(text->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(text->text.text, "Hello"));
        TBOX_TEST_ASSERT(text->first_child == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 38: set_text_content is a no-op on a COMMENT node. */
    {
        tbox_html_document *doc = parse_cstr("<!--c--><p></p>");
        tbox_html_node *comment = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(comment->type == TBOX_HTML_NODE_COMMENT);

        tbox_html_node_set_text_content(doc, comment, tbox_string_view_from_cstr("nope"));

        TBOX_TEST_ASSERT(comment->type == TBOX_HTML_NODE_COMMENT);
        TBOX_TEST_ASSERT(text_eq(comment->text.text, "c"));
        tbox_html_document_destroy(doc);
    }

    /* 39: set_text_content is a no-op on a DOCTYPE node. */
    {
        tbox_html_document *doc = parse_cstr("<!DOCTYPE html><p></p>");
        tbox_html_node *doctype = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(doctype->type == TBOX_HTML_NODE_DOCTYPE);

        tbox_html_node_set_text_content(doc, doctype, tbox_string_view_from_cstr("nope"));

        TBOX_TEST_ASSERT(doctype->type == TBOX_HTML_NODE_DOCTYPE);
        TBOX_TEST_ASSERT(text_eq(doctype->text.text, "html"));
        tbox_html_document_destroy(doc);
    }

    /* 40: set_text_content is a no-op on the DOCUMENT root. */
    {
        tbox_html_document *doc = parse_cstr("<p></p>");
        tbox_html_node *root    = (tbox_html_node *)tbox_html_document_root(doc);
        TBOX_TEST_ASSERT(root->type == TBOX_HTML_NODE_DOCUMENT);
        tbox_html_node *original_first_child = root->first_child;

        tbox_html_node_set_text_content(doc, root, tbox_string_view_from_cstr("nope"));

        TBOX_TEST_ASSERT(root->type == TBOX_HTML_NODE_DOCUMENT);
        TBOX_TEST_ASSERT(root->first_child == original_first_child);
        tbox_html_document_destroy(doc);
    }

    /* 41: set_text_content with an empty string still creates a TEXT child,
     * just with an empty text view. */
    {
        tbox_html_document *doc = parse_cstr("<p>Hello</p>");
        tbox_html_node *p       = (tbox_html_node *)tbox_html_document_root(doc)->first_child;

        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr(""));

        TBOX_TEST_ASSERT(p->first_child != NULL && p->first_child == p->last_child);
        TBOX_TEST_ASSERT(p->first_child->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(p->first_child->text.text.size == 0);
        tbox_html_document_destroy(doc);
    }

    /* 42: set_text_content on a node that already has exactly one TEXT
     * child still replaces it with a fresh node (not mutated in place). */
    {
        tbox_html_document *doc  = parse_cstr("<p>Hello</p>");
        tbox_html_node *p        = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        tbox_html_node *old_text = (tbox_html_node *)p->first_child;

        tbox_html_node_set_text_content(doc, p, tbox_string_view_from_cstr("World"));

        TBOX_TEST_ASSERT(p->first_child != NULL && p->first_child == p->last_child);
        TBOX_TEST_ASSERT(p->first_child != old_text);
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "World"));
        tbox_html_document_destroy(doc);
    }

    /* 43: two unclosed <p> tags in a row become siblings, not nested --
     * each keeps its own text. */
    {
        tbox_html_document *doc    = parse_cstr("<p>primeiro<p>segundo</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        const tbox_html_node *p1 = root->first_child;
        TBOX_TEST_ASSERT(p1 != NULL && text_eq(p1->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p1->first_child->text.text, "primeiro"));
        TBOX_TEST_ASSERT(p1->first_child == p1->last_child);

        const tbox_html_node *p2 = p1->next_sibling;
        TBOX_TEST_ASSERT(p2 != NULL && p2->type == TBOX_HTML_NODE_ELEMENT && text_eq(p2->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p2->first_child->text.text, "segundo"));
        TBOX_TEST_ASSERT(p2 == root->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 44: a p-closing start tag (<div>) inside an open <p> closes the <p>
     * first -- the inner <div> becomes a sibling of <p>, not its child. */
    {
        tbox_html_document *doc  = parse_cstr("<div><p>texto<div>outro</div></p></div>");
        tbox_html_node *outer    = (tbox_html_node *)tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(outer->element.tag_name, "div"));

        const tbox_html_node *p = outer->first_child;
        TBOX_TEST_ASSERT(p != NULL && text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "texto"));
        TBOX_TEST_ASSERT(p->first_child == p->last_child);

        const tbox_html_node *inner = p->next_sibling;
        TBOX_TEST_ASSERT(inner != NULL && inner->type == TBOX_HTML_NODE_ELEMENT && text_eq(inner->element.tag_name, "div"));
        TBOX_TEST_ASSERT(text_eq(inner->first_child->text.text, "outro"));
        TBOX_TEST_ASSERT(inner == outer->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 45: an unclosed <ul><li> list produces sibling <li>s, all direct
     * children of <ul>. */
    {
        tbox_html_document *doc    = parse_cstr("<ul><li>um<li>dois<li>três</ul>");
        const tbox_html_node *ul   = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(ul->element.tag_name, "ul"));

        const tbox_html_node *li1 = ul->first_child;
        TBOX_TEST_ASSERT(li1 != NULL && text_eq(li1->element.tag_name, "li"));
        TBOX_TEST_ASSERT(text_eq(li1->first_child->text.text, "um"));

        const tbox_html_node *li2 = li1->next_sibling;
        TBOX_TEST_ASSERT(li2 != NULL && text_eq(li2->element.tag_name, "li"));
        TBOX_TEST_ASSERT(text_eq(li2->first_child->text.text, "dois"));

        const tbox_html_node *li3 = li2->next_sibling;
        TBOX_TEST_ASSERT(li3 != NULL && text_eq(li3->element.tag_name, "li"));
        TBOX_TEST_ASSERT(text_eq(li3->first_child->text.text, "três"));
        TBOX_TEST_ASSERT(li3 == ul->last_child);
        TBOX_TEST_ASSERT(li3->next_sibling == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 46: <li> also closes an open <p> (not just another <li>). */
    {
        tbox_html_document *doc    = parse_cstr("<p>x<li>y</p>");
        const tbox_html_node *root = tbox_html_document_root(doc);

        const tbox_html_node *p = root->first_child;
        TBOX_TEST_ASSERT(p != NULL && text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "x"));
        TBOX_TEST_ASSERT(p->first_child == p->last_child);

        const tbox_html_node *li = p->next_sibling;
        TBOX_TEST_ASSERT(li != NULL && li->type == TBOX_HTML_NODE_ELEMENT && text_eq(li->element.tag_name, "li"));
        TBOX_TEST_ASSERT(text_eq(li->first_child->text.text, "y"));
        TBOX_TEST_ASSERT(li == root->last_child);
        tbox_html_document_destroy(doc);
    }

    /* 47: regression -- cross-nested closing (<div><span></div>) still
     * closes both, unchanged since v0. */
    {
        tbox_html_document *doc  = parse_cstr("<div><span></div>");
        const tbox_html_node *div = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(text_eq(div->element.tag_name, "div"));
        TBOX_TEST_ASSERT(div->next_sibling == NULL);

        const tbox_html_node *span = div->first_child;
        TBOX_TEST_ASSERT(span != NULL && text_eq(span->element.tag_name, "span"));
        TBOX_TEST_ASSERT(span->first_child == NULL);
        tbox_html_document_destroy(doc);
    }

    /* 48: named and numeric (decimal + hex) entities decode correctly in
     * text content. */
    {
        tbox_html_document *doc = parse_cstr("<p>&amp; &lt; &nbsp; &mdash; &#233; &#xE9;</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *t = p->first_child;
        TBOX_TEST_ASSERT(t != NULL && t->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(t->text.text, "& < \xC2\xA0 \xE2\x80\x94 \xC3\xA9 \xC3\xA9"));
        tbox_html_document_destroy(doc);
    }

    /* 49: an entity inside an attribute value also decodes. */
    {
        tbox_html_document *doc = parse_cstr("<a title=\"Tom &amp; Jerry\">x</a>");
        const tbox_html_node *a = tbox_html_document_root(doc)->first_child;
        TBOX_TEST_ASSERT(a->element.attribute_count == 1);
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].name, "title"));
        TBOX_TEST_ASSERT(text_eq(a->element.attributes[0].value, "Tom & Jerry"));
        tbox_html_document_destroy(doc);
    }

    /* 50: a malformed/unrecognized reference is left as literal text,
     * without disturbing the surrounding text. */
    {
        tbox_html_document *doc = parse_cstr("<p>a&naoexiste;b c&ampd e&#;f</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *t = p->first_child;
        TBOX_TEST_ASSERT(t != NULL && t->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(t->text.text, "a&naoexiste;b c&ampd e&#;f"));
        tbox_html_document_destroy(doc);
    }

    /* 51: an invalid numeric codepoint (0, surrogate, out of range) becomes
     * U+FFFD, and the result stays valid UTF-8. */
    {
        tbox_html_document *doc = parse_cstr("<p>&#0;&#xD800;&#99999999;</p>");
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child;
        const tbox_html_node *t = p->first_child;
        TBOX_TEST_ASSERT(t != NULL && t->type == TBOX_HTML_NODE_TEXT);
        TBOX_TEST_ASSERT(text_eq(t->text.text, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"));
        TBOX_TEST_ASSERT(tbox_string_view_valid_utf8(t->text.text));
        tbox_html_document_destroy(doc);
    }

    /* 52: <script> content keeps "&amp;" literal (raw text never decodes),
     * unlike the same text outside a <script>. */
    {
        tbox_html_document *doc       = parse_cstr("<script>a &amp; b</script><p>a &amp; b</p>");
        const tbox_html_node *root    = tbox_html_document_root(doc);
        const tbox_html_node *script  = root->first_child;
        TBOX_TEST_ASSERT(text_eq(script->element.tag_name, "script"));
        TBOX_TEST_ASSERT(text_eq(script->first_child->text.text, "a &amp; b"));

        const tbox_html_node *p = script->next_sibling;
        TBOX_TEST_ASSERT(p != NULL && text_eq(p->element.tag_name, "p"));
        TBOX_TEST_ASSERT(text_eq(p->first_child->text.text, "a & b"));
        tbox_html_document_destroy(doc);
    }

    /* 53: a comment keeps "&amp;" literal -- comments never decode. */
    {
        tbox_html_document *doc     = parse_cstr("<!-- a &amp; b -->");
        const tbox_html_node *root  = tbox_html_document_root(doc);
        const tbox_html_node *comment = root->first_child;
        TBOX_TEST_ASSERT(comment->type == TBOX_HTML_NODE_COMMENT);
        TBOX_TEST_ASSERT(text_eq(comment->text.text, " a &amp; b "));
        tbox_html_document_destroy(doc);
    }

    return failures;
}
