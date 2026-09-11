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
#ifndef TBOX_EXAMPLE_USER_AGENT_CSS_PATH
#define TBOX_EXAMPLE_USER_AGENT_CSS_PATH "user-agent.css"
#endif
#ifndef TBOX_EXAMPLE_USER_CSS_PATH
#define TBOX_EXAMPLE_USER_CSS_PATH "user.css"
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

/* First element matched by `selector_text` against `root`, or NULL --
 * reuses tbox_css_selector_select (from <tbox/css_selector.h>, pulled in by
 * <tbox/tbox.h>) instead of hand-walking the tree, showing the selector and
 * cascade modules composing naturally. */
static const tbox_html_node *select_one(const tbox_html_node *root, const char *selector_text) {
    tbox_css_selector_node_set set = tbox_css_selector_select(root, selector_text, strlen(selector_text));
    const tbox_html_node *node     = set.count > 0 ? set.items[0] : NULL;
    tbox_css_selector_node_set_destroy(&set);
    return node;
}

/* Resolves `property` for `node` against exactly `sources[0..source_count)`
 * and prints one line: the step's label, then the winning value (or
 * "(not set)" if no source in this stage declares the property at all). */
static void print_resolved(const char *step_label, const tbox_css_cascade_source *sources, size_t source_count, const tbox_html_node *node, const char *property) {
    tbox_css_computed_style style                    = tbox_css_cascade_resolve(sources, source_count, node);
    const tbox_css_resolved_declaration *declaration = tbox_css_computed_style_find(&style, tbox_string_view_make(property, strlen(property)));

    if (declaration != NULL) {
        printf("  %-32s %s = %.*s%s\n", step_label, property, (int)declaration->value.size, declaration->value.data, declaration->important ? " !important" : "");
    } else {
        printf("  %-32s %s = (not set)\n", step_label, property);
    }

    tbox_css_computed_style_destroy(&style);
}

int main(void) {
    size_t html_size, author_css_size, ua_css_size, user_css_size;

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

    char *author_css = read_file(TBOX_EXAMPLE_CSS_PATH, &author_css_size);
    char *ua_css     = read_file(TBOX_EXAMPLE_USER_AGENT_CSS_PATH, &ua_css_size);
    char *user_css   = read_file(TBOX_EXAMPLE_USER_CSS_PATH, &user_css_size);
    if (author_css == NULL || ua_css == NULL || user_css == NULL) {
        fprintf(stderr, "failed to read one of the CSS fixtures (%s, %s, %s)\n", TBOX_EXAMPLE_CSS_PATH, TBOX_EXAMPLE_USER_AGENT_CSS_PATH, TBOX_EXAMPLE_USER_CSS_PATH);
        free(author_css);
        free(ua_css);
        free(user_css);
        tbox_html_document_destroy(document);
        return 1;
    }

    tbox_css_stylesheet *author_sheet = tbox_css_parse(author_css, author_css_size);
    tbox_css_stylesheet *ua_sheet     = tbox_css_parse(ua_css, ua_css_size);
    tbox_css_stylesheet *user_sheet   = tbox_css_parse(user_css, user_css_size);
    free(author_css);
    free(ua_css);
    free(user_css);

    /* A second, inline variant of the user stylesheet with the !important
     * removed -- parsed from a string rather than a file (tbox_css_parse
     * takes any buffer + length, a real .css file is not required), just to
     * contrast "user rule without !important" against "user rule with
     * !important" on the same property. */
    static const char user_css_plain_text[] = "a { color: orange; }";
    tbox_css_stylesheet *user_sheet_plain   = tbox_css_parse(user_css_plain_text, sizeof(user_css_plain_text) - 1);

    if (author_sheet == NULL || ua_sheet == NULL || user_sheet == NULL || user_sheet_plain == NULL) {
        fprintf(stderr, "failed to parse one of the stylesheets\n");
        tbox_html_document_destroy(document);
        return 1;
    }

    const tbox_html_node *nav_link  = select_one(root, "nav a");
    const tbox_html_node *html_node = select_one(root, "html");
    if (nav_link == NULL || html_node == NULL) {
        fprintf(stderr, "expected elements not found in %s\n", TBOX_EXAMPLE_HTML_PATH);
        tbox_css_stylesheet_destroy(user_sheet_plain);
        tbox_css_stylesheet_destroy(user_sheet);
        tbox_css_stylesheet_destroy(author_sheet);
        tbox_css_stylesheet_destroy(ua_sheet);
        tbox_html_document_destroy(document);
        return 1;
    }

    printf("Origin demo -- layering %s (user-agent) + %s (author) + %s (user)\n\n", TBOX_EXAMPLE_USER_AGENT_CSS_PATH, TBOX_EXAMPLE_CSS_PATH, TBOX_EXAMPLE_USER_CSS_PATH);

    printf("<a href=\"#hero\"> (a nav link) -- resolving \"color\" as each origin is added:\n");
    {
        const tbox_css_cascade_source stage1[] = {
            { .stylesheet = ua_sheet, .origin = TBOX_CSS_ORIGIN_USER_AGENT },
        };
        print_resolved("1) user-agent only:", stage1, 1, nav_link, "color");

        const tbox_css_cascade_source stage2[] = {
            { .stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT },
            { .stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR     },
        };
        print_resolved("2) + author (style.css):", stage2, 2, nav_link, "color");

        const tbox_css_cascade_source stage3[] = {
            { .stylesheet = ua_sheet,         .origin = TBOX_CSS_ORIGIN_USER_AGENT },
            { .stylesheet = author_sheet,     .origin = TBOX_CSS_ORIGIN_AUTHOR     },
            { .stylesheet = user_sheet_plain, .origin = TBOX_CSS_ORIGIN_USER       },
        };
        print_resolved("3) + user, WITHOUT !important:", stage3, 3, nav_link, "color");

        const tbox_css_cascade_source stage4[] = {
            { .stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT },
            { .stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR     },
            { .stylesheet = user_sheet,   .origin = TBOX_CSS_ORIGIN_USER       },
        };
        print_resolved("4) + user, WITH !important:", stage4, 3, nav_link, "color");
    }
    printf("  Note: step 3 doesn't change anything -- a plain (non-important) user rule\n"
           "  never outranks the author's normal rule, no matter how specific it is. Only\n"
           "  step 4's !important lets the user's stylesheet win, exactly like a real\n"
           "  browser \"custom CSS\"/userstyle feature.\n\n");

    printf("<html> -- resolving \"font-size\" as each origin is added:\n");
    {
        const tbox_css_cascade_source stage1[] = {
            { .stylesheet = ua_sheet, .origin = TBOX_CSS_ORIGIN_USER_AGENT },
        };
        print_resolved("1) user-agent only:", stage1, 1, html_node, "font-size");

        const tbox_css_cascade_source stage2[] = {
            { .stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT },
            { .stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR     },
        };
        print_resolved("2) + author (style.css):", stage2, 2, html_node, "font-size");

        const tbox_css_cascade_source stage3[] = {
            { .stylesheet = ua_sheet,     .origin = TBOX_CSS_ORIGIN_USER_AGENT },
            { .stylesheet = author_sheet, .origin = TBOX_CSS_ORIGIN_AUTHOR     },
            { .stylesheet = user_sheet,   .origin = TBOX_CSS_ORIGIN_USER       },
        };
        print_resolved("3) + user, trying 8px !important:", stage3, 3, html_node, "font-size");
    }
    printf("  Note: step 3 stays at 16px -- this library ranks a user-agent !important\n"
           "  above even a user !important (the CSS Cascading Level 4 extension to\n"
           "  CSS2.1's cascade, documented in <tbox/css_cascade.h>), so the user's attempt\n"
           "  to shrink the text is refused.\n");

    tbox_css_stylesheet_destroy(user_sheet_plain);
    tbox_css_stylesheet_destroy(user_sheet);
    tbox_css_stylesheet_destroy(author_sheet);
    tbox_css_stylesheet_destroy(ua_sheet);
    tbox_html_document_destroy(document);
    return 0;
}
