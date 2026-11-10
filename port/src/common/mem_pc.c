/* portMemForget, portMemMoved: port/include/port/mem_pc.h. */
#include "port/mem_pc.h"
#include "port/anim_pc.h"
#include "port/gx_arrays.h"
#include "port/tpl_pc.h"

#ifdef TTYD_BACKEND_AURORA
#include <dolphin/gx/GXAurora.h>
#endif

void portMemForget(const void* lo, const void* hi) {
    if (!lo || hi <= lo) {
        return;
    }
    portGXForgetRange(lo, hi);
    portAnimForgetRange(lo, hi);
    portTplForgetRange(lo, hi);
#ifdef TTYD_BACKEND_AURORA
    AuroraForgetMemory(lo, hi);
#endif
}

void portMemForgetTextures(const void* lo, const void* hi) {
    if (!lo || hi <= lo) {
        return;
    }
    /* a TPL that started in the block is freed with it: lookups in it are caught (src/sdk/texPalette.c) */
    portTplForgetRange(lo, hi);
#ifdef TTYD_BACKEND_AURORA
    AuroraForgetMemory(lo, hi);
#endif
}

void portMemMoved(const void* from, const void* to, unsigned int size) {
    if (!from || !to || from == to || !size) {
        return;
    }
#ifdef TTYD_BACKEND_AURORA
    AuroraMoveMemory(from, to, size);
#endif
}
