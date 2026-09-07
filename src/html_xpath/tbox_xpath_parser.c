#include "tbox_xpath_parser.h"

#include <stdlib.h>
#include <string.h>

#include "tbox_xpath_lexer.h"

typedef struct tbox_xpath_parser {
    tbox_xpath_lexer lexer;
    tbox_xpath_token current;
    tbox_xpath_query *query;
    bool has_error;
    size_t error_offset;
} tbox_xpath_parser;

static void tbox_xpath_parser_advance(tbox_xpath_parser *parser) {
    parser->current = tbox_xpath_lexer_next(&parser->lexer);
}

static void tbox_xpath_parser_fail(tbox_xpath_parser *parser, size_t offset) {
    if (!parser->has_error) {
        parser->has_error    = true;
        parser->error_offset = offset;
    }
}

static tbox_string_view tbox_xpath_copy_view(tbox_xpath_query *query, tbox_string_view view) {
    if (view.size == 0) {
        return tbox_string_view_make(NULL, 0);
    }
    char *data = tbox_arena_alloc(&query->arena, view.size);
    memcpy(data, view.data, view.size);
    return tbox_string_view_make(data, view.size);
}

static size_t tbox_xpath_parse_uint(tbox_string_view text) {
    size_t value = 0;
    for (size_t i = 0; i < text.size; i++) {
        value = value * 10 + (size_t)(text.data[i] - '0');
    }
    return value;
}

static tbox_xpath_predicate *tbox_xpath_parse_predicate(tbox_xpath_parser *parser) {
    if (parser->has_error) {
        return NULL;
    }
    tbox_xpath_parser_advance(parser); /* consume '[' */

    tbox_xpath_predicate *predicate = tbox_arena_alloc_zero(&parser->query->arena, sizeof(tbox_xpath_predicate));

    if (parser->current.type == TBOX_XPATH_TOKEN_NUMBER) {
        predicate->kind     = TBOX_XPATH_PREDICATE_POSITION;
        predicate->position = tbox_xpath_parse_uint(parser->current.text);
        tbox_xpath_parser_advance(parser);
    } else if (parser->current.type == TBOX_XPATH_TOKEN_AT) {
        tbox_xpath_parser_advance(parser);
        if (parser->current.type != TBOX_XPATH_TOKEN_NAME) {
            tbox_xpath_parser_fail(parser, parser->current.offset);
            return NULL;
        }
        predicate->attr_name = tbox_xpath_copy_view(parser->query, parser->current.text);
        tbox_xpath_parser_advance(parser);

        if (parser->current.type == TBOX_XPATH_TOKEN_EQUALS) {
            tbox_xpath_parser_advance(parser);
            if (parser->current.type != TBOX_XPATH_TOKEN_STRING) {
                tbox_xpath_parser_fail(parser, parser->current.offset);
                return NULL;
            }
            predicate->kind       = TBOX_XPATH_PREDICATE_ATTR_EQUALS;
            predicate->attr_value = tbox_xpath_copy_view(parser->query, parser->current.text);
            tbox_xpath_parser_advance(parser);
        } else {
            predicate->kind = TBOX_XPATH_PREDICATE_ATTR_EXISTS;
        }
    } else {
        tbox_xpath_parser_fail(parser, parser->current.offset);
        return NULL;
    }

    if (parser->current.type != TBOX_XPATH_TOKEN_RBRACKET) {
        tbox_xpath_parser_fail(parser, parser->current.offset);
        return NULL;
    }
    tbox_xpath_parser_advance(parser);

    return predicate;
}

static tbox_xpath_step *tbox_xpath_parse_step(tbox_xpath_parser *parser, tbox_xpath_combinator combinator) {
    if (parser->has_error) {
        return NULL;
    }

    tbox_xpath_step *step   = tbox_arena_alloc_zero(&parser->query->arena, sizeof(tbox_xpath_step));
    step->combinator_before = combinator;

    if (parser->current.type == TBOX_XPATH_TOKEN_DOT) {
        step->axis      = TBOX_XPATH_AXIS_SELF;
        step->test_kind = TBOX_XPATH_TEST_NONE;
        tbox_xpath_parser_advance(parser);
        return step;
    }
    if (parser->current.type == TBOX_XPATH_TOKEN_DOT_DOT) {
        step->axis      = TBOX_XPATH_AXIS_PARENT;
        step->test_kind = TBOX_XPATH_TEST_NONE;
        tbox_xpath_parser_advance(parser);
        return step;
    }

    if (parser->current.type == TBOX_XPATH_TOKEN_AT) {
        tbox_xpath_parser_advance(parser);
        step->axis = TBOX_XPATH_AXIS_ATTRIBUTE;
        if (parser->current.type == TBOX_XPATH_TOKEN_STAR) {
            step->test_kind = TBOX_XPATH_TEST_WILDCARD;
            tbox_xpath_parser_advance(parser);
        } else if (parser->current.type == TBOX_XPATH_TOKEN_NAME) {
            step->test_kind = TBOX_XPATH_TEST_NAME;
            step->test_name = tbox_xpath_copy_view(parser->query, parser->current.text);
            tbox_xpath_parser_advance(parser);
        } else {
            tbox_xpath_parser_fail(parser, parser->current.offset);
            return NULL;
        }
    } else if (parser->current.type == TBOX_XPATH_TOKEN_STAR) {
        step->axis      = TBOX_XPATH_AXIS_CHILD;
        step->test_kind = TBOX_XPATH_TEST_WILDCARD;
        tbox_xpath_parser_advance(parser);
    } else if (parser->current.type == TBOX_XPATH_TOKEN_NAME) {
        tbox_string_view name = parser->current.text;
        size_t name_offset     = parser->current.offset;
        tbox_xpath_parser_advance(parser);

        if (parser->current.type == TBOX_XPATH_TOKEN_LPAREN) {
            tbox_xpath_parser_advance(parser);
            if (parser->current.type != TBOX_XPATH_TOKEN_RPAREN) {
                tbox_xpath_parser_fail(parser, parser->current.offset);
                return NULL;
            }
            tbox_xpath_parser_advance(parser);

            if (tbox_string_view_equal_cstr(name, "text")) {
                step->axis      = TBOX_XPATH_AXIS_CHILD;
                step->test_kind = TBOX_XPATH_TEST_TEXT;
            } else if (tbox_string_view_equal_cstr(name, "node")) {
                step->axis      = TBOX_XPATH_AXIS_CHILD;
                step->test_kind = TBOX_XPATH_TEST_NODE;
            } else {
                tbox_xpath_parser_fail(parser, name_offset);
                return NULL;
            }
        } else {
            step->axis      = TBOX_XPATH_AXIS_CHILD;
            step->test_kind = TBOX_XPATH_TEST_NAME;
            step->test_name = tbox_xpath_copy_view(parser->query, name);
        }
    } else {
        tbox_xpath_parser_fail(parser, parser->current.offset);
        return NULL;
    }

    tbox_xpath_predicate *last_predicate = NULL;
    while (parser->current.type == TBOX_XPATH_TOKEN_LBRACKET) {
        tbox_xpath_predicate *predicate = tbox_xpath_parse_predicate(parser);
        if (predicate == NULL) {
            return NULL;
        }
        if (last_predicate == NULL) {
            step->predicates = predicate;
        } else {
            last_predicate->next = predicate;
        }
        last_predicate = predicate;
    }

    return step;
}

static tbox_xpath_step *tbox_xpath_parse_relative_location_path(tbox_xpath_parser *parser,
                                                                  tbox_xpath_combinator first_combinator) {
    if (parser->has_error) {
        return NULL;
    }

    tbox_xpath_step *first = tbox_xpath_parse_step(parser, first_combinator);
    if (first == NULL) {
        return NULL;
    }

    tbox_xpath_step *last = first;
    while (parser->current.type == TBOX_XPATH_TOKEN_SLASH || parser->current.type == TBOX_XPATH_TOKEN_SLASH_SLASH) {
        if (last->axis == TBOX_XPATH_AXIS_ATTRIBUTE) {
            tbox_xpath_parser_fail(parser, parser->current.offset);
            return NULL;
        }

        tbox_xpath_combinator combinator = (parser->current.type == TBOX_XPATH_TOKEN_SLASH_SLASH)
                                                ? TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF
                                                : TBOX_XPATH_COMBINATOR_IDENTITY;
        tbox_xpath_parser_advance(parser);

        tbox_xpath_step *step = tbox_xpath_parse_step(parser, combinator);
        if (step == NULL) {
            return NULL;
        }
        last->next = step;
        last       = step;
    }

    return first;
}

tbox_xpath_query *tbox_xpath_parser_parse(const char *expr, size_t length, size_t *out_error_offset) {
    tbox_xpath_query *query = malloc(sizeof(tbox_xpath_query));
    if (query == NULL) {
        return NULL;
    }
    query->arena      = tbox_arena_create(0);
    query->absolute   = false;
    query->first_step = NULL;

    tbox_xpath_parser parser = {.has_error = false, .error_offset = 0, .query = query};
    tbox_xpath_lexer_init(&parser.lexer, expr, length);
    tbox_xpath_parser_advance(&parser);

    tbox_xpath_combinator first_combinator = TBOX_XPATH_COMBINATOR_IDENTITY;
    if (parser.current.type == TBOX_XPATH_TOKEN_SLASH_SLASH) {
        query->absolute  = true;
        first_combinator = TBOX_XPATH_COMBINATOR_DESCENDANT_OR_SELF;
        tbox_xpath_parser_advance(&parser);
    } else if (parser.current.type == TBOX_XPATH_TOKEN_SLASH) {
        query->absolute  = true;
        first_combinator = TBOX_XPATH_COMBINATOR_IDENTITY;
        tbox_xpath_parser_advance(&parser);
    }

    query->first_step = tbox_xpath_parse_relative_location_path(&parser, first_combinator);

    if (!parser.has_error && parser.current.type != TBOX_XPATH_TOKEN_EOF) {
        tbox_xpath_parser_fail(&parser, parser.current.offset);
    }

    if (parser.has_error || query->first_step == NULL) {
        if (out_error_offset != NULL) {
            *out_error_offset = parser.has_error ? parser.error_offset : length;
        }
        tbox_arena_destroy(&query->arena);
        free(query);
        return NULL;
    }

    return query;
}
