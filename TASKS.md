# tbox — Tarefas da v4

Quebra da seção "v4 — Modelo de Caixa CSS Completo" do `ARCHITECTURE.md` em
tarefas executáveis por agentes sem contexto desta conversa. Cada tarefa
abaixo é auto-contida: aponta para a subseção exata do `ARCHITECTURE.md`
(a fonte da verdade de *o quê* construir) e acrescenta só o que esse
documento não cobre — caminho de arquivo, como registrar teste, como
verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v3, que está completa — ver
`ARCHITECTURE.md` para o design de cada camada v0/v1/v2/v3/v4 e `git log --
TASKS.md` para recuperar quebras de tarefas anteriores, se precisar
consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática nova além
das que o `ARCHITECTURE.md` já especifica explicitamente pra ela** (ver
o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`). A
v4 não especifica nenhum global/estático novo pra nenhuma das tarefas
abaixo — se a implementação de alguma parecer precisar de um, **pare e
reporte isso no relatório final da tarefa em vez de adicionar por conta
própria**; é uma decisão que espera discussão com o mantenedor do
projeto, não uma escolha de implementação.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Nenhuma tarefa cria módulo
novo nem grupo de teste novo nesta versão (todas estendem grupos já
registrados: `style`, `layout`, `render`) — sem passo de integração de
`tbox.h`/`CMakeLists.txt`/`tests/main.c`.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

Diferente da v3, a v4 não muda nenhuma assinatura de função já usada por
`example/tbox_app.c` — `tbox_style`/`tbox_layout_box` ganham campos novos,
mas nenhum campo/função existente muda de tipo ou desaparece. Não há
quebra esperada em `example/tbox_app_demo` em nenhum tier antes da
Tarefa 4; se algum tier quebrar esse alvo, é uma regressão real, não uma
quebra esperada como na v3.

---

## Tier 0 — sem dependência de tarefa nova

### Tarefa 1 — Style: `border` (shorthand, só `solid`) + `position: relative` + offsets
**Depende de:** nada. **Bloqueia:** Tarefa 2.

**Por que numa tarefa só:** border e position/offsets são funcionalmente
independentes, mas os dois só tocam `include/tbox/style.h` (novos
campos/enums em `tbox_style`) e `src/style/tbox_style.c`
(`tbox_style_resolve` ganha os dois blocos de parsing lado a lado) —
mesmo raciocínio já usado na v3 pra Tarefa 5: paralelizar aqui só criaria
risco de conflito de merge no mesmo struct/função, sem ganho real (não dá
pra rodar duas tarefas em paralelo no mesmo arquivo de qualquer jeito).

**Leia primeiro:** `ARCHITECTURE.md`, seção "v4 — Modelo de Caixa CSS
Completo" → "Style — `border` (shorthand único, só `solid`)" e "Style —
`position: relative` + offsets" inteiras (tipos/campos exatos, algoritmo
de parsing do shorthand `border`, contrato de `position`/`top`/`right`/
`bottom`/`left`). `include/tbox/style.h`/`src/style/tbox_style.c` atuais
— em particular `tbox_style_resolve_box_shorthand` (o parser de
1/2/3/4-valores de margin/padding) como referência de estilo de código
pro parsing de comprimento (`px`/`%`/`auto`) que `border`/`top`/`right`/
`bottom`/`left` reaproveitam, e como `tbox_css_color_parse` já é chamado
em `background-color`/`color`.

**Arquivos a editar:**
- `include/tbox/style.h` — `tbox_style_border_style` (enum `NONE`/`SOLID`,
  initial `NONE`), `tbox_style_position` (enum `STATIC`/`RELATIVE`,
  initial `STATIC`); em `tbox_style`: `border_width` (`double`, px,
  initial `0.0`), `border_style` (initial `NONE`), `border_color`
  (`tbox_css_rgba`, initial opaco preto), `position` (initial `STATIC`),
  `offset[4]` (`tbox_style_length`, top/right/bottom/left, initial
  `AUTO`) — todos não-herdáveis (mesmo tratamento de `width`/
  `background-color`: nunca olham `parent_style`). Atualizar o comentário
  de `tbox_style_resolve` (bloco "Scope") pra listar `border`/`position`/
  `top`/`right`/`bottom`/`left` como suportados, e remover `position` da
  lista de "Out of scope" onde aparece hoje.
- `src/style/tbox_style.c` — implementar o parsing do shorthand `border`
  (split por espaço em até 3 tokens, ordem livre: token termina em `px` e
  o resto parseia como número → `border_width`; bate case-insensitive com
  `solid`/`none` → `border_style`; senão tenta `tbox_css_color_parse` →
  `border_color`; token não reconhecido é ignorado, sem derrubar a
  declaração) e o parsing de `position` (só `static`/`relative`,
  case-insensitive; qualquer outro valor cai no initial `STATIC`) e de
  `top`/`right`/`bottom`/`left` (mesmo parser de comprimento que
  `width`/`margin` já usam — `auto`, px, ou `%`).
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado)

**Casos de teste mínimos:**
- `border: 2px solid red;` produz `border_width == 2.0`,
  `border_style == SOLID`, `border_color` == vermelho opaco; a mesma
  declaração com os três tokens em outra ordem (ex.: `solid red 2px`)
  produz o mesmo resultado (ordem livre);
- `border: none;` (sozinho, ou junto de `border-width`/`border-color` que
  não existem como propriedade própria nesta versão) produz
  `border_style == NONE`; ausência total de `border` também produz
  `border_style == NONE`/`border_width == 0.0`/`border_color` opaco preto
  (initial);
- um token não reconhecido dentro de `border` (ex.: `border: 2px dashed
  red;` — `dashed` não é suportado) não derruba os outros dois tokens
  válidos (`border_width == 2.0`, `border_color` == vermelho), só
  `border_style` fica em `NONE` (nenhum token bateu em `solid`/`none`);
- `position: relative;` produz `position == RELATIVE`; ausência, ou
  qualquer valor não reconhecido (ex.: `absolute`), produz `STATIC`
  (initial);
- `top: 10px; left: 5%;` produz os `offset[]` certos; `bottom`/`right`
  ausentes ficam `AUTO`; nenhum dos quatro herda do pai (testar um filho
  sem `top` declarado, com o pai tendo `top: 10px` — filho deve ficar
  `AUTO`, não `10px`).

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — depende de Tier 0

### Tarefa 2 — Layout Tree: geometria de borda + deslocamento de `position: relative` + margin collapsing entre siblings
**Depende de:** Tarefa 1 (`border_width`/`border_style`/`position`/
`offset[]` em `tbox_style`) — mergeada. **Bloqueia:** Tarefa 3.

**Por que numa tarefa só:** as três mudanças vivem nas mesmas duas
funções de `src/layout/tbox_layout.c` (`tbox_layout_build_element` para
borda + offset, `tbox_layout_build_children` para margin collapsing, que
por sua vez chama `tbox_layout_build_element`) — mesmo raciocínio de
conflito de merge da Tarefa 1: paralelizar exigiria duas tarefas editando
o mesmo trecho de código ao mesmo tempo, sem ganho real.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v4 — Modelo de Caixa CSS
Completo" → "Layout Tree — geometria de borda", "Layout Tree —
deslocamento visual de `position: relative`" e "Layout Tree — margin
collapsing entre siblings" inteiras (as três, incluindo a assinatura de
`tbox_layout_resolve_offset` e a explicação de por que nenhum conceito
novo de containing block é necessário, e o algoritmo exato de
`border_bottom`/`pending_margin_bottom` pro collapsing).
`src/layout/tbox_layout.c` atual — `tbox_layout_build_element` (onde
`content_x`/`content_y`/`content_width`/`padding_box`/`border_box`/
`margin_box` são computados hoje, linhas ~405-519) e
`tbox_layout_build_children` (onde `cursor_y`/`total_height` são
acumulados hoje, linhas ~368-398).

**Arquivos a editar:**
- `src/layout/tbox_layout.c`:
  1. Geometria de borda: calcular `effective_border` (0.0 se
     `border_style != SOLID`, senão `border_width`); somar aos 4 lados na
     transição `padding_box` → `border_box` (deixa de ser
     `box->border_box = box->padding_box;`); subtrair `2 *
     effective_border` do `content_width` no ramo AUTO; somar
     `effective_border` a `content_x`/`content_y` (junto de
     `padding_left`/`padding_top`).
  2. Deslocamento de `position: relative`: implementar
     `tbox_layout_resolve_offset` (par primário/oposto, CSS2.1 9.4.3,
     com guarda de `percent_base_definite` pro eixo vertical); quando
     `style->position == RELATIVE`, somar `dx`/`dy` a `content_x`/
     `content_y` ANTES de qualquer uso posterior (inclusive
     `children_container.x`) — ver ordem exata na seção do
     `ARCHITECTURE.md`.
  3. Margin collapsing: mudar `tbox_layout_build_children` pra rastrear
     `border_bottom` (fim do `border_box` do último sibling) e
     `pending_margin_bottom` em vez de só `cursor_y`; espiar
     `child_style->margin[0]` (resolvido contra
     `children_container.width`) antes de chamar
     `tbox_layout_build_element`, e passar um `cursor_y` ajustado que
     produz o gap `max(pending_margin_bottom, child_margin_top)` quando
     ambos >= 0 (senão a soma de hoje). Ao final, a margem inferior do
     último filho soma integralmente ao `total_height` retornado (nunca
     colapsa com o pai).
- `tests/layout/test_layout.c` — casos novos (grupo `layout` já
  registrado)

**Casos de teste mínimos:**
- um elemento com `border: 3px solid black` tem `border_box` 3px maior
  que `padding_box` em cada lado, e `content_width` (quando `width:
  auto`) reduzido em 6px a mais do que sem borda;
- um elemento com `border-style` efetivamente `none` (sem `border`
  declarado, ou um token de estilo não reconhecido — ver Tarefa 1) tem
  `border_box == padding_box` (regressão de v0-v3, deve continuar
  valendo);
- um elemento `position: relative; top: 10px; left: 5px;` tem seu
  `content_box`/`padding_box`/`border_box`/`margin_box` todos deslocados
  em (5, 10) em relação à posição estática, mas o PRÓXIMO sibling
  continua posicionado como se o deslocado não tivesse se movido (mesmo
  `cursor_y` que teria sem `position: relative`);
- um filho de um elemento `position: relative` desloca junto (herda o
  deslocamento do pai automaticamente, via `children_container`
  deslocado);
- `top: 20%` com o container de altura indefinida (AUTO) resolve pra 0,
  não NaN/crash;
- dois siblings de bloco com `margin-bottom: 10px`/`margin-top: 20px`
  (respectivamente) têm um gap de exatamente 20px entre o fim do
  `border_box` do primeiro e o início do `border_box` do segundo (não
  30px); o mesmo com `margin-top: -5px` no segundo NÃO colapsa (gap =
  10 + (-5) = 5px, comportamento de soma, não o algoritmo completo de
  negativos);
- o PRIMEIRO filho de um pai nunca colapsa sua margem superior com nada
  (mesmo com um valor grande, ex. `margin-top: 50px`, o filho fica
  exatamente 50px abaixo do topo do content box do pai).

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

## Tier 2 — depende de Tier 1

### Tarefa 3 — Render Pipeline: pintura da borda
**Depende de:** Tarefa 2 (`border_box` maior que `padding_box` quando há
borda efetiva) — mergeada.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v4 — Modelo de Caixa CSS
Completo" → "Render Pipeline — pintura da borda" inteira (nenhum paint op
kind novo — reaproveita `TBOX_PAINT_FILL_RECT`). `src/render/` atual —
onde o `FILL_RECT` de fundo já é emitido sobre `border_box` hoje, exatamente
o ponto onde os 4 `FILL_RECT` de borda entram, na mesma ordem relativa
(depois do fundo, antes de filhos/text runs).

**Arquivos a editar:**
- `src/render/` (arquivo que implementa `tbox_render_build_display_list`)
  — depois do `FILL_RECT` de fundo, se `effective_border > 0` (mesmo
  cálculo de `border_style == SOLID ? border_width : 0` da Tarefa 2, a
  refazer aqui a partir de `style` — Render Pipeline não recebe o valor
  já calculado da Layout Tree, só `style` e as rects do box), emitir até
  4 `FILL_RECT` com `style->border_color`, cada um a faixa entre
  `border_box` e `padding_box` do lado correspondente (topo: da borda
  esquerda à direita do `border_box`, altura = diferença de y entre
  `border_box`/`padding_box`; análogo pros outros 3 lados — atenção pra
  não desenhar os 4 cantos duas vezes, ou deixar buraco neles: a forma
  mais simples é topo/base cobrindo a LARGURA TOTAL do `border_box`
  (incluindo os cantos) e esquerda/direita cobrindo só a ALTURA do
  `padding_box` (sem repetir os cantos já cobertos por topo/base)).
- Atualizar o comentário de `tbox_render_build_display_list` em
  `include/tbox/render.h` pra mencionar a borda (sem mudar nenhuma
  assinatura nem tipo).
- `tests/render/test_render.c` — casos novos (grupo `render` já
  registrado)

**Casos de teste mínimos:**
- um box com borda efetiva > 0 produz, além do `FILL_RECT` de fundo, mais
  4 `FILL_RECT` com `color == border_color`, nas posições/tamanhos
  esperados (topo/direita/base/esquerda, geometricamente cobrindo
  exatamente `border_box` menos `padding_box`, sem sobreposição nem
  buraco nos cantos);
- um box sem borda efetiva (`border_style != SOLID`, ou `border_width ==
  0`) não produz nenhum `FILL_RECT` de borda — só o de fundo (se houver),
  igual a v0-v3 (regressão);
- a ordem dos ops no `tbox_display_list`: fundo, depois os 4 de borda (se
  houver), depois os `TEXT_RUN`/filhos — testar com um box que tem fundo
  E borda E texto/filho ao mesmo tempo.

**Critério de pronto:** `ctest --test-dir build -R '^render$'` verde +
suíte inteira sem regressão.

---

## Tier 3 — fatia vertical v4

### Tarefa 4 — Fatia vertical v4 completa (exemplo + validação)
**Depende de:** Tarefa 3 (mergeada) — e, transitivamente, todas as
outras.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v4 — Modelo de Caixa CSS
Completo" → "Fatia vertical v4 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (os mesmos fixtures da
   v3, sem remover nada do que já demonstram v0-v3) com: um elemento com
   `border: <largura>px solid <cor>` visível; um elemento com
   `border-style` efetivamente `none` (ou sem `border` nenhum) ao lado de
   um com `border-width`/`border-color` declarados mas sem `solid`, pra
   provar visualmente que não desenha nada; um elemento `position:
   relative` com `top`/`left` deslocado do fluxo normal, com um sibling
   logo depois na posição original (provando que o deslocado não
   empurrou ninguém); dois siblings de bloco com margens que colapsam
   (`margin-bottom` de um e `margin-top` do outro, valores diferentes,
   pra deixar visualmente óbvio que o gap é o maior dos dois, não a
   soma).
2. Nenhuma mudança de código em `example/tbox_app.c` deveria ser
   necessária (v4 não muda assinatura nenhuma que o exemplo já usa) — se
   alguma acabar sendo necessária, documentar por quê no relatório final.
3. Validação: mesmo padrão das fatias verticais anteriores —
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   tudo verde (incluindo `example/tbox_app_demo`), e, se
   `WAYLAND_DISPLAY` estiver setado (compositor real disponível), um
   smoke-test via `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que abre,
   roda e fecha sozinho sem crash, e inspeção visual (screenshot, se a
   ferramenta disponível suportar) confirmando os 4 itens do passo 1.

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação acima roda sem erro — este é o
"pronto" da v4 inteira, não só desta tarefa.
