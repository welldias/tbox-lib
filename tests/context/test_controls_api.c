#include <stdint.h>
#include <stdio.h>
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

static int tbox_test_table_api(tbox_font_face_cache *fonts) {
    int failures      = 0;
    /* Out of order on purpose: rows count thead first and tfoot last. */
    tbox_context *ctx = open_html("<body><table id='t'><caption>c</caption>"
        "<tfoot><tr id='f'><td>F</td></tr></tfoot>"
        "<tr id='d'><td>D</td></tr>"
        "<tbody id='b1'><tr id='r1'><th>Ana</th><td><b id='bold'>x</b>y</td></tr></tbody>"
        "<thead><tr id='h'><th>Name</th><th>Info</th></tr></thead>"
        "<tbody id='b2'><tr id='r2'><td>Bia</td><td><table id='inner'><tr id='ir'><td id='ic'>i</td></tr></table></td></tr></tbody>"
        "</table><p id='p'>p</p></body>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *t     = by_id(ctx, "t");
    tbox_html_node *inner = by_id(ctx, "inner");

    const char *order[] = {"h", "d", "r1", "r2", "f"};
    TBOX_TEST_ASSERT(tbox_context_table_row_count(t) == 5);
    for (size_t i = 0; i < 5; i++) {
        TBOX_TEST_ASSERT(tbox_context_table_row_at(t, i) == by_id(ctx, order[i]));
        TBOX_TEST_ASSERT(tbox_context_table_row_index(by_id(ctx, order[i])) == (ptrdiff_t)i);
    }
    TBOX_TEST_ASSERT(tbox_context_table_row_at(t, 5) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_row_count(inner) == 1);
    TBOX_TEST_ASSERT(tbox_context_table_row_index(by_id(ctx, "ir")) == 0);
    TBOX_TEST_ASSERT(tbox_context_table_head(t) == by_id(ctx, "h")->parent);
    TBOX_TEST_ASSERT(tbox_context_table_foot(t) == by_id(ctx, "f")->parent);
    TBOX_TEST_ASSERT(tbox_context_table_row_count(by_id(ctx, "p")) == 0 && tbox_context_table_head(by_id(ctx, "p")) == NULL);

    /* Cells, text, and from a node back to its place. */
    tbox_html_node *r1 = by_id(ctx, "r1");
    TBOX_TEST_ASSERT(tbox_context_table_cell_count(r1) == 2);
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 2, 0), "Ana"));
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 2, 1), "xy"));
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 2, 2), ""));
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 9, 0), ""));
    tbox_html_node *bold = by_id(ctx, "bold");
    TBOX_TEST_ASSERT(tbox_context_table_row_of(bold) == r1);
    TBOX_TEST_ASSERT(tbox_context_table_cell_of(bold) == tbox_context_table_cell_at(r1, 1));
    TBOX_TEST_ASSERT(tbox_context_table_cell_index(tbox_context_table_cell_of(bold)) == 1);
    TBOX_TEST_ASSERT(tbox_context_table_row_of(by_id(ctx, "ic")) == by_id(ctx, "ir")); /* innermost */
    TBOX_TEST_ASSERT(tbox_context_table_cell_of(by_id(ctx, "ic")) == by_id(ctx, "ic"));
    TBOX_TEST_ASSERT(tbox_context_table_row_of(by_id(ctx, "p")) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_cell_of(r1) == NULL && tbox_context_table_cell_of(t) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_row_index(by_id(ctx, "p")) == -1 && tbox_context_table_cell_index(r1) == -1);

    uint64_t revision = tbox_html_document_revision(tbox_context_document(ctx));
    TBOX_TEST_ASSERT(tbox_context_table_set_cell_text(ctx, t, 2, 1, sv("new")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 2, 1), "new"));
    TBOX_TEST_ASSERT(tbox_html_document_revision(tbox_context_document(ctx)) > revision);
    TBOX_TEST_ASSERT(!tbox_context_table_set_cell_text(ctx, t, 2, 5, sv("no")));

    /* Insert before a row lands in its section; past the end, in the last tbody. */
    tbox_string_view cells[] = {sv("Caio"), sv("")};
    tbox_html_node *before_r2 = tbox_context_table_insert_row(ctx, t, 3, cells, 2);
    TBOX_TEST_REQUIRE(before_r2 != NULL);
    TBOX_TEST_ASSERT(before_r2->parent == by_id(ctx, "b2") && tbox_context_table_row_at(t, 3) == before_r2);
    TBOX_TEST_ASSERT(tbox_context_table_cell_count(before_r2) == 2 && text_eq(tbox_context_table_cell_text(ctx, t, 3, 0), "Caio"));
    TBOX_TEST_ASSERT(tbox_context_table_cell_at(before_r2, 1)->first_child == NULL);
    tbox_html_node *last = tbox_context_table_insert_row(ctx, t, SIZE_MAX, NULL, 0);
    TBOX_TEST_ASSERT(last != NULL && last->parent == by_id(ctx, "b2") && tbox_context_table_row_index(last) == 5);
    TBOX_TEST_ASSERT(tbox_context_table_row_count(t) == 7 && tbox_context_table_row_index(by_id(ctx, "f")) == 6);

    /* Cells inside a row. */
    tbox_html_node *first = tbox_context_table_insert_cell(ctx, last, 0, sv("b"));
    tbox_html_node *front = tbox_context_table_insert_cell(ctx, last, 0, sv("a"));
    TBOX_TEST_ASSERT(first != NULL && front != NULL && tbox_context_table_cell_at(last, 0) == front);
    TBOX_TEST_ASSERT(tbox_context_table_insert_cell(ctx, last, SIZE_MAX, sv("c")) == tbox_context_table_cell_at(last, 2));
    TBOX_TEST_ASSERT(tbox_context_table_remove_cell(ctx, last, 1) && tbox_context_table_cell_count(last) == 2);
    TBOX_TEST_ASSERT(!tbox_context_table_remove_cell(ctx, last, 2));
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 5, 1), "c"));

    TBOX_TEST_ASSERT(tbox_context_table_remove_row(ctx, t, 0) && tbox_context_table_head(t)->first_child == NULL);
    TBOX_TEST_ASSERT(!tbox_context_table_remove_row(ctx, t, 6));

    /* clear_body keeps head, foot and the tbody elements. */
    tbox_context_table_clear_body(ctx, t);
    TBOX_TEST_ASSERT(tbox_context_table_row_count(t) == 1 && tbox_context_table_row_at(t, 0) == by_id(ctx, "f"));
    TBOX_TEST_ASSERT(by_id(ctx, "b1") != NULL && by_id(ctx, "b2") != NULL && tbox_context_table_head(t) != NULL);
    tbox_html_node *again = tbox_context_table_insert_row(ctx, t, SIZE_MAX, cells, 1);
    TBOX_TEST_ASSERT(again != NULL && again->parent == by_id(ctx, "b2") && tbox_context_table_row_index(again) == 0);

    /* A table without tbody takes appended rows directly (inner left the
     * document with its row, which still works on the detached tree). */
    tbox_html_node *direct = tbox_context_table_insert_row(ctx, inner, SIZE_MAX, cells, 1);
    TBOX_TEST_ASSERT(direct != NULL && direct->parent == inner && tbox_context_table_row_index(direct) == 1);

    TBOX_TEST_ASSERT(tbox_context_table_insert_row(NULL, t, 0, cells, 1) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_insert_row(ctx, by_id(ctx, "p"), 0, cells, 1) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_row_count(NULL) == 0 && tbox_context_table_cell_at(NULL, 0) == NULL);
    TBOX_TEST_ASSERT(tbox_context_table_row_of(NULL) == NULL && tbox_context_table_row_index(NULL) == -1);

    tbox_display_list list;
    tbox_context_run_frame(ctx, 300.0, 200.0, &list);
    tbox_context_close(ctx);
    return failures;
}

static int tbox_test_generic_items(tbox_font_face_cache *fonts) {
    int failures      = 0;
    tbox_context *ctx = open_html("<body><ul id='list'><li id='keep'>k</li></ul>"
        "<template id='item'><li class='item'><span class='name'>?</span><button class='more'>...</button></li></template>"
        "<template id='pair'><p class='a'>a</p> <p class='b'>b</p></template>"
        "<template id='row'><tr class='file'><td><span class='name'></span></td><td>size</td></tr></template>"
        "<table id='t'><thead><tr><th>Name</th><th>Size</th></tr></thead><tbody id='tb'></tbody></table>"
        "<div id='box'><button id='inner'>in</button><p>text</p></div><button id='out'>out</button>"
        "<select id='s'><option>x</option></select><p id='plain'>p</p></body>", fonts);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 400.0, 300.0, &list);

    tbox_html_node *ul = by_id(ctx, "list"), *item = by_id(ctx, "item");
    /* Instantiate: deep copies, inserted before an element index, the
     * template itself untouched. */
    tbox_html_node *last = tbox_context_control_instantiate(ctx, item, ul, SIZE_MAX);
    TBOX_TEST_REQUIRE(last != NULL);
    TBOX_TEST_ASSERT(last->parent == ul && by_id(ctx, "keep")->next_sibling == last);
    TBOX_TEST_ASSERT(tbox_context_control_has_class(last, sv("item")));
    tbox_html_node *first = tbox_context_control_instantiate(ctx, item, ul, 0);
    TBOX_TEST_ASSERT(first != NULL && ul->first_child == first && first != last);
    TBOX_TEST_ASSERT(tbox_html_node_element_child_count(ul) == 3);
    TBOX_TEST_ASSERT(tbox_html_node_element_child_count(item) == 1);

    /* Lookup inside the new item: descendants only. */
    tbox_html_node *name = tbox_context_query_in(ctx, last, ".name", 5);
    TBOX_TEST_REQUIRE(name != NULL);
    TBOX_TEST_ASSERT(name->parent == last && name != tbox_context_query_in(ctx, first, ".name", 5));
    TBOX_TEST_ASSERT(tbox_context_control_set_label(ctx, name, sv("report.txt")));
    TBOX_TEST_ASSERT(text_eq(tbox_context_control_label(ctx, tbox_context_query_in(ctx, item, ".name", 5)), "?"));
    TBOX_TEST_ASSERT(tbox_context_query_in(ctx, last, ".item", 5) == NULL); /* root excluded */
    TBOX_TEST_ASSERT(tbox_context_query_in(ctx, last, "[", 1) == NULL);
    TBOX_TEST_ASSERT(tbox_context_query_in(ctx, NULL, "li", 2) == NULL);
    TBOX_TEST_ASSERT(tbox_context_query_in(NULL, last, "span", 4) == NULL);

    /* Every element child of the template is copied; the first returns. */
    tbox_html_node *box = by_id(ctx, "box");
    tbox_html_node *a   = tbox_context_control_instantiate(ctx, by_id(ctx, "pair"), box, 1);
    TBOX_TEST_ASSERT(a != NULL && tbox_context_control_has_class(a, sv("a")));
    TBOX_TEST_ASSERT(tbox_context_control_has_class(tbox_html_node_next_element_sibling(a), sv("b")));
    TBOX_TEST_ASSERT(tbox_html_node_first_element_child(box) == by_id(ctx, "inner"));

    /* Table rows from a template are rows for the tbox_table_* functions. */
    tbox_html_node *t   = by_id(ctx, "t");
    tbox_html_node *row = tbox_context_control_instantiate(ctx, by_id(ctx, "row"), by_id(ctx, "tb"), SIZE_MAX);
    TBOX_TEST_ASSERT(row != NULL && tbox_context_table_row_count(t) == 2 && tbox_context_table_row_index(row) == 1);
    TBOX_TEST_ASSERT(tbox_context_table_row_of(tbox_context_query_in(ctx, row, ".name", 5)) == row);
    TBOX_TEST_ASSERT(text_eq(tbox_context_table_cell_text(ctx, t, 1, 1), "size"));

    /* Refused: not a template, parent inside the template, option lists. */
    TBOX_TEST_ASSERT(tbox_context_control_instantiate(ctx, ul, box, 0) == NULL);
    TBOX_TEST_ASSERT(tbox_context_control_instantiate(ctx, item, tbox_html_node_first_element_child(item), 0) == NULL);
    TBOX_TEST_ASSERT(tbox_context_control_instantiate(ctx, item, by_id(ctx, "s"), 0) == NULL);
    TBOX_TEST_ASSERT(tbox_context_control_instantiate(NULL, item, ul, 0) == NULL);

    /* Classes. */
    tbox_html_node *plain = by_id(ctx, "plain");
    TBOX_TEST_ASSERT(tbox_context_control_set_class(ctx, plain, sv("active"), true));
    TBOX_TEST_ASSERT(tbox_context_control_set_class(ctx, plain, sv("active"), true));
    TBOX_TEST_ASSERT(tbox_context_control_has_class(plain, sv("active")));
    TBOX_TEST_ASSERT(tbox_context_control_set_class(ctx, plain, sv("active"), false));
    TBOX_TEST_ASSERT(!tbox_context_control_has_class(plain, sv("active")));
    TBOX_TEST_ASSERT(tbox_context_control_set_class(ctx, plain, sv("absent"), false));
    TBOX_TEST_ASSERT(!tbox_context_control_set_class(ctx, plain, sv("two words"), true));
    TBOX_TEST_ASSERT(!tbox_context_control_set_class(ctx, plain, sv(""), true));
    TBOX_TEST_ASSERT(!tbox_context_control_set_class(NULL, plain, sv("x"), true));

    /* Hidden: [hidden] takes the element out of layout, and focus leaves
     * a hidden element or one inside it. */
    tbox_html_node *out = by_id(ctx, "out"), *inner = by_id(ctx, "inner");
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, out));
    tbox_context_control_set_hidden(ctx, out, true);
    TBOX_TEST_ASSERT(tbox_context_control_hidden(out) && tbox_context_focused_node(ctx) == NULL);
    tbox_context_run_frame(ctx, 400.0, 300.0, &list);
    TBOX_TEST_ASSERT(!tbox_context_control_focus(ctx, out));
    tbox_context_control_set_hidden(ctx, out, false);
    TBOX_TEST_ASSERT(!tbox_context_control_hidden(out));
    tbox_context_run_frame(ctx, 400.0, 300.0, &list);
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, out));
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, inner));
    tbox_context_control_set_hidden(ctx, box, true);
    TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == NULL);
    tbox_context_control_set_hidden(ctx, box, false);
    tbox_context_run_frame(ctx, 400.0, 300.0, &list);

    /* Remove and clear, with focus leaving what goes away. */
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, tbox_context_query_in(ctx, last, ".more", 5)));
    TBOX_TEST_ASSERT(tbox_context_control_remove(ctx, last));
    TBOX_TEST_ASSERT(last->parent == NULL && tbox_context_focused_node(ctx) == NULL);
    TBOX_TEST_ASSERT(!tbox_context_control_remove(ctx, last)); /* no parent any more */
    TBOX_TEST_ASSERT(tbox_html_node_element_child_count(ul) == 2);
    TBOX_TEST_ASSERT(tbox_context_control_focus(ctx, inner));
    TBOX_TEST_ASSERT(tbox_context_control_clear(ctx, box));
    TBOX_TEST_ASSERT(box->first_child == NULL && tbox_context_focused_node(ctx) == NULL);
    TBOX_TEST_ASSERT(!tbox_context_control_remove(ctx, tbox_html_node_first_element_child(by_id(ctx, "s"))));
    TBOX_TEST_ASSERT(!tbox_context_control_clear(ctx, by_id(ctx, "s")));
    tbox_html_node *html = tbox_html_node_first_element_child(tbox_html_document_root(tbox_context_document(ctx)));
    TBOX_TEST_ASSERT(html != NULL && !tbox_context_control_remove(ctx, html));
    TBOX_TEST_ASSERT(!tbox_context_control_remove(NULL, ul));

    tbox_context_run_frame(ctx, 400.0, 300.0, &list);
    tbox_context_close(ctx);

    /* The UA [hidden] rule takes the box out of layout: the block below
     * moves up to the point. Author `display` wins over it, as in browsers. */
    const char *css = "body { margin: 0; } div { height: 50px; } #shown { display: block; }";
    const char *markup = "<body><div id='top'></div><div id='below'></div><div id='shown' hidden></div></body>";
    ctx                = tbox_context_open(markup, strlen(markup), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *top = by_id(ctx, "top");
    tbox_context_control_set_hidden(ctx, top, true);
    tbox_context_run_frame(ctx, 200.0, 200.0, &list);
    const tbox_layout_box *hit = tbox_context_hit_test(ctx, 5.0, 5.0);
    TBOX_TEST_ASSERT(hit != NULL && hit->node == by_id(ctx, "below"));
    hit = tbox_context_hit_test(ctx, 5.0, 55.0);
    TBOX_TEST_ASSERT(hit != NULL && hit->node == by_id(ctx, "shown"));
    tbox_context_control_set_hidden(ctx, top, false);
    tbox_context_run_frame(ctx, 200.0, 200.0, &list);
    hit = tbox_context_hit_test(ctx, 5.0, 5.0);
    TBOX_TEST_ASSERT(hit != NULL && hit->node == top);
    tbox_context_close(ctx);
    return failures;
}

/* What the virtual table's handler saw. `fill` writes "r<index>" into
 * each row's cell at once; otherwise the rows are left for later. */
typedef struct tbox_test_virtual {
    tbox_html_document *document;
    bool fill;
    size_t calls, first, count;
    tbox_html_node *seen[64];
    size_t seen_count;
    int destroyed;
} tbox_test_virtual;

static void tbox_test_virtual_fill(tbox_html_document *document, tbox_html_node *row, size_t index) {
    char text[32];
    int length = snprintf(text, sizeof(text), "r%zu", index);
    tbox_html_node_set_text_content(document, tbox_html_node_first_element_child(row), tbox_string_view_make(text, (size_t)length));
}

static void tbox_test_virtual_rows(tbox_context *ctx, tbox_html_node *table, size_t first, tbox_html_node *const *rows, size_t count, void *userdata) {
    (void)ctx;
    (void)table;
    tbox_test_virtual *seen = userdata;
    seen->calls++;
    seen->first = first;
    seen->count = count;
    for (size_t i = 0; i < count; i++) {
        bool known = false;
        for (size_t k = 0; k < seen->seen_count && !known; k++)
            known = seen->seen[k] == rows[i];
        if (!known && seen->seen_count < 64)
            seen->seen[seen->seen_count++] = rows[i];
        if (seen->fill)
            tbox_test_virtual_fill(seen->document, rows[i], first + i);
    }
}

static void tbox_test_virtual_destroy(void *userdata) {
    ((tbox_test_virtual *)userdata)->destroyed++;
}

/* Whether the body holds rows [first, end), each <tr> filled with its index
 * and among the ones created so far (recycled, never new while scrolling). */
static bool tbox_test_virtual_window(tbox_context *ctx, tbox_html_node *table, const tbox_test_virtual *seen, size_t first, size_t end) {
    for (size_t i = first; i < end; i++) {
        tbox_html_node *row = tbox_context_table_virtual_row(ctx, table, i);
        if (row == NULL || tbox_context_table_virtual_index(ctx, row) != (ptrdiff_t)i)
            return false;
        const tbox_html_node *cell = tbox_html_node_first_element_child(row);
        char text[32];
        int length = snprintf(text, sizeof(text), "r%zu", i);
        if (cell == NULL || cell->first_child == NULL || !tbox_string_view_equal(cell->first_child->text.text, tbox_string_view_make(text, (size_t)length)))
            return false;
        bool known = false;
        for (size_t k = 0; k < seen->seen_count && !known; k++)
            known = seen->seen[k] == row;
        if (!known)
            return false;
    }
    return (first == 0 || tbox_context_table_virtual_row(ctx, table, first - 1) == NULL) && tbox_context_table_virtual_row(ctx, table, end) == NULL;
}

static tbox_string_view tbox_test_spacer_style(const tbox_html_node *section) {
    const tbox_html_attribute *style = tbox_html_node_get_attribute(tbox_html_node_first_element_child(section), tbox_string_view_make("style", 5));
    return style != NULL ? style->value : tbox_string_view_make(NULL, 0);
}

/* A virtual table of 10 000 rows of 20px in a 200px scroll container: the
 * window holds what is in view plus half of it on each side, starts even,
 * recycles its rows while scrolling, and takes rows filled later,
 * refreshed rows, a new total and going back to an ordinary table. */
static int tbox_test_virtual_table(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *css    = "body { margin: 0; } #s { height: 200px; overflow-y: auto; } table { width: 300px; border-collapse: collapse; table-layout: fixed; }"
                         "tr { height: 20px; } td { padding: 0; line-height: 16px; }";
    const char *markup = "<body><div id='s'><table id='t'><thead><tr><th>h</th></tr></thead><tbody><tr><td>old</td></tr></tbody></table></div>"
                         "<template id='row'><tr><td>-</td></tr></template></body>";
    tbox_context *ctx  = tbox_context_open(markup, strlen(markup), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *table = by_id(ctx, "t");
    tbox_test_virtual seen = { .document = tbox_context_document(ctx), .fill = true };
    TBOX_TEST_ASSERT(!tbox_context_table_virtualize(ctx, table, by_id(ctx, "s"), 10, tbox_test_virtual_rows, &seen, NULL));
    TBOX_TEST_REQUIRE(tbox_context_table_virtualize(ctx, table, by_id(ctx, "row"), 10000, tbox_test_virtual_rows, &seen, tbox_test_virtual_destroy));
    TBOX_TEST_ASSERT(!tbox_context_table_virtualize(ctx, table, by_id(ctx, "row"), 10, tbox_test_virtual_rows, &seen, NULL));

    /* The first frame asks for a guess; the second fits the scrollport:
     * below the 20px head, rows reach 180px of the 200px in view, plus
     * 100px of margin: rows [0, 14). */
    tbox_display_list list;
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(seen.calls == 1 && seen.first == 0 && seen.count == 32);
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(tbox_test_virtual_window(ctx, table, &seen, 0, 14));
    TBOX_TEST_ASSERT(tbox_context_table_row_count(table) == 1 + 1 + 14 + 1); /* head, spacers, rows */
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_test_spacer_style(tbox_context_table_head(table)->next_sibling), "height: 0.000px"));

    /* Scrolled by 4000px: rows [194, 214) (the margin starts at 194),
     * every <tr> one of those made before. */
    size_t made = seen.seen_count;
    TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 50.0, 50.0, 4000.0));
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(tbox_test_virtual_window(ctx, table, &seen, 194, 214) && seen.seen_count == made);
    const tbox_html_node *top = tbox_context_table_head(table)->next_sibling;
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_test_spacer_style(top), "height: 3880.000px"));
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_test_spacer_style(top->next_sibling->next_sibling), "height: 195720.000px"));
    TBOX_TEST_ASSERT(tbox_context_table_virtual_row(ctx, table, 10) == NULL && tbox_context_table_virtual_index(ctx, by_id(ctx, "s")) == -1);

    /* Filled later: the handler leaves the rows, the application fills
     * the ones still in view. */
    seen.fill = false;
    TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 50.0, 50.0, 100.0));
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    size_t late_first = seen.first, late_count = seen.count;
    TBOX_TEST_ASSERT(late_first == 214 && late_count == 5);
    for (size_t i = late_first; i < late_first + late_count; i++)
        tbox_test_virtual_fill(seen.document, tbox_context_table_virtual_row(ctx, table, i), i);
    TBOX_TEST_ASSERT(tbox_test_virtual_window(ctx, table, &seen, 198, 219));

    /* Refreshed rows in view are asked for again; nothing else is. */
    size_t calls = seen.calls;
    TBOX_TEST_ASSERT(tbox_context_table_refresh_rows(ctx, table, 210, 3));
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(seen.calls == calls + 1 && seen.first == 210 && seen.count == 3);
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(seen.calls == calls + 1);

    /* A smaller total ends the window and the bottom spacer there. */
    seen.fill = true;
    TBOX_TEST_ASSERT(tbox_context_table_set_row_total(ctx, table, 212));
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(tbox_context_table_virtual_row(ctx, table, 211) != NULL && tbox_context_table_virtual_row(ctx, table, 212) == NULL);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(tbox_test_spacer_style(tbox_context_table_head(table)->next_sibling->next_sibling->next_sibling), "height: 0.000px"));

    /* Back to an ordinary table: the rows in view stay, the spacers go,
     * userdata is released once. */
    size_t rows_in_view = tbox_context_table_row_count(table) - 3;
    TBOX_TEST_ASSERT(tbox_context_table_unvirtualize(ctx, table) && !tbox_context_table_unvirtualize(ctx, table));
    tbox_context_run_frame(ctx, 400.0, 400.0, &list);
    TBOX_TEST_ASSERT(seen.destroyed == 1 && tbox_context_table_row_count(table) == rows_in_view + 1);
    TBOX_TEST_ASSERT(tbox_context_table_virtual_row(ctx, table, 211) == NULL);
    tbox_context_close(ctx);
    TBOX_TEST_ASSERT(seen.destroyed == 1);
    return failures;
}

static void tbox_test_virtual_words(tbox_context *ctx, tbox_html_node *table, size_t first, tbox_html_node *const *rows, size_t count, void *userdata) {
    (void)ctx;
    (void)table;
    (void)first;
    for (size_t i = 0; i < count; i++)
        tbox_html_node_set_text_content(userdata, tbox_html_node_first_element_child(rows[i]), sv("several words that wrap when narrow"));
}

/* A narrower window wraps every row onto more lines: the rows get taller,
 * and the row at the top of the scrollport stays the one there. */
static int tbox_test_virtual_table_anchor(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *css    = "body { margin: 0; } #s { height: 200px; overflow-y: auto; } table { width: 100%; border-collapse: collapse; table-layout: fixed; }"
                         "td { padding: 0; line-height: 16px; }";
    const char *markup = "<body><div id='s'><table id='t'></table></div><template id='row'><tr><td>-</td></tr></template></body>";
    tbox_context *ctx  = tbox_context_open(markup, strlen(markup), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_html_node *table = by_id(ctx, "t");
    TBOX_TEST_REQUIRE(tbox_context_table_virtualize(ctx, table, by_id(ctx, "row"), 5000, tbox_test_virtual_words, tbox_context_document(ctx), NULL));
    tbox_display_list list;
    for (int i = 0; i < 3; i++)
        tbox_context_run_frame(ctx, 600.0, 400.0, &list);
    TBOX_TEST_ASSERT(tbox_context_scroll(ctx, 50.0, 50.0, 3000.0));
    for (int i = 0; i < 3; i++)
        tbox_context_run_frame(ctx, 600.0, 400.0, &list);
    const tbox_layout_box *hit = tbox_context_hit_test(ctx, 5.0, 2.0);
    ptrdiff_t before           = hit != NULL ? tbox_context_table_virtual_index(ctx, tbox_context_table_row_of(hit->node)) : -1;
    TBOX_TEST_ASSERT(before > 100);
    for (int i = 0; i < 4; i++)
        tbox_context_run_frame(ctx, 120.0, 400.0, &list);
    hit             = tbox_context_hit_test(ctx, 5.0, 2.0);
    ptrdiff_t after = hit != NULL ? tbox_context_table_virtual_index(ctx, tbox_context_table_row_of(hit->node)) : -1;
    TBOX_TEST_ASSERT_MSG(after == before, "the row at the top must stay there when rows get taller");
    tbox_context_close(ctx);
    return failures;
}

int tbox_test_controls_api_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    failures += tbox_test_context_lookup(fonts);
    failures += tbox_test_control_values(fonts);
    failures += tbox_test_control_checked(fonts);
    failures += tbox_test_select_options(fonts);
    failures += tbox_test_table_api(fonts);
    failures += tbox_test_generic_items(fonts);
    failures += tbox_test_virtual_table(fonts);
    failures += tbox_test_virtual_table_anchor(fonts);
    return failures;
}
