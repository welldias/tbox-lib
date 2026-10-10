### This project is far from ready for use. See the limitations chapter below. ###


# tbox

tbox is an experimental retained-mode GUI library in C. The interface is
described in HTML and CSS; the library parses it, computes styles and
layout, builds a display list and rasterizes it in software. Application
state and logic stay in C, registered as handlers by CSS selector. A
JavaScript bridge is a later milestone.

```c
tbox_app *app = tbox_app_create(
    "<body><h1>Hello</h1><button>Click</button></body>",
    "button { padding: 8px 16px; }", 480, 240);
tbox_context_on_click(tbox_app_context(app), "button", 6, on_click, NULL);
while (!tbox_app_should_close(app))
    tbox_app_step(app);
tbox_app_close(app);
```

## Highlights

- HTML/CSS parsing, selectors and cascade; a mutable DOM as the public tree.
- Block, inline, inline-block, positioned, table and flexbox layout;
  `calc()`, `var()`, `@media`, `@font-face`, transitions, gradients, shadows.
- Built-in form controls (text, date/time, color, file, range, select,
  textarea, checkbox, radio) with keyboard focus, selection and clipboard.
- Software rendering with partial repaint (damage tracking).
- Headless rendering to PNG; interactive windows on Linux/Wayland.


## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

FreeType is fetched by CMake. Fontconfig and Wayland are optional; without
them the library still builds (headless). Try `build/example/tbox_app_demo`
or `build/example/tbox_keyboard_demo`.

## Documentation

The documentation lives in [`docs/`](docs/README.md) (in Portuguese):

- [Introdução](docs/00-introducao.md) — what tbox is, what it is for, when not to use it
- [Primeiros passos](docs/01-primeiros-passos.md) — build and first program
- [Arquitetura](docs/02-arquitetura.md) — modules and data flow, with diagrams
- [Guia da API](docs/03-guia-da-api.md) and [Exemplos](docs/04-exemplos.md)
- [Atualização da tela](docs/05-atualizacao-da-tela.md) — how tbox decides what to redraw
- [Limitações e roadmap](docs/14-limitacoes-e-roadmap.md)
- Reference: [CSS](docs/referencia/css-suportado.md), [HTML](docs/referencia/html-suportado.md)

Contributors: start with [`docs/dev/`](docs/dev/README.md) (conventions,
design decisions, technical debt, version history).

## License

MIT — see [LICENSE](LICENSE).
