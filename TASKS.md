# tbox — Tarefas do v0

Quebra do `ARCHITECTURE.md` em tarefas executáveis por agentes sem
contexto desta conversa. Cada tarefa abaixo é auto-contida: aponta pra
seção exata do `ARCHITECTURE.md` (a fonte da verdade de *o quê* construir)
e acrescenta só o que esse documento não cobre — caminho de arquivo, como
registrar teste, como verificar que ficou pronto.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Nenhuma tarefa deve editar os
arquivos compartilhados abaixo — isso evita conflito de merge entre
tarefas paralelas do mesmo tier:
- `include/tbox/tbox.h` (agrega todos os headers públicos)
- `src/CMakeLists.txt` (glob de diretórios de módulo)
- `tests/CMakeLists.txt` (lista de grupos de teste)
- `tests/main.c` (registro de grupos de teste)

Depois que as tarefas de um tier terminam, um passo de **integração**
(feito à parte, não por uma tarefa) adiciona as poucas linhas necessárias
nesses 4 arquivos. Exceção: a Tarefa 1 (build system) *é* sobre esses
arquivos compartilhados — como é a única do seu tier, não tem conflito.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

---

## Tier 0 — paralelo, sem dependência de camada nova

### Tarefa 1 — Build system: FreeType core, Fontconfig opcional, vendorizar fonte
**Depende de:** nada. **Bloqueia:** Tarefa 5 (Fonte/Texto), Tarefa 9 (Output Display).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Build system (v0)" (as 4
decisões completas) e o `CMakeLists.txt`/`example/CMakeLists.txt` atuais
(padrão de `FetchContent` do FreeType e de detecção opcional do Wayland
via `pkg_check_modules(... QUIET ...)`, já implementados lá — é o modelo a
replicar).

**Trabalho:**
1. Mover o bloco `FetchContent_Declare(freetype ...)` +
   `FetchContent_MakeAvailable(freetype)` de `example/CMakeLists.txt` para
   o `CMakeLists.txt` raiz (antes de `add_subdirectory(src)`), e linkar
   `freetype` em `tbox_static`/`tbox_shared` dentro de `src/CMakeLists.txt`
   (`target_link_libraries`). Remover a declaração duplicada de
   `example/CMakeLists.txt`, que passa a só linkar o alvo `freetype` já
   disponível.
2. Em `src/CMakeLists.txt`, detectar Fontconfig via
   `pkg_check_modules(FONTCONFIG QUIET fontconfig)` (mesmo padrão de
   `WAYLAND_CLIENT` em `example/CMakeLists.txt`). Guardar o resultado numa
   variável (ex.: `TBOX_FONTCONFIG_FOUND`) que a Tarefa 5 vai usar para
   incluir ou não `tbox_font_source_fontconfig.c` no glob de fontes — como
   esse arquivo ainda não existe, essa tarefa só prepara a variável e
   imprime `message(WARNING ...)` se não encontrado; não precisa gatear
   nada ainda (nenhum arquivo pra gatear).
3. Criar `external/liberation-sans/` com `LiberationSans-Regular.ttf`
   (SIL Open Font License) + `LICENSE` (texto da OFL) + `README.md`
   (origem/versão — mesmo padrão de `external/utf8.h/README.md`). Se o
   ambiente não tiver acesso à internet para baixar o `.ttf`, extrair de
   um pacote `liberation-fonts`/`liberation-fonts-fonts` já instalado no
   sistema (comum em distros Linux) — path típico
   `/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf` ou
   similar; ajustar conforme a distro.

**Critério de pronto:** `cmake -S . -B build && cmake --build build`
continua verde (biblioteca + exemplos existentes, incluindo `tbox_wayland`,
buildam sem mudança de comportamento) e `external/liberation-sans/`
existe com os 3 arquivos.

---

### Tarefa 2 — Base: `tbox_string_collapse_whitespace`
**Depende de:** nada. **Bloqueia:** Tarefa 6 (Layout Tree).

**Leia primeiro:** `ARCHITECTURE.md`, trecho sobre `tbox_string_collapse_whitespace`
na seção Layout Tree (assinatura e comportamento exatos: colapsa
sequências de espaço/tab/quebra-de-linha em um único `' '`, apara bordas —
`white-space: normal`). `src/base/tbox_string.h`/`.c` (arquivos a editar,
já existem) para o padrão de `tbox_string_builder` (`append_byte`,
`append_view`, `finish`) que essa função deve usar internamente.

**Arquivos a editar (existentes, não criar diretório novo):**
- `src/base/tbox_string.h` — adicionar a declaração
- `src/base/tbox_string.c` — implementar
- `tests/base/test_string.c` — adicionar casos de teste (arquivo já
  existe, grupo de teste `string` já registrado — **não precisa tocar em
  `tests/main.c` nem `tests/CMakeLists.txt`**)

**Casos de teste mínimos:** string sem espaço extra (idempotente); espaços
múltiplos no meio colapsam pra um; tabs/quebras de linha tratados como
espaço; espaço só nas bordas é removido; string vazia; string só com
espaço vira vazia.

**Critério de pronto:** `ctest --test-dir build -R '^string$'` verde.

---

### Tarefa 3 — HTML Parser: `tbox_html_node_text_content`
**Depende de:** nada. **Bloqueia:** Tarefa 6 (Layout Tree).

**Leia primeiro:** `ARCHITECTURE.md`, trecho sobre `tbox_html_node_text_content`
no resumo do topo e na seção Layout Tree (equivalente a `textContent` do
DOM: concatena todo nó TEXT descendente em ordem de documento, ignora
estrutura de elementos aninhados — `<p>oi <b>mundo</b></p>` vira
`"oi mundo"`). `include/tbox/html_parser.h` e `src/html_parser/tbox_html_node.c`
(onde as outras funções de `tbox_html_node` já vivem — é o lugar certo pra
essa também).

**Arquivos a editar:**
- `include/tbox/html_parser.h` — adicionar a declaração pública:
  `tbox_string_view tbox_html_node_text_content(tbox_arena *arena, const tbox_html_node *node);`
- `src/html_parser/tbox_html_node.c` — implementar (percorrer
  `first_child`/`next_sibling` recursivamente; para nó TEXT, acumular
  `text.text`; para nó ELEMENT, recursar nos filhos; COMMENT/DOCTYPE
  ignorados; construir o resultado com `tbox_string_builder`, arena vinda
  do parâmetro — não necessariamente a mesma arena do documento)
- `tests/html_parser/test_tree.c` — adicionar casos de teste (arquivo já
  existe, grupo `html_parser_tree` já registrado — **não precisa tocar em
  `tests/main.c` nem `tests/CMakeLists.txt`**)

**Casos de teste mínimos:** texto direto simples; texto com elemento
aninhado no meio (`"oi <b>mundo</b>"` → `"oi mundo"`); múltiplos níveis de
aninhamento; nó sem filhos de texto (retorna vazio); comentário no meio é
ignorado.

**Critério de pronto:** `ctest --test-dir build -R '^html_parser_tree$'`
verde.

---

## Tier 1 — paralelo, só depende de camadas já implementadas

### Tarefa 4 — Style layer
**Depende de:** nada (usa só HTML Parser/CSS Parser/CSS Cascade, já
implementados). **Bloqueia:** Tarefa 6 (Layout Tree).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Style (novo)" inteira —
tipos (`tbox_style_length`, `tbox_style_display`, `tbox_style`,
`tbox_style_entry`, `tbox_style_table`), as duas funções
(`tbox_style_resolve` por nó, `tbox_style_resolve_tree` por árvore +
`tbox_style_table_find`), escopo mínimo de propriedades, decisão de
shorthand, e o padrão de ownership de arena (nota em "Convenções" no topo
do `ARCHITECTURE.md` e comentário ao lado de `tbox_style_resolve_tree`).
`include/tbox/css_cascade.h` (`tbox_css_cascade_resolve_stylesheet`,
`tbox_css_computed_style_find` — é o que `tbox_style_resolve` consome).

**Arquivos a criar:**
- `include/tbox/style.h` — API pública, tipos + as 3 funções
- `src/style/tbox_style.c` — implementação
- `tests/style/test_style.c` — testes (grupo de teste novo `style`; ver
  nota de integração no topo deste documento — **não editar
  `tests/main.c` nem `tests/CMakeLists.txt`**, isso é passo de integração)

**Implementação de `tbox_style_resolve_tree`:** percorre a árvore
top-down (pai antes do filho — herança depende disso), chamando
`tbox_css_cascade_resolve_stylesheet(stylesheet, node)` seguido de
`tbox_style_resolve(node, parent_style, &computed)` para cada nó ELEMENT
(nós TEXT/COMMENT/DOCTYPE não têm style próprio, pular); acumula os pares
`(node, style)` num `tbox_vector` arena-backed, expõe como
`tbox_style_table.items`/`count` ao final. Destruir cada
`tbox_css_computed_style` intermediário com `tbox_css_computed_style_destroy`
assim que não precisar mais dele (não é a mesma vida útil do
`tbox_style_table`).

**Casos de teste mínimos:** propriedade explícita vence sobre valor
inicial; `color` herda do pai quando não declarado, `background-color`
não herda (fica no valor inicial mesmo com pai tendo um valor); shorthand
`margin: 1px 2px` expande pros 4 lados corretos; `width`/`height` com
`auto`, `px` e `%` resultam no `tbox_style_length_kind` certo;
`tbox_style_table_find` acha o nó certo e retorna `NULL` pra nó ausente.

**Critério de pronto:** build limpo + testes escritos passam localmente
(rodar o binário de teste direto, já que o grupo ainda não está
registrado em `tests/main.c` — isso é resolvido na integração).

---

### Tarefa 5 — Fonte/Texto layer
**Depende de:** Tarefa 1 (build system: FreeType core + detecção de
Fontconfig + `external/liberation-sans/` vendorizado). **Bloqueia:**
Tarefa 6 (Layout Tree), Tarefa 9 (Output Display).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Fonte / Texto (novo)"
inteira — as três responsabilidades (font source, font face/métricas,
rasterização de glifo), todos os tipos e assinaturas, decisão de
`tbox_font_source_resolve` sempre devolver bytes (não path), escopo
mínimo (16px, um único `tbox_font_face` pro documento inteiro, sem
shaping/kerning).

**Arquivos a criar:**
- `include/tbox/font.h` — API pública completa
- `src/font/tbox_font_source.c` — a interface `tbox_font_source`
  genérica (o `resolve`/`destroy` despachado por function-pointer interno,
  já que cada backend tem sua própria implementação)
- `src/font/tbox_font_source_fontconfig.c` — backend Fontconfig (via
  `FcFontMatch`/`FcPatternGetString`, lê o arquivo resolvido pra memória
  antes de devolver). Só compilar se `TBOX_FONTCONFIG_FOUND` (variável da
  Tarefa 1) — combinar com quem fizer a integração de `src/CMakeLists.txt`
  se esse arquivo precisa ficar de fora do glob quando Fontconfig não
  existe.
- `src/font/tbox_font_source_embedded.c` — backend embutido (ignora
  `query`, sempre devolve os bytes recebidos no construtor)
- `src/font/tbox_font_face.c` — wrapper de `FT_Face`
  (`tbox_font_face_load`/`_destroy`/`_line_height`/`tbox_font_measure_text`
  via `FT_Set_Pixel_Sizes` + soma de advances, sem kerning/shaping) e
  rasterização (`tbox_font_rasterize_glyph` via `FT_Render_Glyph`)
- `tests/font/test_font.c` — testes (grupo novo `font`; não editar
  `tests/main.c`/`tests/CMakeLists.txt`, passo de integração)

**Casos de teste mínimos (usar só o backend `embedded`, com os bytes de
`external/liberation-sans/LiberationSans-Regular.ttf` lidos via um
helper `read_file()` local ao teste, igual ao já existente em
`example/css_cascade_origins.c`):** `tbox_font_face_load` com 16px não
retorna `NULL`; `tbox_font_measure_text` de uma string vazia é 0;
`tbox_font_measure_text` de duas strings onde uma é prefixo visual da
outra dá larguras crescentes (não precisa comparar contra um valor
hardcoded de pixel exato, só a relação); `tbox_font_face_line_height` >
0; `tbox_font_rasterize_glyph` de um caractere ASCII comum devolve
`width`/`height` > 0.

**Critério de pronto:** build limpo (com e sem Fontconfig disponível, se
possível testar os dois) + testes escritos passam localmente contra o
backend embedded.

---

## Tier 2 — Layout Tree

### Tarefa 6 — Layout Tree
**Depende de:** Tarefa 2 (`tbox_string_collapse_whitespace`), Tarefa 3
(`tbox_html_node_text_content`), Tarefa 4 (Style layer), Tarefa 5
(Fonte/Texto layer) — as 4 precisam estar mergeadas (ou, se optarem pelo
atalho de paralelismo por interface descrito na conversa, pelo menos os
headers públicos delas precisam existir e estar congelados). **Bloqueia:**
Tarefa 7 (Render Pipeline).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Layout Tree (novo)" inteira
— responsabilidade, tipos (`tbox_rect`, `tbox_layout_box`), assinatura de
`tbox_layout_build` (com os 4 parâmetros: arena, root, styles, font, mais
viewport width/height), a regra explícita da lista fixa de tags
(`h1`..`h6`, `p`), a ordem de operações pro texto
(`tbox_html_node_text_content` → `tbox_string_collapse_whitespace` →
`tbox_font_measure_text`/`tbox_font_face_line_height`), e a clarificação
de que largura da caixa segue a regra geral (do pai) enquanto o texto só
pode transbordar visualmente. `include/tbox/style.h` e `include/tbox/font.h`
(das tarefas 4 e 5) como as interfaces reais a consumir.

**Arquivos a criar:**
- `include/tbox/layout.h` — `tbox_rect`, `tbox_layout_box`,
  `tbox_layout_build`
- `src/layout/tbox_layout.c` — implementação: fluxo normal (block boxes
  empilhadas verticalmente, largura do pai salvo `width` explícito,
  altura `auto` = altura de linha da fonte pra caixa de texto ou 0 pra
  caixa vazia — v0 não soma altura de múltiplos filhos de texto porque
  cada elemento só tem uma caixa de texto, não uma lista); checagem da
  lista fixa de tags pra decidir se um nó ganha caixa de texto
- `tests/layout/test_layout.c` — testes (grupo novo `layout`; não editar
  arquivos compartilhados)

**Casos de teste mínimos:** `<div style="width:200px;height:100px">` gera
`tbox_layout_box` com `content_box` do tamanho certo; `<div>` sem `width`
explícito herda a largura do viewport; duas `<div>` irmãs empilham
verticalmente (segunda começa onde a primeira termina); `<h1>oi</h1>`
gera caixa com `content_box.height` = `tbox_font_face_line_height`; `<p>oi
mundo</p>` tem largura de caixa igual à do pai (não ao texto), mesmo que
o texto seja mais curto; um `<span>texto</span>` (tag fora da lista fixa)
gera caixa vazia sem texto associado.

**Critério de pronto:** build limpo + testes passam localmente.

---

## Tier 3 — Render Pipeline

### Tarefa 7 — Render Pipeline
**Depende de:** Tarefa 6 (Layout Tree) — ou, no atalho de paralelismo,
só do header `include/tbox/layout.h` congelado. **Bloqueia:** Tarefa 8
(Output Display).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Render Pipeline (novo)"
inteira — tipos (`tbox_paint_op_kind`, `tbox_paint_op`,
`tbox_display_list`), assinatura de `tbox_render_build_display_list`
(arena do chamador, sem `_destroy`), escopo mínimo (`FILL_RECT` por
`background-color` não-transparente em pré-ordem, `TEXT_RUN` por caixa de
texto de heading/parágrafo).

**Arquivos a criar:**
- `include/tbox/render.h` — `tbox_paint_op_kind`, `tbox_paint_op`,
  `tbox_display_list`, `tbox_render_build_display_list`
- `src/render/tbox_render.c` — implementação: percorre `tbox_layout_box`
  em pré-ordem, emite `FILL_RECT` quando `style->background_color.a != 0`,
  emite `TEXT_RUN` quando a caixa tem texto associado (Tarefa 6 precisa
  deixar isso consultável em `tbox_layout_box` — combinar exposição do
  texto/da face na struct se a Tarefa 6 ainda não tiver decidido onde
  guardar isso)
- `tests/render/test_render.c` — testes (grupo novo `render`)

**Casos de teste mínimos:** caixa com `background_color` transparente não
gera `FILL_RECT`; caixa com cor opaca gera exatamente um `FILL_RECT` do
tamanho/posição certos; ordem dos paint ops segue pré-ordem da árvore;
caixa de texto gera um `TEXT_RUN` com o texto certo.

**Critério de pronto:** build limpo + testes passam localmente.

---

## Tier 4 — Output Display

### Tarefa 8 — Output Display
**Depende de:** Tarefa 7 (Render Pipeline), Tarefa 5 (Fonte/Texto), Tarefa
1 (build system — FreeType já resolvido; a parte de detecção Wayland
também precisa migrar de `example/CMakeLists.txt` pra `src/CMakeLists.txt`
nesta tarefa, já que é aqui que o backend real passa a existir em `src/`).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Output Display (novo)"
inteira — a sub-divisão em `tbox_raster_*` (puro, testável sem janela) e
`tbox_backend_wayland_*` (stateful, não testado por unidade), escopo
mínimo (`tbox_raster_fill_rect`/`tbox_raster_text_run`). O código-fonte
de `example/tbox_wayland.c` como base a evoluir (reaproveitar `wl_shm` +
event loop, generalizar `draw_and_attach` para consumir uma
`tbox_display_list` em vez do checkerboard hardcoded).

**Arquivos a criar:**
- `include/tbox/output.h` — `tbox_raster_fill_rect`,
  `tbox_raster_text_run` (parte pública/testável), mais o que for preciso
  expor do backend Wayland pra Orchestration usar (provavelmente um
  handle opaco `tbox_backend_wayland` com open/frame/poll/close — a
  assinatura exata fica a critério de quem implementa, já que
  `ARCHITECTURE.md` não a especifica em detalhe; documentar a decisão
  tomada de volta no `ARCHITECTURE.md` se divergir do esperado)
- `src/output/tbox_raster.c` — rasterizador software puro sobre
  `uint32_t*` XRGB8888 (sem dependência de Wayland)
- `src/output/tbox_backend_wayland.c` — motor do backend Wayland,
  adaptado de `example/tbox_wayland.c`
- `tests/output/test_raster.c` — testes só de `tbox_raster_*` (comparar
  buffer de saída byte a byte contra um resultado esperado; não testa o
  backend Wayland, que não é testável sem compositor)

**Trabalho de CMake nesta tarefa:** mover a detecção condicional de
`wayland-client`/`wayland-protocols`/`xkbcommon`/`wayland-scanner` (hoje
em `example/CMakeLists.txt`) pra `src/CMakeLists.txt`, gating a
compilação de `tbox_backend_wayland.c`; sem esses pacotes, `libtbox`
ainda builda (só sem esse backend).

**Casos de teste mínimos (só `tbox_raster_*`):** `FILL_RECT` pinta os
pixels certos dentro do retângulo e não toca fora dele; dois `FILL_RECT`
sobrepostos, o de ordem posterior vence (paint order); `TEXT_RUN` com um
glifo simples pinta pixels não-transparentes onde o glifo tem cobertura
(não precisa comparar bitmap exato, só que algo foi pintado na região
esperada).

**Critério de pronto:** build limpo (com e sem Wayland disponível) +
testes de `tbox_raster_*` passam localmente.

---

## Tier 5 — Orchestration

### Tarefa 9 — Orchestration (`tbox_context`)
**Depende de:** Tarefa 4 (Style), Tarefa 5 (Fonte/Texto), Tarefa 6
(Layout), Tarefa 7 (Render), Tarefa 8 (Output Display) — todas
mergeadas, esta é a primeira tarefa que de fato integra o pipeline
inteiro ponta-a-ponta.

**Leia primeiro:** `ARCHITECTURE.md`, seção "Orchestration / Main Loop
(novo)" inteira — `tbox_context` (com `document`, `stylesheet`, `font`,
`frame_arena`), `tbox_context_run_frame` (reset da `frame_arena` no
início, chama Style → Layout → Render em sequência, escreve o resultado
em `out_list`), a confirmação de que v0 não usa UA stylesheet nenhuma
(só `TBOX_CSS_ORIGIN_AUTHOR`), e a responsabilidade de hit-testing (busca
linear na árvore de layout).

**Arquivos a criar:**
- `include/tbox/context.h` — `tbox_context`, `tbox_context_run_frame`,
  mais abertura/fechamento do contexto (`tbox_context_open`/`_close` — a
  Application da Tarefa 10 usa isso; assinatura exata a decidir aqui já
  que o doc não a especifica, deve receber html+css como
  string/tamanho e devolver o `tbox_context` pronto, já com
  `tbox_html_parse` + `tbox_font_face_load` feitos)
- `src/context/tbox_context.c` — implementação

**Testes:** dado que isso já integra o pipeline inteiro, um teste de
integração é mais valioso que testes unitários isolados — em
`tests/context/test_context.c`, montar um HTML+CSS pequeno (ex.: um
`<div>` com cor de fundo) e verificar que `tbox_context_run_frame`
produz uma `tbox_display_list` com o `FILL_RECT` esperado, ponta a ponta.

**Critério de pronto:** build limpo + teste de integração passa
localmente.

---

## Tier 6 — Application

### Tarefa 10 — Application (`tbox_app_open`) + fatia vertical completa
**Depende de:** Tarefa 9 (Orchestration).

**Leia primeiro:** `ARCHITECTURE.md`, seção "Application (novo)" e
"Fatia vertical v0 — critério de 'pronto'" inteiras — escopo mínimo
(`tbox_app_open(html, css, width, height)`, roda até fechar, sem API de
mutação), e o critério final de aceite (janela real mostrando `<div>`
com `width`/`height`/`background-color` do CSS de entrada + headings/
parágrafos mostrando texto).

**Arquivos a criar:**
- `include/tbox/app.h` — `tbox_app_open`
- `src/app/tbox_app.c` — implementação: abre `tbox_context` (Tarefa 9),
  abre o backend Wayland (Tarefa 8), roda o loop chamando
  `tbox_context_run_frame` a cada resize/evento relevante, rasteriza via
  `tbox_raster_*` e apresenta o buffer, fecha tudo ao sair (ESC ou
  fechamento pelo compositor — reaproveitar a lógica já existente em
  `example/tbox_wayland.c`, incluindo o padrão `TBOX_WAYLAND_CLOSE_DELAY_MS`
  pra smoke-test automatizado sem intervenção manual)

**Validação final (não é teste unitário, é a demonstração do critério de
"pronto" do v0):** um HTML/CSS de exemplo (pode reaproveitar/estender
`example/index.html` + `example/style.css`, ou criar um novo par
específico) com um `<div>` (`width`/`height`/`background-color`) e pelo
menos um `<h1>` e um `<p>`, aberto via `tbox_app_open`, rodado com
`TBOX_WAYLAND_CLOSE_DELAY_MS` num ambiente com compositor Wayland
disponível (ex.: `Xvfb`/`weston --backend=headless` em CI, ou sessão
gráfica local), confirmando que a janela abre e fecha sem erro. Anexar
uma captura de tela ao relatório final da tarefa como evidência visual.

**Critério de pronto:** build limpo + a validação acima roda sem erro —
este é o "pronto" do v0 inteiro, não só desta tarefa.

---

## Passo de integração (entre tiers, não uma tarefa própria)

Depois que as tarefas de cada tier terminam: editar
`include/tbox/tbox.h` (adicionar os novos `#include`), `src/CMakeLists.txt`
(adicionar a linha de glob do novo diretório de módulo),
`tests/CMakeLists.txt` (adicionar o novo nome de grupo ao `foreach`), e
`tests/main.c` (declarar `int tbox_test_<módulo>_run(void);` e adicionar
a entrada no array `tbox_test_groups`). Rodar `ctest --test-dir build`
completo pra confirmar que nada quebrou entre módulos.
