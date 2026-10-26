#ifndef PORT_HOOK_SLOT_H
#define PORT_HOOK_SLOT_H

/* One hookable game function's mod hooks (include/port_hook.h). The
 * dispatchers (port/include/port/hooks/*.inc, generated) read it; the mods'
 * code fills it (port/mods/mod_api.cpp). Host data, never in a save state. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_HOOK_MAX 16

typedef struct PortHookSlot {
    int active;                  /* any hook: the dispatcher's fast path is off */
    int preCount, postCount;
    void* pre[PORT_HOOK_MAX];    /* int pre(Ret* result, T0* a0, ...): nonzero skips the function */
    void* post[PORT_HOOK_MAX];   /* void post(Ret* result, T0 a0, ...) */
    void* replace;               /* Ret fn(T0 a0, ...): instead of the game's */
    void* body;                  /* the game's own function */
} PortHookSlot;

extern PortHookSlot portHookSlots[];

#ifdef __cplusplus
}
#endif

#endif
