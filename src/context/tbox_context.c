#include <tbox/context.h>

#include <stdio.h>
#include <stdint.h>
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

typedef struct tbox_text_field {
    const tbox_html_node *node;
    char *value;
    size_t length, capacity, cursor, anchor;
    double scroll_x;
    struct tbox_text_field *next;
} tbox_text_field;

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
    tbox_image_cache *images;           /* borrowed, same lifecycle stance as `fonts` above -- may be NULL ("no images", see <tbox/image.h>) */
    tbox_layout_box *root;              /* last computed layout tree (lives in frame_arena); NULL until the first run_frame */
    tbox_arena frame_arena;             /* backing for tbox_style_table + tbox_layout_box + tbox_display_list; reset at the start of every run_frame */
    tbox_arena handler_arena;           /* backing for `handlers` below -- deliberately NOT frame_arena: a tbox_context_on_click registration must survive every tbox_context_run_frame's arena reset */
    tbox_vector handlers;               /* tbox_context_click_binding elements, arena-backed by handler_arena; array + linear scan on dispatch, same shape as tbox_style_table */
    int next_handler_id;                /* NOVO v3: monotonic counter for tbox_context_on_click's returned handle -- never reused, even after tbox_context_unbind_click removes a binding */
    const tbox_html_node *hovered_node; /* NOVO v3: the node currently under the pointer, or NULL -- a plain struct field with its own lifetime, deliberately NOT part of frame_arena (must survive every tbox_context_run_frame's arena reset so tbox_context_update_hover can compare across frames; see <tbox/context.h>) */
    const tbox_html_node *focused_node;
    tbox_text_field *text_fields;
    tbox_context_input_handler input_handler;
    void *input_userdata;
    tbox_style_table styles; /* last frame's styles, for focus visibility checks */
};

static bool tbox_context_node_attached(const tbox_context *ctx, const tbox_html_node *node);
static bool tbox_context_is_text_input(const tbox_html_node *node);
static tbox_text_field *tbox_context_text_field(tbox_context *ctx, const tbox_html_node *node);

static const tbox_layout_box *tbox_context_find_box(const tbox_layout_box *box, const tbox_html_node *node) {
    for (; box != NULL; box = box->next_sibling) {
        if (box->node == node) return box;
        const tbox_layout_box *child = tbox_context_find_box(box->first_child, node);
        if (child != NULL) return child;
    }
    return NULL;
}

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
 * NOT need to grow again this time -- checked, not assumed; NOVO v12: the
 * pre line below adds ~51 chars of literal template text and zero new %g
 * slots (no numeric value in it), so the worst case barely moves --
 * ~1260 -> ~1311 chars, nowhere near 2048 -- checked, buffer size left
 * unchanged) -- sized with headroom rather than computed exactly. */
#define TBOX_UA_STYLE_CSS_BUFFER_SIZE 2304

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
 * ARCHITECTURE.md's illustrative block to say "0px").
 *
 * NOVO v12 (Tarefa 3): deliberately NOT `static` (unlike every other
 * helper in this file) -- tests/context/test_context.c's `<pre>`
 * font-family test needs to resolve a node's REAL tbox_style against the
 * actual production UA CSS text this function emits, not a hand-copied
 * reimplementation of the template that could silently drift from it and
 * test nothing about tbox_context.c itself. Same "give an internal
 * function external linkage so a test can call it directly" precedent
 * tbox_context_hit_test_box already established (see
 * tbox_context_hit_test.h) -- declared with a plain forward declaration
 * directly in test_context.c instead of a shared header, since this
 * task's file scope is only tbox_context.c + test_context.c. No behavior
 * change: still a pure function of `config`/`buffer`/`buffer_size`. */
bool tbox_ua_style_generate_css(tbox_ua_style_config config, char *buffer, size_t buffer_size) {
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
        "button { display: block; border: 1px solid gray; padding: 4px; }\n"
        "button:focus { border: 2px solid blue; }\n"
        "input { display: block; width: 240px; border: 1px solid gray; padding: 4px; }\n"
        "input:focus { border: 2px solid blue; }\n"
        "hr { display: block; height: %gpx; background-color: gray; margin: %gpx 0px; }\n"
        "pre { display: block; font-family: monospace; }\n"
        "b, strong { display: inline; font-weight: bold; }\n"
        "i, em { display: inline; font-style: italic; }\n"
        "span { display: inline; }\n"
        "a { display: inline; color: blue; text-decoration: underline; }\n"
        "img { display: inline; }\n"
        "small { display: inline; font-size: 80%%; }\n"
        "mark { display: inline; background-color: yellow; }\n"
        "del { display: inline; text-decoration: line-through; }\n"
        "ins { display: inline; text-decoration: underline; }\n"
        "sub { display: inline; font-size: 75%%; vertical-align: sub; }\n"
        "sup { display: inline; font-size: 75%%; vertical-align: super; }\n"
        "table, tr { display: block; }\n"
        "th { font-weight: bold; text-align: center; }\n"
        "td, th { padding: 4px; }\n",
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

tbox_context *tbox_context_open_with_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_ua_style_config config) {
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
    ctx->images              = images;
    ctx->root                = NULL;
    ctx->frame_arena         = tbox_arena_create(0);
    ctx->handler_arena       = tbox_arena_create(0);
    tbox_vector_init(&ctx->handlers, &ctx->handler_arena, sizeof(tbox_context_click_binding), 0);
    ctx->next_handler_id = 0;
    ctx->hovered_node    = NULL;
    ctx->focused_node    = NULL;
    ctx->text_fields     = NULL;
    ctx->input_handler   = NULL;
    ctx->input_userdata  = NULL;
    ctx->styles          = (tbox_style_table){0};

    return ctx;
}

tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images) {
    return tbox_context_open_with_config(html, html_length, css, css_length, fonts, images, tbox_ua_style_config_default());
}

void tbox_context_close(tbox_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    for (tbox_text_field *field = ctx->text_fields; field != NULL;) {
        tbox_text_field *next = field->next;
        free(field->value);
        free(field);
        field = next;
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
    ctx->styles = (tbox_style_table){0};

    if (ctx->focused_node != NULL && !tbox_context_node_attached(ctx, ctx->focused_node)) {
        ctx->focused_node = NULL;
    }

    const tbox_html_node *root = tbox_html_document_root(ctx->document);

    /* NOVO v3: must happen before every tbox_style_resolve_tree call,
     * unconditionally (not only on ticks where the hover state actually
     * changed) -- the cascade needs to see the CURRENT hover state every
     * time it resolves styles, not just as of whenever it last changed.
     * See tbox_context_update_hover and <tbox/css_selector.h>'s
     * tbox_css_selector_set_hover_context for the full sequencing
     * contract this call fulfills. */
    tbox_css_selector_set_hover_context(ctx->hovered_node);
    tbox_css_selector_set_focus_context(ctx->focused_node);

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
    ctx->styles = styles;

    /* NULL for an empty document (e.g. no ELEMENT to lay out) -- tracked
     * so tbox_context_hit_test has something to search (or not) between
     * frames. */
    ctx->root = tbox_layout_build(&ctx->frame_arena, root, &styles, ctx->fonts, ctx->images, viewport_width, viewport_height);

    /* tbox_render_build_display_list already treats a NULL root as "empty
     * subtree", producing {NULL, 0} -- no special-casing needed here. */
    *out_list = tbox_render_build_display_list(&ctx->frame_arena, ctx->root);
    if (tbox_context_is_text_input(ctx->focused_node)) {
        const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
        tbox_text_field *field = box != NULL ? tbox_context_text_field(ctx, ctx->focused_node) : NULL;
        if (field != NULL && box->style != NULL && box->content_box.width > 0) {
            const tbox_style *style = box->style;
            const tbox_font_face *face = tbox_font_face_cache_get(
                ctx->fonts, tbox_string_view_from_cstr(style->font_family),
                style->font_weight_bold, style->font_italic, style->font_size);
            if (face != NULL) {
                double cursor_x = tbox_font_measure_text(face,
                    tbox_string_view_make(field->value, field->cursor));
                double visible_width = box->content_box.width - 1.0;
                if (visible_width < 0.0) visible_width = 0.0;
                if (cursor_x < field->scroll_x) field->scroll_x = cursor_x;
                if (cursor_x > field->scroll_x + visible_width)
                    field->scroll_x = cursor_x - visible_width;
                double text_width = tbox_font_measure_text(face,
                    tbox_string_view_make(field->value, field->length));
                double max_scroll = text_width - visible_width;
                if (max_scroll < 0.0) max_scroll = 0.0;
                if (field->scroll_x > max_scroll) field->scroll_x = max_scroll;
                double x = box->content_box.x + cursor_x - field->scroll_x;
                double height = tbox_font_face_line_height(face);
                if (height > box->content_box.height) height = box->content_box.height;
                tbox_paint_op *items = tbox_arena_alloc(&ctx->frame_arena,
                    (out_list->count + 2) * sizeof(*items));
                if (items != NULL) {
                    size_t count = 0;
                    for (size_t i = 0; i < out_list->count; i++) {
                        tbox_paint_op op = out_list->items[i];
                        bool input_text = op.kind == TBOX_PAINT_TEXT_RUN &&
                            op.has_clip && op.clip.x == box->content_box.x &&
                            op.clip.y == box->content_box.y &&
                            op.text.data != NULL && box->text_run_count > 0 &&
                            op.text.data == box->text_runs[0].text.data;
                        if (input_text) {
                            op.rect.x -= field->scroll_x;
                            if (field->anchor != field->cursor) {
                                size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
                                size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
                                double left = box->content_box.x - field->scroll_x +
                                    tbox_font_measure_text(face, tbox_string_view_make(field->value, start));
                                double right = box->content_box.x - field->scroll_x +
                                    tbox_font_measure_text(face, tbox_string_view_make(field->value, end));
                                if (left < box->content_box.x) left = box->content_box.x;
                                if (right > box->content_box.x + box->content_box.width)
                                    right = box->content_box.x + box->content_box.width;
                                if (right > left) items[count++] = (tbox_paint_op){
                                    .kind = TBOX_PAINT_FILL_RECT,
                                    .rect = { left, box->content_box.y, right - left, height },
                                    .color = { 130, 175, 235, 255 },
                                };
                            }
                        }
                        items[count++] = op;
                    }
                    double caret_width = box->content_box.width < 1.0 ? box->content_box.width : 1.0;
                    items[count++] = (tbox_paint_op){
                        .kind = TBOX_PAINT_FILL_RECT,
                        .rect = { x, box->content_box.y, caret_width, height },
                        .color = style->color,
                    };
                    out_list->items = items;
                    out_list->count = count;
                }
            }
        }
    }
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

static bool tbox_context_node_attached(const tbox_context *ctx, const tbox_html_node *node) {
    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    while (node != NULL && node->parent != NULL) {
        node = node->parent;
    }
    return node == root;
}

static const tbox_html_node *tbox_context_next_node(const tbox_html_node *root, const tbox_html_node *node) {
    if (node->first_child != NULL) {
        return node->first_child;
    }
    while (node != root) {
        if (node->next_sibling != NULL) {
            return node->next_sibling;
        }
        node = node->parent;
    }
    return NULL;
}

static bool tbox_context_is_text_input(const tbox_html_node *node) {
    if (node == NULL || node->type != TBOX_HTML_NODE_ELEMENT ||
        !tbox_string_view_equal_cstr(node->element.tag_name, "input")) {
        return false;
    }
    const tbox_html_attribute *type = tbox_html_node_get_attribute(node, tbox_string_view_make("type", 4));
    return type == NULL || tbox_string_view_equal_ascii_ci(type->value, tbox_string_view_make("text", 4));
}

static void tbox_context_bind_text_value(tbox_context *ctx, tbox_text_field *field) {
    /* Keep the DOM view tied to the field's owned buffer. In particular,
     * bind the initial HTML value before a cursor-only key event arrives. */
    tbox_html_attribute *attribute = (tbox_html_attribute *)tbox_html_node_get_attribute(
        field->node, tbox_string_view_make("value", 5));
    if (attribute == NULL) {
        tbox_html_node_set_attribute(ctx->document, (tbox_html_node *)field->node,
                                     tbox_string_view_make("value", 5),
                                     tbox_string_view_make(field->value, field->length));
        attribute = (tbox_html_attribute *)tbox_html_node_get_attribute(field->node, tbox_string_view_make("value", 5));
    }
    if (attribute != NULL) attribute->value = tbox_string_view_make(field->value, field->length);
}

static tbox_text_field *tbox_context_text_field(tbox_context *ctx, const tbox_html_node *node) {
    for (tbox_text_field *field = ctx->text_fields; field != NULL; field = field->next) {
        if (field->node == node) {
            const tbox_html_attribute *attribute = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
            if (attribute != NULL && attribute->value.data != field->value) {
                if (attribute->value.size == SIZE_MAX) return NULL;
                char *copy = realloc(field->value, attribute->value.size + 1);
                if (copy == NULL) return NULL;
                field->value = copy;
                field->capacity = attribute->value.size + 1;
                if (attribute->value.size > 0) {
                    memcpy(field->value, attribute->value.data, attribute->value.size);
                }
                field->length = field->cursor = field->anchor = attribute->value.size;
                field->scroll_x = 0.0;
                field->value[field->length] = '\0';
                tbox_context_bind_text_value(ctx, field);
            }
            return field;
        }
    }
    const tbox_html_attribute *initial = tbox_html_node_get_attribute(node, tbox_string_view_make("value", 5));
    size_t length = initial != NULL ? initial->value.size : 0;
    if (length == SIZE_MAX) return NULL;
    tbox_text_field *field = calloc(1, sizeof(*field));
    if (field == NULL) return NULL;
    field->value = malloc(length + 1);
    if (field->value == NULL) {
        free(field);
        return NULL;
    }
    if (length > 0) memcpy(field->value, initial->value.data, length);
    field->value[length] = '\0';
    field->node = node;
    field->length = field->cursor = field->anchor = length;
    field->capacity = length + 1;
    field->next = ctx->text_fields;
    ctx->text_fields = field;
    tbox_context_bind_text_value(ctx, field);
    return field;
}

static void tbox_context_sync_text_value(tbox_context *ctx, tbox_text_field *field) {
    /* The field owns its reusable buffer. The DOM view points at it so
     * layout and application code see the live value without an arena
     * allocation for every keystroke. */
    tbox_context_bind_text_value(ctx, field);
    if (ctx->input_handler != NULL) {
        ctx->input_handler(ctx, (tbox_html_node *)field->node,
                           tbox_string_view_make(field->value, field->length), ctx->input_userdata);
    }
}

static size_t tbox_utf8_next(const char *data, size_t length, size_t at) {
    if (at >= length) return length;
    size_t width = (unsigned char)data[at] < 0x80 ? 1 :
                   ((unsigned char)data[at] & 0xe0) == 0xc0 ? 2 :
                   ((unsigned char)data[at] & 0xf0) == 0xe0 ? 3 :
                   ((unsigned char)data[at] & 0xf8) == 0xf0 ? 4 : 0;
    if (width == 0 || width > length - at) return at;
    for (size_t i = 1; i < width; i++) {
        if (((unsigned char)data[at + i] & 0xc0) != 0x80) return at;
    }
    unsigned char b = (unsigned char)data[at];
    unsigned char b1 = width > 1 ? (unsigned char)data[at + 1] : 0;
    if ((width == 2 && b < 0xc2) ||
        (width == 3 && ((b == 0xe0 && b1 < 0xa0) || (b == 0xed && b1 >= 0xa0))) ||
        (width == 4 && ((b == 0xf0 && b1 < 0x90) || (b == 0xf4 && b1 >= 0x90) || b > 0xf4))) return at;
    return at + width;
}

static size_t tbox_utf8_previous(const char *data, size_t at) {
    if (at == 0) return 0;
    at--;
    while (at > 0 && ((unsigned char)data[at] & 0xc0) == 0x80) at--;
    return at;
}

static size_t tbox_context_cursor_at_x(const tbox_text_field *field, const tbox_layout_box *box,
                                       const tbox_font_face *face, double x) {
    double relative_x = x - box->content_box.x + field->scroll_x;
    size_t best = 0;
    for (size_t at = 0; at < field->length;) {
        size_t next = tbox_utf8_next(field->value, field->length, at);
        if (next == at) break;
        double advance = tbox_font_measure_text(face,
            tbox_string_view_make(field->value, next));
        if (relative_x < advance) {
            double previous = tbox_font_measure_text(face,
                tbox_string_view_make(field->value, at));
            return relative_x - previous < advance - relative_x ? at : next;
        }
        best = next;
        at = next;
    }
    return best;
}

static bool tbox_context_focusable(const tbox_context *ctx, const tbox_html_node *node) {
    if (node->type != TBOX_HTML_NODE_ELEMENT) {
        return false;
    }

    tbox_string_view tag = node->element.tag_name;
    bool control = tbox_string_view_equal_cstr(tag, "button") || tbox_context_is_text_input(node);
    if (!control || tbox_html_node_get_attribute(node, tbox_string_view_make("disabled", 8)) != NULL) {
        return false;
    }

    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent) {
        if (ancestor->type != TBOX_HTML_NODE_ELEMENT) {
            continue;
        }
        if (tbox_html_node_get_attribute(ancestor, tbox_string_view_make("hidden", 6)) != NULL) {
            return false;
        }
        const tbox_style *style = tbox_style_table_find(&ctx->styles, ancestor);
        if (style != NULL && style->display == TBOX_STYLE_DISPLAY_NONE) {
            return false;
        }
    }
    return true;
}

static bool tbox_context_set_focus(tbox_context *ctx, const tbox_html_node *node) {
    if (ctx->focused_node == node) {
        return false;
    }
    ctx->focused_node = node;
    tbox_css_selector_set_focus_context(node);
    return true;
}

static bool tbox_context_move_focus(tbox_context *ctx, bool reverse) {
    const tbox_html_node *root = tbox_html_document_root(ctx->document);
    const tbox_html_node *current = ctx->focused_node;
    if (current != NULL && (!tbox_context_node_attached(ctx, current) || !tbox_context_focusable(ctx, current))) {
        current = NULL;
    }

    const tbox_html_node *first = NULL;
    const tbox_html_node *last = NULL;
    const tbox_html_node *before = NULL;
    const tbox_html_node *after = NULL;
    bool seen_current = false;
    for (const tbox_html_node *node = root; node != NULL; node = tbox_context_next_node(root, node)) {
        if (!tbox_context_focusable(ctx, node)) {
            continue;
        }
        if (first == NULL) first = node;
        last = node;
        if (node == current) {
            seen_current = true;
        } else if (!seen_current) {
            before = node;
        } else if (after == NULL) {
            after = node;
        }
    }

    const tbox_html_node *next = reverse ? (current == NULL ? last : (before != NULL ? before : last))
                                         : (current == NULL ? first : (after != NULL ? after : first));
    return tbox_context_set_focus(ctx, next);
}

static bool tbox_context_dispatch_click_node(tbox_context *ctx, const tbox_html_node *node) {
    tbox_css_selector_set_hover_context(ctx->hovered_node);
    tbox_css_selector_set_focus_context(ctx->focused_node);
    bool dispatched      = false;
    size_t handler_count = tbox_vector_length(&ctx->handlers);

    /* NOVO v3: walk the ancestor chain ONCE, nearest to farthest -- real
     * bubbling order. At each level, test EVERY currently-active binding
     * (in registration order); every one that matches fires. A handler
     * returning false (stopPropagation) stops the ancestor walk
     * immediately, so no farther ancestor is even tested. */
    for (const tbox_html_node *ancestor = node; ancestor != NULL; ancestor = ancestor->parent) {
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

bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y) {
    if (ctx == NULL) {
        return false;
    }
    const tbox_layout_box *box = tbox_context_hit_test(ctx, x, y);
    if (box == NULL) {
        tbox_context_set_focus(ctx, NULL);
        return false;
    }
    while (box != NULL && box->node == NULL) {
        box = box->parent;
    }
    if (box == NULL) {
        return false;
    }

    const tbox_html_node *focus = NULL;
    for (const tbox_html_node *node = box->node; node != NULL; node = node->parent) {
        if (tbox_context_focusable(ctx, node)) {
            focus = node;
            break;
        }
    }
    tbox_context_set_focus(ctx, focus);
    if (tbox_context_is_text_input(focus)) {
        tbox_text_field *field = tbox_context_text_field(ctx, focus);
        const tbox_layout_box *input_box = tbox_context_find_box(ctx->root, focus);
        if (field != NULL && input_box != NULL && input_box->style != NULL) {
            const tbox_style *style = input_box->style;
            const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts,
                tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
                style->font_italic, style->font_size);
            if (face != NULL) {
                size_t best = tbox_context_cursor_at_x(field, input_box, face, x);
                field->cursor = best;
                field->anchor = best;
            }
        }
    }
    return tbox_context_dispatch_click_node(ctx, box->node);
}

bool tbox_context_drag_select(tbox_context *ctx, double x) {
    if (ctx == NULL || !tbox_context_is_text_input(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node)) return false;
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    const tbox_layout_box *box = tbox_context_find_box(ctx->root, ctx->focused_node);
    if (field == NULL || box == NULL || box->style == NULL) return false;
    const tbox_style *style = box->style;
    const tbox_font_face *face = tbox_font_face_cache_get(ctx->fonts,
        tbox_string_view_from_cstr(style->font_family), style->font_weight_bold,
        style->font_italic, style->font_size);
    if (face == NULL) return false;
    size_t cursor = tbox_context_cursor_at_x(field, box, face, x);
    if (cursor == field->cursor) return false;
    field->cursor = cursor;
    return true;
}

tbox_string_view tbox_context_selected_text(tbox_context *ctx) {
    if (ctx == NULL || !tbox_context_is_text_input(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node))
        return tbox_string_view_make(NULL, 0);
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    if (field == NULL || field->anchor == field->cursor)
        return tbox_string_view_make(NULL, 0);
    size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
    size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
    return tbox_string_view_make(field->value + start, end - start);
}

bool tbox_context_dispatch_key(tbox_context *ctx, tbox_key_event event) {
    if (ctx == NULL || !event.pressed) {
        return false;
    }
    if (event.key == TBOX_KEY_TAB) {
        return tbox_context_move_focus(ctx, event.shift);
    }
    if (tbox_context_is_text_input(ctx->focused_node) &&
        tbox_context_node_attached(ctx, ctx->focused_node) &&
        tbox_context_focusable(ctx, ctx->focused_node)) {
        tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
        if (field == NULL) return false;
        if (event.control && event.key == TBOX_KEY_A) {
            bool changed = field->anchor != 0 || field->cursor != field->length;
            field->anchor = 0;
            field->cursor = field->length;
            return changed;
        }
        size_t start = field->cursor, end = field->cursor;
        size_t old_anchor = field->anchor;
        switch (event.key) {
        case TBOX_KEY_LEFT:
            if (!event.shift && field->anchor != field->cursor)
                field->cursor = field->anchor < field->cursor ? field->anchor : field->cursor;
            else
                field->cursor = tbox_utf8_previous(field->value, field->cursor);
            break;
        case TBOX_KEY_RIGHT:
            if (!event.shift && field->anchor != field->cursor)
                field->cursor = field->anchor > field->cursor ? field->anchor : field->cursor;
            else
                field->cursor = tbox_utf8_next(field->value, field->length, field->cursor);
            break;
        case TBOX_KEY_HOME:
            field->cursor = 0;
            break;
        case TBOX_KEY_END:
            field->cursor = field->length;
            break;
        case TBOX_KEY_BACKSPACE:
            if (field->anchor != field->cursor) {
                start = field->anchor < field->cursor ? field->anchor : field->cursor;
                end = field->anchor > field->cursor ? field->anchor : field->cursor;
            } else start = tbox_utf8_previous(field->value, field->cursor);
            break;
        case TBOX_KEY_DELETE:
            if (field->anchor != field->cursor) {
                start = field->anchor < field->cursor ? field->anchor : field->cursor;
                end = field->anchor > field->cursor ? field->anchor : field->cursor;
            } else end = tbox_utf8_next(field->value, field->length, field->cursor);
            break;
        default:
            return false;
        }
        if (event.key == TBOX_KEY_LEFT || event.key == TBOX_KEY_RIGHT ||
            event.key == TBOX_KEY_HOME || event.key == TBOX_KEY_END) {
            if (!event.shift) field->anchor = field->cursor;
            return field->cursor != start || field->anchor != old_anchor;
        }
        if (start == end) return false;
        memmove(field->value + start, field->value + end, field->length - end + 1);
        field->length -= end - start;
        field->cursor = field->anchor = start;
        tbox_context_sync_text_value(ctx, field);
        return true;
    }
    if ((event.key == TBOX_KEY_ENTER || event.key == TBOX_KEY_SPACE) &&
        ctx->focused_node != NULL && tbox_context_node_attached(ctx, ctx->focused_node) &&
        tbox_context_focusable(ctx, ctx->focused_node)) {
        tbox_string_view tag = ctx->focused_node->element.tag_name;
        if (tbox_string_view_equal_cstr(tag, "button")) {
            return tbox_context_dispatch_click_node(ctx, ctx->focused_node);
        }
    }
    return false;
}

bool tbox_context_dispatch_text(tbox_context *ctx, tbox_string_view text) {
    if (ctx == NULL || !tbox_context_is_text_input(ctx->focused_node) ||
        !tbox_context_node_attached(ctx, ctx->focused_node) ||
        !tbox_context_focusable(ctx, ctx->focused_node) ||
        text.data == NULL || text.size == 0) return false;
    for (size_t i = 0; i < text.size;) {
        size_t next = tbox_utf8_next(text.data, text.size, i);
        if (next == i || (unsigned char)text.data[i] < 0x20 ||
            (unsigned char)text.data[i] == 0x7f ||
            (next == i + 2 && (unsigned char)text.data[i] == 0xc2 &&
             (unsigned char)text.data[i + 1] >= 0x80 &&
             (unsigned char)text.data[i + 1] <= 0x9f)) return false;
        i = next;
    }
    tbox_text_field *field = tbox_context_text_field(ctx, ctx->focused_node);
    if (field == NULL) return false;
    size_t start = field->anchor < field->cursor ? field->anchor : field->cursor;
    size_t end = field->anchor > field->cursor ? field->anchor : field->cursor;
    size_t remaining = field->length - (end - start);
    if (text.size > SIZE_MAX - remaining - 1) return false;
    size_t needed = remaining + text.size + 1;
    if (needed > field->capacity) {
        size_t capacity = field->capacity;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        char *value = realloc(field->value, capacity);
        if (value == NULL) return false;
        field->value = value;
        field->capacity = capacity;
    }
    memmove(field->value + start + text.size,
            field->value + end, field->length - end + 1);
    memcpy(field->value + start, text.data, text.size);
    field->length = remaining + text.size;
    field->cursor = field->anchor = start + text.size;
    tbox_context_sync_text_value(ctx, field);
    return true;
}

void tbox_context_on_input(tbox_context *ctx, tbox_context_input_handler handler, void *userdata) {
    if (ctx == NULL) return;
    ctx->input_handler = handler;
    ctx->input_userdata = userdata;
}

const tbox_html_node *tbox_context_focused_node(const tbox_context *ctx) {
    return ctx == NULL || ctx->focused_node == NULL || !tbox_context_node_attached(ctx, ctx->focused_node)
               ? NULL : ctx->focused_node;
}

tbox_html_document *tbox_context_document(tbox_context *ctx) {
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->document;
}
