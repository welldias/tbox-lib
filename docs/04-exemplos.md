# 4. Exemplos

> - **Escopo:** receitas curtas para as tarefas mais comuns, do "olá mundo" ao render headless.
> - **Código relacionado:** `example/tbox_app.c`, `example/keyboard_demo.c`, `example/control_theme_demo.c`, `example/assets_showrun.c`
> - **Veja também:** [Guia da API](03-guia-da-api.md), [Eventos e interatividade](06-eventos-e-interatividade.md), [Primeiros passos](01-primeiros-passos.md)

Todos os trechos usam só a API pública (`#include <tbox/tbox.h>`). Os
exemplos completos e executáveis do repositório estão listados no fim.

## Função auxiliar usada abaixo

Trocar o texto do primeiro elemento que casa com um seletor:

```c
#include <tbox/tbox.h>
#include <string.h>

static void set_text(tbox_context *ctx, const char *selector, const char *text) {
    tbox_html_document *doc = tbox_context_document(ctx);
    tbox_css_selector_node_set found =
        tbox_css_selector_select(tbox_html_document_root(doc), selector, strlen(selector));
    if (found.count > 0)
        tbox_html_node_set_text_content(doc, (tbox_html_node *)found.items[0],
                                        tbox_string_view_make(text, strlen(text)));
    tbox_css_selector_node_set_destroy(&found);
}
```

## 1. Abrir uma janela

```c
int main(void) {
    const char *html =
        "<body><h1>Olá, tbox</h1><p>Interface em HTML, lógica em C.</p></body>";
    const char *css = "body { font-family: sans-serif; } h1 { color: #3366cc; }";

    tbox_app *app = tbox_app_create(html, css, 640, 400);
    if (app == NULL) return 1; /* sem backend, sem fonte ou falha de janela */

    while (!tbox_app_should_close(app))
        tbox_app_step(app);
    tbox_app_close(app);
    return 0;
}
```

O documento precisa ter **um único elemento de topo** (`<body>`, `<html>`
ou um `<div>`): só o primeiro elemento do nível raiz é desenhado.

## 2. Reagir a um clique

```c
/* <button class="toggle">Ligar</button> <div id="lamp" class="off"></div>
   CSS: #lamp.off { background: #333 } #lamp.on { background: gold } */
static bool on_toggle(tbox_context *ctx, tbox_html_node *button, void *userdata) {
    (void)button; (void)userdata;
    tbox_html_document *doc = tbox_context_document(ctx);
    tbox_css_selector_node_set lamp =
        tbox_css_selector_select(tbox_html_document_root(doc), "#lamp", 5);
    if (lamp.count > 0) {
        tbox_html_node *node = (tbox_html_node *)lamp.items[0];
        const tbox_html_attribute *cls =
            tbox_html_node_get_attribute(node, tbox_string_view_make("class", 5));
        bool on = cls != NULL && tbox_string_view_equal_cstr(cls->value, "on");
        const char *next = on ? "off" : "on";
        tbox_html_node_set_attribute(doc, node, tbox_string_view_make("class", 5),
                                     tbox_string_view_make(next, strlen(next)));
    }
    tbox_css_selector_node_set_destroy(&lamp);
    return true; /* deixa o clique continuar subindo para outros handlers */
}

/* depois de criar o app: */
tbox_context_on_click(tbox_app_context(app), "button.toggle", 13, on_toggle, NULL);
```

O handler é registrado por **seletor**, não por nó: vale também para
elementos criados depois do registro. Não é preciso pedir redesenho — o
clique já suja o frame.

## 3. Acompanhar o que o usuário digita

```c
/* <input id="nome"> <p id="saudacao"></p> */
static void on_input(tbox_context *ctx, tbox_html_node *input,
                     tbox_string_view value, void *userdata) {
    (void)userdata;
    const tbox_html_attribute *id =
        tbox_html_node_get_attribute(input, tbox_string_view_make("id", 2));
    if (id == NULL || !tbox_string_view_equal_cstr(id->value, "nome")) return;

    char message[128];
    snprintf(message, sizeof message, "Olá, %.*s!", (int)value.size, value.data);
    set_text(ctx, "#saudacao", message);
}

tbox_context_on_input(tbox_app_context(app), on_input, NULL);
```

Há um único handler de input por contexto; distinga os campos pelo `id`
(ou outro atributo). `value` só vale durante a chamada — copie se precisar
guardar.

## 4. Enviar um formulário

```c
/* <form><input name="email" type="email" required>
         <input type="submit" value="Cadastrar"></form> <p id="msg"></p> */
static void on_submit(tbox_context *ctx, tbox_html_node *form,
                      tbox_html_node *submitter, void *userdata) {
    (void)submitter; (void)userdata;
    tbox_css_selector_node_set fields =
        tbox_css_selector_select(form, "input[name=email]", 17);
    if (fields.count > 0) {
        const tbox_html_node *email = fields.items[0];
        set_text(ctx, "#msg", tbox_context_email_valid(email)
                                  ? "Cadastro recebido."
                                  : "E-mail inválido.");
    }
    tbox_css_selector_node_set_destroy(&fields);
}

tbox_context_on_submit(tbox_app_context(app), on_submit, NULL);
```

O valor atual de cada campo está no atributo `value` do nó (para checkbox e
radio, na presença de `checked`). O tbox não navega nem faz requisição de
rede: o que fazer com os dados é decisão da aplicação.

## 5. Atalho de teclado

```c
static bool on_key(tbox_app *app, tbox_key_event event, void *userdata) {
    (void)userdata;
    if (event.pressed && event.control && event.key == TBOX_KEY_A) {
        set_text(tbox_app_context(app), "#status", "Ctrl+A interceptado");
        return true;  /* consumido: o contexto não vê a tecla */
    }
    return false;     /* segue para foco, edição, Esc etc. */
}

tbox_app_on_key(app, on_key, NULL);
```

As teclas disponíveis são as de `tbox_key` em `input.h` (Tab, Enter, Espaço,
Esc, setas, Home/End, Backspace/Delete, A/C/V/X). Texto digitado chega
separado, por `tbox_context_on_input` no campo focado.

## 6. Atualizar a tela a partir do seu próprio código

Mutações feitas fora de um handler precisam de um pedido explícito:

```c
#include <time.h>

double last = 0.0;
while (!tbox_app_should_close(app)) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double seconds = (double)now.tv_sec + now.tv_nsec / 1e9;
    if (seconds - last >= 1.0) {          /* uma vez por segundo */
        last = seconds;
        char text[32];
        snprintf(text, sizeof text, "%.0f s", seconds);
        set_text(tbox_app_context(app), "#relogio", text);
        tbox_app_request_redraw(app);     /* sem isto, nada muda na tela */
    }
    tbox_app_step(app);
}
```

## 7. Criar elementos dinamicamente

```c
static void add_item(tbox_context *ctx, const char *text) {
    tbox_html_document *doc = tbox_context_document(ctx);
    tbox_css_selector_node_set list =
        tbox_css_selector_select(tbox_html_document_root(doc), "ul#itens", 8);
    if (list.count > 0) {
        tbox_html_node *li = tbox_html_node_create(doc, TBOX_HTML_NODE_ELEMENT);
        li->element.tag_name = tbox_string_view_make("li", 2); /* minúsculo, memória estática */
        tbox_html_node_append_child((tbox_html_node *)list.items[0], li);
        tbox_html_node_set_text_content(doc, li, tbox_string_view_make(text, strlen(text)));
    }
    tbox_css_selector_node_set_destroy(&list);
}
```

`tag_name` não é copiado: aponte para uma string que viva tanto quanto o
documento (um literal serve). Remover um nó (`tbox_html_node_remove`) o
desconecta, mas a memória só é liberada quando o documento for destruído.

## 8. Screenshot sem janela (CI)

```c
bool ok = tbox_app_screenshot_from_files("ui.html", "ui.css", 1024, 768, "ui.png");
```

Não precisa de Wayland, só de Fontconfig. É um único frame, igual ao
primeiro que a janela mostraria. O demo faz o mesmo com
`build/example/tbox_app_demo --screenshot saida.png`.

## 9. Pipeline sem `tbox_app`

Para integrar com outro sistema de janelas, ou renderizar com uma fonte
fixa, use o `tbox_context` direto:

```c
#include <stdio.h>
#include <stdlib.h>

static void *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)n);
    if (data != NULL && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

int render_png(const char *html, const char *css, const char *ttf, const char *png) {
    size_t font_size = 0;
    void *font = read_file(ttf, &font_size);
    if (font == NULL) return 1;
    /* mesma fonte para regular e negrito; sem resolver de outras famílias */
    tbox_font_face_cache *fonts = tbox_font_face_cache_create(font, font_size, font, font_size, NULL, NULL);
    free(font); /* o cache copiou os bytes */

    tbox_context *ctx = tbox_context_open(html, strlen(html), css, strlen(css), fonts, NULL);
    const int w = 800, h = 600;
    tbox_display_list list;
    tbox_context_run_frame(ctx, w, h, &list);

    uint32_t *pixels = malloc((size_t)w * h * sizeof *pixels);
    for (size_t i = 0; i < (size_t)w * h; i++) pixels[i] = 0xFFFFFFFFu; /* fundo branco */
    tbox_raster_display_list(pixels, w, h, &list);
    bool ok = tbox_raster_write_png(png, pixels, w, h);

    free(pixels);
    tbox_context_close(ctx);             /* antes dos caches que ele emprestou */
    tbox_font_face_cache_destroy(fonts);
    return ok ? 0 : 1;
}
```

Com a fonte embutida do repositório
(`external/liberation-sans/LiberationSans-Regular.ttf`) o resultado é igual
em qualquer máquina — é assim que os testes do tbox são escritos. Para
eventos, chame `tbox_context_dispatch_click`/`_key`/`_text` com os eventos
da sua plataforma e gere um novo frame quando retornarem `true`.

## 10. Tema dos popups de controles

```c
tbox_context_options options = tbox_context_options_default();
const char *theme =
    "tbox-popup[type=color] { background-color: #20242b; color: white; }"
    "tbox-popup#minha-cor .thumb { background-color: yellow; }";
options.control_css = theme;
options.control_css_length = strlen(theme);
tbox_app *app = tbox_app_create_with_options(html, css, 800, 600, options);
```

Partes e classes disponíveis: [Formulários e controles](10-formularios-e-controles.md#tema-dos-popups).

## Exemplos executáveis no repositório

| Programa (`build/example/`) | O que mostra |
|---|---|
| `tbox_app_demo` | Demo geral de renderização, cliques, hover, bubbling, `stopPropagation`, desregistro, troca de texto. `--screenshot <png>` gera imagem; `TBOX_DEBUG_DAMAGE=1` mostra a repintura. |
| `tbox_keyboard_demo` | Formulário completo: foco por Tab, edição, seleção, clipboard, todos os tipos de input, lista rolável. |
| `tbox_control_theme_demo` | Popups de cor/data/arquivo com tema próprio (`--default` compara com o tema embutido). |
| `tbox_assets_showrun` | Navega pelos fixtures de `tests/assets` com as setas. |
| `html_parser`, `html_xpath`, `css_parser`, `css_selector`, `css_cascade`, `css_cascade_origins`, `css_style_by_node` | Uso isolado de cada camada, sem janela. |
