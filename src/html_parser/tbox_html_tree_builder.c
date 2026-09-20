#include "tbox_html_tree_builder.h"

#include "base/tbox_string.h"
#include "tbox_html_entities.h"
#include "tbox_html_node.h"

/* HTML5 "in body" insertion-mode list of start tags that implicitly close an
 * open <p> -- includes "li" (opening an <li> also closes an open <p>, on top
 * of separately closing an open <li>, see tbox_html_tree_builder_handle_start_tag). */
static const char *const tbox_html_p_closing_tags[] = {
    "address", "article", "aside", "blockquote", "details", "div", "dl", "fieldset", "figcaption", "figure", "footer", "form",
    "h1",      "h2",      "h3",    "h4",         "h5",      "h6",  "header", "hgroup", "hr", "li", "main", "menu",
    "nav",     "ol",      "p",     "pre",        "section", "table", "ul",
};

void tbox_html_tree_builder_init(tbox_html_tree_builder *builder, const char *input, size_t length, tbox_html_document *document) {
    builder->document = document;
    tbox_html_tokenizer_init(&builder->tokenizer, input, length, &document->arena);
    tbox_vector_init(&builder->open_elements, &document->arena, sizeof(tbox_html_node *), 0);
}

static void tbox_html_tree_builder_push(tbox_html_tree_builder *builder, tbox_html_node *node) {
    tbox_html_node **slot = tbox_vector_push(&builder->open_elements);
    *slot                 = node;
}

static tbox_html_node *tbox_html_tree_builder_top(tbox_html_tree_builder *builder) {
    size_t length = tbox_vector_length(&builder->open_elements);
    return *(tbox_html_node **)tbox_vector_at(&builder->open_elements, length - 1);
}

static bool tbox_html_tree_builder_find_matching(tbox_html_tree_builder *builder, tbox_string_view name, size_t *out_index) {
    size_t length = tbox_vector_length(&builder->open_elements);
    for (size_t i = length; i-- > 0;) {
        tbox_html_node *node = *(tbox_html_node **)tbox_vector_at(&builder->open_elements, i);
        if (node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_ascii_ci(node->element.tag_name, name)) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static tbox_string_view tbox_html_tree_builder_copy(tbox_html_document *document, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, &document->arena, view.size);
    tbox_string_builder_append_view(&builder, view);
    return tbox_string_builder_finish(&builder);
}

static tbox_string_view tbox_html_tree_builder_copy_lower(tbox_html_document *document, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, &document->arena, view.size);
    tbox_string_builder_append_view_lower_ascii(&builder, view);
    return tbox_string_builder_finish(&builder);
}

static tbox_string_view tbox_html_tree_builder_copy_decoded(tbox_html_document *document, tbox_string_view view) {
    return tbox_html_decode_entities(&document->arena, view);
}

static bool tbox_html_tree_builder_is_raw_text_parent(const tbox_html_node *top) {
    return top->type == TBOX_HTML_NODE_ELEMENT &&
           (tbox_string_view_equal_cstr(top->element.tag_name, "script") || tbox_string_view_equal_cstr(top->element.tag_name, "style"));
}

static void tbox_html_tree_builder_handle_leaf(tbox_html_tree_builder *builder, tbox_html_node_type type, tbox_string_view content, tbox_html_node *top,
                                                tbox_string_view (*copy_fn)(tbox_html_document *, tbox_string_view)) {
    tbox_html_node *node = tbox_html_node_create(builder->document, type);
    node->text.text      = copy_fn(builder->document, content);
    tbox_html_node_append_child(top, node);
}

static bool tbox_html_tree_builder_is_p_closing_tag(tbox_string_view tag_name) {
    for (size_t i = 0; i < sizeof(tbox_html_p_closing_tags) / sizeof(tbox_html_p_closing_tags[0]); i++) {
        if (tbox_string_view_equal_cstr(tag_name, tbox_html_p_closing_tags[i])) {
            return true;
        }
    }
    return false;
}

static void tbox_html_tree_builder_handle_start_tag(tbox_html_tree_builder *builder, const tbox_html_token *token) {
    tbox_html_document *document = builder->document;

    tbox_string_view tag_name = tbox_html_tree_builder_copy_lower(document, token->text);

    size_t p_index;
    if (tbox_html_tree_builder_is_p_closing_tag(tag_name) && tbox_html_tree_builder_find_matching(builder, tbox_string_view_from_cstr("p"), &p_index)) {
        builder->open_elements.length = p_index;
    }

    size_t li_index;
    if (tbox_string_view_equal_cstr(tag_name, "li") && tbox_html_tree_builder_find_matching(builder, tbox_string_view_from_cstr("li"), &li_index)) {
        builder->open_elements.length = li_index;
    }

    tbox_html_node *top = tbox_html_tree_builder_top(builder);

    tbox_html_node *node = tbox_html_node_create(document, TBOX_HTML_NODE_ELEMENT);

    node->element.tag_name = tag_name;

    bool is_void               = tbox_html_is_void_element(tag_name);
    node->element.self_closing = token->self_closing || is_void;

    tbox_vector attributes;
    tbox_vector_init(&attributes, &document->arena, sizeof(tbox_html_attribute), token->attribute_count);

    for (size_t i = 0; i < token->attribute_count; i++) {
        const tbox_html_token_attribute *source = &token->attributes[i];
        tbox_string_view name                   = tbox_html_tree_builder_copy_lower(document, source->name);

        bool duplicate = false;
        for (size_t j = 0; j < tbox_vector_length(&attributes); j++) {
            const tbox_html_attribute *existing = tbox_vector_at(&attributes, j);
            if (tbox_string_view_equal(existing->name, name)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        tbox_html_attribute *attribute = tbox_vector_push(&attributes);
        attribute->name                = name;
        attribute->value               = tbox_html_tree_builder_copy_decoded(document, source->value);
    }

    node->element.attributes      = attributes.data;
    node->element.attribute_count = attributes.length;

    tbox_html_node_append_child(top, node);

    if (!is_void && !token->self_closing) {
        tbox_html_tree_builder_push(builder, node);
    }

    if (tbox_string_view_equal_cstr(tag_name, "script") || tbox_string_view_equal_cstr(tag_name, "style")) {
        tbox_html_tokenizer_enter_raw_text(&builder->tokenizer, tag_name);
    }
}

tbox_html_node *tbox_html_tree_builder_run(tbox_html_tree_builder *builder) {
    tbox_html_document *document  = builder->document;
    tbox_html_node *document_node = tbox_html_node_create(document, TBOX_HTML_NODE_DOCUMENT);

    document->root = document_node;

    tbox_html_tree_builder_push(builder, document_node);

    tbox_html_token token;
    while (tbox_html_tokenizer_next(&builder->tokenizer, &token) && token.type != TBOX_HTML_TOKEN_EOF) {
        tbox_html_node *top = tbox_html_tree_builder_top(builder);

        switch (token.type) {
        case TBOX_HTML_TOKEN_DOCTYPE:
            tbox_html_tree_builder_handle_leaf(builder, TBOX_HTML_NODE_DOCTYPE, token.text, top, tbox_html_tree_builder_copy);
            break;
        case TBOX_HTML_TOKEN_COMMENT:
            tbox_html_tree_builder_handle_leaf(builder, TBOX_HTML_NODE_COMMENT, token.text, top, tbox_html_tree_builder_copy);
            break;
        case TBOX_HTML_TOKEN_TEXT:
            if (token.text.size > 0) {
                bool raw_text = tbox_html_tree_builder_is_raw_text_parent(top);
                tbox_html_tree_builder_handle_leaf(builder, TBOX_HTML_NODE_TEXT, token.text, top,
                                                    raw_text ? tbox_html_tree_builder_copy : tbox_html_tree_builder_copy_decoded);
            }
            break;
        case TBOX_HTML_TOKEN_START_TAG:
            tbox_html_tree_builder_handle_start_tag(builder, &token);
            break;
        case TBOX_HTML_TOKEN_END_TAG: {
            size_t match_index;
            if (tbox_html_tree_builder_find_matching(builder, token.text, &match_index)) {
                builder->open_elements.length = match_index;
            }
            break;
        }
        case TBOX_HTML_TOKEN_EOF:
            break;
        }
    }

    return document_node;
}
