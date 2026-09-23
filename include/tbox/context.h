#ifndef TBOX_CONTEXT_H
#define TBOX_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/font.h>
#include <tbox/html_parser.h>
#include <tbox/input.h>
#include <tbox/image.h>
#include <tbox/layout.h>
#include <tbox/render.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Orchestration: owns the *compute* pipeline end to end (parse once at
 * open, then per frame: cascade+style -> layout -> render -> display
 * list) and decides when to redo it. It knows nothing about Wayland or
 * any backend -- the platform event loop and presenting the resulting
 * display list is Application's job. See
 * ARCHITECTURE.md's "Orchestration / Main Loop" section for the full
 * rationale, including why an earlier draft of that section (owning the
 * event loop) was wrong.
 *
 * Opaque, unlike tbox_layout_box's plain/visible struct: tbox_context
 * embeds a tbox_arena by value (its per-frame arena), whose full
 * definition lives in the internal src/base/tbox_arena.h, not under
 * include/tbox/ -- so, same reasoning that keeps tbox_html_document/
 * tbox_css_stylesheet/tbox_font_face opaque, this struct cannot be
 * defined in a public header. It has a real open/close lifecycle (like
 * tbox_html_document), not the caller-arena pattern of
 * tbox_style_table/tbox_layout_box/tbox_display_list -- it PRODUCES
 * those every frame into its own internal arena rather than being one of
 * them. Reading the most recent layout from outside this module goes
 * through tbox_context_hit_test, the same shape as
 * tbox_style_table_find/tbox_css_computed_style_find elsewhere in the
 * codebase. */
typedef struct tbox_context tbox_context;

/* NOVO v2 -- Orchestration's user-agent stylesheet configuration (see
 * ARCHITECTURE.md's "CSS Cascade / Orchestration -- folha de estilo
 * user-agent" -> "Configuração da UA stylesheet"). Every number the UA
 * stylesheet text embeds (see tbox_context_open_with_config) comes from one
 * of these fields, never a literal baked into the generated CSS -- a host
 * that wants a different heading scale, margin, or base font-size just
 * copies tbox_ua_style_config_default() and overwrites the one field it
 * cares about, with no CSS to write or parse itself.
 *
 * Grouped into sub-structs by subject (font vs. margin), not a flat struct
 * -- easier to read at the call site (config.font.base_px rather than a
 * lone base_font_size_px among nine unrelated fields) and each sub-struct
 * can grow on its own later (e.g. tbox_ua_style_font_config gaining
 * heading_weight_bold[6] the day that also becomes configurable) without
 * touching the other. index 0 = h1 .. index 5 = h6 in every array indexed
 * by heading in this section -- font and margin deliberately share this
 * same indexing, so the two can be read side by side without reindexing. */
typedef struct tbox_ua_style_font_config {
    /* body's font-size -- every heading's em scale multiplies from here
     * (or from a nearer ancestor, if author CSS overrides font-size partway
     * down -- same rule as any em in CSS). Emitted as an explicit
     * `body { font-size: ... }` declaration in the generated UA stylesheet
     * (tbox_context_open_with_config), so a non-default value takes effect
     * even though it happens to match the Style layer's own hardcoded
     * "no parent" root default (16px) -- that fallback only matters for a
     * document whose top-level element isn't literally `<body>` (tbox's
     * HTML parser doesn't synthesize an implicit one), same pre-existing
     * caveat every other UA rule targeting `body` already has. */
    double base_px;
    double heading_em[6]; /* font-size multiplier per heading level, relative to the inherited font-size */
} tbox_ua_style_font_config;

typedef struct tbox_ua_style_margin_config {
    double heading_px[6]; /* same indexing as tbox_ua_style_font_config::heading_em */
    double paragraph_px;
    double body_px;
    double list_px; /* NOVO v8: margin (top/bottom) of <ul>/<ol> -- same unit/semantics as paragraph_px */
    double hr_px; /* NOVO v11: margin (top/bottom) of <hr> -- same unit/semantics as list_px/paragraph_px */
} tbox_ua_style_margin_config;

typedef struct tbox_ua_style_config {
    tbox_ua_style_font_config font;
    tbox_ua_style_margin_config margin;
    double list_padding_left_px; /* NOVO v8: <ul>/<ol> indentation (padding-left) -- lives directly on this struct, NOT inside tbox_ua_style_margin_config, because it is a padding value, not a margin one, and that sub-struct is specifically for margin fields */
    double hr_height_px; /* NOVO v11: <hr>'s explicit height -- lives directly on this struct, NOT inside tbox_ua_style_margin_config, same reasoning as list_padding_left_px above: it is a height value, not a margin one */
} tbox_ua_style_config;

/* The classic browser values ARCHITECTURE.md documents (heading em scale
 * 2/1.5/1.17/1/0.83/0.67 for h1..h6; heading margins, approximated in px,
 * 21/19/18/21/22/25; paragraph margin 16px; body margin 8px; base_px 16;
 * NOVO v8: list margin (top/bottom of <ul>/<ol>) 16px, same as
 * paragraph_px; list padding-left (indentation of <ul>/<ol>) 40px, the
 * classic value used by every real browser; NOVO v11: <hr> margin
 * (top/bottom) 8px, approximating the `margin-block: 0.5em` real browsers
 * use for <hr> at the default 16px base_px; <hr> height 2px).
 * Never fails, never allocates -- plain field assignment. */
tbox_ua_style_config tbox_ua_style_config_default(void);

/* Parses `html`/`css` (tbox_html_parse/tbox_css_parse) and stores the
 * result. Both of those tolerate malformed markup/CSS themselves (see
 * their own docs) and only return NULL on allocation failure, which is
 * therefore also the only way this function fails. `fonts` (NOVO v2: a
 * tbox_font_face_cache, not a single tbox_font_face -- see <tbox/font.h>)
 * and `images` (a tbox_image_cache -- see <tbox/image.h>; may be NULL, "no
 * images", same as a NULL font resolver) are both borrowed: the caller
 * (Application) builds them once and destroys them once -- tbox_context
 * never takes ownership, and tbox_context_close never touches either. No
 * layout exists yet after this call returns (tbox_context_hit_test returns
 * NULL until the first tbox_context_run_frame).
 *
 * NOVO v2: internally a thin wrapper over tbox_context_open_with_config,
 * passing tbox_ua_style_config_default() -- the caller of this function
 * never needs to know tbox_ua_style_config exists. */
tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images);

/* NOVO v2: same as tbox_context_open, plus an explicit tbox_ua_style_config
 * this context's user-agent stylesheet is generated from (a template CSS
 * text filled in via snprintf, then parsed the same way author `css` is --
 * see ARCHITECTURE.md's "Configuração da UA stylesheet"). This is the REAL
 * implementation; tbox_context_open is a thin wrapper around this one with
 * tbox_ua_style_config_default(). Fails under the exact same conditions as
 * tbox_context_open. */
tbox_context *tbox_context_open_with_config(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, tbox_ua_style_config config);

/* Destroys the parsed document/stylesheet/user-agent-stylesheet (NOVO v2)
 * and the frame arena -- every tbox_style_table/tbox_layout_box/
 * tbox_display_list this context ever produced becomes invalid at that
 * point -- then frees `ctx` itself. Does NOT destroy `fonts`/`images` (see
 * tbox_context_open: both are borrowed, not owned). A no-op if ctx == NULL. */
void tbox_context_close(tbox_context *ctx);

/* Redoes the whole compute pipeline against `viewport_width`/
 * `viewport_height`: no incremental invalidation in v0 (see
 * ARCHITECTURE.md), so this always resets the frame arena first --
 * invalidating whatever Style/Layout/Render produced on the previous
 * call in one shot, since none of those have a `_destroy` of their own
 * (see "Convenções" in ARCHITECTURE.md) -- then runs
 * tbox_style_resolve_tree -> tbox_layout_build ->
 * tbox_render_build_display_list in sequence, against TWO cascade sources
 * (NOVO v2: this context's ua_stylesheet as TBOX_CSS_ORIGIN_USER_AGENT,
 * then its author stylesheet as TBOX_CSS_ORIGIN_AUTHOR -- v0/v1 only ever
 * had the author one). The result is written into `*out_list` (`out_list`
 * must be non-NULL). If the
 * document has nothing to lay out (tbox_layout_build returns NULL, e.g.
 * an empty document), `*out_list` ends up an empty display list
 * ({NULL, 0}) rather than crashing -- tbox_render_build_display_list
 * already handles a NULL layout root that way. */
void tbox_context_run_frame(tbox_context *ctx, double viewport_width, double viewport_height, tbox_display_list *out_list);

/* Linear search, from the layout tree computed by the most recent
 * tbox_context_run_frame, for the deepest box that contains (x, y) --
 * ALWAYS visits every descendant, since a box that is out of normal flow
 * (from v5 on: `position: absolute`/`fixed`) can be positioned entirely
 * outside its own DOM parent's border_box, so a box's border_box failing
 * to contain the point does not rule out a descendant containing it (see
 * ARCHITECTURE.md's "Orchestration -- correção de hit-test pra caixas fora
 * de fluxo"). When more than one candidate box contains (x, y) -- possible
 * now that out-of-flow boxes can overlap on purpose -- the one LATER in
 * document order wins, matching paint order (no stacking context/z-index
 * in this project: later in the tree is always painted on top). Same
 * search shape (and same "fine for UI-sized trees, revisit only if it's
 * ever a measured bottleneck") as tbox_style_table_find/
 * tbox_css_selector_match elsewhere in the codebase. Returns NULL if no
 * frame has run yet (nothing computed) or nothing is under the point. */
const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y);

/* v3 -- Interatividade Avançada: real ":hover" support. See
 * ARCHITECTURE.md's "v3 -- Interatividade Avançada" -> "`:hover`" section,
 * both the "Orchestration" block (this function) and the design rationale
 * above it (why the hover target is a file-static global inside
 * css_selector.c, set via tbox_css_selector_set_hover_context, rather than a
 * parameter threaded through style.h/css_cascade.h).
 *
 * Hit-tests (x, y) against the most recent layout -- same search
 * tbox_context_hit_test already does -- to find the deepest box's ->node
 * (or NULL if `has_position` is false, meaning the pointer left the
 * window, or nothing is under the point). Compares that result against
 * ctx->hovered_node (a plain struct field with its own lifetime, NOT part
 * of frame_arena -- it must survive every tbox_context_run_frame's arena
 * reset for the across-frame comparison here to mean anything). If it
 * changed, updates ctx->hovered_node and returns true; if unchanged,
 * returns false. Same "the return value IS the signal, tbox_context holds
 * no internal dirty flag of its own" contract tbox_context_dispatch_click
 * already established -- the caller (Application, via tbox_app_step) folds
 * this into its own "recompute this tick or not" decision alongside
 * dispatch_click's return and resize detection. A no-op (returns false) if
 * ctx == NULL.
 *
 * tbox_context_run_frame calls tbox_css_selector_set_hover_context(ctx->
 * hovered_node) immediately before every tbox_style_resolve_tree,
 * unconditionally -- not only on ticks where this function's return value
 * was true -- so the cascade always sees the CURRENT hover state, not just
 * the state as of whenever it last changed. */
bool tbox_context_update_hover(tbox_context *ctx, bool has_position, double x, double y);

/* v1 -- Interatividade: event delegation by CSS selector. See
 * ARCHITECTURE.md's "v1 -- Interatividade" -> "Orchestration (tbox_context)
 * -- delegação de evento por seletor" for the full rationale (why this
 * lives here rather than on Application: a future script engine needs the
 * same "register by selector, fire on click, mutate the tree" primitive
 * that a native C handler does).
 *
 * Called by tbox_context_dispatch_click on each ancestor (of the
 * hit-tested node) that matches that binding's registered selector. `node`
 * is the matching ancestor, not necessarily the node directly under the
 * pointer -- see tbox_context_dispatch_click. Non-const, unlike
 * tbox_layout_box::node: the whole point of a click handler is to be able
 * to mutate the tree (e.g. via tbox_html_node_set_attribute, which needs a
 * non-const node).
 *
 * NOVO v3 -- Interatividade Avançada: returns bool instead of void. true
 * means "keep propagating" (other bindings that match at a farther
 * ancestor can still fire); false means "stop propagation immediately"
 * (stopPropagation) -- no other binding fires for this click at all,
 * regardless of how far up the ancestor chain it would otherwise have
 * matched. Breaking change from v1's void-returning handler, same
 * accepted-breaking-change spirit as prior versions -- see
 * ARCHITECTURE.md's "v3 -- Interatividade Avançada" -> "Bubbling completo +
 * stopPropagation + desregistro de handler". */
typedef bool (*tbox_context_click_handler)(tbox_context *ctx, tbox_html_node *node, void *userdata);

/* Compiles `selector` (tbox_css_selector_compile -- same standalone-selector
 * grammar tbox_css_selector_query_evaluate uses, hard-fails on syntax
 * error) and registers `handler`/`userdata` in an arena-backed table owned
 * by `ctx` -- its own arena, NOT frame_arena, so a registration survives
 * every tbox_context_run_frame's arena reset (same array + linear-scan
 * shape as tbox_style_table, not an index).
 *
 * NOVO v3 -- Interatividade Avançada: returns an int binding handle (a
 * monotonically increasing id, scoped to `ctx`, never reused even after a
 * tbox_context_unbind_click -- so a stale handle from a removed binding can
 * never accidentally collide with a newly created one) instead of v1's
 * bool. Returns -1 on a selector syntax error (nothing is registered, same
 * failure condition as before) or if ctx == NULL or handler == NULL. Pass
 * the returned handle to tbox_context_unbind_click to remove this
 * registration later; a binding never unbound lives for `ctx`'s whole
 * lifetime, destroyed (its compiled query, specifically) in
 * tbox_context_close. */
int tbox_context_on_click(tbox_context *ctx, const char *selector, size_t selector_length, tbox_context_click_handler handler, void *userdata);

/* NOVO v3 -- Interatividade Avançada: removes the registration identified
 * by `binding` (a handle previously returned by tbox_context_on_click) from
 * `ctx`'s internal table -- its compiled query is destroyed at this point,
 * same cleanup tbox_context_close already performs for whatever bindings
 * remain at that time. A removed binding never fires again from any later
 * tbox_context_dispatch_click. Returns false (a no-op) if `binding` does
 * not correspond to any currently-active registration -- never registered,
 * already unbound -- or if ctx == NULL; true otherwise. */
bool tbox_context_unbind_click(tbox_context *ctx, int binding);

/* Finds the tbox_layout_box under (x, y) via tbox_context_hit_test, then
 * walks the ancestor chain ONCE, from that box's ->node outward via
 * node->parent (DOM tree, not the layout tree -- the two only coincide in
 * v0/v1/v2 because no anonymous box is in real use yet) -- real bubbling
 * order, nearest ancestor to farthest.
 *
 * NOVO v3 -- Interatividade Avançada: at each ancestor level, EVERY
 * currently-active tbox_context_on_click binding's compiled selector is
 * tested against that node (tbox_css_selector_query_matches); every
 * binding that matches at this level fires (in registration order among
 * the ones that match at this same level), passing that ancestor's node.
 * If any handler call returns false (see tbox_context_click_handler),
 * walking stops immediately -- no farther ancestor is even tested,
 * regardless of what would have matched there (stopPropagation). This
 * replaces v1's "per-registration, nearest-match-wins independently of
 * other registrations' order" dispatch -- see ARCHITECTURE.md's "v3 --
 * Interatividade Avançada" -> "Bubbling completo + stopPropagation +
 * desregistro de handler" for the full rationale.
 *
 * Still returns true if at least one handler fired (the caller --
 * Application today, a script engine's event loop tomorrow -- should treat
 * that the same as a resize: a reason to redo the compute pipeline).
 * No-op (returns false) if ctx == NULL, if there is no layout yet, or if
 * nothing is under the point -- same guard as tbox_context_hit_test. */
bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y);

/* Keyboard focus belongs to the context, not the window backend. Tab and
 * Shift+Tab move through visible enabled buttons and text inputs. Enter and
 * Space activate a focused button; editing keys act on a focused text input.
 * Returns true when a frame should be recomputed. */
bool tbox_context_dispatch_key(tbox_context *ctx, tbox_key_event event);

/* Inserts committed UTF-8 text at the focused input's cursor. Invalid UTF-8
 * and control characters are ignored. Text is distinct from key events so
 * a future backend can deliver composed text through the same API. */
bool tbox_context_dispatch_text(tbox_context *ctx, tbox_string_view text);

/* Called after the value of an input changes through editing. The value
 * view is valid through the call and until the next edit of this field. */
typedef void (*tbox_context_input_handler)(tbox_context *ctx, tbox_html_node *input, tbox_string_view value, void *userdata);
void tbox_context_on_input(tbox_context *ctx, tbox_context_input_handler handler, void *userdata);

const tbox_html_node *tbox_context_focused_node(const tbox_context *ctx);

/* Access to the internal document -- needed for a handler's body to call
 * tbox_html_node_set_attribute, which requires the document's arena, not
 * just the node. No accessor existed before v1 because nothing outside this
 * module needed the document directly. Returns NULL if ctx == NULL. */
tbox_html_document *tbox_context_document(tbox_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CONTEXT_H */
