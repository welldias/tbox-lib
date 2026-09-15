#ifndef TBOX_OUTPUT_H
#define TBOX_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include <tbox/render.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * Two halves, per ARCHITECTURE.md's "Output Display" section:
 *
 * 1. tbox_raster_* -- platform-agnostic, pure software rasterizer over a
 *    caller-allocated XRGB8888 uint32_t* buffer (native-endian
 *    0xFF000000u | (r<<16) | (g<<8) | b, matching WL_SHM_FORMAT_XRGB8888).
 *    No Wayland dependency at all, always compiled, unit-tested directly.
 *
 * 2. tbox_backend_wayland_* -- stateful window/surface management for
 *    Linux/Wayland (registry, wl_shm, xdg-shell, keyboard), evolved from
 *    example/tbox_wayland.c. It owns the frame's wl_shm buffer and calls
 *    tbox_raster_display_list() into it once per tbox_backend_wayland_present()
 *    call. Only compiled when TBOX_WAYLAND_FOUND (see CMakeLists.txt) --
 *    absent that, this whole second half of the header still declares fine,
 *    it just has no definitions to link against. Not unit-tested (no
 *    compositor in CI); see tests/output/test_raster.c for the tested half. */

/* Fills `rect` (clipped to the buffer's [0, buffer_width) x [0, buffer_height)
 * bounds -- rect may legitimately extend past any edge, or lie entirely
 * outside) with `color`, alpha-blended over whatever is already in `pixels`
 * using the standard "over" operator per channel:
 *   result = src * alpha + dst * (1 - alpha), alpha = color.a / 255.0
 * color.a == 255 short-circuits to a plain overwrite; color.a == 0 is a
 * no-op. `pixels` must hold at least buffer_width * buffer_height entries,
 * row-major, no padding between rows. A NULL `pixels`, or a non-positive
 * buffer_width/buffer_height, is a no-op. */
void tbox_raster_fill_rect(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect rect, tbox_css_rgba color);

/* Decodes `text` as UTF-8 and rasterizes it as a single line (v0: no
 * wrapping, matching tbox_font_measure_text/tbox_paint_op's TEXT_RUN scope)
 * into `pixels`, one glyph at a time via tbox_font_rasterize_glyph(face, ...).
 * `origin` is the TEXT_RUN's content-box origin (only origin.x/origin.y are
 * read, matching tbox_paint_op's rect for TEXT_RUN -- width/height are
 * ignored). The baseline is origin.y + tbox_font_face_ascent(face), constant
 * for the whole call; each glyph is composited at
 * (pen_x + bearing_x, baseline_y - bearing_y) and the pen then advances by
 * the glyph's `advance`. Each covered pixel is alpha-blended (same "over"
 * formula as tbox_raster_fill_rect) using
 * (glyph_alpha / 255.0) * (color.a / 255.0) as the combined alpha against
 * `color`'s RGB, clipped to the buffer's bounds exactly like
 * tbox_raster_fill_rect. A NULL `pixels`/`face`, a non-positive
 * buffer_width/buffer_height, an empty `text`, or color.a == 0 is a no-op. */
void tbox_raster_text_run(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, tbox_rect origin, tbox_string_view text, const tbox_font_face *face, tbox_css_rgba color);

/* Convenience: walks `list->items` in order and dispatches each op to
 * tbox_raster_fill_rect (TBOX_PAINT_FILL_RECT) or tbox_raster_text_run
 * (TBOX_PAINT_TEXT_RUN) -- what a backend's present/frame function calls
 * once per frame instead of switching on op->kind itself. A NULL `list` is a
 * no-op. */
void tbox_raster_display_list(uint32_t *pixels, int32_t buffer_width, int32_t buffer_height, const tbox_display_list *list);

/* Opaque: one open Wayland window (registry/compositor/shm/seat/keyboard/
 * xdg_wm_base/xdg_surface/xdg_toplevel, plus the current frame's wl_shm
 * buffer), evolved from example/tbox_wayland.c's tbox_wayland_app. Linux
 * only; only declared meaningfully when TBOX_WAYLAND_FOUND (see
 * CMakeLists.txt) -- callers should check that build-time condition (or
 * simply not call these functions) on other platforms. */
typedef struct tbox_backend_wayland tbox_backend_wayland;

/* Connects to the Wayland display, binds the globals this backend needs
 * (wl_compositor, wl_shm, xdg_wm_base, wl_seat), creates a
 * wl_surface/xdg_surface/xdg_toplevel sized `width`x`height` titled `title`
 * (NULL falls back to "tbox"), and blocks until the compositor's initial
 * xdg_surface::configure is received and ack'd -- so that immediately after
 * this call returns non-NULL, tbox_backend_wayland_size() already reflects
 * whatever size the compositor settled on (a tiling compositor may not
 * honor `width`/`height` exactly) rather than the caller having to poll
 * first just to find that out. Returns NULL on any failure to connect, bind
 * the required globals, or reach that initial configure (e.g. the
 * compositor closing the surface before ever configuring it). */
tbox_backend_wayland *tbox_backend_wayland_open(int32_t width, int32_t height, const char *title);

/* Frees `backend` and every Wayland/xkbcommon/wl_shm resource it owns. A
 * no-op if backend == NULL. */
void tbox_backend_wayland_destroy(tbox_backend_wayland *backend);

/* Pumps at most one round of pending Wayland events (the
 * prepare_read/poll/read_events dance, not a loop -- callers loop
 * themselves, once per frame or however often is appropriate). timeout_ms
 * < 0 blocks until at least one event arrives (or the connection drops);
 * 0 never blocks; > 0 blocks at most that many milliseconds. Returns false
 * if the connection to the compositor was lost (a dead `backend` should
 * then be treated the same as a close request -- tbox_backend_wayland_should_close
 * does NOT automatically become true in that case, since the connection is
 * simply gone, not something further calls can query); true otherwise,
 * including when the wait merely timed out with nothing to do. */
bool tbox_backend_wayland_poll(tbox_backend_wayland *backend, int timeout_ms);

/* True once either the compositor requested this window close (xdg_toplevel's
 * close event) or the user pressed ESC while this window had keyboard focus.
 * NULL is treated as already closed (true). */
bool tbox_backend_wayland_should_close(const tbox_backend_wayland *backend);

/* Writes `backend`'s current window size to out_width/out_height (both
 * pointers must be non-NULL) -- reflects the most recent xdg_surface::configure
 * processed by tbox_backend_wayland_open or tbox_backend_wayland_poll, so it
 * can change across calls if the user resizes the window. NULL `backend`
 * writes 0/0. */
void tbox_backend_wayland_size(const tbox_backend_wayland *backend, int32_t *out_width, int32_t *out_height);

/* Binds wl_pointer (a wl_seat capability, alongside the keyboard already
 * handled for ESC in tbox_backend_wayland_open) and tracks enter/leave (which
 * surface has pointer focus), motion (current surface-local position), and
 * button. Consumes a pending click of the primary (left) mouse button --
 * a PRESS event, not RELEASE, with no drag/double-click tracking (see
 * "Fora de escopo" in ARCHITECTURE.md) -- that occurred since the last call
 * to this function, if any: writes its position to out_x/out_y (surface-
 * local coordinates, already in the system tbox_context_dispatch_click
 * expects) and returns true, clearing the pending state so a second call in
 * a row returns false until another press arrives. NULL `backend`, NULL
 * `out_x`, or NULL `out_y` returns false without writing anything, even if a
 * click was pending. */
bool tbox_backend_wayland_take_click(tbox_backend_wayland *backend, double *out_x, double *out_y);

/* Rasterizes `list` (via tbox_raster_display_list, NULL treated as empty)
 * into this frame's wl_shm buffer -- reallocating it first if the window's
 * size changed since the last call -- clearing to opaque white first (v0
 * has no UA stylesheet, so a <html>/<body> with the CSS-initial transparent
 * background_color emits no FILL_RECT of its own; white matches every
 * browser's actual default canvas color, which is a closer match to
 * expected v0 output than showing nothing/black would be), then attaches
 * and commits it. A no-op if `backend` is NULL, has no valid surface, or its
 * current size is non-positive. */
void tbox_backend_wayland_present(tbox_backend_wayland *backend, const tbox_display_list *list);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_OUTPUT_H */
