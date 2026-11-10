/* Stickers (port/include/port/costume.h, docs/COSTUMES.md): partners' faces,
 * items and enemies, unlocked as the save file meets them (a partner who
 * joined; an item had, a badge found, a dish cooked; an enemy in the Tattle
 * Log), stuck in the world from the Start menu's Wardrobe where Mario stands,
 * facing the camera. Kept per save file in the user folder
 * (Stickers/<game id>/file<n>.txt, beside the achievements' folder), with the
 * items the file has had, as achievements are; drawn in the room they were
 * stuck in, as part of the picture (photos have them). How things look only,
 * but an enemy's sticker is a pose in the game's pose pool and heap. None in
 * replays (the file is this player's alone). Not in the save-state section: a
 * save state loaded doesn't take stickers back off. */
#include "port/file_pc.h"
#include "port/statesync.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/file_write.h"
#include "port/costume.h"
#include "port/practice.h"
#include "port/replay.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/tr.h"
#else
#define portTrGame(english) (english) /* (the headless build has no translations) */
#endif
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "icon_ids.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "win/win_main.h"

/* --- every sticker */

#define ALL_MAX 640
static struct {
    unsigned char kind;
    short id; /* a face's partner (0 Mario), an item, an enemy's unit */
} sAll[ALL_MAX];
static int sAllCount;

static void listAll(void) {
    int id;
    if (sAllCount != 0) {
        return;
    }
    for (id = 0; id < 8; id++) {
        sAll[sAllCount].kind = PORT_STICKER_FACE;
        sAll[sAllCount++].id = (short)id;
    }
    for (id = KEY_ITEM_MIN; id < BADGE_MAX && sAllCount < ALL_MAX; id++) {
        if ((id >= KEY_ITEM_MAX && id < ITEM_MIN) || (id >= ITEM_MAX && id < BADGE_MIN)) {
            continue;
        }
        if (itemDataTable[id].name == NULL || itemDataTable[id].icon_id == 0) {
            continue;
        }
        sAll[sAllCount].kind = PORT_STICKER_ITEM;
        sAll[sAllCount++].id = (short)id;
    }
    for (id = 1; id < UNIT_MAX && sAllCount < ALL_MAX; id++) {
        const TattleEntry* t = battleGetUnitMonosiriPtr(id);
        if (t->tattleLogTag == NULL || t->enemyTag == NULL || t->modelName == NULL || t->poseName == NULL) {
            continue;
        }
        sAll[sAllCount].kind = PORT_STICKER_ENEMY;
        sAll[sAllCount++].id = (short)id;
    }
}

int portStickerCount(void) {
    listAll();
    return sAllCount;
}

int portStickerKind(int i) {
    listAll();
    return i >= 0 && i < sAllCount ? sAll[i].kind : -1;
}

/* the partners' and Mario's faces, by PartyId (statuswindow.c's party_icon) */
static int faceIcon(int party) {
    static const unsigned short kFaces[8] = {ICON_1A3, ICON_1A4, ICON_1A5, ICON_1A6,
                                             ICON_1A7, ICON_1AE, ICON_1AF, ICON_1B0};
    return party >= 0 && party < 8 ? kFaces[party] : -1;
}

const char* portStickerName(int i) {
    static const char* const kNames[8] = {"Mario", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian",
                                          "Ms. Mowz"};
    const char* name = NULL;
    listAll();
    if (i < 0 || i >= sAllCount) {
        return "";
    }
    switch (sAll[i].kind) {
    case PORT_STICKER_FACE:
        return portTrGame(kNames[sAll[i].id]);
    case PORT_STICKER_ITEM:
        name = msgSearch(itemDataTable[sAll[i].id].name);
        break;
    default:
        name = msgSearch(battleGetUnitMonosiriPtr(sAll[i].id)->enemyTag);
        break;
    }
    return name != NULL ? name : "";
}

int portStickerIcon(int i) {
    listAll();
    if (i < 0 || i >= sAllCount) {
        return -1;
    }
    if (sAll[i].kind == PORT_STICKER_FACE) {
        return faceIcon(sAll[i].id);
    }
    return sAll[i].kind == PORT_STICKER_ITEM ? itemDataTable[sAll[i].id].icon_id : -1;
}

void portStickerPoseWant(int i) {
    const TattleEntry* t;
    listAll();
    if (i >= 0 && i < sAllCount && sAll[i].kind == PORT_STICKER_ENEMY &&
        (t = battleGetUnitMonosiriPtr(sAll[i].id)) != NULL) {
        portCosmeticPoseWant(t->modelName, t->poseName);
    }
}

int portStickerPose(int i) {
    const TattleEntry* t;
    AnimationPose* p;
    int pose;
    listAll();
    if (i < 0 || i >= sAllCount || sAll[i].kind != PORT_STICKER_ENEMY) {
        return -1;
    }
    t = battleGetUnitMonosiriPtr(sAll[i].id);
    if ((pose = portCosmeticPose(t->modelName, t->poseName)) < 0) {
        return -1;
    }
    /* drawn as itself (the same pose may be a costume's, with Mario's look) */
    p = animPoseGetAnimPosePtr(pose);
    p->flags &= ~(ANIM_POSE_FLAG_PERA | ANIM_POSE_FLAG_INSTANT_TURN);
    p->materialFlag = 0;
    p->materialLightFlag = 0;
    p->materialColor = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
    p->materialColor2 = (GXColor){0xFF, 0xFF, 0xFF, 0xFF};
    p->gxCallback = NULL;
    p->disableDraw = FALSE;
    return pose;
}

/* --- the file's record */

#define STUCK_MAX 400
#define STUCK_PER_ROOM 24
#define SEEN_BYTES ((BADGE_MAX + 7) / 8)

typedef struct Stuck {
    char map[16];
    unsigned char kind;
    short id;
    float x, y, z, yaw;
} Stuck;

static char sPath[1024];
static char sName[20];
static unsigned char sSeen[SEEN_BYTES];
static Stuck sStuck[STUCK_MAX];
static int sStuckCount;

/* stickers can be drawn and stuck: not in replays */
static int stickersHere(void) {
    return !portReplayActive();
}

static void filePath(char* out, size_t size, const char* from, int file) {
    snprintf(out, size, "%s/Stickers/%s/file%d.txt", from, PORT_GAME_ID, file);
}

static void save(void) {
    FILE* f;
    int i;
    if (sPath[0] == '\0') {
        return;
    }
    portPracticeGameDir("Stickers");
    f = portFileWriteOpen(sPath, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "name=%s\n", sName);
    for (i = 0; i < BADGE_MAX; i++) {
        if (sSeen[i / 8] & (1 << (i % 8))) {
            fprintf(f, "had %d\n", i);
        }
    }
    for (i = 0; i < sStuckCount; i++) {
        const Stuck* s = &sStuck[i];
        fprintf(f, "stick %s %d %d %.2f %.2f %.2f %.1f\n", s->map, s->kind, s->id, s->x, s->y, s->z, s->yaw);
    }
    portFileWriteClose(f, sPath);
}

void portStickersFileLoaded(void) {
    char line[160];
    FILE* f;
    int sameName = 0;
    memset(sSeen, 0, sizeof(sSeen));
    sStuckCount = 0;
    sPath[0] = '\0';
    if (gp == NULL || gp->lastSlot < 0 || gp->lastSlot > 3) {
        return;
    }
    snprintf(sName, sizeof(sName), "%.16s", (const char*)gp->playerName);
    filePath(sPath, sizeof(sPath), portPracticeUserDir(), (int)gp->lastSlot + 1);
    f = portFopen(sPath, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        Stuck s;
        int id, kind;
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "name=", 5) == 0) {
            /* (another file in the slot since: its own record, from nothing) */
            sameName = strcmp(line + 5, sName) == 0;
        } else if (!sameName) {
            break;
        } else if (sscanf(line, "had %d", &id) == 1 && id >= 0 && id < BADGE_MAX) {
            sSeen[id / 8] |= (unsigned char)(1 << (id % 8));
        } else if (sStuckCount < STUCK_MAX &&
                   sscanf(line, "stick %15s %d %d %f %f %f %f", s.map, &kind, &id, &s.x, &s.y, &s.z, &s.yaw) == 7 &&
                   kind >= 0 && kind < PORT_STICKER_KINDS) {
            s.kind = (unsigned char)kind;
            s.id = (short)id;
            sStuck[sStuckCount++] = s;
        }
    }
    fclose(f);
}

/* --- unlocked */

static int seen(int item) {
    return item >= 0 && item < BADGE_MAX && (sSeen[item / 8] & (1 << (item % 8)));
}

int portStickerUnlocked(int i) {
    int id;
    listAll();
    if (i < 0 || i >= sAllCount || gp == NULL) {
        return 0;
    }
    if (getenv("TTYD_STICKERS_ALL") != NULL) {
        return 1; /* (tests, screenshots) */
    }
    if (sPath[0] == '\0') {
        return 0;
    }
    id = sAll[i].id;
    switch (sAll[i].kind) {
    case PORT_STICKER_FACE:
        return id == 0 || (pouchGetPtr()->partyData[id].flags & POUCH_PARTY_FLAG_JOINED);
    case PORT_STICKER_ITEM:
        return seen(id) || (id >= BADGE_MIN && swGet(GSWF_BADGE_FOUND_BASE + id - BADGE_MIN)) ||
               (id >= WIN_RECIPE_MIN && id < ITEM_MAX && swGet(WIN_RECIPE_COOKED_SW(id - WIN_RECIPE_MIN)));
    default:
        return swGet(GSWF_TATTLE_BASE + id);
    }
}

int portStickerUnlockedCount(int kind) {
    int i, n = 0;
    listAll();
    for (i = 0; i < sAllCount; i++) {
        n += sAll[i].kind == kind && portStickerUnlocked(i);
    }
    return n;
}

/* the pouch's items now: noted as had */
static void noteHad(void) {
    PouchData* p = pouchGetPtr();
    int i, added = 0;
#define HAD(item)                                                                                                      \
    do {                                                                                                               \
        const int it_ = (item);                                                                                        \
        if (it_ > 0 && it_ < BADGE_MAX && !seen(it_)) {                                                                \
            sSeen[it_ / 8] |= (unsigned char)(1 << (it_ % 8));                                                         \
            added = 1;                                                                                                 \
        }                                                                                                              \
    } while (0)
    for (i = 0; i < MAX_KEY_ITEMS; i++) {
        HAD(p->keyItems[i]);
    }
    for (i = 0; i < MAX_HELD_ITEMS; i++) {
        HAD(p->heldItems[i]);
    }
    for (i = 0; i < MAX_STORED_ITEMS; i++) {
        HAD(p->storedItems[i]);
    }
    for (i = 0; i < MAX_BADGES; i++) {
        HAD(p->badges[i]);
    }
    if (p->coins > 0) {
        HAD(ITEM_COIN);
    }
    if (p->starPieceCount > 0) {
        HAD(ITEM_STAR_PIECE);
    }
    if (p->shineSpriteCount > 0) {
        HAD(ITEM_SHINE_SPRITE);
    }
#undef HAD
    if (added) {
        save();
    }
}

/* --- the Wardrobe's choice */

static int sKind;
static int sChosen[PORT_STICKER_KINDS] = {-1, -1, -1};

int portStickerKindChosen(void) {
    return sKind;
}

void portStickerStepKind(int dir) {
    sKind = ((sKind + dir) % PORT_STICKER_KINDS + PORT_STICKER_KINDS) % PORT_STICKER_KINDS;
}

int portStickerChosen(void) {
    const int c = sChosen[sKind];
    if (c >= 0 && c < sAllCount && sAll[c].kind == sKind && portStickerUnlocked(c)) {
        return c;
    }
    /* the first unlocked of the kind */
    sChosen[sKind] = -1;
    portStickerStep(1);
    return sChosen[sKind];
}

void portStickerStep(int dir) {
    int i, c = sChosen[sKind];
    listAll();
    if (c < 0) {
        c = dir > 0 ? -1 : sAllCount;
    }
    for (i = 0; i < sAllCount; i++) {
        c = ((c + dir) % sAllCount + sAllCount) % sAllCount;
        if (sAll[c].kind == sKind && portStickerUnlocked(c)) {
            sChosen[sKind] = c;
            return;
        }
    }
    sChosen[sKind] = -1;
}

/* --- stuck in the world */

static int inField(void) {
    return gp != NULL && !gp->inBattle && seqGetSeq() == SEQ_GAME && marioGetPtr() != NULL;
}

static int here(const Stuck* s) {
    return strncmp(s->map, gp->currentMapName, sizeof(s->map) - 1) == 0;
}

int portStickersHere(void) {
    int i, n = 0;
    if (gp == NULL) {
        return 0;
    }
    for (i = 0; i < sStuckCount; i++) {
        n += here(&sStuck[i]);
    }
    return n;
}

/* nearest this room's stuck one to Mario, or -1 */
static int nearest(void) {
    MarioWork* m;
    int i, best = -1;
    float bestD = 0.0f;
    if (gp == NULL || (m = marioGetPtr()) == NULL) {
        return -1;
    }
    for (i = 0; i < sStuckCount; i++) {
        const float dx = sStuck[i].x - m->position.x, dy = sStuck[i].y - m->position.y,
                    dz = sStuck[i].z - m->position.z;
        const float d = dx * dx + dy * dy + dz * dz;
        if (here(&sStuck[i]) && (best < 0 || d < bestD)) {
            best = i;
            bestD = d;
        }
    }
    return best;
}

const char* portStickerWhyNot(int peel) {
    if (!stickersHere()) {
        return "Not in a replay.";
    }
    if (peel) {
        return nearest() < 0 ? "No sticker in this room." : NULL;
    }
    if (portStickerChosen() < 0) {
        return "No sticker of this kind yet.";
    }
    if (!inField()) {
        return "Only in the field, out of battle.";
    }
    if (sPath[0] == '\0') {
        return "Load a save file first.";
    }
    if (portStateSyncCopyHeld()) {
        return "Not in the host's world.";
    }
    if (sStuckCount >= STUCK_MAX) {
        return "No room for more stickers.";
    }
    if (portStickersHere() >= STUCK_PER_ROOM) {
        return "This room is full of stickers.";
    }
    return NULL;
}

int portStickerStick(void) {
    const int c = portStickerChosen();
    MarioWork* m;
    CameraEntry* cam;
    Stuck s;
    float dx, dz, len;
    if (portStickerWhyNot(0) != NULL) {
        return 0;
    }
    m = marioGetPtr();
    cam = camGetPtr(CAMERA_3D);
    memset(&s, 0, sizeof(s));
    snprintf(s.map, sizeof(s.map), "%.15s", gp->currentMapName);
    s.kind = sAll[c].kind;
    s.id = sAll[c].id;
    /* where he stands, a step behind him from the camera, facing it */
    dx = cam->cameraPos.x - m->position.x;
    dz = cam->cameraPos.z - m->position.z;
    len = sqrtf(dx * dx + dz * dz);
    if (len < 1.0f) {
        dx = 0.0f;
        dz = 1.0f;
        len = 1.0f;
    }
    s.x = m->position.x - dx / len * 12.0f;
    s.y = m->position.y;
    s.z = m->position.z - dz / len * 12.0f;
    s.yaw = atan2f(dx, dz) * 57.29578f;
    sStuck[sStuckCount++] = s;
    save();
    return 1;
}

int portStickerPeel(void) {
    const int best = stickersHere() ? nearest() : -1;
    if (best < 0) {
        return 0;
    }
    memmove(&sStuck[best], &sStuck[best + 1], sizeof(Stuck) * (size_t)(sStuckCount - best - 1));
    sStuckCount--;
    save();
    return 1;
}

/* a catalogue entry for a stuck one, or -1 (an item or enemy this version lacks) */
static int entryOf(const Stuck* s) {
    int i;
    listAll();
    for (i = 0; i < sAllCount; i++) {
        if (sAll[i].kind == s->kind && sAll[i].id == s->id) {
            return i;
        }
    }
    return -1;
}

static void stuckDisp(CameraId camId, void* param) {
    const int k = (int)(intptr_t)param;
    const Stuck* s;
    Mtx m, t;
    int e, icon, pose;
    (void)camId;
    if (k < 0 || k >= sStuckCount) {
        return;
    }
    s = &sStuck[k];
    if ((e = entryOf(s)) < 0) {
        return;
    }
    PSMTXTrans(m, s->x, s->y, s->z);
    PSMTXRotRad(t, 'y', s->yaw * 0.017453292f);
    PSMTXConcat(m, t, m);
    if ((icon = portStickerIcon(e)) >= 0) {
        PSMTXScale(t, 1.5f, 1.5f, 1.5f);
        PSMTXConcat(m, t, m);
        iconDispGx2(m, 0, (u16)icon);
    } else if ((pose = portStickerPose(e)) >= 0) {
        animPoseDrawMtx(pose, m, 1, 0.0f, 1.0f);
        animPoseDrawMtx(pose, m, 2, 0.0f, 1.0f);
        animPoseDrawMtx(pose, m, 3, 0.0f, 1.0f);
    }
}

void portStickersFrame(void) {
    static unsigned sLastNote;
    int i;
    if (gp == NULL || sPath[0] == '\0' || seqGetSeq() != SEQ_GAME || portStateSyncCopyHeld()) {
        return; /* (a co-op guest in the host's world: the badges had and rooms are the host's, not this file's) */
    }
    /* In replays none (the stickers are this player's own file, and an enemy's is
     * a pose in the game's pool and heap, portCosmeticPose) */
    if (!stickersHere()) {
        return;
    }
    if (gp->retraceCount - sLastNote >= 30) {
        sLastNote = gp->retraceCount;
        noteHad();
    }
    if (!inField()) {
        return;
    }
    for (i = 0; i < sStuckCount; i++) {
        if (here(&sStuck[i])) {
            const int e = entryOf(&sStuck[i]);
            if (e >= 0) {
                portStickerPoseWant(e); /* (an enemy's: its pose wanted from this frame, not the draw) */
            }
            Vec v = {sStuck[i].x, sStuck[i].y + 10.0f, sStuck[i].z};
            dispEntry(CAMERA_3D, dispCalcZ(&v), 1, stuckDisp, (void*)(intptr_t)i);
        }
    }
}
