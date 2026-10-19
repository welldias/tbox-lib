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
| X* | border-image | A shorthand property for all the border-image-* properties |
| X* | border-image-outset | Specifies the amount by which the border image area extends beyond the border box |
| X* | border-image-repeat | Specifies whether the border image should be repeated, rounded or stretched |
| X* | border-image-slice | Specifies how to slice the border image |
| X* | border-image-source | Specifies the path to the image to be used as a border |
| X* | border-image-width | Specifies the width of the border image |
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
| X* | box-decoration-break | Sets the behavior of the background and border of an element at page-break, or, for in-line elements, at line-break. |
| X | box-reflect | The box-reflect property is used to create a reflection of an element. |
| X | box-shadow | Attaches one or more shadows to an element |
| X | box-sizing | Defines how the width and height of an element are calculated: should they include padding and borders, or not |
| X* | break-after | Specifies whether or not a page-, column-, or region-break should occur after the specified element |
| X* | break-before | Specifies whether or not a page-, column-, or region-break should occur before the specified element |
| X* | break-inside | Specifies whether or not a page-, column-, or region-break should occur inside the specified element |
| X | caption-side | Specifies the placement of a table caption |
| X | caret-color | Specifies the color of the cursor (caret) in inputs, textareas, or any element that is editable |
| X* | @charset | Specifies the character encoding used in the style sheet |
| X | clear | Specifies what should happen with the element that is next to a floating element |
| X* | clip | Deprecated in favor of clip-path. Clips an absolutely positioned element |
| X* | clip-path | Clips an element to a basic shape or to an SVG source |
| X | color | Sets the color of text |
| X* | color-scheme | Indicates which operating system color scheme an element should render with |
| X* | column-count | Specifies the number of columns an element should be divided into |
| X* | column-fill | Specifies how to fill columns, balanced or not |
| X | column-gap | Specifies the gap between the columns |
| X | column-rule | A shorthand property for all the column-rule-* properties |
| X | column-rule-color | Specifies the color of the rule between columns |
| X | column-rule-style | Specifies the style of the rule between columns |
| X | column-rule-width | Specifies the width of the rule between columns |
| X* | column-span | Specifies how many columns an element should span across |
| X* | column-width | Specifies the column width |
| X* | columns | A shorthand property for column-width and column-count |
| X* | @container | Define styles for elements in container, depending on the container's size or style |
| X | container | Shorthand for container-name and container-type |
| X* | container-name | Names a query container for @container |
| X | container-type | Makes an element a size query container (size, inline-size) |
| X* | content | Used with the :before and :after pseudo-elements, to insert generated content |
| X* | counter-increment | Increases or decreases the value of one or more CSS counters |
| X* | counter-reset | Creates or resets one or more CSS counters |
| X* | counter-set | Creates or sets one or more CSS counters |
| X* | @counter-style | Lets you define your own counter styles |
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
| X* | float | Specifies whether an element should float to the left, right, or not at all |
| X* | font | A shorthand property for the font-style, font-variant, font-weight, font-size/line-height, and the font-family properties |
| X* | @font-face | Specifies a custom font to use to display text |
| X* | font-family | Specifies the font family for text |
| X* | font-feature-settings | Allows control over advanced typographic features in OpenType fonts |
| X | font-kerning | Controls the usage of the kerning information (how letters are spaced) |
| X | font-language-override | Controls the usage of language-specific glyphs in a typeface |
| X* | @font-palette-values | Allows you to customize the default values of a font-palette |
| X | font-size | Specifies the font size of text |
| X* | font-size-adjust | Preserves the readability and size of text when fallback font occurs |
| X | font-stretch | Selects a normal, condensed, or expanded face from a font family |
| X | font-style | Specifies the font style for text |
| X* | font-synthesis | Controls which missing typefaces (bold or italic) may be synthesized by the browser |
| X* | font-variant | Specifies whether or not a text should be displayed in a small-caps font |
| X* | font-variant-alternates | Controls the usage of alternate glyphs associated to alternative names defined in @font-feature-values |
| X* | font-variant-caps | Controls the usage of alternate glyphs for capital letters |
| X | font-variant-east-asian | Controls the usage of alternate glyphs for East Asian scripts (e.g Japanese and Chinese) |
| X | font-variant-ligatures | Controls which ligatures and contextual forms are used in textual content of the elements it applies to |
| X | font-variant-numeric | Controls the usage of alternate glyphs for numbers, fractions, and ordinal markers |
| X* | font-variant-position | Controls the usage of alternate glyphs of smaller size positioned as superscript or subscript regarding the baseline of the font |
| X | font-weight | Specifies the weight of a font |
| X | gap | A shorthand property for the row-gap and the column-gap properties |
| X* | grid | A shorthand property for the grid-template-rows, grid-template-columns, grid-template-areas, grid-auto-rows, grid-auto-columns, and the grid-auto-flow properties |
| X* | grid-area | Either specifies a name for the grid item, or this property is a shorthand property for the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties |
| X* | grid-auto-columns | Specifies a default column size |
| X | grid-auto-flow | Specifies how auto-placed items are inserted in the grid |
| X* | grid-auto-rows | Specifies a default row size |
| X* | grid-column | A shorthand property for the grid-column-start and the grid-column-end properties |
| X* | grid-column-end | Specifies where to end the grid item |
| X* | grid-column-start | Specifies where to start the grid item |
| X* | grid-row | A shorthand property for the grid-row-start and the grid-row-end properties |
| X* | grid-row-end | Specifies where to end the grid item |
| X* | grid-row-start | Specifies where to start the grid item |
| X* | grid-template | A shorthand property for the grid-template-rows, grid-template-columns and grid-areas properties |
| X* | grid-template-areas | Specifies how to display columns and rows, using named grid items |
| X* | grid-template-columns | Specifies the size of the columns, and how many columns in a grid layout |
| X* | grid-template-rows | Specifies the size of the rows in a grid layout |
| X | hanging-punctuation | Specifies whether a punctuation character may be placed outside the line box |
| X | height | Sets the height of an element |
| X* | hyphenate-character | Sets the character used at the end of line, before a hyphenation break |
| X* | hyphens | Sets how to split words to improve the layout of text |
| X | image-rendering | Specifies the type of algorithm to use for image scaling |
| X* | @import | Allows you to import a style sheet into another style sheet |
| X* | initial-letter | Specifies the size of the initial-letter and optionally the number of lines the initial letter should sink (down in the text) |
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
| X* | justify-items | Is set on the grid container. Specifies the alignment of grid items in the inline direction |
| X* | justify-self | Is set on the grid item. Specifies the alignment of the grid item in the inline direction |
| X* | @keyframes | Controls the steps in an animation by defining styles for points along the animation sequence |
| X* | @layer | Controls how the CSS cascade layers evaluates the order of styles |
| X | left | Specifies the left position of a positioned element |
| X | letter-spacing | Increases or decreases the space between characters in a text |
| X* | line-break | Specifies how/if to break lines |
| X | line-clamp | Limits the contents of a block to the specified number of lines |
| X | -webkit-line-clamp | Legacy prefixed form of line-clamp (used with display: -webkit-box) |
| X* | -webkit-text-stroke | Shorthand for the text stroke width and color (needs vector glyphs) |
| X | -webkit-text-stroke-color | Color of the text stroke |
| X* | -webkit-text-stroke-width | Width of the text stroke (needs vector glyphs) |
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
| X* | marker | Points to a marker that will be drawn on all vertices of an element's path (the first, middle, and last) |
| X* | marker-end | Points to a marker that will be drawn on the last vertex of an element's path |
| X* | marker-mid | Points to a marker that will be drawn on all the middle vertices of an element's path |
| X* | marker-start | Points to a marker that will be drawn on the first vertex of an element's path |
| X* | mask | A shorthand property for mask-image, mask-mode, mask-repeat, mask-position, mask-clip, mask-origin, mask-size and mask-composite |
| X* | mask-clip | Specifies which area is affected by a mask image |
| X | mask-composite | Specifies a compositing operation used on the current mask layer with the mask layers below it |
| X* | mask-image | Specifies an image to be used as a mask layer for an element |
| X* | mask-mode | Specifies whether the mask layer image is treated as a luminance mask or as an alpha mask |
| X* | mask-origin | Specifies the origin position (the mask position area) of a mask layer image |
| X* | mask-position | Sets the starting position of a mask layer image (relative to the mask position area) |
| X* | mask-repeat | Specifies how the mask layer image is repeated |
| X* | mask-size | Specifies the size of a mask layer image |
| X* | mask-type | Specifies whether an SVG <mask> element is treated as a luminance mask or as an alpha mask |
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
| X* | offset | A shorthand property for the offset-anchor, offset-distance, offset-path, offset-position, and the offset-rotate properties |
| X* | offset-anchor | Specifies a point on an element that is fixed to the path it is animated along |
| X* | offset-distance | Specifies the position along a path where an animated element is placed |
| X* | offset-path | Specifies the path an element is animated along |
| X* | offset-position | Specifies the initial position of an element along a path |
| X | offset-rotate | Specifies rotation of an element as it is animated along a path |
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
| X* | @page | Customizes the dimension, orientation, and margins of printed pages |
| X* | page-break-after | Sets the page-break behavior after an element. Replaced by break-after property |
| X* | page-break-before | Sets the page-break behavior before an element. Replaced by break-before property |
| X* | page-break-inside | Sets the page-break behavior inside an element. Replaced by break-inside property |
| X | paint-order | Sets the order of how an SVG element or text is painted. |
| X* | perspective | Gives a 3D-positioned element some perspective |
| X* | perspective-origin | Defines at which position the user is looking at the 3D-positioned element |
| X | place-content | Specifies align-content and justify-content property values for flexbox and grid layouts |
| X | place-items | Specifies align-items and justify-items property values for grid layouts |
| X | place-self | Specifies align-self and justify-self property values for grid layouts |
| X | pointer-events | Defines whether or not an element reacts to pointer events |
| X | position | Specifies the type of positioning method used for an element (static, relative, absolute or fixed) |
| X* | @property | Defines custom CSS properties directly in the stylesheet without having to run any JavaScript |
| X* | quotes | Sets the type of quotation marks for embedded quotations |
| X* | resize | Defines if (and how) an element is resizable by the user |
| X | right | Specifies the right position of a positioned element |
| X* | rotate | Specifies the rotation of an element |
| X | row-gap | Specifies the gap between the grid rows |
| X | scale | Specifies the size of an element by scaling up or down |
| X* | @scope | Allows you to select elements in specific DOM subtrees and target elements precisely without writing overly-specific selectors |
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
| X* | shape-outside | Defines a shape for wrapping for the inline content |
| X* | shape-margin | Adds a margin to a shape-outside |
| X | shape-image-threshold | Sets the alpha threshold used to extract a shape-outside from an image |
| X* | @starting-style | Defines an element's starting styles before the element gets its first style update |
| X* | @supports | Used to test whether a browser supports a CSS feature |
| X | tab-size | Specifies the width of a tab character |
| X | table-layout | Defines the algorithm used to lay out table cells, rows, and columns |
| X | text-align | Specifies the horizontal alignment of text |
| X | text-align-last | Describes how the last line of a block or a line right before a forced line break is aligned when text-align is "justify" |
| X* | text-combine-upright | Specifies the combination of multiple characters into the space of a single character |
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
| X* | text-orientation | Defines the orientation of characters in a line |
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
| X | transform-origin | Allows you to change the position on transformed elements |
| X* | transform-style | Specifies how nested elements are rendered in 3D space |
| X* | transition | A shorthand property for all the transition-* properties |
| X* | transition-delay | Specifies when the transition effect will start |
| X* | transition-duration | Specifies how many seconds or milliseconds a transition effect takes to complete |
| X* | transition-property | Specifies the name of the CSS property the transition effect is for |
| X* | transition-timing-function | Specifies the speed curve of the transition effect |
| X | translate | Specifies the position of an element |
| X* | unicode-bidi | Used together with the direction property to set or return whether the text should be overridden to support multiple languages in the same document |
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
| X* | writing-mode | Specifies whether lines of text are laid out horizontally or vertically |
| X | z-index | Sets the stack order of a positioned element |
| X* | zoom | Specifies the zoom factor for an element. An element can be zoomed in and out |


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
| X* | fit-content() | Allows you to size an element based on its content |
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
| X | matrix() | Defines a 2D transformation, using a matrix of six values |
| X* | matrix3d() | Defines a 3D transformation, using a 4x4 matrix of 16 values |
| X* | max() | Uses the largest value, from a comma-separated list of values, as the property value |
| X* | min() | Uses the smallest value, from a comma-separated list of values, as the property value |
| X* | minmax() | Defines a size range greater than or equal to a min value and less than or equal to a max value (used with CSS grids) |
| X* | mod() | Returns the remainder left over when a number is divided by another number |
| X* | oklab() | Specifies a color in the OKLAB color space |
| X* | oklch() | Specifies a color in the OKLCH color space |
| X | opacity() | Applies an opacity effect to an element |
| X* | perspective() | Defines a perspective view for a 3D transformed element |
| X* | polygon() | Defines a polygon |
| X* | pow() | Returns the value of a number (x) raised to the power of another number (y) |
| X* | radial-gradient() | Creates a radial gradient |
| X | ray() | Defines the offset-path line segment that an animated element should follow |
| X* | rect() | Defines a rectangle by top, right, bottom and left coordinates, with optional rounded corners in clip-path |
| X* | rem() | Returns the remainder left over when a number is divided by another number |
| X* | repeat() | Repeats a set of columns or rows in a grid |
| X* | repeating-conic-gradient() | Repeats a conic gradient |
| X* | repeating-linear-gradient() | Repeats a linear gradient |
| X* | repeating-radial-gradient() | Repeats a radial gradient |
| X* | rgb() / rgba() | Defines colors using the Red-Green-Blue model (RGB); with an optional alpha component |
| X | rotate() | Defines a 2D rotation of an element |
| X* | rotate3d() | Defines a 3D rotation of an element around its x, y, and z-axes |
| X* | rotateX() | Defines a 3D rotation of an element around the x-axis (horizontal) |
| X* | rotateY() | Defines a 3D rotation of an element around the y-axis (vertical) |
| X | rotateZ() | Defines a 3D rotation of an element around the z-axis |
| X* | round() | Rounds a number according to the specified rounding-strategy |
| X | saturate() | Adjusts the saturation (color intensity) of an element |
| X | scale() | Defines a 2D scaling of an element |
| X* | scale3d() | Defines a 3D scaling of an element |
| X | scaleX() | Scales an element horizontally (the width) |
| X | scaleY() | Scales an element vertically (the height) |
| X | sepia() | Converts an image to sepia |
| X* | sign() | Returns the sign of a number or resolved length as -1, 0 or 1 |
| X* | sin() | Returns the sine of a number (angle) |
| X | skew() | Skews an element along the x- and y-axis |
| X | skewX() | Skews an element along the x-axis |
| X | skewY() | Skews an element along the y-axis |
| X* | sqrt() | Returns the square root of a number |
| X | steps() | Creates a stepped timing function for animations |
| X* | tan() | Returns the tangent of a number |
| X | translate() | Allows you to re-position an element along the x- and y-axis |
| X | translateX() | Allows you to re-position an element along the x-axis |
| X | translateY() | Allows you to re-position an element along the y-axis |
| X* | translateZ() | Allows you to re-position an element along the z-axis in 3D space |
| X* | url() | Allows you to include a file in the stylesheet |
| X | var() | Inserts the value of a custom property |
| X* | xywh() | Creates a rectangle starting at the x and y coordinates of the contaning block, with the specified width and height |
