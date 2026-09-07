#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void print_item(const tbox_xpath_item *item) {
    if (item->type == TBOX_XPATH_ITEM_ATTRIBUTE) {
        printf("  @%.*s=\"%.*s\" (on <%.*s>)\n", (int)item->attribute.attribute->name.size, item->attribute.attribute->name.data,
               (int)item->attribute.attribute->value.size, item->attribute.attribute->value.data,
               (int)item->attribute.owner->element.tag_name.size, item->attribute.owner->element.tag_name.data);
        return;
    }

    const tbox_html_node *node = item->node;
    switch (node->type) {
    case TBOX_HTML_NODE_DOCUMENT:
        printf("  #document\n");
        break;
    case TBOX_HTML_NODE_ELEMENT:
        printf("  <%.*s>\n", (int)node->element.tag_name.size, node->element.tag_name.data);
        break;
    case TBOX_HTML_NODE_TEXT:
        printf("  #text: \"%.*s\"\n", (int)node->text.text.size, node->text.text.data);
        break;
    case TBOX_HTML_NODE_COMMENT:
        printf("  #comment: \"%.*s\"\n", (int)node->text.text.size, node->text.text.data);
        break;
    case TBOX_HTML_NODE_DOCTYPE:
        printf("  #doctype: %.*s\n", (int)node->text.text.size, node->text.text.data);
        break;
    }
}

/* Runs `expr` from `context`, prints every match, and cleans up the
 * resulting node_set -- this is the full lifecycle a caller is expected to
 * follow around tbox_xpath_select. */
static void run_query(const tbox_html_node *context, const char *expr) {
    printf("%s\n", expr);

    tbox_xpath_node_set set = tbox_xpath_select(context, expr, strlen(expr));
    if (set.count == 0) {
        printf("  (no matches)\n");
    } else {
        for (size_t i = 0; i < set.count; i++) {
            print_item(&set.items[i]);
        }
    }
    tbox_xpath_node_set_destroy(&set);

    printf("\n");
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

    const tbox_html_node *root = tbox_html_document_root(document);

    /* Absolute path ending in an attribute. */
    run_query(root, "/html/@lang");

    /* '//' finds a tag at any depth; '@href' reads one attribute of it. */
    run_query(root, "//a/@href");

    /* '*' with a predicate: any element, anywhere, that has a given
     * attribute -- matches <html name=...>, <form name=...> and
     * <output name=...> at once. */
    run_query(root, "//*[@name]");

    /* Chaining two child steps: the <a> that is a direct child of <p>. */
    run_query(root, "//p/a");

    /* text() reaches into an element to read its direct text content. */
    run_query(root, "//title/text()");

    /* Locate-then-edit: find every <script>, detach it from the tree with
     * tbox_html_node_remove, then confirm a re-query no longer finds it. */
    printf("removing every <script> via tbox_html_node_remove...\n\n");
    tbox_xpath_node_set scripts = tbox_xpath_select(root, "//script", strlen("//script"));
    for (size_t i = 0; i < scripts.count; i++) {
        tbox_html_node_remove((tbox_html_node *)scripts.items[i].node);
    }
    tbox_xpath_node_set_destroy(&scripts);

    run_query(root, "//script");

    tbox_html_document_destroy(document);
    return 0;
}
