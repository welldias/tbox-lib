# tbox — Tarefas da v2

Quebra da seção "v2 — Fidelidade Visual" do `ARCHITECTURE.md` em tarefas
executáveis por agentes sem contexto desta conversa. Cada tarefa abaixo é
auto-contida: aponta para a subseção exata do `ARCHITECTURE.md` (a fonte
da verdade de *o quê* construir) e acrescenta só o que esse documento não
cobre — caminho de arquivo, como registrar teste, como verificar que ficou
pronto.

(Este arquivo substitui a quebra de tarefas da v1, que está completa — ver
`ARCHITECTURE.md` para o design de cada camada v0/v1 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Diferente do v0/v1: a v2 NÃO consegue manter "uma tarefa = um diretório
de módulo" o tempo todo.** O motivo é estrutural, não descuido de
planejamento: `tests/CMakeLists.txt` builda **um único binário**
(`tbox_tests`) linkado contra `tbox_static`, que por sua vez agrega TODO
`src/*.c` num só alvo de build (`add_library(tbox_static STATIC
${TBOX_SOURCES})`, glob recursivo). Isso significa que **nenhum teste
roda via `ctest` enquanto QUALQUER arquivo de QUALQUER módulo não
compilar** — diferente do v1, onde a única quebra esperada
(`tbox_app_demo`) vivia num alvo de exemplo separado, sem afetar
`tbox_static`/`tbox_tests`.

A v2 muda assinaturas de funções já existentes e usadas por módulos já
mergeados (`tbox_style_resolve_tree`, `tbox_layout_build`,
`tbox_layout_box`) — diferente do v0 (só adicionava módulos novos, nada
existente dependia deles ainda) e do v1 (só quebrou UM alvo de exemplo,
fora da biblioteca core). Por isso, os tiers abaixo são maiores/menos
granulares que o padrão anterior nos pontos onde essa quebra em cascata
aconteceria — cada tarefa faz o mínimo necessário nos módulos que ela
quebraria, mesmo que esses módulos "pertençam" a uma tarefa posterior, pra
manter `tbox_static`/`tbox_tests` **sempre buildável e testável ao final
de cada tarefa**. Onde isso acontece, a tarefa deixa explícito qual trecho
é "patch mínimo, mecânico, será reescrito por completo por uma tarefa
posterior" vs. o trabalho "de verdade" daquela tarefa.

Fora isso, a convenção de sempre continua valendo: nenhuma tarefa cria
módulo novo nem grupo de teste novo nesta versão (todas estendem módulos/
grupos já existentes: `style`, `font`, `layout`, `render`, `context`), e
`include/tbox/tbox.h`/`src/CMakeLists.txt`/`tests/CMakeLists.txt`/
`tests/main.c` não precisam de passo de integração.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

---

## Tier 0 — paralelo

### Tarefa 1 — Style: `font-size`/`font-weight` + cascade multi-fonte
**Depende de:** nada. **Bloqueia:** Tarefa 3 (Layout Tree/Render/
Orchestration/Application).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v2 — Fidelidade Visual" →
"Style — `font-size` e `font-weight`" inteira (campos novos de
`tbox_style`, algoritmo de resolução de `font-size` em `em`/`%`/`px`
contra o pai, herança de `font-weight`, e a nova assinatura de
`tbox_style_resolve_tree`). `include/tbox/style.h`/`src/style/tbox_style.c`
atuais (onde `display`/`width`/`margin`/`color` já são resolvidos — mesmo
padrão a seguir pros 2 campos novos). `include/tbox/css_cascade.h`
(`tbox_css_cascade_source`, `tbox_css_cascade_resolve` — a primitiva
multi-fonte que `tbox_style_resolve_tree` passa a chamar no lugar de
`tbox_css_cascade_resolve_stylesheet`).

**Arquivos a editar:**
- `include/tbox/style.h` — `font_size`/`font_weight_bold` em `tbox_style`;
  nova assinatura de `tbox_style_resolve_tree(arena, root, sources,
  source_count)`
- `src/style/tbox_style.c` — implementar
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado)

**Patch mínimo e mecânico em `src/context/tbox_context.c` E
`tests/layout/test_layout.c` (só isto, nada mais — nota registrada depois
da execução real desta tarefa: `tests/layout/test_layout.c` também chama
`tbox_style_resolve_tree` diretamente, em ~8 call sites, pra montar sua
própria tabela de estilos nos testes; como `tbox_tests` linka tudo num
binário só, mudar a assinatura sem tocar esses call sites quebra a suíte
inteira do mesmo jeito que não tocar `tbox_context.c` quebraria):**
`tbox_context_run_frame` chama hoje
`tbox_style_resolve_tree(&ctx->frame_arena, root, ctx->stylesheet)`. Troque
essa chamada — e cada chamada equivalente em `tests/layout/test_layout.c`
— por um array de 1 elemento — `tbox_css_cascade_source
source = { ctx->stylesheet, TBOX_CSS_ORIGIN_AUTHOR };` (ou o stylesheet
local equivalente em cada teste) seguido de
`tbox_style_resolve_tree(&ctx->frame_arena, root, &source, 1)` — que
preserva exatamente o comportamento atual (só CSS de autor, sem UA
stylesheet ainda). **Não** adicione `ua_stylesheet` nem toque em mais
nada de `tbox_context` — isso é escopo da Tarefa 4, que vai reescrever
este mesmo trecho por completo.

**Casos de teste mínimos:** `font-size` ausente herda o do pai (raiz sem
pai → 16px); `"2em"` resolve pra 2× o `font_size` do pai; `"150%"` resolve
igual a `"1.5em"`; `"24px"` é absoluto, independe do pai; `font-weight:
bold` → `font_weight_bold = true`; ausente/`"normal"` → herda do pai (ou
`false` sem pai); `tbox_style_resolve_tree` com 2 `tbox_css_cascade_source`
(uma `USER_AGENT`, uma `AUTHOR` declarando a MESMA propriedade) resolve
pra o valor do autor (prioridade de origem, exercitando a assinatura nova
de verdade, não só com 1 fonte).

**Critério de pronto:** `cmake -S . -B build && cmake --build build` verde
(inclusive `tbox_context.c`, graças ao patch mínimo acima) +
`ctest --test-dir build -R '^style$'` verde + suíte inteira
(`ctest --test-dir build`) sem regressão.

---

### Tarefa 2 — Fonte/Texto: cache de faces por (peso, tamanho)
**Depende de:** nada (puramente aditivo — não quebra nenhum call site
existente, já que `tbox_font_face`/`tbox_font_source` não mudam). **Bloqueia:**
Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v2 — Fidelidade Visual" →
"Fonte / Texto — cache de faces por (peso, tamanho)" inteira — tipos e
assinaturas de `tbox_font_face_cache`, por que copia os bytes na criação,
por que duas font sources (não uma resolvida duas vezes) fica a cargo de
quem monta o cache (a Application, mais tarde — esta tarefa só implementa
o cache em si, recebendo os bytes já prontos). `include/tbox/font.h`
atual (`tbox_font_face_load`, padrão de ownership "vida própria, sem
arena do chamador" que o cache também segue).

**Arquivos a editar:**
- `include/tbox/font.h` — `tbox_font_face_cache` (opaco) +
  `_create`/`_destroy`/`_get`
- `src/font/tbox_font_face_cache.c` — novo arquivo, implementação (vetor
  arena-ou-malloc-backed de `{bool bold; double size_px; tbox_font_face
  *face;}`, busca linear, `tbox_font_face_load` sob demanda em cache miss)
- `tests/font/test_font.c` — casos novos (grupo `font` já registrado)

**Nenhum patch em outro módulo é necessário** — `tbox_context`/
`tbox_app` continuam usando `tbox_font_face` sozinho até a Tarefa 3/4
migrarem pra usar o cache.

**Casos de teste mínimos:** `_create` com bytes válidos (regular + bold,
reaproveitar o helper de leitura de `external/liberation-sans/...ttf` já
usado em `test_font.c`) não retorna `NULL`; `_get(regular, 16)` não
retorna `NULL`; duas chamadas `_get(regular, 16)` devolvem o MESMO
ponteiro (cache hit, não recarrega); `_get(bold, 16)` devolve ponteiro
DIFERENTE de `_get(regular, 16)`; `_get(regular, 32)` devolve ponteiro
diferente de `_get(regular, 16)` (tamanhos diferentes = faces diferentes);
`_destroy` não crasha.

**Critério de pronto:** `ctest --test-dir build -R '^font$'` verde + suíte
inteira sem regressão.

---

## Tier 1 — depende de Tier 0

### Tarefa 3 — Layout Tree + Render Pipeline + fiação mínima de Orchestration/Application
**Depende de:** Tarefa 1, Tarefa 2 (mergeadas). **Bloqueia:** Tarefa 4.

**Por que uma tarefa só, cobrindo 4 áreas:** Layout Tree remove os campos
`text`/`font` de `tbox_layout_box` (viram `text_runs`/`text_run_count`) e
muda `tbox_layout_build` pra receber `tbox_font_face_cache*` em vez de
`tbox_font_face*`. Isso quebra `src/render/tbox_render.c` (lê os campos
removidos) E `src/context/tbox_context.c` (chama `tbox_layout_build` com
o `font` antigo) — não há como fazer só a parte "Layout Tree" e deixar
`tbox_static` buildando, então esta tarefa cobre o suficiente de Render/
Orchestration/Application pra manter tudo verde (ver "Convenção" no topo
deste documento). A parte de Orchestration/Application aqui é
DELIBERADAMENTE mínima — a UA stylesheet de verdade e o config struct são
a Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v2 — Fidelidade Visual" →
"Layout Tree — inline formatting context real" (tipos, algoritmo de
quebra de linha, fusão de runs, altura multi-linha) e "Render Pipeline —
múltiplos runs por caixa" (o ajuste, pequeno, do lado do Render).
`include/tbox/layout.h`/`src/layout/tbox_layout.c` e
`include/tbox/render.h`/`src/render/tbox_render.c` atuais.

**Arquivos a editar (Layout Tree e Render Pipeline, trabalho "de
verdade"):**
- `include/tbox/layout.h` — `tbox_layout_text_run`; `tbox_layout_box`
  troca `text`/`font` por `text_runs`/`text_run_count`; nova assinatura de
  `tbox_layout_build(arena, root, styles, fonts, viewport_width,
  viewport_height)` (`fonts` é `tbox_font_face_cache*`)
- `src/layout/tbox_layout.c` — implementar o algoritmo de inline
  formatting descrito no `ARCHITECTURE.md` (lista fixa h1-h6/p continua o
  critério de quem ganha texto; filhos TEXT contribuem palavras na face
  do próprio elemento; filhos ELEMENT com `style->display == INLINE`
  recursam um nível; quebra greedy por palavra via
  `tbox_font_face_cache_get` + `tbox_font_measure_text`; funde palavras
  consecutivas de mesma face num run; altura `auto` = soma das alturas de
  linha)
- `tests/layout/test_layout.c` — casos novos (grupo `layout`)
- `include/tbox/render.h` — provavelmente sem mudança de assinatura (só
  doc comment, se precisar)
- `src/render/tbox_render.c` — trocar leitura de `box->text`/`box->font`
  por iterar `box->text_runs[0..text_run_count)`, um `TEXT_RUN` por run,
  `color` sempre `box->style->color`
- `tests/render/test_render.c` — casos novos (grupo `render`)

**Patch mínimo e mecânico em `src/context/tbox_context.c` e
`src/app/tbox_app.c` (só isto — a UA stylesheet/config completos são a
Tarefa 4):**
- `tbox_context`: troque o campo `tbox_font_face *font` por
  `tbox_font_face_cache *fonts` (ainda emprestado, mesma convenção de
  ownership). `tbox_context_open` troca o parâmetro `tbox_font_face
  *font` por `tbox_font_face_cache *fonts`, só armazenando o ponteiro
  (sem lógica nova). `tbox_context_run_frame` passa `ctx->fonts` pro novo
  `tbox_layout_build`.
- `tbox_app.c`: `tbox_app_create` hoje carrega UMA `tbox_font_face` via
  fontconfig e passa pra `tbox_context_open`. Troque por: resolver a MESMA
  query de sempre (regular, não-bold), e construir o cache reaproveitando
  os MESMOS bytes pros dois slots —
  `tbox_font_face_cache_create(data, size, data, size)` — **isso é um
  placeholder deliberado**: negrito vai renderizar IGUAL a regular até a
  Tarefa 4 trocar isso por duas font sources de verdade (regular + bold).
  Documente isso com um comentário no código apontando pra Tarefa 4/
  ARCHITECTURE.md, pra não parecer um bug esquecido. `tbox_app_close`
  destrói o cache no lugar da face única.

**Casos de teste mínimos (Layout Tree):** texto curto (cabe numa linha)
continua com 1 `text_run`, comportamento inalterado; texto mais largo que
o container gera 2+ `text_runs` em `y` crescente (quebrou linha); `<p>`
com um filho `<b>` gera runs com `font` diferente entre o trecho normal e
o trecho em negrito; altura `auto` da caixa cresce proporcionalmente ao
número de linhas; uma palavra sozinha mais larga que o container ainda
vira 1 run (sem quebra no meio da palavra), `rect.width` maior que a
largura disponível; `<div>` com texto dentro continua SEM `text_runs`
(lista fixa de tags inalterada — teste de regressão); `<h1>` resolve numa
face de tamanho maior que `<p>` (comparar ponteiros de face via
`tbox_font_face_cache_get` com os mesmos parâmetros, ou comparar
`tbox_font_measure_text` do mesmo texto nas duas faces).

**Casos de teste mínimos (Render Pipeline):** caixa com N `text_runs`
emite exatamente N `TEXT_RUN`, na ordem; caixa com `text_run_count == 0`
não emite nenhum; todos os `TEXT_RUN` de uma mesma caixa usam a MESMA
`color` (`box->style->color`), mesmo com `font` diferente entre eles.

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
verde (biblioteca inteira, graças aos patches mínimos) +
`ctest --test-dir build -R '^layout$'` e `-R '^render$'` verdes + suíte
inteira sem regressão.

---

## Tier 2 — depende de Tier 1

### Tarefa 4 — Orchestration: UA stylesheet + config + Application: cache de verdade
**Depende de:** Tarefa 3 (mergeada). **Bloqueia:** Tarefa 5.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v2 — Fidelidade Visual" →
"CSS Cascade / Orchestration — folha de estilo user-agent" inteira
(conteúdo exato da UA stylesheet, subseção "Configuração da UA
stylesheet" com `tbox_ua_style_config`/`tbox_ua_style_font_config`/
`tbox_ua_style_margin_config`/`_default()`, `tbox_context_open_with_config`)
e "Application / Orchestration — fiação do cache de fontes e do config"
(o par `tbox_app_create`/`_create_with_config`, por que duas
`tbox_font_source` em vez de uma resolvida duas vezes).

**Arquivos a editar:**
- `include/tbox/context.h` — `tbox_ua_style_font_config`,
  `tbox_ua_style_margin_config`, `tbox_ua_style_config`,
  `tbox_ua_style_config_default()`, `tbox_context_open_with_config`
- `src/context/tbox_context.c` — `tbox_context` ganha
  `tbox_css_stylesheet *ua_stylesheet`; geração do texto CSS da UA
  stylesheet a partir dos campos do config (template + `snprintf`, ver
  ARCHITECTURE.md pro conteúdo exato dos seletores/propriedades — só os
  NÚMEROS vêm do config); `tbox_context_open` passa a chamar
  `tbox_context_open_with_config` internamente com
  `tbox_ua_style_config_default()`; `tbox_context_close` destrói
  `ua_stylesheet`; `tbox_context_run_frame` monta o array de 2
  `tbox_css_cascade_source` (`ua_stylesheet` como `USER_AGENT`,
  `stylesheet` como `AUTHOR`) **substituindo** o array de 1 elemento que a
  Tarefa 1 deixou como patch mínimo
- `include/tbox/app.h` — `tbox_app_create_with_config`
- `src/app/tbox_app.c` — troca o placeholder da Tarefa 3 (mesmos bytes
  pros dois slots) por duas `tbox_font_source_fontconfig` de verdade
  (uma query `{bold: false}`, outra `{bold: true}`), cada uma resolvida
  uma única vez, os dois pares (dados, tamanho) alimentando
  `tbox_font_face_cache_create`, as duas sources destruídas em seguida
  (ver ARCHITECTURE.md pro motivo de duas sources, não uma resolvida duas
  vezes); `tbox_app_create` chama `tbox_context_open` (sem config);
  `tbox_app_create_with_config` chama `tbox_context_open_with_config`

**Testes:** em `tests/context/test_context.c` (grupo `context` já
registrado): `tbox_context_open` (sem config) aplicado a um `<h1>oi</h1>`
sem CSS de autor produz uma caixa com fonte/tamanho maior que um `<p>oi</p>`
equivalente (prova end-to-end de que a UA stylesheet está entrando no
cascade); CSS de autor `h1 { font-size: 10px }` produz um `<h1>` MENOR que
o UA default de 2em (autor vence UA — prioridade de origem exercitada de
verdade agora, com 2 fontes reais); `tbox_context_open_with_config` com
`heading_em[0]` customizado (ex. `5.0`) muda o tamanho resolvido de `<h1>`
proporcionalmente; um `<p>` sem CSS de autor tem margin não-zero refletida
no `margin_box` vs. `content_box`.

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
verde + `ctest --test-dir build -R '^context$'` verde + suíte inteira sem
regressão.

---

## Tier 3 — fatia vertical v2

### Tarefa 5 — Fatia vertical v2 completa (exemplo + validação)
**Depende de:** Tarefa 4 (mergeada).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v2 — Fidelidade Visual" →
"Fatia vertical v2 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender o HTML/CSS do exemplo (`example/tbox_app.c`, que a v1 já
   deixou com o `<div class="box off">` clicável — preservar isso, sem
   regressão de interatividade) com `<h1>`…`<h6>` (sem CSS de autor pra
   tamanho/negrito — só a UA stylesheet) e um `<p>` com texto longo o
   bastante pra quebrar linha na largura da janela, misturando `<b>`/
   `<em>` (ex.: `<p>texto normal <b>em negrito</b> continuando até
   quebrar a linha sozinho...</p>`).
2. Nenhuma mudança de API deveria ser necessária em `tbox_app.c` além do
   conteúdo HTML/CSS acima — `tbox_app_create` já resolve a UA stylesheet
   sozinho. Só ajuste se algo do fluxo v1 (registro de clique, loop)
   precisar mudar por causa dos campos novos de `tbox_layout_box` (não
   deveria).
3. Validação: mesmo padrão da Tarefa 5 da v1 — rodar
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   (tudo verde) e, se o ambiente tiver um compositor Wayland real
   disponível (`WAYLAND_DISPLAY` setado), um smoke-test via
   `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que a janela abre, mostra o
   conteúdo, e fecha sozinha sem crash. Captura de tela é bem-vinda como
   evidência visual (headings em tamanhos diferentes, negrito, parágrafo
   quebrado em várias linhas) mas opcional — não persiga se não for
   simples no ambiente disponível.

**Critério de pronto:** build limpo + suíte inteira passando + a
validação acima roda sem erro — este é o "pronto" da v2 inteira, não só
desta tarefa.
