#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <dolphin/types.h>
#include <dolphin/pad.h>

/* Host window/input backend. port/src/platform/headless.c is the default;
 * a windowed backend replaces it. */
/* Prints a symbolized backtrace on a crash (port/src/common/crash_pc.c). */
void portCrashInit(void);
/* Crash and stall reports also go to this file (crash_pc.c; "" for none). */
void portCrashSetReportPath(const char* path);
/* Where the game is (sequence, map), for the report's header. */
void portCrashSetContext(const char* text);
/* The fatal error that is about to end the program (Aurora's), for the report. */
void portCrashSetMessage(const char* text);
/* aborts if p is below 2 GiB (sound-name words, include/sfx_name_word.h) */
void portCheckHostAddress(const void* p, const char* what);
/* a frame completed ($TTYD_WATCHDOG, crash_pc.c) */
void portWatchdogFrame(void);
/* This build's region, for messages: "Japan", "US" or "Europe" (PORT_GAME_ID
 * G8MJ01, G8ME01, G8MP01); PORT_REGION_SHORT as in the version names. */
#if defined(VERSION_G8MP01)
#define PORT_REGION_NAME "Europe"
#define PORT_REGION_SHORT "EU"
#elif defined(VERSION_G8ME01)
#define PORT_REGION_NAME "US"
#define PORT_REGION_SHORT "US"
#else
#define PORT_REGION_NAME "Japan"
#define PORT_REGION_SHORT "JP"
#endif
/* The game's video field rate: 60, or 50 for the European release in its 50 Hz mode (ospal_pc.c). */
int portFieldRate(void);
/* the game's versions and which disc is which: portCheckGameDisc, portVersionSelf */
#include "port/versions.h"
/* Bring-up trace of sequence/map changes when $TTYD_TRACE is set (trace_pc.c). */
void portTraceFrame(u32 frame);
/* msgSearch found no message for tag ($TTYD_TRACE logs it once per tag). */
void portTraceMsgMissing(const char* tag);
/* a memory card step's result ($TTYD_TRACE, cardmgr.c) */
void portTraceCard(const char* step, s32 result);
/* Frame captures to PNG (Aurora: port/src/aurora/shot_aurora.c; headless:
 * nothing to capture). Request one named capture; pending until written. */
void portShotRequest(const char* name);
/* The player's screenshot (to Photos on iOS, Pictures/TTYD on the Mac), and
 * the last result for a few seconds (or NULL). */
void portShotRequestUser(void);
const char* portShotStatus(void);
BOOL portShotPending(void);
void portShotFrame(void);
/* The game drew its scene this frame (marioStDisp): Aurora's in-between
 * frames of a higher $TTYD_FRAMERATE redraw it (interp_aurora.c). */
void portInterpSceneDrawn(void);
/* TRUE while such a frame is drawn: draw code that also advances the game's
 * state (a blink, a scroll) leaves it for the game's own frames. */
BOOL portInterpInBetween(void);
/* In such a frame: keep n bytes at p as they are now; they are put back when
 * the frame is drawn. For draw code that also steps the game's state (a menu
 * ring, a blur trail, a fade), one call at the top of its draw function. */
void portInterpKeep(void* p, unsigned int n);
/* In such a frame: a frame-temporary smart heap block (smartAlloc type 3)
 * from a scratch arena, so the game's heap is as without these frames
 * (a smartEntry*; NULL if the arena is full). */
void* portInterpScratchAlloc(u32 size);
/* Save warps (port/src/common/warp_pc.c): continue from a .gci save file
 * (the settings page), at the next retrace the game can take it. */
void portWarpRequestSave(const char* path);
const char* portWarpStatus(void);
void portWarpFrame(void);
/* The game's rand state (runtime_pc.c), kept around in-between frames. */
u32 portRandState(void);
void portRandSetState(u32 state);
/* Scripted controller input ($TTYD_INPUT, port/src/common/input_pc.c). */
BOOL portInputActive(void);
void portInputStart(void);
void portInputApply(PADStatus* pad);
/* run completed ARQ transfers' callbacks (port/src/common/arq_defer_pc.c) */
void portARQPoll(void);
/* $TTYD_FAST: advance the virtual game clock by one frame (headless os_pc.c) */
void portFastClockFrame(void);
/* $TTYD_FAST: the virtual clock, read and put back around the frames Aurora
 * draws between the game's (interp_aurora.c), whose clock reads would move it */
s64 portFastClockGet(void);
void portFastClockSet(s64 t);
/* Stop the game (the caller holds the main thread), its clock and audio, e.g.
 * while the window is minimized or the app is in the background; nests
 * (port/src/common/pause_pc.c). Game time does not advance while paused. */
void portPause(void);
void portResume(void);
BOOL portIsPaused(void);
/* the backend's game clock (os_pc.c / os_time_aurora.c) and audio (hw_pc.c) */
void portClockPause(void);
void portClockResume(void);
/* Aurora: the game clock at speed times real time (fast-forward); 1 normally. */
void portClockSetSpeed(int speed);
int portClockSpeed(void);
void salPcSetPaused(BOOL paused);
/* hw_pc.c: no audio callback runs while locked (save states). */
void salPcLock(void);
void salPcUnlock(void);
/* arq_defer_pc.c: transfers whose callbacks are still to run. */
u32 portARQPendingCount(void);
/* os_thread_pc.c: the main thread (not a game thread's coroutine) runs. */
BOOL portThreadIsMain(void);
/* os_time_aurora.c: while paused, the game time to resume from (save states). */
void portClockSetGame(s64 t);
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
