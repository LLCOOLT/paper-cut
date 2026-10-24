/* Crash reports for bring-up: on a fatal signal/exception, print the fault
 * and a symbolized backtrace to stderr. Installed from portCrashInit(). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "port/platform.h"

/* Sound-name words (include/sfx_name_word.h) tell ids from name pointers as
 * GameCube does: a word that is non-negative as an s32 is an id (ids carry
 * flag bits up to 0x6000000). That needs every host address the game hands
 * over as a name to be 2 GiB or higher; 64-bit Windows, Linux and macOS
 * place images, heaps and stacks there, but check rather than assume. */
void portCheckHostAddress(const void* p, const char* what) {
    if ((uintptr_t)p < 0x80000000u) {
        fprintf(stderr, "fatal: %s at %p is below 2 GiB; sound names would read as ids "
                "(include/sfx_name_word.h)\n", what, p);
        fflush(stderr);
        abort();
    }
}

static void checkAddressSpace(void) {
    static const char image[] = "";
    char stack;
    void* heap = malloc(16);
    portCheckHostAddress(image, "the executable image");
    portCheckHostAddress(&stack, "the stack");
    portCheckHostAddress(heap, "the C heap");
    if (getenv("TTYD_TRACE")) {
        printf("[trace] image %p stack %p heap %p\n", (const void*)image, (void*)&stack, heap);
    }
    free(heap);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <signal.h>
#include <string.h>

static void printFrame(HANDLE proc, DWORD64 addr) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    DWORD lineDisp = 0;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    line.SizeOfStruct = sizeof(line);
    if (SymFromAddr(proc, addr, &disp, sym)) {
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
            fprintf(stderr, "  %016llx %s+0x%llx (%s:%lu)\n", (unsigned long long)addr, sym->Name,
                    (unsigned long long)disp, line.FileName, (unsigned long)line.LineNumber);
        } else {
            fprintf(stderr, "  %016llx %s+0x%llx\n", (unsigned long long)addr, sym->Name, (unsigned long long)disp);
        }
    } else {
        /* no symbol (a system DLL): its module, e.g. where a stalled thread waits */
        HMODULE mod = NULL;
        char name[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)addr, &mod) &&
            GetModuleFileNameA(mod, name, sizeof(name)) != 0) {
            const char* base = strrchr(name, '\\');
            fprintf(stderr, "  %016llx %s+0x%llx\n", (unsigned long long)addr, base != NULL ? base + 1 : name,
                    (unsigned long long)(addr - (DWORD64)(uintptr_t)mod));
        } else {
            fprintf(stderr, "  %016llx ?\n", (unsigned long long)addr);
        }
    }
}

static LONG WINAPI onException(EXCEPTION_POINTERS* info) {
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    CONTEXT ctx = *info->ContextRecord;
    STACKFRAME64 frame = {0};
    EXCEPTION_RECORD* rec = info->ExceptionRecord;
    int i;

    fprintf(stderr, "\n*** crash: exception 0x%08lx at %p", (unsigned long)rec->ExceptionCode, rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        fprintf(stderr, " (%s 0x%llx)", rec->ExceptionInformation[0] ? "write to" : "read from",
                (unsigned long long)rec->ExceptionInformation[1]);
    }
    fprintf(stderr, "\n");
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    for (i = 0; i < 48; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &frame, &ctx, NULL, SymFunctionTableAccess64,
                         SymGetModuleBase64, NULL) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        printFrame(proc, frame.AddrPC.Offset);
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* abort() (also std::terminate, an uncaught C++ exception in Aurora or Dawn)
 * raises SIGABRT and then fast-fails (0xC0000409), which no exception filter
 * or debugger sees: print where it came from first. */
static void onAbort(int sig) {
    HANDLE proc = GetCurrentProcess();
    void* frames[48];
    USHORT n, i;
    (void)sig;
    fflush(stdout);
    fprintf(stderr, "\n*** crash: abort()\n");
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    n = CaptureStackBackTrace(1, 48, frames, NULL);
    for (i = 0; i < n; i++) {
        printFrame(proc, (DWORD64)(uintptr_t)frames[i]);
    }
    fflush(stderr);
}

/* The C runtime reports invalid arguments (e.g. a bad printf format pointer)
 * by ending the process silently: print where it happened first. */
static void onInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line,
                               uintptr_t reserved) {
    HANDLE proc = GetCurrentProcess();
    void* frames[48];
    USHORT n, i;
    (void)expr, (void)func, (void)file, (void)line, (void)reserved;
    fprintf(stderr, "\n*** crash: invalid argument to a C runtime function\n");
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    n = CaptureStackBackTrace(1, 48, frames, NULL);
    for (i = 0; i < n; i++) {
        printFrame(proc, (DWORD64)(uintptr_t)frames[i]);
    }
    fflush(stderr);
    abort();
}

/* $TTYD_WATCHDOG=<seconds>: if no frame completes for that long, print the
 * main thread's stack (where the game is stuck) and exit with status 4. */
static volatile LONG sFrames;
static HANDLE sMainThread;

void portWatchdogFrame(void) {
    InterlockedIncrement(&sFrames);
}

static DWORD WINAPI watchdog(void* arg) {
    int secs = (int)(intptr_t)arg, idle = 0;
    LONG last = -1;
    for (;;) {
        Sleep(1000);
        if (sFrames != last || portIsPaused()) {
            last = sFrames;
            idle = 0;
        } else if (++idle >= secs) {
            HANDLE proc = GetCurrentProcess();
            CONTEXT ctx;
            STACKFRAME64 frame = {0};
            int i;
            SuspendThread(sMainThread);
            memset(&ctx, 0, sizeof(ctx));
            ctx.ContextFlags = CONTEXT_FULL;
            GetThreadContext(sMainThread, &ctx);
            fflush(stdout);
            fprintf(stderr, "\n*** stall: no frame for %d s; main thread:\n", secs);
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(proc, NULL, TRUE);
            printFrame(proc, ctx.Rip); /* even if the walk below cannot start (a thread in a system wait) */
            frame.AddrPC.Offset = ctx.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = ctx.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = ctx.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
            for (i = 0; i < 48; i++) {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, sMainThread, &frame, &ctx, NULL,
                                 SymFunctionTableAccess64, SymGetModuleBase64, NULL) ||
                    frame.AddrPC.Offset == 0) {
                    break;
                }
                printFrame(proc, frame.AddrPC.Offset);
            }
            fflush(stderr);
            _Exit(4);
        }
    }
    return 0;
}

void portCrashInit(void) {
    const char* wd = getenv("TTYD_WATCHDOG");
    checkAddressSpace();
    if (getenv("TTYD_NO_CRASH_HANDLER") == NULL) {
        SetUnhandledExceptionFilter(onException);
        _set_invalid_parameter_handler(onInvalidParameter);
        signal(SIGABRT, onAbort);
    }
    if (wd != NULL && atoi(wd) > 0) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sMainThread, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        CreateThread(NULL, 0, watchdog, (void*)(intptr_t)atoi(wd), 0, NULL);
    }
}

const char* portSymbolName(const void* addr, char* buf, size_t size) {
    static int init;
    char sbuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)sbuf;
    DWORD64 disp = 0;
    if (!init) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
        init = 1;
    }
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    if (addr != NULL && SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)addr, &disp, sym)) {
        snprintf(buf, size, "%s+0x%llx", sym->Name, (unsigned long long)disp);
    } else {
        snprintf(buf, size, "%p", addr);
    }
    return buf;
}

#else
#include <signal.h>
#include <string.h>
#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#endif
#include <unistd.h>

void portWatchdogFrame(void) {} /* $TTYD_WATCHDOG: Windows only for now */

static void onSignal(int sig, siginfo_t* si, void* uc) {
    (void)uc;
    fprintf(stderr, "\n*** crash: signal %d (%s) at address %p\n", sig, strsignal(sig), si->si_addr);
#if defined(__GLIBC__) || defined(__APPLE__)
    {
        void* frames[48];
        int n = backtrace(frames, 48);
        backtrace_symbols_fd(frames, n, 2);
    }
#endif
    signal(sig, SIG_DFL);
    raise(sig);
}

void portCrashInit(void) {
    struct sigaction sa;
    checkAddressSpace();
    if (getenv("TTYD_NO_CRASH_HANDLER") != NULL) {
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = onSignal;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}

const char* portSymbolName(const void* addr, char* buf, size_t size) {
    snprintf(buf, size, "%p", addr);
    return buf;
}
#endif
