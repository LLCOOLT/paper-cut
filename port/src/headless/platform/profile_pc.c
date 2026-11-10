/* $TTYD_PROFILE=<file> (headless, Windows; the turbo work's profiler): a
 * sampling profiler of the game's thread. Every millisecond a helper thread
 * stops it, reads its instruction pointer and lets it go on; the file gets
 * "address count" lines (addresses at the executable's preferred base, for
 * llvm-symbolizer --obj=ttyd-<region>.exe), rewritten every two seconds, as
 * the game leaves with _Exit (no exit handlers). Off: nothing runs. */
#include "port/file_pc.h"
#include "port/platform.h"
#include <dolphin/vi.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <stdint.h>
#pragma comment(lib, "winmm")
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TABLE (1u << 18)
static unsigned long long sAddr[TABLE];
static unsigned sCount[TABLE];
static HANDLE sGame;
static char sPath[1024];
static unsigned long long sBias; /* preferred base - loaded base */
static DWORD sStart;

static void add(unsigned long long a) {
    unsigned h = (unsigned)((a * 0x9E3779B97F4A7C15ull) >> 46) & (TABLE - 1);
    while (sAddr[h] != 0 && sAddr[h] != a) {
        h = (h + 1) & (TABLE - 1);
    }
    sAddr[h] = a;
    sCount[h]++;
}

static void dump(void) {
    FILE* f = portFopen(sPath, "w");
    unsigned i;
    FILETIME c0, c1, k, u;
    if (f == NULL) {
        return;
    }
    /* the game thread's CPU time (a busy machine's wall time says little) */
    if (GetThreadTimes(sGame, &c0, &c1, &k, &u)) {
        const unsigned long long ku = ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                                      ((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime);
        fprintf(f, "# frames %u cpu_ms %llu wall_ms %lu\n", (unsigned)VIGetRetraceCount(), ku / 10000,
                (unsigned long)(GetTickCount() - sStart));
    }
    for (i = 0; i < TABLE; i++) {
        HMODULE m;
        char name[MAX_PATH];
        if (sAddr[i] == 0) {
            continue;
        }
        /* the executable's at its preferred base; another module's by name and offset */
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)(uintptr_t)sAddr[i], &m) &&
            m != GetModuleHandleW(NULL) && GetModuleFileNameA(m, name, sizeof(name)) > 0) {
            const char* slash = strrchr(name, '\\');
            fprintf(f, "%s+0x%llx %u\n", slash != NULL ? slash + 1 : name,
                    sAddr[i] - (unsigned long long)(uintptr_t)m, sCount[i]);
        } else {
            fprintf(f, "0x%llx %u\n", sAddr[i] + sBias, sCount[i]);
        }
    }
    fclose(f);
}

/* each sample's call chain (the stack unwound while the thread is stopped):
 * <file>.chains, one line per sample, the executable's frames at its
 * preferred base, another module's as 0 */
#define CHAIN_DEPTH 16
#define CHAIN_MAX (1u << 17)
static unsigned long long (*sChains)[CHAIN_DEPTH];
static unsigned sChainCount;

/* The modules' unwind tables (.pdata), read from their loaded images once at
 * the start: chain's lookup is this, never RtlLookupFunctionEntry, which takes
 * the loader's function-table lock, and the game thread may be stopped
 * holding it (a library loading): the sampler then waited on it forever. Each
 * module is pinned (never unloaded under the table); a frame in one loaded
 * later ends the chain there. */
#define UNWIND_MODULES 128
typedef struct UnwindModule {
    unsigned long long lo, hi;
    const RUNTIME_FUNCTION* table;
    DWORD count;
} UnwindModule;
static UnwindModule sModules[UNWIND_MODULES];
static int sModuleCount;
static unsigned long long sExeLo; /* the executable's loaded base */

static void addModule(HMODULE m) {
    const unsigned char* base = (const unsigned char*)m;
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt;
    const IMAGE_DATA_DIRECTORY* dir;
    HMODULE pinned;
    if (base == NULL || sModuleCount >= UNWIND_MODULES || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
        return;
    }
    dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (dir->VirtualAddress == 0 || dir->Size < sizeof(RUNTIME_FUNCTION) ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)base, &pinned)) {
        return;
    }
    sModules[sModuleCount].lo = (unsigned long long)(uintptr_t)base;
    sModules[sModuleCount].hi = (unsigned long long)(uintptr_t)base + nt->OptionalHeader.SizeOfImage;
    sModules[sModuleCount].table = (const RUNTIME_FUNCTION*)(base + dir->VirtualAddress);
    sModules[sModuleCount].count = dir->Size / (DWORD)sizeof(RUNTIME_FUNCTION);
    sModuleCount++;
}

static void loadModules(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    MODULEENTRY32W e;
    if (snap == INVALID_HANDLE_VALUE) {
        addModule(GetModuleHandleW(NULL));
        return;
    }
    e.dwSize = sizeof(e);
    if (Module32FirstW(snap, &e)) {
        do {
            addModule(e.hModule);
        } while (Module32NextW(snap, &e));
    }
    CloseHandle(snap);
}

/* the module pc is in (NULL: none of those at the start) */
static const UnwindModule* moduleOf(DWORD64 pc) {
    int i;
    for (i = 0; i < sModuleCount; i++) {
        if (pc >= sModules[i].lo && pc < sModules[i].hi) {
            return &sModules[i];
        }
    }
    return NULL;
}

/* RtlLookupFunctionEntry's answer from m's table (no lock), or NULL (a leaf function) */
static PRUNTIME_FUNCTION lookupUnwind(const UnwindModule* m, DWORD64 pc, DWORD64* imageBase) {
    const DWORD rva = (DWORD)(pc - m->lo);
    DWORD lo = 0, hi = m->count;
    while (lo < hi) {
        const DWORD mid = lo + (hi - lo) / 2;
        const RUNTIME_FUNCTION* f = &m->table[mid];
        if (rva < f->BeginAddress) {
            hi = mid;
        } else if (rva >= f->EndAddress) {
            lo = mid + 1;
        } else {
            *imageBase = m->lo;
            /* (an entry that points at another, as the loader's lookup follows) */
            if (f->UnwindData & 1) {
                f = (const RUNTIME_FUNCTION*)(uintptr_t)(m->lo + (f->UnwindData & ~1u));
            }
            return (PRUNTIME_FUNCTION)f;
        }
    }
    return NULL;
}

static void chain(const CONTEXT* from) {
    CONTEXT c = *from;
    const unsigned long long lo = sExeLo;
    int d;
    /* (no allocation and no lock here: the game thread may be stopped holding
     * the heap's lock, so sChains comes from portHeadlessProfileStart, or the
     * loader's, so the unwind tables are this file's own: lookupUnwind) */
    if (sChains == NULL || sChainCount >= CHAIN_MAX) {
        return;
    }
    for (d = 0; d < CHAIN_DEPTH && c.Rip != 0; d++) {
        DWORD64 imageBase;
        PRUNTIME_FUNCTION fn;
        PVOID handler;
        DWORD64 frame;
        const UnwindModule* m = moduleOf(c.Rip);
        sChains[sChainCount][d] = c.Rip >= lo && c.Rip < lo + 0x4000000 ? c.Rip + sBias : 1;
        if (m == NULL) {
            break; /* (a module loaded after the start, or not code: the chain ends there) */
        }
        fn = lookupUnwind(m, c.Rip, &imageBase);
        if (fn == NULL) {
            c.Rip = *(DWORD64*)(uintptr_t)c.Rsp; /* a leaf function: its return address on top */
            c.Rsp += 8;
        } else {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &handler, &frame, NULL);
        }
    }
    sChainCount++;
}

static void dumpChains(void) {
    char path[1100];
    FILE* f;
    unsigned i;
    int d;
    snprintf(path, sizeof(path), "%s.chains", sPath);
    if (sChains == NULL || (f = portFopen(path, "w")) == NULL) {
        return;
    }
    for (i = 0; i < sChainCount; i++) {
        for (d = 0; d < CHAIN_DEPTH && sChains[i][d] != 0; d++) {
            fprintf(f, d ? " %llx" : "%llx", sChains[i][d]);
        }
        fputc('\n', f);
    }
    fclose(f);
}

static DWORD WINAPI sampler(LPVOID unused) {
    DWORD last = GetTickCount();
    (void)unused;
    timeBeginPeriod(1);
    for (;;) {
        _Alignas(16) CONTEXT c; /* (x64: GetThreadContext wants it aligned) */
        Sleep(1);
        if (SuspendThread(sGame) == (DWORD)-1) {
            continue;
        }
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(sGame, &c)) {
            add(c.Rip);
            chain(&c);
        }
        ResumeThread(sGame);
        if (GetTickCount() - last >= 2000) {
            last = GetTickCount();
            dump();
            dumpChains();
        }
    }
    return 0;
}

void portHeadlessProfileStart(void) {
    const char* path = getenv("TTYD_PROFILE");
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    const IMAGE_NT_HEADERS* nt;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    /* (the loader writes the loaded base into the image's own header: the file's says the preferred one) */
    {
        char exe[MAX_PATH];
        unsigned char head[4096];
        FILE* f;
        unsigned long long preferred = 0x140000000ull;
        if (GetModuleFileNameA(NULL, exe, sizeof(exe)) > 0 && (f = portFopen(exe, "rb")) != NULL) {
            if (fread(head, 1, sizeof(head), f) == sizeof(head)) {
                const LONG at = ((const IMAGE_DOS_HEADER*)head)->e_lfanew;
                if (at > 0 && at + (LONG)sizeof(IMAGE_NT_HEADERS) <= (LONG)sizeof(head)) {
                    nt = (const IMAGE_NT_HEADERS*)(head + at);
                    preferred = nt->OptionalHeader.ImageBase;
                }
            }
            fclose(f);
        }
        sBias = preferred - (unsigned long long)(uintptr_t)base;
    }
    snprintf(sPath, sizeof(sPath), "%s", path);
    sChains = calloc(CHAIN_MAX, sizeof(*sChains)); /* before any sample (see chain) */
    sExeLo = (unsigned long long)(uintptr_t)base;
    loadModules(); /* (the unwind tables, before any sample too) */
    sStart = GetTickCount();
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sGame,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    CreateThread(NULL, 0, sampler, NULL, 0, NULL);
}
#else
void portHeadlessProfileStart(void) {}
#endif
