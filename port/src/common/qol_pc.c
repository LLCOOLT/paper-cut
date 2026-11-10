/* The remake's comfort and rule changes (port/include/port/qol.h): every one
 * off by default, so the game plays as the original until a setting says
 * otherwise; any build takes $TTYD_SET_<key> for a run. */
#include <stdio.h>
#include <string.h>
#include "port/statesync.h"
#include <stdlib.h>
#include "manager/dvdmgr.h"
#include "system.h"
#include "port/qol.h"
#include "port/gameplus.h"
#include "port/statesync.h"
#include "port/settings.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "driver/npcdrv.h"
#include "mario/mario_party.h"
#include "party.h"
#include "data/item_data.h"
#include "eff/eff_recovery.h"
#include "pmario_sound.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/savestate.h"
#include "port/tr.h"
#include "port/battlex.h"
#include "port/pm64_media.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "evt/evt_cmd.h"
#include "evt/evt_mario.h"
#include "evt/evt_msg.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mariost.h"
#include "win/win_main.h"
#include <dolphin/pad.h>
#include <dolphin/os.h>

/* every setting here: key, default, range */
static const struct {
    const char* key;
    int def, min, max;
} kSettings[] = {
    {"coin_cap", 0, 0, 1},           /* coins up to 9,999, Money Money up to 99 a battle */
    {"full_bag_use", 0, 0, 1},       /* a healing item eaten when the bag is full */
    {"levelup_heal_party", 0, 0, 1}, /* a level-up heals every partner */
    {"partner_hint", 0, 0, 1},       /* the partner says where to go next */
    {"talk_prompts", 0, 0, 1},       /* "!" over what can be talked to or checked */
    {"mail_no_stop", 0, 0, 1},       /* mail notices that don't stop Mario */
    {"shop_plus", 0, 0, 1},          /* shop improvements */
    {"list_lr_scroll", 0, 0, 1},     /* Left/Right page through item and badge lists */
    {"bowser_retry", 0, 0, 1},       /* retry in the Bowser intermission's pause */
    {"save_file_details", 0, 0, 1},  /* partner, date, colour, medal on the file screen */
    {"jukebox_unlock", 0, 0, 2},     /* the Jukebox: 0 heard on this file, 1 by Shine Sprites, 2 all */
    {"skip_interludes", 0, 0, 2},    /* the Peach and Bowser interludes skipped: 1 once seen, 2 always */
    {"pace_trims", 0, 0, 1},         /* Glitz Pit and chapter 6 pace trims */
    {"coin_hints", 0, 0, 1},         /* the Journal sells hints for coins */
};

void portQolDeclare(void) {
    static int declared;
    size_t i;
    if (declared) {
        return;
    }
    declared = 1;
    for (i = 0; i < sizeof(kSettings) / sizeof(kSettings[0]); i++) {
        portSettingsDeclare(kSettings[i].key, kSettings[i].def, kSettings[i].min, kSettings[i].max);
    }
}

int portQol(const char* key) {
    int* v;
    portQolDeclare();
    v = portSettingsFind(key);
    return v != NULL ? *v : 0;
}

/* --- Coins up to 9,999 --- */

int portCoinCap(void) {
    return portQol("coin_cap") ? 9999 : 999;
}

/* coins a battle drops on the field at most: the field has 32 item slots, and
 * a coin past them takes an older one's place (lost) */
#define FIELD_COIN_DROPS 16

int portBattleCoinDrop(int coins) {
    if (!portQol("coin_cap") || coins <= 0) {
        return coins;
    }
    if (coins > 99) {
        coins = 99;
    }
    if (coins > FIELD_COIN_DROPS) {
        printf("[qol] battle coins: %d paid at once, %d dropped\n", coins - FIELD_COIN_DROPS, FIELD_COIN_DROPS);
        fflush(stdout);
        pouchAddCoin(coins - FIELD_COIN_DROPS);
        coins = FIELD_COIN_DROPS;
    }
    return coins;
}

/* --- Use a healing item at once when the bag is full --- */

int portFullBagUse(int itemId) {
    PouchData* pouch = pouchGetPtr();
    MarioWork* mario = marioGetPtr();
    const int party = marioGetParty();
    int hp, fp, partyHp, needs;
    if (!portQol("full_bag_use") || pouch == NULL || itemId < ITEM_MIN || itemId >= ITEM_MAX) {
        return 0;
    }
    hp = itemDataTable[itemId].hp_restored;
    fp = itemDataTable[itemId].fp_restored;
    if (hp <= 0 && fp <= 0) {
        return 0;
    }
    partyHp = party > 0 && party < 8 && hp > 0 && pouchGetPartyHP(party) < pouch->partyData[party].maximumHP;
    needs = (hp > 0 && pouch->currentHP < pouch->maximumHP) || (fp > 0 && pouch->currentFP < pouch->maximumFP) ||
            partyHp;
    if (!needs) {
        return 0; /* nothing to heal: thrown away, as the game does */
    }
    if (hp > 0) {
        pouchSetHP((s16)(pouchGetHP() + hp));
        if (partyHp) {
            pouchSetPartyHP(party, pouchGetPartyHP(party) + hp);
        }
        portHealOtherPartners(party, hp);
        effRecoveryEntry(0, hp, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y,
                         mario->position.z);
    }
    if (fp > 0) {
        pouchSetFP((s16)(pouchGetFP() + fp));
        effRecoveryEntry(1, fp, mario->position.x, mario->position.y + mario->wPlayerCollisionBox.y + 20.0f,
                         mario->position.z);
    }
    psndSFXOn(hp > 0 ? "SFX_HP_RECOVER_MID1" : "SFX_FP_RECOVER_MID1");
    printf("[qol] full bag: item %d used at once\n", itemId);
    fflush(stdout);
    return 1;
}

/* --- A level-up heals every partner --- */

void portLevelUpHealParty(void) {
    PouchData* pouch = pouchGetPtr();
    int i;
    if (!portQol("levelup_heal_party") || pouch == NULL) {
        return;
    }
    pouchRevisePartyParam(); /* (HP Plus P worn counted) */
    for (i = 1; i < 8; i++) {
        if (pouch->partyData[i].flags & POUCH_PARTY_FLAG_JOINED) {
            pouch->partyData[i].currentHP = pouch->partyData[i].maximumHP;
        }
    }
}

/* --- Left and Right scroll lists --- */

int portListPageRows(void) {
    return portQol("list_lr_scroll") ? 6 : 0;
}

/* --- Shop improvements --- */

void portShopLine(int itemId, int points, char* out, int size) {
    PouchData* pouch = pouchGetPtr();
    int i, held = 0, stored = 0, n;
    if (pouch != NULL) {
        for (i = 0; i < MAX_HELD_ITEMS; i++) {
            held += pouch->heldItems[i] == itemId;
        }
        for (i = 0; i < MAX_STORED_ITEMS; i++) {
            stored += pouch->storedItems[i] == itemId;
        }
    }
    n = snprintf(out, (size_t)size, portTrGame("Held: %d"), held);
    if (stored > 0 && n > 0 && n < size) {
        n += snprintf(out + n, (size_t)(size - n), "  %s", portTrGame("Stored:"));
        if (n > 0 && n < size) {
            n += snprintf(out + n, (size_t)(size - n), " %d", stored);
        }
    }
    if (n > 0 && n < size) {
        n += snprintf(out + n, (size_t)(size - n), "    %s", portTrGame("Shop points:"));
        if (n > 0 && n < size) {
            snprintf(out + n, (size_t)(size - n), " %d", points);
        }
    }
}

/* --- The partner hint --- */

/* port/src/aurora/journal_hint_aurora.c; the headless build has no hints */
__attribute__((weak)) int portJournalNextStep(const char** area, const char** name) {
    (void)area;
    (void)name;
    return 0;
}

static int sHintAsked;
static EventEntry* sHintEvt;
static char sHintText[512];

/* text (game encoding) broken into lines of about cols characters at spaces */
static void wrapInto(const char* text, char* out, size_t size, int cols) {
    size_t o = 0;
    int col = 0;
    const char* p = text;
    while (*p != '\0' && o + 1 < size) {
        const char* word = p;
        int len = 0;
        while (word[len] != '\0' && word[len] != ' ') {
            len++;
        }
        if (col > 0 && col + 1 + len > cols) {
            out[o++] = '\n';
            col = 0;
        } else if (col > 0) {
            out[o++] = ' ';
            col++;
        }
        while (len-- > 0 && o + 1 < size) {
            out[o++] = *p++;
            col++;
        }
        while (*p == ' ') {
            p++;
        }
    }
    out[o] = '\0';
}

/* LW(0): the hint in the partner's speech window */
static int partnerHintText(EventEntry* event, int isFirstCall) {
    const char* area = NULL;
    const char* name = NULL;
    char raw[384], tag[40];
    (void)isFirstCall;
    if (!portJournalNextStep(&area, &name)) {
        snprintf(raw, sizeof(raw), "%s", portTrGame("The story is over: go wherever you like."));
    } else {
        snprintf(tag, sizeof(tag), "menu_monosiri_%s", area);
        if (portTrLanguage() == 0) { /* the step's name is English only */
            snprintf(raw, sizeof(raw), "%s %s (%s)", portTrGame("Where next:"), msgSearch(tag), name);
        } else {
            snprintf(raw, sizeof(raw), "%s %s", portTrGame("Where next:"), msgSearch(tag));
        }
    }
    wrapInto(raw, sHintText, sizeof(sHintText) - 4, 30);
    strcat(sHintText, "<k>");
    evtSetValue(event, event->args[0], (EvtWord)sHintText);
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(partner_hint_evt)
    CALL(evt_mario_key_onoff, 0)
    CALL(partnerHintText, LW(0))
    CALL(evt_msg_print, 0, LW(0), 0, STRING("party"))
    CALL(evt_mario_key_onoff, 1)
    RETURN
EVT_END()

void portPartnerHintPress(void) {
    if (portQol("partner_hint")) {
        sHintAsked = 1;
    }
}

/* Shop improvements: "mail" of new stock, as the remake's restock letters: the
 * Rogueport plaza shop's next Super Luigi book (one more per stage of the story,
 * gor_01.c gor_01_shop_setup) and the Contact Lens once it has arrived. A notice
 * in the corner and the mail chime, once per new stock on this file. */
static void restockFrame(void) {
    static int sWait;
    int stage, story, sig, last;
    if (++sWait < 60) {
        return; /* (once a second) */
    }
    sWait = 0;
    if (!portQol("shop_plus") || seqGetSeq() != SEQ_GAME || seqCheckSeq() || pouchGetPtr() == NULL ||
        !marioChkKey()) {
        return;
    }
    story = swStoryPeek();
    stage = story < STORY_STG4_15_05_PEACH_X_NAUT_PHD_REPORT ? 0
            : story < STORY_STG5_43_X_NAUTS_RETREAT         ? 1
            : story < STORY_STG6_54_WELCOME_BACK            ? 2
            : story < STORY_STG7_20_LORD_CRUMP_DEFEATED     ? 3
            : story < STORY_STG8_16_SHADOW_QUEEN            ? 4
                                                            : 5;
    sig = 1 + stage * 2 + (swGet(GSWF_GOR01_LENS_ARRIVED) && !swGet(GSWF_GOR01_LENS_DELIVERED));
    last = portFileStock(0, 0);
    if (last < 0 || last == sig) {
        return;
    }
    portFileStock(1, sig);
    if (last == 0 || sig < last || ((sig - 1) / 2 == (last - 1) / 2 && sig % 2 == 1)) {
        return; /* first look at this file, or nothing new (the lens taken) */
    }
    portSaveStateNotice(portTr("Mail: new stock at the Rogueport shop."));
    psndSFXOn("SFX_MAIL_RECEPTION1");
}

void portQolFrame(void) {
    restockFrame();
    if (sHintEvt != NULL && !evtCheckID(sHintEvt->eventId)) {
        sHintEvt = NULL;
    }
    if (!sHintAsked) {
        return;
    }
    sHintAsked = 0;
    /* in the field, Mario free and a partner out; online and in replays every game would have to ask at once */
    if (sHintEvt != NULL || seqGetSeq() != SEQ_GAME || seqCheckSeq() || marioStGetSystemLevel() != 0 ||
        !marioChkKey() || marioGetParty() <= 0 || portStateSyncLinked() || portReplayActive()) {
        return;
    }
    printf("[qol] partner hint\n");
    fflush(stdout);
    sHintEvt = evtEntry(partner_hint_evt, 0, 0);
}

/* --- Event commands --- */

int portEvtQolOn(EventEntry* event, int isFirstCall) {
    EvtWord* args = event->args;
    PouchData* pouch = pouchGetPtr();
    int on = 0, i;
    (void)isFirstCall;
    switch (evtGetValue(event, args[0])) {
        case PORT_QOL_EVT_SHOP:
            on = portQol("shop_plus");
            break;
        case PORT_QOL_EVT_SELL_AGAIN:
            if (portQol("shop_plus") && pouch != NULL) {
                for (i = 0; i < MAX_HELD_ITEMS; i++) {
                    on |= pouch->heldItems[i] != 0;
                }
            }
            break;
        case PORT_QOL_EVT_SELL_STORED:
            on = portQol("shop_plus") && pouch != NULL && pouchGetKeepItemCnt() > 0;
            break;
        case PORT_QOL_EVT_MAIL_QUIET:
            on = portQol("mail_no_stop") && swGet(GSWF_MAIL_TUTORIAL_SEEN) && swGet(GSWF_MAIL_OPENED_ONCE);
            if (on) {
                portSaveStateNotice(portTr("Mail arrived: read it from the Start menu's items."));
                psndSFXOn("SFX_MAIL_RECEPTION1");
            }
            break;
    }
    evtSetValue(event, args[1], on);
    return EVT_RETURN_DONE;
}

/* --- The Notes page --- */

static int inField(void) {
    const s32 seq = (s32)seqGetSeq();
    return (seq == SEQ_GAME || seq == SEQ_BATTLE) && pouchGetPtr() != NULL;
}

int portNotesTroubleTaken(void) {
    int id;
    if (!inField()) {
        return 0;
    }
    id = swByteGet(GSW_TROUBLE_ACTIVE);
    return id >= 1 && id <= 30 ? id : 0;
}

int portNotesTroubleSolved(int id) {
    if (!inField()) {
        return -1;
    }
    return swGet(GSWF_TROUBLE_SOLVED_BASE + id) != 0;
}

extern const char* language_dir[]; /* msgdrv.c */

/* A map's message file (tag, text pairs) read from the disc, kept per language. The game loads only the
 * current map's file (and the global one), so Notes reads the rooms' own: the Trouble Center's texts are in
 * gor_02's, Merluvlee's readings in tik_00's, the Glitz Pit fighters' names and titles in tou_03's. */
static const char* mapText(const char* file, int* length) {
    static struct {
        char file[16];
        int language, length;
        char* text;
    } sCache[3];
    static int sNext;
    char path[160];
    DVDEntry* dvd;
    int i;
    for (i = 0; i < 3; i++) {
        if (sCache[i].text != NULL && sCache[i].language == gp->language && !strcmp(sCache[i].file, file)) {
            *length = sCache[i].length;
            return sCache[i].text;
        }
    }
    i = sNext; /* (the files read: gor_02, tik_00, tou_03) */
    sNext = (sNext + 1) % 3;
    free(sCache[i].text);
    sCache[i].text = NULL;
    sCache[i].length = 0;
    sCache[i].language = gp->language;
    snprintf(sCache[i].file, sizeof(sCache[i].file), "%s", file);
    snprintf(path, sizeof(path), "%s/./%s/%s.txt", getMarioStDvdRoot(), language_dir[gp->language], file);
    dvd = DVDMgrOpen(path, 2, 0);
    if (dvd != NULL) {
        const int len = (int)DVDMgrGetLength(dvd);
        const int rounded = (len + 31) & ~31;
        sCache[i].text = (char*)calloc(1, (size_t)rounded + 2);
        if (sCache[i].text != NULL) {
            DVDMgrRead(dvd, sCache[i].text, rounded, 0);
            sCache[i].length = len;
        }
        DVDMgrClose(dvd);
    }
    *length = sCache[i].length;
    return sCache[i].text;
}

/* message tag of a map's file without its control tags (a page break is a line break); its length, 0 when
 * the file or the tag isn't there */
static int mapMsg(const char* file, const char* tag, char* out, int size) {
    const char* text;
    const char* p;
    int length, o = 0;
    if (size <= 0) {
        return 0;
    }
    out[0] = 0;
    text = mapText(file, &length);
    if (text == NULL) {
        return 0;
    }
    for (p = text; p < text + length && *p != 0;) {
        const char* msg = p + strlen(p) + 1;
        if (msg >= text + length) {
            break;
        }
        if (!strcmp(p, tag)) {
            while (*msg != 0 && o + 1 < size) {
                if (*msg == '<') {
                    if (!strncmp(msg, "<k>", 3) || !strncmp(msg, "<p>", 3)) {
                        if (o > 0 && out[o - 1] != '\n') {
                            out[o++] = '\n';
                        }
                    }
                    while (*msg != 0 && *msg != '>') {
                        msg++;
                    }
                    msg += *msg == '>';
                    continue;
                }
                out[o++] = *msg++;
            }
            while (o > 0 && out[o - 1] == '\n') {
                o--;
            }
            out[o] = 0;
            return o;
        }
        p = msg + strlen(msg) + 1;
    }
    return 0;
}

void portNotesTroubleText(int id, char* client, int clientSize, char* contents, int contentsSize) {
    char tag[32];
    snprintf(tag, sizeof(tag), "irai_client_%02d", id);
    mapMsg("gor_02", tag, client, clientSize);
    snprintf(tag, sizeof(tag), "irai_contents_%02d", id);
    mapMsg("gor_02", tag, contents, contentsSize);
}

/* --- Merluvlee's readings (Notes) --- */

int portNotesFortune(int i, char* out, int size) {
    char label[32];
    if (size <= 0 || !portFortuneGet(i, label, sizeof(label))) {
        return 0;
    }
    return mapMsg("tik_00", label, out, size) > 0;
}

/* --- Hints for coins (the Journal's row) --- */

#define COIN_HINT_PRICE 10

/* the area (first three letters) has a found collectible: Mario has been there */
static int areaVisited(const char* map) {
    const char* m;
    int i, sprite, found;
    for (i = 0; portCollectibleAt(i, &m, &sprite, &found); i++) {
        if (found && strncmp(m, map, 3) == 0) {
            return 1;
        }
    }
    return 0;
}

int portCoinHintPrice(void) {
    return COIN_HINT_PRICE;
}

int portCoinHintBuy(char* out, int size) {
    const char* m;
    const char* best = NULL;
    int i, sprite, found, bestSprite = 0, bestRank = 99, left = 0;
    char tag[40];
    if (!inField() || seqGetSeq() != SEQ_GAME) {
        return -2;
    }
    if (pouchGetCoin() < COIN_HINT_PRICE) {
        return 0;
    }
    /* this area first, then the areas Mario has found something in, then any */
    for (i = 0; portCollectibleAt(i, &m, &sprite, &found); i++) {
        int rank;
        if (found) {
            continue;
        }
        left++;
        rank = strncmp(m, gp->currentMapName, 3) == 0 ? 0 : areaVisited(m) ? 1 : 2;
        if (rank < bestRank) {
            bestRank = rank;
            best = m;
            bestSprite = sprite;
        }
    }
    if (best == NULL) {
        return -1;
    }
    pouchAddCoin(-COIN_HINT_PRICE);
    snprintf(tag, sizeof(tag), "menu_monosiri_%.3s", best);
    snprintf(out, (size_t)size,
             portTrGame(bestSprite ? "A Shine Sprite is still hidden in %s." : "A Star Piece is still hidden in %s."),
             msgSearch(tag));
    printf("[qol] coin hint: %s (%d left)\n", best, left);
    fflush(stdout);
    return 1;
}

/* --- The Jukebox's rules --- */

int portJukeboxUnlocked(int id) {
    const int rule = portQol("jukebox_unlock");
    const char* m;
    int i, sprite, found, sprites = 0, spritesFound = 0, perSprite;
    if (id <= 0 || id >= portBgmCount() || portBgmName(id)[0] == '\0') {
        return 0; /* (the BGM table's nameless slots, 1-2: no track, nothing to play) */
    }
    if (rule == 2) {
        return 1;
    }
    if (rule != 1 || pouchGetPtr() == NULL) {
        return 0;
    }
    for (i = 0; portCollectibleAt(i, &m, &sprite, &found); i++) {
        sprites += sprite;
        spritesFound += sprite && found;
    }
    if (sprites == 0) {
        return 0;
    }
    /* the list in order, a share per Shine Sprite: every track with the last one */
    perSprite = (portBgmCount() + sprites - 1) / sprites;
    return id < spritesFound * perSprite + 1;
}

/* --- Retry in the Bowser intermission's pause --- */

int portBowserRetryRoom(void) {
    MarioWork* mario = marioGetPtr();
    return portQol("bowser_retry") && mario != NULL && mario->characterId == 2;
}

int portBowserRetry(int buttons) {
    if (!portBowserRetryRoom() || !portSaveStateHas(PORT_SAVE_STATE_RETRY)) {
        return 0;
    }
    portSaveStateNotice("Y: this stage again from its start");
    if (!(buttons & PAD_BUTTON_Y)) {
        return 0;
    }
    printf("[qol] Bowser's stage again\n");
    fflush(stdout);
    portSaveStateRequest(PORT_SAVE_STATE_RETRY, 1);
    return 1;
}

/* --- The keyboard in the game's name entry (nameent.c) --- */

/* port/settings/features/menu_typing.cpp (desktop); the headless build has no keyboard */
__attribute__((weak)) void portMenuTypingFrame(void) {
}
__attribute__((weak)) int portMenuTyped(char* out, int size) {
    if (size > 0) {
        out[0] = 0;
    }
    return 0;
}

unsigned portNameEntTyped(char* name, int* nameLen) {
    char typed[32];
    unsigned trg = 0;
    int i, n;
    portMenuTypingFrame();
    n = portMenuTyped(typed, sizeof(typed));
    for (i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)typed[i];
        if (c == 10) { /* Enter: as Start, to End */
            trg |= PAD_BUTTON_START;
        } else if (c == 27) { /* Esc: as B */
            trg |= PAD_BUTTON_B;
        } else if (c == 8) { /* Backspace: the last letter out (never leaves the screen) */
            if (*nameLen > 0 || name[0] != ' ') {
                if (*nameLen < 8 && name[*nameLen] == ' ' && *nameLen > 0) {
                    (*nameLen)--;
                }
                name[*nameLen] = ' ';
                psndSFXOn((const char*)0xB);
            }
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                   c == '.' || c == '-' || c == '!' || c == '?' || c == '\'' || c == '&') {
            if (*nameLen < 8 && !(*nameLen == 7 && name[7] != ' ')) {
                name[*nameLen] = (char)c;
                if (*nameLen < 7) {
                    (*nameLen)++;
                }
                psndSFXOn((const char*)0xA);
            }
        }
    }
    return trg;
}

/* --- The recipe book --- */

typedef struct RecipeHint {
    const char* dish; /* itemDataTable[].id */
    const char* a;
    const char* b; /* "" for a one-ingredient dish */
} RecipeHint;

#include "recipe_hints.inc"

static int itemById(const char* id) {
    int i;
    for (i = 1; i < ITEM_MAX; i++) {
        if (itemDataTable[i].id != NULL && strcmp(itemDataTable[i].id, id) == 0) {
            return i;
        }
    }
    return 0;
}

static int gameRecipeCount(void) {
    int i, n = 0;
    for (i = WIN_RECIPE_MIN; i < ITEM_MAX; i++) {
        n += itemDataTable[i].type_sort_order != -1;
    }
    return n;
}

int portRecipeCount(void) {
    /* and Tayce T.'s, with Paper Mario 64's recipes on (port/include/port/pm64_media.h) */
    return gameRecipeCount() + (portPm64FoodActive() ? portPm64DishCount() : 0);
}

/* a Paper Mario 64 dish's row: cooked by this save file (pm64_food_pc.c), else its first recipe's first ingredient */
static int pm64RecipeAt(int dish, char* name, int nameSize, char* hint, int hintSize) {
    const int id = PORT_PM64_FOOD_MIN + dish;
    int a, b;
    snprintf(name, (size_t)nameSize, "%s", msgSearch(itemDataTable[id].name));
    hint[0] = 0;
    if (portPm64FoodCooked(id)) {
        return 1;
    }
    if (portPm64RecipeHint(id, &a, &b)) {
        snprintf(hint, (size_t)hintSize, portTrGame(b != 0 ? "%s, with something else" : "%s, on its own"),
                 msgSearch(itemDataTable[a].name));
    }
    return 0;
}

int portRecipeAt(int index, char* name, int nameSize, char* hint, int hintSize) {
    int i, k, n = 0, item = 0, cooked;
    if (pouchGetPtr() == NULL) {
        return -1;
    }
    if (index >= gameRecipeCount()) {
        return index - gameRecipeCount() < portRecipeCount() - gameRecipeCount()
                   ? pm64RecipeAt(index - gameRecipeCount(), name, nameSize, hint, hintSize)
                   : -1;
    }
    for (i = WIN_RECIPE_MIN; i < ITEM_MAX; i++) {
        if (itemDataTable[i].type_sort_order != -1 && n++ == index) {
            item = i;
            break;
        }
    }
    if (item == 0) {
        return -1;
    }
    cooked = swGet(WIN_RECIPE_COOKED_SW(item - WIN_RECIPE_MIN)) != 0;
    snprintf(name, (size_t)nameSize, "%s", msgSearch(itemDataTable[item].name));
    hint[0] = 0;
    if (cooked) {
        return 1;
    }
    /* the first way to make it: one ingredient said, the other left to find */
    for (k = 0; k < (int)(sizeof(kRecipeHints) / sizeof(kRecipeHints[0])); k++) {
        if (strcmp(kRecipeHints[k].dish, itemDataTable[item].id) == 0) {
            const int a = itemById(kRecipeHints[k].a);
            if (a == 0) {
                continue;
            }
            snprintf(hint, (size_t)hintSize,
                     portTrGame(kRecipeHints[k].b[0] != 0 ? "%s, with something else" : "%s, on its own"),
                     msgSearch(itemDataTable[a].name));
            break;
        }
    }
    return 0;
}

/* --- The save-file screen's details --- */

unsigned portSaveSlotColour(int slot, int selected) {
    /* red, green, blue, gold: the "File N" label's colour, dimmer when not chosen */
    static const unsigned kColours[4] = {0xF06060u, 0x60D060u, 0x70A0FFu, 0xF0C040u};
    const unsigned c = kColours[slot & 3];
    return selected ? c : ((c >> 1) & 0x7F7F7Fu) + 0x404040u;
}

int portSaveFileLine(const GlobalWork* global, int story, char* out, int size) {
    OSCalendarTime t;
    char tag[24];
    const int partner = global->savePartyId[0];
    int n = 0;
    out[0] = 0;
    if (partner >= 1 && partner <= 7) {
        snprintf(tag, sizeof(tag), "name_party%d", partner - 1);
        n = snprintf(out, (size_t)size, "%s  ", msgSearch(tag));
    }
    if (global->saveTime != 0 && n >= 0 && n < size) {
        OSTicksToCalendarTime(global->saveTime, &t);
        snprintf(out + n, (size_t)(size - n), "%04d-%02d-%02d", t.year, t.mon + 1, t.mday);
    }
    /* the completion medal: the story finished */
    return story >= STORY_STG8_END_AFTER_THE_BATTLE;
}

/* --- Skip the Peach and Bowser interludes on a replay ("skip_interludes") ---
 *
 * Each chapter's interlude begins with a map change (to aji_10, or pik_01 for
 * chapter 7's Bowser-only one) at one story value, and ends with one to
 * Mario's side at another (the decomp's scripts; src/event.c's story jumps
 * set the same flags). Once an interlude's end was reached on any file of
 * this device (setting 1; 2, "Always": the first time too, the same skip on
 * a file and device that never saw it: what it leaves doesn't depend on
 * that), the map change into it goes straight to Mario's side, with
 * the story value and the flags the interlude would have left. Nothing else
 * persists: Peach's mails come from the story value on Mario's side, the
 * items Peach picks up are taken back in the same interlude, and Bowser's
 * stage power, lives and time stay inside his stages. */

typedef struct Interlude {
    const char* inMap; /* the map change that enters it, at story inStory */
    int inStory;
    const char* outMap; /* the one that leaves it, at story outStory */
    const char* outBero;
    int outStory;
    s16 flags[6]; /* GSWF set (0 ends) */
} Interlude;

static const Interlude kInterludes[] = {
    {"aji_10", STORY_STG1_17_FATHER_AND_SON_REUNITED, "nok_01", "nokotarou", STORY_STG1_19_02_BOWSER_CASTLE_INTERIOR,
     {GSWF_AJI15_PEACH1_SHOWER_ROOM_ENTERED, GSWF_AJI16_PEACH1_AFTER_SHOWER_REMARK, GSWF_AJI01_PEACH1_DOOR_OPENED,
      GSWF_AJI10_VISITED, GSWF_AJI15_PEACH_SHOWER_TAKEN}},
    {"aji_10", STORY_STG2_25_AFTER_THE_BOSS_BATTLE, "mri_00", "", STORY_STG2_25_05_BOWSER_PETALBURG,
     {GSWF_AJI16_PEACH2_SCENE_DONE, GSWF_AJI10_VISITED}},
    {"aji_10", STORY_STG3_32_BOSS_BATTLE, "tou_02", NULL, STORY_STG3_33_06_BOWSER_PUNI_ELDER,
     {GSWF_AJI09_PEACH3_UNIFORM_TAKEN, GSWF_AJI09_PEACH3_CHANGE_BACK_REMARK, GSWF_AJI09_PEACH3_CHANGED_IN_BOOTH1,
      GSWF_AJI10_VISITED}}, /* (GSWF_AJI09_PEACH3_DISGUISED cleared: she changed back) */
    {"aji_10", STORY_STG4_15_04_SHADOW_SIRENS_AND_DOOPLISS, "usu_00", NULL, STORY_STG4_15_08_BOWSER_ROGUEPORT_DOCK,
     {GSWF_AJI10_VISITED}},
    {"aji_10", STORY_STG5_43_X_NAUTS_RETREAT, "muj_00", "", STORY_STG5_49_BOWSER_TWILIGHT_TOWN_2,
     {GSWF_AJI11_PEACH5_ROOM_REMARK, GSWF_AJI11_PEACH5_DATA_DISK_TAKEN, GSWF_AJI11_PEACH5_DISK_READ,
      GSWF_AJI06_PEACH5_RIGHT_POTION, GSWF_AJI10_VISITED}},
    {"aji_10", STORY_STG6_54_WELCOME_BACK, "gor_04", "resha_return", STORY_STG6_55_04_BOWSER_RAWK_HAWK,
     {GSWF_AJI10_VISITED}},
    {"pik_01", STORY_STG7_20_LORD_CRUMP_DEFEATED, "aji_18", "e_bero", STORY_STG7_25_00_1_BOWSER_POSHLEY_SANCTUM, {0}},
};

static int sInterludeSkipped;

/* Played, every interlude ends with no partner out: Peach's and Bowser's
 * scripts send Mario's back (evt_mario_kill_party 0: aji_10, aji_16, kpa,
 * mri_01), and each arrival on Mario's side then puts Goombella out and heals
 * (mri_00, tou_02, usu_00, muj_00, gor_04, aji_17; the JP build's heal only
 * then). And the trip passes through other areas, which clears the area's
 * words. The skip does both here, with every script of the old room gone. */
int portInterludeUnload(void) {
    MarioWork* mp;
    if (!sInterludeSkipped) {
        return 0;
    }
    sInterludeSkipped = 0;
    mp = marioGetPtr();
    if (mp != NULL && mp->currPartySlotId[0] >= 0) {
        partyKill(mp->currPartySlotId[0]); /* as evt_mario_kill_party(0) */
    }
    return 1;
}

void portInterludeMapChange(const char** map, const char** bero) {
    const int story = swStoryPeek();
    size_t i;
    int j;
    if (*map == NULL || pouchGetPtr() == NULL) {
        return;
    }
    for (i = 0; i < sizeof(kInterludes) / sizeof(kInterludes[0]); i++) {
        const Interlude* in = &kInterludes[i];
        if (story == in->outStory && !strcmp(*map, in->outMap)) {
            portSceneSeenEver(PORT_SCENE_INTERLUDE - PORT_SCENE_RECORDED + (int)i, 1); /* its end reached */
            return;
        }
        if (story != in->inStory || strcmp(*map, in->inMap) || portQol("skip_interludes") == 0 ||
            (portQol("skip_interludes") == 1 &&
             !portSceneSeenEver(PORT_SCENE_INTERLUDE - PORT_SCENE_RECORDED + (int)i, 0)) ||
            portStateSyncOn() || portReplayActive()) {
            continue;
        }
        for (j = 0; j < 6 && in->flags[j] != 0; j++) {
            swSet(in->flags[j]);
        }
        if (in->inStory == STORY_STG3_32_BOSS_BATTLE) {
            swClear(GSWF_AJI09_PEACH3_DISGUISED);
        }
        swByteSet(GSW_STORY_PROGRESS, in->outStory);
        sInterludeSkipped = 1; /* (portInterludeUnload: the partner back, the area's words cleared) */
        printf("[qol] interlude %d skipped: %s -> %s\n", (int)i + 1, *map, in->outMap);
        fflush(stdout);
        *map = in->outMap;
        *bero = in->outBero;
        return;
    }
}

/* --- Glitz Pit and chapter 6 pace trims ("pace_trims") --- */

int portPaceTrims(void) {
    return portQol("pace_trims") && !portStateSyncOn() && !portReplayActive();
}

int portEvtPaceTrim(struct EventEntry* event, int isFirstCall) {
    (void)isFirstCall;
    evtSetValue(event, event->args[0], portPaceTrims());
    return EVT_RETURN_DONE;
}

/* --- The Glitz Pit roster screen (tou.c's ranking, while its REL is linked) --- */

static PortTouRosterFn sTouRoster;

void portTouRosterRegister(PortTouRosterFn fn) {
    sTouRoster = fn;
}

int portTouRoster(int i, const char** name, const char** title, int* isPlayer) {
    static char sName[48], sTitle[96];
    const char* nameTag = NULL;
    const char* titleTag = NULL;
    if (sTouRoster == NULL || pouchGetPtr() == NULL || !sTouRoster(i, &nameTag, &titleTag, isPlayer)) {
        return 0;
    }
    /* the names and titles are in the ring's (tou_03) message file, not every Glitzville room's */
    if (nameTag == NULL || !mapMsg("tou_03", nameTag, sName, sizeof(sName))) {
        sName[0] = 0;
    }
    if (titleTag == NULL || !mapMsg("tou_03", titleTag, sTitle, sizeof(sTitle))) {
        sTitle[0] = 0;
    }
    *name = sName;
    *title = sTitle;
    return 1;
}

/* --- Tattle prompts: the "!" where Goombella's field Tattle has something new --- */

int portTattlePrompt(void) {
    static int sWait, sNear;
    NpcWork* wp;
    MarioWork* mario = marioGetPtr();
    int i;
    if (!portQol("talk_prompts") || marioGetParty() != PARTNER_GOOMBELLA || mario == NULL) {
        return 0;
    }
    if (++sWait < 10) {
        return sNear; /* (looked at six times a second) */
    }
    sWait = 0;
    sNear = 0;
    wp = npcGetWorkPtr();
    for (i = 0; wp != NULL && i < wp->total; i++) {
        const NpcEntry* npc = &wp->entries[i];
        float dx, dz;
        if (!(npc->flags & NPC_FLAG_ACTIVE) || (npc->flags & (NPC_FLAG_ENEMY | NPC_FLAG_HIDDEN)) ||
            npc->description[0] == '\0') {
            continue;
        }
        dx = npc->position.x - mario->position.x;
        dz = npc->position.z - mario->position.z;
        if (dx * dx + dz * dz > 60.0f * 60.0f || portTattleHeard(npc->description, 0)) {
            continue;
        }
        sNear = 1;
        break;
    }
    return sNear;
}
