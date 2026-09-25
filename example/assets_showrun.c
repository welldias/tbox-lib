#define _POSIX_C_SOURCE 200809L

#include <tbox/app.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef TBOX_SHOWRUN_ASSETS_DIR
#error "TBOX_SHOWRUN_ASSETS_DIR is required"
#endif

typedef struct showrun {
    const char *directory;
    char **names;
    size_t count;
    size_t current;
} showrun;

static char *showrun_path(const char *directory, const char *name) {
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    if (directory_length > SIZE_MAX - name_length - 2) return NULL;
    char *path = malloc(directory_length + name_length + 2);
    if (path == NULL) return NULL;
    memcpy(path, directory, directory_length);
    path[directory_length] = '/';
    memcpy(path + directory_length + 1, name, name_length + 1);
    return path;
}

static int showrun_compare_names(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static void showrun_destroy(showrun *state) {
    for (size_t i = 0; i < state->count; i++) free(state->names[i]);
    free(state->names);
    state->names = NULL;
    state->count = 0;
}

static bool showrun_scan(showrun *state) {
    DIR *directory = opendir(state->directory);
    if (directory == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", state->directory, strerror(errno));
        return false;
    }
    size_t capacity = 0;
    bool complete = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) complete = false;
            break;
        }
        size_t length = strlen(entry->d_name);
        if (length < 6 || strcmp(entry->d_name + length - 5, ".html") != 0) continue;
        char *path = showrun_path(state->directory, entry->d_name);
        if (path == NULL) { complete = false; break; }
        struct stat metadata;
        bool regular = stat(path, &metadata) == 0 && S_ISREG(metadata.st_mode);
        free(path);
        if (!regular) continue;
        if (state->count == capacity) {
            size_t next = capacity == 0 ? 16 : capacity * 2;
            if (next < capacity || next > SIZE_MAX / sizeof(*state->names)) { complete = false; break; }
            char **names = realloc(state->names, next * sizeof(*state->names));
            if (names == NULL) { complete = false; break; }
            state->names = names;
            capacity = next;
        }
        state->names[state->count] = malloc(length + 1);
        if (state->names[state->count] == NULL) { complete = false; break; }
        memcpy(state->names[state->count], entry->d_name, length + 1);
        state->count++;
    }
    if (closedir(directory) != 0) complete = false;
    if (!complete) {
        fputs("Could not scan all HTML files\n", stderr);
        showrun_destroy(state);
        return false;
    }
    qsort(state->names, state->count, sizeof(*state->names), showrun_compare_names);
    return true;
}

static void showrun_announce(const showrun *state) {
    printf("[%zu/%zu] %s\n", state->current + 1, state->count, state->names[state->current]);
    fflush(stdout);
}

static bool showrun_on_key(tbox_app *app, tbox_key_event event, void *userdata) {
    if (!event.pressed || (event.key != TBOX_KEY_RIGHT && event.key != TBOX_KEY_LEFT)) return false;
    showrun *state = userdata;
    size_t next = state->current;
    if (event.key == TBOX_KEY_RIGHT && next + 1 < state->count) next++;
    if (event.key == TBOX_KEY_LEFT && next > 0) next--;
    if (next != state->current) {
        char *path = showrun_path(state->directory, state->names[next]);
        bool loaded = path != NULL && tbox_app_load_from_files(app, path, NULL);
        if (loaded) {
            state->current = next;
            showrun_announce(state);
        } else {
            fprintf(stderr, "Could not load %s\n", path != NULL ? path : state->names[next]);
        }
        free(path);
    }
    return true;
}

int main(int argc, char **argv) {
    bool list_only = argc > 1 && strcmp(argv[1], "--list") == 0;
    if (argc > (list_only ? 3 : 2)) {
        fprintf(stderr, "Usage: %s [--list] [assets-directory]\n", argv[0]);
        return 1;
    }
    showrun state = {.directory = argc > (list_only ? 2 : 1) ? argv[argc - 1] : TBOX_SHOWRUN_ASSETS_DIR};
    if (!showrun_scan(&state)) return 1;
    if (state.count == 0) {
        fprintf(stderr, "No .html files in %s\n", state.directory);
        showrun_destroy(&state);
        return 1;
    }
    if (list_only) {
        for (size_t i = 0; i < state.count; i++) puts(state.names[i]);
        showrun_destroy(&state);
        return 0;
    }
    if (!tbox_app_backend_available()) {
        fputs("No interactive window backend in this build\n", stderr);
        showrun_destroy(&state);
        return 1;
    }
    char *first_path = showrun_path(state.directory, state.names[0]);
    tbox_app *app = first_path != NULL ? tbox_app_create_from_files(first_path, NULL, 900, 700) : NULL;
    free(first_path);
    if (app == NULL) {
        fprintf(stderr, "Could not open %s\n", state.names[0]);
        showrun_destroy(&state);
        return 1;
    }
    showrun_announce(&state);
    tbox_app_on_key(app, showrun_on_key, &state);
    while (!tbox_app_should_close(app)) {
        tbox_app_step(app);
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 8L * 1000L * 1000L};
        nanosleep(&delay, NULL);
    }
    tbox_app_close(app);
    showrun_destroy(&state);
    return 0;
}
