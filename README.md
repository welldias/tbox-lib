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
horizontal container scrolling, grid layout, floats, accessibility tree, or
Windows/macOS window backend. The library is
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
`border-width`, `border-style` (`solid`, `dashed`, `dotted`, `double`,
`none`, `hidden`; `groove`/`ridge`/`inset`/`outset` paint solid), and
`border-color` take one to four values and work alongside the `border`
shorthand, the per-side `border-top`/`-right`/`-bottom`/`-left` shorthands,
and their `-width`/`-style`/`-color` longhands, with normal cascade
precedence. `white-space` accepts `normal`, `nowrap`, `pre`, `pre-wrap`,
and `pre-line`.

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
`border-bottom-right-radius`, and `border-bottom-left-radius`. Percentages
resolve against the smaller side of the border box, so `50%` makes a square
a circle; elliptical radii are not supported, so a rectangle becomes a pill
rather than an ellipse.

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
lines between any two characters. `opacity` (a number or percentage) fades
an element and its descendants; overlapping descendants blend individually
rather than as one flattened group.

`display: inline-block` places a box inside a line of text: it shrinks to
its content (or keeps its declared width), wraps with the words around it,
sits on their baseline (its last line's baseline), and accepts
`vertical-align: top|middle|bottom`, which also grow the line when needed.
Buttons, inputs, selects, and textareas are inline-blocks by default, as in
browsers. An `<img>` with `display: block`, or as a flex item, is a
replaced box sized from its image (keeping its aspect ratio). Inline elements nest to any depth,
each keeping its own style, and words from different elements are only
separated where the source has whitespace.

`display: flex` and `inline-flex` lay out children as flex items with
`flex-direction` (including reversed), `flex-wrap`, `flex-flow`,
`justify-content`, `align-items`, `align-self`, `align-content`,
`gap`/`row-gap`/`column-gap`, `flex-grow`, `flex-shrink`, `flex-basis`, the
`flex` shorthand, `order`, auto margins, and min/max sizes (with the
automatic minimum size of flex items). Loose text in a flex container
becomes an anonymous item. Without a definite height, a container still
honours its `min-height`/`max-height`: a column flexes its items into that
range (and wraps at `max-height`), and a single-line row stretches and
aligns its items within it.

`text-align` also accepts `justify`, `start`, and `end`. The `font`
shorthand sets style, weight, size, line-height, and family (variant and
stretch keywords are accepted and ignored; system fonts are not supported).
An absolutely positioned box with auto width or height stretches between
`left`/`right` or `top`/`bottom`. Outlines accept the same dashed, dotted,
and double styles as borders.

Logical `border-block-start/end` and `border-inline-start/end` shorthands,
plus their `-width`, `-style`, and `-color` longhands, map to physical sides
in left-to-right writing mode. Selectors support `:only-child`, `:empty`,
`:disabled`, and `:enabled`; form-state matching uses the element's own
`disabled` attribute. `text-decoration-line` and `text-decoration` can
combine underline, overline, and line-through; `text-decoration-style`
supports solid, dashed, dotted, and double strokes. Images support
`object-fit: fill|contain|cover|none|scale-down`, centered in the CSS image box. See
`tests/assets/040.html` for examples.
`object-position` aligns the fitted image with `left`, `center`, `right`,
`top`, `bottom`, percentages, or pixel/`em` offsets (one or two values;
three/four-value edge-offset syntax is not supported).
See `tests/assets/041.html` for positioning examples.
`none` keeps intrinsic image pixels and clips overflow; `scale-down` picks
the smaller result of `none` and `contain`. See `tests/assets/043.html`.
`background-clip: border-box|padding-box|content-box` limits a solid
background color to the selected box, including rounded corners. See
`tests/assets/042.html` for a side-by-side example.
Structural selectors also include `:root`, `:first-of-type`,
`:last-of-type`, and `:only-of-type`. See `tests/assets/044.html`.
`image-rendering: pixelated` uses nearest-neighbor sampling when enlarging
images; `auto` keeps smooth scaling. The property is inherited. See
`tests/assets/045.html` for a comparison.
Structural selectors also accept `:nth-child(an+b)` and
`:nth-of-type(an+b)`, including `odd`, `even`, and fixed indices. See
`tests/assets/046.html`.
`:nth-last-child(an+b)` and `:nth-last-of-type(an+b)` use the same formulas
while counting from the last sibling. See `tests/assets/047.html`.
The general sibling combinator `~` matches later siblings even when other
elements appear between them. See `tests/assets/048.html`.
`:not()` excludes one simple type, universal, ID, class, or attribute
selector. See `tests/assets/049.html`.
`:required` and `:optional` match eligible `input`, `select`, and `textarea`
controls by the presence of `required`. See `tests/assets/050.html`.
`:lang()` matches an element's nearest inherited `lang` attribute, including
regional subtags such as `pt-BR` for `:lang(pt)`. See `tests/assets/051.html`.
`image-rendering: crisp-edges` uses the same nearest-neighbor enlargement as
`pixelated`. See `tests/assets/052.html`.
`:focus-within` matches a focused element and its ancestors, and `:link`/
`:any-link` match links with `href` (all links are unvisited here). See
`tests/assets/053.html` and `tests/assets/054.html`.
`list-style-type: decimal-leading-zero` formats 1–9 as 01–09; see
`tests/assets/055.html`. Attribute selectors accept `^=`, `$=`, and `*=`
for prefix, suffix, and substring matching; see `tests/assets/056.html`.
`text-decoration-style: wavy` paints a wave for decoration lines; see
`tests/assets/057.html`. `overflow-wrap: anywhere` uses emergency breaks
and lets those breaks reduce min-content width, unlike `break-word`; see
`tests/assets/058.html`.
`inline-size` and `block-size` map to width and height in the current
horizontal left-to-right writing mode, including normal cascade priority;
see `tests/assets/059.html`. The four logical min/max size properties map
to their physical limits; see `tests/assets/060.html`.
`border-block` applies to top and bottom, while `border-inline` applies to
left and right; individual sides can override them. See `tests/assets/061.html`.
`text-underline-position: under` places the underline below the font's
descender unless `text-underline-offset` is explicit; see
`tests/assets/062.html`. `:hover` also matches ancestors of the hovered
element; see `tests/assets/063.html`. Attribute selectors accept the ASCII
case-insensitive `i` modifier for value comparisons, such as
`[data-label="ALPHA" i]`; see `tests/assets/064.html`.
Lengths accept `rem`, `ex`, `ch`, `pt`, `pc`, `in`, `cm`, `mm`, `Q` and,
with a viewport, `vw`/`vh`/`vmin`/`vmax`; see `tests/assets/065.html`.
`calc()` mixes units with `+ - * /` and parentheses, its percentage part
resolved by layout; see `tests/assets/066.html`. Every property accepts the
CSS-wide keywords `inherit`, `initial` and `unset` (`revert` acts as
`unset`); see `tests/assets/067.html`. `overflow-x` and the two-value
`overflow` clip horizontally (`visible` paired with another value computes
to `auto`); see `tests/assets/068.html`. `text-align-last` aligns a
paragraph's last line; see `tests/assets/069.html`.
`white-space: break-spaces` keeps spaces that take room and may wrap, and
`tab-size` (number or length) sets tab stops; see `tests/assets/070.html`.
List markers hang outside the content box by default, and
`list-style-position: inside` makes them the first word; see
`tests/assets/071.html`. Tables support `empty-cells: hide`,
`table-layout: fixed` and rows with `visibility: collapse`; see
`tests/assets/072.html`. `aspect-ratio` sizes an auto dimension from the
other one; see `tests/assets/073.html`. `cursor` (keywords, `url()`
entries skipped) sets the window's pointer through `wayland-cursor` when
available, and `tbox_context_cursor_at` reports it; `user-select` is parsed
and inherited by `none`/`all`. See `tests/assets/074.html`.
`border-radius` takes elliptical `horizontal / vertical` radii; see
`tests/assets/075.html`. `background-image: url()` with
`background-size`, `background-position` (including edge offsets such as
`right 10px bottom 20%`) and `background-repeat`, also through the
`background` shorthand; see `tests/assets/076.html`.
`linear-gradient()`, `radial-gradient()` and their `repeating-` forms paint
as background images; see `tests/assets/077.html`. `box-shadow` and
`text-shadow` accept comma-separated lists, and `box-shadow` takes `inset`;
see `tests/assets/078.html`. `z-index` orders positioned boxes within
stacking contexts (also created by `opacity` below 1), and hit testing
follows the same order; see `tests/assets/079.html`. `font-weight` keeps the
numeric weight (relative `bolder`/`lighter` included) and `font-stretch`
selects condensed/expanded faces through fontconfig; see
`tests/assets/080.html`. `font-variant: small-caps` synthesizes small
capitals; see `tests/assets/081.html`. `line-clamp`/`-webkit-line-clamp`
limit a block's lines and end the last one with an ellipsis; see
`tests/assets/082.html`. `transform: translate()`/`translateX()`/
`translateY()` and the `translate` property shift a box visually; see
`tests/assets/083.html`. The `transparent` color keyword is accepted
everywhere a color is.
Colors accept the CSS Color 4/5 syntax: space-separated `rgb()`/`hsl()`
with `/ alpha`, hue units, `none`, `hwb()`, `lab()`, `lch()`, `oklab()`,
`oklch()` and `color-mix()`; see `tests/assets/084.html`. `min()`, `max()`
and `clamp()` work alone or inside `calc()`, a percentage mixed with px
keeping the px values as bounds; see `tests/assets/085.html`. `width:
min-content | max-content | fit-content` size a box from its content; see
`tests/assets/086.html`. `place-content`, `place-items` and `place-self`
set the flex alignments; see `tests/assets/087.html`. `groove`, `ridge`,
`inset` and `outset` borders and outlines are shaded; see
`tests/assets/088.html`. `scrollbar-color` and `scrollbar-width`
(`thin`, `none`) style the scroll bars; see `tests/assets/089.html`.
`background-origin` and several comma-separated background layers; see
`tests/assets/090.html`. `text-wrap` (`nowrap`, `balance`),
`text-wrap-mode` and `white-space-collapse`; see `tests/assets/091.html`.
`text-overflow: "<string>"`; see `tests/assets/092.html`. `display:
flow-root` and `display: list-item` (with a marker when the content is
inline); see `tests/assets/093.html`. `text-indent` takes `hanging` and
`each-line`; see `tests/assets/094.html`. `clip-path: inset()`,
`circle()` and `ellipse()`; see `tests/assets/095.html`. `filter` color
functions (grayscale, sepia, saturate, hue-rotate, invert, opacity,
brightness, contrast; blur and drop-shadow are ignored); see
`tests/assets/096.html`. Custom properties (`--name`) and `var()` with
fallbacks; see `tests/assets/097.html`. `list-style-image` and `::marker`
(color, font and `content`); see `tests/assets/098.html`. Dashed and
dotted borders on rounded boxes; see `tests/assets/099.html`. `position:
sticky` holds boxes against their scrollport; see `tests/assets/100.html`.
`font-kerning` (on by default); see `tests/assets/101.html`. Soft hyphens
(`&shy;`) break with a visible hyphen unless `hyphens: none`; see
`tests/assets/102.html`.

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

### Styling picker popups

The popups opened by `input` types `color`, `date`, `datetime-local`, `month`,
`time`, `week`, and `file` have a separate stylesheet. Document CSS still
styles the input itself. The picker stylesheet uses internal `tbox-popup`
and `tbox-part` elements; these are selector targets, not DOM nodes returned
by the HTML API. The popup has a `type` attribute and copies the input's
`id` and classes, so one picker can be targeted with `tbox-popup#my-picker`.

```c
tbox_context_options options = tbox_context_options_default();
const char *theme =
    "tbox-popup[type=color] { width: 270px; background-color: #20242b; color: white; }"
    "tbox-popup[type=color] .track { width: 210px; height: 16px; }"
    "tbox-popup#my-picker .thumb { background-color: yellow; }";
options.control_css = theme;
options.control_css_length = strlen(theme);
tbox_context *ctx = tbox_context_open_with_options(html, html_length,
    document_css, document_css_length, fonts, images, options);
```

The application variants are `tbox_app_create_with_options`,
`tbox_app_create_from_files_with_options`, and
`tbox_app_screenshot_from_files_with_options`. A NULL `control_css` uses the
built-in theme. A non-NULL string replaces it completely, including an empty
string; missing rules then use structural sizes and CSS initial values.
The context copies the parsed CSS. The app also retains a copy for later
`tbox_app_load_from_files` calls.

Available part classes are `.track`, `.channel-label`, `.thumb`, and
`.preview` for color; `.heading`, `.nav`, `.weekday`, `.day`, `.month`,
`.time-track`, `.time-fill`, `.time-label`, and `.confirm` for calendar and
time; and `.back`, `.path`, `.divider`, `.row`, `.empty`, and `.footer` for
files. Parts can also carry `.is-active`, `.is-selected`, `.is-disabled`,
or `.is-directory`. The theme supports colors, backgrounds, font settings,
borders, radii, and the documented part dimensions. Geometry uses positive
`px` or `em` widths and heights; omitted or invalid sizes use structural
fallbacks. Set dimensions on base part selectors; state classes change
appearance without changing the interaction rectangles. Color samples and
file names remain value-dependent.

Geometry reads `width`/`height` on `tbox-popup`, `.track`, `.preview`,
`.nav`, `.day`, `.month`, `.time-track`, `.confirm`, and `.back`; it reads
`height` on `.row`, and `width`/`height` on `.thumb` for painting. The color
popup also reads its top/left padding and `row-gap` for bar placement.
Use the part and state classes for picker states and repeated items;
pseudo-classes and sibling combinators are not part of this theme API.

The runnable example [control_theme_demo.c](example/control_theme_demo.c)
loads the page stylesheet and the picker theme from separate files. On
Wayland, run `build/example/tbox_control_theme_demo` and click the inputs to
see the themed popups. Run it with `--default` to compare the built-in theme,
or pass a path to another picker CSS file. Its theme is in
`example/control_theme_demo.controls.css`; the second color input shows an
override selected by the input's id.

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
