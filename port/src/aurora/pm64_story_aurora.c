/* Paper Mario 64's bosses in the story (port/include/port/pm64_media.h): each one stands in Rogueport, drawn as
 * its sprite from the linked ROM, once the story has come to it (most once their partner's TTYD counterpart has
 * joined: Jr. Troopa after Goombella, the Koopa Bros. after Koops ...; the last three late), and stays until it's
 * beaten. Bumping into it or talking to it starts its fight: an ordinary battle on its field NPC, as Mario is, scaled
 * to his level, its earliest version (Jr. Troopa's first fight), a loss the game's Game Over (below). A win marks it
 * beaten on this file (GSW 2024..2025: its star, its achievement, and with "Win Paper Mario partners from their
 * bosses" its partner); a beaten one is gone. With "pm64_bosses" and "pm64_story_bosses" on and a ROM linked (not
 * online or in a replay).
 *
 * Each is an NPC of Paper Cut's own (a Toad's model underneath, drawn as the boss: gPortPm64FieldLooks), spawned in
 * a marioStMain hook as Rowf is (pm64_badges_aurora.c) and deleted in its pre hook. The spots: the table below;
 * each on open floor no scene walks through, off the paths and the
 * NPCs. */
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/pm64.h"
#include "port/pm64_foes.h"
#include "port/pm64_media.h"
#include "port/qol.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "port/view.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/hitdrv.h"
#include "driver/mapdrv.h"
#include "driver/icondrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_bero.h"
#include "evt/evt_cmd.h"
#include "evt/evt_npc.h"
#include "evt/evt_story.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

/* The schedule: the fight (modes_aurora.c kPm64Fights, the rush's order), the TTYD partner
 * whose joining brings it (1 Goombella, 2 Koops, 3 Bobbery, 4 Yoshi, 5 Flurrie, 6 Vivian; 0: by the story alone),
 * the story value from which it comes (GSW 0), and its spot: the room, x y z, the way it faces */
typedef struct StoryBoss {
    signed char fight, party;
    short story;
    const char* map;
    short x, y, z, ry;
    const char* where; /* the spot in words (the log, the guide) */
} StoryBoss;

static const StoryBoss kBosses[] = {
    /* Prologue, once Goombella joins: Jr. Troopa (Goombario) */
    {0, 1, 0, "gor_01", -470, 0, 330, 90, "the plaza, south-west, off the fence"},
    /* Chapter 1, once Koops joins: the Koopa Bros. (Kooper) */
    /* (the spots, checked against the rooms' collision and scripts: open floor reached from the doors, no wall
       within 25, no NPC, scene walk or pacing path within 70. gor_03's south lane is 5 below the pavements) */
    {1, 2, 0, "gor_03", -210, -5, 330, 90, "the west side, the south street"},
    /* Chapter 2, once Flurrie joins: Huff N. Puff (Lakilester) */
    {6, 5, 0, "gor_03", 60, 0, 600, 90, "the west side, the south lane's far end, by the square's edge"},
    /* Chapter 3, once Yoshi joins: Tutankoopa (Parakarry) */
    {2, 4, 0, "gor_03", -360, -5, 320, 90, "the west side, the south street, west end"},
    /* Chapter 4, once Vivian joins: Tubba Blubba (Bow) and Lava Piranha (Sushie) */
    /* (not the yard's west end, -490 320: the fence and the barrels wall that strip off) */
    {3, 6, 0, "gor_02", -500, 0, 220, 90, "the east side, the street's west end, before the fence"},
    {5, 6, 0, "gor_03", 360, 0, 100, 270, "the west side, the north-east corner of the square"},
    /* After chapter 4 (back in Rogueport, not Ms. Mowz's joining): General Guy (Watt) */
    {4, 0, STORY_STG4_31_FRANKLYS_HOUSE, "gor_03", -300, 0, 430, 90, "the west side, south"},
#if PM64_BOSS_FIGHTS > 10
    /* Chapter 5, once Bobbery joins: the Koopa Bros.' rematch (Bombette): 050-pm64pairs' 11th fight */
    {10, 3, 0, "gor_01", -200, 0, 470, 90, "the plaza, south, west of the harbour steps"},
#endif
    /* After chapter 7 (the hideout gone, back under Rogueport): the Crystal King */
    {7, 0, STORY_STG7_50_A_WAITING_PROF_FRANKLY, "gor_01", 180, 0, 420, 90, "the Star Haven corner, west end"},
    /* Chapter 8, on reaching the Palace of Shadow: Kammy Koopa */
    {8, 0, STORY_STG8_00_START, "gor_01", 470, 0, 290, 270, "the Star Haven corner, east end"},
    /* After the credits: Bowser (the harbour's pier is too narrow for him: its stairs behind him, the sailors
       pacing z -200 there from chapter 5 on; the west side's roomiest spot is Huff N. Puff's) */
    {9, 0, STORY_STG9_02_RETURN_TO_ROGUEPORT, "gor_02", -340, 0, 380, 90, "the east side, the yard behind the fence"},
};
#define COUNT ((int)(sizeof(kBosses) / sizeof(kBosses[0])))
_Static_assert(COUNT <= PM64_FIELD_LOOKS, "a look for each");

/* --- the setting --- */

static int storyOn(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_story_bosses", 1, 0, 1); /* (pm64_media.cpp's toggle) */
    }
    return sOn != NULL && *sOn && portPm64BossesOn();
}

/* --- who stands where --- */

static int partyJoined(int party) {
    const PouchData* pouch = pouchGetPtr();
    return pouch != NULL && party > 0 && party < 8 && (pouch->partyData[party].flags & POUCH_PARTY_FLAG_JOINED);
}

/* it has come in the story and isn't beaten yet */
static int due(const StoryBoss* b) {
    if (b->fight >= PM64_BOSS_FIGHTS || portPm64BossBeaten(b->fight)) {
        return 0;
    }
    if (b->party > 0 && !partyJoined(b->party)) {
        return 0;
    }
    return swByteGet(0) >= b->story;
}

/* it should stand in this room now: the setting on, no mode's run (a lent loadout joins every partner), in the
 * field */
static int wanted(const StoryBoss* b) {
    const PortModeStatus* s = portModeStatus();
    return storyOn() && (s->mode == PORT_MODE_NONE || s->finished) && strcmp(gp->currentMapName, b->map) == 0 &&
           due(b);
}

static char sName[COUNT][16]; /* its NPC's name: "pc_pm64_sb<i>" */

static const char* npcName(int i) {
    if (sName[i][0] == '\0') {
        snprintf(sName[i], sizeof(sName[i]), "pc_pm64_sb%d", i);
    }
    return sName[i];
}

static int inField(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle;
}

/* --- the fight ---
 * An ordinary battle, as TTYD's own story bosses are fought (gor_00's Lord Crump: evt_npc_set_battle_info,
 * evt_npc_battle_start, evt_npc_wait_battle_end in the room's own script): on the boss's own field NPC, in the room,
 * with Mario's own party, items and stats; no mode, no lent loadout, no refight's way back. Its battle is the boss
 * engine's (evt_pm64boss_battle) on Rogueport's battle stage, scaled to Mario's level, its earliest version where it
 * has one (Jr. Troopa's first fight: PM64_BOSS_FLAG_EARLY), no running away. A win marks it beaten on this file (its
 * star, its achievement, its partner: portPm64BossSetBeaten); a loss is the game's Game Over, as any story fight's.
 * (It once ran through the boss list's refight: a mode, the boss's stage map and back to where Mario stood, touching
 * the boss, whose talk was live at once: the A press after a loss met it again, a fight for ever.) */

static int sArmed[COUNT]; /* a bump counts (Mario has been away from it since it stood, or since the last fight) */
static int sFighting = -1; /* the boss (kBosses index) whose fight is on, -1 none */
static int sFightEvt;      /*   its script (gone without its end: a Game Over, the title, a file loaded) */
static int sCalm;          /* frames in the field since the last battle or map change (a meeting waits 1.5 s) */
#define CALM_FRAMES 90

void portCoopNotice(const char* english); /* second_player_pc.c: a notice over the picture */

/* the room's battle stage (btlsetup_gor_tbl): the harbour's (stg_00_0, Lord Crump's, entry 2) for the harbour and the
 * plaza, the west side's (stg_00_4, the gatekeeper's, entry 5) for the east and west sides */
static int stageOf(const char* map) {
    return strcmp(map, "gor_00") == 0 || strcmp(map, "gor_01") == 0 ? 2 : 5;
}

USER_FUNC(evt_pm64boss_battle); /* pm64_bosses_aurora.c */

/* in: the boss (kBosses index), the battle's result (1 won, 3 lost, 4 fled, 5 none: not set) */
USER_FUNC(evt_pc_story_fight_end) {
    EvtWord* args = event->args;
    const int i = evtGetValue(event, args[0]);
    const int result = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (i >= 0 && i < COUNT) {
        printf("[pm64] story boss: %s %s (battle result %d)\n", portPm64FightEnglish(kBosses[i].fight),
               result == 1 ? "won" : result == 3 ? "lost" : result == 5 ? "not fought" : "left", result);
        if (result == 1) {
            portPm64BossSetBeaten(kBosses[i].fight); /* (its star; with "Beat their boss", its partner) */
        }
        fflush(stdout);
        sArmed[i] = 0;
    }
    sFighting = -1;
    sCalm = 0;
    return EVT_RETURN_DONE;
}

/* LW(0) the NPC's name, LW(1) the boss (pm64_bosses_aurora.c's), LW(2) the stage, LW(3) the flags, LW(4) the
 * kBosses index */
/* blocks until Mario is free (a talk's end: the talk event met it), at most 2 s */
USER_FUNC(evt_pc_story_wait_free) {
    if (isFirstCall) {
        event->userWork[0] = 0;
    }
    if ((!marioChkCtrl() || marioStGetSystemLevel() != 0) && event->userWork[0]++ < 120) {
        return EVT_RETURN_BLOCK;
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(boss_fight)
    CALL(evt_pc_story_wait_free)
    CALL(evt_pm64boss_battle, LW(0), LW(1), LW(2), LW(3), LW(5))
    IF_INT_EQUAL(LW(5), 0)
        CALL(evt_pc_story_fight_end, LW(4), 5)
        RETURN
    END_IF
    CALL(evt_npc_battle_start, LW(0))
    CALL(evt_npc_wait_battle_end)
    CALL(evt_npc_get_battle_result, LW(6))
    CALL(evt_pc_story_fight_end, LW(4), LW(6))
    RETURN
EVT_END()

static void meet(int i, const char* how) {
    const char* why = NULL;
    const int fight = kBosses[i].fight;
    sArmed[i] = 0;
    if (sFighting >= 0 || sCalm < CALM_FRAMES) {
        /* (one on already, or straight back in the field: no meeting from the A press that closed the last one) */
        printf("[pm64] story boss: %s not met (%s): %s\n", portPm64FightEnglish(fight), how,
               sFighting >= 0 ? "a fight is on" : "too soon after the last battle");
        fflush(stdout);
        return;
    }
    if (!portPm64BossesReady()) {
        why = portPm64Linked() == PM64_NONE ? "Link your Paper Mario ROM first." : "Not during online play or a replay.";
    } else if (seqGetSeq() != SEQ_GAME || gp->inBattle) {
        why = "";
    } else {
        why = portModeStartRefusal(); /* (followers, story stretches, indoors, a mode on: their own words) */
    }
    if (why == NULL) {
        EventEntry* e = evtEntry(boss_fight, 10, 0);
        const int flags = PM64_BOSS_FLAG_SCALE | PM64_BOSS_FLAG_EARLY | NPC_BATTLE_NO_ESCAPE;
        if (e == NULL) {
            return;
        }
        sFighting = i;
        sFightEvt = e->eventId;
        e->lwData[0] = (EvtWord)npcName(i);
        e->lwData[1] = portPm64FightBoss(fight);
        e->lwData[2] = stageOf(gp->currentMapName);
        e->lwData[3] = flags;
        e->lwData[4] = i;
        printf("[pm64] story boss: %s met (%s) in %s: an ordinary battle on its NPC (stage %d)\n",
               portPm64FightEnglish(fight), how, gp->currentMapName, stageOf(gp->currentMapName));
    } else {
        printf("[pm64] story boss: %s not now: %s\n", portPm64FightEnglish(fight), why);
        if (why[0] != '\0') {
            portCoopNotice(why); /* (on screen too: once the reason went to the log only, the boss just stood there) */
        }
    }
    fflush(stdout);
}

/* the talk script's: the boss Mario is nearest to (the one he spoke to) */
USER_FUNC(evt_pc_story_boss_talk) {
    MarioWork* mario = marioGetPtr();
    float best = 1e9f;
    int i, pick = -1;
    (void)isFirstCall;
    (void)event;
    for (i = 0; i < COUNT; i++) {
        NpcEntry* npc = npcNameToPtr_NoAssert(npcName(i));
        float dx, dz;
        if (npc == NULL) {
            continue;
        }
        dx = npc->position.x - mario->position.x;
        dz = npc->position.z - mario->position.z;
        if (dx * dx + dz * dz < best) {
            best = dx * dx + dz * dz;
            pick = i;
        }
    }
    if (pick >= 0) {
        meet(pick, "talk");
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(boss_talk)
    CALL(evt_pc_story_boss_talk)
    RETURN
EVT_END()

/* --- the NPCs --- */

static NpcSetupInfo sSetup[COUNT][2];

/* LW(0) its name, LW(1..3) where, LW(4) its turn, LW(5) its setup */
static EVT_BEGIN(boss_spawn)
    CALL(evt_npc_entry, LW(0), STRING("c_kinopio"))
    /* (a Toad's tribe, as Rowf's: evt_npc_setup reads its animations from it) */
    CALL(evt_npc_set_tribe, LW(0), STRING("\x83\x4C\x83\x6D\x83\x73\x83\x49"))
    CALL(evt_npc_set_position, LW(0), LW(1), LW(2), LW(3))
    CALL(evt_npc_set_ry, LW(0), LW(4))
    CALL(evt_npc_setup, LW(5))
    CALL(evt_npc_set_anim, LW(0), STRING("S_1"))
    RETURN
EVT_END()

static int sAsked[COUNT]; /* this visit's spawn asked for */

/* open floor at its spot (a spot under a changed map would stand it in the air) */
static int floorAt(const StoryBoss* b) {
    float hx, hy, hz, nx, ny, nz, dist = 80.0f;
    return hitCheckFilter(NULL, &hx, &hy, &hz, &dist, &nx, &ny, &nz, (float)b->x, (float)b->y + 40.0f, (float)b->z,
                          0.0f, -1.0f, 0.0f) != NULL;
}

static void spawn(int i) {
    const StoryBoss* b = &kBosses[i];
    EventEntry* e;
    if (!floorAt(b)) {
        printf("[pm64] story boss: no floor at %d %d %d in %s: %s stays away\n", b->x, b->y, b->z, b->map,
               portPm64FightEnglish(b->fight));
        fflush(stdout);
        return;
    }
    memset(sSetup[i], 0, sizeof(sSetup[i]));
    sSetup[i][0].name = npcName(i);
    sSetup[i][0].flags = NPC_FLAG_SHADOW_NO_COLLISION;
    sSetup[i][0].talkEvent = boss_talk;
    e = evtEntry(boss_spawn, 10, 0);
    if (e == NULL) {
        return;
    }
    e->lwData[0] = (EvtWord)npcName(i);
    e->lwData[1] = b->x;
    e->lwData[2] = b->y;
    e->lwData[3] = b->z;
    e->lwData[4] = b->ry;
    e->lwData[5] = (EvtWord)sSetup[i];
    printf("[pm64] story boss: %s waits in %s at %d %d %d (%s)\n", portPm64FightEnglish(b->fight), b->map, b->x, b->y,
           b->z, b->where);
    fflush(stdout);
}

/* Each boss's look on its NPC's pose for as long as the NPC stands in the field's work, wherever the sequence is:
 * the frames of a battle's start before it loads (the field still drawn, under the wipe and its frame capture),
 * a battle's end, a scene change's last frames, a Game Over, a menu. Once the looks were cleared outside the field:
 * the Toad underneath showed for those frames (the playtest's "the 64 bosses briefly turn into Toads before the
 * fight loads"). In a battle the field NPCs' poses are freed (npcReleaseFiledNpc) and made again after it, new. A
 * boss that can't be read as itself (the ROM unlinked) keeps its pose: drawn as nothing until it is hidden or gone
 * (portPm64MediaDrawPose), never as the Toad. */
static void holdLooks(void) {
    int i;
    for (i = 0; i < COUNT; i++) {
        NpcEntry* npc = gp != NULL && !gp->inBattle && strcmp(gp->currentMapName, kBosses[i].map) == 0
                            ? npcNameToPtr_NoAssert(npcName(i))
                            : NULL;
        if (npc == NULL || npc->poseId < 0) {
            gPortPm64FieldLooks[i].pose = -1;
            continue;
        }
        if (!portPm64BossFieldLook(portPm64FightBoss(kBosses[i].fight), 0, &gPortPm64FieldLooks[i])) {
            gPortPm64FieldLooks[i].sprite = -1;
        }
        gPortPm64FieldLooks[i].pose = npc->poseId;
    }
    for (i = COUNT; i < PM64_FIELD_LOOKS; i++) {
        gPortPm64FieldLooks[i].pose = -1;
    }
}

/* Before the frame (marioStMain's pre hook): one no longer wanted here (beaten, the setting off, the ROM unlinked)
 * gone; hidden while Mario isn't free, deleted once he is (not after npcMain: its draw is queued by then) */
static int preHook(void) {
    int i;
    if (!inField()) {
        return 0; /* (the looks held while the NPCs stand: frameHook's holdLooks) */
    }
    for (i = 0; i < COUNT; i++) {
        NpcEntry* npc = npcNameToPtr_NoAssert(npcName(i));
        if (npc == NULL || wanted(&kBosses[i])) {
            continue;
        }
        if (marioChkCtrl()) {
            gPortPm64FieldLooks[i].pose = -1;
            npcDeleteGroup(npc);
        } else {
            npc->flags |= NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW;
        }
    }
    return 0; /* (a pre hook: nonzero would skip marioStMain) */
}

/* a bump: Mario walked into it (as a field enemy's touch), free to move, not straight back from its fight */
static void bumps(int i, NpcEntry* npc) {
    MarioWork* mario = marioGetPtr();
    const float dx = npc->position.x - mario->position.x, dz = npc->position.z - mario->position.z;
    const float d = sqrtf(dx * dx + dz * dz);
    const float touch = (npc->width > 0.0f ? npc->width * 0.5f : 14.0f) + 18.0f;
    if (d > touch + 40.0f) {
        sArmed[i] = 1;
        return;
    }
    if (sArmed[i] && d <= touch && fabsf(npc->position.y - mario->position.y) < 40.0f && marioChkCtrl() &&
        marioStGetSystemLevel() == 0 && sFighting < 0) {
        printf("[pm64] story boss: touched at %.0f (its width %.0f)\n", d, npc->width);
        meet(i, "bump");
    }
}

/* once a visit to a room of theirs: who stands in it (the tests' record) */
static void logRoom(void) {
    static char sLogged[16];
    char line[256];
    int i, at, any = 0;
    if (strcmp(sLogged, gp->currentMapName) == 0) {
        return;
    }
    snprintf(sLogged, sizeof(sLogged), "%s", gp->currentMapName);
    at = snprintf(line, sizeof(line), "[pm64] story bosses in %s:", gp->currentMapName);
    for (i = 0; i < COUNT; i++) {
        if (strcmp(kBosses[i].map, gp->currentMapName) == 0) {
            any = 1;
            if (wanted(&kBosses[i]) && at < (int)sizeof(line)) {
                at += snprintf(line + at, sizeof(line) - (size_t)at, " %s;", portPm64FightEnglish(kBosses[i].fight));
            }
        }
    }
    if (any) {
        printf("%s%s\n", line, strchr(line + 6, ';') == NULL ? " none" : "");
        fflush(stdout);
    }
}

/* --- the placement check ($TTYD_PM64_PLACEMENT=1; the pm64_placement scenarios) ---
 * Once a visit, each boss standing in the room, once drawn (its sprite's size known): its bounds and the floor
 * under it ("[pm64] placement: ..."), and whether it is in the way, each logged "[pm64] placement problem: ...":
 *  - it cuts the room's open floor: the walkable floor (10-unit cells: floor Mario stands on under them, a step of
 *    at most 10 to the next, nothing in his way between) in more pieces with the ground it takes cut out (its
 *    sprite's half width, or the reach at which a bump starts its fight, and 45 more: Mario's width with room to
 *    spare) than without: a street, a passage or the way round it closed;
 *  - an entrance (a road's end, a pipe, a house's door: the room's bero hits and its doors' and pipes' hit
 *    objects), another NPC or another boss within 25 of that ground; an entrance hidden behind its sprite from the
 *    camera (Tubba Blubba once stood before the east side's houses' doors);
 *  - its sprite in the floor (floor over its lowest pixel under its width) or over nothing (no floor within 5
 *    under its feet).
 *  - stairs, a landing or an upper street (floor of some size higher than its feet) within 25 of its ground, or
 *    hidden behind its sprite (Bowser once stood before the harbour's stairs).
 * $TTYD_PM64_PLACEMENT=13 also draws the room's floor from above in the log, =14 also lists the room's best spots
 * for each boss with a problem (all the checks above passed, the most room to spare first; =15 for each boss): to
 * choose the table's. */
#define PL_STEP 10.0f
#define PL_X0 (-900.0f)
#define PL_Z0 (-500.0f)
#define PL_NX 181
#define PL_NZ 121
#define PL_PIECE 12 /* cells: a smaller piece cut off (a corner behind it) doesn't count */
static float sPlY[PL_NX][PL_NZ];
static unsigned char sPlOpen[PL_NX][PL_NZ]; /* 1 floor; 2 +x open, 4 +z open (to the next cell) */
static short sPlPiece[PL_NX][PL_NZ];
static int sPlQueue[PL_NX * PL_NZ];
static unsigned char sPlMain[PL_NX][PL_NZ]; /* 1 the largest piece (the room's main floor), 2 another of 30 cells
                                                or more (an upper street, a landing: not a crate's top) */
static int sPlLargestId;
static char sPlRoom[16]; /* the room the grid is of */
static struct {
    Vec at;
    char name[24];
} sPlEnt[48]; /* the room's entrances: its bero hits, its doors' and pipes' hit objects */
static int sPlEntN;
static int sPlChecked[COUNT];

static int plMode(void) {
    static int sMode = -1;
    if (sMode < 0) {
        const char* e = getenv("TTYD_PM64_PLACEMENT");
        sMode = e == NULL || e[0] != '1' ? 0 : e[1] >= '2' && e[1] <= '9' ? e[1] - '0' : 1;
    }
    return sMode;
}

/* what Mario's own rays test (mario_hit.c chkfilterVec, without its hidden blocks) */
static BOOL plFilter(HitCheckQuery* query, HitEntry* entry) {
    (void)query;
    if (entry->attributes & (HIT_ATTR_ALWAYS_SOLID | HIT_ATTR_TUBE_PASS)) {
        return TRUE;
    }
    return (entry->attributes & HIT_ATTR_PARTY_PASS) || !(entry->attributes & HIT_ATTR_PASS_THROUGH);
}

/* floor Mario can stand on under (x, y + 60, z), within 140: not water he sinks in (the harbour's), nor spikes */
static int plFloor(float x, float y, float z, float* out) {
    float hx, hy, hz, nx, ny, nz, dist = 140.0f;
    const HitEntry* hit =
        hitCheckFilter(NULL, &hx, &hy, &hz, &dist, &nx, &ny, &nz, x, y + 60.0f, z, 0.0f, -1.0f, 0.0f);
    if (hit == NULL || ny < 0.6f || (hit->attributes & (HIT_ATTR_DEEP_WATER | HIT_ATTR_DAMAGE_LIQUID | HIT_ATTR_SPIKES))) {
        return 0;
    }
    *out = hy;
    return 1;
}

/* nothing in the way of Mario (about 40 tall) from (x, y, z) a cell along (dx, dz): a fence's rails, a crate */
static int plClear(float x, float y, float z, float dx, float dz) {
    static const float kAt[3] = {12.0f, 24.0f, 36.0f};
    int k;
    for (k = 0; k < 3; k++) {
        float hx, hy, hz, nx, ny, nz, dist = PL_STEP;
        if (hitCheckFilter(plFilter, &hx, &hy, &hz, &dist, &nx, &ny, &nz, x, y + kAt[k], z, dx, 0.0f, dz) != NULL) {
            return 0;
        }
    }
    return 1;
}

/* the room's entrances: its bero hits (roads, pipes, doors to other rooms) and every hit object a door or a pipe
 * ("doa", "door", "dokan" in its name: Rogueport's houses' doors are evt_door's, not beros) */
static void plEntrances(void) {
    MapWork* wp = mapGetWork();
    BeroEntry** bero = bero_get_ptr();
    int i, j, k;
    sPlEntN = 0;
    for (i = 0; wp != NULL && i < wp->entries->count; i++) {
        const MapEntry* map = &wp->entries[i];
        const HitEntry* hit = map->hitEntries;
        for (j = 0; hit != NULL && j < map->hitNumJoints && sPlEntN < 48; j++, hit++) {
            const char* name = hit->joint != NULL ? hit->joint->name : NULL;
            int is = 0;
            if (name == NULL || (hit->flags & HIT_FLAG_DELETED)) {
                continue;
            }
            is = strstr(name, "doa") != NULL || strstr(name, "door") != NULL || strstr(name, "dokan") != NULL;
            for (k = 0; !is && bero != NULL && k < 16 && bero[k] != NULL; k++) {
                is = bero[k]->hitName != NULL && strcmp(bero[k]->hitName, name) == 0;
            }
            if (is) {
                sPlEnt[sPlEntN].at = hit->centerWorld;
                snprintf(sPlEnt[sPlEntN].name, sizeof(sPlEnt[sPlEntN].name), "%s", name);
                sPlEntN++;
            }
        }
    }
}

/* the room's floor, once a room (from over the highest floor Mario walks on to under the lowest) */
static void plGrid(void) {
    int u, v;
    if (strcmp(sPlRoom, gp->currentMapName) == 0) {
        return;
    }
    snprintf(sPlRoom, sizeof(sPlRoom), "%s", gp->currentMapName);
    plEntrances();
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            const float x = PL_X0 + (float)u * PL_STEP, z = PL_Z0 + (float)v * PL_STEP;
            sPlOpen[u][v] = (unsigned char)(plFloor(x, 100.0f, z, &sPlY[u][v]) || plFloor(x, 0.0f, z, &sPlY[u][v]));
        }
    }
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            const float x = PL_X0 + (float)u * PL_STEP, z = PL_Z0 + (float)v * PL_STEP;
            if (!(sPlOpen[u][v] & 1)) {
                continue;
            }
            if (u + 1 < PL_NX && (sPlOpen[u + 1][v] & 1) && fabsf(sPlY[u + 1][v] - sPlY[u][v]) <= 10.0f &&
                plClear(x, sPlY[u][v], z, 1.0f, 0.0f) && plClear(x + PL_STEP, sPlY[u + 1][v], z, -1.0f, 0.0f)) {
                sPlOpen[u][v] |= 2;
            }
            if (v + 1 < PL_NZ && (sPlOpen[u][v + 1] & 1) && fabsf(sPlY[u][v + 1] - sPlY[u][v]) <= 10.0f &&
                plClear(x, sPlY[u][v], z, 0.0f, 1.0f) && plClear(x, sPlY[u][v + 1], z + PL_STEP, 0.0f, -1.0f)) {
                sPlOpen[u][v] |= 4;
            }
        }
    }
}

/* the open floor's pieces of PL_PIECE cells or more, the disc of radius cut about (cx, cz) taken out (cut 0:
 * none); the largest's size in *largest */
static int plPieces(float cx, float cz, float cut, int* largest) {
    int u, v, n = 0, big = 0;
    *largest = 0;
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            const float du = PL_X0 + (float)u * PL_STEP - cx, dv = PL_Z0 + (float)v * PL_STEP - cz;
            sPlPiece[u][v] = (sPlOpen[u][v] & 1) && (cut <= 0.0f || du * du + dv * dv > cut * cut) ? 0 : -1;
        }
    }
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            int head = 0, tail = 0, size = 0;
            if (sPlPiece[u][v] != 0) {
                continue;
            }
            n = n < 32000 ? n + 1 : n;
            sPlPiece[u][v] = (short)n;
            sPlQueue[tail++] = u * PL_NZ + v;
            while (head < tail) {
                const int a = sPlQueue[head] / PL_NZ, c = sPlQueue[head] % PL_NZ;
                int k;
                head++;
                size++;
                for (k = 0; k < 4; k++) {
                    const int a2 = a + (k == 0) - (k == 1), c2 = c + (k == 2) - (k == 3);
                    int open;
                    if (a2 < 0 || a2 >= PL_NX || c2 < 0 || c2 >= PL_NZ || sPlPiece[a2][c2] != 0) {
                        continue;
                    }
                    open = k == 0   ? sPlOpen[a][c] & 2
                           : k == 1 ? sPlOpen[a2][c2] & 2
                           : k == 2 ? sPlOpen[a][c] & 4
                                    : sPlOpen[a2][c2] & 4;
                    if (open) {
                        sPlPiece[a2][c2] = (short)n;
                        sPlQueue[tail++] = a2 * PL_NZ + c2;
                    }
                }
            }
            if (size >= PL_PIECE) {
                big++;
                if (size > *largest) {
                    *largest = size;
                    sPlLargestId = n;
                }
            }
        }
    }
    return big;
}

/* the floor under (x, z) as the grid has it nearest to height y: 0 none */
static int plFloorAt(float x, float z, float y, float* out) {
    const int u = (int)floorf((x - PL_X0) / PL_STEP + 0.5f), v = (int)floorf((z - PL_Z0) / PL_STEP + 0.5f);
    if (u < 0 || u >= PL_NX || v < 0 || v >= PL_NZ || !(sPlOpen[u][v] & 1) || fabsf(sPlY[u][v] - y) > 30.0f) {
        return plFloor(x, y, z, out); /* (off the grid, or another level than the grid's: a ray of its own) */
    }
    *out = sPlY[u][v];
    return 1;
}

/* the room's main floor (its largest piece: the floor Mario walks), once a room */
static void plMainFill(void) {
    static char sDone[16];
    static int sSize[32001];
    int u, v, largest;
    if (strcmp(sDone, sPlRoom) == 0) {
        return;
    }
    snprintf(sDone, sizeof(sDone), "%s", sPlRoom);
    plPieces(0.0f, 0.0f, 0.0f, &largest);
    memset(sSize, 0, sizeof(sSize));
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            if (sPlPiece[u][v] > 0) {
                sSize[sPlPiece[u][v]]++;
            }
        }
    }
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            const int p = sPlPiece[u][v];
            sPlMain[u][v] = p == sPlLargestId ? 1 : p > 0 && sSize[p] >= 30 ? 2 : 0;
        }
    }
}

/* higher floor of some size (stairs, a landing, an upper street: a way up) within r of (x, z), or hidden behind its
 * sprite from the camera (along fx fz, nearer than depth, within half and 10 across): 1, what in *what */
static int plSlope(float x, float z, float y0, float r, float fx, float fz, float depth, float half, char* what,
                   size_t size) {
    int u, v;
    for (u = 0; u < PL_NX; u++) {
        for (v = 0; v < PL_NZ; v++) {
            const float dx = PL_X0 + (float)u * PL_STEP - x, dz = PL_Z0 + (float)v * PL_STEP - z;
            const float along = dx * fx + dz * fz, across = fabsf(dx * fz - dz * fx);
            if (!sPlMain[u][v] || sPlY[u][v] - y0 <= 12.0f || sPlY[u][v] - y0 > 60.0f) {
                continue; /* (higher floor Mario steps or climbs to: stairs, a landing; not a roof) */
            }
            if (dx * dx + dz * dz < r * r) {
                snprintf(what, size, "a way up or down (floor %.0f over its feet) %.0f away, at %.0f %.0f",
                         sPlY[u][v] - y0, sqrtf(dx * dx + dz * dz), x + dx, z + dz);
                return 1;
            }
            if (depth > 0.0f && along > 0.0f && along < depth && across < half + 10.0f) {
                snprintf(what, size, "a way up or down hidden behind its sprite, %.0f behind it, at %.0f %.0f", along,
                         x + dx, z + dz);
                return 1;
            }
        }
    }
    return 0;
}

/* the nearest thing in the way of a boss standing at (x, z) with ground cut: an entrance, an NPC (not self), another
 * boss of this room at its spot; its distance less (cut + 25) and the thing in *what; 1e9 none */
static float plClearance(int self, float x, float z, float cut, char* what, size_t size) {
    float best = 1e9f;
    int k;
    NpcWork* wp = npcGetWorkPtr();
    for (k = 0; k < sPlEntN; k++) {
        const Vec at = sPlEnt[k].at;
        const float d = sqrtf((at.x - x) * (at.x - x) + (at.z - z) * (at.z - z)) - (cut + 25.0f);
        if (d < best) {
            best = d;
            snprintf(what, size, "an entrance (%s) %.0f away, at %.0f %.0f", sPlEnt[k].name, d + cut + 25.0f, at.x,
                     at.z);
        }
    }
    for (k = 0; wp != NULL && k < wp->total; k++) {
        const NpcEntry* e = &wp->entries[k];
        float d;
        if (!(e->flags & NPC_FLAG_USED) || (e->flags & NPC_FLAG_HIDDEN) || strcmp(e->description, npcName(self)) == 0) {
            continue;
        }
        d = sqrtf((e->position.x - x) * (e->position.x - x) + (e->position.z - z) * (e->position.z - z)) -
            (cut + 25.0f);
        if (d < best) {
            best = d;
            snprintf(what, size, "%s (%s) %.0f away, at %.0f %.0f %.0f",
                     strncmp(e->description, "pc_pm64_sb", 10) == 0 ? "another boss" : "an NPC", e->description,
                     d + cut + 25.0f, e->position.x, e->position.y, e->position.z);
        }
    }
    return best;
}

/* an entrance, stairs or a slope in its way (plSlope), or an entrance its sprite (half its width, top over its feet)
 * hides from the field's camera standing at (x, z): one behind it, within its width, nearer than where the line from the camera over its top meets the floor (the camera
 * as it looks now, from as far: Rogueport's look along a street) and than twice its height; 0 none */
static int plHidden(float x, float z, float y0, float half, float top, float cut, char* what, size_t size) {
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    float fx, fz, dist, height, depth;
    int k;
    if (cam == NULL) {
        return 0;
    }
    fx = cam->cameraAt.x - cam->cameraPos.x;
    fz = cam->cameraAt.z - cam->cameraPos.z;
    dist = sqrtf(fx * fx + fz * fz);
    height = cam->cameraPos.y - cam->cameraAt.y;
    if (dist < 1.0f) {
        return 0;
    }
    fx /= dist;
    fz /= dist;
    depth = height > top + 1.0f ? top * dist / (height - top) : 1000.0f;
    /* (no further than twice its height: a door across a plaza is seen past it as Mario walks) */
    depth = depth < 2.0f * top ? depth : 2.0f * top;
    for (k = 0; k < sPlEntN; k++) {
        const float dx = sPlEnt[k].at.x - x, dz = sPlEnt[k].at.z - z;
        const float along = dx * fx + dz * fz, across = fabsf(dx * fz - dz * fx);
        if (along > 0.0f && along < depth && across < half + 10.0f) {
            snprintf(what, size, "an entrance (%s) hidden behind its sprite, %.0f behind it, at %.0f %.0f",
                     sPlEnt[k].name, along, sPlEnt[k].at.x, sPlEnt[k].at.z);
            return 1;
        }
    }
    return plSlope(x, z, y0, cut + 25.0f, fx, fz, depth, half, what, size);
}

/* the room's floor from above, every other cell (20 units), +x right, +z down: ' ' none, '.' floor within 25 of 0
   ('^' higher, '_' lower), ':' floor walled off from the cell to its right or below, 'B' a boss, 'N' an NPC, 'E'
   an entrance */
static void plRoomMap(void) {
    static char sDone[16];
    char row[PL_NX / 2 + 2];
    int u, v, k;
    if (plMode() < 3 || strcmp(sDone, gp->currentMapName) == 0) {
        return;
    }
    snprintf(sDone, sizeof(sDone), "%s", gp->currentMapName);
    printf("[pm64] room map: %s, x %.0f..%.0f (20 a cell), z %.0f..%.0f\n", gp->currentMapName, PL_X0,
           PL_X0 + (PL_NX - 1) * PL_STEP, PL_Z0, PL_Z0 + (PL_NZ - 1) * PL_STEP);
    for (v = 0; v < PL_NZ; v += 2) {
        NpcWork* wp = npcGetWorkPtr();
        for (u = 0; u < PL_NX; u += 2) {
            const unsigned char o = sPlOpen[u][v];
            const float y = sPlY[u][v];
            char c = ' ';
            if (o & 1) {
                c = y > 25.0f ? '^' : y < -25.0f ? '_' : '.';
                if ((u + 1 < PL_NX && (sPlOpen[u + 1][v] & 1) && !(o & 2)) ||
                    (v + 1 < PL_NZ && (sPlOpen[u][v + 1] & 1) && !(o & 4))) {
                    c = ':';
                }
            }
            row[u / 2] = c;
        }
        row[PL_NX / 2 + 1] = '\0';
        for (k = 0; wp != NULL && k < wp->total; k++) {
            const NpcEntry* n = &wp->entries[k];
            const int cu = (int)floorf((n->position.x - PL_X0) / 20.0f + 0.5f);
            const int cv = (int)floorf((n->position.z - PL_Z0) / 20.0f + 0.5f);
            if ((n->flags & NPC_FLAG_USED) && cv * 2 == v && cu >= 0 && cu <= PL_NX / 2) {
                row[cu] = strncmp(n->description, "pc_pm64_sb", 10) == 0 ? 'B' : 'N';
            }
        }
        for (k = 0; k < sPlEntN; k++) {
            const int cu = (int)floorf((sPlEnt[k].at.x - PL_X0) / 20.0f + 0.5f);
            const int cv = (int)floorf((sPlEnt[k].at.z - PL_Z0) / 20.0f + 0.5f);
            if (cv * 2 == v && cu >= 0 && cu <= PL_NX / 2) {
                row[cu] = 'E';
            }
        }
        printf("[pm64] room map: %4.0f |%s|\n", PL_Z0 + (float)v * PL_STEP, row);
    }
    fflush(stdout);
}

/* the room's best spots for boss i (ground cut, half width half) at its own height: every check passed, the most
 * room to spare first (=14) */
static void plSearch(int i, float y0, float cut, float half, float top) {
    float bestD[6], bestX[6], bestZ[6];
    char what[160];
    int u, v, k, n = 0, pieces, largest, l2;
    if (plMode() < 4) {
        return;
    }
    pieces = plPieces(0.0f, 0.0f, 0.0f, &largest);
    for (k = 0; k < 6; k++) {
        bestD[k] = -1.0f;
    }
    for (u = 0; u < PL_NX; u += 2) {
        for (v = 0; v < PL_NZ; v += 2) {
            const float x = PL_X0 + (float)u * PL_STEP, z = PL_Z0 + (float)v * PL_STEP;
            float d, f;
            int s, ok = 1;
            if (sPlMain[u][v] != 1 || fabsf(sPlY[u][v] - y0) > 6.0f) { /* (on the floor Mario walks: the main piece) */
                continue;
            }
            for (s = -2; s <= 2 && ok; s++) {
                ok = plFloorAt(x + half * 0.5f * (float)s, z, sPlY[u][v], &f) && f <= sPlY[u][v] + 2.0f &&
                     f >= sPlY[u][v] - 5.0f;
            }
            if (!ok || (d = plClearance(i, x, z, cut, what, sizeof(what))) < 0.0f || d <= bestD[5] ||
                plHidden(x, z, y0, half, top, cut, what, sizeof(what))) {
                continue;
            }
            if (plPieces(x, z, cut, &l2) > pieces) {
                continue;
            }
            for (k = 5; k > 0 && d > bestD[k - 1]; k--) {
                bestD[k] = bestD[k - 1];
                bestX[k] = bestX[k - 1];
                bestZ[k] = bestZ[k - 1];
            }
            bestD[k] = d;
            bestX[k] = x;
            bestZ[k] = z;
            n++;
        }
    }
    for (k = 0; k < 6 && bestD[k] >= 0.0f; k++) {
        printf("[pm64] placement spot: %s in %s: %.0f %.0f %.0f, %.0f to spare\n", portPm64FightEnglish(kBosses[i].fight),
               gp->currentMapName, bestX[k], y0, bestZ[k], bestD[k]);
    }
    if (n == 0) {
        printf("[pm64] placement spot: %s in %s: none\n", portPm64FightEnglish(kBosses[i].fight), gp->currentMapName);
    }
}

static void plProblem(const StoryBoss* b, const char* what) {
    printf("[pm64] placement problem: %s in %s at %d %d %d: %s\n", portPm64FightEnglish(b->fight), b->map, b->x, b->y,
           b->z, what);
}

static void placementCheck(int i, NpcEntry* npc) {
    const StoryBoss* b = &kBosses[i];
    const float bx = npc->position.x, by = npc->position.y, bz = npc->position.z;
    float half, bottom, top, reach, cut, y, lowest = 1e9f, highest = -1e9f, spare;
    int k, nBefore, nAfter, lBefore, lAfter, problems = 0;
    char what[200];
    if (plMode() == 0 || sPlChecked[i] || !portPm64FieldLookBounds(i, &half, &bottom, &top)) {
        return;
    }
    sPlChecked[i] = 1;
    plGrid();
    plMainFill();
    plRoomMap();
    reach = (npc->width > 0.0f ? npc->width * 0.5f : 14.0f) + 18.0f; /* (bumps: where a bump starts its fight) */
    /* (and Mario's width with room to spare: a way past it just a Mario wide, brushing its sprite, is no way) */
    cut = (half > reach ? half : reach) + 45.0f;
    /* the floor under its width (its sprite faces the camera: across the room's x) */
    for (k = -2; k <= 2; k++) {
        if (plFloorAt(bx + half * 0.5f * (float)k, bz, by, &y)) {
            lowest = y < lowest ? y : lowest;
            highest = y > highest ? y : highest;
        }
    }
    nBefore = plPieces(0.0f, 0.0f, 0.0f, &lBefore);
    nAfter = plPieces(bx, bz, cut, &lAfter);
    printf("[pm64] placement: %s in %s at %.0f %.0f %.0f: half width %.0f, %.0f to %.0f over its feet, reach %.0f "
           "(ground taken %.0f), floor under it %.0f to %.0f, the room's open floor in %d pieces (the largest %d "
           "cells), %d with it (%d)\n",
           portPm64FightEnglish(b->fight), b->map, bx, by, bz, half, bottom, top, reach, cut,
           lowest < 1e8f ? lowest - by : 0.0f, highest > -1e8f ? highest - by : 0.0f, nBefore, lBefore, nAfter, lAfter);
    if (nAfter > nBefore) {
        snprintf(what, sizeof(what), "it cuts the room's open floor in %d pieces, not %d (a way past it closed)",
                 nAfter, nBefore);
        plProblem(b, what);
        problems++;
    }
    {
        /* Mario can walk up to it: the main piece within its bump's reach (Tubba Blubba once stood in the east
           side's yard's west strip, walled off by the fence and the barrels: the piece count didn't show it) */
        const int cu = (int)floorf((bx - PL_X0) / PL_STEP + 0.5f), cv = (int)floorf((bz - PL_Z0) / PL_STEP + 0.5f);
        const int r = (int)(reach / PL_STEP) + 1;
        int u, v, near = 0;
        for (u = cu - r; u <= cu + r && !near; u++) {
            for (v = cv - r; v <= cv + r && !near; v++) {
                near = u >= 0 && u < PL_NX && v >= 0 && v < PL_NZ && sPlMain[u][v] == 1 &&
                       fabsf(sPlY[u][v] - by) <= 10.0f;
            }
        }
        if (!near) {
            plProblem(b, "off the room's main floor: Mario can't walk up to it (walled off)");
            problems++;
        }
    }
    if (lowest > 1e8f || lowest < by - 5.0f) {
        plProblem(b, "no floor under its feet");
        problems++;
    } else if (highest > by + bottom + 2.0f) {
        snprintf(what, sizeof(what), "its sprite in the floor (floor %.0f over its feet, its lowest pixel %.0f)",
                 highest - by, bottom);
        plProblem(b, what);
        problems++;
    }
    spare = plClearance(i, bx, bz, cut, what, sizeof(what));
    if (spare < 0.0f) {
        plProblem(b, what);
        problems++;
    }
    if (plHidden(bx, bz, by, half, top, cut, what, sizeof(what))) {
        plProblem(b, what);
        problems++;
    }
    if (problems > 0 || plMode() >= 5) {
        plSearch(i, by, cut, half, top); /* (=14: where it could stand instead; =15 for every boss) */
    }
    fflush(stdout);
}

/* --- the badge over its head ---
 * A boss whose win frees a Paper Mario 64 partner (pm64_join on, the partner not joined yet) wears that partner's
 * face over its head: the outlined face of the Party tab and the co-op cards (portPm64MenuFace) in a small round
 * badge, bobbing gently. A 3D billboard on the field camera, so widescreen, stereo 3D and the room's depth (a wall
 * in front hides it) are the field's own; drawn a little toward the camera, never into the boss's own sprite. Only
 * while Mario is free in the field: not in a scene, a menu, a battle, with the HUD hidden (the 2D camera off, photo
 * mode's HUD off), in a replay or console-accurate mode. Drawn only: online each game shows its own file's. */

#define BADGE_R 15.0f   /* its radius, the field's units (Mario stands about 40 tall) */
#define BADGE_FADE 10   /* frames to come and go */
#define BADGE_STAR 0    /* the bosses that free nobody: 1 a small star badge, 0 none */

static int sBadgeFade[COUNT];     /* 0..BADGE_FADE */
static Vec sBadgeAt[COUNT];       /* its foot: the boss's feet raised to its head */
static unsigned sBadgeTick;
static int sBadgesDrawn, sBadgesDrawnLast; /* drawn this frame, and as last told (the tests' record) */

/* the partner a boss's win frees now: 1..8, 0 none (pm64_join off, joined already, beaten), -1 one that frees
 * nobody (the Crystal King, Kammy, Bowser) */
static int badgePartner(const StoryBoss* b) {
    int p[2], n, k;
    if (portPm64BossBeaten(b->fight) || !portPm64PartnersActive()) {
        return 0;
    }
    n = portPm64BossPartners(b->fight, p);
    if (n == 0) {
        return -1;
    }
    if (!portPm64JoinByBoss()) {
        return 0;
    }
    for (k = 0; k < n; k++) {
        if (!portPm64PartnerMet(p[k]) && !portPm64PartnerAvailable(p[k])) {
            return p[k];
        }
    }
    return 0;
}

static int badgesShown(void) {
    const CameraEntry* hud = camGetPtr(CAMERA_2D);
    return !seqCheckSeq() && marioChkCtrl() && marioStGetSystemLevel() == 0 && hud != NULL &&
           !(hud->flags & CAM_FLAG_HIDDEN) && !portReplayActive() && !portConsoleAccurate();
}

/* the badge's flat state: vertex colours, then (tex) a texture times them; the depth tested, not written */
static void badgeState(int tex) {
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0, 0, 0, 0});
    GXSetCullMode(GX_CULL_NONE);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTevStages(1);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    if (tex) {
        GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
        GXSetNumTexGens(1);
        GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    } else {
        GXSetNumTexGens(0);
        GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }
}

/* a filled disc of radius r about (x, y), z, in colour c */
static void badgeDisc(float x, float y, float z, float r, GXColor c) {
    int k;
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 34);
    GXPosition3f32(x, y, z);
    GXColor4u8(c.r, c.g, c.b, c.a);
    for (k = 0; k <= 32; k++) {
        const float t = (float)k * (6.2831853f / 32.0f);
        GXPosition3f32(x + cosf(t) * r, y + sinf(t) * r, z);
        GXColor4u8(c.r, c.g, c.b, c.a);
    }
}

/* a triangle pointing down from the disc (its tail, toward the head), half-width w, from y0 down to y1 */
static void badgeTail(float w, float y0, float y1, float z, GXColor c) {
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GXPosition3f32(-w, y0, z);
    GXColor4u8(c.r, c.g, c.b, c.a);
    GXPosition3f32(w, y0, z);
    GXColor4u8(c.r, c.g, c.b, c.a);
    GXPosition3f32(0.0f, y1, z);
    GXColor4u8(c.r, c.g, c.b, c.a);
}

/* each face's visible pixels (x, y, w, h in its texture), measured once per face texture: the faces sit in their
 * textures with uneven room round them */
static int sFaceBox[PM64_PARTNER_COUNT][4];
static const void* sFaceBoxOf[PM64_PARTNER_COUNT];

static void faceBounds(int p, const void* face, int size) {
    int x0, y0, x1, y1;
    if (sFaceBoxOf[p] == face) {
        return;
    }
    sFaceBoxOf[p] = face;
    if (!portPm64TexOpaqueBounds(face, size, size, size, &x0, &y0, &x1, &y1)) {
        x0 = y0 = 0;
        x1 = y1 = size - 1;
    }
    sFaceBox[p][0] = x0;
    sFaceBox[p][1] = y0;
    sFaceBox[p][2] = x1 - x0 + 1;
    sFaceBox[p][3] = y1 - y0 + 1;
    printf("[pm64] story boss badge face %s: %d %d %dx%d of %d\n", portPm64PartnerName(p), x0, y0, x1 - x0 + 1,
           y1 - y0 + 1, size);
    fflush(stdout);
}

static void badgeDisp(CameraId camId, void* param) {
    const int i = (int)(intptr_t)param;
    const CameraEntry* cam = camGetPtr(camId);
    int p, size = 0;
    float fade, bob, d, k, r, h, u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f, head[3];
    const void* face = NULL;
    GXTexObj obj;
    Vec at, v;
    Mtx m;
    u8 a;
    if (i < 0 || i >= COUNT || cam == NULL || !inField() || (portPhotoOn() && portPhotoHideHud())) {
        return;
    }
    p = badgePartner(&kBosses[i]);
    if (p > 0) {
        face = portPm64MenuFace(p, &size);
        if (face == NULL) {
            return;
        }
    } else if (!BADGE_STAR || p == 0) {
        return;
    }
    fade = (float)sBadgeFade[i] / (float)BADGE_FADE;
    fade = fade * fade * (3.0f - 2.0f * fade);
    bob = sinf((float)sBadgeTick * 0.07f + (float)i * 1.3f) * 1.5f + 1.5f; /* (0..3: never down into the head) */
    /* in the camera's space: the badge's middle, pulled 30 toward the eye (the same size on the screen) */
    /* over its head as drawn this frame (its sprite's visible top centre), the tail's tip just above it */
    /* (its height only: over the NPC's own place, as its sprite stands centred on it) */
    at = sBadgeAt[i];
    if (portPm64FieldLookHead(i, head)) {
        at.y = head[1];
        if (kBosses[i].fight == 3) {
            at.y -= 22.0f; /* (Tubba Blubba: his sprite's top is the tuft of hair over his head, not his head) */
        }
    }
    at.y += BADGE_R * 1.42f - 2.0f + bob;
    PSMTXMultVec((MtxPtr)cam->view, &at, &v);
    d = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (d < 40.0f) {
        return;
    }
    k = (d - 30.0f) / d;
    PSMTXTrans(m, v.x * k, v.y * k, v.z * k);
    GXLoadPosMtxImm(m, GX_PNMTX0);
    r = BADGE_R * k * (p > 0 ? 1.0f : 0.7f) * (0.85f + 0.15f * fade);
    a = (u8)(255.0f * fade);
    badgeState(0);
    /* the dark outline with its tail down to the head, the gold rim, the paper inside */
    badgeTail(r * 0.42f, -r * 0.6f, -r * 1.42f, 0.0f, (GXColor){0x3A, 0x2A, 0x1C, a});
    badgeDisc(0.0f, 0.0f, 0.0f, r, (GXColor){0x3A, 0x2A, 0x1C, a});
    badgeTail(r * 0.3f, -r * 0.7f, -r * 1.25f, 0.05f, (GXColor){0xF2, 0xC2, 0x3C, a});
    badgeDisc(0.0f, 0.0f, 0.05f, r * 0.88f, (GXColor){0xF2, 0xC2, 0x3C, a});
    badgeDisc(0.0f, 0.0f, 0.1f, r * 0.74f, (GXColor){0xFF, 0xF8, 0xE6, a});
    /* the face, its visible pixels centred and filling 80% of the paper, or the game's Star Piece */
    if (face != NULL) {
        float span;
        GXInitTexObj(&obj, (void*)face, (u16)size, (u16)size, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        faceBounds(p, face, size);
        span = (float)(sFaceBox[p][2] > sFaceBox[p][3] ? sFaceBox[p][2] : sFaceBox[p][3]);
        u0 = ((float)sFaceBox[p][0] + (float)sFaceBox[p][2] * 0.5f - span * 0.5f) / (float)size;
        v0 = ((float)sFaceBox[p][1] + (float)sFaceBox[p][3] * 0.5f - span * 0.5f) / (float)size;
        u1 = u0 + span / (float)size;
        v1 = v0 + span / (float)size;
        h = r * 0.74f * 0.8f;
    } else {
        iconGetTexObj(&obj, itemDataTable[ITEM_STAR_PIECE].icon_id);
        h = r * 0.62f;
    }
    GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GXLoadTexObj(&obj, GX_TEXMAP0);
    badgeState(1);
    sBadgesDrawn++;
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    GXPosition3f32(-h, h, 0.15f);
    GXColor4u8(0xFF, 0xFF, 0xFF, a);
    GXTexCoord2f32(u0, v0);
    GXPosition3f32(h, h, 0.15f);
    GXColor4u8(0xFF, 0xFF, 0xFF, a);
    GXTexCoord2f32(u1, v0);
    GXPosition3f32(h, -h, 0.15f);
    GXColor4u8(0xFF, 0xFF, 0xFF, a);
    GXTexCoord2f32(u1, v1);
    GXPosition3f32(-h, -h, 0.15f);
    GXColor4u8(0xFF, 0xFF, 0xFF, a);
    GXTexCoord2f32(u0, v1);
}

/* each field frame, after the bosses' own: each one's badge comes, goes and is queued to draw */
static void badgesFrame(void) {
    static int sLogged[COUNT];
    const int shown = badgesShown();
    int i;
    sBadgeTick++;
    for (i = 0; i < COUNT; i++) {
        const StoryBoss* b = &kBosses[i];
        NpcEntry* npc = gPortPm64FieldLooks[i].pose >= 0 ? npcNameToPtr_NoAssert(npcName(i)) : NULL;
        const int p = npc != NULL ? badgePartner(b) : 0;
        const int want = shown && (p > 0 || (BADGE_STAR && p < 0));
        const int mark = p > 0 ? p : p < 0 ? 100 : 200;
        if (npc == NULL) {
            sBadgeFade[i] = 0;
            sLogged[i] = 0;
            continue;
        }
        sBadgeFade[i] = want ? (sBadgeFade[i] < BADGE_FADE ? sBadgeFade[i] + 1 : BADGE_FADE)
                             : (sBadgeFade[i] > 0 ? sBadgeFade[i] - 1 : 0);
        if (sLogged[i] != mark) { /* (the tests' record: once each time it changes) */
            sLogged[i] = mark;
            printf("[pm64] story boss badge: %s: %s\n", portPm64FightEnglish(b->fight),
                   p > 0 ? portPm64PartnerName(p) : p < 0 ? "frees nobody" : "none");
            fflush(stdout);
        }
        if (sBadgeFade[i] == 0) {
            continue;
        }
        sBadgeAt[i] = npc->position; /* (until its sprite has been drawn: about a head over its feet) */
        sBadgeAt[i].y += 40.0f;
        {
            static int sHeadLogged[COUNT];
            float head[3];
            if (!sHeadLogged[i] && portPm64FieldLookHead(i, head)) {
                sHeadLogged[i] = 1;
                printf("[pm64] story boss badge: %s's head %.0f over its feet\n", portPm64FightEnglish(b->fight),
                       head[1] - npc->position.y);
                fflush(stdout);
            }
        }
        dispEntry(CAMERA_3D, dispCalcZ(&sBadgeAt[i]), 2, badgeDisp, (void*)(intptr_t)i);
    }
}

/* Goombella's Tattle of a story boss: while it frees a partner, the line saying so (npc_tattles_aurora.c's
 * "pc_pm64_free<fight>"); else the NPC's own (none: her line for one she can't place) */
static const char* bossTattle(const char* name) {
    static char sTag[24];
    int i;
    if (strncmp(name, "pc_pm64_sb", 10) != 0) {
        return NULL;
    }
    for (i = 0; i < COUNT; i++) {
        if (strcmp(name, npcName(i)) == 0 && badgePartner(&kBosses[i]) > 0) {
            snprintf(sTag, sizeof(sTag), "pc_pm64_free%d", kBosses[i].fight);
            return sTag;
        }
    }
    return NULL;
}

static void frameHook(void) {
    int i;
    if (sBadgesDrawn != sBadgesDrawnLast) { /* (last frame's: the tests' record of what was on the screen) */
        sBadgesDrawnLast = sBadgesDrawn;
        printf("[pm64] story boss badges drawn: %d%s\n", sBadgesDrawn, gp != NULL && gp->inBattle ? " in battle" : "");
        fflush(stdout);
    }
    sBadgesDrawn = 0;
    if (sFighting >= 0 && !evtCheckID(sFightEvt)) {
        sFighting = -1; /* (its script gone with no end: a Game Over, the title) */
    }
    if (!inField()) {
        for (i = 0; i < COUNT; i++) {
            sAsked[i] = 0;
            sArmed[i] = 0;
            sBadgeFade[i] = 0;
        }
        sCalm = 0;
        holdLooks();
        return;
    }
    /* (the calm after a battle or a map change: a meeting waits for it) */
    sCalm = seqCheckSeq() ? 0 : sCalm < 100000 ? sCalm + 1 : sCalm;
    if (!seqCheckSeq() && portPm64BossesReady()) {
        logRoom();
    }
    for (i = 0; i < COUNT; i++) {
        const StoryBoss* b = &kBosses[i];
        NpcEntry* npc = npcNameToPtr_NoAssert(npcName(i));
        if (strcmp(gp->currentMapName, b->map) != 0) {
            sAsked[i] = 0;
            sArmed[i] = 0;
            sPlChecked[i] = 0;
            gPortPm64FieldLooks[i].pose = -1;
            continue;
        }
        if (npc != NULL) {
            if (!wanted(b) || !portPm64BossFieldLook(portPm64FightBoss(b->fight), 0, &gPortPm64FieldLooks[i])) {
                /* (gone or hidden at the next frame's start, preHook; its draw this frame is queued already:
                   still the boss, or nothing without a ROM, never the Toad) */
                if (portPm64Linked() == PM64_NONE) {
                    gPortPm64FieldLooks[i].sprite = -1;
                }
                gPortPm64FieldLooks[i].pose = npc->poseId;
                continue;
            }
            gPortPm64FieldLooks[i].pose = npc->poseId;
            npc->flags &= ~(NPC_FLAG_HIDDEN | NPC_FLAG_NO_SHADOW);
            bumps(i, npc);
            placementCheck(i, npc);
            continue;
        }
        sPlChecked[i] = 0;
        gPortPm64FieldLooks[i].pose = -1;
        if (!wanted(b)) {
            sAsked[i] = 0; /* (wanted again in the room: it comes) */
            continue;
        }
        if (!sAsked[i] && !seqCheckSeq()) {
            sAsked[i] = 1;
            sArmed[i] = 0; /* (Mario may stand beside it as it comes: back from its fight) */
            spawn(i);
        }
    }
    badgesFrame();
}

void portPm64StoryInit(void) {
    (void)storyOn(); /* (its setting declared) */
    gPortTattleTagHook = bossTattle;
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portModHostAddHook(0, kSlot_marioStMain, (void*)preHook);
}
