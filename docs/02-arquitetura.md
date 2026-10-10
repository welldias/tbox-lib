# 2. Arquitetura

> - **Escopo:** como a biblioteca está dividida em módulos, o que cada um faz e como os dados passam de HTML/CSS até pixels.
> - **Código relacionado:** `include/tbox/*.h`, `src/*/`
> - **Veja também:** [Atualização da tela](05-atualizacao-da-tela.md), [Memória e concorrência](13-memoria-e-concorrencia.md), [Organização do código](dev/organizacao-do-codigo.md)

## A ideia em uma frase

O tbox é um pequeno motor de navegador sem rede e sem JavaScript: lê HTML e
CSS, calcula estilo e layout, gera uma lista de comandos de desenho e
rasteriza em software. A aplicação C muda o documento (DOM) e a biblioteca
mantém a tela sincronizada — um modelo **retained mode**.

## Camadas

```mermaid
flowchart TB
    subgraph IN [Entrada]
        HTML[HTML Parser<br/>html_parser.h]
        CSS[CSS Parser<br/>css_parser.h]
    end
    subgraph STYG [Estilo]
        SEL[CSS Selector<br/>css_selector.h]
        CASC[CSS Cascade<br/>css_cascade.h]
        STY[Style<br/>style.h]
    end
    subgraph GEO [Geometria e pintura]
        LAY[Layout Tree<br/>layout.h]
        REN[Render Pipeline<br/>render.h]
        OUT[Output Display<br/>output.h]
    end
    subgraph SVC [Serviços transversais]
        FONT[Fonte / Texto<br/>font.h]
        IMG[Imagens<br/>image.h]
        BASE[Base<br/>arena, vector, string]
    end
    CTX[Orchestration<br/>context.h]
    APP[Application<br/>app.h]
    XP[XPath<br/>html_xpath.h]

    HTML --> DOM[(Árvore DOM)]
    CSS --> SS[(Stylesheets)]
    DOM --> SEL
    SS --> SEL
    SEL --> CASC --> STY --> LAY --> REN --> OUT
    FONT --> LAY
    FONT --> OUT
    IMG --> LAY
    IMG --> OUT
    DOM --> XP
    CTX --> HTML & CSS & STY & LAY & REN
    APP --> CTX
    APP --> OUT
    APP --> DOM
```

Cada camada só conhece as que estão abaixo dela. Layout e Render não sabem
que existe uma janela; o Output não sabe de onde veio a display list.

## Módulos

| Módulo | Header | O que faz | Tipo central |
|---|---|---|---|
| Base | (interno) | Arena, vetor, lista, string builder | `tbox_arena` |
| HTML Parser | `html_parser.h` | Tokeniza e constrói uma árvore HTML5 tolerante a erro; API de mutação do DOM | `tbox_html_node`, `tbox_html_document` |
| XPath | `html_xpath.h` | Consultas XPath sobre o DOM | `tbox_xpath_query` |
| CSS Parser | `css_parser.h` | Tokeniza e parseia CSS em regras e declarações (`@media`, `@font-face`) | `tbox_css_stylesheet` |
| CSS Selector | `css_selector.h` | Compila e casa seletores contra o DOM; consultas tipo `querySelectorAll` | `tbox_css_selector_query` |
| CSS Cascade | `css_cascade.h` | Decide, por propriedade, qual declaração vence (origem, `!important`, especificidade, ordem); cores | `tbox_css_computed_style` |
| Style | `style.h` | Converte o resultado textual da cascata em valores tipados: unidades, herança, shorthands, `calc()`, `var()` | `tbox_style`, `tbox_style_table` |
| Fonte / Texto | `font.h` | Descoberta de fontes (Fontconfig ou embutida), métricas, medição, rasterização de glifos, cache de faces | `tbox_font_face_cache` |
| Imagens | `image.h` | Carrega e guarda imagens decodificadas | `tbox_image_cache` |
| Layout Tree | `layout.h` | Constrói a árvore de caixas e resolve a geometria (bloco, inline, posicionamento, tabelas, flexbox) | `tbox_layout_box` |
| Render Pipeline | `render.h` | Percorre as caixas em ordem de pintura e gera comandos de desenho independentes de plataforma | `tbox_display_list` |
| Output Display | `output.h` | Rasterizador em software, escrita de PNG e rastreamento de dano (repintura parcial) | `tbox_raster_*`, `tbox_damage_tracker` |
| Orchestration | `context.h` | Dona do documento e do pipeline de cômputo; hit-test, eventos, foco, formulários, rolagem | `tbox_context` |
| Application | `app.h` | Janela, loop de eventos e apresentação; o ponto de entrada mais simples | `tbox_app` |

Há duas formas de usar a biblioteca:

- **`tbox_app`** — abre uma janela (hoje Linux/Wayland), traduz eventos e
  decide quando redesenhar. É o caminho de quem quer uma aplicação.
- **`tbox_context`** — o mesmo pipeline sem janela. Você fornece o tamanho do
  viewport, recebe uma display list e a rasteriza onde quiser (buffer, PNG,
  outro backend). É o caminho para render headless, testes e integração com
  outro sistema de janelas.

## Um frame, passo a passo

```mermaid
sequenceDiagram
    participant App as Aplicação (seu código)
    participant TA as tbox_app
    participant B as Backend de janela
    participant C as tbox_context
    participant D as Damage tracker
    App->>TA: tbox_app_step()
    TA->>B: poll() / take_event()
    B-->>TA: eventos (clique, tecla, texto, scroll)
    TA->>C: dispatch_click / dispatch_key / update_hover ...
    C-->>App: handlers (on_click, on_input, on_submit...)
    App->>App: muta o DOM (set_attribute, set_text_content)
    C-->>TA: true = algo visível mudou
    alt frame sujo
        TA->>C: run_frame(largura, altura)
        Note over C: cascata + estilo → layout → display list
        C-->>TA: tbox_display_list
        TA->>B: present(lista)
        B->>D: diff com o frame anterior
        D-->>B: regiões danificadas
        B->>B: rasteriza só as regiões e envia ao compositor
    end
```

Detalhes de quando um frame é considerado sujo e de como a repintura parcial
funciona estão em [Atualização da tela](05-atualizacao-da-tela.md).

## Como os dados se transformam

```mermaid
flowchart LR
    A["texto HTML"] -->|tbox_html_parse| B["tbox_html_node<br/>(DOM, vida do documento)"]
    C["texto CSS<br/>+ #60;style#62; + style=#34;#34;<br/>+ folha user-agent"] -->|tbox_css_parse| D["tbox_css_stylesheet"]
    B --> E["tbox_css_computed_style<br/>(pares texto por nó)"]
    D --> E
    E -->|Style| F["tbox_style_table<br/>(valores tipados por nó)"]
    F -->|Layout| G["tbox_layout_box<br/>(retângulos + runs de texto)"]
    G -->|Render| H["tbox_display_list<br/>(FILL_RECT, TEXT_RUN, IMAGE, ...)"]
    H -->|Output| I["pixels XRGB8888"]
```

Pontos que ajudam a entender o resto da documentação:

- **A cascata trabalha com texto.** Quem transforma `2em` em pixels, herda
  `color` do pai e expande `margin: 1px 2px` é a camada Style.
- **Percentuais ficam para o Layout**, porque dependem do containing block.
- **Texto inline não vira caixa.** Um `<b>` dentro de um `<p>` contribui
  *runs* de texto (com o próprio estilo) para a caixa do bloco. Conteúdo
  inline solto num container de bloco ganha uma caixa anônima (`node == NULL`).
- **A display list é a fronteira com a plataforma.** O mesmo resultado serve
  à janela Wayland, ao PNG headless e a qualquer backend futuro.

## Quem é dono de quê

```mermaid
flowchart TB
    subgraph APPLIFE ["Vida da aplicação (você cria e destrói)"]
        FC[tbox_font_face_cache]
        IC[tbox_image_cache]
    end
    subgraph CTXLIFE ["Vida do contexto (tbox_context_open → close)"]
        DOC[tbox_html_document<br/>arena do DOM]
        SS2[stylesheets: autor, #60;style#62;, UA]
        H[handlers registrados]
    end
    subgraph FRAMELIFE ["Vida de um frame (frame_arena, resetada a cada run_frame)"]
        ST[tbox_style_table]
        LB[tbox_layout_box]
        DL[tbox_display_list]
    end
    CTXN[tbox_context] --> DOC & SS2 & H
    CTXN --> ST & LB & DL
    CTXN -. empresta .-> FC & IC
```

- Com `tbox_app`, os caches de fonte e imagem são criados e destruídos por
  ele; você não precisa se preocupar.
- Com `tbox_context` direto, você cria os caches, passa ao contexto
  (emprestados) e os destrói depois de fechar o contexto.
- Nada produzido por um frame sobrevive ao próximo `tbox_context_run_frame`.
  Ponteiros para caixas de layout ou ops da display list não devem ser
  guardados entre frames.

Mais em [Memória e concorrência](13-memoria-e-concorrencia.md).

## Fronteira com a plataforma

```mermaid
flowchart LR
    APP2[tbox_app] -->|tbox_window_backend.h<br/>interface privada| WB{backend}
    WB --> W[Wayland<br/>tbox_backend_wayland.c]
    WB -.futuro.-> WIN[Windows]
    WB -.futuro.-> MAC[macOS]
    W -->|tbox_input_event<br/>clique, arrasto, scroll,<br/>tecla lógica, texto, colar| APP2
    APP2 -->|display list| W
```

O backend abre a janela, entrega eventos já traduzidos para teclas lógicas
(`tbox_key`) e texto UTF-8, cuida do clipboard e do cursor, e apresenta a
display list. Foco, ativação de botões e edição de texto são decididos no
`tbox_context`, não no backend. Um backend novo implementa o mesmo contrato
sem mudar layout, render nem contexto. Ver
[Plataformas e build](11-plataformas-e-build.md).

## Princípios de projeto

- **C puro, sem runtime.** Nenhuma dependência obrigatória além do FreeType
  (baixado pelo CMake). Fontconfig e Wayland são opcionais.
- **Tolerante a entrada ruim.** HTML e CSS malformados nunca fazem o parser
  falhar; o que não é entendido é ignorado.
- **Testável sem janela.** O pipeline inteiro roda headless; nos testes, a
  fonte embutida (Liberation Sans) dá o mesmo resultado em qualquer máquina.
- **API pequena e no formato de um futuro motor de script.** Eventos são
  registrados por seletor CSS; o documento é mutado por funções com
  argumentos simples.
