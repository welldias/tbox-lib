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
| `<textarea>` | Multilinha, quebra e rolagem internas; `rows`/`cols` definem o tamanho inicial (CSS vence). |
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

`tbox_context_on_submit(ctx, handler, ud)` recebe o `<form>` e o botão que
enviou (`NULL` quando foi Enter num campo). O tbox não navega nem faz
requisição: leia os campos (por seletor dentro do form) e aja.

## Tema dos popups

Os popups de `color`, `date`, `datetime-local`, `month`, `time`, `week` e
`file` usam uma folha de estilo **separada** do documento. O CSS do
documento continua estilizando o input em si.

```c
tbox_context_options options = tbox_context_options_default();
options.control_css = tema;               /* NULL = tema embutido */
options.control_css_length = strlen(tema);
tbox_app *app = tbox_app_create_with_options(html, css, 800, 600, options);
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
