/* Enhancements, the second batch (port/include/port/gameplus.h): badge
 * loadouts, storage at every Save Block, the walk-speed boost, coin and EXP
 * multipliers, the Star Piece and Shine Sprite tracker, faster text outside
 * battle, the jukebox and the shops' junk-item helper. Every one off by
 * default. What a file keeps of them (loadouts, tracks heard) is in a text
 * file beside the save, never in it. */
#include "port/file_pc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/file_write.h"
#include "port/gameplus.h"
#include "port/qol.h"
#include "port/settings.h"
#include "port/practice.h"
#include "port/tr.h"
#include "version.h"
#include "system.h"
#include "pmario_sound.h"
#include "driver/seqdrv.h"
#include "evt/evt_story.h"
#include "driver/swdrv.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "data/item_data.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "evt/evt_cmd.h"

/* the second batch's settings: key, default, range */
static const struct {
    const char* key;
    int def, min, max;
} kSettings2[] = {
    {"badge_loadouts", 0, 0, 1},
    {"storage_anywhere", 0, 0, 1},
    {"walk_boost", 0, 0, 1},
    {"walk_boost_speed", 150, 110, 200},
    {"walk_boost_key", 0, 0, 3},
    {"walk_boost_pad", 0, 0, 3},
    {"coin_multiplier", 0, 0, 4},
    {"exp_multiplier", 0, 0, 4},
    {"collectible_tracker", 0, 0, 4},
    {"fast_field_text", 0, 0, 1},
    {"jukebox", 0, 0, 1},
    {"sell_helper", 0, 0, 1},
};

void portGameplus2Declare(void) {
    static int declared;
    size_t i;
    if (declared) {
        return;
    }
    declared = 1;
    for (i = 0; i < sizeof(kSettings2) / sizeof(kSettings2[0]); i++) {
        portSettingsDeclare(kSettings2[i].key, kSettings2[i].def, kSettings2[i].min, kSettings2[i].max);
    }
}

/* a game is being played (its pouch and flags are the file's) */
static int playing(void) {
    const s32 seq = (s32)seqGetSeq();
    return (seq == SEQ_GAME || seq == SEQ_BATTLE || seq == SEQ_MAP_CHANGE) && pouchGetPtr() != NULL;
}

/* --- What a file keeps beside its save --- */

#define BGM_WORDS ((0x106 + 31) / 32)
#define LOADOUT_MAX 64

typedef struct Loadout {
    char name[PORT_LOADOUT_NAME];
    int count;
    s16 badges[LOADOUT_MAX];
} Loadout;

static struct {
    int slot; /* the save slot loaded, -1 none */
    u32 heard[BGM_WORDS];
    Loadout loadouts[PORT_LOADOUT_COUNT];
    u32 seen[2]; /* the scenes seen on this file (portSceneSeen, qol_pc.c) */
    char fortunes[PORT_FORTUNE_COUNT][24]; /* Merluvlee's last readings, newest first (portFortuneRecord) */
    int stock; /* the shop stock last told of by the restock notice (portFileStock, qol_pc.c) */
    int tattles; /* Goombella's field Tattles heard on this file: their messages' hashes (portTattleHeard) */
    u32 tattle[PORT_TATTLE_MAX];
} sFile = {-1};

static void filePath(char* out, size_t size, int slot) {
#if VERSION == VERSION_JP
    const char* v = "jp";
#elif VERSION == VERSION_EU
    const char* v = "eu";
#else
    const char* v = "us";
#endif
    snprintf(out, size, "%s/gameplus_%s_file%d.txt", portPracticeUserDir(), v, slot + 1);
}

static void fileWrite(void) {
    char path[512];
    FILE* f;
    int i, j;
    if (sFile.slot < 0) {
        return;
    }
    filePath(path, sizeof(path), sFile.slot);
    f = portFileWriteOpen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "heard=");
    for (i = 0; i < BGM_WORDS; i++) {
        fprintf(f, "%s%08x", i ? "," : "", (unsigned)sFile.heard[i]);
    }
    fprintf(f, "\n");
    fprintf(f, "seen=%08x,%08x\n", (unsigned)sFile.seen[0], (unsigned)sFile.seen[1]);
    fprintf(f, "stock=%d\n", sFile.stock);
    fprintf(f, "tattle=");
    for (i = 0; i < sFile.tattles; i++) {
        fprintf(f, "%s%08x", i ? "," : "", (unsigned)sFile.tattle[i]);
    }
    fprintf(f, "\n");
    for (i = 0; i < PORT_FORTUNE_COUNT; i++) {
        if (sFile.fortunes[i][0] != '\0') {
            fprintf(f, "fortune%d=%s\n", i, sFile.fortunes[i]);
        }
    }
    for (i = 0; i < PORT_LOADOUT_COUNT; i++) {
        const Loadout* l = &sFile.loadouts[i];
        fprintf(f, "loadout%d=%s\nbadges%d=", i, l->name, i);
        for (j = 0; j < l->count; j++) {
            fprintf(f, "%s%d", j ? "," : "", l->badges[j]);
        }
        fprintf(f, "\n");
    }
    portFileWriteClose(f, path);
}

/* reads what slot's file keeps (nothing when there is no file yet) */
static void fileRead(int slot) {
    char path[512], line[PORT_TATTLE_MAX * 9 + 64];
    FILE* f;
    memset(&sFile, 0, sizeof(sFile));
    sFile.slot = slot;
    filePath(path, sizeof(path), slot);
    f = portFopen(path, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char* eq = strchr(line, '=');
        char* v;
        int i;
        line[strcspn(line, "\r\n")] = '\0';
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        v = eq + 1;
        if (!strcmp(line, "heard")) {
            for (i = 0; i < BGM_WORDS && *v != '\0'; i++) {
                sFile.heard[i] = (u32)strtoul(v, &v, 16);
                v += *v == ',';
            }
        } else if (!strcmp(line, "seen")) {
            for (i = 0; i < 2 && *v != '\0'; i++) {
                sFile.seen[i] = (u32)strtoul(v, &v, 16);
                v += *v == ',';
            }
        } else if (!strcmp(line, "stock")) {
            sFile.stock = atoi(v);
        } else if (!strcmp(line, "tattle")) {
            while (*v != '\0' && sFile.tattles < PORT_TATTLE_MAX) {
                char* end;
                const u32 t = (u32)strtoul(v, &end, 16);
                if (end == v) {
                    v++; /* (not a number: skipped, not kept as a 0) */
                    continue;
                }
                v = end;
                sFile.tattle[sFile.tattles++] = t;
                v += *v == ',';
            }
        } else if (!strncmp(line, "fortune", 7)) {
            i = atoi(line + 7);
            if (i >= 0 && i < PORT_FORTUNE_COUNT) {
                snprintf(sFile.fortunes[i], sizeof(sFile.fortunes[i]), "%s", v);
            }
        } else if (!strncmp(line, "loadout", 7)) {
            i = atoi(line + 7);
            if (i >= 0 && i < PORT_LOADOUT_COUNT) {
                snprintf(sFile.loadouts[i].name, PORT_LOADOUT_NAME, "%s", v);
            }
        } else if (!strncmp(line, "badges", 6)) {
            Loadout* l;
            i = atoi(line + 6);
            if (i < 0 || i >= PORT_LOADOUT_COUNT) {
                continue;
            }
            l = &sFile.loadouts[i];
            while (*v != '\0' && l->count < LOADOUT_MAX) {
                char* end;
                const long id = strtol(v, &end, 10);
                if (end == v) {
                    v++; /* (not a number: a space, a stray letter in a hand-edited file) skipped */
                    continue;
                }
                v = end;
                if (id >= BADGE_MIN && id < BADGE_MAX) {
                    l->badges[l->count++] = (s16)id;
                }
                v += *v == ',';
            }
        }
    }
    fclose(f);
}

/* the file in the slot being played, read once per slot; FALSE when no game runs */
static int fileLoad(void) {
    if (!playing()) {
        return 0;
    }
    if (sFile.slot != gp->saveSlot) {
        fileRead(gp->saveSlot);
    }
    return 1;
}

void portGameplus2NewFile(void) {
    char path[512];
    FILE* f;
    filePath(path, sizeof(path), gp->saveSlot);
    f = portFopen(path, "r");
    if (f == NULL) {
        return; /* nothing kept for this slot */
    }
    fclose(f);
    fileRead(gp->saveSlot);
    memset(sFile.heard, 0, sizeof(sFile.heard)); /* the loadouts stay: they are the player's */
    memset(sFile.seen, 0, sizeof(sFile.seen));
    fileWrite();
}

/* --- Scenes seen on this file (Skip repeated scenes, qol_pc.c) --- */

int portSceneSeen(int id, int mark) {
    int was;
    if (id < 0 || id >= 64) {
        return 0;
    }
    if (mark) {
        portSceneSeenEver(id, 1); /* (every mark also counts for the whole device) */
    }
    if (!fileLoad()) {
        return 0;
    }
    was = (sFile.seen[id / 32] >> (id % 32)) & 1;
    if (mark && !was) {
        sFile.seen[id / 32] |= 1u << (id % 32);
        fileWrite();
    }
    return was;
}

/* --- Merluvlee's readings on this file (Notes, qol_pc.c) --- */

void portFortuneRecord(const char* label) {
    int i;
    if (label == NULL || label[0] == '\0' || strlen(label) >= sizeof(sFile.fortunes[0]) || !fileLoad()) {
        return;
    }
    for (i = 0; i < PORT_FORTUNE_COUNT - 1 && strcmp(sFile.fortunes[i], label) != 0; i++) {
    }
    memmove(sFile.fortunes[1], sFile.fortunes[0], (size_t)i * sizeof(sFile.fortunes[0]));
    snprintf(sFile.fortunes[0], sizeof(sFile.fortunes[0]), "%s", label);
    fileWrite();
}

int portTattleHeard(const char* tag, int mark) {
    u32 h = 2166136261u;
    const char* p;
    int i;
    if (tag == NULL || tag[0] == '\0' || !fileLoad()) {
        return 1;
    }
    for (p = tag; *p != '\0'; p++) {
        h = (h ^ (u8)*p) * 16777619u; /* FNV-1a */
    }
    for (i = 0; i < sFile.tattles; i++) {
        if (sFile.tattle[i] == h) {
            return 1;
        }
    }
    if (mark && sFile.tattles < PORT_TATTLE_MAX) {
        sFile.tattle[sFile.tattles++] = h;
        fileWrite();
    }
    return 0;
}

/* whether msgSearch has the tag, not logging it as missing (driver/msgdrv.h) */
int portMsgHas(const char* tag);

const char* (*gPortTattleTagHook)(const char* name);

const char* portTattleTag(const char* name) {
    const char* own = gPortTattleTagHook != NULL && name != NULL ? gPortTattleTagHook(name) : NULL;
    if (own != NULL && portMsgHas(own)) {
        return own;
    }
    if (name == NULL || name[0] == 0 || !portMsgHas(name)) {
        return "pc_tattle_npc";
    }
    return name;
}

int portFileStock(int set, int value) {
    if (!fileLoad()) {
        return -1;
    }
    if (set && sFile.stock != value) {
        sFile.stock = value;
        fileWrite();
    }
    return sFile.stock;
}

int portFortuneGet(int i, char* label, int size) {
    if (i < 0 || i >= PORT_FORTUNE_COUNT || size <= 0 || !fileLoad() || sFile.fortunes[i][0] == '\0') {
        return 0;
    }
    snprintf(label, (size_t)size, "%s", sFile.fortunes[i]);
    return 1;
}

/* the scenes seen on any file of this device (a replay's story scenes), read once */
static u32 sEver[2];
static int sEverRead;

static void everPath(char* out, size_t size) {
#if VERSION == VERSION_JP
    const char* v = "jp";
#elif VERSION == VERSION_EU
    const char* v = "eu";
#else
    const char* v = "us";
#endif
    snprintf(out, size, "%s/gameplus_%s_seen.txt", portPracticeUserDir(), v);
}

int portSceneSeenEver(int id, int mark) {
    char path[512];
    FILE* f;
    int was;
    if (id < 0 || id >= 64) {
        return 0;
    }
    if (!sEverRead) {
        unsigned a = 0, b = 0;
        sEverRead = 1;
        everPath(path, sizeof(path));
        f = portFopen(path, "r");
        if (f != NULL) {
            if (fscanf(f, "seen=%x,%x", &a, &b) >= 1) {
                sEver[0] = a;
                sEver[1] = b;
            }
            fclose(f);
        }
    }
    was = (sEver[id / 32] >> (id % 32)) & 1;
    if (mark && !was) {
        sEver[id / 32] |= 1u << (id % 32);
        everPath(path, sizeof(path));
        f = portFileWriteOpen(path, "w");
        if (f != NULL) {
            fprintf(f, "seen=%08x,%08x\n", (unsigned)sEver[0], (unsigned)sEver[1]);
            portFileWriteClose(f, path);
        }
    }
    return was;
}

/* --- Badge loadouts --- */

int portLoadoutGet(int i, char* name, int nameSize) {
    if (i < 0 || i >= PORT_LOADOUT_COUNT || !fileLoad()) {
        return -1;
    }
    if (name != NULL) {
        snprintf(name, nameSize, "%s", sFile.loadouts[i].name);
    }
    return sFile.loadouts[i].count;
}

void portLoadoutRename(int i, const char* name) {
    char* n;
    if (i < 0 || i >= PORT_LOADOUT_COUNT || !fileLoad()) {
        return;
    }
    n = sFile.loadouts[i].name;
    snprintf(n, PORT_LOADOUT_NAME, "%s", name);
    n[strcspn(n, "\r\n")] = '\0';
    fileWrite();
}

int portLoadoutStore(int i) {
    PouchData* pouch = pouchGetPtr();
    Loadout* l;
    int k;
    if (i < 0 || i >= PORT_LOADOUT_COUNT || !fileLoad()) {
        return 0;
    }
    l = &sFile.loadouts[i];
    l->count = 0;
    for (k = 0; k < MAX_BADGES && l->count < LOADOUT_MAX; k++) {
        if (pouch->equippedBadges[k] != 0) {
            l->badges[l->count++] = pouch->equippedBadges[k];
        }
    }
    if (l->name[0] == '\0') {
        snprintf(l->name, PORT_LOADOUT_NAME, "Loadout %d", i + 1);
    }
    fileWrite();
    return l->count;
}

static int sWearPending = -1; /* a loadout asked for in the Start menu, worn once it is closed */

void portSaveStateNotice(const char* text); /* a notice over the picture (savestate_aurora.c) */

int portLoadoutWear(int i, char* msg, int msgSize);

void portLoadoutFrame(void) {
    char msg[128];
    const int i = sWearPending;
    if (i < 0) {
        return;
    }
    if (seqGetSeq() != SEQ_GAME) {
        sWearPending = -1; /* (a battle or the title came first: dropped) */
        return;
    }
    if (marioStGetSystemLevel() != 0) {
        return;
    }
    sWearPending = -1;
    portLoadoutWear(i, msg, sizeof(msg));
    portSaveStateNotice(msg);
}

int portLoadoutWear(int i, char* msg, int msgSize) {
    PouchData* pouch = pouchGetPtr();
    s16 worn[MAX_BADGES];
    const Loadout* l;
    int k, j, cost = 0, found = 0;
    if (i < 0 || i >= PORT_LOADOUT_COUNT || !fileLoad()) {
        snprintf(msg, msgSize, "No game is being played.");
        return 0;
    }
    if (seqGetSeq() == SEQ_GAME && marioStGetSystemLevel() != 0 && sFile.loadouts[i].count > 0) {
        /* the Start menu is open (or a scene runs): worn once the game is free again (portLoadoutFrame) */
        sWearPending = i;
        snprintf(msg, msgSize, "Worn once the menu is closed.");
        return 1;
    }
    if (seqGetSeq() != SEQ_GAME || marioStGetSystemLevel() != 0) {
        snprintf(msg, msgSize, "In the field only, with the game's menus closed.");
        return 0;
    }
    l = &sFile.loadouts[i];
    if (l->count == 0) {
        snprintf(msg, msgSize, "This loadout is empty: store the badges worn now first.");
        return 0;
    }
    /* each badge of the loadout takes the first copy in the bag not taken yet */
    memset(worn, 0, sizeof(worn));
    for (j = 0; j < l->count; j++) {
        for (k = 0; k < MAX_BADGES; k++) {
            if (pouch->badges[k] == l->badges[j] && worn[k] == 0) {
                worn[k] = pouch->badges[k];
                cost += itemDataTable[worn[k]].bp_cost;
                found++;
                break;
            }
        }
    }
    if (cost > pouch->maximumBP) {
        snprintf(msg, msgSize, "Needs %d BP; Mario has %d. Nothing changed.", cost, pouch->maximumBP);
        return 0;
    }
    memcpy(pouch->equippedBadges, worn, sizeof(worn));
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    if (found < l->count) {
        snprintf(msg, msgSize, "Wearing %d of %d (%d not in the bag), %d BP.", found, l->count, l->count - found, cost);
    } else {
        snprintf(msg, msgSize, "Wearing %s: %d badges, %d BP.", l->name, found, cost);
    }
    printf("[gameplus] loadout %d worn: %d badges, %d BP\n", i + 1, found, cost);
    fflush(stdout);
    return 1;
}

/* --- Storage at every Save Block --- */

int portEvtStorageOn(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    event->lwData[0] = portGameplus("storage_anywhere");
    return EVT_RETURN_DONE;
}

int portEvtStorageCounts(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    event->lwData[0] = pouchGetHaveItemCnt();
    event->lwData[1] = pouchGetEmptyKeepItemCnt();
    event->lwData[2] = pouchGetKeepItemCnt();
    event->lwData[3] = pouchGetEmptyHaveItemCnt();
    return EVT_RETURN_DONE;
}

int portEvtStorageText(struct EventEntry* event, int isFirstCall) {
    /* the English (the key in port/assets/lang/<code>/save-block-storage.txt) and the tag after it */
    static const char* const kText[][2] = {
        {"What would you like to do?", "<o>"}, {"You have no items to store.", "<k>"},
        {"The storage is full.", "<k>"},       {"Nothing is stored.", "<k>"},
        {"Your bag is full.", "<k>"},
        {"Sell from your bag or from storage?", "<o>"}, /* (5: PORT_STORAGE_TEXT_SELL_ASK) */
    };
    static char sLine[sizeof(kText) / sizeof(kText[0])][256];
    static char sSelect[512];
    const EvtWord* args = event->args;
    const int which = (int)evtGetValue(event, args[0]);
    size_t n;
    (void)isFirstCall;
    if (which == PORT_STORAGE_TEXT_SELECT) {
        n = (size_t)snprintf(sSelect, sizeof(sSelect), "<select 0 3 260 140>\n%s", portTrGame("Save"));
        n += (size_t)snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Store items"));
        n += (size_t)snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Take out items"));
        snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Leave"));
        evtSetValue(event, args[1], (EvtWord)sSelect);
    } else if (which == PORT_STORAGE_TEXT_SELECT_SHOP) { /* the Howz of Badges' box (gor_01.c) */
        n = (size_t)snprintf(sSelect, sizeof(sSelect), "<select 0 3 260 140>\n%s", portTrGame("Shop"));
        n += (size_t)snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Store items"));
        n += (size_t)snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Take out items"));
        snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Leave"));
        evtSetValue(event, args[1], (EvtWord)sSelect);
    } else if (which == PORT_STORAGE_TEXT_SELL_FROM) { /* a shop's selling (evt_shop.c) */
        n = (size_t)snprintf(sSelect, sizeof(sSelect), "<select 0 2 260 105>\n%s", portTrGame("From the bag"));
        n += (size_t)snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("From storage"));
        snprintf(sSelect + n, sizeof(sSelect) - n, "\n%s", portTrGame("Leave"));
        evtSetValue(event, args[1], (EvtWord)sSelect);
    } else if (which >= 0 && which < (int)(sizeof(kText) / sizeof(kText[0]))) {
        snprintf(sLine[which], sizeof(sLine[which]), "%s%s", portTrGame(kText[which][0]), kText[which][1]);
        evtSetValue(event, args[1], (EvtWord)sLine[which]);
    }
    return EVT_RETURN_DONE;
}

/* --- Walk-speed boost --- */

int (*gPortWalkBoostHeld)(int port);

/* each player's own boost: before, anyone's control sped Mario and nobody's sped their own partner */
float portWalkBoostFor(int port) {
    if (!portGameplus("walk_boost") || gPortWalkBoostHeld == NULL || !gPortWalkBoostHeld(port)) {
        return 1.0f;
    }
    {
        static int sTold[4]; /* once a player: the boost first used */
        if (port >= 0 && port < 4 && !sTold[port]) {
            sTold[port] = 1;
            printf("[gameplus] walk boost: %d%% speed (player %d)\n", portGameplus("walk_boost_speed"), port + 1);
        }
    }
    return portGameplus("walk_boost_speed") / 100.0f;
}

float portWalkBoost(void) {
    return portWalkBoostFor(0);
}

/* --- Coin and EXP multipliers --- */

/* x1, x1.5, x2, x2.5, x3 */
static int multiplied(int v, const char* key) {
    const int half = portGameplus(key) + 2; /* in halves */
    return half == 2 || v <= 0 ? v : (v * half + 1) / 2;
}

int portBattleCoins(int coins) {
    const int r = multiplied(coins, "coin_multiplier");
    if (r != coins) {
        printf("[gameplus] coins: %d -> %d\n", coins, r);
        fflush(stdout);
    }
    return r;
}

int portBattleExp(int exp) {
    const int r = multiplied(exp, "exp_multiplier");
    if (r != exp) {
        printf("[gameplus] star points: %d -> %d\n", exp, r);
        fflush(stdout);
    }
    return r;
}

/* --- Star Pieces and Shine Sprites by map, from their flags (evt_story.h) --- */

static const struct {
    char map[8];
    int flag;
    int sprite; /* 0 a Star Piece, 1 a Shine Sprite */
} kCollectibles[] = {
    {"aji_03", GSWF_AJI03_STAR_PIECE_PRIZE, 0}, {"aji_08", GSWF_AJI08_STAR_PIECE, 0}, {"bom_00", GSWF_BOM00_STAR_PIECE_PANEL, 0},
    {"bom_01", GSWF_BOM01_STAR_PIECE, 0}, {"bom_02", GSWF_BOM02_SHINE_SPRITE, 1}, {"bom_02", GSWF_BOM02_STAR_PIECE, 0},
    {"bom_02", GSWF_BOM02_STAR_PIECE_PANEL, 0}, {"bom_03", GSWF_BOM03_SHINE_SPRITE, 1}, {"bom_03", GSWF_BOM03_STAR_PIECE, 0},
    {"bom_04", GSWF_BOM04_STAR_PIECE, 0}, {"dou_02", GSWF_DOU02_SHINE_SPRITE, 1}, {"dou_02", GSWF_DOU02_STAR_PIECE, 0},
    {"dou_03", GSWF_DOU03_STAR_PIECE_PANEL, 0}, {"dou_04", GSWF_DOU04_SHINE_SPRITE, 1}, {"dou_04", GSWF_DOU04_STAR_PIECE, 0},
    {"dou_05", GSWF_DOU05_SHINE_SPRITE, 1}, {"dou_06", GSWF_DOU06_STAR_PIECE_PANEL, 0}, {"dou_09", GSWF_DOU09_SHINE_SPRITE, 1},
    {"dou_10", GSWF_DOU10_SHINE_SPRITE, 1}, {"eki_02", GSWF_EKI02_STAR_PIECE, 0}, {"eki_03", GSWF_EKI03_SHINE_SPRITE, 1},
    {"eki_06", GSWF_EKI06_SHINE_SPRITE, 1}, {"gon_03", GSWF_GON03_STAR_PIECE_PANEL, 0}, {"gon_04", GSWF_GON04_SHINE_SPRITE, 1},
    {"gon_04", GSWF_GON04_STAR_PIECE_1, 0}, {"gon_04", GSWF_GON04_STAR_PIECE_2, 0}, {"gon_05", GSWF_GON05_SHINE_SPRITE, 1},
    {"gon_05", GSWF_GON05_STAR_PIECE, 0}, {"gon_09", GSWF_GON09_STAR_PIECE, 0}, {"gon_12", GSWF_GON12_SHINE_SPRITE, 1},
    {"gor_00", GSWF_GOR00_STAR_PIECE, 0}, {"gor_00", GSWF_GOR00_STAR_PIECE_PANEL, 0}, {"gor_01", GSWF_GOR01_STAR_PIECE_1, 0},
    {"gor_01", GSWF_GOR01_STAR_PIECE_2, 0}, {"gor_01", GSWF_GOR01_STAR_PIECE_3, 0}, {"gor_01", GSWF_GOR01_STAR_PIECE_PANEL_1, 0},
    {"gor_01", GSWF_GOR01_STAR_PIECE_PANEL_2, 0}, {"gor_02", GSWF_GOR02_SHINE_SPRITE_1, 1}, {"gor_02", GSWF_GOR02_SHINE_SPRITE_2, 1},
    {"gor_02", GSWF_GOR02_SHINE_SPRITE_3, 1}, {"gor_02", GSWF_GOR02_STAR_PIECE_1, 0}, {"gor_02", GSWF_GOR02_STAR_PIECE_2, 0},
    {"gor_02", GSWF_GOR02_STAR_PIECE_3, 0}, {"gor_02", GSWF_GOR02_STAR_PIECE_PANEL_1, 0}, {"gor_02", GSWF_GOR02_STAR_PIECE_PANEL_2, 0},
    {"gor_03", GSWF_GOR03_SHINE_SPRITE_1, 1}, {"gor_03", GSWF_GOR03_SHINE_SPRITE_2, 1}, {"gor_03", GSWF_GOR03_STAR_PIECE_1, 0},
    {"gor_03", GSWF_GOR03_STAR_PIECE_2, 0}, {"gor_03", GSWF_GOR03_STAR_PIECE_3, 0}, {"gor_03", GSWF_GOR03_STAR_PIECE_PANEL, 0},
    {"gor_04", GSWF_GOR04_STAR_PIECE, 0}, {"gor_04", GSWF_GOR04_STAR_PIECE_PANEL, 0}, {"gra_02", GSWF_GRA02_STAR_PIECE_1, 0},
    {"gra_02", GSWF_GRA02_STAR_PIECE_2, 0}, {"gra_05", GSWF_GRA05_SHINE_SPRITE, 1}, {"gra_06", GSWF_GRA06_STAR_PIECE, 0},
    {"hei_00", GSWF_HEI00_STAR_PIECE_TREE, 0}, {"hei_01", GSWF_HEI01_STAR_PIECE, 0}, {"hei_02", GSWF_HEI02_STAR_PIECE_BUSH, 0},
    {"jin_03", GSWF_JIN03_STAR_PIECE_PANEL, 0}, {"jin_05", GSWF_JIN05_SHINE_SPRITE, 1}, {"jin_05", GSWF_JIN05_STAR_PIECE, 0},
    {"jin_07", GSWF_JIN07_STAR_PIECE, 0}, {"jin_08", GSWF_JIN08_STAR_PIECE_PANEL, 0}, {"jin_09", GSWF_JIN09_SHINE_SPRITE, 1},
    {"jin_11", GSWF_JIN11_SHINE_SPRITE, 1}, {"moo_02", GSWF_MOO02_STAR_PIECE, 0}, {"mri_02", GSWF_MRI02_STAR_PIECE_PANEL, 0},
    {"mri_03", GSWF_MRI03_STAR_PIECE_PANEL, 0}, {"mri_05", GSWF_MRI05_SHINE_SPRITE, 1}, {"mri_06", GSWF_MRI06_STAR_PIECE_PLANTER, 0},
    {"mri_09", GSWF_MRI09_SHINE_SPRITE, 1}, {"mri_12", GSWF_MRI12_STAR_PIECE, 0}, {"mri_15", GSWF_MRI15_STAR_PIECE_PLANTER, 0},
    {"mri_16", GSWF_MRI16_SHINE_SPRITE, 1}, {"mri_17", GSWF_MRI17_STAR_PIECE, 0}, {"mri_19", GSWF_MRI19_SHINE_SPRITE, 1},
    {"muj_00", GSWF_MUJ00_STAR_PIECE_PANEL, 0}, {"muj_01", GSWF_MUJ01_STAR_PIECE_1, 0}, {"muj_01", GSWF_MUJ01_STAR_PIECE_2, 0},
    {"muj_02", GSWF_MUJ02_STAR_PIECE_BUSH, 0}, {"muj_03", GSWF_MUJ03_SHINE_SPRITE, 1}, {"muj_03", GSWF_MUJ03_STAR_PIECE, 0},
    {"muj_04", GSWF_MUJ04_SHINE_SPRITE, 1}, {"muj_05", GSWF_MUJ05_STAR_PIECE, 0}, {"nok_00", GSWF_NOK00_STAR_PIECE_PANEL, 0},
    {"nok_01", GSWF_NOK01_STAR_PIECE_PANEL, 0}, {"pik_00", GSWF_PIK00_STAR_PIECE_1, 0}, {"pik_00", GSWF_PIK00_STAR_PIECE_2, 0},
    {"pik_00", GSWF_PIK00_STAR_PIECE_PANEL, 0}, {"pik_01", GSWF_PIK01_SHINE_SPRITE, 1}, {"pik_03", GSWF_PIK03_SHINE_SPRITE, 1},
    {"pik_04", GSWF_PIK04_STAR_PIECE, 0}, {"rsh_00", GSWF_RSH00_STAR_PIECE_PANEL, 0}, {"rsh_02", GSWF_RSH02_SHINE_SPRITE, 1},
    {"rsh_02", GSWF_RSH02_STAR_PIECE_PANEL, 0}, {"rsh_03", GSWF_RSH03_STAR_PIECE_GALLEY_POT, 0},
    {"rsh_03", GSWF_RSH03_STAR_PIECE_WAITRESS, 0}, {"rsh_04", GSWF_RSH04_SHINE_SPRITE_AUTOGRAPH, 1},
    {"rsh_04", GSWF_RSH04_STAR_PIECE_SHELF, 0}, {"tik_00", GSWF_TIK00_SHINE_SPRITE, 1}, {"tik_00", GSWF_TIK00_STAR_PIECE_1, 0},
    {"tik_00", GSWF_TIK00_STAR_PIECE_2, 0}, {"tik_00", GSWF_TIK00_STAR_PIECE_3, 0}, {"tik_00", GSWF_TIK00_STAR_PIECE_PANEL, 0},
    {"tik_01", GSWF_TIK01_STAR_PIECE_PANEL, 0}, {"tik_02", GSWF_TIK02_SHINE_SPRITE, 1}, {"tik_03", GSWF_TIK03_STAR_PIECE_PANEL, 0},
    {"tik_04", GSWF_TIK04_STAR_PIECE, 0}, {"tik_05", GSWF_TIK05_SHINE_SPRITE, 1}, {"tik_05", GSWF_TIK05_STAR_PIECE_PANEL, 0},
    {"tik_06", GSWF_TIK06_STAR_PIECE, 0}, {"tik_07", GSWF_TIK07_SHINE_SPRITE, 1}, {"tik_07", GSWF_TIK07_STAR_PIECE_1, 0},
    {"tik_07", GSWF_TIK07_STAR_PIECE_2, 0}, {"tik_13", GSWF_TIK13_STAR_PIECE, 0}, {"tik_16", GSWF_TIK16_SHINE_SPRITE, 1},
    {"tik_19", GSWF_TIK19_STAR_PIECE_PANEL, 0}, {"tik_20", GSWF_TIK20_SHINE_SPRITE_1, 1}, {"tik_20", GSWF_TIK20_SHINE_SPRITE_2, 1},
    {"tik_20", GSWF_TIK20_SHINE_SPRITE_3, 1}, {"tou_01", GSWF_TOU01_SHINE_SPRITE, 1}, {"tou_01", GSWF_TOU01_STAR_PIECE_1, 0},
    {"tou_01", GSWF_TOU01_STAR_PIECE_2, 0}, {"tou_01", GSWF_TOU01_STAR_PIECE_3, 0}, {"tou_01", GSWF_TOU01_STAR_PIECE_4, 0},
    {"tou_01", GSWF_TOU01_STAR_PIECE_PANEL, 0}, {"tou_02", GSWF_TOU02_STAR_PIECE_PANEL, 0}, {"tou_05", GSWF_TOU05_STAR_PIECE, 0},
    {"tou_05", GSWF_TOU05_STAR_PIECE_DRAWER, 0}, {"tou_06", GSWF_TOU06_SHINE_SPRITE, 1}, {"tou_06", GSWF_TOU06_STAR_PIECE_PANEL, 0},
    {"tou_12", GSWF_TOU12_STAR_PIECE, 0}, {"usu_00", GSWF_USU00_STAR_PIECE, 0}, {"usu_00", GSWF_USU00_STAR_PIECE_BUSH, 0},
    {"usu_01", GSWF_USU01_STAR_PIECE, 0}, {"win_02", GSWF_WIN02_SHINE_SPRITE, 1}, {"win_02", GSWF_WIN02_STAR_PIECE, 0},
    {"win_03", GSWF_WIN03_STAR_PIECE, 0}, {"win_03", GSWF_WIN03_STAR_PIECE_TREE, 0}, {"win_05", GSWF_WIN05_STAR_PIECE_PANEL, 0},
};

int portCollectiblesForMap(const char* map, int* pieces, int* piecesTotal, int* sprites, int* spritesTotal) {
    size_t i;
    *pieces = *piecesTotal = *sprites = *spritesTotal = 0;
    if (map == NULL || pouchGetPtr() == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(kCollectibles) / sizeof(kCollectibles[0]); i++) {
        /* the map's name without a variant's suffix (rsh_03_a) */
        if (strncmp(map, kCollectibles[i].map, 6) != 0) {
            continue;
        }
        if (kCollectibles[i].sprite) {
            (*spritesTotal)++;
            *sprites += swGet(kCollectibles[i].flag) != 0;
        } else {
            (*piecesTotal)++;
            *pieces += swGet(kCollectibles[i].flag) != 0;
        }
    }
    return *piecesTotal + *spritesTotal > 0;
}

int portCollectibleAt(int i, const char** map, int* sprite, int* found) {
    if (i < 0 || i >= (int)(sizeof(kCollectibles) / sizeof(kCollectibles[0])) || pouchGetPtr() == NULL) {
        return 0;
    }
    *map = kCollectibles[i].map;
    *sprite = kCollectibles[i].sprite;
    *found = swGet(kCollectibles[i].flag) != 0;
    return 1;
}

int portCollectiblesHere(int* pieces, int* piecesTotal, int* sprites, int* spritesTotal) {
    if (seqGetSeq() != SEQ_GAME || pouchGetPtr() == NULL) {
        *pieces = *piecesTotal = *sprites = *spritesTotal = 0;
        return 0;
    }
    return portCollectiblesForMap(gp->currentMapName, pieces, piecesTotal, sprites, spritesTotal);
}

/* --- Faster text outside battle --- */

int portFieldTextScale(void) {
    return portGameplus("fast_field_text") && seqGetSeq() != SEQ_BATTLE ? 3 : 1;
}

/* --- Jukebox --- */

static int sMapBgm = -1;   /* what the game last asked for on the field channel */
static int sJukebox = -1;  /* what the jukebox plays, -1 none */
static int sJukeboxCall;   /* the jukebox's own psndBGMOn is running */

void portJukeboxHeard(int id, int ch) {
    if (sJukeboxCall || id <= 0 || id >= portBgmCount()) {
        return;
    }
    if (ch == 0) {
        sMapBgm = id;
        sJukebox = -1;
    }
    if (portGameplus("jukebox") && fileLoad() && !(sFile.heard[id / 32] & (1u << (id % 32)))) {
        sFile.heard[id / 32] |= 1u << (id % 32);
        fileWrite();
    }
}

int portJukeboxKnown(int id) {
    if (id <= 0 || id >= portBgmCount() || portBgmName(id)[0] == '\0') {
        return 0; /* (a nameless slot of the BGM table: listed, it was a blank track that played nothing) */
    }
    if (portJukeboxUnlocked(id)) { /* the Jukebox's rules (qol_pc.c) */
        return 1;
    }
    return fileLoad() && (sFile.heard[id / 32] & (1u << (id % 32))) != 0;
}

void portJukeboxPlay(int id) {
    const int play = id < 0 ? sMapBgm : id;
    if (play <= 0 || play >= portBgmCount() || !playing()) {
        return;
    }
    sJukeboxCall = 1;
    printf("[jukebox] plays %s\n", portBgmName(play));
    psndBGMOn(PSND_FADE_RESTART, (char*)portBgmName(play));
    sJukeboxCall = 0;
    sJukebox = id < 0 ? -1 : id;
}

int portJukeboxCalling(void) {
    return sJukeboxCall;
}

int portJukeboxPlaying(void) {
    return sJukebox;
}
