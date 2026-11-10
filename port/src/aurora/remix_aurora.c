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
#include "port/file_pc.h"
#include "port/menu_buttons.h"
#include "port/platform.h"
#include "port/playmodes.h"
#include "port/modes.h"
#include "port/mod_host.h"
#include "port/hook_list.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/tr.h"
#include "port/directors_cut.h"
#include "port/raid.h"
#include "port/save_words.h"
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
    if ((portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished) || portRaidActive()) {
        return 0; /* the boss rush's, the Glitz Pit's and the raid's own fights */
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
            printf("[remix] %s: the fight %s\n", gp->currentMapName, table[i].name);
            fflush(stdout);
            break;
        }
    }
    return 0;
}

/* --- the badge draft ------------------------------------------------------ */

#define DRAFT_CHOICES 3
static int sDrafting;    /* the choice is up */

int portRemixDrafting(void) {
    return sDrafting;
}
static int sCardDrafted; /* this visit to the title card had its draft */
static s16 sChoices[DRAFT_CHOICES];
static int sCursor;

static int draftable(int id) {
    return id >= BADGE_MIN && id < BADGE_MAX && itemDataTable[id].bp_cost > 0 && id != ITEM_AUTO_COMMAND_BADGE &&
           id != ITEM_UNUSED_DEFEND_BADGE && id != ITEM_UNUSED_DEFEND_BADGE_P &&
           (!portDirectorsCutBadge(id) || portDirectorsCut()); /* the cut badges: named only with Director's Cut */
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
    printf("[remix] badge draft: %d %d %d (seed %d)\n", sChoices[0], sChoices[1], sChoices[2], *sSeed);
}

/* a scale for text of width at most room at the scale wanted (a long badge name, a language's longer words) */
static float fitScale(float width, float room, float want) {
    return width * want > room && width > 0.0f ? room / width : want;
}

static void draftDisp(CameraId cam, void* param) {
    const float w = 520.0f, x0 = -w / 2, y0 = 110.0f;
    const char* title = portTrGame("Badge draft: choose one for this chapter");
    const char* desc = msgSearch(itemDataTable[sChoices[sCursor]].description);
    u16 descLines = 0;
    const float descWidth = (float)FontGetMessageWidthLine(desc, &descLines);
    /* top to bottom, each row below the last: the title (about 25 tall from y0), the icons standing on one
     * line under it with room for the chosen one's 1.3 scale (icons draw up from their position), the names
     * and costs under the icons, then the description, shrunk to the box's width (a long one, a language's
     * longer words) and growing the box by its lines (28 apart at full size, FontDrawMessage) */
    const float descScale = fitScale(descWidth, w - 40.0f, 1.0f);
    float iconH = 0.0f, rowY, nameY, descTop, need, h;
    float s;
    int i;
    Mtx m, sm;
    for (i = 0; i < DRAFT_CHOICES; i++) {
        u16 iw = 0, ih = 0;
        iconGetWidthHight(&iw, &ih, itemDataTable[sChoices[i]].icon_id);
        if ((float)ih > iconH) {
            iconH = (float)ih;
        }
    }
    rowY = y0 - 32.0f - iconH * 1.3f;
    nameY = rowY - 14.0f;
    descTop = y0 - nameY + 22.0f + 30.0f; /* (the cost line, 22 under the name, and a gap) */
    need = descTop + (float)(descLines + 1) * 28.0f * descScale + 14.0f;
    h = need > 190.0f ? need : 190.0f;
    /* the game's font takes random numbers per letter, and this text is in each
     * player's menu language: the random state as it was (win_root.c) */
    const u32 seed = portRandState();
    (void)cam;
    (void)param;
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xF0}, x0, y0, w, h, 10.0f);
    FontDrawStart();
    FontDrawColor((GXColor){0x00, 0x00, 0x00, 0xFF});
    s = fitScale((float)FontGetMessageWidth(title), w - 30.0f, 0.8f);
    FontDrawScale(s);
    FontDrawString(-(float)FontGetMessageWidth(title) * s * 0.5f, y0 - 10.0f, title);
    for (i = 0; i < DRAFT_CHOICES; i++) {
        const float cx = -170.0f + 170.0f * i;
        const char* name = msgSearch(itemDataTable[sChoices[i]].name);
        char bp[16];
        Vec pos = {cx, rowY, 0.0f};
        iconDispGx(&pos, ICON_DISP_2D, itemDataTable[sChoices[i]].icon_id, i == sCursor ? 1.3f : 1.0f);
        FontDrawStart();
        FontDrawColor(i == sCursor ? (GXColor){0xD0, 0x20, 0x20, 0xFF} : (GXColor){0x40, 0x40, 0x40, 0xFF});
        s = fitScale((float)FontGetMessageWidth(name), 160.0f, 0.7f); /* (in its column, clear of the next) */
        FontDrawScale(s);
        FontDrawString(cx - (float)FontGetMessageWidth(name) * s * 0.5f, nameY, name);
        FontDrawScale(0.7f);
        snprintf(bp, sizeof(bp), "%d BP", (int)itemDataTable[sChoices[i]].bp_cost);
        FontDrawString(cx - (float)FontGetMessageWidth(bp) * 0.35f, nameY - 22.0f, bp);
    }
    FontDrawStart();
    FontDrawColor((GXColor){0x30, 0x30, 0x30, 0xFF});
    /* (FontDrawMessage draws at full size whatever the scale: a matrix shrinks it) */
    PSMTXTrans(m, x0 + 20.0f, y0 - descTop, 0.0f);
    PSMTXScale(sm, descScale, descScale, 1.0f);
    PSMTXConcat(m, sm, m);
    FontDrawMessageMtx(m, desc);
    portRandSetState(seed);
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
    trg = portMenuButtons(0, keyGetButtonTrg(0)); /* (the controller's own confirm: port/include/port/menu_buttons.h) */
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

#define HC_MARK PORT_GSW_HARDCORE_FIRST /* GSW 2040: 0xC5 on a hardcore file; 2041-2042 its id */
_Static_assert(HC_MARK + 2 <= PORT_GSW_HARDCORE_LAST, "the hardcore mark: port/include/port/save_words.h");
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
    f = portFopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    while (!dead && fgets(line, sizeof(line), f) != NULL) {
        /* (a line with no number, blank or damaged, is no file's: atoi made it id 0's) */
        dead = line[0] >= '0' && line[0] <= '9' && atoi(line) == id;
    }
    fclose(f);
    return dead;
}

static void markDead(int id) {
    char path[1024];
    FILE* f;
    deadPath(path, sizeof(path));
    f = portFopen(path, "a");
    if (f != NULL) {
        fprintf(f, "%d %.16s\n", id, (const char*)gp->playerName);
        fclose(f);
    }
}

static void refusedDisp(CameraId cam, void* param) {
    const char* text = portTrGame("This hardcore file fell: it can't be played again.");
    const u32 seed = portRandState(); /* (the font's random numbers, as in draftDisp) */
    (void)cam;
    (void)param;
    windowDispGX_Waku_col(0, (GXColor){0xFF, 0xFF, 0xFF, 0xF0}, -260.0f, 40.0f, 520.0f, 50.0f, 10.0f);
    FontDrawStart();
    {
        /* (fitted to the box: the line is longer than it at 0.8, and longer still in some languages) */
        const float s = fitScale((float)FontGetMessageWidth(text), 500.0f, 0.8f);
        FontDrawScale(s);
        FontDrawColor((GXColor){0x00, 0x00, 0x00, 0xFF});
        FontDrawString(-(float)FontGetMessageWidth(text) * s * 0.5f, 25.0f, text);
    }
    portRandSetState(seed);
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
            const u32 id = (u32)portLockstepClock() & 0xFFFF; /* (read without moving the clock) */
            swByteSet(HC_MARK, HC_MAGIC);
            swByteSet(HC_MARK + 1, (s32)(id >> 8));
            swByteSet(HC_MARK + 2, (s32)(id & 0xFF));
            *sHardcoreNext = 0;
            portSettingsSave();
            printf("[remix] hardcore file %u\n", (unsigned)id);
            fflush(stdout);
        }
    }
    {
        /* a fallen file refused however it came into play: the file select's load, and also the launcher's
           Continue, the save library and quick resume (portContinueGame: straight to the field, never through
           SEQ_LOAD, so a check made only as SEQ_LOAD ended let a fallen file play on). Checked once per file
           in play; the title, a load or a Game Over checks again. */
        static int sCheckedId = -1;
        if (seq == SEQ_TITLE || seq == SEQ_LOAD || seq == SEQ_GAME_OVER) {
            sCheckedId = -1;
        } else if (seq == SEQ_GAME && gp != NULL && hardcoreId() >= 0 && hardcoreId() != sCheckedId) {
            sCheckedId = hardcoreId();
            if (!portReplayActive() && isDead(hardcoreId())) {
                /* (the fallen files' list is this player's own: not online, and neither
                 * when recording nor playing, so a replay starts as its recording did) */
                printf("[remix] hardcore file %d fell before: back to the title\n", hardcoreId());
                fflush(stdout);
                sRefused = 300;
                seqSetSeq(SEQ_TITLE, NULL, NULL);
            }
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
