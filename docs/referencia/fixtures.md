# Referência — fixtures visuais por recurso

> - **Escopo:** qual arquivo de `tests/assets/` demonstra cada recurso de CSS/HTML. Útil para ver um recurso funcionando ou conferir uma regressão.
> - **Código relacionado:** `tests/assets/`, `example/assets_showrun.c`, `tests/tbox_cmp.cpp`
> - **Veja também:** [Ferramentas e testes](../12-ferramentas-e-testes.md), [CSS suportado](css-suportado.md)

Abra qualquer um com `build/example/tbox_assets_showrun` (setas navegam) ou
gere um PNG com `tbox_app_screenshot_from_files`. Os fixtures `001`–`039`
são casos gerais. Os de `001` a `052` têm um `NNN.png` de
referência capturado num navegador, usado pelo `tbox_cmp` (o de `047` está
salvo como `47.png` e por isso o `tbox_cmp` não o encontra); os demais não têm
referência. A tabela abaixo cobre os que demonstram um recurso específico.

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
