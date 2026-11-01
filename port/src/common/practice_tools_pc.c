/* The Practice tab's tools that read and change the game
 * (port/include/port/practice_tools.h): the saved words and flags, a memory
 * watch over Mario, the party and the pouch, and stats and items. The page
 * is C++ without the game's headers; it reaches the game through these.
 * Every change marks the run (portPracticeTaint): no achievements from it. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "party.h"
#include "port/practice.h"
#include "port/practice_tools.h"

int portPracticeInGame(void) {
    const SequenceType seq = seqGetSeq();
    return gp != NULL && pouchGetPtr() != NULL && marioGetPtr() != NULL &&
           (seq == SEQ_GAME || seq == SEQ_BATTLE || seq == SEQ_MAP_CHANGE || seq == SEQ_GAME_OVER);
}

/* --- GSW / GSWF ---------------------------------------------------------------- */

int portPracticeGsw(int index) {
    return gp != NULL && index >= 0 && index < 0x800 ? swByteGet(index) : 0;
}

void portPracticeSetGsw(int index, int value) {
    if (gp != NULL && index >= 0 && index < 0x800) {
        swByteSet(index, value);
        portPracticeTaint();
    }
}

int portPracticeGswf(int index) {
    return gp != NULL && index >= 0 && index < 0x100 * 32 ? (swGet(index) ? 1 : 0) : 0;
}

void portPracticeSetGswf(int index, int on) {
    if (gp != NULL && index >= 0 && index < 0x100 * 32) {
        if (on) {
            swSet(index);
        } else {
            swClear(index);
        }
        portPracticeTaint();
    }
}

/* --- memory watch -------------------------------------------------------------- */

enum { B_MARIO, B_PARTY, B_FOLLOWER, B_POUCH, B_GAME };
enum { T_S8, T_U8, T_S16, T_U16, T_S32, T_HEX, T_F32, T_VEC };

typedef struct Watch {
    const char* name;
    int base, type;
    size_t offset;
} Watch;

#define MARIO(f, t) {"mario." #f, B_MARIO, t, offsetof(MarioWork, f)}
#define PARTY(f, t) {"party." #f, B_PARTY, t, offsetof(PartyEntry, f)}
#define FOLLOWER(f, t) {"follower." #f, B_FOLLOWER, t, offsetof(PartyEntry, f)}
#define POUCH(f, t) {"pouch." #f, B_POUCH, t, offsetof(PouchData, f)}
#define GAME(f, t) {"gp." #f, B_GAME, t, offsetof(GlobalWork, f)}

static const Watch kWatches[] = {
    MARIO(position, T_VEC),
    MARIO(flags, T_HEX),
    MARIO(dispFlags, T_HEX),
    MARIO(trigFlags, T_HEX),
    MARIO(currMotionId, T_U16),
    MARIO(prevMotionId, T_U16),
    MARIO(characterId, T_S8),
    MARIO(frameCounter, T_HEX),
    MARIO(jumpVelocityY, T_F32),
    MARIO(baseSpeed, T_F32),
    MARIO(baseWalkSpeed, T_F32),
    MARIO(baseDashSpeed, T_F32),
    MARIO(moveDist, T_F32),
    MARIO(controlStickAngle, T_F32),
    MARIO(targetYaw, T_F32),
    MARIO(moveDirection, T_F32),
    MARIO(displayDirection, T_F32),
    MARIO(groundY, T_F32),
    MARIO(lastGroundPos, T_VEC),
    MARIO(jumpPeakY, T_F32),
    MARIO(playerGravity, T_F32),
    PARTY(position, T_VEC),
    PARTY(flags, T_HEX),
    PARTY(currentMemberId, T_S8),
    PARTY(motionId, T_U8),
    PARTY(moveMode, T_U8),
    PARTY(targetSpeed, T_F32),
    FOLLOWER(position, T_VEC),
    FOLLOWER(currentMemberId, T_S8),
    POUCH(currentHP, T_S16),
    POUCH(maximumHP, T_S16),
    POUCH(currentFP, T_S16),
    POUCH(maximumFP, T_S16),
    POUCH(coins, T_S16),
    POUCH(currentSP, T_S16),
    POUCH(maximumSP, T_S16),
    POUCH(level, T_S16),
    POUCH(rank, T_S16),
    POUCH(starPoints, T_S16),
    POUCH(availableBP, T_S16),
    POUCH(maximumBP, T_S16),
    POUCH(starPieceCount, T_S16),
    POUCH(shineSpriteCount, T_S16),
    POUCH(audienceCount, T_F32),
    POUCH(jumpLevel, T_S8),
    POUCH(hammerLevel, T_S8),
    POUCH(shopPoints, T_S16),
    GAME(inBattle, T_S32),
    GAME(currentMapName, -1),
    GAME(currentAreaName, -1),
};
#define WATCH_COUNT ((int)(sizeof(kWatches) / sizeof(kWatches[0])))

int portWatchCount(void) {
    return WATCH_COUNT;
}

const char* portWatchName(int i) {
    return i >= 0 && i < WATCH_COUNT ? kWatches[i].name : "";
}

static const unsigned char* watchBase(int base) {
    s32 id;
    switch (base) {
    case B_MARIO:
        return (const unsigned char*)marioGetPtr();
    case B_PARTY:
    case B_FOLLOWER:
        id = base == B_PARTY ? marioGetPartyId() : marioGetExtraPartyId();
        return id >= 0 ? (const unsigned char*)partyGetPtr(id) : NULL;
    case B_POUCH:
        return (const unsigned char*)pouchGetPtr();
    default:
        return (const unsigned char*)gp;
    }
}

int portWatchRead(int i, char* out, int size) {
    const Watch* w;
    const unsigned char* p;
    if (i < 0 || i >= WATCH_COUNT || !portPracticeInGame()) {
        snprintf(out, (size_t)size, "-");
        return 0;
    }
    w = &kWatches[i];
    p = watchBase(w->base);
    if (p == NULL) {
        snprintf(out, (size_t)size, "(none)");
        return 0;
    }
    p += w->offset;
    switch (w->type) {
    case T_S8: snprintf(out, (size_t)size, "%d", *(const s8*)p); break;
    case T_U8: snprintf(out, (size_t)size, "%u", *(const u8*)p); break;
    case T_S16: snprintf(out, (size_t)size, "%d", *(const s16*)p); break;
    case T_U16: snprintf(out, (size_t)size, "%u", *(const u16*)p); break;
    case T_S32: snprintf(out, (size_t)size, "%d", (int)*(const s32*)p); break;
    case T_HEX: snprintf(out, (size_t)size, "0x%08X", (unsigned)*(const u32*)p); break;
    case T_F32: snprintf(out, (size_t)size, "%.3f", *(const f32*)p); break;
    case T_VEC: {
        const Vec* v = (const Vec*)p;
        snprintf(out, (size_t)size, "%.2f, %.2f, %.2f", v->x, v->y, v->z);
        break;
    }
    default: snprintf(out, (size_t)size, "%.32s", (const char*)p); break;
    }
    return 1;
}

/* --- stats and items ----------------------------------------------------------- */

static s16* statField(PouchData* p, int which) {
    switch (which) {
    case PORT_STAT_COINS: return &p->coins;
    case PORT_STAT_HP: return &p->currentHP;
    case PORT_STAT_MAX_HP: return &p->baseMaxHP;
    case PORT_STAT_FP: return &p->currentFP;
    case PORT_STAT_MAX_FP: return &p->baseMaxFP;
    case PORT_STAT_BP: return &p->maximumBP;
    case PORT_STAT_SP: return &p->currentSP;
    case PORT_STAT_MAX_SP: return &p->maximumSP;
    case PORT_STAT_LEVEL: return &p->level;
    case PORT_STAT_STAR_POINTS: return &p->starPoints;
    case PORT_STAT_STAR_PIECES: return &p->starPieceCount;
    case PORT_STAT_SHINE_SPRITES: return &p->shineSpriteCount;
    default: return NULL;
    }
}

int portPracticeStat(int which) {
    s16* f = portPracticeInGame() ? statField(pouchGetPtr(), which) : NULL;
    return f != NULL ? *f : 0;
}

void portPracticeSetStat(int which, int value) {
    PouchData* p;
    s16* f;
    if (!portPracticeInGame() || (f = statField(p = pouchGetPtr(), which)) == NULL) {
        return;
    }
    *f = (s16)value;
    if (which == PORT_STAT_MAX_HP || which == PORT_STAT_MAX_FP || which == PORT_STAT_BP) {
        pouchReviseMarioParam(); /* the maxima with their badges, BP left */
    }
    if (p->currentHP > p->maximumHP) {
        p->currentHP = p->maximumHP;
    }
    if (p->currentFP > p->maximumFP) {
        p->currentFP = p->maximumFP;
    }
    portPracticeTaint();
}

/* the item's name in the game's text: Latin-1 made UTF-8 for the page (the
 * Japanese release's Shift-JIS: the message tag instead) */
const char* portPracticeItemName(int id, char* out, int size) {
    const char* text;
    int n = 0;
    if (id <= 0 || id >= BADGE_MAX || gp == NULL || itemDataTable[id].name == NULL) {
        snprintf(out, (size_t)size, "?");
        return out;
    }
    text = msgSearch(itemDataTable[id].name);
    if (text == NULL || PORT_GAME_ID[3] == 'J') {
        snprintf(out, (size_t)size, "%s", itemDataTable[id].name);
        return out;
    }
    for (; *text != '\0' && n < size - 3; text++) {
        const unsigned char c = (unsigned char)*text;
        if (c == '<') { /* a control tag */
            while (*text != '\0' && *text != '>') {
                text++;
            }
            if (*text == '\0') {
                break;
            }
            continue;
        }
        if (c < 0x80) {
            out[n++] = (char)c;
        } else {
            out[n++] = (char)(0xC0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = '\0';
    return out;
}

int portPracticeItemFirst(int kind) {
    return kind == 0 ? ITEM_MIN : kind == 1 ? BADGE_MIN : KEY_ITEM_MIN;
}

int portPracticeItemEnd(int kind) {
    return kind == 0 ? ITEM_MAX : kind == 1 ? BADGE_MAX : KEY_ITEM_MAX;
}

int portPracticeGiveItem(int id) {
    if (!portPracticeInGame() || id <= 0 || id >= BADGE_MAX) {
        return 0;
    }
    portPracticeTaint();
    return pouchGetItem((ItemType)id) ? 1 : 0;
}

int portPracticeHeldItem(int i) {
    return portPracticeInGame() && i >= 0 && i < MAX_HELD_ITEMS ? pouchGetPtr()->heldItems[i] : 0;
}

void portPracticeRemoveItem(int id) {
    if (portPracticeInGame() && id > 0) {
        pouchRemoveItem((ItemType)id);
        portPracticeTaint();
    }
}
