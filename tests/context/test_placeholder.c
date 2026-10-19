#include <math.h>
#include <string.h>
#include <tbox/tbox.h>

#include "base/tbox_string.h"
#include "context/tbox_context_internal.h"
#include "test_support.h"

static tbox_html_node *tbox_placeholder_node(tbox_context *ctx, const char *selector) {
    tbox_css_selector_node_set set = tbox_css_selector_select(tbox_html_document_root(ctx->document), selector, strlen(selector));
    tbox_html_node *node = set.count == 1 ? (tbox_html_node *)set.items[0] : NULL;
    tbox_css_selector_node_set_destroy(&set);
    return node;
}

static const tbox_layout_box *tbox_placeholder_box(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node) return box;
        const tbox_layout_box *found = tbox_placeholder_box(box->first_child, node);
        if (found != NULL) return found;
    }
    return NULL;
}

int tbox_test_placeholder_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *html = "<body><input id='hint' placeholder='alpha&#10;Beta'>"
        "<input id='plain'><input id='password' type='password' placeholder='Password'>"
        "<input id='date' type='date' placeholder='Ignored'>"
        "<textarea id='area' placeholder='First line&#10;Second line'></textarea></body>";
    const char *css = "body{margin:0}input,textarea{display:block;width:180px;font:12px/18px sans-serif}"
        "#hint::placeholder{color:rgb(20 40 60 / .8);opacity:.5;font-size:24px}"
        "textarea::placeholder{color:purple;opacity:.5}";
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 800, 600, &list);
    tbox_html_node *hint = tbox_placeholder_node(ctx, "#hint");
    tbox_html_node *plain = tbox_placeholder_node(ctx, "#plain");
    tbox_html_node *password = tbox_placeholder_node(ctx, "#password");
    tbox_html_node *date = tbox_placeholder_node(ctx, "#date");
    tbox_html_node *area = tbox_placeholder_node(ctx, "#area");
    const tbox_layout_box *box = tbox_placeholder_box(ctx->root, hint);
    const tbox_layout_box *plain_box = tbox_placeholder_box(ctx->root, plain);
    const tbox_layout_box *password_box = tbox_placeholder_box(ctx->root, password);
    const tbox_layout_box *area_box = tbox_placeholder_box(ctx->root, area);
    TBOX_TEST_REQUIRE(hint != NULL && plain != NULL && password != NULL && date != NULL && area != NULL);
    TBOX_TEST_REQUIRE(box != NULL && plain_box != NULL && password_box != NULL && area_box != NULL);
    TBOX_TEST_REQUIRE(box->text_run_count == 1 && password_box->text_run_count == 1 && area_box->text_run_count == 2);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(box->text_runs[0].text, "alphaBeta"));
    TBOX_TEST_ASSERT(box->text_runs[0].style->font_size == 24.0);
    TBOX_TEST_ASSERT(box->text_runs[0].style->color.r == 20 && box->text_runs[0].style->color.a == 102);
    TBOX_TEST_ASSERT(fabs(box->border_box.height - plain_box->border_box.height) < 1e-6);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(password_box->text_runs[0].text, "Password"));
    TBOX_TEST_ASSERT(password_box->text_runs[0].style->color.r == 117);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(area_box->text_runs[0].text, "First line"));
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(area_box->text_runs[1].text, "Second line"));
    TBOX_TEST_ASSERT(area_box->text_runs[0].style->color.a == 128);
    TBOX_TEST_ASSERT(tbox_style_table_find(&ctx->styles, date)->placeholder_style == NULL);
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("selector(input::placeholder)")));
    bool painted = false;
    for (size_t i = 0; i < list.count; i++)
        if (list.items[i].kind == TBOX_PAINT_TEXT_RUN && tbox_string_view_equal_cstr(list.items[i].text, "alphaBeta"))
            painted = list.items[i].color.a == 102;
    TBOX_TEST_ASSERT(painted);

    tbox_key_event tab = { .key = TBOX_KEY_TAB, .pressed = true };
    tbox_context_dispatch_key(ctx, &tab);
    TBOX_TEST_ASSERT(tbox_context_focused_node(ctx) == hint);
    tbox_context_run_frame(ctx, 800, 600, &list);
    tbox_text_field *field = tbox_context_text_field(ctx, hint);
    TBOX_TEST_REQUIRE(field != NULL);
    TBOX_TEST_ASSERT(field->length == 0 && field->cursor == 0);
    TBOX_TEST_ASSERT(tbox_context_selected_text(ctx).size == 0);
    tbox_context_dispatch_text(ctx, tbox_string_view_from_cstr("typed"));
    tbox_context_run_frame(ctx, 800, 600, &list);
    box = tbox_placeholder_box(ctx->root, hint);
    TBOX_TEST_REQUIRE(box != NULL && box->text_run_count == 1);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(box->text_runs[0].text, "typed"));
    TBOX_TEST_ASSERT(box->text_runs[0].style->font_size == 12.0);
    const tbox_html_attribute *value = tbox_html_node_get_attribute(hint, tbox_string_view_from_cstr("value"));
    TBOX_TEST_ASSERT(value != NULL && tbox_string_view_equal_cstr(value->value, "typed"));
    tbox_html_node_set_attribute(ctx->document, hint, tbox_string_view_from_cstr("value"), tbox_string_view_from_cstr(""));
    tbox_html_node_set_attribute(ctx->document, hint, tbox_string_view_from_cstr("placeholder"), tbox_string_view_from_cstr("Changed"));
    tbox_context_run_frame(ctx, 800, 600, &list);
    box = tbox_placeholder_box(ctx->root, hint);
    TBOX_TEST_REQUIRE(box != NULL && box->text_run_count == 1);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(box->text_runs[0].text, "Changed"));
    tbox_context_set_focus(ctx, area);
    field = tbox_context_text_field(ctx, area);
    TBOX_TEST_REQUIRE(field != NULL);
    TBOX_TEST_ASSERT(field->length == 0 && field->line_count == 1 && field->scroll_y == 0.0);
    TBOX_TEST_ASSERT(field->lines[0].start == 0 && field->lines[0].end == 0);
    tbox_context_dispatch_text(ctx, tbox_string_view_from_cstr("x"));
    tbox_context_run_frame(ctx, 800, 600, &list);
    area_box = tbox_placeholder_box(ctx->root, area);
    TBOX_TEST_REQUIRE(area_box != NULL && area_box->text_run_count == 1);
    TBOX_TEST_ASSERT(tbox_string_view_equal_cstr(area_box->text_runs[0].text, "x"));
    tbox_context_close(ctx);
    return failures;
}
