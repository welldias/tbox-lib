# Referência — propriedades CSS suportadas

> - **Escopo:** lista de propriedades CSS e se o tbox as reconhece.
> - **Código relacionado:** `include/tbox/style.h`, `src/style/`, `src/css_cascade/`
> - **Veja também:** [HTML e CSS](../07-html-e-css.md), [Layout e pintura](../08-layout-e-pintura.md)

Legenda: `X` = suportado; `X*` = suportado com limitações (o capítulo do
assunto descreve quais); vazio = não suportado. Itens não suportados são
ignorados sem erro — o parser é tolerante.

| Supported | Name | Description |
| --- | --- | --- |
| X | accent-color | Specifies an accent color for user-interface controls |
| X | align-content | Specifies the alignment between the lines inside a flexible container when the items do not use all available space |
| X | align-items | Specifies the alignment for items inside a flexible container |
| X | align-self | Specifies the alignment for selected items inside a flexible container |
| X* | all | Resets all properties (except unicode-bidi and direction) |
| X* | animation | A shorthand property for all the animation-* properties |
| X* | animation-delay | Specifies a delay for the start of an animation |
| X* | animation-direction | Specifies whether an animation should be played forward, backward or in alternate cycles |
| X* | animation-duration | Specifies how long an animation should take to complete one cycle |
| X* | animation-fill-mode | Specifies a style for the element when the animation is not playing (before it starts, after it ends, or both) |
| X* | animation-iteration-count | Specifies the number of times an animation should be played |
| X* | animation-name | Specifies a name for the @keyframes animation |
| X* | animation-play-state | Specifies whether the animation is running or paused |
| X* | animation-timing-function | Specifies the speed curve of an animation |
| X | aspect-ratio | Specifies preferred aspect ratio of an element |
| X* | backdrop-filter | Defines a graphical effect to the area behind an element |
| X* | backface-visibility | Defines whether or not the back face of an element should be visible when facing the user |
| X* | background | A shorthand property for all the background-* properties |
| X | background-attachment | Sets whether a background image scrolls with the rest of the page, or is fixed |
| X | background-blend-mode | Specifies the blending mode of each background layer (color/image) |
| X* | background-clip | Defines how far the background (color or image) should extend within an element |
| X | background-color | Specifies the background color of an element |
| X* | background-image | Specifies one or more background images for an element |
| X | background-origin | Specifies the origin position of a background image |
| X | background-position | Specifies the position of a background image |
| X | background-position-x | Specifies the position of a background image on x-axis |
| X | background-position-y | Specifies the position of a background image on y-axis |
| X | background-repeat | Sets if/how a background image will be repeated |
| X | background-size | Specifies the size of the background images |
| X | block-size | Specifies the size of an element in block direction |
| X | border | A shorthand property for border-width, border-style and border-color |
| X | border-block | A shorthand property for border-block-width, border-block-style and border-block-color |
| X | border-block-color | Sets the color of the borders at start and end in the block direction |
| X | border-block-end | A shorthand property for border-block-end-width, border-block-end-style and border-block-end-color |
| X | border-block-end-color | Sets the color of the border at the end in the block direction |
| X | border-block-end-style | Sets the style of the border at the end in the block direction |
| X | border-block-end-width | Sets the width of the border at the end in the block direction |
| X | border-block-start | A shorthand property for border-block-start-width, border-block-start-style and border-block-start-color |
| X | border-block-start-color | Sets the color of the border at the start in the block direction |
| X | border-block-start-style | Sets the style of the border at the start in the block direction |
| X | border-block-start-width | Sets the width of the border at the start in the block direction |
| X | border-block-style | Sets the style of the borders at start and end in the block direction |
| X | border-block-width | Sets the width of the borders at start and end in the block direction |
| X | border-bottom | A shorthand property for border-bottom-width, border-bottom-style and border-bottom-color |
| X | border-bottom-color | Sets the color of the bottom border |
| X | border-bottom-left-radius | Defines the radius of the border of the bottom-left corner |
| X | border-bottom-right-radius | Defines the radius of the border of the bottom-right corner |
| X | border-bottom-style | Sets the style of the bottom border |
| X | border-bottom-width | Sets the width of the bottom border |
| X | border-collapse | Sets whether table borders should collapse into a single border or be separated |
| X | border-color | Sets the color of the four borders |
| X | border-end-end-radius | Sets the radius of the corner between the block-end and the inline-end sides of the element |
| X | border-end-start-radius | Sets the radius of the corner between the block-end and the inline-start sides of the element |
| | border-image | A shorthand property for all the border-image-* properties |
| | border-image-outset | Specifies the amount by which the border image area extends beyond the border box |
| | border-image-repeat | Specifies whether the border image should be repeated, rounded or stretched |
| | border-image-slice | Specifies how to slice the border image |
| | border-image-source | Specifies the path to the image to be used as a border |
| | border-image-width | Specifies the width of the border image |
| X | border-inline | A shorthand property for border-inline-width, border-inline-style and border-inline-color |
| X | border-inline-color | Sets the color of the borders at start and end in the inline direction |
| X | border-inline-end | A shorthand property for border-inline-end-width, border-inline-end-style and border-inline-end-color |
| X | border-inline-end-color | Sets the color of the border at the end in the inline direction |
| X | border-inline-end-style | Sets the style of the border at the end in the inline direction |
| X | border-inline-end-width | Sets the width of the border at the end in the inline direction |
| X | border-inline-start | A shorthand property for border-inline-start-width, border-inline-start-style and border-inline-start-color |
| X | border-inline-start-color | Sets the color of the border at the start in the inline direction |
| X | border-inline-start-style | Sets the style of the border at the start in the inline direction |
| X | border-inline-start-width | Sets the width of the border at the start in the inline direction |
| X | border-inline-style | Sets the style of the borders at start and end in the inline direction |
| X | border-inline-width | Sets the width of the borders at start and end in the inline direction |
| X | border-left | A shorthand property for all the border-left-* properties |
| X | border-left-color | Sets the color of the left border |
| X | border-left-style | Sets the style of the left border |
| X | border-left-width | Sets the width of the left border |
| X | border-radius | A shorthand property for the four border-*-radius properties |
| X | border-right | A shorthand property for all the border-right-* properties |
| X | border-right-color | Sets the color of the right border |
| X | border-right-style | Sets the style of the right border |
| X | border-right-width | Sets the width of the right border |
| X | border-spacing | Sets the distance between the borders of adjacent cells |
| X | border-start-end-radius | Sets the radius of the corner between the block-start and the inline-end sides of the element |
| X | border-start-start-radius | Sets the radius of the corner between the block-start and the inline-start sides of the element |
| X | border-style | Sets the style of the four borders |
| X | border-top | A shorthand property for border-top-width, border-top-style and border-top-color |
| X | border-top-color | Sets the color of the top border |
| X | border-top-left-radius | Defines the radius of the border of the top-left corner |
| X | border-top-right-radius | Defines the radius of the border of the top-right corner |
| X | border-top-style | Sets the style of the top border |
| X | border-top-width | Sets the width of the top border |
| X | border-width | Sets the width of the four borders |
| X | bottom | Sets the elements position, from the bottom of its parent element |
| | box-decoration-break | Sets the behavior of the background and border of an element at page-break, or, for in-line elements, at line-break. |
| | box-reflect | The box-reflect property is used to create a reflection of an element. |
| X | box-shadow | Attaches one or more shadows to an element |
| X | box-sizing | Defines how the width and height of an element are calculated: should they include padding and borders, or not |
| X* | break-after | Specifies whether or not a page-, column-, or region-break should occur after the specified element |
| X* | break-before | Specifies whether or not a page-, column-, or region-break should occur before the specified element |
| X* | break-inside | Specifies whether or not a page-, column-, or region-break should occur inside the specified element |
| X | caption-side | Specifies the placement of a table caption |
| X | caret-color | Specifies the color of the cursor (caret) in inputs, textareas, or any element that is editable |
| X* | @charset | Specifies the character encoding used in the style sheet |
| X* | clear | Specifies what should happen with the element that is next to a floating element |
| X* | clip | Deprecated in favor of clip-path. Clips an absolutely positioned element |
| X* | clip-path | Clips an element to a basic shape or to an SVG source |
| X | color | Sets the color of text |
| X* | color-scheme | Indicates which operating system color scheme an element should render with |
| | column-count | Specifies the number of columns an element should be divided into |
| | column-fill | Specifies how to fill columns, balanced or not |
| X | column-gap | Specifies the gap between the columns |
| | column-rule | A shorthand property for all the column-rule-* properties |
| | column-rule-color | Specifies the color of the rule between columns |
| | column-rule-style | Specifies the style of the rule between columns |
| | column-rule-width | Specifies the width of the rule between columns |
| | column-span | Specifies how many columns an element should span across |
| | column-width | Specifies the column width |
| | columns | A shorthand property for column-width and column-count |
| | @container | Define styles for elements in container, depending on the container's size or style |
| X* | content | Used with the :before and :after pseudo-elements, to insert generated content |
| X* | counter-increment | Increases or decreases the value of one or more CSS counters |
| X* | counter-reset | Creates or resets one or more CSS counters |
| X* | counter-set | Creates or sets one or more CSS counters |
| | @counter-style | Lets you define your own counter styles |
| X* | cursor | Specifies the mouse cursor to be displayed when pointing over an element |
| X* | direction | Specifies the text direction/writing direction |
| X* | display | Specifies how a certain HTML element should be displayed |
| X | empty-cells | Specifies whether or not to display borders and background on empty cells in a table |
| X* | filter | Defines effects (e.g. blurring or color shifting) on an element before the element is displayed |
| X | flex | A shorthand property for the flex-grow, flex-shrink, and the flex-basis properties |
| X | flex-basis | Specifies the initial length of a flexible item |
| X | flex-direction | Specifies the direction of the flexible items |
| X | flex-flow | A shorthand property for the flex-direction and the flex-wrap properties |
| X | flex-grow | Specifies how much the item will grow relative to the rest |
| X | flex-shrink | Specifies how the item will shrink relative to the rest |
| X | flex-wrap | Specifies whether the flexible items should wrap or not |
| | float | Specifies whether an element should float to the left, right, or not at all |
| X* | font | A shorthand property for the font-style, font-variant, font-weight, font-size/line-height, and the font-family properties |
| X* | @font-face | Specifies a custom font to use to display text |
| X* | font-family | Specifies the font family for text |
| | font-feature-settings | Allows control over advanced typographic features in OpenType fonts |
| X | font-kerning | Controls the usage of the kerning information (how letters are spaced) |
| | font-language-override | Controls the usage of language-specific glyphs in a typeface |
| | @font-palette-values | Allows you to customize the default values of a font-palette |
| X | font-size | Specifies the font size of text |
| X* | font-size-adjust | Preserves the readability and size of text when fallback font occurs |
| X | font-stretch | Selects a normal, condensed, or expanded face from a font family |
| X | font-style | Specifies the font style for text |
| X* | font-synthesis | Controls which missing typefaces (bold or italic) may be synthesized by the browser |
| X* | font-variant | Specifies whether or not a text should be displayed in a small-caps font |
| | font-variant-alternates | Controls the usage of alternate glyphs associated to alternative names defined in @font-feature-values |
| X* | font-variant-caps | Controls the usage of alternate glyphs for capital letters |
| | font-variant-east-asian | Controls the usage of alternate glyphs for East Asian scripts (e.g Japanese and Chinese) |
| | font-variant-ligatures | Controls which ligatures and contextual forms are used in textual content of the elements it applies to |
| | font-variant-numeric | Controls the usage of alternate glyphs for numbers, fractions, and ordinal markers |
| X* | font-variant-position | Controls the usage of alternate glyphs of smaller size positioned as superscript or subscript regarding the baseline of the font |
| X | font-weight | Specifies the weight of a font |
| X | gap | A shorthand property for the row-gap and the column-gap properties |
| | grid | A shorthand property for the grid-template-rows, grid-template-columns, grid-template-areas, grid-auto-rows, grid-auto-columns, and the grid-auto-flow properties |
| | grid-area | Either specifies a name for the grid item, or this property is a shorthand property for the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties |
| | grid-auto-columns | Specifies a default column size |
| | grid-auto-flow | Specifies how auto-placed items are inserted in the grid |
| | grid-auto-rows | Specifies a default row size |
| | grid-column | A shorthand property for the grid-column-start and the grid-column-end properties |
| | grid-column-end | Specifies where to end the grid item |
| | grid-column-start | Specifies where to start the grid item |
| | grid-row | A shorthand property for the grid-row-start and the grid-row-end properties |
| | grid-row-end | Specifies where to end the grid item |
| | grid-row-start | Specifies where to start the grid item |
| | grid-template | A shorthand property for the grid-template-rows, grid-template-columns and grid-areas properties |
| | grid-template-areas | Specifies how to display columns and rows, using named grid items |
| | grid-template-columns | Specifies the size of the columns, and how many columns in a grid layout |
| | grid-template-rows | Specifies the size of the rows in a grid layout |
| | hanging-punctuation | Specifies whether a punctuation character may be placed outside the line box |
| X | height | Sets the height of an element |
| X* | hyphenate-character | Sets the character used at the end of line, before a hyphenation break |
| X* | hyphens | Sets how to split words to improve the layout of text |
| X | image-rendering | Specifies the type of algorithm to use for image scaling |
| X* | @import | Allows you to import a style sheet into another style sheet |
| | initial-letter | Specifies the size of the initial-letter and optionally the number of lines the initial letter should sink (down in the text) |
| X | inline-size | Specifies the size of an element in the inline direction |
| X | inset | Specifies the distance between an element and the parent element |
| X | inset-block | Specifies the distance between an element and the parent element in the block direction |
| X | inset-block-end | Specifies the distance between the end of an element and the parent element in the block direction |
| X | inset-block-start | Specifies the distance between the start of an element and the parent element in the block direction |
| X | inset-inline | Specifies the distance between an element and the parent element in the inline direction |
| X | inset-inline-end | Specifies the distance between the end of an element and the parent element in the inline direction |
| X | inset-inline-start | Specifies the distance between the start of an element and the parent element in the inline direction |
| X | isolation | Defines whether an element must create a new stacking content |
| X | justify-content | Specifies the alignment between the items inside a flexible container when the items do not use all available space |
| | justify-items | Is set on the grid container. Specifies the alignment of grid items in the inline direction |
| | justify-self | Is set on the grid item. Specifies the alignment of the grid item in the inline direction |
| X* | @keyframes | Controls the steps in an animation by defining styles for points along the animation sequence |
| X* | @layer | Controls how the CSS cascade layers evaluates the order of styles |
| X | left | Specifies the left position of a positioned element |
| X | letter-spacing | Increases or decreases the space between characters in a text |
| X* | line-break | Specifies how/if to break lines |
| X | line-clamp | Limits the contents of a block to the specified number of lines |
| X | -webkit-line-clamp | Legacy prefixed form of line-clamp (used with display: -webkit-box) |
| X | line-height | Sets the line height |
| X* | list-style | Sets all the properties for a list in one declaration |
| X | list-style-image | Specifies an image as the list-item marker |
| X | list-style-position | Specifies the position of the list-item markers (bullet points) |
| X* | list-style-type | Specifies the type of list-item marker |
| X | margin | Sets all the margin properties in one declaration |
| X | margin-block | Specifies the margin in the block direction |
| X | margin-block-end | Specifies the margin at the end in the block direction |
| X | margin-block-start | Specifies the margin at the start in the block direction |
| X | margin-bottom | Sets the bottom margin of an element |
| X | margin-inline | Specifies the margin in the inline direction |
| X | margin-inline-end | Specifies the margin at the end in the inline direction |
| X | margin-inline-start | Specifies the margin at the start in the inline direction |
| X | margin-left | Sets the left margin of an element |
| X | margin-right | Sets the right margin of an element |
| X | margin-top | Sets the top margin of an element |
| | marker | Points to a marker that will be drawn on all vertices of an element's path (the first, middle, and last) |
| | marker-end | Points to a marker that will be drawn on the last vertex of an element's path |
| | marker-mid | Points to a marker that will be drawn on all the middle vertices of an element's path |
| | marker-start | Points to a marker that will be drawn on the first vertex of an element's path |
| X* | mask | A shorthand property for mask-image, mask-mode, mask-repeat, mask-position, mask-clip, mask-origin, mask-size and mask-composite |
| X* | mask-clip | Specifies which area is affected by a mask image |
| | mask-composite | Specifies a compositing operation used on the current mask layer with the mask layers below it |
| X* | mask-image | Specifies an image to be used as a mask layer for an element |
| X* | mask-mode | Specifies whether the mask layer image is treated as a luminance mask or as an alpha mask |
| X* | mask-origin | Specifies the origin position (the mask position area) of a mask layer image |
| X* | mask-position | Sets the starting position of a mask layer image (relative to the mask position area) |
| X* | mask-repeat | Specifies how the mask layer image is repeated |
| X* | mask-size | Specifies the size of a mask layer image |
| | mask-type | Specifies whether an SVG <mask> element is treated as a luminance mask or as an alpha mask |
| X | max-block-size | Sets the maximum size of an element in the block direction |
| X | max-height | Sets the maximum height of an element |
| X | max-inline-size | Sets the maximum size of an element in the inline direction |
| X | max-width | Sets the maximum width of an element |
| X* | @media | Sets the style rules for different media types/devices/sizes |
| X | min-block-size | Sets the minimum size of an element in the block direction |
| X | min-height | Sets the minimum height of an element |
| X | min-inline-size | Sets the minimum size of an element in the inline direction |
| X | min-width | Sets the minimum width of an element |
| X | mix-blend-mode | Specifies how an element's content should blend with its direct parent background |
| X* | @namespace | Defines an XML namespace to be used in the style sheet |
| X | object-fit | Specifies how the contents of a replaced element should be fitted to the box established by its used height and width |
| X | object-position | Specifies the alignment of the replaced element inside its box |
| | offset | A shorthand property for the offset-anchor, offset-distance, offset-path, offset-position, and the offset-rotate properties |
| | offset-anchor | Specifies a point on an element that is fixed to the path it is animated along |
| | offset-distance | Specifies the position along a path where an animated element is placed |
| | offset-path | Specifies the path an element is animated along |
| | offset-position | Specifies the initial position of an element along a path |
| | offset-rotate | Specifies rotation of an element as it is animated along a path |
| X | opacity | Sets the opacity level for an element |
| X | order | Sets the order of the flexible item, relative to the rest |
| X* | orphans | Sets the minimum number of lines that must be left at the bottom of a page or column |
| X | outline | A shorthand property for the outline-width, outline-style, and the outline-color properties |
| X | outline-color | Sets the color of an outline |
| X | outline-offset | Offsets an outline, and draws it beyond the border edge |
| X | outline-style | Sets the style of an outline |
| X | outline-width | Sets the width of an outline |
| X* | overflow | Specifies what happens if content overflows an element's box |
| X* | overflow-anchor | Specifies whether or not content in viewable area in a scrollable contianer should be pushed down when new content is loaded above |
| X | overflow-wrap | Specifies whether or not the browser can break lines with long words, if they overflow the container |
| X* | overflow-x | Specifies whether or not to clip the left/right edges of the content, if it overflows the element's content area |
| X* | overflow-y | Specifies whether or not to clip the top/bottom edges of the content, if it overflows the element's content area |
| X | overscroll-behavior | Specifies whether to have scroll chaining or overscroll affordance in x- and y-directions |
| X | overscroll-behavior-block | Specifies whether to have scroll chaining or overscroll affordance in the block direction |
| X* | overscroll-behavior-inline | Specifies whether to have scroll chaining or overscroll affordance in the inline direction |
| X* | overscroll-behavior-x | Specifies whether to have scroll chaining or overscroll affordance in x-direction |
| X | overscroll-behavior-y | Specifies whether to have scroll chaining or overscroll affordance in y-directions |
| X | padding | A shorthand property for all the padding-* properties |
| X | padding-block | Specifies the padding in the block direction |
| X | padding-block-end | Specifies the padding at the end in the block direction |
| X | padding-block-start | Specifies the padding at the start in the block direction |
| X | padding-bottom | Sets the bottom padding of an element |
| X | padding-inline | Specifies the padding in the inline direction |
| X | padding-inline-end | Specifies the padding at the end in the inline direction |
| X | padding-inline-start | Specifies the padding at the start in the inline direction |
| X | padding-left | Sets the left padding of an element |
| X | padding-right | Sets the right padding of an element |
| X | padding-top | Sets the top padding of an element |
| | @page | Customizes the dimension, orientation, and margins of printed pages |
| X* | page-break-after | Sets the page-break behavior after an element. Replaced by break-after property |
| X* | page-break-before | Sets the page-break behavior before an element. Replaced by break-before property |
| X* | page-break-inside | Sets the page-break behavior inside an element. Replaced by break-inside property |
| | paint-order | Sets the order of how an SVG element or text is painted. |
| X* | perspective | Gives a 3D-positioned element some perspective |
| X* | perspective-origin | Defines at which position the user is looking at the 3D-positioned element |
| X | place-content | Specifies align-content and justify-content property values for flexbox and grid layouts |
| X | place-items | Specifies align-items and justify-items property values for grid layouts |
| X | place-self | Specifies align-self and justify-self property values for grid layouts |
| X | pointer-events | Defines whether or not an element reacts to pointer events |
| X | position | Specifies the type of positioning method used for an element (static, relative, absolute or fixed) |
| | @property | Defines custom CSS properties directly in the stylesheet without having to run any JavaScript |
| X* | quotes | Sets the type of quotation marks for embedded quotations |
| | resize | Defines if (and how) an element is resizable by the user |
| X | right | Specifies the right position of a positioned element |
| | rotate | Specifies the rotation of an element |
| X | row-gap | Specifies the gap between the grid rows |
| | scale | Specifies the size of an element by scaling up or down |
| | @scope | Allows you to select elements in specific DOM subtrees and target elements precisely without writing overly-specific selectors |
| X* | scroll-behavior | Specifies whether to smoothly animate the scroll position in a scrollable box, instead of a straight jump |
| X* | scroll-margin | Specifies the margin between the snap position and the container |
| X* | scroll-margin-block | Specifies the margin between the snap position and the container in the block direction |
| X* | scroll-margin-block-end | Specifies the end margin between the snap position and the container in the block direction |
| X* | scroll-margin-block-start | Specifies the start margin between the snap position and the container in the block direction |
| X* | scroll-margin-bottom | Specifies the margin between the snap position on the bottom side and the container |
| X* | scroll-margin-inline | Specifies the margin between the snap position and the container in the inline direction |
| X* | scroll-margin-inline-end | Specifies the end margin between the snap position and the container in the inline direction |
| X* | scroll-margin-inline-start | Specifies the start margin between the snap position and the container in the inline direction |
| X* | scroll-margin-left | Specifies the margin between the snap position on the left side and the container |
| X* | scroll-margin-right | Specifies the margin between the snap position on the right side and the container |
| X* | scroll-margin-top | Specifies the margin between the snap position on the top side and the container |
| X* | scroll-padding | Specifies the distance from the container to the snap position on the child elements |
| X* | scroll-padding-block | Specifies the distance in block direction from the container to the snap position on the child elements |
| X* | scroll-padding-block-end | Specifies the distance in block direction from the end of the container to the snap position on the child elements |
| X* | scroll-padding-block-start | Specifies the distance in block direction from the start of the container to the snap position on the child elements |
| X* | scroll-padding-bottom | Specifies the distance from the bottom of the container to the snap position on the child elements |
| X* | scroll-padding-inline | Specifies the distance in inline direction from the container to the snap position on the child elements |
| X* | scroll-padding-inline-end | Specifies the distance in inline direction from the end of the container to the snap position on the child elements |
| X* | scroll-padding-inline-start | Specifies the distance in inline direction from the start of the container to the snap position on the child elements |
| X* | scroll-padding-left | Specifies the distance from the left side of the container to the snap position on the child elements |
| X* | scroll-padding-right | Specifies the distance from the right side of the container to the snap position on the child elements |
| X* | scroll-padding-top | Specifies the distance from the top of the container to the snap position on the child elements |
| X* | scroll-snap-align | Specifies where to position elements when the user stops scrolling |
| X* | scroll-snap-stop | Specifies scroll behaviour after fast swipe on trackpad or touch screen |
| X* | scroll-snap-type | Specifies how snap behaviour should be when scrolling |
| X | scrollbar-color | Specifies the color of the scrollbar of an element |
| X* | scrollbar-gutter | Reserves space for the scrollbar so content does not shift when it appears |
| X | scrollbar-width | Specifies the thickness of the scrollbar (auto, thin or none) |
| | shape-outside | Defines a shape for wrapping for the inline content |
| X* | @starting-style | Defines an element's starting styles before the element gets its first style update |
| X* | @supports | Used to test whether a browser supports a CSS feature |
| X | tab-size | Specifies the width of a tab character |
| X | table-layout | Defines the algorithm used to lay out table cells, rows, and columns |
| X | text-align | Specifies the horizontal alignment of text |
| X | text-align-last | Describes how the last line of a block or a line right before a forced line break is aligned when text-align is "justify" |
| | text-combine-upright | Specifies the combination of multiple characters into the space of a single character |
| X | text-decoration | Specifies the decoration added to text |
| X | text-decoration-color | Specifies the color of the text-decoration |
| X | text-decoration-line | Specifies the type of line in a text-decoration |
| X | text-decoration-style | Specifies the style of the line in a text decoration |
| X | text-decoration-thickness | Specifies the thickness of the decoration line |
| X* | text-emphasis | A shorthand property for the text-emphasis-style and text-emphasis-color properties |
| X* | text-emphasis-color | Specifies the color of emphasis marks |
| X* | text-emphasis-position | Specifies the position of emphasis marks |
| X* | text-emphasis-style | Specifies the style of emphasis marks |
| X | text-indent | Specifies the indentation of the first line in a text-block |
| X* | text-justify | Specifies the justification method used when text-align is "justify" |
| | text-orientation | Defines the orientation of characters in a line |
| X | text-overflow | Specifies what should happen when text overflows the containing element |
| X | text-shadow | Adds shadow to text |
| X | text-transform | Controls the capitalization of text |
| X | text-underline-offset | Specifies the offset distance of the underline text decoration |
| X | text-underline-position | Specifies the position of the underline text decoration |
| X | text-wrap | A shorthand property for text-wrap-mode and text-wrap-style |
| X | text-wrap-mode | Specifies whether the text inside an element is wrapped |
| X | text-wrap-style | Specifies how lines of text are wrapped (e.g. balance) |
| X | top | Specifies the top position of a positioned element |
| X* | transform | Applies a 2D or 3D transformation to an element |
| | transform-origin | Allows you to change the position on transformed elements |
| X* | transform-style | Specifies how nested elements are rendered in 3D space |
| X* | transition | A shorthand property for all the transition-* properties |
| X* | transition-delay | Specifies when the transition effect will start |
| X* | transition-duration | Specifies how many seconds or milliseconds a transition effect takes to complete |
| X* | transition-property | Specifies the name of the CSS property the transition effect is for |
| X* | transition-timing-function | Specifies the speed curve of the transition effect |
| X | translate | Specifies the position of an element |
| | unicode-bidi | Used together with the direction property to set or return whether the text should be overridden to support multiple languages in the same document |
| X | user-select | Specifies whether the text of an element can be selected |
| X | vertical-align | Sets the vertical alignment of an element |
| X | visibility | Specifies whether or not an element is visible |
| X | white-space | Specifies how white-space inside an element is handled |
| X | white-space-collapse | Specifies how white space inside an element is collapsed |
| X* | widows | Sets the minimum number of lines that must be left at the top of a page or column |
| X | width | Sets the width of an element |
| X | word-break | Specifies how words should break when reaching the end of a line |
| X | word-spacing | Increases or decreases the space between words in a text |
| X | word-wrap | Allows long, unbreakable words to be broken and wrap to the next line |
| | writing-mode | Specifies whether lines of text are laid out horizontally or vertically |
| X | z-index | Sets the stack order of a positioned element |
| | zoom | Specifies the zoom factor for an element. An element can be zoomed in and out |


# Referência — funções CSS suportadas

A legenda acima também vale para funções. As marcações consideram a resolução
no estilo e o efeito no layout/pintura; reconhecer ou ignorar uma chamada no
parser não conta como implementação. O suporte se limita às propriedades e
sintaxes descritas abaixo.


| Supported | Name | Description |
| --- | --- | --- |
| X* | abs() | Returns the absolute value of a number or resolved length |
| X* | acos() | Returns the inverse cosine of a number between -1 and 1 |
| X* | asin() | Returns the inverse sine of a number between -1 and 1 |
| X* | atan() | Returns the inverse tangent of a number between -∞ and ∞ |
| X* | atan2() | Returns the inverse tangent of two values between -infinity and infinity |
| X* | attr() | Returns the value of an attribute of the selected element |
| X* | blur() | Applies a blur effect to an element |
| X | brightness() | Adjusts the brightness of an element (brighter or darker) |
| X* | calc() | Allows you to perform calculations to determine CSS property values |
| X* | circle() | Defines a circle |
| X* | clamp() | Sets a value that will adjust responsively between a minimum value and a maximum value depending on the size of the viewport |
| X* | color() | Allows a color to be specified in a particular, specified color space |
| X* | color-mix() | Mixes two color values in a given color space, by a given amount |
| X* | conic-gradient() | Creates a conic gradient |
| X | contrast() | Adjusts the contrast of an element |
| X* | cos() | Returns the cosine of an angle |
| X* | counter() | Returns the current value of the named counter |
| X* | counters() | Returns the current values of the named and nested counters |
| X | cubic-bezier() | Defines a Cubic Bezier curve |
| X* | drop-shadow() | Applies a drop shadow effect to an image |
| X* | ellipse() | Defines an ellipse |
| X* | exp() | Returns E raised to the power of the specified number x (Ex) |
| | fit-content() | Allows you to size an element based on its content |
| X | grayscale() | Converts an image to grayscale |
| X* | hsl() / hsla() | Defines a color using the Hue-Saturation-Lightness model (HSL); with an optional alpha component |
| X | hue-rotate() | Applies a color rotation to an element |
| X* | hwb() | Defines a color using the Hue-Whiteness-Blackness model (HWB); with an optional alpha component |
| X* | hypot() | Returns the square root of the sum of squares of its parameters |
| X* | inset() | Defines a rectangle at the specified inset distances from each side of the reference box |
| X | invert() | Inverts the color of an image |
| X* | lab() | Specifies a color in the CIE L*a*b color space |
| X* | lch() | Specifies a color in the LCH (Lightness-Chroma-Hue) color space |
| X* | light-dark() | Enables two color-value settings, and returns the first value if the user has set a light color theme, and the second value if the user has set a dark color theme |
| X* | linear-gradient() | Creates a linear gradient |
| X* | log() | Returns the natural logarithm (base E) of a specified number, or the logarithm of the number to the specified base |
| | matrix() | Defines a 2D transformation, using a matrix of six values |
| | matrix3d() | Defines a 3D transformation, using a 4x4 matrix of 16 values |
| X* | max() | Uses the largest value, from a comma-separated list of values, as the property value |
| X* | min() | Uses the smallest value, from a comma-separated list of values, as the property value |
| | minmax() | Defines a size range greater than or equal to a min value and less than or equal to a max value (used with CSS grids) |
| X* | mod() | Returns the remainder left over when a number is divided by another number |
| X* | oklab() | Specifies a color in the OKLAB color space |
| X* | oklch() | Specifies a color in the OKLCH color space |
| X | opacity() | Applies an opacity effect to an element |
| | perspective() | Defines a perspective view for a 3D transformed element |
| X* | polygon() | Defines a polygon |
| X* | pow() | Returns the value of a number (x) raised to the power of another number (y) |
| X* | radial-gradient() | Creates a radial gradient |
| | ray() | Defines the offset-path line segment that an animated element should follow |
| X* | rect() | Defines a rectangle by top, right, bottom and left coordinates, with optional rounded corners in clip-path |
| X* | rem() | Returns the remainder left over when a number is divided by another number |
| | repeat() | Repeats a set of columns or rows in a grid |
| X* | repeating-conic-gradient() | Repeats a conic gradient |
| X* | repeating-linear-gradient() | Repeats a linear gradient |
| X* | repeating-radial-gradient() | Repeats a radial gradient |
| X* | rgb() / rgba() | Defines colors using the Red-Green-Blue model (RGB); with an optional alpha component |
| | rotate() | Defines a 2D rotation of an element |
| | rotate3d() | Defines a 3D rotation of an element around its x, y, and z-axes |
| | rotateX() | Defines a 3D rotation of an element around the x-axis (horizontal) |
| | rotateY() | Defines a 3D rotation of an element around the y-axis (vertical) |
| | rotateZ() | Defines a 3D rotation of an element around the z-axis |
| X* | round() | Rounds a number according to the specified rounding-strategy |
| X | saturate() | Adjusts the saturation (color intensity) of an element |
| | scale() | Defines a 2D scaling of an element |
| | scale3d() | Defines a 3D scaling of an element |
| | scaleX() | Scales an element horizontally (the width) |
| | scaleY() | Scales an element vertically (the height) |
| X | sepia() | Converts an image to sepia |
| X* | sign() | Returns the sign of a number or resolved length as -1, 0 or 1 |
| X* | sin() | Returns the sine of a number (angle) |
| | skew() | Skews an element along the x- and y-axis |
| | skewX() | Skews an element along the x-axis |
| | skewY() | Skews an element along the y-axis |
| X* | sqrt() | Returns the square root of a number |
| X | steps() | Creates a stepped timing function for animations |
| X* | tan() | Returns the tangent of a number |
| X | translate() | Allows you to re-position an element along the x- and y-axis |
| X | translateX() | Allows you to re-position an element along the x-axis |
| X | translateY() | Allows you to re-position an element along the y-axis |
| | translateZ() | Allows you to re-position an element along the z-axis in 3D space |
| X* | url() | Allows you to include a file in the stylesheet |
| X | var() | Inserts the value of a custom property |
| X* | xywh() | Creates a rectangle starting at the x and y coordinates of the contaning block, with the specified width and height |

O [catálogo por custo de implementação](funcoes-css-pendentes.md) detalha
as funções ausentes, suas dependências e o escopo da rodada fácil.

## Limitações e evidências no código

- **Cálculos** (`src/style/tbox_style_functions.c`, `tbox_style_calc_operand`):
  `calc()` opera com números e comprimentos/percentuais, com `+ - * /` e
  aninhamento. `min()`/`max()` aceitam até oito argumentos, inclusive uma
  única expressão (identidade). Percentuais puros e expressões `px + %`
  com o mesmo coeficiente percentual ou o mesmo termo em pixels podem ser
  comparados e aninhados. Nas demais misturas, só um argumento percentual
  com limites fixos em pixels é representável; em `clamp()` ele deve ser o
  central. Não há árvores gerais de comparações/aritmética com limites,
  nem álgebra geral de ângulos/tempos ou avaliação em componentes de cor.
  `pow(a, b)`, `sqrt(a)`, `exp(a)` e `log(a[, base])` aceitam argumentos
  numéricos sem unidade, inclusive expressões aninhadas. O resultado pode
  multiplicar um comprimento ou percentual: `calc(sqrt(2) * 100px)`.
  Resultados e argumentos não finitos, domínios inválidos (como `sqrt(-1)`
  ou `log(0)`) e bases de logaritmo não positivas ou iguais a 1 são
  rejeitados; não há tratamento geral dos valores CSS `NaN`/infinito.
  O avaliador atende comprimentos e `opacity`; outras propriedades numéricas,
  como `animation-iteration-count`, ainda não usam esse caminho. `hypot()`
  aceita de um a oito números ou comprimentos com unidades compatíveis
  convertidas para pixels, sem misturar tipos e sem percentuais/limites
  pendentes. Por exemplo `hypot(30px, 40px)` resulta em 50px.
  `abs()` e `sign()` aceitam um número ou comprimento resolvido em pixels;
  `abs()` preserva o tipo e `sign()` retorna -1, zero ou 1 como número.
  Ambos rejeitam percentuais/limites pendentes; `sign()` preserva o sinal
  de zero, enquanto `abs(-0)` produz zero positivo.
  As constantes `pi` e `e` são números dentro de cálculos, com nomes
  insensíveis a maiúsculas; não são valores isolados de propriedade.
  Em `opacity`, percentuais são convertidos para números antes da avaliação
  (100% = 1), permitindo `calc(50% + .25)` e `calc(pow(.5, 2))`. A saída
  deve ser numérica e finita, limitada a 0–1 apenas depois do cálculo.
  Comprimentos como resultado final são inválidos.
  `sin()`/`cos()`/`tan()` aceitam expressões numéricas em radianos ou
  angulares em `deg`, `rad`, `grad` ou `turn`, inclusive aninhamento como
  `sin(calc(45deg + 45deg))` e `sin(asin(.5))`. Os resultados são números.
  `asin()`/`acos()` aceitam números no intervalo [-1, 1]; `atan()` aceita
  números finitos. Retornam ângulos em graus, também utilizáveis nas
  direções de gradientes lineares e em `hue-rotate()`. `atan2(y, x)` aceita
  dois números, comprimentos resolvidos ou ângulos de tipos iguais;
  preserva quadrantes e zeros com sinal. Percentuais pendentes e outras
  dimensões (como tempo) não são suportados; em `opacity`, percentuais
  continuam convertidos para números. Ângulos não são comprimentos nem
  resultados numéricos válidos para `opacity`; suportam soma/subtração
  entre si, escala/divisão por número e `min`/`max`/`clamp` compatíveis.
  As demais funções escalares ainda não operam sobre ângulos.
  O parser de cores não passa por esse avaliador angular.
  Valores não finitos e polos detectados de `tan()` são rejeitados.
  `round()`, `mod()` e `rem()` aceitam números ou comprimentos resolvidos
  em pixels, com tipos iguais nos operandos; percentuais e limites
  pendentes em cálculos de comprimento não são suportados (em `opacity`,
  percentuais já foram convertidos em números). As três também podem ser o valor direto
  de uma propriedade de comprimento, como `width: round(123px, 10px)`.
  `round([nearest | up | down | to-zero,] a[, passo])` aceita passo não
  nulo, usa seu módulo e permite omiti-lo somente se `a` for um número
  (passo 1). Empates de `nearest` vão para o múltiplo maior. Não há
  estratégia `line-width`. `mod()` segue o sinal do divisor e `rem()` o
  do dividendo; divisor zero e valores não finitos são rejeitados.
- **Cores** (`src/css_cascade/tbox_css_color_parse.c`): as funções de cor
  marcadas aceitam componentes explícitos e alfa; não há sintaxe de cores
  relativas. Componentes `none` são tratados como zero, sem preservar a
  informação de componente ausente durante interpolação. As cores são
  convertidas para RGBA de 8 bits. `color()` aceita somente `srgb` e
  `srgb-linear`, com três números (escala 0–1) ou percentuais e alfa
  opcional depois de `/`; vírgulas e outros espaços são rejeitados.
  Valores fora do gamut são recortados por canal para a saída sRGB.
  `color-mix()` interpola em `srgb`, `srgb-linear` ou OKLab/OKLCH;
  outros espaços reconhecidos (como `lab`, `lch`, `hsl`, `hwb`, `display-p3`,
  `a98-rgb`, `prophoto-rgb`, `rec2020` e `xyz`) continuam aproximados por
  OKLab/OKLCH. Nomes desconhecidos ou perfis customizados são rejeitados.
  Os dois pesos devem ser finitos e estar entre 0% e 100%; soma zero é
  inválida. Soma acima de 100% é normalizada; abaixo de 100%, também reduz
  o alfa. Percentuais podem vir antes ou depois da cor. Em OKLCH há suporte a
  `shorter hue` (padrão), `longer hue`, `increasing hue` e `decreasing hue`,
  inclusive com alfa. Em `lch`/`hsl`, os mesmos métodos atuam sobre a
  aproximação OKLCH; métodos de matiz em espaços retangulares são rejeitados.
- **Tema** (`src/style/tbox_style_vars.c`): `light-dark()` escolhe pelo
  `color-scheme` herdado; o ambiente prefere claro, então escolhe escuro
  apenas quando há `dark` sem `light`.
- **Conteúdo gerado** (`src/style/tbox_style_generated.c`): `attr()` só
  produz texto em `content`, com fallback em string, sem conversão tipada.
  `counter()`/`counters()` usam os estilos de `list-style-type` implementados;
  não há `@counter-style` nem ligação automática com o contador `list-item`.
- **Gradientes** (`src/style/tbox_style_functions.c`, `tbox_style_parse_gradient`;
  `src/output/tbox_raster.c`): lineares, radiais e cônicos, inclusive repetidos,
  aceitam até oito paradas (uma cor com duas posições ocupa duas paradas),
  sem dicas de interpolação ou escolha do espaço de cor. Radiais aceitam
  `circle`/`ellipse`, extensões `closest-*`/`farthest-*` e posição `at`.
  Raios explícitos: um comprimento para círculo (`circle 40px` ou `40px`),
  dois comprimentos/percentuais para elipse (`ellipse 80px 40px` ou `50% 25%`).
  Percentuais usam cada eixo da imagem do gradiente; círculo não aceita
  percentuais. Raios literais negativos, extensão combinada com raio e
  limites pendentes são rejeitados. `calc()` linear é aceito; valores
  usados negativos são limitados a zero na pintura. Raios zero continuam
  aproximados por epsilon, sem a regra completa de elipses degeneradas
  e da cor média dos repetidos. O suporte também vale em `mask-image` e
  na forma `repeating-radial-gradient()`.
  Cônicos aceitam `from <angle>` e `at <position>`, com rotação horária
  a partir do topo, além de stops em ângulos ou percentuais de uma volta.
  Ângulos calculados e percentuais puros calculados são aceitos; misturas
  de ângulo com percentual em um mesmo cálculo não são. Stops sem posição
  são distribuídos e posições decrescentes são ajustadas, inclusive através
  de stops omitidos. Até oito stops, incluindo as duas posições de uma cor;
  sem hints nem escolha de espaço de interpolação (sRGB premultiplicado).
  Repetidos com período nulo ainda não calculam a cor média definida no CSS.
  Fundos em camadas, `background-size`/repetição e máscaras usam o mesmo caminho.
- **Filtros** (`src/style/tbox_style_functions.c`, `tbox_style_parse_filter`;
  `src/output/tbox_raster.c`): as funções de cor atuam em `filter`
  e `backdrop-filter`. `blur()` usa aproximação gaussiana por três box blurs;
  `drop-shadow()` funciona em `filter`, mas é ignorado em `backdrop-filter`.
  Uma lista comporta até seis operações internas (funções de cor
  consecutivas são combinadas numa matriz).
- **Formas** (`src/style/tbox_style_functions.c`, `tbox_style_parse_clip_path`):
  `circle()`, `ellipse()`, `inset()`, `xywh()`, `rect()` e `polygon()` funcionam em `clip-path`, com
  referência fixa à border box; não há caixa explícita nem `shape-outside`.
  `inset()`, `xywh()` e `rect()` aceitam `round` com raios em pixels e percentuais,
  inclusive elípticos com `/`; percentuais usam a dimensão correspondente
  da border box e os raios são reduzidos juntos se não couberem no recorte.
  `xywh()` exige quatro valores (x, y, largura, altura), sem vírgulas;
  aceita `calc()` linear em pixels/percentuais, mas não expressões com
  limites pendentes (`min()`/`max()`/`clamp()` misturando pixels e percentuais).
  Largura/altura negativas literais são inválidas; resultados negativos de
  cálculos produzem dimensão zero. `rect()` usa coordenadas top/right/bottom/left
  separadas por espaços, aceita `auto` (0 para top/left, 100% para right/bottom)
  e dimensão zero quando as arestas se cruzam; também rejeita
  expressões com limites pendentes. `clip: rect(...)` mantém a sintaxe
  legada, que aceita vírgulas e não aceita `round`.
  `polygon([nonzero | evenodd,] x y, ...)` aceita de três a 32 vértices,
  coordenadas em comprimentos/percentuais e cálculos suportados, com fechamento
  implícito. Formas côncavas e auto-interseções usam a regra escolhida.
  Pontos negativos ou fora da border box são permitidos. O recorte afeta o
  elemento, descendentes, barras de rolagem e hit-test; polígonos aninhados
  se intersectam por composição. Não há `round`, antialiasing nas arestas
  poligonais ou interpolação de `clip-path` em animações.
- **Transformações** (`src/style/tbox_style_functions.c`, `tbox_style_parse_transform`):
  apenas `translate()`, `translateX()` e `translateY()` têm efeito; a função
  `perspective()` não é implementada, embora a propriedade `perspective`
  seja aceita sem efeito. A palavra-chave `fit-content` existe, mas a
  função `fit-content()` não é implementada.
- **Recursos** (`tbox_style_parse_url` em `src/style/tbox_style_functions.c`,
  `src/context/tbox_context.c` e `src/context/tbox_context_font.c`): `url()`
  funciona para recursos locais, como imagens, fontes e `@import`; não há
  carregamento HTTP. É ignorado em `content`, filtros e cursores personalizados.

A tabela cataloga as funções listadas acima; não é uma lista exaustiva da
sintaxe CSS. As diferenças entre `clip` legado e `clip-path` são descritas
em [Layout e pintura](../08-layout-e-pintura.md).
