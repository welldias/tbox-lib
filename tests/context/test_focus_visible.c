#include <string.h>
#include <tbox/tbox.h>

#include "base/tbox_string.h"
#include "context/tbox_context_internal.h"
#include "test_support.h"

static const tbox_html_node *tbox_focus_find(tbox_context *ctx, const char *selector) {
    tbox_css_selector_node_set set = tbox_css_selector_select(tbox_html_document_root(ctx->document), selector, strlen(selector));
    const tbox_html_node *node = set.count > 0 ? set.items[0] : NULL;
    tbox_css_selector_node_set_destroy(&set);
    return node;
}

int tbox_test_focus_visible_run(tbox_font_face_cache *fonts) {
    int failures = 0;
    const char *html = "<body><button id='button'>Button</button><input id='input'><textarea id='area'></textarea></body>";
    const char *css = "body{margin:0}button,input,textarea{display:block;width:100px;height:30px;margin:0}"
        "button:focus-visible{color:rgb(12,34,56)}";
    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    tbox_context *other = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    TBOX_TEST_REQUIRE(ctx != NULL && other != NULL);
    tbox_display_list list;
    tbox_context_run_frame(ctx, 300, 200, &list);
    const tbox_html_node *button = tbox_focus_find(ctx, "#button");
    TBOX_TEST_REQUIRE(button != NULL);
    TBOX_TEST_ASSERT(tbox_css_supports(tbox_string_view_from_cstr("selector(button:focus-visible)")));
    tbox_context_dispatch_click(ctx, 10, 10);
    tbox_context_run_frame(ctx, 300, 200, &list);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus") == button);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus-visible") == NULL);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "body:focus-within") != NULL);
    tbox_key_event key = { .key = TBOX_KEY_LEFT, .pressed = true, .control = true };
    tbox_context_dispatch_key(ctx, &key);
    TBOX_TEST_ASSERT(!button->element.focus_visible);
    key.control = false;
    key.pressed = false;
    tbox_context_dispatch_key(ctx, &key);
    TBOX_TEST_ASSERT(!button->element.focus_visible);
    key.pressed = true;
    TBOX_TEST_ASSERT(tbox_context_dispatch_key(ctx, &key));
    tbox_context_run_frame(ctx, 300, 200, &list);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus-visible") == button);
    const tbox_style *style = tbox_style_table_find(&ctx->styles, button);
    TBOX_TEST_REQUIRE(style != NULL);
    TBOX_TEST_ASSERT(style->color.r == 12 && style->color.g == 34 && style->color.b == 56);
    tbox_context_update_hover(ctx, true, 10, 10);
    TBOX_TEST_ASSERT(button->element.focus_visible);
    TBOX_TEST_ASSERT(tbox_context_set_active_at(ctx, true, 10, 10));
    TBOX_TEST_ASSERT(!button->element.focus_visible);
    tbox_context_set_active_at(ctx, false, 10, 10);
    key.key = TBOX_KEY_TAB;
    tbox_context_dispatch_key(ctx, &key);
    TBOX_TEST_ASSERT(!button->element.focus_visible);
    tbox_context_run_frame(ctx, 300, 200, &list);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "input:focus-visible") != NULL);
    tbox_context_dispatch_click(ctx, 10, 50);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "input:focus-visible") != NULL);
    tbox_context_dispatch_click(ctx, 10, 90);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "textarea:focus-visible") != NULL);
    key.shift = true;
    tbox_context_dispatch_key(ctx, &key);
    tbox_context_dispatch_key(ctx, &key);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus-visible") == button);
    tbox_context_run_frame(other, 300, 200, &list);
    tbox_context_dispatch_click(other, 10, 10);
    tbox_context_run_frame(other, 300, 200, &list);
    TBOX_TEST_ASSERT(tbox_focus_find(other, "button:focus-visible") == NULL);
    tbox_context_run_frame(ctx, 300, 200, &list);
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus-visible") == button);
    TBOX_TEST_ASSERT(tbox_context_dispatch_click(ctx, 10, 10));
    TBOX_TEST_ASSERT(tbox_focus_find(ctx, "button:focus-visible") == NULL);
    tbox_context_close(other);
    tbox_context_close(ctx);
    return failures;
}
