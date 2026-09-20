# tbox — Tarefas da v5

Quebra da seção "v5 — `position: absolute`/`fixed`/`sticky`" do
`ARCHITECTURE.md` em tarefas executáveis por agentes sem contexto desta
conversa. Cada tarefa abaixo é auto-contida: aponta para a subseção exata
do `ARCHITECTURE.md` (a fonte da verdade de *o quê* construir) e acrescenta
só o que esse documento não cobre — caminho de arquivo, como registrar
teste, como verificar que ficou pronto.

(Este arquivo substitui a quebra de tarefas da v4, que está completa e
commitada — ver `ARCHITECTURE.md` para o design de cada camada v0-v5 e
`git log -- TASKS.md` para recuperar quebras de tarefas anteriores, se
precisar consultá-las.)

## Regra que vale pra TODA tarefa deste arquivo, sem exceção

**Nenhuma tarefa deve introduzir uma variável global/estática nova além
das que o `ARCHITECTURE.md` já especifica explicitamente pra ela** (ver
o item de débito "Thread-safety futura" no fim do `ARCHITECTURE.md`). A
v5 não especifica nenhum global/estático novo pra nenhuma das tarefas
abaixo (`tbox_layout_positioned_context` é passado por valor pela
recursão, não é global) — se a implementação de alguma parecer precisar
de um, **pare e reporte isso no relatório final da tarefa em vez de
adicionar por conta própria**.

## Convenção entre tarefas (leia antes de despachar qualquer uma)

**Cada tarefa só edita arquivos dentro do seu próprio diretório de
módulo** (`src/<módulo>/`, `include/tbox/<módulo>.h`, `tests/<módulo>/`)
mais os arquivos novos que ela mesma cria. Nenhuma tarefa cria módulo
novo nem grupo de teste novo nesta versão (todas estendem grupos já
registrados: `style`, `context`, `layout`) — sem passo de integração de
`tbox.h`/`CMakeLists.txt`/`tests/main.c`.

Toda tarefa que roda `cmake`/`ctest` assume o padrão já usado no
`CMakeLists.txt` raiz: `cmake -S . -B build && cmake --build build`, depois
`ctest --test-dir build` (ou `cd build && ctest`). Todo build precisa
passar limpo com `-Wall -Wextra -Wpedantic -Werror` (já é como o projeto
builda hoje — nenhuma tarefa deve silenciar warning, deve corrigi-lo).

A v5 não muda nenhuma assinatura de função já usada por
`example/tbox_app.c` — não há quebra esperada em `example/tbox_app_demo`
em nenhum tier antes da Tarefa 4; se algum tier quebrar esse alvo, é uma
regressão real, não uma quebra esperada.

---

## Tier 0 — paralelo, sem dependência de tarefa nova

### Tarefa 1 — Style: `position: absolute`/`fixed`/`sticky`
**Depende de:** nada. **Bloqueia:** Tarefa 3.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v5 — `position: absolute`/
`fixed`/`sticky`" → subseção "Style — `position: absolute`/`fixed`/
`sticky`" inteira (só 3 valores novos de enum, nenhum campo novo em
`tbox_style`). `include/tbox/style.h`/`src/style/tbox_style.c` atuais —
`tbox_style_position`/`tbox_style_resolve_position` (v4) é exatamente
onde os 3 valores novos entram.

**Arquivos a editar:**
- `include/tbox/style.h` — adicionar `TBOX_STYLE_POSITION_ABSOLUTE`,
  `TBOX_STYLE_POSITION_FIXED`, `TBOX_STYLE_POSITION_STICKY` ao enum
  `tbox_style_position` existente (depois de `RELATIVE`). Atualizar o
  comentário de `tbox_style_resolve` (bloco "Scope") pra listar os 3
  valores novos como suportados, removendo `absolute`/`fixed`/`sticky` de
  qualquer menção em "Out of scope".
- `src/style/tbox_style.c` — em `tbox_style_resolve_position` (a função
  `static` que hoje só reconhece `static`/`relative`), adicionar os 3
  `else if` de comparação case-insensitive pra `absolute`/`fixed`/
  `sticky`. Qualquer outro valor continua caindo no initial `STATIC`.
- `tests/style/test_style.c` — casos novos (grupo `style` já registrado):
  `position: absolute`/`fixed`/`sticky` cada um resolve pro enum certo;
  um valor não reconhecido (ex. `position: sticky-typo;`) continua caindo
  em `STATIC`; nenhum dos 3 herda do pai (mesmo padrão de teste já usado
  pra `relative` no v4 — filho sem `position` com pai `position: absolute`
  fica `STATIC`).

**Critério de pronto:** `ctest --test-dir build -R '^style$'` verde +
suíte inteira sem regressão.

---

### Tarefa 2 — Orchestration: correção de hit-test pra caixas fora de fluxo
**Depende de:** nada (o bug existe independente de `position` — já é
alcançável hoje com margens negativas do v4 produzindo sobreposição; a v5
só o torna comum e visível). **Bloqueia:** nada diretamente, mas deve
estar mergeada antes da Tarefa 4 (fatia vertical), já que o critério de
"pronto" da v5 depende dessa correção.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v5 — `position: absolute`/
`fixed`/`sticky`" → subseção "Orchestration — correção de hit-test pra
caixas fora de fluxo" inteira (o bug exato, por que a suposição do
comentário atual deixa de valer, e a correção de duas partes).
`src/context/tbox_context.c` atual — `tbox_context_hit_test_box` (a
função inteira, incluindo o comentário que documenta a suposição que está
sendo corrigida) e `tests/context/test_context.c` pra ver o padrão de
teste já usado pra hit-test.

**Arquivos a editar:**
- `src/context/tbox_context.c` — reescrever `tbox_context_hit_test_box`:
  (1) não retornar `NULL` cedo quando `box->border_box` não contém o
  ponto — sempre percorrer `first_child`/`next_sibling` primeiro; (2)
  entre os filhos cujo hit-test recursivo retornou não-NULL, guardar o
  ÚLTIMO (não retornar no primeiro que casar) — só depois de percorrer
  TODOS os filhos, se algum casou, retornar esse (o último); senão,
  checar se o `box` propriamente dito contém o ponto (retornando `box`)
  ou `NULL`. Atualizar o comentário da função pra refletir a nova lógica
  e por que a suposição antiga não vale mais.
- `tests/context/test_context.c` — casos novos (grupo `context` já
  registrado): duas caixas IRMÃS artificialmente sobrepostas (construa a
  árvore de teste diretamente com `tbox_layout_box`, sem precisar de
  `position` de verdade — só posicione os rects manualmente pra
  overlaparem, já que esta tarefa é sobre o hit-test em si, não sobre a
  Layout Tree) — um ponto na área de sobreposição deve retornar a caixa
  que é IRMÃ POSTERIOR na cadeia `next_sibling` (a "pintada por cima");
  uma caixa FILHA posicionada fora dos limites do `border_box` do PRÓPRIO
  PAI (rects manualmente construídos pra isso) ainda é encontrada por um
  ponto que cai só dentro dela, não do pai; regressão: os testes de
  hit-test já existentes (sem sobreposição) continuam passando sem
  mudança de resultado.

**Critério de pronto:** `ctest --test-dir build -R '^context$'` verde +
suíte inteira sem regressão.

---

## Tier 1 — depende de Tier 0

### Tarefa 3 — Layout Tree: containing block posicionado + geometria de `absolute`/`fixed`
**Depende de:** Tarefa 1 (`TBOX_STYLE_POSITION_ABSOLUTE`/`FIXED`/`STICKY`
em `tbox_style`) — mergeada. **Bloqueia:** Tarefa 4.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v5 — `position: absolute`/
`fixed`/`sticky`" → subseções "Layout Tree — containing block
posicionado", "Layout Tree — filhos fora de fluxo (`absolute`/`fixed`)" e
"Layout Tree — geometria de `absolute`/`fixed`" inteiras (a fonte da
verdade do algoritmo completo: `tbox_layout_positioned_context`, como ele
é threading pela recursão, como `tbox_layout_build_children` decide o
`container` de cada filho conforme `child_style->position`, e a fórmula
de `tbox_layout_resolve_absolute_edge`, incluindo o caso circular
top/bottom/height simplificado). `src/layout/tbox_layout.c` atual inteiro
— em particular tudo que a Tarefa 2 da v4 já modificou
(`tbox_layout_build_element`, `tbox_layout_build_children`,
`tbox_layout_containing_block`, `tbox_layout_resolve_offset`) — a v5
estende essas mesmas funções, não substitui nada do que já funciona pra
`STATIC`/`RELATIVE`.

**Arquivos a editar:**
- `src/layout/tbox_layout.c`:
  1. Definir `tbox_layout_positioned_context` (struct local ao arquivo,
     `nearest_ancestor`/`viewport`, ambos `tbox_rect`).
  2. `tbox_layout_build` inicializa `{ .nearest_ancestor = viewport,
     .viewport = viewport }` (o mesmo rect que já monta
     `tbox_layout_containing_block` da raiz hoje) e passa como parâmetro
     novo pra `tbox_layout_build_element`.
  3. `tbox_layout_build_element`/`tbox_layout_build_children` ganham esse
     parâmetro novo em suas assinaturas (funções `static`, sem impacto em
     `include/tbox/layout.h`). Depois de `box->padding_box` calculado
     (mesmo ponto de sempre): se `style->position !=
     TBOX_STYLE_POSITION_STATIC`, o `positioned_context.nearest_ancestor`
     passado pros FILHOS vira `box->padding_box`; senão passa o recebido
     sem alteração. `viewport` nunca muda em nenhum nível da recursão.
  4. Em `tbox_layout_build_children`: espiar `child_style->position` antes
     de montar cada filho (mesmo padrão de espiar `margin[0]` já usado
     pro collapsing). `ABSOLUTE`/`FIXED`: montar um `tbox_layout_containing_block`
     a partir de `positioned_context.nearest_ancestor` (pra `ABSOLUTE`) ou
     `positioned_context.viewport` (pra `FIXED`), `height_definite = true`
     sempre; chamar `tbox_layout_build_element` com esse container (em vez
     de `children_container`); **não tocar** `border_bottom`/
     `pending_margin_bottom`/`total_height` pra esse filho (ele nunca
     participa de fluxo/collapsing/auto-height do pai) — mas continuar
     linkando normalmente em `parent_box->first_child`/`last_child`/
     `next_sibling`. Qualquer outro valor de `position`: comportamento
     inalterado desde a Tarefa 2 da v4.
  5. Implementar `tbox_layout_resolve_absolute_edge` (par primário/oposto,
     mas resolvendo uma POSIÇÃO ABSOLUTA contra a origem/tamanho do
     containing block e o tamanho da margin box — não um delta como
     `tbox_layout_resolve_offset` do v4). Dentro de
     `tbox_layout_build_element`: quando `style->position` é `ABSOLUTE` ou
     `FIXED`, depois de `content_width`/`border_box.width` já resolvidos
     (precisos pra calcular `margin_box.width`), usar essa função pra
     achar `margin_box.x`/`margin_box.y` (eixo horizontal contra
     `container.x`/`container.width`; eixo vertical contra
     `container.y`/`container.height`, com o caso circular top-auto +
     bottom-definido + height-auto simplificado caindo no mesmo fallback
     de "ambos AUTO" — origem do container). Derivar `border_box`/
     `content_x`/`content_y` a partir daí (`border_box.x = margin_box.x +
     margin_left`, mesma relação já usada em todo o resto do arquivo).
     Para `STATIC`/`RELATIVE`/`STICKY`, nada muda nesse trecho (continua
     usando `cursor_y` + o deslocamento de `RELATIVE`/`STICKY` do v4).
- `tests/layout/test_layout.c` — casos novos (grupo `layout` já
  registrado):
  - um `position: absolute` filho de um `position: relative`, com `top`/
    `left` definidos, posiciona-se contra o `padding_box` do pai
    `relative` (não contra o viewport, não contra o `content_box`);
  - um `position: absolute` SEM nenhum ancestral posicionado posiciona-se
    contra o viewport inteiro;
  - um `position: fixed` aninhado dentro de um `position: relative`
    posiciona-se contra o viewport, ignorando o ancestral `relative`
    (diferente do caso `absolute` acima);
  - um `position: sticky` com `top`/`left` produz EXATAMENTE o mesmo
    `content_box`/`border_box`/`margin_box` que o mesmo elemento teria com
    `position: relative` e os mesmos offsets (prova do "sticky == relative");
  - um filho `absolute`/`fixed` NÃO soma no `total_height` (auto-height)
    do pai, e NÃO participa de margin collapsing com nenhum sibling (um
    sibling em fluxo antes/depois dele posiciona-se como se o elemento
    fora de fluxo não existisse);
  - `width: auto`/`height: auto` num `absolute` preenchem o containing
    block posicionado (menos margem/padding/borda) — não encolhem pro
    conteúdo (prova da simplificação documentada);
  - `left`/`right`/`top`/`bottom` todos `auto` num `absolute` caem no
    fallback (origem do containing block).

**Critério de pronto:** `ctest --test-dir build -R '^layout$'` verde +
suíte inteira sem regressão.

---

## Tier 2 — fatia vertical v5

### Tarefa 4 — Fatia vertical v5 completa (exemplo + validação)
**Depende de:** Tarefa 2 e Tarefa 3 (mergeadas) — e, transitivamente,
Tarefa 1.

**Leia primeiro:** `ARCHITECTURE.md`, seção "v5 — `position: absolute`/
`fixed`/`sticky`" → "Fatia vertical v5 — critério de 'pronto'" inteira.

**Trabalho:**
1. Estender `example/tbox_app_demo.html`/`.css` (fixtures acumulados de
   v0-v4, sem remover nada) com: um `position: relative` contendo um
   filho `position: absolute` visivelmente posicionado DENTRO da área do
   pai; um `position: absolute` SEM ancestral posicionado, visivelmente
   posicionado contra o viewport inteiro (ex.: um canto da janela); um
   `position: fixed` aninhado dentro de um ancestral `position: relative`
   diferente do de cima, posicionado contra o viewport (não contra esse
   ancestral) — prova visual de que `fixed` ignora ancestrais
   posicionados; um `position: sticky` com `top` declarado, ao lado (ou
   com comentário explicando) que deve parecer idêntico a como
   `position: relative` com o mesmo `top` já parece em outro elemento do
   próprio fixture; um `position: absolute` posicionado de propósito de
   forma a escapar visualmente do `border_box` do seu pai DOM, com um
   handler de clique registrado nele (reaproveitando a infraestrutura de
   clique da v1/v3 — `tbox_context_on_click`) que muda sua cor de fundo,
   pra provar interativamente (clicando na área "escapada") que o
   hit-test corrigido da Tarefa 2 realmente funciona.
2. Nenhuma mudança de assinatura deveria ser necessária em
   `example/tbox_app.c` além de, possivelmente, registrar mais um
   `tbox_context_on_click` pro elemento do item acima (mesmo padrão já
   usado pros outros handlers de clique existentes) — se algo mais
   precisar mudar, documentar por quê no relatório final.
3. Validação: mesmo padrão das fatias verticais anteriores —
   `cmake -S . -B build && cmake --build build && ctest --test-dir build`
   tudo verde (incluindo `example/tbox_app_demo`), e, se
   `WAYLAND_DISPLAY` estiver setado (compositor real disponível), um
   smoke-test via `TBOX_WAYLAND_CLOSE_DELAY_MS` confirmando que abre,
   roda e fecha sozinho sem crash, e — mesmo padrão usado na Tarefa 4 da
   v4 — captura de tela (`grim`, se disponível) com inspeção por
   amostragem de pixel confirmando os itens do passo 1, incluindo clicar
   (via simulação real se disponível, ou registrando o clique
   manualmente através da API se não houver `ydotool`/`wtype` — mesmo
   fallback já aceito desde a v3) no elemento `absolute` "escapado" pra
   confirmar que o handler dispara.

**Critério de pronto:** build limpo (TODOS os alvos, incluindo o exemplo)
+ suíte inteira passando + a validação acima roda sem erro — este é o
"pronto" da v5 inteira, não só desta tarefa.
