#ifndef PORT_SAVESTATE_H
#define PORT_SAVESTATE_H

/* Save states (Aurora backend, port/src/aurora/savestate_aurora.c): the
 * whole game kept in memory and put back, for this run of the game. Keys:
 * F5 saves to slot 1, F8 loads it; the settings page has every slot. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_SAVE_STATE_SLOTS 10
/* A slot of the port's own, past the player's: the Enhancements tab's retry on Game Over
 * (port/include/port/gameplus.h), saved quietly at each room and battle, and
 * loadable on the Game Over screen as well. */
#define PORT_SAVE_STATE_RETRY PORT_SAVE_STATE_SLOTS

/* Asks for a save (load 0) or a load into/from slot 0-9; done at the end of
 * the frame, when the game allows it (portSaveStateStatus says the result). */
void portSaveStateRequest(int slot, int load);
/* End of each frame (vi_aurora.c). */
void portSaveStateFrame(void);
/* This build can make save states. */
int portSaveStateSupported(void);
/* A slot's description (when and where), or NULL when empty. */
const char* portSaveStateInfo(int slot);
/* The last result, for a few seconds, or NULL. */
const char* portSaveStateStatus(void);
/* A slot (0-3, or PORT_SAVE_STATE_RETRY) holds a state. */
int portSaveStateHas(int slot);
/* Shows text where the results show, for a few seconds. */
void portSaveStateNotice(const char* text);
/* A player's slot's label (the Practice tab), "" when it has none. */
const char* portSaveStateLabel(int slot);
void portSaveStateSetLabel(int slot, const char* label);

/* For rewind (port/src/aurora/rewind_aurora.c), which keeps states of its
 * own the same way (savestate_aurora.c; Aurora with save states only). */
#if defined(TTYD_SAVE_STATES) && !defined(__cplusplus)
#include <stddef.h>
#include <stdint.h>
#include <aurora/dvd.h>
#include <dolphin/types.h>
#define PORT_SNAP_MAX_FILES 64
typedef struct PortSnapRegion {
    void* addr;
    size_t size;
} PortSnapRegion;
/* What a state holds besides memory: the save-state heap's used size, the
 * clocks, rand()'s state, the REL linked, the disc files open. */
typedef struct PortSnapMeta {
    size_t heapTop;
    s64 gameTime;
    s64 fastClock; /* $TTYD_FAST's virtual clock (os_time_aurora.c) */
    u32 rand;      /* the game's rand() (runtime_pc.c) */
    const void* rel;
    u32 fileCount;
    AuroraDvdOpenFile files[PORT_SNAP_MAX_FILES];
    unsigned lua; /* what the Lua mods kept (portLuaSnapTake, port/include/port/lua_mods.h) */
    u32 retrace;        /* the VI's retrace count, which the game reads as gp->retraceCount */
    double audioDue;    /* the frame-paced audio's phase (salPcPumpDue) */
} PortSnapMeta;
/* The game's memory, in a fixed order; the last one is the save-state heap
 * (heapSize of it). Returns how many. */
int portSnapRegions(PortSnapRegion* out, int max, size_t heapSize);
/* The game can be saved now (restore: or put back, which Game Over allows too). */
int portSnapAllowed(int restore, const char** why);
/* The game's disc thread is in the middle of a read (DVDMgr: a read spans
 * frames, and where the thread is in it lives on its stack, not in the
 * state): no state is taken or put back until it's done. */
int portSnapDiscBusy(void);
/* The game held still (audio held, disc reads finished; pause: the clock and
 * the audio device paused and the GPU idle too, for a load), and let go. */
void portSnapQuiesce(int pause);
void portSnapResume(int pause);
/* The memory codec: packed size, 0 if it doesn't fit in cap. */
size_t portSnapPack(uint8_t* dst, size_t cap, const uint8_t* src, size_t n);
void portSnapUnpack(uint8_t* dst, size_t n, const uint8_t* src, size_t packed);
/* The rest of the state now (0: too many open files), and put back after
 * the memory is (the heap committed to heapTop first: portSnapHeapCommit). */
int portSnapMetaTake(PortSnapMeta* m);
void portSnapMetaPut(const PortSnapMeta* m);
#endif

#ifdef __cplusplus
}
#endif

#endif
