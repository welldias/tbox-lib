#include "tbox_layout_internal.h"
#include <stdio.h>
#include <string.h>

/* ---- flex formatting context ----
 *
 * A simplified CSS Flexbox Level 1 layout (§9): items (element children,
 * plus each run of loose text as an anonymous item), `order`, flex base
 * and hypothetical sizes with the automatic minimum size, line breaking
 * for `flex-wrap`, the flexible-length resolution loop with min/max
 * freezing, cross sizes with `stretch`, `align-content` for multi-line
 * containers, auto margins, `justify-content`, `align-items`/`align-self`
 * (first-baseline alignment for rows) and reversed directions. Each item is
 * laid out with tbox_layout_build_element_sized once its sizes are known;
 * a measuring build first gives its content height where needed.
 * Simplifications: percentages in an item's padding/margins resolve
 * against the container width; a column container without a definite
 * height never grows or shrinks its items and never wraps; `visibility:
 * collapse` items and fragmentation are not supported. */

#define TBOX_LAYOUT_FLEX_INFINITE 1e300

typedef struct tbox_layout_flex_item {
    const tbox_html_node *node;    /* the element, or the first node of an anonymous text run */
    const tbox_html_node *run_end; /* anonymous runs only: end of the run (exclusive) */
    bool anonymous;
    const tbox_style *style; /* the element's style, or the container's for a text run */
    int order;
    size_t index;
    double margin[4]; /* top right bottom left, auto as 0 */
    bool margin_auto[4];
    double edges[4];                                       /* padding + border, top right bottom left */
    double base, hypothetical, min_main, max_main, target; /* border-box main sizes */
    double violation;                                      /* clamping adjustment in the current resolution pass */
    bool frozen;
    double cross; /* border-box cross size */
    bool stretch;
    double baseline;            /* first baseline from the margin-box cross start */
    double main_pos, cross_pos; /* margin-box position, relative to the content box */
    tbox_layout_box *box;       /* the step-5 build at the resolved main size, at the origin */
} tbox_layout_flex_item;

/* True when laying `node` out elsewhere and moving it would misplace a
 * descendant: a `fixed` one (the viewport never moves), or an `absolute`
 * one with no positioned ancestor at or below `node`. */
bool tbox_layout_has_escaping_positioned(const tbox_html_node *node, const tbox_style_table *styles, bool contained) {
    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        if (child->type != TBOX_HTML_NODE_ELEMENT)
            continue;
        const tbox_style *style = tbox_layout_style_or_default(styles, child);
        if (style->display == TBOX_STYLE_DISPLAY_NONE)
            continue;
        if (style->position == TBOX_STYLE_POSITION_FIXED)
            return true;
        if (style->position == TBOX_STYLE_POSITION_ABSOLUTE && !contained)
            return true;
        if (tbox_layout_has_escaping_positioned(child, styles, contained || style->position != TBOX_STYLE_POSITION_STATIC))
            return true;
    }
    return false;
}

typedef struct tbox_layout_flex_line {
    size_t start, end;
    double cross, max_baseline;
} tbox_layout_flex_line;

static void tbox_layout_first_baseline(const tbox_layout_box *box, double *best_y, double *baseline) {
    for (size_t i = 0; i < box->text_run_count; i++) {
        const tbox_layout_text_run *run = &box->text_runs[i];
        if (run->image != NULL || run->font == NULL || run->rect.y >= *best_y)
            continue;
        *best_y   = run->rect.y;
        *baseline = run->rect.y + tbox_font_face_ascent(run->font);
    }
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling)
        tbox_layout_first_baseline(child, best_y, baseline);
}

/* Anonymous items (text runs, images) neither grow nor shrink by more
 * than the initial values: flex: 0 1 auto. */
static double tbox_layout_flex_grow(const tbox_layout_flex_item *item) {
    return item->anonymous ? 0.0 : item->style->flex_grow;
}

static double tbox_layout_flex_shrink(const tbox_layout_flex_item *item) {
    return item->anonymous ? 1.0 : item->style->flex_shrink;
}

/* The item's effective `align-self`. */
static tbox_style_flex_align tbox_layout_flex_align(const tbox_style *container, const tbox_layout_flex_item *item) {
    tbox_style_flex_align align = item->anonymous ? TBOX_STYLE_FLEX_ALIGN_NORMAL : item->style->align_self;
    if (align == TBOX_STYLE_FLEX_ALIGN_NORMAL)
        align = container->align_items;
    return align == TBOX_STYLE_FLEX_ALIGN_NORMAL ? TBOX_STYLE_FLEX_ALIGN_STRETCH : align;
}

/* A border-box size from a px/percent content (or border-box) length. */
static bool tbox_layout_flex_definite(tbox_style_length length, double base, bool base_definite, const tbox_style *style, double edges, double *out) {
    if (length.kind == TBOX_STYLE_LENGTH_PX)
        *out = length.value;
    else if (length.kind == TBOX_STYLE_LENGTH_PERCENT && base_definite)
        *out = tbox_style_length_resolve(length, base);
    else
        return false;
    if (style->box_sizing != TBOX_STYLE_BOX_SIZING_BORDER_BOX)
        *out += edges;
    if (*out < edges)
        *out = edges;
    return true;
}

/* Builds one item with the given border-box sizes (negative = natural) at
 * margin-box origin (x, y). */
static tbox_layout_box *tbox_layout_flex_build_item(tbox_arena *arena, const tbox_layout_flex_item *item, const tbox_style *container_style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double x, double y, double width, double height, double content_width, double content_height, bool height_definite, tbox_layout_positioned_context context) {
    if (item->anonymous) {
        return tbox_layout_build_anonymous_box(arena, item->node, item->run_end, container_style, styles, fonts, images, x, y, width >= 0.0 ? width : content_width, &context);
    }
    tbox_layout_forced_size forced         = { width >= 0.0, height >= 0.0, width, height };
    tbox_layout_containing_block container = { x, y, content_width, content_height, height_definite };
    return tbox_layout_build_element_sized(arena, item->node, styles, fonts, images, container, y, context, NULL, 0, &forced);
}

double tbox_layout_build_flex(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, const tbox_style_table *styles, tbox_font_face_cache *fonts, tbox_image_cache *images, double content_x, double content_y, double content_width, double content_height, bool height_definite, double min_height, double max_height, tbox_layout_box *box, tbox_layout_positioned_context context) {
    bool row            = style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW || style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE;
    bool reverse        = style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_ROW_REVERSE || style->flex_direction == TBOX_STYLE_FLEX_DIRECTION_COLUMN_REVERSE;
    bool main_definite  = row || height_definite;
    double main_size    = row ? content_width : (height_definite ? content_height : TBOX_LAYOUT_FLEX_INFINITE);
    bool cross_definite = row ? height_definite : true;
    double cross_size   = row ? content_height : content_width;
    double column_gap   = style->column_gap.kind == TBOX_STYLE_LENGTH_AUTO ? 0.0 : tbox_layout_resolve_edge(style->column_gap, content_width);
    double row_gap      = style->row_gap.kind == TBOX_STYLE_LENGTH_PX ? style->row_gap.value : style->row_gap.kind == TBOX_STYLE_LENGTH_PERCENT && height_definite ? tbox_style_length_resolve(style->row_gap, content_height) : 0.0;
    double main_gap = row ? column_gap : row_gap, cross_gap = row ? row_gap : column_gap;
    /* Axis indices into top/right/bottom/left arrays. */
    size_t main_start = row ? 3 : 0, main_end = row ? 1 : 2;
    size_t cross_start = row ? 0 : 3, cross_end = row ? 2 : 1;

    /* 1. Items, in `order`-modified document order. */
    tbox_vector items;
    tbox_vector_init(&items, arena, sizeof(tbox_layout_flex_item), 0);
    tbox_vector out_of_flow;
    tbox_vector_init(&out_of_flow, arena, sizeof(const tbox_html_node *), 0);
    for (const tbox_html_node *child = node->first_child; child != NULL;) {
        if (child->type == TBOX_HTML_NODE_TEXT) {
            const tbox_html_node *end = child;
            bool visible              = false;
            while (end != NULL && end->type != TBOX_HTML_NODE_ELEMENT) {
                if (end->type == TBOX_HTML_NODE_TEXT && tbox_string_collapse_whitespace(arena, end->text.text).size > 0)
                    visible = true;
                end = end->next_sibling;
            }
            if (visible) {
                tbox_layout_flex_item *item = (tbox_layout_flex_item *)tbox_vector_push(&items);
                memset(item, 0, sizeof(*item));
                item->node      = child;
                item->run_end   = end;
                item->anonymous = true;
                item->style     = style;
                item->index     = items.length - 1;
            }
            child = end;
            continue;
        }
        if (child->type == TBOX_HTML_NODE_ELEMENT && !tbox_layout_is_hidden_input(child)) {
            const tbox_style *child_style = tbox_layout_style_or_default(styles, child);
            bool positioned_out           = child_style->position == TBOX_STYLE_POSITION_ABSOLUTE || child_style->position == TBOX_STYLE_POSITION_FIXED;
            if (child_style->display == TBOX_STYLE_DISPLAY_NONE) {
                /* no box */
            } else if (positioned_out) {
                *(const tbox_html_node **)tbox_vector_push(&out_of_flow) = child;
            } else {
                tbox_layout_flex_item *item = (tbox_layout_flex_item *)tbox_vector_push(&items);
                memset(item, 0, sizeof(*item));
                item->node  = child;
                item->style = child_style;
                item->order = child_style->order;
                item->index = items.length - 1;
            }
        }
        child = child->next_sibling;
    }
    tbox_layout_flex_item *item_data = (tbox_layout_flex_item *)items.data;
    size_t count                     = items.length;
    for (size_t i = 1; i < count; i++) { /* stable insertion sort by order */
        tbox_layout_flex_item key = item_data[i];
        size_t j                  = i;
        while (j > 0 && item_data[j - 1].order > key.order) {
            item_data[j] = item_data[j - 1];
            j--;
        }
        item_data[j] = key;
    }

    /* 2. Edges, flex base size, min/max and hypothetical main size. */
    for (size_t i = 0; i < count; i++) {
        tbox_layout_flex_item *item = &item_data[i];
        const tbox_style *s         = item->style;
        if (!item->anonymous) {
            for (size_t side = 0; side < 4; side++) {
                item->margin_auto[side] = s->margin[side].kind == TBOX_STYLE_LENGTH_AUTO;
                item->margin[side]      = tbox_layout_resolve_edge(s->margin[side], content_width);
                item->edges[side]       = tbox_layout_resolve_edge(s->padding[side], content_width) + tbox_style_border_side_width(s, side);
            }
        }
        double main_edges              = item->edges[main_start] + item->edges[main_end];
        double cross_edges             = item->edges[cross_start] + item->edges[cross_end];
        double cross_margins           = item->margin[cross_start] + item->margin[cross_end];
        tbox_style_length main_length  = row ? s->width : s->height;
        tbox_style_length cross_length = row ? s->height : s->width;
        item->stretch                  = tbox_layout_flex_align(style, item) == TBOX_STYLE_FLEX_ALIGN_STRETCH && (item->anonymous || cross_length.kind == TBOX_STYLE_LENGTH_AUTO) && !item->margin_auto[cross_start] && !item->margin_auto[cross_end];

        /* Content sizes along the main axis. A row measures intrinsic
         * widths; a column lays the item out at its cross size. */
        double content_min = 0.0, content_max = 0.0;
        if (row) {
            tbox_layout_intrinsic intrinsic;
            if (item->anonymous) {
                tbox_vector words;
                tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);
                tbox_layout_inline_state state = { false, false };
                tbox_layout_collect_words_in(arena, item->node, item->run_end, style, styles, fonts, images, 0.0, false, true, &state, &words);
                intrinsic = tbox_layout_measure_words((const tbox_layout_word *)words.data, words.length, tbox_layout_white_space_nowrap(style));
            } else {
                intrinsic = tbox_layout_intrinsic_content(arena, item->node, styles, fonts, images);
            }
            content_min = intrinsic.min + main_edges;
            content_max = intrinsic.max + main_edges;
        } else {
            double width = -1.0;
            if (item->anonymous || cross_length.kind == TBOX_STYLE_LENGTH_AUTO) {
                double available = cross_size - cross_margins;
                if (item->stretch) {
                    width = available;
                } else {
                    tbox_layout_intrinsic outer = item->anonymous ? (tbox_layout_intrinsic){ available, available } : tbox_layout_intrinsic_outer(arena, item->node, styles, fonts, images);
                    width                       = outer.max - cross_margins;
                    if (width > available)
                        width = available;
                    if (width < outer.min - cross_margins)
                        width = outer.min - cross_margins;
                }
                if (width < cross_edges)
                    width = cross_edges;
            }
            tbox_layout_box *measured = tbox_layout_flex_build_item(arena, item, style, styles, fonts, images, 0.0, 0.0, width, -1.0, content_width, content_height, height_definite, context);
            content_min = content_max = measured->border_box.height;
            item->cross               = measured->border_box.width;
        }

        double specified;
        bool has_specified = !item->anonymous && tbox_layout_flex_definite(main_length, main_size, main_definite, s, main_edges, &specified);
        /* aspect-ratio transfers a definite cross size to the main axis
         * (of box-sizing's box) when the main size is auto. */
        double cross_border;
        if (!item->anonymous && !has_specified && s->aspect_ratio > 0.0 && main_length.kind == TBOX_STYLE_LENGTH_AUTO &&
            tbox_layout_flex_definite(cross_length, cross_size, row ? height_definite : true, s, cross_edges, &cross_border)) {
            bool border_box  = s->box_sizing == TBOX_STYLE_BOX_SIZING_BORDER_BOX;
            double cross_ref = border_box ? cross_border : cross_border - cross_edges;
            double main_ref  = row ? cross_ref * s->aspect_ratio : cross_ref / s->aspect_ratio;
            specified        = border_box ? main_ref : main_ref + main_edges;
            has_specified    = true;
        }
        double basis;
        if (!item->anonymous && tbox_layout_flex_definite(s->flex_basis, main_size, main_definite, s, main_edges, &basis)) {
            /* definite flex-basis */
        } else if (has_specified) {
            basis = specified;
        } else if (!item->anonymous && (row ? s->width_keyword : s->height_keyword) == TBOX_STYLE_SIZE_KEYWORD_MIN_CONTENT) {
            basis = content_min;
        } else {
            basis = content_max;
        }
        item->base = basis;

        tbox_style_length min_length = row ? s->min_width : s->min_height;
        tbox_style_length max_length = row ? s->max_width : s->max_height;
        if (item->anonymous || min_length.kind == TBOX_STYLE_LENGTH_AUTO) {
            /* Automatic minimum size: the content minimum, capped by a
             * specified size; none for scroll containers. */
            item->min_main = !item->anonymous && s->overflow_y != TBOX_STYLE_OVERFLOW_Y_VISIBLE ? main_edges : content_min;
            if (has_specified && specified < item->min_main)
                item->min_main = specified;
        } else if (!tbox_layout_flex_definite(min_length, main_size, main_definite, s, main_edges, &item->min_main)) {
            item->min_main = main_edges;
        }
        if (item->anonymous || !tbox_layout_flex_definite(max_length, main_size, main_definite, s, main_edges, &item->max_main))
            item->max_main = TBOX_LAYOUT_FLEX_INFINITE;
        if (item->max_main < item->min_main)
            item->max_main = item->min_main;
        double hypothetical = item->base;
        if (hypothetical > item->max_main)
            hypothetical = item->max_main;
        if (hypothetical < item->min_main)
            hypothetical = item->min_main;
        item->hypothetical = hypothetical;
    }

    /* 3. Lines. Without a definite height, a column still breaks at its
     * max-height. */
    tbox_vector lines;
    tbox_vector_init(&lines, arena, sizeof(tbox_layout_flex_line), 0);
    double break_limit = main_definite ? main_size : max_height;
    bool multi_line    = style->flex_wrap != TBOX_STYLE_FLEX_WRAP_NOWRAP && break_limit < TBOX_LAYOUT_FLEX_INFINITE;
    size_t line_start  = 0;
    double line_used   = 0.0;
    for (size_t i = 0; i < count; i++) {
        tbox_layout_flex_item *item = &item_data[i];
        double outer                = item->hypothetical + item->margin[main_start] + item->margin[main_end];
        if (multi_line && i > line_start && line_used + main_gap + outer > break_limit) {
            *(tbox_layout_flex_line *)tbox_vector_push(&lines) = (tbox_layout_flex_line){ line_start, i, 0.0, 0.0 };
            line_start                                         = i;
            line_used                                          = outer;
        } else {
            line_used += (i > line_start ? main_gap : 0.0) + outer;
        }
    }
    if (count > 0)
        *(tbox_layout_flex_line *)tbox_vector_push(&lines) = (tbox_layout_flex_line){ line_start, count, 0.0, 0.0 };
    tbox_layout_flex_line *line_data = (tbox_layout_flex_line *)lines.data;
    size_t line_count                = lines.length;

    /* A column without a definite height is as tall as its longest line,
     * clamped by min/max-height; with such a limit the clamped height is
     * then the main size its items flex into (CSS Flexbox §9.3 step 5). */
    if (!main_definite && (min_height > 0.0 || max_height < TBOX_LAYOUT_FLEX_INFINITE)) {
        double longest = 0.0;
        for (size_t l = 0; l < line_count; l++) {
            size_t n    = line_data[l].end - line_data[l].start;
            double used = n > 1 ? main_gap * (double)(n - 1) : 0.0;
            for (size_t i = line_data[l].start; i < line_data[l].end; i++)
                used += item_data[i].hypothetical + item_data[i].margin[main_start] + item_data[i].margin[main_end];
            if (used > longest)
                longest = used;
        }
        main_size = longest > max_height ? max_height : longest;
        if (main_size < min_height)
            main_size = min_height;
        main_definite = true;
    }

    /* 4. Flexible lengths (§9.7), per line. */
    double used_main = 0.0;
    for (size_t l = 0; l < line_count; l++) {
        tbox_layout_flex_line *line = &line_data[l];
        size_t n                    = line->end - line->start;
        double gaps                 = n > 1 ? main_gap * (double)(n - 1) : 0.0;
        double sum_hypothetical     = gaps;
        for (size_t i = line->start; i < line->end; i++)
            sum_hypothetical += item_data[i].hypothetical + item_data[i].margin[main_start] + item_data[i].margin[main_end];
        bool grow = main_definite && sum_hypothetical < main_size;
        for (size_t i = line->start; i < line->end; i++) {
            tbox_layout_flex_item *item = &item_data[i];
            double factor               = grow ? tbox_layout_flex_grow(item) : tbox_layout_flex_shrink(item);
            item->target                = item->hypothetical;
            item->frozen                = !main_definite || factor == 0.0 || (grow && item->base > item->hypothetical) || (!grow && item->base < item->hypothetical);
        }
        double initial_free = 0.0;
        if (main_definite) {
            initial_free = main_size - gaps;
            for (size_t i = line->start; i < line->end; i++) {
                const tbox_layout_flex_item *item = &item_data[i];
                initial_free -= (item->frozen ? item->target : item->base) + item->margin[main_start] + item->margin[main_end];
            }
        }
        for (int iteration = 0; iteration < 64; iteration++) {
            bool any_unfrozen = false;
            double remaining = main_size - gaps, sum_factors = 0.0, sum_scaled = 0.0;
            for (size_t i = line->start; i < line->end; i++) {
                const tbox_layout_flex_item *item = &item_data[i];
                remaining -= (item->frozen ? item->target : item->base) + item->margin[main_start] + item->margin[main_end];
                if (item->frozen)
                    continue;
                any_unfrozen = true;
                sum_factors += grow ? tbox_layout_flex_grow(item) : tbox_layout_flex_shrink(item);
                sum_scaled += tbox_layout_flex_shrink(item) * item->base;
            }
            if (!any_unfrozen)
                break;
            if (sum_factors < 1.0) {
                double scaled_free = initial_free * sum_factors;
                if ((scaled_free < 0.0 ? -scaled_free : scaled_free) < (remaining < 0.0 ? -remaining : remaining))
                    remaining = scaled_free;
            }
            double total_violation = 0.0;
            for (size_t i = line->start; i < line->end; i++) {
                tbox_layout_flex_item *item = &item_data[i];
                if (item->frozen)
                    continue;
                double target = item->base;
                if (grow && sum_factors > 0.0)
                    target += remaining * tbox_layout_flex_grow(item) / sum_factors;
                else if (!grow && sum_scaled > 0.0)
                    target += remaining * tbox_layout_flex_shrink(item) * item->base / sum_scaled;
                double clamped = target;
                if (clamped > item->max_main)
                    clamped = item->max_main;
                if (clamped < item->min_main)
                    clamped = item->min_main;
                item->violation = clamped - target;
                total_violation += item->violation;
                item->target = clamped;
            }
            /* Freeze everything when nothing was clamped, else only the
             * items clamped in the direction of the total violation. */
            for (size_t i = line->start; i < line->end; i++) {
                tbox_layout_flex_item *item = &item_data[i];
                if (item->frozen)
                    continue;
                if (total_violation == 0.0 || (total_violation > 0.0 && item->violation > 0.0) || (total_violation < 0.0 && item->violation < 0.0))
                    item->frozen = true;
            }
        }
        double line_main = gaps;
        for (size_t i = line->start; i < line->end; i++)
            line_main += item_data[i].target + item_data[i].margin[main_start] + item_data[i].margin[main_end];
        if (line_main > used_main)
            used_main = line_main;
    }
    double main_extent = main_definite ? main_size : used_main;

    /* 5. Cross sizes and baselines, laid out at the resolved main size. */
    for (size_t i = 0; i < count; i++) {
        tbox_layout_flex_item *item = &item_data[i];
        tbox_layout_box *measured;
        if (row) {
            measured    = tbox_layout_flex_build_item(arena, item, style, styles, fonts, images, 0.0, 0.0, item->target, -1.0, content_width, content_height, height_definite, context);
            item->cross = measured->border_box.height;
        } else {
            measured = tbox_layout_flex_build_item(arena, item, style, styles, fonts, images, 0.0, 0.0, item->cross, item->target, content_width, content_height, height_definite, context);
        }
        double best_y   = TBOX_LAYOUT_FLEX_INFINITE;
        double baseline = measured->border_box.y + measured->border_box.height;
        tbox_layout_first_baseline(measured, &best_y, &baseline);
        item->baseline = baseline - measured->border_box.y + item->margin[cross_start];
        item->box      = measured;
    }
    for (size_t l = 0; l < line_count; l++) {
        tbox_layout_flex_line *line = &line_data[l];
        double max_outer = 0.0, max_below = 0.0;
        for (size_t i = line->start; i < line->end; i++) {
            const tbox_layout_flex_item *item = &item_data[i];
            double outer                      = item->cross + item->margin[cross_start] + item->margin[cross_end];
            if (row && tbox_layout_flex_align(style, item) == TBOX_STYLE_FLEX_ALIGN_BASELINE) {
                if (item->baseline > line->max_baseline)
                    line->max_baseline = item->baseline;
                if (outer - item->baseline > max_below)
                    max_below = outer - item->baseline;
            } else if (outer > max_outer) {
                max_outer = outer;
            }
        }
        line->cross = line->max_baseline + max_below > max_outer ? line->max_baseline + max_below : max_outer;
        if (!multi_line && cross_definite)
            line->cross = cross_size;
        /* A single-line row without a definite height clamps its line by
         * the container's min/max-height (§9.4 step 8). */
        if (!multi_line && !cross_definite && row) {
            if (line->cross > max_height)
                line->cross = max_height;
            if (line->cross < min_height)
                line->cross = min_height;
        }
    }

    /* 6. align-content: distribute leftover cross space between lines. A
     * multi-line row without a definite height gets its cross size from its
     * lines, clamped by min/max-height, and distributes any excess. */
    double lines_cross = line_count > 1 ? cross_gap * (double)(line_count - 1) : 0.0;
    for (size_t l = 0; l < line_count; l++)
        lines_cross += line_data[l].cross;
    if (row && !cross_definite) {
        double clamped = lines_cross > max_height ? max_height : lines_cross;
        if (clamped < min_height)
            clamped = min_height;
        if (clamped != lines_cross) {
            cross_size     = clamped;
            cross_definite = true;
        }
    }
    double cross_free   = multi_line && cross_definite ? cross_size - lines_cross : 0.0;
    double cross_offset = 0.0, cross_spacing = cross_gap;
    if (cross_free > 0.0 && line_count > 0) {
        switch (style->align_content) {
        case TBOX_STYLE_FLEX_JUSTIFY_NORMAL:
        case TBOX_STYLE_FLEX_JUSTIFY_STRETCH:
            for (size_t l = 0; l < line_count; l++)
                line_data[l].cross += cross_free / (double)line_count;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_END:
            cross_offset = cross_free;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_CENTER:
            cross_offset = cross_free / 2.0;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN:
            if (line_count > 1)
                cross_spacing += cross_free / (double)(line_count - 1);
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND:
            cross_offset = cross_free / (double)line_count / 2.0;
            cross_spacing += cross_free / (double)line_count;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY:
            cross_offset = cross_free / (double)(line_count + 1);
            cross_spacing += cross_free / (double)(line_count + 1);
            break;
        default:
            break;
        }
    }
    double cross_extent = cross_definite ? cross_size : lines_cross;

    /* 7. Positions: auto margins, justify-content, align-self. */
    double line_cross_pos = cross_offset;
    for (size_t l = 0; l < line_count; l++) {
        tbox_layout_flex_line *line = &line_data[l];
        size_t n                    = line->end - line->start;
        double free                 = main_extent - (n > 1 ? main_gap * (double)(n - 1) : 0.0);
        size_t auto_margins         = 0;
        for (size_t i = line->start; i < line->end; i++) {
            const tbox_layout_flex_item *item = &item_data[i];
            free -= item->target + item->margin[main_start] + item->margin[main_end];
            auto_margins += item->margin_auto[main_start] + item->margin_auto[main_end];
        }
        double auto_share = 0.0;
        if (free > 0.0 && auto_margins > 0) {
            auto_share = free / (double)auto_margins;
            free       = 0.0;
        }
        double offset = 0.0, spacing = main_gap;
        switch (style->justify_content) {
        case TBOX_STYLE_FLEX_JUSTIFY_END:
            offset = free;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_CENTER:
            offset = free / 2.0;
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_BETWEEN:
            if (free > 0.0 && n > 1)
                spacing += free / (double)(n - 1);
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_AROUND:
            if (free > 0.0) {
                offset = free / (double)n / 2.0;
                spacing += free / (double)n;
            } else {
                offset = free / 2.0;
            }
            break;
        case TBOX_STYLE_FLEX_JUSTIFY_SPACE_EVENLY:
            if (free > 0.0) {
                offset = free / (double)(n + 1);
                spacing += free / (double)(n + 1);
            } else {
                offset = free / 2.0;
            }
            break;
        default:
            break;
        }
        double pos = offset;
        for (size_t i = line->start; i < line->end; i++) {
            tbox_layout_flex_item *item = &item_data[i];
            double lead                 = item->margin_auto[main_start] ? auto_share : 0.0;
            double trail                = item->margin_auto[main_end] ? auto_share : 0.0;
            double outer                = item->target + item->margin[main_start] + item->margin[main_end] + lead + trail;
            item->main_pos              = pos + lead;
            if (reverse)
                item->main_pos = main_extent - pos - outer + lead;
            pos += outer + spacing;

            /* Cross axis within the line. */
            double outer_cross = item->cross + item->margin[cross_start] + item->margin[cross_end];
            if (item->stretch) {
                double stretched             = line->cross - item->margin[cross_start] - item->margin[cross_end];
                tbox_style_length min_length = row ? item->style->min_height : item->style->min_width;
                tbox_style_length max_length = row ? item->style->max_height : item->style->max_width;
                double cross_edges           = item->edges[cross_start] + item->edges[cross_end], limit;
                if (!item->anonymous && tbox_layout_flex_definite(max_length, cross_size, cross_definite, item->style, cross_edges, &limit) && stretched > limit)
                    stretched = limit;
                if (!item->anonymous && tbox_layout_flex_definite(min_length, cross_size, cross_definite, item->style, cross_edges, &limit) && stretched < limit)
                    stretched = limit;
                if (stretched < cross_edges)
                    stretched = cross_edges;
                item->cross = stretched;
                outer_cross = item->cross + item->margin[cross_start] + item->margin[cross_end];
            }
            double cross_free_item = line->cross - outer_cross;
            double shift           = 0.0;
            if (item->margin_auto[cross_start] && item->margin_auto[cross_end]) {
                shift = cross_free_item > 0.0 ? cross_free_item / 2.0 : 0.0;
            } else if (item->margin_auto[cross_start]) {
                shift = cross_free_item > 0.0 ? cross_free_item : 0.0;
            } else if (!item->margin_auto[cross_end]) {
                switch (tbox_layout_flex_align(style, item)) {
                case TBOX_STYLE_FLEX_ALIGN_END:
                    shift = cross_free_item;
                    break;
                case TBOX_STYLE_FLEX_ALIGN_CENTER:
                    shift = cross_free_item / 2.0;
                    break;
                case TBOX_STYLE_FLEX_ALIGN_BASELINE:
                    shift = row ? line->max_baseline - item->baseline : 0.0;
                    break;
                default:
                    break;
                }
            }
            item->cross_pos = line_cross_pos + shift;
            if (style->flex_wrap == TBOX_STYLE_FLEX_WRAP_WRAP_REVERSE)
                item->cross_pos = cross_extent - line_cross_pos - line->cross + shift;
        }
        line_cross_pos += line->cross + cross_spacing;
    }

    /* 8. Final boxes, children in order-modified document order. */
    tbox_layout_box *previous = NULL;
    for (size_t i = 0; i < count; i++) {
        const tbox_layout_flex_item *item = &item_data[i];
        double x                          = content_x + (row ? item->main_pos : item->cross_pos);
        double y                          = content_y + (row ? item->cross_pos : item->main_pos);
        double width                      = row ? item->target : item->cross;
        double height                     = row ? (item->stretch ? item->cross : -1.0) : item->target;
        /* Reuse the step-5 build when its size is final and moving it is
         * safe; otherwise lay the item out again in place. */
        tbox_layout_box *child_box = item->box;
        bool same_size             = child_box != NULL && child_box->border_box.width == width && (height < 0.0 || child_box->border_box.height == height);
        bool movable               = item->anonymous || !tbox_layout_has_escaping_positioned(item->node, styles, item->style->position != TBOX_STYLE_POSITION_STATIC);
        if (same_size && movable) {
            tbox_layout_translate(child_box, x - child_box->margin_box.x, y - child_box->margin_box.y);
        } else {
            child_box = tbox_layout_flex_build_item(arena, item, style, styles, fonts, images, x, y, width, height, content_width, content_height, height_definite, context);
        }
        child_box->parent = box;
        if (previous == NULL)
            box->first_child = child_box;
        else
            previous->next_sibling = child_box;
        box->last_child = child_box;
        previous        = child_box;
    }
    for (size_t i = 0; i < out_of_flow.length; i++) {
        const tbox_html_node *child            = *(const tbox_html_node **)tbox_vector_at(&out_of_flow, i);
        const tbox_style *child_style          = tbox_layout_style_or_default(styles, child);
        tbox_rect basis                        = child_style->position == TBOX_STYLE_POSITION_ABSOLUTE ? context.nearest_ancestor : context.viewport;
        tbox_layout_containing_block container = { basis.x, basis.y, basis.width, basis.height, true };
        tbox_layout_box *child_box             = tbox_layout_build_element(arena, child, styles, fonts, images, container, content_y, context, NULL, 0);
        child_box->parent                      = box;
        if (previous == NULL)
            box->first_child = child_box;
        else
            previous->next_sibling = child_box;
        box->last_child = child_box;
        previous        = child_box;
    }

    double extent              = row ? cross_extent : main_extent;
    box->scroll_content_height = row ? lines_cross : used_main;
    return extent;
}
