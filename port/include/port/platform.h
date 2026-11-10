#ifndef PORT_PLATFORM_H
#define PORT_PLATFORM_H

#include <stddef.h>
#include <dolphin/types.h>
#include <dolphin/pad.h>

/* Host window/input backend. port/src/platform/headless.c is the default;
 * a windowed backend replaces it. */
/* Prints a symbolized backtrace on a crash (port/src/common/crash_pc.c). */
void portCrashInit(void);
/* POSIX: this thread's own alternate signal stack, so a stack overflow on it
 * is reported too (port/src/common/crash_thread_pc.c); each thread the port
 * starts calls it first (os_thread_pc.c, portDetach in port/detach.h).
 * Elsewhere a no-op. */
void portCrashThreadInit(void);
/* Crash and stall reports also go to this file (crash_pc.c; "" for none). Set before anything that
 * can crash (main_aurora.c: portCrashReportEarly, before Aurora starts); the handlers only open it. */
void portCrashSetReportPath(const char* path);
/* POSIX: the crash report for a fatal signal, from another handler (Android's, before the system's
 * tombstone); nothing while ending on purpose or once a report is being written. */
void portCrashWriteReport(int sig, void* siginfo, void* ucontext);
/* $TTYD_CRASH_TEST=<frames>[:segv|abort|invalid|stack] (tests): once a retrace, a crash on purpose. */
void portCrashTestFrame(void);
/* Where the game is (sequence, map), for the report's header. */
void portCrashSetContext(const char* text);
/* The fatal error that is about to end the program (Aurora's), for the report. */
void portCrashSetMessage(const char* text);
/* A log line (Aurora's, SDL's), kept among the last few for the report's end. */
void portCrashLogLine(const char* text);
/* The graphics adapter (Aurora's log), for the report's header. */
void portCrashSetGpu(const char* text);
const char* portCrashGpu(void);
/* The program is ending on purpose (the window closed, Quit, a restart, the system or a launcher
 * such as Steam asking it to end): a fault from here on is part of ending, not a crash, and
 * writes no report (the next start would call a normal quit a crash, #27). Signal-safe. */
void portCrashQuitStarted(void);
int portCrashQuitting(void);
/* POSIX: SIGTERM, SIGINT and SIGHUP (Steam's Exit game, a logout or shutdown, Ctrl+C, a closed
 * terminal) end the game through its normal quit (SDL's SDL_EVENT_QUIT), marked as above at once.
 * Call after SDL is up (aurora_initialize), whose own SIGTERM and SIGINT handlers it chains to.
 * Elsewhere it does nothing. */
void portCrashQuitSignalsInit(void);
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
/* msgSearch found no message for tag: logged once per tag and map, with the files loaded (area, global) */
void portTraceMsgMissing(const char* tag, const char* area, s32 areaCount, const char* global, s32 globalCount);
/* msgSearch found tag only in another room's file of the area (file), not the room's own (area): once per tag and map */
void portTraceMsgElsewhere(const char* tag, const char* file, const char* area);
/* $TTYD_MAP's warp done, or none asked for (tests that drive the page wait for it: TTYD_PADBIND_TEST) */
int portWarpSettled(void);
/* a memory card step's result ($TTYD_TRACE, cardmgr.c) */
void portTraceCard(const char* step, s32 result);
/* Frame captures to PNG (Aurora: port/src/aurora/shot_aurora.c; headless:
 * nothing to capture). Request one named capture; pending until written. */
void portShotRequest(const char* name);
/* Tests: a named capture that many frames from now, queued (none dropped while another is written). */
void portShotRequestIn(const char* name, int frames);
/* The player's screenshot (to Photos on iOS, Pictures/Paper Cut on the Mac), and
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
/* Whether draw code steps such a counter here: the game's own frames, and stereoscopic 3D's first eye (which
 * keeps it with portInterpKeep first, so both eyes show the same step). */
BOOL portInterpStepsHere(void);
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
/* Tests (fingerprint_pc.c): the fingerprint file and the logs written out; and
 * then _Exit(code), as every exit a test makes (the file is buffered); never returns, which the
 * static analyzer needs to know for the checks before it (input_pc.c's load, walk_to and talk) */
void portTestFlush(void);
__attribute__((noreturn)) void portTestExit(int code);
/* a test's end begun (the input script's end, the map warp's last frames): no more fingerprint
 * lines, so the file ends at the same frame however long Aurora's last capture takes */
void portTestEnding(void);
/* Scripted controller input ($TTYD_INPUT, port/src/common/input_pc.c). */
BOOL portInputActive(void);
void portInputStart(void);
void portInputApply(PADStatus* pad);
/* What is in each controller port (0-3) on this computer (input_pc.c): a controller (its device index), the
 * keyboard and the touch controls (this computer's own: port 0 until a swap), a test's scripted controller
 * ($TTYD_P2_PRESS...; the test script itself stays controller 1's, Mario's). A seat swap moves everything
 * in one port to the other and back (portInputSwapPorts), so no seat is left without its player's input. */
void portInputSwapPorts(int a, int b);
/* the same for a controller taking another port (player 1's, after theirs went): what is in `from` goes to
   `to` and that port's controllers and scripted ones to `from`; the keyboard leaves `to` only for a swap */
void portInputTakePort(int from, int to);
int portInputHomePort(void); /* the keyboard's and the touch controls' port */
int portInputVirtualAt(int port); /* 1: an input with no device index is there (the keyboard, touch, a script) */
int portInputHasAny(int port); /* 1: a controller or such an input is there */
unsigned portInputSwaps(void); /* how many swaps so far (a port's contents changed hands) */
void portInputApplyPresses(PADStatus* pad); /* the scripted controllers' presses alone (a hold's frames) */
/* run completed ARQ transfers' callbacks (port/src/common/arq_defer_pc.c) */
void portARQPoll(void);
/* $TTYD_FAST: advance the virtual game clock by one frame (headless os_pc.c) */
void portFastClockFrame(void);
/* $TTYD_FAST: the virtual clock, read and put back around the frames Aurora
 * draws between the game's (interp_aurora.c), whose clock reads would move it */
s64 portFastClockGet(void);
void portFastClockSet(s64 t);
/* Aurora: the game runs the same from the same input ($TTYD_FAST, or online
 * co-op's lockstep: the virtual clock, disc reads within their frame, audio
 * rendered per frame); portLockstepBegin starts it with the host's clock
 * (os_time_aurora.c) */
int portLockstep(void);
void portLockstepBegin(s64 base);
/* Aurora, in lockstep: the game clock now (OSTime) without moving it, as a
 * read from drawing or a menu must (the raid's week, port/include/port/raid.h) */
s64 portLockstepClock(void);
/* days since 1970-01-01 (UTC) by portLockstepClock: what every date that
 * picks game content uses (port/include/port/date_seed.h's portDateDay) */
unsigned portLockstepDay(void);
/* the game clock now (what OSGetTime would give) without moving it: for
 * reads from drawing, menus, snapshots and anything else that does not run
 * the same number of times in every game of a session or a replay */
s64 portGameClockPeek(void);
s64 portRealTime(void);
/* Stop the game (the caller holds the main thread), its clock and audio, e.g.
 * while the window is minimized or the app is in the background; nests
 * (port/src/common/pause_pc.c). Game time does not advance while paused. */
void portPause(void);
void portResume(void);
BOOL portIsPaused(void);
/* the backend's game clock (os_pc.c / os_time_aurora.c) and audio (hw_pc.c) */
void portClockPause(void);
void portClockResume(void);
/* Aurora: the game clock at speed times real time (fast-forward); 1 normally,
 * rounded up. The Pct pair: in percent (faster battles' 1.5x is 150). */
int portClockSpeed(void);
void portClockSetSpeedPct(int percent);
int portClockSpeedPct(void);
/* Aurora: and at percent % of that (slow speed, 50-100); 100 normally. */
void portClockSetPercent(int percent);
void salPcSetPaused(BOOL paused);
/* hw_pc.c: no audio callback runs while locked (save states). */
void salPcLock(void);
void salPcUnlock(void);
/* hw_pc.c: no mixing runs between these, without blocking the device (it plays a reserve queued ahead):
   the second screen's menu, each frame. The game thread only, not nested. */
void salPcMixHold(void);
void salPcMixRelease(void);
/* hw_pc.c: the reserve those holds play from kept queued (on) or not (off): on while the second screen is
   there, so its first menu frame has one too; off once it goes, so the mix doesn't stay 30 ms ahead. */
void salPcMixReserve(int on);
/* hw_pc.c: the frame-paced audio's phase in its quanta (save states keep it) */
double salPcPumpDue(void);
void salPcSetPumpDue(double due);
/* arq_defer_pc.c: transfers whose callbacks are still to run. */
u32 portARQPendingCount(void);
/* game_address_pc.c: a host pointer as the 32-bit number the GameCube would
 * have had for it (0x80xxxxxx in MEM1), the same every run */
s32 portGameAddress(const void* p);
/* The VI's retrace count set (a save state or rewind put back: vi_aurora.c, vi_pc.c) */
void portVISetRetraceCount(u32 count);
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
