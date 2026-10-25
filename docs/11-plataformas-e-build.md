# 11. Plataformas e build

> - **Escopo:** em quais plataformas o tbox roda, opções de build, dependências e como integrar a biblioteca a outro projeto.
> - **Código relacionado:** `CMakeLists.txt`, `src/CMakeLists.txt`, `example/CMakeLists.txt`, `tests/CMakeLists.txt`, `cmake/tboxConfig.cmake.in`, `src/output/tbox_window_backend.h`, `src/app/tbox_app_unavailable.c`
> - **Veja também:** [Primeiros passos](01-primeiros-passos.md), [Arquitetura](02-arquitetura.md#fronteira-com-a-plataforma), [Limitações e roadmap](14-limitacoes-e-roadmap.md)

## Plataformas

| Ambiente | Situação |
|---|---|
| Linux + Wayland | Janela interativa completa (`tbox_app`): ponteiro, teclado com XKB e Compose, clipboard, cursores, repetição de tecla do compositor, repintura parcial. |
| Linux sem compositor (CI, servidor) | Pipeline completo headless: `tbox_app_screenshot_from_files` (precisa de Fontconfig) ou `tbox_context` + rasterizador (só FreeType). |
| X11 | Sem backend: numa sessão X11 pura (sem compositor Wayland) a janela não abre. |
| Windows, macOS, Android | Sem backend ainda. O núcleo (parser, estilo, layout, render, raster) é C portável; faltam o backend de janela e a descoberta de fontes do SO. |

### Foco e ciclo de vida

Wayland traduz `wl_keyboard.enter/leave` em `TBOX_INPUT_WINDOW_FOCUS` na
fila privada do backend. A aplicação os entrega como pageshow/pagehide;
entrada/saída do ponteiro não altera foco de janela. Redimensionamento usa
as dimensões negociadas por configure, observadas pelo loop. Fechamento
nativo, Escape não consumido e desconexão notificam unload.
Os novos símbolos de registro continuam linkáveis em builds sem Wayland
ou Fontconfig; nesses builds sem janela eles são no-ops. O teste
`app_lifecycle` usa o loop de produção com um backend simulado, sem abrir
janela nem depender de fontes instaladas (requer build estático e Fontconfig
para compilar o mesmo arquivo de aplicação).

## Dependências

| Dependência | Obrigatória? | Para quê | Sem ela |
|---|---|---|---|
| FreeType | sim (instalada no sistema, incluindo headers de desenvolvimento) | métricas, rasterização de glifos e contornos SVG | configure falha |
| Fontconfig | não | achar fontes do sistema; `tbox_app`; screenshots | `tbox_app_*` continua linkável mas retorna falha; use `tbox_context` com fonte própria |
| HarfBuzz | não | shaping: ligaduras, formas contextuais, marcas, kerning OpenType ([Texto e fontes](09-texto-e-fontes.md#shaping-do-texto-aos-glifos)) | um glifo por codepoint (`tbox_font_shaper_simple.c`) |
| wayland-client, wayland-protocols, wayland-scanner, xkbcommon | não | janela interativa | sem janela (`tbox_app_backend_available()` = `false`) |
| wayland-cursor | não | cursores CSS | cursor padrão |
| OpenCV (core, imgproc, imgcodecs) + C++17 | não | ferramenta `tbox_cmp` | `tbox_cmp` não é construído |
| Firefox (só em execução) | não | referência do `tbox_cmp`, capturada em headless | `tbox_cmp` falha em cada fixture |

Toda dependência opcional é detectada no configure; se faltar, o CMake
avisa e segue, removendo do build os arquivos que dependem dela.

FreeType é localizada com `find_package(Freetype REQUIRED)` e compartilhada
pelos alvos via `Freetype::Freetype`. O build não baixa nem compila uma cópia
própria. Instale antes de configurar:

```sh
# Arch Linux
sudo pacman -S freetype2
# Debian/Ubuntu
sudo apt install libfreetype-dev
```

## Opções do CMake

"Raiz" é o build do próprio tbox (`cmake -S tbox-lib`); "subprojeto" é o
tbox incluído por outro projeto com `add_subdirectory` ou `FetchContent`.

| Opção | Padrão (raiz / subprojeto) | Efeito |
|---|---|---|
| `TBOX_ENABLE_WAYLAND` | `ON` | Constrói o backend Wayland, se as dependências existirem. `OFF` gera uma lib headless. |
| `TBOX_ENABLE_FONTCONFIG` | `ON` | Usa Fontconfig, se existir. `OFF` deixa só a fonte que você fornecer. |
| `TBOX_ENABLE_HARFBUZZ` | `ON` | Usa HarfBuzz para o shaping, se existir. `OFF` usa o shaper simples. |
| `TBOX_BUILD_STATIC` / `TBOX_BUILD_SHARED` | `ON`/`ON` / segue `BUILD_SHARED_LIBS` | `libtbox.a` e/ou `libtbox.so` (pelo menos uma). Como subprojeto: só a compartilhada se `BUILD_SHARED_LIBS=ON`, senão só a estática. |
| `TBOX_BUILD_EXAMPLES` | `ON` / `OFF` | Programas de `example/`. |
| `TBOX_BUILD_TESTS` | `ON` / `OFF` | `tbox_tests` e, com OpenCV, `tbox_cmp`. |
| `TBOX_WARNINGS_AS_ERRORS` | `ON` / `OFF` | `-Werror` (`/WX` no MSVC) além de `-Wall -Wextra -Wpedantic`. |
| `TBOX_INSTALL` | `ON` / `OFF` | Regras de `install` e o pacote CMake (`tboxConfig.cmake`). |
| `TBOX_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer. |

Na raiz, o build padrão é `Debug` e o projeto compila com
`-Wall -Wextra -Wpedantic -Werror`. Como subprojeto, o tbox não mexe no
`CMAKE_BUILD_TYPE` nem no `CMAKE_EXPORT_COMPILE_COMMANDS` do projeto pai, e
o alvo `cppcheck` não é criado.

As duas bibliotecas saem dos mesmos objetos (`tbox_objects`, compilados uma
vez, com `-fPIC`). A compartilhada tem soname `libtbox.so.0.1`: enquanto a
versão for 0.x, cada versão menor pode quebrar a ABI.

## Usar em outro projeto

Os alvos públicos são `tbox::tbox` (a compartilhada, se for construída;
senão a estática), `tbox::tbox_shared` e `tbox::tbox_static`, com os mesmos
nomes nas formas de consumo abaixo. As dependências de sistema vêm com o
alvo: a `libtbox.so` as referencia, e a `libtbox.a` as repassa para o link
de quem a usa. O consumidor não lista nenhuma.

**`FetchContent`** (ou `add_subdirectory` de um submódulo git):

```cmake
include(FetchContent)
FetchContent_Declare(tbox GIT_REPOSITORY https://github.com/welldias/tbox-lib.git GIT_TAG main)
FetchContent_MakeAvailable(tbox)
target_link_libraries(app PRIVATE tbox::tbox)
```

Para usar um checkout local no lugar do download:
`-DFETCHCONTENT_SOURCE_DIR_TBOX=/caminho/tbox-lib`. As variáveis e os
alvos importados do pkg-config têm prefixo `TBOX_`
(`PkgConfig::TBOX_FONTCONFIG` etc.) para não colidir com os do projeto pai.

**Instalado** (`cmake --install build --prefix <prefixo>`):

```cmake
find_package(tbox 0.1 CONFIG REQUIRED)   # com CMAKE_PREFIX_PATH=<prefixo>
target_link_libraries(app PRIVATE tbox::tbox)
```

São instalados `lib/libtbox.{a,so}`, `include/tbox/` e `lib/cmake/tbox/`.
O `tboxConfig.cmake` procura de novo as dependências de sistema que o build
encontrou, porque a estática precisa delas no link do consumidor. A versão
é compatível dentro da mesma versão menor (`SameMinorVersion`).

**Sem CMake**, aponte o include para `tbox-lib/include` e linke:

- **compartilhada** (mais simples):
  `-L tbox-lib/build/src -ltbox -Wl,-rpath,<caminho>`;
- **estática**: `tbox-lib/build/src/libtbox.a` e as bibliotecas de sistema
  usadas pelo build. Com todos os backends e HarfBuzz habilitados:
  `$(pkg-config --libs freetype2 fontconfig harfbuzz wayland-client wayland-cursor xkbcommon) -lm -pthread`.
  Para um build só com fontes fornecidas pela aplicação e shaper simples:
  `$(pkg-config --libs freetype2) -lm -pthread`.
  Se as dependências também forem linkadas estaticamente, use
  `pkg-config --static --libs` para incluir as dependências transitivas.

Ainda não há arquivo `pkg-config` (`tbox.pc`). A `.so` exporta todo símbolo
não `static`, sem macro de exportação, o que só funciona assim no Linux
(ver [débito técnico](dev/debito-tecnico.md#consumo-por-outro-projeto)).

## Fronteira para novos backends

`tbox_app` só conversa com a plataforma por `src/output/tbox_window_backend.h`
(privado):

- abrir/destruir janela, tamanho atual, pedido de fechar;
- `poll(timeout)`: esperar eventos nativos até o timeout, que o backend
  reduz aos próprios prazos (no Wayland, a repetição de tecla, que no
  Windows e no macOS é do sistema); `wake()`, a única operação chamada de
  outra thread, que interrompe a espera (Wayland: `eventfd`; Windows:
  `PostMessage`; macOS: `postEvent`/`CFRunLoopWakeUp`);
- `frame_ready()`: se o sistema já pediu o próximo frame (Wayland:
  `wl_surface.frame`; Windows: DWM/DXGI; macOS: `CADisplayLink`, que chega
  em outra thread e por isso usa `wake`);
- fila de `tbox_input_event` (clique com duplo clique, arrasto, soltar,
  scroll, tecla lógica, texto UTF-8, colar);
- posição do ponteiro, cursor, clipboard (copiar/colar);
- `present(display_list)`, que pode recusar o frame (`false`) se não houver
  buffer livre, e o modo de depuração de dano.

Um backend novo implementa esse contrato e traduz os eventos nativos;
layout, render e contexto não mudam. A descoberta de fontes é outra
interface (`tbox_font_source`), com Fontconfig hoje e espaço para
DirectWrite/CoreText/Android.
