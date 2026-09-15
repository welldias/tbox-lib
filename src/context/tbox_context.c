#include <tbox/context.h>

#include <stdlib.h>

#include <tbox/css_parser.h>
#include <tbox/css_selector.h>
#include <tbox/html_parser.h>
#include <tbox/style.h>

#include "base/tbox_arena.h"
#include "base/tbox_vector.h"

/* One tbox_context_on_click registration: a compiled selector-group plus
 * the handler/userdata to fire when some ancestor of a clicked node
 * matches it. Lives in ctx->handlers (see below). */
typedef struct tbox_context_click_binding {
    tbox_css_selector_query *query;
    tbox_context_click_handler handler;
    void *userdata;
} tbox_context_click_binding;

/* See <tbox/context.h> for why this is opaque rather than a plain/visible
 * struct like tbox_layout_box: frame_arena is a tbox_arena BY VALUE, and
 * tbox_arena's full definition lives in the internal src/base/tbox_arena.h,
 * not under include/tbox/. */
struct tbox_context {
    tbox_html_document *document;    /* owned: parsed in tbox_context_open, destroyed in tbox_context_close */
    tbox_css_stylesheet *stylesheet; /* owned, same lifecycle; author-only (see tbox_context_run_frame) */
    tbox_font_face *font;            /* borrowed -- loaded/destroyed by the caller, never by tbox_context */
    tbox_layout_box *root;           /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;          /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;        /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_on_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;            /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
};

tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face *font) {
    tbox_html_document *document = tbox_html_parse(html, html_length);
    if (document == NULL) {
        return NULL;
    }

    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_length);
    if (stylesheet == NULL) {
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_context *ctx = (tbox_context *)malloc(sizeof(tbox_context));
    if (ctx == NULL) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    ctx->document      = document;
    ctx->stylesheet    = stylesheet;
    ctx->font          = font;
    ctx->root          = NULL;
    ctx->frame_arena   = tbox_arena_create(0);
    ctx->handler_arena = tbox_arena_create(0);
    tbox_vector_init(&ctx->handlers, &ctx->handler_arena, sizeof(tbox_context_click_binding), 0);

    return ctx;
}

void tbox_context_close(tbox_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Each binding owns its compiled query's own arena (see
     * tbox_css_selector_query_destroy) -- distinct from handler_arena,
     * which only backs the `handlers` vector itself. */
    size_t handler_count = tbox_vector_length(&ctx->handlers);
    for (size_t i = 0; i < handler_count; i++) {
        tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_at(&ctx->handlers, i);
        tbox_css_selector_query_destroy(binding->query);
    }
    tbox_arena_destroy(&ctx->handler_arena);

    tbox_css_stylesheet_destroy(ctx->stylesheet);
    tbox_html_document_destroy(ctx->document);
    tbox_arena_destroy(&ctx->frame_arena);
    free(ctx);
}

void tbox_context_run_frame(tbox_context *ctx, double viewport_width, double viewport_height, tbox_display_list *out_list) {
    /* Invalidates everything Style/Layout/Render produced last frame in
     * one shot -- no per-layer _destroy to call (see "Convenções" in
     * ARCHITECTURE.md). Must happen before ctx->root is overwritten below:
     * the old tree lives in this same arena. */
    tbox_arena_reset(&ctx->frame_arena);

    const tbox_html_node *root = tbox_html_document_root(ctx->document);

    /* v0: a single AUTHOR-origin source, no user-agent stylesheet (see
     * ARCHITECTURE.md's "Orchestration / Main Loop" section) -- that
     * policy lives inside tbox_style_resolve_tree/tbox_css_cascade_resolve,
     * not here; this call site just hands it the one stylesheet it has. */
    tbox_style_table styles = tbox_style_resolve_tree(&ctx->frame_arena, root, ctx->stylesheet);

    /* NULL for an empty document (e.g. no ELEMENT to lay out) -- tracked
     * so tbox_context_hit_test has something to search (or not) between
     * frames. */
    ctx->root = tbox_layout_build(&ctx->frame_arena, root, &styles, ctx->font, viewport_width, viewport_height);

    /* tbox_render_build_display_list already treats a NULL root as "empty
     * subtree", producing {NULL, 0} -- no special-casing needed here. */
    *out_list = tbox_render_build_display_list(&ctx->frame_arena, ctx->root);
}

/* Recursive part of tbox_context_hit_test: v0's block-flow siblings never
 * overlap, so once `box`'s border_box fails to contain (x, y), no
 * descendant of `box` can contain it either -- safe to stop without
 * visiting the rest of the subtree. */
static const tbox_layout_box *tbox_context_hit_test_box(const tbox_layout_box *box, double x, double y) {
    if (box == NULL) {
        return NULL;
    }

    tbox_rect r = box->border_box;
    if (x < r.x || x >= r.x + r.width || y < r.y || y >= r.y + r.height) {
        return NULL;
    }

    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling) {
        const tbox_layout_box *hit = tbox_context_hit_test_box(child, x, y);
        if (hit != NULL) {
            return hit;
        }
    }

    /* No child matched (or there are none) -- `box` itself is the deepest
     * match. */
    return box;
}

const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return NULL;
    }
    return tbox_context_hit_test_box(ctx->root, x, y);
}

bool tbox_context_on_click(tbox_context *ctx, const char *selector, size_t selector_length, tbox_context_click_handler handler, void *userdata) {
    if (ctx == NULL || handler == NULL) {
        return false;
    }

    /* Hard-fails on a syntax error (see <tbox/css_selector.h>) -- nothing
     * is registered in that case, matching the documented contract. */
    tbox_css_selector_query *query = tbox_css_selector_compile(selector, selector_length, NULL);
    if (query == NULL) {
        return false;
    }

    tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_push(&ctx->handlers);
    binding->query    = query;
    binding->handler  = handler;
    binding->userdata = userdata;
    return true;
}

bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return false;
    }

    /* NULL both when there is no layout yet and when nothing is under the
     * point -- tbox_context_hit_test already covers both guards. `node` is
     * NULL only for an anonymous box (not produced by v0/v1's layout, but
     * guarded defensively -- see tbox_layout_box::node). */
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    if (box == NULL || box->node == NULL) {
        return false;
    }

    bool dispatched      = false;
    size_t handler_count = tbox_vector_length(&ctx->handlers);

    /* Outer loop over registrations, in registration order -- "no firing
     * order between different registrations beyond the order they were
     * registered in" (ARCHITECTURE.md). Inner loop walks ancestors nearest
     * to farthest so each registration fires at most once, on the nearest
     * ancestor that matches it. */
    for (size_t i = 0; i < handler_count; i++) {
        const tbox_context_click_binding *binding = (const tbox_context_click_binding *)tbox_vector_at_const(&ctx->handlers, i);

        for (const tbox_html_node *ancestor = box->node; ancestor != NULL; ancestor = ancestor->parent) {
            if (tbox_css_selector_query_matches(binding->query, ancestor)) {
                /* Non-const cast: tbox_layout_box::node is const (layout's
                 * own read-only view), but a click handler's whole point is
                 * to be able to mutate the tree (e.g.
                 * tbox_html_node_set_attribute) -- see
                 * tbox_context_click_handler's signature. */
                binding->handler(ctx, (tbox_html_node *)ancestor, binding->userdata);
                dispatched = true;
                break;
            }
        }
    }

    return dispatched;
}

tbox_html_document *tbox_context_document(tbox_context *ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->document;
}
