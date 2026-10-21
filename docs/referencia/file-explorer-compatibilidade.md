# Referência — compatibilidade do exemplo file-explorer

> - **Escopo:** recursos usados pelo template do explorador de arquivos que a tbox ainda não implementa, implementa parcialmente ou precisa corrigir.
> - **Código relacionado:** `example/file-explorer/`, `src/style/`, `src/css_selector/`, `src/layout/`, `src/context/`
> - **Veja também:** [HTML suportado](html-suportado.md), [CSS suportado](css-suportado.md), [HTML e CSS](../07-html-e-css.md), [Débito técnico](../dev/debito-tecnico.md)

Análise inicial e atualização de implementação em **2026-10-19**, sobre a
base Git `c0713ed`, dos arquivos
[index.html](../../example/file-explorer/index.html) (136 linhas) e
[styles.css](../../example/file-explorer/styles.css) (237 linhas).
O diretório no repositório é `example/file-explorer/`, no singular.

O HTML e o CSS foram preservados. O levantamento combina inventário dos
arquivos, leitura da implementação, consultas a `tbox_css_supports`, uma
prova de navegação por Tab e renderização headless em 1440×900.
Não é uma medição completa de paridade com navegador nem de todos os breakpoints.

## Atualização SVG — 2026-10-21

O vocabulário SVG usado pelo template agora é desenhado: 19 símbolos,
48 instâncias e o medidor com `pathLength`, cores de CSS/atributos,
`currentColor`, transformações, opacidade e tracejado. HTML/CSS do exemplo
permanecem preservados. A numeração de linhas da auditoria inicial abaixo
é histórica; os arquivos atuais têm formatação expandida.

Os seis fixtures SVG em `tests/assets/` receberam **EQUAL** nas checagens
locais contra Firefox em 800×600. A página completa em 1440×900 recebeu
**DIFFERENT** (SSIM 0,8687; 127 checagens locais): ícones e medidor aparecem,
mas persistem diferenças de layout e texto, incluindo centralização do
medidor/posição do texto “76%”, alinhamento de cabeçalhos e altura de linhas.
A implementação SVG não resolve esses comportamentos do layout HTML.
Imagens em `/tmp/tbox-svg-cmp/` e `/tmp/tbox-svg-explorer/` são temporárias;
os fixtures e este registro preservam a evidência reproduzível.

## Resultado do inventário

| Conjunto | Encontrado | Lacunas |
|---|---:|---|
| Nomes de elementos | 39 | Vocabulário SVG usado implementado; `meta` sem processamento de metadados |
| Propriedades CSS, sem custom properties | 62 | 2 extensões WebKit ignoradas; as 5 propriedades de pintura SVG usadas são implementadas |
| Custom properties | 14 | Nomes `--*` e usos de `var()` reconhecidos |
| Seletores distintos | 100 | Todos reconhecidos, incluindo `:focus-visible` e `::placeholder` |
| Blocos `@media` | 5 | As sintaxes usadas são implementadas; paridade visual nos tamanhos menores ainda não avaliada |
| Nomes de atributos HTML/SVG | 38 | Há atributos preservados no DOM sem o comportamento correspondente; detalhados abaixo |

As **60 propriedades CSS não WebKit** têm suporte às formas usadas, com as
ressalvas de composição e comportamento abaixo. Reconhecer uma declaração
ou conservar uma tag no DOM não garante seu desenho ou sua semântica.

## Elementos SVG e metadados

Os ícones usam **19 símbolos**, referenciados por **48 elementos `use`**.
Há 50 elementos `svg`: o repositório de símbolos oculto, os 48 ícones e o
medidor semicircular. Todo o conjunto SVG soma 156 elementos.

| Elemento | Ocorrências | Implementação atual |
|---|---:|---|
| `svg` | 50 | Caixa HTML externa, viewport e mapeamento de viewBox |
| `defs` | 1 | Definições disponíveis sem pintura direta |
| `symbol` | 19 | Conteúdo reutilizável com viewBox |
| `use` | 48 | Referência local href e estilo independente por instância |
| `path` | 25 | Geometria, preenchimento, traço e tracejado |
| `circle` | 10 | Círculos preenchidos ou traçados |
| `rect` | 1 | Retângulo com arredondamento rx/ry |
| `ellipse` | 2 | Elipses preenchidas ou traçadas |
| `meta` | 4 | Preservado, sem metadados de navegador |

O parser de caminhos agora é compartilhado entre motion path e pintura
SVG. Os limites do subconjunto estão em [SVG inline](../08-layout-e-pintura.md#svg-inline).
O texto `76%` continua sendo um span HTML, separado dos caminhos vetoriais.

Os quatro `meta` têm impacto visual pequeno neste arquivo: a entrada já é
UTF-8, o tamanho vem da janela/aplicação e o tema pretendido é claro.
`meta name="color-scheme"` não equivale a usar a propriedade CSS
`color-scheme`, que tem implementação própria.

As outras **30 tags** são aceitas na estrutura visual: `html`, `head`,
`title`, `link`, `body`, `aside`, `nav`, `main`, `header`, `section`,
`article`, `div`, `span`, `ul`, `li`, `p`, `h1`, `h2`, `h3`, `button`,
`label`, `input`, `a`, `table`, `thead`, `tbody`, `tr`, `th`, `td`, `time`.
`link rel="stylesheet" href="styles.css"` funciona como recurso local.
`input type="search"` é um campo editável suportado. Os limites de `a`,
`label` e dos atributos semânticos não desaparecem por a tag ser aceita.

## Pintura SVG e extensões ignoradas

| Propriedade | Valores usados | Comportamento atual |
|---|---|---|
| `fill` | `none` | Arcos sem preenchimento |
| `stroke` | `var(--soft-background)`, `var(--purple)` | Cores dos arcos e segmentos |
| `stroke-width` | `8` | Espessura nas coordenadas SVG |
| `stroke-dasharray` | `1.7 1.7`, `24 100` | Segmentação normalizada por pathLength |
| `stroke-dashoffset` | `-76` | Posicionamento do trecho final |
| `-webkit-font-smoothing` | `antialiased` | Extensão ignorada |
| `-webkit-tap-highlight-color` | `transparent` | Extensão ignorada |

Os atributos de apresentação `fill`, `stroke`, espessura, ponta/junção,
opacidade e transformações também participam da pintura. CSS de autor
sobrescreve atributos, inclusive dentro de camadas. `currentColor` herda de
cada instância do símbolo. Coordenadas, `viewBox`, `pathLength` e referências
`href` são interpretados pelo módulo SVG, sem exigir alteração do template.

## Recursos implementados após o levantamento

As pendências visuais fora de SVG foram implementadas sequencialmente:

| Recurso | Local | Comportamento atual |
|---|---|---|
| Atributo `placeholder` | `index.html:64` | A busca vazia desenha “Buscar arquivos...” sem mudar o valor editável |
| `::placeholder` | `styles.css:136` | A cascata resolve cor, opacidade e fonte do texto auxiliar; inputs de texto e textarea têm suporte |
| `.visually-hidden` | `styles.css:45–55` | O span absoluto ganha caixa de 1×1px e recorte próprios; “Opções” deixa de aparecer no cabeçalho |
| `:focus-visible` | `styles.css:33,138` | Teclado ativa a correspondência; ponteiro a remove em botões; campos de texto mantêm o destaque também por clique |

O placeholder desaparece quando há valor, não participa da seleção ou do
conteúdo enviado e não aumenta a altura automática do input. Em textarea,
o texto auxiliar aceita várias linhas, sem criar linhas editáveis ou rolagem.
A opacidade do pseudo afeta sua tinta, preservando a opacidade do controle.

`:focus`, `:focus-within` e `:hover` continuam disponíveis. A folha UA ainda
usa `:focus` para seu anel padrão; o autor pode removê-lo e usar apenas
`:focus-visible`. A modalidade é guardada por contexto, sem novo global.

SVG foi implementado na etapa final. As extensões WebKit da tabela anterior são
**ignoradas por decisão de escopo**, e os itens de semântica abaixo ficam
adiados. O HTML e o CSS do explorador permanecem intactos.

## Atributos e comportamentos parciais

| Recurso usado | Local | Limitação atual |
|---|---|---|
| `tabindex="0"` | Contêiner da tabela, `index.html:98` | Não torna o `div` focável nem o inclui na sequência de Tab |
| `a href="#…"` | Menu, `index.html:43–48` | O link pode ser estilizado e receber handler C, mas não há salto automático à âncora; esses links também não entram na navegação padrão por Tab |
| `label` envolvendo input | Busca, `index.html:64` | O contêiner recebe CSS, mas clicar nele fora da caixa do input não encaminha automaticamente o foco ao campo |
| `aria-label`, `aria-labelledby` | Menu, botões, seções e medidor | Atributos guardados, sem construir nomes/associações numa árvore de acessibilidade |
| `aria-hidden="true"`, `aria-current="page"` | SVGs e item Arquivos | Sem semântica de acessibilidade; `aria-current` não acrescenta destaque visual por si só |
| `role="region"`, `role="img"` | Tabela e medidor, `index.html:98,120` | Não criam região acessível ou imagem acessível |
| `scope="col"` | Cabeçalhos, `index.html:100` | Não produz associação acessível entre cabeçalhos e células; a tabela continua sendo desenhada |
| `time datetime="…"` | Datas, `index.html:102–110` | O texto é desenhado; o atributo não é convertido/formatado nem exposto a uma camada de acessibilidade |

Essas lacunas são de comportamento ou semântica, não propriedades CSS
ausentes. `id`, `class`, `lang`, `rel` e `type`, nas formas usadas, são
processados. A API permite ler os demais atributos para implementar lógica
na aplicação. Os links do menu têm `display: flex`, portanto o problema de
hit-test de links inline não é o bloqueio desses cinco links.

A prova de Tab usou link, botão, input e `div tabindex="0"`: a sequência
foi **botão → input → botão → input**, sem link nem região. Após a implementação, o foco por Tab corresponde tanto a
`:focus` quanto a `:focus-visible`. Ver
[foco](../../src/context/tbox_context_focus.c) e
[eventos](../06-eventos-e-interatividade.md#teclado-e-foco).

## Correção de composição: visually-hidden

O `span` “Opções” em `index.html:100` usa `position: absolute`, caixa de
1×1px, margem negativa, `overflow: hidden` e `clip-path: inset(50%)`.
Antes da correção, suas palavras entravam no texto da célula e apareciam.
Agora elementos `absolute`/`fixed` em contextos de texto ganham caixas
próprias, inclusive dentro de spans aninhados. O conteúdo sai do fluxo,
sem alterar a linha que o cerca, e recebe os recortes normais da pintura.

O teste de regressão verifica geometria, ausência de contribuição à linha
e ausência de pixels do texto oculto. Não é necessário acrescentar
`display: block` ao template. O débito específico foi resolvido; os limites
gerais de posicionamento seguem em [Layout e pintura](../08-layout-e-pintura.md).

## Recursos existentes que não precisam ser implementados de novo

| Grupo | Propriedades usadas com implementação |
|---|---|
| Layout | `display` (`block`, `flex`, `inline-flex`, `grid`), `grid-template-columns`, `grid-column`, `grid-row`, `flex`, `flex-shrink`, `flex-wrap`, `align-items`, `align-self`, `justify-content`, `gap`, `column-gap` |
| Dimensões | `width`, `height`, `min-width`, `min-height`, `box-sizing` |
| Espaçamento | `margin`, `margin-top`, `margin-bottom`, `margin-left`, `padding`, `padding-bottom` |
| Posição | `position`, `left`, `right`, `bottom` |
| Cores e caixa | `background`, `border`, `border-bottom`, `border-color`, `border-radius`, `color`, `opacity` |
| Fontes | `font: inherit`, `font-family`, `font-size`, `font-weight`, `font-synthesis: none`, `line-height` |
| Texto | `text-align`, `text-decoration`, `white-space`, `overflow-wrap` |
| Listas e tabela | `list-style: none`, `border-collapse: collapse` |
| Interação e recorte | `cursor`, `outline`, `outline-offset`, `overflow`, `overflow-x`, `scroll-margin-top` |
| Efeitos | `clip-path`, `transform`, `transition` |

Estão implementados os valores usados de `minmax(0, 1fr)`, `repeat()`,
`var()`, `inset(50%)`, `rotate(90deg)`, `fr`, `vh`, percentuais, margens
`auto`, spans e linhas negativas de Grid. Também as consultas
`@media (max-width: …)` em 1200/900/640/380px e
`prefers-reduced-motion: reduce`, configurável nas opções do contexto.

Persistem ressalvas de fidelidade: layout automático de tabela aproximado,
alinhamento vertical padrão das células e fontes disponíveis no sistema.
Segoe UI/aliases Apple podem cair para Arial ou sans-serif; aceitar a lista
de famílias não garante ter as mesmas fontes instaladas. `scroll-margin-top`
também não acrescenta a ação de navegação por `href="#…"` que falta ao link.
As transições de background/cor/borda usadas têm implementação, mas a
captura estática não valida sua evolução temporal.

## Evidências e escopo da verificação

O levantamento consulta 318 condições distintas: 218 pares de declaração
e 100 seletores. Após a implementação, as 100 expressões de seletor são
reconhecidas. Na auditoria inicial restavam nove pares de declaração de sete propriedades.
As cinco propriedades SVG foram implementadas; restam as duas extensões
WebKit ignoradas. O inventário completo de @supports não foi reexecutado
nesta etapa; os novos testes cobrem a validação de valores SVG.
`@supports` sozinho não comprova pintura, interação ou todas as composições.

Os testes de contexto cobrem valor/edição/seleção de placeholders, fonte,
cor/opacidade, textarea, foco por modalidade e independência entre contextos.
Os testes de layout e raster cobrem posicionados inline e texto oculto.
Os fixtures `forms-placeholder`, `selectors-states` e
`layout-block-in-inline` permitem inspeção visual desses recursos.

Na comparação dirigida com Firefox em 800×600, `forms-placeholder` recebeu
`EQUAL` (SSIM 0,9428; nenhum teste local falhou). `selectors-states` e
`layout-block-in-inline` receberam `DIFFERENT`, com diferenças de controles,
fontes e composição ainda presentes; a captura estática não exercita foco.
Os 25 grupos do CTest passaram. A captura de 2026-10-19 do explorador em 1440×900
mostra o placeholder e oculta “Opções”, ainda sem SVG naquela data.
As imagens dessa comparação estão em
`/tmp/tbox-file-explorer-implementation-cmp/`.

As provas iniciais estão em `/tmp/tbox-file-explorer-audit/`; a atualização
usa `render-implemented.png` e `supports-implemented.tsv` no mesmo diretório.
São evidências temporárias; este catálogo conserva as conclusões. A lógica
do explorador ainda será construída. Os arquivos HTML/CSS originais não
foram editados.
