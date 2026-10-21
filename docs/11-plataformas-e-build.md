# 11. Plataformas e build

> - **Escopo:** em quais plataformas o tbox roda, opções de build, dependências e como integrar a biblioteca a outro projeto.
> - **Código relacionado:** `CMakeLists.txt`, `src/CMakeLists.txt`, `example/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/output/tbox_window_backend.h`, `src/app/tbox_app_unavailable.c`
> - **Veja também:** [Primeiros passos](01-primeiros-passos.md), [Arquitetura](02-arquitetura.md#fronteira-com-a-plataforma), [Limitações e roadmap](14-limitacoes-e-roadmap.md)

## Plataformas

| Ambiente | Situação |
|---|---|
| Linux + Wayland | Janela interativa completa (`tbox_app`): ponteiro, teclado com XKB e Compose, clipboard, cursores, repetição de tecla do compositor, repintura parcial. |
| Linux sem compositor (CI, servidor) | Pipeline completo headless: `tbox_app_screenshot_from_files` (precisa de Fontconfig) ou `tbox_context` + rasterizador (só FreeType). |
| X11 | Sem backend: numa sessão X11 pura (sem compositor Wayland) a janela não abre. |
| Windows, macOS, Android | Sem backend ainda. O núcleo (parser, estilo, layout, render, raster) é C portável; faltam o backend de janela e a descoberta de fontes do SO. |

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

| Opção | Padrão | Efeito |
|---|---|---|
| `TBOX_ENABLE_WAYLAND` | `ON` | Constrói o backend Wayland, se as dependências existirem. `OFF` gera uma lib headless. |
| `TBOX_ENABLE_FONTCONFIG` | `ON` | Usa Fontconfig, se existir. `OFF` deixa só a fonte que você fornecer. |
| `TBOX_ENABLE_HARFBUZZ` | `ON` | Usa HarfBuzz para o shaping, se existir. `OFF` usa o shaper simples. |
| `TBOX_BUILD_STATIC` / `TBOX_BUILD_SHARED` | `ON` / `ON` | `libtbox.a` e/ou `libtbox.so` (pelo menos uma). |
| `TBOX_BUILD_EXAMPLES` | `ON` | Programas de `example/`. |
| `TBOX_BUILD_TESTS` | `ON` | `tbox_tests` e, com OpenCV, `tbox_cmp`. |
| `TBOX_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer. |

O build padrão é `Debug`. O próprio projeto compila com `-Wall -Wextra -Wpedantic -Werror`.

## Usar em outro projeto

Ainda não há `install`, `pkg-config` nem suporte a `add_subdirectory` (o
CMake usa `${CMAKE_SOURCE_DIR}` nos caminhos de include, que só está certo
quando o tbox é o projeto raiz). Hoje:

1. Compile o tbox com o CMake dele.
2. Aponte o include para `tbox-lib/include`.
3. Linke:
   - **compartilhada** (mais simples):
     `-L tbox-lib/build/src -ltbox -Wl,-rpath,<caminho>`;
   - **estática**: `tbox-lib/build/src/libtbox.a` e as bibliotecas de sistema
     usadas pelo build. Com todos os backends e HarfBuzz habilitados:
     `$(pkg-config --libs freetype2 fontconfig harfbuzz wayland-client wayland-cursor xkbcommon) -lm -pthread`.
     Para um build só com fontes fornecidas pela aplicação e shaper simples:
     `$(pkg-config --libs freetype2) -lm -pthread`.
     Se as dependências também forem linkadas estaticamente, use
     `pkg-config --static --libs` para incluir as dependências transitivas.

Empacotamento (instalar a lib e a fonte embutida no sistema) faz parte do
[roadmap](14-limitacoes-e-roadmap.md).

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
