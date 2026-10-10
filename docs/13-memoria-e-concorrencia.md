# 13. Memória e concorrência

> - **Escopo:** quem é dono de cada objeto, por quanto tempo ponteiros e views valem, e as regras de uso com threads.
> - **Código relacionado:** `src/base/tbox_arena.*`, `include/tbox/context.h`, `include/tbox/html_parser.h`, `include/tbox/font.h`, `include/tbox/image.h`
> - **Veja também:** [Arquitetura](02-arquitetura.md#quem-é-dono-de-quê), [Convenções](dev/convencoes.md), [Débito técnico](dev/debito-tecnico.md)

## Arenas em vez de `free` individual

O tbox aloca quase tudo em **arenas**: blocos grandes de memória dos quais
objetos são recortados e que são liberados de uma vez. Não existe `free`
de um nó, de uma caixa ou de uma op. Isso torna o pipeline rápido e simples,
e define as regras abaixo.

## Tempo de vida, do mais longo ao mais curto

| Objeto | Criado por | Vive até | Observação |
|---|---|---|---|
| `tbox_app` | `tbox_app_create*` | `tbox_app_close` | Dono de contexto, caches e janela. |
| `tbox_font_face_cache`, `tbox_image_cache` | você (ou o `tbox_app`) | `_destroy` | Emprestados ao contexto; destrua **depois** de fechar o contexto. |
| `tbox_context` | `tbox_context_open*` | `tbox_context_close` | Dono do documento, das folhas de estilo e dos handlers. |
| `tbox_html_node` | parser ou `tbox_html_node_create` | fechamento do contexto/documento | Mesmo removido da árvore, continua válido (só desconectado). |
| atributos e textos do DOM | parser ou funções de mutação | fechamento do documento | Ao trocar um atributo ou texto, o valor antigo fica órfão na arena. |
| `tbox_layout_box`, `tbox_display_list`, `tbox_style_table` | `tbox_context_run_frame` | **próximo** `run_frame` | Não guarde ponteiros entre frames. |
| views passadas a handlers (`value` de `on_input`/`on_select`) | o contexto | fim do handler / próxima edição | Copie se precisar guardar. |
| `tbox_css_selector_node_set`, `tbox_xpath_node_set` | consultas | `_destroy` | Os nós apontados seguem as regras do DOM. |

## Consequências práticas

- **Mutação longa faz a memória crescer.** Uma UI que troca textos ou cria e
  remove nós milhares de vezes numa sessão aumenta a arena do documento até
  ele ser fechado. Para interfaces normais isso é irrelevante; para uma
  aplicação que roda dias com atualização contínua, recarregar o documento
  (`tbox_app_load_from_files`, que cria um contexto novo — os handlers precisam
  ser registrados de novo) devolve a memória. Isso é conhecido e será
  tratado se medido como problema ([Débito técnico](dev/debito-tecnico.md#memória-do-dom-em-sessões-longas)).
- **`tbox_layout_box` é do frame.** Um ponteiro obtido por
  `tbox_context_hit_test` só vale até o próximo frame. Guarde o
  `tbox_html_node` (`box->node`), não a caixa.
- **Os caches crescem e não esvaziam.** Fontes e imagens carregadas ficam até
  o cache ser destruído. Um documento comum usa poucas.
- **Strings de entrada são copiadas.** HTML, CSS, nomes e valores passados à
  API não precisam continuar vivos depois da chamada — com uma exceção:
  `element.tag_name` de um nó criado à mão (ver
  [Exemplos](04-exemplos.md#7-criar-elementos-dinamicamente)).

## Threads

**O tbox não é thread-safe e não tem lock interno.** Regras:

- Use cada `tbox_app`/`tbox_context` (e os caches ligados a ele) de uma
  única thread, a mesma em que foi criado.
- O estado de `:hover`, `:focus` e `:active` é aplicado aos seletores por
  meio de variáveis globais durante o frame. Por isso, nem dois contextos
  independentes em threads diferentes são seguros hoje.
- Trabalho pesado da aplicação (rede, disco, cálculo) pode rodar em outras
  threads; entregue o resultado à thread da UI e, lá, mude o DOM e chame
  `tbox_app_request_redraw`.

A política de concorrência será definida quando houver um caso de uso real
(documentos em paralelo, motor de script em outra thread).
