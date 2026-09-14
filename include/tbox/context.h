#ifndef TBOX_CONTEXT_H
#define TBOX_CONTEXT_H

#include <stddef.h>

#include <tbox/font.h>
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
 * display list is Application's job (tbox_app_open, calling
 * tbox_context_run_frame then tbox_backend_wayland_present). See
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

/* Parses `html`/`css` (tbox_html_parse/tbox_css_parse) and stores the
 * result. Both of those tolerate malformed markup/CSS themselves (see
 * their own docs) and only return NULL on allocation failure, which is
 * therefore also the only way this function fails. `font` is borrowed:
 * the caller (Application) loads it once and destroys it once --
 * tbox_context never takes ownership, and tbox_context_close never
 * touches it. No layout exists yet after this call returns
 * (tbox_context_hit_test returns NULL until the first
 * tbox_context_run_frame). */
tbox_context *tbox_context_open(const char *html, size_t html_length, const char *css, size_t css_length, tbox_font_face *font);

/* Destroys the parsed document/stylesheet and the frame arena -- every
 * tbox_style_table/tbox_layout_box/tbox_display_list this context ever
 * produced becomes invalid at that point -- then frees `ctx` itself.
 * Does NOT destroy `font` (see tbox_context_open: it is borrowed, not
 * owned). A no-op if ctx == NULL. */
void tbox_context_close(tbox_context *ctx);

/* Redoes the whole compute pipeline against `viewport_width`/
 * `viewport_height`: no incremental invalidation in v0 (see
 * ARCHITECTURE.md), so this always resets the frame arena first --
 * invalidating whatever Style/Layout/Render produced on the previous
 * call in one shot, since none of those have a `_destroy` of their own
 * (see "Convenções" in ARCHITECTURE.md) -- then runs
 * tbox_style_resolve_tree -> tbox_layout_build ->
 * tbox_render_build_display_list in sequence, against a single
 * author-origin stylesheet (v0 has no user-agent stylesheet). The result
 * is written into `*out_list` (`out_list` must be non-NULL). If the
 * document has nothing to lay out (tbox_layout_build returns NULL, e.g.
 * an empty document), `*out_list` ends up an empty display list
 * ({NULL, 0}) rather than crashing -- tbox_render_build_display_list
 * already handles a NULL layout root that way. */
void tbox_context_run_frame(tbox_context *ctx, double viewport_width, double viewport_height, tbox_display_list *out_list);

/* Linear search, from the layout tree computed by the most recent
 * tbox_context_run_frame, for the deepest box whose border_box contains
 * (x, y): v0's block-flow siblings never overlap, so if a box's
 * border_box doesn't contain the point, none of its descendants can
 * either. Same search shape (and same "fine for UI-sized trees, revisit
 * only if it's ever a measured bottleneck") as tbox_style_table_find/
 * tbox_css_selector_match elsewhere in the codebase. Returns NULL if no
 * frame has run yet (nothing computed) or nothing is under the point. */
const tbox_layout_box *tbox_context_hit_test(const tbox_context *ctx, double x, double y);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_CONTEXT_H */
