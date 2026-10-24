#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <dolphin/types.h>
#include <dolphin/pad.h>

/* Host window/input backend. port/src/platform/headless.c is the default;
 * a windowed backend replaces it. */
/* Prints a symbolized backtrace on a crash (port/src/common/crash_pc.c). */
void portCrashInit(void);
/* aborts if p is below 2 GiB (sound-name words, include/sfx_name_word.h) */
void portCheckHostAddress(const void* p, const char* what);
/* a frame completed ($TTYD_WATCHDOG, crash_pc.c) */
void portWatchdogFrame(void);
/* FALSE (with a message) unless id (6 bytes, e.g. "G8ME01") is this build's game. */
BOOL portCheckGameId(const char* id, const char* source);
/* Bring-up trace of sequence/map changes when $TTYD_TRACE is set (trace_pc.c). */
void portTraceFrame(u32 frame);
/* msgSearch found no message for tag ($TTYD_TRACE logs it once per tag). */
void portTraceMsgMissing(const char* tag);
/* a memory card step's result ($TTYD_TRACE, cardmgr.c) */
void portTraceCard(const char* step, s32 result);
/* Frame captures to PNG (Aurora: port/src/aurora/shot_aurora.c; headless:
 * nothing to capture). Request one named capture; pending until written. */
void portShotRequest(const char* name);
BOOL portShotPending(void);
void portShotFrame(void);
/* Scripted controller input ($TTYD_INPUT, port/src/common/input_pc.c). */
BOOL portInputActive(void);
void portInputStart(void);
void portInputApply(PADStatus* pad);
/* run completed ARQ transfers' callbacks (port/src/common/arq_defer_pc.c) */
void portARQPoll(void);
/* $TTYD_FAST: advance the virtual game clock by one frame (headless os_pc.c) */
void portFastClockFrame(void);
/* Stop the game (the caller holds the main thread), its clock and audio, e.g.
 * while the window is minimized or the app is in the background; nests
 * (port/src/common/pause_pc.c). Game time does not advance while paused. */
void portPause(void);
void portResume(void);
BOOL portIsPaused(void);
/* the backend's game clock (os_pc.c / os_time_aurora.c) and audio (hw_pc.c) */
void portClockPause(void);
void portClockResume(void);
void salPcSetPaused(BOOL paused);
struct EventEntry;
void portTraceCmd(struct EventEntry* entry);
/* "symbol+0xoff" for an address in the executable (or the address), into buf. */
const char* portSymbolName(const void* addr, char* buf, size_t size);
/* x86-64 integer division by zero (or INT_MIN / -1) at *rip: write the PowerPC
 * result into rax/rdx and step over it; 0 if it is not a div/idiv
 * (port/src/common/divzero_pc.c, called from the crash handlers). */
int portEmulateIntDivide(unsigned long long* rax, unsigned long long* rdx, unsigned long long* rip, int overflow);
void portPlatformInit(void);
/* Pump window events; returns FALSE when the user closed the window. */
BOOL portPlatformPoll(void);
/* Show the frame the GX backend finished. */
void portPlatformPresent(void);
/* Fill one controller's state; returns FALSE if nothing is connected. */
BOOL portPlatformReadPad(s32 chan, PADStatus* status);

#endif
