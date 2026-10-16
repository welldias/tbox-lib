#ifndef TBOX_MUTEX_H
#define TBOX_MUTEX_H

#include <pthread.h>
#include <stdbool.h>

/* Minimal mutex for the few places that accept calls from any thread --
 * today only tbox_app_post's per-app queue (see "Loop da aplicação e
 * threads" in docs/dev/decisoes.md). POSIX threads cover Linux and macOS;
 * a Windows backend adds an SRWLOCK variant here. */
typedef struct tbox_mutex {
    pthread_mutex_t handle;
} tbox_mutex;

static inline bool tbox_mutex_init(tbox_mutex *mutex) {
    return pthread_mutex_init(&mutex->handle, NULL) == 0;
}

static inline void tbox_mutex_destroy(tbox_mutex *mutex) {
    pthread_mutex_destroy(&mutex->handle);
}

static inline void tbox_mutex_lock(tbox_mutex *mutex) {
    pthread_mutex_lock(&mutex->handle);
}

static inline void tbox_mutex_unlock(tbox_mutex *mutex) {
    pthread_mutex_unlock(&mutex->handle);
}

#endif
