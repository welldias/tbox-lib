# tbox — Tarefas da v3

Quebra da seção "v3 — Interatividade Avançada" do `ARCHITECTURE.md` em
tarefas executáveis por agentes sem contexto desta conversa. Cada tarefa
abaixo é auto-contida: aponta para a subseção exata do `ARCHITECTURE.md`
(a fonte da verdade de *o quê* construir) e acrescenta só o que esse
documento não cobre — caminho de arquivo, como registrar teste, como
verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v2, que está completa — ver
`ARCHITECTURE.md` para o design de cada camada v0/v1/v2 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática nova além
das que o `ARCHITECTURE.md` já especifica explicitamente pra ela** (ver
o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`). Se
a implementação de alguma tarefa parecer precisar de um global/estático
que o design não previu, **pare e reporte isso no relatório final da
tarefa em vez de adicionar por conta própria** — é uma decisão que espera
discussão com o mantenedor do projeto, não uma escolha de implementação.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria — com a mesma exceção de
sempre pra quando uma tarefa é a única do seu tier (ver Tarefa 7). Nenhuma
tarefa cria módulo novo nem grupo de teste novo nesta versão (todas
estendem módulos/grupos já existentes: `css_parser_parser`,
`html_parser_tree`, `css_selector`, `context`) — sem passo de integração
de `tbox.h`/`CMakeLists.txt`/`tests/main.c`, mesma situação da v1/v2.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

`example/tbox_app_demo` é um alvo CMake **separado** de `tbox_tests` (não
faz parte do `tbox_static` que `tbox_tests` linka) — diferente da v2,
nenhuma tarefa da v3 muda assinatura de função usada por módulos já
mergeados da BIBLIOTECA core, então não deveria haver quebra em cascata
dentro de `tbox_static`/`tbox_tests` desta vez. A ÚNICA quebra esperada é
`example/tbox_app.c` (o handler `on_box_click` ainda com assinatura antiga
depois da Tarefa 5 mudar `tbox_context_click_handler`) — corrigida só na
Tarefa 7, mesmo padrão de "quebra esperada em alvo separado" já usado na
v1.

---

## Tier 0 — paralelo, sem dependência de tarefa nova

### Tarefa 1 — CSS Parser: correção do bug de profundidade de parênteses
**Depende de:** nada. **Bloqueia:** nada (correção isolada).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "CSS Parser — correção do bug de profundidade de parênteses"
(a causa exata: `TBOX_CSS_TOKEN_FUNCTION` consome o `(` de abertura no
próprio token, mas só `LPAREN`/`RPAREN` são contados como abre/fecha nas
funções de skip/recovery) e "Robustez de parsing — o que a auditoria
confirmou" logo acima, pro contexto de como o bug foi encontrado.
`src/css_parser/tbox_css_parser.c` (onde `tbox_css_token_opens`/
`tbox_css_token_closes` e as funções `skip_block`/`skip_at_rule`/
`recover_ruleset` vivem) e `src/css_parser/tbox_css_token.h` (definição
de `TBOX_CSS_TOKEN_FUNCTION`/`LPAREN`/`RPAREN`).

**Arquivos a editar:**
- `src/css_parser/tbox_css_parser.c` — corrigir `tbox_css_token_opens`
  (ou onde quer que a checagem de "abre" viva) pra também tratar
  `TBOX_CSS_TOKEN_FUNCTION` como abertura de escopo
- `tests/css_parser/test_parser.c` — casos novos (grupo `css_parser_parser`
  já registrado)

**Sem mudança de API pública** — `include/tbox/css_parser.h` não muda.

**Casos de teste mínimos:** `@keyframes spin { from { transform:
rotate(0deg); } } p { color: black; }` produz exatamente 1 ruleset (`p`),
com a declaração `color: black` presente (hoje produz 0 rulesets); a
variante com `to { transform: rotate(360deg); }` no lugar de `from` não
produz nenhum ruleset espúrio (`to { ... }` não pode ser interpretado
como seletor real); `@media screen { div { color: red; } } span { color:
blue; }` (um `@media` com função ausente, caso que já funcionava) continua
correto — regressão; um `calc()`/`var()` dentro de uma declaração NÃO
pulada (nível normal do arquivo) continua parseando sem mudança.

**Critério de pronto:** `ctest --test-dir build -R '^css_parser_parser$'`
verde + suíte inteira sem regressão.

---

### Tarefa 2 — HTML Parser: mutação de texto
**Depende de:** nada. **Bloqueia:** nada (Layout Tree já lê texto a cada
relayout, sem cache — ver ARCHITECTURE.md).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "HTML Parser — mutação de texto" (assinatura e comportamento
exatos de `tbox_html_node_set_text_content`, incluindo por que a Layout
Tree não precisa de nenhuma mudança). `include/tbox/html_parser.h`/
`src/html_parser/tbox_html_node.c` atuais (`tbox_html_node_create`/
`_append_child`/`_remove`/`_set_attribute` — o padrão de arena/ownership
a seguir).

**Arquivos a editar:**
- `include/tbox/html_parser.h` — adicionar a declaração
- `src/html_parser/tbox_html_node.c` — implementar: percorre
  `node->first_child` desconectando cada filho (reaproveitar
  `tbox_html_node_remove` por filho, ou simplesmente zerar
  `first_child`/`last_child` diretamente — os nós removidos viram lixo
  órfão na arena do document, mesmo trade-off já aceito por
  `tbox_html_node_set_attribute`), depois cria um único nó TEXT novo
  (`tbox_html_node_create` com `TBOX_HTML_NODE_TEXT`) com `text.text`
  copiado pra arena do `document`, e o anexa via
  `tbox_html_node_append_child`. No-op se `node->type` não for
  `TBOX_HTML_NODE_ELEMENT`.
- `tests/html_parser/test_tree.c` — casos novos (grupo `html_parser_tree`
  já registrado)

**Casos de teste mínimos:** nó ELEMENT sem filhos ganha um filho TEXT com
o texto certo; nó ELEMENT com filhos existentes (incluindo uma mistura de
TEXT e ELEMENT, tipo `<p>oi <b>mundo</b></p>`) tem TODOS os filhos
substituídos pelo único novo nó TEXT (`tbox_html_node_text_content`
depois da mutação retorna só o texto novo, nada do que existia antes);
chamar duas vezes seguidas substitui o texto de novo (não acumula);
no-op silencioso num nó TEXT/COMMENT/DOCTYPE/DOCUMENT (não corrompe a
union).

**Critério de pronto:** `ctest --test-dir build -R '^html_parser_tree$'`
verde + suíte inteira sem regressão.

---

### Tarefa 3 — CSS Selector: avaliação de `:hover`
**Depende de:** nada. **Bloqueia:** Tarefa 5.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "`:hover` — pseudo-classe dinâmica de verdade" inteira —
a decisão de usar contexto global/estático em vez de parâmetro
propagado por assinatura, e por quê (ver também "Thread-safety futura"
no fim do documento — **este é exatamente o único global que o
`ARCHITECTURE.md` já autoriza explicitamente pra esta tarefa; não
adicione nenhum outro**, ver a regra no topo deste arquivo).
`include/tbox/css_selector.h`/`src/css_selector/tbox_css_selector_match.c`
atuais — onde `tbox_css_selector_matches` já trata pseudo-classes
(`first-child`/`last-child` estruturais, tudo mais "nunca casa") é
exatamente onde o branch novo de `"hover"` entra.

**Arquivos a editar:**
- `include/tbox/css_selector.h` — adicionar a declaração de
  `tbox_css_selector_set_hover_context`. **`tbox_css_selector_matches`
  NÃO muda de assinatura** (continua `(selector, node)`).
- `src/css_selector/tbox_css_selector_match.c` — uma variável estática de
  arquivo (`static const tbox_html_node *tbox_css_selector_hovered_node = NULL;`
  — o único global autorizado por esta tarefa, ver acima) +
  `tbox_css_selector_set_hover_context` (só atribui) + o branch de
  pseudo-classe: nome (case-insensitive) `"hover"` casa se e somente se
  `node == tbox_css_selector_hovered_node`.
- `tests/css_selector/test_selector.c` — casos novos (grupo
  `css_selector` já registrado)

**Casos de teste mínimos:** sem chamar `set_hover_context` nunca (valor
inicial `NULL`), `:hover` não casa com nada (mesmo comportamento de
antes — regressão); depois de `set_hover_context(node_x)`, um seletor
`:hover` sozinho casa com `node_x` e NÃO casa com nenhum outro nó; um
seletor composto tipo `button:hover` casa só se o nó for `<button>` E for
`node_x` ao mesmo tempo (testar com um `<button>` que não é `node_x`, e
com um `<div>` que é `node_x` — nenhum dos dois deve casar); chamar
`set_hover_context(NULL)` depois de já ter setado um nó faz `:hover`
voltar a não casar com nada (simula "ponteiro saiu de cima do elemento").
**Nota pra quem escrever os testes:** como o estado é global/estático,
resetar pra `NULL` ao final de cada bloco de teste que usa isso, pra não
vazar estado entre casos de teste que rodam em sequência no mesmo
binário.

**Critério de pronto:** `ctest --test-dir build -R '^css_selector$'`
verde + suíte inteira sem regressão.

---

### Tarefa 4 — Output Display: posição corrente do ponteiro
**Depende de:** nada (backend Wayland já rastreia posição internamente
desde a v1, esta tarefa só expõe uma leitura). **Bloqueia:** Tarefa 5.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "`:hover`" → o bloco "Output Display (Wayland backend) —
posição do ponteiro, não só clique" (assinatura e contrato exatos de
`tbox_backend_wayland_pointer_position`, incluindo a diferença de
`tbox_backend_wayland_take_click`: esta não consome nada, só lê).
`src/output/tbox_backend_wayland.c` atual (`pointer_x`/`pointer_y`,
`pointer_has_focus` — estado já mantido pelo listener de `motion`/
`enter`/`leave` desde a v1, é o que esta função vai expor).

**Arquivos a editar:**
- `include/tbox/output.h` — adicionar a declaração
- `src/output/tbox_backend_wayland.c` — implementar: retorna
  `pointer_x`/`pointer_y` em `out_x`/`out_y` e `true` se
  `pointer_has_focus`; `false` (sem escrever nada) se `backend == NULL`,
  algum ponteiro de saída for `NULL`, ou o ponteiro não estiver sobre
  esta superfície

**Sem teste automatizado** (mesmo motivo do resto do backend Wayland:
não testável sem compositor — ver `tests/output/test_raster.c`, que
testa só a metade pura).

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde (com e sem Wayland disponível).

---

## Tier 1 — depende de Tier 0

### Tarefa 5 — Orchestration: `:hover` + bubbling completo + `stopPropagation` + unbind
**Depende de:** Tarefa 3 (`tbox_css_selector_set_hover_context`), Tarefa 4
(`tbox_backend_wayland_pointer_position`) — mergeadas. **Bloqueia:**
Tarefa 6.

**Por que numa tarefa só:** hover e bubbling/unbind são funcionalmente
independentes um do outro, mas ambos mexem exatamente nos mesmos
arquivos (`tbox_context.c`/`context.h`, e a fiação em `tbox_app.c`) —
fazer em tarefas separadas só criaria risco de conflito de merge sem
nenhum ganho real de paralelismo (não dá pra rodar duas tarefas em
paralelo no mesmo arquivo de qualquer jeito).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" inteira — os blocos "Orchestration (`tbox_context`)" e
"Application (`tbox_app_step`)" dentro de "`:hover`" (assinatura de
`tbox_context_update_hover`, onde `hovered_node` mora dentro de
`tbox_context`, por que NÃO é parte da `frame_arena`) e a seção "Bubbling
completo + `stopPropagation` + desregistro de handler" inteira (nova
assinatura de `tbox_context_click_handler` retornando `bool`, novo
retorno de `tbox_context_on_click` como handle `int`,
`tbox_context_unbind_click`, e a nova ordem de dispatch "por nível de
ancestral" em vez de "por registro").

**Arquivos a editar:**
- `include/tbox/context.h` — `tbox_context_update_hover`;
  `tbox_context_click_handler` muda de `void` pra `bool` de retorno;
  `tbox_context_on_click` muda de retornar `bool` pra retornar `int`
  (handle, `-1` em erro); `tbox_context_unbind_click` novo
- `src/context/tbox_context.c` — `tbox_context` ganha `const
  tbox_html_node *hovered_node` (campo com vida própria, sobrevive ao
  reset da `frame_arena`) e um id monotônico pra gerar handles de
  binding; implementar `tbox_context_update_hover` (hit-test + comparação
  + atualização, mesmo contrato de retorno "valor é o sinal" que
  `tbox_context_dispatch_click` já usa); `tbox_context_run_frame` chama
  `tbox_css_selector_set_hover_context(ctx->hovered_node)` imediatamente
  antes de `tbox_style_resolve_tree` (nunca deixar essa chamada "velha"
  de um frame anterior); reescrever `tbox_context_dispatch_click` pra
  bubbling por nível de ancestral (subir `node->parent` uma vez só,
  testando TODOS os registros ativos em cada nível; todo registro que
  casa naquele nível dispara, em ordem de registro entre os que casam no
  mesmo nível; se um handler retornar `false`, parar de subir —
  nenhum registro em ancestral mais distante dispara); implementar
  `tbox_context_unbind_click` (remove o registro do vetor interno, ou
  marca tombstone — a escolha de implementação fica a critério de quem
  implementa, contanto que um binding removido nunca mais dispare)
- `include/tbox/app.h` / `src/app/tbox_app.c` — `tbox_app_step` passa a
  chamar `tbox_backend_wayland_pointer_position` e repassar o resultado
  pra `tbox_context_update_hover` a cada tick, incorporando o retorno na
  mesma decisão local de "recomputa este tick" que já combina
  clique+resize

**Nenhuma mudança em `example/tbox_app.c` nesta tarefa** — o handler
`on_box_click` lá vai ficar com assinatura desatualizada (`void` em vez
de `bool`) até a Tarefa 7; isso quebra SÓ o alvo `example/tbox_app_demo`
(alvo CMake separado, não faz parte de `tbox_tests`), esperado e
documentado na "Convenção" no topo deste arquivo.

**Testes:** em `tests/context/test_context.c` (grupo `context` já
registrado): `tbox_context_update_hover` com uma posição dentro da caixa
de um elemento muda `ctx`'s estado de hover e retorna `true`; chamar de
novo com a MESMA posição retorna `false` (nada mudou); `has_position =
false` (ponteiro fora da janela) desfaz um hover anterior e retorna
`true` se havia algo em hover antes; um seletor CSS de autor usando
`:hover` (ex.: `.box:hover { background-color: ... }`) resolvido via
`tbox_context_run_frame` DEPOIS de `update_hover` apontar pro elemento
produz a cor de hover no `FILL_RECT`, e a cor normal quando não há hover
— prova end-to-end; clique num nó aninhado com handlers registrados em
DOIS ancestrais diferentes dispara os dois, do mais próximo pro mais
distante (checar ordem via um contador/log no teste); um handler que
retorna `false` impede um handler registrado num ancestral mais distante
de disparar (`stopPropagation`); `tbox_context_unbind_click` seguido de
um novo `tbox_context_dispatch_click` no mesmo ponto não dispara mais
aquele handler; `tbox_context_unbind_click` com um handle inválido
retorna `false` sem crashar.

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde SÓ pra `tbox_static`/`tbox_shared`/`tbox_tests` (não pro
`example/tbox_app_demo`, quebra esperada — ver "Convenção") +
`ctest --test-dir build -R '^context$'` verde + suíte inteira sem
regressão.

---

## Tier 2 — depende de Tier 1

### Tarefa 6 — Application: leitura de HTML/CSS de arquivo
**Depende de:** Tarefa 5 (mesmo arquivo, `tbox_app.c`/`app.h`) —
mergeada.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "Application — leitura de arquivo externo" inteira
(assinaturas de `tbox_app_create_from_files`/`_with_config`, contrato de
`css_path == NULL`, por que os buffers de leitura não precisam
sobreviver além da chamada).

**Arquivos a editar:**
- `include/tbox/app.h` — as duas declarações novas
- `src/app/tbox_app.c` — implementar: um helper local de leitura de
  arquivo (mesmo padrão já usado em `example/css_cascade_origins.c`/
  vários `tests/*/read_file()` — ler o arquivo inteiro pra um buffer
  malloc'd), chamar `tbox_app_create`/`_create_with_config` com os bytes
  lidos (via a mesma função interna compartilhada que ambas já usam,
  ver Tarefa 4 da v2), liberar os buffers de leitura logo em seguida.
  `css_path == NULL` vira CSS vazio (`""`, length 0), não erro.
  `html_path == NULL` é erro (retorna `NULL`).

**Sem grupo de teste próprio** (Application não é unit-testada, mesmo
padrão de sempre — validação é smoke-test via exemplo, Tarefa 7).

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde só pra `tbox_static`/`tbox_shared`/`tbox_tests` (mesma
quebra esperada de `example/tbox_app_demo` da Tarefa 5, ainda não
corrigida) + `ctest --test-dir build` sem regressão.

---

## Tier 3 — fatia vertical v3

### Tarefa 7 — Fatia vertical v3 completa (exemplo + validação)
**Depende de:** Tarefa 6 (mergeada) — e, transitivamente, todas as
outras (é a única tarefa que efetivamente conserta o alvo
`example/tbox_app_demo`, quebrado desde a Tarefa 5).

**Exceção de escopo:** por ser a única tarefa do seu tier, esta pode
tocar `example/CMakeLists.txt` se precisar (ex.: uma
`target_compile_definitions` apontando pro caminho dos novos arquivos de
fixture, mesmo padrão de `TBOX_EXAMPLE_HTML_PATH`/
`TBOX_TEST_LIBERATION_SANS_PATH` já usados em outros lugares) — sem
risco de conflito, já que nada mais roda em paralelo com ela.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v3 — Interatividade
Avançada" → "Fatia vertical v3 — critério de 'pronto'" inteira.

**Trabalho:**
1. Criar arquivos de fixture de verdade em disco (ex.:
   `example/tbox_app_demo.html` + `example/tbox_app_demo.css`, ou nomes
   parecidos) com: o `<div class="box off">` clicável da v1/v2 (mantido,
   sem regressão), `<h1>`…`<h6>` sem CSS de tamanho/negrito (UA
   stylesheet, v2), um `<p>` misturando `<b>`/`<em>` que quebra linha
   (v2), MAIS pra v3: um elemento cujo `background-color` muda com
   `:hover` (via CSS de autor, ex. `.hoverable:hover { background-color:
   ...; }`), pelo menos dois handlers de clique registrados em
   ancestrais diferentes do mesmo elemento demonstrando bubbling + um
   cenário de `stopPropagation`, um handler que desregistra outro (ou a
   si mesmo) via `tbox_context_unbind_click` depois do primeiro clique, e
   um handler que troca texto via `tbox_html_node_set_text_content`.
2. Migrar `example/tbox_app.c` pra `tbox_app_create_from_files` (ou
   `_with_config`, se fizer sentido usar um config customizado aqui — a
   critério de quem implementa) em vez das strings `TBOX_APP_DEMO_HTML`/
   `_CSS` fixas no código. Corrigir `on_box_click` (e qualquer outro
   handler) pra nova assinatura (`bool` de retorno — `return true;` ao
   final, a menos que o cenário de `stopPropagation` peça `false`
   deliberadamente nalgum handler específico).
3. Validação: mesmo padrão das fatias verticais anteriores —
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   tudo verde (incluindo `example/tbox_app_demo` agora), e, se
   `WAYLAND_DISPLAY` estiver setado (compositor real disponível), um
   smoke-test via `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que abre,
   roda e fecha sozinho sem crash. Simulação de clique/movimento de mouse
   real continua fora de alcance neste ambiente (sem `ydotool`/`wtype`) —
   mesmo fallback já aceito nas fatias anteriores: a prova de
   correção fica nos testes de integração da Tarefa 5
   (`tests/context/test_context.c`).

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação acima roda sem erro — este é o
"pronto" da v3 inteira, não só desta tarefa.
