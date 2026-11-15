/* $TTYD_STACK_REPORT (tests, Windows): at a test's exit (portTestExit), how deep the main thread's stack went in
 * the run. Windows commits a thread's stack pages as they are first touched and keeps them, so the lowest
 * committed page is the deepest point reached. The game runs on the main thread; a phone gives it about 1 MiB
 * (Android's SDLThread, the iOS main thread), so a run that goes near that would end there on a phone. */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void portStackReport(void);

void portStackReport(void) {
#ifdef _WIN32
    ULONG_PTR lo = 0, hi = 0, at;
    MEMORY_BASIC_INFORMATION mbi;
    if (getenv("TTYD_STACK_REPORT") == NULL) {
        return;
    }
    GetCurrentThreadStackLimits(&lo, &hi);
    for (at = lo; at < hi; at = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize) {
        if (VirtualQuery((void*)at, &mbi, sizeof(mbi)) == 0) {
            break;
        }
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD)) {
            break;
        }
    }
    printf("[stack] main thread: %lu KiB of %lu KiB used at the deepest\n", (unsigned long)((hi - at) >> 10),
           (unsigned long)((hi - lo) >> 10));
#endif
}
