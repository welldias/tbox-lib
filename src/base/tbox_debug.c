#include <tbox/debug.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_tbox_log_enabled    = false;
static const char *g_tbox_log_tag = "tbox";

bool tbox_env_bool(const char *name) {
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

long tbox_env_long(const char *name, long default_value) {
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }

    char *end   = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 0) {
        fprintf(stderr, "invalid value for %s: \"%s\" (ignoring)\n", name, value);
        return default_value;
    }
    return parsed;
}

void tbox_log_init(const char *tag, bool enabled) {
    g_tbox_log_tag     = (tag != NULL) ? tag : "tbox";
    g_tbox_log_enabled = enabled;
}

bool tbox_log_is_enabled(void) {
    return g_tbox_log_enabled;
}

void tbox_log(const char *fmt, ...) {
    if (!g_tbox_log_enabled) {
        return;
    }

    fprintf(stderr, "[%s] ", g_tbox_log_tag);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}
