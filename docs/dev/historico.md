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

## Após v20 — Funções CSS de custo médio

`conic-gradient()`/`repeating-conic-gradient()` com ângulo inicial, centro,
stops angulares/percentuais, fundos e máscaras. `polygon()` com até 32 vértices,
`nonzero`/`evenodd`, composição aninhada e recorte de eventos/barras de rolagem.
Fix-up de stops omitidos agora compara com a última posição explícita.
Hash de polígonos por geometria; testes de pixels, hit-test e repintura parcial.
Fixtures 139–140. Campos novos de estilo/pintura exigem recompilar consumidores.

## Após v20 — Catálogo por custo e trigonometria inversa

Catálogo das 27 funções então ausentes, ordenadas por custo e dependências.
Implementadas as quatro fáceis: `asin()`, `acos()`, `atan()` e `atan2()`,
com ângulos tipados, expressões angulares e consumo em gradientes/filtros.
Testes de validação e equivalência de pixels; fixture 138. Restam 23 ausentes.

## Após v20 — Trigonometria e raios radiais

`sin()`/`cos()`/`tan()` aceitam números em radianos e ângulos literais. Os
gradientes radiais, inclusive repetidos/máscaras, aceitam raios explícitos de
círculo/elipse, com percentuais por eixo na elipse. O hash de repintura inclui
os raios. Testes de estilo, pixels e equivalência de repintura parcial;
fixture `tests/assets/137.html`. O acréscimo de `radius` à estrutura pública
`tbox_style_gradient` exige recompilar consumidores.

## Após v20 — Sinal, constantes e opacidade calculada

`abs()`/`sign()` operam sobre números/comprimentos; `hypot()` passa a aceitar
comprimentos compatíveis. `pi`/`e` são operandos numéricos em cálculos.
`opacity` reutiliza o avaliador, convertendo percentuais para números e
limitando a faixa somente no final. Testes de estilo e pixels, com tipos,
sinais de zero e rejeição de entradas inválidas; fixture `tests/assets/136.html`.

## Após v20 — Hipotenusa, passos e validação de color-mix

`hypot()` avalia até oito números. `round()` oferece quatro estratégias em
números/comprimentos; `mod()` e `rem()` distinguem os sinais do divisor e do
dividendo. Leitura de argumentos compartilhada com exponenciais/comparações.
`color-mix()` rejeita espaços desconhecidos, pesos não finitos/fora de 0%–100%
e soma zero, mantendo a normalização de pesos válidos. Testes de estilo e
cores; fixture `tests/assets/135.html`.

## Após v20 — Cores explícitas sRGB, rect e exponenciais

`color()` aceita `srgb` e `srgb-linear`, componentes numéricos/percentuais e
alfa. `clip-path: rect()` compartilha `round` e a representação de `inset()`,
com coordenadas, `auto` e dimensões vazias para arestas cruzadas. `pow()`,
`sqrt()`, `exp()` e `log()` avaliam números finitos dentro dos cálculos de
comprimento. Testes de cores, estilo e pixels; fixture `tests/assets/134.html`.

## Após v20 — Cálculos percentuais, recortes e matiz

`min()`/`max()`/`clamp()` comparam percentuais e expressões afins compatíveis;
`min()`/`max()` de um argumento preservam o valor. `clip-path: xywh()` reutiliza
`inset()`, cujo `round` passa a preservar percentuais e a normalizar raios
elípticos juntos. `color-mix(in oklch … hue, …)` aplica os quatro percursos de
matiz. Testes de estilo, cores e render; fixture `tests/assets/133.html`.
Os campos percentuais acrescentados a `tbox_style_clip_path` exigem recompilar
consumidores da biblioteca.

## Após v20 — Organização das funções CSS

Extraídos os parsers de funções para `tbox_style_functions.c` e os utilitários
de valores para `tbox_style_values.c`, com contratos internos compartilhados.
A resolução de propriedades e o comportamento CSS permanecem preservados.

## Após v20 — Catálogo de funções CSS

Auditoria das 80 entradas da tabela de funções CSS: marca suporte existente,
suporte com limitações e ausências, com referências à implementação.

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

## 2026-10-11 — Propriedades CSS de baixo custo

- **Style**: `border-block-width/-style/-color` e `border-inline-*` (um ou
  dois valores); `hyphenate-character` (herdado, string até 15 bytes);
  `clip: rect()` convertido em `clip-path: inset()`; `overscroll-behavior`
  (eixo vertical); `scroll-margin-*` e `scroll-padding-*` (com formas
  lógicas, via `tbox_style_resolve_box_edges`). `tbox_style` ganha
  `overscroll_y`, `hyphenate_character`, `scroll_margin[4]`,
  `scroll_padding[4]`.
- **Layout Tree**: o traço da quebra em soft hyphen vem de
  `hyphenate-character`.
- **Orchestration**: `tbox_context_scroll` não passa a rolagem ao ancestral
  com `overscroll-behavior` diferente de `auto`; `tbox_context_reveal_focused`
  respeita `scroll-padding` e `scroll-margin`.
- Fixture `115`.

## 2026-10-11 — Mais propriedades CSS de baixo custo

- **Style**: `isolation` (`isolation_isolate`), `text-justify`
  (`text_justify_none`), `line-break` (`line_break_anywhere`),
  `font-variant-position` sintetizado (`font_variant_position`; encolhe a
  fonte onde o valor muda e mapeia para `vertical-align: sub/super`) e
  `all` (expande a palavra-chave para todas as propriedades da tabela de
  `tbox_style_keywords.c`). O `display` inicial usado por
  `initial`/`unset`/`all` passou a ser `inline`, como no CSS.
- **Layout Tree**: `text-justify: none` não distribui espaço;
  `line-break: anywhere` quebra como `break-all` e entra no min-content.
- **Render**: `isolation: isolate` cria contexto de empilhamento.
- Referência: `@media` e `@font-face` estavam marcados como não suportados
  por engano; agora `X*`.
- Fixture `116`.

## 2026-10-11 — Media features, `@import`, `font-synthesis`, `light-dark()`

- **CSS Cascade**: `tbox_css_media_feature` ganha `aspect-ratio`,
  `resolution`, `color`/`monochrome`, a forma booleana e features de valor
  fixo (`prefers-color-scheme: light`, `hover`, `pointer: fine`,
  `scripting: none`…).
- **Orchestration**: `@import` no início de `<style>`, `<link>` e do CSS
  da aplicação é expandido no texto antes do parse
  (`tbox_context_append_css`), relativo à folha que importa.
- **Style**: `font-synthesis` (`font_synthesis_no_small_caps`,
  `font_synthesis_no_position`); `color-scheme` (`color_scheme_dark`) e
  `light-dark()`, substituído no texto depois de `var()`
  (`tbox_style_light_dark_substitute`).
- Referência: `@charset` marcado como suportado (já era ignorado sem
  quebrar nada). Fixture `117`.

## 2026-10-11 — Bloco dentro de parágrafo e listas aninhadas

- **Layout Tree**: `tbox_layout_is_inline_box` (classificação e medição
  intrínseca) faz de `<p>`, `<h1>`–`<h6>`, `<li>`, `<td>`/`<th>`,
  `<caption>`, `<summary>` com filho de bloco em fluxo um container de
  bloco; substitui o desvio por nome de tag que só o `<td>` tinha. O
  marcador do `<li>` passa à primeira caixa anônima
  (`tbox_layout_build_anonymous_box`/`tbox_layout_build_text_runs` ganham
  `marker_node`), ou fica ao lado do primeiro bloco.
- **HTML Parser**: o fechamento implícito de `<li>` para na fronteira da
  lista (`tbox_html_tree_builder_find_open_li`); listas aninhadas sem
  `</li>` explícito do item de fora deixaram de virar irmãs.
- Débito "Bloco dentro de conteúdo inline some" resolvido; ficaram "Bloco
  dentro de um elemento inline" e "Margem de listas aninhadas". Fixture
  `118`. Nenhum fixture de 001–116 mudou de pixels.

## 2026-10-11 — `@layer`

- **CSS Parser**: `@layer` (declaração, bloco, nomes com ponto, anônima,
  aninhada com `@media`). Cada regra recebe `tbox_css_ruleset.layer` (0 =
  fora de camada; senão a posição pós-ordem da camada na folha). Antes, o
  conteúdo de um `@layer` era descartado.
- **CSS Cascade**: `tbox_css_cascade_priority_compare` compara a camada
  entre origem/importância e especificidade (invertida para `!important`).
- **Orchestration**: `@import … layer(nome)`/`layer` embrulha o texto
  importado em `@layer`; declarações `@layer a, b;` antes dos `@import`
  são mantidas no lugar.
- Fixture `119`. Nenhum fixture de 001–116 mudou de pixels.

## 2026-10-11 — Cursor padrão sobre texto

- **Orchestration**: com `cursor: auto`, `tbox_context_cursor_at` não
  devolve mais o cursor de texto sobre texto comum, só sobre campos
  editáveis; `cursor: text` explícito continua valendo.

## 2026-10-11 — `font-size-adjust`, `scroll-behavior`, `@starting-style`, `@namespace`

- **Font**: `tbox_font_face_x_height` (OS/2 `sxHeight`, ou o glifo "x").
- **Style / Layout Tree**: `font-size-adjust` (`font_size_adjust`),
  aplicado só na escolha da face (`tbox_layout_style_face`);
  `scroll-behavior` (`scroll_behavior_smooth`).
- **Orchestration**: rolagem suave em `tbox_scroll_state` (animação pelo
  relógio das transições) para revelar o foco e clique na trilha;
  transições de entrada com `@starting-style`, inclusive ao sair de
  `display: none`.
- **CSS Parser / Cascade**: `@starting-style` marcado em
  `tbox_css_ruleset.starting_style`, fora da cascata normal;
  `tbox_css_cascade_resolve_starting_style_in_viewport`.
- **Application**: o screenshot headless avança animações de entrada até o
  fim antes de capturar.
- Referência: `@namespace` marcado (ignorado, o que para HTML é correto).
  Fixture `120`; nenhum outro fixture mudou de pixels.

## 2026-10-11 — `::before`/`::after`, contadores e `quotes`

- **CSS Parser**: `:before`/`:after`/`:first-line`/`:first-letter` (CSS2)
  normalizados para `::`; `@starting-style` à parte (ver acima).
- **CSS Cascade**: pseudo-elementos pesam como seletor de tipo (antes
  `::marker` pesava como classe).
- **Style**: `tbox_style_generated.c` — durante `tbox_style_resolve_tree`
  (ordem de documento) resolve `::before`/`::after` só quando alguma regra
  os usa, mantém contadores com escopo e a profundidade de `quotes`, e
  monta o texto de `content`. Cada pseudo vira um nó sintético no arena da
  tabela de estilos, com entrada própria (`tbox_style.generated` = 1/2).
- **Layout Tree**: `tbox_layout_build` encaixa os nós sintéticos durante a
  montagem e os retira no fim.
- **Orchestration**: `tbox_context_hit_test` devolve a caixa do elemento
  dono quando o ponto cai em conteúdo gerado (os nós sintéticos duram um
  frame); transições ignoram os pseudos.
- Fixture `121`; nenhum outro fixture mudou; suíte limpa com ASan/UBSan.

## 2026-10-12 — Transições ampliadas, `@supports`, `@keyframes`, efeitos e `direction`

- **Style**: `tbox_style_animate.c` — tabela de propriedades animáveis
  (`tbox_style_animatable`, máscara por nome em `transition-property`),
  leitura/escrita como 4 doubles e funções de tempo (`cubic-bezier()`,
  `steps()`). `transition` vira uma lista (`tbox_style.transitions`,
  substitui `transition_duration[3]`/`_delay`/`_timing`) com os longhands
  pareados por índice; `animation` (`tbox_style.animations`);
  `drop-shadow()` em `filter`; `backdrop-filter`
  (`backdrop_filter_matrix`); `background-blend-mode` por camada;
  `scroll-snap-type`/`-align`/`-stop`; `direction` (e o atributo `dir`) com
  o mapeamento lógico invertido e `text_align_logical`; `text-emphasis`.
  Novas entradas na tabela de palavras-chave globais.
- **CSS Parser**: `@supports` avaliado no parse (`tbox_css_supports`, também
  usado no `supports()` do `@import`); `@keyframes` guardado na folha
  (`tbox_css_stylesheet_keyframes`).
- **CSS Cascade**: `tbox_css_cascade_media_matches` público, para os
  `@keyframes` dentro de `@media`.
- **Orchestration**: transições em todas as propriedades animáveis, com o
  estado de trilhas alocado sob demanda; `tbox_context_animation.c` roda as
  animações depois das transições; scroll snap em `tbox_scroll_state`
  (encaixe 0,15 s depois do último giro da roda, ao soltar a barra ou depois
  de um clique na trilha).
- **Layout Tree**: flex `row` começa na direita com `direction: rtl`;
  `tbox_layout_text_run.emphasis_font` para marcas `<string>`.
- **Render Pipeline / Output**: op `TBOX_PAINT_BACKDROP_FILTER`;
  `tbox_paint_op.blend_mode` em `IMAGE`/`GRADIENT` (16 modos no raster, no
  hash do damage tracker); marcas de `text-emphasis`.
- Referência: animation\*, `@keyframes`, `@supports`, `backdrop-filter`,
  `background-blend-mode`, `direction`, scroll-snap\*, text-emphasis\* e as
  propriedades aceitas sem efeito (break-\*, page-break-\*, `orphans`,
  `widows`, `clear`, `overflow-anchor`, `perspective`, `perspective-origin`,
  `transform-style`, `backface-visibility`) marcadas `X*`. Corrigida a lista
  "O que fica de fora" de [HTML e CSS](../07-html-e-css.md), que ainda
  citava `::before`/`::after` e `@import`.
- Fixtures `122`–`131`; nenhum fixture de 001–121 mudou de pixels; suíte
  limpa com ASan/UBSan.

## 2026-10-12 — Camadas de composição

- **Style**: `filter` e `backdrop-filter` como listas ordenadas
  (`tbox_style_filter_list`: matriz de cor, `blur()`, `drop-shadow()`);
  `has_filter`/`filter_matrix` ficam só para filtros de cor;
  `backdrop_filter_matrix` saiu. `mix-blend-mode`, `mask`/`mask-*` (e
  `-webkit-mask*`), `tbox_style_needs_layer`, `tbox_style_filter_extent`. A
  aproximação de `drop-shadow()` por `box-shadow`/`text-shadow` saiu.
- **Layout Tree**: `tbox_layout_box.mask_image`.
- **Render Pipeline**: ops `LAYER_BEGIN`/`LAYER_MASK`/`LAYER_END` em volta
  de quem precisa de camada (o elemento vira contexto de empilhamento,
  também com filtro de cor ou `backdrop-filter`); `opacity` sai do op a op
  para o `LAYER_BEGIN`; `clip-path` de um elemento com camada recorta a
  composição; o fundo com `background-blend-mode` ganha camada própria.
- **Output**: pixels pré-multiplicados (a tela segue opaca); pintor com
  pilha de camadas (`tbox_raster_paint_range`), blur gaussiano, sombra,
  máscara alfa/luminância e composição com `mix-blend-mode`; o
  `BACKDROP_FILTER` roda a lista de filtros (com blur). Damage tracker:
  bounds de camada, ops inflados pelo alcance do blur, e a região suja
  cresce para cobrir o que um `backdrop-filter` com blur lê.
- **CSS Parser**: `@supports` aceita `blur()` e conhece `mix-blend-mode` e
  `mask-*`.
- Fixture `132` (e `mask-star.png`). Mudaram de pixel: `036` (texto sobre o
  próprio fundo com `opacity` deixou de ficar transparente, como no
  navegador), `116` (1 nível de arredondamento em `isolation`), `124` e
  `125` (sombra na forma e blur no backdrop). Suíte limpa com ASan/UBSan.

## 2026-10-12 — Build otimizado com `-Werror`

- O build `Release` (e `RelWithDebInfo`/`MinSizeRel`) falhava: com
  otimização o GCC analisa faixas de valores e avisava
  (`-Wformat-truncation`) que o `snprintf` de `<input type=week>` podia
  truncar. Era um defeito real: navegando o calendário além do ano 9999,
  o valor gravado saía cortado. **Orchestration**:
  `tbox_context_date_commit` recusa anos fora de 1–9999 (o formato e os
  parsers só aceitam 4 dígitos), usa buffers folgados e confere o tamanho
  escrito.
- `tests/base/test_list.c`: o vetor preenchido no laço é inicializado
  (`-Wmaybe-uninitialized` em `-O3`) e o índice é limitado.
- Todos os tipos de build (GCC e Clang) compilam limpos e passam no `ctest`.
