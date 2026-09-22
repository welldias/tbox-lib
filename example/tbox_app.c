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

/* v0 + v1 + v2 + v3 vertical slice demonstration through tbox's public
 * tbox_app API (see <tbox/app.h>). This single fixture keeps validating all
 * four layers at once rather than forking into per-version example files:
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
 *   into tbox_app_create -- TBOX_APP_DEMO_CSS_PATH's stylesheet adds ZERO
 *   font-size/font-weight rules of its own for headings, on purpose, to
 *   prove the UA default works unassisted -- plus a <p> long enough to wrap
 *   across multiple lines at TBOX_APP_DEMO_WIDTH, mixing plain text with a
 *   <b> run (visibly bolder) and an <em> run (same weight as plain text in
 *   this engine -- v2's Fonte/Texto only has regular/bold faces, no italic
 *   face yet, see ARCHITECTURE.md's "v2 -- Fidelidade Visual" -> "Cor não
 *   varia por run" / face-cache notes).
 * - v3 ("v3 -- Interatividade Avançada" -> "Fatia vertical v3 -- critério
 *   de 'pronto'"): real file-based loading (this is the whole point of
 *   this task -- HTML/CSS come from example/tbox_app_demo.html/.css on
 *   disk via tbox_app_create_from_files, not C string literals baked into
 *   this file anymore), a `.hoverable:hover` author rule that changes
 *   background-color purely from mouse movement (no click at all --
 *   tbox_app_step polls tbox_backend_wayland_pointer_position every tick
 *   and feeds it to tbox_context_update_hover under the hood), a
 *   bubbling demo (on_bubble_inner_click/on_bubble_outer_click, registered
 *   on two different ancestor selectors of the same clicked element, both
 *   firing nearest-first on one click), a stopPropagation demo
 *   (on_stop_inner_click returns false, so on_stop_outer_click -- which
 *   would otherwise match a farther ancestor -- never fires), a
 *   tbox_context_unbind_click demo (on_unbind_target_click unbinds its own
 *   registration after the first click, proven by the fact that its text
 *   never changes again after that), and a tbox_html_node_set_text_content
 *   demo (on_rename_target_click swaps its element's text on every click).
 *
 * Deliberately does NOT reuse example/index.html/style.css, same reasoning
 * v0's tbox_app.c already documented: those exercise flexbox, CSS custom
 * properties and media queries this engine doesn't understand, so they'd
 * render as a broken mess here. example/tbox_app_demo.html/.css (this
 * file's own dedicated fixtures, wired in via TBOX_APP_DEMO_HTML_PATH/
 * TBOX_APP_DEMO_CSS_PATH below -- see example/CMakeLists.txt) stick to
 * what the v0/v1/v2/v3 acceptance scenarios actually call for -- one
 * single top-level element (the Layout Tree only ever lays out the
 * document's first top-level ELEMENT child, hence everything below lives
 * inside one <body>) wrapping the clickable .box, the hoverable box, the
 * bubbling/stopPropagation/unbind/rename demo elements, the h1..h6 ladder,
 * and the mixed-inline wrapping paragraph.
 *
 * Unattended smoke-testing: v0's tbox_app_open blocked until the window
 * closed, so its only way to smoke-test without a human present was an
 * external `timeout 5s ./tbox_app_demo`. tbox_app_step's non-blocking loop
 * makes that unnecessary now -- this demo reads the same
 * TBOX_WAYLAND_CLOSE_DELAY_MS env var example/tbox_wayland.c already uses
 * and, when set, closes itself that many milliseconds after startup,
 * confirming the window opened, laid out and rendered without crashing and
 * closed cleanly, all without manual intervention.
 *
 * `--screenshot <path>` (see tbox_app_demo_run_screenshot below) goes one
 * step further for VISUAL verification specifically: renders this same
 * document offscreen via tbox_app_screenshot_from_files and writes a PNG,
 * with no Wayland window opened at all -- no compositor needed, no
 * screenshot tool, no window-manager coordination to locate/float/position
 * the right window (all of which real compositors, especially tiling ones,
 * make unreliable to script). Every prior version's vertical-slice
 * validation captured a screenshot by opening the real interactive window
 * (this file's normal path below) and driving an external tool like `grim`
 * against whatever the compositor decided to show; `--screenshot` is the
 * more robust alternative going forward.
 *
 * What this file does NOT do, on purpose: it never simulates a click or
 * mouse movement itself (no input-injection tooling such as `ydotool`/
 * `wtype` is a dependency of this library or its examples), and it makes
 * no attempt to assert on rendered pixel output. The correctness of every
 * v3 behavior demonstrated here -- :hover reaching the cascade, real
 * bubbling order, stopPropagation, unbind, text mutation -- is already
 * proven end to end at the test level by tests/context/test_context.c's
 * dedicated integration tests (see ARCHITECTURE.md's "v3 -- Interatividade
 * Avançada" section and that test file for the exact cases). This file is
 * a runnable demonstration for a human who moves the mouse and clicks, not
 * itself a source of correctness proof -- same stance v1/v2's tbox_app.c
 * already took (TBOX_WAYLAND_DEBUG=1 makes every handler's tbox_log() call
 * below visible, which is the intended way to observe bubbling/
 * stopPropagation/unbind order when running this by hand; a screenshot
 * tool such as `grim`, run while TBOX_WAYLAND_CLOSE_DELAY_MS keeps the
 * window open, is optional visual evidence but not required). */

#define TBOX_APP_DEMO_WIDTH 640
/* v0/v1 used 480px tall, enough for the box + one heading + one paragraph.
 * v2 stacked a full h1..h6 ladder plus two paragraphs below the box, which
 * needed 900px. v3 adds five more demo elements (hoverable, bubbling pair,
 * stopPropagation pair, unbind, rename) above the heading ladder -- bumped
 * to 1400px. v5 inserts several more flow elements (see
 * example/tbox_app_demo.html/.css's "v5:" blocks) between .collapse-second
 * and .box, pushing the document's own total (in-flow) height to ~1723px
 * with the fonts this build actually resolves -- bumped again to 1900px so
 * a screenshot can show the whole document PLUS the `position: fixed`
 * element pinned to the window's bottom-right corner (bottom: 20px;
 * right: 20px against this exact viewport height, deliberately placed
 * below all in-flow content with room to spare, not overlapping it). v7
 * makes the three <li> boxes actually grow their in-flow height with real
 * text (Tarefa 1's Layout Tree change) and appends one more paragraph
 * (.v7-entities) after .v6-entities, pushing the total past 1900px again --
 * a screenshot confirmed the new content was being silently cropped off the
 * bottom of the window at 1900px (this engine has no scroll/overflow, so
 * anything beyond the configured window height just never gets painted),
 * so this is bumped to 2100px, with headroom to spare, for the same reason
 * v2/v3/v5 each bumped it before: purely a window-size constant tracking
 * accumulated fixture content, not a layout-pipeline requirement. v8 adds
 * a numbered <ol> list (three more <li> boxes, same shape as the <ul>
 * demo) plus a `margin`/`padding: em` demo element right after it -- a
 * screenshot at 2100px again showed the bottom of the document (the
 * .bubble-/.stop- pairs and the bottom-right-pinned `position: fixed` box)
 * silently cropped off, so this is bumped to 2500px, same reasoning as
 * every prior bump. v11 appends a bare <hr>, a <br>-broken paragraph, a
 * <pre> block and three text-align boxes after the v9 fixtures -- a
 * screenshot at 2500px showed the second physical line of the new <pre>
 * and all three text-align boxes silently cropped off the bottom, so this
 * is bumped to 2900px, same reasoning and same purely-cosmetic
 * window-size-constant nature as every prior bump. v12 appends one more
 * paragraph (.v12-serif-font, with a nested <em>) after the v9 fixtures --
 * a screenshot at 2900px showed that new paragraph silently cropped off the
 * bottom of the window, so this is bumped to 3150px, same reasoning and
 * same purely-cosmetic window-size-constant nature as every prior bump. v13
 * appends seven more paragraphs (.v13-strong-b through .v13-span-a) after
 * the v9 fixtures -- a screenshot at 3150px showed the last two of them
 * (.v13-sub-sup and .v13-span-a) silently cropped off the bottom of the
 * window, so this is bumped to 3450px, same reasoning and same
 * purely-cosmetic window-size-constant nature as every prior bump. */
#define TBOX_APP_DEMO_HEIGHT 3450

#ifndef TBOX_APP_DEMO_HTML_PATH
#error "TBOX_APP_DEMO_HTML_PATH must be defined by the build (see example/CMakeLists.txt)"
#endif
#ifndef TBOX_APP_DEMO_CSS_PATH
#error "TBOX_APP_DEMO_CSS_PATH must be defined by the build (see example/CMakeLists.txt)"
#endif

/* Registered below via tbox_context_on_click(ctx, ".box", ...): fires on any
 * click that lands inside the box's border box (tbox_context_dispatch_click's
 * ancestor walk from the hit-tested node). Reads the node's current `class`
 * via tbox_html_node_get_attribute and flips it between "box off" and
 * "box on" via tbox_html_node_set_attribute -- the exact scenario
 * ARCHITECTURE.md's "Fatia vertical v1" acceptance criteria describe. The
 * next tbox_app_step sees tbox_context_dispatch_click's `true` return,
 * treats it like a resize, and redoes Style -> Layout -> Render, so the box
 * visibly changes color (example/tbox_app_demo.css gives .off/.on different
 * background-color) without the window closing.
 *
 * NOVO v3: returns bool, not void (tbox_context_click_handler's signature
 * changed -- see <tbox/context.h>); true means "keep propagating", and
 * nothing else in the tree matches ".box" at a farther ancestor anyway, so
 * the return value has no observable effect here beyond satisfying the new
 * signature. */
static bool on_box_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;

    tbox_string_view class_name           = tbox_string_view_make("class", strlen("class"));
    const tbox_html_attribute *class_attr = tbox_html_node_get_attribute(node, class_name);

    bool currently_off         = class_attr == NULL || !tbox_string_view_equal_cstr(class_attr->value, "box on");
    tbox_string_view new_class = currently_off ? tbox_string_view_make("box on", strlen("box on")) : tbox_string_view_make("box off", strlen("box off"));

    tbox_log("box clicked: swapping class to \"%.*s\"", (int)new_class.size, new_class.data);

    tbox_html_document *document = tbox_context_document(ctx);
    tbox_html_node_set_attribute(document, node, class_name, new_class);
    return true;
}

/* NOVO v3 -- bubbling demo, half 1 of 2. Registered on ".bubble-inner",
 * an empty div nested inside ".bubble-outer" (see
 * example/tbox_app_demo.html/.css -- a plain <div> never renders text
 * regardless of TEXT children, see that CSS file's own top comment, so
 * both boxes are purely visual, clicked directly rather than through some
 * inner text/span). A click landing in the (smaller, nested) inner box's
 * area is inside both border boxes at once, so tbox_context_hit_test finds
 * .bubble-inner as the deepest box and tbox_context_dispatch_click walks
 * outward from there to .bubble-outer. Fires first (nearest-ancestor-first
 * bubbling order), logs so
 * TBOX_WAYLAND_DEBUG=1 shows the order, and appends its own "-fired" class
 * token (a fixed whole-attribute replace, same shape as on_box_click) so
 * example/tbox_app_demo.css's .bubble-inner-fired rule visibly recolors
 * it. Returns true so propagation continues up to .bubble-outer's
 * handler below -- this pair is what actually proves real bubbling, as
 * opposed to the separate stopPropagation pair further down. */
static bool on_bubble_inner_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[bubbling] .bubble-inner handler fired (nearest ancestor) -- propagation continues");

    tbox_string_view class_name = tbox_string_view_make("class", strlen("class"));
    tbox_string_view new_class  = tbox_string_view_make("bubble-inner bubble-inner-fired", strlen("bubble-inner bubble-inner-fired"));
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, class_name, new_class);
    return true;
}

/* NOVO v3 -- bubbling demo, half 2 of 2. Registered on ".bubble-outer",
 * the farther ancestor. Only reachable because on_bubble_inner_click above
 * returned true -- fires SECOND, proving dispatch walks ancestors nearest
 * to farthest and tests every registration at each level (real bubbling
 * order), not "per-registration, first-match-wins independently of other
 * registrations" like v1 used to. */
static bool on_bubble_outer_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[bubbling] .bubble-outer handler fired (farther ancestor) -- confirms real bubbling order");

    tbox_string_view class_name = tbox_string_view_make("class", strlen("class"));
    tbox_string_view new_class  = tbox_string_view_make("bubble-outer bubble-outer-fired", strlen("bubble-outer bubble-outer-fired"));
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, class_name, new_class);
    return true;
}

/* NOVO v3 -- stopPropagation demo, half 1 of 2. A deliberately SEPARATE
 * nested div pair from the bubbling one above (.stop-outer/.stop-inner,
 * not .bubble-*), so the two scenarios never get tangled with each other.
 * Registered on ".stop-inner"; fires and recolors itself
 * exactly like on_bubble_inner_click, but returns FALSE -- stopping
 * propagation immediately, so on_stop_outer_click below must never run
 * for this click, no matter how far up the ancestor chain ".stop-outer"
 * would otherwise have matched. */
static bool on_stop_inner_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[stopPropagation] .stop-inner handler fired -- returning false, .stop-outer must NOT fire");

    tbox_string_view class_name = tbox_string_view_make("class", strlen("class"));
    tbox_string_view new_class  = tbox_string_view_make("stop-inner stop-inner-fired", strlen("stop-inner stop-inner-fired"));
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, class_name, new_class);
    return false;
}

/* NOVO v3 -- stopPropagation demo, half 2 of 2. If this ever logs or
 * recolors .stop-outer, stopPropagation is broken -- on_stop_inner_click
 * above always returns false first, so tbox_context_dispatch_click's
 * ancestor walk must stop before it ever reaches (or even tests) this
 * registration. Kept registered (rather than omitted entirely) precisely
 * so its silence is itself the proof: example/tbox_app_demo.css's
 * .stop-outer-fired rule is documented there as "should never appear". */
static bool on_stop_outer_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[stopPropagation] BUG: .stop-outer handler fired -- this must never happen while stopPropagation works");

    tbox_string_view class_name = tbox_string_view_make("class", strlen("class"));
    tbox_string_view new_class  = tbox_string_view_make("stop-outer stop-outer-fired", strlen("stop-outer stop-outer-fired"));
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, class_name, new_class);
    return true;
}

/* NOVO v3 -- tbox_context_unbind_click demo. `userdata` points at a plain
 * int living in main()'s stack frame (see below) -- tbox_context_on_click
 * hands back this handler's own binding handle, which main() stores into
 * that int immediately after registering, before the loop (hence before
 * any click) can run. On the first click, this handler reads that handle
 * back out of userdata and unbinds ITSELF: a second click on the same
 * element no longer reaches this function at all (tbox_context_dispatch_click
 * simply finds no active registration left to test), which is otherwise
 * unobservable without click simulation -- so this handler also rewrites
 * its own text via tbox_html_node_set_text_content, and the fact that text
 * never changes again after the first click is the visible proof the
 * unbind took effect. */
static bool on_unbind_target_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    int binding = *(int *)userdata;
    tbox_log("[unbind] .unbind-target clicked -- unbinding this handler now (binding=%d); a second click will do nothing", binding);

    tbox_context_unbind_click(ctx, binding);

    const char *new_text = "Unbound: further clicks do nothing now";
    tbox_html_node_set_text_content(tbox_context_document(ctx), node, tbox_string_view_make(new_text, strlen(new_text)));
    return true;
}

/* NOVO v3 -- tbox_html_node_set_text_content demo, independent of the
 * unbind one above (this one stays bound and keeps firing on every click,
 * just to show set_text_content in isolation). Swaps the element's text to
 * a fixed string -- reflected on screen by the very next tbox_app_step
 * without needing any Layout Tree change of its own (see
 * tbox_html_node_set_text_content's own doc comment in <tbox/html_parser.h>:
 * h1-h6/p already re-read their text by walking direct children on every
 * relayout). */
static bool on_rename_target_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[text] .rename-target clicked -- swapping text via tbox_html_node_set_text_content");

    const char *new_text = "Text changed at runtime via tbox_html_node_set_text_content!";
    tbox_html_node_set_text_content(tbox_context_document(ctx), node, tbox_string_view_make(new_text, strlen(new_text)));
    return true;
}

/* NOVO v5 -- Orchestration hit-test fix (Tarefa 2) interactive proof.
 * Registered on ".v5-escaped-child" (example/tbox_app_demo.html/.css): a
 * `position: absolute` box whose top/left deliberately place it well
 * outside its own DOM parent's (.v5-escape-parent, a small 80x50 box)
 * border_box. Before the fix, tbox_context_hit_test_box gave up as soon as
 * a box's own border_box failed to contain the click point, so a click
 * landing only inside this escaped child -- never inside its small
 * parent's border_box -- would never have reached this handler at all. If
 * this fires and recolors the element (same whole-attribute-replace shape
 * as on_bubble_inner_click above), the fix works end to end through the
 * real Style -> Layout -> Orchestration pipeline, not just at the unit
 * level tests/context/test_context.c already covers. */
static bool on_v5_escaped_click(tbox_context *ctx, tbox_html_node *node, void *userdata) {
    (void)userdata;
    tbox_log("[v5 hit-test] .v5-escaped-child clicked outside its parent's border_box -- hit-test fix confirmed");

    tbox_string_view class_name = tbox_string_view_make("class", strlen("class"));
    tbox_string_view new_class  = tbox_string_view_make("v5-escaped-child v5-escaped-child-clicked", strlen("v5-escaped-child v5-escaped-child-clicked"));
    tbox_html_node_set_attribute(tbox_context_document(ctx), node, class_name, new_class);
    return true;
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

/* Registers `selector`'s handler and bails out (closing `app` and printing
 * to stderr) if tbox_context_on_click failed (-1: bad selector syntax, or
 * ctx/handler NULL). Small helper shared by every registration in main()
 * below purely to avoid repeating this same six-line check seven times. */
static bool tbox_app_demo_register(tbox_app *app, const char *selector, tbox_context_click_handler handler, void *userdata, int *out_binding) {
    int binding = tbox_context_on_click(tbox_app_context(app), selector, strlen(selector), handler, userdata);
    if (binding < 0) {
        fprintf(stderr, "tbox_context_on_click failed to register the \"%s\" handler\n", selector);
        return false;
    }
    if (out_binding != NULL) {
        *out_binding = binding;
    }
    return true;
}

/* --screenshot <path>: renders `html_path`/`css_path` into `path` via
 * tbox_app_screenshot_from_files (<tbox/app.h>) and exits, WITHOUT ever
 * opening a Wayland window -- see that function's doc comment for why: no
 * compositor, no screenshot tool (grim/similar), no window-manager
 * coordination to get right, just a deterministic offscreen render of this
 * demo's first (and only, in this mode) frame. Replaces the interactive
 * loop entirely when passed; every other flag/env var below (TBOX_WAYLAND_DEBUG,
 * TBOX_WAYLAND_CLOSE_DELAY_MS, click handlers) is meaningless in this mode
 * and simply not reached. Prints a one-line result to stderr and returns 0
 * on success, 1 on failure (bad HTML/CSS, font resolution failure, or the
 * PNG couldn't be written -- tbox_app_screenshot_from_files doesn't
 * distinguish which, so neither does this message). */
static int tbox_app_demo_run_screenshot(const char *html_path, const char *css_path, const char *png_path) {
    bool ok = tbox_app_screenshot_from_files(html_path, css_path, TBOX_APP_DEMO_WIDTH, TBOX_APP_DEMO_HEIGHT, png_path);
    if (!ok) {
        fprintf(stderr, "tbox_app_screenshot_from_files failed to write \"%s\"\n", png_path);
        return 1;
    }
    fprintf(stderr, "wrote %dx%d screenshot to \"%s\"\n", TBOX_APP_DEMO_WIDTH, TBOX_APP_DEMO_HEIGHT, png_path);
    return 0;
}

/* `[<html_path> [<css_path>]]`: both optional, positional, and independent
 * of `--screenshot`'s own flag+argument pair (which may appear anywhere
 * among argv, same as before this was added). Neither file is read here --
 * this only decides which path strings tbox_app_create_from_files/
 * tbox_app_screenshot_from_files below get handed.
 *
 * `html_path` omitted (`argc`'s only positional-eligible args are none, or
 * just `--screenshot <path>`) -- resolves BOTH `*out_html_path` and
 * `*out_css_path` to the existing TBOX_APP_DEMO_HTML_PATH/
 * TBOX_APP_DEMO_CSS_PATH build-time defaults, unchanged from this file's
 * behavior before this function existed -- this is the exact fallback the
 * project's maintainer asked for.
 *
 * `html_path` given but `css_path` omitted -- `*out_css_path` resolves to
 * NULL, not the demo's own default CSS: pairing a caller-supplied HTML
 * fixture with this file's own hoverable/bubbling/stopPropagation-styled
 * demo stylesheet would misleadingly style unrelated markup. NULL is a
 * legitimate value for both tbox_app_create_from_files and
 * tbox_app_screenshot_from_files (see tbox_app_create_from_files_impl in
 * src/app/tbox_app.c: "NULL means no author stylesheet", not an error) --
 * the caller's HTML then renders against the UA stylesheet alone, same as
 * any other author-stylesheet-less document this engine already supports.
 *
 * Returns false (having already printed to stderr) only on a genuine usage
 * error: more than two positional arguments. */
static bool tbox_app_demo_parse_args(int argc, char **argv, const char **out_screenshot_path, const char **out_html_path, const char **out_css_path) {
    const char *screenshot_path = NULL;
    const char *html_path       = NULL;
    const char *css_path        = NULL;
    int positional_count        = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--screenshot requires a <path> argument\n");
                return false;
            }
            screenshot_path = argv[++i];
            continue;
        }

        if (positional_count == 0) {
            html_path = argv[i];
        } else if (positional_count == 1) {
            css_path = argv[i];
        } else {
            fprintf(stderr, "unexpected extra argument \"%s\"\n", argv[i]);
            return false;
        }
        positional_count++;
    }

    if (html_path == NULL) {
        html_path = TBOX_APP_DEMO_HTML_PATH;
        css_path  = TBOX_APP_DEMO_CSS_PATH;
    }

    *out_screenshot_path = screenshot_path;
    *out_html_path       = html_path;
    *out_css_path        = css_path;
    return true;
}

int main(int argc, char **argv) {
    const char *screenshot_path;
    const char *html_path;
    const char *css_path;
    if (!tbox_app_demo_parse_args(argc, argv, &screenshot_path, &html_path, &css_path)) {
        return 1;
    }

    if (screenshot_path != NULL) {
        return tbox_app_demo_run_screenshot(html_path, css_path, screenshot_path);
    }

    tbox_log_init("tbox_app_demo", tbox_env_bool("TBOX_WAYLAND_DEBUG"));
    long close_delay_ms = tbox_env_long("TBOX_WAYLAND_CLOSE_DELAY_MS", 0);

    tbox_app *app = tbox_app_create_from_files(html_path, css_path, TBOX_APP_DEMO_WIDTH, TBOX_APP_DEMO_HEIGHT);
    if (app == NULL) {
        fprintf(stderr, "tbox_app_create_from_files failed\n");
        return 1;
    }

    /* unbind_target_binding is a plain local (automatic storage duration,
     * NOT static/global -- it simply needs an address that outlives the
     * registration call below, which main()'s own stack frame already
     * does for the app's whole run) that on_unbind_target_click reads back
     * out of its userdata to unbind itself after firing once. */
    int unbind_target_binding = -1;

    tbox_app_demo_register(app, ".box", on_box_click, NULL, NULL);
    tbox_app_demo_register(app, ".bubble-inner", on_bubble_inner_click, NULL, NULL);
    tbox_app_demo_register(app, ".bubble-outer", on_bubble_outer_click, NULL, NULL);
    tbox_app_demo_register(app, ".stop-inner", on_stop_inner_click, NULL, NULL);
    tbox_app_demo_register(app, ".stop-outer", on_stop_outer_click, NULL, NULL);
    tbox_app_demo_register(app, ".unbind-target", on_unbind_target_click, &unbind_target_binding, &unbind_target_binding);
    tbox_app_demo_register(app, ".rename-target", on_rename_target_click, NULL, NULL);
    tbox_app_demo_register(app, ".v5-escaped-child", on_v5_escaped_click, NULL, NULL);

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
