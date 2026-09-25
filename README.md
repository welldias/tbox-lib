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
  keyboard focus on buttons, `<input type="button">`, `<input type="checkbox">`,
  `<input type="color">`, `<input type="date">`, and
  `<input type="datetime-local">`, `<input type="file">`, `<input type="image">`,
  `<input type="month">`, `<input type="radio">`, `<input type="range">`,
  `<input type="reset">`, `<input type="submit">`, `<input type="time">`,
  and `<input type="week">` controls, single-line `<input type="email">`,
  `<input type="number">`, `<input type="password">`, `<input type="search">`,
  `<input type="tel">`, `<input type="text">`, and `<input type="url">` fields, and
  single-choice `<select>` controls with `<option>` entries, and multiline
  `<textarea>` controls with `rows` and `cols`,
  `:focus`, Enter/Space button activation, UTF-8 text editing and selection
  (keyboard, mouse drag, double click, Ctrl+A, and XKB Compose sequences),
  Ctrl+C/Ctrl+X/Ctrl+V through the Wayland clipboard, keyboard repeat using
  the compositor's settings, horizontal input scrolling, vertical scrolling
  and clipping for `overflow-y: auto` blocks, visible scrollbars with
  draggable thumbs and page clicks, automatic reveal of controls focused
  with Tab or Shift+Tab, and resize handling. The application
  can request a redraw after external DOM
  mutations with `tbox_app_request_redraw()`.
- Unit/integration tests for the core pipeline and a separate, optional
  visual comparison tool (`tbox_cmp`).

The supported HTML/CSS subset is defined by the public headers under
`include/tbox/`; unsupported properties and elements may be ignored. There
is currently no multi-select, `<select size>`, `<optgroup>`, IME composition,
horizontal container scrolling, flex/grid
layout, accessibility tree, or Windows/macOS window backend. The library is
not yet suitable for a complete desktop application.

Tables support `<caption>`, `<colgroup>`, `<col>`, `<thead>`, `<tbody>`,
`<tfoot>`, `<tr>`, `<th>`, and `<td>`. Column and row spans share one grid
across sections; CSS `width` and `min-width` on columns and cells set minimum widths.
Captions can use `caption-side: top|bottom`; tables accept `border-spacing`
and `border-collapse: collapse`; cells accept `vertical-align:
top|middle|bottom`. Block content inside a cell gets its own layout boxes.
The parser expects explicitly closed table tags in this version and keeps
direct `<tr>` children without inserting a `<tbody>`.

Recent style support includes `font-weight: normal|bold|400|700`,
`font-style: normal|italic`, `white-space: normal|nowrap`,
`text-decoration: overline`, `text-decoration-color`, pixel or `em`
`text-decoration-thickness`, `word-spacing`, `text-indent`, and
`overflow-y: hidden`. Individual `margin-*` and `padding-*` sides work with
their shorthands; `background: <color>` works alongside `background-color`.
Uniform `outline` and `outline-width/style/color` paint outside the border.
`outline-offset` controls its gap, and borders or outlines can use
`currentColor`. Border widths accept `thin`, `medium`, and `thick` (1, 3,
and 5 pixels). `min-width` and `max-width` constrain content width in pixels,
`em`, or percentages; table cells use `min-width` when sizing columns.
`box-sizing: border-box` makes declared width and height include padding and
border. `line-height` accepts `normal`, unitless numbers, pixels, `em`, and
percentages; `letter-spacing` accepts pixels and `em`. `visibility: hidden`
preserves layout while hiding the element, and a descendant can explicitly
set `visibility: visible`. `text-overflow: ellipsis` truncates a single line
when paired with `white-space: nowrap` and `overflow: hidden`.
Uniform `border-width`, `border-style` (`solid`/`none`),
and `border-color` work alongside the `border` shorthand with normal cascade
precedence. Other white-space modes and per-side borders are not yet implemented.

`min-height` and `max-height` constrain box height in pixels, `em`, or
percentages when the containing block has a definite height. They work with
`box-sizing` and vertical scrolling; `min-height` wins when it exceeds
`max-height`. `font-weight` accepts numeric values from 100 to 900 in steps of
100, mapped to
the available regular (100–500) and bold (600–900) faces. `font-style:
oblique` uses the italic face. `background` and `background-color` accept
`currentColor`.

`font-size` also accepts `xx-small`, `x-small`, `small`, `medium`, `large`,
`x-large`, and `xx-large` on a fixed 9/10/13/16/18/24/32px scale.
`smaller` and `larger` scale the inherited size by 1/1.2 and 1.2.
`border-radius` accepts one to four circular values in pixels or `em`, plus
`border-top-left-radius`, `border-top-right-radius`,
`border-bottom-right-radius`, and `border-bottom-left-radius`. Percentage
and elliptical radii are not supported.

`overflow-wrap: break-word` breaks a text word at UTF-8 codepoint boundaries
when it cannot fit on an empty line; `normal` restores the default overflow.
It does not override `white-space: nowrap` or the preserved lines in `<pre>`.
`pointer-events: none` removes an element from pointer hit testing while
keeping it visible; descendants can use `pointer-events: auto` to receive
pointer events again. Keyboard focus is unaffected.

`inset` sets `top`, `right`, `bottom`, and `left` with one to four values.
Logical `margin-block`, `margin-inline`, `padding-block`, `padding-inline`,
`inset-block`, `inset-inline`, and their `-start`/`-end` longhands map to
physical sides for left-to-right text. `font-weight` accepts `bolder` and
`lighter`; `overflow: clip` acts as `hidden` and `overflow: scroll` as
`auto`; `border-style: hidden` hides the border. `text-decoration` accepts a
line, color, and thickness together (`underline red 2px`), alongside
`text-decoration-line` and `text-underline-offset`. `vertical-align` also
accepts `text-top`, `text-bottom`, lengths, and percentages of the
line-height. `accent-color` colors checked checkboxes and radio buttons, and
`caret-color` colors the text insertion caret.

`list-style-type` (and the type keyword of `list-style`) selects `disc`,
`circle`, `square`, `decimal`, `lower-alpha`, `upper-alpha`, `lower-roman`,
`upper-roman`, or `none` list markers. `text-transform` accepts `uppercase`,
`lowercase`, and `capitalize`. `text-shadow` paints one shadow with an
optional approximated blur, `box-shadow` accepts a spread radius and
defaults its color to `currentColor`, and `word-break: break-all` breaks
lines between any two characters.

## Build and checks

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To browse the HTML fixtures in `tests/assets`, run
`build/example/tbox_assets_showrun`. It opens the first `.html` file in
alphabetical order; Right Arrow loads the next file and Left Arrow loads the
previous one. Escape closes the window. The current filename is printed in
the terminal. `--list` prints the browsing order without opening a window;
an optional directory argument replaces `tests/assets`.

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
A fixed-height list below the form scrolls with the mouse wheel. Drag its
scrollbar thumb or click the track to move a page at a time. Tab and Shift+Tab
move through its buttons and scroll the focused one into view.
An `<input type="button" value="Label">` displays its value as the label and
activates `tbox_context_on_click()` handlers with a mouse click, Enter, or
Space. A disabled input button does not activate.
An `<input type="checkbox">` toggles with a mouse click or Space. Its live
state is the presence of `checked` in the DOM; click handlers run after the
toggle and can read it with `tbox_html_node_get_attribute()`. Use
`tbox_html_node_set_attribute()` and `tbox_html_node_remove_attribute()` to
change it programmatically. The `:checked` selector styles checked controls.
Disabled checkboxes do not activate.
An `<input type="radio" name="choice">` is selected with a click or Space.
Selecting it clears `checked` from other radios with the same `name` in the
same form; `:checked` reflects the live state. Disabled radios do not activate.
An `<input type="range">` shows a slider. Click or drag the thumb, use arrow
keys to change by `step`, or use Home/End for the limits. Its value is kept in
the DOM and reported through `tbox_context_on_input()`; defaults are 0–100.
An `<input type="reset">` inside a `<form>` restores the form's initial input,
textarea, and select state with a click, Enter, or Space. Its `value` is the
label, defaulting to “Reset”.
An `<input type="submit">` activates the enclosing form with a click, Enter,
or Space. Register `tbox_context_on_submit()` to receive the form and the
submit button. Enter in a single-line field also submits its form, with a
null submitter. Its default label is “Submit”.
An `<input type="search">` edits like a text field and has a clear button;
Escape clears its value while focused. Changes call `tbox_context_on_input()`.
An `<input type="color" value="#3366cc">` shows a color swatch. Click it or
press Enter/Space while focused to open the RGB picker. Click or drag a channel
bar to change its value; Up/Down select a channel, Left/Right adjust it, Home/End
set its limits, and Escape closes the picker. Changes update the `value`
attribute as lowercase `#rrggbb` and call `tbox_context_on_input()`.
An `<input type="date" value="2026-09-24">` shows its ISO date and opens a
calendar with a mouse click or Enter/Space. Click a day or move with the arrow
keys and confirm with Enter/Space; Shift+Left/Right changes month, Home/End
moves to the first/last day, and Escape closes it. `min` and `max` restrict
selectable dates. A selection updates `value` as `YYYY-MM-DD` and calls
`tbox_context_on_input()`.
An `<input type="datetime-local" value="2026-09-24T14:30">` uses the same
calendar, with hour and minute bars and an OK button. Click or drag the bars,
or use Shift+Up/Down for hours and Ctrl+Up/Down for minutes while the picker is
open. Enter/Space confirms; `min` and `max` include the time. The stored value
uses `YYYY-MM-DDTHH:MM` with no timezone conversion.
An `<input type="month" value="2026-09">` opens a grid of twelve months.
Arrow keys move by one or four months, Shift+Left/Right changes the year,
Home/End selects January/December, and Enter/Space confirms. A click on a
month selects it directly. `min` and `max` use `YYYY-MM`; changes update the
DOM `value` and call `tbox_context_on_input()`.
An `<input type="time" value="14:30">` opens hour and minute bars. Use the
mouse, or Up/Down for hours and Ctrl+Up/Down for minutes; Enter confirms.
`min` and `max` accept `HH:MM` values. Invalid initial values are cleared.
An `<input type="week" value="2026-W39">` opens a calendar and selects ISO
weeks. Click a day to choose its week; Left/Right and Up/Down move by one or
four weeks. `min` and `max` accept `YYYY-Www` values. The selected week is
stored in the DOM and reported through `tbox_context_on_input()`.
An `<input type="email">` edits like a single-line text field. Its live
`value` is available through the DOM and `tbox_context_on_input()`;
`tbox_context_email_valid()` checks ASCII address syntax, `required`, and
addresses separated by commas when `multiple` is present.
An `<input type="number">` accepts decimal numbers, including exponent
notation. Up/Down or the small arrow buttons change the value by `step`
(default 1); Shift+Up/Down changes it by ten steps. `min` and `max` limit stepping. The live `value` is in the DOM,
and `tbox_context_number_valid()` checks syntax, `required`, bounds, and step.
An `<input type="password">` edits like a text field but displays one mask
glyph per character. Its actual value remains available in the DOM and input
callback. Clipboard copy and cut do not expose the selected password.
An `<input type="tel">` edits as a single-line text field without imposing a
phone-number format. An `<input type="url">` also edits as text;
`tbox_context_url_valid()` checks `required` and basic absolute URL syntax.
An `<input type="file">` opens an in-app picker rooted at the process's current
directory. Choose one file by mouse or keyboard; folders can be opened and
the list scrolled. Its `value` attribute contains the selected file name,
while `tbox_context_file_path()` returns the absolute path. Selection calls
`tbox_context_on_input()` with the file name. The current picker selects one
file; `multiple` and `accept` filtering are not implemented yet.
An `<input type="hidden">` keeps its `value` in the DOM without occupying
space, painting, receiving pointer clicks, or joining keyboard focus order.
An `<input type="image" src="button.png" alt="Send">` displays the image at
its intrinsic size or the size set by `width` and `height`. It activates
`tbox_context_on_click()` handlers with a click, Enter, or Space. If the image
cannot be loaded, its `alt` text is shown. Disabled image inputs do not activate.
The select control supports direct child `<option>` elements, mouse choice,
Up/Down/Home/End, Enter/Space to
open or confirm, and Escape to dismiss. Its value is available through
`tbox_context_select_value()` or `tbox_context_on_select()`; applications can
set it with `tbox_context_select_set_value()`.
The textarea reads its initial value from the text between its tags. Enter
inserts a new line; Up/Down move between visual lines. Long text wraps and
scrolls inside the control. `rows` and `cols` set its initial size, while
explicit CSS width and height take precedence.
A repeat rate of zero disables repetition; until the compositor sends its
settings, repetition stays disabled.
The example also compiles
without Wayland or Fontconfig, but needs both to open a window.

`build/tests/tbox_cmp tests/assets` compares the renderer against image
references when OpenCV and Fontconfig are installed. It reports SSIM for
diagnosis and uses local content, edge, and color checks for its verdict,
allowing small glyph and position differences. Annotated images are written
to the temporary directory printed at startup. Its current fixtures include
unsupported features, so a `DIFFERENT` result is diagnostic rather than a
test failure. The former optional SSIM threshold is accepted with a warning
and no longer affects the verdict.

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
