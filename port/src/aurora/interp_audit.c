/* $TTYD_INTERP_AUDIT (Windows): lists the game's draw code that changes the
 * game's state, which an in-between frame (interp_aurora.c) must not. While an
 * in-between frame is drawn:
 * - the game's heaps are read-only: each write traps, its instruction is
 *   counted (and the page opened for that one instruction);
 * - the executable's writable sections are compared before and after, and
 *   the globals that changed are counted.
 * Each new site, by function and line (and two callers) or by global, is
 * printed once, every 64 in-between frames and at exit (script runs end in
 * _Exit: the last 64 frames' new sites may be left out). Some
 * entries are expected (the cameras' matrices, which interp_aurora.c puts
 * back; poses and matrices each real frame computes again before use).
 *
 * "net" sites are what is left changed once such a frame is over and everything
 * the draw changed is put back (portInterpAuditSettle: the poses, the random
 * numbers, what draw code kept with portInterpKeep), before the blend is undone: each
 * written heap page is kept as it was before its first write and compared
 * after, and each byte still different is put down to the last write to it;
 * the globals are compared alike. A net site is a draw that changed the game
 * (a timer stepped twice, a one-shot taken, a block allocated or freed) unless
 * it is a cache the next real frame computes again (tools/port/interp_audit_check.py
 * and its allowlist, tests/port/interp_audit_allow.txt). */
#include <dolphin/types.h>
#include "port/platform.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 4096
#define MAX_SITES 65536
#define MAX_SECTIONS 16

extern void* heapStart[];
extern void* heapEnd[];

static int sOn = -1;
static int sCompanionOn = -1; /* $TTYD_COMPANION_AUDIT: the same for the second screen's menu's frames */
static int sSetUpDone;
static const char* sTag = "interp-audit";
static u8* sHeapLo;
static u8* sHeapHi;
static volatile LONG sArmed;
static __declspec(thread) u8* tOpenPage;

static struct {
    DWORD64 at;
    DWORD64 from[2]; /* a writer's callers: who used a helper (memcpy, MTXCopy) */
    u32 hits;
    u32 netHits; /* writes still in place after the frame was put back (portInterpAuditSettle) */
    BOOL global; /* at: a changed global's address, else a writing instruction */
} sSites[MAX_SITES];
static int sSiteCount;
static SRWLOCK sLock = SRWLOCK_INIT;

static struct {
    u8* lo;
    size_t size;
    u8* copy;
    u8* base; /* as the frames drawn outside the game's own found it (the net check) */
} sSections[MAX_SECTIONS];
static int sSectionCount;

/* The net check: each heap page written, as it was before its first write in the cycle (from the first
 * audited frame to portInterpAuditSettle), and the site of the last write at each of its words. */
#define SNAP_PAGES 8192
#define SNAP_HASH 16384
#define PAGE_WORDS (PAGE / 4)
#define WRITE_SPAN 16 /* words: the widest single write (a vector store) put down to the site at its start */
static BOOL sCycle;
static struct {
    u8* page;
    u8* copy;
    int* site; /* per word: the last writing site, or -1 */
} sSnap[SNAP_PAGES];
static int sSnapCount;
static int sSnapHash[SNAP_HASH]; /* index + 1 into sSnap */
static BOOL sSnapFull;

static int count(DWORD64 at, const DWORD64* from, BOOL global) {
    int i;
    AcquireSRWLockExclusive(&sLock);
    for (i = 0; i < sSiteCount; i++) {
        if (sSites[i].at == at && sSites[i].global == global &&
            (global || (sSites[i].from[0] == from[0] && sSites[i].from[1] == from[1]))) {
            sSites[i].hits++;
            break;
        }
    }
    if (i == sSiteCount && sSiteCount < MAX_SITES) {
        sSites[sSiteCount].at = at;
        sSites[sSiteCount].hits = 1;
        sSites[sSiteCount].global = global;
        if (!global) {
            sSites[sSiteCount].from[0] = from[0];
            sSites[sSiteCount].from[1] = from[1];
        }
        sSiteCount++;
    }
    ReleaseSRWLockExclusive(&sLock);
    return i < MAX_SITES ? i : -1;
}

/* A write about to be let through: its page kept as it was before the cycle's first write to it, and its word's
 * site noted. */
static void snapWrite(u8* addr, int site) {
    u8* page = (u8*)((uintptr_t)addr & ~(uintptr_t)(PAGE - 1));
    unsigned h = (unsigned)(((uintptr_t)page / PAGE) * 2654435761u) % SNAP_HASH;
    int i;
    while (sSnapHash[h] != 0 && sSnap[sSnapHash[h] - 1].page != page) {
        h = (h + 1) % SNAP_HASH;
    }
    if (sSnapHash[h] == 0) {
        if (sSnapCount >= SNAP_PAGES) {
            sSnapFull = TRUE;
            return;
        }
        i = sSnapCount;
        if (sSnap[i].copy == NULL) {
            sSnap[i].copy = malloc(PAGE);
            sSnap[i].site = malloc(PAGE_WORDS * sizeof(int));
            if (sSnap[i].copy == NULL || sSnap[i].site == NULL) {
                sSnapFull = TRUE;
                return;
            }
        }
        sSnap[i].page = page;
        memcpy(sSnap[i].copy, page, PAGE);
        memset(sSnap[i].site, 0xFF, PAGE_WORDS * sizeof(int));
        sSnapHash[h] = ++sSnapCount;
    }
    sSnap[sSnapHash[h] - 1].site[(addr - page) / 4] = site;
}

/* The two callers above the faulting instruction's function. */
static void callers(const CONTEXT* fault, DWORD64* from) {
    CONTEXT c = *fault;
    int i;
    for (i = 0; i < 2; i++) {
        DWORD64 base, frame;
        void* handler;
        RUNTIME_FUNCTION* f = RtlLookupFunctionEntry(c.Rip, &base, NULL);
        if (f == NULL) { /* a leaf: the return address is on top */
            c.Rip = *(DWORD64*)c.Rsp;
            c.Rsp += 8;
        } else {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, c.Rip, f, &c, &handler, &frame, NULL);
        }
        from[i] = c.Rip;
    }
}

static LONG CALLBACK onWrite(EXCEPTION_POINTERS* e) {
    EXCEPTION_RECORD* r = e->ExceptionRecord;
    DWORD old;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->ExceptionInformation[0] == 1) {
        u8* addr = (u8*)r->ExceptionInformation[1];
        if (addr >= sHeapLo && addr < sHeapHi) {
            if (sArmed) {
                DWORD64 from[2];
                int site;
                callers(e->ContextRecord, from);
                site = count(e->ContextRecord->Rip, from, FALSE);
                AcquireSRWLockExclusive(&sLock);
                snapWrite(addr, site);
                ReleaseSRWLockExclusive(&sLock);
            }
            tOpenPage = (u8*)((uintptr_t)addr & ~(uintptr_t)(PAGE - 1));
            VirtualProtect(tOpenPage, PAGE, PAGE_READWRITE, &old);
            e->ContextRecord->EFlags |= 0x100; /* trap after this instruction */
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    } else if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && tOpenPage != NULL) {
        if (sArmed) {
            VirtualProtect(tOpenPage, PAGE, PAGE_READONLY, &old);
        }
        tOpenPage = NULL;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void describe(HANDLE proc, DWORD64 at, BOOL global, char* out, size_t n) {
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)buf;
    DWORD64 disp = 0;
    IMAGEHLP_LINE64 line;
    DWORD lineDisp = 0;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    line.SizeOfStruct = sizeof(line);
    if (!SymFromAddr(proc, at, &disp, sym)) {
        snprintf(out, n, "%016llx ?", (unsigned long long)at);
    } else if (global) {
        snprintf(out, n, "%s", sym->Name);
    } else if (SymGetLineFromAddr64(proc, at, &lineDisp, &line)) {
        const char* file = strrchr(line.FileName, '\\');
        snprintf(out, n, "%s (%s:%lu)", sym->Name, file != NULL ? file + 1 : line.FileName,
                 (unsigned long)line.LineNumber);
    } else {
        snprintf(out, n, "%s+0x%llx", sym->Name, (unsigned long long)disp);
    }
}

/* Prints the sites not printed before, one line each (map_sweep --collect). */
static void report(void) {
    static BOOL sSymbols;
    static char (*sPrinted)[900];
    static int sPrintedCount;
    HANDLE proc = GetCurrentProcess();
    int i, j;
    if (!sSymbols) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        sSymbols = SymInitialize(proc, NULL, TRUE);
        sPrinted = calloc(MAX_SITES, sizeof(*sPrinted));
    }
    for (i = 0; i < sSiteCount; i++) {
        char what[280], from[2][280], name[900];
        int pass;
        if (sSites[i].hits == 0 && sSites[i].netHits == 0) {
            continue; /* printed */
        }
        describe(proc, sSites[i].at, sSites[i].global, what, sizeof(what));
        if (!sSites[i].global) {
            describe(proc, sSites[i].from[0], FALSE, from[0], sizeof(from[0]));
            describe(proc, sSites[i].from[1], FALSE, from[1], sizeof(from[1]));
        }
        for (pass = 0; pass < 2; pass++) {
            const char* net = pass ? "net " : "";
            if (pass ? sSites[i].netHits == 0 : sSites[i].hits == 0) {
                continue;
            }
            if (sSites[i].global) {
                snprintf(name, sizeof(name), "%sglobal %s", net, what);
            } else {
                snprintf(name, sizeof(name), "%swriter %s <- %s <- %s", net, what, from[0], from[1]);
            }
            for (j = 0; j < sPrintedCount && strcmp(sPrinted[j], name) != 0; j++) {
            }
            if (j == sPrintedCount && sPrintedCount < MAX_SITES) {
                memcpy(sPrinted[sPrintedCount++], name, sizeof(name));
                printf("[%s] %s\n", sTag, name);
            }
        }
        sSites[i].hits = 0;
        sSites[i].netHits = 0;
    }
    if (sSnapFull) {
        printf("[%s] net check incomplete: more pages or writes than it keeps\n", sTag);
        sSnapFull = FALSE;
    }
    fflush(stdout);
}

static void setup(void) {
    if (sSetUpDone) {
        return;
    }
    sSetUpDone = 1;
    {
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((u8*)dos + dos->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    int i;
    for (i = 0; i < nt->FileHeader.NumberOfSections && sSectionCount < MAX_SECTIONS; i++, s++) {
        if ((s->Characteristics & IMAGE_SCN_MEM_WRITE) && s->Misc.VirtualSize != 0) {
            sSections[sSectionCount].lo = (u8*)dos + s->VirtualAddress;
            sSections[sSectionCount].size = s->Misc.VirtualSize;
            sSections[sSectionCount].copy = malloc(s->Misc.VirtualSize);
            sSections[sSectionCount].base = malloc(s->Misc.VirtualSize);
            sSectionCount++;
        }
    }
    AddVectoredExceptionHandler(1, onWrite);
    atexit(report);
    }
}

static void auditBegin(void) {
    DWORD old;
    int i;
    for (i = 0; i < sSectionCount; i++) {
        memcpy(sSections[i].copy, sSections[i].lo, sSections[i].size);
    }
    if (!sCycle) { /* the first frame since the last settle: what the net check compares with */
        int k;
        sCycle = TRUE;
        for (i = 0; i < sSectionCount; i++) {
            memcpy(sSections[i].base, sSections[i].lo, sSections[i].size);
        }
        for (k = 0; k < SNAP_HASH; k++) {
            sSnapHash[k] = 0;
        }
        sSnapCount = 0;
    }
    /* the heaps, in whole pages (memInit lays them out back to back) */
    sHeapLo = (u8*)(((uintptr_t)heapStart[0] + PAGE - 1) & ~(uintptr_t)(PAGE - 1));
    sHeapHi = (u8*)((uintptr_t)heapEnd[0] & ~(uintptr_t)(PAGE - 1));
    for (i = 1; heapEnd[i] != NULL && (u8*)heapStart[i] == (u8*)heapEnd[i - 1]; i++) {
        sHeapHi = (u8*)((uintptr_t)heapEnd[i] & ~(uintptr_t)(PAGE - 1));
    }
    sArmed = 1;
    VirtualProtect(sHeapLo, sHeapHi - sHeapLo, PAGE_READONLY, &old);
}

static void auditEnd(void) {
    static u32 sFrames;
    DWORD old;
    int i;
    sArmed = 0;
    VirtualProtect(sHeapLo, sHeapHi - sHeapLo, PAGE_READWRITE, &old);
    for (i = 0; i < sSectionCount; i++) {
        size_t k = 0, n = sSections[i].size;
        u8* now = sSections[i].lo;
        const u8* was = sSections[i].copy;
        while (k < n) {
            if (now[k] != was[k]) {
                size_t same = 0;
                count((DWORD64)(uintptr_t)(now + k), NULL, TRUE);
                /* one count per changed run (up to 64 bytes alike) */
                for (; k < n && same < 64; k++) {
                    same = now[k] == was[k] ? same + 1 : 0;
                }
            } else {
                k++;
            }
        }
    }
    if (++sFrames % 64 == 0) {
        report();
    }
}

/* The net check, once everything a frame drawn outside the game's own changed is put back. */
static void settle(void) {
    int p, i;
    if (!sCycle) {
        return;
    }
    sCycle = FALSE;
    for (p = 0; p < sSnapCount; p++) {
        const u8* was = sSnap[p].copy;
        const u8* now = sSnap[p].page;
        int word;
        for (word = 0; word < PAGE_WORDS; word++) {
            int site = -1, w;
            if (memcmp(now + word * 4, was + word * 4, 4) == 0) {
                continue;
            }
            for (w = word; w >= 0 && w > word - WRITE_SPAN; w--) { /* the write that covers the word */
                if (sSnap[p].site[w] >= 0) {
                    site = sSnap[p].site[w];
                    break;
                }
            }
            if (site >= 0) {
                sSites[site].netHits++;
            }
        }
    }
    for (i = 0; i < sSectionCount; i++) {
        size_t k = 0, n = sSections[i].size;
        u8* now = sSections[i].lo;
        const u8* was = sSections[i].base;
        while (k < n) {
            if (now[k] != was[k]) {
                size_t same = 0;
                int site = count((DWORD64)(uintptr_t)(now + k), NULL, TRUE);
                if (site >= 0) {
                    sSites[site].hits--; /* (counted as changed in the frame already: this is the net count) */
                    sSites[site].netHits++;
                }
                for (; k < n && same < 64; k++) {
                    same = now[k] == was[k] ? same + 1 : 0;
                }
            } else {
                k++;
            }
        }
    }
}

/* $TTYD_INTERP_AUDIT=N (N > 1): one frame in N audited (a long run's pace: each audited write traps) */
static int sEvery = 1;
static unsigned sFrameNo;
static BOOL sThis;

void portInterpAuditSettle(void) {
    if (sOn > 0 && sThis) {
        settle();
    }
}

/* Around the in-between frame's draw. */
void portInterpAuditBegin(void) {
    if (sOn < 0) {
        const char* v = getenv("TTYD_INTERP_AUDIT");
        sOn = v != NULL;
        if (sOn) {
            sEvery = atoi(v) > 1 ? atoi(v) : 1;
            setup();
        }
    }
    if (sOn) {
        sThis = sFrameNo++ % (unsigned)sEvery == 0;
        if (sThis) {
            sTag = "interp-audit";
            auditBegin();
        }
    }
}

void portInterpAuditEnd(void) {
    if (sOn && sThis) {
        auditEnd();
    }
}

/* Around a frame of the second screen's menu (companion_menu_aurora.c). */
void portCompanionAuditBegin(void) {
    if (sCompanionOn < 0) {
        sCompanionOn = getenv("TTYD_COMPANION_AUDIT") != NULL;
        if (sCompanionOn) {
            setup();
        }
    }
    if (sCompanionOn) {
        sTag = "companion-audit";
        auditBegin();
    }
}

void portCompanionAuditEnd(void) {
    if (sCompanionOn) {
        auditEnd();
        settle();
    }
}
#else
void portInterpAuditBegin(void) {}
void portInterpAuditEnd(void) {}
void portInterpAuditSettle(void) {}
void portCompanionAuditBegin(void) {}
void portCompanionAuditEnd(void) {}
#endif
