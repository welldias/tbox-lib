# tbox — Tarefas da v6

Quebra da seção "v6 — Robustez de HTML (fechamento implícito de tags +
entidades)" do `ARCHITECTURE.md` em tarefas executáveis por agentes sem
contexto desta conversa. Cada tarefa abaixo é auto-contida: aponta para a
subseção exata do `ARCHITECTURE.md` (a fonte da verdade de *o quê*
construir) e acrescenta só o que esse documento não cobre — caminho de
arquivo, como registrar teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v5, que está completa e
commitada — ver `ARCHITECTURE.md` para o design de cada camada v0-v6 e
`git log -- TASKS.md` para recuperar quebras de tarefas anteriores, se
precisar consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática nova além
das que o `ARCHITECTURE.md` já especifica explicitamente pra ela** (ver
o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`). A
v6 não especifica nenhum global/estático novo — se a implementação de
alguma tarefa parecer precisar de um, **pare e reporte isso no relatório
final da tarefa em vez de adicionar por conta própria**.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Nenhuma tarefa cria grupo de
teste novo nesta versão (estende o grupo já registrado `html_parser_tree`)
— sem passo de integração de `tbox.h`/`tests/main.c`. `src/CMakeLists.txt`
já faz glob de `src/html_parser/*.c`, então o arquivo novo
`tbox_html_entities.c` da Tarefa 1 é pego automaticamente pelo build, sem
precisar editar `CMakeLists.txt`.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

A v6 não muda nenhuma assinatura pública (`include/tbox/html_parser.h`
fica intacto) nem nenhuma assinatura já usada por `example/tbox_app.c` —
não há quebra esperada em `example/tbox_app_demo` antes da Tarefa 2; se
algo quebrar, é uma regressão real, não uma quebra esperada.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — HTML Parser: fechamento implícito de `<p>`/`<li>` + decodificação de entidades
**Depende de:** nada. **Bloqueia:** Tarefa 2.

**Por que numa tarefa só:** as duas features são conceitualmente
independentes, mas ambas precisam editar `tbox_html_tree_builder.c`
(fechamento implícito mexe em `tbox_html_tree_builder_handle_start_tag`;
decodificação de entidades troca as chamadas de cópia usadas pra texto e
valor de atributo, que vivem no mesmo arquivo) — mesmo raciocínio de
conflito de merge já usado em tarefas anteriores (v4 Tarefa 1, v5 Tarefa
1): paralelizar aqui só criaria risco de conflito no mesmo arquivo, sem
ganho real.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v6 — Robustez de HTML" INTEIRA
— "Escopo" (as decisões de corte: só `<p>`/`<li>`, só as ~23 entidades
listadas, exige `;` final, sem remapeamento windows-1252), "HTML Parser —
fechamento implícito de `<p>`/`<li>`" (a lista exata de tags que fecham
`<p>`, a regra de `<li>` fechar `<p>` E outro `<li>`) e "HTML Parser —
decodificação de entidades" (assinatura de `tbox_html_decode_entities`,
onde ela é chamada, e onde NÃO é chamada — comentário/DOCTYPE,
`<script>`/`<style>`, nomes de tag/atributo). `src/html_parser/tbox_html_tree_builder.c`
atual inteiro — `open_elements`, `tbox_html_tree_builder_find_matching`
(já usado por `END_TAG`, mesmo mecanismo que o fechamento implícito vai
reaproveitar), `tbox_html_tree_builder_copy`/`_copy_lower`
(exatamente onde `_copy_decoded` vai entrar ao lado), e
`tbox_html_tree_builder_handle_start_tag`/`_run` (onde as duas features
se encaixam). `src/base/tbox_string.h` — confirme
`tbox_string_builder_append_codepoint` (já existe, é o primitivo que a
tabela de entidades vai usar pra codificar tanto nomeadas quanto
numéricas em UTF-8).

**Arquivos a editar/criar:**
- `src/html_parser/tbox_html_entities.h` (novo, arquivo interno — não
  entra em `include/tbox/html_parser.h`): declara
  `tbox_string_view tbox_html_decode_entities(tbox_arena *arena, tbox_string_view text);`
- `src/html_parser/tbox_html_entities.c` (novo): implementa a função
  acima. Tabela estática `{ const char *name; int codepoint; }` com as
  ~23 entidades do "Escopo" do ARCHITECTURE.md (amp, lt, gt, quot, apos,
  nbsp, copy, reg, trade, mdash, ndash, hellip, lsquo, rsquo, ldquo,
  rdquo, euro, pound, yen, cent, sect, para, middot, deg — confirme a
  lista exata na seção do ARCHITECTURE.md). Varre `text` byte a byte;
  trechos sem `&` são copiados em bloco via
  `tbox_string_builder_append_view`; ao achar `&`, tenta primeiro uma
  referência numérica (`#` + dígitos decimais, ou `#x`/`#X` + hex,
  terminada em `;`) — codepoint 0, surrogate (`0xD800`-`0xDFFF`), ou >
  `0x10FFFF` vira U+FFFD; senão tenta uma referência nomeada (letras
  ASCII até `;`, comparação case-sensitive contra a tabela); se nenhuma
  bate, emite o `&` literal e avança só 1 byte. Ambos os casos de sucesso
  usam `tbox_string_builder_append_codepoint`.
- `src/html_parser/tbox_html_tree_builder.c`:
  1. Fechamento implícito: adicionar a lista `tbox_html_p_closing_tags`
     (array `static const char *const[]`, ver a lista exata no
     ARCHITECTURE.md — inclui `"li"`) e a lógica em
     `tbox_html_tree_builder_handle_start_tag` (logo depois de `tag_name`
     resolvido, antes de criar o nó): se `tag_name` está na lista E há um
     `<p>` aberto (via `find_matching`), truncar `open_elements` até esse
     índice; separadamente, se `tag_name == "li"` E há um `<li>` aberto,
     truncar até esse índice também. A função deixa de receber `top`
     fixo do chamador — recalcula via `tbox_html_tree_builder_top`
     DEPOIS de qualquer truncamento, antes de anexar o novo nó (ajuste
     também o call site em `tbox_html_tree_builder_run`, que hoje
     calcula `top` uma vez e passa pra todo handler).
  2. Decodificação de entidades: incluir `tbox_html_entities.h`; nova
     função `tbox_html_tree_builder_copy_decoded` (mesmo padrão de
     `_copy`/`_copy_lower`, só que chama `tbox_html_decode_entities` em
     vez de só copiar). Usar essa função em vez de `_copy` em exatamente
     dois lugares: (a) no caso `TBOX_HTML_TOKEN_TEXT` de
     `tbox_html_tree_builder_run`, MAS só quando o `top` (recalculado
     conforme o item 1) não é um elemento `<script>`/`<style>` — checar
     `top->type == TBOX_HTML_NODE_ELEMENT` e o `tag_name` contra essas
     duas strings antes de decidir qual função de cópia usar; (b) no
     valor de cada atributo, dentro de
     `tbox_html_tree_builder_handle_start_tag` (`attribute->value = ...`).
     `_copy`/`_copy_lower` continuam exatamente como estão pra
     COMMENT/DOCTYPE e pra nomes de tag/atributo — sem decodificação
     nesses casos.
- `tests/html_parser/test_tree.c` — casos novos (grupo `html_parser_tree`
  já registrado):
  - `<p>primeiro<p>segundo</p>` produz dois `<p>` IRMÃOS (não aninhados)
    na árvore, cada um com seu próprio texto;
  - `<div><p>texto<div>outro</div></p></div>` (um `<div>` — tag que fecha
    `<p>` — dentro de um `<p>` aberto) fecha o `<p>` corretamente antes de
    abrir o `<div>` interno (o `<div>` interno vira irmão do `<p>`, não
    filho dele);
  - `<ul><li>um<li>dois<li>três</ul>` produz três `<li>` IRMÃOS (não
    aninhados), cada `<li>` filho direto do `<ul>`;
  - `<p>x<li>y</p>` (caso do "li fecha p também") — o `<li>` fecha o
    `<p>` aberto antes de ser aberto ele mesmo;
  - regressão: `<div><span></div>` (fechamento cruzado já suportado desde
    v0) continua funcionando sem mudança;
  - `&amp;`, `&lt;`, `&nbsp;`, `&mdash;`, `&#233;`, `&#xE9;` (é, dois
    jeitos) em texto decodificam pro caractere certo; um valor de
    atributo com `&amp;` também decodifica;
  - `&naoexiste;` (nome não reconhecido), `&amp` (sem `;`), `&#;` (sem
    dígitos) ficam como texto literal, sem crash nem corromper o resto
    do texto ao redor;
  - `&#0;`, `&#xD800;` (surrogate), `&#99999999;` (fora de range) viram
    U+FFFD, não crasham nem produzem UTF-8 inválido
    (`tbox_string_view_valid_utf8` no resultado deve retornar `true`);
  - `<script>a &amp; b</script>` mantém `&amp;` LITERAL no texto do nó
    (raw text nunca decodifica) — comparar com o mesmo texto fora de um
    `<script>`, que decodifica;
  - um comentário `<!-- a &amp; b -->` mantém `&amp;` literal (comentário
    nunca decodifica).

**Critério de pronto:** `ctest --test-dir build -R '^html_parser_tree$'`
verde + suíte inteira sem regressão.

---

## Tier 1 — fatia vertical v6

### Tarefa 2 — Fatia vertical v6 completa (exemplo + validação)
**Depende de:** Tarefa 1 (mergeada).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v6 — Robustez de HTML" →
"Fatia vertical v6 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v5, sem remover nada) com: um trecho `<p>texto sem fechar<p>outro
   parágrafo</p>` (prova visual de dois parágrafos separados, não um
   aninhado dentro do outro); uma lista `<ul><li>um<li>dois<li>três</ul>`
   sem `</li>` nenhum (prova visual de três itens de lista separados,
   idealmente com marcadores/numeração ou alguma indicação visual clara
   de que são 3 itens distintos — mesmo sem `list-style` de verdade
   implementado, pode usar espaçamento/cor por item via seletor
   `li:first-child`/etc. se ajudar a diferenciar visualmente); um
   parágrafo com `&amp;`, `&nbsp;`, `&mdash;`, `&copy;` e uma referência
   numérica (`&#233;` ou `&#xE9;`) misturados no texto, decodificados na
   tela; um bloco confirmando que `<script>`/comentário com `&amp;`
   dentro continua literal (não precisa ser visível na tela — pode ser
   confirmado via inspeção do texto fonte + um teste de smoke separado,
   já que `<script>` não renderiza texto de qualquer forma).
2. Nenhuma mudança de código deveria ser necessária em
   `example/tbox_app.c` (a v6 não muda nenhuma assinatura pública nem
   introduz API nova pra Application) — se algo precisar mudar, documente
   por quê no relatório final.
3. Validação: mesmo padrão das fatias verticais anteriores —
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   tudo verde (incluindo `example/tbox_app_demo`), e, se
   `WAYLAND_DISPLAY` estiver setado (compositor real disponível), um
   smoke-test via `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que abre,
   roda e fecha sozinho sem crash, e — mesmo padrão das fatias verticais
   anteriores — captura de tela (`grim`, se disponível) com inspeção por
   amostragem de pixel/texto confirmando os itens do passo 1 (os dois
   parágrafos separados, os três itens de lista, e o texto das entidades
   decodificado corretamente, não literal). Se `WAYLAND_DISPLAY` não
   estiver setado, ou não houver captura de tela disponível, pule esse
   passo e diga isso claramente no relatório.

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação acima roda sem erro — este é o
"pronto" da v6 inteira, não só desta tarefa.
