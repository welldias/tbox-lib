#include <tbox/style.h>

#include <string.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "css_parser/tbox_css_stylesheet.h"
#include "test_support.h"

static tbox_css_stylesheet *css(const char *text) { return tbox_css_parse(text, strlen(text)); }
static bool eq(tbox_string_view text, const char *expected) { return tbox_string_view_equal_cstr(text, expected); }

int tbox_test_style_at_rules_run(void) {
    int failures = 0;
    {
        const char *html = "<div><p></p><p class='bad'></p><p class='inherit'></p></div>";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *sheet = css(
            "@property --cor { syntax: '<color>'; inherits: false; initial-value: #008000; }"
            "@property --size { syntax: '<length>'; inherits: true; initial-value: 3px; }"
            "div { font-size: 20px; --cor: red; --size: 2em; }"
            "p { font-size: 10px; background-color: var(--cor); width: var(--size); }"
            ".bad { --cor: 10px; --size: nonsense; } .inherit { --cor: inherit; }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        const tbox_html_node *parent = tbox_html_document_root(doc)->first_child;
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *normal = tbox_style_table_find(&table, parent->first_child);
        const tbox_style *bad = tbox_style_table_find(&table, parent->first_child->next_sibling);
        const tbox_style *inherit = tbox_style_table_find(&table, parent->last_child);
        TBOX_TEST_REQUIRE(normal != NULL && bad != NULL && inherit != NULL);
        TBOX_TEST_ASSERT(normal->background_color.g == 128 && normal->background_color.r == 0);
        TBOX_TEST_ASSERT(normal->width.kind == TBOX_STYLE_LENGTH_PX && normal->width.value == 40);
        TBOX_TEST_ASSERT(bad->background_color.g == 128 && bad->width.value == 3);
        TBOX_TEST_ASSERT(inherit->background_color.r == 255 && inherit->background_color.g == 0);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(sheet);
        tbox_html_document_destroy(doc);
    }
    {
        const char *html = "<div></div>";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *sheet = css("@property --size { syntax:'<length>'; inherits:false; initial-value:1px; } div { --font:20px; font-size:var(--font); --size:2em; width:var(--size); }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *style = tbox_style_table_find(&table, tbox_html_document_root(doc)->first_child);
        TBOX_TEST_REQUIRE(style != NULL);
        TBOX_TEST_ASSERT(style->font_size == 20 && style->width.value == 40);
        tbox_arena_destroy(&arena); tbox_css_stylesheet_destroy(sheet); tbox_html_document_destroy(doc);
    }
    /* Typed values are computed once, then inherited as canonical text. */
    {
        static const struct { const char *syntax, *value, *expected, *invalid; } cases[] = {
            { "<length>", "2em", "40px", "20%" },
            { "<color>", "#ff0000", "rgba(255,0,0,1)", "3px" },
            { "<number>", "calc(2 + .5)", "2.5", "2px" },
            { "<percentage>", "25%", "25%", "25px" },
            { "<length-percentage>", "calc(2em + 25%)", "calc(25% + 40px)", "auto" },
            { "<angle>", ".5turn", "180deg", "10px" },
            { "<integer>", "-3", "-3", "1.5" },
            { "*", "anything 20px", "anything 20px", NULL },
        };
        tbox_arena arena = tbox_arena_create(0);
        tbox_style_units units = { 20, 16, 800, 600, false, -1.0, -1.0, 1.0, TBOX_STYLE_FRAME_NONE, 0.0, 0.0 };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_string_view out = {0};
            TBOX_TEST_ASSERT_MSG(tbox_style_registered_value(&arena, tbox_string_view_from_cstr(cases[i].syntax), tbox_string_view_from_cstr(cases[i].value), &units, &out), cases[i].syntax);
            TBOX_TEST_ASSERT(eq(out, cases[i].expected));
            if (cases[i].invalid != NULL)
                TBOX_TEST_ASSERT(!tbox_style_registered_value(&arena, tbox_string_view_from_cstr(cases[i].syntax), tbox_string_view_from_cstr(cases[i].invalid), &units, &out));
        }
        tbox_arena_destroy(&arena);
    }
    {
        tbox_arena arena = tbox_arena_create(0);
        tbox_style_units units = { 20, 16, 800, 600, false, -1.0, -1.0, 1.0, TBOX_STYLE_FRAME_NONE, 0.0, 0.0 };
        tbox_string_view out;
        TBOX_TEST_ASSERT(!tbox_style_registered_value(&arena, tbox_string_view_from_cstr("<number>"), tbox_string_view_from_cstr("calc(25%)"), &units, &out));
        TBOX_TEST_ASSERT(!tbox_style_registered_value(&arena, tbox_string_view_from_cstr("<length>"), tbox_string_view_from_cstr("calc(2)"), &units, &out));
        tbox_arena_destroy(&arena);
    }
    /* A universal registration without an initial is undefined on the
     * child with inherits:false; an empty declared value stays defined. */
    {
        const char *html = "<div><p></p></div>";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *sheet = css("@property --empty { syntax:'*'; inherits:TRUE; } @property --local { syntax:'*'; inherits:false; } div { --empty:; --local:50px; } p { width:var(--local, 7px); margin-left:var(--empty, 8px); }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        const tbox_style *style = tbox_style_table_find(&table, tbox_html_document_root(doc)->first_child->first_child);
        TBOX_TEST_REQUIRE(style != NULL);
        TBOX_TEST_ASSERT(style->width.value == 7 && style->margin[3].value == 0);
        tbox_arena_destroy(&arena); tbox_css_stylesheet_destroy(sheet); tbox_html_document_destroy(doc);
    }
    /* Bad registrations are ignored, names remain case-sensitive, and the
     * last valid registration overrides a previous one across sheets. */
    {
        const char *html = "<div><p></p></div>";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *a = css(
            "@property --x { syntax:'<length>'; inherits:false; initial-value:5px; }"
            "@property --x { syntax:'<length>'; inherits:false; initial-value:2em; }"
            "@property --bad { syntax:'<length>'; initial-value:8px; }"
            "div { --bad:9px; --X:11px; } p { width:var(--x); height:var(--X); margin-left:var(--bad); }");
        tbox_css_stylesheet *b = css("@media (min-width:600px) { @property --x { syntax:'<length>'; inherits:false; initial-value:7px; } }");
        tbox_css_cascade_source sources[] = { { a, TBOX_CSS_ORIGIN_AUTHOR }, { b, TBOX_CSS_ORIGIN_AUTHOR } };
        tbox_arena arena = tbox_arena_create(0);
        const tbox_html_node *p = tbox_html_document_root(doc)->first_child->first_child;
        tbox_style_table table = tbox_style_resolve_tree_in_viewport(&arena, tbox_html_document_root(doc), sources, 2, 800, 600);
        const tbox_style *style = tbox_style_table_find(&table, p);
        TBOX_TEST_REQUIRE(style != NULL);
        TBOX_TEST_ASSERT(style->width.value == 7 && style->height.value == 11 && style->margin[3].value == 9);
        table = tbox_style_resolve_tree_in_viewport(&arena, tbox_html_document_root(doc), sources, 2, 400, 600);
        style = tbox_style_table_find(&table, p);
        TBOX_TEST_ASSERT(style->width.value == 5);
        tbox_arena_destroy(&arena);
        tbox_css_stylesheet_destroy(a); tbox_css_stylesheet_destroy(b);
        tbox_html_document_destroy(doc);
    }
    {
        tbox_css_stylesheet *sheet = css(
            "@counter-style cycle { system:cyclic; symbols:'*' '+'; prefix:'['; suffix:'] '; }"
            "@counter-style roman { system:additive; additive-symbols:10 X, 9 IX, 5 V, 4 IV, 1 I; }"
            "@counter-style alpha { system:alphabetic; symbols:A B C; prefix:'('; suffix:') '; }"
            "@counter-style fix { system:fixed -1; symbols:A B C; fallback:alpha; prefix:'<'; suffix:'>  '; }"
            "@counter-style fixed-decimal { system:fixed; symbols:'X'; prefix:'['; suffix:']  '; }"
            "@counter-style bin { system:numeric; symbols:'0' '1'; pad:5 '0'; negative:'(' ')'; }"
            "@counter-style sym { symbols:'*' '+'; }"
            "@counter-style limited { system:cyclic; symbols:'!'; range:2 3, 6 8; fallback:alpha; prefix:'{'; suffix:'} '; }"
            "@counter-style derived { system:extends cycle; prefix:'('; suffix:')'; }"
            "@counter-style builtin { system:extends upper-roman; prefix:'<'; suffix:'>'; }"
            "@counter-style cyclic-fallback { system:fixed; symbols:'X'; fallback:cyclic-fallback; }"
            "@counter-style bad { system:numeric; symbols:'x'; }");
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_arena arena = tbox_arena_create(0);
        tbox_style_counter_styles *styles = tbox_style_counter_styles_create(&arena, &source, 1, 800, 600, false);
        tbox_css_stylesheet_destroy(sheet); /* the frame owns its descriptor copies */
        static const struct { const char *name; long value; bool marker; const char *expected; } cases[] = {
            { "cycle", 3, true, "[*] " }, { "cycle", 0, false, "+" }, { "cycle", -1, false, "*" },
            { "roman", 14, false, "XIV" }, { "roman", 0, false, "0" },
            { "alpha", 4, true, "(AA) " }, { "fix", -1, false, "A" }, { "fix", 0, false, "B" },
            { "fix", 2, false, "B" },
            /* Fallback supplies only the representation; the named style
             * keeps its marker prefix/suffix, including trailing spaces. */
            { "fix", 2, true, "<B>  " }, { "fixed-decimal", 4, true, "[4]  " },
            { "fixed-decimal", 4, false, "4" }, { "fixed-decimal", 5, true, "[5]  " }, { "bin", 3, false, "00011" }, { "bin", -3, false, "(011)" },
            { "sym", 3, false, "**" }, { "sym", -3, false, "-3" },
            { "limited", 1, false, "A" }, { "limited", 1, true, "{A} " }, { "limited", 6, false, "!" },
            { "derived", 2, true, "(+)" }, { "builtin", 9, true, "<IX>" },
            { "cyclic-fallback", 2, false, "2" }, { "bad", 1, false, "1" },
            { "'hello'", 3, true, "hello" }, { "symbols(cyclic '*' '+')", 4, true, "+ " },
            { "symbols(alphabetic 'a' 'b')", 3, false, "aa" },
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            tbox_string_view formatted = tbox_style_counter_format(&arena, styles, tbox_string_view_from_cstr(cases[i].name), cases[i].value, cases[i].marker);
            TBOX_TEST_ASSERT_MSG(eq(formatted, cases[i].expected), cases[i].name);
        }
        tbox_arena_destroy(&arena);
    }
    /* Registered counter styles feed both counter() and nested counters().
     * Prefix/suffix decorate markers only, not content representations. */
    {
        const char *html = "<ol><li><ol><li></li></ol></li></ol>";
        tbox_html_document *doc = tbox_html_parse(html, strlen(html));
        tbox_css_stylesheet *sheet = css("@counter-style x { system:cyclic; symbols:A B; prefix:'('; suffix:')'; } ol { counter-reset:n; } li { counter-increment:n; } li::before { content:counter(n,x) '/' counters(n,'.',x); }");
        tbox_arena arena = tbox_arena_create(0);
        tbox_css_cascade_source source = { sheet, TBOX_CSS_ORIGIN_AUTHOR };
        tbox_style_table table = tbox_style_resolve_tree(&arena, tbox_html_document_root(doc), &source, 1);
        size_t found = 0;
        const char *expected[] = { "A/A", "A/A.A" };
        for (size_t i = 0; i < table.count; i++) if (table.items[i].style->generated) {
            TBOX_TEST_ASSERT(found < 2 && eq(table.items[i].node->first_child->text.text, expected[found]));
            found++;
        }
        TBOX_TEST_ASSERT(found == 2);
        tbox_arena_destroy(&arena); tbox_css_stylesheet_destroy(sheet); tbox_html_document_destroy(doc);
    }
    return failures;
}
