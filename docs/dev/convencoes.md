# Convenções do projeto

> - **Escopo:** regras de código e de processo que toda mudança no tbox deve seguir.
> - **Código relacionado:** todo o repositório; `src/base/` (arena, vector, string)
> - **Veja também:** [Organização do código](organizacao-do-codigo.md), [Decisões](decisoes.md)

Leitura obrigatória antes de qualquer mudança de código.

## Linguagem e estilo

- C puro (C23, `CMAKE_C_EXTENSIONS OFF`), headers públicos com `extern "C"`.
  A única exceção é `tests/tbox_cmp.cpp`, isolado e opcional (ver
  [Ferramentas e testes](../12-ferramentas-e-testes.md)).
- Prefixo `tbox_` em todo símbolo público; tipos internos de um módulo ficam
  em headers `*_internal.h` dentro de `src/<módulo>/`.
- Sem exceções, sem alocação escondida: memória vem de uma `tbox_arena` ou
  de `malloc` explícito.
- `tbox_string_view` para toda string que não é dona da memória.
- Compilação com `-Wall -Wextra -Wpedantic -Werror`; o build tem de sair limpo.

## Posse de memória

Há dois padrões. Uma estrutura usa um ou outro, nunca os dois.

1. **Dona da própria arena, com `_destroy`.** Estruturas de vida longa:
   `tbox_html_document`, `tbox_css_stylesheet`, `tbox_context`,
   `tbox_font_face_cache`, `tbox_image_cache`. Tudo que pendura nelas é
   liberado de uma vez no `_destroy`; não há `free` individual.
   Structs de resultado com `items`/`count` e `void *reserved_` privado
   (`tbox_css_computed_style`, `tbox_css_selector_node_set`) também têm
   `_destroy` próprio.
2. **Arena do chamador, sem `_destroy`.** Estruturas reconstruídas a cada
   frame (`tbox_style_table`, `tbox_layout_box`, `tbox_display_list`) recebem a
   `tbox_arena` como parâmetro. A limpeza é o chamador resetar a arena — na
   prática, `tbox_context_run_frame` reseta a `frame_arena` no início de cada
   frame.

**Callbacks.** Toda função que guarda um `userdata` junto de um callback
recebe também um `tbox_destroy_notify` (`include/tbox/callback.h`) e o
chama exatamente uma vez quando deixa de usá-lo — adiado com
`src/base/tbox_release.h` se um callback estiver em execução. Registro que
falha não chama o `destroy`. Nenhum callback recebe struct por valor
(ponteiro `const` ou `(const char *, size_t)`), e structs que podem crescer
entram por ponteiro. Ver [Decisões](decisoes.md#callbacks-e-structs-na-fronteira-de-ffi).

Consequência para mutação do DOM: remover nós ou trocar atributos deixa a
memória antiga órfã na arena do documento até ele ser destruído. Isso é
aceito; só será revisto se for medido como problema real (ver
[Débito técnico](debito-tecnico.md)).

## Entrada malformada

- Camadas de parsing (HTML, CSS) **nunca falham** por markup/CSS inválido:
  o que não é reconhecido é ignorado ou fica como texto literal.
- Falha explícita (`NULL`/`false` + offset do erro) só onde já é a política:
  `tbox_css_selector_compile` e `tbox_xpath_compile`.
- Funções públicas aceitam `NULL` no handle principal como no-op (ou
  retorno neutro), conforme documentado em cada header.

## Estado global e threads

A biblioteca usa **afinidade de thread**: cada `tbox_app`/`tbox_context`
(e tudo ligado a ele) é usado só pela thread de UI. A única exceção é
`tbox_app_post`, cuja fila por instância é protegida por um mutex
(`src/base/tbox_mutex.h`) — o único lock da biblioteca. Uma nova função
chamável de outra thread, ou um novo lock, precisa de discussão como um
global (ver [Decisões](decisoes.md#loop-da-aplicação-e-threads)). A análise
de thread-safety do resto continua adiada até existir um caso de uso real
(ver [Débito técnico](debito-tecnico.md#thread-safety)).

**Regra: nenhum estado global/estático mutável novo sem discussão.** O
estado global que já existe (contextos de `:hover`/`:focus`/`:active` em
`css_selector.c`) não é precedente. Cada nova variável global ou estática
mutável precisa ser discutida explicitamente com o mantenedor antes de ser
adicionada. Tabelas `static const` (dados imutáveis) não entram nessa regra.

## Dependências

- FreeType é obrigatório e baixado pelo CMake (`FetchContent`).
- Toda outra dependência de sistema é **opcional, com graceful-skip**:
  Fontconfig, Wayland (+ `xkbcommon`, `wayland-cursor`), OpenCV. Se ausente, o
  configure avisa e exclui **o arquivo inteiro** que depende dela — o
  projeto não usa `#ifdef` parcial dentro de um arquivo para dependências
  opcionais.
- Código de terceiros vendorizado fica em `external/<nome>/` com `LICENSE` e
  `README.md`.

## Processo

- Design antes do código: uma mudança de arquitetura é escrita no capítulo
  correspondente de `docs/` (e, se for decisão, em [decisoes.md](decisoes.md))
  antes de ganhar código, e atualizada se a implementação revelar outra coisa.
- Ao terminar uma mudança visível ao usuário:
  1. atualize o capítulo do assunto em `docs/`;
  2. atualize o mapa em `docs/referencia/` se uma propriedade CSS ou
     elemento HTML mudou de status;
  3. acrescente uma entrada em [historico.md](historico.md) e, se houve
     escolha entre alternativas, em [decisoes.md](decisoes.md);
  4. inclua um teste de comportamento e, quando fizer sentido, um fixture em
     `tests/assets/NNN.html`.
- Testes: `ctest --test-dir build --output-on-failure` precisa passar.
- Depois de mudar código, rode `graphify update .` se `graphify-out/` existir.
