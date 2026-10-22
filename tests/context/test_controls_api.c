#include <stdint.h>
#include <string.h>
#include <tbox/tbox.h>

#include "context/tbox_context_controls.h"
#include "test_support.h"

static bool text_eq(tbox_string_view view, const char *expected) {
    size_t length = strlen(expected);
    return view.size == length && (length == 0 || memcmp(view.data, expected, length) == 0);
}

static tbox_context *open_html(const char *html, tbox_font_face_cache *fonts) {
    return tbox_context_open(html, strlen(html), "", 0, fonts, NULL);
}

static int tbox_test_context_lookup(tbox_font_face_cache *fonts) {
    int failures     = 0;
    tbox_context *ctx = open_html("<body><div id='a'><p class='x' id='p1'>1</p><span><p class='x'>2</p></span></div>"
        "<p id='b'>3</p></body>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);

    tbox_html_node *p1 = tbox_context_get_element_by_id(ctx, tbox_string_view_make("p1", 2));
    TBOX_TEST_REQUIRE(p1 != NULL);
    TBOX_TEST_ASSERT(text_eq(p1->element.tag_name, "p"));
    TBOX_TEST_ASSERT(tbox_context_get_element_by_id(ctx, tbox_string_view_make("P1", 2)) == NULL);
    TBOX_TEST_ASSERT(tbox_context_get_element_by_id(ctx, tbox_string_view_make(NULL, 0)) == NULL);

    TBOX_TEST_ASSERT(tbox_context_query(ctx, ".x", 2) == p1);
    TBOX_TEST_ASSERT(tbox_context_query(ctx, "#b", 2) == tbox_context_get_element_by_id(ctx, tbox_string_view_make("b", 1)));
    TBOX_TEST_ASSERT(tbox_context_query(ctx, "#nope", 5) == NULL);
    TBOX_TEST_ASSERT(tbox_context_query(ctx, "[", 1) == NULL);

    tbox_css_selector_node_set all = tbox_context_query_all(ctx, "p", 1);
    TBOX_TEST_ASSERT(all.count == 3 && all.items[0] == p1);
    tbox_css_selector_node_set_destroy(&all);

    tbox_html_node *inner = p1->parent->first_child->next_sibling->first_child;
    TBOX_TEST_ASSERT(tbox_css_selector_closest(inner, "div", 3) == p1->parent);
    TBOX_TEST_ASSERT(tbox_css_selector_closest(inner, ".x", 2) == inner);
    TBOX_TEST_ASSERT(tbox_css_selector_closest(inner, "table", 5) == NULL);

    TBOX_TEST_ASSERT(tbox_context_query(NULL, "p", 1) == NULL);
    tbox_context_close(ctx);
    return failures;
}

static tbox_html_node *by_id(tbox_context *ctx, const char *id) {
    return tbox_context_get_element_by_id(ctx, tbox_string_view_make(id, strlen(id)));
}

static tbox_string_view sv(const char *text) {
    return tbox_string_view_make(text, strlen(text));
}

static int tbox_test_control_values(tbox_font_face_cache *fonts) {
    int failures      = 0;
    tbox_context *ctx = open_html("<body><input id='t' value='abc'><textarea id='ta'>old</textarea>"
        "<input id='n' type='number' value='3'><input id='r' type='range' min='0' max='10' step='2' value='4'>"
        "<input id='f' type='file'><output id='o'>1</output><progress id='p' value='0.5'></progress>"
        "<button id='b'>Go <b>now</b></button><input id='s' type='submit' value='Send'>"
        "<input id='c' type='checkbox'><p id='plain'>x</p></body>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 400.0, 300.0, &list);

    tbox_html_node *t = by_id(ctx, "t");
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, t), "abc"));
    /* A field being edited takes the new value with the caret at its end. */
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, t));
    TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == t);
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, t, sv("hello")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, t), "hello"));
    TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, sv("!")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, t), "hello!"));

    tbox_html_node *ta = by_id(ctx, "ta");
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, ta), "old"));
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, ta));
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, ta, sv("line1\nline2")));
    TBOX_TEST_ASSERT(tbox_context_dispatch_text(ctx, sv("?")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, ta), "line1\nline2?"));
    tbox_context_control_blur(ctx);
    TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);

    tbox_html_node *n = by_id(ctx, "n");
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, n, sv("abc")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, n), ""));
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, n, sv("12.5")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, n), "12.5"));
    tbox_html_node *r = by_id(ctx, "r");
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, r, sv("99")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, r), "10"));
    tbox_html_node *f = by_id(ctx, "f");
    TBOX_TEST_ASSERT(!tbox_context_control_set_value(ctx, f, sv("x.txt")));
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, f, sv("")));

    tbox_html_node *o = by_id(ctx, "o");
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, o, sv("42")) && text_eq(tbox_context_control_value(ctx, o), "42"));
    tbox_html_node *p = by_id(ctx, "p");
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, p, sv("0.75")) && text_eq(tbox_context_control_value(ctx, p), "0.75"));
    TBOX_TEST_ASSERT(!tbox_context_control_set_value(ctx, by_id(ctx, "plain"), sv("v")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, by_id(ctx, "c")), "on"));

    /* Captions: element text, or `value` for button-type inputs. */
    tbox_html_node *b = by_id(ctx, "b");
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_label(ctx, b), "Go now"));
    TBOX_TEST_ASSERT(tbox_context_control_set_label(ctx, b, sv("Stop")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_label(ctx, b), "Stop") && b->first_child == b->last_child);
    tbox_html_node *s = by_id(ctx, "s");
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_label(ctx, s), "Send"));
    TBOX_TEST_ASSERT(tbox_context_control_set_label(ctx, s, sv("Save")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, s), "Save"));
    TBOX_TEST_ASSERT(!tbox_context_control_set_label(ctx, t, sv("no")));

    /* Disabling the focused control drops focus; disabled can't be focused. */
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, b));
    tbox_context_control_set_disabled(ctx, b, true);
    TBOX_TEST_ASSERT(tbox_context_control_disabled(b) && tbox_context_focused_node(ctx) == NULL);
    TBOX_TEST_ASSERT(!tbox_context_control_focus(ctx, b));
    tbox_context_control_set_disabled(ctx, b, false);
    TBOX_TEST_ASSERT(!tbox_context_control_disabled(b) && tbox_context_control_focus(ctx, b));
    TBOX_TEST_ASSERT(!tbox_context_control_focus(ctx, by_id(ctx, "plain")));

    tbox_context_close(ctx);
    return failures;
}

static int tbox_test_control_checked(tbox_font_face_cache *fonts) {
    int failures      = 0;
    tbox_context *ctx = open_html("<body><input id='c' type='checkbox'>"
        "<form><input id='r1' type='radio' name='g' checked><input id='r2' type='radio' name='g'></form>"
        "<input id='r3' type='radio' name='g'><p id='plain'></p></body>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *c = by_id(ctx, "c"), *r1 = by_id(ctx, "r1"), *r2 = by_id(ctx, "r2"), *r3 = by_id(ctx, "r3");

    TBOX_TEST_ASSERT(!tbox_context_control_checked(c));
    TBOX_TEST_ASSERT(tbox_context_control_set_checked(ctx, c, true) && tbox_context_control_checked(c));
    TBOX_TEST_ASSERT(tbox_context_control_set_checked(ctx, c, false) && !tbox_context_control_checked(c));
    TBOX_TEST_ASSERT(!tbox_context_control_set_checked(ctx, by_id(ctx, "plain"), true));

    TBOX_TEST_ASSERT(tbox_context_control_radio_checked(ctx, r2) == r1);
    TBOX_TEST_ASSERT(tbox_context_control_set_checked(ctx, r3, true)); /* other form: separate group */
    TBOX_TEST_ASSERT(tbox_context_control_checked(r1) && tbox_context_control_checked(r3));
    TBOX_TEST_ASSERT(tbox_context_control_set_checked(ctx, r2, true));
    TBOX_TEST_ASSERT(!tbox_context_control_checked(r1) && tbox_context_control_checked(r2));
    TBOX_TEST_ASSERT(tbox_context_control_radio_checked(ctx, r1) == r2);
    TBOX_TEST_ASSERT(tbox_context_control_set_checked(ctx, r2, false));
    TBOX_TEST_ASSERT(tbox_context_control_radio_checked(ctx, r1) == NULL);

    /* :checked follows the API. */
    const char *css = "#c:checked { width: 31px; }";
    tbox_context_close(ctx);
    ctx = tbox_context_open("<input id='c' type='checkbox'>", 30, css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    c = by_id(ctx, "c");
    tbox_context_control_set_checked(ctx, c, true);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 200.0, 100.0, &list);
    const tbox_layout_box *box = tbox_context_hit_test(ctx, 5.0, 5.0);
    TBOX_TEST_ASSERT(box != NULL && box->node == c && box->style != NULL && box->style->width.value == 31.0);
    tbox_context_close(ctx);
    return failures;
}

static int tbox_test_select_options(tbox_font_face_cache *fonts) {
    int failures      = 0;
    tbox_context *ctx = open_html("<select id='s'><option value='a'>A</option>"
        "<optgroup label='G'><option value='b' selected>B</option><option disabled>C</option></optgroup></select>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *s = by_id(ctx, "s");
    TBOX_TEST_ASSERT(tbox_context_select_option_count(s) == 3);
    TBOX_TEST_ASSERT(tbox_context_select_selected_index(ctx, s) == 1);
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, s), "b"));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, tbox_context_select_option_at(s, 2)), ""));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_label(ctx, tbox_context_select_option_at(s, 2)), "C"));

    TBOX_TEST_ASSERT(!tbox_context_select_set_selected_index(ctx, s, 2)); /* disabled */
    TBOX_TEST_ASSERT(!tbox_context_select_set_selected_index(ctx, s, 9));
    TBOX_TEST_ASSERT(tbox_context_select_set_selected_index(ctx, s, 0) && text_eq(tbox_context_control_value(ctx, s), "a"));
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, s, sv("a"))); /* already chosen: still applied */

    /* Inserted before an option inside a group lands in that group. */
    tbox_html_node *z = tbox_context_select_add_option(ctx, s, sv("z"), sv("Zed"), 1);
    TBOX_TEST_REQUIRE(z != NULL);
    TBOX_TEST_ASSERT(tbox_context_select_option_at(s, 1) == z && text_eq(z->parent->element.tag_name, "optgroup"));
    tbox_html_node *last = tbox_context_select_add_option(ctx, s, tbox_string_view_make(NULL, 0), sv("Last"), SIZE_MAX);
    TBOX_TEST_ASSERT(tbox_context_select_option_count(s) == 5 && tbox_context_select_option_at(s, 4) == last && last->parent == s);
    TBOX_TEST_ASSERT(tbox_context_control_set_value(ctx, s, sv("Last")) && tbox_context_select_selected_index(ctx, s) == 4);

    /* Removing the chosen option falls back to the `selected` one. */
    TBOX_TEST_ASSERT(tbox_context_select_remove_option(ctx, s, 4));
    TBOX_TEST_ASSERT(tbox_context_select_option_count(s) == 4 && text_eq(tbox_context_control_value(ctx, s), "b"));
    TBOX_TEST_ASSERT(!tbox_context_select_remove_option(ctx, s, 4));

    tbox_context_select_clear(ctx, s);
    TBOX_TEST_ASSERT(tbox_context_select_option_count(s) == 0 && tbox_context_select_selected_index(ctx, s) == -1);
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_value(ctx, s), ""));
    tbox_context_select_add_option(ctx, s, sv("n"), sv("New"), 0);
    TBOX_TEST_ASSERT(tbox_context_select_selected_index(ctx, s) == 0 && text_eq(tbox_context_control_value(ctx, s), "n"));

    tbox_display_list list;
    tbox_context_run_frame(ctx, 300.0, 200.0, &list);
    tbox_context_close(ctx);
    return failures;
}

int tbox_test_controls_api_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    failures += tbox_test_context_lookup(fonts);
    failures += tbox_test_control_values(fonts);
    failures += tbox_test_control_checked(fonts);
    failures += tbox_test_select_options(fonts);
    return failures;
}
