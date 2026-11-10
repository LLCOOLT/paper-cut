/* The log's proof that an effect was drawn (port/include/port/vfx_trace.h). */
#include "port/vfx_trace.h"

#include <dolphin/vi.h>
#include <stdio.h>
#include <string.h>

/* the effects drawn lately: an entry and its work, its name and the retrace
 * it was last drawn on (an entry and work seen again after a gap of frames is
 * a new effect made in the same memory) */
#define KEEP 16
#define GAP 4

static struct {
    const void* effect;
    const void* work;
    const char* what;
    u32 seen;
} sLogged[KEEP];
static int sNext;

void portVfxDrawn(const void* effect, const void* work, const char* what, int kind, int shown) {
    const u32 now = VIGetRetraceCount();
    int i;
    if (shown <= 0 || effect == NULL) {
        return;
    }
    for (i = 0; i < KEEP; i++) {
        if (sLogged[i].effect == effect && sLogged[i].work == work) {
            const int same = now - sLogged[i].seen <= GAP;
            sLogged[i].seen = now;
            if (same) {
                return;
            }
            break;
        }
    }
    if (i == KEEP) {
        i = sNext;
        sNext = (sNext + 1) % KEEP;
        sLogged[i].effect = effect;
        sLogged[i].work = work;
    }
    sLogged[i].what = what;
    sLogged[i].seen = now;
    printf("[vfx] %s drawn: type %d, %d shown\n", what, kind, shown);
    fflush(stdout);
}

int portVfxShownLately(const char* what) {
    const u32 now = VIGetRetraceCount();
    int i;
    for (i = 0; i < KEEP; i++) {
        if (sLogged[i].what != NULL && now - sLogged[i].seen <= GAP && strcmp(sLogged[i].what, what) == 0) {
            return 1;
        }
    }
    return 0;
}
