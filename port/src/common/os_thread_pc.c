/* Cooperative OS threads and alarms. The game's threads (DVD manager, disc
 * check) loop forever calling OSYieldThread, relying on the main thread
 * blocking in VIWaitForRetrace. Here each game thread is a coroutine that
 * runs one slice whenever the main thread yields (portThreadsRunSlice).
 *
 * Coroutines: Windows fibers; ucontext on glibc; elsewhere (Apple, whose
 * ucontext needs _XOPEN_SOURCE before every system header; Android and musl,
 * which have no makecontext) one host thread per game thread, with a baton so
 * that exactly one of them, or the main thread, runs at a time. */
#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#define PORT_CORO_FIBER 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__GLIBC__) && !defined(PORT_CORO_PTHREAD)
#define PORT_CORO_UCONTEXT 1
#include <ucontext.h>
#else
#ifndef PORT_CORO_PTHREAD
#define PORT_CORO_PTHREAD 1
#endif
#include <pthread.h>
#endif
#include "port/os_pc.h"

#ifdef PORT_CTX_ARM64
/* Saves x19-x30 and d8-d15 on the current stack, stores sp in *save, loads
 * sp = to and restores the same frame from there; "returns" into the other
 * context. A new context's frame has x30 = its entry and x29 = 0. */
void portCtxSwap(void** save, void* to);
__asm__(".text\n"
        ".p2align 2\n"
        ".private_extern _portCtxSwap\n"
        "_portCtxSwap:\n"
        "    sub sp, sp, #160\n"
        "    stp x19, x20, [sp, #0]\n"
        "    stp x21, x22, [sp, #16]\n"
        "    stp x23, x24, [sp, #32]\n"
        "    stp x25, x26, [sp, #48]\n"
        "    stp x27, x28, [sp, #64]\n"
        "    stp x29, x30, [sp, #80]\n"
        "    stp d8, d9, [sp, #96]\n"
        "    stp d10, d11, [sp, #112]\n"
        "    stp d12, d13, [sp, #128]\n"
        "    stp d14, d15, [sp, #144]\n"
        "    mov x9, sp\n"
        "    str x9, [x0]\n"
        "    mov sp, x1\n"
        "    ldp x19, x20, [sp, #0]\n"
        "    ldp x21, x22, [sp, #16]\n"
        "    ldp x23, x24, [sp, #32]\n"
        "    ldp x25, x26, [sp, #48]\n"
        "    ldp x27, x28, [sp, #64]\n"
        "    ldp x29, x30, [sp, #80]\n"
        "    ldp d8, d9, [sp, #96]\n"
        "    ldp d10, d11, [sp, #112]\n"
        "    ldp d12, d13, [sp, #128]\n"
        "    ldp d14, d15, [sp, #144]\n"
        "    add sp, sp, #160\n"
        "    ret\n");
#endif

/* The game's stacks (4-16 KiB) are too small for host libc calls. */
#define PORT_THREAD_STACK (512 * 1024)
#define PORT_MAX_THREADS 8
#define PORT_MAX_ALARMS 16

#ifdef PORT_CORO_PTHREAD
/* A game thread's host thread. Heap-allocated and owned by that thread once
 * it is cancelled or finished, so the slot (PortThread) can be reused. */
typedef struct PortCoro {
    pthread_cond_t wake;
    BOOL cancelled;
    struct PortThread* owner;
} PortCoro;
#endif

typedef struct PortThread {
    OSThread* thread;
    void* (*func)(void*);
    void* param;
    BOOL runnable;
    BOOL finished;
#if defined(PORT_CORO_FIBER)
    LPVOID fiber;
#elif defined(PORT_CORO_UCONTEXT)
    ucontext_t ctx;
    void* stack;
#else
    PortCoro* coro;
#endif
} PortThread;

static OSThread sMainThread;
static PortThread sThreads[PORT_MAX_THREADS];
static PortThread* sCurrent; /* NULL while the main thread runs */
#if defined(PORT_CORO_FIBER)
static LPVOID sMainFiber;
#elif defined(PORT_CORO_UCONTEXT)
static ucontext_t sMainCtx;
#else
static pthread_mutex_t sCoroLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sMainWake = PTHREAD_COND_INITIALIZER;
static PortCoro* sTurn; /* the host thread that may run; NULL: the main thread */

/* With sCoroLock held: park until it is this coroutine's turn. A cancelled
 * coroutine's host thread ends here (its stack is dropped, as a deleted fiber's). */
static void coroWait(PortCoro* c) {
    while (sTurn != c || c->cancelled) {
        if (c->cancelled) {
            pthread_mutex_unlock(&sCoroLock);
            pthread_cond_destroy(&c->wake);
            free(c);
            pthread_exit(NULL);
        }
        pthread_cond_wait(&c->wake, &sCoroLock);
    }
}

/* From the main thread: run c until it hands the baton back. */
static void coroRun(PortCoro* c) {
    pthread_mutex_lock(&sCoroLock);
    sTurn = c;
    pthread_cond_signal(&c->wake);
    while (sTurn != NULL) {
        pthread_cond_wait(&sMainWake, &sCoroLock);
    }
    pthread_mutex_unlock(&sCoroLock);
}
#endif

static OSAlarm* sAlarms[PORT_MAX_ALARMS];

static PortThread* findThread(OSThread* thread) {
    int i;
    for (i = 0; i < PORT_MAX_THREADS; i++) {
        if (sThreads[i].thread == thread) {
            return &sThreads[i];
        }
    }
    return NULL;
}

static void switchToMain(void) {
    PortThread* self = sCurrent;
    sCurrent = NULL;
#if defined(PORT_CORO_FIBER)
    (void)self;
    SwitchToFiber(sMainFiber);
#elif defined(PORT_CORO_UCONTEXT)
    swapcontext(&self->ctx, &sMainCtx);
#else
    {
        PortCoro* c = self->coro;
        pthread_mutex_lock(&sCoroLock);
        sTurn = NULL;
        pthread_cond_signal(&sMainWake);
        coroWait(c);
        pthread_mutex_unlock(&sCoroLock);
    }
#endif
}

#if defined(PORT_CORO_FIBER)
static VOID CALLBACK threadEntry(LPVOID arg) {
    PortThread* t = (PortThread*)arg;
#elif defined(PORT_CORO_UCONTEXT)
static void threadEntry(void) {
    PortThread* t = sCurrent;
#else
static void* threadEntry(void* arg) {
    PortCoro* c = (PortCoro*)arg;
    PortThread* t;
    pthread_mutex_lock(&sCoroLock);
    coroWait(c);
    pthread_mutex_unlock(&sCoroLock);
    t = c->owner;
#endif
    t->func(t->param);
    t->finished = TRUE;
    t->runnable = FALSE;
#ifdef PORT_CORO_PTHREAD
    /* Done for good: give the baton back and end the host thread (the slot
     * no longer refers to it). */
    pthread_mutex_lock(&sCoroLock);
    t->coro = NULL;
    sCurrent = NULL;
    sTurn = NULL;
    pthread_cond_signal(&sMainWake);
    pthread_mutex_unlock(&sCoroLock);
    pthread_cond_destroy(&c->wake);
    free(c);
    return NULL;
#else
    for (;;) {
        switchToMain();
    }
#endif
}

int OSCreateThread(OSThread* thread, void* (*func)(void*), void* param, void* stack, u32 stackSize,
                   OSPriority priority, u16 attr) {
    PortThread* t = findThread(NULL);
    if (t == NULL) {
        return FALSE;
    }
    memset(thread, 0, sizeof(*thread));
    thread->priority = thread->base = priority;
    thread->attr = attr;
    thread->suspend = 1;
    thread->state = OS_THREAD_STATE_READY;
    t->thread = thread;
    t->func = func;
    t->param = param;
    t->runnable = FALSE;
    t->finished = FALSE;
#if defined(PORT_CORO_FIBER)
    t->fiber = CreateFiber(PORT_THREAD_STACK, threadEntry, t);
    if (t->fiber == NULL) {
        t->thread = NULL;
        return FALSE;
    }
#elif defined(PORT_CORO_UCONTEXT)
    t->stack = malloc(PORT_THREAD_STACK);
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = PORT_THREAD_STACK;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, threadEntry, 0);
#else
    {
        pthread_attr_t pattr;
        pthread_t host;
        PortCoro* c = (PortCoro*)calloc(1, sizeof(PortCoro));
        int err;
        if (c == NULL) {
            t->thread = NULL;
            return FALSE;
        }
        pthread_cond_init(&c->wake, NULL);
        c->owner = t;
        t->coro = c;
        pthread_attr_init(&pattr);
        pthread_attr_setstacksize(&pattr, PORT_THREAD_STACK);
        pthread_attr_setdetachstate(&pattr, PTHREAD_CREATE_DETACHED);
        err = pthread_create(&host, &pattr, threadEntry, c);
        pthread_attr_destroy(&pattr);
        if (err != 0) {
            pthread_cond_destroy(&c->wake);
            free(c);
            t->coro = NULL;
            t->thread = NULL;
            return FALSE;
        }
    }
#endif
    return TRUE;
}

s32 OSResumeThread(OSThread* thread) {
    PortThread* t = findThread(thread);
    s32 prev = thread->suspend;
    if (thread->suspend > 0) {
        thread->suspend--;
    }
    if (t != NULL && thread->suspend == 0 && !t->finished) {
        t->runnable = TRUE;
    }
    return prev;
}

void OSCancelThread(OSThread* thread) {
    PortThread* t = findThread(thread);
    if (t == NULL) {
        return;
    }
    /* Coroutines are only ever cancelled from the main thread here. */
    t->runnable = FALSE;
    t->finished = TRUE;
    thread->state = OS_THREAD_STATE_MORIBUND;
#if defined(PORT_CORO_FIBER)
    DeleteFiber(t->fiber);
#elif defined(PORT_CORO_UCONTEXT)
    free(t->stack);
#else
    /* parked (or not started): it wakes, frees its PortCoro and ends */
    if (t->coro != NULL) {
        pthread_mutex_lock(&sCoroLock);
        t->coro->cancelled = TRUE;
        pthread_cond_signal(&t->coro->wake);
        pthread_mutex_unlock(&sCoroLock);
    }
#endif
    memset(t, 0, sizeof(*t));
}

BOOL portThreadIsMain(void) {
    return sCurrent == NULL;
}

OSThread* OSGetCurrentThread(void) {
    return sCurrent != NULL ? sCurrent->thread : &sMainThread;
}

static void fireAlarms(void) {
    OSTime now = OSGetTime();
    int i;
    for (i = 0; i < PORT_MAX_ALARMS; i++) {
        OSAlarm* alarm = sAlarms[i];
        if (alarm != NULL && now >= alarm->fire) {
            OSAlarmHandler handler = alarm->handler;
            if (alarm->period > 0) {
                alarm->fire += alarm->period;
            } else {
                sAlarms[i] = NULL;
                alarm->handler = NULL;
            }
            if (handler != NULL) {
                handler(alarm, NULL);
            }
        }
    }
}

void portThreadsRunSlice(void) {
    int i;
    if (sCurrent != NULL) {
        return;
    }
#if defined(PORT_CORO_FIBER)
    if (sMainFiber == NULL) {
        sMainFiber = ConvertThreadToFiber(NULL);
    }
#endif
    fireAlarms();
    for (i = 0; i < PORT_MAX_THREADS; i++) {
        PortThread* t = &sThreads[i];
        if (t->thread == NULL || !t->runnable) {
            continue;
        }
        sCurrent = t;
        t->thread->state = OS_THREAD_STATE_RUNNING;
#if defined(PORT_CORO_FIBER)
        SwitchToFiber(t->fiber);
#elif defined(PORT_CORO_UCONTEXT)
        swapcontext(&sMainCtx, &t->ctx);
#else
        coroRun(t->coro);
#endif
        if (t->thread != NULL && !t->finished) {
            t->thread->state = OS_THREAD_STATE_READY;
        }
    }
}

void OSYieldThread(void) {
    if (sCurrent != NULL) {
        switchToMain();
    } else {
        portThreadsRunSlice();
    }
}

void OSCreateAlarm(OSAlarm* alarm) {
    memset(alarm, 0, sizeof(*alarm));
}

static void addAlarm(OSAlarm* alarm) {
    int i;
    for (i = 0; i < PORT_MAX_ALARMS; i++) {
        if (sAlarms[i] == alarm || sAlarms[i] == NULL) {
            sAlarms[i] = alarm;
            return;
        }
    }
    OSPanic(__FILE__, __LINE__, "too many alarms");
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
    alarm->handler = handler;
    alarm->period = 0;
    alarm->start = OSGetTime();
    alarm->fire = alarm->start + tick;
    addAlarm(alarm);
}

void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
    alarm->handler = handler;
    alarm->period = period;
    alarm->start = start;
    alarm->fire = start + period;
    addAlarm(alarm);
}

void OSCancelAlarm(OSAlarm* alarm) {
    int i;
    for (i = 0; i < PORT_MAX_ALARMS; i++) {
        if (sAlarms[i] == alarm) {
            sAlarms[i] = NULL;
        }
    }
    alarm->handler = NULL;
}
