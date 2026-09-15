#include <tbox/tbox.h>

/* Smallest possible demonstration of the whole v0 vertical slice through the
 * single public tbox_app_open() entry point (see <tbox/app.h> and
 * ARCHITECTURE.md's "Application" / "Fatia vertical v0" sections) -- HTML +
 * CSS in, a real Wayland window out, laid out and rendered through the real
 * Style -> Layout Tree -> Render Pipeline -> Output Display pipeline.
 *
 * Deliberately does NOT reuse example/index.html/style.css: those exercise
 * flexbox, CSS custom properties and media queries, none of which this v0
 * engine understands, so they would render as a broken mess here. This
 * fixture instead sticks to exactly what ARCHITECTURE.md's "Fatia vertical
 * v0" acceptance criteria call for -- one single top-level element (the
 * Layout Tree only ever lays out the document's first top-level ELEMENT
 * child, see ARCHITECTURE.md's Layout Tree section) wrapping a <div> with
 * explicit width/height/background-color, plus a heading and a paragraph
 * (recognized by tbox_layout_build's fixed tag list and actually shown as
 * text, unlike the <div>). tbox has no inline `style="..."` attribute
 * concept (see <tbox/css_cascade.h>'s tbox_css_specificity comment: "no
 * fourth style attribute bucket") -- everything here is styled through a
 * real stylesheet with tag/class selectors, same as tests/style/test_style.c
 * and tests/layout/test_layout.c.
 *
 * Unlike example/tbox_wayland.c, this demo takes no env vars of its own --
 * tbox_app_open is a real library function and, per <tbox/debug.h>, the
 * library itself never reads the environment or logs on its own. To smoke-
 * test it without a human present (this call blocks until the window
 * closes, and v0 has no control API to close it from outside), bound it
 * from the shell instead, e.g.:
 *   timeout 5s ./tbox_app_demo
 * A timeout-induced kill confirms the window opened, laid out and rendered
 * without crashing; it just doesn't exercise the graceful ESC/compositor-
 * close path, which needs a real event nothing scripted here can send. */

static const char *TBOX_APP_DEMO_HTML =
    "<body>"
    "<div class=\"box\"></div>"
    "<h1>tbox v0</h1>"
    "<p>Hello from tbox's Application layer.</p>"
    "</body>";

static const char *TBOX_APP_DEMO_CSS =
    "body { background-color: white; }"
    ".box { width: 200px; height: 100px; background-color: cornflowerblue; }";

int main(void) {
    bool ok = tbox_app_open(TBOX_APP_DEMO_HTML, TBOX_APP_DEMO_CSS, 640, 480);
    return ok ? 0 : 1;
}
