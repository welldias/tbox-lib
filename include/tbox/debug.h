#ifndef TBOX_DEBUG_H
#define TBOX_DEBUG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Small, opt-in debug helpers meant for example programs and other
 * command-line tools built on tbox -- the library itself never calls any of
 * these, so linking against tbox never logs anything or reads the
 * environment on its own. Not thread-safe: like the rest of tbox, there is
 * no internal locking.
 *
 * Typical use, once at startup:
 *   tbox_log_init("my_example", tbox_env_bool("MY_EXAMPLE_DEBUG"));
 *   long close_delay_ms = tbox_env_long("MY_EXAMPLE_CLOSE_DELAY_MS", 0);
 * and then tbox_log(...) anywhere that's useful to trace. */

/* Reads environment variable `name` as a boolean flag: unset, empty, or "0"
 * count as false; any other value counts as true. The common
 * `SOMETHING_DEBUG=1` pattern. */
bool tbox_env_bool(const char *name);

/* Reads environment variable `name` as a non-negative integer, returning
 * `default_value` if it's unset, empty, or not a valid non-negative integer
 * (the last case also prints a warning to stderr, since it likely means a
 * typo in the variable's value rather than an intentional default). */
long tbox_env_long(const char *name, long default_value);

/* Configures tbox_log(): `tag` prefixes every subsequent message as
 * "[tag] ", and `enabled` gates whether tbox_log() prints anything at all.
 * `tag` is stored by pointer, not copied -- pass a string literal or
 * something else that outlives every later tbox_log() call. Before the
 * first call to tbox_log_init(), logging is disabled and the tag is
 * "tbox". */
void tbox_log_init(const char *tag, bool enabled);

/* Whether tbox_log() currently prints anything -- the `enabled` value from
 * the last tbox_log_init() call (false if that was never called). Useful to
 * skip expensive log-argument formatting when logging is off, e.g.:
 *   if (tbox_log_is_enabled()) {
 *       char name[64];
 *       expensive_describe(value, name, sizeof(name));
 *       tbox_log("value: %s", name);
 *   } */
bool tbox_log_is_enabled(void);

/* Printf-style logging to stderr, prefixed with "[tag] " (see
 * tbox_log_init) and newline-terminated. A silent no-op unless enabled. */
void tbox_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* TBOX_DEBUG_H */
