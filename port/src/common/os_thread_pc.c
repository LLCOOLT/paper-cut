/* Cooperative OS threads and alarms. The game's threads (DVD manager, disc
 * check) loop forever calling OSYieldThread, relying on the main thread
 * blocking in VIWaitForRetrace. Here each game thread is a coroutine that
 * runs one slice whenever the main thread yields (portThreadsRunSlice). */
#include <dolphin/os.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ucontext.h>
#endif
#include "port/os_pc.h"

/* The game's stacks (4-16 KiB) are too small for host libc calls. */
#define PORT_THREAD_STACK (512 * 1024)
#define PORT_MAX_THREADS 8
#define PORT_MAX_ALARMS 16

typedef struct PortThread {
    OSThread* thread;
    void* (*func)(void*);
    void* param;
    BOOL runnable;
    BOOL finished;
#ifdef _WIN32
    LPVOID fiber;
#else
    ucontext_t ctx;
    void* stack;
#endif
} PortThread;

static OSThread sMainThread;
static PortThread sThreads[PORT_MAX_THREADS];
static PortThread* sCurrent; /* NULL while the main thread runs */
#ifdef _WIN32
static LPVOID sMainFiber;
#else
static ucontext_t sMainCtx;
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
#ifdef _WIN32
    (void)self;
    SwitchToFiber(sMainFiber);
#else
    swapcontext(&self->ctx, &sMainCtx);
#endif
}

#ifdef _WIN32
static VOID CALLBACK threadEntry(LPVOID arg) {
    PortThread* t = (PortThread*)arg;
#else
static void threadEntry(void) {
    PortThread* t = sCurrent;
#endif
    t->func(t->param);
    t->finished = TRUE;
    t->runnable = FALSE;
    for (;;) {
        switchToMain();
    }
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
#ifdef _WIN32
    t->fiber = CreateFiber(PORT_THREAD_STACK, threadEntry, t);
    if (t->fiber == NULL) {
        t->thread = NULL;
        return FALSE;
    }
#else
    t->stack = malloc(PORT_THREAD_STACK);
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = PORT_THREAD_STACK;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, threadEntry, 0);
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
#ifdef _WIN32
    DeleteFiber(t->fiber);
#else
    free(t->stack);
#endif
    memset(t, 0, sizeof(*t));
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
#ifdef _WIN32
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
#ifdef _WIN32
        SwitchToFiber(t->fiber);
#else
        swapcontext(&sMainCtx, &t->ctx);
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
