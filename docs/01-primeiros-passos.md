# 1. Primeiros passos

> - **Escopo:** compilar o tbox, rodar os exemplos e criar o primeiro programa.
> - **Código relacionado:** `CMakeLists.txt`, `src/CMakeLists.txt`, `example/`
> - **Veja também:** [Plataformas e build](11-plataformas-e-build.md), [Exemplos](04-exemplos.md), [Guia da API](03-guia-da-api.md)

## Requisitos

- Linux, compilador C com suporte a C23, CMake ≥ 3.25.
- **FreeType**: instalado no sistema, incluindo headers de desenvolvimento
  (`freetype2` no Arch Linux; `libfreetype-dev` no Debian/Ubuntu).
- **Fontconfig** (opcional): necessário para `tbox_app` e para screenshots.
- **Wayland** (opcional): `wayland-client`, `wayland-protocols`,
  `wayland-scanner`, `xkbcommon` (e `wayland-cursor` para cursores CSS) para
  janelas interativas.

Sem os opcionais a biblioteca compila do mesmo jeito; só as funções que
dependem deles ficam indisponíveis (`tbox_app_backend_available()` diz se
há janela).

## Compilar e testar

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

O build padrão é Debug. Para medir desempenho use
`-DCMAKE_BUILD_TYPE=Release`.

## Ver funcionando

```sh
build/example/tbox_app_demo                          # demo geral (Wayland)
build/example/tbox_app_demo --screenshot demo.png    # mesmo demo, sem janela
build/example/tbox_keyboard_demo                     # todos os controles, visual padrão
build/example/tbox_assets_showrun                    # navega pelos fixtures com ← →
TBOX_DEBUG_DAMAGE=1 build/example/tbox_app_demo      # mostra o que é repintado
```

Esc fecha as janelas.

## Primeiro programa

`ola.c`:

```c
#include <tbox/tbox.h>

static bool on_click(tbox_app *app, tbox_html_node *button, void *ud) {
    tbox_context *ctx = tbox_app_context(app);
    (void)ud;
    tbox_html_node_set_text_content(tbox_context_document(ctx), button,
                                    tbox_string_view_make("Obrigado!", 9));
    return true;
}

int main(void) {
    tbox_app *app = tbox_app_create(
        "<body><h1>Olá</h1><button>Clique</button></body>",
        "button { padding: 8px 16px; }", 480, 240);
    if (app == NULL) return 1;

    tbox_app_on_click(app, "button", 6, on_click, NULL, NULL);
    tbox_app_run(app);
    tbox_app_close(app);
    return 0;
}
```

Num projeto CMake, traga o tbox com `FetchContent` e linke `tbox::tbox`:

```cmake
include(FetchContent)
FetchContent_Declare(tbox GIT_REPOSITORY https://github.com/welldias/tbox-lib.git GIT_TAG main)
FetchContent_MakeAvailable(tbox)

add_executable(ola ola.c)
target_link_libraries(ola PRIVATE tbox::tbox)
```

As dependências de sistema (FreeType e, quando habilitados, Fontconfig,
HarfBuzz e Wayland) vêm com o alvo. `add_subdirectory` de um submódulo e
`find_package(tbox)` depois de um `cmake --install` também funcionam; veja
[Plataformas e build](11-plataformas-e-build.md#usar-em-outro-projeto).

Sem CMake, compile contra a biblioteca compartilhada gerada pelo build:

```sh
cc -std=c23 -I tbox-lib/include ola.c -o ola \
   -L tbox-lib/build/src -ltbox -Wl,-rpath,"$PWD/tbox-lib/build/src"
```

## Próximos passos

- Receitas prontas: [Exemplos](04-exemplos.md).
- Como o tbox decide o que redesenhar: [Atualização da tela](05-atualizacao-da-tela.md).
- O que o CSS suporta: [HTML e CSS](07-html-e-css.md).
