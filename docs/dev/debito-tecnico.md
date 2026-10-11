# Débito técnico conhecido

> - **Escopo:** trabalho conscientemente adiado, com o gatilho que justifica revisitar cada item. Só itens ainda abertos.
> - **Código relacionado:** indicado em cada item
> - **Veja também:** [Decisões](decisoes.md), [Limitações e roadmap](../14-limitacoes-e-roadmap.md)

Diferente de uma lista de features faltando (essa está em
[Limitações e roadmap](../14-limitacoes-e-roadmap.md)), aqui ficam decisões
de design adiadas que têm complexidade própria. Cada item diz **o que é**,
**por que importa**, **quando revisitar** e **o que toca**. Itens resolvidos
saem da lista (o histórico deles está em [historico.md](historico.md)).

## Pipeline

### Estilo e layout incrementais

- **O que é:** desde a v20 só a repintura é parcial. `tbox_style_resolve_tree`
  e `tbox_layout_build` ainda rodam sobre o documento inteiro a cada frame
  sujo e passam a dominar o custo do frame (ver medição em
  [Atualização da tela](../05-atualizacao-da-tela.md#custo-medido)).
- **Por que importa:** é o próximo gargalo para hover, digitação e cursor
  piscando em documentos grandes.
- **Quando revisitar:** quando o tempo de estilo+layout for medido como
  problema num caso real. Esboço já discutido:
  - marcar nós sujos na origem da mudança (hover, foco, `:active`, texto,
    scroll, mutação via API);
  - estilo: invalidar o nó, os descendentes (herança) e os afetados por
    seletores dependentes (`:hover > x`, `+`, `~`);
  - layout: reaproveitar caixas cujo estilo e conteúdo não mudaram;
    mudança de tamanho propaga para pais e irmãos seguintes;
  - arenas: hoje tudo vive na `frame_arena`; reter árvores entre frames
    exige outro esquema de vida;
  - transições, sticky, scroll e hit-test leem a árvore completa e precisam
    continuar consistentes.

  O damage tracking da v20 continua válido: ele compara display lists,
  independentemente de como foram produzidas.
- **Toca:** `tbox_context_run_frame`, Style, Layout, arenas.

### Buffer duplo no backend Wayland

- **O que é:** o backend escreve no mesmo buffer `wl_shm` que o compositor
  pode ainda estar lendo, o que arrisca corrupção visual momentânea. Com dois
  buffers alternados, só se escreve no que o compositor liberou
  (`wl_buffer.release`).
- **Atenção:** o buffer livre guarda o frame de *dois* presents atrás; antes de
  repintar o dano atual é preciso copiar do outro buffer as regiões sujas do
  frame anterior (ou acumular o dano dos dois últimos frames).
- **Quando revisitar:** próximo passo natural de saída; escopo pequeno.
- **Toca:** `src/output/tbox_backend_wayland.c`.

### Screenshot headless incremental

- **O que é:** o caminho de captura (`tbox_app_screenshot_from_files`) sempre
  repinta tudo.
- **Quando revisitar:** só se a captura passar a ser usada em loop.

### Loop sem espera

- **O que é:** `tbox_app_step` chama o backend com timeout 0 e nunca bloqueia;
  um `while (!tbox_app_should_close(app)) tbox_app_step(app);` gira sem pausa.
  Não há agendamento por vsync (`wl_surface.frame`) nem espera por evento.
- **Por que importa:** consumo de CPU com a janela ociosa.
- **Quando revisitar:** junto com o contrato de invalidação explícita do
  roadmap (item 1 em [Limitações e roadmap](../14-limitacoes-e-roadmap.md)).
- **Toca:** `src/app/tbox_app.c`, `tbox_window_backend_poll`.

## API e modelo de documento

### Caminhos em folhas CSS vinculadas

- **O que é:** `<link rel=stylesheet>` concatena CSS local às folhas do
  documento. Isso preserva a ordem das regras, mas `url()` de `@font-face`
  ainda é resolvido contra `asset_base_dir`, não contra o diretório da folha.
- **Quando revisitar:** quando um exemplo precisar de fontes em folhas CSS
  armazenadas em subdiretórios.
- **Toca:** `src/context/tbox_context.c`, registro de fontes e cascata.

### Memória do DOM em sessões longas

- **O que é:** `tbox_html_node` vive na arena do documento sem `free`
  individual. `tbox_html_node_remove`, `set_attribute`, `remove_attribute` e
  `set_text_content` deixam a memória antiga órfã até o documento ser
  destruído.
- **Por que importa:** uma UI de vida longa com milhares de mutações só faz a
  arena crescer.
- **Quando revisitar:** quando medido como problema real. Não resolver
  preventivamente.
- **Toca:** `src/html_parser/`, `tbox_context`.

### Um documento por janela

- **O que é:** `tbox_context` assume um documento, uma árvore de layout e uma
  display list. Tooltips, menus de contexto e diálogos modais poderiam pedir
  passadas de layout/render separadas (ou subsurfaces) compostas na mesma
  janela, para não forçar relayout do app inteiro e escapar do clipping do
  container. Os popups atuais dos controles (`tbox-popup`) são resolvidos
  dentro do mesmo contexto.
- **Por que importa:** mudaria a assinatura de `tbox_context` e
  `tbox_context_run_frame` desde a raiz.
- **Quando revisitar:** quando um desses casos entrar em escopo.

### Composição de texto (IME)

- **O que é:** o texto chega por `tbox_context_dispatch_text`, separado das
  teclas, mas o backend Wayland ainda não implementa o protocolo de
  composição (text-input/IME). Sequências XKB Compose funcionam.
- **Quando revisitar:** quando houver necessidade de idiomas com IME.
- **Toca:** `src/output/tbox_backend_wayland.c`, `tbox_window_backend.h`.

### Carregamento assíncrono de recursos

- **O que é:** fontes e imagens são carregadas de forma síncrona durante o
  frame que precisa delas. Não há gatilho de redesenho por recurso que
  terminou de carregar.
- **Quando revisitar:** quando houver recursos remotos ou grandes.

## Build e empacotamento

### Consumo por outro projeto

- **O que é:** não há regras de `install`, arquivo `pkg-config` nem export
  de alvos CMake. `src/CMakeLists.txt` usa `${CMAKE_SOURCE_DIR}` nos
  `target_include_directories`, o que só é correto quando o tbox é o projeto
  raiz — com `add_subdirectory` a partir de outro projeto, os caminhos
  apontam para o lugar errado (`${PROJECT_SOURCE_DIR}` ou
  `${CMAKE_CURRENT_SOURCE_DIR}/..` resolveriam). A fonte embutida também
  precisaria ser instalada junto.
- **Quando revisitar:** item 4 do roadmap (alvos de distribuição), ou antes,
  se um projeto externo quiser usar o tbox.
- **Toca:** `CMakeLists.txt`, `src/CMakeLists.txt`.

## Thread-safety

- **O que é:** a lib declara "não thread-safe, sem lock interno". Estado
  compartilhado conhecido: arenas, os contextos globais de `:hover`/`:focus`/
  `:active` em `css_selector.c` (`tbox_css_selector_set_*_context`), caches de
  fonte e imagem.
- **Por que importa:** decisão transversal; a estratégia (lock por estrutura,
  lock global, "cada contexto só na thread que o criou") depende do caso de
  uso.
- **Quando revisitar:** quando threading virar requisito real (documentos em
  paralelo, motor de script em outra thread). Nesse momento, mapear cada
  estado mutável compartilhado e decidir caso a caso.
- **Regra até lá:** ver [Convenções](convencoes.md#estado-global-e-threads) —
  nenhum global/estático mutável novo sem discussão.

## Layout

### Margens negativas no collapsing entre irmãos

- **O que é:** o collapsing usa `max` só quando as duas margens são ≥ 0; com
  qualquer lado negativo o par soma, em vez do algoritmo do CSS 2.1 §8.3.1.
- **Quando revisitar:** quando um caso real mostrar o gap errado.
- **Toca:** `src/layout/tbox_layout_flow.c`.

### Posição estática de `absolute`/`fixed`

- **O que é:** com os dois lados de um eixo em `auto` (ou no caso circular
  `top: auto` + `bottom` + `height: auto`), a caixa vai para a origem do
  containing block, não para "onde estaria se fosse `static`".
- **Toca:** `tbox_layout_resolve_absolute_edge` em `tbox_layout_geometry.c`.

### Algoritmo CSS 2.1 §10.3.7 completo

- **O que é:** `left`+`right` esticam a caixa, mas o sistema completo de
  equações (incluindo shrink-to-fit quando largura e os dois lados são `auto`)
  não está implementado.
- **Quando revisitar:** um tooltip que precise "abraçar" o conteúdo.

### Bloco dentro de conteúdo inline some

- **O que é:** um filho `display: block` no meio do texto de um parágrafo
  (ex.: `<p>um <span style="display:block">dois</span> três</p>`) não é
  desenhado; o CSS 2.1 §9.2.1.1 o colocaria numa caixa de bloco própria
  entre as linhas de antes e de depois.
- **Por que importa:** apareceu com `all: initial` enquanto o `display`
  inicial era `block`; também vale para qualquer `display: block`
  declarado.
- **Toca:** `tbox_layout_build_anonymous_box` e a separação de runs inline
  em `tbox_layout_flow.c`.

### Containing block de ancestral posicionado com `height: auto`

- **O que é:** o `padding_box` passado como containing block aos descendentes
  `absolute`/`fixed` é calculado antes dos filhos de fluxo, quando a altura
  final ainda não é conhecida. Um descendente ancorado por `bottom` vê um
  containing block mais baixo que o real.
- **Por que importa:** caso comum (card de altura variável com badge no canto
  inferior).
- **Quando revisitar:** exigiria uma segunda passada (fluxo primeiro, fora de
  fluxo depois).
- **Toca:** `tbox_layout_build_element` (`context_for_children.nearest_ancestor`).

## HTML Parser

### Fechamento implícito de `<tr>`/`<td>`/`<th>`/`<option>`

- **O que é:** só `<p>` e `<li>` fecham implicitamente. Tabelas e `<select>`
  escritos à mão costumam omitir esses fechamentos.
- **Quando revisitar:** um caso real de tabela ou `<select>` com fechamento
  omitido. Mesmo mecanismo de truncar `open_elements` já usado para `<p>`.
- **Toca:** `tbox_html_tree_builder_handle_start_tag`.

### Entidades: windows-1252, maiúsculas alternativas, multi-codepoint

- **O que é:** referências numéricas em `0x80`–`0x9F` não são remapeadas para
  Windows-1252; variantes como `&AMP;` não são reconhecidas; as ~12
  entidades de 2 codepoints ficam como texto literal. O modo legado sem `;`
  também não existe (decisão, não débito).
- **Quando revisitar:** só se um caso real depender disso.
- **Toca:** `src/html_parser/tbox_html_entities.c`.
