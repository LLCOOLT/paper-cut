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
#include "battle/battle_ac_help.h"
#include "battle/battle_detect.h"
#include "battle/battle_mario.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_unit.h"
#include "battle/sac/sac_common.h"
#include "data/item_data.h"
#include "driver/msgdrv.h"
#include "mario/mario_pouch.h"

#include <demo/DEMOPad.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern BattleWeapon marioDefaultWeapon_Jump;  /* battle_mario.c */
extern BattleWeapon marioWeapon_KururinJump;
extern BattleWeapon marioWeapon_JyabaraJump;
extern BattleWeapon marioDefaultWeapon_Hammer;
extern BattleWeapon marioWeapon_KaitenHammer;
extern BattleWeapon marioWeapon_UltraHammer;
extern s32 _jump_badge_table[8];  /* battle_seq_command.c */
extern s32 _hammer_badge_table[9];
void BattleCommandDisplay_ActMenuEnd(BattleWork* wp); /* battle_seq_command.c */
void BattleCommandDisplay_WeaponSelectMenuEnd(BattleWork* wp);
void BattleCommandDisplay_OperationMenuEnd(BattleWork* wp);
void BattleCommandDisplay_MultiItemMenuEnd(BattleWork* wp);
void BattleCommandDisplay_ChangePartySelectMenuEnd(BattleWork* wp);
void BattleCommandDisplay_TargetSelectMenuEnd(BattleWork* wp);
void BattleCommandDisplay_ProtectPartnerSelectMenuEnd(BattleWork* wp);

/* by skill: easy, normal, hard */
static const int kAcPct[3] = {45, 75, 95};    /* its action commands done right */
static const int kGuardPct[3] = {20, 55, 80}; /* a guard */
static const int kSuperPct[3] = {0, 6, 20};   /* of those, a Superguard */
static const int kRandomPct[3] = {35, 12, 0}; /* a move picked at random among the usable ones */
static const int kThinkFrames[3] = {55, 40, 28};
static const int kHealBelow[3] = {25, 40, 45}; /* % of max HP */
static const int kSpecialPct[3] = {40, 85, 100}; /* the Star Powers looked at, this turn */
static const int kChargePct[3] = {30, 70, 100};  /* Charge looked at, this turn */
/* frames of its turn with the menu not where it chooses (state 2) before it
 * gives up and Defends: its turn can never wait forever */
#define STUCK_FRAMES 300

static int sSkill = PORT_VERSUS_NORMAL;
static unsigned sRng = 1;
static int sThink;
static int sStuck;
static int sAcGood;
static int sSac = -1; /* the Star Power it picked this turn (superActionTable's; -1 none) */
static int sSacCur, sSacRound, sSacWait, sSacGood; /* Showstopper's buttons as it plays them */

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
    sStuck = 0;
    sAcGood = 0;
    sSac = -1;
}

void portVersusCpuCommandBegins(void) {
    sThink = 0;
    sStuck = 0;
    sSac = -1;
}

static int playerKind(const BattleUnit* u) {
    return u->currentKind == UNIT_MARIO || (u->currentKind >= TYPE_PARTNER_MIN && u->currentKind < TYPE_PARTNER_MAX);
}

/* --- what the menu would offer --- */

typedef struct Option {
    int type;                 /* the menu's: 0 jumps, 1 hammers, 2 items, 4 Star Powers, 6 the partner's moves; 5 Tactics */
    BattleCommandWeapon w;    /* the list's row (type 0, 1, 2, 4, 6) */
    int special;              /* type 4: which Star Power (superActionTable's) */
    int operation;            /* Tactics: 0 change partner, 1 Charge, 5 Appeal, 6 Defend */
    int partyKind;            /* Tactics' change partner: to whom */
    int target;               /* the target (its place in the sampled targets), single-target moves */
    int score;
} Option;

#define MAX_OPTIONS 64 /* every jump, hammer, Star Power and item Mario can have (51) */

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
        /* the Star Powers (the menu's Special list), some turns: the harder, the more often */
        if (getenv("TTYD_VERSUS_CPU_SAC") != NULL || chance(kSpecialPct[sSkill])) {
            for (i = 0; i < 8; i++) {
                if ((pouch->starPowersObtained & (1 << i)) && superActionTable[i] != NULL && sCount < MAX_OPTIONS) {
                    addWeapon(4, superActionTable[i], 0, -1);
                    sOptions[sCount - 1].special = i;
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

/* HP missing, up to a heal's worth */
static int missing(const BattleUnit* u, int most) {
    const int m = u != NULL && u->currentHp > 0 ? u->maxHp - u->currentHp : 0;
    return m < most ? m : most;
}

/* --- statuses --- */

/* the move's chance (%) of the status, as battle_damage.c's _getSickStatusRate */
static int statusRate(const BattleWeapon* w, int type) {
    switch (type) {
        case STATUS_SLEEP: return w->sleepChance;
        case STATUS_STOP: return w->stopChance;
        case STATUS_DIZZY: return w->dizzyChance;
        case STATUS_POISON: return w->poisonChance;
        case STATUS_CONFUSE: return w->confuseChance;
        case STATUS_ELECTRIC: return w->electricChance;
        case STATUS_DODGY: return w->dodgyChance;
        case STATUS_BURN: return w->burnChance;
        case STATUS_FREEZE: return w->freezeChance;
        case STATUS_TINY: return w->sizeChangeStrength <= 0 ? w->sizeChangeChance : 0;
        case STATUS_ATTACK_UP: return w->atkChangeStrength > 0 ? w->atkChangeChance : 0;
        case STATUS_ATTACK_DOWN: return w->atkChangeStrength <= 0 ? w->atkChangeChance : 0;
        case STATUS_DEFENSE_UP: return w->defChangeStrength > 0 ? w->defChangeChance : 0;
        case STATUS_DEFENSE_DOWN: return w->defChangeStrength <= 0 ? w->defChangeChance : 0;
        case STATUS_INVISIBLE: return w->invisibleChance;
        case STATUS_FAST: return w->fastChance;
        case STATUS_SLOW: return w->slowChance;
        case STATUS_HP_REGEN: return w->hpRegenTime > 0 ? 100 : 0;
        case STATUS_PAYBACK: return w->paybackTime != 0 ? 100 : 0;
        case STATUS_HOLD_FAST: return w->holdFastTime > 0 ? 100 : 0;
        case STATUS_INSTANT_KILL: return (u8)w->ohkoChance;
        default: return 0;
    }
}

/* the unit's own rate against it (%), as battle_damage.c's _getRegistStatus */
static int statusRegist(const BattleUnit* u, int type) {
    const BattleVulnerableStatus* v = u->vulnStatus;
    if (v == NULL) {
        return 100;
    }
    switch (type) {
        case STATUS_SLEEP: return v->sleepRate;
        case STATUS_STOP: return v->stopRate;
        case STATUS_DIZZY: return v->dizzyRate;
        case STATUS_POISON: return v->poisonRate;
        case STATUS_CONFUSE: return v->confuseRate;
        case STATUS_ELECTRIC: return v->electricRate;
        case STATUS_DODGY: return v->dodgyRate;
        case STATUS_BURN: return v->burnRate;
        case STATUS_FREEZE: return v->freezeRate;
        case STATUS_TINY: return v->tinyRate;
        case STATUS_ATTACK_UP: return v->attackUpRate;
        case STATUS_ATTACK_DOWN: return v->attackDownRate;
        case STATUS_DEFENSE_UP: return v->defenseUpRate;
        case STATUS_DEFENSE_DOWN: return v->defenseDownRate;
        case STATUS_INVISIBLE: return v->invisibleRate;
        case STATUS_FAST: return v->fastRate;
        case STATUS_SLOW: return v->slowRate;
        case STATUS_INSTANT_KILL: return v->instaKillRate;
        default: return 100;
    }
}

/* The chance (%) it takes on the target: the move's, the target's own rate,
 * and what keeps it off (as BattleSetStatusDamage: immune parts, Allergic,
 * Feeling Fine, Ice Power); 0 when the target has it already. */
static int statusChance(const BattleWeapon* w, BattleUnit* t, const BattleUnitPart* part, int type) {
    int rate = statusRate(w, type);
    if (rate <= 0 || part == NULL || (part->attributes & (PART_ATTR_IMMUNE_TO_DAMAGE_OR_STATUS | PART_ATTR_IMMUNE_TO_STATUS))) {
        return 0;
    }
    if (BtlUnit_CheckStatus(t, (StatusEffectType)type)) {
        return 0; /* (already: no gain) */
    }
    switch (type) {
        case STATUS_SLEEP:
        case STATUS_STOP:
        case STATUS_DIZZY:
        case STATUS_POISON:
        case STATUS_CONFUSE:
        case STATUS_TINY:
        case STATUS_ATTACK_DOWN:
        case STATUS_DEFENSE_DOWN:
        case STATUS_INSTANT_KILL:
            if (t->badgesEquipped.FeelingFine) {
                return 0;
            }
            /* fall through */
        case STATUS_BURN:
        case STATUS_FREEZE:
        case STATUS_SLOW:
            if (BtlUnit_CheckStatus(t, STATUS_ALLERGIC) || (part->attributes & PART_ATTR_IMMUNE_TO_MOST_STATUS) ||
                (type == STATUS_BURN && t->badgesEquipped.IcePower)) {
                return 0;
            }
            break;
        default:
            break;
    }
    if (!(w->special & SPECIAL_IGNORE_STATUS_VULNERABILITY) || statusRegist(t, type) == 0) {
        rate = rate * statusRegist(t, type) / 100;
    }
    return rate > 100 ? 100 : rate;
}

/* turns a status lasts, as far as the computer looks ahead (three) */
static int turnsOf(int time) {
    return time <= 0 ? 1 : time > 3 ? 3 : time;
}

/* What a move's statuses are worth on a foe: turns it can't act (sleep,
 * stop, freeze: as Clock Out's), half of that for dizzy, slow and confused,
 * the damage poison and burn do, the attack and defence it loses, and a
 * knock-out (Showstopper's: worth its whole HP); each by its chance. */
static int foeStatusWorth(const BattleWeapon* w, BattleUnit* t, const BattleUnitPart* part) {
    const int turn = t->currentKind == UNIT_MARIO ? 45 : 25;
    int v = 0, p;
    if (t->currentHp <= 0) {
        return 0;
    }
    if ((p = statusChance(w, t, part, STATUS_SLEEP)) > 0) {
        v += turn * turnsOf(w->sleepTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_STOP)) > 0) {
        v += turn * turnsOf(w->stopTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_FREEZE)) > 0) {
        v += turn * turnsOf(w->freezeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_DIZZY)) > 0) {
        v += turn / 2 * turnsOf(w->dizzyTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_CONFUSE)) > 0) {
        v += turn / 2 * turnsOf(w->confuseTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_SLOW)) > 0) {
        v += turn / 2 * turnsOf(w->slowTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_POISON)) > 0) {
        v += worth(t, (w->poisonStrength > 0 ? w->poisonStrength : 1) * turnsOf(w->poisonTime)) * 7 / 10 * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_BURN)) > 0) {
        v += worth(t, turnsOf(w->burnTime)) * 7 / 10 * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_TINY)) > 0) {
        v += 15 * turnsOf(w->sizeChangeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_ATTACK_DOWN)) > 0) {
        v += 12 * (w->atkChangeStrength < 0 ? -w->atkChangeStrength : 1) * turnsOf(w->atkChangeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_DEFENSE_DOWN)) > 0) {
        v += 10 * (w->defChangeStrength < 0 ? -w->defChangeStrength : 1) * turnsOf(w->defChangeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_INSTANT_KILL)) > 0) {
        v += worth(t, t->currentHp) * p / 100;
    }
    return v;
}

/* What a move's statuses are worth on its own side: an extra move (Rally
 * Wink), attack and defence up, dodging and going unseen, healing over
 * turns while hurt, Payback and Hold Fast */
static int allyStatusWorth(const BattleWeapon* w, BattleUnit* t, const BattleUnitPart* part) {
    const int lead = t->currentKind == UNIT_MARIO;
    int v = 0, p;
    if (t->currentHp <= 0) {
        return 0;
    }
    if ((p = statusChance(w, t, part, STATUS_FAST)) > 0) {
        v += (lead ? 60 : 40) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_ATTACK_UP)) > 0) {
        v += 14 * (w->atkChangeStrength > 0 ? w->atkChangeStrength : 1) * turnsOf(w->atkChangeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_DEFENSE_UP)) > 0) {
        v += (lead ? 12 : 8) * (w->defChangeStrength > 0 ? w->defChangeStrength : 1) * turnsOf(w->defChangeTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_DODGY)) > 0) {
        v += (lead ? 25 : 15) * turnsOf(w->dodgyTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_INVISIBLE)) > 0) {
        v += (lead ? 40 : 20) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_ELECTRIC)) > 0) {
        v += 12 * turnsOf(w->electricTime) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_HP_REGEN)) > 0 && t->currentHp * 100 < t->maxHp * 70) {
        v += missing(t, (w->hpRegenStrength > 0 ? w->hpRegenStrength : 1) * turnsOf(w->hpRegenTime)) * (lead ? 10 : 6) *
             p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_PAYBACK)) > 0) {
        v += (lead ? 25 : 15) * p / 100;
    }
    if ((p = statusChance(w, t, part, STATUS_HOLD_FAST)) > 0) {
        v += (lead ? 20 : 12) * p / 100;
    }
    return v;
}

/* --- Showstopper --- */

/* its knock-out chance as the computer plays its buttons (each press right
 * by kShowPressPct): 50% with no round cleared, 10 more a round
 * (sac_suki.c), so about 58, 70 and 89% */
static const int kShowstopperPct[3] = {58, 70, 89};
static const int kShowPressPct[3] = {80, 92, 98};
static const int kShowPressFrames[3] = {30, 20, 14}; /* (each round gives 180 frames and up for 3 to 7 buttons) */

int portShowstopperWants(s32* button, s32* cur, s32* round); /* sac_suki.c */

static unsigned showstopperButtons(void) {
    s32 button, cur, round;
    if (!portShowstopperWants(&button, &cur, &round)) {
        return 0;
    }
    if (cur != sSacCur || round != sSacRound) {
        /* the next button: a moment to find it, and whether it is pressed right */
        sSacCur = cur;
        sSacRound = round;
        sSacWait = kShowPressFrames[sSkill];
        sSacGood = chance(kShowPressPct[sSkill]);
    }
    if (sSacWait > 0) {
        sSacWait--;
        return 0;
    }
    if (!sSacGood) {
        return 0u; /* (not right: it lets the time run out) */
    }
    if (sSacWait == 0) {
        sSacWait = -1;
        printf("[versus] the computer: Showstopper, round %d, button %d\n", (int)round + 1, (int)cur + 1);
        fflush(stdout);
    }
    return (unsigned)button;
}

/* --- the other Star Powers' games, played by skill (easy poor, normal
 * decent, hard near-perfect but never frame-perfect), from its own random
 * numbers --- */

static int between(int lo, int hi) { /* lo..hi */
    return lo + (int)(next() % (unsigned)(hi - lo + 1));
}

static void say(const char* what) {
    printf("[versus] the computer: %s\n", what);
    fflush(stdout);
}

/* Earth Tremor: A as the cursor passes each star (within 7 of it). Right:
 * somewhere inside that, never at its middle every time; wrong: too early. */
static const int kTremorPct[3] = {82, 93, 99};
static float sTrNext, sTrAt;
static int sTrDone;

static unsigned earthTremor(void) {
    f32 cursor, speed, at;
    if (!portEarthTremorWants(&cursor, &speed, &at)) {
        return 0;
    }
    (void)speed;
    if (at != sTrNext) {
        const int good = chance(kTremorPct[sSkill]);
        sTrNext = at;
        sTrAt = good ? at + (float)between(-5, 5) : at - (float)between(9, 13);
        sTrDone = 0;
    }
    if (!sTrDone && cursor >= sTrAt) {
        sTrDone = 1;
        return PAD_BUTTON_A;
    }
    return 0;
}

/* A button mashed: a press every few frames, by skill (Supernova fills its
 * gauge in time with about 13 frames between presses on the first cut, 10
 * on the last: easy makes a cut or two, normal about three, hard all five;
 * Clock Out's fills well under 10) */
static const int kMashMin[3] = {11, 8, 5};
static const int kMashMax[3] = {15, 11, 7};
static const int kNovaMin[3] = {12, 10, 5};
static const int kNovaMax[3] = {15, 13, 7};
static int sMashWait, sMashes;

static int mash(const int* lo, const int* hi) {
    if (sMashWait > 0) {
        sMashWait--;
        return 0;
    }
    sMashWait = between(lo[sSkill], hi[sSkill]) - 1;
    sMashes++;
    return 1;
}

static unsigned supernova(void) {
    return portSupernovaWants() && mash(kNovaMin, kNovaMax) ? PAD_BUTTON_A : 0;
}

/* Clock Out: mashes the button that throws the bomb to the foes (keeping it
 * there fills the gauge too); a moment to find it when the buttons swap,
 * and now and then the wrong one */
static const int kClockReact[3] = {45, 25, 12};
static const int kClockWrongPct[3] = {6, 2, 0};
static u32 sCoButton;

static unsigned clockOut(void) {
    static const unsigned kAbx[3] = {PAD_BUTTON_A, PAD_BUTTON_B, PAD_BUTTON_X};
    u32 foe;
    if (!portClockOutWants(&foe)) {
        return 0;
    }
    if (foe != sCoButton) {
        sCoButton = foe;
        sMashWait = kClockReact[sSkill];
    }
    if (!mash(kMashMin, kMashMax)) {
        return 0;
    }
    if (chance(kClockWrongPct[sSkill])) {
        unsigned b;
        do {
            b = kAbx[next() % 3u];
        } while (b == foe);
        return b;
    }
    return foe;
}

/* Power Lift: the cursor to the nearest arrow (attack first on a tie) and
 * A; a moment for each step; the poison mushroom now and then taken for an
 * arrow */
static const int kLiftReactMin[3] = {16, 8, 4};
static const int kLiftReactMax[3] = {24, 12, 6};
static const int kLiftPoisonPct[3] = {15, 4, 0};
static int sPlWait;

static unsigned powerLift(unsigned* dirs) {
    s32 cur, types[9];
    int i, best = -1, bestD = 99, wrong;
    const int r = portPowerLiftGrid(&cur, types);
    if (r <= 0) {
        return 0;
    }
    if (sPlWait > 0) {
        sPlWait--;
        return 0;
    }
    wrong = chance(kLiftPoisonPct[sSkill]);
    for (i = 0; i < 9; i++) {
        int d;
        if (types[i] < 0 || (types[i] == 2 && !wrong)) {
            continue;
        }
        d = (i / 3 > cur / 3 ? i / 3 - cur / 3 : cur / 3 - i / 3) + (i % 3 > cur % 3 ? i % 3 - cur % 3 : cur % 3 - i % 3);
        d = d * 2 + (types[i] != 0); /* (attack on a tie) */
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    if (best < 0) {
        sPlWait = 3; /* (nothing yet) */
        return 0;
    }
    sPlWait = between(kLiftReactMin[sSkill], kLiftReactMax[sSkill]);
    if (best == cur) {
        return PAD_BUTTON_A;
    }
    if (best % 3 != cur % 3) {
        *dirs = best % 3 > cur % 3 ? DEMO_STICK_RIGHT : DEMO_STICK_LEFT;
    } else {
        *dirs = best / 3 > cur / 3 ? DEMO_STICK_DOWN : DEMO_STICK_UP;
    }
    return 0;
}

/* Sweet Treat and Sweet Feast: the stick pulled and let go when a shot then
 * would hit an item (the shot leaves 8 frames later), judged a few frames
 * off by skill; a breath between shots; easy doesn't see the poison */
static const int kTreatErr[3] = {4, 2, 1};
static const int kTreatGap[3] = {30, 14, 8};
static int sStPulled, sStGap, sStErr;

static int sweetTreat(void) {
    int r;
    if (sStPulled) {
        sStPulled = 0; /* (let go: it fires) */
        sStGap = kTreatGap[sSkill];
        return 0;
    }
    if (sStGap > 0) {
        sStGap--;
        return 0;
    }
    r = portSweetTreatAim(8 + sStErr);
    if (r == 1 || (r == 2 && sSkill == PORT_VERSUS_EASY)) {
        sStPulled = 1;
        sStErr = sSkill == PORT_VERSUS_HARD && chance(70) ? 0 : between(-kTreatErr[sSkill], kTreatErr[sSkill]);
        return -127;
    }
    return 0;
}

/* Art Attack: a loop drawn around all the foes it can hit (one loop hits
 * each it covers by 30% or more, harder the more), again and again until
 * the time is up: an ellipse round their boxes, overshot so the line
 * crosses itself and closes; steered with the stick (the star speeds up by
 * stick / 50 a frame and keeps 86% of its speed). Easy draws slow, loose
 * loops that miss some; hard quick, sure ones. */
#define ART_POINTS 24
static const float kArtSpeed[3] = {5.0f, 7.0f, 9.0f};
static const int kArtSizeMin[3] = {60, 85, 105}; /* % of the loop that covers the boxes */
static const int kArtSizeMax[3] = {120, 120, 118};
static const int kArtShift[3] = {25, 8, 0};
static const int kArtPause[3] = {40, 15, 4};
static float sArtX[ART_POINTS + 1], sArtY[ART_POINTS + 1];
static int sArtPlan, sArtIdx, sArtLast, sArtPause, sArtLoops;

static void artPlan(float hx, float hy) {
    f32 box[4][4];
    const int n = portArtAttackTargets(box, 4);
    float x0 = 100000.0f, y0 = 100000.0f, x1 = -100000.0f, y1 = -100000.0f, cx, cy, rx, ry, a0, f;
    int i;
    if (n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        x0 = box[i][0] < x0 ? box[i][0] : x0;
        y0 = box[i][1] < y0 ? box[i][1] : y0;
        x1 = box[i][2] > x1 ? box[i][2] : x1;
        y1 = box[i][3] > y1 ? box[i][3] : y1;
    }
    f = (float)between(kArtSizeMin[sSkill], kArtSizeMax[sSkill]) / 100.0f;
    cx = (x0 + x1) / 2.0f + (float)between(-kArtShift[sSkill], kArtShift[sSkill]);
    cy = (y0 + y1) / 2.0f + (float)between(-kArtShift[sSkill], kArtShift[sSkill]);
    /* (an ellipse through a box's corners is its half-size times 1.42) */
    rx = ((x1 - x0) / 2.0f * 1.42f + 12.0f) * f;
    ry = ((y1 - y0) / 2.0f * 1.42f + 12.0f) * f;
    rx = rx < 30.0f ? 30.0f : rx;
    ry = ry < 30.0f ? 30.0f : ry;
    a0 = (float)atan2(hy - cy, hx - cx);
    for (i = 0; i <= ART_POINTS; i++) {
        /* once round and a quarter more */
        const float a = a0 + (float)i * (2.5f * 3.14159265f) / (float)ART_POINTS;
        float px = cx + rx * (float)cos(a), py = cy + ry * (float)sin(a);
        sArtX[i] = px < -295.0f ? -295.0f : px > 295.0f ? 295.0f : px;
        sArtY[i] = py < -195.0f ? -195.0f : py > 195.0f ? 195.0f : py;
    }
    sArtPlan = 1;
    sArtIdx = 0;
}

static void artAttack(int* sx, int* sy) {
    f32 x, y, vx, vy;
    s32 points, timer;
    float dx, dy, d, want, ax, ay, m;
    if (!portArtAttackDraw(&x, &y, &vx, &vy, &points, &timer)) {
        sArtPlan = 0;
        return;
    }
    if (points < sArtLast) {
        sArtPlan = 0; /* (a loop closed: the next) */
        sArtPause = kArtPause[sSkill];
        sArtLoops++;
        printf("[versus] the computer: Art Attack, loop %d\n", sArtLoops);
        fflush(stdout);
    }
    sArtLast = points;
    if (!sArtPlan) {
        if (sArtPause > 0) {
            sArtPause--;
            return;
        }
        artPlan(x, y);
        if (!sArtPlan) {
            return;
        }
    }
    dx = sArtX[sArtIdx] - x;
    dy = sArtY[sArtIdx] - y;
    d = (float)sqrt(dx * dx + dy * dy);
    if (d < 18.0f) {
        /* the next point; past the last, round again (it closes soon) */
        sArtIdx = sArtIdx >= ART_POINTS ? ART_POINTS / 4 : sArtIdx + 1;
        dx = sArtX[sArtIdx] - x;
        dy = sArtY[sArtIdx] - y;
        d = (float)sqrt(dx * dx + dy * dy);
    }
    if (d < 0.001f) {
        return;
    }
    want = d * 0.35f + 2.0f;
    want = want > kArtSpeed[sSkill] ? kArtSpeed[sSkill] : want;
    /* the stick that brings its speed to that (v' = 0.86 (v + stick / 50)) */
    ax = 50.0f * (dx / d * want / 0.86f - vx);
    ay = 50.0f * (dy / d * want / 0.86f - vy);
    m = (float)sqrt(ax * ax + ay * ay);
    if (m > 127.0f) {
        ax *= 127.0f / m;
        ay *= 127.0f / m;
    } else if (m < 43.0f) {
        /* (under 42 the stick does nothing: a nudge, or let it coast) */
        if (m < 20.0f) {
            return;
        }
        ax *= 43.0f / m;
        ay *= 43.0f / m;
    }
    *sx = (int)ax;
    *sy = (int)ay;
}

void portVersusCpuSacPad(PortVersusCpuPad* pad) {
    int sx = 0, sy = 0;
    memset(pad, 0, sizeof(*pad));
    switch (sSac) {
        case 0: /* Sweet Treat */
        case 5: /* Sweet Feast */
            pad->stickX = sweetTreat();
            break;
        case 1:
            pad->press = earthTremor();
            break;
        case 2:
            pad->press = clockOut();
            break;
        case 3:
            pad->press = powerLift(&pad->dirs);
            break;
        case 4:
            artAttack(&sx, &sy);
            pad->stickX = sx;
            pad->stickY = sy;
            break;
        case 6:
            pad->press = showstopperButtons();
            break;
        case 7:
            pad->press = supernova();
            break;
        default:
            break;
    }
}

/* a Star Power picked: its game from the start */
static void sacBegins(void) {
    sSacCur = sSacRound = -1;
    sTrNext = -1.0f;
    sTrDone = 1;
    sMashWait = kMashMin[sSkill];
    sMashes = 0;
    sCoButton = 0;
    sPlWait = kLiftReactMax[sSkill];
    sStPulled = sStGap = sStErr = 0;
    sArtPlan = sArtIdx = sArtLast = sArtPause = sArtLoops = 0;
    say(sSac == 0 ? "Sweet Treat" : sSac == 1 ? "Earth Tremor" : sSac == 2 ? "Clock Out" : sSac == 3 ? "Power Lift"
        : sSac == 4 ? "Art Attack" : sSac == 5 ? "Sweet Feast" : sSac == 6 ? "Showstopper" : "Supernova");
}

/* A Star Power's worth: the heals once HP run low; the attacks by the damage
 * they would likely do (their action commands about half right; they pierce
 * defence); Clock Out by the foes' turns it may take; Power Lift while it
 * isn't on; Showstopper by the knock-outs it would likely make (as it plays
 * the buttons); less their Star Power, which is slow to fill. */
static void judgeSpecial(BattleWork* wp, BattleUnit* unit, Option* o, int n) {
    static const int kHit[8] = {0, 3, 0, 0, 5, 0, 0, 10}; /* a foe's likely damage: Earth Tremor, Art Attack, Supernova */
    BattleUnit* party = BattleGetPartnerPtr(wp, unit);
    const char* force = getenv("TTYD_VERSUS_CPU_SAC"); /* (tests: this Star Power whenever it can) */
    int i, v = 0;
    if (force != NULL) {
        if (atoi(force) == o->special) {
            o->target = 0;
            o->score = 100000;
        }
        return;
    }
    switch (o->special) {
        case 0:   /* Sweet Treat */
        case 5: { /* Sweet Feast */
            const int big = o->special == 5;
            const int fp = unit->maxFp - unit->currentFp;
            if (unit->currentHp * 100 >= unit->maxHp * kHealBelow[sSkill]) {
                return;
            }
            v = missing(unit, big ? 20 : 8) * 14 + missing(party, big ? 20 : 6) * 7;
            if (unit->currentFp * 100 < unit->maxFp * 30) {
                v += (fp < (big ? 15 : 6) ? fp : (big ? 15 : 6)) * 4;
            }
            break;
        }
        case 1: /* Earth Tremor */
        case 4: /* Art Attack */
        case 7: /* Supernova */
            for (i = 0; i < n; i++) {
                BattleUnitPart* part;
                BattleUnit* t = targetUnit(wp, i, &part);
                if (t != NULL && !ownSide(unit, t) && playerKind(t)) {
                    v += worth(t, kHit[o->special]);
                }
            }
            break;
        case 2: /* Clock Out: a turn or two lost, by chance */
            for (i = 0; i < n; i++) {
                BattleUnitPart* part;
                BattleUnit* t = targetUnit(wp, i, &part);
                if (t != NULL && !ownSide(unit, t) && playerKind(t) && t->currentHp > 0 && t->stopTurns <= 0) {
                    v += t->currentKind == UNIT_MARIO ? 45 : 25;
                }
            }
            break;
        case 3: /* Power Lift */
            if (unit->atkChangeTurns <= 0) {
                v = 70;
            }
            break;
        case 6: /* Showstopper: a knock-out of each foe by chance; with the rule off, a share of its HP */
            for (i = 0; i < n; i++) {
                BattleUnitPart* part;
                BattleUnit* t = targetUnit(wp, i, &part);
                int p;
                if (t == NULL || ownSide(unit, t) || !playerKind(t)) {
                    continue;
                }
                if (portVersusShowstopperSoft()) {
                    /* (sac_suki.c: the knock-out chance the stars earned, over 3, of its max HP) */
                    v += worth(t, (t->maxHp * kShowstopperPct[sSkill] + 299) / 300);
                } else if ((p = statusChance(o->w.weapon, t, part, STATUS_INSTANT_KILL)) > 0) {
                    v += worth(t, t->currentHp) * p / 100 * kShowstopperPct[sSkill] / 100;
                }
            }
            break;
        default:
            return;
    }
    v -= o->w.weapon->baseSpCost * (sSkill == PORT_VERSUS_HARD ? 12 : 18);
    if (v > 0) {
        o->target = 0;
        o->score = v;
    }
}

/* scores a move (and picks its target) */
static void judge(BattleWork* wp, BattleUnit* unit, Option* o) {
    BattleWeapon* weapon = o->w.weapon;
    int n, i, best = -1, bestV = 0, sum = 0;
    o->score = -1;
    if (weapon->attackEvtScript == NULL || (weapon->baseSpCost > 0 && o->type != 4) ||
        !BtlUnit_CheckWeaponCost(unit, weapon)) {
        return;
    }
    n = sample(wp, unit, weapon);
    if (n <= 0) {
        return;
    }
    if (o->type == 4) {
        judgeSpecial(wp, unit, o, n);
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
        if (t == NULL || !playerKind(t)) {
            continue;
        }
        if (ownSide(unit, t)) {
            /* a move for its own side (Rally Wink, Veil, a Power Punch, ...): its statuses */
            v = allyStatusWorth(weapon, t, part);
            if (v <= 0) {
                continue;
            }
        } else {
            /* the damage, and the statuses it gives (alone: Sleepy Sheep, Tease, a Stopwatch, ...) */
            v = worth(t, damage(wp, unit, weapon, t, part, o->type)) + foeStatusWorth(weapon, t, part);
        }
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
        o->score = weapon->hpDamageFn == NULL ? 1 : 0; /* (a move for an effect it can't weigh: little) */
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

/* Tactics' Charge (the Charge or Charge P badge): worth it when no hit now
 * would win and the charge isn't high already; the next move does the more */
static int charge(BattleUnit* unit, int bestScore, Option* o) {
    BattleWeapon* weapon = unit->currentKind == UNIT_MARIO ? &badgeWeapon_Charge : &badgeWeapon_ChargeP;
    int v;
    if (!unit->badgesEquipped.Charge || (unit->attributeFlags & UNIT_ATTR_VEILED) || unit->chargeStrength >= 4 ||
        bestScore >= 400 || !BtlUnit_CheckWeaponCost(unit, weapon) || !chance(kChargePct[sSkill])) {
        return 0;
    }
    if (unit->currentKind == UNIT_MARIO && BtlUnit_GetFp(unit) - BtlUnit_GetWeaponCost(unit, weapon) < 2) {
        return 0; /* (FP left for a move to follow it) */
    }
    /* (+2 a badge to the next move, a jump's two hits: against what the best move does now, so it
     * charges when its hits are weak, against high defence) */
    v = 2 * unit->badgesEquipped.Charge * (unit->chargeStrength > 0 ? 11 : 22) -
        BtlUnit_GetWeaponCost(unit, weapon) * (sSkill == PORT_VERSUS_HARD ? 4 : 6);
    if (v <= bestScore) {
        return 0;
    }
    memset(o, 0, sizeof(*o));
    o->type = 5;
    o->operation = 1;
    o->score = v;
    return 1;
}

static BattleCursor* cursorFor(BattleCommand* cmd, int type) {
    switch (type) {
        case 0:
            return &cmd->cursor[2];
        case 1:
            return &cmd->cursor[3];
        case 2:
            return &cmd->cursor[11];
        case 4:
            return &cmd->cursor[7];
        default:
            return &cmd->cursor[4];
    }
}

/* whatever menus are up put away, as their own steps do */
static void closeMenus(BattleWork* wp) {
    BattleCommand* cmd = &wp->commandMenu;
    if (cmd->flags & CMD_MENU_FLAG_ACT) {
        BattleCommandDisplay_ActMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_WEAPON_SELECT) {
        BattleCommandDisplay_WeaponSelectMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_MULTI_ITEM) {
        BattleCommandDisplay_MultiItemMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_OPERATION) {
        BattleCommandDisplay_OperationMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_CHANGE_PARTY) {
        BattleCommandDisplay_ChangePartySelectMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_TARGET_SELECT) {
        BattleCommandDisplay_TargetSelectMenuEnd(wp);
    }
    if (cmd->flags & CMD_MENU_FLAG_PROTECT_PARTNER) {
        BattleCommandDisplay_ProtectPartnerSelectMenuEnd(wp);
    }
    cmd->flags &= ~(CMD_MENU_FLAG_ACT | CMD_MENU_FLAG_WEAPON_SELECT | CMD_MENU_FLAG_MULTI_ITEM | CMD_MENU_FLAG_OPERATION |
                    CMD_MENU_FLAG_CHANGE_PARTY | CMD_MENU_FLAG_TARGET_SELECT | CMD_MENU_FLAG_PROTECT_PARTNER);
    BattleAcHelpSetDispType(0);
}

/* the pick in the menu's place, and its last step next (state 29: the attack set) */
static void decide(BattleWork* wp, BattleUnit* unit, const Option* o) {
    BattleCommand* cmd = &wp->commandMenu;
    closeMenus(wp);
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
        if (!(o->w.weapon->targetClassFlags & TARGET_CLASS_SINGLE) &&
            (o->w.weapon->targetPropertyFlags & (TARGET_PROP_START_OWN_SIDE | TARGET_PROP_START_OPPOSING_SIDE))) {
            /* a move on all at once (Clock Out, a Fire Flower): from where the menu starts it
             * (_btlcmd_SelectWeaponDecide), as the attack hits from there to the list's end:
             * the last of its own side, or the first of the other */
            int i, sel = -1;
            for (i = 0; i < sTargets.numTargets; i++) {
                BattleUnitPart* part;
                BattleUnit* t = targetUnit(wp, i, &part);
                if (t == NULL) {
                    continue;
                }
                if (o->w.weapon->targetPropertyFlags & TARGET_PROP_START_OWN_SIDE) {
                    if (ownSide(unit, t)) {
                        sel = i;
                    }
                } else if (!ownSide(unit, t)) {
                    sel = i;
                    break;
                }
            }
            if (sel >= 0) {
                wp->weaponTargets.currentTarget = (s8)sel;
            }
        }
    }
    sSac = o->type == 4 ? o->special : -1;
    if (sSac >= 0) {
        sacBegins();
    }
    wp->flags |= BATTLE_FLAG_ACT_DECIDED;
    cmd->state = 29;
}

/* Defend (Appeal while its Star Power isn't full), when nothing else will do */
static void fallback(Option* o) {
    const PouchData* pouch = pouchGetPtr();
    memset(o, 0, sizeof(*o));
    o->type = 5;
    o->operation = pouch->starPowersObtained != 0 && pouchGetAP() < pouch->maximumSP ? 5 : 6;
}

static const char* pickName(const Option* o) {
    if (o->type == 5) {
        return o->operation == 0 ? "another partner" : o->operation == 1 ? "Charge" : o->operation == 5 ? "Appeal" : "Defend";
    }
    return o->w.weapon->name != NULL ? o->w.weapon->name : "an item";
}

int portVersusCpuCommand(BattleWork* wp, BattleUnit* unit) {
    static Option sPick;
    int i, best = -1, usable = 0;
    if (wp->commandMenu.state != 2) {
        /* The menu somewhere the computer doesn't choose from: an error message
         * or an audience's item ends by itself. Anything that doesn't, a while
         * on, ends in Defend (in a Double Dip, the rest passed, as its cancel
         * does), so its turn always ends. */
        if (++sStuck < STUCK_FRAMES) {
            return 0;
        }
        printf("[versus] the computer: the menu stuck in state %d; Defend\n", (int)wp->commandMenu.state);
        fflush(stdout);
        sStuck = 0;
        if ((s8)(u8)unit->moveState >= 5 && (s8)(u8)unit->moveState <= 9) {
            closeMenus(wp);
            wp->commandMenu.cancelled = 1;
            wp->flags |= BATTLE_FLAG_ACT_DECIDED;
            wp->commandMenu.state = 29;
            return 1;
        }
        memset(&sPick, 0, sizeof(sPick));
        sPick.type = 5;
        sPick.operation = 6;
        decide(wp, unit, &sPick);
        return 1;
    }
    if (++sThink < kThinkFrames[sSkill]) {
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
    } else if (charge(unit, best >= 0 ? sOptions[best].score : 0, &sPick)) {
        /* (a Charge first: no hit now worth more) */
    } else if (best >= 0) {
        sPick = sOptions[best];
    } else {
        fallback(&sPick);
    }
    printf("[versus] the computer: %s (item %d, score %d)\n", pickName(&sPick), sPick.type == 2 ? sPick.w.itemId : 0,
           sPick.score);
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
