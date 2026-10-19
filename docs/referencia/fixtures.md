# Referência — fixtures visuais por recurso

> - **Escopo:** qual arquivo de `tests/assets/` demonstra cada recurso de CSS/HTML. Útil para ver um recurso funcionando ou conferir uma regressão.
> - **Código relacionado:** `tests/assets/`, `example/assets_showrun.c`, `tests/tbox_cmp.cpp`
> - **Veja também:** [Ferramentas e testes](../12-ferramentas-e-testes.md), [CSS suportado](css-suportado.md)

Abra qualquer um com `build/example/tbox_assets_showrun` (setas navegam) ou
gere um PNG com `tbox_app_screenshot_from_files`. Os fixtures `001`–`039`
são casos gerais. Os textos são em inglês. A referência de cada um é
capturada no Firefox pelo `tbox_cmp` a cada execução, sem PNG guardado. A
tabela abaixo cobre os que demonstram um recurso específico.

| Fixture | Recurso |
|---|---|
| 040 | `object-fit` em imagens |
| 041 | `object-position` |
| 042 | `background-clip` |
| 043 | `object-fit: none` e `scale-down` |
| 044 | `:root`, `:first-of-type`, `:last-of-type`, `:only-of-type` |
| 045 | `image-rendering: pixelated` |
| 046 | `:nth-child()`, `:nth-of-type()` |
| 047 | `:nth-last-child()`, `:nth-last-of-type()` |
| 048 | combinador `~` |
| 049 | `:not()` |
| 050 | `:required`, `:optional` |
| 051 | `:lang()` |
| 052 | `image-rendering: crisp-edges` |
| 053 | `:focus-within` |
| 054 | `:link`, `:any-link` |
| 055 | `list-style-type: decimal-leading-zero` |
| 056 | seletores de atributo `^=`, `$=`, `*=` |
| 057 | `text-decoration-style: wavy` |
| 058 | `overflow-wrap: anywhere` |
| 059 | `inline-size`, `block-size` |
| 060 | min/max lógicos (`min-inline-size` etc.) |
| 061 | `border-block`, `border-inline` |
| 062 | `text-underline-position: under` |
| 063 | `:hover` em ancestrais |
| 064 | modificador `i` em seletores de atributo |
| 065 | unidades `rem`, `ex`, `ch`, `pt`, `pc`, `in`, `cm`, `mm`, `Q`, `vw`/`vh`/`vmin`/`vmax` |
| 066 | `calc()` |
| 067 | `inherit`, `initial`, `unset` (`revert`) |
| 068 | `overflow-x` e `overflow` com dois valores |
| 069 | `text-align-last` |
| 070 | `white-space: break-spaces`, `tab-size` |
| 071 | `list-style-position` |
| 072 | `empty-cells: hide`, `table-layout: fixed`, linhas `visibility: collapse` |
| 073 | `aspect-ratio` |
| 074 | `cursor`, `user-select` |
| 075 | `border-radius` elíptico |
| 076 | `background-image`, `-size`, `-position`, `-repeat` |
| 077 | `linear-gradient()`, `radial-gradient()`, `repeating-*` |
| 078 | listas de `box-shadow` (com `inset`) e `text-shadow` |
| 079 | `z-index` e contextos de empilhamento |
| 080 | `font-weight` numérico, `font-stretch` |
| 081 | `font-variant: small-caps` |
| 082 | `line-clamp` |
| 083 | `transform: translate*()`, `translate` |
| 084 | cores CSS 4/5 (`hwb`, `lab`, `lch`, `oklab`, `oklch`, `color-mix`) |
| 085 | `min()`, `max()`, `clamp()` |
| 086 | `width: min-content / max-content / fit-content` |
| 087 | `place-content`, `place-items`, `place-self` |
| 088 | bordas e contornos `groove`, `ridge`, `inset`, `outset` |
| 089 | `scrollbar-color`, `scrollbar-width` |
| 090 | `background-origin` e várias camadas de fundo |
| 091 | `text-wrap` (`nowrap`, `balance`), `text-wrap-mode`, `white-space-collapse` |
| 092 | `text-overflow: "<string>"` |
| 093 | `display: flow-root`, `display: list-item` |
| 094 | `text-indent` com `hanging`, `each-line` |
| 095 | `clip-path: inset()`, `circle()`, `ellipse()` |
| 096 | `filter` de cor |
| 097 | custom properties e `var()` |
| 098 | `list-style-image`, `::marker` |
| 099 | bordas tracejadas e pontilhadas em caixas arredondadas |
| 100 | `position: sticky` |
| 101 | `font-kerning` |
| 102 | soft hyphen (`&shy;`) e `hyphens` |
| 103 | `word-wrap`, `text-wrap-style`, raios de canto lógicos, `background-repeat: space/round`, modificador `s` |
| 104 | unidades `sv*`/`lv*`/`dv*`, mais cursores, `lower-greek`/`disclosure-*`, `:active`, `background-attachment: fixed`, `scrollbar-gutter: stable` |
| 105 | `@media`, `transition`, `@font-face` |
| 106 | `<output>`, `<progress>` e `<meter>` |
| 107 | `<optgroup>` com grupos habilitados e desabilitados |
| 108 | `<details>`/`<summary>` fechado e aberto |
| 109 | `<legend>` na borda de `<fieldset>` |
| 110 | `<link rel="stylesheet">` com CSS local |
| 111 | `<source>` em `<picture>` com `<img>` fallback |
| 112 | `<base href>` para CSS e imagens locais |
| 113 | `<map>`/`<area>` com regiões retangulares, circulares e poligonais |
| 114 | `<ruby>`, `<rt>` e `<rp>` com anotação simples e múltiplos trechos |
| 115 | `border-block-*`/`border-inline-*` (longhands por eixo), `hyphenate-character`, `clip: rect()`, `overscroll-behavior`, `scroll-margin`, `scroll-padding` |
| 116 | `isolation`, `text-justify`, `line-break: anywhere`, `font-variant-position`, `all` |
| 117 | `@import` aninhado (`117.css`, `117-import/`), media features, `font-synthesis`, `color-scheme` e `light-dark()` |
| 118 | bloco dentro de `<p>`/`<h3>`/`<li>`/`<td>`, listas aninhadas |
| 119 | `@layer` (ordem, subcamadas, anônima, dentro de `@media`, `!important`) e `@import … layer()` (`119-reset.css`) |
| 120 | `font-size-adjust`, `scroll-behavior: smooth` (com Tab), `@starting-style`, `@namespace` |
| 121 | `::before`/`::after` (obrigatório, badge com `attr()`, status, breadcrumb, bloco vazio), `counter()`, `counters()`, `quotes` |
| 122 | `transition` em propriedades de layout e pintura (`width`, `padding`, bordas, `outline`, `translate`), `cubic-bezier()`, `steps()`, listas de longhands (passe o mouse) |
| 123 | `@supports` (`not`/`and`/`or`, `selector()`, custom property, dentro de `@media`) |
| 124 | `filter: drop-shadow()` em caixas com fundo, em texto e encadeado (sombra na forma desde as camadas de composição) |
| 125 | `backdrop-filter` (funções de cor e `blur()`) |
| 126 | propriedades aceitas sem efeito (impressão, 3D, `clear`, `overflow-anchor`) e `@supports` delas |
| 127 | `animation` e `@keyframes` (`infinite alternate`, keyframe intermediário, `steps()` com atraso, `forwards`, `paused`) |
| 128 | `scroll-snap-type` (`mandatory`, `proximity`), `scroll-snap-align`, `scroll-snap-stop` (role a lista) |
| 129 | `background-blend-mode` (16 modos, lista sobre várias camadas) |
| 130 | `direction: rtl` e atributo `dir` (propriedades lógicas, flex, `text-align: start/end`) |
| 131 | `text-emphasis` (formas cheias/abertas, `<string>`, `under`, herança) |
| 132 | camadas de composição: `opacity` de grupo, `filter: blur()`, `drop-shadow()` na forma, `mask-image`/`-webkit-mask` (`mask-star.png`), `mix-blend-mode`, `isolation`, `backdrop-filter: blur()`, `background-blend-mode` isolado |
| 133 | `min()`/`max()`/`clamp()` percentuais, `clip-path: xywh()`, raios percentuais em `inset(round ...)`, quatro percursos de matiz em `color-mix(in oklch)` |
| 134 | `color(srgb)`/`color(srgb-linear)`, `clip-path: rect()` com `auto` e `round`, `pow()`/`sqrt()`/`exp()`/`log()` em cálculos de comprimento |
| 135 | `hypot()`, `round()` com quatro estratégias, sinais de `mod()`/`rem()` e validação/normalização dos pesos de `color-mix()` |
| 136 | `abs()`/`sign()`, `hypot()` com comprimentos, constantes `pi`/`e` e `opacity` calculada com números/percentuais |
| 137 | `sin()`/`cos()`/`tan()` com radianos e ângulos literais; raios explícitos em gradientes radiais circulares, elípticos e repetidos |
| 138 | `asin()`/`acos()`/`atan()`/`atan2()`: pares com ângulos literais em gradientes e filtros; aninhamento em largura e opacidade |
| 139 | Gradientes cônicos: interpolação, setores, `from`/`at`, repetição angular, mosaico e máscara |
| 140 | `polygon()`: triângulo, concavidade, `nonzero`/`evenodd`, interseção de recortes e filtros |
| 141 | Shaping com HarfBuzz: ligaduras (e `letter-spacing` desligando-as), kerning GPOS com `font-kerning: none`, acentos combinantes e marcas empilhadas, formas contextuais árabes (isoladas com ZWNJ), hebraico com niqqud, frase árabe sem bidi; cursor dentro de ligadura em `<input>`/`<textarea>`. Sem HarfBuzz, as linhas "depois" saem iguais às "antes" |
| 142 | `accent-color` em range, progress, checkbox e radio: padrão, herdada do contêiner, por controle (com `var()`), meter sem mudança |
| 143 | Partes dos controles (`::slider-thumb`, `::slider-track`, `::slider-fill`, `::picker-icon`, `::checkmark`, `::file-selector-button`, `::step-*`, `::clear-icon`, `::color-swatch`, `::selection`), inclusive desabilitado, contêiner escuro com `var()` e raio por canto |
| 144 | Quebra de linha do `<textarea>`: entre palavras, espaços repetidos pendurados, quebras do próprio texto, palavra maior que a linha |
| 145 | Checkbox e radio marcados e desmarcados alinhados na mesma linha; campos de texto vazios e preenchidos alinhados; `outline` como anel de foco em radio (redondo), checkbox, botão, slider e campos com borda reta e arredondada |
| 146 | Lista do `<select>` pelo CSS padrão: `::picker(select)`, `option`/`optgroup`, `:hover`, `:checked`, `:disabled` (inclusive em `optgroup` desabilitado), tema por `var()`. Abra a lista para ver; o Firefox ainda mostra a lista nativa |
| 147 | Scrollbar: padrão discreto, `scrollbar-width: auto`, `scrollbar-color`, partes `::-tbox-scrollbar-track`/`-thumb` (largura, borda, raio, margem, altura mínima, `:hover`, `:active`) e `scrollbar-gutter: stable` com largura própria. O Firefox só aplica as propriedades padrão |
| 148 | `border-image`: gradientes como fonte (`slice` numérico/percentual, `fill`, `round`), imagem com `stretch`/`repeat`/`round`/`space`, `border-image-width` (comprimento e número), `border-image-outset` |
| 150 | Recursos OpenType com fonte local (`150-sans.ttf`, subset da Roboto, OFL): `tabular-nums`/`proportional-nums`, `oldstyle-nums`, `diagonal-fractions`, `font-variant-ligatures: none`, `font-feature-settings` (`"smcp"`, `"liga" 0`). Exige HarfBuzz |
| 153 | `@scope` (limite `to`, proximidade, `:scope`), `@property` (`inherits: false`, valor inválido cai no `initial-value`), `@counter-style` (`cyclic`, `additive`, `alphabetic` com `prefix`/`suffix`, `fixed` com fallback) |
| 149 | `transform` com `rotate()`, `scale()`, `skewX()`, `matrix()`, `rotateZ()`, `rotateX()` achatado, `transform-origin`, propriedades `rotate`/`scale`, composição `translate() rotate()` |
| 151 | Grid: trilhas fixas + `fr`, `repeat()`, `minmax()`, `auto-fill`, spans, linhas negativas e nomeadas |
| 152 | Grid: `grid-template-areas`, `grid-auto-flow: column`/`dense`, `justify-items`/`align-items`/`justify-self`/`place-self`, `justify-content: space-between`, `auto-fit` |
| 154 | `float: left/right`, floats lado a lado, `clear: both`, `display: flow-root` contendo float x bloco comum, `shape-outside: circle()` |
| 156 | `@container` em contêineres `inline-size` de 200/380/600px, unidades `cqi`, contêiner nomeado consultado através de um sem nome |
| 158 | `zoom` (1, 1.5, 50%, pai 2 com filho .5) e `resize` (`both`, `horizontal`, textarea padrão sem alça, textarea `vertical`) |
| 155 | Multicoluna: `column-count` com `column-rule`, `column-width`, `column-fill: auto` com altura fixa, `column-span: all`, regra tracejada com gap grande, bloco com fundo/borda partido entre colunas |
| 159 | Texto bidirecional: hebraico/árabe com números e latim em `ltr` e `rtl`, espelhamento de parênteses, `<bdo>`, `unicode-bidi: normal` x `isolate`, `plaintext`, `<bdi>` + `embed` |
| 163 | `-webkit-text-stroke` com `paint-order: normal` x `stroke`, texto vazado (`color: transparent`), `marker`/`mask-type`/`@page` aceitos sem quebrar a folha |
| 161 | Motion path: `offset-path: path()` com `offset-distance` 0/50/100% e `offset-rotate: auto`, `circle()`, `ray()`, `offset-anchor` |
| 157 | Duas camadas de `mask` com `mask-composite` (`add`, `subtract`, `intersect`, `exclude`), `box-decoration-break: slice` x `clone` em inline partido em 3 linhas, `-webkit-box-reflect` (N/A no Firefox) |
| 162 | `::first-letter` flutuante, `initial-letter: 3` e `3 2` (N/A no Firefox), `hanging-punctuation: first` e `allow-end` com texto justificado (N/A no Firefox) |
| 160 | Escrita vertical: `vertical-rl`, `vertical-lr`, `text-orientation: upright`, `text-combine-upright: all`, `sideways-lr`, fluxo ortogonal com `margin-block-start`/`padding-inline-start` |
