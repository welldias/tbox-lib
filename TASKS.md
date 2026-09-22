# tbox — Tarefas da v14

Quebra da seção "v14 — Caixas de bloco anônimas (conteúdo inline solto)"
do `ARCHITECTURE.md` em tarefas executáveis por agentes sem contexto
desta conversa. Cada tarefa abaixo é auto-contida: aponta para a
subseção exata do `ARCHITECTURE.md` (a fonte da verdade de *o quê*
construir) e acrescenta só o que esse documento não cobre — caminho de
arquivo, como registrar teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v13, que está completa —
ver `ARCHITECTURE.md` para o design de cada camada v0-v14 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

Diferente das versões anteriores, a v14 é fundamentalmente UM algoritmo
coeso dentro de UMA função (`tbox_layout_build_children`, mais duas
generalizações de assinatura em funções que ela já usa) — não há
paralelismo real entre camadas aqui (Style/Font/Render/Orchestration não
mudam nada nesta versão). Por isso só há duas tarefas, sequenciais.

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL
nova.** A v14 não especifica nenhum global/estático mutável novo — se a
implementação parecer precisar de um, **pare e reporte isso no relatório
final da tarefa em vez de adicionar por conta própria**.

**Escopo travado, não expanda:** ver "Fora de escopo" na seção v14 do
ARCHITECTURE.md — elemento inline `position: absolute`/`fixed` sempre
interrompe a sequência (nunca entra numa caixa anônima); nenhuma mudança
em `white-space` além do já suportado; nenhuma tentativa de bater
pixel-a-pixel contra o golden de `011.html` (só o TEXTO precisa aparecer,
na ordem/posição corretas); nenhuma mudança em `include/tbox/layout.h`
(a struct pública `tbox_layout_box`/`tbox_layout_text_run` já suporta
`node == NULL` desde a v2, nada novo a expor).

---

### Tarefa 1 — Layout Tree: caixas de bloco anônimas
**Depende de:** nada. **Bloqueia:** Tarefa 2.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v14" INTEIRA (contexto,
"Escopo", as assinaturas exatas, o algoritmo de detecção de sequência, e
"Fora de escopo") — é a especificação completa e a fonte da verdade pra
esta tarefa. `src/layout/tbox_layout.c` inteiro — em especial
`tbox_layout_is_text_tag`, `tbox_layout_collect_words`,
`tbox_layout_build_text_runs`, `tbox_layout_build_children` (a função
principal a mudar), `tbox_layout_build_element` (onde `tbox_layout_build_children`
é chamada, e onde o `style` do container já está disponível pra virar o
novo parâmetro `container_style`), `tbox_layout_default_style` (o
template de style zerado a usar como base do style sintetizado da caixa
anônima). `include/tbox/style.h` — confirme exatamente quais campos de
`tbox_style` são herdáveis (procure "inheritable" nos comentários de cada
campo: `color`, `font_family`, `font_weight_bold`, `font_italic`,
`font_size`, `text_align` — todo o resto é "not inheritable"). `src/context/tbox_context.c`,
função que faz hit-test (procure `box->node == NULL`) — confirme que já
trata uma caixa sem `node` com segurança, sem precisar de nenhuma mudança
sua aí.

**Arquivos a editar:**
- `src/layout/tbox_layout.c`:
  1. `tbox_layout_collect_words`: assinatura ganha `first_sibling`/
     `end_exclusive` no lugar de `node` (ver assinatura exata no
     ARCHITECTURE.md). O loop interno passa a ser `for (child =
     first_sibling; child != end_exclusive; child = child->next_sibling)`
     em vez de `for (child = node->first_child; child != NULL; ...)`. O
     ÚNICO call site existente (dentro de `tbox_layout_build_text_runs`)
     passa a chamar `tbox_layout_collect_words(arena, node->first_child,
     NULL, style, styles, fonts, &words)` — comportamento idêntico ao de
     hoje.
  2. `tbox_layout_build_text_runs`: assinatura ganha `first_sibling`/
     `end_exclusive` (ver assinatura exata no ARCHITECTURE.md); `node`
     continua existindo mas agora pode ser `NULL` (caixa anônima). Onde
     hoje calcula `is_preformatted = tbox_string_view_equal_cstr(node-
     >element.tag_name, "pre")`, passa a ser `is_preformatted = node !=
     NULL && tbox_string_view_equal_cstr(node->element.tag_name, "pre")`.
     Onde hoje chama `tbox_layout_push_list_marker(arena, node, style,
     fonts, &words)` incondicionalmente, passa a só chamar se `node !=
     NULL`. Onde hoje chama `tbox_layout_collect_words(arena, node, ...)`,
     passa a chamar `tbox_layout_collect_words(arena, first_sibling,
     end_exclusive, ...)`. O ÚNICO call site existente (dentro de
     `tbox_layout_build_element`, pra uma tag de texto real) passa
     `(arena, node, node->first_child, NULL, style, ...)` — comportamento
     idêntico ao de hoje.
  3. Nova função `tbox_layout_build_anonymous_box` (assinatura exata no
     ARCHITECTURE.md): constrói o style sintetizado (`tbox_style anon =
     tbox_layout_default_style;` seguido de copiar os 6 campos herdáveis
     de `container_style` — `color`, `font_family` via `memcpy` do buffer
     inteiro, `font_weight_bold`, `font_italic`, `font_size`,
     `text_align`), aloca a struct no arena (`tbox_arena_alloc`, já que
     `box->style` é um ponteiro que precisa sobreviver à chamada), aloca a
     `tbox_layout_box` via `tbox_arena_alloc_zero` (mesmo padrão de
     `tbox_layout_build_element`), preenche `box->node = NULL`, `box->style
     = <ponteiro pro style sintetizado>`, chama `tbox_layout_build_text_runs(arena,
     NULL, run_start, run_end, style_sintetizado, styles, fonts, content_x,
     cursor_y, available_width, box)` pra obter a altura, e preenche
     `content_box`/`padding_box`/`border_box`/`margin_box` todos idênticos
     (`{content_x, cursor_y, available_width, altura}` — sem margem/
     padding/borda, ver "Escopo" no ARCHITECTURE.md). Devolve a caixa.
  4. `tbox_layout_build_children`: ganha o parâmetro `container_style`
     (ver assinatura exata no ARCHITECTURE.md — repassado pelo ÚNICO call
     site, dentro de `tbox_layout_build_element`, que já tem essa variável
     como `style`). A iteração principal (`for (child = node->first_child;
     child != NULL; ...)`) muda pra, em CADA `child`, primeiro checar se é
     "gatilho de sequência inline" (algoritmo exato na seção "Algoritmo de
     `tbox_layout_build_children`" do ARCHITECTURE.md): TEXTO cujo
     `tbox_string_collapse_whitespace` tem `size > 0`, OU ELEMENT com
     `display == TBOX_STYLE_DISPLAY_INLINE` E `position` diferente de
     `ABSOLUTE`/`FIXED`. Se NÃO for gatilho, o código de hoje se aplica sem
     nenhuma mudança de comportamento (mas precisa continuar cobrindo:
     TEXTO só-espaço → pula sem construir caixa; ELEMENT `display: none` →
     pula sem construir caixa, como já faz; ELEMENT fora de fluxo → path
     de sempre; ELEMENT `display: block` em fluxo → path de sempre). Se
     FOR gatilho, um scan pra frente (a partir do MESMO `child`) acha o
     fim da sequência: avança por TEXTO de qualquer conteúdo e ELEMENT
     `display: none` (transparentes, nunca terminam a sequência) e por
     ELEMENT em fluxo com `display: inline`; PARA (sem consumir) no
     primeiro ELEMENT em fluxo com `display` diferente de `inline`, no
     primeiro ELEMENT fora de fluxo, ou no fim dos filhos (`NULL`). Chame
     `tbox_layout_build_anonymous_box` com esse intervalo
     `[run_start, run_end)`, ligue a caixa devolvida no
     `first_child`/`last_child`/`next_sibling` do `parent_box` (mesmo
     padrão que qualquer outra caixa filha já usa nesta função — não
     esqueça `child_box->parent = parent_box`), avance `border_bottom`
     pela altura da caixa (SEM somar/considerar margem — a caixa anônima
     não tem, ver "Escopo"; zere `pending_margin_bottom` depois dela,
     mesma lógica que margens de blocos reais já zeram o próprio lado),
     e continue a iteração externa a partir de `run_end` (não
     `child->next_sibling` — pule TODA a sequência de uma vez).
- `tests/layout/test_layout.c` — casos novos (grupo `layout` já
  registrado), cobrindo:
  - um `<body>`/`<div>` com um `<strong>` (ou qualquer inline) SOLTO,
    direto como filho, SEM nenhum `<p>` envolvendo — o texto aparece
    (produz `text_runs` com o conteúdo esperado numa caixa filha
    ANÔNIMA, `child_box->node == NULL`);
  - regressão: um `<div>` com só TEXTO só-espaço entre dois `<p>` (ex.
    `<div><p>a</p>\n  <p>b</p></div>`) não produz NENHUMA caixa anônima
    entre eles — só as duas caixas de `<p>` normais, na ordem certa;
  - caso misto (o padrão exato de `011.html`): um `<div>` com um `<small>`
    (ou qualquer inline) solto como PRIMEIRO filho, seguido de dois ou
    mais `<p>` — produz uma caixa anônima primeiro (com o texto do
    `<small>`), seguida das caixas de `<p>` normais, todas na ordem certa
    em `first_child`/`next_sibling`, com posições Y crescentes sem
    sobreposição;
  - um ELEMENT `display: none` no meio de uma sequência inline (ex.
    `<div>texto <span style="display:none">oculto</span> mais
    texto</div>`) não quebra a sequência em duas caixas — produz UMA
    caixa anônima só, contendo as duas partes de texto visível (o
    elemento oculto não contribui palavra nenhuma, mesmo comportamento
    de `display: none` em qualquer outro lugar);
  - um ELEMENT `position: absolute` inline no meio de conteúdo inline
    solto TERMINA a sequência antes dele (produz uma caixa anônima só
    com o texto ANTES do elemento posicionado, o elemento posicionado
    constrói sua própria caixa via o path de fora-de-fluxo de sempre, e
    se houver texto DEPOIS dele, uma SEGUNDA caixa anônima separada);
  - a caixa anônima herda `color`/`font_family`/`font_weight_bold`/
    `font_italic`/`font_size`/`text_align` do style do container pai
    (monte um `<div style="color: red; font-weight: bold;">texto
    solto</div>` e confirme que o `text_runs[0].style` resultante tem
    `color` vermelho e `font_weight_bold == true`), mas NÃO herda
    `background-color`/`border` do pai (monte um `<div style="background-color:
    blue; border: 1px solid black;">texto solto</div>` e confirme que a
    caixa anônima em si — não o texto dentro dela, a caixa — tem
    `background_color.a == 0` e `border_style == TBOX_STYLE_BORDER_STYLE_NONE`,
    provando que ela não pinta uma segunda cópia do fundo/borda do pai).

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

### Tarefa 2 — Fatia vertical v14 completa (validação + exemplo)
**Depende de:** Tarefa 1 (mergeada).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v14" → "Fatia vertical v14
— critério de 'pronto'" inteira.

**Trabalho:**
1. Renderize `tests/assets/011.html` via `tbox_app_screenshot_from_files`
   (a mesma função pública que `tbox_cmp`/`example/tbox_app.c --screenshot`
   já usam — width/height exatos: leia as dimensões de `tests/assets/011.png`
   primeiro, ex. com `python3 -c "from PIL import Image;
   print(Image.open('tests/assets/011.png').size)"` ou equivalente, e use
   ESSAS dimensões exatas pro render, pra golden e render ficarem no
   mesmo tamanho). Você pode escrever um pequeno programa C standalone
   (compilado contra `build/src/libtbox.a`, incluindo só `<tbox/app.h>`)
   pra isso, ou usar `./build/tests/tbox_cmp tests/assets` se
   `TBOX_OPENCV_FOUND` estiver disponível (mais simples, já faz
   exatamente esse render+compare pra TODOS os assets de uma vez — olhe
   a linha do resultado específica de `011`). De qualquer forma, LEIA a
   imagem renderizada (ferramenta Read) e compare visualmente contra
   `tests/assets/011.png`: confirme que TODO o texto agora aparece —
   "This text is important!" (negrito), "This text is italic." (itálico),
   "This text is emphasized." (itálico), e "This is some smaller text."
   (menor) — nas posições/ordem corretas em relação aos parágrafos ao
   redor. Não precisa bater pixel-a-pixel (fontes diferem do browser que
   gerou o golden) — o critério é o TEXTO estar presente e na ordem/
   posição certa.
2. Estenda `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v13, sem remover nada) com um exemplo NOVO reproduzindo o padrão de
   `011.html`: um elemento inline (ex. `<strong>` ou `<em>`) solto direto
   em `<body>` ou dentro de um `<div>` (sem `<p>` envolvendo), misturado
   com pelo menos um `<p>` de bloco normal como irmão, pra provar
   visualmente que o texto aparece e a ordem/posição batem.
3. Provavelmente nenhuma mudança de código é necessária em
   `example/tbox_app.c`, exceto possivelmente `TBOX_APP_DEMO_HEIGHT` se o
   conteúdo novo empurrar o layout pra baixo da altura atual — confirme
   visualmente via `--screenshot` ANTES de decidir se precisa mudar o
   número.
4. Validação, nesta ordem:
   - `cmake -S . -B build && cmake --build build` limpo (TODOS os alvos),
     `-Wall -Wextra -Wpedantic -Werror`.
   - `ctest --test-dir build` — suíte inteira verde.
   - `./build/example/tbox_app_demo --screenshot <path>.png` — RODE PELO
     MENOS 3 VEZES SEGUIDAS e compare os PNGs via `md5sum` (devem ser
     byte-idênticos — mesma verificação de determinismo que v12/v13 já
     exigiram, essa área de código já teve bugs reais de
     não-determinismo antes). Depois LEIA a imagem e confirme visualmente
     o item 2 acima, e que nada de v0-v13 quebrou ou sumiu.

**Critério de pronto:** build limpo + suíte inteira passando + PNGs
deterministicamente idênticos entre execuções + `011.html` renderizado
mostra TODO o texto que faltava + o exemplo novo na demo confirma
visualmente — este é o "pronto" da v14 inteira, não só desta tarefa.
