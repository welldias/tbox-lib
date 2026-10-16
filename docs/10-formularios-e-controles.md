# 10. Formulários e controles

> - **Escopo:** os controles de formulário nativos do tbox, como se comportam com mouse e teclado, como ler valores e como estilizar os popups.
> - **Código relacionado:** `include/tbox/context.h`, `src/context/tbox_context_{input,text,textarea,range,select,color,calendar,file,forms,control_theme}.c`, `src/layout/tbox_layout_{input,select,textarea}.c`
> - **Veja também:** [Eventos e interatividade](06-eventos-e-interatividade.md), [Exemplos](04-exemplos.md), `example/keyboard_demo.c`, `example/control_theme_demo.c`

Os controles são desenhados e operados pelo próprio tbox (não usam widgets
do sistema). São `inline-block` por padrão e aceitam CSS como qualquer
elemento.

## Princípio: o valor vive no DOM

O estado de cada controle fica nos atributos do nó: `value` em campos de
texto, data, cor, range, arquivo; presença de `checked` em checkbox e
radio. A aplicação lê com `tbox_html_node_get_attribute` e altera com
`tbox_html_node_set_attribute`/`_remove_attribute`. Mudanças feitas pelo
usuário disparam `tbox_context_on_input` (ou `on_select` para `<select>`).

## Controles disponíveis

| Elemento | Comportamento |
|---|---|
| `<button>`, `input type=button` | Clique, Enter ou Espaço disparam `on_click`. `disabled` não ativa. |
| `input type=submit` / `reset` | Envia (`on_submit`) ou restaura os valores iniciais do `<form>`. |
| `input type=image` | Botão com imagem (`src`, `width`/`height`); mostra `alt` se a imagem falhar. |
| `input type=checkbox` / `radio` | Clique ou Espaço. Radios com o mesmo `name` no mesmo form se excluem. `:checked` reflete o estado. |
| `input` de texto: `text`, `search`, `email`, `url`, `tel`, `password`, `number` | Edição UTF-8 com cursor, seleção (Shift+setas, arrasto, duplo clique, Ctrl+A), clipboard, rolagem horizontal. `search` tem botão de limpar e Esc limpa. `password` mascara e não copia. `number` aceita setas/botões e `step`/`min`/`max`. |
| `<textarea>` | Multilinha, quebra e rolagem internas; `rows`/`cols` definem o tamanho inicial (CSS vence). Quebra depois dos espaços (que ficam pendurados no fim da linha); só uma palavra mais larga que a linha é cortada. Como no layout de texto, só espaço e tab são oportunidades de quebra. Veja `tests/assets/144.html`. |
| `input type=range` | Slider; clique, arrasto, setas por `step`, Home/End. Padrão 0–100. |
| `input type=color` | Amostra; abre um seletor RGB (clique/arrasto nos canais, teclado). Valor `#rrggbb`. |
| `input type=date`, `datetime-local`, `month`, `week`, `time` | Abrem calendário / grade de meses / barras de hora. `min`/`max` respeitados. Valores em formato ISO, sem fuso. |
| `input type=file` | Seletor de arquivos interno (a partir do diretório atual). `value` = nome; `tbox_context_file_path` = caminho absoluto. Um arquivo por vez. |
| `input type=hidden` | Guarda `value`, não aparece nem recebe foco. |
| `<select>` | Escolha única com `<option>` diretos ou dentro de `<optgroup>`; mouse, setas, Home/End, Enter/Espaço, Esc. `tbox_context_select_value` / `_select_set_value`. |

`<optgroup label="…">` acrescenta um título não selecionável ao popup.
As opções dentro de um grupo `disabled` aparecem desabilitadas e são
ignoradas pela navegação e por `_select_set_value`. Feche explicitamente
`<optgroup>` e `<option>`: o parser ainda não faz o fechamento implícito.
Veja `tests/assets/107.html`.

`<fieldset>` recebe borda e espaçamento padrão. Quando `<legend>` é o
primeiro elemento filho, seu texto fica sobre a borda superior, que é
interrompida atrás dele. A interrupção acompanha bordas retas; bordas
arredondadas e estilos de borda complexos não têm tratamento especial.
Veja `tests/assets/109.html`.

## Indicadores e resultado

`<output>` mostra seu conteúdo como texto inline. A aplicação pode atualizá-lo
com `tbox_html_node_set_text_content`, mas o tbox não calcula expressões nem
implementa as propriedades DOM `value`/`defaultValue` ou o reset de `<output>`.

`<progress>` desenha uma barra a partir de `value`/`max` (padrão `max=1`).
Sem `value` válido, mostra uma faixa estática indicando progresso
indeterminado. `<meter>` desenha uma medição com `min`/`max`/`value` e
usa `low`/`high`/`optimum` para escolher verde, amarelo ou vermelho.
A barra do `<progress>` usa `accent-color`; as cores do `<meter>` são
semânticas e não mudam com ela.
Os atributos podem ser alterados pela API de mutação do DOM; o frame seguinte
mostra o novo valor. Ambos são controles passivos: não recebem entrada nem
disparam `on_input`. Seu texto filho permanece no DOM como fallback, mas não
é pintado. Largura e altura podem ser definidas por CSS. Veja
`tests/assets/106.html`.

## Validação

`tbox_context_email_valid`, `tbox_context_url_valid` e
`tbox_context_number_valid` verificam o valor atual (sintaxe, `required`,
`multiple`, `min`/`max`/`step`). `:required`/`:optional` casam pelo
atributo. Não há mensagens de validação nativas nem bloqueio de envio — a
aplicação decide no handler de submit.

## Envio

`tbox_context_on_submit(ctx, handler, ud, destroy)` recebe o `<form>` e o botão que
enviou (`NULL` quando foi Enter num campo). O tbox não navega nem faz
requisição: leia os campos (por seletor dentro do form) e aja.

## Cor de destaque

`accent-color` (herdada) pinta a marca do checkbox, o ponto do radio, o
preenchimento e o polegar do `input type=range` e a barra do `<progress>`.
Com `auto`, checkbox e radio usam `color`; range e progress mantêm o azul
embutido. O trilho e as cores do `<meter>` não mudam. Veja
`tests/assets/142.html`.

## Partes dos controles

As partes internas dos controles são estilizadas no CSS do documento por
pseudo-elementos com os nomes do rascunho W3C *CSS Form Control Styling*.
Cada parte é resolvida como um filho do controle: herda `color` dele e
casa com os seletores do controle (`input:focus::slider-thumb`,
`.escuro input::slider-thumb`, `var()`).

| Pseudo-elemento | Elementos | O que muda | Padrão |
|---|---|---|---|
| `::slider-track` | range | `height`, fundo, borda, raio. A largura é o percurso do polegar | 4px, `#cdd2d8` |
| `::slider-fill` | range | `height` (padrão: a do trilho), fundo, borda, raio | `accent-color` |
| `::slider-thumb` | range | `width`, `height`, fundo, borda, raio. Pode passar da caixa do input | 14×14px, raio 50%, `accent-color` |
| `::slider-track` | progress, meter | fundo, borda, raio (ocupa a caixa toda) | `#cdd2d8` |
| `::slider-fill` | progress, meter | fundo, borda, raio (dentro da borda do trilho) | progress: `accent-color`; meter: verde/amarelo/vermelho |
| `::picker-icon` | select | `width`, `height`, `color` da seta; fundo, borda | 10×6px, `color` do select |
| `::checkmark` | checkbox, radio | `color` da marca; `width` do ponto do radio | `accent-color` (ou `color`) |
| `::file-selector-button` | file | `width`, fundo, borda, raio, `color` do texto | 88px, `#f0f2f6`, borda `#767c86`; cinza quando `disabled` |
| `::clear-icon` | search | `width`, fundo, borda, `color` do X | 16px, `#f5f6f8` |
| `::step-control` | number | `width`, fundo, borda; a borda esquerda também desenha a divisória | 16px, `#eef0f3`, `border-left` `#afb4bc` |
| `::step-up`, `::step-down` | number | fundo, borda, `color` da seta | `color` do input |
| `::color-swatch` | color | borda (um anel na cor de cima), raio | sem borda |
| `::selection` | `input` de texto, `<textarea>` | fundo; `color` repinta o texto selecionado | `#82afeb`, texto inalterado |

Os padrões estão na folha UA, então o CSS do autor os substitui normalmente
(inclusive `border: 0`). Uma parte sem `background-color` (ou `color`)
declarada usa a cor embutida do controle; por isso o preenchimento e o
polegar seguem `accent-color` até alguém declarar outra cor. A largura da
coluna do number e da busca também vale para o clique e para a área de
texto; a do polegar, para o percurso do clique e do arraste.

Limitações: não há pseudo-classes na própria parte (`::slider-thumb:hover`);
use o estado do controle (`input:hover::slider-thumb`). Transições e
animações não se aplicam às partes. Não há fonte nas partes (o texto do
botão de arquivo segue em 13px), nem raios elípticos, sombras ou gradientes.
A forma das marcas (triângulo da seta, X da busca, setas do number) é fixa.
Veja `tests/assets/143.html`.

### Equivalentes em outros navegadores

O tbox reconhece **só** os nomes da tabela acima. Os pseudo-elementos com
prefixo dos navegadores são ignorados, como qualquer seletor desconhecido.
A tabela serve de referência para portar CSS existente ou para uma futura
implementação de compatibilidade (ver
[débito técnico](dev/debito-tecnico.md#pseudo-elementos-com-prefixo-de-navegador)).

| tbox (rascunho W3C) | Firefox | Chrome / Safari |
|---|---|---|
| range `::slider-thumb` | `::-moz-range-thumb` | `::-webkit-slider-thumb` |
| range `::slider-track` | `::-moz-range-track` | `::-webkit-slider-runnable-track` |
| range `::slider-fill` | `::-moz-range-progress` | — |
| progress `::slider-track` | o próprio `progress` | `::-webkit-progress-bar` |
| progress `::slider-fill` | `::-moz-progress-bar` | `::-webkit-progress-value` |
| meter `::slider-track` | o próprio `meter` | `::-webkit-meter-bar` |
| meter `::slider-fill` | `::-moz-meter-bar` | `::-webkit-meter-optimum-value`, `-suboptimum-value`, `-even-less-good-value` |
| select `::picker-icon` | — | `::picker-icon` (Chrome, com `appearance: base-select`) |
| checkbox/radio `::checkmark` | — | — (o Chrome tem `::checkmark` só em `<option>`) |
| `::file-selector-button` | igual | igual (antigo: `::-webkit-file-upload-button`) |
| search `::clear-icon` | — | `::-webkit-search-cancel-button` |
| number `::step-control`, `::step-up`, `::step-down` | — | `::-webkit-inner-spin-button` (um só, sem separar subir/descer) |
| color `::color-swatch` | `::-moz-color-swatch` | `::-webkit-color-swatch` (e `::-webkit-color-swatch-wrapper`) |
| `::selection` | igual | igual |

"—" indica que o navegador não oferece a parte.

## Tema dos popups

Os popups de `color`, `date`, `datetime-local`, `month`, `time`, `week` e
`file` usam uma folha de estilo **separada** do documento. O CSS do
documento continua estilizando o input em si.

```c
tbox_context_options options = tbox_context_options_default();
options.control_css = tema;               /* NULL = tema embutido */
options.control_css_length = strlen(tema);
tbox_app *app = tbox_app_create_with_options(html, css, 800, 600, &options);
```

- O popup é apresentado aos seletores como um elemento `tbox-popup` com
  atributo `type` e com o `id` e as classes do input:
  `tbox-popup[type=color]`, `tbox-popup#meu-input`.
- Cada parte é um `tbox-part` com classe: `.track`, `.channel-label`,
  `.thumb`, `.preview` (cor); `.heading`, `.nav`, `.weekday`, `.day`,
  `.month`, `.time-track`, `.time-fill`, `.time-label`, `.confirm`
  (calendário e hora); `.back`, `.path`, `.divider`, `.row`, `.empty`,
  `.footer` (arquivos).
- Estados: `.is-active`, `.is-selected`, `.is-disabled`, `.is-directory`.
- Propriedades: cores, fundos, fonte, bordas, raios e dimensões
  documentadas (`width`/`height` em `px` ou `em` de `tbox-popup`, `.track`,
  `.preview`, `.nav`, `.day`, `.month`, `.time-track`, `.confirm`, `.back`;
  `height` de `.row`; o popup de cor também lê padding superior/esquerdo e
  `row-gap`). Defina dimensões nos seletores de parte, não nos de estado.
- Sem pseudo-classes nem combinadores de irmão nesta folha.
- Um `control_css` não-`NULL` substitui o tema embutido por completo (até
  uma string vazia); regras ausentes caem em tamanhos estruturais.

Veja `example/control_theme_demo.c` e `example/control_theme_demo.controls.css`.

## Limitações

Sem `<select multiple>`, `<select size>`,
`input type=file multiple`/`accept`, composição por IME e árvore de
acessibilidade.
