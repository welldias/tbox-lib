#include "tbox_layout_internal.h"
#include <string.h>

bool tbox_layout_is_hidden_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("hidden", 6));
}



bool tbox_layout_is_password_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8));
}

static bool tbox_layout_is_checked_checkbox(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL &&
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("checkbox", 8)) &&
           tbox_html_node_get_attribute(node, tbox_string_view_make("checked", 7)) != NULL;
}

void tbox_layout_build_checkbox_checkmark(tbox_arena *arena, const tbox_html_node *node,
                                                  const tbox_style *style, tbox_font_face_cache *fonts,
                                                  tbox_layout_box *box) {
    if (!tbox_layout_is_checked_checkbox(node) || fonts == NULL) return;

    tbox_string_view entity = tbox_string_view_make("&checkmark;", 11);
    tbox_string_view checkmark = tbox_html_decode_entities(arena, entity);
    if (checkmark.size != 3) return;

    double size = box->content_box.height;
    if (size > 14.0) size = 14.0;
    if (size <= 0.0) return;
    const tbox_font_face *face = tbox_font_face_cache_get(fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, size);
    if (!tbox_font_face_has_glyph(face, 0x2713))
        face = tbox_font_face_cache_get(fonts, tbox_string_view_make("DejaVu Sans", 11),
                                        false, false, size);
    if (!tbox_font_face_has_glyph(face, 0x2713)) return;

    double width = tbox_font_measure_text(face, checkmark);
    double line_height = tbox_font_face_line_height(face);
    tbox_layout_text_run *run = tbox_arena_alloc_zero(arena, sizeof(*run));
    run->rect = (tbox_rect){box->content_box.x + (box->content_box.width - width) / 2.0,
                            box->content_box.y + (box->content_box.height - line_height) / 2.0,
                            width, line_height};
    run->text = checkmark;
    run->font = face;
    run->style = style;
    box->text_runs = run;
    box->text_run_count = 1;
}

bool tbox_layout_input_is_text_tag(const tbox_html_node *node) {
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type == NULL ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("text", 4)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("email", 5)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("number", 6)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("password", 8)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("search", 6)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("tel", 3)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("url", 3)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("button", 6)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("submit", 6)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("date", 4)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("time", 4)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("week", 4)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("month", 5)) ||
           tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("datetime-local", 14));
}

void tbox_layout_input_push_value_word(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words) {
    const tbox_html_attribute *value = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    tbox_string_view label = value != NULL ? value->value :
        type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("reset", 5)) ?
            tbox_string_view_make("Reset", 5) :
        type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("submit", 6)) ?
            tbox_string_view_make("Submit", 6) : tbox_string_view_make(NULL, 0);
    const tbox_font_face *face = tbox_layout_style_face(fonts, style);
    if (label.size > 0 && face != NULL) {
        tbox_string_view display = label;
        if (tbox_layout_is_password_input(node)) {
            bool bullet = tbox_font_face_has_glyph(face, 0x2022);
            size_t count = 0;
            for (size_t i = 0; i < label.size; i++)
                if (((unsigned char)label.data[i] & 0xc0) != 0x80) count++;
            size_t unit = bullet ? 3 : 1;
            display = tbox_string_view_make(NULL, 0);
            char *masked = count <= SIZE_MAX / unit ? tbox_arena_alloc(arena, count * unit) : NULL;
            if (masked != NULL) {
                for (size_t i = 0; i < count; i++) {
                    if (bullet) memcpy(masked + i * unit, "\xe2\x80\xa2", 3);
                    else masked[i] = '*';
                }
                display = tbox_string_view_make(masked, count * unit);
            }
        }
        tbox_layout_word *word = tbox_layout_new_word(words);
        word->text = display;
        word->face = face;
        word->style = style;
        word->width = tbox_font_measure_text_spaced(face, display, style->letter_spacing);
        word->space_width = 0.0;
        word->image = NULL;
        word->image_height = 0.0;
        word->hard_break = false;
        word->no_space_before = false;
    }
}
