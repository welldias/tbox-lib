# Registro de decisões

> - **Escopo:** decisões de design já tomadas, com o motivo e as alternativas descartadas. Consolida as seções "Decisões já tomadas" de v0 a v20.
> - **Código relacionado:** todo o pipeline
> - **Veja também:** [Histórico](historico.md), [Débito técnico](debito-tecnico.md), [Convenções](convencoes.md)

Cada item: **decisão** — motivo (versão em que foi tomada). Itens marcados
*(superado)* foram substituídos depois; ficam aqui porque explicam o
formato atual do código. O texto integral original de cada versão está em
`git show fe51140:ARCHITECTURE.md`.

## Pipeline e camadas

- **`<ruby>` é uma unidade inline indivisível.** Cada trecho de texto-base
  seguido de `<rt>` forma uma célula: a largura é o maior dos dois textos,
  com a anotação centralizada acima. `<rp>` não ocupa espaço quando a
  anotação é desenhada. Esse modelo usa as caixas e text runs existentes,
  sem introduzir um novo modo geral de formatação CSS.
- **Recursos HTML usam caminhos locais.** `<link rel=stylesheet>` lê arquivos
  locais na abertura do contexto, na ordem do documento. O primeiro `<base
  href>` fornece um diretório para links e imagens; URLs de rede continuam
  fora do escopo da biblioteca. `<source>` em `<picture>` escolhe uma imagem
  antes do `<img>` fallback. Mapas de imagem resolvem `<area>` durante o
  clique, sem criar caixas visuais para as áreas.
- **O atributo `open` é a fonte do estado de `<details>`.** O layout consulta
  o DOM a cada frame e o contexto alterna o atributo; não há estado paralelo.
- **A legenda conserva espaço no fluxo e é deslocada sobre a borda.** A caixa
  do primeiro `<legend>` continua contando para a altura de `<fieldset>`;
  a pintura divide apenas o trecho superior da borda reta sob ela.

- **`<progress>` e `<meter>` compartilham o pintor de controles passivos.** A
  geometria vem do layout e os atributos são lidos a cada frame, como no
  `input type=range`; o texto filho fica no DOM para fallback sem ocupar a
  caixa visual. Não há estado global nem valor duplicado fora do DOM.

- **Style é uma camada própria entre Cascade e Layout.** A cascata resolve
  texto (`property: value` como `tbox_string_view`); o Layout precisa de
  valores tipados. Style tipa, herda, expande shorthand e aplica valores
  iniciais (v0).
- **Shorthand expande na Style, não no CSS Parser nem na Cascade.** O parser
  continua agnóstico de propriedade; só a Style sabe que `margin` é
  shorthand (v0).
- **Fonte/Texto é um serviço transversal, não um estágio do pipeline.**
  Layout usa para medir; Output para rasterizar; Render só carrega a
  referência da face adiante (v0).
- **Render Pipeline separado do Output Display.** A display list é o ponto
  de reuso entre backends (Wayland hoje, headless/PNG, outros SOs depois);
  layout e render não mudam por plataforma (v0).
- **Orchestration (`tbox_context`) é dona do cômputo, não do event loop.**
  O loop da plataforma e a apresentação ficam na Application; `tbox_context`
  não sabe que Wayland existe (v0, corrigido durante a implementação).
- **`tbox_style_table` é uma tabela plana com busca linear.** Escolhida entre
  callback pull-based, tabela plana e hash por ponteiro: reaproveita o
  padrão de `tbox_css_computed_style_find`, não exige hash table na Base e
  não acopla a ordem de travessia entre Style e Layout (v0).
- **Uma `frame_arena` por contexto, resetada a cada frame**, compartilhada
  por Style, Layout e Render (v0).
- **Recompute total por frame.** Sem dirty-tracking de estilo/layout; desde a
  v20, só a *repintura* é parcial (ver [Atualização da tela](../05-atualizacao-da-tela.md)).
- **`tbox_context` não guarda flag "sujo".** Funções de evento
  (`dispatch_click`, `update_hover`, `dispatch_key`, ...) retornam `bool`; quem
  decide recomputar é a Application (v1, v3).

## API pública e interatividade

- **`scroll-behavior: smooth` só anima rolagens feitas pela tbox** (foco,
  clique na trilha); roda e arrasto do polegar são manipulação direta e
  continuam imediatos, como esperado num app (2026-10-11).
- **Screenshot headless mostra a página assentada**: com `@starting-style`
  o primeiro frame inicia uma animação; capturar o estado inicial deixaria
  elementos invisíveis (2026-10-11).
- **`cursor: auto` mantém a seta sobre texto comum**, ao contrário dos
  navegadores: numa interface de aplicação o texto raramente é para
  selecionar, e o I-beam sugeriria o contrário. Campos editáveis e links
  seguem como nos navegadores; quem quiser o I-beam declara
  `cursor: text` (2026-10-11).

- **A árvore pública é o próprio `tbox_html_node` (Opção C).** Sem
  `tbox_widget` e sem sincronização entre duas árvores. Handlers e estado de
  interação ficam fora do nó, em tabelas do `tbox_context`. O custo de
  memória (arena só cresce) é aceito até ser medido (v1).
- **Delegação de eventos por seletor CSS, não handler por nó.** Reaproveita
  `tbox_css_selector_query_matches`, não deixa handler pendurado em nó
  removido e tem o formato que um futuro motor de script usaria
  (`addEventListener`) (v1).
- **Tabela de handlers mora no `tbox_context`, não no `tbox_app`** — o mesmo
  primitivo serve ao app C e a um futuro binding de script (v1).
- **Loop não-bloqueante: `tbox_app_create`/`_step`/`_should_close`/`_close`**
  no lugar de `tbox_app_open` bloqueante, para compor com a fila de tarefas de
  um motor de script. Breaking change aceito antes de haver consumidores (v1).
- **Bubbling real com `stopPropagation` pelo retorno `bool` do handler**
  (v3; *supera* o bubbling simplificado da v1).
- **Desregistro por handle `int` monotônico**, não por `(seletor, handler,
  userdata)`, para evitar ambiguidade com registros duplicados (v3).
- **`:hover` (e depois `:focus`/`:active`) via contexto global em
  `css_selector.c`**, sem mudar assinaturas de `css_selector.h`/
  `css_cascade.h`/`style.h` e sem sujar o DOM com estado de interação.
  Aceito porque a lib já não é thread-safe; **não** serve de precedente para
  outros globais (v3).
- **Screenshot headless como função pública da Application**
  (`tbox_app_screenshot_from_files`), não código só do demo; PNG escrito sem
  libpng/zlib (DEFLATE "stored"), CRC/Adler calculados bit a bit, sem tabela
  global (ferramentas pós-v7).

## CSS e estilo

- **Folha user-agent compilada na lib, configurável por struct**
  (`tbox_ua_style_config`), sem arquivo externo. Regras fixas são
  declarações tipadas em `src/context/tbox_ua_style.c` (v2).
- **Inicial de `display` era `block` no v0** por falta de UA stylesheet;
  voltou ao comportamento CSS quando a UA stylesheet chegou (v0 → v2).
- **Margens da UA em `px` fixo** aproximando os valores em `em` dos
  navegadores (v2).
- **`em` resolvido para `px` dentro da Style**, contra o `font-size` do
  próprio nó (exceto `font-size`, contra o pai); só `%` fica para o Layout,
  porque depende do containing block (v2, v8).
- **`style=""` resolvido dentro de `tbox_css_cascade_resolve`**, com uma
  origem nova `TBOX_CSS_ORIGIN_AUTHOR_INLINE` (aditiva no enum). Inline vira
  dois degraus na tabela de rank em vez de "especificidade infinita" (v9).
- **CSS externo e `<style>` interno são a mesma origem `AUTHOR`**; múltiplos
  `<style>` são concatenados; em empate, o interno vence (ordem no array de
  fontes). A busca por `<style>` parte da raiz real do documento (v9).
- **`style=""` parseado como `"* { ... }"` reaproveitando `tbox_css_parse`**,
  sem API nova de lista de declarações (v9).
- **`font-family`: só o primeiro nome da lista**, guardado num buffer fixo
  `char font_family[64]` em `tbox_style` (evita ponteiro pendurado no
  stylesheet sintético de `style=""`) (v12).
- **`text-decoration`/`vertical-align` não herdam**, como no CSS (v13).
- **Palavras-chave globais (`inherit`/`initial`/`unset`) e `var()`** são
  resolvidas por remoção das declarações antes da passada principal e cópia
  posterior dos campos (v18, v19).
- **`clip: rect()` reaproveita `clip-path: inset()`**: os lados direito e
  inferior viram `calc(100% - Npx)` em `tbox_style_length`, então render e
  hit-test não mudaram. Alternativa descartada: um campo `clip` próprio com
  seu caminho de recorte. `clip` fica fora da tabela de palavras-chave
  globais porque divide o campo com `clip-path` (`clip: initial` apagaria um
  `clip-path`) (2026-10-11).
- **`font-variant-position` sintetizado na Style**, não no Layout: a fonte
  encolhe só onde o valor muda (herdar não acumula) e o deslocamento reusa
  `vertical-align: sub/super`, que não acumula entre aninhados. Sem shaping
  não há como usar os glifos OpenType (2026-10-11).
- **`all` reaproveita a tabela de campos das palavras-chave globais**: cada
  propriedade da tabela recebe a palavra-chave, respeitando a prioridade de
  declarações específicas (`tbox_style_field_overridden`). O que não está na
  tabela não é afetado (2026-10-11).
- **Media features sem fonte real têm valores fixos** de um app desktop
  (tema claro, mouse, 1dppx) em vez de novos campos em
  `tbox_context_options`; uma opção como `prefers_reduced_motion` pode vir
  quando houver um caso de uso (2026-10-11).
- **`@layer` como um número por regra (`tbox_css_ruleset.layer`)**,
  calculado no fim do parse pela ordem pós-ordem da árvore de camadas, e
  comparado na cascata sem tabela extra. Consequência: a ordem vale por
  folha; como `<style>`/`<link>` já são concatenados numa folha, só o CSS
  da aplicação fica com camadas à parte (2026-10-11).
- **`@import` expandido no texto, antes do parse**, como o `<link>`: o
  parser e a cascata não mudaram e a ordem das regras sai certa. O custo:
  as regras importadas não sabem de que arquivo vieram (2026-10-11).
- **`light-dark()` substituído no texto, junto de `var()`**, em vez de no
  parser de cor: o parser de cor não conhece o elemento, e a substituição
  textual cobre toda propriedade que aceita cor (2026-10-11).
- **`hyphenate-character` num buffer fixo de 16 bytes, `""` = `auto`**,
  como `text_overflow_string`; o custo é tratar a string vazia explícita
  como `auto` (2026-10-11).
- **Propriedades animáveis como uma tabela única** (`tbox_style_animatable`,
  `tbox_style_animate.c`) lida/escrita como 4 doubles, usada por
  `transition` e por `animation`. A orquestração interpola sem conhecer o
  layout de `tbox_style`. Transições e animações rodam antes do layout, então
  `width`/`padding` animam refazendo o layout, que já é refeito a cada frame
  (2026-10-12).
- **Estado de transição: alvos sempre, trilhas sob demanda.** Cada elemento
  guarda o último alvo de toda propriedade animável (é o que detecta uma
  mudança que chega junto com o `transition`, como num `:hover`), mas as
  trilhas em andamento só são alocadas quando a primeira transição começa
  (2026-10-12).
- **`@supports` decidido no parse**, por uma tabela de propriedades no
  módulo do parser (`tbox_css_supports.c`), em vez de levar a condição até a
  Style: o parser fica abaixo da Style e o que a tbox suporta não muda em
  execução. O valor não é validado, exceto os poucos casos que enganariam a
  detecção (`display: grid`, rotação, `blur()`); as propriedades 3D aceitas
  sem efeito ficam de fora para não ativar caminhos 3D (2026-10-12).
- **Keyframes resolvidos pela Style normal**, montando uma cascata com as
  declarações do keyframe acima das normais (e abaixo das `!important`), uma
  vez quando a animação começa. Alternativa descartada: interpretar os
  valores do keyframe à parte, que duplicaria o parser de cada propriedade
  (2026-10-12).
- **`filter: drop-shadow()` sem buffer separado**: vira `box-shadow` em
  quem pinta fundo/borda e `text-shadow` nos demais. Uma sombra fiel à forma
  alfa precisaria pintar o elemento num buffer próprio, que o raster não tem
  (2026-10-12).
- **`direction` pelo `tbox_style_units`**: a flag `rtl` passa por todas as
  funções que já recebiam as unidades, e só os pontos de mapeamento lógico
  olham para ela. O atributo `dir` é lido na própria Style (como um
  presentational hint, abaixo de qualquer CSS), porque a folha UA tipada não
  tem seletores de atributo (2026-10-12).

## Texto e fontes

- **Fontconfig no Linux atrás de `tbox_font_source`**; backend embedded
  (Liberation Sans Regular) para testes determinísticos sem depender das
  fontes da máquina (v0).
- **Sem shaping (HarfBuzz)**: soma de advances via FreeType. HarfBuzz seria
  troca de implementação atrás da mesma assinatura (v0).
- **Cache de faces carregado sob demanda** por (família, peso, itálico,
  tamanho, …), vida própria, não resetado por frame (v2, v12, v13, v18, v19).
- **Resolução de família por callback (`tbox_font_resolver_fn`)**, para o
  cache não depender de fontconfig nem precisar de `#ifdef` parcial (v12).
- **Itálico real**: família vazia + itálico também passa pelo resolver (v13).
- **Kerning ligado por padrão**, usado igualmente na medição e no raster (v19).

## Layout

- **`::before`/`::after` como nós sintéticos encaixados só durante o
  layout**, em vez de ensinar cada caminho do layout (palavras, caixas
  anônimas, flex, medição) a visitar filhos virtuais. A Style cria os nós
  (ela já percorre a árvore em ordem de documento, o que contadores e
  `quotes` precisam) e `tbox_layout_build` os liga e desliga. O
  documento nunca fica alterado fora dessa chamada, e seletores
  (`:first-child`…) são avaliados antes, sem enxergá-los. Como os nós
  vivem um frame, o hit-test devolve a caixa do elemento dono
  (2026-10-11).
- **Text tag com filho de bloco vira container de bloco** (reaproveita as
  caixas anônimas de `tbox_layout_build_children`) em vez de ensinar o
  caminho de texto a abrir blocos no meio das linhas. A decisão olha o
  `display` dos filhos, não nomes de tag, e vale igual na medição
  intrínseca (2026-10-11).
- **Texto inline como runs na caixa de texto**, sem `tbox_layout_box` por
  elemento inline (v2). `tbox_layout_text_run.style` aponta para o estilo do
  trecho, dando cor, destaque, decoração e `vertical-align` por run (v13).
- **Caixas de bloco anônimas (`node == NULL`)** para conteúdo inline solto
  num container de bloco, com estilo sintetizado só dos campos herdáveis do
  pai (evita pintar fundo/borda duas vezes) e sem margem própria (v14).
- **`position: relative` não cria conceito novo de containing block** — é
  deslocamento de coordenada antes de recursar nos filhos (v4).
- **Margin collapsing só entre irmãos adjacentes**; com margem negativa o
  par soma (v4).
- **`absolute`/`fixed` continuam filhos do pai DOM na árvore de layout**,
  só com geometria e participação em fluxo diferentes; `fixed` reaproveita o
  código de `absolute` (v5).
- **`sticky` como sinônimo de `relative`** enquanto não havia rolagem (v5;
  *superado* na v19, que implementou sticky vertical).
- **Hit-test visita todos os filhos e fica com o último que casa** (o que
  está por cima) — corrige caixas fora de fluxo que escapam do pai (v5).
  Desde a v18 segue a mesma ordem de pintura (`tbox_render_paint_order`).
- **Marcador de lista como texto prefixado** (v8; *superado* na v18, que
  criou marcador `outside` pendurado).
- **`<br>`/`<pre>` compartilham o mecanismo de `hard_break`**; `text-align`
  como pós-processamento de posição das linhas (v11).
- **Infraestrutura de medição antes de posicionar** (tamanhos intrínsecos,
  `tbox_layout_build_element_sized`, `tbox_layout_translate`) para
  inline-block e flexbox (v15, v16).

## HTML

- **Fechamento implícito só de `<p>` e `<li>`** (com `<li>` fechando também
  um `<p>` aberto, como no HTML5) (v6).
- **Entidades exigem `;` final**; tabela nomeada gerada do `entities.json` do
  WHATWG, só entidades de 1 codepoint, mapeadas para codepoint (`int`) e
  codificadas por um único caminho (`tbox_string_builder_append_codepoint`)
  (v6, v7).

## Saída e repintura

- **Damage tracking por assinatura `{bounds, hash}` de cada op**, guardada em
  memória própria do tracker (a display list antiga morre com a
  `frame_arena`). Hash FNV-1a campo a campo; texto pelo conteúdo; fonte e
  imagem pelo ponteiro (vêm de caches de vida longa) (v20).
- **Bounds conservadores calculados no Output**, não no Render, porque o
  bound de texto usa métrica de fonte (v20).
- **`tbox_app_set_debug_damage` em vez de ler variável de ambiente** — a lib
  não lê o ambiente; os exemplos mapeiam `TBOX_DEBUG_DAMAGE=1` (v20).
- **`backdrop-filter` como um op que reprocessa os pixels já pintados**
  (`TBOX_PAINT_BACKDROP_FILTER`): num raster em software, aplicar a matriz
  de cor ao buffer é barato e compatível com a repintura parcial, que sempre
  repinta a região inteira em ordem (2026-10-12).
- **Marcas de `text-emphasis` como formas, não glifos**: não dependem de a
  fonte ter `●`/`◎`/`﹅` (não há fallback por glifo) e são alinhadas à
  grade de pixels, porque o raster testa cobertura no centro do pixel e uma
  marca de poucos pixels sairia torta. Só a `<string>` usa texto, numa face
  de meio tamanho que o Layout escolhe (2026-10-12).

## Ferramentas

- **`tbox_cmp` em C++/OpenCV, opcional, fora do `ctest`**: a API C do OpenCV
  não existe mais; o mantenedor quis manter o algoritmo existente. Só
  `core`+`imgproc`+`imgcodecs` são linkados. O veredito por asset não vira
  código de saída (v10).
