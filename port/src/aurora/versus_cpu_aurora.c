/* The computer's side in a versus battle (port/include/port/versus.h,
 * docs/VERSUS.md): Luigi and his partners played by the computer, at a skill
 * of easy, normal or hard.
 *
 * Its commands go through the game's own command menu: it looks at what the
 * menu would offer (Mario's jumps and hammers with the badges' moves, the
 * partner's moves, the items), weighs each by the damage it would do to the
 * other side (the weapon's own power less the target's defence, a win or a
 * knock-out worth more) against its FP, heals when HP run low, swaps in
 * another partner when its own is down, and Defends when nothing else will
 * do; then puts its pick in the menu's place (the list, its cursor, the
 * targets) and lets the menu's last step make the attack, as an A press on
 * the target would. Its guards and Superguards and its own action commands
 * come up by chance, more often the harder it is.
 *
 * The random numbers are its own, from the same start each fight, so every
 * game of an online session (and a replay) plays the computer out the same. */
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_detect.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_unit.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "mario/mario_pouch.h"

#include <stdio.h>
#include <string.h>

extern BattleWeapon marioDefaultWeapon_Jump;  /* battle_mario.c */
extern BattleWeapon marioWeapon_KururinJump;
extern BattleWeapon marioWeapon_JyabaraJump;
extern BattleWeapon marioDefaultWeapon_Hammer;
extern BattleWeapon marioWeapon_KaitenHammer;
extern BattleWeapon marioWeapon_UltraHammer;
extern s32 _jump_badge_table[8];  /* battle_seq_command.c */
extern s32 _hammer_badge_table[9];
void BattleCommandDisplay_ActMenuEnd(BattleWork* wp);

/* by skill: easy, normal, hard */
static const int kAcPct[3] = {45, 75, 95};    /* its action commands done right */
static const int kGuardPct[3] = {20, 55, 80}; /* a guard */
static const int kSuperPct[3] = {0, 6, 20};   /* of those, a Superguard */
static const int kRandomPct[3] = {35, 12, 0}; /* a move picked at random among the usable ones */
static const int kThinkFrames[3] = {55, 40, 28};
static const int kHealBelow[3] = {25, 40, 45}; /* % of max HP */

static int sSkill = PORT_VERSUS_NORMAL;
static unsigned sRng = 1;
static int sThink;
static int sAcGood;

static unsigned next(void) {
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

static int chance(int pct) {
    return (int)(next() % 100u) < pct;
}

void portVersusCpuReset(int skill) {
    sSkill = skill < PORT_VERSUS_EASY ? PORT_VERSUS_EASY : skill > PORT_VERSUS_HARD ? PORT_VERSUS_HARD : skill;
    sRng = 0x2545F491u;
    sThink = 0;
    sAcGood = 0;
}

void portVersusCpuCommandBegins(void) {
    sThink = 0;
}

static int playerKind(const BattleUnit* u) {
    return u->currentKind == UNIT_MARIO || (u->currentKind >= TYPE_PARTNER_MIN && u->currentKind < TYPE_PARTNER_MAX);
}

/* --- what the menu would offer --- */

typedef struct Option {
    int type;                 /* the menu's: 0 jumps, 1 hammers, 2 items, 6 the partner's moves; 5 Tactics */
    BattleCommandWeapon w;    /* the list's row (type 0, 1, 2, 6) */
    int operation;            /* Tactics: 0 change partner, 6 Defend */
    int partyKind;            /* Tactics' change partner: to whom */
    int target;               /* the target (its place in the sampled targets), single-target moves */
    int score;
} Option;

#define MAX_OPTIONS 48

static Option sOptions[MAX_OPTIONS];
static int sCount;
static BattleCommand sScratch;           /* the partner's move list is made here */
static BattleWorkWeaponTargets sTargets;

static void addWeapon(int type, BattleWeapon* weapon, int itemId, int index) {
    Option* o;
    if (weapon == NULL || sCount >= MAX_OPTIONS) {
        return;
    }
    o = &sOptions[sCount++];
    memset(o, 0, sizeof(*o));
    o->type = type;
    o->w.weapon = weapon;
    o->w.enabled = 1;
    o->w.itemId = itemId;
    o->w.index = index;
    o->w.icon = itemId != 0 ? itemDataTable[itemId].icon_id : weapon->icon;
    o->w.name = msgSearch(weapon->name != NULL ? weapon->name : itemDataTable[weapon->itemId].name); /* (as the menu's row) */
}

static void listOptions(BattleWork* wp, BattleUnit* unit) {
    PouchData* pouch = pouchGetPtr(); /* (the acting side's: versus_aurora.c swaps them) */
    int i;
    sCount = 0;
    if (unit->currentKind == UNIT_MARIO) {
        if (!unit->badgesEquipped.Hammerman) {
            if (pouch->jumpLevel >= 1) {
                addWeapon(0, &marioDefaultWeapon_Jump, 0, -1);
            }
            if (pouch->jumpLevel >= 2) {
                addWeapon(0, &marioWeapon_KururinJump, 0, -1);
            }
            if (pouch->jumpLevel >= 3) {
                addWeapon(0, &marioWeapon_JyabaraJump, 0, -1);
            }
            for (i = 0; i < 8; i++) {
                if (pouchEquipCheckBadge(_jump_badge_table[i]) > 0) {
                    addWeapon(0, itemDataTable[_jump_badge_table[i]].battle_params, _jump_badge_table[i], i);
                }
            }
        }
        if (!unit->badgesEquipped.Jumpman) {
            if (pouch->hammerLevel >= 1) {
                addWeapon(1, &marioDefaultWeapon_Hammer, 0, -1);
            }
            if (pouch->hammerLevel >= 2) {
                addWeapon(1, &marioWeapon_KaitenHammer, 0, -1);
            }
            if (pouch->hammerLevel >= 3) {
                addWeapon(1, &marioWeapon_UltraHammer, 0, -1);
            }
            for (i = 0; i < 9; i++) {
                if (pouchEquipCheckBadge(_hammer_badge_table[i]) > 0) {
                    addWeapon(1, itemDataTable[_hammer_badge_table[i]].battle_params, _hammer_badge_table[i], i);
                }
            }
        }
    } else if (!unit->flippedTurns) {
        void (*make)(BattleCommand*, s32*) = (void (*)(BattleCommand*, s32*))BtlUnit_GetData(unit, 1);
        s32 n = 0;
        if (make != NULL) {
            memset(&sScratch, 0, sizeof(sScratch));
            make(&sScratch, &n);
            for (i = 0; i < n && i < 21; i++) {
                addWeapon(6, sScratch.weapon[i].weapon, 0, -1);
            }
        }
    }
    if (!unit->flippedTurns) {
        for (i = 0; i < 20; i++) {
            const int id = pouch->heldItems[i];
            if (id != 0 && (itemDataTable[id].usable_locations & 2) && itemDataTable[id].battle_params != NULL) {
                addWeapon(2, itemDataTable[id].battle_params, id, i);
            }
        }
    }
    (void)wp;
}

/* the targets the move would have, as the menu samples them */
static int sample(BattleWork* wp, BattleUnit* unit, BattleWeapon* weapon) {
    BattleSamplingEnemy(&sTargets, weapon, wp->activeUnitId, BtlUnit_GetEnemyBelong(unit), weapon->targetClassFlags,
                        weapon->targetPropertyFlags, (s32)wp->alliances[unit->alliance].attackDirection);
    return sTargets.numTargets;
}

static BattleUnit* targetUnit(BattleWork* wp, int i, BattleUnitPart** part) {
    const BattleWorkTarget* t = &sTargets.targets[sTargets.targetIndices[i]];
    BattleUnit* u = BattleGetUnitPtr(wp, t->unitIdx);
    *part = u != NULL ? BtlUnit_GetPartsPtr(u, t->partIdx) : NULL;
    return u;
}

/* the damage the move would do to the target, its action command done right */
static int damage(BattleWork* wp, BattleUnit* unit, BattleWeapon* weapon, BattleUnit* target, BattleUnitPart* part,
                  int type) {
    const s32 keptResult = wp->actionCommands.result;
    int power, def = 0;
    if (weapon->hpDamageFn == NULL || part == NULL) {
        return 0;
    }
    wp->actionCommands.result |= 2; /* (the jumps' and hammers' power reads the command's result) */
    power = weapon->hpDamageFn(unit, weapon, target, part);
    wp->actionCommands.result = keptResult;
    if (part->defense != NULL && weapon->element < 5) {
        def = part->defense->defenses[weapon->element];
    }
    if (def >= 99) {
        return 0;
    }
    power -= def;
    if (power < 0) {
        power = 0;
    }
    return type == 0 ? power * 2 : power; /* (most jumps land twice) */
}

/* what hitting the target is worth: the damage, more on the leader (whose
 * fall wins) and on a knock-out */
static int worth(const BattleUnit* target, int dmg) {
    int v;
    if (target->currentHp <= 0 || dmg <= 0) {
        return 0;
    }
    v = (dmg < target->currentHp ? dmg : target->currentHp) * 10;
    if (target->currentKind == UNIT_MARIO) {
        v = v * 13 / 10;
        if (dmg >= target->currentHp) {
            v += 400; /* the win */
        }
    } else if (dmg >= target->currentHp) {
        v += 60;
    }
    return v;
}

static int ownSide(const BattleUnit* unit, const BattleUnit* other) {
    return other->alliance == unit->alliance;
}

/* scores a move (and picks its target) */
static void judge(BattleWork* wp, BattleUnit* unit, Option* o) {
    BattleWeapon* weapon = o->w.weapon;
    int n, i, best = -1, bestV = 0, sum = 0;
    o->score = -1;
    if (weapon->attackEvtScript == NULL || weapon->baseSpCost > 0 || !BtlUnit_CheckWeaponCost(unit, weapon)) {
        return;
    }
    n = sample(wp, unit, weapon);
    if (n <= 0) {
        return;
    }
    if (o->type == 2 && (itemDataTable[o->w.itemId].hp_restored > 0 || itemDataTable[o->w.itemId].fp_restored > 0)) {
        /* a healing item: worth it on whoever is lowest, once low */
        const ItemData* item = &itemDataTable[o->w.itemId];
        for (i = 0; i < n; i++) {
            BattleUnitPart* part;
            BattleUnit* t = targetUnit(wp, i, &part);
            int v = 0;
            if (t == NULL || !ownSide(unit, t) || t->currentHp <= 0) {
                continue;
            }
            if (item->hp_restored > 0 && t->currentHp * 100 < t->maxHp * kHealBelow[sSkill]) {
                const int gain = t->maxHp - t->currentHp < item->hp_restored ? t->maxHp - t->currentHp : item->hp_restored;
                v = gain * (t->currentKind == UNIT_MARIO ? 14 : 9);
            }
            if (item->fp_restored > 0 && t->currentKind == UNIT_MARIO && t->currentFp * 100 < t->maxFp * 30) {
                v += (t->maxFp - t->currentFp < item->fp_restored ? t->maxFp - t->currentFp : item->fp_restored) * 4;
            }
            if (v > bestV) {
                bestV = v;
                best = i;
            }
        }
        if (best < 0) {
            return;
        }
        o->target = (weapon->targetClassFlags & TARGET_CLASS_SINGLE) ? best : 0;
        o->score = bestV;
        return;
    }
    for (i = 0; i < n; i++) {
        BattleUnitPart* part;
        BattleUnit* t = targetUnit(wp, i, &part);
        int v;
        if (t == NULL || ownSide(unit, t) || !playerKind(t)) {
            continue;
        }
        v = worth(t, damage(wp, unit, weapon, t, part, o->type));
        sum += v;
        if (v > bestV || best < 0) {
            bestV = v;
            best = i;
        }
    }
    if (best < 0) {
        return;
    }
    if (weapon->targetClassFlags & TARGET_CLASS_SINGLE) {
        o->target = best;
        o->score = bestV;
        if (sSkill == PORT_VERSUS_EASY && chance(40)) {
            o->target = (int)(next() % (unsigned)n); /* (easy: any target the move has) */
        }
    } else {
        o->target = 0;
        o->score = sum;
    }
    if (o->score <= 0) {
        o->score = weapon->hpDamageFn == NULL ? 1 : 0; /* (a move for its effect alone: little) */
        return;
    }
    o->score -= BtlUnit_GetWeaponCost(unit, weapon) * (sSkill == PORT_VERSUS_HARD ? 4 : 6);
    if (o->type == 2) {
        o->score -= 15; /* (items are few: kept unless they do better) */
    }
    if (o->score < 1) {
        o->score = 1;
    }
}

/* Tactics: another partner when the one out is down */
static int changePartner(BattleWork* wp, BattleUnit* unit, Option* o) {
    BattleUnit* party = BattleGetPartnerPtr(wp, unit);
    PouchData* pouch = pouchGetPtr();
    int id;
    if (unit->currentKind != UNIT_MARIO || party == NULL || party->currentHp > 0) {
        return 0;
    }
    for (id = 1; id <= PARTNER_MSMOWZ; id++) {
        const BattleUnitType kind = BattleTransPartyIdToUnitKind(id);
        if ((pouch->partyData[id].flags & POUCH_PARTY_FLAG_JOINED) && pouch->partyData[id].currentHP > 0 &&
            kind != party->currentKind) {
            memset(o, 0, sizeof(*o));
            o->type = 5;
            o->operation = 0;
            o->partyKind = kind;
            o->score = 120;
            return 1;
        }
    }
    return 0;
}

static BattleCursor* cursorFor(BattleCommand* cmd, int type) {
    switch (type) {
        case 0:
            return &cmd->cursor[2];
        case 1:
            return &cmd->cursor[3];
        case 2:
            return &cmd->cursor[11];
        default:
            return &cmd->cursor[4];
    }
}

/* the pick in the menu's place, and its last step next (state 29: the attack set) */
static void decide(BattleWork* wp, BattleUnit* unit, const Option* o) {
    BattleCommand* cmd = &wp->commandMenu;
    if (cmd->flags & CMD_MENU_FLAG_ACT) {
        BattleCommandDisplay_ActMenuEnd(wp);
    }
    cmd->flags &= ~(CMD_MENU_FLAG_WEAPON_SELECT | CMD_MENU_FLAG_MULTI_ITEM | CMD_MENU_FLAG_OPERATION |
                    CMD_MENU_FLAG_CHANGE_PARTY | CMD_MENU_FLAG_TARGET_SELECT | CMD_MENU_FLAG_PROTECT_PARTNER);
    cmd->cancelled = 0;
    cmd->actType = o->type;
    if (o->type == 5) {
        cmd->operationType = o->operation;
        cmd->partyKind = o->partyKind;
    } else {
        BattleCursor* cursor = cursorFor(cmd, o->type);
        cmd->weapon[0] = o->w;
        cursor->absPos = 0;
        cursor->relPos = 0;
        cursor->count = 1;
        sample(wp, unit, o->w.weapon);
        wp->weaponTargets = sTargets;
        wp->weaponTargets.currentTarget = (s8)(o->target < sTargets.numTargets ? o->target : 0);
    }
    wp->flags |= BATTLE_FLAG_ACT_DECIDED;
    cmd->state = 29;
}

int portVersusCpuCommand(BattleWork* wp, BattleUnit* unit) {
    static Option sPick;
    int i, best = -1, usable = 0;
    if (wp->commandMenu.state != 2 || ++sThink < kThinkFrames[sSkill]) {
        return 0; /* (the menu up a moment first, as a player's would be) */
    }
    listOptions(wp, unit);
    for (i = 0; i < sCount; i++) {
        judge(wp, unit, &sOptions[i]);
        if (sOptions[i].score > 0) {
            usable++;
            if (best < 0 || sOptions[i].score > sOptions[best].score) {
                best = i;
            }
        }
    }
    if (best >= 0 && usable > 1 && chance(kRandomPct[sSkill])) {
        int k = (int)(next() % (unsigned)usable);
        for (i = 0; i < sCount; i++) {
            if (sOptions[i].score > 0 && k-- == 0) {
                best = i;
                break;
            }
        }
    }
    if ((best < 0 || sOptions[best].score < 400) && changePartner(wp, unit, &sPick)) {
        /* (the partner down and no win in sight: another partner first) */
    } else if (best >= 0) {
        sPick = sOptions[best];
    } else {
        memset(&sPick, 0, sizeof(sPick));
        sPick.type = 5;
        sPick.operation = 6; /* Defend */
    }
    printf("[versus] the computer: %s\n", sPick.type == 5 ? (sPick.operation == 0 ? "another partner" : "Defend")
                                          : sPick.w.weapon->name != NULL ? sPick.w.weapon->name : "an item");
    fflush(stdout);
    decide(wp, unit, &sPick);
    return 1;
}

int portVersusCpuGuard(BattleUnit* unit, BattleWeapon* weapon) {
    if (unit->currentHp <= 0 || BtlUnit_CheckStatus(unit, STATUS_SLEEP) || BtlUnit_CheckStatus(unit, STATUS_STOP) ||
        BtlUnit_CheckStatus(unit, STATUS_FREEZE) || BtlUnit_CheckStatus(unit, STATUS_INSTANT_KILL)) {
        return 2;
    }
    if (!chance(kGuardPct[sSkill])) {
        return 2; /* no guard */
    }
    if (weapon != NULL && weapon->superguardsAllowed && chance(kSuperPct[sSkill] * 100 / kGuardPct[sSkill])) {
        return 5; /* a Superguard */
    }
    return 4; /* a guard */
}

void portVersusCpuActionCommand(void) {
    sAcGood = chance(kAcPct[sSkill]);
}

int portVersusCpuActionGood(void) {
    return sAcGood;
}
