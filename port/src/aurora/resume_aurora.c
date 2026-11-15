/* Quick resume (General > Quick resume, on by default): the game as it is,
 * kept as a save of its own that the launcher's Continue loads, so a phone
 * app closed in the background (iOS ends them as it likes) or a window shut
 * without saving loses nothing.
 *
 * Save states can't do this: they hold this run's memory, pointers and all
 * (savestate_aurora.c). This is a real save instead, made as a Save Block
 * makes one (portCardSnapshot, cardmgr.c) but never written to the card:
 * about every half second while the player has control in the field (not in
 * a scene, a battle, a menu or a partner's action, where a save would catch a
 * script halfway), it's made in memory; it's written out as
 * <user>/resume_<game ID>.gci when the app goes to the background or quits,
 * and once a minute meanwhile. Beside it, resume_<game ID>.txt names the
 * save file it came from, so that Continue loads it as that file and a later
 * Save Block save goes to the player's own file; then "v2", and where and when
 * the save was made ("time=<Unix seconds>", "map=gor_01", "area=<the area's
 * name, UTF-8>"), for the recovery screen after a crash (crash_report.cpp). */
#include "port/file_pc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "port/area_maps.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "mario/mario_pouch.h"
#include "pmario_sound.h"
#include "port/file_write.h"
#include "port/cardsave_pc.h"
#include "port/statesync.h"
#include "port/handheld.h"
#include "port/mode_lending.h"
#include "port/platform.h"
#include "port/playmodes.h"
#include "port/replay.h"
#include "port/resume.h"
#include "port/settings.h"
#include "port/title_menu.h"
#include "port/postgame_extra.h"
#include "port/practice.h"
#include "port/boards.h"
#include "port/brothers.h"
#include "port/party.h"
#include "port/soft_crash.h"
#include "port/special_places.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

BOOL portCardSnapshot(void* image); /* cardmgr.c */

#define GCI_HEADER 0x40
#define SNAP_EVERY 30      /* frames between snapshots */
#define WRITE_EVERY 3600   /* frames between writes of a changed snapshot */

static uint8_t sImage[PORT_CARD_SLOT_SIZE];
static int sHave, sDirty, sSlot;
/* the last write was a periodic one, not synced to the disk: the next flush (background, quit) writes it
   again, synced, even with nothing new */
static int sUnsynced;
static long long sSnapTime; /* when the snapshot was made (the computer's clock, Unix seconds) */
static char sSnapMap[16];   /* and on which map */
static int sSnapLuigiHp = -1, sSnapLuigiFp = -1; /* Brothers mode: Luigi's then (-1: none), Continue keeps them */
static unsigned sFrame, sLastWrite;
static int* sOn;
static void crashSafeForget(const char* gameId, int slot);

static int on(void) {
    if (sOn == NULL) {
        sOn = portSettingsDeclare("quick_resume", 1, 0, 1);
    }
    return *sOn != 0;
}

void portResumePath(char* out, size_t size, const char* gameId, int sidecar) {
    snprintf(out, size, "%s/resume_%s.%s", portSettingsUserPath(), gameId, sidecar ? "txt" : "gci");
}

/* the player has the controls in the field: a save now is a clean one. Not
 * inside a house or shop the game shows on walking in (evt_door.c: the room
 * appears, its floor's collision goes on, snd_door_in): a save there loads
 * with the map as it starts, the inside hidden, and Mario fell through the
 * floor for ever (tou_01's bar); the snapshot from just outside stays. */
/* Nor while a mode lends anything (port/include/port/mode_lending.h: a boss rush's or a raid's loadout, a mode's
 * map, versus's words, the Pit's scaling), from its start's frame (the map change asked, the lent pouch in a
 * room of the file's) to Mario back in his spot: a snapshot then was the lent pouch in a normal room, or a
 * mode's map with no way out and no mode running, which Continue loaded. Nor with a map change asked. */
static int safe(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !seqCheckSeq() && !gp->inBattle && marioChkKey() &&
           marioChkCtrl() &&
           !(psndGetFlag() & PSND_FLAG_MUTE_DOOR_SFX) && /* (inside: snd_door_in; a map change clears it) */
           gp->saveSlot >= 0 && gp->saveSlot < 4 &&
           !portStateSyncCopyHeld() && /* (online: the host's world, never the guest's own) */
           !portModeLending() &&       /* (a mode's: see above) */
           !portTitleTempFile() && /* (the title menu's file for the modes: never saved, title_menu.h) */
           !portExhibitionRoom() && /* (an exhibition's room: no way out but its fight, postgame_extra.h) */
           !portSpecialRoom(gp->currentMapName); /* (a special place: a visit, put back as it ends, special_places.h) */
}

/* quick: the periodic write on the game thread (every 10 s on a handheld), not synced to the disk, so a
 * slow microSD card doesn't hitch the frame each time (portFileWriteCloseQuick: the rename still keeps
 * each file whole against a crash). Every other write (portResumeFlush: the app going to the background,
 * quitting, a restart, sleep) is synced, and writes an unsynced one again, since a handheld whose battery
 * runs out is a real power cut. */
static void resumeWrite(int quick) {
    char path[1024];
    FILE* f;
    static uint8_t header[GCI_HEADER];
    static uint8_t meta[PORT_CARD_META_SIZE];
    int (*const closeFile)(FILE*, const char*) = quick ? portFileWriteCloseQuick : portFileWriteClose;
    if (!sHave || !(sDirty || (sUnsynced && !quick)) || (portReplayPlaying() || portGhostRacing())) {
        return; /* (a replay's playback: someone else's game, not the player's to come back to) */
    }
    /* a GCI as the save warps read one (warp_pc.c): the game ID first, then
     * the card's file header (unused there), then the slot */
    memset(header, 0, sizeof(header));
    memcpy(header, PORT_GAME_ID, 6);
    /* Two files, each written whole: for another save file than the last, the old save goes first and the
     * sidecar is written before the new save, so a crash between them leaves no quick resume, never one of
     * this file's loaded as another's */
    {
        char side[1024];
        int of = -1, version = 0;
        portResumePath(side, sizeof(side), PORT_GAME_ID, 1);
        if ((f = portFopen(side, "r")) != NULL) {
            if (fscanf(f, "%d v%d", &of, &version) != 2 || version != 2) {
                of = -1; /* (none, or an older one's: written again) */
            }
            fclose(f);
        }
        portResumePath(path, sizeof(path), PORT_GAME_ID, 0);
        if (of != sSlot) {
            portRemove(path);
            if ((f = portFileWriteOpen(side, "w")) == NULL) {
                return;
            }
            fprintf(f, "%d\nv2\n", sSlot); /* v2: never made inside a house (0.4.1, safe(); warp_pc.c checks) */
            if (!closeFile(f, side)) {
                return;
            }
        }
    }
    f = portFileWriteOpen(path, "wb");
    if (f == NULL) {
        return;
    }
    fwrite(header, 1, sizeof(header), f);
    fwrite(meta, 1, sizeof(meta), f);
    fwrite(sImage, 1, sizeof(sImage), f);
    if (!closeFile(f, path)) {
        printf("[resume] the quick resume save couldn't be written in the user folder\n");
        fflush(stdout);
        return; /* (the last one stays; tried again at the next write) */
    }
    /* the sidecar again, now with where and when (the same file's: a crash before this leaves the last
       place and time, never another file's) */
    portResumePath(path, sizeof(path), PORT_GAME_ID, 1);
    f = portFileWriteOpen(path, "w");
    if (f != NULL) {
        const char* area = portAreaName(sSnapMap);
        fprintf(f, "%d\nv2\n", sSlot); /* v2: never made inside a house (0.4.1, safe(); warp_pc.c checks) */
        fprintf(f, "time=%lld\nmap=%s\narea=%s\n", sSnapTime, sSnapMap, area != NULL ? area : "");
        if (sSnapLuigiHp >= 0) {
            fprintf(f, "luigi=%d %d\n", sSnapLuigiHp, sSnapLuigiFp); /* (warp_pc.c: Continue keeps them) */
        }
        closeFile(f, path);
    }
    printf("[resume] written: %s, file %d%s\n", sSnapMap, sSlot + 1, quick ? "" : " (synced)");
    fflush(stdout);
    sDirty = 0;
    sUnsynced = quick;
    sLastWrite = sFrame;
}

void portResumeFlush(void) {
    resumeWrite(0);
}

void portResumeForget(const char* gameId, int slot) {
    char path[1024];
    FILE* f;
    int of = 3; /* (Continue's slot when the sidecar is gone) */
    crashSafeForget(gameId, slot); /* (the crash-safe snapshots of that file alike: never the old game over the new) */
    portResumePath(path, sizeof(path), gameId, 1);
    if ((f = portFopen(path, "r")) != NULL) {
        if (fscanf(f, "%d", &of) != 1) {
            of = 3;
        }
        fclose(f);
    }
    if (strcmp(gameId, PORT_GAME_ID) == 0 && sHave && (slot < 0 || sSlot == slot)) {
        sHave = sDirty = sUnsynced = 0; /* this run's, not written out again */
    }
    if (slot >= 0 && of != slot) {
        return;
    }
    portRemove(path);
    portResumePath(path, sizeof(path), gameId, 0);
    if (portRemove(path) == 0) {
        printf("[resume] %s's quick resume save of file %d dropped: the file was replaced\n", gameId, of + 1);
    }
}

int portResumeHave(void) {
    return on() && sHave && !(portReplayPlaying() || portGhostRacing());
}

/* replays (replay.cpp): the last clean save written out now,
 * with the setting off too (a recording starts from it); 0 when none */
int portResumeWriteNow(void) {
    if (!sHave) {
        return 0;
    }
    sDirty = 1;
    portResumeFlush();
    return 1;
}

/* tests (input_pc.c's resume_continue): the last snapshot written out and loaded as the launcher's Continue
 * loads it (its own file slot, the sidecar's lines read: warp_pc.c) */
void portResumeContinueTest(void) {
    char path[1024];
    if (!portResumeWriteNow()) {
        printf("[resume] continue: no snapshot yet\n");
        fflush(stdout);
        return;
    }
    portResumePath(path, sizeof(path), PORT_GAME_ID, 0);
    printf("[resume] continue: file %d\n", sSlot + 1);
    fflush(stdout);
    portWarpRequestResume(path, sSlot);
}

/* replays (replay.cpp): the last clean save in memory, its
 * card slot, or NULL */
const void* portResumeImage(int* slot, size_t* size) {
    if (!sHave) {
        return NULL;
    }
    *slot = sSlot;
    *size = sizeof(sImage);
    return sImage;
}

/* --- Crash-safe snapshots (port/include/port/soft_crash.h) ---
 * The clean save above, also written every few minutes of play and at room changes to two files in turn, on a
 * thread of their own (a phone's flash can take tens of ms for a synced write: never on the game's frame). */

#define CS_EVERY (3 * 60 * 60)  /* frames: 3 minutes */
#define CS_ROOM_GAP (60 * 60)   /* frames: a room change's at most once a minute */
#define CS_FILE_SIZE (GCI_HEADER + PORT_CARD_META_SIZE + PORT_CARD_SLOT_SIZE)

static int* sCsOn;
static unsigned sCsLastFrame;       /* the frame of the last one asked for */
static long long sCsLastTime;       /* its snapshot's time (only a newer one is written) */
static char sCsRoom[16];            /* its map */
static int sCsNext = -1;            /* which file the next one replaces (-1: not looked at yet) */
static const char* sCsPaused;       /* why none are written now (NULL: they are) */
static SDL_AtomicInt sCsBusy;       /* a write in flight: the next waits for its turn */
static SDL_AtomicInt sCsForgotten;  /* bumped by portResumeForget: a write begun before it is dropped */

static int crashSafeOn(void) {
    if (sCsOn == NULL) {
        sCsOn = portSettingsDeclare("crash_safe_resume", 1, 0, 1);
    }
    return *sCsOn != 0;
}

void portCrashSafePath(char* out, size_t size, const char* gameId, int which, int sidecar) {
    snprintf(out, size, "%s/crashsafe_%s_%d.%s", portSettingsUserPath(), gameId, which + 1, sidecar ? "txt" : "gci");
}

void portCrashSafeStatusPath(char* out, size_t size, const char* gameId) {
    snprintf(out, size, "%s/crashsafe_%s.txt", portSettingsUserPath(), gameId);
}

/* why no snapshot may be written now, in a word the recovery screen explains (soft_crash.cpp); NULL: one may.
 * Online: the world is the host's (or shared), never this player's to come back to alone. The rest would
 * break a run's rules: a replay or ghost race plays from its own start, a leaderboard run is checked by its
 * replay, a daily seed and a bingo lockout room are races against others' clocks, a hardcore file has one
 * life (no save state loads either: savestate_aurora.c). */
static const char* crashSafeWhy(void) {
    if (portStateSyncOn() || portStateSyncHostWorld() || portStateSyncCopyHeld()) {
        return "online";
    }
    if (portReplayActive() || portGhostRacing()) {
        return "replay";
    }
    if (portBoardRunState() != PORT_BOARD_RUN_NONE) {
        return "leaderboard";
    }
    if (portDailyFileDate() != 0) {
        return "daily";
    }
    if (portBingoLockout()) {
        return "bingo";
    }
    if (portHardcoreActive()) {
        return "hardcore";
    }
    return NULL;
}

typedef struct CsJob {
    char gci[1024], txt[1024], status[1024];
    char side[256];   /* the .txt's text ("" none: a status write only) */
    char note[160];   /* the status file's text ("" none) */
    uint8_t* data;    /* CS_FILE_SIZE bytes, or NULL */
    int forgotten;    /* sCsForgotten when it was asked for */
    char map[16];
    int slot;
} CsJob;

static int csWrite(void* arg) {
    CsJob* job = (CsJob*)arg;
    const Uint64 t0 = SDL_GetTicksNS();
    int ok = 1;
    if (job->note[0] != '\0') {
        portFileWriteAllSdl(job->status, job->note, strlen(job->note));
    }
    if (job->data != NULL && SDL_GetAtomicInt(&sCsForgotten) == job->forgotten) {
        /* the .txt out first, back last: a crash between leaves a .gci with no .txt, never offered */
        SDL_RemovePath(job->txt);
        ok = portFileWriteAllSdl(job->gci, job->data, CS_FILE_SIZE) &&
             SDL_GetAtomicInt(&sCsForgotten) == job->forgotten &&
             portFileWriteAllSdl(job->txt, job->side, strlen(job->side));
        {
            const char* name = strrchr(job->gci, '/');
            printf("[crashsafe] %s %s: %s, file %d, %u KB in %.1f ms (synced, off the game's thread)\n",
                   ok ? "written" : "not written", name != NULL ? name + 1 : job->gci, job->map, job->slot + 1,
                   (unsigned)(CS_FILE_SIZE >> 10), (double)(SDL_GetTicksNS() - t0) / 1e6);
            fflush(stdout);
        }
    }
    free(job->data);
    free(job);
    SDL_SetAtomicInt(&sCsBusy, 0);
    return ok;
}

/* the job on its own thread (inline where there are no threads: the browser) */
static void csStart(CsJob* job) {
    SDL_Thread* t;
    SDL_SetAtomicInt(&sCsBusy, 1);
    t = SDL_CreateThread(csWrite, "crash-safe snapshot", job);
    if (t != NULL) {
        SDL_DetachThread(t);
    } else {
        csWrite(job);
    }
}

/* which file is the older (or missing), from the .txt's time= line */
static int csOlder(void) {
    long long when[PORT_CRASH_SAFE_KEPT] = {0, 0};
    int i;
    for (i = 0; i < PORT_CRASH_SAFE_KEPT; i++) {
        char path[1024];
        char* text;
        portCrashSafePath(path, sizeof(path), PORT_GAME_ID, i, 1);
        if ((text = (char*)SDL_LoadFile(path, NULL)) != NULL) {
            const char* t = strstr(text, "time=");
            when[i] = t != NULL ? atoll(t + 5) : 0;
            SDL_free(text);
        }
    }
    return when[1] < when[0] ? 1 : 0;
}

static unsigned csEnv(const char* name, unsigned def) {
    const char* v = getenv(name);
    return v != NULL && v[0] != '\0' ? (unsigned)strtoul(v, NULL, 10) : def;
}

/* each retrace, after the snapshot (portResumeFrame) */
static void crashSafeFrame(void) {
    static unsigned sEvery, sGap;
    const char* why;
    int room, due;
    if (sEvery == 0) {
        sEvery = csEnv("TTYD_CRASH_SAFE_EVERY", CS_EVERY);
        sGap = csEnv("TTYD_CRASH_SAFE_ROOM_GAP", CS_ROOM_GAP);
        sEvery = sEvery > 0 ? sEvery : CS_EVERY;
    }
    if (!crashSafeOn() || gp == NULL || SDL_GetAtomicInt(&sCsBusy)) {
        return;
    }
    why = crashSafeWhy();
    if (why != sCsPaused) {
        /* the reason's start and end, for the recovery screen ("none while playing online") */
        CsJob* job = (CsJob*)calloc(1, sizeof(CsJob));
        const long long now = (long long)time(NULL);
        static long long sSince;
        if (job != NULL) {
            portCrashSafeStatusPath(job->status, sizeof(job->status), PORT_GAME_ID);
            if (why != NULL) {
                sSince = now;
                snprintf(job->note, sizeof(job->note), "paused=%s\nsince=%lld\nuntil=0\n", why, now);
            } else {
                snprintf(job->note, sizeof(job->note), "paused=%s\nsince=%lld\nuntil=%lld\n", sCsPaused, sSince, now);
            }
            printf("[crashsafe] %s%s\n", why != NULL ? "none while: " : "again after: ", why != NULL ? why : sCsPaused);
            fflush(stdout);
            sCsPaused = why;
            csStart(job);
        }
        return;
    }
    if (why != NULL || !sHave || sSnapTime <= sCsLastTime || sSlot < 0 || sSlot > 3) {
        return;
    }
    /* every few minutes of play, and when the room changes (not more than once a gap; nor in a run's first gap,
       so a run continued from one that crashes again soon leaves both in place) */
    room = strncmp(sSnapMap, sCsRoom, sizeof(sCsRoom)) != 0 && sFrame - sCsLastFrame >= sGap && sFrame >= sGap;
    due = sFrame - sCsLastFrame >= sEvery && sFrame >= sGap;
    if (!room && !due) {
        return;
    }
    {
        CsJob* job = (CsJob*)calloc(1, sizeof(CsJob));
        uint8_t* data = (uint8_t*)calloc(1, CS_FILE_SIZE);
        const char* area = portAreaName(sSnapMap);
        if (job == NULL || data == NULL) {
            free(job);
            free(data);
            return;
        }
        if (sCsNext < 0) {
            sCsNext = csOlder();
        }
        /* a GCI as quick resume's (resumeWrite): the game ID, the card's file header (unused), the slot */
        memcpy(data, PORT_GAME_ID, 6);
        memcpy(data + GCI_HEADER + PORT_CARD_META_SIZE, sImage, sizeof(sImage));
        job->data = data;
        job->forgotten = SDL_GetAtomicInt(&sCsForgotten);
        job->slot = sSlot;
        snprintf(job->map, sizeof(job->map), "%s", sSnapMap);
        portCrashSafePath(job->gci, sizeof(job->gci), PORT_GAME_ID, sCsNext, 0);
        portCrashSafePath(job->txt, sizeof(job->txt), PORT_GAME_ID, sCsNext, 1);
        snprintf(job->side, sizeof(job->side), "%d\nv2\ntime=%lld\nmap=%s\narea=%s\n", sSlot, sSnapTime, sSnapMap,
                 area != NULL ? area : "");
        printf("[crashsafe] snapshot %d asked for (%s): %s\n", sCsNext + 1, room ? "room change" : "minutes of play",
               sSnapMap);
        fflush(stdout);
        sCsNext = (sCsNext + 1) % PORT_CRASH_SAFE_KEPT;
        sCsLastFrame = sFrame;
        sCsLastTime = sSnapTime;
        memcpy(sCsRoom, sSnapMap, sizeof(sCsRoom));
        csStart(job);
    }
}

/* the save file slot replaced from outside (portResumeForget): its snapshots go too */
static void crashSafeForget(const char* gameId, int slot) {
    int i;
    SDL_AddAtomicInt(&sCsForgotten, 1);
    for (i = 0; i < PORT_CRASH_SAFE_KEPT; i++) {
        char path[1024];
        char* text;
        int of = -1;
        portCrashSafePath(path, sizeof(path), gameId, i, 1);
        if ((text = (char*)SDL_LoadFile(path, NULL)) != NULL) {
            of = text[0] >= '0' && text[0] <= '3' ? text[0] - '0' : -1;
            SDL_free(text);
        }
        if (slot < 0 || of == slot || of < 0) {
            SDL_RemovePath(path);
            portCrashSafePath(path, sizeof(path), gameId, i, 0);
            if (SDL_RemovePath(path)) {
                printf("[crashsafe] %s's snapshot %d dropped: the file was replaced\n", gameId, i + 1);
                fflush(stdout);
            }
        }
    }
}

/* once a retrace (vi_aurora.c); in replays, in memory even with the setting off
 * (a recording from where the player is starts from it); with crash-safe snapshots on, too */
void portResumeFrame(void) {
    sFrame++;
    if (sFrame % SNAP_EVERY == 0) {
        crashSafeFrame(); /* (the last snapshot's: this frame's comes next time) */
    }
    if (sFrame % SNAP_EVERY != 0 || (!on() && !crashSafeOn() && !portReplayActive())) {
        /* (in replays, made with the setting off too: the snapshot reads the
           clock, so the recording and its playback make the same ones) */
        return;
    }
    {
        static int sLentSaid;
        const int lent = gp != NULL ? portModeLending() : 0;
        if (lent != 0 && !sLentSaid) {
            printf("[resume] a mode lends (%d): no snapshot until it's over; the last one stays\n", lent);
            fflush(stdout);
        }
        sLentSaid = lent != 0;
    }
    {
        /* Practice's changes (a Jump to chapter, a warp, stats, flags: port/include/port/practice.h) are no
         * part of the player's file: no snapshot of them until a file is loaded, as autosave skips them; the
         * last clean one stays, so Continue comes back to the player's own game, never a jumped one */
        static int sPracticeSaid;
        const int practice = portPracticeTainted();
        if (practice && !sPracticeSaid) {
            printf("[resume] practice changes: no snapshot until a file is loaded; the last one stays\n");
            fflush(stdout);
        }
        sPracticeSaid = practice;
        if (practice) {
            goto write;
        }
    }
    if (safe() && portCardSnapshot(sImage)) {
        /* (its own name: once this was a second sSnapMap, which hid the file's, so every sidecar said "map="
           and the log "written: , file N") */
        static char sSaidMap[sizeof(gp->currentMapName)];
        if (strncmp(sSaidMap, gp->currentMapName, sizeof(sSaidMap)) != 0) {
            memcpy(sSaidMap, gp->currentMapName, sizeof(sSaidMap));
            sSaidMap[sizeof(sSaidMap) - 1] = '\0';
            printf("[resume] snapshot in %s (level %d)\n", sSaidMap, pouchGetPtr() != NULL ? pouchGetPtr()->level : 0);
            fflush(stdout);
        }
        sHave = 1;
        sDirty = on() && !(portReplayPlaying() || portGhostRacing());
        sSlot = gp->saveSlot;
        sSnapTime = (long long)time(NULL);
        snprintf(sSnapMap, sizeof(sSnapMap), "%.15s", gp->currentMapName);
        sSnapLuigiHp = sSnapLuigiFp = -1;
        if (portBrothersOn()) {
            portBrothersGetStats(&sSnapLuigiHp, &sSnapLuigiFp);
        }
    }
write:
    /* (a handheld: every 10 seconds, as its firmware may end the game without a word) */
    if (sDirty && sFrame - sLastWrite >= (portHandheld() ? WRITE_EVERY / 6 : WRITE_EVERY)) {
        resumeWrite(1); /* (not synced: the background/quit flush syncs it) */
    }
}
