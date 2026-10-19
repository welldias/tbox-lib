# Organização do código

> - **Escopo:** onde cada coisa mora em `src/`, `include/`, `tests/` e `example/`; headers internos.
> - **Código relacionado:** `src/`, `include/tbox/`, `src/CMakeLists.txt`
> - **Veja também:** [Arquitetura](../02-arquitetura.md), [Convenções](convencoes.md)

O mapa conceitual dos módulos está em [Arquitetura](../02-arquitetura.md).
Este arquivo é o mapa físico, para quem vai editar o código.

## Raiz

| Caminho | Conteúdo |
|---|---|
| `include/tbox/` | API pública. `tbox.h` inclui todos os outros headers. |
| `src/<módulo>/` | Implementação; um diretório por módulo. Os `.c` entram no build pelo glob de `src/CMakeLists.txt`. |
| `tests/<módulo>/` | Testes unitários/integração, todos no binário `tbox_tests` (`tests/main.c`). |
| `tests/assets/` | Fixtures visuais `NNN.html`, com textos em inglês; a referência é capturada no Firefox pelo `tbox_cmp`. |
| `tests/tbox_cmp.cpp` | Ferramenta opcional de comparação visual (C++/OpenCV). |
| `example/` | Programas de exemplo e demos (ver [Exemplos](../04-exemplos.md)). |
| `external/` | Código vendorizado: `utf8.h`, `stb_image`, fonte Liberation Sans. |
| `docs/` | Esta documentação. |

## Módulos em `src/`

| Diretório | Header público | Arquivos principais |
|---|---|---|
| `base/` | `callback.h` (`tbox_destroy_notify`) | `tbox_arena`, `tbox_vector`, `tbox_list`, `tbox_string` (UTF-8, ASCII, string builder, colapso de espaços), `tbox_number` (números sem locale), `tbox_debug`, `tbox_mutex.h` (fila de `tbox_app_post`), `tbox_release` (liberação de `userdata` adiada durante callbacks) |
| `html_parser/` | `html_parser.h` | tokenizer, tree builder (fechamento implícito), `tbox_html_entities.c` (tabela WHATWG gerada) |
| `html_xpath/` | `html_xpath.h` | lexer, parser, avaliador de XPath sobre `tbox_html_node` |
| `css_parser/` | `css_parser.h` | tokenizer, parser, stylesheet (incl. `@media`, `@font-face`, `@keyframes`), `tbox_css_supports.c` (condições de `@supports`) |
| `css_selector/` | `css_selector.h` | parse e match de seletores; contextos de `:hover`/`:focus`/`:active` |
| `css_cascade/` | `css_cascade.h` | cascata por origem/especificidade, parsing e conversão de cores |
| `style/` | `style.h` | `tbox_style.c` (resolução de propriedades), `tbox_style_values.c` (valores compartilhados), `tbox_style_functions.c` (funções CSS), `tbox_style_keywords.c` (`inherit`/`initial`/`unset`), `tbox_style_vars.c` (`var()`), `tbox_style_generated.c` (`::before`/`::after`), `tbox_style_animate.c` (interpolação de propriedades e avaliação de curvas de tempo) |
| `font/` | `font.h` | `tbox_font_source*` (fontconfig/embedded), `tbox_font_face`, `tbox_font_face_cache` |
| `image/` | `image.h` | `tbox_image_cache` (decodificação via `stb_image`) |
| `layout/` | `layout.h` | ver abaixo |
| `render/` | `render.h` | `tbox_render.c` (display list), `tbox_render_internal.h` (`tbox_render_paint_order`) |
| `output/` | `output.h` | `tbox_raster.c`, `tbox_raster_damage.c`, `tbox_window_backend.*`, `tbox_backend_wayland.*` |
| `context/` | `context.h` | ver abaixo |
| `app/` | `app.h` | `tbox_app.c`; `tbox_app_unavailable.c` (stubs quando falta Fontconfig) |

O teste geométrico de polígonos fica em `src/base/tbox_polygon.h`, compartilhado
por raster e hit-test. A resolução para pixels fica em Render, inclusive para
barras de rolagem acrescentadas pelo contexto; parsing de `polygon()` e dos
gradientes cônicos continua centralizado em `tbox_style_functions.c`.

## `src/style/`

`tbox_style.c` coordena a resolução das propriedades e monta o estilo final.
`tbox_style_functions.c` interpreta funções de cálculo, gradientes, filtros,
formas, transformações e funções de tempo, produzindo os tipos existentes.
`tbox_style_values.c` reúne os utilitários compartilhados de números, unidades,
comprimentos, ângulos, posições, raios e argumentos. Os contratos entre esses
arquivos ficam em `tbox_style_internal.h`, sem ampliar a API pública.

Cores continuam em `css_cascade/tbox_css_color_parse.c`; substituições
`var()`/`light-dark()` em `tbox_style_vars.c`; funções dependentes do DOM e
contadores em `tbox_style_generated.c`. Layout e pintura consomem os valores
resolvidos sem interpretar a sintaxe das funções.

## `src/layout/`

`tbox_layout.c` é a entrada e classifica os nós. Operações compartilhadas:
`tbox_layout_box.c`, `tbox_layout_common.c`, `tbox_layout_geometry.c`,
`tbox_layout_flow.c` (fluxo de bloco, margin collapsing, caixas anônimas),
`tbox_layout_sizing.c` (tamanhos intrínsecos). Texto em linha:
`tbox_layout_inline_words.c` (coleta de palavras) e `tbox_layout_inline.c`
(quebra de linha, runs). Casos específicos: `tbox_layout_text.c`,
`tbox_layout_input.c`, `tbox_layout_select.c`, `tbox_layout_textarea.c`,
`tbox_layout_image.c`, `tbox_layout_list.c`, `tbox_layout_table.c`,
`tbox_layout_table_grid.c`, `tbox_layout_flex.c`. Tipos e protótipos
compartilhados: `tbox_layout_internal.h`.

## `src/context/`

`tbox_context.c` abre/fecha o contexto e coordena cada frame. Estado
compartilhado e declarações entre arquivos: `tbox_context_internal.h`.

| Arquivo | Responsabilidade |
|---|---|
| `tbox_context_common.c`, `tbox_context_hit_test.c` | utilitários e localização de elementos na árvore de layout |
| `tbox_context_scroll.c`, `tbox_context_focus.c`, `tbox_context_events.c` | rolagem, foco, despacho de eventos |
| `tbox_context_input.c`, `tbox_context_range.c`, `tbox_context_text.c`, `tbox_context_textarea.c` | controles de entrada e edição de texto |
| `tbox_context_select.c`, `tbox_context_color.c`, `tbox_context_calendar.c`, `tbox_context_file.c` | controles com popup próprio |
| `tbox_context_forms.c` | valores iniciais, submit e reset de formulários |
| `tbox_context_control_theme.c` | CSS dos popups (`tbox-popup`/`tbox-part`) |
| `tbox_context_transition.c` | `transition` (toda propriedade animável, `@starting-style`) |
| `tbox_context_animation.c` | `animation` + `@keyframes` |
| `tbox_context_font.c` | `@font-face` |
| `tbox_ua_style.c` | folha user-agent gerada a partir de regras tipadas + `tbox_ua_style_config` |

Geometria e pintura próprias de cada controle ficam no módulo de layout ou
render correspondente; o contexto as chama durante o frame e o despacho de
eventos.

## Fronteira de plataforma

`src/output/tbox_window_backend.h` é a interface privada que `tbox_app` usa
para abrir janela, ler eventos (`tbox_input_event`), clipboard, cursor e
apresentar quadros. Tipos nativos (Wayland, keymap, compositor) não podem
aparecer acima dela. O único backend hoje é `tbox_backend_wayland.c`.
