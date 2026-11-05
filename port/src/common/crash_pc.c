/* Crash reports for bring-up: on a fatal signal/exception, print the fault
 * and a symbolized backtrace to stderr. Installed from portCrashInit(). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "port/platform.h"

/* The crash report file (portCrashSetReportPath): what is printed for a
 * crash or stall also goes there, after a header with the build and where
 * the game was (portCrashSetContext), so it can be copied from the game at
 * the next start (Aurora: the settings page) or found in the save folder. */
static char sReportPath[1024];
static char sContext[200];
static char sMessage[400];

void portCrashSetMessage(const char* text) {
    snprintf(sMessage, sizeof(sMessage), "%s", text);
}

void portCrashSetReportPath(const char* path) {
    snprintf(sReportPath, sizeof(sReportPath), "%s", path != NULL ? path : "");
}

void portCrashSetContext(const char* text) {
    snprintf(sContext, sizeof(sContext), "%s", text);
}

/* the last log lines (a ring), written at the report's end: what led up to it */
#define LOG_LINES 24
static char sLog[LOG_LINES][240];
static unsigned sLogNext;
static char sGpu[160];

void portCrashLogLine(const char* text) {
    /* (any thread logs: each line claims its own slot) */
    char* line = sLog[__atomic_fetch_add(&sLogNext, 1, __ATOMIC_RELAXED) % LOG_LINES];
    size_t i;
    snprintf(line, sizeof(sLog[0]), "%s", text != NULL ? text : "");
    for (i = 0; line[i] != '\0'; i++) {
        if (line[i] == '\n' || line[i] == '\r') {
            line[i] = ' '; /* one line each */
        }
    }
}

void portCrashSetGpu(const char* text) {
    snprintf(sGpu, sizeof(sGpu), "%s", text != NULL ? text : "");
}

const char* portCrashGpu(void) {
    return sGpu;
}

/* the ring's lines, oldest first, through put */
static void logTail(void (*put)(const char* line)) {
    unsigned i, first = sLogNext > LOG_LINES ? sLogNext - LOG_LINES : 0;
    if (sLogNext == 0) {
        return;
    }
    put("\nlast log lines:\n");
    for (i = first; i < sLogNext; i++) {
        put(sLog[i % LOG_LINES]);
        put("\n");
    }
}

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
    char stack = 0; /* (only its address is used) */
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
#include <stdarg.h>
#include <string.h>

static FILE* sReport;

/* stderr, and the report file while one is open */
static void out(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    if (sReport != NULL) {
        va_start(ap, fmt);
        vfprintf(sReport, fmt, ap);
        va_end(ap);
    }
}

/* Starts the report file: the build, where the game was, the message, and
 * where the executable is loaded (offsets symbolize against that build). */
static void reportBegin(const char* what) {
    if (sReportPath[0] == '\0' || sReport != NULL) {
        return;
    }
    sReport = fopen(sReportPath, "w");
    if (sReport == NULL) {
        return;
    }
    fprintf(sReport, "Paper Cut %s report\nversion: %s, built %s %s\ngame: %s\n%s%s%s%s%s%simage: %p\n", what,
            PORT_REGION_SHORT " (" PORT_GAME_ID ")",
            __DATE__, __TIME__, sContext[0] != '\0' ? sContext : "?", sGpu[0] != '\0' ? "gpu: " : "", sGpu,
            sGpu[0] != '\0' ? "\n" : "", sMessage[0] != '\0' ? "message: " : "",
            sMessage, sMessage[0] != '\0' ? "\n" : "", (void*)GetModuleHandleA(NULL));
}

static void putReport(const char* line) {
    fputs(line, sReport);
}

static void reportEnd(void) {
    if (sReport != NULL) {
        logTail(putReport);
        fflush(sReport);
        fclose(sReport);
        sReport = NULL;
    }
}

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
            out("  %016llx %s+0x%llx (%s:%lu)\n", (unsigned long long)addr, sym->Name,
                    (unsigned long long)disp, line.FileName, (unsigned long)line.LineNumber);
        } else {
            out("  %016llx %s+0x%llx\n", (unsigned long long)addr, sym->Name, (unsigned long long)disp);
        }
    } else {
        /* no symbol (a system DLL): its module, e.g. where a stalled thread waits */
        HMODULE mod = NULL;
        char name[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)addr, &mod) &&
            GetModuleFileNameA(mod, name, sizeof(name)) != 0) {
            const char* base = strrchr(name, '\\');
            out("  %016llx %s+0x%llx\n", (unsigned long long)addr, base != NULL ? base + 1 : name,
                    (unsigned long long)(addr - (DWORD64)(uintptr_t)mod));
        } else {
            out("  %016llx ?\n", (unsigned long long)addr);
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

    reportBegin("crash");
    out("\n*** crash: exception 0x%08lx at %p", (unsigned long)rec->ExceptionCode, rec->ExceptionAddress);
    if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2) {
        out(" (%s 0x%llx)", rec->ExceptionInformation[0] ? "write to" : "read from",
                (unsigned long long)rec->ExceptionInformation[1]);
    }
    out("\n");
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
    reportEnd();
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
    reportBegin("crash");
    out("\n*** crash: abort()\n");
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    n = CaptureStackBackTrace(1, 48, frames, NULL);
    for (i = 0; i < n; i++) {
        printFrame(proc, (DWORD64)(uintptr_t)frames[i]);
    }
    fflush(stderr);
    reportEnd();
}

/* The C runtime reports invalid arguments (e.g. a bad printf format pointer)
 * by ending the process silently: print where it happened first. */
static void onInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line,
                               uintptr_t reserved) {
    HANDLE proc = GetCurrentProcess();
    void* frames[48];
    USHORT n, i;
    (void)expr, (void)func, (void)file, (void)line, (void)reserved;
    reportBegin("crash");
    out("\n*** crash: invalid argument to a C runtime function\n");
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    n = CaptureStackBackTrace(1, 48, frames, NULL);
    for (i = 0; i < n; i++) {
        printFrame(proc, (DWORD64)(uintptr_t)frames[i]);
    }
    fflush(stderr);
    reportEnd();
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
            reportBegin("stall");
            out("\n*** stall: no frame for %d s; main thread:\n", secs);
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
            reportEnd();
            _Exit(4);
        }
    }
    return 0;
}

/* Integer division by zero: the PowerPC result, as the GameCube computes it
 * (divzero_pc.c). First in line, before any crash report. */
static LONG WINAPI onIntDivide(EXCEPTION_POINTERS* info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    CONTEXT* ctx = info->ContextRecord;
    if ((code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW) &&
        portEmulateIntDivide((unsigned long long*)&ctx->Rax, (unsigned long long*)&ctx->Rdx,
                             (unsigned long long*)&ctx->Rip, code == EXCEPTION_INT_OVERFLOW)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void portCrashInit(void) {
    const char* wd = getenv("TTYD_WATCHDOG");
    checkAddressSpace();
    AddVectoredExceptionHandler(1, onIntDivide); /* game behaviour, not a report: always */
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
/* POSIX (Linux, Android, macOS, iOS): the same reports as the Windows half.
 * Fatal signals are handled on an alternate stack (a stack overflow still
 * reports); frames come from the unwinder (_Unwind_Backtrace, which crosses
 * the signal frame) starting at the interrupted instruction. Names: on Linux
 * and Android from the executable's own ELF symbol table, read at startup
 * (the game's functions are not in the dynamic symbol table, and statics
 * never are), else dladdr. Each frame line is
 *   "  <16 hex digits> symbol+0xoff (module+0xoff)"
 * (tools/port/map_sweep.py reads the symbol; `addr2line -e <exe> 0x<off>`
 * gives the line of an executable frame). glibc needs _GNU_SOURCE for
 * dladdr/dl_iterate_phdr/REG_RIP: CMakeLists.txt defines it for this file. */
#if defined(__GLIBC__) && !defined(__USE_GNU)
#error "crash_pc.c needs _GNU_SOURCE on glibc (set for this file in CMakeLists.txt)"
#endif
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <dlfcn.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <unwind.h>
#endif
#include <fcntl.h>
#if defined(__linux__)
#include <ucontext.h>
#include <elf.h>
#include <link.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif

#define MAX_FRAMES 48
/* $TTYD_WATCHDOG: the watchdog thread sends this to the main thread, which
 * prints its own stack (no portable way to read another thread's). */
#define STALL_SIGNAL SIGUSR2

static int sReportFd = -1;

/* stderr without stdio: the fault may be inside it (its lock held). */
static void out(const char* fmt, ...) {
    char buf[600];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1;
    }
    if (n > 0) {
        ssize_t written = write(2, buf, (size_t)n);
        if (sReportFd >= 0) {
            written = write(sReportFd, buf, (size_t)n);
        }
        (void)written;
    }
}

/* Starts the report file (signal-safe: open/write only). */
static void reportBegin(const char* what) {
    if (sReportPath[0] == '\0' || sReportFd >= 0) {
        return;
    }
    sReportFd = open(sReportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (sReportFd < 0) {
        return;
    }
    {
        /* write() to the file only: stderr gets the usual lines */
        const int fd = sReportFd;
        char buf[1024];
        int n = snprintf(buf, sizeof(buf), "Paper Cut %s report\nversion: %s, built %s %s\ngame: %s\n%s%s%s%s%s%s",
                         what, PORT_REGION_SHORT " (" PORT_GAME_ID ")",
                         __DATE__, __TIME__, sContext[0] != '\0' ? sContext : "?", sGpu[0] != '\0' ? "gpu: " : "",
                         sGpu, sGpu[0] != '\0' ? "\n" : "",
                         sMessage[0] != '\0' ? "message: " : "", sMessage, sMessage[0] != '\0' ? "\n" : "");
        ssize_t w = write(fd, buf, (size_t)(n > 0 && n < (int)sizeof(buf) ? n : 0));
#if defined(__APPLE__)
        {
            /* the image's UUID and load address: offsets symbolize against that build */
            const struct mach_header_64* h = (const struct mach_header_64*)_dyld_get_image_header(0);
            const struct load_command* lc = (const struct load_command*)(h + 1);
            uint32_t i;
            for (i = 0; h != NULL && i < h->ncmds; i++) {
                if (lc->cmd == LC_UUID) {
                    const uint8_t* u = ((const struct uuid_command*)lc)->uuid;
                    n = snprintf(buf, sizeof(buf),
                                 "image: %02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X at %p\n",
                                 u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11], u[12],
                                 u[13], u[14], u[15], (const void*)h);
                    w = write(fd, buf, (size_t)(n > 0 && n < (int)sizeof(buf) ? n : 0));
                    break;
                }
                lc = (const struct load_command*)((const char*)lc + lc->cmdsize);
            }
        }
#endif
        (void)w;
    }
}

static void putReport(const char* line) {
    ssize_t w = write(sReportFd, line, strlen(line));
    (void)w;
}

static void reportEnd(void) {
    if (sReportFd >= 0) {
        logTail(putReport);
        fsync(sReportFd);
        close(sReportFd);
        sReportFd = -1;
    }
}

/* The interrupted instruction of a signal's context. */
static uintptr_t contextPC(void* ucv) {
#if defined(__linux__) && defined(__x86_64__)
#ifndef REG_RIP
#define REG_RIP 16
#endif
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext.gregs[REG_RIP];
#elif defined(__linux__) && defined(__i386__)
#ifndef REG_EIP
#define REG_EIP 14
#endif
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext.gregs[REG_EIP];
#elif defined(__linux__) && defined(__aarch64__)
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext.pc;
#elif defined(__linux__) && defined(__arm__)
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext.arm_pc;
#elif defined(__APPLE__) && defined(__x86_64__)
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__) && defined(__darwin_arm_thread_state64_get_pc)
    return (uintptr_t)__darwin_arm_thread_state64_get_pc(((ucontext_t*)ucv)->uc_mcontext->__ss);
#elif defined(__APPLE__) && defined(__aarch64__)
    return (uintptr_t)((ucontext_t*)ucv)->uc_mcontext->__ss.__pc;
#else
    (void)ucv;
    return 0;
#endif
}

#if defined(__linux__)
/* The executable's symbols (functions and data, statics included), sorted by
 * address, from /proc/self/exe's .symtab (.dynsym if stripped). */
typedef struct ExeSym {
    uintptr_t addr;
    uintptr_t size;
    const char* name;
} ExeSym;
static ExeSym* sSyms;
static size_t sSymCount;
static uintptr_t sExeBias, sExeLo, sExeHi;
static char sExeName[64] = "exe";
static int sSymsLoaded;

static int exeSegments(struct dl_phdr_info* info, size_t size, void* arg) {
    int i;
    (void)size;
    (void)arg;
    /* the first object reported is the executable */
    sExeBias = (uintptr_t)info->dlpi_addr;
    sExeLo = UINTPTR_MAX;
    sExeHi = 0;
    for (i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD) {
            uintptr_t lo = sExeBias + (uintptr_t)ph->p_vaddr;
            if (lo < sExeLo) {
                sExeLo = lo;
            }
            if (lo + (uintptr_t)ph->p_memsz > sExeHi) {
                sExeHi = lo + (uintptr_t)ph->p_memsz;
            }
        }
    }
    return 1;
}

static int compareSyms(const void* a, const void* b) {
    uintptr_t x = ((const ExeSym*)a)->addr, y = ((const ExeSym*)b)->addr;
    return x < y ? -1 : x > y;
}

static int readAt(int fd, void* buf, size_t size, off_t off) {
    return pread(fd, buf, size, off) == (ssize_t)size;
}

static void loadSymbols(void) {
    char path[1024];
    ssize_t len;
    int fd;
    ElfW(Ehdr) eh;
    ElfW(Shdr)* sh = NULL;
    ElfW(Sym)* syms = NULL;
    char* strtab = NULL;
    size_t i, nsyms, strSize;
    int symIdx = -1;
    if (sSymsLoaded) {
        return;
    }
    sSymsLoaded = 1;
    dl_iterate_phdr(exeSegments, NULL);
    len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len <= 0) {
        return;
    }
    path[len] = '\0';
    snprintf(sExeName, sizeof(sExeName), "%s", strrchr(path, '/') != NULL ? strrchr(path, '/') + 1 : path);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    if (!readAt(fd, &eh, sizeof(eh), 0) || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_shentsize != sizeof(ElfW(Shdr)) || eh.e_shnum == 0) {
        goto done;
    }
    sh = (ElfW(Shdr)*)malloc(eh.e_shnum * sizeof(*sh));
    if (sh == NULL || !readAt(fd, sh, eh.e_shnum * sizeof(*sh), (off_t)eh.e_shoff)) {
        goto done;
    }
    for (i = 0; i < eh.e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            symIdx = (int)i;
        }
    }
    for (i = 0; symIdx < 0 && i < eh.e_shnum; i++) {
        if (sh[i].sh_type == SHT_DYNSYM) {
            symIdx = (int)i;
        }
    }
    if (symIdx < 0 || sh[symIdx].sh_link >= eh.e_shnum) {
        goto done;
    }
    nsyms = (size_t)(sh[symIdx].sh_size / sizeof(ElfW(Sym)));
    strSize = (size_t)sh[sh[symIdx].sh_link].sh_size;
    syms = (ElfW(Sym)*)malloc(nsyms * sizeof(ElfW(Sym)) + 1);
    strtab = (char*)malloc(strSize + 1);
    sSyms = (ExeSym*)malloc(nsyms * sizeof(ExeSym) + 1);
    if (syms == NULL || strtab == NULL || sSyms == NULL ||
        !readAt(fd, syms, nsyms * sizeof(ElfW(Sym)), (off_t)sh[symIdx].sh_offset) ||
        !readAt(fd, strtab, strSize, (off_t)sh[sh[symIdx].sh_link].sh_offset)) {
        free(strtab);
        free(sSyms);
        strtab = NULL;
        sSyms = NULL;
        goto done;
    }
    strtab[strSize] = '\0';
    for (i = 0; i < nsyms; i++) {
        const ElfW(Sym)* s = &syms[i];
        int type = s->st_info & 0xF;
        if ((type == STT_FUNC || type == STT_OBJECT) && s->st_shndx != SHN_UNDEF && s->st_shndx < SHN_LORESERVE &&
            s->st_value != 0 && s->st_name < strSize) {
            sSyms[sSymCount].addr = sExeBias + (uintptr_t)s->st_value;
            sSyms[sSymCount].size = (uintptr_t)s->st_size;
            sSyms[sSymCount].name = strtab + s->st_name;
            sSymCount++;
        }
    }
    qsort(sSyms, sSymCount, sizeof(ExeSym), compareSyms);
    strtab = NULL; /* kept: the names point into it */
done:
    free(strtab);
    free(syms);
    free(sh);
    close(fd);
}

/* the executable's symbol at addr (NULL if outside it or past its size) */
static const ExeSym* findExeSym(uintptr_t addr) {
    size_t lo = 0, hi = sSymCount;
    const ExeSym* s;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (sSyms[mid].addr <= addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return NULL;
    }
    s = &sSyms[lo - 1];
    return s->size != 0 && addr >= s->addr + s->size ? NULL : s;
}
#else
static void loadSymbols(void) {}
#endif

/* "symbol+0xoff (module+0xoff)" for addr, or "module+0xoff", or "?".
 * lookup is addr - 1 for return addresses (a call may end a function). */
static void describe(uintptr_t addr, uintptr_t lookup, char* buf, size_t size) {
    Dl_info info;
#if defined(__linux__)
    if (lookup >= sExeLo && lookup < sExeHi) {
        const ExeSym* s = findExeSym(lookup);
        if (s != NULL) {
            snprintf(buf, size, "%s+0x%llx (%s+0x%llx)", s->name, (unsigned long long)(addr - s->addr), sExeName,
                     (unsigned long long)(addr - sExeBias));
        } else {
            snprintf(buf, size, "%s+0x%llx", sExeName, (unsigned long long)(addr - sExeBias));
        }
        return;
    }
#endif
    memset(&info, 0, sizeof(info));
    if (dladdr((void*)lookup, &info) && info.dli_fname != NULL) {
        const char* base = strrchr(info.dli_fname, '/') != NULL ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
        if (info.dli_sname != NULL && info.dli_saddr != NULL) {
            snprintf(buf, size, "%s+0x%llx (%s+0x%llx)", info.dli_sname,
                     (unsigned long long)(addr - (uintptr_t)info.dli_saddr), base,
                     (unsigned long long)(addr - (uintptr_t)info.dli_fbase));
        } else {
            snprintf(buf, size, "%s+0x%llx", base, (unsigned long long)(addr - (uintptr_t)info.dli_fbase));
        }
        return;
    }
    snprintf(buf, size, "?");
}

#ifdef __EMSCRIPTEN__
/* WebAssembly has no unwinder: the runtime's call stack, with function names. */
static void printStack(uintptr_t pc, int fromAbort) {
    static char buf[16384];
    (void)pc;
    (void)fromAbort;
    emscripten_get_callstack(EM_LOG_C_STACK, buf, sizeof(buf));
    out("%s\n", buf);
}
#else
typedef struct Trace {
    uintptr_t* ip;
    int n;
    int max;
} Trace;

static _Unwind_Reason_Code traceStep(struct _Unwind_Context* ctx, void* arg) {
    Trace* t = (Trace*)arg;
    uintptr_t ip = (uintptr_t)_Unwind_GetIP(ctx);
    if (ip == 0 || t->n >= t->max) {
        return _URC_END_OF_STACK;
    }
    t->ip[t->n++] = ip;
    return _URC_NO_REASON;
}

/* The current thread's stack from the interrupted instruction pc (the
 * handler's own frames and the signal trampoline left out). fromAbort: start
 * at abort()'s caller, as the frames below it are the C library's. */
static void printStack(uintptr_t pc, int fromAbort) {
    uintptr_t ips[MAX_FRAMES * 2];
    Trace t;
    char buf[480];
    int i, start = -1, printed;
    t.ip = ips;
    t.n = 0;
    t.max = MAX_FRAMES * 2;
    _Unwind_Backtrace(traceStep, &t);
    for (i = 0; i < t.n; i++) {
        if (ips[i] == pc) {
            start = i;
            break;
        }
    }
    if (fromAbort) {
        for (i = start < 0 ? 0 : start; i < t.n; i++) {
            Dl_info info;
            memset(&info, 0, sizeof(info));
            if (dladdr((void*)(ips[i] - 1), &info) && info.dli_sname != NULL && strcmp(info.dli_sname, "abort") == 0) {
                start = i + 1;
                break;
            }
        }
    }
    if (start < 0) {
        /* the unwinder did not reach the interrupted frame: that frame, then
         * everything it saw */
        if (pc != 0) {
            describe(pc, pc, buf, sizeof(buf));
            out("  %016llx %s\n", (unsigned long long)pc, buf);
        }
        start = 0;
    }
    for (i = start, printed = 0; i < t.n && printed < MAX_FRAMES; i++, printed++) {
        describe(ips[i], ips[i] == pc ? ips[i] : ips[i] - 1, buf, sizeof(buf));
        out("  %016llx %s\n", (unsigned long long)ips[i], buf);
    }
}
#endif /* __EMSCRIPTEN__ */

static const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGBUS:
        return "SIGBUS";
    case SIGILL:
        return "SIGILL";
    case SIGFPE:
        return "SIGFPE";
    case SIGABRT:
        return "SIGABRT";
    case SIGTRAP:
        return "SIGTRAP";
    default:
        return "?";
    }
}

static volatile sig_atomic_t sInHandler;

/* SIGFPE from an integer division by zero (x86-64): the PowerPC result, as the
 * GameCube computes it (divzero_pc.c), and carry on. */
static int emulateDivide(siginfo_t* si, void* ucv) {
#if defined(__x86_64__) && (defined(__linux__) || defined(__APPLE__))
    if (si->si_code == FPE_INTDIV || si->si_code == FPE_INTOVF) {
#if defined(__linux__)
        greg_t* g = ((ucontext_t*)ucv)->uc_mcontext.gregs;
        return portEmulateIntDivide((unsigned long long*)&g[REG_RAX], (unsigned long long*)&g[REG_RDX],
                                    (unsigned long long*)&g[REG_RIP], si->si_code == FPE_INTOVF);
#else
        __typeof__(((ucontext_t*)ucv)->uc_mcontext->__ss)* ss = &((ucontext_t*)ucv)->uc_mcontext->__ss;
        return portEmulateIntDivide((unsigned long long*)&ss->__rax, (unsigned long long*)&ss->__rdx,
                                    (unsigned long long*)&ss->__rip, si->si_code == FPE_INTOVF);
#endif
    }
#else
    (void)si;
    (void)ucv;
#endif
    return 0;
}

static void onSignal(int sig, siginfo_t* si, void* uc) {
    uintptr_t pc = contextPC(uc);
    if (sig == SIGFPE && emulateDivide(si, uc)) {
        return;
    }
    if (sInHandler) {
        /* a fault while reporting one: just die */
        signal(sig, SIG_DFL);
        return;
    }
    sInHandler = 1;
    reportBegin("crash");
    if (sig == SIGABRT) {
        /* abort() (also std::terminate in Aurora or Dawn): as on Windows,
         * the game's trace first */
        fflush(stdout);
        out("\n*** crash: abort()\n");
    } else {
        out("\n*** crash: signal %d (%s) at %p", sig, signalName(sig), (void*)pc);
        if (sig == SIGSEGV || sig == SIGBUS) {
            out(" (address 0x%llx)", (unsigned long long)(uintptr_t)si->si_addr);
        }
        out("\n");
    }
    printStack(pc, sig == SIGABRT);
    reportEnd();
    /* the default action when the handler returns: core dump / exit status */
    signal(sig, SIG_DFL);
    raise(sig);
}

/* $TTYD_WATCHDOG=<seconds>: if no frame completes for that long (paused time
 * does not count), the main thread prints its stack (where the game is
 * stuck) and the process exits with status 4. */
static volatile int sFrames;
static volatile int sStallSecs;
static pthread_t sMainThread;

void portWatchdogFrame(void) {
    __atomic_fetch_add(&sFrames, 1, __ATOMIC_RELAXED);
}

static void onStall(int sig, siginfo_t* si, void* uc) {
    (void)sig;
    (void)si;
    fflush(stdout);
    reportBegin("stall");
    out("\n*** stall: no frame for %d s; main thread:\n", (int)sStallSecs);
    printStack(contextPC(uc), 0);
    reportEnd();
    _Exit(4);
}

static void sleepSeconds(int secs) {
    struct timespec ts;
    ts.tv_sec = secs;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) != 0) {
    }
}

static void* watchdog(void* arg) {
    int secs = (int)(intptr_t)arg, idle = 0, last = -1;
    for (;;) {
        int frames;
        sleepSeconds(1);
        frames = __atomic_load_n(&sFrames, __ATOMIC_RELAXED);
        if (frames != last || portIsPaused()) {
            last = frames;
            idle = 0;
        } else if (++idle >= secs) {
            sStallSecs = secs;
            pthread_kill(sMainThread, STALL_SIGNAL);
            /* the main thread exits from its handler; if it cannot run it
             * (the signal blocked, or stuck in the kernel), exit anyway */
            sleepSeconds(10);
            out("\n*** stall: no frame for %d s; the main thread did not report its stack\n", secs);
            _Exit(4);
        }
    }
    return NULL;
}

void portCrashInit(void) {
    const char* wd = getenv("TTYD_WATCHDOG");
    struct sigaction sa;
    checkAddressSpace();
    if (getenv("TTYD_NO_CRASH_HANDLER") == NULL) {
        /* SIGTRAP: __builtin_trap and Apple's libc checks (brk on arm64) */
        static const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
        stack_t ss;
        size_t i;
        /* names, the unwinder and dladdr ready before anything can crash */
        loadSymbols();
#ifndef __EMSCRIPTEN__
        {
            uintptr_t ip[4];
            Trace t;
            Dl_info info;
            t.ip = ip;
            t.n = 0;
            t.max = 4;
            _Unwind_Backtrace(traceStep, &t);
            dladdr((void*)&portCrashInit, &info);
        }
#endif
        /* an alternate stack, so a stack overflow is reported too */
        memset(&ss, 0, sizeof(ss));
        ss.ss_size = 256 * 1024;
        ss.ss_sp = malloc(ss.ss_size);
        if (ss.ss_sp != NULL) {
            sigaltstack(&ss, NULL);
        }
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = onSignal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        for (i = 0; i < sizeof(kSignals) / sizeof(kSignals[0]); i++) {
            sigaction(kSignals[i], &sa, NULL);
        }
    } else {
        /* no crash reports, but division by zero is game behaviour (divzero_pc.c) */
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = onSignal;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGFPE, &sa, NULL);
    }
    if (wd != NULL && atoi(wd) > 0) {
        pthread_t thread;
        pthread_attr_t attr;
        loadSymbols();
        sMainThread = pthread_self();
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = onStall;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(STALL_SIGNAL, &sa, NULL);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&thread, &attr, watchdog, (void*)(intptr_t)atoi(wd)) != 0) {
            fprintf(stderr, "TTYD_WATCHDOG: cannot start the watchdog thread\n");
        }
        pthread_attr_destroy(&attr);
    }
}

const char* portSymbolName(const void* addr, char* buf, size_t size) {
    loadSymbols();
#if defined(__linux__)
    if ((uintptr_t)addr >= sExeLo && (uintptr_t)addr < sExeHi) {
        const ExeSym* s = findExeSym((uintptr_t)addr);
        if (s != NULL) {
            snprintf(buf, size, "%s+0x%llx", s->name, (unsigned long long)((uintptr_t)addr - s->addr));
            return buf;
        }
    }
#endif
    {
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (addr != NULL && dladdr(addr, &info) && info.dli_sname != NULL && info.dli_saddr != NULL) {
            snprintf(buf, size, "%s+0x%llx", info.dli_sname,
                     (unsigned long long)((uintptr_t)addr - (uintptr_t)info.dli_saddr));
            return buf;
        }
    }
    snprintf(buf, size, "%p", addr);
    return buf;
}
#endif
