/* Virtual tables (docs/10-formularios-e-controles.md, "Tabela virtual"):
 * only the rows in view, plus a margin, are in the DOM; the application
 * fills them through a handler, now or later. The window is a <tbody> of
 * recycled <tr>s between two spacer <tbody>s that stand for the rows
 * above and below, sized with the rows' measured pitch. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html_parser/tbox_html_document.h"
#include "tbox_context_controls.h"
#include "tbox_context_internal.h"

/* Rows asked for before the first layout measured any. */
#define TBOX_VIRTUAL_INITIAL_ROWS 32
/* Pitch assumed until then, px. */
#define TBOX_VIRTUAL_GUESS_HEIGHT 24.0

static bool tbox_virtual_is(const tbox_html_node *node, const char *tag) {
    return node != NULL && node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, tag);
}

static tbox_virtual_table *tbox_virtual_find(tbox_context *ctx, const tbox_html_node *table) {
    for (tbox_virtual_table *vt = ctx != NULL ? ctx->virtual_tables : NULL; vt != NULL; vt = vt->next)
        if (vt->table == table && !vt->removed)
            return vt;
    return NULL;
}

static size_t tbox_virtual_row_count(const tbox_virtual_table *vt) {
    size_t count = 0;
    for (const tbox_html_node *row = tbox_html_node_first_element_child(vt->body); row != NULL; row = tbox_html_node_next_element_sibling(row))
        count++;
    return count;
}

/* Points the spacer's `style` at its own buffer: written every time the
 * window moves, so it must not allocate in the document. */
static void tbox_virtual_set_spacer(tbox_virtual_table *vt, int which, double height) {
    tbox_html_node *spacer = which == 0 ? vt->top_spacer : vt->bottom_spacer;
    char *buffer           = vt->spacer_style[which];
    int length             = snprintf(buffer, sizeof(vt->spacer_style[which]), "height: %.3fpx", height > 0.0 ? height : 0.0);
    if (length < 0 || (size_t)length >= sizeof(vt->spacer_style[which]))
        length = 0;
    for (size_t i = 0; i < spacer->element.attribute_count; i++)
        if (tbox_string_view_equal_cstr(spacer->element.attributes[i].name, "style")) {
            if (!tbox_string_view_equal(spacer->element.attributes[i].value, tbox_string_view_make(buffer, (size_t)length))) {
                spacer->element.attributes[i].value = tbox_string_view_make(buffer, (size_t)length);
                tbox_html_document_touch(spacer->owner_document);
            }
            return;
        }
}

static tbox_html_node *tbox_virtual_section(tbox_context *ctx, tbox_html_node *table, tbox_html_node *before, tbox_html_node **row) {
    tbox_html_node *section = tbox_html_element_create(ctx->document, tbox_string_view_make("tbody", 5));
    if (section == NULL)
        return NULL;
    tbox_html_node_insert_before(table, section, before);
    if (row != NULL) {
        *row = tbox_html_element_create(ctx->document, tbox_string_view_make("tr", 2));
        if (*row == NULL)
            return NULL;
        /* A placeholder the spacer then points at its own buffer. */
        tbox_html_node_set_attribute(ctx->document, *row, tbox_string_view_make("style", 5), tbox_string_view_make("height: 0px", 11));
        tbox_html_node_append_child(section, *row);
    }
    return section;
}

/* Whether `row` holds the focused node. */
static bool tbox_virtual_holds_focus(const tbox_context *ctx, const tbox_html_node *row) {
    for (const tbox_html_node *at = ctx->focused_node; at != NULL; at = at->parent)
        if (at == row)
            return true;
    return false;
}

static bool tbox_virtual_keep_spare(tbox_virtual_table *vt, tbox_html_node *row) {
    if (vt->spare_count == vt->spare_capacity) {
        size_t capacity        = vt->spare_capacity > 0 ? vt->spare_capacity * 2 : 16;
        tbox_html_node **spare = realloc(vt->spare, capacity * sizeof(*spare));
        if (spare == NULL)
            return false;
        vt->spare          = spare;
        vt->spare_capacity = capacity;
    }
    vt->spare[vt->spare_count++] = row;
    return true;
}

/* A <tr> for a row entering the window: a spare one, or a new copy of
 * the template appended to the body (moved into place by the caller). */
static tbox_html_node *tbox_virtual_take_row(tbox_context *ctx, tbox_virtual_table *vt) {
    if (vt->spare_count > 0)
        return vt->spare[--vt->spare_count];
    tbox_html_node *row = tbox_context_control_instantiate(ctx, vt->row_template, vt->body, SIZE_MAX);
    if (row != NULL)
        tbox_html_node_remove(row);
    return row;
}

/* The window [first, end) the scroll position asks for, from the last
 * measured geometry: what the scrollport shows plus half of it on each
 * side, starting at an even row so :nth-child(even/odd) stays put. */
static void tbox_virtual_desired(tbox_context *ctx, const tbox_virtual_table *vt, size_t *out_first, size_t *out_end) {
    if (!vt->measured || vt->row_height <= 0.0) {
        *out_first = 0;
        *out_end   = vt->total < TBOX_VIRTUAL_INITIAL_ROWS ? vt->total : TBOX_VIRTUAL_INITIAL_ROWS;
        return;
    }
    /* The row at the top of the scrollport, fractional: from the anchor
     * and what scrolled since, so a new pitch (the spacers not yet resized
     * to it) doesn't move it; from the offset alone after a jump, where
     * the position on the scrollbar is what counts. */
    double offset = vt->container != NULL ? tbox_context_scroll_offset(ctx, vt->container) : 0.0;
    double row    = (offset - vt->table_top) / vt->row_height;
    if (vt->has_anchor && fabs(offset - vt->anchor_scroll) < 2.0 * vt->port_height)
        row = (double)vt->anchor_index + (offset - vt->anchor_scroll - vt->anchor_offset) / vt->row_height;
    double margin = vt->port_height / 2.0 / vt->row_height;
    double first  = row - margin > 0.0 ? floor(row - margin) : 0.0;
    double end    = row + 3.0 * margin > 0.0 ? ceil(row + 3.0 * margin) : 0.0;
    if (end > (double)vt->total)
        end = (double)vt->total;
    if (first > end)
        first = end;
    size_t start = (size_t)first & ~(size_t)1;
    *out_first   = start;
    *out_end     = (size_t)end;
}

/* Calls the handler for rows [first, end) of the window `rows` (which
 * starts at logical row `window_first`), with the release bracket. */
static void tbox_virtual_ask(tbox_context *ctx, tbox_virtual_table *vt, tbox_html_node **rows, size_t window_first, size_t first, size_t end) {
    if (first >= end || vt->handler == NULL || vt->removed)
        return;
    vt->busy = true;
    tbox_release_enter(&ctx->releases);
    vt->handler(ctx, vt->table, first, rows + (first - window_first), end - first, vt->userdata);
    tbox_release_leave(&ctx->releases);
    vt->busy = false;
}

static void tbox_virtual_update_one(tbox_context *ctx, tbox_virtual_table *vt) {
    /* A new pitch resizes the spacers; the scroll anchor (measure) keeps
     * what is in view where it was. */
    double height = vt->measured && vt->row_height > 0.0 ? vt->row_height : TBOX_VIRTUAL_GUESS_HEIGHT;

    size_t first, end;
    tbox_virtual_desired(ctx, vt, &first, &end);
    size_t old_first = vt->first, old_count = tbox_virtual_row_count(vt), old_end = old_first + old_count;
    size_t count  = end - first;
    bool moved    = first != old_first || end != old_end;
    tbox_html_node **rows = count > 0 ? malloc(count * sizeof(*rows)) : NULL;
    if (count > 0 && rows == NULL)
        return;

    if (moved) {
        /* Rows that stay keep their <tr>; the others go to the spare list,
         * where the entering ones take theirs from. */
        size_t index = old_first;
        for (tbox_html_node *row = tbox_html_node_first_element_child(vt->body), *next; row != NULL; row = next, index++) {
            next = tbox_html_node_next_element_sibling(row);
            if (index >= first && index < end) {
                rows[index - first] = row;
                continue;
            }
            if (tbox_virtual_holds_focus(ctx, row))
                tbox_context_set_focus(ctx, NULL);
            tbox_html_node_remove(row);
            if (!tbox_virtual_keep_spare(vt, row))
                break;
        }
        for (size_t i = first; i < end; i++) {
            if (i >= old_first && i < old_end)
                continue;
            rows[i - first] = tbox_virtual_take_row(ctx, vt);
            if (rows[i - first] == NULL) {
                end   = i; /* out of memory: a shorter window */
                count = end - first;
                break;
            }
        }
        /* Back into the body in order. */
        for (tbox_html_node *row = tbox_html_node_first_element_child(vt->body), *next; row != NULL; row = next) {
            next = tbox_html_node_next_element_sibling(row);
            tbox_html_node_remove(row);
        }
        for (size_t i = 0; i < count; i++)
            tbox_html_node_append_child(vt->body, rows[i]);
        vt->first = first;
    } else {
        size_t i = 0;
        for (tbox_html_node *row = tbox_html_node_first_element_child(vt->body); row != NULL && i < count; row = tbox_html_node_next_element_sibling(row))
            rows[i++] = row;
    }
    vt->spacer_height = height;
    tbox_virtual_set_spacer(vt, 0, (double)first * height);
    tbox_virtual_set_spacer(vt, 1, (double)(vt->total - end) * height);

    /* The entering rows: above and below what stayed, or all of them. */
    size_t keep_first = old_first > first ? old_first : first, keep_end = old_end < end ? old_end : end;
    if (!moved) {
        keep_first = first;
        keep_end   = end;
    }
    if (keep_first >= keep_end) {
        tbox_virtual_ask(ctx, vt, rows, first, first, end);
    } else {
        tbox_virtual_ask(ctx, vt, rows, first, first, keep_first);
        tbox_virtual_ask(ctx, vt, rows, first, keep_end, end);
        /* Rows that stayed but whose data changed (refresh_rows). */
        size_t refresh_first = vt->refresh_first > keep_first ? vt->refresh_first : keep_first;
        size_t refresh_end   = vt->refresh_end < keep_end ? vt->refresh_end : keep_end;
        tbox_virtual_ask(ctx, vt, rows, first, refresh_first, refresh_end);
    }
    vt->refresh_first = vt->refresh_end = 0;
    free(rows);
}

/* The spacers' `style` stops pointing at the state, which goes. */
static void tbox_virtual_release(tbox_context *ctx, tbox_virtual_table *vt) {
    for (int which = 0; which < 2; which++) {
        tbox_html_node *spacer = which == 0 ? vt->top_spacer : vt->bottom_spacer;
        for (size_t i = 0; spacer != NULL && i < spacer->element.attribute_count; i++)
            if (tbox_string_view_equal_cstr(spacer->element.attributes[i].name, "style"))
                spacer->element.attributes[i].value = tbox_string_view_make(NULL, 0);
    }
    tbox_release_add(&ctx->releases, vt->destroy, vt->userdata);
    free(vt->spare);
    free(vt);
}

void tbox_context_virtual_tables_update(tbox_context *ctx) {
    if (ctx == NULL)
        return;
    for (tbox_virtual_table **link = &ctx->virtual_tables; *link != NULL;) {
        tbox_virtual_table *vt = *link;
        if (!vt->removed && tbox_context_node_attached(ctx, vt->table) && vt->body->parent == vt->table)
            tbox_virtual_update_one(ctx, vt);
        else
            vt->removed = true; /* the table left the document */
        if (vt->removed) {
            *link = vt->next;
            tbox_virtual_release(ctx, vt);
        } else {
            link = &vt->next;
        }
    }
}

void tbox_context_virtual_tables_measure(tbox_context *ctx, double viewport_height) {
    for (tbox_virtual_table *vt = ctx->virtual_tables; vt != NULL; vt = vt->next) {
        const tbox_layout_box *top    = tbox_context_find_box(ctx->root, vt->top_spacer);
        const tbox_layout_box *bottom = tbox_context_find_box(ctx->root, vt->bottom_spacer);
        if (vt->removed || top == NULL || bottom == NULL) {
            vt->measured = false;
            continue;
        }
        /* The nearest scroll container, in unscrolled coordinates (scroll
         * offsets are applied after this). */
        const tbox_layout_box *container = top->parent;
        while (container != NULL && !(container->style != NULL && container->style->overflow_y == TBOX_STYLE_OVERFLOW_Y_AUTO && container->node != NULL))
            container = container->parent;
        const tbox_html_node *container_node = container != NULL ? container->node : NULL;
        if (container_node != vt->container)
            vt->has_anchor = false;
        vt->container   = container_node;
        vt->port_height = container != NULL ? container->padding_box.height : viewport_height;
        double base     = container != NULL ? container->padding_box.y : 0.0;
        vt->table_top   = top->border_box.y - base;
        /* Scroll anchoring: the anchor row back where it was, shifted by
         * whatever scrolled since (a wheel, the scrollbar). Before
         * scrolling is applied, so this frame already shows it. */
        double scroll = vt->container != NULL ? tbox_context_scroll_offset(ctx, vt->container) : 0.0;
        if (vt->has_anchor && vt->container != NULL) {
            const tbox_html_node *anchor = tbox_context_table_virtual_row(ctx, vt->table, vt->anchor_index);
            const tbox_layout_box *box   = anchor != NULL ? tbox_context_find_box(ctx->root, anchor) : NULL;
            if (box != NULL) {
                double wanted = box->border_box.y - base - (vt->anchor_offset - (scroll - vt->anchor_scroll));
                if (fabs(wanted - scroll) > 0.5) {
                    tbox_context_scroll_set_offset(ctx, vt->container, wanted);
                    scroll = tbox_context_scroll_offset(ctx, vt->container);
                }
            }
        }
        vt->has_anchor = false;
        size_t index   = vt->first;
        for (const tbox_html_node *row = tbox_html_node_first_element_child(vt->body); row != NULL; row = tbox_html_node_next_element_sibling(row), index++) {
            const tbox_layout_box *box = tbox_context_find_box(ctx->root, row);
            if (box == NULL || box->border_box.y - base + box->border_box.height <= scroll)
                continue;
            vt->has_anchor    = true;
            vt->anchor_index  = index;
            vt->anchor_offset = box->border_box.y - base - scroll;
            vt->anchor_scroll = scroll;
            break;
        }
        /* A row's pitch: the data rows' span over their count (the
         * border-spacing between them included). */
        size_t count = tbox_virtual_row_count(vt);
        const tbox_layout_box *first_row = count > 0 ? tbox_context_find_box(ctx->root, tbox_html_node_first_element_child(vt->body)) : NULL;
        if (first_row != NULL) {
            double pitch = (bottom->border_box.y - first_row->border_box.y) / (double)count;
            if (pitch > 0.5)
                vt->row_height = pitch;
        }
        bool was_measured = vt->measured;
        vt->measured      = vt->row_height > 0.0;
        /* A window that doesn't match what is in view (the first measure,
         * a taller scrollport) moves at the next frame. */
        size_t first, end;
        tbox_virtual_desired(ctx, vt, &first, &end);
        if (!was_measured || first != vt->first || end != vt->first + count || fabs(vt->row_height - vt->spacer_height) > 0.01)
            tbox_context_wake_at(ctx, -INFINITY);
    }
}

void tbox_context_free_virtual_tables(tbox_context *ctx) {
    for (tbox_virtual_table *vt = ctx->virtual_tables; vt != NULL;) {
        tbox_virtual_table *next = vt->next;
        tbox_virtual_release(ctx, vt);
        vt = next;
    }
    ctx->virtual_tables = NULL;
}

bool tbox_context_table_virtualize(tbox_context *ctx, tbox_html_node *table, const tbox_html_node *row_template, size_t row_count,
                                   tbox_context_table_rows_handler handler, void *userdata, tbox_destroy_notify destroy) {
    if (ctx == NULL || handler == NULL || !tbox_virtual_is(table, "table") || table->owner_document != ctx->document ||
        !tbox_virtual_is(row_template, "template") || row_template->owner_document != ctx->document ||
        !tbox_virtual_is(tbox_html_node_first_element_child(row_template), "tr") || tbox_virtual_find(ctx, table) != NULL)
        return false;
    for (const tbox_html_node *at = table; at != NULL; at = at->parent)
        if (at == row_template)
            return false;
    tbox_virtual_table *vt = calloc(1, sizeof(*vt));
    if (vt == NULL)
        return false;
    /* The body rows go, and so do the <tbody>s: the three of the window
     * take their place, before the <tfoot>. */
    tbox_context_table_clear_body(ctx, table);
    tbox_html_node *foot = NULL;
    for (tbox_html_node *child = table->first_child, *next; child != NULL; child = next) {
        next = child->next_sibling;
        if (tbox_virtual_is(child, "tbody"))
            tbox_html_node_remove(child);
        else if (foot == NULL && tbox_virtual_is(child, "tfoot"))
            foot = child;
    }
    vt->table        = table;
    vt->row_template = row_template;
    vt->total        = row_count;
    vt->top          = tbox_virtual_section(ctx, table, foot, &vt->top_spacer);
    vt->body         = tbox_virtual_section(ctx, table, foot, NULL);
    vt->bottom       = tbox_virtual_section(ctx, table, foot, &vt->bottom_spacer);
    if (vt->top == NULL || vt->body == NULL || vt->bottom == NULL || vt->top_spacer == NULL || vt->bottom_spacer == NULL) {
        free(vt);
        return false;
    }
    tbox_virtual_set_spacer(vt, 0, 0.0);
    tbox_virtual_set_spacer(vt, 1, 0.0);
    vt->handler         = handler;
    vt->userdata        = userdata;
    vt->destroy         = destroy;
    vt->next            = ctx->virtual_tables;
    ctx->virtual_tables = vt;
    tbox_context_wake_at(ctx, -INFINITY);
    return true;
}

bool tbox_context_table_set_row_total(tbox_context *ctx, tbox_html_node *table, size_t row_count) {
    tbox_virtual_table *vt = tbox_virtual_find(ctx, table);
    if (vt == NULL)
        return false;
    vt->total = row_count;
    tbox_context_wake_at(ctx, -INFINITY);
    return true;
}

bool tbox_context_table_refresh_rows(tbox_context *ctx, tbox_html_node *table, size_t first, size_t count) {
    tbox_virtual_table *vt = tbox_virtual_find(ctx, table);
    if (vt == NULL)
        return false;
    size_t end = count > SIZE_MAX - first ? SIZE_MAX : first + count;
    if (vt->refresh_first >= vt->refresh_end) {
        vt->refresh_first = first;
        vt->refresh_end   = end;
    } else {
        if (first < vt->refresh_first) vt->refresh_first = first;
        if (end > vt->refresh_end) vt->refresh_end = end;
    }
    tbox_context_wake_at(ctx, -INFINITY);
    return true;
}

tbox_html_node *tbox_context_table_virtual_row(tbox_context *ctx, const tbox_html_node *table, size_t index) {
    tbox_virtual_table *vt = tbox_virtual_find(ctx, table);
    if (vt == NULL || index < vt->first)
        return NULL;
    size_t at = vt->first;
    for (tbox_html_node *row = tbox_html_node_first_element_child(vt->body); row != NULL; row = tbox_html_node_next_element_sibling(row), at++)
        if (at == index)
            return row;
    return NULL;
}

ptrdiff_t tbox_context_table_virtual_index(tbox_context *ctx, const tbox_html_node *row) {
    if (ctx == NULL || row == NULL)
        return -1;
    for (tbox_virtual_table *vt = ctx->virtual_tables; vt != NULL; vt = vt->next) {
        if (vt->removed || row->parent != vt->body)
            continue;
        size_t at = vt->first;
        for (const tbox_html_node *child = tbox_html_node_first_element_child(vt->body); child != NULL; child = tbox_html_node_next_element_sibling(child), at++)
            if (child == row)
                return (ptrdiff_t)at;
    }
    return -1;
}

bool tbox_context_table_scroll_to_row(tbox_context *ctx, tbox_html_node *table, size_t index) {
    tbox_virtual_table *vt = tbox_virtual_find(ctx, table);
    if (vt == NULL || index >= vt->total || !vt->measured || vt->container == NULL)
        return false;
    tbox_context_scroll_set_offset(ctx, vt->container, vt->table_top + (double)index * vt->row_height);
    return true;
}

bool tbox_context_table_unvirtualize(tbox_context *ctx, tbox_html_node *table) {
    tbox_virtual_table *vt = tbox_virtual_find(ctx, table);
    if (vt == NULL)
        return false;
    /* The rows in view stay as ordinary rows; the spacers go. */
    tbox_html_node_remove(vt->top);
    tbox_html_node_remove(vt->bottom);
    vt->removed = true; /* freed by the next update, after a running handler */
    tbox_context_wake_at(ctx, -INFINITY);
    return true;
}
