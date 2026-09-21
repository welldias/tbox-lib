# tbox — Tarefas da v9

Quebra da seção "v9 — CSS de autor: inline (`style=""`) e `<style>` interno
na cascata" do `ARCHITECTURE.md` em tarefas executáveis por agentes sem
contexto desta conversa. Cada tarefa abaixo é auto-contida: aponta para a
subseção exata do `ARCHITECTURE.md` (a fonte da verdade de *o quê*
construir) e acrescenta só o que esse documento não cobre — caminho de
arquivo, como registrar teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v8, que está completa e
commitada — ver `ARCHITECTURE.md` para o design de cada camada v0-v9 e
`git log -- TASKS.md` para recuperar quebras de tarefas anteriores, se
precisar consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL nova
além das que o `ARCHITECTURE.md` já especifica explicitamente pra ela**
(ver o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`).
A v9 não especifica nenhum global/estático mutável novo — se a
implementação de alguma tarefa parecer precisar de um, **pare e reporte
isso no relatório final da tarefa em vez de adicionar por conta própria**.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**As Tarefas 1 e 2 abaixo não compartilham NENHUM arquivo** (módulos
diferentes: `css_cascade` vs. `context`) — rodam em paralelo sem risco de
conflito de merge. Nenhuma das duas depende da outra: a Tarefa 1 (inline)
não precisa que `<style>` interno exista pra funcionar, e a Tarefa 2
(`<style>` interno) não precisa que inline exista — cada uma é
independentemente testável, e só a fatia vertical (Tarefa 3) precisa das
duas juntas pra mostrar o cenário completo.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

**Importante sobre compatibilidade:** esta versão NÃO renomeia
`TBOX_CSS_ORIGIN_AUTHOR` (decisão revisada nesta sessão, ver
"ARCHITECTURE.md"'s v9 — a primeira versão do design teria dividido em
três valores; a versão final só ACRESCENTA `TBOX_CSS_ORIGIN_AUTHOR_INLINE`
no fim do enum). Nenhum código existente que já usa
`TBOX_CSS_ORIGIN_AUTHOR` precisa mudar — não toque nesses usos a menos que
a tarefa mande explicitamente.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — CSS Cascade: `style=""` inline
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v9 — CSS de autor..." →
"Escopo" (por que só um valor novo no enum, aditivo; por que a resolução
fica dentro de `tbox_css_cascade_resolve`; por que `!important` é
suportado) e "CSS Cascade — `TBOX_CSS_ORIGIN_AUTHOR_INLINE` + resolução de
inline dentro de `tbox_css_cascade_resolve`" INTEIRA (assinaturas exatas,
a tabela de rank nova, onde o passo de inline entra na função).
`src/css_cascade/tbox_css_cascade.c` inteiro — em especial
`tbox_css_cascade_rank` (a tabela que cresce), `tbox_css_cascade_resolve`
(onde o loop de `sources` já existe e onde o passo novo entra DEPOIS
dele), e a struct local `winners`/a lógica "existe candidato pra essa
propriedade? `tbox_css_cascade_wins_or_ties`? substitui" que já existe
nesse loop — o passo novo REAPROVEITA essa mesma lógica, não duplica.
`include/tbox/css_parser.h` — `tbox_css_ruleset`/`tbox_css_declaration`
(pra iterar as declarações do stylesheet sintético) e a doc comment de
`tbox_css_parse` (confirma que a entrada é copiada pro arena da própria
biblioteca, então um buffer sintético pode ser liberado logo depois da
chamada retornar). `include/tbox/html_parser.h` —
`tbox_html_node_get_attribute` (já devia estar acessível neste arquivo,
confirme o include).

**Arquivos a editar:**
- `include/tbox/css_cascade.h`: `tbox_css_origin` ganha
  `TBOX_CSS_ORIGIN_AUTHOR_INLINE` no FIM do enum (depois de
  `TBOX_CSS_ORIGIN_AUTHOR`, que não muda de posição nem de valor).
  Atualize o comentário de ordem de prioridade acima do enum (hoje
  documenta só 3 origens) pra incluir o degrau de inline (ver a ordem
  completa no ARCHITECTURE.md). Remova/reescreva a frase "There is no
  fourth 'style attribute' bucket..." — ela deixa de ser verdade a partir
  desta versão.
- `src/css_cascade/tbox_css_cascade.c`:
  1. `tbox_css_cascade_rank`: tabela `rank[3][2]` vira `rank[4][2]` — os
     valores exatos estão no ARCHITECTURE.md (não invente números
     diferentes, a ordem relativa importa: UA-normal < USER-normal <
     AUTHOR-normal < AUTHOR_INLINE-normal < AUTHOR-important <
     AUTHOR_INLINE-important < USER-important < UA-important).
  2. Nova função `static tbox_css_stylesheet
     *tbox_css_cascade_parse_inline_style(tbox_string_view declarations)`:
     se `declarations.size == 0`, retorna `NULL`. Senão, aloca (`malloc`,
     liberado antes de retornar — não precisa sobreviver além desta
     função, já que `tbox_css_parse` copia tudo que precisa) um buffer do
     tamanho EXATO `strlen("* {") + declarations.size + strlen("}") + 1`
     (não um tamanho fixo — `style=""` real pode ser longo, truncar
     seria um bug visível), monta o texto `"* {" + declarations + "}"`,
     chama `tbox_css_parse(buffer, tamanho_sem_o_nul)`, libera o buffer,
     retorna o resultado (pode ser `NULL` se `tbox_css_parse` falhar —
     propague).
  3. Em `tbox_css_cascade_resolve`: DEPOIS do loop que já escaneia
     `sources` (esse loop não muda nenhuma linha), adicione: se `node !=
     NULL`, chame `tbox_html_node_get_attribute(node, tbox_string_view_make("style", 5))`;
     se achou um atributo com `value.size > 0`, chame
     `tbox_css_cascade_parse_inline_style(attr->value)`; se o resultado
     não é `NULL`, itere o (único) ruleset desse stylesheet sintético
     (`tbox_css_stylesheet_rulesets`/`tbox_css_stylesheet_ruleset_count`,
     mesmas funções que o loop de `sources` já usa) e, pra cada
     `tbox_css_declaration`, monte um `tbox_css_resolved_declaration`
     candidato com `origin = TBOX_CSS_ORIGIN_AUTHOR_INLINE`,
     `specificity = {0,0,0}` (não importa pro rank de inline, que já
     vence só pela origem — mas preencha com um valor válido, não
     deixe lixo), `value = tbox_css_cascade_strip_important(declaration->value, &candidate.important)`
     (mesma chamada que o loop de `sources` já faz) — e jogue esse
     candidato no MESMO "existe pra essa propriedade? vence/empata?
     substitui" que o loop de `sources` já usa (extraia isso pra uma
     função/bloco reaproveitável se for mais limpo do que duplicar o
     texto, mas NÃO duplique a lógica de decisão em si). Destrua o
     stylesheet sintético (`tbox_css_stylesheet_destroy`) antes da
     função retornar, em QUALQUER caminho de saída (sucesso ou não).
  4. `tbox_css_cascade_resolve_stylesheet` não muda (continua usando
     `TBOX_CSS_ORIGIN_AUTHOR` pro único stylesheet que recebe) — o inline
     agora é automático pra ela também, de graça, sem precisar de
     nenhuma mudança na função em si.
- `tests/css_cascade/test_cascade.c` — casos novos (grupo `css_cascade`
  já registrado, mesmo padrão de `tbox_css_cascade_resolve_stylesheet`/
  `find_cstr`/`TBOX_TEST_ASSERT` já usado no arquivo):
  - `<p style="color: red;">x</p>` sem stylesheet nenhum (`sheet = ""`)
    — `color` resolve pra `"red"`;
  - `<p id="x" style="color: red;">x</p>` com stylesheet
    `"#x { color: blue; }"` (especificidade de ID, a mais alta possível
    por seletor) — `color` AINDA resolve pra `"red"` (inline vence
    QUALQUER seletor, não importa a especificidade);
  - `<p style="color: red !important;">x</p>` com stylesheet
    `"#x { color: blue !important; }"` no mesmo `<p id="x" ...>` — `color`
    resolve pra `"red"` (inline `!important` vence author `!important` de
    seletor forte);
  - regressão: um `<p>` sem atributo `style` nenhum resolve exatamente
    como antes (nenhuma mudança de comportamento pra quem não usa
    inline);
  - regressão: `style=""` (atributo presente, vazio) não contribui
    NADA pro resultado (mesmo efeito de não ter o atributo).

**Critério de pronto:** `ctest --test-dir build -R '^css_cascade$'` verde
+ suíte inteira sem regressão.

### Tarefa 2 — Orchestration: `<style>` interno na cascata
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v9 — CSS de autor..." →
"Escopo" (por que a busca percorre o documento INTEIRO, não só o que o
Layout Tree renderiza; por que múltiplos `<style>` são concatenados num
stylesheet só; por que `<style>` interno vence empate contra CSS externo)
e "Orchestration — extração de `<style>` do documento inteiro" INTEIRA
(assinatura do campo novo, onde a travessia entra em
`tbox_context_open_with_config`, o array `sources` de 3 elementos em
`tbox_context_run_frame`, e o aviso sobre o comentário existente que
precisa ser atualizado). `src/context/tbox_context.c` inteiro — em
especial `struct tbox_context` (o campo novo), `tbox_context_open_with_config`
(onde a travessia entra), `tbox_context_close` (o destroy novo), e
`tbox_context_run_frame` (o array `sources`). `include/tbox/html_parser.h`
— `tbox_html_document_root`, `tbox_html_node_text_content` (reaproveitada
pra extrair o texto raw de cada `<style>`), e os campos
`first_child`/`next_sibling`/`type`/`element.tag_name` de `tbox_html_node`
(pra escrever a travessia recursiva). `src/base/tbox_string.h` —
`tbox_string_builder_init`/`_append_view`/`_finish` (pra concatenar o
texto de múltiplos `<style>`).

**Arquivos a editar:**
- `src/context/tbox_context.c`:
  1. `struct tbox_context` ganha `tbox_css_stylesheet *internal_stylesheet;`
     (mesmo lugar dos outros campos de stylesheet, mesmo padrão de
     comentário).
  2. Nova função privada `static void
     tbox_context_collect_style_elements(tbox_arena *arena, const
     tbox_html_node *node, tbox_string_builder *builder)`: se `node ==
     NULL`, retorna. Se `node->type == TBOX_HTML_NODE_ELEMENT` e
     `tbox_string_view_equal_cstr(node->element.tag_name, "style")`,
     chama `tbox_html_node_text_content(arena, node)` e
     `tbox_string_builder_append_view(builder, ...)` com o resultado.
     Depois, SEMPRE (independente de ter sido um `<style>` ou não),
     recursa em `node->first_child`/`next_sibling` (mesmo padrão de
     travessia em pré-ordem que já existe em outros lugares do projeto,
     ex. `tbox_layout_collect_words`).
  3. Em `tbox_context_open_with_config`: depois de `tbox_html_parse`
     bem-sucedido e ANTES de `tbox_css_parse(css, ...)` (o CSS externo),
     crie uma `tbox_arena scratch = tbox_arena_create(0)`, um
     `tbox_string_builder` sobre ela, chame
     `tbox_context_collect_style_elements(&scratch,
     tbox_html_document_root(document), &builder)` (a raiz de VERDADE do
     documento — `tbox_html_document_root`, não o primeiro elemento de
     topo que `tbox_layout_build` isola por conta própria), pegue o
     resultado via `tbox_string_builder_finish`. Se o tamanho for > 0,
     `tbox_css_parse` nele vira `internal_stylesheet` (falha de alocação
     aqui segue o mesmo padrão de toda falha nesta função: desfaz o que
     já foi alocado — incluindo `document` — e retorna `NULL`); senão,
     `internal_stylesheet = NULL`. Destrua `scratch` depois (o texto já
     foi copiado pra dentro do stylesheet por `tbox_css_parse`, não
     precisa sobreviver além dele).
  4. `tbox_context_close`: adicione
     `tbox_css_stylesheet_destroy(ctx->internal_stylesheet);` ao lado dos
     outros `_destroy` (seguro com `NULL`).
  5. `tbox_context_run_frame`: o array `sources` cresce de
     `tbox_css_cascade_source sources[2]` pra `sources[3]`, com o
     terceiro elemento `{ ctx->internal_stylesheet, TBOX_CSS_ORIGIN_AUTHOR }`
     — MESMA origem que `ctx->stylesheet` já usa, não uma nova. Ordem no
     array: UA, depois `stylesheet` (externo), depois
     `internal_stylesheet` — nessa ordem exata, porque a ordem entre os
     dois `AUTHOR` decide o empate (interno por último = interno vence,
     ver ARCHITECTURE.md). Atualize o comentário acima dessa linha (hoje
     diz "Order in this array does not affect cascade priority" sem
     qualificação) pra explicar que isso deixou de ser totalmente verdade
     entre os dois `AUTHOR` especificamente.
- `tests/context/test_context.c` — casos novos (grupo `context` já
  registrado, mesmo padrão de testes existentes que montam HTML/CSS via
  `tbox_context_open`/`tbox_context_open_with_config` e inspecionam a
  árvore via `tbox_context_hit_test`/`tbox_style_table`/cor resolvida):
  - um documento com `<style>.algo{color:blue;}</style>` embutido (HTML
    puro, sem CSS externo nenhum — `css`/`css_path` vazio) e um elemento
    `class="algo"` — a cor resolvida do elemento é azul;
  - o mesmo documento, mas agora com CSS externo `.algo{color:green;}`
    (mesma especificidade) — a cor resolvida é AZUL (interno vence
    empate);
  - regressão: um documento SEM `<style>` nenhum embutido continua
    resolvendo o CSS externo exatamente como antes (nenhuma mudança de
    comportamento pra documentos sem `<style>`);
  - um documento com DOIS `<style>` separados (ex. um antes e um depois
    de outro conteúdo) — confirme que as regras dos dois se aplicam
    (prova de que a concatenação funciona, não só o primeiro `<style>`
    encontrado).

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — fatia vertical v9

### Tarefa 3 — Fatia vertical v9 completa (exemplo + validação)
**Depende de:** Tarefa 1 e Tarefa 2 (ambas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v9 — CSS de autor..." →
"Fatia vertical v9 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v8, sem remover nada):
   - Adicione um bloco `<style>` embutido no HTML (não no arquivo `.css`
     externo) com pelo menos uma regra que colidiria com uma regra do CSS
     externo na MESMA especificidade, pra provar visualmente que o
     `<style>` interno vence o empate (ex. uma classe nova, digamos
     `.v9-internal-wins`, declarada com uma cor no `.css` externo E
     redeclarada com OUTRA cor dentro do `<style>` embutido no HTML — a
     cor que aparece na tela tem que ser a do `<style>` interno).
   - Adicione um elemento com `style="..."` inline que sobrescreve uma
     regra de alta especificidade (ex. um seletor `#id.classe` no CSS
     externo ou interno) — a cor/propriedade que aparece na tela tem que
     ser a do `style=""` inline, não a do seletor de alta especificidade.
   - Adicione um elemento com um `style="...!important;"` inline que
     sobrescreve uma regra `!important` de seletor forte no CSS externo
     ou interno pra mesma propriedade — mostrando que inline `!important`
     também vence.
   - Use cores fortemente contrastantes e classes CSS novas (mesmo padrão
     de toda demo anterior, ex. `.v9-*`) pra cada cenário ficar fácil de
     achar/comparar no screenshot.
2. Nenhuma mudança de código deveria ser necessária em `example/tbox_app.c`
   além de possivelmente `TBOX_APP_DEMO_HEIGHT` (mesmo padrão de todo
   incremento anterior — confira visualmente via `--screenshot` antes de
   mudar o número).
3. Validação: use `--screenshot` (ver ARCHITECTURE.md's "Ferramentas de
   desenvolvimento — captura de tela headless") — `./tbox_app_demo
   --screenshot <path>.png` renderiza offscreen, sem depender de
   compositor/janela nenhuma. `cmake -S . -B build && cmake --build build
   && ctest --test-dir build` tudo verde primeiro, depois confira
   visualmente na imagem gerada: o cenário do `<style>` interno vencendo
   empate, o cenário do `style=""` inline vencendo especificidade alta, e
   o cenário do `style=""` inline `!important` vencendo um `!important`
   de seletor forte — todos mostrando a cor/resultado ESPERADO (não o que
   seria produzido se a prioridade estivesse errada).

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação visual acima confirma os três
cenários sem erro — este é o "pronto" da v9 inteira, não só desta tarefa.
