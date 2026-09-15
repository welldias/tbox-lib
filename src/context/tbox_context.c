#include <tbox/context.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    tbox_html_document *document;       /* owned: parsed in tbox_context_open, destroyed in tbox_context_close */
    tbox_css_stylesheet *stylesheet;    /* owned, same lifecycle; author-only (see tbox_context_run_frame) */
    tbox_css_stylesheet *ua_stylesheet; /* NOVO v2: owned, same lifecycle -- generated from a tbox_ua_style_config and parsed once in tbox_context_open_with_config, TBOX_CSS_ORIGIN_USER_AGENT in tbox_context_run_frame's cascade */
    tbox_font_face_cache *fonts;        /* borrowed -- built/destroyed by the caller, never by tbox_context (NOVO v2: was a single tbox_font_face) */
    tbox_layout_box *root;              /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;             /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;           /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_on_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;               /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
};

tbox_ua_style_config tbox_ua_style_config_default(void) {
    tbox_ua_style_config config;

    config.font.base_px       = 16.0;
    config.font.heading_em[0] = 2.0;    /* h1 */
    config.font.heading_em[1] = 1.5;    /* h2 */
    config.font.heading_em[2] = 1.17;   /* h3 */
    config.font.heading_em[3] = 1.0;    /* h4 */
    config.font.heading_em[4] = 0.83;   /* h5 */
    config.font.heading_em[5] = 0.67;   /* h6 */

    config.margin.heading_px[0] = 21.0; /* h1 */
    config.margin.heading_px[1] = 19.0; /* h2 */
    config.margin.heading_px[2] = 18.0; /* h3 */
    config.margin.heading_px[3] = 21.0; /* h4 */
    config.margin.heading_px[4] = 22.0; /* h5 */
    config.margin.heading_px[5] = 25.0; /* h6 */
    config.margin.paragraph_px  = 16.0;
    config.margin.body_px       = 8.0;

    return config;
}

/* Generous enough for the fixed template below with any finite double
 * formatted via "%g" (at most a couple dozen significant characters) in
 * every one of its 14 slots -- comfortably under half this size in
 * practice; sized with headroom rather than computed exactly. */
#define TBOX_UA_STYLE_CSS_BUFFER_SIZE 1024

/* NOVO v2: renders the UA stylesheet's CSS text from `config`. The
 * selectors and properties are FIXED, exactly as ARCHITECTURE.md's "CSS
 * Cascade / Orchestration -- folha de estilo user-agent" documents -- only
 * the em/px NUMBERS vary, via config's fields. This is a template filled in
 * with snprintf, not a serializer: `config` is the source of truth, this
 * text only exists because tbox_css_parse is how new declarations enter
 * the cascade. `config.font.base_px` is emitted as `body`'s own
 * `font-size` (see the template below) -- every heading's `em` scale
 * multiplies from whatever font-size the root element resolves to, so
 * this is the one declaration that makes a non-default `base_px` actually
 * take effect; without it the field would be silently inert (see
 * tbox_ua_style_font_config::base_px's doc comment in <tbox/context.h>).
 * Writes into `buffer` (`buffer_size` bytes) and returns true, or returns
 * false (without a well-defined `buffer` contents) if the rendered text
 * would not fit -- should never happen in practice since every field is a
 * bounded double and the template itself is small and fixed.
 *
 * DEVIATION from ARCHITECTURE.md's literal illustrative CSS text: the
 * two-value margin shorthand there is shown as e.g. "margin: 21px 0" (a
 * bare, unitless "0" for left/right) -- valid CSS2.1, but
 * src/style/tbox_style.c's tbox_style_parse_length (already-merged, out of
 * this task's scope) does not accept a unitless "0": it requires a "px"/
 * "%" suffix (or the literal keyword "auto") on every token, so a bare "0"
 * fails to parse, which fails the WHOLE shorthand, which silently falls
 * back to 0px on every side -- discovered empirically while testing this
 * task, not documented anywhere prior. This template emits "0px" instead,
 * which parses correctly under the existing Style layer and matches
 * ARCHITECTURE.md's intent (zero left/right margin) exactly -- only the
 * unit suffix differs from the doc's illustrative text. Flagged in this
 * task's final report as a documentation gap worth fixing (either teach
 * tbox_style_parse_length unitless zero, matching real CSS2.1, or amend
 * ARCHITECTURE.md's illustrative block to say "0px"). */
static bool tbox_ua_style_generate_css(tbox_ua_style_config config, char *buffer, size_t buffer_size) {
    int written = snprintf(buffer, buffer_size,
        "body { display: block; margin: %gpx; font-size: %gpx; }\n"
        "div { display: block; }\n"
        "h1 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "h2 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "h3 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "h4 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "h5 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "h6 { display: block; font-size: %gem; font-weight: bold; margin: %gpx 0px; }\n"
        "p { display: block; margin: %gpx 0px; }\n"
        "b, strong { display: inline; font-weight: bold; }\n"
        "i, em, span, a { display: inline; }\n",
        config.margin.body_px, config.font.base_px,
        config.font.heading_em[0], config.margin.heading_px[0],
        config.font.heading_em[1], config.margin.heading_px[1],
        config.font.heading_em[2], config.margin.heading_px[2],
        config.font.heading_em[3], config.margin.heading_px[3],
        config.font.heading_em[4], config.margin.heading_px[4],
        config.font.heading_em[5], config.margin.heading_px[5],
        config.margin.paragraph_px);

    return written >= 0 && (size_t)written < buffer_size;
}

tbox_context *tbox_context_open_with_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_ua_style_config config) {
    tbox_html_document *document = tbox_html_parse(html, html_length);
    if (document == NULL) {
        return NULL;
    }

    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_length);
    if (stylesheet == NULL) {
        tbox_html_document_destroy(document);
        return NULL;
    }

    char ua_css_text[TBOX_UA_STYLE_CSS_BUFFER_SIZE];
    if (!tbox_ua_style_generate_css(config, ua_css_text, sizeof(ua_css_text))) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_css_stylesheet *ua_stylesheet = tbox_css_parse(ua_css_text, strlen(ua_css_text));
    if (ua_stylesheet == NULL) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_context *ctx = (tbox_context *)malloc(sizeof(tbox_context));
    if (ctx == NULL) {
        tbox_css_stylesheet_destroy(ua_stylesheet);
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    ctx->document      = document;
    ctx->stylesheet    = stylesheet;
    ctx->ua_stylesheet = ua_stylesheet;
    ctx->fonts         = fonts;
    ctx->root          = NULL;
    ctx->frame_arena   = tbox_arena_create(0);
    ctx->handler_arena = tbox_arena_create(0);
    tbox_vector_init(&ctx->handlers, &ctx->handler_arena, sizeof(tbox_context_click_binding), 0);

    return ctx;
}

tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts) {
    return tbox_context_open_with_config(html, html_length, css, css_length, fonts, tbox_ua_style_config_default());
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

    tbox_css_stylesheet_destroy(ctx->ua_stylesheet);
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

    /* NOVO v2: two cascade sources -- the user-agent stylesheet and the
     * author stylesheet -- replacing the 1-element placeholder array
     * TASKS.md's Tarefa 1/Tarefa 3 left here. Order in this array does not
     * affect cascade priority (tbox_css_cascade_resolve already ranks by
     * origin internally regardless of array order); kept UA-then-AUTHOR for
     * readability, matching the order ARCHITECTURE.md lists them in. */
    tbox_css_cascade_source sources[2] = {
        { ctx->ua_stylesheet, TBOX_CSS_ORIGIN_USER_AGENT },
        { ctx->stylesheet, TBOX_CSS_ORIGIN_AUTHOR },
    };
    tbox_style_table styles = tbox_style_resolve_tree(&ctx->frame_arena, root, sources, 2);

    /* NULL for an empty document (e.g. no ELEMENT to lay out) -- tracked
     * so tbox_context_hit_test has something to search (or not) between
     * frames. */
    ctx->root = tbox_layout_build(&ctx->frame_arena, root, &styles, ctx->fonts, viewport_width, viewport_height);

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
    binding->query                      = query;
    binding->handler                    = handler;
    binding->userdata                   = userdata;
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
