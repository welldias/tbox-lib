# Referência — fixtures visuais por recurso

> - **Escopo:** qual arquivo de `tests/assets/` demonstra cada recurso de CSS/HTML. Útil para ver um recurso funcionando ou conferir uma regressão.
> - **Código relacionado:** `tests/assets/`, `example/assets_showrun.c`, `tests/tbox_cmp.cpp`
> - **Veja também:** [Ferramentas e testes](../12-ferramentas-e-testes.md), [CSS suportado](css-suportado.md)

Abra qualquer um com `build/example/tbox_assets_showrun` (setas navegam) ou
gere um PNG com `tbox_app_screenshot_from_files`. A referência de cada um é
capturada no Firefox pelo `tbox_cmp` a cada execução, sem PNG guardado.

Convenções:

- Nome `<categoria>-<tema>.html`. Cada recurso fica em **um** fixture; quando
  um tema não cabe numa página, ele se divide em mais de um arquivo com o
  mesmo prefixo (`text-shaping-latin` e `text-shaping-scripts`).
- Cada fixture cabe em 800x600 (o tamanho padrão do `tbox_cmp`), sem rolagem.
- Textos em inglês, `lang="en"` (exceto onde o idioma é o próprio teste, como
  `:lang()`).
- Recursos auxiliares levam o nome do fixture que os usa: `cascade-link.css`,
  `cascade-import.css` e `cascade-import/`, `cascade-layers-reset.css`,
  `base-demo/base.css`, `opentype-sans.ttf` (subset da Roboto, OFL; licença
  em `opentype-sans-license.txt`). Imagens compartilhadas: `black.png`,
  `yellow.png`, `bird.png`, `pixel-heart.png`, `mask-star.png`.

Até 2026-10-19 os fixtures eram numerados (`001.html`…`163.html`, `a01.html`)
na ordem em que os recursos foram implementados. A tabela no fim desta página
mostra para onde foi cada um.

## SVG inline

| Fixture | Recurso |
|---|---|
| `svg-basic-shapes` | Formas básicas, arredondamento, percentuais, viewBox e preserveAspectRatio |
| `svg-paths` | Comandos absolutos/relativos, curvas, arcos, subcaminhos e winding rules |
| `svg-strokes` | Pontas, junções, tracejado, deslocamento e medidor com pathLength |
| `svg-symbols` | Os 19 símbolos do file explorer em cores/tamanhos diferentes e rotação CSS |
| `svg-compositing` | Transformações SVG, opacidade de grupo e recortes de viewports |
| `svg-style-layout` | Cascata de atributos/CSS/@layer, var(), herança, flex/grid e SVG inline |

## HTML

| Fixture | Recurso |
|---|---|
| `html-headings` | `<h1>`–`<h6>`, `<hr>` |
| `html-paragraphs` | `<p>`, `<br>`, `<pre>`, espaços colapsados |
| `html-text-formatting` | `<b>`, `<strong>`, `<i>`, `<em>`, `<mark>`, `<small>`, `<del>`, `<ins>`, `<sub>`, `<sup>`, `<q>`, `<blockquote>`, `<abbr>`, `<address>`, `<cite>`, `<bdo>` |
| `html-links` | `<a href>` absoluto e relativo, `target` |
| `html-images` | `<img>` com `width`/`height`, tamanho natural, `alt` de imagem ausente, imagem em link, `float` |
| `html-picture-and-map` | `<source>` em `<picture>` com `<img>` de fallback; `<map>`/`<area>` retangular, circular e poligonal |
| `html-base-href` | `<base href>` para CSS e imagens locais (`base-demo/`) |
| `html-tables` | tabela básica, bordas colapsadas invisíveis, largura percentual de coluna |
| `html-tables-layout` | `empty-cells: hide`, `table-layout: fixed`, linha `visibility: collapse`, `max-width`/`min-width`, `rules` |
| `css-style-attribute` | atributo `style`: cor de texto e de fundo, tamanho de fonte |

## Cores

| Fixture | Recurso |
|---|---|
| `colors-names-and-formats` | nomes de cor em fundo, texto e borda; a mesma cor em cada notação |
| `colors-rgb-and-hex` | `rgb()`/`rgba()`, hexadecimal de 3/4/6/8 dígitos |
| `colors-hsl` | `hsl()`/`hsla()` |
| `colors-modern` | `hwb`, `lab`, `lch`, `oklab`, `oklch`, `currentColor`, `color-scheme` e `light-dark()` |
| `colors-mix-and-color-function` | `color-mix()` em vários espaços, pesos (normalização, alfa, espaço inválido), percursos de matiz em `oklch`, `color(srgb)`/`color(srgb-linear)` |

## Caixa

| Fixture | Recurso |
|---|---|
| `box-border-padding-margin` | `border`, `padding`, `margin` |
| `box-border-styles` | `dashed`, `dotted`, `double`, estilo por lado, `hidden`; bordas por lado; `outline` (`thick`/`thin`, `outline-offset`, tracejado) |
| `box-border-styles-3d` | `groove`, `ridge`, `inset`, `outset` em bordas e contornos |
| `box-border-styles-rounded` | tracejado e pontilhado em caixas arredondadas |
| `box-radius` | `border-radius` de um a quatro valores, longhands de canto, raio maior que a caixa, raios de canto lógicos |
| `box-radius-elliptical` | raios elípticos (`/`) e percentuais |
| `box-logical-borders` | `border-inline-*`/`border-block-*` (longhands e atalhos por eixo) |
| `box-logical-sizes` | `inline-size`/`block-size`, min/max lógicos, `margin-block`/`padding-inline`, `inset-block`/`inset-inline` |
| `box-sizing` | `box-sizing`, `min-height`/`max-height` com rolagem, `min-width` vencendo `max-width` |
| `box-sizing-keywords` | `width: min-content / max-content / fit-content` |
| `box-aspect-ratio` | `aspect-ratio` |

## Texto e fontes

| Fixture | Recurso |
|---|---|
| `text-font-size` | palavras-chave absolutas e relativas de tamanho, atalho `font`, `font-size-adjust` |
| `text-font-weight-and-stretch` | `font-weight` numérico e relativo (`bolder`/`lighter`), `font-style` (`italic`, `oblique`), `font-stretch` |
| `text-font-variants` | `font-variant: small-caps`, `font-variant-position`, `font-synthesis` |
| `text-font-face-and-opentype` | `@font-face` com fonte local (`opentype-sans.ttf`); `tabular-nums`/`proportional-nums`, `oldstyle-nums`, `diagonal-fractions`, `font-variant-ligatures: none`, `font-feature-settings`. Exige HarfBuzz |
| `text-shaping-latin` | HarfBuzz: ligaduras (e `letter-spacing` desligando-as), kerning com `font-kerning: auto`/`none`, acentos combinantes e marcas empilhadas |
| `text-shaping-scripts` | HarfBuzz: formas contextuais árabes (ZWNJ), hebraico com niqqud; cursor dentro de ligadura em `<input>`/`<textarea>`. Sem HarfBuzz, "depois" sai igual a "antes" |
| `text-decoration` | `underline`/`overline`/`line-through`, longhands, cor, estilos (`wavy`, `dashed`, `dotted`, `double`), espessura, `text-underline-position: under` |
| `text-transform-and-shadow` | `text-transform`, listas de `text-shadow` |
| `text-spacing` | `line-height`, `letter-spacing`, `word-spacing`, `vertical-align` |
| `text-alignment` | `text-align`, `text-align-last`, `text-justify` |
| `text-indent` | `text-indent` com `hanging` e `each-line` |
| `text-white-space` | `white-space` (inclusive `break-spaces`), `text-wrap-mode`, `white-space-collapse`, `tab-size` |
| `text-wrapping` | `overflow-wrap` (`break-word`, `anywhere`), `word-wrap`, `word-break` (`break-all`, `keep-all`), `line-break: anywhere` |
| `text-hyphens-and-balance` | `&shy;` com `hyphens: manual`/`none`, `hyphenate-character`, `text-wrap: balance`, `text-wrap-style` |
| `text-overflow` | `text-overflow: ellipsis` e `"<string>"`, `line-clamp` |
| `text-emphasis` | `text-emphasis` (formas cheias/abertas, `<string>`, `under`, herança) |
| `text-stroke` | `-webkit-text-stroke` com `paint-order`, texto vazado; `marker`/`mask-type`/`@page` aceitos sem quebrar a folha |
| `text-initial-letter` | `::first-letter` flutuante, `initial-letter` (N/A no Firefox), `hanging-punctuation` |
| `text-ruby` | `<ruby>`, `<rt>`, `<rp>` |
| `text-direction` | `direction: rtl` e atributo `dir` (propriedades lógicas, flex, `text-align: start/end`) |
| `text-bidi` | hebraico/árabe com números e latim, espelhamento, `<bdo>`, `<bdi>`, `unicode-bidi` (`isolate`, `plaintext`, `embed`) |
| `text-vertical-writing` | `vertical-rl`/`vertical-lr`, `text-orientation: upright`, `text-combine-upright`, `sideways-lr`, fluxo ortogonal |

## Listas e conteúdo gerado

| Fixture | Recurso |
|---|---|
| `lists-style-type` | `list-style-type` (inclusive `decimal-leading-zero`, `lower-greek`, `disclosure-*`), listas aninhadas |
| `lists-position-and-list-item` | `list-style-position`, `display: list-item` |
| `lists-markers` | `list-style-image`, `::marker` |
| `generated-content` | `::before`/`::after` (campo obrigatório, badge com `attr()`, status, breadcrumb, bloco vazio), `quotes` |
| `generated-counters` | `counter()`, `counters()`, `@counter-style` (`cyclic`, `additive`, `alphabetic` com `prefix`/`suffix`, `fixed` com fallback) |

## Seletores

| Fixture | Recurso |
|---|---|
| `selectors-structural` | `:nth-child()`, `:nth-of-type()`, `:nth-last-child()`, `:nth-last-of-type()` |
| `selectors-type-and-only` | `:root`, `:first-of-type`/`:last-of-type`/`:only-of-type`, `:only-child`, `:empty` |
| `selectors-combinators-and-not` | combinador `~`, `:not()` |
| `selectors-attributes-and-lang` | `^=`, `$=`, `*=`, modificadores `i` e `s`, `:lang()` |
| `selectors-states` | `:required`/`:optional`, `:focus-within`/`:focus-visible`, `:enabled`/`:disabled`, `:link`/`:any-link`, `:hover` em ancestrais |

## Cascata e at-rules

| Fixture | Recurso |
|---|---|
| `cascade-keywords` | `inherit`, `initial`, `unset`, `revert`, `all`; texto anônimo herda |
| `cascade-custom-properties` | custom properties e `var()` (fallback, redefinição), `@property` (`inherits: false`, valor inválido cai no `initial-value`) |
| `cascade-link-and-import` | `<link rel="stylesheet">`, `@import` aninhado e com media que não casa, `@media` por largura, media features |
| `cascade-layers` | `@layer` (ordem, subcamadas, anônima, dentro de `@media`, `!important`) e `@import … layer()` |
| `cascade-supports` | `@supports` (`not`/`and`/`or`, `selector()`, custom property, dentro de `@media`); propriedades aceitas sem efeito (impressão, 3D, `clear`, `overflow-anchor`) |
| `cascade-scope-and-namespace` | `@scope` (limite `to`, proximidade, `:scope`), `@namespace` |
| `cascade-container-queries` | `@container` em contêineres `inline-size`, unidades `cqi`, contêiner nomeado |

## Unidades e funções

| Fixture | Recurso |
|---|---|
| `units-lengths` | `rem`, `ex`, `ch`, `pt`, `pc`, `in`, `cm`, `mm`, `Q`, `vw`/`vh`/`vmin`/`vmax`, `sv*`/`lv*`/`dv*` |
| `units-calc-min-max-clamp` | `calc()`, `min()`, `max()`, `clamp()` (inclusive percentuais) |
| `units-math-functions` | `pow()`/`sqrt()`/`exp()`/`log()`, `hypot()`, `round()`, `mod()`/`rem()`, `abs()`/`sign()`, `pi`/`e`, `opacity` calculada |
| `units-math-trig` | `sin()`/`cos()`/`tan()`, `asin()`/`acos()`/`atan()`/`atan2()` |

## Fundos

| Fixture | Recurso |
|---|---|
| `backgrounds-image` | `background-image`, `-size`, `-position`, `-repeat` (inclusive `space`/`round`), `background-attachment: fixed` |
| `backgrounds-clip-and-origin` | `background-clip`, `background-origin` |
| `backgrounds-layers` | várias camadas de fundo (atalho e listas de longhands), lista de `background-blend-mode` |
| `backgrounds-blend-mode` | os 16 modos de `background-blend-mode` |
| `backgrounds-gradients` | `linear-gradient()`, `radial-gradient()`, `repeating-*`, raios explícitos |
| `backgrounds-gradients-conic` | gradientes cônicos: interpolação, setores, `from`/`at`, repetição, mosaico, máscara |
| `backgrounds-border-image` | `border-image` com gradiente e imagem, `slice`, `fill`, `stretch`/`repeat`/`round`/`space`, `-width`, `-outset` |

## Layout

| Fixture | Recurso |
|---|---|
| `layout-inline-block` | `inline-block`, `vertical-align`, shrink-to-fit |
| `layout-block-in-inline` | bloco dentro de `<p>`, `<h3>`, `<li>`, `<td>`; span absoluto com `.visually-hidden` em célula |
| `layout-flexbox` | flexbox: `grow`, `justify-content`, `align-items`, margem `auto`, `wrap`, `baseline`, `row-reverse`; layout de aplicação |
| `layout-alignment` | `place-content`, `place-items`, `place-self` |
| `layout-grid-tracks` | grid: trilhas fixas + `fr`, `repeat()`, `minmax()`, `auto-fill`, spans, linhas negativas e nomeadas |
| `layout-grid-areas-and-alignment` | grid: `grid-template-areas`, `grid-auto-flow: column`/`dense`, alinhamento de itens, `auto-fit` |
| `layout-floats` | `float`, `clear`, `display: flow-root`, `shape-outside: circle()` |
| `layout-multicol` | `column-count`/`column-width`, `column-rule`, `column-fill`, `column-span`, bloco partido entre colunas |
| `layout-positioning` | `position: sticky`, `absolute` esticado por `inset`, `relative` |
| `layout-z-index` | `z-index` e contextos de empilhamento |
| `layout-overflow` | `overflow-x`, `overflow` com dois valores, `overflow: clip`, `overflow-y: scroll` |
| `layout-scrollbars` | scrollbar padrão discreto, `scrollbar-width`, `scrollbar-color`, partes `::-tbox-scrollbar-*`, `scrollbar-gutter: stable`. O Firefox só aplica as propriedades padrão |
| `layout-scrolling` | `overscroll-behavior`, `scroll-margin`, `scroll-padding`, `scroll-behavior: smooth` (com Tab) |
| `layout-scroll-snap` | `scroll-snap-type`, `scroll-snap-align`, `scroll-snap-stop` |

## Efeitos

| Fixture | Recurso |
|---|---|
| `effects-box-shadow` | listas de `box-shadow`, `inset`, spread e blur |
| `effects-filter` | funções de cor de `filter` |
| `effects-drop-shadow-and-backdrop` | `filter: drop-shadow()` na forma, `backdrop-filter` (cor e `blur()`) |
| `effects-compositing` | `opacity` de grupo, `visibility`, `mix-blend-mode`, `isolation`, `background-blend-mode` isolado |
| `effects-clip-path-shapes` | `clip-path: inset()`/`circle()`/`ellipse()`/`xywh()`/`rect()`, `clip: rect()` |
| `effects-clip-path-polygon` | `polygon()`: concavidade, `nonzero`/`evenodd`, interseção de recortes |
| `effects-masks-and-reflection` | `mask-image`/`-webkit-mask` (`mask-star.png`), `mask-composite`, `box-decoration-break`, `-webkit-box-reflect` (N/A no Firefox) |
| `effects-transform-translate` | `transform: translate*()`, propriedade `translate` |
| `effects-transform-functions` | `rotate()`, `scale()`, `skewX()`, `matrix()`, `rotateZ()`/`rotateX()`, `transform-origin`, propriedades `rotate`/`scale` |
| `effects-motion-path` | `offset-path` (`path()`, `circle()`, `ray()`), `offset-distance`, `offset-rotate`, `offset-anchor` |

## Animação

| Fixture | Recurso |
|---|---|
| `animation-transitions` | `transition` (layout, bordas, `outline`, `translate`, cor, `opacity`), `cubic-bezier()`, `steps()`, listas de longhands, `prefers-reduced-motion`, `@starting-style` (passe o mouse) |
| `animation-keyframes` | `animation` e `@keyframes` (`infinite alternate`, keyframe intermediário, `steps()` com atraso, `forwards`, `paused`) |

## Interação

| Fixture | Recurso |
|---|---|
| `interaction-cursor-and-pointer` | `cursor` (todos os valores), `user-select`, `pointer-events: none`, `:active` |
| `interaction-zoom-and-resize` | `zoom`, `resize` (inclusive o `<textarea>` padrão) |

## Formulários

| Fixture | Recurso |
|---|---|
| `forms-grouping` | `<fieldset>`/`<legend>`, `<details>`/`<summary>`, `<optgroup>` habilitado e desabilitado |
| `forms-progress-and-output` | `<output>`, `<progress>` determinado e indeterminado, `<meter>` |
| `forms-accent-and-caret` | `accent-color` (padrão, herdado, por controle com `var()`, `meter` sem mudança), `caret-color` |
| `forms-control-parts` | partes dos controles (`::slider-*`, `::picker-icon`, `::checkmark`, `::file-selector-button`, `::step-*`, `::clear-icon`, `::color-swatch`, `::selection`) |
| `forms-checkbox-radio-and-focus` | checkbox e radio alinhados, campos vazios e preenchidos alinhados, `outline` como anel de foco |
| `forms-select` | lista do `<select>`: `::picker(select)`, `option`/`optgroup`, `:hover`, `:checked`, `:disabled`, tema por `var()`. O Firefox mostra a lista nativa |
| `forms-placeholder` | `placeholder`/`::placeholder`: padrão, cor/opacidade, fonte, password, valor preenchido e textarea multilinha |
| `forms-textarea-wrapping` | quebra de linha do `<textarea>` |

## Imagens

| Fixture | Recurso |
|---|---|
| `images-object-fit` | `object-fit` (inclusive `none` e `scale-down`) |
| `images-object-position` | `object-position` |
| `images-rendering` | `image-rendering: pixelated`/`crisp-edges`, herança |

## Numeração antiga

| Antigo | Agora em |
|---|---|
| 001 | `html-headings` |
| 002 | `html-headings` |
| 003 | `html-headings` |
| 004 | `html-paragraphs` |
| 005 | `html-headings` |
| 006 | `html-paragraphs` |
| 007 | `html-paragraphs` |
| 008 | `css-style-attribute` |
| 009 | `css-style-attribute` |
| 010 | `css-style-attribute` |
| 011 | `html-text-formatting` |
| 012 | `html-text-formatting` |
| 013 | `html-paragraphs` |
| 014 | `colors-names-and-formats` |
| 015 | `colors-names-and-formats` |
| 016 | `colors-rgb-and-hex` |
| 017 | `colors-rgb-and-hex` |
| 018 | `colors-hsl` |
| 019 | `css-style-attribute` |
| 020 | `box-border-padding-margin` |
| 021 | `box-border-padding-margin` |
| 022 | `box-border-padding-margin` |
| 023 | `html-links` |
| 024 | `html-images` |
| 025 | `html-tables` |
| 026 | `html-tables` |
| 027 | `html-tables` |
| 028 | `box-border-styles`, `text-decoration`, `text-indent`, `text-spacing` |
| 029 | `box-border-styles`, `box-sizing`, `html-tables-layout` |
| 030 | `box-sizing`, `effects-compositing`, `text-overflow`, `text-spacing` |
| 031 | `box-sizing`, `colors-modern`, `text-font-weight-and-stretch` |
| 032 | `box-radius`, `text-font-size` |
| 033 | `interaction-cursor-and-pointer`, `text-wrapping` |
| 034 | `box-border-styles`, `box-logical-sizes`, `forms-accent-and-caret`, `layout-overflow`, `text-decoration`, `text-font-weight-and-stretch`, `text-spacing` |
| 035 | `effects-box-shadow`, `lists-style-type`, `text-transform-and-shadow`, `text-wrapping` |
| 036 | `box-border-styles`, `effects-compositing`, `html-tables-layout`, `text-white-space` |
| 037 | `box-border-styles`, `box-radius-elliptical`, `cascade-keywords`, `layout-positioning`, `text-alignment`, `text-font-size` |
| 038 | `layout-inline-block` |
| 039 | `layout-flexbox` |
| 040 | `box-logical-borders`, `images-object-fit`, `selectors-states`, `selectors-type-and-only`, `text-decoration` |
| 041 | `images-object-position` |
| 042 | `backgrounds-clip-and-origin` |
| 043 | `images-object-fit` |
| 044 | `selectors-type-and-only` |
| 045 | `images-rendering` |
| 046 | `selectors-structural` |
| 047 | `selectors-structural` |
| 048 | `selectors-combinators-and-not` |
| 049 | `selectors-combinators-and-not` |
| 050 | `selectors-states` |
| 051 | `selectors-attributes-and-lang` |
| 052 | `images-rendering` |
| 053 | `selectors-states` |
| 054 | `selectors-states` |
| 055 | `lists-style-type` |
| 056 | `selectors-attributes-and-lang` |
| 057 | `text-decoration` |
| 058 | `text-wrapping` |
| 059 | `box-logical-sizes` |
| 060 | `box-logical-sizes` |
| 061 | `box-logical-borders` |
| 062 | `text-decoration` |
| 063 | `selectors-states` |
| 064 | `selectors-attributes-and-lang` |
| 065 | `units-lengths` |
| 066 | `units-calc-min-max-clamp` |
| 067 | `cascade-keywords` |
| 068 | `layout-overflow`, `text-overflow` |
| 069 | `text-alignment` |
| 070 | `text-white-space`, `text-wrapping` |
| 071 | `lists-position-and-list-item` |
| 072 | `html-tables-layout` |
| 073 | `box-aspect-ratio` |
| 074 | `interaction-cursor-and-pointer` |
| 075 | `box-radius-elliptical` |
| 076 | `backgrounds-image` |
| 077 | `backgrounds-gradients` |
| 078 | `effects-box-shadow`, `text-transform-and-shadow` |
| 079 | `layout-z-index` |
| 080 | `text-font-size`, `text-font-weight-and-stretch` |
| 081 | `text-font-variants` |
| 082 | `text-overflow` |
| 083 | `effects-transform-translate` |
| 084 | `colors-mix-and-color-function`, `colors-modern` |
| 085 | `units-calc-min-max-clamp` |
| 086 | `box-sizing-keywords` |
| 087 | `layout-alignment` |
| 088 | `box-border-styles-3d` |
| 089 | `layout-scrollbars` |
| 090 | `backgrounds-clip-and-origin`, `backgrounds-layers` |
| 091 | `text-hyphens-and-balance`, `text-white-space` |
| 092 | `text-overflow` |
| 093 | `layout-floats`, `lists-position-and-list-item` |
| 094 | `text-indent` |
| 095 | `effects-clip-path-shapes` |
| 096 | `effects-filter` |
| 097 | `cascade-custom-properties` |
| 098 | `lists-markers` |
| 099 | `box-border-styles-rounded` |
| 100 | `layout-positioning` |
| 101 | `text-shaping-latin` |
| 102 | `text-hyphens-and-balance` |
| 103 | `backgrounds-image`, `box-radius`, `images-object-position`, `selectors-attributes-and-lang`, `text-hyphens-and-balance`, `text-wrapping` |
| 104 | `backgrounds-image`, `interaction-cursor-and-pointer`, `layout-scrollbars`, `lists-style-type`, `units-lengths` |
| 105 | `animation-transitions`, `cascade-link-and-import`, `text-font-face-and-opentype` |
| 106 | `forms-progress-and-output` |
| 107 | `forms-grouping` |
| 108 | `forms-grouping` |
| 109 | `forms-grouping` |
| 110 | `cascade-link-and-import` |
| 111 | `html-picture-and-map` |
| 112 | `html-base-href` |
| 113 | `html-picture-and-map` |
| 114 | `text-ruby` |
| 115 | `box-logical-borders`, `effects-clip-path-shapes`, `layout-scrolling`, `text-hyphens-and-balance` |
| 116 | `cascade-keywords`, `effects-compositing`, `text-alignment`, `text-font-variants`, `text-wrapping` |
| 117 | `cascade-link-and-import`, `colors-modern`, `text-font-variants` |
| 118 | `layout-block-in-inline`, `lists-style-type` |
| 119 | `cascade-layers` |
| 120 | `animation-transitions`, `cascade-scope-and-namespace`, `layout-scrolling`, `text-font-size` |
| 121 | `generated-content`, `generated-counters` |
| 122 | `animation-transitions` |
| 123 | `cascade-supports` |
| 124 | `effects-drop-shadow-and-backdrop` |
| 125 | `effects-drop-shadow-and-backdrop` |
| 126 | `cascade-supports` |
| 127 | `animation-keyframes` |
| 128 | `layout-scroll-snap` |
| 129 | `backgrounds-blend-mode`, `backgrounds-layers` |
| 130 | `text-direction` |
| 131 | `text-emphasis` |
| 132 | `effects-compositing`, `effects-drop-shadow-and-backdrop`, `effects-masks-and-reflection` |
| 133 | `colors-mix-and-color-function`, `effects-clip-path-shapes`, `units-calc-min-max-clamp` |
| 134 | `colors-mix-and-color-function`, `effects-clip-path-shapes`, `units-math-functions` |
| 135 | `colors-mix-and-color-function`, `units-math-functions` |
| 136 | `units-math-functions` |
| 137 | `backgrounds-gradients`, `units-math-trig` |
| 138 | `units-math-trig` |
| 139 | `backgrounds-gradients-conic` |
| 140 | `effects-clip-path-polygon` |
| 141 | `text-shaping-latin`, `text-shaping-scripts` |
| 142 | `forms-accent-and-caret` |
| 143 | `forms-control-parts` |
| 144 | `forms-textarea-wrapping` |
| 145 | `forms-checkbox-radio-and-focus` |
| 146 | `forms-select` |
| 147 | `layout-scrollbars` |
| 148 | `backgrounds-border-image` |
| 149 | `effects-transform-functions` |
| 150 | `text-font-face-and-opentype` |
| 151 | `layout-grid-tracks` |
| 152 | `layout-grid-areas-and-alignment` |
| 153 | `cascade-custom-properties`, `cascade-scope-and-namespace`, `generated-counters` |
| 154 | `layout-floats` |
| 155 | `layout-multicol` |
| 156 | `cascade-container-queries` |
| 157 | `effects-masks-and-reflection` |
| 158 | `interaction-zoom-and-resize` |
| 159 | `text-bidi` |
| 160 | `text-vertical-writing` |
| 161 | `effects-motion-path` |
| 162 | `text-initial-letter` |
| 163 | `text-stroke` |
| a01 | `box-radius`, `effects-box-shadow` |
