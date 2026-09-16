#include <tbox/css_parser.h>

#include <string.h>

#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t expected_len = strlen(expected);
    return view.size == expected_len && memcmp(view.data, expected, expected_len) == 0;
}

static tbox_css_stylesheet *parse_cstr(const char *css) {
    return tbox_css_parse(css, strlen(css));
}

int tbox_test_css_parser_parser_run(void) {
    int failures = 0;

    /* 1: empty stylesheet. */
    {
        tbox_css_stylesheet *ss = parse_cstr("");
        TBOX_TEST_ASSERT(ss != NULL);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 0);
        tbox_css_stylesheet_destroy(ss);
    }

    /* 2: one ruleset, one selector, one declaration. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p { color: red; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);

        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(r[0].selector_count == 1);
        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selector_count == 1);
        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selectors[0].combinator_before == TBOX_CSS_COMBINATOR_NONE);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "red"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 3: trailing ';' before '}' is optional. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p { color: red; margin: 0 }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);

        TBOX_TEST_ASSERT(r[0].declaration_count == 2);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[1].property, "margin"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[1].value, "0"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 4: multiple selectors in one group. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("h1, h2, .title { color: blue; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);

        TBOX_TEST_ASSERT(r[0].selector_count == 3);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "h1"));
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[1].simple_selectors[0].name, "h2"));
        TBOX_TEST_ASSERT(r[0].selectors[2].simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_CLASS);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[2].simple_selectors[0].name, "title"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 5: descendant combinator. */
    {
        tbox_css_stylesheet *ss    = parse_cstr("div p { color: green; }");
        const tbox_css_ruleset *r  = tbox_css_stylesheet_rulesets(ss);
        const tbox_css_selector *s = &r[0].selectors[0];

        TBOX_TEST_ASSERT(s->simple_selector_count == 2);
        TBOX_TEST_ASSERT(text_eq(s->simple_selectors[0].name, "div") && s->simple_selectors[0].combinator_before == TBOX_CSS_COMBINATOR_NONE);
        TBOX_TEST_ASSERT(text_eq(s->simple_selectors[1].name, "p") && s->simple_selectors[1].combinator_before == TBOX_CSS_COMBINATOR_DESCENDANT);

        tbox_css_stylesheet_destroy(ss);
    }

    /* 6: child combinator; empty declaration block is legal. */
    {
        tbox_css_stylesheet *ss    = parse_cstr("ul > li { }");
        const tbox_css_ruleset *r  = tbox_css_stylesheet_rulesets(ss);
        const tbox_css_selector *s = &r[0].selectors[0];

        TBOX_TEST_ASSERT(s->simple_selector_count == 2);
        TBOX_TEST_ASSERT(s->simple_selectors[1].combinator_before == TBOX_CSS_COMBINATOR_CHILD);
        TBOX_TEST_ASSERT(r[0].declaration_count == 0);

        tbox_css_stylesheet_destroy(ss);
    }

    /* 7: adjacent-sibling combinator. */
    {
        tbox_css_stylesheet *ss    = parse_cstr("h1 + p { }");
        const tbox_css_ruleset *r  = tbox_css_stylesheet_rulesets(ss);
        const tbox_css_selector *s = &r[0].selectors[0];

        TBOX_TEST_ASSERT(s->simple_selector_count == 2);
        TBOX_TEST_ASSERT(s->simple_selectors[1].combinator_before == TBOX_CSS_COMBINATOR_ADJACENT_SIBLING);

        tbox_css_stylesheet_destroy(ss);
    }

    /* 8: a fused compound has NONE combinators after the first item. */
    {
        tbox_css_stylesheet *ss    = parse_cstr("div#main.hero:hover { }");
        const tbox_css_ruleset *r  = tbox_css_stylesheet_rulesets(ss);
        const tbox_css_selector *s = &r[0].selectors[0];

        TBOX_TEST_ASSERT(s->simple_selector_count == 4);
        for (size_t i = 1; i < s->simple_selector_count; i++) {
            TBOX_TEST_ASSERT(s->simple_selectors[i].combinator_before == TBOX_CSS_COMBINATOR_NONE);
        }
        TBOX_TEST_ASSERT(s->simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_TYPE && text_eq(s->simple_selectors[0].name, "div"));
        TBOX_TEST_ASSERT(s->simple_selectors[1].kind == TBOX_CSS_SIMPLE_SELECTOR_ID && text_eq(s->simple_selectors[1].name, "main"));
        TBOX_TEST_ASSERT(s->simple_selectors[2].kind == TBOX_CSS_SIMPLE_SELECTOR_CLASS && text_eq(s->simple_selectors[2].name, "hero"));
        TBOX_TEST_ASSERT(s->simple_selectors[3].kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && text_eq(s->simple_selectors[3].name, "hover"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 9: universal selector. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("* { margin: 0; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);

        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selector_count == 1);
        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_UNIVERSAL);

        tbox_css_stylesheet_destroy(ss);
    }

    /* 10: a compound with no explicit type/universal has no synthetic item. */
    {
        tbox_css_stylesheet *ss    = parse_cstr(".btn:hover { }");
        const tbox_css_ruleset *r  = tbox_css_stylesheet_rulesets(ss);
        const tbox_css_selector *s = &r[0].selectors[0];

        TBOX_TEST_ASSERT(s->simple_selector_count == 2);
        TBOX_TEST_ASSERT(s->simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_CLASS && text_eq(s->simple_selectors[0].name, "btn"));
        TBOX_TEST_ASSERT(s->simple_selectors[1].kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO && text_eq(s->simple_selectors[1].name, "hover"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 11: attribute selector, all four operator forms across four rulesets. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("[disabled]{} [type=text]{} [class~=btn]{} [lang|=en]{}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);

        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 4);

        const tbox_css_simple_selector *a = &r[0].selectors[0].simple_selectors[0];
        TBOX_TEST_ASSERT(a->kind == TBOX_CSS_SIMPLE_SELECTOR_ATTRIBUTE && text_eq(a->name, "disabled") && a->attribute_operator == TBOX_CSS_ATTR_EXISTS);

        const tbox_css_simple_selector *b = &r[1].selectors[0].simple_selectors[0];
        TBOX_TEST_ASSERT(text_eq(b->name, "type") && b->attribute_operator == TBOX_CSS_ATTR_EQUALS && text_eq(b->attribute_value, "text"));

        const tbox_css_simple_selector *c = &r[2].selectors[0].simple_selectors[0];
        TBOX_TEST_ASSERT(text_eq(c->name, "class") && c->attribute_operator == TBOX_CSS_ATTR_INCLUDES && text_eq(c->attribute_value, "btn"));

        const tbox_css_simple_selector *d = &r[3].selectors[0].simple_selectors[0];
        TBOX_TEST_ASSERT(text_eq(d->name, "lang") && d->attribute_operator == TBOX_CSS_ATTR_DASHMATCH && text_eq(d->attribute_value, "en"));

        tbox_css_stylesheet_destroy(ss);
    }

    /* 12: attribute-selector value accepts both a bare ident and a quoted
     * string. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("[title=foo]{}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].attribute_value, "foo"));
        tbox_css_stylesheet_destroy(ss);

        tbox_css_stylesheet *ss2   = parse_cstr("[title=\"a b\"]{}");
        const tbox_css_ruleset *r2 = tbox_css_stylesheet_rulesets(ss2);
        TBOX_TEST_ASSERT(text_eq(r2[0].selectors[0].simple_selectors[0].attribute_value, "a b"));
        tbox_css_stylesheet_destroy(ss2);
    }

    /* 13: pseudo-class, single-colon pseudo-element, and a functional
     * pseudo-class. */
    {
        tbox_css_stylesheet *ss   = parse_cstr(":first-child{}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].selectors[0].simple_selectors[0].kind == TBOX_CSS_SIMPLE_SELECTOR_PSEUDO);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "first-child"));
        tbox_css_stylesheet_destroy(ss);

        tbox_css_stylesheet *ss2   = parse_cstr("p:before{}");
        const tbox_css_ruleset *r2 = tbox_css_stylesheet_rulesets(ss2);
        TBOX_TEST_ASSERT(r2[0].selectors[0].simple_selector_count == 2);
        TBOX_TEST_ASSERT(text_eq(r2[0].selectors[0].simple_selectors[1].name, "before"));
        tbox_css_stylesheet_destroy(ss2);

        tbox_css_stylesheet *ss3   = parse_cstr(":lang(en){}");
        const tbox_css_ruleset *r3 = tbox_css_stylesheet_rulesets(ss3);
        TBOX_TEST_ASSERT(text_eq(r3[0].selectors[0].simple_selectors[0].name, "lang"));
        TBOX_TEST_ASSERT(text_eq(r3[0].selectors[0].simple_selectors[0].pseudo_argument, "en"));
        tbox_css_stylesheet_destroy(ss3);
    }

    /* 14: declaration value preserves internal structure verbatim. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{font: 1em/1.5 \"Segoe UI\", sans-serif;}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "1em/1.5 \"Segoe UI\", sans-serif"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 15: a ';' inside a quoted string does not terminate the value early. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{content: \";\";}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "\";\""));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 16: a url(...) with unescaped '.'/'/' doesn't desync the following
     * declaration. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{background: url(../img/a.png) no-repeat; color: red;}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].declaration_count == 2);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "url(../img/a.png) no-repeat"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[1].value, "red"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 17: stray/doubled semicolons are tolerated without producing spurious
     * declarations. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{ ; color: red;; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        tbox_css_stylesheet_destroy(ss);
    }

    /* 18: a broken declaration (missing ':') is skipped; the sibling
     * declaration survives. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{ color red; margin: 0; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "margin"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 19: an empty value is not an error -- values aren't validated. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{ color: ; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(r[0].declarations[0].value.size == 0);
        tbox_css_stylesheet_destroy(ss);
    }

    /* 20: a malformed selector drops only that ruleset, not the next one. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("1bad { color: red; } p { color: blue; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 21: a CSS3-only pseudo-class (out of the CSS2.1 grammar) drops only
     * that ruleset. */
    {
        tbox_css_stylesheet *ss   = parse_cstr(":not(.foo) { color: red; } p { color: blue; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 22: a trailing comma in a selector group drops the whole rule. */
    {
        tbox_css_stylesheet *ss = parse_cstr("p, { color: red; }");
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 0);
        tbox_css_stylesheet_destroy(ss);
    }

    /* 23: an at-rule with a block is fully skipped without affecting either
     * neighbor. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p{color:red;} @media screen { div{color:blue;} } a{color:green;}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 2);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        TBOX_TEST_ASSERT(text_eq(r[1].selectors[0].simple_selectors[0].name, "a"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 24: an at-rule with no block (';'-terminated) is skipped. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@import url(foo.css); p{color:red;}");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 25: @charset/@font-face/@page are each individually skipped without
     * breaking a following ruleset. */
    {
        tbox_css_stylesheet *ss = parse_cstr("@charset \"utf-8\"; p{color:red;}");
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        tbox_css_stylesheet_destroy(ss);

        tbox_css_stylesheet *ss2 = parse_cstr("@font-face { font-family: X; src: url(x.woff); } p{color:red;}");
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss2) == 1);
        tbox_css_stylesheet_destroy(ss2);

        tbox_css_stylesheet *ss3 = parse_cstr("@page { margin: 1in; } p{color:red;}");
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss3) == 1);
        tbox_css_stylesheet_destroy(ss3);
    }

    /* 26: an unclosed block runs safely to EOF and still yields whatever was
     * captured before EOF. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p { color: red");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "red"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 27: top-level CDO/CDC are ignored like whitespace. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("<!-- p { color: red; } -->");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 28: a FUNCTION token (e.g. rotate(...)) inside a skipped @keyframes
     * block must count as opening a paren scope the same way LPAREN does --
     * otherwise its matching RPAREN desyncs the skip depth counter and the
     * ruleset following the at-rule is silently lost. Regression test for
     * the bug documented in ARCHITECTURE.md ("CSS Parser -- correção do bug
     * de profundidade de parênteses"). */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@keyframes spin { from { transform: rotate(0deg); } } p { color: black; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "black"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 29: same as 28 but with 'to' instead of 'from' -- the entire
     * @keyframes block (including the nested "to { ... }" block) is opaque
     * to the parser once skip_at_rule/skip_block take over, so "to" must
     * never be misread as a real selector producing a spurious ruleset;
     * exactly one ruleset (the trailing 'p') comes out, same as case 28. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@keyframes spin { to { transform: rotate(360deg); } } p { color: black; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "black"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 30: regression check -- an @media block with no function calls inside
     * (the case that already worked before the fix) still parses correctly
     * afterwards. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@media screen { div { color: red; } } span { color: blue; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "span"));
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "blue"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 31: regression check -- calc()/var() inside a declaration value at the
     * top level (never handed to skip_block/skip_at_rule/recover_ruleset at
     * all, since depth there was always 0 regardless of the fix) parses
     * unchanged. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("p { width: calc(100% - 10px); color: var(--main-color, blue); }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(r[0].declaration_count == 2);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].property, "width"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "calc(100% - 10px)"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[1].property, "color"));
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[1].value, "var(--main-color, blue)"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 32: a nested function call (a FUNCTION token appearing again inside
     * another FUNCTION's arguments) while skipping must not desync the depth
     * counter either -- two opens (translate(, calc() need to be balanced by
     * two closes (both RPARENs). */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@keyframes spin { from { transform: translate(calc(10px + 5px), 0); } } p { color: black; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "p"));
        TBOX_TEST_ASSERT(r[0].declaration_count == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].declarations[0].value, "black"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 33: a FUNCTION token in an at-rule's *prelude* (before '{'), not just
     * inside its block, exercises tbox_css_token_opens_nested (used by
     * skip_at_rule's prelude scan) rather than tbox_css_token_opens. */
    {
        tbox_css_stylesheet *ss   = parse_cstr("@media (min-width: calc(500px)) { div { color: red; } } span { color: blue; }");
        const tbox_css_ruleset *r = tbox_css_stylesheet_rulesets(ss);
        TBOX_TEST_ASSERT(tbox_css_stylesheet_ruleset_count(ss) == 1);
        TBOX_TEST_ASSERT(text_eq(r[0].selectors[0].simple_selectors[0].name, "span"));
        tbox_css_stylesheet_destroy(ss);
    }

    /* 34: destroying NULL is a safe no-op. */
    {
        tbox_css_stylesheet_destroy(NULL);
    }

    return failures;
}
