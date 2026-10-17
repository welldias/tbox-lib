# Registro de decisões

> - **Escopo:** decisões de design já tomadas, com o motivo e as alternativas descartadas. Consolida as seções "Decisões já tomadas" de v0 a v20.
> - **Código relacionado:** todo o pipeline
> - **Veja também:** [Histórico](historico.md), [Débito técnico](debito-tecnico.md), [Convenções](convencoes.md)

Cada item: **decisão** — motivo (versão em que foi tomada). Itens marcados
*(superado)* foram substituídos depois; ficam aqui porque explicam o
formato atual do código. O texto integral original de cada versão está em
`git show fe51140:ARCHITECTURE.md`.

## Gradientes cônicos e recortes poligonais

Cônicos reutilizam `tbox_style_gradient`, com tipo CONIC, `angle` como
rotação inicial e `center` como centro. Stops angulares são convertidos para
percentuais de uma volta no parser; o raster amostra o ângulo horário a partir
do topo. Fix-up e interpolação sRGB premultiplicada continuam compartilhados.

`polygon()` preserva até 32 pares length-percentage no estilo. Render resolve
os pontos uma vez na arena de frame e prende o polígono ao LAYER_BEGIN. O
recorte é aplicado na composição, depois de filtros, evitando perder formas
em descendentes e recortes aninhados. A representação pintada guarda pontos
locais e origem separada para rasterização em buffers deslocados. Raster e
hit-test compartilham um teste geométrico imutável; eventos verificam também
polígonos dos ancestrais. Damage inclui pontos, origem e regra no hash e usa
bounds conservadores do conteúdo. Novos campos públicos exigem recompilação.
Sem caixas de referência explícitas, arredondamento de polygon ou hints de
interpolação/seleção de espaço de cor em cônicos nesta rodada.

## Ângulos tipados para funções inversas

O avaliador privado distingue ângulos de números e comprimentos, armazenando
seu valor em graus. As funções inversas ficam em `tbox_style_functions.c`;
a leitura literal de unidades em `tbox_style_values.c` não chama o avaliador,
evita recursão circular e mantém o limite de profundidade no mesmo parser.
Consumidores angulares recebem as unidades reais do elemento, inclusive para
`atan2(1em, 32px)`. Não houve alteração adicional na API pública.
A decisão anterior de limitar trigonometria a ângulos literais fica superada;
álgebra angular reutiliza soma, escala e comparações com tipos compatíveis.
O catálogo de pendências classifica mudanças pelo efeito em todo o pipeline.

## Trigonometria e raios radiais explícitos

`sin()`/`cos()`/`tan()` aceitam expressões numéricas em radianos ou um ângulo
literal, reutilizando o parser de unidades angulares. Não introduzem álgebra
angular geral no avaliador. Raios explícitos ficam em `tbox_style_gradient.radius`;
`AUTO` mantém o dimensionamento automático por extensão. Style valida forma,
quantidade e tipo dos raios; o raster resolve percentuais elípticos por eixo.
O hash de repintura inclui os dois raios. A extensão da estrutura pública
exige recompilar os consumidores.

## Avaliador numérico para opacity e funções de sinal

A entrada comum do avaliador de funções separa a avaliação da conversão final
para comprimento ou número. `opacity` ativa um contexto interno em que
percentuais são números (100% = 1), exige resultado numérico finito e limita
a faixa apenas ao final. Isso permite reutilizar as funções existentes sem
ampliar estruturas públicas ou habilitar cálculos nas demais propriedades
numéricas. `abs()`/`sign()` e `hypot()` usam os mesmos comprimentos resolvidos
em pixels; percentuais pendentes continuam rejeitados. `pi`/`e` são
reconhecidos apenas como operandos de cálculos.

## Operações matemáticas por passos e validação de misturas

`hypot()` reutiliza a leitura compartilhada de argumentos numéricos (até oito)
e acumula a norma com `hypot` de C para evitar o overflow da soma de quadrados.
`round()`/`mod()`/`rem()` operam em escalares e comprimentos já convertidos para
pixels, sem criar expressões pendentes no layout. `round()` calcula os dois
múltiplos vizinhos com resto, preservando os empates para cima e evitando
uma divisão que poderia transbordar. Percentuais/limites pendentes continuam
fora desse caminho. `color-mix()` mantém aproximações para espaços conhecidos,
mas passa a rejeitar nomes desconhecidos e pesos fora da faixa antes da
normalização. Não há mudanças de estruturas públicas nessa rodada.

## Exponenciais CSS e color(srgb)

As exponenciais são operandos numéricos no avaliador de comprimentos existente;
não ampliam a tipagem para potências de unidades, ângulos ou propriedades
numéricas independentes. Valores não finitos são rejeitados em vez de
introduzir a semântica completa de NaN/infinito CSS. `color()` reutiliza as
conversões sRGB/RGBA existentes e fica restrito a `srgb`/`srgb-linear`.
`clip-path: rect()` compartilha o parser de cantos e a representação de
`inset()`, preservando o parser separado de `clip: rect()` legado.

## Representação de recortes e cálculos CSS

`xywh()` é convertido para os quatro insets existentes, sem criar um novo
primitivo de pintura. Essa conversão aceita expressões lineares `px + %`,
mas rejeita limites que ainda dependam do layout. Raios percentuais de
`round` ficam separados dos pixels até a pintura, que conhece o tamanho da
border box; a normalização elíptica usa um fator comum para os dois eixos.
Comparações de expressões com coeficientes compatíveis são reduzidas no
avaliador existente; combinações gerais continuam sem árvore de expressões.

## Organização das funções CSS

Os parsers de funções de valores ficam em `src/style/tbox_style_functions.c`,
com utilitários em `tbox_style_values.c` e contratos em `tbox_style_internal.h`.
A resolução de propriedades continua em `tbox_style.c`. Cores, substituições
e conteúdo gerado mantêm seus arquivos especializados, pois dependem de
contextos distintos (cascata, custom properties e DOM/contadores). Reunir
todas as chamadas CSS num dispatcher único aumentaria essas dependências.

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

- **Partes dos controles por pseudo-elementos no CSS do documento**, não
  no `control_css` dos popups: assim seguem o estado do controle (`:hover`,
  `:focus`, `[disabled]`), `var()` e `light-dark()`. Nomes do rascunho W3C
  *CSS Form Control Styling*, sem prefixo, para a migração ficar natural se
  virarem padrão (escolha do mantenedor, 2026-10-16).
- **Estilo de parte compacto (`tbox_style_part_style`)** em vez de um
  `tbox_style` inteiro (16,7 KB) por parte e por frame: como no `::marker`,
  só os campos que as partes usam são copiados. Cada propriedade nova
  numa parte pede um campo. Só os elementos que pintam a parte a recebem
  (2026-10-16).
- **Scrollbar discreta por padrão** (`thin`, trilho transparente, pílula
  com recuo de 1px), escolha do mantenedor. O recuo é margem transparente,
  não borda branca: funciona em qualquer fundo e não se soma à borda de um
  polegar personalizado. `thin` vem da UA (`* { scrollbar-width: thin }`),
  então `scrollbar-width: auto` volta aos 10px; o trilho transparente é a
  reserva do pintor, não uma declaração, para não anular `scrollbar-color`
  (2026-10-16).
- **Partes da scrollbar com prefixo `-tbox-`.** O CSS padrão só tem
  `scrollbar-width`/`scrollbar-color` e não há rascunho para as partes. Entre
  estender `scrollbar-width` com tamanho (só largura), usar os nomes
  `::-webkit-scrollbar*` (prefixo de navegador, fora da regra de nomes) ou
  um tema no `control_css` (global, e será revisto), o mantenedor escolheu
  partes próprias no CSS do documento, no mesmo mecanismo das outras
  partes (2026-10-16).
- **Estado de parte como parâmetro da cascata**, não como estado global:
  `:hover`/`:active` depois do pseudo-elemento são avaliados pelo `state`
  passado na resolução, e o polegar é resolvido uma vez por variante
  (2026-10-16).
- **Lista do select pelo CSS padrão, sem opt-in.** As linhas são os
  `<option>`/`<optgroup>` do documento e a caixa é `::picker(select)`, como
  no select customizável; `appearance: base-select` não é exigido, porque
  o tbox não tem lista nativa a preservar (2026-10-16).
- **`option:hover` reaproveita o contexto de hover**: com a lista aberta,
  ele aponta para a opção destacada. Nenhum estado global novo; o `:hover`
  dos ancestrais acompanha, como num navegador com o ponteiro sobre a
  lista (2026-10-16).
- **`option:checked` por um campo no nó, não por estado global.**
  `tbox_html_node.selectedness` guarda a escolha atual, separada do
  atributo `selected` (escolha inicial, usada pelo reset), como a
  *selectedness* do HTML. O contexto o sincroniza antes de resolver
  estilos; o seletor só lê o nó (escolha do mantenedor entre isso, um
  estado global no seletor ou adiar, 2026-10-16).
- **Padrões das partes na folha UA, cor de destaque no pintor.** Tamanhos,
  fundos e bordas padrão são regras UA, para o autor substituí-los como
  qualquer CSS (`border: 0` funciona). O tbox não tem a cor de sistema
  `AccentColor`, então "use `accent-color`" é o pintor aplicando-a quando a
  parte não declara cor; a resolução registra a declaração
  (`background_declared`), e por isso `transparent` explícito vale
  (2026-10-16).
- **Foco por `outline`, não por borda**, como nos navegadores: o anel não
  ocupa espaço, então o foco não refaz o layout. O `outline-offset: -1px`
  mantém o desenho anterior (2px sobre a borda), passando só 1px para fora
  da caixa. A cor é o cinza da borda padrão (`#808080`), fixo: não segue
  um `border-color` do documento, só `outline-color`. Vale para todo
  controle focado, inclusive o slider: Tab precisa sempre mostrar o foco
  (escolha do mantenedor, 2026-10-16).
- **`outline` sólido acompanha `border-radius`**, como no Firefox e no
  Chrome atuais (o CSS 2 desenhava reto). Cantos elípticos e estilos não
  sólidos seguem retos (2026-10-16).
- **A marca do checkbox não define a linha de base**: ela é decoração, e
  usá-la faria o controle mudar de altura ao ser marcado (2026-10-16).
- **Campo de texto vazio alinha pela linha que o texto ocuparia**, não pela
  borda de baixo: a linha de base de um controle não pode depender de ele
  ter conteúdo, ou editar o valor desloca a linha (2026-10-16).
- **O polegar do slider pode sair da caixa do input**, como nos navegadores;
  só ancestrais com rolagem o recortam. A largura do polegar define o
  percurso, para o clique cair sob o centro dele (2026-10-16).
- **`accent-color` vale para range e progress, não para meter.** Como nos
  navegadores, o destaque pinta o valor do range e a barra do progress. O
  verde/amarelo/vermelho do meter comunica `low`/`high`/`optimum` e não
  segue o destaque. Com `auto`, range e progress mantêm o azul embutido em
  vez de cair em `color` como o checkbox: um preenchimento da cor do texto
  ficaria pesado demais (2026-10-16).

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
- **`filter: drop-shadow()` sem buffer separado** (substituída no mesmo
  dia pelas camadas de composição, abaixo): virava `box-shadow` em quem
  pinta fundo/borda e `text-shadow` nos demais (2026-10-12).
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
  troca de implementação atrás da mesma assinatura (v0). *(superado: ver
  "HarfBuzz como shaper opcional" abaixo)*
- **Cache de faces carregado sob demanda** por (família, peso, itálico,
  tamanho, …), vida própria, não resetado por frame (v2, v12, v13, v18, v19).
- **Resolução de família por callback (`tbox_font_resolver_fn`)**, para o
  cache não depender de fontconfig nem precisar de `#ifdef` parcial (v12).
- **Itálico real**: família vazia + itálico também passa pelo resolver (v13).
- **Kerning ligado por padrão**, usado igualmente na medição e no raster (v19).
- **UTF-8 saneado na entrada, não verificado em cada consumidor**:
  `tbox_html_parse`, `tbox_css_parse`, setters do DOM e paste trocam cada
  subparte máxima malformada por U+FFFD (mesma regra do WHATWG Encoding).
  Alternativas descartadas: rejeitar o documento inteiro (contraria "parsing
  nunca falha") e validar em cada laço (repete a regra em dezenas de
  lugares). `tbox_context_dispatch_text` mantém o contrato de rejeitar
  texto inválido: quem digita é o backend, que já entrega UTF-8; o paste,
  vindo de outro programa, é saneado antes em `tbox_app`. Um só
  decodificador (`src/base/tbox_string`) em vez de `utf8codepoint` do
  utf8.h, que não recebe o fim do buffer. O utf8.h fica confinado a
  `src/base/tbox_string.c` (caixa e codificação), imposto pelo CMake, para
  que trocá-lo no futuro (por exemplo por utf8proc, para grafemas) toque um
  só arquivo (após v20).
- **Números convertidos por código próprio, não por `strtod_l`**:
  `strtod_l`/`uselocale` exigem um `locale_t` "C" criado uma vez e guardado
  (estado global, ver Convenções) ou criado a cada número (caro). O scanner
  de `tbox_number` segue a gramática de `<number>` do CSS e converte com o
  caminho rápido exato (mantissa ≤ 2^53 e expoente decimal em ±22, uma
  operação IEEE); fora disso usa `pow`, com erro de poucos ulps — aceitável
  para valores de UI. A escrita usa `snprintf("%.*g")` e troca o separador
  decimal do locale por `.` (após v20).
- **Medidor incremental em vez de vetor de larguras por prefixo**: o
  medidor (`tbox_text_measurer`) não aloca, cabe em quem para cedo
  (quebra, reticências) e é a própria base de
  `tbox_font_measure_text_spaced`, o que garante resultado idêntico. Um
  vetor de prefixos só compensaria com buscas repetidas no mesmo texto
  (após v20).
- **Glifos na operação de pintura, posicionados pelo layout**: o
  `TEXT_RUN` carrega o vetor de `tbox_glyph`, e o raster só pinta.
  Alternativa descartada: glifos só no run do layout, com o raster lendo
  texto — manteria as duas implementações de posicionamento (medição e
  raster) que o item quis eliminar, e o `tbox_cmp`/headless já consomem a
  display list. Um op sem glifos ainda é moldado pelo raster, mas pelo
  mesmo shaper, para não quebrar quem monta display lists à mão. Cada
  palavra é moldada isoladamente, na posição que a linha lhe deu; o espaço
  entre palavras de um run vira um glifo de espaço no início do vão (após
  v20).
- **HarfBuzz como shaper opcional, em arquivo próprio**: pela regra de
  dependências, `tbox_font_shaper_harfbuzz.c` ou `tbox_font_shaper_simple.c`
  é compilado, nunca os dois, atrás de `src/font/tbox_font_face_internal.h`.
  Sem `hb-ft`: o processo tem o FreeType estático do build e o do sistema
  (de que o HarfBuzz depende), e o `hb-ft` chamaria o do sistema com um
  `FT_Face` do nosso; o `hb_face` lê os bytes que a face já guarda e um
  sub-font troca só o avanço pelo do nosso FreeType (com hinting, o mesmo dos
  bitmaps). `hb_font`/`hb_buffer` ficam na face, sem estado global.
  `letter-spacing` ≠ 0 desliga `liga`/`clig`/`dlig` e entra uma vez por
  cluster; `font-kerning: none` desliga `kern`. O medidor molda o trecho
  inteiro e reparte a largura de um cluster entre seus codepoints, como os
  navegadores fazem com ligaduras (após v20).
- **Edição de texto fica no medidor, não nos glifos pintados**: cursor,
  seleção e clique em `<input>`/`<textarea>` trabalham sobre o valor do
  campo, que não é o texto pintado (máscara de senha, linhas do textarea).
  O medidor itera a saída do shaper (com ou sem HarfBuzz), e a edição o
  acompanha (após v20).
- **Strings de `tbox_style` continuam `char[N]`**: corte seguro por
  codepoint agora; trocar por views mexe no ABI público e na vida das
  strings herdadas, e fica para quando houver caso real (ver Débito
  técnico).

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
- **Camadas de composição como marcadores na display list**
  (`LAYER_BEGIN`/`LAYER_MASK`/`LAYER_END`) em vez de uma árvore de camadas:
  a display list continua plana, o damage tracker continua comparando op a
  op, e o Output pinta a camada num buffer só do tamanho do conteúdo, com um
  deslocamento inteiro (cada op sai igual ao da tela). O buffer é
  pré-multiplicado no mesmo `uint32_t` da tela; como a tela é sempre opaca,
  a mistura por canal não mudou e nenhum fixture antigo mudou de pixel, a
  não ser os que dependiam de `opacity` op a op (2026-10-12).
- **Quem ganha camada é decidido pela Style** (`tbox_style_needs_layer`):
  `opacity` < 1, filtro que espalha, máscara, `mix-blend-mode`,
  `isolation`. Um `filter` só de cor fica op a op (sem buffer). `opacity`
  sempre vira camada: é o comportamento certo do CSS para filhos
  sobrepostos, e o custo é um buffer do tamanho do elemento (2026-10-12).
- **Repintura parcial sem cache de camadas**: uma camada é refeita, a cada
  região suja, sobre a região inflada pelo alcance dos efeitos. Guardar o
  buffer entre frames economizaria o blur, mas pediria invalidação por
  camada; fica para quando o custo for medido como problema (2026-10-12).
- **`backdrop-filter` como um op que reprocessa os pixels já pintados**
  (`TBOX_PAINT_BACKDROP_FILTER`): num raster em software, aplicar a matriz
  de cor ao buffer é barato e compatível com a repintura parcial, que sempre
  repinta a região inteira em ordem (2026-10-12).
- **Marcas de `text-emphasis` como formas, não glifos**: não dependem de a
  fonte ter `●`/`◎`/`﹅` (não há fallback por glifo) e são alinhadas à
  grade de pixels, porque o raster testa cobertura no centro do pixel e uma
  marca de poucos pixels sairia torta. Só a `<string>` usa texto, numa face
  de meio tamanho que o Layout escolhe (2026-10-12).

## Loop da aplicação e threads

- **Afinidade de thread + uma única porta thread-safe** (`tbox_app_post`).
  Toda a API roda na thread de UI (no macOS será obrigatoriamente a
  principal); só `tbox_app_post` pode ser chamada de qualquer thread. A
  fila é por instância de `tbox_app`, protegida por um mutex próprio — não
  é estado global e não muda a regra de nenhuma outra estrutura.
  Descartadas: contexto inteiro thread-safe (custo e complexidade sem caso
  de uso) e expor um fd para loops externos (não existe no Windows nem no
  macOS, cujos loops são de mensagens/run loop) (2026-10-16).
- **Só `post`, sem `wake` separado**: acordar sem trabalho é
  `tbox_app_post` com uma função vazia; uma função a menos na superfície
  thread-safe. `wake` pode entrar depois sem quebrar nada (2026-10-16).
- **O prazo interno não é público na aplicação.** `tbox_app_wait(app,
  max_ms)` calcula sozinho quanto pode dormir (animações, atrasos, snap,
  repetição de tecla, frame do compositor); quem tem loop próprio só passa
  o próprio limite. O contexto expõe `tbox_context_next_frame_time` para
  quem usa `tbox_context` sem `tbox_app`. Tempo absoluto em segundos no
  relógio monotônico (o mesmo de `tbox_context_set_animation_time`)
  internamente; `int32_t` ms na API de aplicação, por ser o tipo mais
  simples de mapear em qualquer FFI (2026-10-16).
- **`tbox_app_step` continua não-bloqueante** (é `tbox_app_wait(app, 0)`),
  para não mudar a semântica de quem já integra um loop (2026-10-16).
- **Ritmo pelo compositor, não por timer**: com animação ativa, o próximo
  frame espera o `wl_surface.frame`; uma janela oculta pausa as animações
  (o relógio é real, então elas "pulam" ao voltar, como no navegador). Cada
  backend sabe seus próprios prazos (a repetição de tecla só existe no
  Wayland; Windows e macOS repetem no sistema) e reduz o timeout recebido
  (2026-10-16).
- **Buffer duplo com dano acumulado por buffer**: cada buffer guarda o que
  mudou desde que foi pintado pela última vez; o dano de cada frame entra
  em todos. Se nenhum buffer estiver livre, o `present` recusa o frame e a
  aplicação tenta de novo quando o compositor liberar um — o dano não se
  perde porque já foi somado aos buffers (2026-10-16).

## Callbacks e structs na fronteira de FFI

- **Todo `userdata` guardado pela biblioteca vem com um
  `tbox_destroy_notify`** (`on_click`, `on_input`, `on_submit`,
  `on_select`, `tbox_app_on_key`, `tbox_app_post`, o resolver de
  `tbox_font_face_cache_create`). A biblioteca o chama **exatamente uma
  vez**, quando deixa de usar o `userdata`: `unbind`, troca do handler
  (inclusive por `NULL`), fim de uma tarefa postada (rodou ou foi
  descartada), `close`/`destroy` do dono. `NULL` = nada a liberar. Motivo:
  bindings de linguagens com GC precisam soltar a referência à closure no
  momento certo; sem isso, ou vaza, ou é use-after-free. Padrão do
  `GDestroyNotify` (GLib) e do `xDestroy` (SQLite) (2026-10-16).
- **Falha não transfere posse**: se o registro falha (`-1`/`false`), o
  `destroy` não é chamado e o `userdata` continua do chamador (2026-10-16).
- **Liberação adiada durante o despacho**: um handler que faz `unbind` de
  si mesmo, ou troca o próprio handler, continua usando o `userdata` até
  retornar; o `destroy` roda quando o despacho termina
  (`src/base/tbox_release.h`). Descartado: chamar na hora (use-after-free
  dentro do próprio handler) (2026-10-16).
- **Nenhum callback recebe struct por valor**: `tbox_key_event` e
  `tbox_font_query` chegam por ponteiro `const`; o valor de `on_input`/
  `on_select` chega como `(const char *, size_t)`. Funções que recebem
  structs grandes ou que vão crescer (`tbox_context_options`) recebem
  ponteiro (`NULL` = padrão); `tbox_context_dispatch_key` também, por
  simetria com o handler. Motivo: callbacks com struct por valor não
  funcionam (ou funcionam mal) em várias FFIs, e uma struct passada por
  valor que ganha um campo quebra a ABI de quem já compilou. Retornos por
  valor (`tbox_context_options_default()`) e `tbox_string_view` em
  parâmetros comuns ficam: as FFIs os suportam bem (2026-10-16).
- **Saem as variantes `_with_config`**: `tbox_context_options` já contém
  `tbox_ua_style_config`; com a API ainda instável, mudar as assinaturas é
  preferível a manter variantes de compatibilidade (2026-10-16).

## Ferramentas

- **`tbox_cmp` em C++/OpenCV, opcional, fora do `ctest`**: a API C do OpenCV
  não existe mais; o mantenedor quis manter o algoritmo existente. Só
  `core`+`imgproc`+`imgcodecs` são linkados. O veredito por asset não vira
  código de saída (v10).
