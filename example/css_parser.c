#include <stdio.h>
#include <stdlib.h>

#include <tbox/tbox.h>

#ifndef TBOX_EXAMPLE_CSS_PATH
#define TBOX_EXAMPLE_CSS_PATH "style.css"
#endif

/* Reads the whole file into a malloc'd buffer (not NUL-terminated:
 * tbox_css_parse takes an explicit length). Caller frees the buffer with
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

static void print_combinator(tbox_css_combinator combinator) {
    switch (combinator) {
    case TBOX_CSS_COMBINATOR_NONE:
        break;
    case TBOX_CSS_COMBINATOR_DESCENDANT:
        printf(" ");
        break;
    case TBOX_CSS_COMBINATOR_CHILD:
        printf(" > ");
        break;
    case TBOX_CSS_COMBINATOR_ADJACENT_SIBLING:
        printf(" + ");
        break;
    }
}

/* Renders a single simple selector (plus the combinator that attaches it to
 * the one before it) back out roughly as it appeared in the source, to make
 * the decomposition into tbox_css_simple_selector items visually obvious. */
static void print_simple_selector(const tbox_css_simple_selector *item) {
    print_combinator(item->combinator_before);

    switch (item->kind) {
    case TBOX_CSS_SIMPLE_SELECTOR_TYPE:
        printf("%.*s", (int)item->name.size, item->name.data);
        break;
    case TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL:
        printf("*");
        break;
    case TBOX_CSS_SIMPLE_SELECTOR_ID:
        printf("#%.*s", (int)item->name.size, item->name.data);
        break;
    case TBOX_CSS_SIMPLE_SELECTOR_CLASS:
        printf(".%.*s", (int)item->name.size, item->name.data);
        break;
    case TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE:
        printf("[%.*s", (int)item->name.size, item->name.data);
        switch (item->attribute_operator) {
        case TBOX_CSS_ATTR_EXISTS:
            break;
        case TBOX_CSS_ATTR_EQUALS:
            printf("=%.*s", (int)item->attribute_value.size, item->attribute_value.data);
            break;
        case TBOX_CSS_ATTR_INCLUDES:
            printf("~=%.*s", (int)item->attribute_value.size, item->attribute_value.data);
            break;
        case TBOX_CSS_ATTR_DASHMATCH:
            printf("|=%.*s", (int)item->attribute_value.size, item->attribute_value.data);
            break;
        }
        printf("]");
        break;
    case TBOX_CSS_SIMPLE_SELECTOR_PSEUDO:
        printf(":%.*s", (int)item->name.size, item->name.data);
        if (item->pseudo_argument.size > 0) {
            printf("(%.*s)", (int)item->pseudo_argument.size, item->pseudo_argument.data);
        }
        break;
    }
}

static void print_selector(const tbox_css_selector *selector) {
    for (size_t i = 0; i < selector->simple_selector_count; i++) {
        print_simple_selector(&selector->simple_selectors[i]);
    }
}

static void print_ruleset(const tbox_css_ruleset *ruleset) {
    for (size_t i = 0; i < ruleset->selector_count; i++) {
        if (i > 0) {
            printf(", ");
        }
        print_selector(&ruleset->selectors[i]);
    }
    printf(" {\n");

    for (size_t i = 0; i < ruleset->declaration_count; i++) {
        const tbox_css_declaration *declaration = &ruleset->declarations[i];
        printf("  %.*s: %.*s;\n", (int)declaration->property.size, declaration->property.data, (int)declaration->value.size, declaration->value.data);
    }

    printf("}\n\n");
}

int main(void) {
    size_t css_size;
    char *css = read_file(TBOX_EXAMPLE_CSS_PATH, &css_size);
    if (css == NULL) {
        fprintf(stderr, "failed to read %s\n", TBOX_EXAMPLE_CSS_PATH);
        return 1;
    }

    /* tbox_css_parse never fails on malformed CSS -- it only returns NULL on
     * allocation failure -- and nothing it returns points back into `css`,
     * so the input buffer can be freed immediately. */
    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_size);
    free(css);
    if (stylesheet == NULL) {
        fprintf(stderr, "failed to parse CSS\n");
        return 1;
    }

    size_t ruleset_count             = tbox_css_stylesheet_ruleset_count(stylesheet);
    const tbox_css_ruleset *rulesets = tbox_css_stylesheet_rulesets(stylesheet);

    /* The @media at-rule in example/style.css contributes nothing here
     * (skipped whole), and ".section-title::after" -- CSS2.1 has no "::"
     * syntax -- is silently dropped by error recovery; the ruleset right
     * after it still shows up below. */
    printf("%zu ruleset(s) parsed from %s\n\n", ruleset_count, TBOX_EXAMPLE_CSS_PATH);
    for (size_t i = 0; i < ruleset_count; i++) {
        print_ruleset(&rulesets[i]);
    }

    tbox_css_stylesheet_destroy(stylesheet);
    return 0;
}
