#include <stdio.h>
#include <string.h>

typedef int (*tbox_test_group_fn)(void);

typedef struct tbox_test_group {
    const char *name;
    tbox_test_group_fn run;
} tbox_test_group;

int tbox_test_arena_run(void);
int tbox_test_list_run(void);
int tbox_test_vector_run(void);
int tbox_test_string_run(void);
int tbox_test_html_parser_tokenizer_run(void);
int tbox_test_html_parser_tree_run(void);
int tbox_test_html_xpath_run(void);
int tbox_test_css_parser_tokenizer_run(void);
int tbox_test_css_parser_parser_run(void);
int tbox_test_css_selector_run(void);

static const tbox_test_group tbox_test_groups[] = {
    { "arena",                 tbox_test_arena_run                 },
    { "list",                  tbox_test_list_run                  },
    { "vector",                tbox_test_vector_run                },
    { "string",                tbox_test_string_run                },
    { "html_parser_tokenizer", tbox_test_html_parser_tokenizer_run },
    { "html_parser_tree",      tbox_test_html_parser_tree_run      },
    { "html_xpath",            tbox_test_html_xpath_run            },
    { "css_parser_tokenizer",  tbox_test_css_parser_tokenizer_run  },
    { "css_parser_parser",     tbox_test_css_parser_parser_run     },
    { "css_selector",          tbox_test_css_selector_run          },
};

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <group>\n", argv[0]);
        return 2;
    }

    size_t group_count = sizeof(tbox_test_groups) / sizeof(tbox_test_groups[0]);
    for (size_t i = 0; i < group_count; i++) {
        if (strcmp(argv[1], tbox_test_groups[i].name) == 0) {
            return tbox_test_groups[i].run();
        }
    }

    fprintf(stderr, "unknown test group: %s\n", argv[1]);
    return 2;
}
