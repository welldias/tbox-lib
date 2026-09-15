# tbox — Tarefas da v1

Quebra da seção "v1 — Interatividade" do `ARCHITECTURE.md` em tarefas
executáveis por agentes sem contexto desta conversa. Cada tarefa abaixo é
auto-contida: aponta para a subseção exata do `ARCHITECTURE.md` (a fonte
da verdade de *o quê* construir) e acrescenta só o que esse documento não
cobre — caminho de arquivo, como registrar teste, como verificar que ficou
pronto.

(Este arquivo substitui a quebra de tarefas do v0, que está completo — ver
`ARCHITECTURE.md` para o design de cada camada v0 e `git log -- TASKS.md`
para recuperar a quebra de tarefas original, se precisar consultá-la.)

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Diferente do v0: **nenhuma
tarefa da v1 cria módulo novo nem grupo de teste novo** — todas estendem
módulos/grupos já existentes (`html_parser`, `context`, `output`, `app`) —
então **não há passo de integração** de `include/tbox/tbox.h`/
`src/CMakeLists.txt`/`tests/CMakeLists.txt`/`tests/main.c` desta vez; esses
4 arquivos não devem ser tocados por nenhuma tarefa abaixo.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

---

## Tier 0 — paralelo, sem dependência de tarefa nova

### Tarefa 1 — HTML Parser: mutação de atributo
**Depende de:** nada. **Bloqueia:** Tarefa 5 (fatia vertical v1).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v1 — Interatividade" →
"HTML Parser — mutação de atributo" (assinaturas e comportamento exatos de
`tbox_html_node_set_attribute`/`tbox_html_node_get_attribute`, incluindo a
nota de que o array de atributos antigo fica órfão na arena ao crescer).
`include/tbox/html_parser.h` e `src/html_parser/tbox_html_node.c` (onde
`tbox_html_node_create`/`_append_child`/`_remove`/`_text_content` já
vivem — é o lugar certo para as duas funções novas também).
`src/html_parser/tbox_html_document.h` (definição interna de
`tbox_html_document`, para acessar `document->arena` do mesmo jeito que
`tbox_html_node_create` já faz).

**Arquivos a editar:**
- `include/tbox/html_parser.h` — adicionar as duas declarações
- `src/html_parser/tbox_html_node.c` — implementar
- `tests/html_parser/test_tree.c` — adicionar casos de teste (arquivo já
  existe, grupo `html_parser_tree` já registrado — **não editar
  `tests/main.c` nem `tests/CMakeLists.txt`**)

**Casos de teste mínimos:** `set_attribute` num nó sem atributos cria o
primeiro; `set_attribute` com `name` já existente substitui o `value` (não
duplica entrada); `set_attribute` várias vezes com nomes diferentes
acumula todos; `get_attribute` acha o atributo certo (comparação
case-insensitive de `name`) e retorna `NULL` para nome ausente ou nó não-
ELEMENT; um atributo definido via `set_attribute` aparece em
`node->element.attributes`/`attribute_count` do jeito que qualquer
atributo parseado apareceria (sem tratamento especial em nenhum outro
lugar do código).

**Critério de pronto:** `ctest --test-dir build -R '^html_parser_tree$'`
verde.

---

### Tarefa 2 — Output Display: evento de ponteiro (Wayland)
**Depende de:** nada (backend Wayland já existe, esta tarefa só o
estende). **Bloqueia:** Tarefa 4 (Application).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v1 — Interatividade" →
"Output Display (Wayland backend) — evento de ponteiro" (assinatura e
comportamento exato de `tbox_backend_wayland_take_click` — PRESS, não
RELEASE, sem drag/duplo-clique). `src/output/tbox_backend_wayland.c`
(estado/listeners já existentes para `wl_seat`/teclado — é o padrão a
replicar para `wl_pointer`; o registry/bind de globals fica no mesmo lugar
onde `wl_compositor`/`wl_shm`/`xdg_wm_base`/`wl_seat` já são bindados).

**Arquivos a editar:**
- `include/tbox/output.h` — adicionar a declaração de
  `tbox_backend_wayland_take_click`
- `src/output/tbox_backend_wayland.c` — implementar: listener de
  `wl_pointer` (enter/leave para saber qual superfície, motion para
  atualizar a posição corrente, button para marcar "há um clique
  pendente" quando o botão principal é pressionado — `BTN_LEFT`/
  `linux/input-event-codes.h`, mesma fonte que o teclado já deve usar para
  keycodes), estado interno (posição do último clique pendente + flag),
  consumido e limpo por `tbox_backend_wayland_take_click`.

**Sem teste automatizado** (mesmo motivo do resto do backend Wayland: não
testável sem compositor — ver `tests/output/test_raster.c`, que testa só
a metade pura/`tbox_raster_*`). Validação é smoke-test manual/via
Tarefa 5.

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde (com e sem Wayland disponível).

---

### Tarefa 3 — Orchestration: delegação de clique por seletor
**Depende de:** nada (usa só `tbox_css_selector_compile`/
`_query_matches`, `tbox_context_hit_test`, e navegação `node->parent`, já
existentes). **Bloqueia:** Tarefa 4 (Application).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v1 — Interatividade" →
"Orchestration (`tbox_context`) — delegação de evento por seletor" inteira
— assinaturas de `tbox_context_on_click`/`_dispatch_click`/`_document`,
a decisão de por que mora na Orchestration e não na Application, a regra
de dispatch (sobe `node->parent` a partir do nó sob o hit-test, primeiro
ancestral que casa com cada registro dispara, sem bubbling completo).
`include/tbox/context.h` e `src/context/tbox_context.c` (onde
`tbox_context_open`/`_close`/`_run_frame`/`_hit_test` já vivem).
`include/tbox/css_selector.h` (`tbox_css_selector_compile`,
`tbox_css_selector_query_matches`, `tbox_css_selector_query_destroy` — as
funções a consumir).

**Arquivos a editar:**
- `include/tbox/context.h` — adicionar `tbox_context_click_handler` (tipo
  de função), `tbox_context_on_click`, `tbox_context_dispatch_click`,
  `tbox_context_document`
- `src/context/tbox_context.c` — implementar: `tbox_context` ganha um
  vetor arena-backed (arena própria do `tbox_context`, **não** a
  `frame_arena` — os registros de clique não podem ser invalidados por um
  `tbox_context_run_frame`) de `{tbox_css_selector_query*, handler,
  userdata}`; `tbox_context_dispatch_click` implementa a subida de
  ancestrais + `tbox_css_selector_query_matches` por registro; lembrar de
  destruir cada `tbox_css_selector_query` em `tbox_context_close`

**Testes:** em `tests/context/test_context.c` (grupo `context` já
registrado — **não editar `tests/main.c` nem `tests/CMakeLists.txt`**):
clique dentro da caixa de um elemento que casa com o seletor registrado
dispara o handler exatamente uma vez, com o `node` certo; clique fora de
qualquer caixa não dispara nada; dois elementos aninhados onde só o
elemento externo casa com o seletor — clique no elemento interno ainda
dispara o handler (bubbling até o ancestral que casa); dois handlers
registrados com seletores diferentes, ambos aplicáveis ao mesmo nó, ambos
disparam num único clique; seletor com erro de sintaxe em
`tbox_context_on_click` retorna `false` e não registra nada;
`tbox_context_dispatch_click` antes de qualquer `tbox_context_run_frame`
retorna `false` sem crashar (mesma guarda de `tbox_context_hit_test`).

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde.

---

## Tier 1 — depende de Tier 0

### Tarefa 4 — Application: loop não-bloqueante
**Depende de:** Tarefa 2 (`tbox_backend_wayland_take_click`), Tarefa 3
(`tbox_context_on_click`/`_dispatch_click`) — mergeadas. **Bloqueia:**
Tarefa 5 (fatia vertical v1).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v1 — Interatividade" →
"Application (`tbox_app`) — loop não-bloqueante" inteira — tipos/
assinaturas (`tbox_app_create`/`_context`/`_step`/`_should_close`/
`_close`), a política de `tbox_app_step` (poll não-bloqueante, dispatch de
clique, detecção de resize, recompute só se sujo), a decisão explícita de
que `tbox_app_open` é removido (breaking change aceito). `include/tbox/app.h`
e `src/app/tbox_app.c` atuais (implementação de `tbox_app_open` a partir
da qual esta tarefa evolui — reaproveitar a lógica de abrir
context+backend+fonte e a ordem de limpeza, só trocando o formato
bloqueante por um handle stepável).

**Arquivos a editar:**
- `include/tbox/app.h` — remover a declaração de `tbox_app_open`,
  adicionar `tbox_app` (opaco) + as 5 funções novas
- `src/app/tbox_app.c` — implementar

**Sem grupo de teste próprio** (Application não é unit-testada em v0
tampouco — validação é smoke-test via exemplo, ver Tarefa 5).

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde (com e sem Wayland/Fontconfig disponíveis).

---

## Tier 2 — fatia vertical v1

### Tarefa 5 — Fatia vertical v1 completa (exemplo + validação)
**Depende de:** Tarefa 1 (mutação de atributo), Tarefa 4 (Application) —
mergeadas.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v1 — Interatividade" →
"Fatia vertical v1 — critério de 'pronto'" inteira — o cenário exato
(`<div class="box off">` com CSS diferenciando `.off`/`.on`, handler
registrado por seletor que chama `tbox_html_node_set_attribute` para
trocar a classe, clique muda a cor sem fechar a janela).

**Trabalho:**
1. Estender (ou criar par novo, a critério de quem implementa) o HTML/CSS
   de exemplo (`example/index.html` + `example/style.css`, mesmo padrão
   do v0) com um elemento clicável cujo `background-color` difere entre
   duas classes.
2. Atualizar `example/tbox_app.c` para o novo formato não-bloqueante:
   `tbox_app_create` → `tbox_context_on_click` com um handler que troca a
   classe via `tbox_html_node_set_attribute` (usando `tbox_context_document`
   para ter a arena certa) → loop `while (!tbox_app_should_close(app))
   tbox_app_step(app);` → `tbox_app_close`. Manter o padrão
   `TBOX_WAYLAND_CLOSE_DELAY_MS` já usado no v0 para smoke-test
   automatizado sem intervenção manual, mas agora **sem** exigir clique
   manual para validar (ver item 3).
3. Validação (não é teste unitário, é a demonstração do critério de
   "pronto" da v1): rodar o exemplo num ambiente com compositor Wayland
   disponível (`Xvfb`/`weston --backend=headless` em CI, ou sessão gráfica
   local) confirmando que a janela abre, mostra a cor inicial (`.off`), e
   — se o ambiente permitir simular um clique programático nas
   coordenadas do elemento (ex. via uma ferramenta de automação do
   compositor headless, ou um modo de teste que injete um clique sintético
   direto em `tbox_context_dispatch_click` sem depender do compositor de
   verdade) — a cor muda para `.on` antes de fechar. Se não houver como
   simular o clique de forma automatizada no ambiente de CI disponível,
   documentar isso explicitamente no relatório da tarefa e, no mínimo,
   confirmar via teste de integração (reaproveitando a infraestrutura da
   Tarefa 3) que `tbox_context_dispatch_click` + `tbox_html_node_set_attribute`
   juntos produzem a `tbox_display_list` esperada após a mudança de
   classe — cobrindo a mesma cadeia sem depender do compositor. Anexar uma
   captura de tela ao relatório final como evidência visual sempre que a
   validação com janela real for possível.

**Critério de pronto:** build limpo + a validação acima roda sem erro —
este é o "pronto" da v1 inteira, não só desta tarefa.
