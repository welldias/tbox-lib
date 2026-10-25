# 7. HTML e CSS

> - **Escopo:** como o tbox lê o documento e as folhas de estilo: regras do parser, origens da cascata, folha user-agent, seletores, unidades e valores. A lista completa do que é suportado está em `referencia/`.
> - **Código relacionado:** `src/html_parser/`, `src/css_parser/`, `src/css_selector/`, `src/css_cascade/`, `src/style/`, `src/context/tbox_ua_style.c`
> - **Veja também:** [CSS suportado](referencia/css-suportado.md), [HTML suportado](referencia/html-suportado.md), [Layout e pintura](08-layout-e-pintura.md)

## O documento HTML

- **Um elemento de topo.** O parser não cria `<html>`/`<body>` implícitos, e
  só o **primeiro** elemento do nível raiz é desenhado. Envolva tudo num
  único `<html>`, `<body>` ou `<div>`. (Um `<head>` irmão do `<body>`
  continua valendo para `<style>`, veja abaixo.)
- **Tolerante.** Markup quebrado nunca faz o parse falhar. Tags
  desconhecidas viram elementos genéricos; comentários e DOCTYPE são aceitos;
  aninhamento muito profundo não estoura a pilha (a construção é iterativa).
- **Fechamento implícito** só para `<p>` (fechado por um novo bloco como
  `<div>`, `<ul>`, `<h1>`, outro `<p>`…) e `<li>` (fechado por outro `<li>`
  da mesma lista: a busca para em `<ul>`/`<ol>`/`<menu>` e em fronteiras
  como `<table>`/`<td>`/`<button>`, então o primeiro `<li>` de uma lista
  aninhada não fecha o item de fora).
  Feche explicitamente `<tr>`, `<td>`, `<th>` e `<option>`; o parser também
  não insere `<tbody>`.
- **Entidades**: numéricas (`&#233;`, `&#xE9;`) e todas as nomeadas do HTML5
  de um codepoint (`&amp;`, `&nbsp;`, `&frac12;`…), sempre com `;` final.
  Não são decodificadas dentro de `<script>`/`<style>`.
- **`<script>` não é executado.** O conteúdo é guardado como texto, mas não
  há motor de JavaScript.
- **`<template>`** não é desenhado (`display: none`), e o conteúdo dele fica
  como filhos comuns, não num fragmento à parte (inclusive `<tr>`/`<td>` fora
  de `<table>`). É o modelo de `tbox_control_instantiate` (ver
  [Guia da API](03-guia-da-api.md#itens-classes-e-visibilidade--controlsh)).

## SVG inline

O vocabulário SVG básico participa do pipeline de estilo e pintura; veja
[SVG inline](08-layout-e-pintura.md#svg-inline). Os nomes de atributos
continuam normalizados pelo parser (`viewBox` → `viewbox`, `pathLength` →
`pathlength`); a API de atributos já compara nomes sem distinguir caixa.
Não há parser XML separado nem suporte geral a namespaces.

Atributos de apresentação SVG entram na origem de autor antes de todas as
folhas de autor, inclusive suas camadas, com especificidade zero. `style`,
seletores e `!important` seguem a cascata normal; `!important` dentro do
atributo de apresentação é inválido. Isso vale para `fill`, `stroke`,
`stroke-width`, `stroke-linecap`, `stroke-linejoin`, `stroke-miterlimit`,
`stroke-dasharray`, `stroke-dashoffset`, `fill-rule`, `fill-opacity`,
`stroke-opacity`, além dos atributos comuns `color`, `opacity`, `transform`,
`display`, `visibility`, `overflow` e `paint-order`. `width`/`height` do SVG
externo também participam da cascata. Os atributos de geometria das formas
são interpretados pelo módulo SVG; suas equivalentes CSS não são suportadas.

Pinturas aceitam cores sólidas, `currentColor` e `none`; gradientes e outros
paint servers via `url()` ficam falsos em `@supports`. Comprimentos de traço
aceitam números em unidades do desenho, comprimentos CSS e percentuais
(relativos à diagonal normalizada do viewport). Valores iniciais: fill
preto, stroke none, espessura 1, ponta butt, junção miter, limite 4,
fill-rule nonzero e opacidades 1. Propriedades de pintura são herdadas e
aceitam `var()` e keywords CSS-wide. `opacity` de elemento/grupo não herda.

## De onde vem o CSS

```mermaid
flowchart LR
    UA["Folha user-agent<br/>(gerada a partir de tbox_ua_style_config)"] --> C{cascata}
    EXT["CSS externo<br/>(css / css_path)"] --> C
    INT["#60;style#62; do documento<br/>(todos, concatenados)"] --> C
    INL["atributo style=#34;…#34;"] --> C
    C --> R[valor de cada propriedade por elemento]
```

Prioridade, da menor para a maior:

```
user-agent < user < autor < autor-inline
  < autor !important < autor-inline !important
    < user !important < user-agent !important
```

- A origem *user* existe na cascata (`TBOX_CSS_ORIGIN_USER`), mas o
  `tbox_context` não carrega nenhuma folha dessa origem.
- CSS externo e `<style>` interno são a mesma origem (autor). Com mesma
  especificidade, o `<style>` interno vence o externo.
- `style=""` vence qualquer seletor de autor, e `!important` dentro dele
  vence qualquer `!important` de seletor.
- **Camadas (`@layer`)**: dentro de uma origem e importância, antes da
  especificidade. Nas declarações normais, uma regra fora de camada vence
  todas as camadas, e uma camada posterior vence uma anterior; nas
  `!important` a ordem se inverte (camada anterior vence, e qualquer camada
  vence a regra fora de camada). A ordem é a da primeira menção; as
  subcamadas de `a` vêm antes das regras diretas de `a`. Cada folha tem a
  própria ordem: o CSS da aplicação e o CSS do documento não compartilham
  nomes de camada. `tbox_css_ruleset.layer` guarda a posição (0 = fora de
  camada).
- `<style>` é procurado no documento inteiro, inclusive num `<head>`.
- `<link rel="stylesheet" href="...">` lê CSS de arquivo local na abertura do
  contexto. `<link>` e `<style>` obedecem à ordem do documento; a folha CSS
  passada pela aplicação vem antes de ambas. Caminhos relativos de `<link>`
  usam `asset_base_dir` e o primeiro `<base href>` do documento, se houver.
  `<base href>` deve apontar para um diretório e terminar com `/`; também
  afeta `src` de `<img>` e `<source>` em `<picture>`. Não há URLs de rede,
  `target`, atualização dinâmica de `<link>` ou `@import`. URLs de fontes em
  folhas vinculadas ainda usam `asset_base_dir`, sem resolução relativa ao
  arquivo CSS. Exemplos: `tests/assets/cascade-link-and-import.html` e
  `html-base-href.html`.

### A folha user-agent

É o "estilo padrão do navegador": `display` de cada elemento, tamanhos e
negrito de `<h1>`–`<h6>`, margens de `<p>`, recuo de listas, `<pre>`
monoespaçado, `<mark>` amarelo, controles de formulário como `inline-block`
etc. É gerada pela biblioteca (não há arquivo externo) e os valores
numéricos vêm de `tbox_ua_style_config`:

```c
tbox_context_options options = tbox_context_options_default();
options.ua_style.font.base_px = 18.0;         /* tamanho base do body */
options.ua_style.margin.paragraph_px = 10.0;  /* margem vertical de <p> */
tbox_app *app = tbox_app_create_with_options(html, css, 800, 600, &options);
```

Campos: `font.base_px`, `font.heading_em[6]` (escala de `h1`…`h6`),
`margin.heading_px[6]`, `margin.paragraph_px`, `margin.body_px`,
`margin.list_px`, `margin.hr_px`, `list_padding_left_px`, `hr_height_px`.
Qualquer regra da UA pode ser sobrescrita pelo CSS de autor normalmente.
As margens de `p`, `h1`…`h6`, `ul`/`ol` são `margin-block` e o recuo de
`ul`/`ol` é `padding-inline-start` (como nos navegadores): seguem
`direction` e `writing-mode` (em `rtl` o recuo vai para a direita).

Também seguem os navegadores:
- **Listas aninhadas:** perdem as margens de bloco, e o marcador de `ul` vira `circle` no segundo nível e `square` do terceiro em diante.
- **Recuo de bloco:** `blockquote` e `figure` têm 40px de recuo lateral, e `pre` tem margem de bloco.
- **Texto:** `address` sai em itálico, `abbr[title]` com sublinhado pontilhado e `q` com aspas (`open-quote`/`close-quote` em `::before`/`::after`).
- **Links:** só `a:any-link` (com `href`) fica azul e sublinhado.
- **`hidden`:** `[hidden]` tem `display: none`. Como é uma regra da UA, um
  `display` do autor no mesmo elemento a vence.

A folha aceita, além de tag, estado e parte, um atributo presente e uma
cadeia de ancestrais (`UA_SEL_ATTR`, `UA_SEL_DESC`).

O `body` recebe `font.base_px` como `em` sobre o `medium` inicial, e não
como `px`. Assim, o tamanho continua "derivado de palavra-chave", e a
família genérica `monospace` usa um `medium` próprio de 13px, como nos
navegadores. Isso vale para tamanhos que vêm de `medium`/`small`/... ou de
`em`/`%` sobre eles: `pre` e `code` saem com 13px, aninhados não encolhem
de novo, e `font: 16px monospace` fica com 16px
(`tbox_style.font_size_keyword`). O `<textarea>` usa a fonte do documento
(escolha de app nativo), e não a monoespaçada do navegador.

## Regras e at-rules

- **`@media`**: `screen`/`all`, listas com vírgula, `and`/`not`,
  `min-`/`max-width`, `min-`/`max-height`, `orientation`
  (`px`, `em`, `rem`), `aspect-ratio` (com `min-`/`max-`) e
  `prefers-reduced-motion` (definido por
  `tbox_context_options.prefers_reduced_motion`). As demais features têm
  valores fixos de um app desktop: `prefers-color-scheme: light`,
  `prefers-contrast: no-preference`, `forced-colors: none`,
  `inverted-colors: none`, `hover`/`any-hover: hover`,
  `pointer`/`any-pointer: fine`, `resolution` 1dppx (`dppx`, `x`, `dpi`,
  `dpcm`), `color` 8, `monochrome` 0, `scripting: none`, `update: fast`.
  A forma booleana (`(hover)`) também vale. Reavaliado a cada frame,
  inclusive no resize. Sem `print`, sem sintaxe de intervalo.
- **`@import`**: só no início da folha (depois de um `@charset` opcional),
  com `url()` ou string e uma lista de media opcional (vira `@media`).
  Arquivos locais, relativos à folha que importa (ou ao documento, numa
  `<style>`), aninhados até 8 níveis; o texto importado entra antes das
  regras da folha. `supports()` no `@import` é avaliado como `@supports`
  (falso descarta a importação); `layer()` é aceito (ver `@layer`). Vale em `<style>`,
  `<link>` e no CSS passado pela aplicação.
- **`@charset`**: aceito e ignorado; todo CSS é lido como UTF-8.
- **`@namespace`**: aceito e ignorado. Para HTML isso equivale ao
  comportamento correto: o namespace padrão não muda nada, e uma regra com
  seletor prefixado (`svg|circle`) é descartada — namespaces de seletor
  não estão implementados, mesmo em SVG inline.
- **`@starting-style { regras }`**: as regras ficam marcadas
  (`tbox_css_ruleset.starting_style`) e não entram na cascata normal. Quando
  um elemento aparece (primeiro frame ou saída de `display: none`) e tem
  `transition`, o estilo resolvido com essas regras
  (`tbox_css_cascade_resolve_starting_style_in_viewport`) é o ponto de
  partida da transição. Vale em todas as propriedades que transicionam
  (ver [Transições e animações](#transições-e-animações)); a forma aninhada
  dentro de uma regra (CSS nesting) não é suportada.
- **`@layer`**: `@layer a, b;`, `@layer a { … }`, nomes com ponto
  (`a.b`), camadas anônimas (`@layer { … }`), aninhamento com `@media` nos
  dois sentidos e `@import … layer(nome)`/`layer`. Um prelúdio inválido
  descarta a regra inteira.
- **`@container [<nome>] <condição>`** (CSS Containment 3): um elemento
  vira contêiner de consulta com `container-type: size | inline-size`
  (`normal` é o inicial), `container-name: <ident>+ | none` ou o atalho
  `container: <nome> [/ <tipo>]` (nenhum herda). A condição usa as
  features `width`, `height`, `inline-size`, `block-size`, `aspect-ratio`
  e `orientation` — forma `nome: valor` (com `min-`/`max-`), booleana
  (`(width)`) e de faixa (`(width > 30em)`, `(400px <= width < 700px)`) —,
  `and`/`or`/`not`, parênteses, `style(--x)`/`style(--x: valor)` (custom
  property computada, comparada como texto aparado) e uma lista com
  vírgula (qualquer uma basta). `@container nome { … }` sem condição vale
  se existe um ancestral com esse nome. A regra consulta o **ancestral**
  contêiner mais próximo que tem o nome (se dado) e o tipo que as features
  pedem (`height`/`block-size`/`aspect-ratio`/`orientation` exigem
  `size`); um `style()` sem nome consulta o pai. Para pseudo-elementos o
  próprio elemento conta. Lógica de três valores: feature desconhecida,
  unidade inválida ou contêiner ausente/não medido é "desconhecido", que
  `not` mantém e a regra trata como falso. `em`/`rem` na condição usam a
  fonte do contêiner. Aninha com `@media`/`@layer`/`@supports` nos dois
  sentidos. Escrita sempre horizontal (inline = largura). Um `@keyframes`
  ou `@font-face` dentro de `@container` não vale (não há elemento para
  decidir). Código: `src/css_cascade/tbox_css_container.c`.

  *O ciclo estilo → layout → estilo.* O tamanho do contêiner vem do
  layout, que depende do estilo. `tbox_context_run_frame` resolve o estilo
  uma vez sem tamanhos (toda consulta de tamanho é falsa, `cq*` usam o
  viewport); se a tabela tem algum contêiner `size`/`inline-size`, faz um
  layout descartável, coleta o content box de cada contêiner e resolve o
  estilo de novo com esses tamanhos (`tbox_style_container_size`), antes
  de transições/animações e do layout final. Uma iteração por frame, sem
  estado entre frames — resize reavalia tudo. A contenção de tamanho torna
  o tamanho do contêiner (quase sempre) independente do que as consultas
  mudam dentro dele: `inline-size` zera a contribuição intrínseca do
  conteúdo na largura (min/max-content), `size` também zera a altura
  automática. Um documento sem contêiner paga só uma varredura da tabela.
  Limites: contêineres aninhados cujo tamanho depende de uma consulta a um
  contêiner externo usam o tamanho da primeira passada; o tamanho medido
  ignora transições/animações em curso.
- **`@font-face`**: arquivos locais via `url()` relativos ao documento (ou ao
  diretório do CSS externo), com `font-family`, um `font-weight` e
  `font-style`. Sem URLs remotas nem faixas de peso.
- **`@supports`**: `not`/`and`/`or` (misturar `and` e `or` sem parênteses
  invalida a regra), `(propriedade: valor)` e `selector()`. É decidido uma
  vez, no parse (`tbox_css_supports`): o que a tbox suporta não muda. Uma
  declaração é verdadeira quando a tbox resolve a **propriedade** (tabela
  em `tbox_css_supports.c`, igual à [referência](referencia/css-suportado.md))
  e custom properties sempre; o valor não é validado, exceto os que
  enganariam a detecção: `display` fora dos valores suportados,
  `transform` com funções 3D (`rotateX/Y`, `rotate3d`, `matrix3d`,
  `translateZ`/`translate3d`, `scaleZ`/`scale3d`, `perspective`: a tbox só
  as achata, então a página deve seguir o caminho 2D; as 2D são
  verdadeiras), `filter`/`backdrop-filter` com
  `url()` e `backdrop-filter` com `drop-shadow()`. `selector()` exige que o seletor compile e só use
  pseudo-classes e pseudo-elementos que o matcher conhece. Outras funções
  (`font-tech()`, `font-format()`) são falsas. Aninha com `@media` e
  `@layer` nos dois sentidos.
- **`@keyframes nome { … }`** (e `@-webkit-keyframes`): `from`, `to` e
  porcentagens, listas (`0%, 50% { … }`) e o nome entre aspas. Um keyframe
  com seletor inválido é descartado sozinho; os nomes `none`/`initial`/
  `inherit`/`unset` descartam a regra. Dentro de `@media`, só vale enquanto
  a condição vale; com dois de mesmo nome, vence o último. Ver
  [Transições e animações](#transições-e-animações).
- **Ignorados sem quebrar o resto**: um `@import` fora do início da folha e
  qualquer at-rule desconhecida; propriedades e valores desconhecidos.

### Escopos e registros de cascata

- **`@scope [(início)] [to (limite)] { regras }`**: seletores de início e
  limite (inclusive listas), limite exclusivo, `:scope`, proximidade depois
  da especificidade e antes da ordem de declaração. Aninha com `@scope`,
  `@layer`, `@media`, `@supports` e `@starting-style`. Um prelúdio inválido
  ou com pseudo-elemento descarta o bloco. A proximidade usa a raiz mais
  próxima cuja instância e seletor casam; escopos externos continuam impondo
  seus limites. Sem início, um `<style>` usa seu pai como raiz; CSS sem nó
  proprietário usa o elemento raiz do documento. Um escopo interno sem
  início reutiliza a raiz externa. Seletores com `:scope` explícito podem
  referenciar ancestrais externos. Sem `&` ou combinador inicial implícito
  (`> p`); escreva `:scope > p`. Permanecem as limitações gerais dos seletores.
- **`@property --nome`**: exige `syntax` e `inherits`; `initial-value` é
  obrigatório salvo com `syntax: '*'`. Aceita um tipo simples: `<length>`,
  `<color>`, `<number>`, `<percentage>`, `<length-percentage>`, `<angle>`,
  `<integer>` ou `*`. Nomes são sensíveis a maiúsculas. `inherits: false`
  bloqueia a herança, inclusive sem declaração local; valores ausentes ou
  inválidos usam o inicial. `inherit`, `initial` e `unset` explícitos são
  respeitados. Comprimentos relativos declarados são computados para px
  antes da herança, cores para RGBA e ângulos para graus. Registros inválidos
  são ignorados e o último válido ativo por nome vence. Sem gramáticas com
  alternativas/multiplicadores, `currentColor` registrado, inicial relativo,
  transição de custom property ou resolução de dependências circulares com
  `font-size`; cálculos seguem o subconjunto existente de valores.
- **`@counter-style nome`**: `cyclic`, `numeric`, `alphabetic`, `symbolic`,
  `additive`, `fixed [inteiro]` e `extends nome`; `symbols`,
  `additive-symbols`, `prefix`, `suffix`, `range`, `pad`, `negative` e
  `fallback`. Usa-se em `list-style-type`, no shorthand `list-style`,
  `counter()` e `counters()`. Prefixo/sufixo só decoram marcadores e pertencem
  ao estilo original mesmo quando a representação vem do `fallback`. Também
  aceita `list-style-type: "texto"` e `symbols([sistema] "a" "b" ...)`.
  As representações são compartilhadas entre Layout e conteúdo gerado.
  Limites: 64 símbolos/pares aditivos, 16 intervalos, inteiros de descritor
  de 32 bits, 4096 repetições/padding, 16 referências encadeadas e 255 bytes
  na expressão de `list-style-type`. Tuplas aditivas e `pad` usam inteiro
  antes do símbolo. `extends` cobre estilos registrados e os estilos
  textuais predefinidos (decimal, letras, romanos, grego e bullets).
  Estilos de contador dentro de media queries são reavaliados por frame;
  ciclos/falhas de representação terminam em fallback decimal.

`@scope` guarda seus limites e o vínculo externo na arena da folha. O matcher
recebe a raiz de `:scope` por argumento, sem estado global; a API de matching
de folhas também respeita os escopos. Folhas internas preservam a associação
entre cada trecho CSS e o pai do `<style>` durante a concatenação.
`@property` e `@counter-style` mantêm registros na folha, separados das
regras de seletores. A cascata acrescenta defaults registrados mesmo quando
nenhum seletor casa. Textos computados são copiados para a arena do frame.
A tabela de estilos copia os descritores de contador para a arena do frame;
a folha pode ser destruída depois de resolver a tabela. Registros emprestados
em `tbox_css_resolved_declaration` vivem até destruir
a folha; os campos novos alteram o tamanho de `tbox_css_ruleset`,
`tbox_css_resolved_declaration` e `tbox_style`, exigindo recompilação.

`@supports selector(:scope)` é verdadeiro. A função `at-rule()` não existe
no parser de supports e continua falsa. Fixtures: `tests/assets/cascade-scope-and-namespace.html`,
`cascade-custom-properties.html` e `generated-counters.html`.

## Seletores

- Tipo, universal, `#id`, `.classe`, atributo (`[a]`, `[a=v]`, `^=`, `$=`,
  `*=`, com modificadores `i`/`s`).
- Combinadores: descendente, `>`, `+`, `~`.
- Estruturais: `:root`, `:first-child`, `:last-child`, `:only-child`,
  `:empty`, `:first-of-type`, `:last-of-type`, `:only-of-type`,
  `:nth-child()`, `:nth-of-type()`, `:nth-last-child()`,
  `:nth-last-of-type()` (com `an+b`, `odd`, `even`).
- Estado: `:hover`, `:active`, `:focus`, `:focus-visible`, `:focus-within`, `:checked`,
  `:disabled`, `:enabled`, `:required`, `:optional`, `:link`/`:any-link`
  (todo link é não visitado), `:lang()`. Em `<option>`, `:checked` é a
  opção escolhida agora no select (não o atributo `selected`) e `:disabled`
  inclui opções de um `<optgroup disabled>`.
- `:not()` com um seletor simples.
- Pseudo-elementos: `::marker`, `::before`, `::after` e `::first-letter`
  (também na forma CSS2 com um `:`); pesam como seletor de tipo na especificidade.
  As partes dos controles de formulário (`::slider-thumb`, `::selection`,
  o funcional `::picker(select)`…) estão em
  [Formulários e controles](10-formularios-e-controles.md); as da barra de
  rolagem (`::-tbox-scrollbar-thumb`, único pseudo-elemento seguido de
  `:hover`/`:active`), em
  [Eventos e interatividade](06-eventos-e-interatividade.md#partes-da-barra).

`::placeholder` estiliza o texto auxiliar de inputs de texto e textarea:
cor, opacidade e estilos de texto, sem alterar o valor ou dimensionar o
controle. Veja [Texto auxiliar dos campos](10-formularios-e-controles.md#texto-auxiliar-dos-campos).

### Conteúdo gerado (`::before`/`::after`)

- **`content`**: strings (com escapes CSS, `"\201C"`), `attr(nome)` (com
  fallback em string), `counter(nome[, estilo])`,
  `counters(nome, "sep"[, estilo])`, `open-quote`/`close-quote`/
  `no-open-quote`/`no-close-quote`; `none`/`normal` não geram caixa;
  `url()` é ignorado (sem imagem gerada); o texto alternativo depois de `/`
  é ignorado. `content: ""` gera uma caixa vazia (barras decorativas).
- **`display`** padrão do pseudo é `inline`; `block`, `inline-block` etc.
  funcionam como num elemento real.
- **Contadores**: `counter-reset`, `counter-increment`, `counter-set` em
  elementos e nos próprios pseudos, com escopos aninhados (o padrão
  `ol { counter-reset } li { counter-increment }` com `counters()`
  funciona); elementos em `display: none` não contam. Estilos de contador:
  os de `list-style-type` (decimal, decimal-leading-zero, romanos, letras,
  grego, disc/circle/square, none), além dos registros `@counter-style`.
  O contador implícito `list-item`
  não está ligado à numeração das listas.
- **`quotes`** (herdado): `auto` (aspas curvas “ ” ‘ ’), `none` ou pares de
  strings; a profundidade de aninhamento corre pelo documento.
- **Onde não há pseudo**: elementos substituídos e vazios (`img`, `input`,
  `select`, `textarea`, `br`, `hr`, `progress`, `meter`…).
- **Interação**: um clique ou hover sobre o conteúdo gerado conta como sobre
  o elemento (`tbox_context_hit_test` devolve a caixa do elemento).
- **Limitações**: pseudos não têm `transition`, `animation` nem `@starting-style`;
  `::first-line` é reconhecido mas não estiliza nada.
- Um seletor não suportado invalida só a própria regra, como no CSS.

### Primeira letra (`::first-letter`)

A primeira letra tipográfica do contexto inline de um bloco pode receber
fonte, cor, margem, padding, borda, fundo e `float`. A seleção inclui
pontuação precedente (categorias Unicode P*) e marcas combinantes da
letra; atravessa fronteiras de texto/inline e espaços entre a pontuação e
a letra. O DOM permanece intacto. Sem `float`, a caixa participa da linha;
com `float`, as linhas desviam da sua margin box. O estilo herda do inline
que contém o primeiro texto. Não exige `content`.

`initial-letter: normal | <number> [<integer> | drop | raise]?` transforma
a letra em capitular; `@supports` valida esta sintaxe e a de
`hanging-punctuation`. Valores literais inválidos são ignorados antes da
cascata, preservando declarações válidas anteriores. Veja
[capitulares e pontuação](08-layout-e-pintura.md#capitulares-e-pontuação-pendurada)
e o fixture `text-initial-letter.html`.

## Valores

A resolução das propriedades fica em `src/style/tbox_style.c`. Os parsers de
funções de cálculo, gradientes, filtros, formas, transformações e tempo ficam
em `tbox_style_functions.c`, apoiados pelos utilitários de valores em
`tbox_style_values.c`. Cores, substituições e conteúdo gerado mantêm seus
arquivos especializados; veja [Organização do código](dev/organizacao-do-codigo.md).

- **Unidades**: `px`, `em`, `rem`, `ex`/`ch` (altura-x e avanço do "0" da face regular da `font-family`, medidos pelo contexto; 0,5em sem fontes), `pt`,
  `pc`, `in`, `cm`, `mm`, `Q`, `%`, `vw`/`vh`/`vmin`/`vmax` (e as variantes
  `sv*`/`lv*`/`dv*`, iguais ao tamanho da janela), `cqw`/`cqh`/`cqi`/`cqb`/
  `cqmin`/`cqmax` (1% do content box do contêiner de consulta mais próximo
  naquele eixo — `inline-size` ou `size` para o eixo inline, só `size` para
  o de bloco; sem contêiner medido, caem nas `sv*`).
- **Funções**: `calc()` (`+ - * /`, parênteses, aninhado), `min()`, `max()`,
  `clamp()`, `var()` com fallback (custom properties `--nome`, herdadas por
  padrão; registros `@property` podem desativar a herança).
  Comparações aceitam percentuais puros (`min(20%, 50%)`) e expressões com
  o mesmo termo em pixels ou percentual; uma chamada de `min()`/`max()`
  com argumento único preserva seu valor. Misturas gerais continuam
  limitadas aos casos descritos na referência. `pow()`, `sqrt()`, `exp()` e
  `log()` avaliam números sem unidade dentro de cálculos de comprimento,
  como `calc(pow(2, 3) * 10px)`; domínios inválidos e valores não finitos
  são rejeitados. `hypot()` aceita até oito números ou comprimentos com
  unidades compatíveis. `abs()` preserva o tipo do número/comprimento e
  `sign()` retorna -1, zero ou 1. As constantes `pi`/`e` podem ser usadas
  dentro de cálculos, como `calc(pi * 10px)`. `round()`, `mod()` e
  `rem()` aceitam números ou comprimentos resolvidos em pixels. `round()`
  oferece `nearest` (padrão, empate para cima), `up`, `down` e `to-zero`.
  `mod()` segue o sinal do divisor e `rem()` o do dividendo; percentuais e
  limites pendentes nessas operações ficam de fora. `sin()`/`cos()`/`tan()`
  recebem expressão numérica em radianos, como `sin(pi / 2)`, ou um ângulo
  em `deg`/`rad`/`grad`/`turn`, como `cos(60deg)`, inclusive expressões
  angulares como `sin(45deg + 45deg)`. `asin()`/`acos()`/`atan()` retornam
  ângulos; `atan2(y, x)` aceita dois números, comprimentos resolvidos ou
  ângulos de tipos iguais. Os resultados podem alimentar trigonometria,
  gradientes lineares e `hue-rotate()`. Valores não finitos, domínios
  inválidos e percentuais de comprimento pendentes são rejeitados.
  Veja o [catálogo por custo](referencia/funcoes-css-pendentes.md).
- **Gradientes radiais**: aceitam raio explícito de círculo (`circle 40px`)
  ou dois raios de elipse (`ellipse 80px 40px`), inclusive percentuais por
  eixo na elipse. Isso também vale para `repeating-radial-gradient()`;
  a forma omitida é inferida pelo número de raios.
- **Gradientes cônicos**: `conic-gradient()` e `repeating-conic-gradient()`
  aceitam `from <angle>`, `at <position>` e stops angulares/percentuais,
  inclusive duas posições por cor. Reutilizam fundos em camadas, máscaras e
  dimensionamento/repetição de imagem; interpolação em sRGB premultiplicado,
  até oito stops, sem hints ou seleção de espaço de cor.
- **Polígonos**: `clip-path: polygon(...)` aceita três a 32 vértices em
  comprimentos/percentuais e regra `nonzero` (padrão) ou `evenodd`. Recorta
  descendentes e barras de rolagem, com o mesmo interior no hit-test.
  Sem `round`, caixa de referência explícita ou antialiasing nas arestas.
- **Opacidade calculada**: `opacity` usa o mesmo avaliador e aceita funções
  numéricas diretamente, como `sqrt(.25)`, ou dentro de `calc()`. Percentuais
  viram números antes da aritmética (100% = 1): `calc(50% + .25)` resulta em
  0,75. O valor final é limitado a 0–1; resultados em unidades de comprimento
  e valores não finitos são rejeitados. Outras propriedades numéricas ainda
  não passam por esse avaliador.
- **Palavras-chave globais**: `inherit`, `initial`, `unset` (`revert` age
  como `unset`), também via `all`, que as aplica a toda propriedade que a
  tbox resolve (custom properties ficam de fora); uma declaração de
  prioridade maior de uma propriedade específica ainda vence. O `display`
  inicial é `inline`, como no CSS.
- **Cores**: nomes CSS, hexadecimal (`#rgb`, `#rrggbb`), `rgb()`/`hsl()` nas duas
  sintaxes, `hwb()`, `lab()`, `lch()`, `oklab()`, `oklch()`,
  `color(srgb ...)`/`color(srgb-linear ...)`, `color-mix()`
  (em OKLCH: `shorter hue`, `longer hue`, `increasing hue`, `decreasing hue`;
  nomes de espaços desconhecidos e pesos fora de 0%–100% são rejeitados),
  `transparent`, `currentColor`, `light-dark()` (escolhe pelo
  `color-scheme` herdado: escuro só com `dark` sem `light`, já que o
  ambiente prefere claro; vale em qualquer propriedade, inclusive vindo de
  `var()`). `color-scheme` não muda as cores da UA (controles, barras).
- **Herança e valores iniciais** seguem o CSS: `color`, fonte, `text-align`
  etc. herdam; `background`, `border`, `margin` não.

## Transições e animações

As duas rodam na orquestração (`tbox_context_transition.c`,
`tbox_context_animation.c`) sobre os estilos já resolvidos do frame e antes
do layout, então propriedades de layout (`width`, `padding`…) animam
refazendo o layout a cada frame. O relógio é
`tbox_context_set_animation_time`; enquanto alguma estiver ativa,
`tbox_context_animations_active` é verdadeiro, e
`tbox_context_next_frame_time` diz quando o próximo frame muda algo (no
atraso inicial de uma `transition-delay`/`animation-delay`, o fim do atraso).

- **Propriedades que interpolam** (`tbox_style_animatable`): `color`,
  `background-color`, `opacity`, `border-*-color`, `border-*-width`,
  `border-*-radius`, `outline-color`/`-width`/`-offset`,
  `text-decoration-color`, `accent-color`, `caret-color`, `translate`,
  `rotate`, `scale`, `transform-origin` e `transform` (função a função,
  ver abaixo), `width`/`height` (e `inline-size`/
  `block-size`), `padding-*`, `margin-*`, `top`/`right`/`bottom`/`left`
  (`inset`), `letter-spacing`, `word-spacing`; os shorthands (`border`,
  `border-color`, `padding`, `outline`…) valem pelos longhands. Cores
  interpolam em sRGB pré-multiplicado. Um comprimento que troca de tipo
  (px ↔ % ↔ `auto`) ou um `accent-color`/`caret-color` `auto` muda de uma
  vez. `transform` interpola posição a posição da lista: funções do mesmo
  tipo (`translateX` e `translate` são o mesmo; `rotateZ` é `rotate`)
  misturam os argumentos, uma posição vazia (lista mais curta, `none`) vale
  a identidade da outra, e tipos diferentes na mesma posição trocam de uma
  vez no meio — não há interpolação por decomposição de matriz. `rotate`
  interpola o ângulo quando o eixo é o mesmo.
- **Funções de tempo**: `ease`, `linear`, `ease-in`, `ease-out`,
  `ease-in-out`, `cubic-bezier()`, `steps(n[, jump-start | jump-end |
  jump-none | jump-both | start | end])`, `step-start`, `step-end`. A forma
  `linear()` com paradas não é suportada.
- **`transition`**: o shorthand com várias entradas e os longhands
  (`-property`, `-duration`, `-delay`, `-timing-function`), cada um uma
  lista pareada pelo índice com `transition-property` e repetida quando é
  mais curta. Com duas entradas para a mesma propriedade, vale a última. Uma
  mudança no meio de uma transição recomeça do valor atual.
- **`animation`**: shorthand (palavras-chave em qualquer ordem, o nome por
  último) e os longhands `animation-name`, `-duration`, `-delay`,
  `-timing-function`, `-iteration-count` (inclusive `infinite` e
  fracionário), `-direction`, `-fill-mode`, `-play-state`, pareados com
  `animation-name`. `animation-timing-function` dentro de um keyframe vale
  para o trecho que começa nele; `!important` dentro de keyframes é
  ignorado. Keyframes de 0%/100% ausentes usam o valor atual do elemento.
  Uma animação recomeça quando o nome sai e volta para a lista do elemento
  (ou o elemento sai de `display: none`). Com várias animações na mesma
  propriedade, vence a última da lista.
- **Limitações**: os valores de cada keyframe são resolvidos quando a
  animação começa (`em`, `%` e `currentColor` ficam fixos dali em diante);
  não há `animation-composition`, `animation-timeline` nem eventos
  (`animationend`); pseudo-elementos não animam. Com
  `prefers_reduced_motion` as animações continuam: use
  `@media (prefers-reduced-motion: reduce)` como na web.

## Propriedades aceitas sem efeito

Não fazem nada numa tela sem paginação e sem 3D, e isso é o
resultado correto para o que a tbox desenha: `break-before`/`-after`/
`-inside` fora de multicoluna, `page-break-*`, `orphans`, `widows` (impressão), `overflow-anchor` (não há ancoragem de rolagem, o mesmo que
`none`), `perspective`, `perspective-origin`, `transform-style` e
`backface-visibility` (sem transformações 3D, a face de trás nunca
aparece). As de impressão contam como suportadas em `@supports`; as de 3D
não, para não levar uma página ao seu caminho 3D.

Em multicolunas, `break-before/after: column` e
`break-inside: avoid | avoid-column` passam a controlar fragmentação; veja
[Multicolunas](08-layout-e-pintura.md#multicolunas). Fora desse contexto,
continuam sem efeito. `orphans`/`widows` ainda não restringem as quebras.

`marker`, `marker-start`, `marker-mid` e `marker-end` aceitam `none` ou
`url()` e são herdados. O shorthand aplica o mesmo valor aos três pontos,
respeitando a prioridade dos longhands. As URLs são guardadas (até 255 bytes
UTF-8 por referência), sem carregar recursos. `mask-type` aceita `luminance`
(inicial) ou `alpha` e não é herdado. CSS inválido é ignorado; os keywords
CSS-wide funcionam como nas outras propriedades. Continuam falsos em
`@supports`: o subconjunto SVG não desenha marcadores
nem máscaras SVG.
`paint-order`, ao contrário, tem efeito sobre texto HTML com contorno;
veja [Texto e fontes](09-texto-e-fontes.md#contorno-e-ordem-de-pintura-do-texto).

`@page` é lido e descartado como um bloco balanceado, inclusive seletores
como `:first` e at-rules de margem aninhadas (`@top-left`, etc.). As regras
seguintes continuam sendo processadas. Não há paginação nem suporte à função
`at-rule()` em `@supports` (ela continua falsa). Fixture: `tests/assets/text-stroke.html`.

## O que fica de fora

Os grandes ausentes, para decidir rápido: transformações
3D de verdade (as funções 3D são achatadas, ver
[Layout e pintura](08-layout-e-pintura.md#transformações)). A lista completa
por propriedade e função (incluindo limitações e referências ao código) está em [CSS suportado](referencia/css-suportado.md)
e [HTML suportado](referencia/html-suportado.md); o que existe de
layout está em [Layout e pintura](08-layout-e-pintura.md).
