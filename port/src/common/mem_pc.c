/* portMemForget: port/include/port/mem_pc.h. */
#include "port/mem_pc.h"
#include "port/anim_pc.h"
#include "port/gx_arrays.h"
#include "port/tpl_pc.h"

void portMemForget(const void* lo, const void* hi) {
    if (!lo || hi <= lo) {
        return;
    }
    portGXForgetRange(lo, hi);
    portAnimForgetRange(lo, hi);
    portTplForgetRange(lo, hi);
}
