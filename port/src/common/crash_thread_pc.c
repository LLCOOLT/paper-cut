/* portCrashThreadInit (port/include/port/platform.h): the crash handler's
 * alternate signal stack for a thread the port starts. sigaltstack is per
 * thread and portCrashInit gives the main thread its own; without one, a
 * stack overflow on another thread (the game's OS threads, os_thread_pc.c;
 * work in portDetach's threads) ran the handler on the overflowed stack and
 * died with no report. Freed when the thread ends. Windows has no such
 * stack (its handler runs on the faulting thread's guard page): a no-op. */
#include "port/platform.h"

#if defined(_WIN32) || defined(__EMSCRIPTEN__)
void portCrashThreadInit(void) {
}
#else
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define ALT_STACK_SIZE (128 * 1024)

static pthread_key_t sKey;
static pthread_once_t sOnce = PTHREAD_ONCE_INIT;
static int sKeyOk;

static void altStackDone(void* mem) {
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    sigaltstack(&ss, NULL);
    free(mem);
}

static void makeKey(void) {
    sKeyOk = pthread_key_create(&sKey, altStackDone) == 0;
}

void portCrashThreadInit(void) {
    stack_t cur, ss;
    if (getenv("TTYD_NO_CRASH_HANDLER") != NULL) {
        return;
    }
    pthread_once(&sOnce, makeKey);
    if (!sKeyOk || (sigaltstack(NULL, &cur) == 0 && (cur.ss_flags & SS_DISABLE) == 0)) {
        return; /* (one already: the main thread's, or this thread's) */
    }
    memset(&ss, 0, sizeof(ss));
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_sp = malloc(ss.ss_size);
    if (ss.ss_sp == NULL) {
        return;
    }
    if (sigaltstack(&ss, NULL) != 0) {
        free(ss.ss_sp);
    } else if (pthread_setspecific(sKey, ss.ss_sp) != 0) {
        altStackDone(ss.ss_sp);
    }
}
#endif
