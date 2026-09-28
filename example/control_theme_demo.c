#include <tbox/tbox.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TBOX_CONTROL_THEME_HTML_PATH
#error "TBOX_CONTROL_THEME_HTML_PATH is required"
#endif
#ifndef TBOX_CONTROL_THEME_PAGE_CSS_PATH
#error "TBOX_CONTROL_THEME_PAGE_CSS_PATH is required"
#endif
#ifndef TBOX_CONTROL_THEME_CSS_PATH
#error "TBOX_CONTROL_THEME_CSS_PATH is required"
#endif

/* The theme is read separately from the page stylesheet. The application
 * copies it during creation, so the file buffer can be freed immediately. */
static char *read_theme(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long size = ftell(file);
    if (size < 0 || (unsigned long)size >= SIZE_MAX) { fclose(file); return NULL; }
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    char *buffer = malloc((size_t)size + 1);
    if (buffer == NULL) { fclose(file); return NULL; }
    size_t read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) { free(buffer); return NULL; }
    buffer[read_size] = '\0';
    *length = read_size;
    return buffer;
}

static void on_input(tbox_context *ctx, tbox_html_node *input,
                     tbox_string_view value, void *userdata) {
    (void)ctx;
    (void)userdata;
    const tbox_html_attribute *id = tbox_html_node_get_attribute(input,
        tbox_string_view_make("id", 2));
    printf("%.*s = %.*s\n", id != NULL ? (int)id->value.size : 0,
        id != NULL ? id->value.data : "", (int)value.size,
        value.data != NULL ? value.data : "");
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        printf("Usage: %s [--default | control-theme.css]\n", argv[0]);
        printf("No argument uses the example theme; --default uses the built-in theme.\n");
        return argc > 2 ? 1 : 0;
    }
    if (!tbox_app_backend_available()) {
        fputs("An interactive window backend is required for this example.\n", stderr);
        return 1;
    }

    tbox_context_options options = tbox_context_options_default();
    char *theme = NULL;
    if (argc == 1 || strcmp(argv[1], "--default") != 0) {
        const char *theme_path = argc == 2 ? argv[1] : TBOX_CONTROL_THEME_CSS_PATH;
        size_t theme_length = 0;
        theme = read_theme(theme_path, &theme_length);
        if (theme == NULL) {
            fprintf(stderr, "Could not read theme: %s\n", theme_path);
            return 1;
        }
        options.control_css = theme;
        options.control_css_length = theme_length;
    }

    tbox_app *app = tbox_app_create_from_files_with_options(
        TBOX_CONTROL_THEME_HTML_PATH, TBOX_CONTROL_THEME_PAGE_CSS_PATH,
        620, 720, options);
    free(theme);
    if (app == NULL) {
        fputs("Could not create the example window.\n", stderr);
        return 1;
    }
    tbox_context_on_input(tbox_app_context(app), on_input, NULL);
    while (!tbox_app_should_close(app)) tbox_app_step(app);
    tbox_app_close(app);
    return 0;
}
