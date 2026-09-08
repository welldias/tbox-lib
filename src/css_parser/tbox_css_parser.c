#include "tbox_css_parser.h"

#include "base/tbox_string.h"
#include "base/tbox_vector.h"

/* Grammar implemented (CSS2.1 Appendix G.1, informal, trimmed):
 *
 *   stylesheet      : [ CDO | CDC | S | statement ]*
 *   statement       : ruleset | at_rule
 *   at_rule         : AT_KEYWORD <anything> [ block | ';' ]     -- contents discarded
 *   ruleset         : selector_group '{' declaration_list '}'
 *   selector_group  : selector [ ',' S* selector ]*
 *   selector        : compound [ combinator compound ]*
 *   combinator      : S* '>' S* | S* '+' S* | S+               (S+ alone = descendant)
 *   compound        : [ element_name | '*' ]? qualifier*
 *                   | qualifier+
 *   qualifier       : HASH | '.' IDENT | attrib | pseudo
 *   attrib          : '[' S* IDENT S* [ [ '=' | '~=' | '|=' ] S* [ IDENT | STRING ] S* ]? ']'
 *   pseudo          : ':' IDENT | ':' FUNCTION S* IDENT? S* ')'
 *   declaration_list: S* [ declaration? [ ';' S* declaration? ]* ]
 *   declaration     : IDENT S* ':' S* <raw tokens up to top-level ';' or '}'>
 *
 * Unlike tbox_xpath_parser (which hard-fails on the first syntax error via a
 * sticky has_error flag), CSS is defined to tolerate errors within a
 * stylesheet: a grammar production that can't match just returns false and
 * leaves resynchronization to its caller's recovery logic, instead of
 * aborting the whole parse. See tbox_css_parser_recover_ruleset,
 * tbox_css_parser_skip_at_rule and tbox_css_parser_skip_to_semicolon_or_rbrace. */

static void tbox_css_parser_advance(tbox_css_parser *parser) {
    tbox_css_tokenizer_next(&parser->tokenizer, &parser->current);
}

void tbox_css_parser_init(tbox_css_parser *parser, const char *input, size_t length, tbox_arena *arena) {
    tbox_css_tokenizer_init(&parser->tokenizer, input, length);
    parser->arena = arena;
    tbox_css_parser_advance(parser);
}

static tbox_string_view tbox_css_parser_copy(tbox_css_parser *parser, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, parser->arena, view.size);
    tbox_string_builder_append_view(&builder, view);
    return tbox_string_builder_finish(&builder);
}

static tbox_string_view tbox_css_parser_copy_lower(tbox_css_parser *parser, tbox_string_view view) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, parser->arena, view.size);
    tbox_string_builder_append_view_lower_ascii(&builder, view);
    return tbox_string_builder_finish(&builder);
}

static void tbox_css_parser_skip_s(tbox_css_parser *parser) {
    while (parser->current.type == TBOX_CSS_TOKEN_S) {
        tbox_css_parser_advance(parser);
    }
}

static bool tbox_css_token_can_start_simple_selector(tbox_css_token_type type) {
    switch (type) {
    case TBOX_CSS_TOKEN_IDENT:
    case TBOX_CSS_TOKEN_STAR:
    case TBOX_CSS_TOKEN_HASH:
    case TBOX_CSS_TOKEN_DOT:
    case TBOX_CSS_TOKEN_LBRACKET:
    case TBOX_CSS_TOKEN_COLON:
        return true;
    default:
        return false;
    }
}

/* '{'/'('/'[' vs '}'/')'/']' -- used where a whole block (any bracket kind)
 * must be skipped as one balanced unit (skip_block, and the declaration
 * value scanner in parse_declaration). */
static bool tbox_css_token_opens(tbox_css_token_type type) {
    return type == TBOX_CSS_TOKEN_LBRACE || type == TBOX_CSS_TOKEN_LPAREN || type == TBOX_CSS_TOKEN_LBRACKET;
}

static bool tbox_css_token_closes(tbox_css_token_type type) {
    return type == TBOX_CSS_TOKEN_RBRACE || type == TBOX_CSS_TOKEN_RPAREN || type == TBOX_CSS_TOKEN_RBRACKET;
}

/* '('/'[' only -- used where scanning must stop AT a top-level '{' rather
 * than swallow it (skip_at_rule's prelude, recover_ruleset). */
static bool tbox_css_token_opens_nested(tbox_css_token_type type) {
    return type == TBOX_CSS_TOKEN_LPAREN || type == TBOX_CSS_TOKEN_LBRACKET;
}

static bool tbox_css_token_closes_nested(tbox_css_token_type type) {
    return type == TBOX_CSS_TOKEN_RPAREN || type == TBOX_CSS_TOKEN_RBRACKET;
}

/* Consumes tokens, tracking '{'/'('/'[' vs '}'/')'/']' depth, until either a
 * depth-0 ';' is seen (consumed) or a depth-0 '}' is seen (NOT consumed,
 * left for the caller) or EOF. Used to skip a malformed declaration. */
static void tbox_css_parser_skip_to_semicolon_or_rbrace(tbox_css_parser *parser) {
    int depth = 0;
    for (;;) {
        tbox_css_token_type type = parser->current.type;
        if (type == TBOX_CSS_TOKEN_EOF) {
            return;
        }
        if (depth == 0 && type == TBOX_CSS_TOKEN_SEMICOLON) {
            tbox_css_parser_advance(parser);
            return;
        }
        if (depth == 0 && type == TBOX_CSS_TOKEN_RBRACE) {
            return;
        }
        if (tbox_css_token_opens(type)) {
            depth++;
        } else if (tbox_css_token_closes(type) && depth > 0) {
            depth--;
        }
        tbox_css_parser_advance(parser);
    }
}

/* Current token must be '{'. Consumes it and everything up to and including
 * the matching '}'. Used to discard an at-rule's block, and to discard a
 * malformed ruleset's block once its selector-group failed to parse. */
static void tbox_css_parser_skip_block(tbox_css_parser *parser) {
    int depth = 0;
    do {
        tbox_css_token_type type = parser->current.type;
        if (type == TBOX_CSS_TOKEN_EOF) {
            return;
        }
        if (tbox_css_token_opens(type)) {
            depth++;
        } else if (tbox_css_token_closes(type) && depth > 0) {
            depth--;
        }
        tbox_css_parser_advance(parser);
    } while (depth > 0);
}

/* AT_KEYWORD already consumed by the caller. Skips the at-rule's prelude,
 * tracking only '('/'[' depth, until it finds a depth-0 '{' (delegates to
 * skip_block) or a depth-0 ';' (consumed) or EOF. */
static void tbox_css_parser_skip_at_rule(tbox_css_parser *parser) {
    int depth = 0;
    for (;;) {
        tbox_css_token_type type = parser->current.type;
        if (type == TBOX_CSS_TOKEN_EOF) {
            return;
        }
        if (depth == 0 && type == TBOX_CSS_TOKEN_LBRACE) {
            tbox_css_parser_skip_block(parser);
            return;
        }
        if (depth == 0 && type == TBOX_CSS_TOKEN_SEMICOLON) {
            tbox_css_parser_advance(parser);
            return;
        }
        if (tbox_css_token_opens_nested(type)) {
            depth++;
        } else if (tbox_css_token_closes_nested(type) && depth > 0) {
            depth--;
        }
        tbox_css_parser_advance(parser);
    }
}

/* The selector-group failed to parse, or didn't land on '{'. Skips forward
 * (tracking only '('/'[' depth) to the first depth-0 '{' (then skip_block)
 * or EOF -- i.e. "throw away up to and including this ruleset's block, if
 * it has one." */
static void tbox_css_parser_recover_ruleset(tbox_css_parser *parser) {
    int depth = 0;
    for (;;) {
        tbox_css_token_type type = parser->current.type;
        if (type == TBOX_CSS_TOKEN_EOF) {
            return;
        }
        if (depth == 0 && type == TBOX_CSS_TOKEN_LBRACE) {
            tbox_css_parser_skip_block(parser);
            return;
        }
        if (tbox_css_token_opens_nested(type)) {
            depth++;
        } else if (tbox_css_token_closes_nested(type) && depth > 0) {
            depth--;
        }
        tbox_css_parser_advance(parser);
    }
}

static tbox_css_simple_selector *tbox_css_parser_push_simple_selector(tbox_vector *out, tbox_css_simple_selector_kind kind, tbox_css_combinator combinator, tbox_string_view name) {
    tbox_css_simple_selector *item = tbox_vector_push(out);
    item->kind                     = kind;
    item->combinator_before        = combinator;
    item->name                     = name;
    item->attribute_operator       = TBOX_CSS_ATTR_EXISTS;
    item->attribute_value          = tbox_string_view_make(NULL, 0);
    item->pseudo_argument          = tbox_string_view_make(NULL, 0);
    return item;
}

/* attrib: '[' S* IDENT S* [ [ '=' | '~=' | '|=' ] S* [ IDENT | STRING ] S* ]? ']' */
static bool tbox_css_parser_parse_attribute_qualifier(tbox_css_parser *parser, tbox_vector *out, tbox_css_combinator combinator) {
    tbox_css_parser_advance(parser); /* '[' */
    tbox_css_parser_skip_s(parser);

    if (parser->current.type != TBOX_CSS_TOKEN_IDENT) {
        return false;
    }
    tbox_string_view name = tbox_css_parser_copy_lower(parser, parser->current.text);
    tbox_css_parser_advance(parser);
    tbox_css_parser_skip_s(parser);

    tbox_css_attribute_operator op = TBOX_CSS_ATTR_EXISTS;

    if (parser->current.type == TBOX_CSS_TOKEN_EQUALS) {
        op = TBOX_CSS_ATTR_EQUALS;
        tbox_css_parser_advance(parser);
    } else if (parser->current.type == TBOX_CSS_TOKEN_TILDE) {
        tbox_css_parser_advance(parser);
        if (parser->current.type != TBOX_CSS_TOKEN_EQUALS) {
            return false;
        }
        op = TBOX_CSS_ATTR_INCLUDES;
        tbox_css_parser_advance(parser);
    } else if (parser->current.type == TBOX_CSS_TOKEN_PIPE) {
        tbox_css_parser_advance(parser);
        if (parser->current.type != TBOX_CSS_TOKEN_EQUALS) {
            return false;
        }
        op = TBOX_CSS_ATTR_DASHMATCH;
        tbox_css_parser_advance(parser);
    }

    tbox_string_view value = tbox_string_view_make(NULL, 0);
    if (op != TBOX_CSS_ATTR_EXISTS) {
        tbox_css_parser_skip_s(parser);
        if (parser->current.type != TBOX_CSS_TOKEN_IDENT && parser->current.type != TBOX_CSS_TOKEN_STRING) {
            return false;
        }
        value = tbox_css_parser_copy(parser, parser->current.text);
        tbox_css_parser_advance(parser);
        tbox_css_parser_skip_s(parser);
    }

    if (parser->current.type != TBOX_CSS_TOKEN_RBRACKET) {
        return false;
    }
    tbox_css_parser_advance(parser); /* ']' */

    tbox_css_simple_selector *item = tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE, combinator, name);
    item->attribute_operator       = op;
    item->attribute_value          = value;
    return true;
}

/* pseudo: ':' IDENT | ':' FUNCTION S* IDENT? S* ')' */
static bool tbox_css_parser_parse_pseudo_qualifier(tbox_css_parser *parser, tbox_vector *out, tbox_css_combinator combinator) {
    tbox_css_parser_advance(parser); /* ':' */

    if (parser->current.type == TBOX_CSS_TOKEN_IDENT) {
        tbox_string_view name = tbox_css_parser_copy_lower(parser, parser->current.text);
        tbox_css_parser_advance(parser);
        tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_PSEUDO, combinator, name);
        return true;
    }

    if (parser->current.type == TBOX_CSS_TOKEN_FUNCTION) {
        tbox_string_view name = tbox_css_parser_copy_lower(parser, parser->current.text);
        tbox_css_parser_advance(parser);
        tbox_css_parser_skip_s(parser);

        tbox_string_view argument = tbox_string_view_make(NULL, 0);
        if (parser->current.type == TBOX_CSS_TOKEN_IDENT) {
            argument = tbox_css_parser_copy(parser, parser->current.text);
            tbox_css_parser_advance(parser);
            tbox_css_parser_skip_s(parser);
        }

        if (parser->current.type != TBOX_CSS_TOKEN_RPAREN) {
            return false;
        }
        tbox_css_parser_advance(parser); /* ')' */

        tbox_css_simple_selector *item = tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_PSEUDO, combinator, name);
        item->pseudo_argument          = argument;
        return true;
    }

    return false;
}

/* compound: [ element_name | '*' ]? qualifier* | qualifier+
 * Every qualifier fused onto this compound (no combinator/whitespace between
 * them in the source) after the first item gets combinator_before == NONE;
 * only the very first item pushed carries `combinator`. */
static bool tbox_css_parser_parse_compound(tbox_css_parser *parser, tbox_vector *out, tbox_css_combinator combinator) {
    bool pushed_any = false;

    if (parser->current.type == TBOX_CSS_TOKEN_IDENT) {
        tbox_string_view name = tbox_css_parser_copy_lower(parser, parser->current.text);
        tbox_css_parser_advance(parser);
        tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_TYPE, combinator, name);
        pushed_any = true;
    } else if (parser->current.type == TBOX_CSS_TOKEN_STAR) {
        tbox_css_parser_advance(parser);
        tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL, combinator, tbox_string_view_make(NULL, 0));
        pushed_any = true;
    }

    for (;;) {
        tbox_css_combinator qualifier_combinator = pushed_any ? TBOX_CSS_COMBINATOR_NONE : combinator;

        if (parser->current.type == TBOX_CSS_TOKEN_HASH) {
            tbox_string_view name = tbox_css_parser_copy(parser, parser->current.text);
            tbox_css_parser_advance(parser);
            tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_ID, qualifier_combinator, name);
            pushed_any = true;
        } else if (parser->current.type == TBOX_CSS_TOKEN_DOT) {
            tbox_css_parser_advance(parser);
            if (parser->current.type != TBOX_CSS_TOKEN_IDENT) {
                return false;
            }
            tbox_string_view name = tbox_css_parser_copy(parser, parser->current.text);
            tbox_css_parser_advance(parser);
            tbox_css_parser_push_simple_selector(out, TBOX_CSS_SIMPLE_SELECTOR_CLASS, qualifier_combinator, name);
            pushed_any = true;
        } else if (parser->current.type == TBOX_CSS_TOKEN_LBRACKET) {
            if (!tbox_css_parser_parse_attribute_qualifier(parser, out, qualifier_combinator)) {
                return false;
            }
            pushed_any = true;
        } else if (parser->current.type == TBOX_CSS_TOKEN_COLON) {
            if (!tbox_css_parser_parse_pseudo_qualifier(parser, out, qualifier_combinator)) {
                return false;
            }
            pushed_any = true;
        } else {
            break;
        }
    }

    return pushed_any;
}

/* selector: compound [ combinator compound ]*
 * S is the one place this has to consult the raw token type instead of
 * blindly skipping it, since S alone (with no '>'/'+' following) is itself
 * the descendant-combinator signal. */
static bool tbox_css_parser_parse_selector(tbox_css_parser *parser, tbox_css_selector *out_selector) {
    tbox_vector simple_selectors;
    tbox_vector_init(&simple_selectors, parser->arena, sizeof(tbox_css_simple_selector), 0);

    tbox_css_combinator combinator = TBOX_CSS_COMBINATOR_NONE;

    for (;;) {
        if (!tbox_css_parser_parse_compound(parser, &simple_selectors, combinator)) {
            return false;
        }

        if (parser->current.type == TBOX_CSS_TOKEN_S) {
            tbox_css_parser_advance(parser);
            if (parser->current.type == TBOX_CSS_TOKEN_GT) {
                combinator = TBOX_CSS_COMBINATOR_CHILD;
                tbox_css_parser_advance(parser);
                tbox_css_parser_skip_s(parser);
                continue;
            }
            if (parser->current.type == TBOX_CSS_TOKEN_PLUS) {
                combinator = TBOX_CSS_COMBINATOR_ADJACENT_SIBLING;
                tbox_css_parser_advance(parser);
                tbox_css_parser_skip_s(parser);
                continue;
            }
            if (tbox_css_token_can_start_simple_selector(parser->current.type)) {
                combinator = TBOX_CSS_COMBINATOR_DESCENDANT;
                continue;
            }
            break; /* trailing S before ',' / '{' / garbage -- selector is done */
        }
        if (parser->current.type == TBOX_CSS_TOKEN_GT) {
            combinator = TBOX_CSS_COMBINATOR_CHILD;
            tbox_css_parser_advance(parser);
            tbox_css_parser_skip_s(parser);
            continue;
        }
        if (parser->current.type == TBOX_CSS_TOKEN_PLUS) {
            combinator = TBOX_CSS_COMBINATOR_ADJACENT_SIBLING;
            tbox_css_parser_advance(parser);
            tbox_css_parser_skip_s(parser);
            continue;
        }
        break;
    }

    out_selector->simple_selectors      = simple_selectors.data;
    out_selector->simple_selector_count = simple_selectors.length;
    return true;
}

/* selector_group: selector [ ',' S* selector ]* */
static bool tbox_css_parser_parse_selector_group(tbox_css_parser *parser, tbox_vector *out_selectors) {
    for (;;) {
        tbox_css_selector selector;
        if (!tbox_css_parser_parse_selector(parser, &selector)) {
            return false;
        }
        *(tbox_css_selector *)tbox_vector_push(out_selectors) = selector;

        if (parser->current.type != TBOX_CSS_TOKEN_COMMA) {
            return true;
        }
        tbox_css_parser_advance(parser);
        tbox_css_parser_skip_s(parser);
    }
}

/* declaration: IDENT S* ':' S* <raw tokens up to top-level ';' or '}'>
 *
 * STRING/URL tokens are already atomic (the tokenizer swallows their
 * internal ';'/'{'/'}'/quotes as part of one token), so this only needs to
 * track '('/'['/'{' vs ')'/']'/'}' depth at the *token* level -- it never
 * needs to special-case string/URL contents itself. Uses
 * tokenizer.position (the byte cursor after consuming a token), not
 * token.text.size, to find the end of the raw slice: STRING/URL/HASH/
 * AT_KEYWORD tokens all strip delimiters from .text, so .text.size alone
 * would under-count the source bytes actually consumed.
 *
 * Worked example: `content: ";";` -- the ';' inside the STRING token never
 * desynchronizes anything because it's part of one atomic STRING token; the
 * captured raw value is `";"`, quotes included.
 *
 * Worked example: `color red; margin: 0;` -- `color` isn't followed by ':',
 * so this falls into skip_to_semicolon_or_rbrace and discards `color red;`
 * whole; the next call starts fresh at `margin` and succeeds normally. */
static bool tbox_css_parser_parse_declaration(tbox_css_parser *parser, tbox_vector *out_declarations) {
    if (parser->current.type != TBOX_CSS_TOKEN_IDENT) {
        tbox_css_parser_skip_to_semicolon_or_rbrace(parser);
        return false;
    }
    tbox_string_view property = tbox_css_parser_copy_lower(parser, parser->current.text);
    tbox_css_parser_advance(parser);
    tbox_css_parser_skip_s(parser);

    if (parser->current.type != TBOX_CSS_TOKEN_COLON) {
        tbox_css_parser_skip_to_semicolon_or_rbrace(parser);
        return false;
    }
    tbox_css_parser_advance(parser);
    tbox_css_parser_skip_s(parser);

    size_t value_start          = parser->current.offset;
    size_t last_significant_end = value_start;
    int depth                   = 0;

    while (parser->current.type != TBOX_CSS_TOKEN_EOF && !(depth == 0 && (parser->current.type == TBOX_CSS_TOKEN_SEMICOLON || parser->current.type == TBOX_CSS_TOKEN_RBRACE))) {
        tbox_css_token_type type = parser->current.type;

        if (tbox_css_token_opens(type)) {
            depth++;
        } else if (tbox_css_token_closes(type) && depth > 0) {
            depth--;
        }

        /* tokenizer.position is, right now (before advancing), the byte
         * offset just past the *current* token -- advancing would move it
         * past whatever token comes next instead, which is why this capture
         * has to happen here and not after tbox_css_parser_advance below. */
        if (type != TBOX_CSS_TOKEN_S) {
            last_significant_end = parser->tokenizer.position;
        }

        tbox_css_parser_advance(parser);
    }

    tbox_string_view raw_value = tbox_string_view_make(parser->tokenizer.input + value_start, last_significant_end - value_start);

    tbox_css_declaration *declaration = tbox_vector_push(out_declarations);
    declaration->property             = property;
    declaration->value                = tbox_css_parser_copy(parser, raw_value);

    if (parser->current.type == TBOX_CSS_TOKEN_SEMICOLON) {
        tbox_css_parser_advance(parser);
    }

    return true;
}

/* declaration_list: S* [ declaration? [ ';' S* declaration? ]* ]
 * Current token is '{' on entry; consumes through the matching '}' (or EOF)
 * itself. */
static void tbox_css_parser_parse_declaration_list(tbox_css_parser *parser, tbox_css_ruleset *out_ruleset) {
    tbox_css_parser_advance(parser); /* '{' */

    tbox_vector declarations;
    tbox_vector_init(&declarations, parser->arena, sizeof(tbox_css_declaration), 0);

    while (parser->current.type != TBOX_CSS_TOKEN_RBRACE && parser->current.type != TBOX_CSS_TOKEN_EOF) {
        if (parser->current.type == TBOX_CSS_TOKEN_S || parser->current.type == TBOX_CSS_TOKEN_SEMICOLON) {
            tbox_css_parser_advance(parser);
            continue;
        }
        tbox_css_parser_parse_declaration(parser, &declarations);
    }

    if (parser->current.type == TBOX_CSS_TOKEN_RBRACE) {
        tbox_css_parser_advance(parser);
    }

    out_ruleset->declarations      = declarations.data;
    out_ruleset->declaration_count = declarations.length;
}

/* ruleset: selector_group '{' declaration_list '}'
 *
 * Worked example: `1bad { color: red; } p { color: blue; }` -- parse_compound
 * fails on the leading NUMBER token, so parse_selector_group fails having
 * consumed nothing; recover_ruleset then scans forward to the next
 * top-level '{' and discards that whole block, resynchronizing exactly at
 * `p { ... }`, which then parses normally.
 *
 * Worked example: `p, { color: red; }` -- the second selector attempt (after
 * the comma) fails immediately on '{', so the *whole* selector_group fails
 * even though `p` alone parsed fine; recover_ruleset discards the entire
 * `{ color: red; }` block, dropping the rule completely -- this matches
 * CSS2.1's rule that an error anywhere in a selector group invalidates the
 * whole ruleset. */
static bool tbox_css_parser_parse_ruleset(tbox_css_parser *parser, tbox_css_ruleset *out_ruleset) {
    tbox_vector selectors;
    tbox_vector_init(&selectors, parser->arena, sizeof(tbox_css_selector), 0);

    if (!tbox_css_parser_parse_selector_group(parser, &selectors) || parser->current.type != TBOX_CSS_TOKEN_LBRACE) {
        tbox_css_parser_recover_ruleset(parser);
        return false;
    }

    out_ruleset->selectors      = selectors.data;
    out_ruleset->selector_count = selectors.length;

    tbox_css_parser_parse_declaration_list(parser, out_ruleset);
    return true;
}

/* stylesheet: [ CDO | CDC | S | statement ]*
 * Forward-progress invariant: every iteration either consumes at least one
 * S/CDO/CDC/AT_KEYWORD token, or calls parse_ruleset, which -- whether it
 * succeeds or calls recover_ruleset -- always leaves the tokenizer strictly
 * past where it started (recovery always finds a block to discard or
 * reaches EOF). So the parser can never spin without making progress on
 * malformed input. */
void tbox_css_parser_run(tbox_css_parser *parser, tbox_css_ruleset **out_rulesets, size_t *out_ruleset_count) {
    tbox_vector rulesets;
    tbox_vector_init(&rulesets, parser->arena, sizeof(tbox_css_ruleset), 0);

    for (;;) {
        while (parser->current.type == TBOX_CSS_TOKEN_S || parser->current.type == TBOX_CSS_TOKEN_CDO || parser->current.type == TBOX_CSS_TOKEN_CDC) {
            tbox_css_parser_advance(parser);
        }
        if (parser->current.type == TBOX_CSS_TOKEN_EOF) {
            break;
        }
        if (parser->current.type == TBOX_CSS_TOKEN_AT_KEYWORD) {
            tbox_css_parser_advance(parser);
            tbox_css_parser_skip_at_rule(parser);
            continue;
        }

        tbox_css_ruleset ruleset;
        if (tbox_css_parser_parse_ruleset(parser, &ruleset)) {
            *(tbox_css_ruleset *)tbox_vector_push(&rulesets) = ruleset;
        }
    }

    *out_rulesets      = rulesets.data;
    *out_ruleset_count = rulesets.length;
}

/* selector_group with no trailing declaration block; see the header comment
 * for the hard-fail-on-error rationale. */
bool tbox_css_parser_parse_standalone_selector_group(const char *input, size_t length, tbox_arena *arena,
                                                       tbox_css_selector **out_selectors, size_t *out_count,
                                                       size_t *out_error_offset) {
    tbox_css_parser parser;
    tbox_css_parser_init(&parser, input, length, arena);
    tbox_css_parser_skip_s(&parser);

    tbox_vector selectors;
    tbox_vector_init(&selectors, arena, sizeof(tbox_css_selector), 0);

    if (!tbox_css_parser_parse_selector_group(&parser, &selectors)) {
        if (out_error_offset != NULL) {
            *out_error_offset = parser.current.offset;
        }
        return false;
    }

    tbox_css_parser_skip_s(&parser);
    if (parser.current.type != TBOX_CSS_TOKEN_EOF) {
        if (out_error_offset != NULL) {
            *out_error_offset = parser.current.offset;
        }
        return false;
    }

    *out_selectors = selectors.data;
    *out_count     = selectors.length;
    return true;
}
