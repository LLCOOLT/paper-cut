/* The second-screen companion's game side (port/include/port/companion.h):
 * the snapshot of what it shows, filled from the game's state a few times a
 * second while a presenter shows it, and the taps, which reach the game only
 * as controller 1's input. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <iconv.h>
#endif
#include "port/companion.h"
#include "port/gameplus.h"
#include "port/settings.h"
#include "port/tr.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/journal_hint.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/pad.h>
#include <demo/DEMOPad.h>
#include "battle/battle.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "mot/mot_roll.h"
#include "mot/mot_slit.h"
#include "party.h"
#include "pmario_sound.h"

void mapGetBoundingBox(Vec* min, Vec* max); /* mapdrv.c */
int portCollectiblesHere(int* pieces, int* piecesTotal, int* sprites, int* spritesTotal); /* gameplus2_pc.c */

/* every few frames: 10 a second at 60 */
#define FILL_FRAMES 6
/* the mark in controller 1's analogA that it carries a tap (the kind in the low bits) */
#define PAD_MARK 0xC0

/* --- The lock: the game thread fills, a presenter's thread may read and tap --- */

static volatile char sLock;

static void lock(void) {
    while (__atomic_test_and_set(&sLock, __ATOMIC_ACQUIRE)) {
    }
}

static void unlock(void) {
    __atomic_clear(&sLock, __ATOMIC_RELEASE);
}

static PortCompanionSnapshot sShown; /* as last filled (under the lock) */
static PortCompanionSnapshot sWork;  /* being filled (the game thread's) */
static int sActive;

int portCompanionSetting(void) {
#ifdef __ANDROID__
    /* on by default; shown on the settings page only on a device with a
     * second display of its own (TtydCompanion.java, portCompanionSetDualScreen) */
    static int* sValue;
    if (sValue == NULL) {
        sValue = portSettingsDeclare("second_screen", 1, 0, 1);
    }
    return *sValue;
#else
    /* elsewhere only for tests and development: $TTYD_COMPANION_WINDOW=1, a
     * window of its own on the desktop (no setting) */
    static int sValue = -1;
    if (sValue < 0) {
        const char* v = getenv("TTYD_COMPANION_WINDOW");
        sValue = v != NULL && atoi(v) != 0;
    }
    return sValue;
#endif
}

static volatile int sDualScreen;

void portCompanionSetDualScreen(int dual) {
    sDualScreen = dual;
}

int portCompanionDualScreen(void) {
    return sDualScreen;
}

void portCompanionSetActive(int active) {
    sActive = active;
}

int portCompanionShowing(void) {
    return sActive;
}

/* --- The game's text as UTF-8 --- */

/* a Shift-JIS lead byte: the next byte is the same character's */
static int sjisLead(unsigned char c) {
    return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

void portCompanionUtf8(const char* game, char* out, int size, int lines) {
    char raw[1024];
    int n = 0, space = 0;
    const int jp = PORT_GAME_ID[3] == 'J';
    out[0] = '\0';
    if (game == NULL || size <= 0) {
        return;
    }
    /* the tags out (a new page, <p>, a line break of its own), runs of spaces as one */
    for (; *game != '\0' && n < (int)sizeof(raw) - 3; game++) {
        const unsigned char c = (unsigned char)*game;
        if (c == '<') {
            const char* end = strchr(game, '>');
            if (end == NULL) {
                break;
            }
            if (lines && end - game == 2 && game[1] == 'p' && n > 0) {
                raw[n++] = '\n';
                space = 1;
            }
            game = end;
            continue;
        }
        if (jp && sjisLead(c) && game[1] != '\0') {
            raw[n++] = (char)c;
            raw[n++] = *++game;
            space = 0;
            continue;
        }
        if (c == '\n' && jp) { /* the message box's line breaks: Japanese runs on without a space */
            continue;
        }
        if (c == '\n' || c == ' ' || c == '\t') {
            if (!space && n > 0) {
                raw[n++] = ' ';
            }
            space = 1;
            continue;
        }
        if (c < 0x20) {
            continue;
        }
        raw[n++] = (char)c;
        space = 0;
    }
    while (n > 0 && (raw[n - 1] == ' ' || raw[n - 1] == '\n')) {
        n--;
    }
    raw[n] = '\0';
    if (!jp) {
        /* Latin-1 */
        int k = 0, i;
        for (i = 0; raw[i] != '\0' && k < size - 3; i++) {
            const unsigned char c = (unsigned char)raw[i];
            if (c < 0x80) {
                out[k++] = (char)c;
            } else {
                out[k++] = (char)(0xC0 | (c >> 6));
                out[k++] = (char)(0x80 | (c & 0x3F));
            }
        }
        out[k] = '\0';
        return;
    }
#ifdef _WIN32
    {
        WCHAR wide[1024];
        const int w = MultiByteToWideChar(932, 0, raw, -1, wide, 1024);
        if (w <= 0 || WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, size, NULL, NULL) <= 0) {
            out[0] = '\0';
        }
    }
#elif !defined(__EMSCRIPTEN__)
    {
        static iconv_t sConv = (iconv_t)0;
        char* in = raw;
        char* to = out;
        size_t inLeft = strlen(raw), outLeft = (size_t)size - 1;
        if (sConv == (iconv_t)0) {
            sConv = iconv_open("UTF-8", "SHIFT_JIS");
        }
        if (sConv == (iconv_t)-1) {
            out[0] = '\0';
            return;
        }
        iconv(sConv, NULL, NULL, NULL, NULL);
        iconv(sConv, &in, &inLeft, &to, &outLeft);
        *to = '\0';
    }
#else
    out[0] = '\0';
#endif
}

static void gameName(const char* tag, char* out, int size) {
    const char* text = tag != NULL ? msgSearch(tag) : NULL;
    portCompanionUtf8(text, out, size, 0);
}

/* --- The snapshot --- */

static int playing(void) {
    return gp != NULL && pouchGetPtr() != NULL && (seqGetSeq() == SEQ_GAME || seqGetSeq() == SEQ_BATTLE);
}

static int fieldReady(void) {
    MarioWork* mario = marioGetPtr();
    return seqGetSeq() == SEQ_GAME && mario != NULL && mario->characterId == 0 && marioStGetSystemLevel() == 0 &&
           !marioCheckMenuDisable();
}

static void fillMenu(PortCompanionSnapshot* s) {
    PouchData* p = pouchGetPtr();
    /* the partner menu's order (win_party.c winPartyDt) */
    static const int kOrder[] = {1, 2, 5, 4, 6, 3, 7};
    static const char* const kNames[8] = {NULL,          "name_party0", "name_party1", "name_party2",
                                          "name_party3", "name_party4", "name_party5", "name_party6"};
    const int out = !marioBgmodeChk() ? marioGetParty() : marioGetPtr()->prevPartyMemberId[0];
    int i, j;
    s->level = p->level;
    s->starPoints = p->starPoints;
    s->coins = p->coins;
    s->starPieces = p->starPieceCount;
    s->shineSprites = p->shineSpriteCount;
    s->fp = p->currentFP;
    s->maxFp = p->maximumFP;
    s->bp = p->availableBP;
    s->maxBp = p->maximumBP;
    s->sp = p->currentSP;
    s->maxSp = p->maximumSP;
    /* bit 0 is Sweet Treat (the Magical Map's); bits 1-7 the Crystal Stars' */
    s->crystalStars = (p->starPowersObtained >> 1) & 0x7F;
    for (i = 0; i < 7; i++) {
        gameName(itemDataTable[ITEM_DIAMOND_STAR + i].name, s->crystalNames[i], PORT_COMPANION_NAME);
    }
    gameName(itemDataTable[ITEM_COIN].name, s->coinName, PORT_COMPANION_NAME);
    gameName(itemDataTable[ITEM_STAR_PIECE].name, s->pieceName, PORT_COMPANION_NAME);
    gameName(itemDataTable[ITEM_SHINE_SPRITE].name, s->spriteName, PORT_COMPANION_NAME);
    s->memberCount = 0;
    {
        PortCompanionMember* m = &s->members[s->memberCount++];
        m->id = 0;
        m->hp = p->currentHP;
        m->maxHp = p->maximumHP;
        m->rank = 0;
        m->out = 0;
        gameName("btl_un_mario", m->name, PORT_COMPANION_NAME);
    }
    for (i = 0; i < 7; i++) {
        const int id = kOrder[i];
        const PouchPartyData* d = &p->partyData[id];
        PortCompanionMember* m;
        if (!(d->flags & POUCH_PARTY_FLAG_JOINED)) {
            continue;
        }
        m = &s->members[s->memberCount++];
        m->id = id;
        m->hp = d->currentHP;
        m->maxHp = d->maximumHP;
        m->rank = d->attackLevel;
        m->out = id == out ? 1 : (d->flags & POUCH_PARTY_FLAG_AWAY) ? -1 : 0;
        if (id == 4 && pouchGetYoshiName() != NULL && pouchGetYoshiName()[0] != '\0') {
            portCompanionUtf8(pouchGetYoshiName(), m->name, PORT_COMPANION_NAME, 0);
        } else {
            gameName(kNames[id], m->name, PORT_COMPANION_NAME);
        }
    }
    s->itemCount = 0;
    for (i = 0; i < PORT_COMPANION_ITEMS && i < MAX_HELD_ITEMS; i++) {
        const int id = p->heldItems[i];
        PortCompanionItem* it;
        if (id == ITEM_NULL) {
            break;
        }
        it = &s->items[s->itemCount++];
        it->id = id;
        it->hp = itemDataTable[id].hp_restored;
        it->fp = itemDataTable[id].fp_restored;
        it->usable = (itemDataTable[id].usable_locations & 4) != 0;
        gameName(itemDataTable[id].name, it->name, PORT_COMPANION_NAME);
    }
    s->badgeCount = s->badgeTotal = 0;
    for (i = 0; i < MAX_BADGES; i++) {
        const int id = p->equippedBadges[i];
        if (id == ITEM_NULL) {
            continue;
        }
        s->badgeTotal++;
        for (j = 0; j < s->badgeCount && s->badges[j].id != id; j++) {
        }
        if (j < s->badgeCount) {
            s->badges[j].count++;
        } else if (s->badgeCount < PORT_COMPANION_BADGES) {
            PortCompanionBadge* b = &s->badges[s->badgeCount++];
            b->id = id;
            b->count = 1;
            b->bp = itemDataTable[id].bp_cost;
            gameName(itemDataTable[id].name, b->name, PORT_COMPANION_NAME);
        }
    }
}

static void fillMap(PortCompanionSnapshot* s) {
    char tag[48];
    Vec mn, mx;
    MarioWork* mario = marioGetPtr();
    snprintf(tag, sizeof(tag), "menu_monosiri_%s", gp->currentAreaName);
    gameName(tag, s->area, PORT_COMPANION_NAME);
    snprintf(s->map, sizeof(s->map), "%s", gp->currentMapName);
    s->hint[0] = '\0';
#ifdef TTYD_BACKEND_AURORA
    {
        const char* area;
        const char* name;
        if (portJournalNextStep(&area, &name)) {
            char where[PORT_COMPANION_NAME];
            snprintf(tag, sizeof(tag), "menu_monosiri_%s", area);
            gameName(tag, where, sizeof(where));
            if (portTrLanguage() == 0) { /* the step's name is English only */
                snprintf(s->hint, sizeof(s->hint), "%s (%s)", where, name);
            } else {
                snprintf(s->hint, sizeof(s->hint), "%s", where);
            }
        }
    }
#endif
    portCollectiblesHere(&s->pieces, &s->piecesTotal, &s->sprites, &s->spritesTotal);
    s->dotCount = 0;
    s->boxMin[0] = s->boxMin[1] = s->boxMax[0] = s->boxMax[1] = 0.0f;
    if (seqGetSeq() != SEQ_GAME || mario == NULL) {
        return;
    }
    mapGetBoundingBox(&mn, &mx);
    s->boxMin[0] = mn.x;
    s->boxMin[1] = mn.z;
    s->boxMax[0] = mx.x;
    s->boxMax[1] = mx.z;
    {
        NpcWork* w = npcGetWorkPtr();
        int i;
        for (i = 0; w != NULL && i < w->total && s->dotCount < PORT_COMPANION_DOTS - 2; i++) {
            const NpcEntry* e = &w->entries[i];
            PortCompanionDot* d;
            /* those parked below the map (out of the scene) left out */
            if ((e->flags & (NPC_FLAG_USED | NPC_FLAG_ACTIVE)) != (NPC_FLAG_USED | NPC_FLAG_ACTIVE) ||
                (e->flags & NPC_FLAG_HIDDEN) || e->position.y < mn.y - 50.0f) {
                continue;
            }
            d = &s->dots[s->dotCount++];
            d->x = e->position.x;
            d->z = e->position.z;
            d->kind = (e->flags & NPC_FLAG_ENEMY) ? 3 : 2;
        }
    }
    if (marioGetPartyId() >= 0 && marioGetParty() > 0) {
        PartyEntry* party = partyGetPtr(marioGetPartyId());
        if (party != NULL) {
            PortCompanionDot* d = &s->dots[s->dotCount++];
            d->x = party->position.x;
            d->z = party->position.z;
            d->kind = 1;
        }
    }
    {
        PortCompanionDot* d = &s->dots[s->dotCount++];
        d->x = mario->position.x;
        d->z = mario->position.z;
        d->kind = 0;
    }
}

/* the kind tattled (the Tattle Log's flag, or this battle's), not counting Peekaboo */
static int tattled(BattleWork* wp, const BattleUnit* unit) {
    int kind = unit->currentKind;
    int flag = swGet(kind + GSWF_TATTLE_BASE) || (wp->tattledUnitTypeFlags[kind / 32] & (1u << (kind % 32)));
    if (unit->flags & UNIT_FLAG_KIND_CHANGED) {
        kind = unit->initialKind;
        flag |= (wp->tattledUnitTypeFlags[kind / 32] & (1u << (kind % 32))) != 0;
    }
    return flag;
}

static void fillBattle(PortCompanionSnapshot* s) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    s->enemyCount = s->orderCount = 0;
    s->orderActive = -1;
    s->turn = 0;
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL) {
        return;
    }
    s->turn = wp->turnCount;
    for (i = 0; i < BATTLE_UNIT_MAX && s->enemyCount < PORT_COMPANION_ENEMIES; i++) {
        BattleUnit* unit = wp->units[i];
        const TattleEntry* t;
        PortCompanionEnemy* e;
        if (unit == NULL || unit->alliance != 1 || unit->kindParams == NULL ||
            (unit->attributeFlags & (UNIT_ATTR_HIDDEN | UNIT_ATTR_NOT_SAMPLED_AS_TARGET))) {
            continue;
        }
        t = battleGetUnitMonosiriPtr(unit->currentKind);
        if (t == NULL || t->tattleBtlTag == NULL) { /* a part of another (a boss's arm) */
            continue;
        }
        e = &s->enemies[s->enemyCount++];
        e->unitId = unit->unitId;
        e->known = tattled(wp, unit);
        e->hpShown = e->known || (wp->badgeEquippedFlags & BADGE_FLAG_PEEKABOO);
        e->hp = unit->currentHp;
        e->maxHp = unit->maxHp;
        e->defense = unit->parts != NULL && unit->parts->defense != NULL ? unit->parts->defense->defenses[0] : 0;
        e->acting = unit->unitId == wp->activeUnitId;
        gameName(t->enemyTag, e->name, PORT_COMPANION_NAME);
        e->text[0] = '\0';
        if (e->known) {
            portCompanionUtf8(msgSearch(t->tattleLogTag != NULL ? t->tattleLogTag : t->tattleBtlTag), e->text,
                              PORT_COMPANION_TEXT, 1);
        }
    }
    for (i = 0; i < BATTLE_UNIT_MAX && s->orderCount < PORT_COMPANION_ORDER; i++) {
        const int id = wp->movePriorityQueue[i];
        BattleUnit* unit;
        if (id == -1) {
            continue;
        }
        unit = BattleGetUnitPtr(wp, id);
        if (unit == NULL || unit->kindParams == NULL) {
            continue;
        }
        if (id == wp->activeUnitId) {
            s->orderActive = s->orderCount;
        }
        gameName(unit->kindParams->name, s->order[s->orderCount++], PORT_COMPANION_NAME);
    }
}

static void fill(void) {
    PortCompanionSnapshot* s = &sWork;
    const unsigned serial = s->serial + 1;
    memset(s, 0, sizeof(*s));
    s->serial = serial;
    if (playing()) {
        s->scene = seqGetSeq() == SEQ_BATTLE ? 2 : 1;
        s->fieldReady = fieldReady();
        fillMenu(s);
        fillMap(s);
        fillBattle(s);
    }
    lock();
    sShown = *s;
    unlock();
}

void portCompanionFrame(void) {
    static int sClock;
    if (!sActive) {
        return;
    }
    if (++sClock >= FILL_FRAMES) {
        static int sDump = -1;
        sClock = 0;
        fill();
        if (sDump < 0) {
            sDump = getenv("TTYD_COMPANION_DUMP") != NULL;
        }
        if (sDump && sWork.serial % 50 == 1) { /* tests: the snapshot in the log now and then */
            static char sText[16384];
            portCompanionText(sText, sizeof(sText));
            printf("[companion] snapshot\n%s[companion] end\n", sText);
            fflush(stdout);
        }
    }
}

void portCompanionCopy(PortCompanionSnapshot* out) {
    lock();
    *out = sShown;
    unlock();
}

/* a text field without the separators (tabs, line ends; a line end in the
 * Tattle text becomes 0x1F) */
static int field(char* out, int size, const char* text) {
    int n = 0;
    for (; *text != '\0' && n < size - 1; text++) {
        const char c = *text;
        out[n++] = c == '\t' ? ' ' : c == '\n' ? '\x1F' : c;
    }
    out[n] = '\0';
    return n;
}

int portCompanionText(char* out, int size) {
    static PortCompanionSnapshot s;
    char a[PORT_COMPANION_TEXT], b[PORT_COMPANION_TEXT];
    int n = 0, i;
#define PUT(...)                                                                \
    do {                                                                        \
        if (n < size) {                                                         \
            const int k = snprintf(out + n, (size_t)(size - n), __VA_ARGS__); \
            n += k > 0 ? k : 0;                                                 \
        }                                                                       \
    } while (0)
    portCompanionCopy(&s);
    PUT("scene\t%d\t%d\t%u\n", s.scene, s.fieldReady, s.serial);
    PUT("stats\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", s.level, s.starPoints, s.coins, s.starPieces,
        s.shineSprites, s.fp, s.maxFp, s.bp, s.maxBp, s.sp, s.maxSp, s.crystalStars);
    field(a, sizeof(a), s.coinName);
    field(b, sizeof(b), s.pieceName);
    PUT("names\t%s\t%s\t", a, b);
    field(a, sizeof(a), s.spriteName);
    PUT("%s\n", a);
    for (i = 0; i < 7; i++) {
        field(a, sizeof(a), s.crystalNames[i]);
        PUT("crystal\t%s\n", a);
    }
    for (i = 0; i < s.memberCount; i++) {
        const PortCompanionMember* m = &s.members[i];
        field(a, sizeof(a), m->name);
        PUT("member\t%d\t%d\t%d\t%d\t%d\t%s\n", m->id, m->hp, m->maxHp, m->rank, m->out, a);
    }
    for (i = 0; i < s.itemCount; i++) {
        const PortCompanionItem* it = &s.items[i];
        field(a, sizeof(a), it->name);
        PUT("item\t%d\t%d\t%d\t%d\t%d\t%s\n", i, it->id, it->hp, it->fp, it->usable, a);
    }
    PUT("badges\t%d\n", s.badgeTotal);
    for (i = 0; i < s.badgeCount; i++) {
        field(a, sizeof(a), s.badges[i].name);
        PUT("badge\t%d\t%d\t%s\n", s.badges[i].count, s.badges[i].bp, a);
    }
    field(a, sizeof(a), s.area);
    field(b, sizeof(b), s.hint);
    PUT("map\t%s\t%s\t%s\n", s.map, a, b);
    PUT("collect\t%d\t%d\t%d\t%d\n", s.pieces, s.piecesTotal, s.sprites, s.spritesTotal);
    PUT("box\t%.1f\t%.1f\t%.1f\t%.1f\n", s.boxMin[0], s.boxMin[1], s.boxMax[0], s.boxMax[1]);
    for (i = 0; i < s.dotCount; i++) {
        PUT("dot\t%d\t%.1f\t%.1f\n", s.dots[i].kind, s.dots[i].x, s.dots[i].z);
    }
    PUT("turn\t%d\n", s.turn);
    for (i = 0; i < s.enemyCount; i++) {
        const PortCompanionEnemy* e = &s.enemies[i];
        field(a, sizeof(a), e->name);
        field(b, sizeof(b), e->text);
        PUT("enemy\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\n", e->known, e->hpShown, e->hp, e->maxHp, e->defense, e->acting, a,
            b);
    }
    for (i = 0; i < s.orderCount; i++) {
        field(a, sizeof(a), s.order[i]);
        PUT("order\t%d\t%s\n", i == s.orderActive, a);
    }
#undef PUT
    return n < size ? n : size - 1;
}

/* --- Taps: queued, then one a frame into controller 1's read --- */

#define QUEUE 8
static struct {
    unsigned char kind, arg, target;
} sQueue[QUEUE];
static int sQueueHead, sQueueCount;

void portCompanionTap(int kind, int arg, int target) {
    lock();
    if (sQueueCount < QUEUE && kind > 0 && kind < 16 && arg >= 0 && arg < 32) {
        const int at = (sQueueHead + sQueueCount++) % QUEUE;
        sQueue[at].kind = (unsigned char)kind;
        sQueue[at].arg = (unsigned char)arg;
        sQueue[at].target = (unsigned char)(kind == PORT_COMPANION_TAP_ITEM_ON ? target & 7 : target != 0);
    }
    unlock();
}

void portCompanionApplyPad(PADStatus* pad) {
    /* analogA and analogB are never the game's: kept clear but for a tap */
    pad[0].analogA = 0;
    pad[0].analogB = 0;
    lock();
    if (sQueueCount > 0) {
        pad[0].analogA = (u8)(PAD_MARK | sQueue[sQueueHead].kind);
        pad[0].analogB = (u8)(sQueue[sQueueHead].arg | (sQueue[sQueueHead].target << 5));
        sQueueHead = (sQueueHead + 1) % QUEUE;
        sQueueCount--;
    }
    unlock();
}

/* this frame's tap, as the game read it */
static int sTapKind, sTapArg, sTapTarget;

void portCompanionRoute(void) {
    const u8 a = DemoPad[0].pst.analogA;
    const u8 b = DemoPad[0].pst.analogB;
    sTapKind = sTapArg = sTapTarget = 0;
    if ((a & 0xF0) == PAD_MARK) {
        sTapKind = a & 0x0F;
        sTapArg = b & 0x1F;
        sTapTarget = (b >> 5) & 7;
        printf("[companion] tap %d %d %d\n", sTapKind, sTapArg, sTapTarget);
        fflush(stdout);
    }
}

int portCompanionPartner(void) {
    if (sTapKind != PORT_COMPANION_TAP_PARTNER) {
        return 0;
    }
    sTapKind = 0;
    return sTapArg;
}

/* An item on a partner who isn't out, as the Start menu's item list uses one
 * (win_item.c, states 300-301: at once, without the scene). */
static int companionItemOn(int idx, int partyId) {
    PouchData* p = pouchGetPtr();
    int id;
    if (idx >= MAX_HELD_ITEMS || (id = p->heldItems[idx]) == ITEM_NULL || !(itemDataTable[id].usable_locations & 4) ||
        partyId < 1 || partyId > 7 || !(p->partyData[partyId].flags & POUCH_PARTY_FLAG_JOINED)) {
        return 0;
    }
    psndSFXOn(SFX_NAME_WORD(0x20012));
    pouchSetPartyHP(partyId, pouchGetPartyHP((PartyId)partyId) + itemDataTable[id].hp_restored);
    portHealOtherPartners(partyId, itemDataTable[id].hp_restored); /* Enhancements */
    pouchSetFP((s16)(pouchGetFP() + itemDataTable[id].fp_restored));
    pouchSetAP((s16)(pouchGetAP() + itemDataTable[id].sp_restored));
    pouchRemoveItemIndex((ItemType)id, idx);
    printf("[companion] item %d (slot %d) used on partner %d\n", id, idx, partyId);
    fflush(stdout);
    return 1;
}

int portCompanionFieldItem(void) {
    MarioWork* mp = marioGetPtr();
    int idx, id, party;
    if (sTapKind != PORT_COMPANION_TAP_ITEM && sTapKind != PORT_COMPANION_TAP_ITEM_ON) {
        return 0;
    }
    idx = sTapArg;
    if (sTapKind == PORT_COMPANION_TAP_ITEM_ON) {
        /* a partner named (the second screen's Start menu): as the partner out when it's that one */
        party = !marioBgmodeChk() ? marioGetParty() : mp->prevPartyMemberId[0];
        sTapKind = 0;
        if (sTapTarget != 0 && sTapTarget != party) {
            return companionItemOn(idx, sTapTarget);
        }
        sTapTarget = sTapTarget != 0;
    }
    sTapKind = 0;
    if (idx >= MAX_HELD_ITEMS || (id = pouchGetPtr()->heldItems[idx]) == ITEM_NULL ||
        !(itemDataTable[id].usable_locations & 4)) {
        return 0;
    }
    /* as the Start menu's item list (win_item.c, state 300): not while
     * sailing, gliding, rolling or slipping through a gap; the partner only
     * when one is out */
    party = !marioBgmodeChk() ? marioGetParty() : mp->prevPartyMemberId[0];
    if (mp->currMotionId == MARIO_MOTION_SHIP || mp->currMotionId == MARIO_MOTION_PLANE || marioRollCancelDisable() ||
        U_marioSlitContinueChk() || (sTapTarget && (marioBgmodeChk() || party <= 0))) {
        return 0;
    }
    psndSFXOn(SFX_NAME_WORD(0x20012));
    if (!sTapTarget) {
        pouchSetHP((s16)(pouchGetHP() + itemDataTable[id].hp_restored));
    } else {
        pouchSetPartyHP(party, pouchGetPartyHP((PartyId)party) + itemDataTable[id].hp_restored);
        portHealOtherPartners(party, itemDataTable[id].hp_restored); /* Enhancements */
    }
    pouchSetFP((s16)(pouchGetFP() + itemDataTable[id].fp_restored));
    pouchSetAP((s16)(pouchGetAP() + itemDataTable[id].sp_restored));
    pouchRemoveItemIndex((ItemType)id, idx);
    printf("[companion] item %d (slot %d) used on %s\n", id, idx, sTapTarget ? "the partner" : "Mario");
    fflush(stdout);
    return 1;
}
