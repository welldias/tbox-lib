/* Needed for clock_gettime/nanosleep/struct timespec under the project's
 * strict -std=c23 (CMAKE_C_EXTENSIONS OFF, see root CMakeLists.txt): unlike
 * example/tbox_wayland.c, this file doesn't need the wider _GNU_SOURCE (no
 * mmap/memfd_create here), just POSIX.1b's clock/timer extensions. Must
 * come before any system header is included. */
#define _POSIX_C_SOURCE 199309L

#include <tbox/tbox.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

/* v0 + v1 + v2 vertical slice demonstration through tbox's public tbox_app
 * API (see <tbox/app.h>). This single fixture keeps validating all three
 * layers at once rather than forking into per-version example files:
 *
 * - v0 (ARCHITECTURE.md's "v0" section): the basic HTML+CSS-in/
 *   Wayland-window-out static rendering pipeline.
 * - v1 ("v1 -- Interatividade" -> "Fatia vertical v1 -- critério de
 *   'pronto'"): a clickable element that mutates the document and redraws
 *   without closing the window, via the non-blocking tbox_app_create /
 *   tbox_app_step / tbox_app_should_close / tbox_app_close handle (v0's
 *   blocking tbox_app_open() is gone -- see ARCHITECTURE.md's Application
 *   section: a breaking change, accepted since example/ was its only real
 *   consumer) plus a tbox_context_on_click handler registered before the
 *   loop starts.
 * - v2 ("v2 -- Fidelidade Visual" -> "Fatia vertical v2 -- critério de
 *   'pronto'"): <h1>..<h6> headings that get real, visibly-different
 *   font-size and bold font-weight purely from the UA stylesheet baked
 *   into tbox_app_create -- TBOX_APP_DEMO_CSS below adds ZERO font-size/
 *   font-weight rules of its own for headings, on purpose, to prove the UA
 *   default works unassisted -- plus a <p> long enough to wrap across
 *   multiple lines at TBOX_APP_DEMO_WIDTH, mixing plain text with a <b>
 *   run (visibly bolder) and an <em> run (same weight as plain text in
 *   this engine -- v2's Fonte/Texto only has regular/bold faces, no italic
 *   face yet, see ARCHITECTURE.md's "v2 -- Fidelidade Visual" -> "Cor não
 *   varia por run" / face-cache notes).
 *
 * Deliberately does NOT reuse example/index.html/style.css, same reasoning
 * v0's tbox_app.c already documented: those exercise flexbox, CSS custom
 * properties and media queries this engine doesn't understand, so they'd
 * render as a broken mess here. This fixture sticks to what the v0/v1/v2
 * acceptance scenarios actually call for -- one single top-level element
 * (the Layout Tree only ever lays out the document's first top-level
 * ELEMENT child) wrapping a clickable <div> whose two classes (.off/.on)
 * differ only in background-color, the h1..h6 ladder, and the mixed-inline
 * wrapping paragraph.
 *
 * Unattended smoke-testing: v0's tbox_app_open blocked until the window
 * closed, so its only way to smoke-test without a human present was an
 * external `timeout 5s ./tbox_app_demo`. tbox_app_step's non-blocking loop
 * makes that unnecessary now -- this demo reads the same
 * TBOX_WAYLAND_CLOSE_DELAY_MS env var example/tbox_wayland.c already uses
 * and, when set, closes itself that many milliseconds after startup,
 * confirming the window opened, laid out and rendered without crashing and
 * closed cleanly, all without manual intervention. It does NOT simulate a
 * click itself (no input-injection tooling is a dependency of this library
 * or its examples) -- the click -> tbox_html_node_set_attribute -> redraw
 * chain this file's on_box_click() wires up is instead covered end to end
 * by tests/context/test_context.c's dedicated tbox_context_on_click/
 * _dispatch_click test case (see ARCHITECTURE.md's "Fatia vertical v1"
 * section and that test file for details). Likewise this file makes no
 * attempt to assert on rendered pixel output for the v2 headings/wrapping
 * content -- that's a rendering-correctness question, exercised here only
 * as a visual smoke test (a screenshot tool such as `grim`, run against
 * this demo while TBOX_WAYLAND_CLOSE_DELAY_MS keeps the window open, is
 * optional visual evidence but not required); the numeric font-size/weight
 * resolution itself is covered by tests/style/test_style.c and
 * tests/font/test_font.c. */

#define TBOX_APP_DEMO_WIDTH 640
/* v0/v1 used 480px tall, enough for the box + one heading + one paragraph.
 * v2 stacks a full h1..h6 ladder plus two paragraphs (one of them wrapping
 * across several lines) below the box, which no longer fits in 480px --
 * bumped to 900px purely so a screenshot taken during the smoke test below
 * can show the whole document at once; nothing about the layout pipeline
 * requires this specific height. */
#define TBOX_APP_DEMO_HEIGHT 900

static const char *TBOX_APP_DEMO_HTML =
    "<body>"
    "<div class=\"box off\"></div>"
    "<h1>tbox v2: Heading 1</h1>"
    "<h2>Heading 2</h2>"
    "<h3>Heading 3</h3>"
    "<h4>Heading 4</h4>"
    "<h5>Heading 5</h5>"
    "<h6>Heading 6</h6>"
    "<p>Click the box above to toggle its color.</p>"
    "<p>Este par\xc3\xa1grafo tem texto normal, <b>uma parte em negrito</b>, e "
    "<em>uma parte em it\xc3\xa1lico</em> (que ainda renderiza igual ao texto "
    "normal -- Fonte/Texto s\xc3\xb3 tem face regular/bold em v2, sem it\xc3"
    "\xa1lico de verdade), continuando at\xc3\xa9 quebrar a linha sozinho "
    "dentro da largura da janela.</p>"
    "</body>";

static const char *TBOX_APP_DEMO_CSS = "body { background-color: white; }"
                                       ".box { width: 200px; height: 100px; }"
                                       ".off { background-color: cornflowerblue; }"
                                       ".on { background-color: tomato; }";

/* Registered below via tbox_context_on_click(ctx, ".box", ...): fires on any
 * click that lands inside the box's border box (tbox_context_dispatch_click's
 * ancestor walk from the hit-tested node). Reads the node's current `class`
 * via tbox_html_node_get_attribute and flips it between "box off" and
 * "box on" via tbox_html_node_set_attribute -- the exact scenario
 * ARCHITECTURE.md's "Fatia vertical v1" acceptance criteria describe. The
 * next tbox_app_step sees tbox_context_dispatch_click's `true` return,
 * treats it like a resize, and redoes Style -> Layout -> Render, so the box
 * visibly changes color (TBOX_APP_DEMO_CSS above gives .off/.on different
 * background-color) without the window closing. */
static void on_box_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;

    tbox_string_view class_name           = tbox_string_view_make("class", strlen("class"));
    const tbox_html_attribute *class_attr = tbox_html_node_get_attribute(node, class_name);

    bool currently_off         = class_attr == NULL || !tbox_string_view_equal_cstr(class_attr->value, "box on");
    tbox_string_view new_class = currently_off ? tbox_string_view_make("box on", strlen("box on")) : tbox_string_view_make("box off", strlen("box off"));

    tbox_log("box clicked: swapping class to \"%.*s\"", (int)new_class.size, new_class.data);

    tbox_html_document *document = tbox_context_document(ctx);
    tbox_html_node_set_attribute(document, node, class_name, new_class);
}

/* Milliseconds elapsed since `start` (CLOCK_MONOTONIC) -- same helper shape
 * as example/tbox_wayland.c's tbox_wayland_elapsed_ms. */
static long tbox_app_demo_elapsed_ms(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long seconds_diff = (long)(now.tv_sec - start->tv_sec);
    long nanos_diff   = now.tv_nsec - start->tv_nsec;
    return seconds_diff * 1000L + nanos_diff / 1000000L;
}

int main(void) {
    tbox_log_init("tbox_app_demo", tbox_env_bool("TBOX_WAYLAND_DEBUG"));
    long close_delay_ms = tbox_env_long("TBOX_WAYLAND_CLOSE_DELAY_MS", 0);

    tbox_app *app = tbox_app_create(TBOX_APP_DEMO_HTML, TBOX_APP_DEMO_CSS, TBOX_APP_DEMO_WIDTH, TBOX_APP_DEMO_HEIGHT);
    if (app == NULL) {
        fprintf(stderr, "tbox_app_create failed\n");
        return 1;
    }

    if (!tbox_context_on_click(tbox_app_context(app), ".box", strlen(".box"), on_box_click, NULL)) {
        fprintf(stderr, "tbox_context_on_click failed to register the .box handler\n");
        tbox_app_close(app);
        return 1;
    }

    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    if (close_delay_ms > 0) {
        tbox_log("auto-close armed: closing after %ld ms", close_delay_ms);
    }

    while (!tbox_app_should_close(app)) {
        tbox_app_step(app);

        if (close_delay_ms > 0 && tbox_app_demo_elapsed_ms(&start_time) >= close_delay_ms) {
            tbox_log("auto-close delay elapsed, closing");
            break;
        }

        /* tbox_app_step polls with a 0ms (non-blocking) timeout -- see its
         * doc comment in <tbox/app.h> -- by design, so a future caller can
         * interleave its own task queue between ticks. This demo has no
         * such queue, so it sleeps a bit itself between ticks purely to
         * avoid spinning a CPU core at 100%; tbox itself never sleeps or
         * reads the clock on its own (see <tbox/debug.h>). */
        struct timespec tick_delay = { .tv_sec = 0, .tv_nsec = 8L * 1000L * 1000L };
        nanosleep(&tick_delay, NULL);
    }

    tbox_app_close(app);
    return 0;
}
