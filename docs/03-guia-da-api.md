# 3. Guia da API

> - **Escopo:** as funções públicas agrupadas por tarefa, com os contratos de posse, retorno e erro que importam para quem usa.
> - **Código relacionado:** `include/tbox/*.h` (a fonte da verdade: cada função tem comentário detalhado no header)
> - **Veja também:** [Exemplos](04-exemplos.md), [Eventos e interatividade](06-eventos-e-interatividade.md), [Memória e concorrência](13-memoria-e-concorrencia.md)

Inclua tudo com:

```c
#include <tbox/tbox.h>
```

Este capítulo é um mapa. A descrição completa de cada função (casos de
borda, NULL, vida dos ponteiros) está no comentário dela no header.

## Convenções que valem para toda a API

- **Strings de entrada** são `const char *` + tamanho ou `tbox_string_view`
  (`tbox_string_view_make(ptr, len)`); não precisam terminar em NUL, salvo
  quando a função diz "NUL-terminated" (`tbox_app_create`).
- **A biblioteca copia o que precisa.** Buffers passados a parsers e a
  funções de mutação não precisam sobreviver à chamada.
- **`NULL` no handle principal é no-op** (ou retorna `false`/`NULL`).
- **Conjuntos de resultado** (`tbox_css_selector_node_set`,
  `tbox_xpath_node_set`) têm `items`/`count` e precisam do seu `_destroy`.
- **Views retornadas** (`tbox_string_view`) são emprestadas; o header diz até
  quando valem (tipicamente até o próximo frame ou a próxima edição).
- **Uma thread de UI.** Toda a API roda na thread que criou o `tbox_app`
  (no macOS, será obrigatoriamente a principal). A única função que outras
  threads podem chamar é `tbox_app_post`. Ver
  [Memória e concorrência](13-memoria-e-concorrencia.md#threads).

A estrutura `tbox_style_clip_path` preserva os raios de `round` em pixels
(`round_h`/`round_v`) e percentuais (`round_h_percent`/`round_v_percent`).
Inicialize a estrutura inteira com zero antes de preencher campos diretamente.
A adição dos campos percentuais altera o tamanho de `tbox_style_clip_path` e
`tbox_style`: consumidores binários devem ser recompilados com o novo header.

`tbox_style_gradient.radius[2]` preserva os raios explícitos de gradientes
radiais. Inicializados com zero, ficam `AUTO`, mantendo o tamanho por extensão.
Círculos usam `radius[0]`; elipses usam os dois eixos. Esse acréscimo altera o
tamanho de `tbox_style_gradient` e das estruturas que o contêm, incluindo
`tbox_style`; consumidores binários devem ser recompilados.

`tbox_style_clip_path` agora guarda até 32 pares em `points`, `point_count`
e `evenodd` para `polygon()`. `tbox_paint_op` recebe `polygon_clip` (ponteiro
emprestado para `tbox_paint_polygon` na arena de frame) e origem local separada.
Esses campos alteram o tamanho das estruturas públicas: recompile consumidores.
O tipo `TBOX_STYLE_GRADIENT_CONIC` reutiliza `angle`/`center`; posições explícitas
dos stops são percentuais de uma volta. Inicialize as estruturas com zero.

`tbox_style.parts` guarda ponteiros emprestados para os estilos das partes dos
controles (`tbox_style_part_style`, na mesma arena da `tbox_style_table`);
leia com `tbox_style_part_find`. `color_declared`/`background_declared`
dizem se a folha declarou a cor (senão vale a cor embutida do controle). O
campo altera o tamanho de `tbox_style`: recompile consumidores.

`tbox_html_node.element.focus_visible` é estado de execução mantido pelo
contexto, não um atributo HTML; seletores o consultam junto do nó focado.

### Grupos fora de linha de `tbox_style`

Grupos de propriedades grandes e raros não ficam mais dentro de
`tbox_style` (que caiu de ~39 KB para ~4 KB): ficam na arena em que o
estilo foi resolvido, atrás de um ponteiro `const` que é `NULL` enquanto
nenhuma propriedade do grupo foi declarada. Leia sempre pelo acessor, que
devolve os valores iniciais do grupo para `NULL`:

| Campo | Acessor |
|---|---|
| `masks` (camadas de `mask-*`) | `tbox_style_masks_of` |
| `background_layers` (2ª camada de fundo em diante) | `tbox_style_background_layers_of` |
| `grid_templates` (texto de `grid-template-*`/`grid-auto-*`) | `tbox_style_grid_templates_of` |
| `clip_path` | `tbox_style_clip_path_of` |
| `shape_outside` | `tbox_style_shape_outside_of` |
| `offset_path` | `tbox_style_offset_path_of` |
| `filters`, `backdrop_filters` | `tbox_style_filters_of`, `tbox_style_backdrop_filters_of` |
| `border_image` | `tbox_style_border_image_of` |
| `box_reflect` | `tbox_style_box_reflect_of` |
| `animation_lists` (listas de `transition` e `animation`) | `tbox_style_animation_lists_of` |
| `markers` (`list-style-image`, `list-style-type` personalizado, `marker-*` do SVG; herdado) | `tbox_style_markers_of` |
| `background_source` (imagem ou gradiente da 1ª camada de fundo) | `tbox_style_background_source_of` |
| `transforms` (`transform`, `offset-position`, `offset-anchor`) | `tbox_style_transforms_of` |
| `font_settings` (`font-feature-settings`, `font-palette`; herdado) | `tbox_style_font_settings_of` |
| `scroll_spacing` (`scroll-margin`, `scroll-padding`) | `tbox_style_scroll_spacing_of` |
| `grid_placement` (`grid-row-*`, `grid-column-*`) | `tbox_style_grid_placement_of` |
| `marker` (sobreposições de `::marker`; `NULL` = nenhuma, sem acessor) | — |

- **Nunca escreva através do ponteiro:** estilos compartilham grupos (herança,
  cópias do layout); a camada de estilo copia o grupo antes de mudá-lo. Para
  montar um estilo à mão, preencha um grupo seu e aponte o campo para ele.
  Um grupo herdado (`markers`, `font_settings`, `svg_paint`) é o mesmo
  ponteiro do pai enquanto o elemento não muda nenhum valor dele.
- `tbox_style_animatable_write` recebe uma arena: escrever um slot de
  `transform` ou `offset-position`/`offset-anchor` copia o grupo nela.
- **Vida:** a da arena da resolução — a `frame_arena` do contexto para a
  tabela do frame. `tbox_style_resolve` e `tbox_style_resolve_in_viewport`
  passaram a receber essa arena como primeiro parâmetro.
- O antigo `tbox_style.mask` (cópia da primeira camada) saiu: use
  `tbox_style_masks_of(style)->layers[0].source`.
- Saíram de `tbox_style` (2026-10-27): `transitions`/`transition_count`,
  `animations`/`animation_count`, `list_style_image`, `list_style_custom`,
  `marker_urls`, `background_image`, `background_gradient` (agora
  `image`/`gradient` de `background_source`), `transforms` (agora
  `functions`), `offset_position`, `offset_anchor`,
  `font_feature_settings`, `font_palette`, `scroll_margin`,
  `scroll_padding`, `grid_units` (agora em `grid_templates`),
  `grid_row_*`/`grid_column_*` e os campos `marker_*` de `::marker`.

`tbox_style.placeholder_style` é um ponteiro emprestado para o estilo de
texto de `::placeholder`, na arena do frame, ou NULL quando não há texto
auxiliar aplicável. Sua adição altera `tbox_style`: recompile consumidores.

`tbox_css_cascade_resolve_pseudo_element_in_viewport` recebe `state`
(`tbox_css_part_state`: `HOVER`, `ACTIVE`), para seletores como
`::-tbox-scrollbar-thumb:hover`; passe `0` para um pseudo-elemento sem
estado. `tbox_style_scrollbar_size` dá a largura da barra de rolagem de um
estilo (parte do trilho ou `scrollbar-width`). `tbox_style_part_style`
ganhou `margin` e `min_height`.

`tbox_html_node.element.selectedness` diz se um `<option>` é a escolha atual
do seu `<select>`; o `tbox_context` o atualiza antes de cada resolução de
estilo. Não confunda com o atributo `selected` (a escolha inicial). O campo
altera o tamanho de `tbox_html_node`: recompile consumidores.

## 1. Aplicação com janela — `app.h`

| Função | Uso |
|---|---|
| `tbox_app_backend_available()` | `true` se este build consegue abrir janela. |
| `tbox_app_create(html, css, w, h)` | Abre janela a partir de strings. `NULL` em falha ou sem backend. |
| `tbox_app_create_from_files(html_path, css_path, w, h)` | Igual, lendo arquivos. `css_path` pode ser `NULL`. Imagens e `@font-face` relativos usam o diretório do HTML. |
| `…_with_options(…, const tbox_context_options *)` | Config UA (`ua_style`: tamanho base, escala de headings, margens) + tema dos popups + diretórios de recursos + `prefers_reduced_motion`. `NULL` = padrão. |
| `tbox_app_run(app)` | O loop pronto: dorme até haver evento, tarefa ou frame de animação; volta quando a janela fecha. |
| `tbox_app_wait(app, max_ms)` | Uma volta do loop esperando no máximo `max_ms` (`-1`: sem limite). Para quem tem loop próprio: passe o seu próximo prazo. |
| `tbox_app_step(app)` | `tbox_app_wait(app, 0)`: uma volta que nunca bloqueia. |
| `tbox_app_post(app, fn, userdata, destroy)` | **Única função thread-safe.** Executa `fn(app, userdata)` na thread de UI, na próxima volta, e marca o frame como sujo; depois chama `destroy(userdata)`. |
| `tbox_app_should_close(app)` | `true` quando a janela pediu fechamento ou a conexão caiu. Esc também fecha (se nenhum controle consumir a tecla). |
| `tbox_app_close(app)` | Libera tudo. |
| `tbox_app_context(app)` | O `tbox_context` interno (pipeline, foco, validação). Para buscar nós e controlar controles, use `tbox_app_query` e `controls.h`. |
| `tbox_app_on_key(app, handler, userdata, destroy)` | Handler de tecla (recebe `const tbox_key_event *`) chamado antes do contexto; retorne `true` para consumir. |
| `tbox_app_on_load(app, handler, ud, destroy)` | Uma vez por documento, após o primeiro frame aceito pelo backend. |
| `tbox_app_on_unload(app, handler, ud, destroy)` | Uma vez ao fechar a janela, com o contexto ainda válido; não cancela fechamento. |
| `tbox_app_on_pagehide(app, handler, ud, destroy)` | A janela perde foco de teclado. |
| `tbox_app_on_pageshow(app, handler, ud, destroy)` | A janela ganha foco de teclado; foco inicial vem após load. |
| `tbox_app_on_resize(app, handler, ud, destroy)` | Recebe `const tbox_app_resize_event *` com dimensões anterior/nova. Não inclui tamanho inicial. |
| `tbox_app_on_error(app, handler, ud, destroy)` | Registro reservado com `const tbox_app_error_event *`; ainda não dispara. |
| `tbox_app_load_from_files(app, html, css)` | Troca o documento na mesma janela (cria um contexto novo). Em falha, o atual continua. Registros de clique/input/select/submit do documento antigo são liberados: registre de novo. |
| `tbox_app_request_redraw(app)` | Peça um frame. Desnecessário depois de mudar o DOM pelas funções da API (ver abaixo); só depois de escrever direto nos campos de um `tbox_html_node`. |
| `tbox_app_set_debug_damage(app, on)` | Contorna as regiões repintadas (depuração). |
| `tbox_app_screenshot_from_files(html, css, w, h, png)` | Renderiza um frame num PNG sem abrir janela. |

O loop típico:

```c
tbox_app *app = tbox_app_create_from_files("ui.html", "ui.css", 800, 600);
if (app == NULL) return 1;
/* registrar handlers aqui */
tbox_app_run(app);
tbox_app_close(app);
```

Os seis registros de ciclo de vida pertencem ao `tbox_app`, sobrevivem à
recarga de HTML e têm um callback por evento. Registrar NULL limpa o slot.
Load não é emitido dentro de create: registre antes de step/wait/run.
Pagehide/pageshow seguem o contrato de foco da janela da tbox. Não há
execução de código dos atributos HTML `onload="..."` etc.

Callbacks de ciclo de vida e teclado podem carregar outro documento e
substituir registros. Se um callback chamar `tbox_app_close`, a destruição aguarda a operação externa
terminar; o chamador não deve reutilizar o handle nem fechá-lo novamente
após esse retorno. Unload pode chamar close recursivamente com segurança.
Chamadas aninhadas de step/wait/run não entram novamente no loop. Exemplo
completo em `example/lifecycle_demo.c`.

### Eventos do documento

| Registro | Chamado quando |
|---|---|
| `tbox_app_on_click(app, "seletor", len, handler, ud, destroy)` → `int` | Um clique (ou Enter/Espaço num botão focado) atinge um elemento que casa com o seletor, ou um descendente dele. Retorna um id; `-1` em seletor inválido. |
| `tbox_app_unbind_click(app, id)` | Remove o registro. |
| `tbox_app_on_input(app, handler, ud, destroy)` | O valor de um `input`/`textarea` muda por edição ou por um popup. O handler recebe `(const char *value, size_t value_length)`. |
| `tbox_app_on_select(app, handler, ud, destroy)` | O usuário escolhe outra opção num `<select>`. Valor como em `on_input`. |
| `tbox_app_on_submit(app, handler, ud, destroy)` | Um formulário é enviado (botão submit ou Enter num campo). |

O handler de clique retorna `bool`: `true` continua a propagação para
ancestrais, `false` para (`stopPropagation`). Detalhes em
[Eventos e interatividade](06-eventos-e-interatividade.md).

`destroy` (`tbox_destroy_notify` em `callback.h`, pode ser `NULL`) libera o
`ud` uma única vez, quando a biblioteca deixa de usá-lo: `unbind`, troca do
handler, `close`. Nenhum callback recebe struct por valor. Ver
[Exemplos](04-exemplos.md#11-userdata-com-dono).

Os callbacks usam os tipos `tbox_app_click_handler`, `tbox_app_input_handler`,
`tbox_app_select_handler` e `tbox_app_submit_handler`; o primeiro parâmetro é
sempre `tbox_app *`. Os registros pertencem ao documento atual e são
liberados no reload ou close. Para mantê-los em novas páginas, registre no
`on_load`. Dentro de um callback de controle, agende a troca de documento
com `tbox_app_post` para ela acontecer depois do despacho atual.

### Busca

| Função | Uso |
|---|---|
| `tbox_app_get_element_by_id(app, id)` | Primeiro elemento com esse `id`. |
| `tbox_app_query(app, "seletor", len)` | Primeiro elemento que casa (`querySelector`); `NULL` também em erro de sintaxe. |
| `tbox_app_query_all(app, "seletor", len)` | Todos (`querySelectorAll`); libere com `tbox_css_selector_node_set_destroy`. |
| `tbox_app_query_in(app, raiz, "seletor", len)` | Primeiro descendente de `raiz` que casa (`Element.querySelector`, sem a própria raiz); para preencher um item criado por `tbox_control_instantiate`. |

Os nós voltam não-`const` e pertencem ao documento atual do app: podem ir
direto para `controls.h` ou para as funções de mutação do DOM (o documento
de um nó está em `node->owner_document`).

### Controles — `controls.h`

Cada controle guarda o estado num lugar (atributo `value`, texto do
`<textarea>`, escolha do `<select>` no contexto, atributo `checked`). Estas
funções escondem onde e mantêm caret, popup e foco coerentes. Recebem o
`tbox_app` e agem no documento atual dele (o contexto é obtido por dentro);
as que só leem o nó (`tbox_control_checked`, `_disabled`,
`tbox_select_option_count`, `_option_at`) não precisam dele. Mudanças por
código não disparam `on_input`/`on_select`.

| Função | Uso |
|---|---|
| `tbox_control_value(app, c)` / `_set_value(app, c, v)` | Valor de input, textarea, select, output, progress, meter, button, option. `set_value` normaliza como o controle (número, data, range com clamp); em `file` só aceita `""`. |
| `tbox_control_label(app, c)` / `_set_label(app, c, t)` | Texto visível: `value` de `input type=button/submit/reset`; `label` ou texto de `<option>`; texto de qualquer outro elemento (`<button>`, `<label>`, `<p>`…). |
| `tbox_control_checked(c)` / `_set_checked(app, c, on)` | Checkbox e radio; marcar um radio desmarca o grupo. |
| `tbox_control_radio_checked(app, radio)` | O radio marcado do grupo, ou `NULL`. |
| `tbox_control_disabled(c)` / `_set_disabled(app, c, on)` | Semântica de `:disabled`; desabilitar o focado tira o foco. |
| `tbox_control_focus(app, c)` / `tbox_control_blur(app)` | Foco por código (rola até o controle no próximo frame). |
| `tbox_select_option_count(s)` / `_option_at(s, i)` | Opções em ordem, atravessando `<optgroup>`. |
| `tbox_select_selected_index(app, s)` / `_set_selected_index(app, s, i)` | Índice escolhido (`-1` = nenhum). |
| `tbox_select_add_option(app, s, valor, rótulo, antes_de)` | Cria e insere uma `<option>`; `SIZE_MAX` = no fim. |
| `tbox_select_remove_option(app, s, i)` / `tbox_select_clear(app, s)` | Remove uma opção / todas (e os grupos). |

```c
tbox_html_node *botao = tbox_app_get_element_by_id(app, tbox_string_view_make("salvar", 6));
tbox_control_set_label(app, botao, tbox_string_view_make("Salvando…", 12));
tbox_html_node *lista = tbox_app_query(app, "#frutas", 7);
tbox_select_add_option(app, lista, tbox_string_view_make("uva", 3), tbox_string_view_make("Uva", 3), SIZE_MAX);
bool aceito = tbox_control_checked(tbox_app_query(app, "#termos", 7));
```

Exemplo completo em `example/controls_demo.c`.

### Tabelas — `controls.h`

Funções `tbox_table_*` para ler e editar `<table>` sem andar na árvore à
mão. Seguem `HTMLTableElement`/`HTMLTableRowElement` do DOM: as linhas são
contadas como `table.rows` (linhas do `<thead>` primeiro, depois as de
`<tbody>` e os `<tr>` filhos diretos da tabela em ordem, depois as do
`<tfoot>`; tabelas aninhadas ficam de fora), então a linha do cabeçalho é a
`0`. O índice de célula é a posição entre os `<td>`/`<th>` da linha, não a
coluna da grade (`colspan`/`rowspan` não são resolvidos). Não há estado
guardado: as funções só leem e editam a árvore, então misturam-se
livremente com as de `html_parser.h`. As que só leem não recebem o app.

| Função | Uso |
|---|---|
| `tbox_table_row_count(t)` / `_row_at(t, i)` | Linhas na ordem de `rows`. |
| `tbox_table_insert_row(app, t, antes_de, células, n)` | Cria um `<tr>` com `n` `<td>` de texto e insere antes da linha `antes_de`, na seção dela; `SIZE_MAX` = no fim do último `<tbody>` (ou da própria tabela, se não houver). |
| `tbox_table_remove_row(app, t, i)` / `tbox_table_clear_body(app, t)` | Remove uma linha / todas as do corpo (mantém caption, thead, tfoot e os `<tbody>`). |
| `tbox_table_head(t)` / `_foot(t)` | Primeiro `<thead>`/`<tfoot>`, ou `NULL`. |
| `tbox_table_cell_count(r)` / `_cell_at(r, i)` | Células (`<td>`/`<th>`) da linha. |
| `tbox_table_insert_cell(app, r, antes_de, texto)` / `_remove_cell(app, r, i)` | Insere um `<td>` / remove uma célula. |
| `tbox_table_cell_text(app, t, linha, coluna)` / `_set_cell_text(…, texto)` | Texto da célula (`textContent`) / troca o conteúdo por um texto. |
| `tbox_table_row_of(nó)` / `_cell_of(nó)` | O `<tr>` / `<td>`/`<th>` mais próximo acima do nó (a tabela mais interna vence). |
| `tbox_table_row_index(r)` / `_cell_index(c)` | `rowIndex` / `cellIndex`; `-1` fora de tabela/linha. |

```c
static bool on_linha(tbox_app *app, tbox_html_node *node, void *userdata) {
    tbox_html_node *t = userdata;
    ptrdiff_t linha = tbox_table_row_index(tbox_table_row_of(node));
    tbox_string_view nome = tbox_table_cell_text(app, t, (size_t)linha, 0);
    ...
    return true;
}

tbox_html_node *t = tbox_app_query(app, "#clientes", 9);
tbox_table_clear_body(app, t);
tbox_string_view celulas[] = { tbox_string_view_make("Ana", 3), tbox_string_view_make("ativo", 5) };
tbox_table_insert_row(app, t, SIZE_MAX, celulas, 2);
tbox_app_on_click(app, "#clientes tbody td", 18, on_linha, t, NULL);
```

Exemplo completo em `example/table_demo.c`.

Ordenação de linhas e seleção gerenciada pela biblioteca ficaram de fora;
seleção se faz com `tbox_control_set_class` e CSS. Para linhas com mais do
que texto (ícone, botão), use um `<template>` com o `<tr>` e
`tbox_control_instantiate` no `<tbody>` (próxima seção).

### Itens, classes e visibilidade — `controls.h`

Para listas, cards e linhas que a aplicação monta conforme os dados. O HTML
declara cada item uma vez dentro de um `<template>` (não desenhado; os
filhos são nós comuns), e o código cria cópias, preenche cada uma com
`tbox_app_query_in` + `tbox_control_set_label`/`_set_value` e marca o estado
visual com classes. `<select>`/`<optgroup>` ficam de fora: as opções têm
estado no contexto, use `tbox_select_*`.

| Função | Uso |
|---|---|
| `tbox_control_instantiate(app, modelo, pai, antes_de)` | Copia (profundo) cada elemento filho do `<template>` e insere no `pai` antes do filho de elemento `antes_de` (`SIZE_MAX` = no fim). Devolve a primeira cópia. `id`s são copiados como estão: deixe-os fora do modelo. |
| `tbox_control_remove(app, nó)` / `tbox_control_clear(app, pai)` | Tira um item / todos os filhos, movendo antes o foco se estiver dentro. Em `<table>` o `clear` também tira `thead`/`tfoot`; para linhas use `tbox_table_clear_body`. |
| `tbox_control_has_class(nó, c)` / `_set_class(app, nó, c, on)` | Uma classe (sem espaços); `set_class` é idempotente. |
| `tbox_control_hidden(nó)` / `_set_hidden(app, nó, on)` | Atributo `hidden` (`[hidden] { display: none }` na UA); esconder o focado, ou quem o contém, tira o foco. |

```c
/* <ul id="pastas"></ul>
 * <template id="pasta"><li class="item"><span class="nome"></span></li></template> */
tbox_html_node *lista = tbox_app_query(app, "#pastas", 7);
tbox_html_node *modelo = tbox_app_query(app, "#pasta", 6);
tbox_control_clear(app, lista);
tbox_html_node *item = tbox_control_instantiate(app, modelo, lista, SIZE_MAX);
tbox_control_set_label(app, tbox_app_query_in(app, item, ".nome", 5), tbox_string_view_make("Fotos", 5));
tbox_control_set_class(app, item, tbox_string_view_make("ativo", 5), true);
```

## 2. Documento e eventos — `context.h`

O `tbox_context` é o documento vivo mais o pipeline. Com `tbox_app` você o
obtém por `tbox_app_context(app)`; sem janela, você o abre direto.

### Abrir sem janela

| Função | Uso |
|---|---|
| `tbox_context_open(html, len, css, len, fonts, images)` | Parseia e guarda. `fonts`/`images` são emprestados (`images` pode ser `NULL`). |
| `tbox_context_open_with_options(…, const tbox_context_options *)` | Mesmas opções de `tbox_app`; `NULL` = padrão. |
| `tbox_context_run_frame(ctx, w, h, &list)` | Recalcula tudo e devolve a display list (válida até o próximo frame). |
| `tbox_context_set_animation_time(ctx, s)` / `tbox_context_next_frame_time(ctx)` | Tempo para `transition`, `animation`, rolagem suave e scroll snap. Depois de um frame, `next_frame_time` diz quando o próximo muda algo: `INFINITY` = ocioso; ≤ agora = já; senão, um prazo (fim de um atraso, do snap). `tbox_context_animations_active` = prazo finito. |
| `tbox_context_close(ctx)` | Libera documento, folhas e arena de frame. Não toca `fonts`/`images`. |
| `tbox_context_document(ctx)` | O `tbox_html_document`, necessário para mutar o DOM. |

### Eventos de ponteiro e teclado

Com `tbox_app`, ele chama estas funções para você. Sem janela, você as
chama com eventos da sua plataforma. Todas retornam `true` quando um novo
frame é necessário.

| Função | Evento |
|---|---|
| `tbox_context_dispatch_click(ctx, x, y)` | Clique: foco, controles, handlers `on_click` com bubbling |
| `tbox_context_update_hover(ctx, has_pos, x, y)` | Posição do ponteiro → `:hover` |
| `tbox_context_set_active_at(ctx, pressed, x, y)` | Pressionar/soltar → `:active` |
| `tbox_context_scroll(ctx, x, y, dy)` | Roda do mouse |
| `tbox_context_scrollbar_press/_drag/_release` | Barra de rolagem |
| `tbox_context_drag_select_at`, `_color_drag`, `_range_drag`, `_datetime_drag` | Arrastos em campos e popups |
| `tbox_context_dispatch_key(ctx, const tbox_key_event *)` | Tecla lógica (`tbox_key` em `input.h`) |
| `tbox_context_dispatch_text(ctx, utf8)` | Texto já composto (digitação, colar) |
| `tbox_context_hit_test(ctx, x, y)` | Caixa mais profunda sob o ponto (último frame) |
| `tbox_context_cursor_at(ctx, x, y)` | Cursor CSS a mostrar |

### Outros auxiliares de formulário

| Função | Uso |
|---|---|
| `tbox_context_focused_node(ctx)` | Elemento com foco. |
| `tbox_context_selected_text(ctx)` | Texto selecionado no campo focado. |
| `tbox_context_file_path(ctx, input)` | Caminho absoluto escolhido num `input type=file`. |
| `tbox_context_email_valid`, `_url_valid`, `_number_valid` | Validação no formato do HTML. |

O tbox não envia formulários pela rede; o handler de submit lê os campos
e decide.

## 3. O DOM — `html_parser.h`

`tbox_html_node` é uma struct visível: `type`, ponteiros `parent`,
`first_child`, `last_child`, `next_sibling`, `prev_sibling` e, para elementos,
`element.tag_name` e `element.attributes[attribute_count]`. Nomes de tag e
atributo estão em minúsculas.

| Função | Uso |
|---|---|
| `tbox_html_document_root(doc)` | Raiz do documento. |
| `tbox_html_node_get_attribute(node, nome)` | Ponteiro para o atributo ou `NULL`. |
| `tbox_html_node_set_attribute(doc, node, nome, valor)` | Cria ou substitui. |
| `tbox_html_node_remove_attribute(node, nome)` | `true` se existia. |
| `tbox_html_node_set_text_content(doc, node, texto)` | Troca todos os filhos por um nó de texto (`textContent =`). |
| `tbox_html_node_text_content(arena, node)` | Lê o texto concatenado (`textContent`). |
| `tbox_html_element_create(doc, "tag")` / `tbox_html_text_create(doc, texto)` | Cria nós soltos; as strings são copiadas. |
| `tbox_html_node_clone(doc, node, deep)` | Cópia solta (com a subárvore se `deep`). |
| `tbox_html_node_append_child`, `_prepend_child`, `_insert_before(pai, filho, ref)` | Insere; um nó já na árvore é movido. Ciclos são recusados (no-op). |
| `tbox_html_node_replace(velho, novo)` | Troca um nó por outro. |
| `tbox_html_node_remove(node)` / `_remove_children(node)` | Desconecta (a memória continua na arena do documento). |
| `tbox_html_node_first_element_child`, `_next_element_sibling`, `_element_child_count` | Navegação só por elementos. |
| `tbox_html_node_has_attribute`, `_set_bool_attribute(doc, node, nome, on)` | Atributos booleanos (`hidden`, `disabled`…). |
| `tbox_html_node_has_class`, `_add_class`, `_remove_class`, `_toggle_class` | `classList`. |
| `tbox_html_parse` / `tbox_html_document_destroy` | Parser avulso, sem pipeline. |

Toda mutação feita por estas funções (ou por `controls.h`) avança
`tbox_html_document_revision(doc)`, e o `tbox_app` desenha um frame quando
ela muda — dentro ou fora de handlers, sem `tbox_app_request_redraw`.
Escrever direto nos campos da struct (`node->text.text = …`) não é visto:
nesse caso peça o frame. Cada nó aponta para o seu documento em
`owner_document`. Veja [Atualização da tela](05-atualizacao-da-tela.md).

> **Nota sobre `const`.** Consultas de baixo nível (`tbox_css_selector_select`,
> `tbox_html_document_root`) devolvem ponteiros `const`. Prefira
> `tbox_app_get_element_by_id`/`tbox_app_query`, que devolvem nós
> mutáveis; os handlers também recebem o nó já não-`const`.

## 4. Consultas — `css_selector.h` e `html_xpath.h`

```c
tbox_css_selector_node_set set =
    tbox_css_selector_select(tbox_html_document_root(doc), "#status", 7);
for (size_t i = 0; i < set.count; i++) { /* set.items[i] */ }
tbox_css_selector_node_set_destroy(&set);
```

- `tbox_css_selector_select` — compila, avalia e destrói; erro de sintaxe vira
  conjunto vazio. Semântica de `querySelectorAll` (a raiz não entra).
- `tbox_css_selector_compile` / `_query_evaluate` / `_query_matches` /
  `_query_first` — para reutilizar uma consulta e obter o offset do erro.
- `tbox_css_selector_closest(node, "seletor", len)` — o próprio nó ou o
  ancestral mais próximo que casa (`Element.closest`).
- `tbox_xpath_select` / `tbox_xpath_compile` — subconjunto prático de XPath
  1.0 (`//a`, `a/b`, `@nome`, `text()`, `[n]`, `[@nome='v']`; sem funções nem
  eixos nomeados).

## 5. Saída sem janela — `output.h`

| Função | Uso |
|---|---|
| `tbox_raster_display_list(pixels, w, h, &list)` | Rasteriza a lista inteira num buffer XRGB8888. |
| `tbox_raster_write_png(path, pixels, w, h)` | Grava o buffer em PNG. |
| `tbox_damage_tracker_*`, `tbox_raster_display_list_damaged` | Repintura parcial (ver [capítulo 5](05-atualizacao-da-tela.md#usando-o-damage-tracking-sem-tbox_app)). |
| `tbox_raster_fill_rect`, `_text_run`, `_image`, `_fill_rounded_rect` | Primitivas, para quem escreve seu próprio backend. |

## 6. Recursos — `font.h` e `image.h`

Só necessários quando você usa `tbox_context` direto (o `tbox_app` cria os
seus).

| Função | Uso |
|---|---|
| `tbox_font_face_cache_create(regular, n, bold, n, resolver, ud, destroy)` | Cache de faces. Os bytes são copiados. `resolver` (opcional, recebe `const tbox_font_query *`) busca outras famílias sob demanda; `destroy` libera `ud` no `_destroy` do cache. |
| `tbox_font_source_fontconfig_create()` + `tbox_font_source_resolve()` | Descoberta pelo Fontconfig (Linux). |
| `tbox_font_source_embedded_create(data, n)` | Fonte fixa, útil para testes determinísticos. |
| `tbox_font_face_cache_register(cache, família, peso, itálico, data, n)` | Registra uma fonte própria (é o que `@font-face` usa). |
| `tbox_image_cache_create(base_dir)` / `_destroy` | Cache de imagens; caminhos relativos resolvem contra `base_dir`. |

## 7. Camadas internas expostas

`css_parser.h`, `css_cascade.h`, `style.h`, `layout.h` e `render.h` são
públicos porque o pipeline é montado com eles e os testes os usam. Servem
para inspecionar o resultado (por exemplo, ler `tbox_layout_box` depois de
um hit-test, ou resolver o estilo de um nó), mas não são necessários para
construir uma aplicação. São as partes da API mais sujeitas a mudar.

## Estabilidade

O tbox é experimental. A API mudou entre versões (por exemplo, o handler de
clique passou de `void` para `bool`) e ainda vai mudar; o
[roadmap](14-limitacoes-e-roadmap.md) prevê fechar o contrato de aplicação e
eventos como primeiro marco.

## Pintura vetorial SVG

`tbox_style.svg_paint` é um grupo imutável fora de linha: leia por
`tbox_style_svg_paint_of`, que devolve os valores iniciais se o ponteiro é
NULL. Como os demais grupos, vive na arena de resolução e só pode ser
alterado por cópia. `tbox_style.svg` é uma cena interna emprestada do frame.
`tbox_paint_op` pode ter kind `TBOX_PAINT_PATH`: `path` aponta para segmentos
públicos e parâmetros de pintura de `render.h`, válidos até resetar a arena
da display list. A matriz mapeia geometria ao espaço de `rect`; `rect` já
inclui todo o traço. `tbox_css_resolved_declaration.presentation_attribute`
identifica atributos SVG, de prioridade inferior às folhas de autor.
Essas extensões alteram o tamanho das structs públicas; recompile clientes.
