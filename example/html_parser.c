#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <tbox/tbox.h>

#ifndef TBOX_EXAMPLE_HTML_PATH
#define TBOX_EXAMPLE_HTML_PATH "example.html"
#endif

/* Reads the whole file into a malloc'd buffer (not NUL-terminated:
 * tbox_html_parse takes an explicit length). Caller frees the buffer with
 * free(). Returns NULL and leaves *out_size untouched on any I/O failure. */
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

static void print_text(const tbox_string_view *text) {
    if (text->size == 0) {
        return;
    }

    printf("#text: \"");
    for (size_t i = 0; i < text->size; i++) {
        char c = text->data[i];
        if (c == '\n') {
            printf("↵");
        } else if (c == '\t') {
            printf("⇥");
        } else if (isprint((unsigned char)c)) {
            printf("%c", c);
        }
    }
    printf("\"\n");
}

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

static void print_node(const tbox_html_node *node, int depth) {
    print_indent(depth);

    switch (node->type) {
    case TBOX_HTML_NODE_DOCUMENT:
        printf("#document\n");
        break;
    case TBOX_HTML_NODE_ELEMENT:
        printf("<%.*s>", (int)node->element.tag_name.size, node->element.tag_name.data);
        for (size_t i = 0; i < node->element.attribute_count; i++) {
            const tbox_html_attribute *attribute = &node->element.attributes[i];
            printf(" %.*s=\"%.*s\"", (int)attribute->name.size, attribute->name.data, (int)attribute->value.size, attribute->value.data);
        }
        printf("\n");
        break;
    case TBOX_HTML_NODE_TEXT:
        print_text(&node->text.text);
        break;
    case TBOX_HTML_NODE_COMMENT:
        printf("#comment: \"%.*s\"\n", (int)node->text.text.size, node->text.text.data);
        break;
    case TBOX_HTML_NODE_DOCTYPE:
        printf("#doctype: %.*s\n", (int)node->text.text.size, node->text.text.data);
        break;
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        print_node(child, depth + 1);
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

    print_node(tbox_html_document_root(document), 0);
    printf("\n");

    tbox_html_document_destroy(document);
    return 0;
}
