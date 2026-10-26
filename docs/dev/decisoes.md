# Registro de decisões

> - **Escopo:** decisões de design já tomadas, com o motivo e as alternativas descartadas. Consolida as seções "Decisões já tomadas" de v0 a v20.
> - **Código relacionado:** todo o pipeline
> - **Veja também:** [Histórico](historico.md), [Débito técnico](debito-tecnico.md), [Convenções](convencoes.md)

Cada item: **decisão** — motivo (versão em que foi tomada). Itens marcados
*(superado)* foram substituídos depois; ficam aqui porque explicam o
formato atual do código. O texto integral original de cada versão está em
`git show fe51140:ARCHITECTURE.md`.

## Controles desativados (2026-10-23)

- **Uma regra de "desativado" em `tbox_html_node_disabled`:** o seletor
  `:disabled` e o contexto (foco, clique, popups, API) consultam a mesma
  função, que inclui `<fieldset disabled>`. Antes cada lugar lia o atributo
  por conta própria, e as respostas divergiam.
- **Aparência só pela folha UA, não nos pintores:** regras `:disabled` em
  vez de os pintores testarem o estado. Assim o CSS do autor substitui as
  cores pela cascata normal, e a marca do checkbox/radio fica cinza de
  graça por seguir `color`. Cores tiradas da captura do Firefox.
- **Linha de base do `input type=color` centralizada, não a do Firefox:**
  o Firefox usa o fim da área de conteúdo, o que só alinha porque o padding
  dele é maior. Com o padding da tbox isso deixava a amostra deslocada; a
  linha de texto centralizada alinha o centro com campos e botões
  independentemente das alturas.

## Eventos de ciclo de vida (2026-10-21)

- **Registros por aplicação, um slot por evento:** seguem on_key e sobrevivem
  ao contexto substituído. Funções específicas preservam tipos de payload
  e evitam strings de nome de evento, casts de funções e atributos com script.
- **Load adiado ao primeiro frame aceito:** permite registrar depois de
  create e usar o layout dentro do callback. Recarga bem-sucedida agenda
  outro load. Unload é da janela, não da substituição do documento.
- **Pagehide/pageshow significam foco de janela:** contrato solicitado pelo
  projeto, diferente da semântica de navegação/cache dos navegadores. Usa
  foco de teclado em vez de inferir foco pela presença do ponteiro.
- **Destruição adiada e loop não reentrante:** preserva recursos durante
  callbacks, inclusive quando close é chamado por load/unload, tecla ou
  tarefa. A fila de liberações existente conserva userdata em uso.
- **Error reservado:** o registro funciona, mas nenhum retorno de falha nem
  markup inválido emite o evento antes de definir uma política de erros.
- **Teste de integração headless:** o mesmo arquivo do loop é compilado com
  backend simulado e construtor interno compartilhado; não é necessário
  adicionar um backend de teste ao produto nem estado global de mock.

## Placeholder, modalidade de foco e posicionados inline

`placeholder` tem estilo de texto próprio, emprestado da arena de frame
por `tbox_style.placeholder_style`. Resolver um estilo completo reaproveita
herança, fontes e cascata sem ampliar todas as partes de controles com
campos tipográficos. O texto é apenas uma representação do campo vazio;
não entra em `value`, seleção, submissão ou linhas editáveis de textarea.

`:focus-visible` usa modalidade no contexto e um booleano no nó focado,
lido junto do contexto de foco já existente do seletor. Isso evita novo
estado global e mantém o estado de cada documento independente. Teclas
sem Control ativam a modalidade de teclado; cliques e pressão do ponteiro
ativam a de ponteiro. Controles de texto sempre exibem foco visível.

Elementos `absolute`/`fixed` no conteúdo inline são retirados da coleta de
palavras e construídos como caixas próprias no mesmo contexto posicionado.
Não interrompem a linha entre seus irmãos. Essa solução aplica tamanho e
recorte ao `.visually-hidden` sem adaptar o HTML/CSS do explorador.

## FreeType obrigatório do sistema

O build usa `find_package(Freetype REQUIRED)` e o alvo importado
`Freetype::Freetype` para a biblioteca, os exemplos e as ferramentas.
FreeType continua responsável pelas métricas com hinting, rasterização e
contornos; HarfBuzz permanece opcional para shaping. A instalação de
desenvolvimento do FreeType é pré-requisito, sem download ou fallback via
`FetchContent`. Isso elimina a compilação de uma cópia própria e permite
usar a mesma biblioteca do sistema que atende Fontconfig e HarfBuzz.

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
- **`tbox_style_table` é uma tabela plana, indexada por hash de ponteiro.**
  Na v0 a busca era linear (reaproveitava o padrão de
  `tbox_css_computed_style_find`). Em 2026-10-26 a busca linear tornou o
  layout quadrático em tabelas de centenas de linhas, e a tabela ganhou um
  índice privado (`index_`) montado na arena ao fim de
  `tbox_style_resolve_tree*`. O índice ficou na tabela, e não no
  `tbox_html_node`, por três motivos. Há várias tabelas vivas para o mesmo
  nó: as duas passadas de `@container`, a sobreposta da escrita vertical
  (`fallback`) e as montadas pela própria API pública. Um índice no nó
  ficaria obsoleto para nós criados entre frames. E a resolução teria de
  escrever no DOM, que ela recebe `const`. Tabelas montadas à mão (sem
  índice) ou que cresceram depois de indexadas voltam à busca linear.
- **Uma `frame_arena` por contexto, resetada a cada frame**, compartilhada
  por Style, Layout e Render (v0).
- **Culling no Render, por subárvore e só sob clip de `overflow`.**
  Escolhido entre filtrar a display list pronta (cada op já tem seu clip),
  pular subárvores no Render e usar também o viewport. Filtrar depois não
  poupa a construção nem a memória da lista, e as camadas (máscara,
  reflexo, filtros) tornam arriscado remover ops soltos. A subárvore é
  pulada só quando um limite conservador, calculado das caixas, não toca o
  clip; o que não se limita barato (transformação, filtro, máscara,
  reflexo, `border-image`, SVG) é sempre pintado, então um erro de limite
  só pode deixar de cortar, nunca cortar a mais. O viewport ficou de fora:
  a página não rola fora dos contêineres, e
  `tbox_render_build_display_list` usa a caixa raiz como viewport, menor
  que o conteúdo que transborda dela (2026-10-26).
- **Filtro por nome antes dos validadores de declaração.** A cascata
  passa cada declaração por seis validadores (OpenType, pintura, motion
  path, composição, inline, SVG), puros em (propriedade, valor), cada um
  comparando o nome com a própria lista: ~21% do frame de uma tabela
  grande, quase todo para concluir "não é comigo". Escolhido entre
  guardar a validade por declaração na folha e um filtro por nome: a
  folha fica no parser, abaixo da Style, que é dona dos validadores, e
  calcular sob demanda numa folha `const` seria mutação escondida. Ficou
  `tbox_style_declaration_checked`, uma tabela constante ordenada (busca
  binária) com a união dos nomes, mais todo `-webkit-`. Para a tabela não
  ficar para trás quando um validador ganhar uma propriedade,
  `tests/style/test_style.c` confere nos dois sentidos, contra a lista do
  `@supports`: quem algum validador rejeita está na tabela, e todo nome da
  tabela é rejeitado por algum (2026-10-26).
- **Índice de regras por regra, não por seletor, e montado na folha.**
  A cascata testava todos os seletores de todas as regras: 171 a 307 por
  elemento nos exemplos, ~24% do frame de uma tabela grande e mais 8,6% nas
  cascatas de pseudo-elementos. A chave é o componente mais à direita (id,
  senão a primeira classe, senão a tag), como fazem os navegadores; o que
  há dentro de `:is()`/`:not()` não conta, de modo que a chave é sempre
  algo que o elemento precisa ter. O índice guarda regras (uma regra entra
  no balde de cada seletor seu) e a cascata reaproveita o laço por regra
  que já existia, só com menos regras; indexar seletores exigiria
  reagrupar os vencedores por regra. As candidatas são juntadas, ordenadas
  e sem repetição, para manter a ordem de origem, que decide empates entre
  baldes. A busca de candidatas fica na cascata, que já conhece o nó; a
  folha só monta o índice, sem depender do DOM. Sem índice (falta de
  memória) a cascata volta a visitar todas as regras (2026-10-26).
- **Compartilhamento de estilo conferido pela cascata.** Medido antes (1000
  linhas de tabela): a conversão das declarações era ~58% do estilo e a
  cascata ~29%, e 99,7% dos elementos tinham um anterior igual. Escolhido
  entre (A) rodar a cascata sempre e compartilhar só a conversão quando o
  resultado da cascata, a tag, os atributos e a classe do pai são iguais e
  (C) pular também a cascata, com uma chave por tag, atributos, pai e
  posição entre os irmãos. A chave de C é segura em tabelas comuns (83% de
  compartilhamento, zero erros) mas erra com seletores que dependem da
  posição de um ancestral, de irmãos ou de estado (`tr:nth-child(even) td`,
  `tr.x + tr td`, `tr:hover td`): 2855 erros em 6011 elementos. Ficou A,
  correta por construção; C fica como passo seguinte, exigindo que o matcher
  registre o que consultou (posição própria, de ancestral, irmãos, estado,
  `:has`, contêiner) para recusar o modelo quando a chave não basta. A
  igualdade da cascata compara propriedade, valor, regra, origem,
  `!important` e registro `@property`, nessa ordem; os textos são copiados
  para o modelo porque a cascata libera os seus. Um modelo só conta se os
  passos por nó seguintes (marcador, partes, contêiner) não mudaram seu
  estilo, conferido por comparação de bytes com a cópia feita logo após a
  conversão, para que um passo novo desse tipo não quebre o compartilhamento
  em silêncio (2026-10-26).
- **`border` sem cópia de `border-image`.** O `border` redefine
  `border-image` para o inicial, e a resolução alocava uma cópia do inicial
  (1720 bytes) para todo elemento com `border`. `NULL` já é o inicial; a
  cópia só existe quando alguma declaração de `border-image` vem depois do
  último `border` (2026-10-26).
- **Tabela de estilos reservada pelo número de elementos.** Na arena, um
  `tbox_vector` que dobra deixa para trás cada cópia superada, e a tabela
  (entradas de 9,7 KB) ocupava 2 a 4 vezes o necessário: 151 MiB para
  56 MiB úteis com 1000 linhas de tabela. A resolução conta os elementos
  antes e reserva uma entrada para cada. Os `::before`/`::after` também
  entram na tabela e só são conhecidos depois do estilo; escolhido entre
  reservar só os elementos (um excedente de pseudo-elementos faz o vetor
  dobrar uma vez), reservar o triplo quando há regras desses
  pseudo-elementos e separá-los numa tabela ligada por `fallback`: ficou a
  primeira, a mais simples, cujo pior caso (toda célula com `::before`)
  ainda ocupa menos da metade de antes (2026-10-26).
- **Recompute total por frame.** Sem dirty-tracking de estilo/layout; desde a
  v20, só a *repintura* é parcial (ver [Atualização da tela](../05-atualizacao-da-tela.md)).
- **`tbox_context` não guarda flag "sujo".** Funções de evento
  (`dispatch_click`, `update_hover`, `dispatch_key`, ...) retornam `bool`; quem
  decide recomputar é a Application (v1, v3).

- **Container queries em duas passadas por frame**: estilo sem tamanhos;
  se houver contêiner `size`/`inline-size`, um layout descartável mede o
  content box de cada um; a árvore é re-resolvida com esses tamanhos e só
  então vêm transições/animações e o layout final. Uma iteração, sem
  estado entre frames nem global; um documento sem contêiner paga só uma
  varredura da tabela. Descartados: usar os tamanhos do frame anterior
  (um frame atrasado, estado no contexto, screenshot errado), refazer só
  as subárvores dependentes (o layout não tem relayout parcial) e iterar
  até estabilizar (custo imprevisível). Transições rodam só sobre os
  estilos finais, para não disparar transições falsas entre as passadas
  (2026-10-18).

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
- **Estado de transição só para quem declara `transition`; trilhas sob
  demanda.** Até 2026-10-26 todo elemento guardava o último alvo de toda
  propriedade animável (~4 KB, `malloc`, numa lista ligada procurada por
  elemento), para animar também uma mudança que chega junto com o
  `transition`, como num `:hover` que declara os dois. Numa tabela de 1000
  linhas isso eram 6000 estados (~25 MB) e 173 ms por frame, quadrático.
  Escolhido entre (a) estado só para elementos com `transition` efetiva,
  (b) estado para todos mas compacto (só valores fora do inicial, ou só
  propriedades que alguma regra transiciona) e (c) só trocar a lista por
  um índice: ficou (a), por ser a maior economia e por o caso perdido ser
  raro numa UI de aplicação, com efeito discreto (a mudança acontece sem
  animar). Consequências aceitas: ganhar `transition` junto com a mudança
  não anima; um elemento visível que ganha `transition` é tratado como
  recém-aparecido (`@starting-style` pode valer); perder `transition`
  encerra as em curso. A busca usa um índice por hash montado a cada frame
  na `frame_arena` a partir da lista, e os estados não vistos no frame
  (nó fora do documento, `transition` removida) são liberados ao fim. As
  trilhas em andamento continuam alocadas só quando a primeira transição
  começa (2026-10-12; revisto em 2026-10-26).
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

- **Raiz de `@scope` passada ao matcher** como parâmetro explícito, em vez
  de estado do contexto de seleção; registros de `@property` e
  `@counter-style` ficam na folha, separados das regras, e os descritores
  de contador são copiados para a arena do frame, com uma formatação só
  para listas e `counter()` (2026-10-18).
- **`ex`/`ch` medidos por um provedor passado à Style**
  (`tbox_style_font_metrics`, parâmetro de
  `tbox_style_resolve_tree_in_viewport_with_preferences`), em vez de a Style
  depender do módulo de fontes ou de uma tabela fixa por família. A razão
  (altura-x e avanço do "0" sobre o tamanho da fonte) fica no `tbox_style`,
  herdada, e só é medida onde a `font-family` muda; usa a face regular,
  sem peso nem itálico. Sem provedor, 0,5em como antes (2026-10-19).
- **`medium` de 13px para `monospace` só em tamanho derivado de
  palavra-chave**, como no Gecko. A escala direta de todo texto
  monoespaçado encolheria `font: 16px monospace`, e um `font-size` fixo na
  folha UA encolheria `code` dentro de `h1`. Por isso o `body` da UA passou
  de `px` para `em` (2026-10-19).
- **`<textarea>` continua com a fonte do documento**, e não com a
  monoespaçada dos navegadores. Os controles do tbox seguem um app nativo
  (2026-10-19).
- **Decoração propagada aproxima várias camadas por uma**: as linhas de
  todos os ancestrais se somam, mas estilo, cor e espessura são os do mais
  próximo. Dois ancestrais com cores diferentes desenham ambos na cor do
  mais interno (2026-10-19).
- **Grupos grandes e raros de `tbox_style` fora de linha** (máscaras,
  camadas extras de fundo, texto do grid, `clip-path`, `shape-outside`,
  `offset-path`, filtros, `border-image`, `box-reflect`): ponteiro `const`
  para a arena da resolução, `NULL` = valores iniciais, lidos por acessores
  `tbox_style_*_of` que devolvem um `const` estático com os iniciais (a
  mesma constante substitui as funções `init_*`, uma fonte só). Escrita
  sempre numa cópia nova (`tbox_style_group_copy`/`_edit`): grupos são
  compartilhados por herança e pelas cópias do layout, então nunca são
  escritos no lugar. As palavras-chave CSS-wide copiam campo a campo dentro
  do grupo (`tbox_style_field.group`), o que mantém casos como
  `mask-image: inherit; mask-size: 10px`. Motivo: copiar e zerar o struct
  de ~39 KB era ~35% das instruções de um frame. Alternativas descartadas:
  só passar por ponteiro (as cópias por herança e no layout continuariam);
  `NULL` = zero (exigiria que todo leitor conhecesse os iniciais não nulos).
  `tbox_style_resolve`/`_in_viewport` passaram a receber a arena (2026-10-20).

## Texto e fontes

- **Lista de `font-family` resolvida no cache de faces**, nome a nome, com
  a família padrão no fim. O Fontconfig só aceita a própria família ou um
  alias de ligação forte/`same`, e recusa o substituto fraco. É a regra do
  Firefox, que faz um nome desconhecido cair no próximo da lista em vez de
  virar DejaVu Sans. A escolha é por run, sem fallback por glifo
  (2026-10-19).

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
  Na integração original, evitar `hb-ft` impedia misturar o FreeType estático
  do build com o do sistema. Com FreeType obrigatório do sistema, esse
  motivo fica superado, mas a integração é preservada: o `hb_face` lê os
  bytes que a face já guarda e um sub-font troca só o avanço pelo do
  FreeType (com hinting, o mesmo dos bitmaps). `hb_font`/`hb_buffer` ficam na
  face, sem estado global.
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

- **Features OpenType na chave do cache de faces**: a configuração de
  features e idioma é imutável por face e entra na chave do cache, em vez
  de ir como parâmetro a cada shaping. `font-feature-settings` explícito
  prevalece sobre o que os `font-variant-*` pedem para a mesma tag
  (2026-10-18).

- **Bidi próprio, no layout**: implementação de UAX #9 sem FriBidi/ICU (sem
  dependência nova; o HarfBuzz não faz bidi), aplicada sobre as palavras já
  montadas — níveis do parágrafo inteiro antes da quebra, reordenação por
  linha depois — em vez de dentro do shaper. O espelhamento (L4) fica no
  shaper. Os padrões HTML de `unicode-bidi` (`<bdo>`, `<bdi>`, `dir`)
  ficam em `tbox_style.c`, como o `dir`, porque a folha UA não tem seletor
  de atributo; `<br>` fecha o parágrafo bidi e reabre as marcas
  (2026-10-18).

- **Contorno de texto por `FT_Glyph_Stroke`** (raio = metade da largura
  CSS) em vez de dilatar a máscara do glifo, para preservar curvas e bordas
  internas. `marker*`/`mask-type` (só SVG) ficam falsos em `@supports`,
  como as propriedades 3D, para não levar a página a um caminho SVG que
  não existe (2026-10-18).

- **Motion path como mais um termo da matriz afim** (translate → rotate →
  scale → offset → transform), reaproveitando a camada das transformações;
  o caminho é amostrado com tolerância de 0,125px em tabelas por elemento
  na arena do frame (2026-10-18).
- **`paint-order` fora das propriedades animáveis** para caber na máscara
  de 64 bits; alternativa adiada: trocar a máscara pública por um conjunto
  maior (muda `tbox_style_animatable_mask`) (2026-10-18).

- **Cache de runs moldados na face**, não global nem no contexto: a face já
  carrega o estado do shaper e vive no `tbox_font_face_cache`, então o
  cache tem o mesmo dono, a mesma vida e a mesma afinidade de thread.
  Chave (texto, `letter-spacing`, direção); o resto do que muda o shaping
  é fixo na face. Textos acima de 256 bytes não entram. Alternativa
  adiada: o layout incremental, que evitaria o shaping por não refazer o
  layout (ver [Débito técnico](debito-tecnico.md#estilo-e-layout-incrementais))
  (2026-10-20). O despejo era esvaziar tudo ao passar da metade de 2048
  posições, e uma tabela de ~400 linhas tem mais palavras que isso: o
  cache se esvaziava várias vezes por frame e nada era reaproveitado. Em
  2026-10-26 a tabela passou a dobrar até 65536 posições e, no máximo, a
  varrer com segunda chance (fica o que foi reaproveitado desde a última
  varredura), em vez de LRU: sem lista nem relógio, e o custo da varredura
  só aparece quando o conjunto de trabalho passa de 32768 runs. A marca de
  uso vale para o reaproveitamento, não para a entrada; marcar na entrada
  faria a primeira varredura achar tudo usado e esvaziar o cache.
- **Avanços de glifo em cache na face**, em páginas de 256 glifos alocadas
  sob demanda, e não um vetor do tamanho da fonte (uma fonte CJK tem dezenas
  de milhares de glifos, e cada tamanho é uma face). Continuam vindo do
  `FT_Load_Glyph` com hinting, que o raster também usa; desligar o hinting
  só no avanço mudaria as métricas e a paridade com o Firefox (2026-10-26).

## Layout

- **Larguras de coluna pelo algoritmo automático dos navegadores**
  (min/max-content, `%` antes de fixas antes de automáticas), e não pela
  divisão igual da sobra que havia. Com `border-spacing: 2px` na folha UA,
  quase toda tabela passa pelo caminho estendido
  (`tbox_layout_table_grid.c`). O caminho simples
  (`tbox_layout_table.c`) só fica para tabelas sem espaçamento nem outros
  recursos (2026-10-19).

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

- **Grid separado de flex**: `src/layout/tbox_layout_grid.c` com as listas
  de trilhas e áreas já validadas no estilo (`tbox_style_grid.c`), em
  buffers próprios do `tbox_style` — não dependem da vida das declarações
  da cascata nem pedem destrutor. O algoritmo usa a arena do frame e
  constrói os itens com tamanho forçado (2026-10-18).

- **Um gerenciador de floats por BFC**, criado pela caixa que estabelece o
  contexto e levado em `tbox_layout_positioned_context.floats` — em vez de
  um parâmetro novo em todas as funções de layout ou de estado global. Um
  float no meio do texto vira uma palavra de largura zero com a caixa
  pronta, posta por `break_lines`. Floats são pintados como camadas do
  contexto de empilhamento, entre o conteúdo em fluxo e os posicionados;
  dividir `tbox_render_box` nas fases exatas do Apêndice E ficou para
  depois, por ser grande demais (2026-10-18).

- **Fragmentos de multicoluna são caixas comuns** na arena do frame,
  compartilhando o nó DOM (um nó pode ter várias caixas); a regra entre
  colunas reaproveita a pintura de bordas; o balanceamento procura a menor
  altura que cabe (2026-10-18).

- **Capitulares como floats**: `initial-letter` e `::first-letter`
  flutuante usam o gerenciador de floats do BFC; a exclusão segue a tinta
  dos glifos (como o Firefox), não a caixa inteira da linha. O estilo de
  `::first-letter` fica na arena do frame, e a validação das propriedades
  novas é a mesma na cascata e em `@supports` (2026-10-18).

- **Escrita vertical por referencial e transformação**, em vez de um layout
  com eixos abstratos: a subárvore é diagramada como horizontal e girada
  (ou transposta) pelo caminho das transformações, que já cuida de pintura,
  damage e hit test; o custo para documentos horizontais é uma comparação
  por caixa. Os estilos são convertidos numa tabela sobreposta
  (`tbox_style_table.fallback`), não na cascata, para o Style continuar
  físico. O avanço de glifo em pé é 1em sintetizado (como o Firefox faz
  para fontes sem `vmtx`) em vez de `HB_DIRECTION_TTB`, o que funciona
  também sem HarfBuzz. Motion path dentro do modo vertical é amostrado na
  orientação da tela (2026-10-18).

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
- **`border-image` como ops recortados**: cada fatia e cada ladrilho é um
  `IMAGE`/`GRADIENT` com `clip` e `source_rect`, recortado no Output.
  Descartados: escalar a imagem inteira e recortar (memória, vazamento de
  pixels vizinhos) e rasterizar o gradiente no Render (o Render não
  rasteriza). Gradientes são dispostos no tamanho da área e esticados de
  forma não uniforme, para `circle` e ângulos ficarem contínuos entre as
  fatias; ladrilhos de imagem são alinhados a pixel inteiro para não deixar
  frestas (2026-10-18).

- **Transformações como matriz afim 2D numa camada**: a caixa transformada
  vira um `LAYER_BEGIN` com a matriz, por fora da camada de efeitos (o
  `mix-blend-mode` passa para ela, que é quem compõe com o fundo), composto
  pela inversa com amostragem bilinear. Translação pura continua deslocando
  as caixas no layout, sem camada. Funções 3D: a sequência é multiplicada
  em 4x4 e só a parte 2D do produto é guardada (achatar função por função
  erra `rotateX() rotateY()`); `@supports` aceita as funções 2D e recusa as
  3D, para a página seguir o caminho 2D. Interpolação de `transform`
  posição a posição, com troca discreta quando os tipos diferem, em vez de
  decomposição de matriz (2026-10-18).

- **Máscaras e reflexo sobre as camadas de composição existentes**: as
  camadas de máscara são acumuladas de trás para frente numa cobertura
  alfa; o reflexo copia o grupo já pintado sob uma matriz de espelhamento;
  os fragmentos de `clone` reaproveitam a pintura normal de caixas
  (2026-10-18).

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

- **`<textarea>` sem `resize` por padrão** (app nativo, ao contrário dos
  navegadores); o autor liga com `resize`. O tamanho arrastado é aplicado
  sobre o estilo resolvido a cada frame, não gravado como estilo inline no
  DOM, e o arrasto reaproveita `tbox_context_scrollbar_press`/`_drag`/
  `_release` em vez de uma API pública nova (2026-10-18).

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

- **Fixtures nomeados por categoria, um recurso em um só lugar**, em vez da
  numeração na ordem de implementação. Com números, o mesmo estilo aparecia
  em vários fixtures (bordas em 028, 029, 034, 036, 037...) e achar onde um
  recurso era testado exigia a tabela. Com `<categoria>-<tema>`, o nome
  diz o assunto, recursos vizinhos ficam juntos e uma regressão de
  paridade aponta para um só arquivo. A alternativa de juntar tudo de uma
  categoria num arquivo só foi descartada: o fixture precisa caber em
  800x600 para a comparação com o Firefox (2026-10-19).

- **Referência do `tbox_cmp` capturada no Firefox a cada execução**, em
  vez de PNGs guardados no repositório. Uma imagem tirada à mão envelhece
  quando o fixture muda, depende do navegador e da tela de quem a tirou e
  não existia para a maioria dos fixtures. A captura headless com tamanho
  fixo reproduz a medição de [paridade](../referencia/paridade-firefox.md)
  e cobre todos. O custo é ter o Firefox na máquina para rodar a
  ferramenta, que já era opcional e fora do `ctest` (2026-10-19).

- **`tbox_cmp` em C++/OpenCV, opcional, fora do `ctest`**: a API C do OpenCV
  não existe mais; o mantenedor quis manter o algoritmo existente. Só
  `core`+`imgproc`+`imgcodecs` são linkados. O veredito por asset não vira
  código de saída (v10).

## SVG inline básico (2026-10-21)

- **Caixa HTML externa e árvore vetorial interna:** formas não participam
  do layout HTML. As instâncias de `use` vivem na arena do frame, com
  herança recalculada; não clonam nem reparentam o DOM público.
- **Caminhos compartilhados e FreeType:** extraído o parser amostrado de
  motion path para `base/tbox_path.*`. O Output usa o rasterizador e o
  stroker do FreeType obrigatório, evitando uma dependência adicional ou
  outro rasterizador de curvas. Um estado por chamada de rasterização é
  inicializado sob demanda e destruído ao terminar; não reutiliza o global
  de fontes nem exige fonte carregada para pintar vetores.
- **Atributos na cascata:** prioridade própria abaixo de qualquer folha de
  autor, inclusive `@layer`; evitado aplicar atributos depois do CSS.
- **Display list vetorial pública:** `PATH` carrega segmentos e parâmetros
  de pintura acessíveis a outros backends, emprestados da arena do frame.
  Hash por conteúdo evita repintura contínua por endereços transitórios.

## Registro público de eventos centralizado em app — 2026-10-21

Clique, input, select e submit passam a ser registrados por `tbox_app`,
assim como teclado e ciclo de vida. O callback também recebe o app. Hooks
privados do contexto continuam responsáveis pelo despacho; adaptadores
por registro traduzem o contexto para o app sem estado global. Remove-se
a API pública anterior, incluindo o unbind de clique, em vez de manter
dois caminhos públicos para o mesmo registro. A duração dos registros de
controles continua sendo a do documento, preservando a limpeza no reload.

A implementação de eventos fica em `src/app/tbox_app_events.c`; criação,
recarga, loop e destruição da aplicação ficam em `tbox_app.c`. O header
interno compartilha apenas as operações de emissão, limpeza e proteção da
vida do app. Essa separação mantém o registro e o tratamento de callbacks
juntos sem alterar a API ou introduzir estado global.

## Camada de controles separada do DOM — 2026-10-22

O estado dos controles está em lugares diferentes: atributo `value`
(inputs), texto (textarea, output), escolha no contexto (`<select>`),
atributo `checked`. Em vez de mover tudo para um só lugar, uma camada
fina em `controls.h` despacha por tipo. A implementação fica no contexto
(que sabe onde cada estado vive e mantém caret, popup e foco coerentes),
mas a API pública pertence ao módulo app e recebe o `tbox_app`, como os
registros de eventos: o adaptador obtém o contexto por `tbox_app_context`.
Funções que só leem o nó (`checked`, `disabled`, contagem de opções) não
recebem o app. A busca de elementos (`tbox_app_query`, `_get_element_by_id`)
segue o mesmo caminho, para o código de aplicação não precisar do
`tbox_context`. O DOM
(`html_parser.h`) continua genérico, recebendo só o documento.
Alternativas descartadas: funções por controle no DOM (o DOM não conhece o
contexto, então não sincroniza caret/popup), e um "valor" sempre em
atributo (o `<select>` perderia a distinção entre escolha atual e
`selected` inicial, que o reset precisa). Mudanças por código não chamam
`on_input`/`on_select`, como no DOM do navegador. Índices de opção usam
`size_t` com `SIZE_MAX` = fim; o índice escolhido usa `ptrdiff_t` com `-1`
= nenhum, como `selectedIndex`.

## Revisão do documento para redesenhar — 2026-10-22

Para desenhar mutações feitas fora de handlers sem `tbox_app_request_redraw`,
o documento conta as mutações e o `tbox_app` compara com a contagem do
último frame. Como várias funções de mutação recebem só o nó, cada
`tbox_html_node` ganhou `owner_document` (+8 bytes por nó), aceito pelo
mantenedor. Alternativas descartadas: marcar sujo a cada volta do loop
(redesenho contínuo), comparar a árvore (caro) e um contador global (estado
global, e misturaria janelas). A revisão é lida depois do `run_frame`
porque o frame normaliza valores no DOM (sanitização de inputs, ligação do
buffer de edição); essas escritas acomodam em um frame, sem laço.
Escrita direta nos campos da struct não é vista: continua exigindo
`tbox_app_request_redraw`.

## API de tabelas segue o DOM — 2026-10-24

As `tbox_table_*` copiam a semântica de `HTMLTableElement.rows`/`insertRow`
e `HTMLTableRowElement.cells`/`rowIndex`/`cellIndex`, para a futura camada
de script só expor o que já existe. Por isso a linha do `<thead>` conta como
linha `0`; a alternativa de numerar só as linhas do corpo foi descartada
por fugir do DOM. O índice de célula é a posição no DOM, não a coluna da
grade: resolver `colspan`/`rowspan` fica para uma função separada, se
aparecer o caso. As funções ficam em `controls.h`, ao lado das
`tbox_select_*`, em vez de um header próprio. Diferente do `<select>`, a
tabela não tem estado no contexto, então nada novo foi guardado. Ordenação
ficou para outro momento.

## tbox como subprojeto CMake — 2026-10-25

O `add_subdirectory`/`FetchContent` foi escolhido como a forma principal de
consumo, com `find_package` sobre um install como alternativa, porque não
exige empacotar nada e mantém as opções do tbox ao alcance do projeto pai.
O consumidor linka o mesmo `tbox::tbox` nas duas formas. Um
`ExternalProject` (o que o t-explorer precisava fazer antes) foi descartado
como recomendação: ele esconde os alvos do pai, e o alvo importado precisa
ser montado à mão.

- **Padrões por `PROJECT_IS_TOP_LEVEL`**, em vez de exigir que o pai
  desligue exemplos, testes e `-Werror`: quem só quer a biblioteca não
  deveria compilar os exemplos, ter os testes no `ctest` dele nem quebrar
  por um aviso novo do compilador.
- **Como subprojeto, uma biblioteca só, escolhida por `BUILD_SHARED_LIBS`**,
  a convenção do `add_library` sem tipo. No build raiz continuam as duas,
  porque os testes linkam a estática (internos, `app_lifecycle`).
- **Uma `OBJECT` library compartilhada pelas duas**, em vez de compilar as
  fontes duas vezes. O custo é `-fPIC` também na estática, irrelevante aqui.
- **Prefixo `TBOX_` nos módulos do pkg-config** (`PkgConfig::TBOX_FONTCONFIG`):
  os alvos importados são por diretório, mas o nome sem prefixo colidiria
  com um `pkg_check_modules(FONTCONFIG ...)` do próprio projeto pai.
- **O alvo `cppcheck` mantém o nome**, mas só existe no build raiz. Renomear
  para `tbox_cppcheck` foi descartado: mudaria o comando do mantenedor sem
  ganho, já que como subprojeto ele não é criado.
- **Soname `0.1`** (maior.menor), porque na série 0.x a ABI muda entre
  versões menores. Pelo mesmo motivo, o pacote CMake usa `SameMinorVersion`.

## Itens a partir de `<template>` em `controls.h` — 2026-10-25

Para a aplicação montar listas e linhas com estrutura (ícone, texto,
botão) sem andar no DOM, o modelo vem do próprio HTML, num `<template>`, e
`tbox_control_instantiate` copia os filhos dele. A marcação fica no HTML,
onde o CSS que a estiliza está, e o C só preenche dados. Descartadas:

- **Um construtor em C** (criar elemento, pôr classe, anexar): é a API de
  DOM de novo, e a estrutura do item ficaria espalhada pelo código.
- **Parse de um fragmento de HTML vindo do C** (`innerHTML`): exige montar
  e escapar strings, e cada chamada passaria pelo parser.
- **Funções por tipo de item** (`tbox_list_*`, `tbox_card_*`): não há
  semântica própria a esconder, como há na tabela e no select.

`<select>`/`<optgroup>` são recusados como contêiner (e `<option>` em
`remove`) porque a escolha vive no contexto e precisa de
`tbox_context_select_options_changed`; para eles já existem as
`tbox_select_*`. O índice de inserção conta só elementos, como as demais
funções de posição. Classe e `hidden` entraram como funções genéricas, e
não como estados nomeados (`selected`, `current`), para o CSS da aplicação
decidir o que cada estado significa. `hidden` passou a ter
`display: none` na folha UA, que o `display` do autor vence, como nos
navegadores.
