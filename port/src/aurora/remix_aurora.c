/* Remix modes (port/include/port/playmodes.h), each off by default:
 *
 * - remix_enemies: the fight behind each field enemy is drawn from all of
 *   its area's ordinary fights (npcSetupBattleInfo's setup swapped), the
 *   same each time for a room and a seed (remix_seed). Bosses and scripted
 *   fights (no escape, losing allowed, no Jump or Hammer...) and the Glitz
 *   Pit's and the Pit of 100 Trials' are left as they are.
 * - remix_badge_draft: at each chapter's title card, three badges to choose
 *   one from (the card waits for A anyway; that A takes the badge).
 * - hardcore: a file started with "hardcore_next" set is marked (GSW
 *   2040-2042) and has one life: no Retry on Game Over, no save states, and
 *   a Game Over ends it for good (its id in <user>/hardcore.txt; loading it
 *   again goes back to the title). A run recorded as a replay is saved at
 *   the Game Over, as a ghost to race (docs/REPLAYS.md). */
#include "port/playmodes.h"
#include "port/modes.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "data/item_data.h"
#include "data/mapdata.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/icondrv.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "driver/windowdrv.h"
#include "evt/evt_story.h"
#include "manager/fontmgr.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static int* sEnemies;
static int* sSeed;
static int* sDraft;
static int* sHardcoreNext;

void portRemixDeclare(void) {
    if (sEnemies != NULL) {
        return;
    }
    sEnemies = portSettingsDeclare("remix_enemies", 0, 0, 1);
    sSeed = portSettingsDeclare("remix_seed", 1, 0, 99999);
    sDraft = portSettingsDeclare("remix_badge_draft", 0, 0, 1);
    sHardcoreNext = portSettingsDeclare("hardcore_next", 0, 0, 1);
}

static u32 hashStr(u32 h, const char* s) {
    while (s != NULL && *s != '\0') {
        h = (h ^ (u8)*s++) * 16777619u;
    }
    return h;
}

static u32 hashInt(u32 h, u32 v) {
    int i;
    for (i = 0; i < 4; i++) {
        h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    return h;
}

/* --- enemies ------------------------------------------------------------- */

#define SCRIPTED                                                                                                   \
    (NPC_BATTLE_NO_ESCAPE | NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_PLAYER_ENTRY | NPC_BATTLE_NO_JUMP |                \
     NPC_BATTLE_NO_HAMMER | NPC_BATTLE_NO_PARTY_CHANGE)

static int ordinary(const NpcSetupBattleInfo* s) {
    return s->name != NULL && s->select != NULL && s->select->weight > 0 && s->randomMode == 0 &&
           (s->battleFlags & SCRIPTED) == 0;
}

static int setupPre(NpcEntry** npc, NpcSetupBattleInfo** setup) {
    char area[4];
    NpcSetupBattleInfo* table;
    int count = 0, picks = 0, i, pick;
    portRemixDeclare();
    if (!*sEnemies || *setup == NULL || !ordinary(*setup) || gp == NULL) {
        return 0;
    }
    if (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished) {
        return 0; /* the boss rush's and the Glitz Pit's own fights */
    }
    memcpy(area, gp->currentMapName, 3);
    area[3] = '\0';
    if (strcmp(area, "tou") == 0 || strcmp(area, "jon") == 0) {
        return 0;
    }
    table = areaDataPtr(area) != NULL ? (NpcSetupBattleInfo*)areaDataPtr(area)->btlSetupTbl : NULL;
    if (table == NULL) {
        return 0;
    }
    for (count = 0; table[count].name != NULL && count < 512; count++) {
        picks += ordinary(&table[count]);
    }
    if (picks < 2) {
        return 0;
    }
    pick = (int)(hashStr(hashStr(hashInt(2166136261u, (u32)*sSeed), gp->currentMapName),
                         *npc != NULL ? (*npc)->description : (*setup)->name) %
                 (u32)picks);
    for (i = 0; i < count; i++) {
        if (ordinary(&table[i]) && pick-- == 0) {
            *setup = &table[i];
            break;
        }
    }
    return 0;
}

/* --- the badge draft ------------------------------------------------------ */

#define DRAFT_CHOICES 3
static int sDrafting;    /* the choice is up */
static int sCardDrafted; /* this visit to the title card had its draft */
static s16 sChoices[DRAFT_CHOICES];
static int sCursor;

static int draftable(int id) {
    return id >= BADGE_MIN && id < BADGE_MAX && itemDataTable[id].bp_cost > 0 && id != ITEM_AUTO_COMMAND_BADGE &&
           id != ITEM_UNUSED_DEFEND_BADGE && id != ITEM_UNUSED_DEFEND_BADGE_P;
}

static void makeDraft(void) {
    const int chapter = swByteGet(GSW_STORY_PROGRESS);
    u32 h = hashInt(hashStr(hashInt(2166136261u, (u32)*sSeed), (const char*)gp->playerName), (u32)chapter);
    int n = 0, i;
    while (n < DRAFT_CHOICES) {
        int id, dup = 0;
        h = hashInt(h, (u32)n * 7919u + 1u);
        id = BADGE_MIN + (int)(h % (u32)(BADGE_MAX - BADGE_MIN));
        if (!draftable(id)) {
            continue;
        }
        for (i = 0; i < n; i++) {
            dup |= sChoices[i] == id;
        }
        if (!dup) {
            sChoices[n++] = (s16)id;
        }
    }
    sCursor = 1;
    sDrafting = 1;
}

static void draftDisp(CameraId cam, void* param) {
    const float w = 520.0f, h = 190.0f, x0 = -w / 2, y0 = 110.0f;
    const char* title = portTrGame("Badge draft: choose one for this chapter");
    const char* desc;
    int i;
    (void)cam;
    (void)param;
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xF0}, x0, y0, w, h, 10.0f);
    FontDrawStart();
    FontDrawColor((GXColor){0x00, 0x00, 0x00, 0xFF});
    FontDrawScale(0.8f);
    FontDrawString(-(float)FontGetMessageWidth(title) * 0.4f, y0 - 10.0f, title);
    for (i = 0; i < DRAFT_CHOICES; i++) {
        const float cx = -170.0f + 170.0f * i;
        const char* name = msgSearch(itemDataTable[sChoices[i]].name);
        char bp[16];
        Vec pos = {cx, y0 - 70.0f, 0.0f};
        iconDispGx(&pos, ICON_DISP_2D, itemDataTable[sChoices[i]].icon_id, i == sCursor ? 1.3f : 1.0f);
        FontDrawStart();
        FontDrawScale(0.7f);
        FontDrawColor(i == sCursor ? (GXColor){0xD0, 0x20, 0x20, 0xFF} : (GXColor){0x40, 0x40, 0x40, 0xFF});
        FontDrawString(cx - (float)FontGetMessageWidth(name) * 0.35f, y0 - 100.0f, name);
        snprintf(bp, sizeof(bp), "%d BP", (int)itemDataTable[sChoices[i]].bp_cost);
        FontDrawString(cx - (float)FontGetMessageWidth(bp) * 0.35f, y0 - 122.0f, bp);
    }
    desc = msgSearch(itemDataTable[sChoices[sCursor]].description);
    FontDrawStart();
    FontDrawScale(0.6f);
    FontDrawColor((GXColor){0x30, 0x30, 0x30, 0xFF});
    FontDrawMessage((s32)(x0 + 20.0f), (s32)(y0 - 145.0f), desc);
}

/* The draft shows over the chapter's title card (sys_01), which waits for A:
 * left and right choose, and the A that takes the badge also lets the card go
 * on. */
static void draftFrame(void) {
    u32 trg;
    if (gp == NULL || strcmp(gp->currentMapName, "sys_01") != 0 || seqGetSeq() != SEQ_GAME) {
        if (gp == NULL || strcmp(gp->currentMapName, "sys_01") != 0) {
            sCardDrafted = 0;
            sDrafting = 0;
        }
        return;
    }
    if (!sCardDrafted && *sDraft && portModeStatus()->mode == PORT_MODE_NONE) {
        sCardDrafted = 1;
        makeDraft();
    }
    if (!sDrafting) {
        return;
    }
    trg = keyGetButtonTrg(0);
    if (trg & PAD_BUTTON_LEFT) {
        sCursor = (sCursor + DRAFT_CHOICES - 1) % DRAFT_CHOICES;
    } else if (trg & PAD_BUTTON_RIGHT) {
        sCursor = (sCursor + 1) % DRAFT_CHOICES;
    } else if (trg & PAD_BUTTON_A) {
        pouchGetItem((ItemType)sChoices[sCursor]);
        sDrafting = 0;
        return;
    }
    dispEntry(CAMERA_2D, 900.0f, 1, draftDisp, NULL);
}

/* --- hardcore --------------------------------------------------------------- */

#define HC_MARK 2040 /* GSW 2040: 0xC5 on a hardcore file; 2041-2042 its id */
#define HC_MAGIC 0xC5
static int sRefused;  /* frames the "this file has fallen" notice shows */
static int sDeadMarked;

static int hardcoreId(void) {
    return gp != NULL && swByteGet(HC_MARK) == HC_MAGIC ? (swByteGet(HC_MARK + 1) << 8) | swByteGet(HC_MARK + 2) : -1;
}

int portHardcoreActive(void) {
    const SequenceType seq = seqGetSeq();
    return seq != SEQ_TITLE && seq != SEQ_LOGO && seq != SEQ_LOAD && hardcoreId() >= 0;
}

static void deadPath(char* out, size_t size) {
    snprintf(out, size, "%s/hardcore.txt", portSettingsUserPath());
}

static int isDead(int id) {
    char path[1024], line[64];
    FILE* f;
    int dead = 0;
    deadPath(path, sizeof(path));
    f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    while (!dead && fgets(line, sizeof(line), f) != NULL) {
        dead = atoi(line) == id;
    }
    fclose(f);
    return dead;
}

static void markDead(int id) {
    char path[1024];
    FILE* f;
    deadPath(path, sizeof(path));
    f = fopen(path, "a");
    if (f != NULL) {
        fprintf(f, "%d %.16s\n", id, (const char*)gp->playerName);
        fclose(f);
    }
}

static void refusedDisp(CameraId cam, void* param) {
    const char* text = portTrGame("This hardcore file fell: it can't be played again.");
    (void)cam;
    (void)param;
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xF0}, -260.0f, 40.0f, 520.0f, 50.0f, 10.0f);
    FontDrawStart();
    FontDrawScale(0.8f);
    FontDrawColor((GXColor){0x00, 0x00, 0x00, 0xFF});
    FontDrawString(-(float)FontGetMessageWidth(text) * 0.4f, 25.0f, text);
}

static void hardcoreFrame(void) {
    static SequenceType sPrevSeq = SEQ_UNDEFINED;
    const SequenceType seq = seqGetSeq();
    if (sRefused > 0) {
        sRefused--;
        dispEntry(CAMERA_2D, 900.0f, 1, refusedDisp, NULL);
    }
    if (sPrevSeq == SEQ_LOAD && seq != SEQ_LOAD && seq != SEQ_TITLE && gp != NULL) {
        sDeadMarked = 0;
        if (swByteGet(GSW_STORY_PROGRESS) == 0 && *sHardcoreNext && hardcoreId() < 0) {
            /* a new file, with hardcore asked for: marked one-life (the game's
             * virtual clock: the same in the run's replay) */
            const u32 id = (u32)OSGetTime() & 0xFFFF;
            swByteSet(HC_MARK, HC_MAGIC);
            swByteSet(HC_MARK + 1, (s32)(id >> 8));
            swByteSet(HC_MARK + 2, (s32)(id & 0xFF));
            *sHardcoreNext = 0;
            portSettingsSave();
            printf("[remix] hardcore file %u\n", (unsigned)id);
            fflush(stdout);
        } else if (hardcoreId() >= 0 && isDead(hardcoreId()) && !portReplayPlaying()) {
            printf("[remix] hardcore file %d fell before: back to the title\n", hardcoreId());
            fflush(stdout);
            sRefused = 300;
            seqSetSeq(SEQ_TITLE, NULL, NULL);
        }
    }
    if (seq == SEQ_GAME_OVER && sPrevSeq != SEQ_GAME_OVER && hardcoreId() >= 0 && !sDeadMarked) {
        sDeadMarked = 1;
        printf("[remix] hardcore file %d fell\n", hardcoreId());
        fflush(stdout);
        if (!portReplayPlaying()) {
            markDead(hardcoreId());
            portReplaySaveIfRecording(); /* the run, as a ghost to race */
        }
    }
    sPrevSeq = seq;
}

void portRemixFrame(void) {
    portRemixDeclare();
    draftFrame();
    hardcoreFrame();
}

void portRemixInit(void) {
    portRemixDeclare();
    portModHostAddHook(0, kSlot_npcSetupBattleInfo, (void*)setupPre);
}
