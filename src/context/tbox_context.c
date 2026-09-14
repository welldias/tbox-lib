#include <tbox/context.h>

#include <stdlib.h>

#include <tbox/css_parser.h>
#include <tbox/html_parser.h>
#include <tbox/style.h>

#include "base/tbox_arena.h"

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

    ctx->document    = document;
    ctx->stylesheet  = stylesheet;
    ctx->font        = font;
    ctx->root        = NULL;
    ctx->frame_arena = tbox_arena_create(0);

    return ctx;
}

void tbox_context_close(tbox_context *ctx) {
    if (ctx == NULL) {
        return;
    }

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
