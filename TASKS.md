# tbox — Tarefas da v13

Quebra da seção "v13 — `<strong>`, `<i>`, `<em>`, `<small>`, `<mark>`,
`<del>`, `<ins>`, `<sub>`, `<sup>`" do `ARCHITECTURE.md` em tarefas
executáveis por agentes sem contexto desta conversa. Cada tarefa abaixo é
auto-contida: aponta para a subseção exata do `ARCHITECTURE.md` (a fonte
da verdade de *o quê* construir) e acrescenta só o que esse documento não
cobre — caminho de arquivo, como registrar teste, como verificar que
ficou pronto.

(Este arquivo substitui a quebra de tarefas da v12, que está completa —
ver `ARCHITECTURE.md` para o design de cada camada v0-v13 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática MUTÁVEL
nova** (ver o item de débito "Thread-safety futura" no fim do
`ARCHITECTURE.md`). A v13 não especifica nenhum global/estático mutável
novo — se a implementação de alguma tarefa parecer precisar de um,
**pare e reporte isso no relatório final da tarefa em vez de adicionar
por conta própria**.

**Escopo travado, não expanda em nenhuma tarefa:** ver "Fora de escopo"
na seção v13 do ARCHITECTURE.md — sem `oblique` distinto de `italic`,
sem múltiplos valores de `text-decoration` na mesma declaração, sem
métrica real de fonte pra subscript/superscript/decoração (usa fração
fixa de `font-size`/1px, já documentado), sem `vertical-align` além de
`sub`/`super`, sem mudança nenhuma no HTML Parser.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**As Tarefas 1 e 2 (Tier 0) não compartilham NENHUM arquivo** (`font` vs
`style`) — rodam em paralelo sem risco de conflito de merge. **A Tarefa 3
(Layout Tree) depende da Tarefa 1 E da Tarefa 2** (precisa da nova
assinatura de `tbox_font_face_cache_get` E dos 3 campos novos de
`tbox_style`). **A Tarefa 4 (Render Pipeline) depende só da Tarefa 3**
(precisa do campo `tbox_layout_text_run.style`). **A Tarefa 5
(Orchestration) depende só da Tarefa 2** (usa os 3 campos novos de
`tbox_style` só no teste que confirma a UA stylesheet resolvendo pra
esses campos — a mudança em si, no template CSS, não depende de nada) —
roda em paralelo com a Tarefa 3/4 sem conflito de arquivo.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`,
depois `ctest --test-dir build` (ou `cd build && ctest`). Todo build
precisa passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o
projeto builda hoje — nenhuma tarefa deve silenciar warning, deve
corrigi-lo).

**Mudança de assinatura pública, não é quebra silenciosa:**
`tbox_font_face_cache_get` ganha um parâmetro novo (`italic`, como quarto
argumento, logo depois de `bold`, antes de `size_px`) — todo call site
existente (produção E testes) precisa ser atualizado. A Tarefa 1 lista os
arquivos de teste afetados; a Tarefa 3 tem os 6 call sites de produção
próprios (mesmos 6 já tocados pela v12).

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Font: `italic` deixa de ser hardcoded
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Font — `italic`
deixa de ser hardcoded" INTEIRA. `include/tbox/font.h` inteiro — em
especial a assinatura atual de `tbox_font_face_cache_get` (a v12 já
adicionou `family` como primeiro parâmetro depois de `cache`; `italic`
entra como quarto, depois de `bold`, antes de `size_px`).
`src/font/tbox_font_face_cache.c` inteiro (já cresceu bastante na v12 —
struct `tbox_font_face_cache_entry`, vetor `family_blobs`, o algoritmo de
`tbox_font_face_cache_get`).

**Arquivos a editar:**
- `include/tbox/font.h`: `tbox_font_face_cache_get` ganha `bool italic`
  como quarto parâmetro (`cache, family, bold, italic, size_px`).
- `src/font/tbox_font_face_cache.c`:
  1. `tbox_font_face_cache_entry` ganha `bool italic;` — a busca linear
     em `entries` passa a comparar `italic` também (além de `family`/
     `bold`/`size_px` já existentes).
  2. O elemento de `family_blobs` (`{ char family[64]; bool bold; const
     void *data; size_t size; }`, da v12) ganha `bool italic;` também —
     resolvido/cacheado por `(family, bold, italic)` em vez de só
     `(family, bold)`.
  3. No caminho de MISS com família vazia (`regular_data`/`bold_data`
     pré-carregado): `italic` é ignorado, comportamento idêntico ao de
     hoje (esse caminho nunca teve face itálica).
  4. No caminho de resolução sob demanda (`cache->resolver`): a query
     passada ao resolver passa a ser `{family, bold, italic}` de verdade,
     não mais `{family, bold, false}` hardcoded.
- `tests/font/test_font.c` — TODOS os call sites existentes de
  `tbox_font_face_cache_get` precisam do argumento novo (`italic = false`
  pra preservar o comportamento de teste de hoje, EXCETO nos casos novos
  abaixo). Casos NOVOS (grupo `font` já registrado, mesmo padrão dos
  testes de `family` da v12 como referência):
  - com o resolver de teste já existente (ou um novo, se mais claro):
    `_get(cache, "Alguma Familia", false, true, 16.0)` (itálico) retorna
    um ponteiro DIFERENTE de `_get(cache, "Alguma Familia", false, false,
    16.0)` (mesma família/peso, não-itálico) — chaves de cache distintas;
  - uma SEGUNDA chamada com os mesmos `(family, bold, italic, size_px)`
    retorna o MESMO ponteiro (cache hit);
  - regressão: `_get(cache, tbox_string_view_make(NULL, 0), false, true,
    16.0)` (família vazia, itálico) continua no caminho default
    (`italic` ignorado nesse caminho) — mesmo ponteiro que `_get(cache,
    tbox_string_view_make(NULL, 0), false, false, 16.0)`.

**Critério de pronto:** `ctest --test-dir build -R '^font$'` verde +
suíte inteira sem regressão.

### Tarefa 2 — Style: `font-style`, `text-decoration`, `vertical-align`
**Depende de:** nada. **Bloqueia:** Tarefa 3, Tarefa 5 (só o teste
específico desta última).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Style —
`font-style`, `text-decoration`, `vertical-align`" INTEIRA.
`include/tbox/style.h` — os enums existentes (`tbox_style_text_align`
como referência de estilo de enum) e a struct `tbox_style`.
`src/style/tbox_style.c` — `tbox_style_parse_text_align` (padrão a
replicar pro parse de `font-style`), e os blocos de resolução de
`font-weight` (3 ramos, herdável) e `border`/`position` (2 ramos, não
herdável) em `tbox_style_resolve` — os DOIS padrões que esta tarefa usa.

**Arquivos a editar:**
- `include/tbox/style.h`:
  1. Novo enum `tbox_style_text_decoration` (`NONE` inicial,
     `UNDERLINE`, `LINE_THROUGH`) e `tbox_style_vertical_align`
     (`BASELINE` inicial, `SUB`, `SUPER`) — assinaturas exatas no
     ARCHITECTURE.md.
  2. `tbox_style` ganha `bool font_italic;` (herdável, initial `false`),
     `tbox_style_text_decoration text_decoration;` (NÃO herdável,
     initial `NONE`), `tbox_style_vertical_align vertical_align;` (NÃO
     herdável, initial `BASELINE`).
- `src/style/tbox_style.c`:
  1. `font_italic`: reconhece a declaração `font-style` com valor exato
     `"italic"` (case-insensitive, `tbox_string_view_equal_ascii_ci`,
     mesmo helper que `tbox_style_parse_text_align` já usa) — mesmo
     padrão de TRÊS ramos de `font_weight_bold`: declaração reconhecida
     → `true`; senão herda `parent_style->font_italic`; senão `false`.
  2. `text_decoration`: nova função pequena (mesmo padrão de
     `tbox_style_resolve_position`/o bloco de `border` — só `computed`
     como entrada, sem `parent_style`) que reconhece `text-decoration:
     underline` → `UNDERLINE`, `text-decoration: line-through` →
     `LINE_THROUGH` (case-insensitive), qualquer outra coisa/ausente →
     `NONE`. DOIS ramos só (nunca olha o pai).
  3. `vertical_align`: mesma forma, reconhece `vertical-align: sub` →
     `SUB`, `vertical-align: super` → `SUPER`, qualquer outra
     coisa/ausente → `BASELINE`. DOIS ramos só.
- `tests/style/test_style.c` — casos novos (grupo `style` já
  registrado):
  - `div { font-style: italic; }` resolve `font_italic == true`;
    regressão: sem a declaração, `font_italic == false`; herança: um
    `<div><p>x</p></div>` com `font-style: italic` só no `div` — o `p`
    herda `font_italic == true`;
  - `div { text-decoration: underline; }` resolve
    `TBOX_STYLE_TEXT_DECORATION_UNDERLINE`; `line-through` resolve
    `LINE_THROUGH`; sem declaração resolve `NONE`; um filho SEM
    `text-decoration` própria dentro de um `div` COM
    `text-decoration: underline` resolve `NONE` no filho (prova de
    não-herança — diferente de `font_italic` acima);
  - mesma bateria de 3 casos (declarado/ausente/não-herda-pro-filho) pra
    `vertical-align: sub`/`vertical-align: super`.

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — depende de Tarefa 1 e/ou Tarefa 2

### Tarefa 3 — Layout Tree: `text_run.style` + alinhamento de linha de base
**Depende de:** Tarefa 1 E Tarefa 2 (ambas mergeadas — precisa da nova
assinatura de `tbox_font_face_cache_get` E dos 3 campos novos de
`tbox_style`). **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Layout Tree —
`tbox_layout_text_run.style` + alinhamento de linha de base" INTEIRA (o
algoritmo exato dos dois deslocamentos verticais somados, as constantes
`0.15`/`0.35`). `src/layout/tbox_layout.c` inteiro — em especial
`tbox_layout_word` (struct interna), `tbox_layout_line` (struct interna),
`tbox_layout_break_lines` (onde `height` já é calculado como máximo por
linha — `ascent` entra do mesmo jeito, mesmo loop, 3 pontos onde uma
linha se fecha), `tbox_layout_build_line_runs` (onde runs são
fundidos/posicionados), e os 6 pontos de chamada de
`tbox_font_face_cache_get` (mesmos da v12, procure toda ocorrência).
`include/tbox/layout.h` — struct pública `tbox_layout_text_run`.

**Arquivos a editar:**
- `include/tbox/layout.h`: `tbox_layout_text_run` ganha `const
  tbox_style *style;` (nunca `NULL` — sempre o mesmo style que decidiu
  `font` pra aquele run).
- `src/layout/tbox_layout.c`:
  1. `tbox_layout_word` (struct interna, não pública) ganha `const
     tbox_style *style;` ao lado de `face` — populado nos MESMOS pontos
     que hoje populam `face` (`tbox_layout_push_words`,
     `tbox_layout_push_hard_break`, e todo call site que os chama —
     sempre o mesmo `style`/`child_style` já usado ali pra decidir
     `bold`/`size`/`family`, nenhuma lógica nova).
  2. `tbox_layout_line` (struct interna) ganha `double ascent;` — mesmo
     loop que já calcula `height` como o máximo `tbox_font_face_line_height`
     entre as palavras do range, só que com `tbox_font_face_ascent` (já
     existe em `include/tbox/font.h`) em vez de `tbox_font_face_line_height`.
     Os 3 pontos de `tbox_layout_break_lines` que fecham uma linha
     precisam do cálculo espelhado.
  3. `tbox_layout_build_line_runs`: a condição `new_run` passa a comparar
     `word->face != run_face || word->style != run_style` (hoje só
     `face`). Ao finalizar cada run (os dois pontos onde `tbox_layout_text_run`
     é preenchido — dentro do loop e depois dele), `rect.y` recebe
     `line_y + (line->ascent - tbox_font_face_ascent(run_face)) +
     deslocamento_extra`, onde `deslocamento_extra` é `0.0` se
     `run_style->vertical_align == BASELINE`, `+0.15 *
     run_style->font_size` se `SUB`, `-0.35 * run_style->font_size` se
     `SUPER`. `run->style = run_style;` é atribuído junto.
  4. Os 6 pontos de chamada de `tbox_font_face_cache_get` ganham
     `style->font_italic` (ou `child_style->font_italic`) como quinto
     argumento — mesmo `style`/`child_style` já usado ali pra `bold`/
     `size`/`family`.
- `tests/layout/test_layout.c` — os call sites diretos de
  `tbox_font_face_cache_get` ganham o argumento novo (`italic = false`
  pra manter o comportamento de teste de hoje, a menos que o teste seja
  especificamente sobre itálico). Casos NOVOS:
  - `<p><i>itálico</i> normal</p>` — a face do run `<i>` é DIFERENTE da
    face do run "normal" mesmo com peso/tamanho/família iguais (prova de
    que `font_italic` chega até a escolha de face);
  - `<p>Normal <small>pequeno</small></p>` — o run "pequeno" usa uma face
    de tamanho MENOR (`font_size` do `<small>`, resolvido via UA `80%` —
    esse teste pode montar a árvore de style manualmente com
    `font_size` menor em vez de depender da UA stylesheet real, mais
    simples e mais isolado) E sua `rect.y` reflete o deslocamento de
    alinhamento de linha de base (`line->ascent - ascent(small_face) >
    0`, ou seja, `rect.y` do run pequeno é MAIOR que `rect.y` do run
    normal na mesma linha — verifique com o cálculo exato, não só "é
    diferente");
  - `<p>Normal <sub>baixo</sub></p>` — `rect.y` do run `<sub>` é MAIOR
    (mais pra baixo) que o alinhamento de linha de base sozinho
    explicaria — a diferença extra bate com `0.15 * font_size` do
    `<sub>`;
  - o mesmo pro `<sup>` com `-0.35 * font_size` (mais pra cima);
  - regressão: um `<p>` só com texto normal (todos os runs mesma face)
    continua com TODOS os `rect.y` iguais a `line_y` puro (deslocamento
    zero quando não há variação de face na linha) — prova de que v0-v12
    não regrediu;
  - `<p><mark>x</mark> normal</p>` onde ambos os trechos resolvem pra
    face IDÊNTICA (nenhum peso/tamanho/família diferente) — mesmo assim
    produz DOIS runs separados, não um só (prova de que `style` agora
    também é parte da chave de fusão de runs, não só `face`).

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

### Tarefa 5 — Orchestration: UA stylesheet das 9 tags
**Depende de:** Tarefa 2 (mergeada — só o teste que confirma
`font_italic`/`text_decoration`/`vertical_align` resolvidos a partir da
UA stylesheet real precisa da struct `tbox_style` já ter esses campos; a
mudança no template CSS em si não depende de nada, mesma observação que
a Tarefa 3 da v12 já fazia pro `<pre>`). **Bloqueia:** nada (a fatia
vertical depende dela, nenhuma outra tarefa).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Orchestration — UA
stylesheet" INTEIRA — o texto exato das linhas novas/alteradas do
template. `src/context/tbox_context.c` — `tbox_ua_style_generate_css`,
em especial a linha atual `"i, em, span, a { display: inline; }\n"` (que
se divide em duas) e onde `"b, strong { display: inline; font-weight:
bold; }\n"` já vive (referência de posição, sem mudança nela mesma).

**Arquivos a editar:**
- `src/context/tbox_context.c`: no template `snprintf` de
  `tbox_ua_style_generate_css`:
  1. Divida a linha `"i, em, span, a { display: inline; }\n"` em
     `"i, em { display: inline; font-style: italic; }\n"` +
     `"span, a { display: inline; }\n"`.
  2. Adicione seis linhas novas (texto exato no ARCHITECTURE.md): regras
     pra `small` (`font-size: 80%`), `mark` (`background-color: yellow`),
     `del` (`text-decoration: line-through`), `ins` (`text-decoration:
     underline`), `sub` (`font-size: 75%; vertical-align: sub`), `sup`
     (`font-size: 75%; vertical-align: super`) — todas com `display:
     inline` também, mesmo padrão de `i, em`.
  Nenhum `%g` novo, nenhuma mudança em `include/tbox/context.h` nem em
  `tbox_ua_style_config_default()` — texto literal, mesma categoria da
  linha do `<pre>` que a v12 já adicionou.
- `tests/context/test_context.c` — casos novos (grupo `context` já
  registrado, mesmo padrão dos testes de UA stylesheet da v8/v11/v12):
  documentos com `<i>x</i>`, `<em>x</em>`, `<small>x</small>`,
  `<mark>x</mark>`, `<del>x</del>`, `<ins>x</ins>`, `<sub>x</sub>`,
  `<sup>x</sup>` (sem CSS de autor), cada um confirmando o campo
  resolvido correspondente (`font_italic == true`, `font_size` reduzido,
  `background_color` amarelo, `text_decoration ==
  LINE_THROUGH`/`UNDERLINE`, `vertical_align == SUB`/`SUPER`) via
  `tbox_style_table`, mesmo padrão de inspeção já usado. **Estes casos
  específicos dependem da Tarefa 2 já ter mergeado os 3 campos novos de
  `tbox_style` pra compilar** — se a Tarefa 2 ainda não estiver pronta
  quando for escrever ESSES testes específicos, escreva o resto da
  tarefa (a mudança no template) e deixe esses testes como último passo,
  ou adicione depois que a Tarefa 2 mergear. A MUDANÇA em
  `tbox_context.c` em si não depende de nada.

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão.

---

## Tier 2 — depende de Tarefa 3

### Tarefa 4 — Render Pipeline: destaque de fundo + linha de decoração
**Depende de:** Tarefa 3 (mergeada — precisa do campo
`tbox_layout_text_run.style`). **Bloqueia:** Tarefa 6.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Render Pipeline —
destaque de fundo e linha de decoração por run" INTEIRA (a ordem exata
dos paint ops, as constantes de posicionamento da linha de decoração).
`src/render/tbox_render.c` inteiro (arquivo pequeno) — em especial
`tbox_render_push_fill_rect` (o helper a reaproveitar, já usado por
fundo/borda de caixa) e o loop `for (size_t i = 0; i < box->text_run_count;
i++)` dentro de `tbox_render_walk`, onde o `TEXT_RUN` de cada run é
empurrado hoje usando `box->style->color`.

**Arquivos a editar:**
- `src/render/tbox_render.c`, dentro do loop de `box->text_runs`:
  1. `op->color` do `TBOX_PAINT_TEXT_RUN` passa a vir de
     `run->style->color` (nunca `NULL`, per Tarefa 3) em vez de
     `text_color`/`box->style->color`.
  2. ANTES de empurrar o `TEXT_RUN` de cada run: se `run->style-
     >background_color.a != 0`, empurre um `FILL_RECT` cobrindo
     `run->rect` inteiro com essa cor, via `tbox_render_push_fill_rect`
     (mesma função já usada por fundo/borda de caixa).
  3. DEPOIS de empurrar o `TEXT_RUN`: se `run->style->text_decoration !=
     TBOX_STYLE_TEXT_DECORATION_NONE`, empurre um `FILL_RECT` fino (1px
     de altura, `run->rect.width` de largura, `run->rect.x` de x), cor =
     `run->style->color`, y calculado a partir de `run->rect.y +
     tbox_font_face_ascent(run->font)` (a linha de base, mesmo cálculo
     que o Output Display já faz): `UNDERLINE` → baseline `+ 2px`;
     `LINE_THROUGH` → baseline `- tbox_font_face_ascent(run->font) *
     0.3`.
- **Nenhuma mudança** em `include/tbox/render.h` (`tbox_paint_op` não
  ganha campo novo) nem em `src/output/` — os dois efeitos novos são só
  mais `FILL_RECT`s, o mesmo `tbox_paint_op.kind` que já existe.

**Critério de pronto:** `ctest --test-dir build -R '^render$'` verde +
suíte inteira sem regressão. Sem grupo de teste próprio pra output
visual além do que `render`/`output_raster` já cobrem — casos novos vão
no grupo `render` (mesmo padrão de inspeção do `tbox_display_list`/
`tbox_paint_op` que os testes de fundo/borda de caixa já usam): um run
com `background_color` não-transparente produz um `FILL_RECT` extra
ANTES do seu `TEXT_RUN` cobrindo exatamente `run->rect`; um run com
`text_decoration != NONE` produz um `FILL_RECT` fino extra DEPOIS do seu
`TEXT_RUN` na posição y esperada (`UNDERLINE` abaixo da linha de base,
`LINE_THROUGH` acima); um run com `background_color` transparente E
`text_decoration == NONE` continua produzindo só o `TEXT_RUN`, nenhum
`FILL_RECT` extra (regressão v0-v12).

---

## Tier 3 — fatia vertical v13

### Tarefa 6 — Fatia vertical v13 completa (exemplo + validação)
**Depende de:** Tarefas 1, 2, 3, 4 e 5 (todas mergeadas).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v13" → "Fatia vertical v13
— critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v12, sem remover nada) com um parágrafo (ou vários, um por
   elemento, se ficar mais claro na screenshot) demonstrando as 9 tags:
   `<strong>` (negrito, ao lado de um `<b>` já existente pra comparação
   visual direta), `<i>`/`<em>` (itálico de verdade — confirme
   visualmente que os glifos estão inclinados, não só "igual ao texto
   normal" como era até a v12), `<small>` (visivelmente menor, linha de
   base alinhada com o resto da linha, não flutuando), `<mark>` (fundo
   amarelo só atrás do trecho marcado), `<del>` (tachado) e `<ins>`
   (sublinhado) lado a lado, `<sub>` e `<sup>` (visivelmente deslocados
   da linha de base, menores). Um `<span>` ou `<a>` também deve aparecer
   em algum lugar, sem nenhum efeito visual próprio, provando que a
   divisão da regra UA antiga não quebrou nada.
2. Provavelmente nenhuma mudança de código é necessária em
   `example/tbox_app.c`, exceto possivelmente `TBOX_APP_DEMO_HEIGHT` se o
   conteúdo novo empurrar o layout pra baixo da altura atual — confirme
   visualmente via `--screenshot` ANTES de decidir se precisa mudar o
   número (não mude preventivamente).
3. Validação, nesta ordem:
   - `cmake -S . -B build && cmake --build build` limpo (TODOS os alvos,
     incluindo `tbox_app_demo`), `-Wall -Wextra -Wpedantic -Werror`.
   - `ctest --test-dir build` — suíte inteira verde.
   - `./build/example/tbox_app_demo --screenshot <path>.png` — RODE PELO
     MENOS 3 VEZES SEGUIDAS e compare os PNGs resultantes (ex. `md5sum`)
     — devem ser byte-idênticos entre execuções (mesma classe de
     verificação de não-determinismo que a v12 precisou fazer depois de
     um bug real de use-after-free ter sido encontrado e corrigido nesta
     mesma área de código). Depois, LEIA a imagem (ferramenta Read, abre
     PNG) e confirme visualmente cada item do critério de "pronto" da v13
     no ARCHITECTURE.md: negrito igual entre `<strong>`/`<b>`, itálico de
     verdade em `<i>`/`<em>`, `<small>` menor com linha de base alinhada,
     `<mark>` com fundo só atrás do trecho, `<del>` tachado, `<ins>`
     sublinhado, `<sub>`/`<sup>` deslocados e menores, `<span>`/`<a>` sem
     efeito próprio, nada de v0-v12 quebrado.
4. Opcional, não é critério de aprovação: se `TBOX_OPENCV_FOUND`
   disponível, rode `./build/tests/tbox_cmp tests/assets` manualmente e
   documente o resultado no relatório final (não bloqueia nada).

**Critério de pronto:** build limpo (TODOS os alvos) + suíte inteira
passando + PNGs deterministicamente idênticos entre execuções + a
validação visual acima confirma todos os itens sem erro — este é o
"pronto" da v13 inteira, não só desta tarefa.
