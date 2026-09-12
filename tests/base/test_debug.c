#define _POSIX_C_SOURCE 200809L

#include <tbox/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_support.h"

/* Redirects stderr to a fresh temp file for the duration of `action`, then
 * restores it and returns everything written, NUL-terminated, in
 * `out_buffer` (truncated to out_buffer_size - 1 bytes if need be). Returns
 * false (leaving out_buffer untouched) if any of the fd juggling failed. */
static bool capture_stderr(void (*action)(void), char *out_buffer, size_t out_buffer_size) {
    fflush(stderr);
    int saved_fd = dup(fileno(stderr));
    if (saved_fd == -1) {
        return false;
    }

    char path[] = "/tmp/tbox_test_debug_XXXXXX";
    int fd      = mkstemp(path);
    if (fd == -1) {
        close(saved_fd);
        return false;
    }

    if (dup2(fd, fileno(stderr)) == -1) {
        close(fd);
        close(saved_fd);
        unlink(path);
        return false;
    }
    close(fd);

    action();

    fflush(stderr);
    dup2(saved_fd, fileno(stderr));
    close(saved_fd);

    FILE *f  = fopen(path, "r");
    size_t n = 0;
    if (f != NULL) {
        n = fread(out_buffer, 1, out_buffer_size - 1, f);
        fclose(f);
    }
    out_buffer[n] = '\0';
    unlink(path);
    return true;
}

static void log_hello_42(void) {
    tbox_log("hello %d", 42);
}

static void log_while_disabled(void) {
    tbox_log("should not appear");
}

int tbox_test_debug_run(void) {
    int failures = 0;

    /* 1: unset/empty/"0" are false; anything else is true. */
    {
        unsetenv("TBOX_TEST_DEBUG_BOOL");
        TBOX_TEST_ASSERT(!tbox_env_bool("TBOX_TEST_DEBUG_BOOL"));

        setenv("TBOX_TEST_DEBUG_BOOL", "", 1);
        TBOX_TEST_ASSERT(!tbox_env_bool("TBOX_TEST_DEBUG_BOOL"));

        setenv("TBOX_TEST_DEBUG_BOOL", "0", 1);
        TBOX_TEST_ASSERT(!tbox_env_bool("TBOX_TEST_DEBUG_BOOL"));

        setenv("TBOX_TEST_DEBUG_BOOL", "1", 1);
        TBOX_TEST_ASSERT(tbox_env_bool("TBOX_TEST_DEBUG_BOOL"));

        setenv("TBOX_TEST_DEBUG_BOOL", "yes", 1);
        TBOX_TEST_ASSERT(tbox_env_bool("TBOX_TEST_DEBUG_BOOL"));

        unsetenv("TBOX_TEST_DEBUG_BOOL");
    }

    /* 2: unset/empty fall back to the default. */
    {
        unsetenv("TBOX_TEST_DEBUG_LONG");
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 7);

        setenv("TBOX_TEST_DEBUG_LONG", "", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 7);

        unsetenv("TBOX_TEST_DEBUG_LONG");
    }

    /* 3: a valid non-negative integer parses exactly. */
    {
        setenv("TBOX_TEST_DEBUG_LONG", "1500", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 0) == 1500);
        setenv("TBOX_TEST_DEBUG_LONG", "0", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 0);
        unsetenv("TBOX_TEST_DEBUG_LONG");
    }

    /* 4: garbage, trailing garbage, and negative values all fall back to
     * the default -- a negative number is syntactically valid but rejected
     * since this is documented as "non-negative integer". */
    {
        setenv("TBOX_TEST_DEBUG_LONG", "abc", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 7);

        setenv("TBOX_TEST_DEBUG_LONG", "12abc", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 7);

        setenv("TBOX_TEST_DEBUG_LONG", "-5", 1);
        TBOX_TEST_ASSERT(tbox_env_long("TBOX_TEST_DEBUG_LONG", 7) == 7);

        unsetenv("TBOX_TEST_DEBUG_LONG");
    }

    /* 5: tbox_log_is_enabled() reflects the last tbox_log_init() call. */
    {
        tbox_log_init("test", false);
        TBOX_TEST_ASSERT(!tbox_log_is_enabled());
        tbox_log_init("test", true);
        TBOX_TEST_ASSERT(tbox_log_is_enabled());
        tbox_log_init("test", false);
    }

    /* 6: disabled logging prints nothing at all. */
    {
        char captured[256];
        tbox_log_init("test", false);
        TBOX_TEST_ASSERT(capture_stderr(log_while_disabled, captured, sizeof(captured)));
        TBOX_TEST_ASSERT(captured[0] == '\0');
    }

    /* 7: enabled logging prints "[tag] " + the formatted message + '\n'. */
    {
        char captured[256];
        tbox_log_init("mytag", true);
        TBOX_TEST_ASSERT(capture_stderr(log_hello_42, captured, sizeof(captured)));
        TBOX_TEST_ASSERT(strcmp(captured, "[mytag] hello 42\n") == 0);
        tbox_log_init("test", false);
    }

    return failures;
}
