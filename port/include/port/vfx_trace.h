#ifndef PORT_VFX_TRACE_H
#define PORT_VFX_TRACE_H

/* The log's proof that an effect was drawn, not only made (tests: the
 * testpass scenarios' log_must on "[vfx] ... drawn"): an effect's draw
 * callback reports itself with how much of it showed this frame, and the
 * first frame an effect (each one made) shows anything is logged once:
 *
 *   [vfx] GonbabaBreath drawn: type 7, 9 shown
 *
 * Log only; nothing drawn changes. */

#ifdef __cplusplus
extern "C" {
#endif

/* effect: the EffectEntry drawing (with its work, to tell a reused entry
 * from the one before); what: its name; kind: its type; shown: how many of
 * its parts were drawn with anything to see (0: none yet) */
void portVfxDrawn(const void* effect, const void* work, const char* what, int kind, int shown);

/* an effect named what showed something within the last few frames (the
 * input script's wait_vfx, port/src/common/input_pc.c) */
int portVfxShownLately(const char* what);

#ifdef __cplusplus
}
#endif

#endif
