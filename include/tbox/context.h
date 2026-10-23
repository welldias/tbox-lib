#ifndef TBOX_CONTEXT_H
#define TBOX_CONTEXT_H

#include <stdbool.h>
#include <stddef.h>

#include <tbox/callback.h>
#include <tbox/font.h>
#include <tbox/html_parser.h>
#include <tbox/image.h>
#include <tbox/input.h>
#include <tbox/layout.h>
#include <tbox/render.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UI thread only: a context, and everything reached through it, belongs to
 * the thread that uses it; there is no internal locking (see "Threads" in
 * docs/13-memoria-e-concorrencia.md).
 *
 * Orchestration: owns the *compute* pipeline end to end (parse once at
 * open, then per frame: cascade+style -> layout -> render -> display
 * list) and decides when to redo it. It knows nothing about Wayland or
 * any backend -- the platform event loop and presenting the resulting
 * display list is Application's job. See
 * docs/dev/historico.md's "Orchestration / Main Loop" section for the full
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

/* Orchestration's user-agent stylesheet configuration (see
 * docs/dev/historico.md's "CSS Cascade / Orchestration -- folha de estilo
 * user-agent" -> "Configuração da UA stylesheet"). Every number the UA
 * stylesheet text embeds (see tbox_context_open_with_options) comes from one
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
     * down -- same rule as any em in CSS). Included as an explicit
     * `body { font-size: ... }` declaration in the UA stylesheet
     * (tbox_context_open_with_options), so a non-default value takes effect
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
    double list_px; /* margin (top/bottom) of <ul>/<ol> -- same unit/semantics as paragraph_px */
    double hr_px;   /* margin (top/bottom) of <hr> -- same unit/semantics as list_px/paragraph_px */
} tbox_ua_style_margin_config;

typedef struct tbox_ua_style_config {
    tbox_ua_style_font_config font;
    tbox_ua_style_margin_config margin;
    double list_padding_left_px; /* <ul>/<ol> indentation (padding-inline-start: the left in horizontal ltr) -- lives directly on this struct, NOT inside tbox_ua_style_margin_config, because it is a padding value, not a margin one, and that sub-struct is specifically for margin fields */
    double hr_height_px;         /* <hr>'s explicit height -- lives directly on this struct, NOT inside tbox_ua_style_margin_config, same reasoning as list_padding_left_px above: it is a height value, not a margin one */
} tbox_ua_style_config;

/* The classic browser values docs/dev/historico.md documents (heading em scale
 * 2/1.5/1.17/1/0.83/0.67 for h1..h6; heading margins, approximated in px,
 * 21/19/18/21/22/25; paragraph margin 16px; body margin 8px; base_px 16;
 * list margin (top/bottom of <ul>/<ol>) 16px, same as
 * paragraph_px; list padding-left (indentation of <ul>/<ol>) 40px, the
 * classic value used by every real browser; <hr> margin
 * (top/bottom) 8px, approximating the `margin-block: 0.5em` real browsers
 * use for <hr> at the default 16px base_px; <hr> height 2px).
 * Never fails, never allocates -- plain field assignment. */
tbox_ua_style_config tbox_ua_style_config_default(void);

/* CSS for the internal color, calendar and file pickers is independent of
 * document CSS. NULL selects the built-in theme; a non-NULL empty string
 * selects an empty theme with only structural geometry fallbacks. */
typedef struct tbox_context_options {
    tbox_ua_style_config ua_style;
    const char *control_css;
    size_t control_css_length;
    const char *asset_base_dir; /* local HTML resources and @font-face url() files */
    const char *css_base_dir; /* @font-face URLs in the CSS passed by the application; defaults to asset_base_dir */
    bool prefers_reduced_motion; /* exposed to @media (prefers-reduced-motion: ...) */
} tbox_context_options;

tbox_context_options tbox_context_options_default(void);

/* Same as tbox_context_open, with explicit options; NULL `options` is
 * tbox_context_options_default(). The context parses and owns a copy of
 * control_css; neither `options` nor its strings need outlive this call. */
tbox_context *tbox_context_open_with_options(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images, const tbox_context_options *options);

/* Parses `html`/`css` (tbox_html_parse/tbox_css_parse) and stores the
 * result. Both of those tolerate malformed markup/CSS themselves (see
 * their own docs) and only return NULL on allocation failure, which is
 * therefore also the only way this function fails. `fonts` (a
 * tbox_font_face_cache, not a single tbox_font_face -- see <tbox/font.h>)
 * and `images` (a tbox_image_cache -- see <tbox/image.h>; may be NULL, "no
 * images", same as a NULL font resolver) are both borrowed: the caller
 * (Application) builds them once and destroys them once -- tbox_context
 * never takes ownership, and tbox_context_close never touches either. No
 * layout exists yet after this call returns (tbox_context_hit_test returns
 * NULL until the first tbox_context_run_frame).
 *
 * tbox_context_open_with_options(..., NULL). */
tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face_cache *fonts, tbox_image_cache *images);

/* Destroys the parsed document/stylesheet/user-agent-stylesheet
 * and the frame arena -- every tbox_style_table/tbox_layout_box/
 * tbox_display_list this context ever produced becomes invalid at that
 * point -- then frees `ctx` itself. Does NOT destroy `fonts`/`images` (see
 * tbox_context_open: both are borrowed, not owned). A no-op if ctx == NULL. */
void tbox_context_close(tbox_context *ctx);

/* Redoes the whole compute pipeline against `viewport_width`/
 * `viewport_height`: no incremental invalidation in v0 (see
 * docs/dev/historico.md), so this always resets the frame arena first --
 * invalidating whatever Style/Layout/Render produced on the previous
 * call in one shot, since none of those have a `_destroy` of their own
 * (see "Convenções" in docs/dev/convencoes.md) -- then runs
 * tbox_style_resolve_tree -> tbox_layout_build ->
 * tbox_render_build_display_list in sequence, against TWO cascade sources
 * (this context's ua_stylesheet as TBOX_CSS_ORIGIN_USER_AGENT,
 * then its author stylesheet as TBOX_CSS_ORIGIN_AUTHOR -- v0/v1 only ever
 * had the author one). The result is written into `*out_list` (`out_list`
 * must be non-NULL). If the
 * document has nothing to lay out (tbox_layout_build returns NULL, e.g.
 * an empty document), `*out_list` ends up an empty display list
 * ({NULL, 0}) rather than crashing -- tbox_render_build_display_list
 * already handles a NULL layout root that way. */
void tbox_context_run_frame(tbox_context *ctx, double viewport_width, double viewport_height, tbox_display_list *out_list);

/* Sets a monotonic time in seconds before run_frame. The application does
 * this automatically; callers rendering deterministic frames can supply it. */
void tbox_context_set_animation_time(tbox_context *ctx, double seconds);

/* After a run_frame: the earliest animation time (same clock as
 * tbox_context_set_animation_time) at which another run_frame would look
 * different with no new input -- a transition or animation in progress,
 * the end of a transition-delay/animation-delay, a smooth scroll, a scroll
 * snap waiting for the wheel to stop. -INFINITY (or any value not after
 * the current time) means "the next frame"; INFINITY means idle: nothing
 * changes until an event or a DOM mutation. A sleeping loop can wait until
 * then. INFINITY for NULL. */
double tbox_context_next_frame_time(const tbox_context *ctx);

/* tbox_context_next_frame_time(ctx) < INFINITY. */
bool tbox_context_animations_active(const tbox_context *ctx);

/* Linear search, from the layout tree computed by the most recent
 * tbox_context_run_frame, for the deepest box that contains (x, y) --
 * ALWAYS visits every descendant, since a box that is out of normal flow
 * (from v5 on: `position: absolute`/`fixed`) can be positioned entirely
 * outside its own DOM parent's border_box, so a box's border_box failing
 * to contain the point does not rule out a descendant containing it (see
 * docs/dev/historico.md's "Orchestration -- correção de hit-test pra caixas fora
 * de fluxo"). When more than one candidate box contains (x, y) -- possible
 * now that out-of-flow boxes can overlap on purpose -- the one LATER in
 * document order wins, matching paint order (no stacking context/z-index
 * in this project: later in the tree is always painted on top). Same
 * search shape (and same "fine for UI-sized trees, revisit only if it's
 * ever a measured bottleneck") as tbox_style_table_find/
 * tbox_css_selector_match elsewhere in the codebase. Returns NULL if no
 * frame has run yet (nothing computed) or nothing is under the point. */
const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y);

/* The pointer cursor for (x, y): the CSS `cursor` of the box there, or for
 * `auto` the text cursor over editable fields and text, the pointing hand
 * over links (`<a href>`) and the arrow elsewhere. TBOX_STYLE_CURSOR_DEFAULT
 * when nothing is there. Applications pass it to their window backend. */
tbox_style_cursor tbox_context_cursor_at(const tbox_context *ctx, double x, double y);

/* Scrolls the innermost overflow-y:auto block under (x,y). Returns true
 * when its offset changed; caller should then request a new frame. */
bool tbox_context_scroll(tbox_context *ctx, double x, double y, double delta_y);

/* Pointer handling for the visible vertical scrollbar and the `resize`
 * handle. A press returns true when the track or a handle consumed the
 * event. Drag returns true while they own the press (and moves the thumb
 * when it was grabbed, or resizes the element); release ends it. */
bool tbox_context_scrollbar_press(tbox_context *ctx, double x, double y);
bool tbox_context_scrollbar_drag(tbox_context *ctx, double x, double y);
void tbox_context_scrollbar_release(tbox_context *ctx);

/* v3 -- Interatividade Avançada: real ":hover" support. See
 * docs/dev/historico.md's "v3 -- Interatividade Avançada" -> "`:hover`" section,
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

/* Records the hit element on pointer press for :active. Release (pressed=false)
 * clears it. Returns true when the active target changed. */
bool tbox_context_set_active_at(tbox_context *ctx, bool pressed, double x, double y);

/* Finds the tbox_layout_box under (x, y) via tbox_context_hit_test, then
 * walks the ancestor chain ONCE, from that box's ->node outward via
 * node->parent (DOM tree, not the layout tree -- the two only coincide in
 * v0/v1/v2 because no anonymous box is in real use yet) -- real bubbling
 * order, nearest ancestor to farthest.
 *
 * Interatividade Avançada: at each ancestor level, EVERY
 * currently-active tbox_app_on_click binding's compiled selector is
 * tested against that node (tbox_css_selector_query_matches); every
 * binding that matches at this level fires (in registration order among
 * the ones that match at this same level), passing that ancestor's node.
 * If any handler call returns false (see tbox_app_click_handler),
 * walking stops immediately -- no farther ancestor is even tested,
 * regardless of what would have matched there (stopPropagation). This
 * replaces v1's "per-registration, nearest-match-wins independently of
 * other registrations' order" dispatch -- see docs/dev/historico.md's "v3 --
 * Interatividade Avançada" -> "Bubbling completo + stopPropagation +
 * desregistro de handler" for the full rationale.
 *
 * A click on an enabled input type=checkbox toggles its `checked` attribute
 * before handlers run. A disabled control (input, button, select,
 * textarea -- also inside a disabled fieldset) does not dispatch click
 * handlers, including for clicks on its content. A color input opens its RGB picker; clicks inside the
 * picker adjust a channel. Date, datetime-local, and month inputs open a picker;
 * choosing a day or month, or confirming date and time, updates the value.
 * A reset input restores the initial values of controls in its form.
 * Returns true if :focus-visible changed, a handler fired, a checkbox changed,
 * or a picker handled the click (the caller --
 * Application today, a script engine's event loop tomorrow -- should treat
 * that the same as a resize: a reason to redo the compute pipeline).
 * Returns false for NULL ctx. With no target, handlers do not fire, but
 * clearing :focus-visible can still require a frame. */
bool tbox_context_dispatch_click(tbox_context *ctx, double x, double y);

/* Adjusts an open input type=color RGB slider during a pointer drag.
 * Returns true when the color changed and a new frame is needed. */
bool tbox_context_color_drag(tbox_context *ctx, double x, double y);

/* Adjusts a pressed range input while the pointer is dragged. */
bool tbox_context_range_drag(tbox_context *ctx, double x, double y);
void tbox_context_range_release(tbox_context *ctx);

/* Adjusts an open datetime-local picker hour or minute bar during a pointer drag.
 * Returns true when the displayed selection changed. */
bool tbox_context_datetime_drag(tbox_context *ctx, double x, double y);

/* Extends the focused text input's selection from the last click or keyboard
 * anchor to the character nearest x. Returns true when the caret moves. */
bool tbox_context_drag_select(tbox_context *ctx, double x);

/* Extends selection in a text control using both pointer coordinates. */
bool tbox_context_drag_select_at(tbox_context *ctx, double x, double y);

/* Borrowed view of the focused text control's selected UTF-8 bytes, or an empty
 * view when nothing is selected. Valid until the next edit of this field. */
tbox_string_view tbox_context_selected_text(tbox_context *ctx);

/* Keyboard focus belongs to the context, not the window backend. Tab and
 * Shift+Tab move through visible enabled buttons (including input type=button),
 * checkboxes, color, date, datetime-local, month, file, and image inputs, text controls (including
 * email and number inputs), and selects; the next
 * frame reveals the newly focused control inside scrollable ancestors.
 * Enter and Space activate a focused button through click handlers. Space
 * toggles a focused checkbox and runs its click handlers. Enter or Space
 * opens a focused color input's RGB picker, a calendar input's popup, or the
 * file picker; editing
 * keys act on a focused text control. Up/Down steps a focused number input.
 * Shift with Left, Right, Home or End extends its selection. Backspace and
 * Delete remove a selection when one exists; Ctrl+A selects all text.
 * Returns true when a frame should be recomputed; false for a NULL event. */
bool tbox_context_dispatch_key(tbox_context *ctx, const tbox_key_event *event);

/* Inserts committed UTF-8 text at the focused text control's cursor, replacing its
 * selection when one exists. Invalid UTF-8 and control characters are
 * ignored, except newline and tab in a textarea. Text is distinct from key events so
 * a future backend can deliver composed text through the same API. */
bool tbox_context_dispatch_text(tbox_context *ctx, tbox_string_view text);

/* Checks an input type=email's current value. Empty values are valid unless
 * `required` is present; `multiple` accepts comma-separated addresses.
 * Returns false for other elements. */
bool tbox_context_email_valid(const tbox_html_node *input);

/** Checks a URL input's required state and basic absolute URL syntax. */
bool tbox_context_url_valid(const tbox_html_node *input);

/* Checks a number input's value against numeric syntax, required, min, max,
 * and step. Empty values are valid unless required is present. */
bool tbox_context_number_valid(const tbox_html_node *input);

/* Absolute path chosen by the user for input type=file, or an empty view.
 * The value attribute contains only the file name. The path is owned by the
 * context and remains valid until a new selection, clearing the value, or
 * closing the context. */
tbox_string_view tbox_context_file_path(tbox_context *ctx, const tbox_html_node *input);

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
