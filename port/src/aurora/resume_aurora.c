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
static unsigned sFrame, sLastWrite;
static int* sOn;

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
           !portExhibitionRoom(); /* (an exhibition's room: no way out but its fight, postgame_extra.h) */
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

/* once a retrace (vi_aurora.c); in replays, in memory even with the setting off
 * (a recording from where the player is starts from it) */
void portResumeFrame(void) {
    sFrame++;
    if (sFrame % SNAP_EVERY != 0 || (!on() && !portReplayActive())) {
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
    }
write:
    /* (a handheld: every 10 seconds, as its firmware may end the game without a word) */
    if (sDirty && sFrame - sLastWrite >= (portHandheld() ? WRITE_EVERY / 6 : WRITE_EVERY)) {
        resumeWrite(1); /* (not synced: the background/quit flush syncs it) */
    }
}
