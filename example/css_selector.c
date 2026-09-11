#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void print_node(const tbox_html_node *node) {
    if (node->type == TBOX_HTML_NODE_ELEMENT) {
        printf("  <%.*s>\n", (int)node->element.tag_name.size, node->element.tag_name.data);
    } else {
        printf("  (unexpected non-element node)\n");
    }
}

/* Runs `text` from `root`, prints every matching element, and cleans up the
 * resulting node_set -- the full lifecycle a caller is expected to follow
 * around tbox_css_selector_select. */
static void run_query(const tbox_html_node *root, const char *text) {
    printf("%s\n", text);

    tbox_css_selector_node_set set = tbox_css_selector_select(root, text, strlen(text));
    if (set.count == 0) {
        printf("  (no matches)\n");
    } else {
        for (size_t i = 0; i < set.count; i++) {
            print_node(set.items[i]);
        }
    }
    tbox_css_selector_node_set_destroy(&set);

    printf("\n");
}

/* Applies every ruleset of `stylesheet` to `root` via
 * tbox_css_selector_match_stylesheet -- the browser-engine-style entry
 * point, built on the same matching engine as tbox_css_selector_select.
 * style.css includes a universal '*' rule, so printing one line per matched
 * element would be a very long list; instead this groups consecutive
 * matches by the ruleset that produced them (match_stylesheet always
 * returns them in ruleset order) and prints one summary line per ruleset. */
static void apply_stylesheet(const tbox_css_stylesheet *stylesheet, const tbox_html_node *root) {
    printf("applying %s to %s via tbox_css_selector_match_stylesheet (grouped by ruleset)...\n\n", TBOX_EXAMPLE_CSS_PATH, TBOX_EXAMPLE_HTML_PATH);

    tbox_css_selector_match_set matches = tbox_css_selector_match_stylesheet(stylesheet, root);
    size_t total                        = matches.count;

    size_t i = 0;
    while (i < matches.count) {
        const tbox_css_ruleset *ruleset = matches.items[i].ruleset;

        size_t group_start = i;
        while (i < matches.count && matches.items[i].ruleset == ruleset) {
            i++;
        }
        size_t group_count = i - group_start;

        const tbox_css_declaration *first_declaration = &ruleset->declarations[0];
        printf("  %zu element(s) matched by the ruleset declaring \"%.*s: %.*s;\"\n", group_count, (int)first_declaration->property.size, first_declaration->property.data, (int)first_declaration->value.size, first_declaration->value.data);
    }
    tbox_css_selector_match_set_destroy(&matches);

    printf("\n%zu match(es) total\n\n", total);
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

    /* Standalone compiled selectors, evaluated directly against the HTML
     * tree -- same lifecycle as tbox_xpath_select. */
    run_query(root, "a");                    /* every anchor: 3 nav links + the hero "Explore Features" button */
    run_query(root, "nav a");                /* descendant combinator: just the 3 nav links */
    run_query(root, ".navbar > ul");         /* child combinator: the <ul> directly inside <nav class="navbar"> */
    run_query(root, "h2.section-title");     /* compound selector: tag + class together */
    run_query(root, "[id]");                 /* attribute existence: the 3 <section id="..."> elements */
    run_query(root, ".gallery-item.item-1"); /* compound with two classes, both required */
    run_query(root, "footer p");             /* the copyright paragraph */

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

    /* Selectors already parsed from a loaded stylesheet, matched against the
     * same HTML tree -- the second entry point, sharing the same engine. */
    apply_stylesheet(stylesheet, root);

    tbox_css_stylesheet_destroy(stylesheet);
    tbox_html_document_destroy(document);
    return 0;
}
