#include "base/tbox_string.h"
#include "style/tbox_style_internal.h"
#include "svg/tbox_svg_internal.h"

#define TBOX_SVG_MAX_DEPTH 64
#define TBOX_SVG_MAX_NODES 16384

typedef struct tbox_svg_id {
    tbox_string_view id;
    const tbox_html_node *node;
} tbox_svg_id;
typedef struct tbox_svg_builder {
    tbox_arena *arena;
    const tbox_css_cascade_source *sources;
    size_t source_count;
    tbox_style_table *styles;
    tbox_vector ids;
    double width, height;
    bool reduced_motion;
    size_t count;
    const tbox_html_node *references[TBOX_SVG_MAX_DEPTH];
} tbox_svg_builder;

static const tbox_html_node *tbox_svg_reference(tbox_svg_builder *b, tbox_string_view href) {
    if (href.size < 2 || href.data[0] != '#')
        return NULL;
    href.data++;
    href.size--;
    const tbox_svg_id *ids = b->ids.data;
    for (size_t i = 0; i < b->ids.length; i++)
        if (tbox_string_view_equal(ids[i].id, href))
            return ids[i].node;
    return NULL;
}
static tbox_svg_node *tbox_svg_build(tbox_svg_builder *b, const tbox_html_node *node, const tbox_style *parent, bool instance, bool referenced, size_t depth) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT || depth >= TBOX_SVG_MAX_DEPTH || b->count >= TBOX_SVG_MAX_NODES)
        return NULL;
    bool container = tbox_svg_tag(node, "svg") || tbox_svg_tag(node, "g") || tbox_svg_tag(node, "use") || (referenced && tbox_svg_tag(node, "symbol"));
    bool shape     = tbox_svg_tag(node, "path") || tbox_svg_tag(node, "circle") || tbox_svg_tag(node, "ellipse") || tbox_svg_tag(node, "rect") || tbox_svg_tag(node, "line") || tbox_svg_tag(node, "polygon") || tbox_svg_tag(node, "polyline");
    if (!container && !shape)
        return NULL;
    const tbox_style *style = tbox_style_table_find(b->styles, node);
    if (instance) {
        tbox_css_computed_style computed = tbox_css_cascade_resolve_in_viewport(b->sources, b->source_count, node, b->width, b->height, b->reduced_motion, &(tbox_css_query_context){ parent->query_container, parent->custom_properties });
        tbox_style *copy                 = tbox_arena_alloc(b->arena, sizeof(*copy));
        if (copy != NULL)
            *copy = tbox_style_resolve_in_viewport(b->arena, node, parent, &computed, b->width, b->height);
        tbox_css_computed_style_destroy(&computed);
        style = copy;
    }
    if (style == NULL || style->display == TBOX_STYLE_DISPLAY_NONE)
        return NULL;
    tbox_svg_node *result = tbox_arena_alloc_zero(b->arena, sizeof(*result));
    if (result == NULL)
        return NULL;
    b->count++;
    result->node  = node;
    result->style = style;
    if (tbox_svg_tag(node, "use")) {
        tbox_string_view href = tbox_svg_attr(node, "href");
        if (href.size == 0)
            href = tbox_svg_attr(node, "xlink:href");
        const tbox_html_node *target = tbox_svg_reference(b, href);
        if (target == NULL)
            return result;
        for (size_t i = 0; i < depth; i++)
            if (b->references[i] == target)
                return result;
        b->references[depth] = target;
        result->first_child  = tbox_svg_build(b, target, style, true, true, depth + 1);
        b->references[depth] = NULL;
    } else if (container) {
        for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
            tbox_svg_node *n = tbox_svg_build(b, child, style, instance, false, depth + 1);
            if (n == NULL)
                continue;
            if (result->last_child != NULL)
                result->last_child->next_sibling = n;
            else
                result->first_child = n;
            result->last_child = n;
        }
    }
    return result;
}
void tbox_svg_prepare(tbox_arena *arena, const tbox_html_node *root, const tbox_css_cascade_source *sources, size_t source_count, tbox_style_table *styles, double width, double height, bool reduced_motion) {
    (void)root;
    bool any = false;
    for (size_t i = 0; i < styles->count; i++)
        if (tbox_svg_tag(styles->items[i].node, "svg")) {
            any = true;
            break;
        }
    if (!any)
        return;
    tbox_svg_builder b = { .arena = arena, .sources = sources, .source_count = source_count, .styles = styles, .width = width, .height = height, .reduced_motion = reduced_motion };
    tbox_vector_init(&b.ids, arena, sizeof(tbox_svg_id), 0);
    for (size_t i = 0; i < styles->count; i++) {
        const tbox_html_node *node = styles->items[i].node;
        tbox_string_view id        = tbox_svg_attr(node, "id");
        if (id.size == 0 || !tbox_svg_is_element(node))
            continue;
        tbox_svg_id *entry = tbox_vector_push(&b.ids);
        if (entry != NULL)
            *entry = (tbox_svg_id){ id, node };
    }
    for (size_t i = 0; i < styles->count; i++) {
        const tbox_html_node *node = styles->items[i].node;
        if (!tbox_svg_tag(node, "svg") || tbox_svg_is_element(node->parent))
            continue;
        b.count                    = 0;
        styles->items[i].style.svg = tbox_svg_build(&b, node, NULL, false, false, 0);
    }
}
