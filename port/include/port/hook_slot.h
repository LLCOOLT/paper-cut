#ifndef PORT_HOOK_SLOT_H
#define PORT_HOOK_SLOT_H

/* One hookable game function's mod hooks (include/port_hook.h). The
 * dispatchers (the generated .inc files in port/include/port/hooks) read it; the mods'
 * code fills it (port/mods/mod_api.cpp). Host data, never in a save state. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_HOOK_MAX 48 /* (the frame slot alone takes ~20 of Paper Cut's own, plus the mods') */

typedef struct PortHookSlot {
    int active;                  /* any hook: the dispatcher's fast path is off */
    int preCount, postCount;
    void* pre[PORT_HOOK_MAX];    /* int pre(Ret* result, T0* a0, ...): nonzero skips the function */
    void* post[PORT_HOOK_MAX];   /* void post(Ret* result, T0 a0, ...) */
    void* replace;               /* Ret fn(T0 a0, ...): instead of the game's */
    void* body;                  /* the game's own function (NULL: a slot no longer used) */
    unsigned calls;              /* calls, in a build with PORT_HOOK_PROFILE (tools/port/gen_hooks.py) */
} PortHookSlot;

#ifdef PORT_HOOK_PROFILE
#define PORT_HOOK_COUNT_CALL(s) ((s)->calls++)
#else
#define PORT_HOOK_COUNT_CALL(s) ((void)0)
#endif

extern PortHookSlot portHookSlots[];

#ifdef __cplusplus
}
#endif

#endif
