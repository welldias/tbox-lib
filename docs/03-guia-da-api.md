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
| `tbox_app_context(app)` | O `tbox_context` interno, para registrar handlers e acessar o DOM. |
| `tbox_app_on_key(app, handler, userdata, destroy)` | Handler de tecla (recebe `const tbox_key_event *`) chamado antes do contexto; retorne `true` para consumir. |
| `tbox_app_load_from_files(app, html, css)` | Troca o documento na mesma janela (cria um contexto novo). Em falha, o atual continua. Handlers do contexto antigo não são levados: registre de novo. |
| `tbox_app_request_redraw(app)` | Peça um frame depois de mudar o DOM fora de um handler. |
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

### Handlers

| Registro | Chamado quando |
|---|---|
| `tbox_context_on_click(ctx, "seletor", len, handler, ud, destroy)` → `int` | Um clique (ou Enter/Espaço num botão focado) atinge um elemento que casa com o seletor, ou um descendente dele. Retorna um id; `-1` em seletor inválido. |
| `tbox_context_unbind_click(ctx, id)` | Remove o registro. |
| `tbox_context_on_input(ctx, handler, ud, destroy)` | O valor de um `input`/`textarea` muda por edição ou por um popup. O handler recebe `(const char *value, size_t value_length)`. |
| `tbox_context_on_select(ctx, handler, ud, destroy)` | O usuário escolhe outra opção num `<select>`. Valor como em `on_input`. |
| `tbox_context_on_submit(ctx, handler, ud, destroy)` | Um formulário é enviado (botão submit ou Enter num campo). |

O handler de clique retorna `bool`: `true` continua a propagação para
ancestrais, `false` para (`stopPropagation`). Detalhes em
[Eventos e interatividade](06-eventos-e-interatividade.md).

`destroy` (`tbox_destroy_notify` em `callback.h`, pode ser `NULL`) libera o
`ud` uma única vez, quando a biblioteca deixa de usá-lo: `unbind`, troca do
handler, `close`. Nenhum callback recebe struct por valor. Ver
[Exemplos](04-exemplos.md#11-userdata-com-dono).

### Formulários

| Função | Uso |
|---|---|
| `tbox_context_focused_node(ctx)` | Elemento com foco. |
| `tbox_context_select_value(ctx, select)` / `_select_set_value` | Ler e escolher a opção de um `<select>`. |
| `tbox_context_selected_text(ctx)` | Texto selecionado no campo focado. |
| `tbox_context_file_path(ctx, input)` | Caminho absoluto escolhido num `input type=file`. |
| `tbox_context_email_valid`, `_url_valid`, `_number_valid` | Validação no formato do HTML. |

O valor dos campos vive no DOM: leia o atributo `value` (ou `checked`) do
nó. O tbox não envia formulários pela rede; o handler de submit lê os
campos e decide.

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
| `tbox_html_node_create(doc, tipo)` + `_append_child(pai, filho)` | Cria e insere nós. |
| `tbox_html_node_remove(node)` | Desconecta (a memória continua na arena do documento). |
| `tbox_html_parse` / `tbox_html_document_destroy` | Parser avulso, sem pipeline. |

Mutar o DOM não redesenha sozinho: dentro de handlers o frame já fica sujo;
fora deles, chame `tbox_app_request_redraw`. Veja
[Atualização da tela](05-atualizacao-da-tela.md).

> **Nota sobre `const`.** Consultas (`tbox_css_selector_select`,
> `tbox_html_document_root`) devolvem ponteiros `const`. Para mutar um nó
> encontrado assim, os exemplos do repositório fazem cast para
> `tbox_html_node *`; os handlers recebem o nó já não-`const`.

## 4. Consultas — `css_selector.h` e `html_xpath.h`

```c
tbox_css_selector_node_set set =
    tbox_css_selector_select(tbox_html_document_root(doc), "#status", 7);
for (size_t i = 0; i < set.count; i++) { /* set.items[i] */ }
tbox_css_selector_node_set_destroy(&set);
```

- `tbox_css_selector_select` — compila, avalia e destrói; erro de sintaxe vira
  conjunto vazio. Semântica de `querySelectorAll` (a raiz não entra).
- `tbox_css_selector_compile` / `_query_evaluate` / `_query_matches` — para
  reutilizar uma consulta e obter o offset do erro.
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
