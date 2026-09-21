#include <tbox/context.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tbox/css_parser.h>
#include <tbox/css_selector.h>
#include <tbox/html_parser.h>
#include <tbox/style.h>

#include "base/tbox_arena.h"
#include "base/tbox_string.h"
#include "base/tbox_vector.h"
#include "context/tbox_context_hit_test.h"

/* One tbox_context_on_click registration: a compiled selector-group plus
 * the handler/userdata to fire when some ancestor of a clicked node
 * matches it. Lives in ctx->handlers (see below).
 *
 * NOVO v3 -- Interatividade Avançada: `id` is the opaque handle
 * tbox_context_on_click hands back (see ctx->next_handler_id below); `active`
 * is a tombstone flag -- tbox_context_unbind_click sets it to false and
 * destroys `query` (setting it to NULL) rather than physically removing the
 * slot from ctx->handlers, since tbox_vector has no removal primitive and
 * this is a small UI-sized array where a dead slot costs nothing measurable
 * (same "linear scan is fine" precedent already used elsewhere in this
 * module). tbox_context_dispatch_click skips any binding with active ==
 * false; tbox_context_close destroys whatever `query` is still non-NULL
 * (an unbound binding's query is already destroyed and NULLed, so it is
 * never double-destroyed there). */
typedef struct tbox_context_click_binding {
    tbox_css_selector_query *query;
    tbox_context_click_handler handler;
    void *userdata;
    int id;
    bool active;
} tbox_context_click_binding;

/* See <tbox/context.h> for why this is opaque rather than a plain/visible
 * struct like tbox_layout_box: frame_arena is a tbox_arena BY VALUE, and
 * tbox_arena's full definition lives in the internal src/base/tbox_arena.h,
 * not under include/tbox/. */
struct tbox_context {
    tbox_html_document *document;       /* owned: parsed in tbox_context_open, destroyed in tbox_context_close */
    tbox_css_stylesheet *stylesheet;    /* owned, same lifecycle; author-only (see tbox_context_run_frame) */
    tbox_css_stylesheet *ua_stylesheet; /* NOVO v2: owned, same lifecycle -- generated from a tbox_ua_style_config and parsed once in tbox_context_open_with_config, TBOX_CSS_ORIGIN_USER_AGENT in tbox_context_run_frame's cascade */
    tbox_css_stylesheet *internal_stylesheet; /* NOVO v9: owned, same lifecycle as `stylesheet` -- NULL se o documento não tem nenhum <style>; concatenação de todo <style> encontrado na árvore, mesma origem TBOX_CSS_ORIGIN_AUTHOR que `stylesheet` em tbox_context_run_frame */
    tbox_font_face_cache *fonts;        /* borrowed -- built/destroyed by the caller, never by tbox_context (NOVO v2: was a single tbox_font_face) */
    tbox_layout_box *root;              /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;             /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;           /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_on_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;               /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
    int next_handler_id;                /* NOVO v3: monotonic counter for tbox_context_on_click's returned handle -- never reused, even after tbox_context_unbind_click removes a binding */
    const tbox_html_node *hovered_node; /* NOVO v3: the node currently under the pointer, or NULL -- a plain struct field with its own lifetime, deliberately NOT part of frame_arena (must survive every tbox_context_run_frame's arena reset so tbox_context_update_hover can compare across frames; see <tbox/context.h>) */
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
    config.margin.list_px       = 16.0; /* NOVO v8: same as paragraph_px -- 1em at the default 16px base_px */
    config.margin.hr_px         = 8.0;  /* NOVO v11: ~0.5em at the default 16px base_px, approximating the `margin-block: 0.5em` real browsers use for <hr> */

    config.list_padding_left_px = 40.0; /* NOVO v8: classic list indentation used by every real browser */
    config.hr_height_px         = 2.0;  /* NOVO v11: <hr>'s explicit height */

    return config;
}

/* Generous enough for the fixed template below with any finite double
 * formatted via "%g" (at most a couple dozen significant characters) in
 * every one of its 19 slots (NOVO v8: was 14/15 before the two ul/ol/li
 * lines below added 2 more %g slots -- bumped from 1024 to 2048 so the
 * worst case, ~24 chars/slot times 17 slots plus the fixed template text,
 * still has real headroom instead of landing right at the old buffer's
 * edge; NOVO v11: the hr line below added 2 more %g slots, 17 -> 19 --
 * worst case is now ~24 chars/slot * 19 slots + ~804 chars of fixed
 * template text = ~1260 chars, still well within 2048, so the buffer did
 * NOT need to grow again this time -- checked, not assumed) -- sized with
 * headroom rather than computed exactly. */
#define TBOX_UA_STYLE_CSS_BUFFER_SIZE 2048

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
        "ul, ol { display: block; margin: %gpx 0px; padding: 0px 0px 0px %gpx; }\n"
        "li { display: block; }\n"
        "hr { display: block; height: %gpx; background-color: gray; margin: %gpx 0px; }\n"
        "b, strong { display: inline; font-weight: bold; }\n"
        "i, em, span, a { display: inline; }\n",
        config.margin.body_px, config.font.base_px,
        config.font.heading_em[0], config.margin.heading_px[0],
        config.font.heading_em[1], config.margin.heading_px[1],
        config.font.heading_em[2], config.margin.heading_px[2],
        config.font.heading_em[3], config.margin.heading_px[3],
        config.font.heading_em[4], config.margin.heading_px[4],
        config.font.heading_em[5], config.margin.heading_px[5],
        config.margin.paragraph_px,
        config.margin.list_px, config.list_padding_left_px,
        config.hr_height_px, config.margin.hr_px);

    return written >= 0 && (size_t)written < buffer_size;
}

/* NOVO v9: pre-order traversal of the WHOLE document tree (starting at
 * tbox_html_document_root -- the real root, which may have several
 * top-level children such as <head> and <body>, not the single "first
 * top-level element" tbox_layout_build isolates for itself in a separate
 * layer -- see ARCHITECTURE.md's v9 "Escopo"), appending the raw text
 * content of every <style> element found onto `builder`, in document
 * order. tbox_html_node_text_content already returns raw (non-entity-
 * decoded) text for a raw-text element like <style>, which is exactly what
 * CSS text needs. Pure function: reads the tree, writes only to `builder`
 * (a parameter) -- no global/static state. */
static void tbox_context_collect_style_elements(tbox_arena *arena, const tbox_html_node *node, tbox_string_builder *builder) {
    if (node == NULL) {
        return;
    }

    if (node->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(node->element.tag_name, "style")) {
        tbox_string_view text = tbox_html_node_text_content(arena, node);
        tbox_string_builder_append_view(builder, text);
    }

    for (const tbox_html_node *child = node->first_child; child != NULL; child = child->next_sibling) {
        tbox_context_collect_style_elements(arena, child, builder);
    }
}

tbox_context *tbox_context_open_with_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_ua_style_config config) {
    tbox_html_document *document = tbox_html_parse(html, html_length);
    if (document == NULL) {
        return NULL;
    }

    /* NOVO v9: gathers every <style> element's raw text from the WHOLE
     * document (not just what Layout later renders) into one concatenated
     * buffer, parsed as a single extra author stylesheet below. `scratch`
     * only needs to survive long enough for tbox_css_parse to copy the
     * concatenated text into its own arena -- destroyed right after. */
    tbox_arena scratch = tbox_arena_create(0);
    tbox_string_builder style_builder;
    tbox_string_builder_init(&style_builder, &scratch, 0);
    tbox_context_collect_style_elements(&scratch, tbox_html_document_root(document), &style_builder);
    tbox_string_view internal_css_text = tbox_string_builder_finish(&style_builder);

    tbox_css_stylesheet *internal_stylesheet = NULL;
    if (internal_css_text.size > 0) {
        internal_stylesheet = tbox_css_parse(internal_css_text.data, internal_css_text.size);
        if (internal_stylesheet == NULL) {
            tbox_arena_destroy(&scratch);
            tbox_html_document_destroy(document);
            return NULL;
        }
    }
    tbox_arena_destroy(&scratch);

    tbox_css_stylesheet *stylesheet = tbox_css_parse(css, css_length);
    if (stylesheet == NULL) {
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    char ua_css_text[TBOX_UA_STYLE_CSS_BUFFER_SIZE];
    if (!tbox_ua_style_generate_css(config, ua_css_text, sizeof(ua_css_text))) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_css_stylesheet *ua_stylesheet = tbox_css_parse(ua_css_text, strlen(ua_css_text));
    if (ua_stylesheet == NULL) {
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    tbox_context *ctx = (tbox_context *)malloc(sizeof(tbox_context));
    if (ctx == NULL) {
        tbox_css_stylesheet_destroy(ua_stylesheet);
        tbox_css_stylesheet_destroy(stylesheet);
        tbox_css_stylesheet_destroy(internal_stylesheet);
        tbox_html_document_destroy(document);
        return NULL;
    }

    ctx->document            = document;
    ctx->stylesheet          = stylesheet;
    ctx->ua_stylesheet       = ua_stylesheet;
    ctx->internal_stylesheet = internal_stylesheet;
    ctx->fonts               = fonts;
    ctx->root                = NULL;
    ctx->frame_arena         = tbox_arena_create(0);
    ctx->handler_arena       = tbox_arena_create(0);
    tbox_vector_init(&ctx->handlers, &ctx->handler_arena, sizeof(tbox_context_click_binding), 0);
    ctx->next_handler_id = 0;
    ctx->hovered_node    = NULL;

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
     * which only backs the `handlers` vector itself. NOVO v3: a binding
     * already removed via tbox_context_unbind_click has query == NULL (its
     * query was destroyed there) -- skip it here to avoid a double
     * destroy. */
    size_t handler_count = tbox_vector_length(&ctx->handlers);
    for (size_t i = 0; i < handler_count; i++) {
        tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_at(&ctx->handlers, i);
        if (binding->query != NULL) {
            tbox_css_selector_query_destroy(binding->query);
        }
    }
    tbox_arena_destroy(&ctx->handler_arena);

    tbox_css_stylesheet_destroy(ctx->ua_stylesheet);
    tbox_css_stylesheet_destroy(ctx->stylesheet);
    tbox_css_stylesheet_destroy(ctx->internal_stylesheet);
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

    /* NOVO v3: must happen before every tbox_style_resolve_tree call,
     * unconditionally (not only on ticks where the hover state actually
     * changed) -- the cascade needs to see the CURRENT hover state every
     * time it resolves styles, not just as of whenever it last changed.
     * See tbox_context_update_hover and <tbox/css_selector.h>'s
     * tbox_css_selector_set_hover_context for the full sequencing
     * contract this call fulfills. */
    tbox_css_selector_set_hover_context(ctx->hovered_node);

    /* NOVO v2: three cascade sources -- the user-agent stylesheet, the
     * external author stylesheet, and (NOVO v9) the internal stylesheet
     * assembled from every <style> element found in the document -- replacing
     * the 1-element placeholder array TASKS.md's Tarefa 1/Tarefa 3 left here.
     * Order in this array does not affect cascade priority BETWEEN DIFFERENT
     * origins (tbox_css_cascade_resolve already ranks by origin internally
     * regardless of array order) -- true for USER_AGENT vs. AUTHOR here, as
     * before. It is NO LONGER true between `stylesheet` and
     * `internal_stylesheet` specifically: both carry the SAME origin
     * (TBOX_CSS_ORIGIN_AUTHOR), so when a property ties in specificity
     * between the two, tbox_css_cascade_wins_or_ties's ">=" tie-break makes
     * whichever comes LAST in this array win -- `internal_stylesheet` is
     * placed after `stylesheet` on purpose, so an embedded <style> wins ties
     * against the external CSS (see ARCHITECTURE.md's v9 "Escopo"). */
    tbox_css_cascade_source sources[3] = {
        { ctx->ua_stylesheet, TBOX_CSS_ORIGIN_USER_AGENT },
        { ctx->stylesheet, TBOX_CSS_ORIGIN_AUTHOR },
        { ctx->internal_stylesheet, TBOX_CSS_ORIGIN_AUTHOR },
    };
    tbox_style_table styles = tbox_style_resolve_tree(&ctx->frame_arena, root, sources, 3);

    /* NULL for an empty document (e.g. no ELEMENT to lay out) -- tracked
     * so tbox_context_hit_test has something to search (or not) between
     * frames. */
    ctx->root = tbox_layout_build(&ctx->frame_arena, root, &styles, ctx->fonts, viewport_width, viewport_height);

    /* tbox_render_build_display_list already treats a NULL root as "empty
     * subtree", producing {NULL, 0} -- no special-casing needed here. */
    *out_list = tbox_render_build_display_list(&ctx->frame_arena, ctx->root);
}

/* Recursive part of tbox_context_hit_test.
 *
 * v0-v4 assumed "if box's border_box doesn't contain (x, y), no descendant
 * of box can contain it either" -- true only while every box stays fully
 * nested inside its DOM parent's border_box, which is what plain block flow
 * guarantees. From v5 on (`position: absolute`/`fixed`, and already true
 * today via v4 negative margins) that guarantee is gone: an out-of-flow
 * descendant can be positioned entirely outside its own parent's
 * border_box on purpose. So this always visits every child first,
 * regardless of whether `box` itself contains the point, and only falls
 * back to checking `box` once none of them matched.
 *
 * Overlap is also now possible between two boxes that are not
 * ancestor/descendant of each other (e.g. two positioned siblings, or a
 * sibling and an absolute box that escaped its parent). The project has no
 * stacking context/z-index, so paint order is always document order
 * (tbox_render_build_display_list walks the tree pre-order) -- whichever
 * box comes later in `next_sibling` order is painted on top. To match that
 * visually, among the children whose recursive hit-test matched, this
 * keeps the LAST one instead of returning on the first match. For v0-v4
 * content (no intentional overlap), at most one child ever matches at a
 * time, so this is behavior-preserving there -- zero regression.
 *
 * Not `static`: declared in tbox_context_hit_test.h (not <tbox/context.h>
 * -- still not part of the public API) so tests/context/test_context.c can
 * drive this recursion directly against a hand-built tbox_layout_box tree,
 * without going through the opaque tbox_context/the whole Style+Layout
 * pipeline just to get overlapping or out-of-parent-bounds geometry. */
const tbox_layout_box *tbox_context_hit_test_box(const tbox_layout_box *box, double x, double y) {
    if (box == NULL) {
        return NULL;
    }

    const tbox_layout_box *last_hit = NULL;
    for (const tbox_layout_box *child = box->first_child; child != NULL; child = child->next_sibling) {
        const tbox_layout_box *hit = tbox_context_hit_test_box(child, x, y);
        if (hit != NULL) {
            last_hit = hit;
        }
    }

    if (last_hit != NULL) {
        return last_hit;
    }

    tbox_rect r = box->border_box;
    if (x < r.x || x >= r.x + r.width || y < r.y || y >= r.y + r.height) {
        return NULL;
    }

    return box;
}

const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return NULL;
    }
    return tbox_context_hit_test_box(ctx->root, x, y);
}

bool tbox_context_update_hover(tbox_context *ctx, bool has_position, double x, double y) {
    if (ctx == NULL) {
        return false;
    }

    /* `has_position == false` (pointer left the window) or nothing under
     * the point both mean "nothing hovered" -- new_hovered stays NULL in
     * either case. */
    const tbox_html_node *new_hovered = NULL;
    if (has_position) {
        const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
        if (box != NULL) {
            new_hovered = box->node;
        }
    }

    if (new_hovered == ctx->hovered_node) {
        return false;
    }

    ctx->hovered_node = new_hovered;
    return true;
}

int tbox_context_on_click(tbox_context *ctx, const char *selector, size_t selector_length, tbox_context_click_handler handler, void *userdata) {
    if (ctx == NULL || handler == NULL) {
        return -1;
    }

    /* Hard-fails on a syntax error (see <tbox/css_selector.h>) -- nothing
     * is registered in that case, matching the documented contract. */
    tbox_css_selector_query *query = tbox_css_selector_compile(selector, selector_length, NULL);
    if (query == NULL) {
        return -1;
    }

    tbox_context_click_binding *binding = (tbox_context_click_binding *)tbox_vector_push(&ctx->handlers);
    binding->query                      = query;
    binding->handler                    = handler;
    binding->userdata                   = userdata;
    binding->active                     = true;
    binding->id                         = ctx->next_handler_id;
    ctx->next_handler_id++;
    return binding->id;
}

bool tbox_context_unbind_click(tbox_context *ctx, int binding) {
    if (ctx == NULL) {
        return false;
    }

    size_t handler_count = tbox_vector_length(&ctx->handlers);
    for (size_t i = 0; i < handler_count; i++) {
        tbox_context_click_binding *entry = (tbox_context_click_binding *)tbox_vector_at(&ctx->handlers, i);
        if (entry->active && entry->id == binding) {
            /* Tombstone rather than physically removing the slot -- see
             * tbox_context_click_binding's doc comment above for why. */
            tbox_css_selector_query_destroy(entry->query);
            entry->query  = NULL;
            entry->active = false;
            return true;
        }
    }

    return false;
}

bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return false;
    }

    /* NULL both when there is no layout yet and when nothing is under the
     * point -- tbox_context_hit_test already covers both guards. `node` is
     * NULL only for an anonymous box (not produced by v0/v1/v2's layout,
     * but guarded defensively -- see tbox_layout_box::node). */
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    if (box == NULL || box->node == NULL) {
        return false;
    }

    bool dispatched      = false;
    size_t handler_count = tbox_vector_length(&ctx->handlers);

    /* NOVO v3: walk the ancestor chain ONCE, nearest to farthest -- real
     * bubbling order. At each level, test EVERY currently-active binding
     * (in registration order); every one that matches fires. A handler
     * returning false (stopPropagation) stops the ancestor walk
     * immediately, so no farther ancestor is even tested. */
    for (const tbox_html_node *ancestor = box->node; ancestor != NULL; ancestor = ancestor->parent) {
        bool stop_propagation = false;

        for (size_t i = 0; i < handler_count; i++) {
            const tbox_context_click_binding *binding = (const tbox_context_click_binding *)tbox_vector_at_const(&ctx->handlers, i);
            if (!binding->active) {
                continue;
            }

            if (tbox_css_selector_query_matches(binding->query, ancestor)) {
                /* Non-const cast: tbox_layout_box::node is const (layout's
                 * own read-only view), but a click handler's whole point is
                 * to be able to mutate the tree (e.g.
                 * tbox_html_node_set_attribute) -- see
                 * tbox_context_click_handler's signature. */
                bool keep_propagating = binding->handler(ctx, (tbox_html_node *)ancestor, binding->userdata);
                dispatched            = true;

                if (!keep_propagating) {
                    stop_propagation = true;
                    break;
                }
            }
        }

        if (stop_propagation) {
            break;
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
