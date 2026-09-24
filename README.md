# tbox

tbox is an experimental retained-mode GUI library in C. It parses HTML and
CSS, computes a layout tree, builds a display list, and rasterizes it in
software. The intended use is a desktop application whose interface is
described by HTML/CSS while application state and operating-system work live
in C. A JavaScript bridge is a later milestone.

## Current capabilities

- HTML/CSS parsing, selectors, cascade, mutable document tree, and a limited
  set of box, text, image, and table layout rules.
- Software rendering and PNG screenshots without opening a window.
- Interactive windows on Linux/Wayland with pointer clicks, `:hover`,
  keyboard focus on buttons and single-line `<input type="text">` fields,
  `:focus`, Enter/Space button activation, UTF-8 text editing and selection
  (keyboard, mouse drag, double click, Ctrl+A, and XKB Compose sequences),
  Ctrl+C/Ctrl+X/Ctrl+V through the Wayland clipboard, keyboard repeat using
  the compositor's settings, horizontal input scrolling, and
  resize handling. The application can request a redraw after external DOM
  mutations with `tbox_app_request_redraw()`.
- Unit/integration tests for the core pipeline and a separate, optional
  visual comparison tool (`tbox_cmp`).

The supported HTML/CSS subset is defined by the public headers under
`include/tbox/`; unsupported properties and elements may be ignored. There
is currently no IME composition, general scrolling or clipping, flex/grid
layout, accessibility tree, or Windows/macOS window backend. The library is
not yet suitable for a complete desktop application.

## Build and checks

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

FreeType is fetched by CMake. Fontconfig enables application rendering.
Wayland enables interactive windows on Linux; `-DTBOX_ENABLE_WAYLAND=OFF`
builds a headless library that can still create screenshots when Fontconfig
is present. `-DTBOX_ENABLE_FONTCONFIG=OFF` builds the core with embedded-font
support; application functions remain linkable but report that the backend
is unavailable. `tbox_app_backend_available()` checks whether this build can
create a window. With `TBOX_BUILD_EXAMPLES=ON`, build the keyboard example
using `cmake --build build --target tbox_keyboard_demo`. Run
`build/example/tbox_keyboard_demo` on Wayland to try button focus and
activation with Tab, Shift+Tab, Enter and Space, plus typing into a text
field with Backspace, Delete, Left, Right, Home and End. Hold Shift with
Left, Right, Home or End to select text; Ctrl+A or a double click selects it
all, and dragging selects a range. Ctrl+C, Ctrl+X, and Ctrl+V use the system
clipboard. Holding a
key repeats according to the Wayland compositor's configured rate and delay.
A repeat rate of zero disables repetition; until the compositor sends its
settings, repetition stays disabled.
The example also compiles
without Wayland or Fontconfig, but needs both to open a window.

`build/tests/tbox_cmp tests/assets` compares the renderer against image
references when OpenCV and Fontconfig are installed. Its current fixtures
include unsupported features, so a `DIFFERENT` result is diagnostic rather
than a test failure.

## Roadmap

1. Establish the public application/event contract, resource ownership, and
   explicit invalidation. Keep headless rendering independent of the window
   backend.
2. Build a Linux pilot with a form and scrollable list: focus, keyboard and
   text input, buttons, editing, selection, scrolling, clipping, and simple
   persistence through C callbacks.
3. Add the CSS layout and painting capabilities needed by that pilot,
   including adaptive sizing and correct hit testing of clipped content.
4. Add Windows/macOS backends and complete the Linux distribution targets.
   Validate DPI, clipboard, accessibility, packaging, and lifecycle on each.
5. Add a JavaScript runtime with an explicit bridge to registered C functions.

Each milestone should include behavior tests, a runnable example, and
platform-specific acceptance checks. `ARCHITECTURE.md` contains the detailed
history and design of the existing pipeline.
