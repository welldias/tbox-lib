# Débito técnico conhecido

> - **Escopo:** trabalho conscientemente adiado, com o gatilho que justifica revisitar cada item. Só itens ainda abertos.
> - **Código relacionado:** indicado em cada item
> - **Veja também:** [Decisões](decisoes.md), [Limitações e roadmap](../14-limitacoes-e-roadmap.md)

Diferente de uma lista de features faltando (essa está em
[Limitações e roadmap](../14-limitacoes-e-roadmap.md)), aqui ficam decisões
de design adiadas que têm complexidade própria. Cada item diz **o que é**,
**por que importa**, **quando revisitar** e **o que toca**. Itens resolvidos
saem da lista (o histórico deles está em [historico.md](historico.md)).

## Limites de polígonos e gradientes cônicos

- **O que é:** polígonos limitam-se a 32 pontos, sem arredondamento,
  antialiasing das arestas, caixas explícitas ou interpolação de animação.
  São compostos em camada temporária; bounds de damage conservam a área
  original, sem otimização pelo polígono. Barras de rolagem anexadas depois
  do conteúdo recebem camadas extras para os polígonos ancestrais.
  Cônicos usam sRGB premultiplicado e até oito stops, sem hints, escolha de
  espaço de cor ou cálculos misturando ângulos e percentuais. Período nulo
  em repetidos ainda não produz a cor média especificada.
- **Quando resolver:** conforme casos reais exigirem fidelidade ou a medição
  mostrar custo das camadas; compartilhar a geometria entre pintura e eventos.
- **Código:** style functions, render, raster/damage e context hit-test/scroll.

## Limites do avaliador angular CSS

- **O que é:** `asin`/`acos`/`atan`/`atan2` operam com entradas finitas;
  `atan2` não resolve percentuais de comprimento pendentes nem dimensões de
  tempo/frequência. O contexto de opacidade converte percentuais em números.
  Ângulos suportam aritmética básica, comparações e trigonometria, mas ainda
  não as demais funções escalares (`abs`, `hypot`, `round`, `mod`, `rem`).
  Funções matemáticas em componentes de cor seguem fora desse avaliador.
- **Quando resolver:** ao ampliar os tipos matemáticos ou conectar novos
  consumidores; preservar a distinção entre ângulos, números e comprimentos.
- **Código:** `src/style/tbox_style_functions.c`, `tbox_style_internal.h`.

## Pipeline

### Estilo e layout incrementais

- **O que é:** desde a v20 só a repintura é parcial. `tbox_style_resolve_tree`
  e `tbox_layout_build` ainda rodam sobre o documento inteiro a cada frame
  sujo e passam a dominar o custo do frame (ver medição em
  [Atualização da tela](../05-atualizacao-da-tela.md#custo-medido)).
- **Por que importa:** é o próximo gargalo para hover, digitação e cursor
  piscando em documentos grandes.
- **Quando revisitar:** quando o tempo de estilo+layout for medido como
  problema num caso real. Esboço já discutido:
  - marcar nós sujos na origem da mudança (hover, foco, `:active`, texto,
    scroll, mutação via API);
  - estilo: invalidar o nó, os descendentes (herança) e os afetados por
    seletores dependentes (`:hover > x`, `+`, `~`);
  - layout: reaproveitar caixas cujo estilo e conteúdo não mudaram;
    mudança de tamanho propaga para pais e irmãos seguintes;
  - arenas: hoje tudo vive na `frame_arena`; reter árvores entre frames
    exige outro esquema de vida;
  - transições, sticky, scroll e hit-test leem a árvore completa e precisam
    continuar consistentes.

  O damage tracking da v20 continua válido: ele compara display lists,
  independentemente de como foram produzidas.
- **Toca:** `tbox_context_run_frame`, Style, Layout, arenas.

### Screenshot headless incremental

- **O que é:** o caminho de captura (`tbox_app_screenshot_from_files`) sempre
  repinta tudo.
- **Quando revisitar:** só se a captura passar a ser usada em loop.

### Integração com loops externos

- **O que é:** quem já tem um loop (glib, libuv, o executor de uma
  linguagem) só integra chamando `tbox_app_wait` com o próprio prazo e
  recebendo trabalho por `tbox_app_post`. Não há um fd + timeout exportado
  para ser vigiado de fora, nem um modo em que o loop é do chamador e o tbox
  só reage a callbacks (como o `SDL_AppIterate` do SDL3).
- **Por que importa:** no Windows e no macOS o sistema prende o loop em
  laços modais ao arrastar/redimensionar; com o loop do chamador, a janela
  congela nesse intervalo.
- **Quando revisitar:** junto do primeiro backend Windows/macOS, ou do
  motor de script.
- **Toca:** `src/app/tbox_app.c`, `tbox_window_backend.h`.

## API e modelo de documento

### Caminhos em folhas CSS vinculadas

- **O que é:** `<link rel=stylesheet>` concatena CSS local às folhas do
  documento. Isso preserva a ordem das regras, mas `url()` de `@font-face`
  ainda é resolvido contra `asset_base_dir`, não contra o diretório da folha
  (o `@import` já resolve contra a folha que importa).
- **Quando revisitar:** quando um exemplo precisar de fontes em folhas CSS
  armazenadas em subdiretórios.
- **Toca:** `src/context/tbox_context.c`, registro de fontes e cascata.

### Memória do DOM em sessões longas

- **O que é:** `tbox_html_node` vive na arena do documento sem `free`
  individual. `tbox_html_node_remove`, `set_attribute`, `remove_attribute` e
  `set_text_content` deixam a memória antiga órfã até o documento ser
  destruído.
- **Por que importa:** uma UI de vida longa com milhares de mutações só faz a
  arena crescer.
- **Quando revisitar:** quando medido como problema real. Não resolver
  preventivamente.
- **Toca:** `src/html_parser/`, `tbox_context`.

### Um documento por janela

- **O que é:** `tbox_context` assume um documento, uma árvore de layout e uma
  display list. Tooltips, menus de contexto e diálogos modais poderiam pedir
  passadas de layout/render separadas (ou subsurfaces) compostas na mesma
  janela, para não forçar relayout do app inteiro e escapar do clipping do
  container. Os popups atuais dos controles (`tbox-popup`) são resolvidos
  dentro do mesmo contexto.
- **Por que importa:** mudaria a assinatura de `tbox_context` e
  `tbox_context_run_frame` desde a raiz.
- **Quando revisitar:** quando um desses casos entrar em escopo.

### Composição de texto (IME)

- **O que é:** o texto chega por `tbox_context_dispatch_text`, separado das
  teclas, mas o backend Wayland ainda não implementa o protocolo de
  composição (text-input/IME). Sequências XKB Compose funcionam.
- **Quando revisitar:** quando houver necessidade de idiomas com IME.
- **Toca:** `src/output/tbox_backend_wayland.c`, `tbox_window_backend.h`.

### Carregamento assíncrono de recursos

- **O que é:** fontes e imagens são carregadas de forma síncrona durante o
  frame que precisa delas. Não há gatilho de redesenho por recurso que
  terminou de carregar.
- **Quando revisitar:** quando houver recursos remotos ou grandes.

### ABI das structs públicas

- **O que é:** structs de opções (`tbox_context_options`) já entram por
  ponteiro, mas não têm campo de tamanho/versão; `tbox_context_options_default()`
  e `tbox_ua_style_config_default()` retornam por valor. Um binding
  compilado contra uma versão continua quebrando se a struct crescer em
  outra.
- **Quando revisitar:** antes da primeira versão estável (até lá a API muda
  livremente). Caminho provável: `size_t struct_size` no início, ou funções
  `_init(ptr)` no lugar dos `_default()`.
- **Toca:** `include/tbox/context.h`, `include/tbox/app.h`.

## Build e empacotamento

### Consumo por outro projeto

- **O que é:** não há regras de `install`, arquivo `pkg-config` nem export
  de alvos CMake. `src/CMakeLists.txt` usa `${CMAKE_SOURCE_DIR}` nos
  `target_include_directories`, o que só é correto quando o tbox é o projeto
  raiz — com `add_subdirectory` a partir de outro projeto, os caminhos
  apontam para o lugar errado (`${PROJECT_SOURCE_DIR}` ou
  `${CMAKE_CURRENT_SOURCE_DIR}/..` resolveriam). A fonte embutida também
  precisaria ser instalada junto.
- **Quando revisitar:** item 4 do roadmap (alvos de distribuição), ou antes,
  se um projeto externo quiser usar o tbox.
- **Toca:** `CMakeLists.txt`, `src/CMakeLists.txt`.

## Thread-safety

- **O que é:** a lib usa afinidade de thread: só `tbox_app_post` (fila por
  instância, com mutex) pode ser chamada de outra thread. Estado
  compartilhado conhecido no resto: arenas, os contextos globais de `:hover`/`:focus`/
  `:active` em `css_selector.c` (`tbox_css_selector_set_*_context`), caches de
  fonte e imagem.
- **Por que importa:** decisão transversal; a estratégia (lock por estrutura,
  lock global, "cada contexto só na thread que o criou") depende do caso de
  uso.
- **Quando revisitar:** quando threading virar requisito real (documentos em
  paralelo, motor de script em outra thread). Nesse momento, mapear cada
  estado mutável compartilhado e decidir caso a caso.
- **Regra até lá:** ver [Convenções](convencoes.md#estado-global-e-threads) —
  nenhum global/estático mutável novo sem discussão.

### Limites das at-rules de cascata

`@scope` não aceita `&` nem seletores relativos (`> p`; use `:scope > p`).
`@property` não tem gramáticas compostas (`<length>+`, `|`), `currentColor`
nem valor inicial relativo, e as propriedades registradas não fazem
transição. `@counter-style`: até 64 símbolos, 16 intervalos de `range` e
4096 repetições de `pad`.

### Limites das container queries

Uma iteração por frame: contêineres aninhados cujo tamanho depende de uma
consulta a um contêiner externo usam o tamanho da primeira passada. O
tamanho medido ignora transições em curso no próprio contêiner. Com
contêiner presente, a árvore inteira é re-resolvida e o layout é feito
duas vezes. `@keyframes`/`@font-face`/`@font-palette-values` dentro de
`@container` são ignorados; escrita só horizontal; nomes até 64 bytes.
Unidades `cq*` em listas de trilhas do grid e no valor inicial de
`@property` caem nas `sv*`.

## Layout

### Limites de `border-image`

Sem `-webkit-border-image`; não é interpolável (transições trocam de uma
vez); `border: inherit`/`initial` não zera o `border-image` (o shorthand
`border` com valor comum zera); ladrilhos de `url()` alinhados a pixel
inteiro podem ficar 1px fora do navegador; um trecho com mais de 4096
ladrilhos não é desenhado; `border-radius` é ignorado sob a imagem.

### Recursos OpenType

Sem glifos coloridos (COLR/CPAL), `font-palette` e `@font-palette-values`
não mudam o desenho; sem `@font-feature-values`, as funções de
`font-variant-alternates` (`stylistic()`, `swash()`…) não têm efeito.
Limites: 64 tags distintas em `font-feature-settings` e nomes de paleta de
até 63 bytes. Sem HarfBuzz nada disso tem efeito.

### Limites das transformações

Controles com geometria interna (cursor de texto, slider, barra de rolagem,
popups) não levam o ponteiro pela inversa da matriz; `backdrop-filter`
dentro de uma caixa transformada lê só a própria camada; listas de
`transform` com tipos diferentes trocam de uma vez (sem decomposição de
matriz); até 8 funções; a camada transformada é re-rasterizada a cada frame
em que suja; `rotate`/`translate` zero contam como `none` (sem contexto de
empilhamento).

### Limites do Grid

Contribuições de itens com span sobre trilhas flexíveis usam partes iguais,
sem todas as redistribuições do §11; até 128 trilhas por eixo, 256 nomes de
linha e 64 áreas; sem escapes CSS em nomes; `last baseline` vira a
primeira; sem `safe`/`unsafe`, `legacy`, subgrid nem fragmentação; itens
absolutos só usam áreas que cabem nas trilhas existentes; listas de trilhas
não interpolam.

### Blur de `box-shadow` e fundo do canvas

O blur de `box-shadow` sai mais largo e em faixas que no Firefox (visto ao
testar a T02, com ou sem transformação). O fundo do `body` não se propaga
para o canvas como no CSS (fixture 123).

### Limites dos floats

Floats são pintados depois do texto dos blocos em fluxo (aparece quando
o texto cruza a área pintada fora da forma); a faixa de cada linha usa a
`line-height` do bloco, não a altura final da linha; absolutos dentro de
um float inline resolvem contra uma origem provisória; um BFC desvia de
floats pela margin box (a CSS pede a border box); `shape-margin` é
aproximado em `polygon()` e imagens; a geometria das formas está duplicada
entre render e layout; `text-wrap: balance` é desligado quando há floats;
formas não animam.

### Limites de `zoom` e `resize`

`zoom: reset` não existe e `zoom: inherit` vale 1; `zoom` não anima; partes
embutidas dos controles e barras de rolagem não ampliam; `list-style-image`
não escala. O tamanho arrastado com `resize` não tem API para ser zerado e
só vale enquanto o eixo continuar permitido; `block`/`inline` supõem escrita
horizontal.

### Limites da multicoluna

`column-span` só em filhos diretos; sem `orphans`/`widows`; `max-height`
com altura `auto` é aplicado depois de fragmentar; flex, tabelas, imagens
e multicolunas aninhadas são atômicos na fragmentação; até 4096 colunas.

### Máscara de propriedades animáveis

`tbox_style_animatable` usa uma máscara de 64 bits e está cheia (64
entradas, 8 delas as posições de `transform`); `paint-order` ficou de fora
por isso e não anima em `@keyframes` (troca na hora). A próxima propriedade
animável exige trocar a máscara pública por um conjunto maior.

### Limites do motion path

Sem `url()` em `offset-path` (não há SVG); `offset-path` não interpola;
expressões percentuais com limites em `offset-distance`/`-position`/
`-anchor` interpolam de forma discreta; dados de `path()` até 4095 bytes,
polígonos até 32 vértices, amostragem até 16384 segmentos.

### `<br>` sozinho entre blocos

Um `<br>` entre dois blocos não vira uma linha anônima (o navegador dá uma
linha em branco); visto ao testar a multicoluna.

### Limites do texto bidirecional

No `<input>`, seleção, clique e setas seguem a ordem lógica (só o caret é
visual); `<textarea>` e o rótulo do `<select>` não são reordenados; as
reticências de `text-overflow` supõem LTR; com `unicode-bidi: plaintext`,
`text-align: start` segue o `direction` do bloco; mais de 6 marcas
aninhadas por palavra são ignoradas; o marcador de lista em `rtl` continua
à esquerda.

### Contorno em fontes bitmap

`-webkit-text-stroke` exige glifos vetoriais; fontes só bitmap ficam sem
contorno (falta um fallback por dilatação da máscara).

### Limites de máscara e `box-decoration-break`

Até quatro camadas de `mask`; `match-source` é alfa (sem SVG); `no-clip`
fica limitado ao viewport. `box-decoration-break` não dá continuidade entre
inlines aninhados de estilos diferentes, e o fundo de `slice` não é
contínuo entre fragmentos. O fragmento inline usa a altura da linha, não a
da área de conteúdo da fonte como no navegador (pré-existente).

### Limites de `::first-letter` e `initial-letter`

`::first-letter` não se propaga por blocos descendentes (aplique ao bloco
que contém o texto), não trata grafemas/dígrafos completos, e `::first-line`
continua sem estilo; `initial-letter` usa estilo uniforme no trecho e a
altura sujeita ao arredondamento da fonte; `hanging-punctuation` só em
escrita horizontal LTR.

### Espaço em branco entre `inline-block`

O tbox não dá largura ao espaço em branco entre caixas `inline-block`
(o navegador dá um espaço da fonte): duas caixas que somadas cabem
exatamente na linha ficam lado a lado no tbox e quebram no navegador.
Visto nos fixtures 148 (1px) e 154/155 quando reformatados.

### Limites da escrita vertical

Imagens, fundos com imagem ou gradiente, `border-image` e sombras dentro do
referencial giram junto com ele; `absolute`/`fixed` numa subárvore vertical
usam a caixa que inicia o modo como referência, e o ancorado pelo fim do
bloco cai na simplificação de altura `auto`; a medição intrínseca de uma
caixa ortogonal não conhece a altura da janela; sem `vmtx`/`VORG` nem as
features `vert`/`vrt2`; `text-combine-upright: digits` não existe;
`overflow` e rolagem não trocam de eixo; tabelas e controles só giram; a
tabela sobreposta copia os estilos da subárvore a cada frame.

### Margens negativas no collapsing entre irmãos

- **O que é:** o collapsing usa `max` só quando as duas margens são ≥ 0; com
  qualquer lado negativo o par soma, em vez do algoritmo do CSS 2.1 §8.3.1.
- **Quando revisitar:** quando um caso real mostrar o gap errado.
- **Toca:** `src/layout/tbox_layout_flow.c`.

### Posição estática de `absolute`/`fixed`

- **O que é:** com os dois lados de um eixo em `auto` (ou no caso circular
  `top: auto` + `bottom` + `height: auto`), a caixa vai para a origem do
  containing block, não para "onde estaria se fosse `static`".
- **Toca:** `tbox_layout_resolve_absolute_edge` em `tbox_layout_geometry.c`.

### Algoritmo CSS 2.1 §10.3.7 completo

- **O que é:** `left`+`right` esticam a caixa, e sem um dos dois a largura
  `auto` é shrink-to-fit, limitada ao espaço depois do deslocamento. O resto
  do sistema de equações não está implementado: margens `auto` centrando a
  caixa e `right` contra a posição estática.
- **Quando revisitar:** um diálogo centrado por `margin: auto` com
  `left: 0; right: 0`.

### Bloco dentro de um elemento inline

- **O que é:** um `display: block` dentro de um elemento inline
  (`<p>x <span><div>y</div></span> z</p>`) é achatado em linha, em vez de
  dividir o inline em volta (CSS 2.1 §9.2.1.1). Um bloco filho direto de
  `<p>`/`<li>`/`<td>`… já funciona; `<button>` e `<pre>` ainda ignoram
  filhos de bloco.
- **Toca:** `tbox_layout_collect_words_in` (`inside_inline`) e
  `tbox_layout_inline_run_end`.

### Camadas entre folhas diferentes

- **O que é:** a ordem das camadas (`@layer`) é calculada por folha. O CSS
  passado pela aplicação e o CSS do documento (`<style>`/`<link>`, que são
  concatenados) não compartilham nomes: `@layer base` em cada um são duas
  camadas, comparadas pela posição numérica dentro de cada folha.
- **Quando revisitar:** quando a aplicação e o documento precisarem
  combinar camadas de mesmo nome.
- **Toca:** `tbox_css_parser_run` (ranqueamento) e a montagem das folhas
  em `tbox_context_open_with_options`.

### Estilo das caixas anônimas copiado campo a campo

- **O que é:** `tbox_layout_build_anonymous_box` monta o estilo do texto
  solto de um bloco copiando as propriedades herdáveis uma a uma. Um campo
  herdável novo que não entre nessa lista some do texto anônimo, embora
  funcione em `<span>`. Foi o que aconteceu com `text-emphasis`.
- **Quando revisitar:** na próxima propriedade herdável. O natural é partir
  de uma cópia do estilo do contêiner e zerar só os campos não herdáveis.
- **Toca:** `src/layout/tbox_layout_flow.c`.

### Fallback de fonte por glifo

- **O que é:** a lista de `font-family` escolhe uma face por run. Um glifo
  que falta nela não é procurado no próximo nome nem no Fontconfig.
- **Quando revisitar:** texto misturando escritas (latim com CJK ou emoji)
  numa família que só cobre uma delas.
- **Toca:** `tbox_font_face_cache_get_shaped` e o shaping por run.

### Layout automático de tabela aproximado

- **O que é:** as colunas usam min/max-content e `%`/px na ordem dos
  navegadores, mas não reproduzem cada regra de distribuição. Células que
  abrangem colunas dividem o excesso igualmente. `min-width` em célula é
  respeitado, enquanto o Firefox o ignora. Tabelas sem espaçamento nem
  outros recursos ainda passam pelo caminho simples
  (`tbox_layout_table.c`), sem shrink-to-fit.
- **Quando revisitar:** uma tabela real com larguras visivelmente diferentes
  das do navegador, ou para unificar os dois caminhos.
- **Toca:** `src/layout/tbox_layout_table_grid.c`, `tbox_layout_table.c`.

### Padrão de `dashed` medido no Firefox

- **O que é:** os segmentos dos lados tracejados reproduzem medidas do
  Firefox, mas não o algoritmo dele. Com cantos, o número de segmentos é o
  par mais próximo de (trecho + largura) / (3 × largura). Sem cantos, é o
  ímpar mínimo com segmento de até 3× a largura. Lados vizinhos com estilos
  diferentes, e o encontro de tracejado com raio pequeno, podem divergir.
- **Toca:** `tbox_render_push_box_side`, `tbox_render_push_border_side`.

### Quebra de linha do `<textarea>`

- **O que é:** o `<textarea>` quebra uma palavra longa em qualquer
  caractere. Não usa as oportunidades depois de `-` e de `/` que o texto
  comum ganhou.
- **Toca:** `src/layout/tbox_layout_textarea.c`,
  `src/context/tbox_context_textarea.c`.

### Containing block de ancestral posicionado com `height: auto`

- **O que é:** o `padding_box` passado como containing block aos descendentes
  `absolute`/`fixed` é calculado antes dos filhos de fluxo, quando a altura
  final ainda não é conhecida. Um descendente ancorado por `bottom` vê um
  containing block mais baixo que o real.
- **Por que importa:** caso comum (card de altura variável com badge no canto
  inferior).
- **Quando revisitar:** exigiria uma segunda passada (fluxo primeiro, fora de
  fluxo depois).
- **Toca:** `tbox_layout_build_element` (`context_for_children.nearest_ancestor`).

### Tabela de propriedades do `@supports`

- **O que é:** `tbox_css_supports.c` tem a própria lista de propriedades e funcões 
  suportadas, uma cópia da [referência](../referencia/css-suportado.md); a
  Style não tem um registro que o parser (camada de baixo) possa consultar.
  Uma propriedade nova que não entrar nessa lista responde falso.
- **Quando revisitar:** se as duas listas divergirem na prática, ou quando
  a Style ganhar um registro de propriedades.
- **Toca:** `src/css_parser/tbox_css_supports.c`.

### Keyframes resolvidos uma vez

- **O que é:** os valores de cada keyframe são resolvidos quando a animação
  começa; uma mudança depois disso em `font-size` (para `em`), no containing
  block (para `%`) ou em `color` (para `currentColor`) não chega à animação
  em curso.
- **Quando revisitar:** quando uma animação longa precisar acompanhar o
  layout (ex.: redimensionar a janela no meio).
- **Toca:** `tbox_animation_resolve_frames` em
  `src/context/tbox_context_animation.c`.

### Camadas refeitas a cada repintura

- **O que é:** uma camada de composição não é guardada entre frames: cada
  região suja que a toca refaz o buffer e os efeitos. Um blur grande custa
  proporcional à área (~0,1 s em tela cheia num build Release), então uma
  animação dentro de um elemento com blur grande, ou um `backdrop-filter`
  com blur sobre conteúdo que muda, pesa em todo frame.
- **Quando revisitar:** quando medido numa UI real; o caminho é guardar o
  buffer da camada e só refazê-lo quando um op de dentro mudar.
- **Toca:** `tbox_raster_paint_layer`, `tbox_raster_blur` em
  `src/output/tbox_raster.c`.

### Gradientes radiais degenerados

- **O que é:** raios nulos são substituídos por epsilon no raster. Elipses
  com um eixo nulo e a cor média de gradientes repetidos degenerados ainda
  não implementam as regras específicas de CSS Images.
- **Por que importa:** raios explícitos zero ou cálculos que chegam a zero
  tornam esses casos acessíveis diretamente pelo CSS.
- **Quando revisitar:** ao precisar de fidelidade nesses limites, junto dos
  casos de período nulo dos gradientes repetidos.
- **Toca:** `src/output/tbox_raster.c` (`tbox_raster_gradient`).

### `mask` com uma camada

- **O que é:** só a primeira camada de `mask`/`mask-image` vale, com
  `mask-origin`/`mask-clip` `border-box` e sem `mask-composite`;
  `mask-mode: match-source` é alfa (não há SVG). `box-shadow`/`text-shadow`
  com blur continuam em degraus, sem usar o blur gaussiano das camadas.
- **Quando revisitar:** quando um caso real pedir máscaras compostas, ou
  sombras mais fiéis.
- **Toca:** `tbox_style_resolve_mask`, `tbox_render_push_box_shadow`.

## HTML Parser

### Fechamento implícito de `<tr>`/`<td>`/`<th>`/`<option>`

- **O que é:** só `<p>` e `<li>` fecham implicitamente. Tabelas e `<select>`
  escritos à mão costumam omitir esses fechamentos.
- **Quando revisitar:** um caso real de tabela ou `<select>` com fechamento
  omitido. Mesmo mecanismo de truncar `open_elements` já usado para `<p>`.
- **Toca:** `tbox_html_tree_builder_handle_start_tag`.

### Shaping por palavra e a cada frame

- **O que é:** o layout molda cada palavra sozinha, e de novo a cada frame
  (não há cache de shaping). Sem contexto através de um espaço ou de uma
  fronteira de elemento (`<b>f</b>i` não forma ligadura, kerning entre
  palavras não existe).
- **Por que importa:** com HarfBuzz o shaping é a parte cara da medição;
  documentos com muito texto pagam isso em todo frame.
- **Quando revisitar:** quando um perfil mostrar o shaping no topo, ou com
  o layout incremental (ver "Estilo e layout incrementais"). Um cache por
  (face, texto, letter-spacing) na face resolveria o custo.
- **Toca:** `src/font/tbox_font_face.c`, `src/font/tbox_font_shaper_harfbuzz.c`,
  `src/layout/tbox_layout_inline.c`.

### Segmentação por grafema

- **O que é:** edição de texto, `break-all`/`overflow-wrap`, reticências e
  máscara de senha trabalham por codepoint. Grafemas de vários codepoints
  (acento combinante, emoji com modificador ou ZWJ, bandeiras) podem ser
  partidos.
- **Por que importa:** backspace num "é" decomposto apaga só o acento; o
  cursor para no meio de 👨‍👩‍👧.
- **Quando revisitar:** quando texto de usuário com emoji ou NFD for caso
  real. Exige tabelas UAX #29 (libgrapheme ou utf8proc como dependência
  opcional, ou tabela própria gerada).
- **Toca:** `src/base/tbox_string.c` (um `tbox_grapheme_next/previous`),
  `src/font/tbox_font_measure.h` (o passo do medidor),
  `src/context/tbox_context_text.c`, `src/context/tbox_context_events.c`,
  `src/layout/tbox_layout_inline.c`, `src/layout/tbox_layout_sizing.c`.

### Strings de tamanho fixo em `tbox_style`

- **O que é:** `text-overflow`, `hyphenate-character`, `text-emphasis`,
  `content` do marcador, `font-family`, `url()` e nome de animação vão para
  `char[N]` dentro de `tbox_style` (16 a 256 bytes). Um valor maior é
  cortado — sempre entre codepoints (`tbox_utf8_copy_truncated`), mas
  cortado.
- **Por que importa:** perde texto silenciosamente; o struct público fica
  grande e copiado por valor na herança.
- **Quando revisitar:** quando um valor real passar do limite, ou na próxima
  revisão do ABI de `tbox_style`. A troca natural é `tbox_string_view`
  apontando para a arena do frame/folha.
- **Toca:** `include/tbox/style.h`, `src/style/tbox_style.c`,
  `src/layout/tbox_layout_flow.c` (cópia para caixas anônimas).

### Entidades: windows-1252, maiúsculas alternativas, multi-codepoint

- **O que é:** referências numéricas em `0x80`–`0x9F` não são remapeadas para
  Windows-1252; variantes como `&AMP;` não são reconhecidas; as ~12
  entidades de 2 codepoints ficam como texto literal. O modo legado sem `;`
  também não existe (decisão, não débito).
- **Quando revisitar:** só se um caso real depender disso.
- **Toca:** `src/html_parser/tbox_html_entities.c`.

## Controles de formulário

### Pseudo-elementos com prefixo de navegador

- **O que é:** as partes dos controles só respondem aos nomes do rascunho
  W3C (`::slider-thumb`, `::step-up`…). CSS escrito para Firefox
  (`::-moz-range-thumb`) ou Chrome/Safari (`::-webkit-slider-thumb`) é
  ignorado. A equivalência está em
  [Formulários e controles](../10-formularios-e-controles.md#equivalentes-em-outros-navegadores).
- **Por que importa:** CSS existente não estiliza os controles do tbox sem
  ser reescrito; e a captura de um fixture no Firefox (referência do
  `tbox_cmp`) não mostra as partes com os nomes do rascunho.
- **Quando revisitar:** quando houver CSS real a portar. Seria uma tabela
  de apelidos na resolução das partes (`src/style/tbox_style_parts.c`),
  com cuidado onde não há correspondência 1:1 (o spin button único do
  Chrome; as três cores do meter).
- **Toca:** `src/style/tbox_style_parts.c`, `docs/10-formularios-e-controles.md`.

### Pseudo-classes nas demais partes

- **O que é:** só `::-tbox-scrollbar-thumb` aceita `:hover`/`:active`
  depois do pseudo-elemento. `::slider-thumb:hover`, `::step-up:active`
  etc. ainda não: use o estado do controle (`input:hover::slider-thumb`).
- **Quando revisitar:** quando um controle pedir estado por parte. O
  mecanismo existe (`state` na cascata e variantes da parte); falta saber
  que parte está sob o ponteiro em cada controle.
- **Toca:** `src/style/tbox_style_parts.c`, os pintores em `src/context/`.
