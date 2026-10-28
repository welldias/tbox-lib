# Histórico de versões

> - **Escopo:** o que cada versão (v0–v20) trouxe, em forma condensada.
> - **Código relacionado:** todo o pipeline
> - **Veja também:** [Decisões](decisoes.md), [Débito técnico](debito-tecnico.md)

Este histórico substitui o antigo `ARCHITECTURE.md`, que crescia uma seção por
versão. Comentários no código ainda citam seções desse texto pelo nome
("v9 — Escopo", "Orchestration — correção de hit-test pra caixas fora de
fluxo", "Convenções" etc.). Os nomes estão preservados abaixo, em negrito,
para busca; o texto integral original está no git:

```sh
git show fe51140:ARCHITECTURE.md
```

Cada versão foi uma "fatia vertical": um critério de pronto ponta a ponta,
validado com `example/tbox_app_demo.html` e, a partir da v7, com screenshot
headless.

## Após v20 — Caminhada do estilo sem cópias para quem não muda (2026-10-27)

Por elemento, a caminhada do estilo ainda gastava ~1600 instruções em
`tbox_svg_is_element` (comparava cada ancestral com "svg" e
"foreignobject" sem olhar o tamanho antes), ~930 em
`tbox_style_generated_allowed` (21 nomes, chamada duas vezes) e ~940 em
copiar o estilo do modelo para a pilha e compará-lo inteiro depois dos
passos por nó. As duas primeiras comparam o tamanho antes das letras, e a
segunda roda uma vez por elemento. Um elemento que compartilha e não tem
passo por nó (contêiner de consulta, marcador, partes de controle; a
condição de entrada de cada um, `tbox_style_has_node_steps`) usa o estilo
do modelo como está, sem cópia nem comparação; os inline ainda ganham a
cópia própria, feita direto do modelo. Um teste novo cobre o caso que a
comparação quadro a quadro não pegava: um defeito injetado (nenhum passo por
nó) passava pelos 9301 quadros; com o teste, um `<li>` que compartilha a
cascata mas não o `::marker` falha. Medido por instruções (callgrind,
Release): resolução de estilo −31% e frame −11,5% na tabela de 200 linhas;
−19% e −11,6% na tabela com elementos inline; −3,3% e −2,6% no
file-explorer. Os 9301 quadros de 131 documentos e os 142 da página
adversária e da tabela com elementos inline são idênticos; testes e
`tbox_frame_dump` passam com ASan+UBSan.

## Após v20 — Índice de regras por pseudo-elemento (2026-10-27)

Cada elemento rodava duas cascatas, de `::before` e de `::after`, porque a
folha do user agent cita os dois (para `<q>`). Elas reuniam os candidatos
de um índice comum a todos os pseudo-elementos (`::marker`,
`::placeholder` e as partes dos controles são universais) e descartavam um
por um. Cada nome de pseudo-elemento de uma folha agora tem as próprias
regras e o próprio índice (`tbox_css_pseudo_element_rules`), e a cascata
volta antes de alocar quando nenhum balde serve ao elemento
(`tbox_css_cascade_may_match`). A busca do nome recebe uma
`tbox_string_view` calculada uma vez por cascata. O índice comum saiu.
Medido por instruções (callgrind, Release): resolução de estilo −20% e
frame −8,4% na tabela de 200 linhas; −16% e −10% na tabela com elementos
inline; file-explorer igual. Os 9301 quadros de 131 documentos e os 142 da
página adversária e da tabela com elementos inline são idênticos; testes e
`tbox_frame_dump` passam com ASan+UBSan.

## Após v20 — Cascata pulada entre elementos iguais (passo C) (2026-10-27)

Um elemento com a tag, os atributos e a classe do pai de um modelo do
compartilhamento agora pula também a cascata, desde que as guardas do
modelo valham para ele (ver
[HTML e CSS](../07-html-e-css.md#compartilhamento-de-estilo) e
[Decisões](decisoes.md)). O matcher marca as avaliações que leem posição,
irmãos, filhos, estado ou identidade de nó
(`tbox_css_selector_matches_traced`), e a cascata as guarda com o
resultado, junto das condições de `@container`
(`tbox_css_cascade_resolve_traced`, `tbox_css_cascade_guards_hold`). O
modelo passou a guardar uma cópia inteira do resultado da cascata, em vez
das declarações resumidas. A primeira versão marcava também os
pseudo-elementos que aparecem em seletores da cascata do elemento (eles
nunca casam ali) e não pulava nada; a lista passou a ser das pseudo-classes
que leem contexto. Conferido com uma comparação temporária de cada cascata
pulada contra a real (1 milhão de pulos em 133 documentos, incluindo uma
página com `:nth-child` em ancestral, `+`, `~`, `:hover`, `:empty`,
`@container`, `@scope` e `:checked`), removida depois; um defeito injetado
(guardas sempre válidas) quebra o teste novo e a comparação quadro a
quadro. Medido por instruções (callgrind, Release): resolução de estilo
−40% e frame −22% na tabela de 200 linhas; −28% e −20% na tabela com
elementos inline; file-explorer −3% e −2,6%. Memória igual. Os 9301 quadros
de 131 documentos e os 142 da página adversária e da tabela com elementos
inline são idênticos aos do HEAD; testes e `tbox_frame_dump` passam com
ASan+UBSan.

## Após v20 — Busca de face, comparação de tag e pseudo-elementos sem regra (2026-10-27)

Duas funções que o layout chama várias vezes por caixa custavam caro:
`tbox_layout_style_face` (~1260 instruções; a família era copiada com
validação UTF-8 a cada busca, e as opções padrão de shaping eram zeradas a
cada chamada) e `tbox_layout_is_inline_box` (~1850; cada nome de tag era
comparado por uma função de outra unidade, com `strlen`). O cache de faces
agora encontra uma face pela própria `tbox_string_view` da lista de
famílias, com o tamanho guardado na entrada, e só copia ao criar uma; as
opções padrão são uma constante. `tbox_layout_tag` passou a ser `inline` no
header interno, comparando o tamanho antes dos bytes. Medido por instruções
(callgrind, Release): layout −24% na tabela de 200 linhas e −20% na
tabela com elementos inline; frame −9% e −4,5%.

No estilo, cada elemento rodava a cascata de `::first-letter` mesmo sem
nenhuma regra para ele: o atalho da cascata de pseudo-elementos só valia
quando a folha não tinha regra de pseudo-elemento nenhuma, e a folha do
user agent tem várias. Cada folha agora guarda os nomes de pseudo-elemento
que suas regras citam (`tbox_css_stylesheet_names_pseudo_element`, lidos
como a cascata os compara), e a cascata de um pseudo-elemento que nenhuma
folha cita volta logo. `tbox_string_view_equal_cstr` compara byte a byte
sem `strlen`, saindo na primeira diferença (listas de nomes de tag, como a
de `tbox_style_generated_allowed`). Resolução de estilo −13% e frame mais
−9% na tabela de 200 linhas. Os 9301 quadros de 131 documentos são
idênticos; testes e `tbox_frame_dump` passam com ASan+UBSan.

## Após v20 — Texto da célula medido uma vez (2026-10-27)

Cada célula de tabela coletava e media suas palavras duas vezes por frame:
na medida intrínseca das colunas e de novo no layout do texto. A medida
agora guarda as palavras no rascunho da tabela e o layout da célula as
copia (`tbox_layout_premeasured`, ver
[Layout e pintura](../08-layout-e-pintura.md#rascunho-do-layout) e
[Decisões](decisoes.md)); células com inline-block, float ou imagem
continuam coletando de novo. O shaping já era feito uma vez, pelo cache de
runs. Conferido com uma comparação temporária palavra a palavra nos 131
documentos e na tabela com elementos inline (528 mil caixas, todas
idênticas byte a byte fora do ponteiro do texto, que é uma cópia com o
mesmo conteúdo), removida depois. Medido por instruções (callgrind,
Release, o relógio da máquina oscila ±15%): layout −9,6% na tabela de 200
linhas e na tabela com elementos inline, frame −4% e −2,4%. Os 9301
quadros de 131 documentos são idênticos; testes e `tbox_frame_dump` passam
com ASan+UBSan.

## Após v20 — Display list fora da arena do frame (2026-10-27)

A display list crescia num vetor na arena do frame, e cada vez que dobrava
deixava a cópia anterior lá: 38 MB para 10 MB de ops numa tabela de 1000
linhas com um elemento inline por célula. Ela passou a crescer num
rascunho e é copiada no tamanho exato ao final (ver
[Layout e pintura](../08-layout-e-pintura.md#da-caixa-ao-desenho)); os
vetores de cada contexto de empilhamento e o espaço de troca do
`box-reflect` também foram para o rascunho, e as marcas de
`text-emphasis` moldam os glifos na arena passada, não na do vetor. O
contexto copiava a lista inteira duas vezes por frame, para pôr as barras
de rolagem (mesmo sem nenhuma) e os controles por cima; agora pinta os dois
num vetor à parte e junta numa cópia exata, só se houver algo. Medido em
Release na tabela com elementos inline: memória da display list de 38 para
9,9 MB, arena do frame de 76 para 38 MB, RSS de 117 para 97 MB; tabela
simples com 1000 linhas, arena de 8,1 para 7,7 MB. O tempo não mudou além
do ruído. Os 9301 quadros de 131 documentos são idênticos; testes e
`tbox_frame_dump` passam com ASan+UBSan.

## Após v20 — Dados raros fora do `tbox_style` (2026-10-27)

`tbox_style` caiu de 9688 para 4088 bytes: listas de `transition` e
`animation`, `list-style-image`, `list-style-type` personalizado e
`marker-*`, imagem e gradiente da 1ª camada de fundo, `transform` com
`offset-position`/`offset-anchor`, `font-feature-settings` e
`font-palette`, `scroll-margin`/`scroll-padding`, as linhas de grid do item
e as sobreposições de `::marker` viraram grupos fora de linha, no padrão
dos já existentes (ver
[Guia da API](../03-guia-da-api.md#grupos-fora-de-linha-de-tbox_style) e
[Decisões](decisoes.md)); `grid_units` foi para `grid_templates`.
`tbox_style_animatable_write`, `tbox_style_resolve_paint`,
`tbox_style_resolve_opentype`, `tbox_style_parse_opentype` e
`tbox_layout_frame_remap` ganharam o parâmetro de que precisam (arena ou
grupo). Medido em Release com uma tabela de 1000 linhas e um `<span>`,
`<a>` ou `<b>` em cada célula (5000 estilos inline próprios): memória do
estilo de ~49 MB (pelo tamanho antigo) para 20,6 MB; contra o HEAD de
antes do compartilhamento por ponteiro, estilo de 102,6 para 20,6 MB,
arena do frame de 179 para 76 MB, RSS de 218 para 117 MB, frame de ~97
para ~77 ms. A tabela simples não muda (estilo já compartilhado, 0,5 MB);
o frame do file-explorer fica ~4% abaixo do HEAD (12,6 → 12,1 ms,
alternando as duas builds; perto do ruído). Os 9301 quadros de 131 documentos
são idênticos; testes e `tbox_frame_dump` passam com ASan+UBSan.

## Após v20 — Rascunho do layout fora da arena do frame (2026-10-27)

Medido por pilha de chamada de cada alocação: ~60% da memória do layout de
uma tabela era trabalho intermediário que ficava na `frame_arena` até o
frame seguinte (palavras montadas duas vezes por célula, saída do shaping
copiada para o run, cópia das palavras no split, linhas, vetores que dobram).
O texto de cada caixa, a grade de cada tabela e as medidas de tamanho
intrínseco passaram a usar uma arena de rascunho local, destruída ao
retornar; runs, glifos e bordas colapsadas são copiados para a arena do
frame no tamanho exato (ver
[Layout e pintura](../08-layout-e-pintura.md#rascunho-do-layout) e
[Decisões](decisoes.md)). `tbox_layout_split_overlong_words` só copia as
palavras quando alguma é quebrada. `tbox_layout_push_run` e
`tbox_layout_ellipsize_line` recebem a arena onde os glifos ficam, em vez
de usar a do vetor de runs. Medido em Release (tabela de 5 colunas,
`border-collapse`): memória do layout com 1000 linhas de 27,5 para 6,1 MB,
arena do frame de 30 para 8,1 MB, RSS de 48 para 27 MB; com 2000 linhas,
layout de 55 para 12 MB e RSS de 81 para 42 MB. Tempo do layout com 1000
linhas de ~21 para ~18 ms; file-explorer igual, dentro do ruído (~13,7 ms). Os 9301 quadros de 131 documentos são idênticos
aos de antes; testes e o `tbox_frame_dump` de todos os documentos passam com
ASan+UBSan, que acusaria um ponteiro da árvore para um rascunho já liberado.

## Após v20 — Um `tbox_style` para elementos iguais (2026-10-27)

O compartilhamento de estilo poupava a conversão, mas cada entrada da
tabela ainda guardava uma cópia de 9688 bytes do estilo do modelo:
~56 MiB com 1000 linhas de tabela. `tbox_style_entry.style` passou a ser
`const tbox_style *`; elementos em nível de bloco iguais ao modelo apontam
para o estilo dele, e quem altera uma entrada passa por
`tbox_style_table_mutable`, que copia antes (ver
[HTML e CSS](../07-html-e-css.md#compartilhamento-de-estilo) e
[Decisões](decisoes.md)). O cursor e o image map, que achavam o elemento
de um run pelo endereço do estilo, agora procuram dentro do elemento da
caixa (`tbox_context_run_element`). Medido em Release (tabela de 5 colunas,
`border-collapse`): memória do estilo com 1000 linhas de 55,9 para 0,6 MB,
arena do frame de 85 para 30 MB, RSS de 103 para 48 MB, frame de 50 para
35 ms; com 2000 linhas, arena de 169 para 59 MB, RSS de 192 para 81 MB,
frame de 118 para 83 ms. File-explorer: frame igual (12,4 ms), estilo
isolado de 9,8 para 11,1 ms. Os 9301 quadros de 131 documentos (fixtures,
exemplos e dois documentos com linhas iguais com `transition`, `:hover`,
`::first-letter`, spans irmãos com borda, marcadores, `::before`, container
queries, animação e image map) são idênticos aos de antes; compartilhar
também os inline faz a comparação acusar o documento dos spans. Teste novo
em `tests/style/test_style.c`, conferido contra os inline compartilhados,
contra o modelo gravável e contra a cópia esquecida no acessor.

## Após v20 — Ferramentas de medição: `tbox_bench` e `tbox_frame_dump` (2026-10-26)

As medições e verificações das otimizações de tabelas grandes eram feitas
com programas soltos. Viraram duas ferramentas em `tests/` (ver
[Ferramentas e testes](../12-ferramentas-e-testes.md)): `tbox_bench`
cronometra o frame e suas etapas e mede a memória, numa página ou numa
tabela sintética de N linhas; `tbox_frame_dump` assina cada frame
(geometria, display list, pixels) de um conjunto de documentos, com hover
e rolagem, e compara as assinaturas de dois builds. Ambas usam a fonte
embutida e são ligadas à biblioteca estática, porque leem campos internos
do contexto. O `ctest` ganhou `tool_bench` e `tool_frame_dump`, que só
conferem que elas rodam. Validação: a assinatura das fixtures e exemplos é
idêntica entre a versão atual e a anterior ao compartilhamento de estilo e
ao índice de regras (9656 frames), e o defeito de considerar só a
primeira classe do elemento aparece em 77 documentos.

De passagem, o cppcheck achou um defeito no culling da display list:
`tbox_render_shadow_reach(shadows, tbox_render_box_shadows(style, &shadows, …))`
dependia da ordem de avaliação dos argumentos, que o C não define; com a
outra ordem, a função receberia `NULL` com contagem positiva. O GCC
avaliava na ordem que funciona, por isso nada falhou. A contagem agora é
calculada antes.

## Após v20 — Validadores de declaração só para quem eles conhecem (2026-10-26)

`tbox_css_cascade_offer` chamava seis validadores para toda declaração
oferecida a todo elemento, e cada um comparava o nome com sua lista: ~21%
do frame. Um filtro por nome (`tbox_style_declaration_checked`, ver
[Decisões](decisoes.md)) deixa passar só as 44 propriedades que eles
conhecem e os nomes `-webkit-`. Medido em Release: estilo com 1000 linhas
de tabela de 29 para 20 ms, frame de 55 para 46 ms (200 linhas: 9,6 para
7,6 ms; 2000: 113 para 96 ms); file-explorer de 12,2 para 10,8 ms. Os 9928
quadros da comparação com a versão de referência e as 123 fixtures seguem
idênticos. O teste novo acusa nome faltando, fora de ordem e obsoleto na
tabela.

## Após v20 — Índice de regras na cascata (2026-10-26)

A cascata de cada elemento passa a visitar só as regras cujo seletor pode
casar com ele, por um índice montado na folha pelo id, classe ou tag do
componente mais à direita (ver [HTML e CSS](../07-html-e-css.md#seletores)
e [Decisões](decisoes.md)). Medido antes: 171 seletores por elemento numa
tabela, 307 no file-explorer; com o índice, 6 e 14. Medido em Release:
estilo com 1000 linhas de tabela de 45 para 29 ms, frame de 71 para 55 ms
(200 linhas: 12,8 para 9,6 ms; 2000: 144 para 113 ms); frame do
file-explorer de 17,0 para 12,2 ms, do tbox_app_demo de 4,0 para 2,8 ms
(esses dois contra a versão sem compartilhamento nem índice). Conferido
contra a versão sem compartilhamento nem índice em 136 documentos, inclusive
um de seletores (`:where`, `:not`, atributos, maiúsculas, SVG, várias
classes, ids, listas, combinadores, `@scope`, `@media`, `@layer`, CSS
aninhado, `!important`): 9928 quadros idênticos. A mesma comparação acusa
candidatas fora de ordem (2 documentos) e classes ignoradas (70); tags com
outra caixa não chegam ao DOM (parser e `tbox_html_element_create` as
passam para minúsculas). Teste novo em `tests/css_cascade/test_cascade.c`,
conferido contra as duas falhas. Resolve o débito "Regras sem índice por
seletor".

## Após v20 — Compartilhamento de estilo e `border` sem `border-image` (2026-10-26)

Elementos iguais (mesma tag, atributos, classe do pai e resultado da
cascata) passam a copiar o `tbox_style` de um modelo em vez de converter as
declarações de novo (ver [HTML e CSS](../07-html-e-css.md#compartilhamento-de-estilo)
e [Decisões](decisoes.md)). E `border` deixou de alocar uma cópia de 1720
bytes do `border-image` inicial por elemento. Medido em Release (tabela de
5 colunas, 1000 linhas): estilo de 110 para 47 ms numa tabela simples, de
122 para 51 ms com zebra, `:first-child` e `:hover`, de 140 para 62 ms com
seletores de ancestral e irmão; frame inteiro de 131 para 71 ms (200
linhas: 25 para 13 ms; 2000: 269 para 144 ms); arena do frame de 93 para
85 MB. Conferido contra uma variante sem compartilhamento em 134 documentos
(fixtures, exemplos, tabelas com zebra/hover/seletores de ancestral e irmão,
um documento com variáveis CSS, marcadores, `::first-letter`, contadores,
`attr()`, container queries, `:empty`, `dir`, controles, `img` com
atributos, estilo inline e SVG), com hover em 73 pontos e rolagem: 9782
quadros com geometria de caixas e textos, ops e pixels idênticos; a mesma
comparação acusa 12 documentos quando a igualdade da cascata é ignorada.
Testes novos em `tests/style/test_style.c`, conferidos contra o
compartilhamento desligado, contra a classe do pai ignorada e contra a
cópia do `border-image`.

## Após v20 — Culling da display list (2026-10-26)

Uma tabela num contêiner rolável emitia ops para todas as linhas, inclusive
as fora da área visível, que o clip descartava só na rasterização. O Render
agora pula a subárvore de um filho cuja pintura não alcança o clip do
contêiner `overflow`, por um limite conservador calculado das caixas, e
descarta as bordas colapsadas da tabela fora do clip (ver
[Layout e pintura](../08-layout-e-pintura.md#culling)). Medido em Release
(tabela de 5 colunas, `border-collapse`, contêiner de 600px): 342 ops com
200, 1000 ou 2000 linhas (antes 3222, 16022 e 32022); com 1000 linhas o
diff do damage tracking foi de 11 para 0,24 ms, a display list de 19 MB
para quase nada e a arena do frame de 121 para 93 MB. Pixels idênticos ao
código anterior nas 123 fixtures, em 129 documentos (fixtures e exemplos)
renderizados antes e depois de rolar todos os contêineres, e num
documento de casos de borda (conteúdo transbordando, sombras, `outline`,
inline com padding grande, `text-shadow`, transformação, relativo e
absoluto deslocados, margem negativa, tabela colapsada) rolado em 30
posições. Testes novos em `tests/render/test_composition.c`, conferidos
contra o culling desligado, contra sombras fora do limite e contra as
bordas colapsadas sem culling.

## Após v20 — Tabela de estilos com capacidade reservada (2026-10-26)

O vetor da tabela de estilos crescia dobrando dentro da `frame_arena`, que
não libera as cópias superadas: com 1000 linhas de tabela, 151 MiB alocados
para 56 MiB de entradas. `tbox_style_resolve_tree*` agora conta os
elementos e reserva uma entrada para cada (ver [Decisões](decisoes.md)).
Medido em Release (tabela de 5 colunas, `border-collapse`): arena do frame
com 1000 linhas de 217 para 121 MB, RSS de 219 para 142 MB, frame de 151
para 138 ms; com 200 linhas, frame de 33 para 27 ms. Com `::before` em toda
célula o vetor dobra uma vez: 176 MB contra 312 MB no código anterior às
mudanças de 2026-10-26. Screenshots das 123 fixtures idênticos.

## Após v20 — Estado de transição só para quem declara `transition` (2026-10-26)

`tbox_context_apply_transitions` criava um estado de 4128 bytes para todo
elemento e o procurava numa lista ligada: com 1000 linhas de tabela, 173 ms
por frame e ~25 MB; com 2000, 679 ms. Agora só elementos com `transition`
efetiva têm estado, achado por um índice por hash montado a cada frame, e
os estados não vistos no frame são liberados (ver
[Decisões](decisoes.md) para o que deixa de animar). De quebra, um
elemento visto pela primeira vez em `display: none` não anima mais do zero
até o próprio valor (antes, uma transição invisível pedia frames ao abrir o
documento). Medido em Release: frame inteiro com 1000 linhas de 331 para
151 ms, com 2000 de 1037 para 325 ms; com `tr { transition: … }` em todas as
linhas, a etapa custa 1,6 ms com 1000 linhas (antes 166 ms) e guarda 1001
estados em vez de 6011. Teste novo em `tests/context/test_context.c`,
conferido contra as duas regressões (estado para todos; elemento oculto
animando). Screenshots das 123 fixtures idênticos.

## Após v20 — Cache de runs que cresce e cache de avanços (2026-10-26)

Acima de ~300 linhas, uma tabela tinha mais palavras distintas que as 1024
runs do cache da face, que se esvaziava várias vezes por frame: o layout
saltava de 7,8 para 71 ms entre 300 e 400 linhas. O cache agora dobra até
65536 posições e, no limite, descarta só o que não foi reaproveitado (ver
[Texto e fontes](../09-texto-e-fontes.md#cache-de-runs)); os avanços de
glifo ficam em cache na face em vez de um `FT_Load_Glyph` com hinting por
glifo a cada shaping. Medido em Release (tabela de 5 colunas,
`border-collapse`): layout com 1000 linhas de 172 para 28 ms, com 2000 de
386 para 62 ms; frame inteiro com 1000 linhas de 481 para 331 ms. Os
screenshots das 123 fixtures são idênticos byte a byte aos de antes. O
teste do cache em `tests/font/test_font.c` passa pelo crescimento e pela
varredura e confere os glifos contra uma face nova.

## Após v20 — Busca de estilo por hash (2026-10-26)

Uma tabela com muitas linhas ficava lenta: `tbox_style_table_find`
percorria todas as entradas (9,7 KB cada), e layout, render e contexto a
chamam várias vezes por nó, o que tornava o layout quadrático. A tabela
ganhou um índice por hash de ponteiro (`tbox_style_table.index_`, ver
[Decisões](decisoes.md)). Medido em Release, tabela de 5 colunas com
`border-collapse` dentro de um `overflow: auto`: layout com 1000 linhas
foi de 249 para 172 ms, com 2000 de 717 para 386 ms, e o frame inteiro com
2000 linhas de 1710 para 1335 ms. A contagem de instruções do layout agora
cresce linearmente. Os demais custos medidos estão em
[Débito técnico](debito-tecnico.md#tabelas-grandes).

## Após v20 — Itens de `<template>`, classes e `hidden` em `controls.h` (2026-10-25)

Pedido do t-explorer, que só mexe na tela por `controls.h`: faltava como
montar listas, cards e linhas com ícone e botão, e como marcar estado
visual. Entraram `tbox_control_instantiate` (copia os filhos de um
`<template>` para um contêiner), `tbox_control_remove`/`_clear`,
`tbox_control_has_class`/`_set_class`, `tbox_control_hidden`/`_set_hidden`
e, em `app.h`, `tbox_app_query_in` (busca dentro de um item). A folha UA
ganhou `[hidden] { display: none }`; até então só o foco ignorava elementos
`hidden`, mas eles continuavam no layout. Remover, limpar e esconder tiram
o foco de dentro do que sai, como `set_disabled` já fazia (lógica
compartilhada em `tbox_context_blur_within`). Testes em
`tests/context/test_controls_api.c` (`tbox_test_generic_items`), conferidos
sem a regra `[hidden]` (falham) e com ASan+UBSan.

## Após v20 — tbox como subprojeto CMake e pacote instalável (2026-10-25)

O tbox passa a poder ser incluído por outro projeto com `add_subdirectory`
ou `FetchContent`, e instalado com `cmake --install` para
`find_package(tbox)`. O alvo é `tbox::tbox` nos dois casos. Caminhos usam
`PROJECT_SOURCE_DIR`/`PROJECT_BINARY_DIR` em vez de
`CMAKE_SOURCE_DIR`/`CMAKE_BINARY_DIR`. Exemplos, testes, `-Werror`
(`TBOX_WARNINGS_AS_ERRORS`, nova), regras de install (`TBOX_INSTALL`,
nova), o build type padrão e o alvo `cppcheck` só valem quando o tbox é o
projeto raiz. Fontconfig e HarfBuzz são detectados na raiz, como o Wayland,
com alvos `PkgConfig::TBOX_*`; os `PARENT_SCOPE` e as listas de
`*_LIBRARIES`/`*_INCLUDE_DIRS` nos exemplos e no `tbox_cmp` saíram. As fontes
são compiladas uma vez (`tbox_objects`) para a estática e a compartilhada:
o build completo caiu de 323 para 206 passos. A `.so` ganhou soname
`libtbox.so.0.1` e exporta os mesmos 944 símbolos de antes. O mínimo de CMake
caiu de 4.0 para 3.25 (build e `ctest` conferidos com o 3.25.3). O alias sem
namespace `tbox` foi removido; os exemplos usam `tbox::tbox`.

## Após v20 — Cantos arredondados nos controles (2026-10-22)

A folha UA passa a dar `border-radius: 4px` a `button`, `input`, `select` e
`textarea`; checkbox 2px, botão interno do `type=file` 3px; `range` e
`type=image` (sem borda) ficam com 0 para não recortar trilha e imagem;
radio continua redondo. Conferido por screenshot antes/depois em todos os
fixtures `forms-*` e no `keyboard_demo`: só mudam os cantos; partes
internas (setas do number, limpar do search) e anel de foco seguem
corretos; `forms-progress-and-output` idêntico.

## Após v20 — `keyboard_demo` reescrito (2026-10-22)

O exemplo volta ao propósito de mostrar os controles no visual padrão: o CSS
estiliza só a página (cabeçalho, cartões, grade rótulo + controle, lista
rolável) e nenhum controle. O C caiu de 166 para ~100 linhas: um único log
do último evento (click, input com validade de email/url/number, caminho
do arquivo, select, submit) substitui um parágrafo de status por controle.
Entraram `<progress>` e `<meter>`; saiu o `input type=hidden` (invisível).
O exemplo expôs que `input type=button/submit/reset` não têm largura
intrínseca (ver [débito](debito-tecnico.md#largura-dos-botões-input)).

## Após v20 — Busca de elementos no módulo app (2026-10-22)

`tbox_context_get_element_by_id/_query/_query_all` saem de `context.h` e
viram privadas (`src/context/tbox_context_controls.h`); a API pública é
`tbox_app_get_element_by_id`, `tbox_app_query` e `tbox_app_query_all`
(`app.h`, adaptadores em `src/app/tbox_app_controls.c`). Com
`node->owner_document`, os exemplos deixam de precisar de `tbox_context`.

## Após v20 — API de controles no módulo app (2026-10-22)

As funções de `controls.h` passam a receber `tbox_app *` em vez de
`tbox_context *`, como os registros de eventos. Adaptadores em
`src/app/tbox_app_controls.c` obtêm o contexto por `tbox_app_context`, por
isso também compilam com `tbox_app_unavailable.c` (sem backend, tudo vira
no-op). A implementação fica privada no contexto (`tbox_context_control_*`,
`tbox_context_select_*`, em `src/context/tbox_context_controls.h`), usada
pelos testes do contexto. As funções que só leem o nó continuam sem app.

## Após v20 — Redesenho automático após mutação do DOM (2026-10-22)

`tbox_html_node.owner_document` liga cada nó ao seu documento, e
`tbox_html_document_revision` conta as mutações (inserir, remover,
atributos, texto; também escolha de `<select>` e foco, por
`tbox_html_document_touch` interno). `tbox_app` guarda a revisão lida logo
após cada `run_frame` e redesenha (e não dorme) quando ela muda, então
mutação fora de handler dispensa `tbox_app_request_redraw` — que fica para
escrita direta em campos da struct. Teste em `test_app_lifecycle.c` cobre
mudança fora de handler e loop ocioso com controles que o frame normaliza.

## Após v20 — API de controles e DOM (2026-10-22)

Camada para controlar o conteúdo dos controles por código, sem saber onde
cada um guarda o estado. Novo `include/tbox/controls.h`
(`src/context/tbox_context_controls.c` e funções de `<select>` em
`tbox_context_select.c`): `tbox_control_value/_set_value`, `_label/_set_label`,
`_disabled/_set_disabled`, `_focus/_blur`, `_checked/_set_checked` (radio
desmarca o grupo), `_radio_checked`, e `tbox_select_option_count/_at`,
`_selected_index/_set_selected_index`, `_add_option`, `_remove_option`,
`_clear`. `tbox_context_select_value/_set_value` saíram da API pública
(absorvidas por `tbox_control_value/_set_value`).

DOM (`html_parser.h`): `tbox_html_element_create`/`_text_create` (copiam),
`_node_clone`, `_insert_before`, `_prepend_child`, `_replace`,
`_remove_children`, navegação só por elementos, `_has_attribute`,
`_set_bool_attribute` e classes (`_has/_add/_remove/_toggle_class`).
`append_child` passou a mover um nó já inserido e a recusar ciclos;
`set_text_content` desliga os filhos antigos um a um (sem ponteiros
velhos). Busca: `tbox_context_get_element_by_id`, `tbox_context_query`,
`tbox_context_query_all` (nós mutáveis, sem cast),
`tbox_css_selector_query_first` e `tbox_css_selector_closest`.
Exemplo `example/controls_demo.c`; demais exemplos migrados.

## Após v20 — Exemplo de tabela formatada (2026-10-21)

Adicionado `tests/assets/html-tables-styled.html`: resumo de pedidos com
legenda, cabeçalho, linhas alternadas e hover, valores alinhados, status
coloridos e rodapé de total com `colspan`. Exemplo autocontido em 800x600,
catalogado na referência de fixtures.

## Após v20 — Eventos do ciclo de vida da aplicação (2026-10-21)

Acrescentados `tbox_app_on_load`, `on_unload`, `on_pagehide`, `on_pageshow`,
`on_resize` e `on_error`. Load espera o primeiro frame, unload é único antes
da destruição, pagehide/pageshow representam foco da janela e resize
informa dimensões anterior/nova. Error tem registro e limpeza, sem emissão.
Callbacks sobrevivem à recarga e podem substituir registros, recarregar ou
fechar a aplicação com destruição adiada. Wayland entrega transições de
foco de teclado; builds sem backend preservam símbolos no-op. Incluídos
testes do loop com backend simulado e `example/lifecycle_demo.c`, sem novo
estado global mutável.

## Após v20 — SVG inline básico (2026-10-21)

Implementados os ícones e o medidor SVG do file explorer: formas básicas,
`defs`/`symbol`/`use` locais, viewports, transformações afins, preenchimento,
traço e tracejado com `pathLength`. Atributos entram na cascata abaixo das
folhas de autor; instâncias têm herança própria, sem alterar o DOM. Caminhos
compartilham o parser com motion path; FreeType rasteriza os contornos com
estado local, sem novo global mutável ou dependência. Damage assina a
geometria por conteúdo. Incluídos testes de pixels, herança, ciclos e
repintura parcial e seis fixtures SVG de paridade com Firefox.

## Após v20 — Pendências visuais do explorador de arquivos

Implementados sequencialmente `placeholder`/`::placeholder`, caixas próprias
para elementos inline `absolute`/`fixed` e `:focus-visible` por modalidade.
A busca vazia passa a mostrar o texto auxiliar; o span “Opções” recebe o
recorte de `.visually-hidden`; as regras de foco por teclado passam a casar.
Placeholder preserva valor, seleção e altura do input; textarea mantém suas
linhas editáveis independentes. Nenhum novo estado global mutável.

Incluídos testes de interação, geometria e pixels, fixture `forms-placeholder`
e casos em `selectors-states` e `layout-block-in-inline`. Atualizados os mapas
e o catálogo do explorador; resolvido o débito de inline posicionado oculto.
SVG fica para a última etapa; extensões WebKit são ignoradas; semântica
permanece adiada. O template do explorador foi preservado.

## Após v20 — FreeType obrigatório do sistema

FreeType passa a ser localizado por `find_package(Freetype REQUIRED)`;
bibliotecas, exemplos e ferramentas usam `Freetype::Freetype`. Removidos
o download via `FetchContent`, a compilação da cópia estática e a imposição
global de `BUILD_SHARED_LIBS=OFF`. A instalação de desenvolvimento do
FreeType é obrigatória antes do configure. Métricas, rasterização,
contornos e a integração de shaping permanecem com as mesmas funções.

## Após v20 — Shaping com HarfBuzz

HarfBuzz entra como shaper opcional (`TBOX_ENABLE_HARFBUZZ`, padrão `ON`):
`src/font/tbox_font_shaper_harfbuzz.c` ou `tbox_font_shaper_simple.c`
(o comportamento anterior, idêntico pixel a pixel nos 141 fixtures), atrás
de `src/font/tbox_font_face_internal.h`. Ligaduras, formas contextuais
(árabe: `سلام` vira 3 glifos), composição de acentos, GPOS. O medidor passa
a moldar o trecho inteiro e andar por ele (`tbox_text_measurer_init` recebe o
texto; `tbox_text_measurer_finish`; `tbox_text_prefix_width` para cursor e
seleção). Com HarfBuzz, 117 dos 141 fixtures mudam alguns pixels: o kerning
GPOS (fracionário) desloca o texto.

## Após v20 — Texto como glifos posicionados

O layout passa a entregar glifos, não só texto (etapas 1–4 do item 6).
`tbox_glyph` (`<tbox/font.h>`: `id`, `cluster`, `x`, `y`, `advance`);
`tbox_font_shape` gera um glifo por passo do `tbox_text_measurer`;
`tbox_layout_text_run` e o `TEXT_RUN` carregam `glyphs`/`glyph_count`; o
raster pinta por índice (`tbox_font_rasterize_glyph_id`) sem decodificar
UTF-8 nem calcular kerning, e molda na hora só um op montado à mão.
Reticências cortam o vetor de glifos; `text-emphasis` centra nos glifos;
`word-spacing` e `justify` deixam de partir a linha em um run por palavra;
o hash de damage inclui os glifos. Os 141 fixtures de `tests/assets`
renderizam idênticos pixel a pixel antes e depois. Próximo passo: HarfBuzz
dentro do shaper.

## Após v20 — Texto e números centralizados em `src/base/`

Cinco ajustes estruturais em sequência. (1) `tbox_utf8_copy_truncated`
corta cópias para campos fixos de `tbox_style` e buffers de caminho sempre
entre codepoints (antes `text-overflow: "a→→→→→→"` saía com meia seta).
(2) Máscara de senha (`src/layout/tbox_layout_password.h`) única para layout
e contexto. (3) `tbox_ascii_is_space`, `tbox_string_view_trim_ascii_space`,
`tbox_string_view_starts_with_ascii_ci` e `tbox_string_copy_lower_ascii`
substituem 11 `is_space`, 5 `trim`, 3 `copy_lower` e os prefixos sem caixa
locais, além das 17 chamadas a `<ctype.h>` (que aceitavam `'\v'` e
dependiam do locale); só o XPath mantém seu conjunto de espaços. (4) `src/base/tbox_number` lê e escreve
números sem `LC_NUMERIC`: com pt_BR, `strtod("1.5")` dava 1. (5)
`tbox_text_measurer` (`src/font/tbox_font_measure.h`) mede prefixos de forma
incremental, com resultado idêntico bit a bit; cursor de `<input>` e
`<textarea>`, quebra do `<textarea>`, reticências, `overflow-wrap` e
`text-emphasis` passam de O(n²) para O(n). Testes novos em
`tests/base/test_number.c` (inclusive com `LC_NUMERIC=pt_BR`),
`test_string.c`, `test_font.c`, `test_layout.c` e `test_style.c`.

## Após v20 — UTF-8 validado na entrada

Texto que entra por `tbox_html_parse`, `tbox_css_parse`, setters do DOM e
paste é saneado (`tbox_utf8_sanitize`: malformado vira U+FFFD). Um único
decodificador limitado à view (`tbox_utf8_decode`/`_next`/`_previous` em
`src/base/tbox_string`) substitui `utf8codepoint` do utf8.h e as varreduras
`& 0xC0` espalhadas pelo layout, contexto e render. Corrige leitura além do
fim de uma view com sequência truncada e o cursor que travava num byte
inválido do `value` inicial. Depois, o resto do utf8.h (caixa e
codificação) também foi para trás de `tbox_string.h`, e o caminho de include
`external/utf8.h` passou a valer só para `src/base/tbox_string.c`; o
codificador manual de `tbox_style_generated.c` foi removido. Testes em `tests/base/test_string.c`,
`tests/html_parser/test_tree.c` e `tests/context/test_context.c`.

## Após v20 — Funções CSS de custo médio

`conic-gradient()`/`repeating-conic-gradient()` com ângulo inicial, centro,
stops angulares/percentuais, fundos e máscaras. `polygon()` com até 32 vértices,
`nonzero`/`evenodd`, composição aninhada e recorte de eventos/barras de rolagem.
Fix-up de stops omitidos agora compara com a última posição explícita.
Hash de polígonos por geometria; testes de pixels, hit-test e repintura parcial.
Fixtures 139–140. Campos novos de estilo/pintura exigem recompilar consumidores.

## Após v20 — Catálogo por custo e trigonometria inversa

Catálogo das 27 funções então ausentes, ordenadas por custo e dependências.
Implementadas as quatro fáceis: `asin()`, `acos()`, `atan()` e `atan2()`,
com ângulos tipados, expressões angulares e consumo em gradientes/filtros.
Testes de validação e equivalência de pixels; fixture 138. Restam 23 ausentes.

## Após v20 — Trigonometria e raios radiais

`sin()`/`cos()`/`tan()` aceitam números em radianos e ângulos literais. Os
gradientes radiais, inclusive repetidos/máscaras, aceitam raios explícitos de
círculo/elipse, com percentuais por eixo na elipse. O hash de repintura inclui
os raios. Testes de estilo, pixels e equivalência de repintura parcial;
fixture `tests/assets/137.html`. O acréscimo de `radius` à estrutura pública
`tbox_style_gradient` exige recompilar consumidores.

## Após v20 — Sinal, constantes e opacidade calculada

`abs()`/`sign()` operam sobre números/comprimentos; `hypot()` passa a aceitar
comprimentos compatíveis. `pi`/`e` são operandos numéricos em cálculos.
`opacity` reutiliza o avaliador, convertendo percentuais para números e
limitando a faixa somente no final. Testes de estilo e pixels, com tipos,
sinais de zero e rejeição de entradas inválidas; fixture `tests/assets/136.html`.

## Após v20 — Hipotenusa, passos e validação de color-mix

`hypot()` avalia até oito números. `round()` oferece quatro estratégias em
números/comprimentos; `mod()` e `rem()` distinguem os sinais do divisor e do
dividendo. Leitura de argumentos compartilhada com exponenciais/comparações.
`color-mix()` rejeita espaços desconhecidos, pesos não finitos/fora de 0%–100%
e soma zero, mantendo a normalização de pesos válidos. Testes de estilo e
cores; fixture `tests/assets/135.html`.

## Após v20 — Cores explícitas sRGB, rect e exponenciais

`color()` aceita `srgb` e `srgb-linear`, componentes numéricos/percentuais e
alfa. `clip-path: rect()` compartilha `round` e a representação de `inset()`,
com coordenadas, `auto` e dimensões vazias para arestas cruzadas. `pow()`,
`sqrt()`, `exp()` e `log()` avaliam números finitos dentro dos cálculos de
comprimento. Testes de cores, estilo e pixels; fixture `tests/assets/134.html`.

## Após v20 — Cálculos percentuais, recortes e matiz

`min()`/`max()`/`clamp()` comparam percentuais e expressões afins compatíveis;
`min()`/`max()` de um argumento preservam o valor. `clip-path: xywh()` reutiliza
`inset()`, cujo `round` passa a preservar percentuais e a normalizar raios
elípticos juntos. `color-mix(in oklch … hue, …)` aplica os quatro percursos de
matiz. Testes de estilo, cores e render; fixture `tests/assets/133.html`.
Os campos percentuais acrescentados a `tbox_style_clip_path` exigem recompilar
consumidores da biblioteca.

## Após v20 — Organização das funções CSS

Extraídos os parsers de funções para `tbox_style_functions.c` e os utilitários
de valores para `tbox_style_values.c`, com contratos internos compartilhados.
A resolução de propriedades e o comportamento CSS permanecem preservados.

## Após v20 — Catálogo de funções CSS

Auditoria das 80 entradas da tabela de funções CSS: marca suporte existente,
suporte com limitações e ausências, com referências à implementação.

## Após v20 — Indicadores HTML

`<output>` passa a ter estilo inline e texto atualizável pelo DOM.
`<progress>` e `<meter>` usam caixas inline-block e pintura de barras passivas,
com atributos numéricos lidos a cada frame. Fixture: `tests/assets/106.html`.

## Após v20 — Anotações ruby

`<ruby>` agrupa trechos de texto-base e `<rt>` em células indivisíveis,
com anotação centralizada acima. `<rp>` fica oculto no layout suportado.
Exemplo e teste: `tests/assets/114.html`.

## Após v20 — Recursos locais e mapas de imagem

`<link rel=stylesheet>` lê CSS local em ordem com `<style>`. `<source>`
seleciona a imagem em `<picture>`, e `<base href>` define o prefixo local para
CSS vinculado e imagens. `<map>`/`<area>` traduzem cliques na imagem para
regiões retangulares, circulares ou poligonais. Testes e exemplos:
`tests/assets/110.html` a `113.html`.

## Após v20 — Grupos, detalhes e legendas HTML

`<optgroup>` acrescenta títulos e opções aninhadas ao `<select>`.
`<details>`/`<summary>` alternam o atributo `open` por clique ou teclado;
o layout omite o conteúdo fechado. O primeiro `<legend>` é posicionado
sobre a borda de `<fieldset>`, que abre espaço para seu texto. Fixtures:
`tests/assets/107.html` a `109.html`.

## v0 — HTML+CSS estático numa janela

Primeira fatia: um `<div>` com `width`/`height`/`background-color` vindos do
CSS, e `<h1>`–`<h6>`/`<p>` com texto, passando por todo o pipeline real.

- **Visão geral das camadas**: Base, HTML Parser, CSS Parser, CSS Selector e
  CSS Cascade já existiam; entraram **Style**, **Fonte / Texto**, **Layout
  Tree**, **Render Pipeline**, **Output Display**, **Orchestration / Main
  Loop** e **Application**.
- **Convenções** (hoje em [convencoes.md](convencoes.md)).
- **Fonte / Texto — camada transversal**: `tbox_font_source` (fontconfig e
  embedded/Liberation Sans), `tbox_font_face`, rasterização de glifo; uma face
  de 16px para o documento todo, sem shaping.
- **Style — resolução de valores tipados**: `display`, `width`, `height`,
  `margin`, `padding`, `color`, `background-color`; inicial de `display` =
  `block` por falta de UA stylesheet.
- **Layout Tree**: fluxo de bloco; texto só em `h1`–`h6`/`p` (lista fixa de
  "text tags"), uma linha, sem quebra; só o primeiro elemento de topo do
  documento é laid out.
- **Render Pipeline**: `FILL_RECT` e `TEXT_RUN` em pré-ordem.
- **Output Display**: `tbox_raster_*` em software (XRGB8888) + backend Wayland
  migrado de `example/` para `src/`.
- **Orchestration / Main Loop**: `tbox_context` com `frame_arena`, recompute
  total; hit-test linear.
- **Build system (v0)**: FreeType obrigatório; Fontconfig e Wayland opcionais.

## v1 — Interatividade

Um clique muda uma classe e a tela reflete, sem fechar a janela.

- **HTML Parser — mutação de atributo**: `tbox_html_node_set_attribute`/`_get_attribute`.
- **Orchestration (`tbox_context`) — delegação de evento por seletor**:
  `tbox_context_on_click`, `tbox_context_dispatch_click`, `tbox_context_document`.
- **Output Display (Wayland backend) — evento de ponteiro**: `take_click`.
- **Application (`tbox_app`) — loop não-bloqueante**: `tbox_app_create`/`_step`/
  `_should_close`/`_close` substituem `tbox_app_open`.

## v2 — Fidelidade Visual

Headings maiores e em negrito, margens de parágrafo, quebra de linha real.

- **CSS Cascade / Orchestration — folha de estilo user-agent** e
  **Configuração da UA stylesheet** (`tbox_ua_style_config`).
- **Style — `font-size` e `font-weight`** (`em`/`%`/`px` contra o pai).
- **Fonte / Texto — cache de faces por (peso, tamanho)**: `tbox_font_face_cache`.
- **Layout Tree — inline formatting context real**: `tbox_layout_text_run`,
  quebra gulosa por palavra; inline (`<b>`, `<em>`) contribui runs, sem caixa
  própria. **Cor não varia por run** (resolvido na v13).
- **Render Pipeline — múltiplos runs por caixa**.
- **Application / Orchestration — fiação do cache de fontes e do config**.

## v3 — Interatividade Avançada

- **Robustez de parsing — o que a auditoria confirmou**; **CSS Parser —
  correção do bug de profundidade de parênteses** (`FUNCTION` conta como
  abertura ao pular blocos).
- **HTML Parser — mutação de texto**: `tbox_html_node_set_text_content`.
- **`:hover` — pseudo-classe dinâmica de verdade**: `tbox_css_selector_set_hover_context`,
  `tbox_context_update_hover`.
- **Bubbling completo + `stopPropagation` + desregistro de handler**: handler
  retorna `bool`; `tbox_context_unbind_click`.
- **Application — leitura de arquivo externo**: `tbox_app_create_from_files`.

## v4 — Modelo de Caixa CSS Completo

- **Style — `border` (shorthand único, só `solid`)**.
- **Style — `position: relative` + offsets**.
- **Layout Tree — geometria de borda**; **deslocamento visual de `position:
  relative`**; **margin collapsing entre siblings**.
- **Render Pipeline — pintura da borda** (quatro `FILL_RECT`).

## v5 — `position: absolute`/`fixed`/`sticky`

- **Escopo deliberadamente contido**: `sticky` = `relative` (sem rolagem na
  época), sem `z-index`, sem §10.3.7 completo, sem posição estática.
- **Style — `position: absolute`/`fixed`/`sticky`**.
- **Layout Tree — containing block posicionado** (`tbox_layout_positioned_context`);
  **filhos fora de fluxo**; **geometria de `absolute`/`fixed`**.
- **Orchestration — correção de hit-test pra caixas fora de fluxo**: visita
  todos os filhos e fica com o último que casa.

## v6 — Robustez de HTML

- **HTML Parser — fechamento implícito de `<p>`/`<li>`**.
- **HTML Parser — decodificação de entidades**: numéricas + ~23 nomeadas;
  `;` obrigatório; nada em `<script>`/`<style>`.

## v7 — Listas HTML e tabela completa de entidades

- **Layout Tree — `<li>` como text tag**.
- **HTML Parser — tabela completa de entidades nomeadas**: gerada do
  `entities.json` do WHATWG (~2200 entradas de 1 codepoint); nomes com dígitos.

## Ferramentas de desenvolvimento — captura de tela headless (`--screenshot`)

- **Output Display — `tbox_raster_write_png`** (sem libpng/zlib).
- **Application — `tbox_app_screenshot_from_files`**.
- **Example — flag `--screenshot <path>`** em `example/tbox_app.c`.

## v8 — Marcadores de lista + `em` além de `font-size`

- **Style — `em` em `width`/`height`/`margin`/`padding`/offsets** (contra o
  `font-size` do próprio nó).
- **Layout Tree — marcador de `<li>`** (texto prefixado; substituído na v18).
- **CSS Cascade / Orchestration — UA stylesheet de `<ul>`/`<ol>`/`<li>`**.

## v9 — CSS de autor: inline (`style=""`) e `<style>` interno

- **CSS Cascade — `TBOX_CSS_ORIGIN_AUTHOR_INLINE` + resolução de inline dentro
  de `tbox_css_cascade_resolve`** (tabela de rank com 4 origens).
- **Orchestration — extração de `<style>` do documento inteiro**
  (`internal_stylesheet`; interno vence externo em empate).

## v10 — `tbox_cmp`: regressão visual contra fixtures reais

- `tests/tbox_cmp.cpp` (C++/OpenCV, opcional, fora do `ctest`), pares
  `tests/assets/NNN.html`/`NNN.png`.
- **Build — OpenCV opcional (C++ isolado) + `tbox_cmp` como executável próprio**.

## v11 — `<hr>`, `<br>`, `<pre>` e `text-align`

- **Style — `text-align`** (`left`/`center`/`right`).
- **Layout Tree — `<br>` (quebra forçada)**, **`<pre>` (texto verbatim)**,
  **aplicação de `text-align`**.
- **Orchestration — UA stylesheet de `<hr>`**.

## v12 — CSS `font-family`

- **CSS Cascade / Font — assinaturas exatas**: `tbox_font_resolver_fn`.
- **Font — cache com resolução sob demanda** por (família, peso).
- **Style — `font-family`** (primeiro nome, `char[64]`).
- **Application — resolução sob demanda + limpeza de duplicação**.
- **Layout Tree — os 6 pontos de chamada**.
- **Orchestration — `<pre>` monoespaçado via UA stylesheet**.

## v13 — `<strong>`, `<i>`, `<em>`, `<small>`, `<mark>`, `<del>`, `<ins>`, `<sub>`, `<sup>`

- **Font — `italic` deixa de ser hardcoded**.
- **Style — `font-style`, `text-decoration`, `vertical-align`**.
- **Layout Tree — `tbox_layout_text_run.style` + alinhamento de linha de base**
  (`tbox_layout_line.ascent`).
- **Render Pipeline — destaque de fundo e linha de decoração por run**.

## v14 — Caixas de bloco anônimas

Conteúdo inline solto dentro de `<body>`/`<div>` sumia. Agora cada sequência
contígua de inline vira uma caixa anônima (`node == NULL`).

- **Escopo**, **Layout Tree — assinaturas exatas**, **Algoritmo de
  `tbox_layout_build_children` (detecção de sequência)**.

## v15 — Infraestrutura de layout e `inline-block`

- **Infraestrutura**: coleta inline recursiva, `tbox_layout_translate`,
  `tbox_layout_build_element_sized`, tamanhos intrínsecos (min/max-content).
- **`inline-block` (e `inline-flex`)** como palavra atômica na linha.

## v16 — Flexbox

`tbox_layout_build_flex`, seguindo CSS Flexbox Level 1 §9 (itens, base,
linhas, resolução de comprimentos flexíveis, alinhamentos, margens auto).

## v17 — Fechando as limitações da v15/v16

`<img>` como caixa própria (block/flex item), inline-block com descendentes
posicionados, `vertical-align` aumentando a linha, flex com min/max-height
sem altura definida, controles de formulário como `inline-block` na UA.

## v18 — Novos atributos de estilo

- **Style — unidades, `calc()` e palavras-chave globais**.
- **Style — demais propriedades** (overflow-x, z-index, aspect-ratio,
  line-clamp, transform translate, sombras em lista, `background` completo…).
- **Layout Tree** (marcador `outside`, `table-layout: fixed`, small-caps,
  `tbox_layout_finish`).
- **Render Pipeline / Output Display** (gradientes, cantos elípticos,
  contextos de empilhamento, `tbox_render_paint_order`).
- **Orchestration / Application** (`tbox_context_cursor_at`, cursor via
  `wayland-cursor`).

## v19 — Mais atributos de estilo

- **CSS Parser / Cascade**: custom properties, `::marker`, cores CSS Color 4/5.
- **Style**: `var()`, `min()`/`max()`/`clamp()`, min/max/fit-content, filtros,
  `clip-path`, várias camadas de fundo…
- **Layout Tree**: `text-indent: hanging`, `text-wrap: balance`, soft hyphen,
  `display: list-item`, kerning.
- **Render / Output**: bordas 3D, tracejado arredondado, `filter`, `clip-path`.
- **Orchestration**: `position: sticky` real (eixo vertical), `scrollbar-width: none`.

## v20 — Repintura parcial (damage tracking)

Antes cada frame sujo repintava a janela inteira; agora só as regiões que
mudaram. Ver [Atualização da tela](../05-atualizacao-da-tela.md).

- **Output Display**: `tbox_damage_tracker`, `tbox_raster_display_list_damaged`,
  dano por região no Wayland.
- **Application**: `tbox_app_set_debug_damage`.
- **Detecção de mudanças por hash** (FNV-1a por op).
- **Próximos passos**: em [Débito técnico](debito-tecnico.md#pipeline).

## Sem número de versão

Entraram ao longo das versões acima sem seção própria no documento antigo
(só no README e no código):
controles de formulário (inputs de texto, data, cor, arquivo, range, select,
textarea), foco por teclado, seleção e clipboard, rolagem com scrollbars,
tabelas, `@media`, `transition`, `@font-face`, `:active`, temas dos popups
(`tbox_context_options.control_css`) e a **Entrada e fronteira de backend**
(`src/output/tbox_window_backend.h`, `tbox_input_event`).

Daqui em diante, cada mudança relevante ganha uma entrada curta aqui.

## 2026-10-11 — Propriedades CSS de baixo custo

- **Style**: `border-block-width/-style/-color` e `border-inline-*` (um ou
  dois valores); `hyphenate-character` (herdado, string até 15 bytes);
  `clip: rect()` convertido em `clip-path: inset()`; `overscroll-behavior`
  (eixo vertical); `scroll-margin-*` e `scroll-padding-*` (com formas
  lógicas, via `tbox_style_resolve_box_edges`). `tbox_style` ganha
  `overscroll_y`, `hyphenate_character`, `scroll_margin[4]`,
  `scroll_padding[4]`.
- **Layout Tree**: o traço da quebra em soft hyphen vem de
  `hyphenate-character`.
- **Orchestration**: `tbox_context_scroll` não passa a rolagem ao ancestral
  com `overscroll-behavior` diferente de `auto`; `tbox_context_reveal_focused`
  respeita `scroll-padding` e `scroll-margin`.
- Fixture `115`.

## 2026-10-11 — Mais propriedades CSS de baixo custo

- **Style**: `isolation` (`isolation_isolate`), `text-justify`
  (`text_justify_none`), `line-break` (`line_break_anywhere`),
  `font-variant-position` sintetizado (`font_variant_position`; encolhe a
  fonte onde o valor muda e mapeia para `vertical-align: sub/super`) e
  `all` (expande a palavra-chave para todas as propriedades da tabela de
  `tbox_style_keywords.c`). O `display` inicial usado por
  `initial`/`unset`/`all` passou a ser `inline`, como no CSS.
- **Layout Tree**: `text-justify: none` não distribui espaço;
  `line-break: anywhere` quebra como `break-all` e entra no min-content.
- **Render**: `isolation: isolate` cria contexto de empilhamento.
- Referência: `@media` e `@font-face` estavam marcados como não suportados
  por engano; agora `X*`.
- Fixture `116`.

## 2026-10-11 — Media features, `@import`, `font-synthesis`, `light-dark()`

- **CSS Cascade**: `tbox_css_media_feature` ganha `aspect-ratio`,
  `resolution`, `color`/`monochrome`, a forma booleana e features de valor
  fixo (`prefers-color-scheme: light`, `hover`, `pointer: fine`,
  `scripting: none`…).
- **Orchestration**: `@import` no início de `<style>`, `<link>` e do CSS
  da aplicação é expandido no texto antes do parse
  (`tbox_context_append_css`), relativo à folha que importa.
- **Style**: `font-synthesis` (`font_synthesis_no_small_caps`,
  `font_synthesis_no_position`); `color-scheme` (`color_scheme_dark`) e
  `light-dark()`, substituído no texto depois de `var()`
  (`tbox_style_light_dark_substitute`).
- Referência: `@charset` marcado como suportado (já era ignorado sem
  quebrar nada). Fixture `117`.

## 2026-10-11 — Bloco dentro de parágrafo e listas aninhadas

- **Layout Tree**: `tbox_layout_is_inline_box` (classificação e medição
  intrínseca) faz de `<p>`, `<h1>`–`<h6>`, `<li>`, `<td>`/`<th>`,
  `<caption>`, `<summary>` com filho de bloco em fluxo um container de
  bloco; substitui o desvio por nome de tag que só o `<td>` tinha. O
  marcador do `<li>` passa à primeira caixa anônima
  (`tbox_layout_build_anonymous_box`/`tbox_layout_build_text_runs` ganham
  `marker_node`), ou fica ao lado do primeiro bloco.
- **HTML Parser**: o fechamento implícito de `<li>` para na fronteira da
  lista (`tbox_html_tree_builder_find_open_li`); listas aninhadas sem
  `</li>` explícito do item de fora deixaram de virar irmãs.
- Débito "Bloco dentro de conteúdo inline some" resolvido; ficaram "Bloco
  dentro de um elemento inline" e "Margem de listas aninhadas". Fixture
  `118`. Nenhum fixture de 001–116 mudou de pixels.

## 2026-10-11 — `@layer`

- **CSS Parser**: `@layer` (declaração, bloco, nomes com ponto, anônima,
  aninhada com `@media`). Cada regra recebe `tbox_css_ruleset.layer` (0 =
  fora de camada; senão a posição pós-ordem da camada na folha). Antes, o
  conteúdo de um `@layer` era descartado.
- **CSS Cascade**: `tbox_css_cascade_priority_compare` compara a camada
  entre origem/importância e especificidade (invertida para `!important`).
- **Orchestration**: `@import … layer(nome)`/`layer` embrulha o texto
  importado em `@layer`; declarações `@layer a, b;` antes dos `@import`
  são mantidas no lugar.
- Fixture `119`. Nenhum fixture de 001–116 mudou de pixels.

## 2026-10-11 — Cursor padrão sobre texto

- **Orchestration**: com `cursor: auto`, `tbox_context_cursor_at` não
  devolve mais o cursor de texto sobre texto comum, só sobre campos
  editáveis; `cursor: text` explícito continua valendo.

## 2026-10-11 — `font-size-adjust`, `scroll-behavior`, `@starting-style`, `@namespace`

- **Font**: `tbox_font_face_x_height` (OS/2 `sxHeight`, ou o glifo "x").
- **Style / Layout Tree**: `font-size-adjust` (`font_size_adjust`),
  aplicado só na escolha da face (`tbox_layout_style_face`);
  `scroll-behavior` (`scroll_behavior_smooth`).
- **Orchestration**: rolagem suave em `tbox_scroll_state` (animação pelo
  relógio das transições) para revelar o foco e clique na trilha;
  transições de entrada com `@starting-style`, inclusive ao sair de
  `display: none`.
- **CSS Parser / Cascade**: `@starting-style` marcado em
  `tbox_css_ruleset.starting_style`, fora da cascata normal;
  `tbox_css_cascade_resolve_starting_style_in_viewport`.
- **Application**: o screenshot headless avança animações de entrada até o
  fim antes de capturar.
- Referência: `@namespace` marcado (ignorado, o que para HTML é correto).
  Fixture `120`; nenhum outro fixture mudou de pixels.

## 2026-10-11 — `::before`/`::after`, contadores e `quotes`

- **CSS Parser**: `:before`/`:after`/`:first-line`/`:first-letter` (CSS2)
  normalizados para `::`; `@starting-style` à parte (ver acima).
- **CSS Cascade**: pseudo-elementos pesam como seletor de tipo (antes
  `::marker` pesava como classe).
- **Style**: `tbox_style_generated.c` — durante `tbox_style_resolve_tree`
  (ordem de documento) resolve `::before`/`::after` só quando alguma regra
  os usa, mantém contadores com escopo e a profundidade de `quotes`, e
  monta o texto de `content`. Cada pseudo vira um nó sintético no arena da
  tabela de estilos, com entrada própria (`tbox_style.generated` = 1/2).
- **Layout Tree**: `tbox_layout_build` encaixa os nós sintéticos durante a
  montagem e os retira no fim.
- **Orchestration**: `tbox_context_hit_test` devolve a caixa do elemento
  dono quando o ponto cai em conteúdo gerado (os nós sintéticos duram um
  frame); transições ignoram os pseudos.
- Fixture `121`; nenhum outro fixture mudou; suíte limpa com ASan/UBSan.

## 2026-10-12 — Transições ampliadas, `@supports`, `@keyframes`, efeitos e `direction`

- **Style**: `tbox_style_animate.c` — tabela de propriedades animáveis
  (`tbox_style_animatable`, máscara por nome em `transition-property`),
  leitura/escrita como 4 doubles e funções de tempo (`cubic-bezier()`,
  `steps()`). `transition` vira uma lista (`tbox_style.transitions`,
  substitui `transition_duration[3]`/`_delay`/`_timing`) com os longhands
  pareados por índice; `animation` (`tbox_style.animations`);
  `drop-shadow()` em `filter`; `backdrop-filter`
  (`backdrop_filter_matrix`); `background-blend-mode` por camada;
  `scroll-snap-type`/`-align`/`-stop`; `direction` (e o atributo `dir`) com
  o mapeamento lógico invertido e `text_align_logical`; `text-emphasis`.
  Novas entradas na tabela de palavras-chave globais.
- **CSS Parser**: `@supports` avaliado no parse (`tbox_css_supports`, também
  usado no `supports()` do `@import`); `@keyframes` guardado na folha
  (`tbox_css_stylesheet_keyframes`).
- **CSS Cascade**: `tbox_css_cascade_media_matches` público, para os
  `@keyframes` dentro de `@media`.
- **Orchestration**: transições em todas as propriedades animáveis, com o
  estado de trilhas alocado sob demanda; `tbox_context_animation.c` roda as
  animações depois das transições; scroll snap em `tbox_scroll_state`
  (encaixe 0,15 s depois do último giro da roda, ao soltar a barra ou depois
  de um clique na trilha).
- **Layout Tree**: flex `row` começa na direita com `direction: rtl`;
  `tbox_layout_text_run.emphasis_font` para marcas `<string>`.
- **Render Pipeline / Output**: op `TBOX_PAINT_BACKDROP_FILTER`;
  `tbox_paint_op.blend_mode` em `IMAGE`/`GRADIENT` (16 modos no raster, no
  hash do damage tracker); marcas de `text-emphasis`.
- Referência: animation\*, `@keyframes`, `@supports`, `backdrop-filter`,
  `background-blend-mode`, `direction`, scroll-snap\*, text-emphasis\* e as
  propriedades aceitas sem efeito (break-\*, page-break-\*, `orphans`,
  `widows`, `clear`, `overflow-anchor`, `perspective`, `perspective-origin`,
  `transform-style`, `backface-visibility`) marcadas `X*`. Corrigida a lista
  "O que fica de fora" de [HTML e CSS](../07-html-e-css.md), que ainda
  citava `::before`/`::after` e `@import`.
- Fixtures `122`–`131`; nenhum fixture de 001–121 mudou de pixels; suíte
  limpa com ASan/UBSan.

## 2026-10-12 — Camadas de composição

- **Style**: `filter` e `backdrop-filter` como listas ordenadas
  (`tbox_style_filter_list`: matriz de cor, `blur()`, `drop-shadow()`);
  `has_filter`/`filter_matrix` ficam só para filtros de cor;
  `backdrop_filter_matrix` saiu. `mix-blend-mode`, `mask`/`mask-*` (e
  `-webkit-mask*`), `tbox_style_needs_layer`, `tbox_style_filter_extent`. A
  aproximação de `drop-shadow()` por `box-shadow`/`text-shadow` saiu.
- **Layout Tree**: `tbox_layout_box.mask_image`.
- **Render Pipeline**: ops `LAYER_BEGIN`/`LAYER_MASK`/`LAYER_END` em volta
  de quem precisa de camada (o elemento vira contexto de empilhamento,
  também com filtro de cor ou `backdrop-filter`); `opacity` sai do op a op
  para o `LAYER_BEGIN`; `clip-path` de um elemento com camada recorta a
  composição; o fundo com `background-blend-mode` ganha camada própria.
- **Output**: pixels pré-multiplicados (a tela segue opaca); pintor com
  pilha de camadas (`tbox_raster_paint_range`), blur gaussiano, sombra,
  máscara alfa/luminância e composição com `mix-blend-mode`; o
  `BACKDROP_FILTER` roda a lista de filtros (com blur). Damage tracker:
  bounds de camada, ops inflados pelo alcance do blur, e a região suja
  cresce para cobrir o que um `backdrop-filter` com blur lê.
- **CSS Parser**: `@supports` aceita `blur()` e conhece `mix-blend-mode` e
  `mask-*`.
- Fixture `132` (e `mask-star.png`). Mudaram de pixel: `036` (texto sobre o
  próprio fundo com `opacity` deixou de ficar transparente, como no
  navegador), `116` (1 nível de arredondamento em `isolation`), `124` e
  `125` (sombra na forma e blur no backdrop). Suíte limpa com ASan/UBSan.

## 2026-10-12 — Build otimizado com `-Werror`

- O build `Release` (e `RelWithDebInfo`/`MinSizeRel`) falhava: com
  otimização o GCC analisa faixas de valores e avisava
  (`-Wformat-truncation`) que o `snprintf` de `<input type=week>` podia
  truncar. Era um defeito real: navegando o calendário além do ano 9999,
  o valor gravado saía cortado. **Orchestration**:
  `tbox_context_date_commit` recusa anos fora de 1–9999 (o formato e os
  parsers só aceitam 4 dígitos), usa buffers folgados e confere o tamanho
  escrito.
- `tests/base/test_list.c`: o vetor preenchido no laço é inicializado
  (`-Wmaybe-uninitialized` em `-O3`) e o índice é limitado.
- Todos os tipos de build (GCC e Clang) compilam limpos e passam no `ctest`.

## 2026-10-16 — Loop que dorme, buffer duplo e `tbox_app_post`

- **Application**: `tbox_app_wait(app, max_ms)` (uma volta do loop que
  dorme até evento, tarefa, prazo do contexto ou o limite do chamador),
  `tbox_app_run` (`wait(-1)` até fechar) e `tbox_app_post` (fila por
  instância com mutex, a única função thread-safe). `tbox_app_step` vira
  `tbox_app_wait(app, 0)`, com a mesma semântica de antes. Um frame sujo
  espera o compositor (`frame_ready`); depois de cada frame o `:hover` é
  reconferido. Com a janela fechada, `wait`/`step` só rodam a fila.
- **Orchestration**: `ctx->animations_active` (bool) virou
  `ctx->next_frame_time` (prazo), público em `tbox_context_next_frame_time`.
  Atrasos de `transition`/`animation` e a espera do scroll snap são prazos,
  não um frame por volta. `tbox_context_animations_active` continua (=
  prazo finito).
- **Output**: backend Wayland com dois buffers `wl_shm` e dano acumulado por
  buffer, `wl_buffer.release`, `wl_surface.frame` em todo commit, `eventfd`
  para `wake`, pipes do clipboard e prazo da repetição de tecla no `poll`.
  `present` retorna `bool` (recusa sem buffer livre). Interface de backend
  ganhou `wake` e `frame_ready`.
- **Base**: `src/base/tbox_mutex.h` (pthread); CMake liga `Threads::Threads`.
- Exemplos: `tbox_worker_post_demo` (novo); os demais usam `tbox_app_run`
  ou `tbox_app_wait`, sem `nanosleep`.
- Testes de `tbox_context_next_frame_time` (transição e animação com
  atraso, snap). Validação manual no sway: ocioso 0,06 s de CPU em 5 s;
  animação contínua a ~75 frames/s num monitor de 75 Hz, `attach` =
  `release`; resize repetido e fechamento com tarefas pendentes limpos com
  ASan/UBSan; contornos de `debug_damage` sem fantasmas entre buffers.
  Repetição de tecla e clipboard não foram exercitados (sem ferramenta de
  entrada sintética).
- Débito resolvido: "Buffer duplo no backend Wayland" e "Loop sem espera".
  Novos: integração com loops externos, callbacks para bindings.

## 2026-10-16 — Callbacks e structs prontos para bindings

- **API pública** (mudança de assinaturas, sem variantes de
  compatibilidade): novo `include/tbox/callback.h` com
  `tbox_destroy_notify`. `tbox_context_on_click`, `on_input`, `on_select`,
  `on_submit`, `tbox_app_on_key`, `tbox_app_post` e
  `tbox_font_face_cache_create` ganharam um `destroy` para o `userdata`,
  chamado uma vez quando a biblioteca deixa de usá-lo (`unbind`, troca do
  handler, tarefa concluída ou descartada, `close`/`destroy`); registro
  que falha não o chama.
- Nenhum callback recebe struct por valor: o handler de tecla recebe
  `const tbox_key_event *`, o resolver de fontes `const tbox_font_query *`,
  `on_input`/`on_select` recebem `(const char *value, size_t value_length)`.
  `tbox_context_dispatch_key` recebe ponteiro. `tbox_context_options` entra
  por ponteiro (`NULL` = padrão) em `tbox_context_open_with_options`,
  `tbox_app_create*_with_options` e `tbox_app_screenshot_from_files_with_options`.
- Saíram `tbox_context_open_with_config`, `tbox_app_create_with_config` e
  `tbox_app_create_from_files_with_config`: `tbox_context_options.ua_style`
  cobre o caso.
- **Base**: `src/base/tbox_release.{h,c}` adia o `destroy` enquanto um
  callback está em execução (handler que faz `unbind` de si mesmo ou troca
  o próprio `on_input`). **Orchestration**: toda chamada de handler passa
  por `tbox_context_emit_input/select/submit` ou pelo despacho de clique,
  que delimitam a execução.
- Testes: liberação no `unbind`, na troca, no `close`, nunca em registro
  que falhou, adiada para handler que remove a si mesmo (clique e
  `on_input`); `destroy` do resolver de fontes. Uma mutação que chama o
  `destroy` na hora é pega por dois testes. Suíte limpa com ASan/UBSan;
  demos abertos e fechados sob ASan sem vazamento.
- Débito: sai "Callbacks para bindings"; entra "ABI das structs públicas"
  (campo de tamanho/versão antes da versão estável).

## 2026-10-16 — `accent-color` no range e no progress

- Primeira fase da personalização dos controles por CSS: o
  preenchimento e o polegar do `input type=range` e a barra do `<progress>`
  passam a usar `accent-color`. Com `auto`, o azul embutido continua; o
  trilho e as cores do `<meter>` não mudam.
- Teste: range padrão, range e progress herdando `accent-color` do pai, e
  meter ignorando-a. Fixture `142`.

## 2026-10-16 — Estilos das partes dos controles

- Segunda fase da personalização dos controles: `tbox_style_resolve_tree`
  resolve os pseudo-elementos das partes (`::slider-track`, `::slider-fill`,
  `::slider-thumb`, `::picker-icon`, `::checkmark`, `::file-selector-button`,
  `::clear-icon`, `::step-control`, `::step-up`, `::step-down`,
  `::color-swatch`, `::selection`) para os elementos que as têm e guarda o
  resultado em `tbox_style.parts`. Acesso público por
  `tbox_style_part_find`. Os pintores ainda não os usam.
- Código em `src/style/tbox_style_parts.c`; `tbox_style.c` só chama
  `tbox_style_resolve_parts` ao lado de `::marker`.
- Teste: herança de `color`, `var()`, seletor de ancestral e de atributo do
  controle, raios por canto e em %, partes ausentes em elementos sem elas.
  Fixture `143` com o CSS de cada parte.

## 2026-10-16 — Quebra de linha do `<textarea>` entre palavras

- O `<textarea>` quebrava antes do caractere que transbordava, cortando
  palavras no meio ("t" / "exto"). Agora quebra depois do último espaço da
  linha, como o soft wrap dos navegadores; os espaços ficam pendurados no
  fim da linha. Uma palavra mais larga que a linha ainda é cortada.
  Cursor, seleção, rolagem e clique usam as mesmas linhas.
- Teste: quebra depois de espaços repetidos e palavra longa dividida em
  linhas sem espaço. Fixture `144`.

## 2026-10-16 — Controles pintam as partes

- Terceira fase da personalização: range, progress, meter, select,
  checkbox/radio, number, search, file, color e a seleção de texto pintam a
  partir das partes (`tbox_context_part`, `tbox_context_paint_part`). O
  pintor de caixa de `tbox_context_control_theme.c` passou a servir ao
  tema dos popups e às partes.
- Os padrões viraram regras da folha UA, que ganhou seletores com
  pseudo-classe e pseudo-elemento (`input[type=file]:disabled::file-selector-button`)
  e `border-left`. A aparência padrão não mudou (fixture `142` igual).
- `tbox_style_part_style` ganhou `color_declared`/`background_declared`:
  sem declaração, preenchimento, polegar e marca usam `accent-color`.
- A largura do polegar define o percurso do clique e do arraste; a da
  coluna do number e do X da busca, o clique e a área de texto
  (`tbox_context_input_control_rect`). O polegar pode passar da caixa.
- Testes: cada parte com cor/tamanho próprios, clique no percurso novo e
  na coluna de 24px, `::selection` com fundo e cor do texto e o padrão no
  `<textarea>`. Suíte limpa com ASan/UBSan.

## 2026-10-16 — Foco sem deslocar o controle

- O foco de `button`/`input`/`select`/`textarea` trocava a borda de 1px por
  uma de 2px: a caixa crescia, o conteúdo e os vizinhos andavam 1px. Agora
  a folha UA usa `outline: 2px solid #0000ff; outline-offset: -1px`, quase
  o mesmo desenho, sem efeito no layout. A UA ganhou `outline` e
  `outline-offset`.
- Teste: posição e tamanho de cada controle e do texto ao lado iguais com e
  sem foco. O teste do spinner do number (fase 3) esperava o fundo da
  coluna em `direita − 16` e só passava por coincidência com o
  deslocamento do foco; corrigido para `direita − 15`, dentro da borda.

## 2026-10-16 — Efeitos colaterais do anel de foco; checkbox marcado

- `input type=range` e `type=image` passaram a mostrar o anel de foco: a
  regra UA deles zerava a borda e assim anulava a antiga borda de foco, mas
  não anulava o `outline`. Agora zeram o `outline` também; o conjunto de
  controles com destaque voltou a ser o de antes.
- Um `outline` sólido acompanha o `border-radius` (anel `FILL_RING`), como
  nos navegadores: o foco do radio voltou a ser redondo.
- Marcar um checkbox o descia ~2px: a linha de texto do ✓ (U+2713) passava a
  definir a linha de base do `inline-block`. A marca é decoração; o
  checkbox mantém a linha de base na borda de baixo, marcado ou não
  (`tbox_layout_is_checkbox` em `tbox_layout_push_atomic_word`).
- Testes: range sem anel no foco; checkbox e radio parados ao marcar (a
  fonte dos testes não tem o ✓, então o caso do glifo foi conferido com as
  fontes do sistema); outline arredondado vira um anel com os raios certos.
  Fixture `145`.

## 2026-10-16 — Anel de foco cinza

- A cor padrão do anel de foco passou de azul (`#0000ff`) para `#808080`, a
  mesma da borda padrão dos controles (pedido do mantenedor). Um
  `border-color` do documento não muda o anel; `outline-color` muda.
- Teste: borda vermelha mantém o anel cinza; `outline-color` o substitui.

## 2026-10-16 — Campo de texto vazio mantém a linha de base

- Apagar o conteúdo de um `<input>` de texto (no `143`, a busca) descia o
  `number` ao lado; digitar de novo o devolvia. Sem texto, a caixa não tinha
  linha e a linha de base caía na borda de baixo, realinhando a linha toda.
  Agora um campo de texto vazio alinha pela linha que o texto ocuparia
  (topo do conteúdo + meia entrelinha + ascendente), como nos navegadores;
  isso também vale para campos que já começam vazios.
- Teste: apagar e redigitar a busca não move ela nem o `number` ao lado,
  com `line-height` normal e explícito. Fixture `145`.

## 2026-10-16 — Anel de foco em todos os controles

- Desfeita a exceção do `range` e do `type=image`: sem anel, a navegação por
  Tab não mostrava qual controle estava focado (pedido do mantenedor).
  Todo controle focado recebe `outline: 2px solid #808080`.
- Teste: o slider focado tem o anel. Fixture `145` com o slider.

## 2026-10-16 — Lista do select pelo CSS padrão

- Quarta fase da personalização: a lista aberta do `<select>` segue o
  select customizável. `::picker(select)` (pseudo-elemento funcional, novo
  no parser) é a caixa; cada linha usa o estilo do `<option>`/`<optgroup>`
  (fundo, borda, raio, cor, fonte, `padding`). Padrões na UA, que ganhou
  regras com pseudo-classe (`option:hover`). Sem `appearance: base-select`.
- `option:hover` é a opção destacada: com a lista aberta, o contexto de
  hover aponta para ela, e o ponteiro sobre a lista passa a movê-la
  (antes só as setas). `option:checked` lê `tbox_html_node.selectedness`,
  sincronizado do `tbox_select_field` antes de cada resolução de estilo.
  `:disabled` inclui opções de `optgroup` desabilitado.
- A altura das linhas é a da mais alta (fonte + `padding`); saiu o mínimo
  fixo de 24px. Com as fontes padrão, nada muda.
- Corrigido defeito antigo: com `<optgroup>`, as setas paravam no meio da
  lista, porque a navegação usava a posição na lista de linhas (que conta
  os títulos dos grupos) como índice de opção.
- Testes: lista com CSS próprio (caixa, padding, `:checked`, `:hover` pelo
  teclado e pelo ponteiro, `:disabled` no grupo), `selected` intacto após
  escolher, setas atravessando grupos. Suíte limpa com ASan/UBSan.
  Fixture `146`.

## 2026-10-16 — Partes da barra de rolagem

- Quinta fase da personalização: `::-tbox-scrollbar-track` (largura, fundo,
  borda, raio) e `::-tbox-scrollbar-thumb` (fundo, borda, raio, `margin`,
  `min-height`) em qualquer caixa com `overflow-y: auto`, com
  `::-tbox-scrollbar-thumb:hover` (ponteiro sobre o polegar) e `:active`
  (arraste). `scrollbar-width`/`scrollbar-color` seguem como padrão.
- A cascata aceita `:hover`/`:active` depois de um pseudo-elemento, com o
  estado passado em `tbox_css_cascade_resolve_pseudo_element_in_viewport`
  (parâmetro novo `state`). O polegar é resolvido nas três variantes.
- A largura da barra vem só de `tbox_style_scrollbar_size`, usada pela
  pintura, pelos testes de clique e pelo layout (`scrollbar-gutter`); saiu
  o 10/6 repetido. O polegar percorre o trilho menos as margens (`rail`).
- A folha UA aceita `*` e `min-height`; o padrão do polegar é
  `border-radius: 40%; min-height: 18px` (o mesmo desenho de antes).
- O contexto guarda a caixa cujo polegar está sob o ponteiro
  (`scroll_thumb_hover`); `tbox_context_update_hover` passa a pedir
  repintura quando ela muda.
- Testes: largura no trilho e na faixa reservada, margem e mínimo do
  polegar, variantes `:hover`/`:active`, arraste 1:1 no percurso interno.
  O ASan pegou o campo novo do contexto sem inicializar (o contexto é
  preenchido campo a campo); corrigido. Fixture `147`.

## 2026-10-16 — Scrollbar discreta por padrão

- Pedido do mantenedor: sem CSS, a barra é `thin` (6px), o trilho é
  transparente e o polegar é uma pílula cinza com 1px de recuo (4px
  visíveis). A UA ganhou `* { scrollbar-width: thin }` e o polegar
  `margin: 1px; border-radius: 50%`; o trilho transparente é a cor de
  reserva do pintor, para `scrollbar-color` ainda colori-lo.
- A primeira versão usava `border: 2px solid white` no polegar: sobravam 2px
  visíveis, o anel aparecia em fundos escuros e polegares personalizados
  sem `border` herdavam o anel. Trocado pelo recuo transparente.
- Os testes de mecânica de rolagem fixam a barra clássica por CSS
  (`scrollbar-width: auto`, `scrollbar-color`); um teste novo cobre o
  padrão discreto.

## 2026-10-16 — Aviso do build Release em `tbox_context_number_step`

- Com otimização, o GCC acusava `min`/`max` possivelmente não
  inicializados (`-Wmaybe-uninitialized`, erro por `-Werror`), embora só
  sejam lidos quando `has_min`/`has_max` são verdadeiros. Inicializados com
  0. Era o único aviso do build Release; suíte passa em Debug e Release.

## 2026-10-17 — cppcheck: banco de compilação sem duplicatas e addon `threadsafety`

- O target `cppcheck` analisava cada fonte de `src/` duas vezes (entradas
  de `tbox_static` e `tbox_shared` no `compile_commands.json`). Agora um
  script (`cmake/tbox_cppcheck_compile_db.cmake`) grava uma cópia com a
  primeira entrada de cada arquivo. Com cache vazio: CPU de 34 para 17,5 min,
  tempo de 7 para 5 min; os achados são os mesmos.
- Addon `threadsafety` ativado, exceto em `tests/` e `example/`. Achados em
  `src/`: três tabelas `static const char *[]` do calendário viraram
  `const char *const`; `readdir` em `tbox_context_file.c` foi suprimido (o
  `DIR` é local). `localtime` no calendário virou `localtime_r`, com
  `_POSIX_C_SOURCE 200809L` no topo do arquivo (como em `tbox_app.c`); o
  backend Windows vai precisar de `localtime_s`.
- `negativeIndex` em `tbox_style_animatable_length` era falso positivo, mas
  dependia de `kind_of` mandar tudo o que sobra para `KIND_LENGTH` pelo
  `default`. Agora `kind_of` lista todas as propriedades sem `default`
  (`-Wswitch` acusa uma propriedade nova sem tipo), e o acessor de
  comprimento testa a faixa `TOP..LEFT` e devolve `NULL` fora dela; leitura e
  escrita ignoram o `NULL`.

## 2026-10-17 — Relatório do cppcheck zerado

- `tests/`: novo `TBOX_TEST_REQUIRE` (encerra o grupo) nas 33 checagens de
  ponteiro de `test_tree.c`, que antes seguiam para um `NULL` desreferenciado
  com `TBOX_TEST_ASSERT`. `constVariablePointer` e `redundantAssignment`
  suprimidos em `tests/` no `tbox.cppcheck.in`.
- `src/`: `const` em variáveis locais e parâmetros (incluindo
  `tbox_backend_wayland_wake` e helpers internos de popup/tema/calendário).
  `tbox_context_document` continua recebendo `tbox_context *`: devolve o
  documento mutável.
- Limpezas apontadas: condição sempre verdadeira (`node != NULL` na
  cascata, `!word->hard_break` no sizing), atribuição descartada em
  `tbox_app.c`, variáveis sombreadas renomeadas, ramo redundante na
  interpolação de gradiente do raster (comportamento igual).
- Falsos positivos suprimidos com motivo: normalização de `-0.0` e laço de
  sufixo no damage tracking, `uninitvar` nos `memcpy` de transições e
  animações, `unreadVariable` em `test_raster.c`.
- `cmake --build build --target cppcheck` sai com 0; pode virar verificação
  de CI.

## 2026-10-18 — `border-image`

- `border-image` e os cinco longhands (Backgrounds 3 §6): nove fatias de
  `url()` ou gradiente, `fill`, `border-image-width`/`-outset` com
  comprimento, percentual ou número, os quatro modos de `border-image-repeat`.
  Fixture 148.
- `tbox_paint_op` ganhou `source_rect`: em `IMAGE`, o recorte da imagem; em
  `GRADIENT`, a caixa em que o gradiente é disposto, esticada até o `rect`.

## 2026-10-18 — Recursos OpenType

- `font-feature-settings`, `font-variant-ligatures`, `-numeric`,
  `-east-asian`, `-alternates` (`historical-forms`) e
  `font-language-override` viram features/idioma do HarfBuzz; herdáveis.
  `font-variant` ajusta esses longhands e `font` os reseta. Fixture 150, com
  uma fonte própria (`150-sans.ttf`) para Firefox e tbox usarem os mesmos
  glifos.
- `@font-palette-values` e `font-palette` passam pelo parser e pela cascata,
  sem efeito no desenho enquanto não houver glifos coloridos.

## 2026-10-18 — `@scope`, `@property`, `@counter-style`

- `@scope (<raiz>) [to (<limite>)]`, raiz implícita num `<style>`, `:scope`
  e proximidade de escopo na cascata (depois da especificidade, antes da
  ordem).
- `@property` registra custom properties tipadas (`<length>`, `<color>`,
  `<number>`, `<percentage>`, `<length-percentage>`, `<angle>`,
  `<integer>`, `*`): `inherits`, `initial-value` e valor inválido caindo no
  inicial.
- `@counter-style` com os sete sistemas e todos os descritores, usado por
  `list-style-type`, `counter()` e `counters()`; `list-style-type` também
  aceita `<string>` e `symbols()`. No fallback, o marcador mantém
  `prefix`/`suffix` do estilo nomeado, como no Firefox. Fixture 153.

## 2026-10-18 — Transformações além de translação

- `transform` com `rotate()`, `scale*()`, `skew*()`, `matrix()` e listas
  compostas (até 8 funções); propriedades individuais `rotate`, `scale` e
  `translate` (ordem translate → rotate → scale → transform);
  `transform-origin`. Funções 3D multiplicadas em 4x4 e achatadas por
  projeção ortográfica. Fixture 149.
- Caixa transformada pintada numa camada composta pela inversa da matriz
  (bilinear); translação pura continua no layout, sem camada. Damage pelo
  bounding box transformado; hit test pela inversa.
- Qualquer transformação cria contexto de empilhamento e bloco de contenção
  de `absolute`/`fixed`. `@supports` aceita rotação/escala/skew/matriz (o
  fixture 123 foi ajustado).
- API: `tbox_style_animatable_*` usam arrays de `TBOX_STYLE_ANIM_VALUE_SIZE`
  (8); `tbox_style_animatable_mix` recebe a propriedade; novos
  `tbox_style_has_transform`/`tbox_style_transform_matrix`.

## 2026-10-18 — CSS Grid

- `display: grid`/`inline-grid`, `grid-template-*`, `grid-auto-*`,
  `grid-area`/`-row`/`-column`, `grid`/`grid-template`, `minmax()`,
  `repeat()` (inclusive `auto-fill`/`auto-fit`), `fit-content()`, nomes de
  linha e áreas, `dense`, `justify-items`/`justify-self` e alinhamentos
  existentes aplicados a grid. Algoritmo em `src/layout/tbox_layout_grid.c`
  (CSS Grid 1 §11, spans aproximados). `@supports (display: grid)` passa a
  ser verdadeiro (fixture 123 ajustado). Fixtures 151 e 152.

## 2026-10-18 — Floats

- `float` (CSS 2.1 §9.5, com os lógicos `inline-start`/`inline-end`),
  `clear` com clearance, blocos que criam BFC (`flow-root`, `overflow`,
  flex, tabelas, inline-block, imagens, controles) contendo e evitando
  floats; `float` vira `none` em itens flex/grid e em absolutos.
- `shape-outside` (formas básicas, caixas e `url()` com
  `shape-image-threshold`) e `shape-margin`. Fixture 154; fixture 123
  ajustado (`float` passa a ser suportado em `@supports`).

## 2026-10-18 — Container queries

- `@container` (tamanho, faixas, `and`/`or`/`not`, `style()` com custom
  properties), `container-type`, `container-name`, `container` e unidades
  `cqw`/`cqh`/`cqi`/`cqb`/`cqmin`/`cqmax`; contenção de tamanho. Fixture 156.
- `tbox_context_run_frame` faz uma segunda passada de estilo quando há
  contêineres. Assinaturas: as três `tbox_css_cascade_resolve_*_in_viewport`
  recebem um `tbox_css_query_context`; `tbox_style_units` ganhou
  `container_inline`/`container_block` (-1 sem contêiner).

## 2026-10-18 — `zoom` e `resize`

- `zoom` padronizado: fator efetivo (produto com os ancestrais) em
  `tbox_style_units.zoom`, multiplicando as unidades absolutas; imagens
  naturais escaladas no layout; `rem` = raiz sem zoom × fator.
- `resize` com alça arrastável (`src/context/tbox_context_resize.c`, estado
  por nó no contexto aplicado sobre o estilo a cada frame), parte
  `::-tbox-resizer`, cursores `nwse-`/`nesw-resize`. Fixture 158.

## 2026-10-18 — Multicoluna

- `columns`, `column-count`, `column-width`, `column-fill`, `column-span`
  (filhos diretos), `column-rule*`; fragmentação de blocos e linhas entre
  colunas, margens truncadas nas quebras (CSS Fragmentation §5.2),
  marcadores de lista acompanhando a linha; `break-before`/`-after:
  column` e `break-inside: avoid` passam a ter efeito em multicoluna.
  Fixture 155.

## 2026-10-18 — Texto bidirecional

- UAX #9 próprio (`src/base/tbox_bidi.c`, tabelas geradas do UCD 16; passa
  nos 91 707 casos do `BidiCharacterTest.txt`), níveis por parágrafo antes
  da quebra e reordenação por linha depois (`src/layout/tbox_layout_bidi.c`);
  linhas só LTR não passam pelo algoritmo.
- `unicode-bidi` completo; `<bdo>`, `<bdi>` e `dir` dão os valores padrão;
  caret visual no `<input>`. `tbox_font_shape` ganhou `tbox_font_direction`
  (espelhamento L4 no shaper). `all` não reseta mais `direction`/
  `unicode-bidi`; caixas anônimas herdam `direction`. Fixture 159; os
  fixtures 012 (`<bdo>`) e 130 passaram a bater com o Firefox.

## 2026-10-18 — Contorno de texto e propriedades só de SVG/impressão

- `-webkit-text-stroke` (+ `-width`/`-color`) e `paint-order` no texto
  HTML; `marker*` e `mask-type` aceitos sem efeito (falsos em `@supports`);
  `@page` lido e descartado com seletores e at-rules de margem. Fixture 163.

## 2026-10-18 — Motion path

- `offset-path` (`path()` com dados SVG, formas básicas com `<coord-box>`,
  `ray()`), `offset-distance`, `offset-position`, `offset-anchor`,
  `offset-rotate` e `offset`, compostos na matriz afim depois de
  `translate`/`rotate`/`scale` e antes de `transform`. Fixture 161.
  `tbox_style_transform_matrix` ganhou o parâmetro `offset`.
- Na integração, o conjunto de propriedades animáveis chegou a 64 (o
  limite da máscara de 64 bits): `paint-order` saiu dele (é discreta e uma
  transição comum já a troca de uma vez).

## 2026-10-18 — Máscaras em camadas, reflexo e `box-decoration-break`

- `mask` com até quatro camadas (geometria e modo por camada) e
  `mask-composite` (`add`/`subtract`/`intersect`/`exclude` e os aliases
  `-webkit-` `source-over`/`source-out`/`source-in`/`xor`).
- `box-reflect`/`-webkit-box-reflect` (quatro direções, deslocamento,
  máscara de imagem ou gradiente; amplia o damage).
- `box-decoration-break: slice | clone` em inline partido, também em linhas
  bidi e ao lado de floats. Fixture 157.

## 2026-10-18 — Capitulares e pontuação pendurada

- Pseudo-elemento `::first-letter` (primeira letra tipográfica com a
  pontuação que a precede); `initial-letter` (tamanho por N linhas,
  alinhado à cap-height, com `sink`), desviado pelo gerenciador de floats;
  `hanging-punctuation` (`first`, `last`, `force-end`, `allow-end`).
  `tbox_font_face_cap_height` novo. Fixture 162.

## 2026-10-18 — Escrita vertical

- `writing-mode` (os cinco valores e os legados), `text-orientation`
  (`mixed`, `upright`, `sideways`) e `text-combine-upright: all`. A
  subárvore vertical é diagramada como horizontal num referencial
  (`tbox_style_frame`, `src/layout/tbox_layout_writing.c`) e levada à tela
  por um filho anônimo com `has_transform`; os glifos são orientados por uma
  variante vertical da face e o raster os amostra pela inversa. Propriedades
  lógicas, `inline-size`/`block-size` e `direction` seguem o modo. Fixture
  160; os fixtures 001–163 saem idênticos byte a byte.
- Folha UA: `p`, `h1`–`h6`, `ul`/`ol` usam `margin-block` e
  `padding-inline-start`, para seguir `writing-mode` e `direction` (em `rtl`
  o recuo de lista vai para a direita, como no navegador).
- API: `tbox_glyph` ganhou `orient`/`scale`; `tbox_font_shaping_options`,
  `tbox_style` e `tbox_style_table` (`fallback`) ganharam campos;
  `tbox_style_units` ganhou `frame`.

## 2026-10-19 — Correções de paridade com o Firefox

Defeitos apontados pela comparação de todos os fixtures com o Firefox
(relatório em `docs/referencia/paridade-firefox.md`).

- `ex` e `ch` medidos na fonte do elemento (`tbox_style_font_metrics`,
  `tbox_style.ex_ratio`/`ch_ratio`); antes, 0,5em fixo. Fixture 065.
- Folha UA:
  - listas aninhadas sem margem de bloco, com `circle`/`square`;
  - `blockquote`/`figure` com recuo de 40px e `pre` com margem;
  - `address` em itálico, `abbr[title]` pontilhado e aspas em `q`;
  - cor de link só em `a:any-link`;
  - seletores com atributo e ancestrais;
  - `body` em `em`.

  Fixtures 007, 012, 054 e 118.
- `monospace` com `medium` de 13px para tamanhos derivados de palavra-chave
  (`tbox_style.font_size_keyword`). Fixture 007.
- Parser HTML descarta a quebra de linha logo após `<pre>`, `<listing>` e
  `<textarea>`. Fixture 007.
- `text-decoration` se propaga a descendentes e ao texto anônimo
  (`tbox_style.decoration_propagated`, `tbox_style_propagate_decoration`,
  `tbox_style_decoration_lines`). Fixtures 023 e 054.
- `font-family` guarda a lista inteira, e o cache de faces tenta cada nome
  (`TBOX_FONT_FAMILY_CAPACITY`, falha lembrada por família). O Fontconfig
  recusa substitutos fracos, e a família vazia é `serif` também no itálico.
  Fixtures 010, 011, 019 e 038.
- Quebra de linha depois de `-` entre letras e depois de `/` (não antes de
  dígito nem em `//`), também no `min-content`
  (`tbox_layout_word.break_before`). Fixtures 033, 086, 103, 116 e 122.
- Tabelas:
  - layout automático por min/max-content com colunas `%` e fixas;
  - `width: auto` encolhe ao conteúdo;
  - linha `visibility: collapse` dimensiona as colunas;
  - folha UA com `border-spacing: 2px` e `padding: 1px` em `td`/`th`.

  Fixtures 025, 026, 027 e 072.
- `text-overflow`: a reticência ou string vem colada ao texto cortado, e não
  na borda da caixa. Fixtures 068 e 092.
- Ampliação de imagem com filtro triangular (bilinear) em vez de cúbico,
  sem overshoot em imagens pequenas. Fixtures 045 e 052.
- `absolute`/`fixed` com `width: auto` e sem `left` e `right` ao mesmo tempo
  é shrink-to-fit (CSS 2.1 §10.3.7), limitado ao espaço depois do
  deslocamento; antes ocupava todo o bloco de contenção. Fixture 083.
- Bordas como no Firefox: 3D com a cor e a cor × 2/3 e cantos em diagonal,
  `dashed` com traço e intervalo de 3× e meio traço junto aos cantos,
  tracejado arredondado carimbado com quadrados. Fixtures 076, 088, 090 e
  099.
- `<img>` inline com padding, borda e margem: ocupam espaço na linha, fundo
  e borda são pintados, e a imagem (com `object-fit`/`object-position`)
  fica no content box. Fixture 103.
- `<fieldset>`/`<legend>` com a geometria dos navegadores: a legenda no topo,
  a borda pelo meio dela e o conteúdo logo abaixo, sem o vão de antes.
  Folha UA alinhada ao Firefox. Fixture 109.
- `text-emphasis` herdado chega ao texto anônimo do bloco (os campos faltavam
  na cópia do estilo anônimo). Fixture 131.

## 2026-10-19 — Referências capturadas no Firefox

- **Fixtures:** os PNGs de referência (`tests/assets/NNN.png`, `47.png`, `a01.png`) foram apagados, e todos os textos dos fixtures passaram para o inglês. Também foram traduzidos os comentários e as folhas em `tests/assets/*.css`. Identificadores de CSS e atributos `lang` ficaram como estavam.
- **Referência:** o `tbox_cmp` captura cada fixture no Firefox headless a cada execução e renderiza no mesmo tamanho (800x600 por padrão). As três imagens (`NNN_firefox.png`, `NNN_render.png`, `NNN_diff.png`) ficam no diretório de saída.
- **Opções novas:** `--size`, `--output`, `--firefox` (ou `TBOX_CMP_FIREFOX`) e filtro por fixture. O argumento de limiar de SSIM, já obsoleto, saiu.


## 2026-10-19 — Fixtures reorganizados por categoria

- **Nomes:** os 164 fixtures numerados (`001.html`…`163.html`, `a01.html`)
  viraram 114 arquivos `<categoria>-<tema>.html` (html, colors, box, text,
  lists, generated, selectors, cascade, units, backgrounds, layout, effects,
  animation, interaction, forms, images). Cada propriedade ou elemento fica
  num só fixture; temas grandes se dividem com o mesmo prefixo
  (`text-shaping-latin`/`-scripts`, `backgrounds-layers`/`-blend-mode`). A
  tabela antigo → novo está em [fixtures](../referencia/fixtures.md).
- **Tamanho:** todos cabem em 800x600, inclusive os que antes passavam
  (`forms-accent-and-caret`, `forms-control-parts`,
  `forms-textarea-wrapping`, `cascade-layers`).
- **Recursos auxiliares** renomeados com o fixture: `110.css` →
  `cascade-link.css` (agora restrito a `.linked`), `117.css`/`117-import/` →
  `cascade-import.css`/`cascade-import/`, `119-reset.css` →
  `cascade-layers-reset.css`, `base-demo/112.css` → `base-demo/base.css`,
  `150-sans.ttf` → `opentype-sans.ttf`. Os testes de `test_context.c`,
  `test_font.c` e `test_layout.c` que os carregam foram ajustados.
- Classes e variáveis em português dentro dos fixtures passaram para o
  inglês, e `lang="pt-BR"` virou `lang="en"` (menos em
  `selectors-attributes-and-lang`, que testa `:lang()`).

## 2026-10-19 — Nova medição de paridade com o Firefox

- **Medição:** os 114 fixtures reorganizados foram capturados no Firefox
  158 e comparados com a tbox. O `tbox_cmp` deu 46 EQUAL e 68 DIFFERENT; a
  revisão lado a lado deu nota média 9,2. O resultado, por fixture e por
  causa, está em [paridade](../referencia/paridade-firefox.md).
- **Defeitos novos** registrados em [débito técnico](debito-tecnico.md):
  floats em item de grid, borda por linha no bloco partido entre colunas,
  fundo de inline com `line-height` grande, `vertical-align: middle` em
  células, conteúdo do `<button>` de tamanho fixo, altura de linha com
  marcador ampliado ou `vertical-align` deslocado, e `font-size-adjust`.
- **Fixtures:** `layout-floats` e `layout-multicol` passaram a usar grid,
  porque o espaço entre `inline-block` empilhava as células no Firefox;
  `layout-flexbox` foi ajustado para caber em 800×600.

## 2026-10-19 — Revalidação da paridade após reorganização

- **Execução atual:** os 114 HTML foram novamente capturados no Firefox
  158.0 e renderizados pelo build Debug com HarfBuzz em 800×600: 46 EQUAL,
  68 DIFFERENT e nenhuma falha operacional. Os 25 grupos do CTest passaram.
- **Relatório:** [paridade](../referencia/paridade-firefox.md) reescrita com
  ambiente, métricas por fixture, falhas locais, revisão dos 114 pares e
  limites da captura estática. EQUAL não implica ausência de defeitos,
  como mostram as bordas da multicoluna; SSIM não decide o veredito.
- **Textarea:** prova temporária com fonte e caixa igualadas confirmou a
  diferença de quebra de URL, já registrada como débito. Os fixtures e o
  código de renderização/comparação não foram alterados nesta revalidação.

## 2026-10-19 — Catálogo do template file-explorer

- **Análise:** `example/file-explorer/index.html` e `styles.css`
  inventariados sem alterar o template: 39 nomes de elementos, 62
  propriedades CSS, 14 custom properties e 100 seletores distintos.
  As lacunas de SVG, metadados, 7 propriedades CSS, foco, placeholder e
  atributos semânticos estão no
  [catálogo](../referencia/file-explorer-compatibilidade.md).
- **Verificação:** consultas à biblioteca compilada, prova de Tab e
  renderização headless em 1440×900 confirmaram as lacunas principais.
  O texto `.visually-hidden` visível no cabeçalho foi registrado no
  débito técnico; uma folha adicional temporária com `display: block`
  suprimiu esse texto. Nenhum suporte novo ou lógica do explorador foi
  implementado nesta etapa.

## 2026-10-20 — Custo do estilo no hover do file-explorer

- **Medição:** hover com `transition` em `example/file-explorer` custava
  ~90 ms por frame em Release (~200 ms em Debug), 81% em estilo; layout
  ~5%, raster ~1 ms. Os gargalos eram `font: inherit` em `button, input`
  (`tbox_style_keywords_apply` refazia a busca linear de propriedade para
  cada um dos 17 campos de `font` × cada declaração), `@media` re-lido do
  texto para cada nó e pseudo-elemento antes do seletor, e
  `tbox_string_view_from_cstr` medindo o tamanho byte a byte.
- **Mudança:** uma busca de propriedade por declaração em
  `tbox_style_fields_overridden`; a cascata só avalia `@media` depois que
  um seletor da regra casa; `tbox_string_view_from_cstr` usa `strlen`.
  Resultado: ~47 ms (Release) e ~69 ms (Debug) por frame. 118 screenshots
  idênticos byte a byte antes/depois. Nenhum estado global novo.
- **Segunda rodada:** `tbox_css_computed_style` ganhou um filtro privado de
  256 bits (tamanho + primeira e última letra da propriedade), montado por
  `tbox_css_computed_style_index` (`src/css_cascade/tbox_css_cascade_internal.h`)
  em todo lugar que preenche um; `tbox_css_computed_style_find` rejeita sem
  varrer as propriedades ausentes, que são a maioria das ~385 consultas por
  elemento. Cada `tbox_css_stylesheet` guarda os índices das regras com
  pseudo-elemento; a cascata de `::before`/`::after` percorre só essas e
  retorna sem alocar quando nenhuma folha tem uma. Medido A/B em Release:
  ~48,7 → ~44,1 ms por frame (resoluções de cascata por frame ~1440 →
  ~1080). Screenshots idênticos; testes novos em `test_cascade.c`.
- **Restante:** casar seletores (~570 por elemento, sem indexar regras por
  id/classe/tag) ~20% do frame, layout ~20%, `find` ~12% (pelo número de
  chamadas); o passo maior segue o
  [estilo incremental](debito-tecnico.md#estilo-e-layout-incrementais).

## 2026-10-20 — Trabalho redundante no estilo (fase A de enxugar `tbox_style`)

- **Medição (callgrind):** `sizeof(tbox_style)` é 39 464 bytes, e copiar e
  zerar esse struct dava ~35% das instruções do frame. Os nós com `var()`
  calculavam o estilo completo três vezes; os com palavra-chave CSS-wide,
  duas.
- **Mudança:** `tbox_style_resolve_opentype` não zera mais um `tbox_style`
  inteiro como rascunho do `font-variant`; sem `@property`, a substituição
  de `var()` não depende das unidades do elemento e dispensa os dois
  estilos preliminares (`tbox_style_vars_registered_present`); com
  `@property`, o estilo calculado para conferir o `font-size` é reaproveitado
  quando nada muda depois dele; o estilo "nada declarado", base de
  `inherit`/`initial`/`unset`, é calculado uma vez por resolução da árvore;
  `resolve_declared`/`_keywords`/`_full` escrevem num `tbox_style *` em vez
  de devolver por valor (o GCC copiava o struct em cada `return`).
- **Resultado:** `tbox_context_run_frame` no hover do file-explorer caiu de
  10,5 G para 6,4 G instruções (−39%); ~44 → ~34,6 ms por frame em Release,
  ~64 → ~50 ms em Debug. Screenshots idênticos byte a byte. O layout virou
  metade do frame; a fase B (grupos raros fora do struct) segue.

## 2026-10-20 — Cache de runs moldados na face

- **Medição:** depois da fase A, o shaping do HarfBuzz (com o hinting do
  FreeType nos avanços) era ~38% do frame de transição do file-explorer:
  o layout molda as mesmas palavras de novo a cada frame, e
  `tbox_font_measure_text_spaced` (só largura) era a maior parte.
- **Mudança:** cada `tbox_font_face` guarda os runs já moldados por
  (texto, `letter-spacing`, direção), em `src/font/tbox_font_face.c`; ver
  [Texto e fontes](../09-texto-e-fontes.md#cache-de-runs) e
  [Decisões](decisoes.md#texto-e-fontes). Testes em `tests/font/test_font.c`
  (glifos depois de só medir, troca de kerning, texto longo, esvaziamento).
- **Resultado:** `tbox_context_run_frame` 6,4 G → 4,0 G instruções; o layout
  de 3,3 G para 0,85 G; ~34,6 → ~22 ms por frame em Release. Screenshots
  idênticos byte a byte. Agora o estilo é ~75% do frame.

## 2026-10-20 — Grupos fora de linha em `tbox_style` (fase B)

- **Mudança:** nove grupos grandes e raros saíram do struct para ponteiros
  `const` na arena da resolução (ver [Decisões](decisoes.md#css-e-estilo) e
  [Guia da API](../03-guia-da-api.md#grupos-fora-de-linha-de-tbox_style)):
  `sizeof(tbox_style)` 39 464 → 9 672 bytes. `tbox_style_resolve` e
  `_in_viewport` ganharam o parâmetro de arena; `tbox_style.mask` saiu. O
  grid e o motion path deixaram de resolver num `tbox_style` temporário
  inteiro por declaração.
- **Resultado:** `tbox_context_run_frame` 4,0 G → 3,15 G instruções; cópia +
  zeragem de 1,33 G → 0,49 G; ~22 → ~16 ms por frame em Release (~60 fps),
  ~32 ms em Debug. Screenshots idênticos byte a byte; testes ajustados aos
  acessores. Os arquivos de teste que resolvem estilos guardam os grupos
  numa arena própria do arquivo.

## 2026-10-21 — Registros de controles na API app

- `tbox_context_on_click/input/submit/select` e o unbind de clique deixam
  a API pública; os equivalentes `tbox_app_*` recebem o app, assim como os
  novos tipos de callback. O contexto é resolvido internamente.
- Despacho e armazenamento por documento continuam privados no contexto,
  com liberação adiada de userdata em callbacks e limpeza no reload.
- Exemplos e documentação migrados; testes exercitam eventos de controles
  pelo loop real com backend simulado, além da suíte do contexto.

## 2026-10-21 — Arquivo próprio para eventos da aplicação

- Registros `tbox_app_on_*`, unbind de clique, adaptadores, notificações e
  liberação dos handlers extraídos para `src/app/tbox_app_events.c`.
- Helpers entre loop e eventos declarados apenas em `tbox_app_internal.h`;
  API pública e comportamento preservados.
- Teste de integração compila ambos os arquivos de produção; builds sem
  Wayland/Fontconfig mantêm os stubs correspondentes.

## 2026-10-23 — `<button>` envia e reseta o form

- `<button>` sem `type`, com `type=submit` ou com valor desconhecido envia o
  `<form>` ancestral (clique ou Enter/Espaço), com o botão como `submitter`;
  `type=reset` restaura o form; `type=button` só dispara clique, como no HTML.
- No clique, envio e reset agora partem do controle focável atingido, não da
  caixa sob o ponteiro: clicar no conteúdo do botão funciona, e um controle
  que não pode receber foco (desativado, oculto) não envia nem reseta.

## 2026-10-23 — `fieldset disabled` e atributo `form`

- `<fieldset disabled>` desativa os controles descendentes (exceto dentro do
  primeiro `<legend>`), como no HTML. A regra "está desativado" ficou numa
  função só, `tbox_html_node_disabled`, usada por `:disabled`/`:enabled`,
  foco, clique, popups, pintura do `input type=file` e
  `tbox_control_disabled`.
- Clicar num controle desativado (ou no conteúdo dele) não dispara mais
  `on_click`; antes isso só valia para `<input>`.
- O form de um controle passou a ser o "form owner" do HTML: o atributo
  `form="id"` liga o controle a um `<form>` em qualquer lugar do documento,
  para envio, reset e grupos de radio. `<option>` segue o seu `<select>`.

## 2026-10-23 — Aparência de controles desativados

- Regras `:disabled` na folha UA (`tbox_ua_style.c`) para `input`,
  `button`, `select` e `textarea`: texto `#6d6d6d`, borda `#dcdde1`, fundo
  branco a 50% onde havia branco, e partes do range em cinza. Cores medidas
  na captura do Firefox. Valem também dentro de `<fieldset disabled>`.
- A folha UA ganhou `border-color` e cores com alfa.
- Fixture novo `forms-disabled`.

## 2026-10-23 — Alinhamento de `legend` e `input type=color`

- `<legend>` passou de `inline-block` a `display: block` na folha UA (o
  valor calculado do Firefox); o legend renderizado com largura automática
  encolhe até o conteúdo. Antes, conteúdo inline depois dele (sem `<p>`)
  subia para a linha do legend.
- `input type=color` não tem texto, então se alinhava pela borda de baixo e
  ficava mais alto que o `date` ao lado. Agora a linha de base é a de uma
  linha de texto centralizada na área de conteúdo, como num botão.

## 2026-10-24 — API de tabelas

- `controls.h` ganhou as `tbox_table_*`: contar e pegar linhas e células,
  inserir/remover linha e célula, esvaziar o corpo, ler/trocar o texto de
  uma célula, achar `<thead>`/`<tfoot>` e ir de um nó qualquer (o de um
  `on_click`) para a sua linha, célula e índices. Sem estado no contexto;
  implementação em `tbox_context_controls.c`. Exemplo
  `example/table_demo.c`.
