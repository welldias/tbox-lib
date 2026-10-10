# Histórico de versões

> - **Escopo:** o que cada versão (v0–v20) trouxe, em forma condensada.
> - **Código relacionado:** todo o pipeline
> - **Veja também:** [Decisões](decisoes.md), [Débito técnico](debito-tecnico.md)

Este histórico substitui o antigo `ARCHITECTURE.md`, que crescia uma seção por
versão. Comentários no código ainda citam seções desse texto pelo nome
("v9 — Escopo", "Orchestration — correção de hit-test pra caixas fora de
fluxo", "Convenções" etc.). Os nomes estão preservados abaixo, em negrito,
para busca; o texto integral original está no git:

```sh
git show fe51140:ARCHITECTURE.md
```

Cada versão foi uma "fatia vertical": um critério de pronto ponta a ponta,
validado com `example/tbox_app_demo.html` e, a partir da v7, com screenshot
headless.

## Após v20 — Indicadores HTML

`<output>` passa a ter estilo inline e texto atualizável pelo DOM.
`<progress>` e `<meter>` usam caixas inline-block e pintura de barras passivas,
com atributos numéricos lidos a cada frame. Fixture: `tests/assets/106.html`.

## Após v20 — Anotações ruby

`<ruby>` agrupa trechos de texto-base e `<rt>` em células indivisíveis,
com anotação centralizada acima. `<rp>` fica oculto no layout suportado.
Exemplo e teste: `tests/assets/114.html`.

## Após v20 — Recursos locais e mapas de imagem

`<link rel=stylesheet>` lê CSS local em ordem com `<style>`. `<source>`
seleciona a imagem em `<picture>`, e `<base href>` define o prefixo local para
CSS vinculado e imagens. `<map>`/`<area>` traduzem cliques na imagem para
regiões retangulares, circulares ou poligonais. Testes e exemplos:
`tests/assets/110.html` a `113.html`.

## Após v20 — Grupos, detalhes e legendas HTML

`<optgroup>` acrescenta títulos e opções aninhadas ao `<select>`.
`<details>`/`<summary>` alternam o atributo `open` por clique ou teclado;
o layout omite o conteúdo fechado. O primeiro `<legend>` é posicionado
sobre a borda de `<fieldset>`, que abre espaço para seu texto. Fixtures:
`tests/assets/107.html` a `109.html`.

## v0 — HTML+CSS estático numa janela

Primeira fatia: um `<div>` com `width`/`height`/`background-color` vindos do
CSS, e `<h1>`–`<h6>`/`<p>` com texto, passando por todo o pipeline real.

- **Visão geral das camadas**: Base, HTML Parser, CSS Parser, CSS Selector e
  CSS Cascade já existiam; entraram **Style**, **Fonte / Texto**, **Layout
  Tree**, **Render Pipeline**, **Output Display**, **Orchestration / Main
  Loop** e **Application**.
- **Convenções** (hoje em [convencoes.md](convencoes.md)).
- **Fonte / Texto — camada transversal**: `tbox_font_source` (fontconfig e
  embedded/Liberation Sans), `tbox_font_face`, rasterização de glifo; uma face
  de 16px para o documento todo, sem shaping.
- **Style — resolução de valores tipados**: `display`, `width`, `height`,
  `margin`, `padding`, `color`, `background-color`; inicial de `display` =
  `block` por falta de UA stylesheet.
- **Layout Tree**: fluxo de bloco; texto só em `h1`–`h6`/`p` (lista fixa de
  "text tags"), uma linha, sem quebra; só o primeiro elemento de topo do
  documento é laid out.
- **Render Pipeline**: `FILL_RECT` e `TEXT_RUN` em pré-ordem.
- **Output Display**: `tbox_raster_*` em software (XRGB8888) + backend Wayland
  migrado de `example/` para `src/`.
- **Orchestration / Main Loop**: `tbox_context` com `frame_arena`, recompute
  total; hit-test linear.
- **Build system (v0)**: FreeType obrigatório; Fontconfig e Wayland opcionais.

## v1 — Interatividade

Um clique muda uma classe e a tela reflete, sem fechar a janela.

- **HTML Parser — mutação de atributo**: `tbox_html_node_set_attribute`/`_get_attribute`.
- **Orchestration (`tbox_context`) — delegação de evento por seletor**:
  `tbox_context_on_click`, `tbox_context_dispatch_click`, `tbox_context_document`.
- **Output Display (Wayland backend) — evento de ponteiro**: `take_click`.
- **Application (`tbox_app`) — loop não-bloqueante**: `tbox_app_create`/`_step`/
  `_should_close`/`_close` substituem `tbox_app_open`.

## v2 — Fidelidade Visual

Headings maiores e em negrito, margens de parágrafo, quebra de linha real.

- **CSS Cascade / Orchestration — folha de estilo user-agent** e
  **Configuração da UA stylesheet** (`tbox_ua_style_config`).
- **Style — `font-size` e `font-weight`** (`em`/`%`/`px` contra o pai).
- **Fonte / Texto — cache de faces por (peso, tamanho)**: `tbox_font_face_cache`.
- **Layout Tree — inline formatting context real**: `tbox_layout_text_run`,
  quebra gulosa por palavra; inline (`<b>`, `<em>`) contribui runs, sem caixa
  própria. **Cor não varia por run** (resolvido na v13).
- **Render Pipeline — múltiplos runs por caixa**.
- **Application / Orchestration — fiação do cache de fontes e do config**.

## v3 — Interatividade Avançada

- **Robustez de parsing — o que a auditoria confirmou**; **CSS Parser —
  correção do bug de profundidade de parênteses** (`FUNCTION` conta como
  abertura ao pular blocos).
- **HTML Parser — mutação de texto**: `tbox_html_node_set_text_content`.
- **`:hover` — pseudo-classe dinâmica de verdade**: `tbox_css_selector_set_hover_context`,
  `tbox_context_update_hover`.
- **Bubbling completo + `stopPropagation` + desregistro de handler**: handler
  retorna `bool`; `tbox_context_unbind_click`.
- **Application — leitura de arquivo externo**: `tbox_app_create_from_files`.

## v4 — Modelo de Caixa CSS Completo

- **Style — `border` (shorthand único, só `solid`)**.
- **Style — `position: relative` + offsets**.
- **Layout Tree — geometria de borda**; **deslocamento visual de `position:
  relative`**; **margin collapsing entre siblings**.
- **Render Pipeline — pintura da borda** (quatro `FILL_RECT`).

## v5 — `position: absolute`/`fixed`/`sticky`

- **Escopo deliberadamente contido**: `sticky` = `relative` (sem rolagem na
  época), sem `z-index`, sem §10.3.7 completo, sem posição estática.
- **Style — `position: absolute`/`fixed`/`sticky`**.
- **Layout Tree — containing block posicionado** (`tbox_layout_positioned_context`);
  **filhos fora de fluxo**; **geometria de `absolute`/`fixed`**.
- **Orchestration — correção de hit-test pra caixas fora de fluxo**: visita
  todos os filhos e fica com o último que casa.

## v6 — Robustez de HTML

- **HTML Parser — fechamento implícito de `<p>`/`<li>`**.
- **HTML Parser — decodificação de entidades**: numéricas + ~23 nomeadas;
  `;` obrigatório; nada em `<script>`/`<style>`.

## v7 — Listas HTML e tabela completa de entidades

- **Layout Tree — `<li>` como text tag**.
- **HTML Parser — tabela completa de entidades nomeadas**: gerada do
  `entities.json` do WHATWG (~2200 entradas de 1 codepoint); nomes com dígitos.

## Ferramentas de desenvolvimento — captura de tela headless (`--screenshot`)

- **Output Display — `tbox_raster_write_png`** (sem libpng/zlib).
- **Application — `tbox_app_screenshot_from_files`**.
- **Example — flag `--screenshot <path>`** em `example/tbox_app.c`.

## v8 — Marcadores de lista + `em` além de `font-size`

- **Style — `em` em `width`/`height`/`margin`/`padding`/offsets** (contra o
  `font-size` do próprio nó).
- **Layout Tree — marcador de `<li>`** (texto prefixado; substituído na v18).
- **CSS Cascade / Orchestration — UA stylesheet de `<ul>`/`<ol>`/`<li>`**.

## v9 — CSS de autor: inline (`style=""`) e `<style>` interno

- **CSS Cascade — `TBOX_CSS_ORIGIN_AUTHOR_INLINE` + resolução de inline dentro
  de `tbox_css_cascade_resolve`** (tabela de rank com 4 origens).
- **Orchestration — extração de `<style>` do documento inteiro**
  (`internal_stylesheet`; interno vence externo em empate).

## v10 — `tbox_cmp`: regressão visual contra fixtures reais

- `tests/tbox_cmp.cpp` (C++/OpenCV, opcional, fora do `ctest`), pares
  `tests/assets/NNN.html`/`NNN.png`.
- **Build — OpenCV opcional (C++ isolado) + `tbox_cmp` como executável próprio**.

## v11 — `<hr>`, `<br>`, `<pre>` e `text-align`

- **Style — `text-align`** (`left`/`center`/`right`).
- **Layout Tree — `<br>` (quebra forçada)**, **`<pre>` (texto verbatim)**,
  **aplicação de `text-align`**.
- **Orchestration — UA stylesheet de `<hr>`**.

## v12 — CSS `font-family`

- **CSS Cascade / Font — assinaturas exatas**: `tbox_font_resolver_fn`.
- **Font — cache com resolução sob demanda** por (família, peso).
- **Style — `font-family`** (primeiro nome, `char[64]`).
- **Application — resolução sob demanda + limpeza de duplicação**.
- **Layout Tree — os 6 pontos de chamada**.
- **Orchestration — `<pre>` monoespaçado via UA stylesheet**.

## v13 — `<strong>`, `<i>`, `<em>`, `<small>`, `<mark>`, `<del>`, `<ins>`, `<sub>`, `<sup>`

- **Font — `italic` deixa de ser hardcoded**.
- **Style — `font-style`, `text-decoration`, `vertical-align`**.
- **Layout Tree — `tbox_layout_text_run.style` + alinhamento de linha de base**
  (`tbox_layout_line.ascent`).
- **Render Pipeline — destaque de fundo e linha de decoração por run**.

## v14 — Caixas de bloco anônimas

Conteúdo inline solto dentro de `<body>`/`<div>` sumia. Agora cada sequência
contígua de inline vira uma caixa anônima (`node == NULL`).

- **Escopo**, **Layout Tree — assinaturas exatas**, **Algoritmo de
  `tbox_layout_build_children` (detecção de sequência)**.

## v15 — Infraestrutura de layout e `inline-block`

- **Infraestrutura**: coleta inline recursiva, `tbox_layout_translate`,
  `tbox_layout_build_element_sized`, tamanhos intrínsecos (min/max-content).
- **`inline-block` (e `inline-flex`)** como palavra atômica na linha.

## v16 — Flexbox

`tbox_layout_build_flex`, seguindo CSS Flexbox Level 1 §9 (itens, base,
linhas, resolução de comprimentos flexíveis, alinhamentos, margens auto).

## v17 — Fechando as limitações da v15/v16

`<img>` como caixa própria (block/flex item), inline-block com descendentes
posicionados, `vertical-align` aumentando a linha, flex com min/max-height
sem altura definida, controles de formulário como `inline-block` na UA.

## v18 — Novos atributos de estilo

- **Style — unidades, `calc()` e palavras-chave globais**.
- **Style — demais propriedades** (overflow-x, z-index, aspect-ratio,
  line-clamp, transform translate, sombras em lista, `background` completo…).
- **Layout Tree** (marcador `outside`, `table-layout: fixed`, small-caps,
  `tbox_layout_finish`).
- **Render Pipeline / Output Display** (gradientes, cantos elípticos,
  contextos de empilhamento, `tbox_render_paint_order`).
- **Orchestration / Application** (`tbox_context_cursor_at`, cursor via
  `wayland-cursor`).

## v19 — Mais atributos de estilo

- **CSS Parser / Cascade**: custom properties, `::marker`, cores CSS Color 4/5.
- **Style**: `var()`, `min()`/`max()`/`clamp()`, min/max/fit-content, filtros,
  `clip-path`, várias camadas de fundo…
- **Layout Tree**: `text-indent: hanging`, `text-wrap: balance`, soft hyphen,
  `display: list-item`, kerning.
- **Render / Output**: bordas 3D, tracejado arredondado, `filter`, `clip-path`.
- **Orchestration**: `position: sticky` real (eixo vertical), `scrollbar-width: none`.

## v20 — Repintura parcial (damage tracking)

Antes cada frame sujo repintava a janela inteira; agora só as regiões que
mudaram. Ver [Atualização da tela](../05-atualizacao-da-tela.md).

- **Output Display**: `tbox_damage_tracker`, `tbox_raster_display_list_damaged`,
  dano por região no Wayland.
- **Application**: `tbox_app_set_debug_damage`.
- **Detecção de mudanças por hash** (FNV-1a por op).
- **Próximos passos**: em [Débito técnico](debito-tecnico.md#pipeline).

## Sem número de versão

Entraram ao longo das versões acima sem seção própria no documento antigo
(só no README e no código):
controles de formulário (inputs de texto, data, cor, arquivo, range, select,
textarea), foco por teclado, seleção e clipboard, rolagem com scrollbars,
tabelas, `@media`, `transition`, `@font-face`, `:active`, temas dos popups
(`tbox_context_options.control_css`) e a **Entrada e fronteira de backend**
(`src/output/tbox_window_backend.h`, `tbox_input_event`).

Daqui em diante, cada mudança relevante ganha uma entrada curta aqui.
