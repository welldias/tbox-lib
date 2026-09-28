#include "tbox_layout_internal.h"
#include <string.h>



/* Pushes one tbox_layout_word for an <img> element (see
 * tbox_layout_collect_words below) -- carries decoded image data instead of
 * text, sized from CSS/HTML-attribute width/height (`img_style`, the img's
 * OWN resolved tbox_style -- already includes the `<img width/height>`
 * attribute fallback, see tbox_style_resolve_img_dimension_attribute in
 * src/style/tbox_style.c) falling back to the image's intrinsic pixel
 * dimensions on any axis left AUTO or PERCENT (percentage `width` keeps its
 * existing intrinsic-size fallback in this inline path). Its measured width
 * is then constrained by min/max-width against `containing_width`. A missing
 * `src`, or a `src` tbox_image_cache_get can't
 * decode (bad path, corrupt/unsupported file), falls back to the `alt`
 * attribute's text -- pushed as an ORDINARY text word (tbox_layout_push_words,
 * in `img_style`'s own face/color) rather than an image word, same fallback
 * every real browser shows in place of a broken image (see
 * tests/assets/024.html's `notfound.png`, alt="Image not found" -- the
 * fixture this behavior was added to match). No broken-image ICON is drawn
 * (out of scope, no icon asset anywhere in this engine) -- text only. An
 * absent OR empty `alt` contributes nothing at all, matching a real
 * browser's own "no visible fallback for a purely decorative image"
 * behavior (`alt=""` is the standard way to mark an image decorative).
 *
 * `context_face` is the SURROUNDING text flow's own face (whatever
 * tbox_layout_collect_words would have used for an ordinary word at this
 * exact position) -- used for this word's `space_width` measurement in the
 * successful-decode case, and reused as the alt-text fallback's own face
 * too (an <img> has no font of its own to speak of; the surrounding
 * context's face is the closest sensible choice, and tbox_layout_push_words
 * already no-ops safely if it's NULL). `word->style` is set to `img_style`
 * (the img's OWN resolved style, not the surrounding context's) both for
 * the image case (purely so tbox_layout_build_line_runs' run-merge-boundary
 * `style` comparison still makes sense -- an image word never actually
 * merges with a neighbor regardless, see that function's `new_run` check)
 * and for the alt-text case (so the fallback text picks up the img
 * element's own resolved `color`, exactly like any other inline text
 * would). */


void tbox_layout_push_image_word(tbox_arena *arena, const tbox_html_node *img_node, const tbox_style *img_style, const tbox_font_face *context_face, tbox_image_cache *images, double containing_width, tbox_vector *words) {
    const tbox_html_attribute *src_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("src", 3));
    const tbox_image *image             = src_attr != NULL ? tbox_image_cache_get(images, src_attr->value) : NULL;

    if (image == NULL) {
        const tbox_html_attribute *alt_attr = tbox_html_node_get_attribute(img_node, tbox_string_view_make("alt", 3));
        if (alt_attr != NULL && alt_attr->value.size > 0) {
            tbox_layout_push_text_words(arena, words, alt_attr->value, context_face, img_style);
        }
        return;
    }

    double width  = img_style->width.kind == TBOX_STYLE_LENGTH_PX ? img_style->width.value : (double)image->width;
    double height = img_style->height.kind == TBOX_STYLE_LENGTH_PX ? img_style->height.value : (double)image->height;
    width = tbox_layout_constrain_width(img_style, width, containing_width, 0.0);

    tbox_layout_word *entry = tbox_layout_new_word(words);
    entry->text             = tbox_string_view_make(NULL, 0);
    entry->face             = context_face;
    entry->style             = img_style;
    entry->width             = width;
    entry->space_width       = context_face != NULL ? tbox_font_measure_text_spaced(context_face, tbox_string_view_make(" ", 1), img_style->letter_spacing) + img_style->word_spacing : 0.0;
    entry->image              = image;
    entry->image_height       = height;
    entry->hard_break          = false;
    entry->no_space_before     = false;
}

tbox_layout_replaced_image tbox_layout_image_prepare(tbox_arena *arena,
    const tbox_html_node *node, const tbox_style *style,
    tbox_font_face_cache *fonts, tbox_image_cache *images) {
    tbox_layout_replaced_image content = {0};
    const tbox_html_attribute *src = tbox_html_node_get_attribute(node, tbox_string_view_make("src", 3));
    content.image = src != NULL ? tbox_image_cache_get(images, src->value) : NULL;
    const tbox_html_attribute *alt = content.image == NULL ?
        tbox_html_node_get_attribute(node, tbox_string_view_make("alt", 3)) : NULL;
    content.fallback_text = alt != NULL ?
        tbox_string_collapse_whitespace(arena, alt->value) : tbox_string_view_make(NULL, 0);
    if (content.fallback_text.size > 0)
        content.fallback_face = tbox_font_face_cache_get(fonts,
            tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
            style->font_italic, style->font_size);
    return content;
}

double tbox_layout_image_auto_width(const tbox_layout_replaced_image *content,
    const tbox_style *style, tbox_layout_containing_block container, double vertical_edges) {
    const tbox_image *image = content->image;
    double width = image != NULL ? (double)image->width :
        content->fallback_face != NULL ?
            tbox_font_measure_text(content->fallback_face, content->fallback_text) : 16.0;
    if (image != NULL && image->height > 0) {
        if (style->height.kind == TBOX_STYLE_LENGTH_PX)
            width = (style->height.value -
                (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? vertical_edges : 0.0)) *
                image->width / image->height;
        else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
            width = (style->height.value / 100.0 * container.height -
                (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX ? vertical_edges : 0.0)) *
                image->width / image->height;
    }
    return width;
}

double tbox_layout_image_height(const tbox_layout_replaced_image *content,
    const tbox_style *style, tbox_layout_containing_block container,
    double content_width, double vertical_edges) {
    const tbox_image *image = content->image;
    double height = image != NULL ? (double)image->height :
        content->fallback_face != NULL ? tbox_font_face_line_height(content->fallback_face) : 16.0;
    if (style->height.kind == TBOX_STYLE_LENGTH_PX) height = style->height.value;
    else if (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)
        height = style->height.value / 100.0 * container.height;
    else if (image != NULL && image->width > 0)
        height = content_width * image->height / image->width;
    if (style->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX &&
        (style->height.kind == TBOX_STYLE_LENGTH_PX ||
         (style->height.kind == TBOX_STYLE_LENGTH_PERCENT && container.height_definite)))
        height = height > vertical_edges ? height - vertical_edges : 0.0;
    return height;
}

void tbox_layout_image_append_run(tbox_arena *arena,
    const tbox_layout_replaced_image *content, const tbox_style *style,
    tbox_layout_box *box) {
    if (content->image == NULL && content->fallback_face == NULL) return;
    tbox_layout_text_run *run = tbox_arena_alloc_zero(arena, sizeof(*run));
    run->rect = box->content_box;
    run->text = content->fallback_text;
    run->font = content->fallback_face;
    run->style = style;
    run->image = content->image;
    box->text_runs = run;
    box->text_run_count = 1;
}

bool tbox_layout_is_replaced_image(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT) return false;
    if (tbox_string_view_equal_cstr(node->element.tag_name, "img")) return true;
    if (!tbox_string_view_equal_cstr(node->element.tag_name, "input")) return false;
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type != NULL && tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("image", 5));
}
