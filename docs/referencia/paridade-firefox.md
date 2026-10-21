# Referência — paridade visual com o Firefox

> - **Escopo:** comparação visual dos 114 fixtures medidos de `tests/assets/`, com métricas por arquivo, diferenças observadas e limites da medição.
> - **Código relacionado:** `tests/assets/`, `tests/tbox_cmp.cpp`
> - **Veja também:** [Fixtures](fixtures.md), [Ferramentas e testes](../12-ferramentas-e-testes.md), [Débito técnico](../dev/debito-tecnico.md)

## Verificação dirigida de SVG — 2026-10-21

Complemento ao levantamento histórico abaixo, sem reexecutar os outros
fixtures. Build Debug, FreeType 2.15.0 do sistema, Firefox 158.0 headless
(build 20261013210526), fontes Arial/sans-serif do sistema, 800×600:

| Fixture | Veredito | SSIM | Checagens locais reprovadas |
|---|---|---:|---:|
| `svg-basic-shapes` | EQUAL | 0,9816 | 0 |
| `svg-compositing` | EQUAL | 0,9825 | 0 |
| `svg-paths` | EQUAL | 0,9812 | 0 |
| `svg-strokes` | EQUAL | 0,9883 | 0 |
| `svg-style-layout` | EQUAL | 0,9815 | 0 |
| `svg-symbols` | EQUAL | 0,9737 | 0 |

Capturas revisadas visualmente: formas, curvas, símbolos, medidor e
sobreposições presentes. `EQUAL` é tolerância do comparador, não identidade
pixel a pixel; SSIM é diagnóstico. Reprodução:

```sh
build/tests/tbox_cmp --output /tmp/tbox-svg-cmp tests/assets svg-basic-shapes svg-compositing svg-paths svg-strokes svg-style-layout svg-symbols
build/tests/tbox_cmp --output /tmp/tbox-svg-explorer --size 1440x900 example/file-explorer
```

A página completa continua DIFFERENT (SSIM 0,8687; 127 checagens locais),
com diferenças de layout e texto HTML além dos vetores; veja
[compatibilidade do explorer](file-explorer-compatibilidade.md).
Os 25 grupos CTest passaram também com ASan/UBSan/LeakSanitizer.

## Medição e ambiente

Esta é a medição anterior às implementações de placeholder, `:focus-visible`
e posicionados inline e à inclusão de `forms-placeholder`. As métricas
abaixo permanecem um retrato daquela árvore, sem extrapolação às mudanças.

Revalidação de **2026-10-19**, com os **114 HTML reorganizados por
categoria** existentes em `tests/assets/`. Todos foram novamente capturados
no Firefox e renderizados pela tbox; os 114 pares foram revisados visualmente.
Não houve alteração dos fixtures, do comparador ou da biblioteca nesta
revalidação.

| Item | Valor desta execução |
|---|---|
| Referência | Mozilla Firefox 158.0, `/usr/bin/firefox`, headless |
| Renderizador | `tbox_app_screenshot_from_files`, chamado pelo `tbox_cmp` |
| Área capturada | 800×600 pixels em ambos |
| Build | `build/`, Debug, sanitizers desligados |
| Integrações | Fontconfig 2.18.3, HarfBuzz 14.6.0 e OpenCV 5.0.0 habilitados |
| Fontes genéricas do ambiente | DejaVu Serif, DejaVu Sans e DejaVu Sans Mono (`fc-match`) |
| Árvore medida | Base Git `2270a4e`, incluindo as alterações locais já presentes |
| Recursos | Arquivos locais via `file://`, com perfil temporário próprio do Firefox |

Os números dependem dessas fontes, desse navegador e dos fixtures atuais.
Eles não são diretamente comparáveis aos da antiga coleção de 164 HTML:
a reorganização fundiu casos e mudou o espaço ocupado pelos exemplos.

## Resultado

**114 comparações concluídas: 46 EQUAL, 68 DIFFERENT e zero falhas
operacionais.** Os 25 grupos de testes do `ctest` também passaram.

| Área | Fixtures | EQUAL | DIFFERENT |
|---|---:|---:|---:|
| HTML | 10 | 3 | 7 |
| Cores | 5 | 1 | 4 |
| Caixa | 11 | 10 | 1 |
| Texto e fontes | 22 | 8 | 14 |
| Listas e conteúdo gerado | 5 | 3 | 2 |
| Seletores | 5 | 1 | 4 |
| Cascata e at-rules | 7 | 3 | 4 |
| Unidades e funções | 4 | 1 | 3 |
| Fundos | 7 | 4 | 3 |
| Layout | 14 | 7 | 7 |
| Efeitos | 10 | 4 | 6 |
| Animação | 2 | 0 | 2 |
| Interação | 2 | 1 | 1 |
| Formulários | 7 | 0 | 7 |
| Imagens | 3 | 0 | 3 |
| **Total** | **114** | **46** | **68** |

### Como interpretar

`EQUAL` significa que o par passou nas tolerâncias do comparador, não que
os pixels ou todos os comportamentos coincidem. As checagens de conteúdo
e bordas permitem proximidade de 8px e exigem cobertura mínima de 80%;
checagens de cor também participam do veredito. O **SSIM é diagnóstico**:
não há um limiar de SSIM que decida EQUAL/DIFFERENT.

Dois exemplos mostram por que as capturas precisam ser revisadas:
`html-tables` passa com SSIM 0,5642, enquanto `text-bidi` fica DIFFERENT
com 0,9586. `layout-multicol` passa, mesmo com bordas indevidas por linha.
Assim, os 46 EQUAL não representam uma porcentagem de conformidade CSS/HTML.

A categoria de cada fixture descreve a diferença principal observada no
recurso mostrado. **Recurso equivalente** admite as diferenças comuns de
canvas, texto e rasterização descritas abaixo; não significa igualdade da
página inteira. As categorias são qualitativas e não substituem as métricas.

| Categoria | Fixtures | Interpretação |
|---|---:|---|
| Recurso equivalente | 46 | O recurso mostrado tem desenho comparável; persistem diferenças comuns de texto, rasterização ou canvas. |
| Fonte e métricas | 21 | Larguras ou alturas de linha mudam quebras e posições. |
| Escolha documentada | 16 | Margens, fonte de controles ou barras seguem decisões já registradas. |
| Tema dos controles | 10 | Aparência própria, dimensões ou partes de controles diferentes. |
| Referência e ambiente | 7 | Diferença de aceitação/suporte ou do ambiente de captura; não determina qual implementação está conforme. |
| Tempo ou interação | 2 | O instante de captura difere ou o comportamento exige entrada. |
| Débito conhecido | 3 | Diferença já descrita no débito técnico, incluindo quebra de textarea. |
| Defeito observado | 9 | Desvio visual concreto da tbox, ainda aberto; ver lista abaixo. |

## Diferenças que se repetem

- **Fundo da página e canvas:** o fundo da tbox termina com a caixa pintada,
  deixando faixas brancas nas bordas ou abaixo do conteúdo; no Firefox o
  fundo da página cobre a captura. Aparece também no fixture com fundo em
  `:root`. O débito de propagação do fundo não deve ser confundido com
  falha de cada recurso testado sobre ele.
- **Margem do primeiro filho:** sem colapso pai/filho, títulos e parágrafos
  deslocam o conjunto cerca de 6–20px. As margens da folha UA também são
  fixas em px; fontes grandes deixam `html-headings` e
  `css-style-attribute` mais compactos. São escolhas documentadas em
  [layout](../08-layout-e-pintura.md#o-que-está-implementado) e
  [decisões](../dev/decisoes.md).
- **Métricas de texto:** linhas normais de fontes pequenas ficam cerca de
  1px mais baixas na tbox. Pequenas diferenças de largura mudam quebras,
  alturas e posições acumuladas. Em `effects-compositing`, por exemplo,
  o título ocupa uma linha na tbox e duas no Firefox.
- **Controles e rolagem:** os controles usam o tema da tbox e normalmente
  herdam a fonte do documento. A barra padrão fina sobrepõe o conteúdo;
  na referência, várias caixas reservam calha, mesmo sem barra visível.
  Isso altera a largura útil e as quebras.
- **Rasterização:** há diferenças pequenas em glifos, curvas, tracejados,
  marcas de ênfase e sombras. O blur de `box-shadow`, em particular, é
  visivelmente mais largo e escuro na tbox e continua como débito.

## Defeitos encontrados

A revisão confirma **oito tipos de defeito em nove fixtures** classificados
como Defeito observado, já registrados no
[débito técnico](../dev/debito-tecnico.md#defeitos-da-medição-de-paridade-de-2026-10-19):

1. **Floats em item de grid** — `layout-floats`: as duas primeiras células
   deslocam os floats e comprimem o texto; há sobreposição com o painel
   seguinte. Os demais casos do fixture não apresentam o mesmo desvio.
2. **Bordas na fragmentação multicoluna** — `layout-multicol`: o bloco
   dividido recebe bordas em torno de cada linha, além das do fragmento.
   É um defeito visível apesar do EQUAL; esta rodada não determina quando
   ele foi introduzido.
3. **Altura de fundo/borda de inline** — `effects-masks-and-reflection`:
   o span partido usa a altura de linha de 36px para pintar suas caixas.
4. **Alinhamento vertical de células** — `html-tables-layout` e
   `layout-block-in-inline`: o conteúdo fica no topo onde o Firefox o
   centraliza. A folha UA da tbox não define `vertical-align: middle`
   para `td`/`th`.
5. **Texto do botão de tamanho fixo** — `interaction-cursor-and-pointer`:
   fica no canto superior esquerdo em vez do centro.
6. **Marcador ampliado não aumenta a linha** — `lists-markers`: o marcador
   serifado de 24px e o bullet maior deixam as linhas apertadas.
7. **Inline deslocado não aumenta a linha** — `text-spacing`:
   `vertical-align: +6px`/`-0.4em` não expande a caixa como na referência.
8. **Ajuste de x-height** — `text-font-size`: `font-size-adjust` produz
   escalas diferentes das do Firefox entre as famílias. A captura confirma
   o desvio, mas não isola a causa interna.

Outro desvio já conhecido aparece em `forms-textarea-wrapping`: palavras
longas são cortadas sem aproveitar oportunidades como `/`. Uma comparação
complementar em cópia temporária igualou a família (`DejaVu Sans Mono`),
line-height, padding, borda e largura dos campos. A URL continuou quebrando
em posições diferentes, embora essa prova tenha recebido **EQUAL**
(SSIM 0,8433). Portanto, o problema não pode ser atribuído apenas à fonte
padrão do controle. Esse caso está na categoria Débito conhecido e em
[quebra de linha do textarea](../dev/debito-tecnico.md#quebra-de-linha-do-textarea).

## Limites da referência e da cobertura

Estas observações valem para o Firefox 158.0 **neste ambiente headless**:

- Os painéis de `backdrop-filter` não filtram o fundo na captura de
  `effects-drop-shadow-and-backdrop`; isso não estabelece o comportamento
  de uma janela com composição gráfica.
- A estrela de máscara `url()` não aparece ao carregar o fixture via
  `file://`. O teste não isola a política de carregamento/CORS de suporte
  ao desenho da máscara; as máscaras de gradiente aparecem.
- `initial-letter`, `hanging-punctuation` e `-webkit-box-reflect` não
  produzem o mesmo resultado da tbox nos exemplos. Não há referência
  equivalente para validar esses resultados específicos.
- `@supports` diverge em recursos como `preserve-3d` e `orphans`. As media
  features `hover`/`pointer` e `scripting` também dependem das capacidades
  anunciadas por cada ambiente.
- `opacity: calc(50% + .25)` é rejeitada pela referência e aceita pela
  tbox. Esta medição registra a diferença, sem concluir conformidade.
- `text-overflow` mistura formas de clamp: um painel omite
  `-webkit-box-orient: vertical`, e o texto do painel de três linhas já
  cabe no limite. Eles não validam isoladamente o suporte ao clamp. O
  painel de duas linhas com padding ainda mostra recorte diferente.
- `::-tbox-scrollbar-*` é específico da tbox. Os fixtures de partes e
  pickers misturam seletores com suporte diferente; capturas dos selects
  fechados não verificam a lista ou seu tema.

A rodada usa **uma captura inicial por fixture**, sem mouse, teclado ou
sequência temporal sincronizada. Não valida navegação de links/mapas,
foco/caret, seleção, edição dentro de ligaduras, abertura de popups,
rolagem/snap/overscroll, arrasto de resize, nem evolução de transições e
keyframes. Mesmo que esses recursos sejam suportados pela biblioteca,
esta rodada só confirma sua aparência no estado capturado.

## Como repetir e consultar as evidências

Com as dependências instaladas e Firefox no caminho indicado:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DTBOX_ENABLE_FONTCONFIG=ON -DTBOX_ENABLE_HARFBUZZ=ON \
  -DTBOX_ENABLE_SANITIZERS=OFF
cmake --build build --target tbox_cmp tbox_tests -j 4
build/tests/tbox_cmp --firefox /usr/bin/firefox --size 800x600 \
  --output /tmp/tbox-paridade-20261019 tests/assets \
  > /tmp/tbox-paridade-20261019.log 2>&1
ctest --test-dir build --output-on-failure
```

A ferramenta não faz parte do `ctest`. Seu código de saída indica erro
operacional, não DIFFERENT. O log deve terminar com:

```text
Summary: 114 fixtures | 46 EQUAL | 68 DIFFERENT | 0 operational failures
```

As evidências desta execução estão em `/tmp/tbox-paridade-20261019/`:
`<fixture>_firefox.png`, `<fixture>_render.png` e `<fixture>_diff.png`
(342 PNGs de comparação), além de `index.html` para consulta lado a lado,
`resultados.csv` com métricas e `assets-sha256.txt` com hashes dos insumos.
O índice, o CSV e o manifesto são auxiliares desta revisão; o comando
`tbox_cmp` acima gera as capturas e o log.
O log integral é `/tmp/tbox-paridade-20261019.log`. A prova adicional do
textarea está em `/tmp/tbox-paridade-probes/` e seu HTML em
`/tmp/tbox-paridade-probes-assets/`. Esses arquivos são locais e temporários;
as métricas e conclusões permanentes estão nas tabelas deste documento.
Não há artifact publicado correspondente a esta rodada.

## Por fixture

Cada HTML atual aparece uma vez, agrupado como no mapa de
[fixtures](fixtures.md). **Falhas locais** é o contador diagnóstico de
checagens reprovadas do comparador; não são falhas operacionais nem uma
contagem de bugs. SSIM e contador foram transcritos do log da nova execução.
As observações referem-se ao recurso do fixture e complementam as
diferenças comuns e os limites descritos acima.

### HTML

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `css-style-attribute` | DIFFERENT | 0,7307 | 24 | Escolha documentada | Estilos inline aplicados igual, mas a página é bem mais compacta: as margens da folha UA são px fixos na tbox, e o p de 40px e os títulos perdem as margens em em do Firefox. |
| `html-base-href` | DIFFERENT | 0,9119 | 9 | Escolha documentada | CSS e imagem vêm de base-demo/ nos dois; tudo 20px mais baixo porque a margem do h1 não colapsa com a do body. |
| `html-headings` | DIFFERENT | 0,7789 | 28 | Escolha documentada | Tamanhos de h1–h6 iguais; o h1 de 60px fica com a margem de um h1 normal (margens da folha UA em px fixos), e o resto sobe 40px. |
| `html-images` | EQUAL | 0,9150 | 0 | Escolha documentada | Igual; deslocamento de 6px no topo (margem do h1 com a do body). |
| `html-links` | EQUAL | 0,9375 | 0 | Tema dos controles | Links e sublinhados equivalentes na captura; botão com fonte e tema da tbox. Destinos e navegação não foram exercitados. |
| `html-paragraphs` | DIFFERENT | 0,8413 | 3 | Escolha documentada | Igual; tudo 8px mais baixo (margem do primeiro p com a do body). |
| `html-picture-and-map` | DIFFERENT | 0,8380 | 16 | Escolha documentada | As duas imagens aparecem como esperado; conjunto cerca de 20px abaixo pela margem inicial. As regiões de map/area não são verificáveis nesta captura. |
| `html-tables` | EQUAL | 0,5642 | 0 | Escolha documentada | Larguras de coluna e bordas iguais; deslocamento de 8px no topo e linhas 1px mais altas. |
| `html-tables-layout` | DIFFERENT | 0,7531 | 2 | Defeito observado | Colunas e bordas semelhantes; na tabela fixed e nas células de alturas diferentes, o texto fica no topo em vez de centralizado verticalmente. |
| `html-text-formatting` | DIFFERENT | 0,8167 | 1 | Escolha documentada | Formatação, aspas e recuos comparáveis; conjunto cerca de 8px abaixo pela margem inicial e pequenas diferenças nas métricas do texto. |

### Cores

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `colors-hsl` | DIFFERENT | 0,8241 | 1 | Fonte e métricas | Cores iguais; o texto serifado é um pouco mais estreito na tbox e as linhas quebram em outros pontos. |
| `colors-mix-and-color-function` | DIFFERENT | 0,7475 | 2 | Fonte e métricas | Todas as cores iguais; textos pequenos com altura de linha menor sobem o conteúdo até 9px. |
| `colors-modern` | EQUAL | 0,8247 | 0 | Recurso equivalente | Igual. |
| `colors-names-and-formats` | DIFFERENT | 0,6582 | 1 | Fonte e métricas | Cores iguais; o primeiro bloco desce 4px (margem do body não colapsa) e as faixas com texto serifado negrito ficam 2 a 4px mais baixas. |
| `colors-rgb-and-hex` | DIFFERENT | 0,5666 | 1 | Fonte e métricas | Cores iguais; deslocamento de 3px no topo e altura de linha serifada levemente diferente. |

### Caixa

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `box-aspect-ratio` | EQUAL | 0,8957 | 0 | Fonte e métricas | Proporções explícitas coincidem. A caixa dependente do conteúdo fica mais baixa na tbox por diferenças na altura das linhas. |
| `box-border-padding-margin` | EQUAL | 0,7024 | 0 | Escolha documentada | Caixas iguais; tudo 6px mais baixo porque a margem do h1 não colapsa com a do body. |
| `box-border-styles` | EQUAL | 0,8624 | 0 | Recurso equivalente | Igual; pontos da borda dotted com espaçamento um pouco diferente. |
| `box-border-styles-3d` | EQUAL | 0,8954 | 0 | Recurso equivalente | Igual. |
| `box-border-styles-rounded` | EQUAL | 0,9227 | 0 | Recurso equivalente | Igual; tracejado e pontilhado com fase um pouco diferente nas curvas. |
| `box-logical-borders` | EQUAL | 0,8234 | 0 | Recurso equivalente | Igual. |
| `box-logical-sizes` | EQUAL | 0,8602 | 0 | Recurso equivalente | Igual. |
| `box-radius` | EQUAL | 0,9006 | 0 | Recurso equivalente | Igual. |
| `box-radius-elliptical` | EQUAL | 0,9226 | 0 | Recurso equivalente | Igual. |
| `box-sizing` | EQUAL | 0,8662 | 0 | Recurso equivalente | Igual; a tbox desenha a barra fina na caixa com max-height, o Firefox headless não mostra barra. |
| `box-sizing-keywords` | DIFFERENT | 0,8029 | 2 | Fonte e métricas | Larguras min-content, max-content e fit-content comparáveis; linhas mais baixas na tbox reduzem a altura das caixas e deslocam os exemplos seguintes. |

### Texto e fontes

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `text-alignment` | EQUAL | 0,8186 | 0 | Fonte e métricas | Os modos de alinhamento são iguais; o texto de 15px é um pouco mais estreito na tbox, e os parágrafos justificados quebram em outra palavra. |
| `text-bidi` | DIFFERENT | 0,9586 | 1 | Recurso equivalente | Ordem visual, espelhamento e casos de bdo/bdi/isolate/plaintext comparáveis neste conjunto; pequenas diferenças de rasterização. |
| `text-decoration` | DIFFERENT | 0,8178 | 2 | Fonte e métricas | Linhas, cores, estilos e posições iguais; espessura e fase de dotted/dashed/wavy com pequenas diferenças. |
| `text-direction` | DIFFERENT | 0,8449 | 3 | Recurso equivalente | Direção, lados lógicos e ordem flex equivalentes no recorte. Persistem diferenças comuns de texto e canvas. |
| `text-emphasis` | EQUAL | 0,8213 | 0 | Fonte e métricas | Posições e herança iguais. O Firefox desenha sesame com o glifo da fonte de fallback (quadrados), a tbox com o glifo certo; marcas abertas um pouco maiores na tbox. |
| `text-font-face-and-opentype` | EQUAL | 0,8875 | 0 | Recurso equivalente | Igual. |
| `text-font-size` | DIFFERENT | 0,7977 | 5 | Defeito observado | Palavras-chave e atalho font próximos. O ajuste de x-height com font-size-adjust difere entre as famílias, sobretudo sans e serif; causa ainda não isolada. |
| `text-font-variants` | DIFFERENT | 0,7565 | 10 | Fonte e métricas | small-caps sintetizado, font-variant-position e font-synthesis iguais; o texto em versalete é mais estreito na tbox, e uma linha deixa de quebrar. |
| `text-font-weight-and-stretch` | EQUAL | 0,8555 | 0 | Fonte e métricas | Pesos, estilos e stretch iguais; linhas 1px mais baixas. |
| `text-hyphens-and-balance` | DIFFERENT | 0,7866 | 1 | Fonte e métricas | &shy;, hyphens, hyphenate-character e balance iguais; com o texto um pouco mais estreito, a tbox hifeniza "extra-" no fim da linha onde o Firefox passa a palavra inteira. |
| `text-indent` | DIFFERENT | 0,8342 | 2 | Fonte e métricas | Recuos iguais; uma quebra muda na última caixa. |
| `text-initial-letter` | DIFFERENT | 0,8140 | 1 | Referência e ambiente | first-letter flutuante comparável. initial-letter e hanging-punctuation têm resultados diferentes nos painéis; a referência desta execução não demonstra equivalência desses recursos. |
| `text-overflow` | DIFFERENT | 0,6627 | 2 | Referência e ambiente | Reticências/strings comparáveis, com pequeno espaçamento diferente. O clamp de duas linhas com padding deixa texto adicional visível no Firefox; o caso de uma linha omite box-orient e diverge. O painel de três linhas já cabe no limite e não comprova suporte ao line-clamp sem prefixo. |
| `text-ruby` | DIFFERENT | 0,8906 | 18 | Escolha documentada | Ruby igual; tudo 20px mais baixo (margem do h1 com a do body). |
| `text-shaping-latin` | EQUAL | 0,8800 | 0 | Recurso equivalente | Igual. |
| `text-shaping-scripts` | EQUAL | 0,8875 | 0 | Tema dos controles | Formas árabes, ZWNJ e marcas hebraicas comparáveis. Campos com tema próprio; posicionamento/edição do cursor dentro de ligaduras não foi exercitado. |
| `text-spacing` | DIFFERENT | 0,8812 | 1 | Defeito observado | line-height, letter-spacing e word-spacing comparáveis. Os deslocamentos +6px e -0.4em não expandem a caixa de linha como no Firefox, aproximando as duas últimas linhas. |
| `text-stroke` | EQUAL | 0,9737 | 0 | Recurso equivalente | Igual. |
| `text-transform-and-shadow` | DIFFERENT | 0,8852 | 1 | Recurso equivalente | Igual; as sombras com blur saem um pouco mais densas. |
| `text-vertical-writing` | EQUAL | 0,9458 | 0 | Recurso equivalente | Igual. |
| `text-white-space` | DIFFERENT | 0,6155 | 2 | Fonte e métricas | Todos os modos de espaço e tab-size iguais. O título negrito cabe numa linha a menos na tbox, e o texto monoespaçado é um pouco mais largo, quebrando uma palavra antes. |
| `text-wrapping` | DIFFERENT | 0,6339 | 4 | Fonte e métricas | overflow-wrap, word-break e line-break iguais; algumas quebras dentro de palavra caem um caractere antes ou depois. |

### Listas e conteúdo gerado

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `generated-content` | EQUAL | 0,8836 | 0 | Recurso equivalente | Igual. |
| `generated-counters` | EQUAL | 0,9098 | 0 | Recurso equivalente | Igual. |
| `lists-markers` | DIFFERENT | 0,7892 | 12 | Defeito observado | Marcadores de imagem, ::marker colorido e content iguais. Defeito: um ::marker maior (o 1. serifado de 24px, o bullet ampliado) não aumenta a altura da linha na tbox, e as linhas ficam apertadas. O marcador com content: "→ " sai com mais espaço antes do texto. |
| `lists-position-and-list-item` | DIFFERENT | 0,7336 | 1 | Fonte e métricas | Posições de marcador iguais; o quadrado de square é maior na tbox e as linhas de 14px têm 1px a menos. |
| `lists-style-type` | EQUAL | 0,8111 | 0 | Fonte e métricas | Todos os tipos iguais; quadrado maior na tbox e linhas 1px mais baixas, que somam 20px na lista de dez itens. |

### Seletores

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `selectors-attributes-and-lang` | DIFFERENT | 0,7997 | 1 | Recurso equivalente | As mesmas bordas e cores selecionam os casos de atributos, modificadores i/s e idioma. Persistem as diferenças comuns de texto e canvas. |
| `selectors-combinators-and-not` | DIFFERENT | 0,8104 | 1 | Recurso equivalente | As mesmas linhas recebem os estilos de ~ e :not(). Persistem as diferenças comuns de texto e canvas. |
| `selectors-states` | DIFFERENT | 0,6969 | 1 | Tema dos controles | required/optional, enabled/disabled e link/any-link exibem os mesmos estados iniciais. Campos mais altos na tbox; hover e focus-within não foram acionados. |
| `selectors-structural` | EQUAL | 0,7568 | 0 | Recurso equivalente | Igual. |
| `selectors-type-and-only` | DIFFERENT | 0,8620 | 1 | Recurso equivalente | Os casos de root, posições por tipo, only-child e empty recebem os mesmos estilos; há pequenas diferenças de dimensões e no preenchimento do canvas. |

### Cascata e at-rules

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `cascade-container-queries` | EQUAL | 0,9986 | 0 | Recurso equivalente | Igual. |
| `cascade-custom-properties` | EQUAL | 0,8463 | 0 | Fonte e métricas | Cascata igual; alturas de linha um pouco menores na tbox deslocam o resto em até 5px. |
| `cascade-keywords` | DIFFERENT | 0,5903 | 1 | Fonte e métricas | Os quatro valores e all resolvem igual; alturas de linha menores deslocam o fim em 7px. |
| `cascade-layers` | DIFFERENT | 0,7490 | 1 | Recurso equivalente | Igual. |
| `cascade-link-and-import` | DIFFERENT | 0,7877 | 3 | Referência e ambiente | CSS externo, imports aninhados e media por largura funcionam no recorte. hover + pointer: fine e scripting: none ficam verdes na tbox e vermelhos no Firefox headless. |
| `cascade-scope-and-namespace` | EQUAL | 0,9568 | 0 | Recurso equivalente | Igual. |
| `cascade-supports` | DIFFERENT | 0,7338 | 2 | Referência e ambiente | As condições comuns exibem os mesmos resultados. preserve-3d e orphans produzem respostas diferentes em @supports; aceitar sintaxe não demonstra efeito visual. |

### Unidades e funções

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `units-calc-min-max-clamp` | DIFFERENT | 0,6724 | 14 | Recurso equivalente | Todos os valores iguais; textos de 12px com linhas menores sobem a página alguns px. |
| `units-lengths` | EQUAL | 0,8516 | 0 | Recurso equivalente | Igual. |
| `units-math-functions` | DIFFERENT | 0,6522 | 17 | Referência e ambiente | Barras e pares numéricos comparáveis. opacity: calc(50% + .25) fica opaca no Firefox e em 75% na tbox: divergência de aceitação da expressão, sem conclusão de conformidade por esta captura. |
| `units-math-trig` | DIFFERENT | 0,7221 | 14 | Recurso equivalente | Igual; linhas de 12px um pouco menores. |

### Fundos

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `backgrounds-blend-mode` | EQUAL | 0,8950 | 0 | Recurso equivalente | Os 16 modos coincidem. |
| `backgrounds-border-image` | EQUAL | 0,9775 | 0 | Recurso equivalente | Igual, inclusive slice, round, space e outset. |
| `backgrounds-clip-and-origin` | DIFFERENT | 0,7921 | 2 | Recurso equivalente | Recortes e origens de fundo equivalentes no recurso mostrado; há diferenças de rasterização, altura de texto e fundo do canvas. O veredito não isola uma causa única. |
| `backgrounds-gradients` | EQUAL | 0,8449 | 0 | Recurso equivalente | Igual. |
| `backgrounds-gradients-conic` | DIFFERENT | 0,7173 | 6 | Escolha documentada | Gradientes idênticos; tudo 9px mais baixo porque a margem do h1 não colapsa com a do body. |
| `backgrounds-image` | DIFFERENT | 0,6908 | 1 | Débito conhecido | Imagens, cover/contain e repetição comparáveis. No painel de attachment: fixed, diferem a quebra do texto, a barra de rolagem e a fase vertical do padrão. |
| `backgrounds-layers` | EQUAL | 0,8518 | 0 | Recurso equivalente | Igual. |

### Layout

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `layout-alignment` | EQUAL | 0,9106 | 0 | Recurso equivalente | Igual. |
| `layout-block-in-inline` | DIFFERENT | 0,8723 | 3 | Defeito observado | Os blocos em p, h3 e li aparecem nas mesmas posições relativas. Na tabela, a coluna é mais larga e a célula vizinha alinha no topo em vez do meio. |
| `layout-flexbox` | DIFFERENT | 0,7227 | 1 | Escolha documentada | Distribuição, wrap, baseline, reversão e layout de aplicação comparáveis. O conjunto começa cerca de 7px abaixo, pela margem inicial. |
| `layout-floats` | DIFFERENT | 0,7520 | 29 | Defeito observado | Nas duas primeiras células de grid, floats descem e o texto se espreme ou se sobrepõe ao painel seguinte. clear, flow-root e a forma circular ficam próximos da referência. |
| `layout-grid-areas-and-alignment` | EQUAL | 0,8207 | 0 | Recurso equivalente | Igual. |
| `layout-grid-tracks` | EQUAL | 0,8436 | 0 | Recurso equivalente | Igual. |
| `layout-inline-block` | DIFFERENT | 0,7414 | 1 | Escolha documentada | Igual; deslocamento de 6px no topo (margem do h2 com a do body). |
| `layout-multicol` | EQUAL | 0,9549 | 0 | Defeito observado | Contagem/largura de colunas, regras e span comparáveis. O bloco fragmentado ganha bordas indevidas em torno de cada linha. O comparador retorna EQUAL apesar desse defeito. |
| `layout-overflow` | EQUAL | 0,8934 | 0 | Escolha documentada | Recortes iguais. Na caixa com overflow-y: scroll, o Firefox reserva a calha da barra e a tbox desenha a barra fina por cima do conteúdo, por isso o texto quebra em outro ponto. |
| `layout-positioning` | DIFFERENT | 0,8751 | 1 | Fonte e métricas | Posições iniciais de sticky, absolute e relative comparáveis; uma legenda quebra a mais na tbox. A captura não verifica sticky durante a rolagem. |
| `layout-scroll-snap` | EQUAL | 0,9144 | 0 | Escolha documentada | Os painéis começam nas mesmas posições, com barras sobrepostas na tbox e calha no Firefox. Não houve gesto de rolagem para verificar snap ou stop. |
| `layout-scrollbars` | DIFFERENT | 0,8631 | 1 | Referência e ambiente | As propriedades padrão alteram os exemplos; o desenho e a calha diferem. O Firefox não reproduz as partes específicas ::-tbox-scrollbar-*. |
| `layout-scrolling` | DIFFERENT | 0,8445 | 8 | Tema dos controles | Mesmos painéis iniciais, com botões, fontes e barras diferentes. overscroll, margem/padding de rolagem e smooth não foram exercitados por entrada. |
| `layout-z-index` | EQUAL | 0,8763 | 0 | Recurso equivalente | Igual. |

### Efeitos

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `effects-box-shadow` | EQUAL | 0,8794 | 0 | Débito conhecido | Offsets, cores e spread comparáveis; o blur da tbox é mais largo, escuro e apresenta faixas. O EQUAL não elimina essa diferença. |
| `effects-clip-path-polygon` | DIFFERENT | 0,7991 | 5 | Escolha documentada | Recortes idênticos; tudo 9px mais baixo pela margem do h1 que não colapsa com a do body. |
| `effects-clip-path-shapes` | DIFFERENT | 0,7523 | 5 | Recurso equivalente | Formas e clip iguais; textos pequenos com altura de linha menor sobem a grade alguns px. |
| `effects-compositing` | DIFFERENT | 0,5803 | 30 | Fonte e métricas | Opacidade de grupo, visibility, blend e isolation iguais; o título negrito de 26px é um pouco mais estreito na tbox e cabe numa linha, e o resto sobe 30px. |
| `effects-drop-shadow-and-backdrop` | DIFFERENT | 0,7072 | 8 | Referência e ambiente | drop-shadow comparável. Nesta execução headless os painéis do Firefox mantêm o fundo sem backdrop-filter, enquanto a tbox aplica cor e blur. |
| `effects-filter` | DIFFERENT | 0,7302 | 4 | Recurso equivalente | Igual. |
| `effects-masks-and-reflection` | DIFFERENT | 0,9334 | 4 | Defeito observado | Composição das máscaras de gradiente comparável. A estrela de máscara url() não aparece no Firefox via file://, e a reflexão só aparece na tbox. No inline partido, fundo e borda usam indevidamente a altura da linha inteira. |
| `effects-motion-path` | EQUAL | 0,9833 | 0 | Recurso equivalente | Igual. |
| `effects-transform-functions` | EQUAL | 0,9863 | 0 | Recurso equivalente | Igual. |
| `effects-transform-translate` | EQUAL | 0,9221 | 0 | Recurso equivalente | Igual. |

### Animação

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `animation-keyframes` | DIFFERENT | 0,8635 | 1 | Tempo ou interação | Pulso/barra e geometria inicial próximos; entrada, cursor piscante e opacidade da mensagem estão em instantes diferentes. Não mede a evolução da animação. |
| `animation-transitions` | DIFFERENT | 0,8613 | 3 | Tempo ou interação | Exemplos em repouso semelhantes; o cartão de @starting-style fica invisível no Firefox e visível na tbox. Hover e curvas de transição não foram exercitados. |

### Interação

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `interaction-cursor-and-pointer` | DIFFERENT | 0,8273 | 3 | Defeito observado | Caixas e sobreposição aparecem, mas o texto do botão de tamanho fixo fica no canto superior esquerdo na tbox. Cursores, seleção, cliques e active não foram acionados. |
| `interaction-zoom-and-resize` | EQUAL | 0,9741 | 0 | Recurso equivalente | Escalas de zoom e dimensões iniciais comparáveis; alças de resize com desenho diferente. Não houve arrasto para testar redimensionamento. |

### Formulários

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `forms-accent-and-caret` | DIFFERENT | 0,7474 | 20 | Tema dos controles | As cores de accent-color chegam a range/progress/checkbox/radio, e meter mantém as cores semânticas. Barras e controles têm tema próprio; caret-color não foi verificado com foco. |
| `forms-checkbox-radio-and-focus` | DIFFERENT | 0,7729 | 1 | Tema dos controles | Estados marcados/desmarcados e caixas com outline aparecem. Tema, fonte, dimensões e cantos diferem; não houve navegação de foco por teclado. |
| `forms-control-parts` | DIFFERENT | 0,7405 | 13 | Tema dos controles | A tbox mostra partes estilizadas e o Firefox seu desenho de controles. O fixture mistura partes padrão e específicas; fontes e dimensões também diferem. Não testa os popups abertos. |
| `forms-grouping` | DIFFERENT | 0,7929 | 9 | Tema dos controles | Legenda atravessando a borda, details aberto/fechado e selects aparecem com estrutura comparável; campos mais altos e tema próprio. Abrir details e grupos do popup não foi testado. |
| `forms-progress-and-output` | DIFFERENT | 0,8057 | 10 | Tema dos controles | Valores e proporções de preenchimento comparáveis. A tbox usa barras retangulares mais grossas; a aparência indeterminada é apenas um frame. |
| `forms-select` | DIFFERENT | 0,8452 | 5 | Tema dos controles | Seleções iniciais iguais, com fonte, seta e dimensões diferentes. Listas fechadas: esta captura não avalia o tema de picker nem a navegação nas opções. |
| `forms-textarea-wrapping` | DIFFERENT | 0,8586 | 4 | Débito conhecido | Preserva espaços e quebras explícitas, mas a URL longa é cortada em caracteres diferentes: falta aproveitar oportunidades como /. O desvio persiste em prova complementar com fonte e caixa igualadas; também difere a fonte padrão. |

### Imagens

| Fixture | tbox_cmp | SSIM | Falhas locais | Categoria | Observação |
|---|---|---:|---:|---|---|
| `images-object-fit` | DIFFERENT | 0,6871 | 1 | Recurso equivalente | Recortes e proporções de fill/contain/cover/none/scale-down comparáveis; o bloco grande começa alguns pixels acima na tbox. |
| `images-object-position` | DIFFERENT | 0,6644 | 3 | Recurso equivalente | Enquadramentos comparáveis nos exemplos de posições e offsets; deslocamentos verticais de alguns pixels por métricas de linha. |
| `images-rendering` | DIFFERENT | 0,8874 | 6 | Recurso equivalente | O padrão ampliado distingue auto suave de pixelated/crisp-edges nos dois. Há pequenas diferenças de rasterização e as diferenças comuns de texto e canvas. |
