#ifndef TBOX_APP_H
#define TBOX_APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Not thread-safe: like the rest of tbox, there is no internal locking.
 *
 * The public, top-level API for the "just open a window and show this
 * document" use case (see ARCHITECTURE.md's "Application" section and the
 * "Fatia vertical v0" acceptance criteria this materializes end to end).
 * Retained-mode widget mutation is explicitly out of scope for v0 -- see
 * ARCHITECTURE.md -- so this is the ENTIRE public surface of the Application
 * layer for now: one function, no handle, no way to reach back in from the
 * caller once it has been called.
 *
 * Parses `html`/`css` (both plain NUL-terminated C strings, unlike
 * tbox_context_open's explicit-length pair -- this is the ergonomic
 * top-level entry point, so it computes strlen() itself), resolves a single
 * document-wide sans-serif font via tbox_font_source_fontconfig (16px,
 * neither bold nor italic -- see ARCHITECTURE.md's "Fonte / Texto" section:
 * Fontconfig is for running a real window, the embedded backend is a
 * test-only concern this function deliberately does not fall back to), opens
 * a `width`x`height` Wayland window, and BLOCKS, running the render loop
 * until the window closes -- either the user presses ESC, the compositor
 * sends a close request, or the connection to the compositor is lost. There
 * is no timeout parameter and no way to close the window from outside this
 * call: v0 has no control API (see ARCHITECTURE.md), so the only way out is
 * through the window itself.
 *
 * The redraw policy is the simplest one ARCHITECTURE.md's Orchestration
 * section describes for v0: the document is laid out and presented once for
 * the very first frame, and again only when the window's size changes (a
 * resize) -- no other trigger exists yet (no incremental invalidation, no
 * animation, no input-driven repaint).
 *
 * Returns true if the window opened, ran, and closed normally. Returns
 * false if it failed to even start (HTML/CSS parse failure -- extremely
 * unlikely, since both parsers tolerate malformed input and only fail on
 * allocation -- font source/resolve/load failure, or the window itself
 * failing to open), cleaning up whatever had already been allocated first;
 * never crashes either way. Compiled only when both a real Wayland backend
 * and real font discovery are available at build time (TBOX_WAYLAND_FOUND
 * and TBOX_FONTCONFIG_FOUND, see src/CMakeLists.txt) -- with either missing,
 * this declaration still exists, but nothing implements it. */
bool tbox_app_open(const char *html, const char *css, int32_t width, int32_t height);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_APP_H */
