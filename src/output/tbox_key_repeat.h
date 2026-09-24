#ifndef TBOX_KEY_REPEAT_H
#define TBOX_KEY_REPEAT_H

#include <stdint.h>
#include <stddef.h>

/* Advances a monotonic repeat deadline and returns the number of events due.
 * A slow frame catches up by at most eight events before resuming at the
 * current time. A rate of zero disables repetition. */
static inline int tbox_key_repeat_due(uint64_t now_ns, uint64_t *next_ns, int32_t rate) {
    if (next_ns == NULL || rate <= 0 || now_ns < *next_ns) return 0;
    uint64_t interval = 1000000000u / (uint64_t)rate;
    if (interval < 1000000u) interval = 1000000u;
    int count = 0;
    while (count < 8 && now_ns >= *next_ns) {
        count++;
        *next_ns += interval;
    }
    if (*next_ns <= now_ns) *next_ns = now_ns + interval;
    return count;
}

#endif
