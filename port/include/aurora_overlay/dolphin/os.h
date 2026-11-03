/* Aurora build: Aurora's <dolphin/os.h>, with the game's clock reads routed
 * through port/src/aurora/os_time_aurora.c, so $TTYD_FAST runs on a virtual
 * 60 Hz clock as the headless build does (fades, timed waits and message
 * pacing take the same number of frames as in real time). Aurora's own code
 * keeps the real clock. */
#include_next <dolphin/os.h>

#ifndef PORT_AURORA_OVERLAY_OS_H
#define PORT_AURORA_OVERLAY_OS_H
#ifndef __cplusplus
OSTime portOSGetTime(void);
OSTick portOSGetTick(void);
#define OSGetTime portOSGetTime
#define OSGetTick portOSGetTick
#endif
/* OSGetLanguage's values under the SDK's names (Aurora: OS_LANGUAGE_*) */
#ifndef OS_LANG_ENGLISH
#define OS_LANG_ENGLISH OS_LANGUAGE_ENGLISH
#define OS_LANG_GERMAN OS_LANGUAGE_GERMAN
#define OS_LANG_FRENCH OS_LANGUAGE_FRENCH
#define OS_LANG_SPANISH OS_LANGUAGE_SPANISH
#define OS_LANG_ITALIAN OS_LANGUAGE_ITALIAN
#define OS_LANG_DUTCH OS_LANGUAGE_DUTCH
#endif
#endif
