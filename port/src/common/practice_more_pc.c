/* More of the Practice tab's powers (port/include/port/practice_more.h):
 * badges given and worn in battle (#13), the partners' HP and rank (#3) and a
 * warp to any room (#4). Each change marks the run as practice. */
#include <stdio.h>
#include <string.h>
#include "port/practice_more.h"
#include "port/practice.h"
#include "port/practice_tools.h"
#include "port/area_maps.h"
#include "port/online.h"
#include "port/replay.h"
#include "port/statesync.h"
#include "port/warp_map.h"
#include <dolphin/vi.h>
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "driver/seqdrv.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "version.h"

/* --- #13: a badge given and worn --- */

int portPracticeGiveBadgeWorn(int id) {
    BattleWork* wp = _battleWorkPointer;
    if (!portPracticeCanEdit() || id < BADGE_MIN || id >= BADGE_MAX || !portPracticeItemListed(id)) {
        return 0;
    }
    if (!pouchGetItem((ItemType)id)) {
        return 0;
    }
    pouchEquipBadgeID(id);
    pouchReviseMarioParam();
    pouchRevisePartyParam();
    if (seqGetSeq() == SEQ_BATTLE && wp != NULL) {
        /* the battle counts what its units wear at its start: counted again */
        BattleUnit* mario = BattleGetMarioPtr(wp);
        BattleUnit* party = BattleGetPartyPtr(wp);
        if (mario != NULL) {
            BtlUnit_EquipItem(mario, EQUIP_MARIO | EQUIP_RESET, ITEM_NULL);
        }
        if (party != NULL) {
            /* the partner's re-count takes its HP from the pouch (BtlUnit_ReviseHpFp), which a battle
             * only fills at a swap or its end: the unit's HP kept across it, as Mario's is */
            const s16 hp = party->currentHp;
            BtlUnit_EquipItem(party, EQUIP_PARTNER | EQUIP_RESET, ITEM_NULL);
            party->currentHp = hp > party->maxHp ? party->maxHp : hp;
        }
    }
    portPracticeTaint();
    printf("[practice] badge %d given and worn\n", id);
    fflush(stdout);
    return 1;
}

/* --- #3: the partners --- */

/* the battle unit kinds' partner ids (UNIT_GOOMBELLA ... UNIT_MS_MOWZ) */
static const int kUnitPartner[7] = {1, 2, 4, 5, 6, 3, 7};

static BattleUnit* battlePartner(int partner) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u;
    int k;
    if (seqGetSeq() != SEQ_BATTLE || wp == NULL || (u = BattleGetPartyPtr(wp)) == NULL) {
        return NULL;
    }
    k = (int)u->currentKind - TYPE_PARTNER_MIN;
    return k >= 0 && k < 7 && kUnitPartner[k] == partner ? u : NULL;
}

int portPracticeParty(int partner, int field) {
    PouchData* p;
    PouchPartyData* d;
    BattleUnit* u;
    if (!portPracticeInGame() || partner < 1 || partner > 7) {
        return -1;
    }
    p = pouchGetPtr();
    d = &p->partyData[partner];
    if (!(d->flags & POUCH_PARTY_FLAG_JOINED)) {
        return -1;
    }
    u = battlePartner(partner);
    switch (field) {
        case PORT_PARTY_HP: return u != NULL ? u->currentHp : d->currentHP;
        case PORT_PARTY_MAX_HP: return u != NULL ? u->maxHp : d->maximumHP;
        case PORT_PARTY_RANK: return d->attackLevel;
    }
    return -1;
}

void portPracticeSetParty(int partner, int field, int value) {
    PouchPartyData* d;
    BattleUnit* u;
    if (portPracticeParty(partner, field) < 0 || !portPracticeCanEdit()) {
        return;
    }
    d = &pouchGetPtr()->partyData[partner];
    u = battlePartner(partner);
    switch (field) {
        case PORT_PARTY_HP:
            if (value < 0) {
                value = 0;
            }
            if (value > d->maximumHP) {
                value = d->maximumHP;
            }
            d->currentHP = (s16)value;
            if (u != NULL) {
                u->currentHp = (s16)(value > u->maxHp ? u->maxHp : value);
            }
            break;
        case PORT_PARTY_MAX_HP:
            if (value < 1) {
                value = 1;
            }
            if (value > 999) {
                value = 999;
            }
            d->maximumHP = (s16)value;
            d->baseMaxHP = (s16)value;
            if (d->currentHP > value) {
                d->currentHP = (s16)value;
            }
            if (u != NULL) {
                u->maxHp = (s16)value;
                if (u->currentHp > value) {
                    u->currentHp = (s16)value;
                }
            }
            break;
        case PORT_PARTY_RANK:
            if (value < 0) {
                value = 0;
            }
            if (value > 2) {
                value = 2;
            }
            /* as Merlon's rank-up does (evt_win.c unitwin_setpartylank) */
            d->HPLevel = d->attackLevel = d->techLevel = (s16)value;
            pouchRevisePartyParam();
            d->currentHP = d->maximumHP;
            if (u != NULL) {
                u->maxHp = d->maximumHP;
                u->currentHp = d->maximumHP;
            }
            break;
    }
    if (u != NULL) {
        /* the partner in this battle: the HUD shows its unit's HP, and the battle's end writes the unit's back
         * over the pouch (BtlUnit_SetParamToPouch); Danger and Peril as the new HP has them */
        BtlUnit_CheckPinchStatus(u, TRUE);
        d->currentHP = u->currentHp;
    }
    printf("[practice] partner %d %s set to %d%s\n", partner,
           field == PORT_PARTY_HP ? "hp" : field == PORT_PARTY_MAX_HP ? "maxhp" : "rank", value,
           u != NULL ? " (its battle unit too)" : "");
    fflush(stdout);
    portPracticeTaint();
}

const char* portPracticePartyName(int partner, char* out, int size) {
    char tag[16];
    const char* text;
    int n = 0;
    snprintf(tag, sizeof(tag), "name_party%d", partner - 1);
    text = partner >= 1 && partner <= 7 ? msgSearch(tag) : NULL;
    if (text == NULL || PORT_GAME_ID[3] == 'J') {
        static const char* const kNames[8] = {"", "Goombella", "Koops", "Bobbery", "Yoshi", "Flurrie", "Vivian", "Ms. Mowz"};
        snprintf(out, (size_t)size, "%s", partner >= 1 && partner <= 7 ? kNames[partner] : "?");
        return out;
    }
    /* the game's text is Latin-1: made UTF-8 for the page */
    for (; *text != '\0' && n < size - 3; text++) {
        const unsigned char c = (unsigned char)*text;
        if (c == '<') {
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

/* --- #4: a warp to any room --- */

int portRoomWarpCount(void) {
    return portAreaRoomCount;
}

const char* portRoomWarpMap(int i) {
    return i >= 0 && i < portAreaRoomCount ? portAreaRooms[i].map : "";
}

const char* portRoomWarpArea(int i) {
    const char* name;
    if (i < 0 || i >= portAreaRoomCount) {
        return "";
    }
    name = portAreaName(portAreaRooms[i].area);
    return name != NULL ? name : "";
}

int portRoomWarpEntrances(const char* map, const char** out, int max) {
    int i, n = 0;
    for (i = 0; i < portAreaExitCount && n < max; i++) {
        const PortAreaExit* e = &portAreaExits[i];
        if (e->room >= 0 && e->room < portAreaRoomCount && strcmp(portAreaRooms[e->room].map, map) == 0 &&
            e->hit != NULL) {
            out[n++] = e->hit;
        }
    }
    return n;
}

static char sWarpMap[16], sWarpEntrance[32];
static int sWarping;

int portRoomWarpRequest(const char* map, const char* entrance) {
    if (!portPracticeCanEdit() || seqGetSeq() != SEQ_GAME || map == NULL || map[0] == '\0') {
        return 0;
    }
    if (portPracticeModeRefusal() != NULL) {
        /* (a mode's own rooms: a Tower climb or the Pit in co-op would end on the new room, a lent pouch
           left behind; the mode is left first, from the Start menu) */
        printf("[practice] warp refused: a mode runs\n");
        fflush(stdout);
        return 0;
    }
    snprintf(sWarpMap, sizeof(sWarpMap), "%s", map);
    /* a room the area maps keep by six letters whose map has a part letter (rsh_03 is rsh_03_a ...) */
    if (strncmp(sWarpMap, "rsh_", 4) == 0 && strlen(sWarpMap) == 6) {
        snprintf(sWarpMap + 6, sizeof(sWarpMap) - 6, "_a");
    }
    /* the safe spawn: the room's first way in, where the game itself puts Mario
     * (the map's origin, with no entrance, is inside the Glitzville ticket booth
     * or over Petal Meadows' void); the origin only for a room with none */
    if (entrance == NULL) {
        const char* ways[1];
        if (portRoomWarpEntrances(map, ways, 1) > 0) {
            entrance = ways[0];
        }
    }
    snprintf(sWarpEntrance, sizeof(sWarpEntrance), "%s", entrance != NULL ? entrance : "");
    sWarping = 1;
    return 1;
}

void portRoomWarpFrame(void) {
    if (!sWarping) {
        return;
    }
    if (seqGetSeq() != SEQ_GAME) {
        sWarping = 0; /* a battle or the title: dropped */
        return;
    }
    if (seqCheckSeq() || marioStGetSystemLevel() != 0) {
        return; /* a scene or a menu: wait for it */
    }
    sWarping = 0;
    portPracticeTaint();
    printf("[practice] warp: %s %s\n", sWarpMap, sWarpEntrance[0] != '\0' ? sWarpEntrance : "(safe spawn)");
    fflush(stdout);
    seqSetSeq(SEQ_MAP_CHANGE, sWarpMap, sWarpEntrance[0] != '\0' ? sWarpEntrance : NULL);
}

/* the full settings page's Map (port/include/port/warp_map.h): the Start menu opens on the map
 * once the page is closed; a request nobody took in ten seconds is dropped */
static int sMapAsked;
static u32 sMapAskedAt;

void portWarpMapRequest(void) {
    sMapAsked = 1;
    sMapAskedAt = VIGetRetraceCount();
}

int portWarpMapRequested(void) {
    const int asked = sMapAsked && VIGetRetraceCount() - sMapAskedAt < 600;
    sMapAsked = 0;
    return asked;
}
