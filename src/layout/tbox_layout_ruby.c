#include "tbox_layout_internal.h"

typedef struct tbox_layout_ruby_segment {
    tbox_string_view base;
    tbox_string_view annotation;
    const tbox_style *annotation_style;
} tbox_layout_ruby_segment;

static void tbox_layout_ruby_segments(tbox_arena *arena, const tbox_html_node *node,
    const tbox_style_table *styles, tbox_vector *segments) {
    tbox_string_builder builder;
    tbox_string_builder_init(&builder, arena, 32);
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "rp"))
            continue;
        if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "rt")) {
            tbox_string_view base = tbox_string_collapse_whitespace(arena, tbox_string_builder_finish(&builder));
            if (base.size > 0) {
                const tbox_style *rt_style = tbox_layout_style_or_default(styles, child);
                tbox_layout_ruby_segment *segment = tbox_vector_push(segments);
                segment->base = base;
                segment->annotation = rt_style->display == TBOX_STYLE_DISPLAY_NONE ? tbox_string_view_make(NULL, 0) :
                    tbox_string_collapse_whitespace(arena, tbox_html_node_text_content(arena, child));
                segment->annotation_style = rt_style;
            }
            tbox_string_builder_init(&builder, arena, 32);
            continue;
        }
        if (child->type == TBOX_HTML_NODE_TEXT)
            tbox_string_builder_append_view(&builder, child->text.text);
        else if (child->type == TBOX_HTML_NODE_ELEMENT &&
            tbox_layout_style_or_default(styles, child)->display != TBOX_STYLE_DISPLAY_NONE)
            tbox_string_builder_append_view(&builder, tbox_html_node_text_content(arena, child));
    }
    tbox_string_view base = tbox_string_collapse_whitespace(arena, tbox_string_builder_finish(&builder));
    if (base.size > 0) {
        tbox_layout_ruby_segment *segment = tbox_vector_push(segments);
        segment->base = base;
        segment->annotation = tbox_string_view_make(NULL, 0);
        segment->annotation_style = NULL;
    }
}

static double tbox_layout_ruby_text_width(const tbox_font_face *face, const tbox_style *style, tbox_string_view text) {
    return face != NULL && text.size > 0 ? tbox_font_measure_text_spaced(face, text, style->letter_spacing) : 0.0;
}

double tbox_layout_ruby_intrinsic_width(tbox_arena *arena, const tbox_html_node *node,
    const tbox_style_table *styles, tbox_font_face_cache *fonts) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);
    const tbox_font_face *base_face = tbox_layout_style_face(fonts, style);
    tbox_vector segments;
    tbox_vector_init(&segments, arena, sizeof(tbox_layout_ruby_segment), 4);
    tbox_layout_ruby_segments(arena, node, styles, &segments);
    double width = 0.0;
    for (size_t i = 0; i < segments.length; i++) {
        const tbox_layout_ruby_segment *segment = tbox_vector_at_const(&segments, i);
        const tbox_style *rt_style = segment->annotation_style;
        const tbox_font_face *rt_face = rt_style != NULL ? tbox_layout_style_face(fonts, rt_style) : NULL;
        tbox_string_view base = tbox_layout_transform_text(arena, segment->base, style->text_transform);
        tbox_string_view annotation = rt_style != NULL ?
            tbox_layout_transform_text(arena, segment->annotation, rt_style->text_transform) : segment->annotation;
        double base_width = tbox_layout_ruby_text_width(base_face, style, base);
        double rt_width = rt_style != NULL ? tbox_layout_ruby_text_width(rt_face, rt_style, annotation) : 0.0;
        width += base_width > rt_width ? base_width : rt_width;
    }
    return width;
}

double tbox_layout_build_ruby_runs(tbox_arena *arena, const tbox_html_node *node,
    const tbox_style_table *styles, tbox_font_face_cache *fonts, double x, double y, tbox_layout_box *box) {
    const tbox_style *style = tbox_layout_style_or_default(styles, node);
    const tbox_font_face *base_face = tbox_layout_style_face(fonts, style);
    if (base_face == NULL) return 0.0;
    tbox_vector segments, runs;
    tbox_vector_init(&segments, arena, sizeof(tbox_layout_ruby_segment), 4);
    tbox_vector_init(&runs, arena, sizeof(tbox_layout_text_run), 8);
    tbox_layout_ruby_segments(arena, node, styles, &segments);

    double annotation_height = 0.0;
    for (size_t i = 0; i < segments.length; i++) {
        const tbox_layout_ruby_segment *segment = tbox_vector_at_const(&segments, i);
        if (segment->annotation.size == 0 || segment->annotation_style == NULL) continue;
        const tbox_font_face *face = tbox_layout_style_face(fonts, segment->annotation_style);
        if (face == NULL) continue;
        double height = tbox_layout_style_line_height(segment->annotation_style, face);
        if (height > annotation_height) annotation_height = height;
    }

    double cursor_x = x;
    for (size_t i = 0; i < segments.length; i++) {
        const tbox_layout_ruby_segment *segment = tbox_vector_at_const(&segments, i);
        const tbox_style *rt_style = segment->annotation_style;
        const tbox_font_face *rt_face = rt_style != NULL ? tbox_layout_style_face(fonts, rt_style) : NULL;
        tbox_string_view base = tbox_layout_transform_text(arena, segment->base, style->text_transform);
        tbox_string_view annotation = rt_style != NULL ?
            tbox_layout_transform_text(arena, segment->annotation, rt_style->text_transform) : segment->annotation;
        double base_width = tbox_layout_ruby_text_width(base_face, style, base);
        double rt_width = rt_style != NULL ? tbox_layout_ruby_text_width(rt_face, rt_style, annotation) : 0.0;
        double cell_width = base_width > rt_width ? base_width : rt_width;
        if (annotation.size > 0 && rt_face != NULL) {
            tbox_layout_text_run *run = tbox_vector_push(&runs);
            *run = (tbox_layout_text_run){
                .rect = {cursor_x + (cell_width - rt_width) / 2.0, y, rt_width, annotation_height},
                .text = annotation, .font = rt_face, .style = rt_style,
            };
        }
        tbox_layout_text_run *run = tbox_vector_push(&runs);
        *run = (tbox_layout_text_run){
            .rect = {cursor_x + (cell_width - base_width) / 2.0, y + annotation_height,
                base_width, tbox_layout_style_line_height(style, base_face)},
            .text = base, .font = base_face, .style = style,
        };
        cursor_x += cell_width;
    }
    box->text_runs = runs.data;
    box->text_run_count = runs.length;
    return annotation_height + tbox_layout_style_line_height(style, base_face);
}
