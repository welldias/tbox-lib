# tbox — Tarefas da v11

Quebra da seção "v11 — `<hr>`, `<br>`, `<pre>` e `text-align`" do
`ARCHITECTURE.md` em tarefas executáveis por agentes sem contexto desta
conversa. Cada tarefa abaixo é auto-contida: aponta para a subseção exata
do `ARCHITECTURE.md` (a fonte da verdade de *o quê* construir) e
acrescenta só o que esse documento não cobre — caminho de arquivo, como
registrar teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v10, que está completa —
ver `ARCHITECTURE.md` para o design de cada camada v0-v11 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL nova
além das que o `ARCHITECTURE.md` já especifica explicitamente pra ela**
(ver o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`).
A v11 não especifica nenhum global/estático mutável novo — se a
implementação de alguma tarefa parecer precisar de um, **pare e reporte
isso no relatório final da tarefa em vez de adicionar por conta própria**.

**`font-family` NÃO entra nesta versão** (adiado pra v12, decisão do
mantenedor) — nenhuma tarefa abaixo deve tentar resolver fontes por nome
nem mudar `tbox_font_face_cache`/`tbox_font_source_*`. `<pre>` usa a mesma
fonte sans-serif de sempre.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**As Tarefas 1 e 2 abaixo não compartilham NENHUM arquivo** (módulos
diferentes: `style` vs. `context`) — rodam em paralelo sem risco de
conflito de merge. **A Tarefa 3 depende da Tarefa 1** (precisa do campo
`text_align` que a Tarefa 1 acrescenta a `tbox_style` pra compilar) — só
pode começar depois dela mergeada. A Tarefa 3 NÃO depende da Tarefa 2
(arquivos completamente diferentes: `tbox_layout.c` vs. `tbox_context.c`).

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Style: propriedade `text-align`
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v11 — `<hr>`, `<br>`,
`<pre>` e `text-align`" → "Escopo" (por que sem `justify`) e "Style —
`text-align`" INTEIRA (o enum exato, a inheritance, o parsing
case-insensitive). `include/tbox/style.h` — a struct `tbox_style` (onde o
campo novo entra, ao lado de `font_weight_bold` que já é o exemplo mais
próximo de "propriedade booleana/enum simples, herdável, com fallback pra
não-reconhecido") e o comentário "Scope" logo abaixo de
`tbox_style_resolve` (que precisa ser atualizado pra listar `text-align`
como suportado, tirando-o implicitamente de "fora de escopo" — hoje ele
não é mencionado nem como suportado nem como fora de escopo nesse
comentário específico, mas confira se `white-space`/outros continuam
corretos). `src/style/tbox_style.c` — `tbox_style_resolve` inteiro (em
especial como `font-weight` já trata herança + fallback pra
não-reconhecido, é o padrão a replicar) e `tbox_style_parse_display`
(mesmo padrão de função pequena com `if`/`else if` por palavra-chave
case-insensitive, pra `tbox_style_parse_text_align` seguir o mesmo
estilo).

**Arquivos a editar:**
- `include/tbox/style.h`:
  1. Novo enum, antes de `typedef struct tbox_style`:
     ```c
     typedef enum tbox_style_text_align {
         TBOX_STYLE_TEXT_ALIGN_LEFT, /* initial */
         TBOX_STYLE_TEXT_ALIGN_CENTER,
         TBOX_STYLE_TEXT_ALIGN_RIGHT,
     } tbox_style_text_align;
     ```
  2. `tbox_style` ganha `tbox_style_text_align text_align;` — comentário
     ao lado documentando que é herdável, igual `color`/`font_weight_bold`.
  3. Atualize a doc comment de "Scope" de `tbox_style_resolve` pra
     mencionar `text-align` (`left`/`center`/`right`, sem `justify`) como
     suportado nesta versão.
- `src/style/tbox_style.c`:
  1. Nova função `static bool tbox_style_parse_text_align(tbox_string_view raw, tbox_style_text_align *out)`
     — reconhece `"left"`/`"center"`/`"right"` case-insensitive (mesma
     função de comparação que `tbox_style_parse_display` já usa pra
     `"block"`/`"inline"`/`"none"`), retorna `false` pra qualquer outro
     valor (incluindo `"justify"`).
  2. Em `tbox_style_resolve`: resolve `text-align` com o MESMO padrão de
     herança que `font-weight` já usa — se a declaração existe e
     `tbox_style_parse_text_align` reconhece o valor, usa o valor
     parseado; senão, herda `parent_style->text_align` se houver pai;
     senão, `TBOX_STYLE_TEXT_ALIGN_LEFT` (o valor inicial, que também é
     `0` — o primeiro membro do enum, então nem precisa de
     inicialização explícita se a struct já for zero-inicializada em
     algum lugar, mas siga o padrão explícito que `font_weight_bold` já
     usa, não confie em zero-init implícito).
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado,
  mesmo padrão `resolve_node`/`TBOX_TEST_ASSERT` já usado no arquivo):
  - `div { text-align: center; }` resolve `style.text_align ==
    TBOX_STYLE_TEXT_ALIGN_CENTER`;
  - `div { text-align: RIGHT; }` (maiúsculas) resolve `RIGHT` — confirma
    case-insensitive;
  - um `<div><p>x</p></div>` com `div { text-align: center; }` e `p` SEM
    `text-align` próprio — o `p` herda `CENTER` do pai (mesmo padrão do
    teste de herança de `color`/`font-weight` já existente no arquivo,
    procure por eles como referência);
  - `div { text-align: justify; }` cai no valor herdado/inicial (`LEFT`
    sem pai) — `justify` não é reconhecido, mesmo tratamento que qualquer
    valor não suportado já recebe;
  - regressão: um `div` sem `text-align` nenhum, sem pai, resolve `LEFT`
    (o valor inicial).

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

### Tarefa 2 — Orchestration: UA stylesheet de `<hr>`
**Depende de:** nada. **Bloqueia:** nada (a fatia vertical, Tarefa 4,
depende dela, mas nenhuma outra tarefa desta versão).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v11 — `<hr>`, `<br>`,
`<pre>` e `text-align`" → "Escopo" (por que `background-color` em vez de
`border`) e "Orchestration — UA stylesheet de `<hr>`" INTEIRA (os campos
novos exatos, os valores default, a linha de template). `include/tbox/context.h`
— `tbox_ua_style_config`/`tbox_ua_style_margin_config` (as structs que
ganham campo novo — mesmo padrão que `list_px`/`list_padding_left_px` já
adicionaram na v8, veja como esses foram documentados como referência
direta de estilo). `src/context/tbox_context.c` —
`tbox_ua_style_config_default` e `tbox_ua_style_generate_css` (o template
`snprintf` — confira se `TBOX_UA_STYLE_CSS_BUFFER_SIZE` ainda é
suficiente com mais uma linha e dois `%g`; se não for, aumente o
`#define`, documentando por quê, não silenciosamente — mesma checagem
que a v8 já fez quando adicionou as linhas de `ul`/`ol`/`li`).

**Arquivos a editar:**
- `include/tbox/context.h`:
  - `tbox_ua_style_margin_config` ganha `double hr_px;` (margin vertical
    de `<hr>`, mesmo padrão de `list_px`).
  - `tbox_ua_style_config` ganha `double hr_height_px;` (campo direto na
    struct, igual `list_padding_left_px` — não é margin).
  - Atualize os comentários de doc das duas structs e de
    `tbox_ua_style_config_default` pra descrever os campos novos e seus
    valores default.
- `src/context/tbox_context.c`:
  - `tbox_ua_style_config_default()`: `config.margin.hr_px = 8.0;`
    (~0.5em num base_px de 16px) e `config.hr_height_px = 2.0;`.
  - `tbox_ua_style_generate_css`: adiciona ao template
    `"hr { display: block; height: %gpx; background-color: gray; margin: %gpx 0px; }\n"`,
    passando `config.hr_height_px` e `config.margin.hr_px` nessa ordem.
    `"gray"` já é uma cor nomeada suportada (case-insensitive) — não
    precisa mudar nada na Style layer/CSS Cascade pra isso funcionar.
- `tests/context/test_context.c` — casos novos (grupo `context` já
  registrado, mesmo padrão de `default_config.margin.list_px`/
  `list_padding_left_px` que a v8 já usa como referência de asserção
  geométrica):
  - um documento com `<hr>` sozinho (sem CSS de autor) — a caixa do `<hr>`
    tem `margin_box`/`content_box` refletindo `default_config.hr_height_px`
    de altura e `default_config.margin.hr_px` de margem vertical (mesmo
    tipo de asserção geométrica que os testes de `list_px`/`paragraph_px`
    já fazem);
  - a cor de fundo resolvida do `<hr>` é `Gray` (0x80, 0x80, 0x80) — pode
    confirmar via `tbox_style_table`/estilo resolvido do nó, ou via
    inspeção do `tbox_display_list` gerado (`TBOX_PAINT_FILL_RECT` com
    essa cor) — use o padrão que os testes de `context` já usam pra
    inspecionar cor de um elemento;
  - regressão: um `<p>`/`<h1>` isolados continuam com a mesma margem de
    antes (a mudança no buffer/template não pode ter afetado elementos
    que já existiam).

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — depende de Tarefa 1

### Tarefa 3 — Layout Tree: `<br>`, `<pre>` e aplicação de `text-align`
**Depende de:** Tarefa 1 (mergeada — precisa do campo `tbox_style.text_align`
pra compilar). **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v11 — `<hr>`, `<br>`,
`<pre>` e `text-align`" → "Escopo" INTEIRO, e as três subseções "Layout
Tree — `<br>` (quebra forçada)", "Layout Tree — `<pre>` (texto verbatim)"
e "Layout Tree — aplicação de `text-align`", nessa ordem — têm as
assinaturas exatas, o algoritmo completo de `tbox_layout_break_lines`
(incluindo o caso de linha vazia entre duas quebras consecutivas e a
guarda da linha final), e a fórmula exata do deslocamento de
`text-align`. `src/layout/tbox_layout.c` inteiro — em especial
`tbox_layout_word`/`tbox_layout_push_words`/`tbox_layout_collect_words`/
`tbox_layout_break_lines`/`tbox_layout_build_line_runs`/
`tbox_layout_build_text_runs`/`tbox_layout_is_text_tag`/
`tbox_layout_push_list_marker` (o padrão mais próximo de "função nova
chamada condicionalmente dentro de `tbox_layout_build_text_runs`, baseada
em tag name" já existe pra `<li>`, é a referência de estilo).

**Arquivos a editar:**
- `src/layout/tbox_layout.c`:
  1. `tbox_layout_is_text_tag`: `"pre"` entra na lista fixa
     `text_tags[]`.
  2. `tbox_layout_word` ganha `bool hard_break;`. TODO ponto que já
     empurra uma palavra (`tbox_layout_push_words`, e qualquer outro que
     a Tarefa 3 adicionar) passa a setar `entry->hard_break = false;`
     explicitamente — não dependa de zero-init do `tbox_vector`.
  3. Nova função `static void tbox_layout_push_hard_break(tbox_arena *arena, tbox_vector *words, const tbox_font_face *face)`
     (o parâmetro `arena` pode não ser necessário dependendo de como você
     implementar — confira se precisa antes de incluir; a versão mais
     simples não aloca nada, só empurra uma entrada com `text = {NULL,
     0}`, `width = 0`, `space_width = 0`, `hard_break = true`).
  4. `tbox_layout_collect_words`: no loop de filhos diretos, ANTES do
     `if (child->type == TBOX_HTML_NODE_TEXT)` existente, adicione um
     `if (child->type == TBOX_HTML_NODE_ELEMENT && tbox_string_view_equal_cstr(child->element.tag_name, "br"))`
     que empurra um hard break (face = `tbox_font_face_cache_get(fonts,
     style->font_weight_bold, style->font_size)`) e faz `continue` — ANTES
     do teste de `display == INLINE` que já existe (não depende dele).
  5. `tbox_layout_break_lines` ganha um parâmetro novo, `bool no_wrap`,
     como ÚLTIMO parâmetro antes de `tbox_vector *lines`. No topo do
     corpo do `for`, ANTES do cálculo de `prospective` já existente, um
     `if (words[i].hard_break) { ... }` que fecha a linha atual em
     `[line_start, i)` (com o fallback de altura pra intervalo vazio, ver
     ARCHITECTURE.md), avança `line_start = i + 1`, zera `line_width`, e
     `continue`. A condição de quebra por largura já existente ganha
     `!no_wrap &&` no início (`if (!no_wrap && i > line_start &&
     prospective > available_width)`). O push da linha final (depois do
     loop) ganha a guarda `if (line_start < word_count) { ... }` em volta
     do que já existe ali.
  6. Nova função `static void tbox_layout_collect_preformatted_words(tbox_arena *arena, const tbox_html_node *node, const tbox_style *style, tbox_font_face_cache *fonts, tbox_vector *words)`
     — algoritmo exato no ARCHITECTURE.md (usa
     `tbox_html_node_text_content`, varre byte a byte procurando `\n`,
     cada trecho vira uma palavra inteira via `tbox_font_measure_text`
     direto, sem `tbox_layout_push_words`).
  7. `tbox_layout_build_text_runs`: no topo, decide
     `bool is_preformatted = tbox_string_view_equal_cstr(node->element.tag_name, "pre");`
     — se `true`, chama `tbox_layout_collect_preformatted_words` no lugar
     de `tbox_layout_push_list_marker` + `tbox_layout_collect_words`
     (`<pre>` não tem marcador de lista, então nem faz sentido chamar
     `tbox_layout_push_list_marker` pra ele — pule os dois). Passa
     `is_preformatted` como o novo argumento `no_wrap` de
     `tbox_layout_break_lines`. No loop que já chama
     `tbox_layout_build_line_runs` por linha, adicione o pós-processamento
     de `text-align` descrito no ARCHITECTURE.md (guarda o tamanho de
     `runs` antes/depois da chamada daquela linha, calcula e aplica o
     deslocamento quando `style->text_align != TBOX_STYLE_TEXT_ALIGN_LEFT`).
- `tests/layout/test_layout.c` — casos novos (grupo `layout` já
  registrado, mesmo padrão de navegação de `tbox_layout_box`/inspeção de
  `text_run_count`/`text_runs[i].text`/`.rect` já usado no arquivo,
  inclusive os testes de marcador de lista da v8 como referência de
  estilo):
  - `<p>um<br>dois</p>` → `text_run_count == 2`, os dois runs com texto
    "um"/"dois" em `rect.y` DIFERENTES (linhas separadas), não um run só;
  - `<p>um<br><br>tres</p>` → 3 linhas no total (confirme via `rect.y`
    de cada run, ou inspecionando o número de "linhas" que o texto ocupa
    — decida a asserção mais direta dado como `text_runs`/`rect`
    realmente saem, sem assumir um índice de linha explícito que não
    existe na struct);
  - `<p>x<br></p>` (quebra no final, nada depois) → SEM linha em branco
    fantasma depois de "x" — confirme que não sobra um run/linha extra
    vazio (esse é o caso da guarda `line_start < word_count`);
  - `<pre>a    b</pre>` (4 espaços internos) → o texto do run contém os 4
    espaços literais, não colapsados pra 1 (`text_eq`/comparação de
    string exata, não `strstr`);
  - `<pre>linha um\nlinha dois</pre>` → 2 runs/linhas separadas,
    `rect.y` diferentes, cada uma com o texto da respectiva linha física;
  - `<pre>` com uma linha muito mais larga que `available_width` passado
    ao `tbox_layout_build` → a palavra/linha NÃO quebra (`text_run_count`
    pra aquela linha é 1, o texto inteiro num run só, mesmo que
    `rect.width` ultrapasse `available_width`) — prova de `no_wrap`;
  - `<div style="text-align: center;"><p>oi</p></div>` (herda o
    `text-align` pro `<p>`) → o `rect.x` do run de "oi" fica deslocado
    pra a direita de onde ficaria em `left` (calcule o valor esperado
    a partir da largura medida da palavra "oi" e da largura do container
    — não hardcode um número sem derivar);
  - regressão: `<p>texto normal</p>` sem `<br>`/`text-align` continua
    exatamente como antes (1 run, `rect.x == content_x`);
  - regressão: `<ul><li>item</li></ul>` (marcador da v8) continua
    funcionando sem mudança — confirme que `<li>` não caiu
    acidentalmente no caminho de `<pre>`.

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

## Tier 2 — fatia vertical v11

### Tarefa 4 — Fatia vertical v11 completa (exemplo + validação)
**Depende de:** Tarefas 1, 2 e 3 (todas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v11 — `<hr>`, `<br>`,
`<pre>` e `text-align`" → "Fatia vertical v11 — critério de 'pronto'"
inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v10, sem remover nada) com:
   - Um `<hr>` entre dois blocos existentes (ex. logo antes ou depois de
     algum fixture já presente) — sem CSS de autor nenhum, só pra provar
     o default da UA stylesheet.
   - Um parágrafo novo com `<br>`, incluindo um `<br><br>` consecutivo
     pra provar a linha em branco (ex. `"linha um<br>linha dois<br><br>linha
     quatro"`).
   - Um `<pre>` novo com múltiplos espaços internos e pelo menos uma
     quebra de linha literal no HTML fonte.
   - Três elementos (ou um só com três `<p>` filhos) usando
     `text-align: left`/`center`/`right` explícitos via `style=""` ou
     classe CSS nova (`.v11-*`, mesmo padrão de toda demo anterior),
     com largura/borda visível suficiente pra diferenciar visualmente o
     alinhamento na screenshot.
2. Nenhuma mudança de código deveria ser necessária em `example/tbox_app.c`
   além de possivelmente `TBOX_APP_DEMO_HEIGHT` (mesmo padrão de todo
   incremento anterior — confira visualmente via `--screenshot` antes de
   mudar o número).
3. Validação: use `--screenshot` (`./tbox_app_demo --screenshot
   <path>.png`, sem depender de compositor/janela). `cmake -S . -B build
   && cmake --build build && ctest --test-dir build` tudo verde primeiro,
   depois confira visualmente na imagem gerada: a barra horizontal do
   `<hr>`, as quatro linhas do parágrafo com `<br>` (incluindo a linha em
   branco), os espaços preservados e a quebra de linha do `<pre>`, e os
   três alinhamentos de texto visivelmente diferentes.
4. Opcional, mas recomendado dado que existe: rode `tests/tbox_cmp`
   manualmente (`./build/tests/tbox_cmp tests/assets`, se
   `TBOX_OPENCV_FOUND` estiver disponível no ambiente) e confira se
   `010.html` (que já usa `text-align: center`) mudou de SSIM/contagem de
   diferenças em relação à v10 — não precisa bater 100% (a fonte
   continua diferente do browser que gerou o golden, e `font-family`
   daquele mesmo arquivo ainda não tem efeito), só documente no relatório
   se a métrica melhorou, sem tratar isso como critério de aprovação
   (ver ARCHITECTURE.md's v10 "Escopo" — vereditos por asset não são
   gate).

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação visual acima confirma os quatro
itens (hr, br, pre, text-align) sem erro — este é o "pronto" da v11
inteira, não só desta tarefa.
