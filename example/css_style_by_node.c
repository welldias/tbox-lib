#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <tbox/tbox.h>

#ifndef TBOX_EXAMPLE_HTML_PATH
#define TBOX_EXAMPLE_HTML_PATH "index.html"
#endif
#ifndef TBOX_EXAMPLE_CSS_PATH
#define TBOX_EXAMPLE_CSS_PATH "style.css"
#endif

/* Reads the whole file into a malloc'd buffer (not NUL-terminated: both
 * tbox_html_parse and tbox_css_parse take an explicit length). Caller frees
 * the buffer with free(). Returns NULL and leaves *out_size untouched on any
 * I/O failure. */
static char *read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)size);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (bytes_read != (size_t)size) {
        free(buffer);
        return NULL;
    }

    *out_size = (size_t)size;
    return buffer;
}

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

/* Prints every declaration, from every ruleset in `stylesheet` that has at
 * least one selector matching `node`, in stylesheet source order -- the same
 * cascade order a browser would consider before resolving
 * specificity/!important. Uses tbox_css_selector_matches, the lowest-level
 * primitive documented in <tbox/css_selector.h>, directly against each
 * ruleset's selectors -- no node_set/match_set involved. */
static void print_matching_declarations(const tbox_css_stylesheet *stylesheet, const tbox_html_node *node, int depth) {
    size_t ruleset_count             = tbox_css_stylesheet_ruleset_count(stylesheet);
    const tbox_css_ruleset *rulesets = tbox_css_stylesheet_rulesets(stylesheet);

    for (size_t i = 0; i < ruleset_count; i++) {
        const tbox_css_ruleset *ruleset = &rulesets[i];

        bool matched = false;
        for (size_t j = 0; j < ruleset->selector_count && !matched; j++) {
            matched = tbox_css_selector_matches(&ruleset->selectors[j], node);
        }
        if (!matched) {
            continue;
        }

        for (size_t k = 0; k < ruleset->declaration_count; k++) {
            const tbox_css_declaration *declaration = &ruleset->declarations[k];
            print_indent(depth + 1);
            printf("style: %.*s: %.*s;\n", (int)declaration->property.size, declaration->property.data, (int)declaration->value.size, declaration->value.data);
        }
    }
}

/* Walks the tree in document (pre-order) order -- the same first_child/
 * next_sibling recursion example/html_parser.c uses, since the library has no
 * generic tree iterator -- printing each ELEMENT node's own markup (tag +
 * attributes) immediately followed by the CSS declarations that apply to
 * *that* element. This is the same underlying match primitive
 * example/css_selector.c's apply_stylesheet() uses via
 * tbox_css_selector_match_stylesheet, but grouped by node instead of by
 * ruleset: here every element carries its own style alongside it as the tree
 * is walked, instead of one summary line per ruleset. Text/comment/doctype
 * nodes carry no style and are skipped (they are always leaves, so skipping
 * them loses no children). */
static void print_node_with_style(const tbox_css_stylesheet *stylesheet, const tbox_html_node *node, int depth) {
    if (node->type == TBOX_HTML_NODE_DOCUMENT) {
        print_indent(depth);
        printf("#document\n");
    } else if (node->type == TBOX_HTML_NODE_ELEMENT) {
        print_indent(depth);
        printf("<%.*s", (int)node->element.tag_name.size, node->element.tag_name.data);
        for (size_t i = 0; i < node->element.attribute_count; i++) {
            const tbox_html_attribute *attribute = &node->element.attributes[i];
            printf(" %.*s=\"%.*s\"", (int)attribute->name.size, attribute->name.data, (int)attribute->value.size, attribute->value.data);
        }
        printf(">\n");

        print_matching_declarations(stylesheet, node, depth);
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        print_node_with_style(stylesheet, child, depth + 1);
    }
}

int main(void) {
    size_t html_size;
    char *html = read_file(TBOX_EXAMPLE_HTML_PATH, &html_size);
    if (html == NULL) {
        fprintf(stderr, "failed to read %s\n", TBOX_EXAMPLE_HTML_PATH);
        return 1;
    }

    tbox_html_document *document = tbox_html_parse(html, html_size);
    free(html);
    if (document == NULL) {
        fprintf(stderr, "failed to parse HTML\n");
        return 1;
    }

    size_t css_size;
    char *css = read_file(TBOX_EXAMPLE_CSS_PATH, &css_size);
    if (css == NULL) {
        fprintf(stderr, "failed to read %s\n", TBOX_EXAMPLE_CSS_PATH);
        tbox_html_document_destroy(document);
        return 1;
    }

    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_size);
    free(css);
    if (stylesheet == NULL) {
        fprintf(stderr, "failed to parse CSS\n");
        tbox_html_document_destroy(document);
        return 1;
    }

    printf("walking %s in document order, resolving style from %s per element (grouped by node)...\n\n", TBOX_EXAMPLE_HTML_PATH, TBOX_EXAMPLE_CSS_PATH);
    print_node_with_style(stylesheet, tbox_html_document_root(document), 0);

    tbox_css_stylesheet_destroy(stylesheet);
    tbox_html_document_destroy(document);
    return 0;
}
