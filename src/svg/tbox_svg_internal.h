#ifndef TBOX_SVG_INTERNAL_H
#define TBOX_SVG_INTERNAL_H
#include "base/tbox_arena.h"
#include "base/tbox_path.h"
#include <tbox/render.h>

/* Prepared once per frame after cascade. Definitions stay in the DOM;
 * use instances carry their own inherited style and never mutate it. */
typedef struct tbox_svg_node {
    const tbox_html_node *node;
    const tbox_style *style;
    struct tbox_svg_node *first_child, *last_child, *next_sibling;
} tbox_svg_node;

bool tbox_svg_tag(const tbox_html_node *node, const char *tag);
tbox_string_view tbox_svg_attr(const tbox_html_node *node, const char *name);
size_t tbox_svg_numbers(tbox_string_view text, double *out, size_t capacity);
bool tbox_svg_parse_transform(tbox_string_view text, double out[6]);
void tbox_svg_matrix_multiply(double a[6], const double b[6]);
bool tbox_svg_viewbox(const tbox_html_node *node, tbox_rect *out);
void tbox_svg_view_matrix(const tbox_html_node *node, tbox_rect viewport, double out[6]);
void tbox_svg_prepare(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count, tbox_style_table *styles, double width, double height, bool reduced_motion);
void tbox_render_svg(const tbox_layout_box *box, tbox_vector *items, tbox_arena *arena);
/* Flattens a shape in SVG user coordinates into caller-arena edges. */
bool tbox_svg_shape(const tbox_html_node *node, tbox_vector *segments, double tolerance, double viewport_width, double viewport_height, double font_size);
#endif
