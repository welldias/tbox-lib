# tbox — Tarefas da v8

Quebra da seção "v8 — Marcadores de lista + `em` em propriedades além de
`font-size`" do `ARCHITECTURE.md` em tarefas executáveis por agentes sem
contexto desta conversa. Cada tarefa abaixo é auto-contida: aponta para a
subseção exata do `ARCHITECTURE.md` (a fonte da verdade de *o quê*
construir) e acrescenta só o que esse documento não cobre — caminho de
arquivo, como registrar teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v7, que está completa e
commitada — ver `ARCHITECTURE.md` para o design de cada camada v0-v8 e
`git log -- TASKS.md` para recuperar quebras de tarefas anteriores, se
precisar consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL nova
além das que o `ARCHITECTURE.md` já especifica explicitamente pra ela**
(ver o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`).
A v8 não especifica nenhum global/estático mutável novo — se a
implementação de alguma tarefa parecer precisar de um, **pare e reporte
isso no relatório final da tarefa em vez de adicionar por conta própria**.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. As Tarefas 1, 2 e 3 abaixo não
compartilham NENHUM arquivo entre si (módulos diferentes: `style`,
`context`, `layout`) — rodam as três em paralelo sem risco de conflito de
merge. Nenhuma tarefa cria grupo de teste novo nesta versão (estende os
grupos já registrados `style`, `context` e `layout`).

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

A v8 muda uma assinatura INTERNA (`tbox_style_parse_length`,
`tbox_style_resolve_length_property`, `tbox_style_resolve_box_shorthand` —
todas `static`, sem uso fora de `src/style/tbox_style.c`) e ACRESCENTA dois
campos a uma struct pública já existente (`tbox_ua_style_config`, ver
Tarefa 2) — isso não quebra nenhum código existente que já monta essa
struct por nome de campo (`tbox_ua_style_config_default()` é a única
fábrica, e ela mesma é quem a Tarefa 2 atualiza). Nenhuma outra assinatura
pública muda.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Style: `em` em `width`/`height`/`margin`/`padding`/offsets
**Depende de:** nada. **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v8 — Marcadores de lista..."
→ "Escopo" (por que `em` resolve contra o font-size do PRÓPRIO nó, não o
do pai — diferente de `font-size: em`) e "Style — `em` em
`width`/`height`/`margin`/`padding`/offsets" INTEIRA (assinaturas exatas,
onde `font_size` precisa ser calculado mais cedo em `tbox_style_resolve`,
e o "Fora de escopo" — `border-width`, `rem`). `src/style/tbox_style.c`
inteiro (arquivo pequeno) — em especial `tbox_style_parse_length` (a
função que ganha o parâmetro novo), `tbox_style_resolve_font_size` (mesmo
padrão de checagem de sufixo `"em"` case-insensitive a copiar),
`tbox_style_resolve_length_property`, `tbox_style_resolve_box_shorthand`
(as duas chamadoras) e `tbox_style_resolve` (onde `font_size` precisa
subir pra antes de `width`/`margin`/`padding`/`offset`).

**Arquivos a editar:**
- `src/style/tbox_style.c`:
  1. `tbox_style_parse_length(tbox_string_view raw, tbox_style_length *out)`
     vira `tbox_style_parse_length(tbox_string_view raw, double font_size,
     tbox_style_length *out)` — um sufixo `"em"` (case-insensitive, 2
     bytes, mesma checagem que `tbox_style_resolve_font_size` já faz pro
     mesmo sufixo) parseia o número antes dele e escreve `out->kind =
     TBOX_STYLE_LENGTH_PX; out->value = font_size * number;`. As
     checagens de `"auto"`/`"%"`/`"px"` já existentes não mudam.
  2. `tbox_style_resolve_length_property`/`tbox_style_resolve_box_shorthand`
     ganham o mesmo parâmetro `double font_size`, só repassado pra
     `tbox_style_parse_length`.
  3. Em `tbox_style_resolve`: mova o cálculo de `style.font_size` (hoje
     depois de `width`/`margin`/`padding`) pra ANTES dessas quatro
     chamadas (`width`, `height`, `margin` shorthand, `padding` shorthand)
     — e também antes de `offset[0..3]` (`top`/`right`/`bottom`/`left`),
     que também passam a receber `style.font_size`. Nenhum cálculo em si
     muda, só a ORDEM dentro da função.
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado,
  mesmo padrão de `resolve_node`/`TBOX_TEST_ASSERT` já usado no arquivo):
  - `<div style_com_font_size_diferente>` — um `div { font-size: 20px;
    width: 2em; }` deve resolver `style.width.value == 40.0` (2 × 20px do
    PRÓPRIO nó, não os 16px default nem o font-size do pai);
  - um filho com `font-size` diferente do pai e `margin: 1.5em;` — o
    `margin` resolvido usa o font-size do FILHO, não o do pai (monte um
    `<div><p>x</p></div>` com `div { font-size: 10px; } p { font-size:
    30px; margin: 1em; }` e confirme `child_style.margin[0].value ==
    30.0`, não `10.0`);
  - regressão: `width: 50%`/`margin: 10px` continuam resolvendo
    exatamente como antes (kind `PERCENT`/`PX`, valores inalterados) —
    prova de que adicionar `em` não quebrou os dois casos já suportados;
  - um valor sem unidade reconhecida (ex. `width: 2foo;`) continua
    falhando a parsear (cai no valor inicial), mesmo comportamento de
    antes.

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

### Tarefa 2 — CSS Cascade/Orchestration: UA stylesheet de `<ul>`/`<ol>`/`<li>`
**Depende de:** nada. **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v8 — Marcadores de lista..."
→ "CSS Cascade / Orchestration — UA stylesheet de `<ul>`/`<ol>`/`<li>`"
INTEIRA (os dois campos novos de `tbox_ua_style_config`, os valores
default, o template CSS exato — em especial POR QUE `padding-left` é
escrito como o shorthand `padding: 0 0 0 N` e não a longhand isolada).
`include/tbox/context.h` — `tbox_ua_style_margin_config`/
`tbox_ua_style_config` (as structs que ganham campo novo) e
`tbox_ua_style_config_default`'s doc comment (que precisa descrever os
valores novos). `src/context/tbox_context.c` —
`tbox_ua_style_config_default` (preenche os campos novos) e
`tbox_ua_style_generate_css` (o template `snprintf`, incluindo o comentário
`TBOX_UA_STYLE_CSS_BUFFER_SIZE` logo acima — confirme se o buffer de 1024
bytes ainda é suficiente com as duas linhas novas; se não for, aumente o
`#define`, documentando por quê, não silenciosamente).

**Arquivos a editar:**
- `include/tbox/context.h`:
  - `tbox_ua_style_margin_config` ganha `double list_px;` (margin
    top/bottom de `<ul>`/`<ol>`, mesma unidade que `paragraph_px`).
  - `tbox_ua_style_config` ganha `double list_padding_left_px;` (campo
    direto na struct externa, NÃO dentro de `margin` — é padding, não
    margin, ver o "porquê" no ARCHITECTURE.md).
- `src/context/tbox_context.c`:
  - `tbox_ua_style_config_default()`: `config.margin.list_px = 16.0;`
    (mesmo valor de `paragraph_px`) e `config.list_padding_left_px =
    40.0;` (valor clássico de indentação de lista de qualquer browser
    real).
  - `tbox_ua_style_generate_css`: adiciona ao template (via `snprintf`,
    mesmo padrão de toda linha já existente)
    `"ul, ol { display: block; margin: %gpx 0px; padding: 0px 0px 0px %gpx; }\n"`
    e `"li { display: block; }\n"` (a segunda é redundante com o valor
    inicial de `display`, mas seguindo a mesma convenção já usada por
    `div { display: block; }` no template) — passe `config.margin.list_px`
    e `config.list_padding_left_px` nos dois `%g` da primeira linha, na
    ordem em que aparecem no template.
- `tests/context/test_context.c` — casos novos (grupo `context` já
  registrado, mesmo padrão de `tbox_ua_style_config_default()` +
  `tbox_context_hit_test`/inspeção de `content_box`/`margin_box` já usado
  no arquivo, ver os testes que usam `default_config.margin.heading_px[0]`
  como referência):
  - um documento com `<ul><li>x</li></ul>` (sem CSS de autor) tem a caixa
    do `<ul>` com `margin_box`/`content_box` refletindo
    `default_config.margin.list_px` de margem vertical E
    `default_config.list_padding_left_px` de recuo horizontal do
    `content_box` em relação ao `border_box` (mesmo tipo de asserção
    geométrica que os testes de `paragraph_px`/`heading_px` já fazem para
    parágrafos/headings);
  - regressão: um `<p>`/`<h1>` continuam com a mesma margem de antes (a
    mudança no buffer/template não pode ter introduzido nenhuma diferença
    de espaçamento nos elementos que já existiam).

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão.

### Tarefa 3 — Layout Tree: marcador de `<li>`
**Depende de:** nada. **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v8 — Marcadores de lista..."
→ "Escopo" (bullet pra `<ul>`, número pra `<ol>`, decidido pelo PAI DIRETO
do `<li>` — não propriedade CSS; um `<li>` fora de `<ul>`/`<ol>` não ganha
marcador) e "Layout Tree — marcador de `<li>`" INTEIRA (assinatura exata,
onde a função é chamada dentro de `tbox_layout_build_text_runs`, como
contar os `<li>` irmãos pra `<ol>`, por que usa `tbox_layout_push_words`
em vez de manipular o vetor de palavras diretamente, e a mudança de
comportamento pra `<li>` vazio — `text_run_count` passa de 0 pra 1).
`src/layout/tbox_layout.c` inteiro — em especial `tbox_layout_push_words`
(a função que o marcador reaproveita), `tbox_layout_collect_words`,
`tbox_layout_build_text_runs` (onde a chamada nova entra, logo depois de
`tbox_vector_init(&words, ...)`), e a struct `tbox_html_node` em
`include/tbox/html_parser.h` (campo `parent`, usado pra achar o `<ul>`/
`<ol>` e contar irmãos `<li>`).

**Arquivos a editar:**
- `src/layout/tbox_layout.c`:
  - Inclua `<stdio.h>` (pra `snprintf`) e `<string.h>` (pra `memcpy`) no
    topo do arquivo (hoje só inclui `<stddef.h>` + os headers de
    base/arena/string/vector).
  - Nova função `static void tbox_layout_push_list_marker(tbox_arena
    *arena, const tbox_html_node *node, const tbox_style *style,
    tbox_font_face_cache *fonts, tbox_vector *words)` — ver a assinatura e
    o algoritmo completo no ARCHITECTURE.md (bullet U+2022 = bytes `0xE2
    0x80 0xA2` em UTF-8; contagem de `<li>` irmãos via `node->parent->first_child`
    +`next_sibling` até achar `node`; número formatado via `snprintf` num
    buffer de pilha pequeno, copiado pra dentro de `arena` via
    `tbox_arena_alloc` antes de virar um `tbox_string_view` — a palavra
    precisa apontar pra memória que sobrevive além desta chamada). Chama
    `tbox_layout_push_words` internamente pra empurrar o marcador — NÃO
    duplique a lógica de medição de largura (`tbox_font_measure_text`)
    que `tbox_layout_push_words` já faz.
  - Em `tbox_layout_build_text_runs`: chame
    `tbox_layout_push_list_marker(arena, node, style, fonts, &words);`
    logo depois de `tbox_vector_init(&words, arena, sizeof(tbox_layout_word), 0);`
    e ANTES de `tbox_layout_collect_words(...)` — o marcador precisa ser a
    PRIMEIRA palavra.
- `tests/layout/test_layout.c` — casos novos (grupo `layout` já
  registrado, mesmo padrão dos testes 29-31 da v7 — navegação de
  `tbox_layout_box` via `first_child`/`next_sibling`, inspeção de
  `text_run_count`/`text_runs[i].text`):
  - `<ul><li>um</li></ul>` → o `text_runs[0].text` do `<li>` começa com o
    bullet (`"\xE2\x80\xA2 um"` ou equivalente, dependendo de como a
    quebra de linha juntou marcador+palavra no mesmo run — confira o
    comportamento real em vez de assumir);
  - `<ol><li>um</li><li>dois</li><li>três</li></ol>` → os três `<li>`
    mostram `"1."`, `"2."`, `"3."` (nessa ordem) antes do próprio texto —
    NÃO reinicia a contagem, incrementa por `<li>` irmão em ordem de
    documento;
  - `<ul><li></li></ul>` (item vazio) → `text_run_count == 1` (só o
    marcador) — regressão proposital do comportamento antigo (que dava 0
    pra um text tag sem palavra nenhuma);
  - regressão: `<p>texto</p>`/`<h1>texto</h1>` continuam SEM marcador
    nenhum (`<li>` é o único tag afetado) — o texto começa exatamente como
    antes, sem bullet/número extra;
  - regressão: `<li>solto</li>` (sem `<ul>`/`<ol>` como pai direto — ex.
    `<div><li>solto</li></div>`) NÃO ganha marcador nenhum — o texto do
    `text_runs[0]` é só `"solto"`, sem prefixo.

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — fatia vertical v8

### Tarefa 4 — Fatia vertical v8 completa (exemplo + validação)
**Depende de:** Tarefas 1, 2 e 3 (todas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v8 — Marcadores de lista..."
→ "Fatia vertical v8 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v7, sem remover nada):
   - A lista `<ul>` já existente da v6/v7 (`.v6-li-one/-two/-three`)
     ganha bullets de verdade agora — nenhuma mudança de HTML necessária
     nela, mas atualize o comentário HTML acima dela (que hoje já foi
     atualizado pela v7 pra dizer "o texto renderiza" — acrescente que
     agora também mostra o bullet "•" antes do texto de cada item).
   - Adicione uma lista NUMERADA nova (`<ol><li>...`), sem fechamento
     explícito de `</li>` (reaproveitando o fechamento implícito da v6),
     com uma classe CSS nova (ex. `.v8-ol-one/-two/-three`, mesmo padrão
     de cores contrastantes que `.v6-li-one/-two/-three` já usa) —
     mostrando "1.", "2.", "3." antes do texto de cada item.
   - Adicione um elemento novo (ex. um `<p>` ou `<div>`, com uma classe
     CSS nova tipo `.v8-em-margin`) cujo CSS de autor declare um
     `font-size` PRÓPRIO diferente do herdado (ex. `font-size: 20px;`) E
     um `margin`/`padding` em `em` (ex. `margin: 2em;`) — visualmente, o
     espaçamento ao redor desse elemento deve ser 2× o `font-size` DELE
     (40px), não 2× o font-size do pai nem o valor herdado.
2. Nenhuma mudança de código deveria ser necessária em `example/tbox_app.c`
   além de possivelmente `TBOX_APP_DEMO_HEIGHT` (mesmo padrão de todo
   incremento anterior — v2/v3/v5/v7 todos precisaram bumpar essa
   constante conforme o fixture acumulado cresce; confira visualmente via
   `--screenshot` antes de mudar o número, não invente um valor).
3. Validação: **prefira `--screenshot` (novo na v7) a `grim`/`swaymsg`** —
   `./tbox_app_demo --screenshot <path>.png` renderiza offscreen, sem
   depender de compositor/janela nenhuma (ver ARCHITECTURE.md's seção
   "Ferramentas de desenvolvimento — captura de tela headless" pra
   detalhes; se por algum motivo essa flag não estiver disponível no
   binário buildado, caia pro fluxo antigo — abrir a janela de verdade com
   `TBOX_WAYLAND_CLOSE_DELAY_MS` e `grim`, SEM usar `swaymsg` pra forçar
   floating/mover a janela, já que isso deixou um efeito colateral
   permanente na sessão do compositor numa rodada anterior — ver o
   histórico desta conversa se precisar do contexto completo). De um jeito
   ou de outro: `cmake -S . -B build && cmake --build build && ctest
   --test-dir build` tudo verde primeiro, depois confira visualmente na
   imagem gerada: bullets aparecem na lista `<ul>`, números sequenciais
   aparecem na lista `<ol>`, e o elemento com `margin: 2em` mostra
   espaçamento visivelmente proporcional ao SEU PRÓPRIO tamanho de fonte
   (maior que os 2em de um elemento com font-size menor ao lado, se a
   demo tiver os dois pra comparar).

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação visual acima confirma os três
itens (bullets, números, `em` próprio) sem erro — este é o "pronto" da v8
inteira, não só desta tarefa.
