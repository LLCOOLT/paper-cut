/* Paper Mario 64's bosses as refights (port/include/port/pm64_foes.h, docs/PM64_EXTRAS.md "Bosses").
 *
 * Each boss is a battle unit of Paper Cut's own (a unit type the game never uses), drawn as its sprite from
 * the linked ROM (pm64_enemy_pc.c's draw), its name and Tattle from the ROM's text, and its attacks rebuilt
 * from scratch as TTYD battle scripts from what PM64's boss does (no N64 code): a few move kinds shared by
 * all (a jump onto one, a rush into one, a shot at one, a blow at everyone, a quake, a heal, a charge), each
 * boss its own list of them with their power, the sprite row they play and the phase they belong to (from
 * the start, or once the boss is below half its HP). The body that isn't drawn is Whacka's field model
 * (c_koburon), as pit_whacka_aurora.c's unit.
 *
 * Post-game scaling: HP = PM64 HP x 2.5, each hit = PM64 power + 2, defence PM64's (TTYD's post-game bosses
 * run 100-200 HP with 6-10 attack).
 *
 * The fights: the boss rush and boss refights list them after the game's own (modes_aurora.c) when
 * "pm64_bosses" is on and a ROM is linked; refights open once the story is over. Off online and in replays
 * (the boss rush's own rule). */
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_foes.h"
#include "port/pm64_media.h"
#include "port/pm64_music.h"
#include "port/pm64_sfx.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/replay.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/versus.h"
#include "port/tr.h"
#include "battle/battle.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_database_common.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_event_subset.h"
#include "battle/battle_monosiri.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_sub.h"
#include "evt/evt_batstage.h"
#include "evt/evt_eff.h"
#include "evt/evt_msg.h"
#include "eff/n64/eff_akari_sflush_n64.h"
#include "eff/n64/eff_bomb_n64.h"
#include "eff/n64/eff_breath_fire_n64.h"
#include "eff/n64/eff_explosion_n64.h"
#include "eff/n64/eff_fire_spark_n64.h"
#include "eff/n64/eff_fireworks_n64.h"
#include "eff/n64/eff_flame_n64.h"
#include "eff/n64/eff_gareki_n64.h"
#include "eff/n64/eff_hokori_n64.h"
#include "eff/n64/eff_ice_n64.h"
#include "eff/n64/eff_lensflare_n64.h"
#include "eff/n64/eff_magic1_n64.h"
#include "eff/eff_charge.h"
#include "eff/n64/eff_packn_bfire_n64.h"
#include "eff/n64/eff_pokopi_thunder_n64.h"
#include "eff/n64/eff_radiation_n64.h"
#include "eff/n64/eff_recover_heart_n64.h"
#include "eff/n64/eff_shock_n64.h"
#include "eff/n64/eff_snow_dust_n64.h"
#include "eff/n64/eff_thunderflare_n64.h"
#include "eff/n64/eff_whirlwind_n64.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "evt/evt_cmd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include "system.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void npcSetupBattleInfo(NpcEntry* npc, NpcSetupBattleInfo* setup); /* npcdrv.c */

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK
void portBattlexVariantTexts(int kind, const char* name, const char* tattle);

#include "pm64_bosses/pm64_boss.h"


/* --- the units: every file's in pm64_bosses/ (registry.h), the fights first in the registry's order, then the
 * units they bring in; links made from within a file to the whole table's indices --- */
#define BOSS_UNITS_MAX 64
static signed char sSetOf[BOSS_UNITS_MAX]; /* each unit's file */
static const Pm64BossSet* const kSets[] = {
#define PM64_BOSS(id) &kPm64Boss_##id,
#include "pm64_bosses/registry.h"
#undef PM64_BOSS
};
#define SETS (int)(sizeof(kSets) / sizeof(kSets[0]))
static Pm64Boss sBoss[BOSS_UNITS_MAX];
static int sBossCount, sFightCount, sFoeBase = -1;

static void bossTables(void) {
    static int sDone;
    int s, u, f, k, base[SETS], other[SETS];
    if (sDone) {
        return;
    }
    sDone = 1;
    for (f = 0, s = 0; s < SETS; s++) {
        base[s] = f;
        f += kSets[s]->fights;
    }
    sFightCount = f;
    for (s = 0; s < SETS; s++) {
        other[s] = f;
        f += kSets[s]->count - kSets[s]->fights;
    }
    if (f > BOSS_UNITS_MAX) {
        printf("[pm64] boss: %d units, room for %d\n", f, BOSS_UNITS_MAX);
        return;
    }
    sBossCount = f;
    for (s = 0; s < SETS; s++) {
        const Pm64BossSet* set = kSets[s];
#define GLOBAL1(rel1) ((rel1) <= 0 || (rel1) > set->count ? 0                                                     \
                       : (rel1) - 1 < set->fights ? base[s] + (rel1) : other[s] + (rel1) - set->fights)
        for (u = 0; u < set->count; u++) {
            Pm64Boss* p = &sBoss[u < set->fights ? base[s] + u : other[s] + u - set->fights];
            *p = set->units[u];
            p->lead1 = (signed char)GLOBAL1(p->lead1);
            p->spawn1 = (signed char)GLOBAL1(p->spawn1);
            p->shed1 = (signed char)GLOBAL1(p->shed1);
            p->early1 = (signed char)GLOBAL1(p->early1);
            for (k = 0; k < BOSS_WITH; k++) {
                p->with1[k] = (signed char)GLOBAL1(p->with1[k]);
            }
            for (k = 0; k < BOSS_MOVES; k++) {
                p->moves[k].unit1 = (signed char)GLOBAL1(p->moves[k].unit1);
            }
            sSetOf[p - sBoss] = (signed char)s;
        }
#undef GLOBAL1
        if (strcmp(set->id, "story_foes") == 0) {
            sFoeBase = other[s];
        }
    }
    /* the units of one file (one fight) each their own unit type (its Tattle Log entry holds the unit's name
       and Tattle while it fights); a unit naming one an earlier unit of its file has gets a spare. Units of
       different files may share one: bossOfUnit tells them apart by their kind. Spares: the game's unused unit
       types no other code uses. */
    {
        static const int kSpare[] = {UNIT_UNUSED_TEST, UNIT_UNUSED_FIGHTER, UNIT_UNUSED_HYPER_BOB_OMB,
                                     UNIT_UNUSED_ULTRA_BOB_OMB, UNIT_UNUSED_ANTI_GUY, UNIT_UNUSED_HOTDOG_MASTER,
                                     UNIT_UNUSED_MASTER, UNIT_UNUSED_REPORTER, UNIT_UNUSED_MINI_BZZAP,
                                     UNIT_UNUSED_RED_BUZZY, UNIT_UNUSED_RED_PARABUZZY,
                                     UNIT_UNUSED_RED_SPIKY_PARABUZZY, UNIT_UNUSED_ZESS_T};
        const int spares = (int)(sizeof(kSpare) / sizeof(kSpare[0]));
        for (s = 0; s < SETS; s++) {
            int idx[BOSS_UNITS_MAX], n = kSets[s]->count, i, j, k;
            for (u = 0; u < n; u++) {
                idx[u] = u < kSets[s]->fights ? base[s] + u : other[s] + u - kSets[s]->fights;
            }
            for (i = 1; i < n; i++) {
                for (j = 0; j < i && sBoss[idx[j]].unitType != sBoss[idx[i]].unitType; j++) {
                }
                if (j == i) {
                    continue;
                }
                for (k = 0; k < spares; k++) {
                    for (j = 0; j < n && sBoss[idx[j]].unitType != kSpare[k]; j++) {
                    }
                    if (j == n) {
                        break;
                    }
                }
                if (k == spares) {
                    printf("[pm64] boss: no unit type left for %s\n", sBoss[idx[i]].english);
                    continue;
                }
                sBoss[idx[i]].unitType = kSpare[k];
            }
        }
    }
}

static BattleUnitKind sKinds[BOSS_UNITS_MAX];

/* the boss table's unit a battle unit is (by its kind: unit types are shared between fights), -1 none */
static int bossOfParams(const BattleUnitKind* k) {
    return k != NULL && k >= sKinds && k < sKinds + BOSS_UNITS_MAX ? (int)(k - sKinds) : -1;
}

static int bossOfUnit(const BattleUnit* unit) {
    return unit != NULL ? bossOfParams(unit->kindParams) : -1;
}
#define kBosses sBoss
#define BOSSES sBossCount
#define FIGHTS sFightCount
#define UW_TURN 13
#define UW_CHARGE 14
#define UW_ANIM 15 /* the sprite row a move plays, + 1; 0: by the unit's state */
#define UW_FORCE 12 /* its next turn's move, + 1 (0: picked by weight) */
#define UW_CHARGED 11 /* it has gathered its strength (its idle row the angry one) */
#define UW_GONE 10    /* it's fleeing into the unit that takes its place */
#define UW_SLOT 0     /* a companion's place beside its lead (+ 1; set by its setup's unitWork[0]) */

/* --- engine 2: each battle unit's state (by its id), the Koopa Bros.' tower ----------------------------------- */
typedef struct {
    signed char move;              /* the move it's playing (-1 none) */
    signed char uses[BOSS_MOVES];  /* how many times each move has been played */
    signed char order;             /* its next place in its order table */
    signed char hitTurns;          /* turns it has been hit in (leaveHits) */
    signed char toppled;           /* a tower brother: turns left on its back */
    signed char gotUp;             /* it got up this turn (a hop) */
    signed char doused;            /* its fire is out (frozen) */
    signed char vanish;            /* a clone that has been hit */
    signed char shed;              /* it has been hit: a piece breaks off */
    signed char hitsLeft;          /* the move's hits still to come */
    signed char swell;             /* % bigger for the move */
    signed char saidLow;           /* it has spoken its low-HP line */
    short lastHitTurn, skipRound;  /* the turn it was last hit; a gang's round it sat out */
    short lastTurn[BOSS_MOVES];    /* its turn each move was last played (cooldown) */
    signed char darkened;          /* its move darkened the stage */
    signed char armed;             /* HZ_* bits it holds with its gathered strength */
    signed char barrier;           /* its barrier is up (Bowser's Star Rod: defence 99, attacks doubled) */
    signed char bounced, broke;    /* a hit bounced off its barrier / broke it (the damage event shows it) */
    signed char flip;              /* a lone tower brother jumped on: he flips onto his back */
    short brokeTurn;               /* its turn when its barrier last broke (PM64 AVAR_TurnsSinceStarBeam) */
    signed char died;              /* its death sound has played */
    signed char out;               /* the companion its move has sent at the target (id + 1; 0 none) */
    signed char strayed;           /* the companion a cut move sent home (id + 1; 0 none): checked at its next turn */
    signed char foot;              /* its next footstep: 0 left, 1 right */
    float stepAt[2], walked;       /* where it stood last frame (x, z); walked since its last footstep */
} BossRt;
static BossRt sRt[BATTLE_UNIT_MAX];
static unsigned long long sMateDone; /* the companions a move has already used */
static float sLeadHome[3] = {90.0f, 0.0f, 0.0f}; /* where the fight's lead stood at its start (a new wave's home) */
/* the lines each kind has spoken this fight (bit 0 its move line, 1 its first-turn line, 2 its low-HP line, 3 + i
 * move i's line, 11 its barrier's bounce line, 12 its break line, 13 the barrier's hint): look-alikes (twins,
 * clones) speak a line once between them (PM64: one of the pair) */
#define SAID_BOUNCE 11
#define SAID_BREAK 12
#define SAID_HINT 13
static unsigned short sSaid[BOSS_UNITS_MAX];
static int sayOnce(int b, int bit) {
    if (sSaid[b] & (1u << bit)) {
        return 0;
    }
    sSaid[b] |= (unsigned short)(1u << bit);
    return 1;
}

static BossRt* rtOf(const BattleUnit* unit) {
    return &sRt[(unsigned)unit->unitId % BATTLE_UNIT_MAX];
}

/* the tower (PM64 trd_part_2 fake_bowser.c): who stands in it bottom to top, where each stood on the ground */
static struct {
    signed char up, tipped, falling, height, placed;
    short building; /* the brother building it (id + 1) */
    short ids[4];
    float ground[4][3];
    float x;        /* where it stands */
    short spun;     /* the round it last spun (+ 1): one Spin Attack a round, whoever's on top */
    signed char joining;  /* the brother building is joining a standing tower (no build line) */
    signed char built;    /* it has just gone up (its build line, once a fight: saidBuild) */
    signed char saidBuild, sayOver, saidOver; /* the lines: MSG_CH1_010A, MSG_CH1_0109 (once per tipping) */
} sTower;

static int towerPlace(const BattleUnit* unit) {
    int i;
    for (i = 0; sTower.up && i < sTower.height; i++) {
        if (sTower.ids[i] == unit->unitId) {
            return i;
        }
    }
    return -1;
}

static int moveEnd(const BossMove* m) {
    /* (a list of BOSS_MOVES moves needs no {-1}: the rest are zeros) */
    return m->kind < 0 || (m->kind == 0 && m->power == 0 && m->row == 0 && m->weight == 0 && m->sfx == NULL);
}

static int alive(const BattleUnit* u) {
    return u != NULL && u->currentHp > 0 && !(u->flags & UNIT_FLAG_DELETE_PENDING) &&
           !(u->attributeFlags & UNIT_ATTR_INACTIVE);
}

static BattleUnit* unitAt(int i) {
    return _battleWorkPointer != NULL ? BattleGetUnitPtr(_battleWorkPointer, i) : NULL;
}

int portPm64BossCount(void) {
    bossTables();
    return FIGHTS;
}

const char* portPm64BossEnglish(int b) {
    bossTables();
    return b >= 0 && b < FIGHTS ? kBosses[b].english : "";
}

int portPm64BossesReady(void) {
    return portPm64Linked() != PM64_NONE && !portStateSyncLinked() && !portReplayActive();
}

/* "Paper Mario 64 bosses: Scale to your level" (a fight from the boss list or the board: PM64_BOSS_FLAG_SCALE):
 * the fight's PM64 HP and power x f, f = 50% + 50% x (Mario's level / the boss's PM64 chapter level), at most
 * 100%: a level-5 Mario against Huff N. Puff (22) gets 61% of PM64's own numbers, one at his level or past it
 * PM64's own; never below half of them (and at least 1). The post-game rule (x 2.5 HP, + 2 power) is off then.
 * 0: off (the boss rush, refights without it). */
static int sScale;
/* about Mario's level in PM64 at each fight (the rush's order: Jr. Troopa .. Bowser) */
static const unsigned char kPm64Level[] = {3, 6, 9, 13, 16, 19, 22, 25, 27, 27};

static int scaled(int value, int least) {
    const int v = (value * sScale + 50) / 100;
    return v < least ? least : v;
}

/* a hit's power: PM64's scaled, or the post-game's PM64 + 2 */
static int bossPower(int power) {
    return sScale > 0 ? scaled(power, power > 0 ? 1 : 0) : power + 2;
}

int portPm64BossesOn(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_bosses", 0, 0, 1);
    }
    return sOn != NULL && *sOn && portPm64BossesReady();
}

/* the Tattle card's picture (pm64_enemy_pc.c): its sprite, palette and idle row (port/include/port/pm64_foes.h) */
int portPm64BossCardRow(const BattleUnit* unit, int* sprite, int* palette, int* row) {
    const int b = bossOfUnit(unit);
    if (b < 0 || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    *sprite = kBosses[b].sprite;
    *palette = kBosses[b].palette;
    *row = kBosses[b].idle;
    return 1;
}

/* the draw (pm64_enemy_pc.c): a boss unit's sprite, palette, row and size; 0 if the unit isn't one */
int portPm64BossDraw(const BattleUnit* unit, int* sprite, int* palette, int* anim, float* scale) {
    const Pm64Boss* p;
    const BattleUnitPart* part;
    const char* pose = NULL;
    const char* u;
    int b;
    char c;
    if (unit == NULL || (b = bossOfUnit(unit)) < 0 || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    p = &kBosses[b];
    *sprite = p->sprite;
    *palette = p->chargedPal > 0 && unit->work[UW_CHARGED] != 0 ? p->chargedPal - 1 : p->palette;
    /* (under the Star Rod's barrier he keeps his own palette: PM64's UpdateEnchantedBowserColors blends towards the
       StarRod palette and back, never sits on it, and that palette drawn as it is turned him cyan and white from
       his second turn, when the barrier always goes up, to the end of the fight. The blend is the cycling glow
       drawn over him: portPm64BossFx's glow 3. barrierPal is kept for a true blend.) */
    *scale = p->scale;
    {
        /* its size: smaller as its HP goes (shrink), bigger for a move (swell), a wobble while it idles */
        const BossRt* rt = rtOf(unit);
        float f = 1.0f;
        if (p->shrink > 0 && unit->maxHp > 0) {
            f -= (float)p->shrink / 100.0f * (1.0f - (float)unit->currentHp / (float)unit->maxHp);
        }
        f *= 1.0f + (float)rt->swell / 100.0f;
        if (p->wobble > 0 && unit->work[UW_ANIM] == 0) {
            f *= 1.0f + (float)p->wobble / 100.0f * sinf((float)(gp->retraceCount % 120u) * 0.05236f);
        }
        *scale *= f;
    }
    if (unit->work[UW_ANIM] > 0) {
        *anim = (int)unit->work[UW_ANIM] - 1;
        if (p->hpRows > 1 && unit->maxHp > 0) {
            /* (a move's rows follow its HP too: 4xDraw, 3xDraw...) */
            const int left = (unit->currentHp * p->hpRows + unit->maxHp - 1) / unit->maxHp;
            *anim += (p->hpRows - (left < 1 ? 1 : left)) * p->hpStep;
        }
        return 1;
    }
    for (part = unit->parts; part != NULL && pose == NULL; part = part->nextPart) {
        /* (the pose it plays: a status-picked one, as after a hit, leaves poseName as the last set by hand) */
        pose = portPm64PoseAnimName(part->poseId, part->poseName);
    }
    u = pose != NULL ? strrchr(pose, '_') : NULL;
    c = u != NULL && u > pose ? u[-1] : 'S';
    *anim = c == 'D' || c == 'X' ? p->hurt
            : c == 'W' || c == 'R' ? p->walk
            : unit->work[UW_CHARGED] != 0 && p->angry > 0 ? p->angry
                                                           : p->idle;
    if (p->hpRows > 1 && unit->maxHp > 0) {
        /* (rows that follow its HP: the Shy Stack one member shorter for each stage lost) */
        const int left = (unit->currentHp * p->hpRows + unit->maxHp - 1) / unit->maxHp;
        *anim += (p->hpRows - (left < 1 ? 1 : left)) * p->hpStep;
    }
    if (p->tower) {
        const int at = towerPlace(unit);
        if (rtOf(unit)->toppled > 0 && p->downRow > 0) {
            *anim = p->downRow;
        } else if (at >= 0 && at == sTower.height - 1 && sTower.tipped && p->tipRow > 0) {
            *anim = p->tipRow;
        } else if (at >= 0 && at < sTower.height - 1 && p->crouchRow > 0) {
            *anim = p->crouchRow;
        }
    }
    return 1;
}

/* a sprite in parts (Huff N. Puff's body, face and arms): the n-th part's row over the row just drawn */
int portPm64BossLayer(const BattleUnit* unit, int row, int n, int* anim) {
    const int b = bossOfUnit(unit);
    const signed char* l;
    if (b < 0 || n < 0 || n > 1 || (l = kBosses[b].layers) == NULL) {
        return 0;
    }
    for (; l[0] >= 0; l += 3) {
        if (l[0] == row) {
            *anim = l[1 + n];
            return *anim >= 0;
        }
    }
    return 0;
}

int portPm64BossFieldLook(int fight, int talking, Pm64FieldLook* out) {
    const Pm64Boss* p;
    const signed char* l;
    bossTables();
    if (fight < 0 || fight >= FIGHTS || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    p = &kBosses[fight];
    out->pose = -1;
    out->sprite = p->sprite;
    out->palette = p->palette;
    out->anim = talking && p->talkRow > 0 ? p->talkRow : p->idle;
    out->layer[0] = out->layer[1] = -1;
    for (l = p->layers; l != NULL && l[0] >= 0; l += 3) {
        if (l[0] == out->anim) {
            out->layer[0] = l[1];
            out->layer[1] = l[2];
            break;
        }
    }
    out->vineA = p->vineA > 0 && p->vine > 0 ? p->vineA - 1 : -1;
    out->vineB = p->vineB > 0 && p->vine > 0 ? p->vineB - 1 : -1;
    out->vine = (float)p->vine;
    out->scale = p->scale;
    /* a flyer floats a little over the field's floor (in its battle it hovers at fly, over the stage's front:
       Huff N. Puff 80, out of the field's picture there) */
    out->hover = p->fly > 0 ? 16.0f : 0.0f;
    return 1;
}

int portPm64BossVine(const BattleUnit* unit, int* rowA, int* rowB, float* height) {
    const int b = bossOfUnit(unit);
    if (b < 0 || kBosses[b].vineA <= 0 || kBosses[b].vine == 0) {
        return 0;
    }
    *rowA = kBosses[b].vineA - 1;
    *rowB = (kBosses[b].vineB > 0 ? kBosses[b].vineB : kBosses[b].vineA) - 1;
    *height = (float)kBosses[b].vine;
    return 1;
}

/* --- the units --------------------------------------------------------------------------------------------- */

static BattleUnitDefense sDefence[BOSS_UNITS_MAX];
static BattleUnitDefenseAttr sDefenceAttr[BOSS_UNITS_MAX]; /* (2: immune to the element; IM_* bits) */
/* a boss and its fight's own units: no Fright, Gale Force or instant KO (PM64: Air Lift, Hurricane and Spook
 * never work on a boss or its minions) */
static StatusVulnerability sRegist = {30, 30, 40, 50, 30, 100, 80, 50, 100, 50, 100, 100, 100, 100, 100, 0, 0,
                                      100, 50, 100, 100, 0};
/* a story enemy (its `home` chapter set: Forest Fuzzy, Hyper Goomba, the Shy Guys, Clubba, Duplighost...): an
 * ordinary foe, as in PM64, where Air Lift carries it off, Hurricane blows it away and Spook scares it; the
 * statuses as TTYD's ordinary enemies take them */
static StatusVulnerability sRegistFoe = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
                                         100, 100, 100, 100, 100, 100, 95};
/* a big story enemy (Clubba, Duplighost: PM64's are harder to lift and blow away) */
static StatusVulnerability sRegistFoeBig = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
                                            100, 80, 60, 100, 100, 100, 100, 80};
static PoseTableEntry sPoses[] = {
    {1, "S_1"},  {2, "S_1"},  {9, "S_1"},  {5, "S_1"},  {4, "S_1"},  {3, "S_1"},  {28, "S_1"},
    {29, "D_1"}, {30, "D_1"}, {31, "S_1"}, {39, "D_1"}, {50, "S_1"}, {51, "S_1"}, {42, "S_1"},
    {41, "S_1"}, {40, "S_1"}, {56, "S_1"}, {57, "D_1"}, {BTL_POSE_TALK, "T_1"}, {BTL_POSE_STAY_DEFAULT, "S_1"},
};
static BattleDataEntry sData[] = {
    {48, btldefaultevt_Dummy},
    {0, NULL},
};
static BattleUnitKindPart sParts[BOSS_UNITS_MAX];
static BattleWeapon sWeapons[BOSS_UNITS_MAX][BOSS_MOVES];
static BattleWeapon sWeaponsP[BOSS_UNITS_MAX][BOSS_MOVES]; /* a move's weapon on the partner (its `partner`) */
static BattleUnitSetup sSpawn[BOSS_UNITS_MAX]; /* each unit as another one's stand-in brings it in */
static BattleUnitSetup sSlotSetup[16];         /* a summoned companion's setup (its place in unitWork[0]) */
static int sSlotNext;
static signed char sMain[BOSS_UNITS_MAX];      /* the fight's own units (its lead and those that take its place) */
static char sTag[BOSS_UNITS_MAX][2][24];
/* its lines: before a move, as it flees, its first turn, at low HP, as its tower tips, as its tower first stands,
 * as an attack bounces off its barrier, as its barrier breaks */
#define LINE_TIP 4
#define LINE_BUILD 5
#define LINE_BOUNCE 6
#define LINE_BREAK 7
static char sLineTag[BOSS_UNITS_MAX][8][24];
static const char kHintTag[] = "pc_pm64b_barrier_hint"; /* the barrier's hint (port text: pm64-foes.txt) */
static char sMoveTag[BOSS_UNITS_MAX][BOSS_MOVES][20]; /* a move's own line */
USER_FUNC(evt_pm64boss_line); /* (below: a line to speak) */
static int bossLog(void);
static void bossKey(const BattleUnit* unit, const char* key, int frames);

#define BOSS_TARGETS                                                                                               \
    (TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE | TARGET_CLASS_NO_SELF | \
     TARGET_CLASS_MAIN_PARTS | TARGET_CLASS_NO_PARTNER) /* (PM64's enemies attack Mario alone: SetTargetActor \
     ACTOR_PLAYER. Not TARGET_CLASS_ONLY_MARIO: the PC build's versus rule in _btlSamplingEnemy drops a Mario on \
     the sampled side, which for an enemy's weapon is the only Mario there, so no move ever found a target) */

static void weaponFor(BattleWeapon* w, const BossMove* m, int partner) {
    memset(w, 0, sizeof(*w));
    w->baseAccuracy = 100;
    w->superguardsAllowed = m->kind == MV_JUMP || m->kind == MV_RUSH || m->kind == MV_POUND || m->kind == MV_SLAM;
    w->reserved_14 = 1.0f;
    w->stylishCommandMultiplier = 1;
    w->reserved_19 = 1;
    w->bingoSlotChance = 1;
    w->reserved_1B = 1;
    w->hpDamageFn = weaponGetPowerDefault;
    w->hpDamageWork[0] = bossPower(m->power); /* post-game: PM64's power + 2 (a story enemy's: set where it's met) */
    w->targetClassFlags =
        BOSS_TARGETS | (m->kind == MV_ALL || m->kind == MV_QUAKE ? TARGET_CLASS_MULTIPLE : TARGET_CLASS_SINGLE);
    w->targetPropertyFlags = TARGET_PROP_START_OPPOSING_SIDE | (m->kind == MV_QUAKE ? TARGET_PROP_NO_FLOATING : 0);
    w->element = ELEMENT_NORMAL;
    w->acLevel = 3;
    w->reserved_6F = 2;
    w->special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE |
                 (m->kind == MV_QUAKE ? SPECIAL_GROUNDS_WINGED : SPECIAL_SHATTERS_FREEZE);
    w->targetWeightFlags = m->kind == MV_RUSH ? TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_RANDOM : TARGET_WEIGHT_RANDOM;
    if (m->kind == MV_QUAKE) {
        w->dizzyChance = 30;
        w->dizzyTime = 1;
    }
    w->stageEffectRates[3] = 100;
    /* the counters it ignores: a bolt, a breath, a shot, falling debris, a quake or a swarm never touches the
       target, so no Zap Tap / Electric, Return Postage / Payback, Hold Fast or poison counter (TTYD's own ranged
       enemy weapons: COUNTER_RESIST_ALL); a jump, rush, slam or pound is contact and is countered as TTYD's are */
    w->counterResistFlags = (m->kind == MV_JUMP || m->kind == MV_RUSH || m->kind == MV_POUND || m->kind == MV_SLAM)
                                ? 0
                                : COUNTER_RESIST_ALL;
    /* engine 2: its element (EL_* is TTYD's ELEMENT_* order), the status it gives, the partner hit too */
    w->element = (m->element > 0 && m->element <= EL_SHOCK) ? m->element : ELEMENT_NORMAL;
    if (m->element == EL_ICE) {
        w->special &= ~SPECIAL_SHATTERS_FREEZE;
    }
    if (m->status != ST_NONE) {
        const u8 chance = (u8)(m->statusChance > 0 ? m->statusChance : 100);
        const s8 turns = (s8)(m->statusTurns > 0 ? m->statusTurns : 1);
        switch (m->status) {
            case ST_FREEZE: w->freezeChance = chance, w->freezeTime = turns; break;
            case ST_BURN: w->burnChance = chance, w->burnTime = turns; break;
            case ST_DIZZY: w->dizzyChance = chance, w->dizzyTime = turns; break;
            case ST_SLEEP: w->sleepChance = chance, w->sleepTime = turns; break;
            case ST_STOP: w->stopChance = chance, w->stopTime = turns; break;
            case ST_POISON: w->poisonChance = chance, w->poisonTime = turns, w->poisonStrength = 1; break;
            case ST_SHRINK: w->sizeChangeChance = chance, w->sizeChangeTime = turns, w->sizeChangeStrength = -2; break;
            case ST_CONFUSE: w->confuseChance = chance, w->confuseTime = turns; break;
            default: break;
        }
    }
    if (m->partner > 0) {
        w->targetClassFlags &= ~TARGET_CLASS_NO_PARTNER;
        if (partner) {
            w->hpDamageWork[0] = bossPower(m->partner);
        }
    }
}

/* --- the scripts ------------------------------------------------------------------------------------------- */
/* its companions' kinds: those beside it from the start (with1), those it summons, the pieces it sheds */
static int mateKind(int b, int k) {
    int i;
    if (k < 0 || b < 0 || kBosses[k].illusory) {
        return 0;
    }
    for (i = 0; i < BOSS_WITH; i++) {
        if (kBosses[b].with1[i] - 1 == k) {
            return 1;
        }
    }
    for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
        if (kBosses[b].moves[i].kind == MV_SUMMON && kBosses[b].moves[i].unit1 - 1 == k) {
            return 1;
        }
    }
    return kBosses[b].shed1 - 1 == k;
}

/* its clones' kind */
static int cloneKind(int b, int k) {
    int i;
    for (i = 0; b >= 0 && k >= 0 && i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
        if (kBosses[b].moves[i].kind == MV_CLONES && kBosses[b].moves[i].unit1 - 1 == k) {
            return 1;
        }
    }
    return 0;
}

/* how many of its companions (or clones, or units of kind `kind`) stand */
static int countUnits(const BattleUnit* self, int b, int what, int kind) {
    int i, n = 0;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        int k;
        if (!alive(u) || u == self || u->alliance != 1 || (k = bossOfUnit(u)) < 0) {
            continue;
        }
        n += what == 0 ? mateKind(b, k) : what == 1 ? cloneKind(b, k) && kBosses[k].illusory : k == kind;
    }
    return n;
}

static int marioFrozen(void) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        if (u != NULL && u->alliance == 0 && u->currentKind == UNIT_MARIO) {
            return u->freezeTurns > 0;
        }
    }
    return 0;
}

/* Mario's partner is out and not KO'd */
static int partnerUp(void) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        if (u != NULL && u->alliance == 0 && u->currentKind != UNIT_MARIO && u->currentKind >= UNIT_GOOMBELLA &&
            u->currentKind <= UNIT_MS_MOWZ && u->currentHp > 0 && !(u->attributeFlags & UNIT_ATTR_INACTIVE)) {
            return 1;
        }
    }
    return 0;
}

static int condOk(const BattleUnit* unit, int b, const BossMove* m) {
    switch (m->cond) {
        case C_MATES: return countUnits(unit, b, 0, -1) > 0;
        case C_NO_MATES: return countUnits(unit, b, 0, -1) == 0;
        case C_CLONES: return countUnits(unit, b, 1, -1) > 0;
        case C_NO_CLONES: return countUnits(unit, b, 1, -1) == 0;
        case C_FROZEN: return marioFrozen();
        case C_HURT: return unit->currentHp < unit->maxHp;
        case C_UNIT: return countUnits(unit, b, 2, m->unit1 - 1) > 0;
        case C_NO_UNIT: return countUnits(unit, b, 2, m->unit1 - 1) == 0;
        case C_NOT_FROZEN: return !marioFrozen();
        case C_UNIT_PARTNER: return countUnits(unit, b, 2, m->unit1 - 1) > 0 && partnerUp();
        default: return 1;
    }
}

/* it may be played at all now: its uses left, its condition, a summon or clones with room, a heal that fits */
static int usable(const BattleUnit* unit, int b, int i) {
    const BossMove* m = &kBosses[b].moves[i];
    if (i < 0 || i >= BOSS_MOVES || moveEnd(m) || (m->uses > 0 && rtOf(unit)->uses[i] >= m->uses) ||
        !condOk(unit, b, m)) {
        return 0;
    }
    if (m->cooldown > 0 && rtOf(unit)->uses[i] > 0 &&
        (int)unit->work[UW_TURN] - rtOf(unit)->lastTurn[i] <= m->cooldown) {
        return 0;
    }
    if (m->kind == MV_SWALLOW) {
        return countUnits(unit, b, 0, -1) > 0;
    }
    if (m->kind == MV_BARRIER) {
        /* (from its second turn, while its barrier is down: final_bowser_1.c EVS_TakeTurn_Inner, AVAR_TurnCount
           > 1 and not ACTOR_EVENT_FLAG_STAR_ROD_ENCHANTED) */
        return !rtOf(unit)->barrier && unit->work[UW_TURN] >= 2;
    }
    if (m->kind == MV_HEAL) {
        return unit->currentHp < unit->maxHp;
    }
    return 1;
}

static int movable(const BattleUnit* unit, int b, int i, int low) {
    const BossMove* m = &kBosses[b].moves[i];
    return usable(unit, b, i) &&
           !((m->phase == 1 && low) || (m->phase == 2 && !low) || m->phase == 3 || m->weight <= 0 ||
             (m->kind == MV_HEAL && m->chance == NULL && m->uses == 0 && unit->currentHp + m->power > unit->maxHp));
}

/* a move's chance table by HP share ({share %, chance %, ...0}, ascending) */
static int chanceOk(const BattleUnit* unit, const BossMove* m) {
    const signed char* c = m->chance;
    if (m->lead > 0 && _battleWorkPointer != NULL) {
        /* (Mario ahead by m->lead points of HP share, or not at all: PM64's Recover only when he is winning) */
        const BattleUnit* mario = BattleGetMarioPtr(_battleWorkPointer);
        if (mario == NULL || mario->maxHp <= 0 || unit->maxHp <= 0 ||
            mario->currentHp * 100 / mario->maxHp - unit->currentHp * 100 / unit->maxHp < m->lead) {
            return 0;
        }
    }
    if (c == NULL) {
        return 1;
    }
    for (; c[0] > 0; c += 2) {
        if (unit->currentHp * 100 < c[0] * unit->maxHp) {
            return irand(100) < c[1];
        }
    }
    return 0;
}

/* the barrier goes up (PM64 final_bowser_1.c EVS_TakeTurn_Inner): the first time on its second turn (turn 2:
 * MSG_CH8_0084 and EVS_Move_UseStarRod, always); after that by the turns since it last broke (AVAR_TurnsSinceStarBeam:
 * 1 or 2: never, 3: 15 in 100, more: 75 in 100) */
static int barrierChance(const BattleUnit* unit, int i) {
    const BossRt* rt = rtOf(unit);
    const int since = (int)unit->work[UW_TURN] - rt->brokeTurn;
    if (rt->uses[i] == 0) {
        return 1;
    }
    return irand(100) < (since <= 2 ? 0 : since == 3 ? 15 : 75);
}

static int pickByWeight(const BattleUnit* unit, int b, int low) {
    int i, total = 0, pick;
    for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
        total += movable(unit, b, i, low) ? kBosses[b].moves[i].weight : 0;
    }
    pick = total > 0 ? irand(total) : 0;
    for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
        if (movable(unit, b, i, low) && (pick -= kBosses[b].moves[i].weight) < 0) {
            return i;
        }
    }
    return 0;
}

static unsigned hazardAttrs(int hz) {
    return ((hz & HZ_FIRE) ? COUNTER_ATTR_FIERY : 0) | ((hz & HZ_SPIKE) ? COUNTER_ATTR_TOP_SPIKY : 0) |
           ((hz & HZ_SHOCK) ? COUNTER_ATTR_ELECTRIC : 0) | ((hz & HZ_ICE) ? COUNTER_ATTR_ICY : 0);
}

/* what touching it does now: its own hazards (its fire out while doused) and those it holds charged */
static void armHazard(BattleUnit* unit, int b, int extra) {
    BossRt* rt = rtOf(unit);
    int hz = kBosses[b].hazard;
    rt->armed = (signed char)extra;
    if (rt->doused) {
        hz &= ~HZ_FIRE;
    }
    if (unit->parts != NULL) {
        unit->parts->counterAttributes = (PartsCounterAttribute_Flags)hazardAttrs(hz | extra);
    }
}

static void setDefence(int b, int d) {
    int i;
    for (i = 0; i < 5; i++) {
        sDefence[b].defenses[i] = (s8)d;
    }
}

/* A scaled fight's defence: PM64's, never past what Mario's boots or hammer get through (TTYD's Jump and Hammer
 * hit 1, 2, 3 by their level: battle_weapon_power.c), so the boots and hammer of his chapter always hurt it
 * (a prologue Mario's 1 against Jr. Troopa's 2 was no damage at all). 99: not scaled, PM64's own. */
static int sDefCap = 99;

static int ownDefence(int b) {
    return kBosses[b].defense < sDefCap ? kBosses[b].defense : sDefCap;
}

static short roundNow(void) {
    return _battleWorkPointer != NULL ? (short)(_battleWorkPointer->turnCount + 1) : 0;
}

/* a tower brother's turn (PM64 fake_bowser.c EVS_KoopaBros_TakeTurn): on its back it waits (its first turn) and
 * gets up (its second); in the tower only the top one moves (the Spin Attack for the tower's height); with no
 * tower, two or more standing build one; one standing beside a tower joins it (EVS_TryJoiningTower) on top.
 * The tower spins once a round, whoever is on top when it does. -1: it rests; 0: its move as usual; 1: it builds
 * the tower first; 2: it joins the tower first, then rests (the tower has spun this round) */
static int towerTurn(BattleUnit* unit, int b) {
    BossRt* rt = rtOf(unit);
    int i, standing = 0;
    rt->gotUp = 0;
    if (rt->toppled > 0) {
        if (--rt->toppled == 0) {
            rt->gotUp = 1; /* (BOSS_CMD_TRY_GET_UP: a hop, back to its idle and its defence) */
            setDefence(b, ownDefence(b));
        }
        return -1;
    }
    if (sTower.up) {
        if (towerPlace(unit) < 0 && sTower.height < 4) {
            /* (EVS_TryJoiningTower: a brother back on his feet leaps onto the tower; it steadies, and its new top
               spins for the new height if it hasn't spun this round) */
            sTower.ids[(int)sTower.height] = (short)unit->unitId;
            sTower.ground[(int)sTower.height][0] = unit->homePosition.x;
            sTower.ground[(int)sTower.height][1] = unit->homePosition.y;
            sTower.ground[(int)sTower.height][2] = unit->homePosition.z;
            sTower.placed = sTower.height;
            sTower.height++;
            sTower.building = (short)(unit->unitId + 1);
            sTower.joining = 1;
            sTower.tipped = 0;
            sTower.saidOver = 0;
            if (sTower.spun == roundNow()) {
                return 2;
            }
            sTower.spun = roundNow();
            return 0;
        }
        if (towerPlace(unit) != sTower.height - 1 || sTower.spun == roundNow()) {
            return -1;
        }
        sTower.tipped = 0; /* (BOSS_CMD_STABLE: a tipping tower steadies) */
        sTower.saidOver = 0;
        sTower.spun = roundNow();
        return 0;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        const int k = bossOfUnit(u);
        standing += alive(u) && u->alliance == 1 && k >= 0 && kBosses[k].tower && rtOf(u)->toppled == 0;
    }
    return standing >= 2 ? 1 : 0;
}

/* in: the unit; out: the move's kind, sprite row, weapon, sound (NULL none), the boss's walk and jump rows, the
 * move's power and whether it hops */
/* tests ($TTYD_PM64_LOG=1): $TTYD_PM64_FORCE "<unit's English name>:<move>,..." plays that move (1 its first) each
 * of the unit's turns while it may be played, ahead of its pattern (the moves' looks captured one by one: the
 * pm64_bossfx_* scenarios); the first entry for its name that may be played wins. -1 none. */
static int forcedMove(const BattleUnit* unit, int b) {
    const char* s = bossLog() ? getenv("TTYD_PM64_FORCE") : NULL;
    const size_t n = strlen(kBosses[b].english);
    while (s != NULL && *s != '\0') {
        if (strncmp(s, kBosses[b].english, n) == 0 && s[n] == ':') {
            const int i = atoi(s + n + 1) - 1;
            if (i >= 0 && i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]) && usable(unit, b, i)) {
                return i;
            }
        }
        s = strchr(s, ',');
        s = s != NULL ? s + 1 : NULL;
    }
    return -1;
}

USER_FUNC(evt_pm64boss_pick) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    const Pm64Boss* p;
    BossRt* rt;
    const BossMove* m;
    int i = -1, low, pre = 0, power, tower = 0;
    (void)isFirstCall;
    if (b < 0) {
        evtSetValue(event, args[1], -1);
        return EVT_RETURN_DONE;
    }
    p = &kBosses[b];
    rt = rtOf(unit);
    if (rt->strayed != 0) {
        /* (a companion its last move left at the target when it was cut short: home again by now, pm64boss_finish) */
        const BattleUnit* s = BattleGetUnitPtr(_battleWorkPointer, rt->strayed - 1);
        rt->strayed = 0;
        if (s != NULL && alive(s) && bossLog()) {
            const float dx = s->position.x - s->homePosition.x, dz = s->position.z - s->homePosition.z;
            const float dy = s->position.y - s->homePosition.y;
            if (fabsf(dx) > 2.0f || fabsf(dy) > 2.0f || fabsf(dz) > 2.0f) {
                printf("[pm64] boss: %s's companion %d left away from home (%.0f, %.0f, %.0f off)\n", p->english,
                       s->unitId, dx, dy, dz);
            } else {
                printf("[pm64] boss: %s's companion %d home again\n", p->english, s->unitId);
            }
            fflush(stdout);
        }
    }
    rt->move = -1;
    rt->swell = 0;
    sMateDone = 0;
    low = unit->currentHp * 2 <= unit->maxHp;
    if (p->tower && ((tower = towerTurn(unit, b)) < 0 || tower == 2)) {
        /* (resting; joining a tower that has spun this round: pm64boss_rest has it leap on) */
        evtSetValue(event, args[1], MV_REST);
        evtSetValue(event, args[2], -1);
        return EVT_RETURN_DONE;
    }
    if (_battleWorkPointer != NULL && rt->skipRound == _battleWorkPointer->turnCount + 1) {
        /* (it swarmed with its own kind this round) */
        evtSetValue(event, args[1], MV_REST);
        evtSetValue(event, args[2], -1);
        return EVT_RETURN_DONE;
    }
    /* (tests: a move asked for, while it may be played) */
    if ((i = forcedMove(unit, b)) >= 0) {
        pre = 1;
    }
    /* moves looked at first, ahead of the pattern (Crystal King's Recover, a last stand) */
    for (i = pre ? i : 0; !pre && i < BOSS_MOVES && !moveEnd(&p->moves[i]); i++) {
        const BossMove* q = &p->moves[i];
        if (q->pre && usable(unit, b, i) && !((q->phase == 1 && low) || (q->phase == 2 && !low)) &&
            (q->kind == MV_BARRIER ? barrierChance(unit, i) : chanceOk(unit, q))) {
            pre = 1;
            break;
        }
    }
    if (!pre) {
        if (unit->work[UW_FORCE] > 0 && unit->work[UW_FORCE] <= BOSS_MOVES) {
            /* the warning's follow-up */
            i = (int)unit->work[UW_FORCE] - 1;
            unit->work[UW_FORCE] = 0;
            if ((p->moves[i].phase == 1 && low) || (p->moves[i].phase == 2 && !low)) {
                /* (a follow-up of the other half: its HP has crossed half since; PM64's AfterBitsAttack looks
                   at the HP again) */
                i = pickByWeight(unit, b, low);
            }
        } else if (p->order != NULL && p->order[0] > 0) {
            /* a fixed turn order (Huff N. Puff's twelve turns), round and round */
            if (p->order[rt->order] <= 0) {
                rt->order = 0;
            }
            i = p->order[rt->order++] - 1;
        } else {
            i = pickByWeight(unit, b, low);
        }
        {
            /* (its condition fails: its else1, and that one's, or one by weight) */
            int k;
            for (k = 0; k < 4 && i >= 0 && i < BOSS_MOVES && !usable(unit, b, i) && p->moves[i].else1 > 0; k++) {
                i = p->moves[i].else1 - 1;
            }
            if (!usable(unit, b, i)) {
                i = pickByWeight(unit, b, low);
            }
        }
    }
    if (i < 0 || i >= BOSS_MOVES || moveEnd(&p->moves[i])) {
        i = 0;
    }
    m = &p->moves[i];
    rt->move = (signed char)i;
    rt->swell = m->swell;
    if (rt->uses[i] < 127) {
        rt->uses[i]++;
    }
    rt->lastTurn[i] = (short)unit->work[UW_TURN];
    if (!pre) {
        /* the next turn's move: this one's follow-up (PM64's AVAR_NextMove) */
        unit->work[UW_FORCE] = m->after1 > 0 && m->afterN > 0 && rt->uses[i] >= m->afterN ? m->after1 : m->next1;
    }
    /* its power now: the tower's height (the Spin Attack), or less as its HP goes (byHp) */
    power = m->power;
    if (p->tower && (sTower.up || tower == 1)) {
        int n = 0, j;
        for (j = 0; tower == 1 && j < BATTLE_UNIT_MAX; j++) {
            const BattleUnit* u = unitAt(j);
            const int k = bossOfUnit(u);
            n += alive(u) && u->alliance == 1 && k >= 0 && kBosses[k].tower && rtOf(u)->toppled == 0;
        }
        power = sTower.up ? sTower.height : n;
    } else if ((m->byHp & BY_HP_POWER) && unit->maxHp > 0) {
        power = (m->power * unit->currentHp + unit->maxHp - 1) / unit->maxHp;
        power = power < 1 ? 1 : power;
    }
    /* (a slam with its companions: + perMate for each one standing, Huff N. Puff's Group Slam) */
    power += m->perMate * countUnits(unit, b, 0, -1);
    if (rt->barrier && !m->flat && m->kind < MV_HEAL) {
        /* (the Star Rod's power doubles its attacks: final_bowser_2.c DMG_BOOSTED_*, 8 -> 16, 10 -> 20) */
        power *= 2;
    }
    if (p->home == 0 && m->kind < MV_HEAL) {
        sWeapons[b][i].hpDamageWork[0] = bossPower(power);
    }
    if (p->home == 0 && m->kind == MV_HEAL && sScale > 0) {
        power = scaled(power * 2 / 5, 1); /* (written as PM64's x 2.5: back to PM64's, then scaled) */
    }
    rt->hitsLeft = (signed char)(m->hits > 1 ? m->hits : 1);
    if ((m->byHp & BY_HP_HITS) && m->hits > 1 && unit->maxHp > 0) {
        const int h = (m->hits * unit->currentHp + unit->maxHp - 1) / unit->maxHp;
        rt->hitsLeft = (signed char)(h < 1 ? 1 : h);
    }
    if (tower == 1) {
        sTower.building = (short)(unit->unitId + 1);
        sTower.joining = 0;
        sTower.spun = roundNow();
    }
    {
        if (bossLog()) {
            printf("[pm64] boss: %s turn %d: move %d (kind %d, power %d)\n", p->english, (int)unit->work[UW_TURN],
                   i + 1, m->kind, power);
            fflush(stdout);
        }
        bossKey(unit, "pick", 5);
        bossKey(unit, "move", 50);
        bossKey(unit, "move", 120);
        bossKey(unit, "move", 200);
    }
    evtSetValue(event, args[1], m->kind);
    evtSetValue(event, args[2], m->row);
    evtSetValue(event, args[3], (EvtWord)(intptr_t)&sWeapons[b][i]);
    evtSetValue(event, args[4], (EvtWord)(intptr_t)m->sfx);
    evtSetValue(event, args[5], p->walk);
    evtSetValue(event, args[6], p->jump);
    evtSetValue(event, args[7], power);
    evtSetValue(event, args[8], p->hops);
    return EVT_RETURN_DONE;
}

/* tests ($TTYD_PM64_LOG=1): a line per boss move, and captures through it (bossKey) */
static int bossLog(void) {
    static int sLog = -1;
    if (sLog < 0) {
        const char* log = getenv("TTYD_PM64_LOG");
        sLog = log != NULL && log[0] == '1';
    }
    return sLog;
}

void portShotRequestIn(const char* name, int frames); /* shot_aurora.c */

/* Tests' captures through each move, driven from C at its key frames (no event of their own to die with the move):
 * with $TTYD_PM64_LOG=1, "pm64_<unit>_t<turn>_<k>_<key>" a few frames after the move is picked and timed through
 * it, after each sprite row it plays (wind-up, raise, slam, leap...), as its swarm sets off, and as it hits.
 * Queued in shot_aurora.c, so none is dropped while another is being written. */
static void bossKey(const BattleUnit* unit, const char* key, int frames) {
    static int sLastB = -1, sLastTurn = -1, sK;
    const int b = bossOfUnit(unit);
    char name[64];
    int turn;
    if (b < 0 || !bossLog()) {
        return;
    }
    turn = (int)unit->work[UW_TURN];
    if (b != sLastB || turn != sLastTurn) {
        sLastB = b;
        sLastTurn = turn;
        sK = 0;
    }
    snprintf(name, sizeof(name), "pm64_%02d_t%02d_%02d_%s", b, turn, sK++, key);
    portShotRequestIn(name, frames);
    printf("[pm64] key: %s in %d\n", name, frames);
    fflush(stdout);
}

/* in: the unit, a sprite row (-1: by its state) */
USER_FUNC(evt_pm64boss_row) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int row = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (unit != NULL) {
        const int b = bossOfUnit(unit);
        if (row >= 0 && unit->work[UW_ANIM] != row + 1) {
            char key[16];
            snprintf(key, sizeof(key), "row%d", row);
            bossKey(unit, key, 4);
        }
        /* (-3 its walk row, -4 its hurt row) */
        unit->work[UW_ANIM] = row >= 0 ? row + 1 : row == -3 && b >= 0 ? kBosses[b].walk + 1 : row == -4 && b >= 0 ? kBosses[b].hurt + 1 : 0;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: the unit that takes its place now that it has fallen (its setup; 0 none or still up), and
 * the fallen one's walk and the new one's walk rows */
USER_FUNC(evt_pm64boss_fallen) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    int next;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    if (b < 0 || (next = kBosses[b].spawn1 - 1) < 0 || next >= BOSSES) {
        return EVT_RETURN_DONE;
    }
    /* after the damage's reaction (each hit restarts the damage event, and a hit before the attack's last waits
       for its end, so this runs once the attack is over); bossDamagePre marked it (UW_GONE 1) and kept it at
       1 HP or more: it flees, it isn't beaten */
    if (unit->work[UW_GONE] != 1) {
        return EVT_RETURN_DONE;
    }
    unit->work[UW_GONE] = 2;
    evtSetValue(event, args[1], (EvtWord)(intptr_t)&sSpawn[next]);
    evtSetValue(event, args[2], kBosses[b].walk);
    evtSetValue(event, args[3], kBosses[next].walk);
    return EVT_RETURN_DONE;
}


/* --- engine 2: companions, summons, clones, phases, the tower ------------------------------------------------ */

#define ARG_UNIT(n) BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[n])))

/* --- its Paper Mario sounds (pm64_boss.h Pm64BossSounds, a move's sfx/wind/go) ---
 * Each sound the engine played stays the stand-in of the unit's own PM64 sound there ("SOUND_X|SFX_Y",
 * port/pm64_sfx.h): with a ROM linked and "pm64_sfx" on PM64's plays, with its timing and 3D place; otherwise the
 * TTYD sound, as before. Where the engine played nothing, PM64's plays alone, with a ROM only. */
#define BOSS_SFX_VOLUME 100 /* a PM64 sound alone, centred (0-127) */
enum {
    BS_NAME = -1, /* the stand-in argument is a whole name: a move's sfx ("SOUND_X|SFX_Y", "SOUND_X|" or TTYD's) */
    BS_JUMP, BS_LAND, BS_HEAL, BS_POWER, BS_SUMMON, BS_CLONES0, BS_CLONES1, BS_INHALE, BS_CAST0, BS_CAST1,
    BS_BARRIER0, BS_BARRIER1, BS_EXIT, BS_SWARM0, BS_SWARM1, BS_SWARM2, BS_SWARM3, BS_FLIP, BS_GO
};

/* plays pm64 (a SOUND_ name, or a whole "SOUND_X|SFX_Y" / "SOUND_X|" name) for unit with stand-in standin (NULL:
 * none was played there); pm64 NULL: the stand-in as it was */
static void bossSnd(BattleUnit* unit, const char* pm64, const char* standin) {
    static char sNames[4][96];
    static unsigned sNext;
    const char* bar = pm64 != NULL ? strchr(pm64, '|') : NULL;
    if (unit == NULL) {
        return;
    }
    if (bar != NULL) {
        if (bar[1] != '\0') {
            BtlUnit_snd_se(unit, pm64, EVT_NULLPTR, 0);
        } else {
            char name[64];
            const size_t n = (size_t)(bar - pm64) < sizeof(name) - 1 ? (size_t)(bar - pm64) : sizeof(name) - 1;
            memcpy(name, pm64, n);
            name[n] = '\0';
            portPm64SfxPlayName(name, BOSS_SFX_VOLUME);
        }
    } else if (pm64 == NULL) {
        if (standin != NULL) {
            BtlUnit_snd_se(unit, standin, EVT_NULLPTR, 0);
        }
    } else if (standin == NULL) {
        portPm64SfxPlayName(pm64, BOSS_SFX_VOLUME);
    } else {
        char* name = sNames[sNext++ % 4];
        snprintf(name, sizeof(sNames[0]), "%s|%s", pm64, standin);
        BtlUnit_snd_se(unit, name, EVT_NULLPTR, 0);
    }
}

/* the unit's own PM64 sound `which` (BS_*), or NULL */
static const char* bossSndOf(const BattleUnit* unit, int which) {
    const int b = bossOfUnit(unit);
    const Pm64BossSounds* s = b >= 0 ? kBosses[b].sounds : NULL;
    const BossRt* rt = unit != NULL ? rtOf(unit) : NULL;
    if (b < 0) {
        return NULL;
    }
    if (which == BS_GO) {
        return rt->move >= 0 ? kBosses[b].moves[rt->move].go : NULL;
    }
    if (s == NULL) {
        return NULL;
    }
    switch (which) {
    case BS_JUMP: return s->jump;
    case BS_LAND: return s->land;
    case BS_HEAL: return s->heal;
    case BS_POWER: return s->power;
    case BS_SUMMON: return s->summon;
    case BS_CLONES0: return s->clones[0];
    case BS_CLONES1: return s->clones[1];
    case BS_INHALE: return s->inhale;
    case BS_CAST0: return s->cast[0];
    case BS_CAST1: return s->cast[1];
    case BS_BARRIER0: return s->barrier[0];
    case BS_BARRIER1: return s->barrier[1];
    case BS_EXIT: return s->exit;
    case BS_SWARM0: return s->swarm[0];
    case BS_SWARM1: return s->swarm[1];
    case BS_SWARM2: return s->swarm[2];
    case BS_SWARM3: return s->swarm[3];
    case BS_FLIP: return s->flip;
    default: return NULL;
    }
}

/* in: the unit, which sound (BS_*), the TTYD sound the engine plays there (0: none). Plays the unit's own PM64
 * sound with it as the stand-in (see above) */
USER_FUNC(evt_pm64boss_snd) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int which = evtGetValue(event, args[1]);
    const char* standin = (const char*)(intptr_t)evtGetValue(event, args[2]);
    (void)isFirstCall;
    if (which == BS_NAME) {
        if (standin != NULL && strchr(standin, '|') != NULL) {
            bossSnd(unit, standin, NULL);
        } else {
            bossSnd(unit, NULL, standin);
        }
    } else {
        bossSnd(unit, bossSndOf(unit, which), standin);
    }
    return EVT_RETURN_DONE;
}

/* as evt_pm64boss_snd, placed at another unit (args[3]: the swarm's squeeze on Mario) */
USER_FUNC(evt_pm64boss_snd_at) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    BattleUnit* at = ARG_UNIT(3);
    (void)isFirstCall;
    bossSnd(at != NULL ? at : unit, bossSndOf(unit, evtGetValue(event, args[1])),
            (const char*)(intptr_t)evtGetValue(event, args[2]));
    return EVT_RETURN_DONE;
}

/* where a companion stands: its own `at` from its lead's home, or in line behind it (k of n) */
static void slotPos(const Vec* home, int w, int k, int n, float out[3]) {
    const Pm64Boss* q = &kBosses[w];
    if (q->at[0] != 0 || q->at[1] != 0 || q->at[2] != 0) {
        out[0] = home->x + (float)q->at[0];
        out[1] = (float)q->at[1];
        out[2] = home->z + (float)q->at[2];
        return;
    }
    out[0] = home->x + (n <= 2 ? 90.0f + 60.0f * (float)k : 45.0f * (float)(k + 1));
    out[1] = (float)q->fly;
    out[2] = home->z + 10.0f * (float)(k + 1);
}

/* a companion of kind w in place `slot` is standing */
static int slotTaken(int w, int slot) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        if (alive(u) && bossOfUnit(u) == w && u->work[UW_SLOT] == slot) {
            return 1;
        }
    }
    return 0;
}

static BattleUnitSetup* slotSetup(int w, int slot, const float pos[3]) {
    BattleUnitSetup* s = &sSlotSetup[sSlotNext++ % 16];
    *s = sSpawn[w];
    s->unitWork[0] = slot;
    s->position.x = pos[0];
    s->position.y = pos[1];
    s->position.z = pos[2];
    return s;
}

static void setOut3(EventEntry* event, int n, const float v[3]) {
    EvtWord* args = event->args;
    evtSetValue(event, args[n], (int)v[0]);
    evtSetValue(event, args[n + 1], (int)v[1]);
    evtSetValue(event, args[n + 2], (int)v[2]);
}

/* in: the summoner, 0 its move's (MV_SUMMON) or 1 its with1 (a new phase's companions); out: the next one to
 * bring in (its setup, 0 none), its home, how it comes in. PM64: crystal_king.c EVS_Move_SummonBits (the Bits
 * that are gone come back), tutankoopa.c EVS_Move_SummonChomp. */
USER_FUNC(evt_pm64boss_nextsummon) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int mode = evtGetValue(event, args[1]);
    const int b = bossOfUnit(unit);
    const BossMove* m;
    float pos[3];
    int k, n;
    (void)isFirstCall;
    evtSetValue(event, args[2], 0);
    if (b < 0) {
        return EVT_RETURN_DONE;
    }
    m = mode == 0 && rtOf(unit)->move >= 0 ? &kBosses[b].moves[rtOf(unit)->move] : NULL;
    if (m != NULL && m->unit1 > 0) {
        const int w = m->unit1 - 1;
        n = m->count > 0 ? m->count : 1;
        for (k = 0; k < n; k++) {
            if (!slotTaken(w, 16 + k)) {
                slotPos(&unit->homePosition, w, k, n, pos);
                evtSetValue(event, args[2], (EvtWord)(intptr_t)slotSetup(w, 16 + k, pos));
                setOut3(event, 3, pos);
                evtSetValue(event, args[6], kBosses[w].enter);
                return EVT_RETURN_DONE;
            }
        }
        return EVT_RETURN_DONE;
    }
    for (n = 0; n < BOSS_WITH && kBosses[b].with1[n] > 0; n++) {
    }
    for (k = 0; k < n; k++) {
        const int w = kBosses[b].with1[k] - 1;
        if (!slotTaken(w, k + 1)) {
            slotPos(&unit->homePosition, w, k, n, pos);
            evtSetValue(event, args[2], (EvtWord)(intptr_t)slotSetup(w, k + 1, pos));
            setOut3(event, 3, pos);
            evtSetValue(event, args[6], kBosses[w].enter);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: its next clone to make (setup, 0 none) and where it goes (PM64 crystal_king.c
 * EVS_Move_MakeClones: two copies beside him, ACTOR_EVENT_FLAG_ILLUSORY) */
USER_FUNC(evt_pm64boss_nextclone) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    const BossMove* m;
    float pos[3];
    int w, n, have;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    if (b < 0 || rtOf(unit)->move < 0 || (m = &kBosses[b].moves[rtOf(unit)->move])->kind != MV_CLONES ||
        (w = m->unit1 - 1) < 0) {
        return EVT_RETURN_DONE;
    }
    n = m->count > 0 ? m->count : 1;
    if ((have = countUnits(unit, b, 2, w)) >= n) {
        return EVT_RETURN_DONE;
    }
    slotPos(&unit->homePosition, w, have, n, pos);
    if (kBosses[w].at[0] == 0 && kBosses[w].at[1] == 0 && kBosses[w].at[2] == 0) {
        /* (in a row with it, either side) */
        pos[0] = unit->homePosition.x + (have % 2 == 0 ? 55.0f : -55.0f) * (float)(have / 2 + 1);
        pos[1] = unit->homePosition.y;
        pos[2] = unit->homePosition.z - 5.0f;
    }
    evtSetValue(event, args[1], (EvtWord)(intptr_t)slotSetup(w, 48 + have, pos));
    setOut3(event, 2, pos);
    return EVT_RETURN_DONE;
}

/* in: a new clone, its maker: the clone at its maker's HP */
USER_FUNC(evt_pm64boss_cloneup) {
    EvtWord* args = event->args;
    BattleUnit* clone = ARG_UNIT(0);
    BattleUnit* lead = ARG_UNIT(1);
    (void)isFirstCall;
    if (clone != NULL && lead != NULL) {
        clone->maxHp = lead->maxHp;
        clone->currentHp = lead->currentHp;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: it and its clones trade places (which is which, Mario has to guess) */
USER_FUNC(evt_pm64boss_shuffle) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    BattleUnit* pick[8];
    int i, n = 0;
    (void)isFirstCall;
    if (b < 0) {
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < BATTLE_UNIT_MAX && n < 8; i++) {
        BattleUnit* u = unitAt(i);
        if (alive(u) && u->alliance == 1 && cloneKind(b, bossOfUnit(u)) && kBosses[bossOfUnit(u)].illusory) {
            pick[n++] = u;
        }
    }
    if (n > 0 && (i = irand(n + 1)) < n) {
        const Vec a = unit->homePosition, c = pick[i]->homePosition;
        BtlUnit_SetHomePos(unit, c.x, c.y, c.z);
        BtlUnit_SetPos(unit, c.x, c.y, c.z);
        BtlUnit_SetHomePos(pick[i], a.x, a.y, a.z);
        BtlUnit_SetPos(pick[i], a.x, a.y, a.z);
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: the next companion its move uses (-1 none): with a gang, itself first and then the others
 * of its kind (who sit their own turns out this round); else its companions (Spit Bits, a swallow) */
USER_FUNC(evt_pm64boss_nextmate) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    const int gang = b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].gang;
    int i;
    (void)isFirstCall;
    evtSetValue(event, args[1], -1);
    if (b < 0) {
        return EVT_RETURN_DONE;
    }
    if (gang && !(sMateDone & (1ull << (unit->unitId % 64)))) {
        sMateDone |= 1ull << (unit->unitId % 64);
        evtSetValue(event, args[1], unit->unitId);
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = unitAt(i);
        const int k = bossOfUnit(u);
        if (!alive(u) || u == unit || u->alliance != 1 || k < 0 || (sMateDone & (1ull << i)) ||
            !(gang ? k == b : mateKind(b, k))) {
            continue;
        }
        sMateDone |= 1ull << i;
        if (gang && _battleWorkPointer != NULL) {
            rtOf(u)->skipRound = (short)(_battleWorkPointer->turnCount + 1);
        }
        evtSetValue(event, args[1], u->unitId);
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 its companions come back after their hit (a gang), 0 they're used up (Spit Bits) */
USER_FUNC(evt_pm64boss_mateback) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[1], b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].gang);
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 its move hits once per companion (BOSS_HITS_MATES) */
USER_FUNC(evt_pm64boss_mateshits) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[1],
                b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].hits == BOSS_HITS_MATES);
    return EVT_RETURN_DONE;
}

/* in: the unit, the target; out: its move's weapon on that target (the partner's own power: General Guy's
 * Lightning, partner 2, Mario 5) */
USER_FUNC(evt_pm64boss_weapon) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    BattleUnit* target = ARG_UNIT(1);
    const int b = bossOfUnit(unit);
    int i;
    (void)isFirstCall;
    if (b < 0 || (i = rtOf(unit)->move) < 0) {
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, args[2],
                (EvtWord)(intptr_t)(kBosses[b].moves[i].partner > 0 && target != NULL && target->alliance == 0 &&
                                            target->currentKind != UNIT_MARIO
                                        ? &sWeaponsP[b][i]
                                        : &sWeapons[b][i]));
    return EVT_RETURN_DONE;
}

/* a target that's down: KO'd (its HP gone; a Life Shroom revival may be under way) */
static int downed(const BattleUnit* u) {
    return u == NULL || u->currentHp <= 0 || BtlUnit_CheckStatus((BattleUnit*)u, STATUS_INSTANT_KILL);
}

/* in: the unit, the target; out: 1 its move has another hit to come. None once the target is down (as TTYD's own
 * multi-hit enemies stop: a hit on a KO'd Mario would restart his damage event and cut a Life Shroom off) */
USER_FUNC(evt_pm64boss_morehits) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    BattleUnit* target = ARG_UNIT(1);
    int more;
    (void)isFirstCall;
    more = unit != NULL && --rtOf(unit)->hitsLeft > 0;
    if (more && downed(target)) {
        if (bossLog() && bossOfUnit(unit) >= 0) {
            printf("[pm64] boss: %s's hits stop: the target is down (%d to come)\n", kBosses[bossOfUnit(unit)].english,
                   rtOf(unit)->hitsLeft);
            fflush(stdout);
        }
        rtOf(unit)->hitsLeft = 0;
        more = 0;
    }
    evtSetValue(event, args[2], more);
    return EVT_RETURN_DONE;
}

/* in: the unit, the target, the pre-check's result (tests: each hit a boss's move tries, TTYD_PM64_LOG) */
USER_FUNC(evt_pm64boss_hitlog) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    BattleUnit* target = ARG_UNIT(1);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && bossLog()) {
        printf("[pm64] boss: %s's hit on kind %d (HP %d): pre-check %d\n", kBosses[b].english,
               target != NULL ? (int)target->currentKind : -1, target != NULL ? (int)target->currentHp : -1,
               (int)evtGetValue(event, args[2]));
        fflush(stdout);
    }
    return EVT_RETURN_DONE;
}

/* in: the target; out: 1 it's down (a volley stops: no more companions fly at it) */
USER_FUNC(evt_pm64boss_down) {
    EvtWord* args = event->args;
    (void)isFirstCall;
    evtSetValue(event, args[1], downed(ARG_UNIT(0)));
    return EVT_RETURN_DONE;
}

/* in: the unit, the companion its move sends at the target (-1: it's back, or gone) */
USER_FUNC(evt_pm64boss_mateout) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int id = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (unit != NULL) {
        BattleUnit* m = id >= 0 ? ARG_UNIT(1) : NULL;
        rtOf(unit)->out = (signed char)(m != NULL ? m->unitId + 1 : 0);
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 it's in the middle of a move (picked, not yet done) */
USER_FUNC(evt_pm64boss_midmove) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    const int mid = b >= 0 && rtOf(unit)->move >= 0;
    (void)isFirstCall;
    if (mid && bossLog()) {
        printf("[pm64] boss: %s's move %d cut short (countered or superguarded): its end run by its damage event\n",
               kBosses[b].english, rtOf(unit)->move + 1);
        fflush(stdout);
    }
    evtSetValue(event, args[1], mid);
    return EVT_RETURN_DONE;
}

/* in: the unit whose move was cut short (countered, superguarded); out: the companion it left at the target (-1
 * none; the unit itself is walked home by its damage event), 1 it goes home (a gang), 0 it's used up (Spit Bits) */
USER_FUNC(evt_pm64boss_stray) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    BattleUnit* m;
    (void)isFirstCall;
    evtSetValue(event, args[1], -1);
    evtSetValue(event, args[2], 0);
    if (b < 0 || rtOf(unit)->out == 0) {
        return EVT_RETURN_DONE;
    }
    m = BattleGetUnitPtr(_battleWorkPointer, rtOf(unit)->out - 1);
    rtOf(unit)->out = 0;
    if (m == NULL || m == unit || !alive(m)) {
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, args[1], m->unitId);
    evtSetValue(event, args[2], rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].gang);
    if (rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].gang) {
        rtOf(unit)->strayed = (signed char)(m->unitId + 1);
        if (bossLog()) {
            printf("[pm64] boss: %s's cut move sends its companion %d home\n", kBosses[b].english, m->unitId);
            fflush(stdout);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit, the wind-up's frames by default; out: its frames (the move's windup, PM64 frames). Its wind-up
 * row on. */
USER_FUNC(evt_pm64boss_windup) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    int frames = evtGetValue(event, args[1]);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0) {
        const BossMove* m = &kBosses[b].moves[rtOf(unit)->move];
        unit->work[UW_ANIM] = m->row + 1;
        if (m->windup > 0) {
            frames = PM64_FRAMES(m->windup);
        }
        /* its PM64 sound as the wind-up starts (ROM only); the Koopa Bros.' tower spins up by its height
           (common_koopa_bros.inc.c BOSS_CMD_SPIN_ATTACK: SOUND_KOOPA_BROS_TOWER_SPIN_3 at 4, _2 at 3, _1 at 2) */
        if (kBosses[b].tower && sTower.up && sTower.height >= 2) {
            bossSnd(unit, sTower.height >= 4   ? "SOUND_KOOPA_BROS_TOWER_SPIN_3"
                          : sTower.height == 3 ? "SOUND_KOOPA_BROS_TOWER_SPIN_2"
                                               : "SOUND_KOOPA_BROS_TOWER_SPIN_1",
                    NULL);
        } else if (m->wind != NULL) {
            bossSnd(unit, m->wind, NULL);
        }
    }
    evtSetValue(event, args[2], frames);
    return EVT_RETURN_DONE;
}

/* in: the unit: its release row on (a two-pose move's second) */
USER_FUNC(evt_pm64boss_release) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].release > 0) {
        unit->work[UW_ANIM] = kBosses[b].moves[rtOf(unit)->move].release + 1;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: its row once the hit has landed (a move's post: PostExhale, ExitShell), if it has one */
USER_FUNC(evt_pm64boss_post) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].post > 0) {
        unit->work[UW_ANIM] = kBosses[b].moves[rtOf(unit)->move].post + 1;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: the frames the stage takes to darken for its move (0: it doesn't). PM64 huff_n_puff.c
 * EVS_Attack_GroundLightning SetBackgroundAlpha 0 -> 200 over 60 frames; final_jr_troopa.c and general_guy.c's
 * lightning the same way */
USER_FUNC(evt_pm64boss_darken) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    int frames = 0;
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0 && kBosses[b].moves[rtOf(unit)->move].darken > 0) {
        frames = PM64_FRAMES(kBosses[b].moves[rtOf(unit)->move].darken);
        rtOf(unit)->darkened = 1;
    }
    evtSetValue(event, args[1], frames);
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 its move darkened the stage (it lightens again) */
USER_FUNC(evt_pm64boss_lighten) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    evtSetValue(event, args[1], unit != NULL && rtOf(unit)->darkened);
    if (unit != NULL) {
        rtOf(unit)->darkened = 0;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: the hits each companion of a volley lands (its move's mateHits) */
USER_FUNC(evt_pm64boss_matehits) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0) {
        const int n = kBosses[b].moves[rtOf(unit)->move].mateHits;
        rtOf(unit)->hitsLeft = (signed char)(n > 1 ? n : 1);
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: its move's own line now (the first time; 0 none), 1 if it's from off the field */
USER_FUNC(evt_pm64boss_movemsg) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    const int i = b >= 0 ? rtOf(unit)->move : -1;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    evtSetValue(event, args[2], 0);
    if (i >= 0 && kBosses[b].moves[i].msg != 0 && rtOf(unit)->uses[i] == 1 && sayOnce(b, 3 + i)) {
        evtSetValue(event, args[1], (EvtWord)(intptr_t)sMoveTag[b][i]);
        evtSetValue(event, args[2], kBosses[b].moves[i].offstage);
    }
    return EVT_RETURN_DONE;
}


/* in: the unit; out: its line at this turn's start (its first turn's, or the first below its lowAt; 0 none), its
 * talk row (-1: as it is). PM64 final_jr_troopa.c: MSG_CH8_005D (EVS_HandlePhase, the first time), MSG_CH8_0060
 * (EVS_TakeTurn, the first turn under 15 HP) */
USER_FUNC(evt_pm64boss_turnmsg) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    evtSetValue(event, args[2], b >= 0 && kBosses[b].talkRow > 0 ? kBosses[b].talkRow : -1);
    if (b < 0) {
        return EVT_RETURN_DONE;
    }
    if (kBosses[b].startMsg != 0 && unit->work[UW_TURN] == 1 && sayOnce(b, 1)) {
        evtSetValue(event, args[1], (EvtWord)(intptr_t)sLineTag[b][2]);
    } else if (kBosses[b].lowMsg != 0 && !rtOf(unit)->saidLow &&
               unit->currentHp * 100 < kBosses[b].lowAt * unit->maxHp && sayOnce(b, 2)) {
        rtOf(unit)->saidLow = 1;
        evtSetValue(event, args[1], (EvtWord)(intptr_t)sLineTag[b][3]);
    }
    return EVT_RETURN_DONE;
}
/* in: the unit: its move is over (its swell gone; an attack spends a gathered strength) */
USER_FUNC(evt_pm64boss_done) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0) {
        BossRt* rt = rtOf(unit);
        if (rt->move >= 0 && kBosses[b].moves[rt->move].kind < MV_HEAL) {
            /* (any attack spends a gathered strength: its angry idle, its glow, its charged palette and
               hazard go) */
            unit->work[UW_CHARGED] = 0;
            if (rt->armed != 0) {
                armHazard(unit, b, 0);
            }
        }
        rt->swell = 0;
        rt->move = -1;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 it got up off its back this turn */
USER_FUNC(evt_pm64boss_getup) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    evtSetValue(event, args[1], unit != NULL && rtOf(unit)->gotUp);
    if (unit != NULL) {
        rtOf(unit)->gotUp = 0;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: the next brother to climb into the tower it builds (-1 done) and his place. PM64
 * fake_bowser.c EVS_TryFormingTower: the standing ones leap onto each other at the front one's place, the
 * builder on top */
USER_FUNC(evt_pm64boss_towernext) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    BattleUnit* u;
    int i;
    (void)isFirstCall;
    evtSetValue(event, args[1], -1);
    if (unit == NULL || sTower.building != unit->unitId + 1) {
        return EVT_RETURN_DONE;
    }
    if (!sTower.up) {
        int n = 0;
        for (i = BATTLE_UNIT_MAX - 1; i >= 0 && n < 3; i--) {
            const int k = bossOfUnit(u = unitAt(i));
            if (alive(u) && u != unit && u->alliance == 1 && k >= 0 && kBosses[k].tower && rtOf(u)->toppled == 0) {
                sTower.ids[n++] = (short)u->unitId;
            }
        }
        sTower.ids[n++] = (short)unit->unitId;
        for (i = 0; i < n; i++) {
            u = unitAt(sTower.ids[i]);
            sTower.ground[i][0] = u->homePosition.x;
            sTower.ground[i][1] = u->homePosition.y;
            sTower.ground[i][2] = u->homePosition.z;
        }
        sTower.height = (signed char)n;
        sTower.up = 1;
        sTower.tipped = 0;
        sTower.saidOver = 0;
        sTower.placed = 0;
        sTower.x = sTower.ground[0][0];
        for (i = 1; i < n; i++) {
            sTower.x = sTower.ground[i][0] < sTower.x ? sTower.ground[i][0] : sTower.x;
        }
    }
    if (sTower.placed >= sTower.height) {
        /* (it stands: the top one's line the first time it's built, not as one joins) */
        sTower.built = !sTower.joining;
        sTower.joining = 0;
        sTower.building = 0;
        return EVT_RETURN_DONE;
    }
    {
        float pos[3];
        u = unitAt(sTower.ids[sTower.placed]);
        pos[0] = sTower.x;
        pos[1] = (float)sTower.placed * (float)(u != NULL ? u->height : 40) * 0.6f;
        pos[2] = 0.0f;
        evtSetValue(event, args[1], sTower.ids[sTower.placed]);
        setOut3(event, 2, pos);
        sTower.placed++;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 the tower has just toppled (each brother on his back 2 turns at defence 0) */
USER_FUNC(evt_pm64boss_toppling) {
    EvtWord* args = event->args;
    int i;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    if (!sTower.falling) {
        return EVT_RETURN_DONE;
    }
    sTower.falling = 0;
    sTower.up = 0;
    sTower.tipped = 0;
    sTower.placed = 0;
    sTower.sayOver = 0;
    sTower.saidOver = 0; /* (fake_bowser.c EVS_Broadcast_ToppleHit clears AFLAG_Boss_Dialogue_WereGoingOver) */
    for (i = 0; i < sTower.height; i++) {
        BattleUnit* u = unitAt(sTower.ids[i]);
        const int k = bossOfUnit(u);
        if (u != NULL && k >= 0) {
            rtOf(u)->toppled = 2;
            setDefence(k, 0);
        }
    }
    evtSetValue(event, args[1], 1);
    return EVT_RETURN_DONE;
}

/* out: the next toppled brother (-1 none) and his place on the ground */
USER_FUNC(evt_pm64boss_towerfall) {
    EvtWord* args = event->args;
    (void)isFirstCall;
    evtSetValue(event, args[0], -1);
    if (sTower.placed < sTower.height) {
        evtSetValue(event, args[0], sTower.ids[sTower.placed]);
        setOut3(event, 1, sTower.ground[sTower.placed]);
        sTower.placed++;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit, 0 (after a hit: the tower tips) or 1 (it has just gone up); out: the top brother (speaker) and his
 * line (0 none). PM64 fake_bowser.c: MSG_CH1_0109 by the top one as the tower tips (EVS_KoopaBros_HandleEvent,
 * once per tipping: AFLAG_Boss_Dialogue_WereGoingOver), MSG_CH1_010A by the top one once the tower first stands
 * (EVS_BuildTowerWithKoopa, AFLAG_Boss_Dialogue_HaveATaste) */
USER_FUNC(evt_pm64boss_towerline) {
    EvtWord* args = event->args;
    const int which = evtGetValue(event, args[1]);
    const BattleUnit* top;
    int k;
    (void)isFirstCall;
    evtSetValue(event, args[2], -1);
    evtSetValue(event, args[3], 0);
    if (!sTower.up || sTower.height <= 0 || (top = unitAt(sTower.ids[sTower.height - 1])) == NULL ||
        (k = bossOfUnit(top)) < 0) {
        sTower.sayOver = 0;
        sTower.built = 0;
        return EVT_RETURN_DONE;
    }
    if (which == 0 && sTower.sayOver) {
        sTower.sayOver = 0;
        if (!sTower.saidOver && kBosses[k].tipMsg != 0) {
            sTower.saidOver = 1;
            evtSetValue(event, args[2], top->unitId);
            evtSetValue(event, args[3], (EvtWord)(intptr_t)sLineTag[k][LINE_TIP]);
        }
    } else if (which == 1 && sTower.built) {
        sTower.built = 0;
        if (!sTower.saidBuild && kBosses[k].buildMsg != 0) {
            sTower.saidBuild = 1;
            evtSetValue(event, args[2], top->unitId);
            evtSetValue(event, args[3], (EvtWord)(intptr_t)sLineTag[k][LINE_BUILD]);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 it has just been flipped onto its back (a lone tower brother jumped on: PM64
 * common_koopa_bros.inc.c EVS_HandleEvent EVENT_FLIP_TRIGGER, toppled 2 turns at ToppledDefense 0) */
USER_FUNC(evt_pm64boss_flip) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    evtSetValue(event, args[1], unit != NULL && rtOf(unit)->flip && unit->currentHp > 0);
    if (unit != NULL) {
        rtOf(unit)->flip = 0;
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: its barrier goes up (PM64 final_bowser_1.c EVS_StarRodCast: ACTOR_EVENT_FLAG_STAR_ROD_ENCHANTED,
 * BoostedDefense 99, its attacks the DMG_BOOSTED_* ones, recoloured) */
USER_FUNC(evt_pm64boss_barrier) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0) {
        rtOf(unit)->barrier = 1;
        setDefence(b, 99);
        if (bossLog()) {
            printf("[pm64] boss: %s raises the Star Rod barrier\n", kBosses[b].english);
            fflush(stdout);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 0 nothing, 1 a hit bounced off its barrier, 2 a Star Power special broke it; its line then
 * (0 none: the first time each, PM64 final_bowser_1.c MSG_CH8_0085 as an attack has no effect, final_bowser_2.c
 * MSG_CH8_00A7 as the Peach Beam breaks it), the barrier's hint (0 none: the first bounce) */
USER_FUNC(evt_pm64boss_barrierhit) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    BossRt* rt;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    evtSetValue(event, args[2], 0);
    evtSetValue(event, args[3], 0);
    if (b < 0 || unit->currentHp <= 0) {
        return EVT_RETURN_DONE;
    }
    rt = rtOf(unit);
    if (rt->broke) {
        evtSetValue(event, args[1], 2);
        if (kBosses[b].breakMsg != 0 && sayOnce(b, SAID_BREAK)) {
            evtSetValue(event, args[2], (EvtWord)(intptr_t)sLineTag[b][LINE_BREAK]);
        }
    } else if (rt->bounced) {
        evtSetValue(event, args[1], 1);
        if (kBosses[b].bounceMsg != 0 && sayOnce(b, SAID_BOUNCE)) {
            evtSetValue(event, args[2], (EvtWord)(intptr_t)sLineTag[b][LINE_BOUNCE]);
        }
        if (sayOnce(b, SAID_HINT)) {
            evtSetValue(event, args[3], (EvtWord)(intptr_t)kHintTag);
        }
    }
    rt->broke = 0;
    rt->bounced = 0;
    return EVT_RETURN_DONE;
}

/* in: the unit; out: its talk row (-1 none) */
USER_FUNC(evt_pm64boss_talkrow) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[1], b >= 0 && kBosses[b].talkRow > 0 ? kBosses[b].talkRow : -1);
    return EVT_RETURN_DONE;
}

/* in: the unit; out: its row as an attack bounces off its barrier (-4: its hurt row) */
USER_FUNC(evt_pm64boss_mockrow) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[1], b >= 0 && kBosses[b].mockRow > 0 ? kBosses[b].mockRow : -4);
    return EVT_RETURN_DONE;
}

/* in: the unit: once the fight's last unit falls for good, its companions fall with it (the fight is over) */
USER_FUNC(evt_pm64boss_fell) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    int i;
    (void)isFirstCall;
    /* its death sound, once, as it falls for good (not as it gives way to the unit that takes its place) */
    if (b >= 0 && unit->currentHp <= 0 && (kBosses[b].spawn1 == 0 || kBosses[b].exit == EXIT_BEATEN) &&
        !kBosses[b].illusory && !rtOf(unit)->died) {
        rtOf(unit)->died = 1;
        if (kBosses[b].sounds != NULL && kBosses[b].sounds->death != NULL) {
            bossSnd(unit, kBosses[b].sounds->death, NULL);
        }
    }
    if (b < 0 || unit->currentHp > 0 || !sMain[b] || kBosses[b].spawn1 > 0 || kBosses[b].tower) {
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = unitAt(i);
        if (alive(u) && u != unit && u->alliance == 1) {
            u->currentHp = 0;
            BtlUnit_ClearStatus(u);
            BtlUnit_SetStatus(u, STATUS_INSTANT_KILL, 1, 1);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: 1 it's a clone that has been hit (it vanishes) */
USER_FUNC(evt_pm64boss_vanish) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    evtSetValue(event, args[1], unit != NULL && rtOf(unit)->vanish);
    return EVT_RETURN_DONE;
}

/* in: the unit; out: one of its clones (they vanish when it is hit; -1 none) */
USER_FUNC(evt_pm64boss_nextclonegone) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    int i;
    (void)isFirstCall;
    evtSetValue(event, args[1], -1);
    for (i = 0; b >= 0 && !kBosses[b].illusory && i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = unitAt(i);
        const int k = bossOfUnit(u);
        if (alive(u) && u->alliance == 1 && k >= 0 && kBosses[k].illusory && cloneKind(b, k)) {
            evtSetValue(event, args[1], u->unitId);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit; out: the piece a hit broke off it (setup, 0 none) and its place (Tuff Puffs off Huff N. Puff) */
USER_FUNC(evt_pm64boss_shed) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    float pos[3];
    int k, n, w;
    (void)isFirstCall;
    evtSetValue(event, args[1], 0);
    if (b < 0 || !rtOf(unit)->shed || (w = kBosses[b].shed1 - 1) < 0 || unit->currentHp <= 0) {
        return EVT_RETURN_DONE;
    }
    rtOf(unit)->shed = 0;
    n = kBosses[b].shedMax > 0 ? kBosses[b].shedMax : 4;
    for (k = 0; k < n; k++) {
        if (!slotTaken(w, 32 + k)) {
            slotPos(&unit->homePosition, w, k, n, pos);
            evtSetValue(event, args[1], (EvtWord)(intptr_t)slotSetup(w, 32 + k, pos));
            setOut3(event, 2, pos);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the fallen unit, the new one; out: one of the fallen one's companions the new one's replace (revive 1:
 * Lava Piranha's Buds rise again on fire; -1 none) */
USER_FUNC(evt_pm64boss_nextoldmate) {
    EvtWord* args = event->args;
    BattleUnit* old = ARG_UNIT(0);
    BattleUnit* neu = ARG_UNIT(1);
    const int bo = bossOfUnit(old), bn = bossOfUnit(neu);
    int i;
    (void)isFirstCall;
    evtSetValue(event, args[2], -1);
    if (bo < 0 || bn < 0 || kBosses[bn].revive != 1) {
        return EVT_RETURN_DONE;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = unitAt(i);
        const int k = bossOfUnit(u);
        if (alive(u) && u != old && u != neu && u->alliance == 1 && mateKind(bo, k) && !mateKind(bn, k)) {
            evtSetValue(event, args[2], u->unitId);
            return EVT_RETURN_DONE;
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the fallen unit, the new one's setup; out: how it gives way (EXIT_*), how the new one comes in (ENTER_*),
 * the new one's revive, the new one's home (the fallen one's, at the new one's height) */
USER_FUNC(evt_pm64boss_exitinfo) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const BattleUnitSetup* setup = (const BattleUnitSetup*)evtGetValue(event, args[1]);
    const int b = bossOfUnit(unit);
    const int n = setup != NULL ? bossOfParams(setup->unitKindParams) : -1;
    float pos[3];
    (void)isFirstCall;
    if (b < 0 || n < 0) {
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, args[2], kBosses[b].exit);
    evtSetValue(event, args[3], kBosses[n].enter);
    evtSetValue(event, args[4], kBosses[n].revive);
    /* (the fallen one a companion, a twin of a wave (its place UW_SLOT set): the new wave stands where the fight's
       lead stood, not at the twin's place beside it. Each wave once took the last-fallen twin's home, 90 to the
       right each time: General Guy at 270 and his Light Bulb at 301, off the stage) */
    pos[0] = unit->work[UW_SLOT] > 0 ? sLeadHome[0] : unit->homePosition.x;
    pos[1] = (float)kBosses[n].fly;
    pos[2] = unit->work[UW_SLOT] > 0 ? sLeadHome[2] : unit->homePosition.z;
    setOut3(event, 5, pos);
    return EVT_RETURN_DONE;
}

/* in: the unit: its state for a new fight (or a new unit) */
USER_FUNC(evt_pm64boss_reset) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    if (unit != NULL) {
        BossRt* rt = rtOf(unit);
        memset(rt, 0, sizeof(*rt));
        rt->move = -1;
    }
    return EVT_RETURN_DONE;
}

/* in: a unit that has just come in (a hand-off, a summon): where it stands, logged, "on stage" when it stands
 * within the stage's width and is drawn (the tests: pm64_boss_handoff_*; a transform once left the new form at
 * x 400, off the screen and with no home set, until its next attack); with $TTYD_PM64_LOG a capture
 * "pm64_<unit>_in<n>" a few frames on */
USER_FUNC(evt_pm64boss_entered) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0) {
        static int sShots;
        const int drawn = !(unit->attributeFlags & UNIT_ATTR_HIDDEN) &&
                          (unit->parts == NULL || !(unit->parts->attributes & PART_ATTR_HIDDEN));
        const int on = drawn && fabsf(unit->position.x) <= 240.0f && fabsf(unit->position.x - unit->homePosition.x) < 2.0f;
        printf("[pm64] boss: %s came in at x %.0f y %.0f (home %.0f), drawn %d: %s\n", kBosses[b].english,
               unit->position.x, unit->position.y, unit->homePosition.x, drawn, on ? "on stage" : "OFF STAGE");
        fflush(stdout);
        if (bossLog()) {
            char name[48];
            snprintf(name, sizeof(name), "pm64_%02d_in%d", b, sShots++);
            portShotRequestIn(name, 6);
        }
    }
    return EVT_RETURN_DONE;
}

/* a unit comes in (LW(3) the unit, LW(5..7) its home, LW(14) ENTER_*): drawn from where it starts coming in (a
 * hand-off spawns it hidden at its home: pm64boss_handoff2), its home set first, so a move cut short still goes
 * back there; at the end it logs where it stands (evt_pm64boss_entered) */
#define BOSS_HIDE (UNIT_ATTR_HIDDEN | UNIT_ATTR_NO_SHADOW)
static EVT_BEGIN(pm64boss_enter)
    CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
    SWITCH(LW(14))
        CASE_EQUAL(ENTER_PLACE)
            CALL(btlevtcmd_SetPos, LW(3), LW(5), LW(6), LW(7))
            CALL(btlevtcmd_OffAttribute, LW(3), BOSS_HIDE)
            CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(5), LW(6), LW(7), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            WAIT_FRAMES(20)
        CASE_EQUAL(ENTER_DROP)
            SET_VALUE(LW(0), LW(6))
            ADD(LW(0), 240)
            CALL(btlevtcmd_SetPos, LW(3), LW(5), LW(0), LW(7))
            CALL(btlevtcmd_OffAttribute, LW(3), BOSS_HIDE)
            CALL(btlevtcmd_JumpSetting, LW(3), 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, LW(3), LW(5), LW(6), LW(7), 30, -1)
            CALL(evt_pm64boss_snd, LW(3), BS_LAND, 0)
            CALL(evt_btl_camera_shake_h, 0, 3, 0, 12, 0)
        CASE_EQUAL(ENTER_LEFT)
            CALL(btlevtcmd_SetPos, LW(3), -300, LW(6), LW(7))
            CALL(btlevtcmd_OffAttribute, LW(3), BOSS_HIDE)
            CALL(evt_pm64boss_row, LW(3), -3)
            CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(3.0f))
            CALL(btlevtcmd_MovePosition, LW(3), LW(5), LW(6), LW(7), 0, -1, 0)
        CASE_DEFAULT
            CALL(btlevtcmd_SetPos, LW(3), 300, LW(6), LW(7))
            CALL(btlevtcmd_OffAttribute, LW(3), BOSS_HIDE)
            CALL(evt_pm64boss_row, LW(3), -3)
            CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(3.0f))
            CALL(btlevtcmd_MovePosition, LW(3), LW(5), LW(6), LW(7), 0, -1, 0)
    END_SWITCH
    CALL(btlevtcmd_SetPos, LW(3), LW(5), LW(6), LW(7))
    CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
    CALL(evt_pm64boss_row, LW(3), -1)
    CALL(evt_pm64boss_entered, LW(3))
    RETURN
EVT_END()

/* LW(8) the summoner, LW(9) 0 its move's summon or 1 its with1: each that's gone comes in */
static EVT_BEGIN(pm64boss_summon)
    LABEL(1)
    CALL(evt_pm64boss_nextsummon, LW(8), LW(9), LW(13), LW(5), LW(6), LW(7), LW(14))
    IF_INT_EQUAL(LW(13), 0)
        RETURN
    END_IF
    CALL(btlevtcmd_SpawnUnit, LW(3), LW(13), 0)
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL_CHILD(pm64boss_enter)
    GOTO(1)
EVT_END()

/* its clones come out of it and they trade places */
static EVT_BEGIN(pm64boss_clones)
    LABEL(1)
    CALL(evt_pm64boss_nextclone, BTL_UNIT_SELF, LW(13), LW(5), LW(6), LW(7))
    IF_INT_NOT_EQUAL(LW(13), 0)
        CALL(btlevtcmd_SpawnUnit, LW(3), LW(13), 0)
        IF_INT_NOT_EQUAL(LW(3), -1)
            CALL(evt_pm64boss_cloneup, LW(3), BTL_UNIT_SELF)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_SetPos, LW(3), LW(0), LW(1), LW(2))
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_CLONES1, STRING("SFX_ENM_MAHO_ATTACK1"))
            CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(4.0f))
            CALL(btlevtcmd_MovePosition, LW(3), LW(5), LW(6), LW(7), 0, -1, 0)
            CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
            GOTO(1)
        END_IF
    END_IF
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
    CALL(evt_pm64boss_shuffle, BTL_UNIT_SELF)
    WAIT_FRAMES(20)
    RETURN
EVT_END()

/* it draws its companions in and heals LW(9) HP for each (PM64 huff_n_puff.c EVS_Move_HealOrSlam's inhale) */
USER_FUNC(evt_pm64boss_healfx); /* (a heal's look: with the moves' looks, below) */

static EVT_BEGIN(pm64boss_swallow)
    SET_VALUE(LW(6), 0)
    CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_INHALE, 0)
    LABEL(1)
    CALL(evt_pm64boss_nextmate, BTL_UNIT_SELF, LW(3))
    IF_INT_NOT_EQUAL(LW(3), -1)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(6.0f))
        CALL(btlevtcmd_MovePosition, LW(3), LW(0), LW(1), LW(2), 0, -1, 0)
        CALL(btlevtcmd_KillUnit, LW(3), 0)
        ADD(LW(6), 1)
        GOTO(1)
    END_IF
    IF_INT_GREATER(LW(6), 0)
        MULTIPLY(LW(6), LW(9))
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_HEAL, STRING("SFX_BTL_RECOVERY1"))
        CALL(evt_pm64boss_healfx, BTL_UNIT_SELF)
        CALL(btlevtcmd_RecoverHp, BTL_UNIT_SELF, 1, LW(6))
    END_IF
    RETURN
EVT_END()

/* the tower goes up: each standing brother leaps into his place (or one joins it on top); the first time it
 * stands, the top one points (PointForward, his talkRow) and says MSG_CH1_010A (fake_bowser.c
 * EVS_BuildTowerWithKoopa: PointForward, a lens flare, Wait 20, ActorSpeak) */
static EVT_BEGIN(pm64boss_build)
    LABEL(1)
    CALL(evt_pm64boss_towernext, BTL_UNIT_SELF, LW(3), LW(5), LW(6), LW(7))
    IF_INT_NOT_EQUAL(LW(3), -1)
        CALL(evt_pm64boss_snd, LW(3), BS_JUMP, STRING("SFX_ENM_WAN_JUMP1"))
        CALL(btlevtcmd_JumpSetting, LW(3), 20, FLOAT(0.0f), FLOAT(0.7f))
        CALL(btlevtcmd_JumpPosition, LW(3), LW(5), LW(6), LW(7), PM64_FRAMES(10), -1)
        CALL(evt_pm64boss_snd, LW(3), BS_LAND, 0)
        CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
        GOTO(1)
    END_IF
    CALL(evt_pm64boss_towerline, BTL_UNIT_SELF, 1, LW(3), LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_btl_camera_set_mode, 0, 7)
        CALL(evt_btl_camera_set_homing_unit, 0, LW(3), BTL_UNIT_NONE)
        CALL(evt_btl_camera_set_zoom, 0, 150)
        CALL(evt_pm64boss_talkrow, LW(3), LW(2))
        IF_INT_NOT_EQUAL(LW(2), -1)
            CALL(evt_pm64boss_row, LW(3), LW(2))
        END_IF
        WAIT_FRAMES(PM64_FRAMES(20))
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, LW(3))
        CALL(evt_pm64boss_row, LW(3), -1)
        CALL(evt_btl_camera_set_mode, 0, 0)
    END_IF
    RETURN
EVT_END()

/* a turn it waits (a tower brother under the top one, one on its back, one that has just joined the tower);
 * getting up: a hop (PM64 common_koopa_bros.inc.c BOSS_CMD_TRY_GET_UP: a hop of 8 frames) */
static EVT_BEGIN(pm64boss_rest)
    CALL_CHILD(pm64boss_build)
    CALL(evt_pm64boss_getup, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_JUMP, STRING("SFX_ENM_WAN_JUMP1"))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(1.0f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(8), -1)
    END_IF
    WAIT_FRAMES(10)
    RETURN
EVT_END()

/* A hand-off's fallen unit leaves the stage: hidden, out of the fight (0 HP, inactive: not alive() and no turns),
 * but NOT deleted. The hand-offs run in its own damage event, and a unit's deletion (KillUnit without a flag:
 * DELETE_PENDING, done at the next frame's start by BattleCheckUnitBroken -> BtlUnit_Delete) deletes its damage
 * event with it: before, everything after the old one's KillUnit was cut off at the next WAIT. The new form never
 * came in (left where it was spawned, x 400 off the screen, until its next attack walked it home) and the units a
 * hand-off brings (General Guy's Light Bulb and each wave's twin, Lava Piranha's fiery Buds) never spawned. The
 * hand-off deletes it as its last step. */
static EVT_BEGIN(pm64boss_gone)
    CALL(btlevtcmd_OnAttribute, BTL_UNIT_SELF, BOSS_HIDE)
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, KILL_UNIT_INACTIVE)
    RETURN
EVT_END()

/* the unit gives way to the one that takes its place, by its exit and the new one's enter (LW(13) the setup) */
static EVT_BEGIN(pm64boss_handoff2)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
    CALL(evt_btl_camera_set_zoom, 0, 150)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -4)
    WAIT_FRAMES(30)
    CALL(evt_pm64boss_line, BTL_UNIT_SELF, -1, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, BTL_UNIT_SELF)
    END_IF
    CALL(evt_pm64boss_exitinfo, BTL_UNIT_SELF, LW(13), LW(9), LW(14), LW(10), LW(5), LW(6), LW(7))
    CALL(btlevtcmd_SpawnUnit, LW(3), LW(13), 0)
    IF_INT_EQUAL(LW(3), -1)
        /* (no slot left for it: the fallen one is gone all the same, so the fight can still end) */
        CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
        CALL(evt_btl_camera_set_mode, 0, 0)
        RETURN
    END_IF
    /* (hidden at its home until it comes in: pm64boss_enter shows it where it starts. It once stood at x 400 from
       here, off the screen) */
    CALL(btlevtcmd_OnAttribute, LW(3), BOSS_HIDE)
    CALL(btlevtcmd_SetPos, LW(3), LW(5), LW(6), LW(7))
    CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7)) /* (its companions' places are from its home) */
    /* (revive 1: its companions go; the new one's take their place) */
    LABEL(1)
    CALL(evt_pm64boss_nextoldmate, BTL_UNIT_SELF, LW(3), LW(12))
    IF_INT_NOT_EQUAL(LW(12), -1)
        CALL(btlevtcmd_GetPos, LW(12), LW(0), LW(1), LW(2))
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_KillUnit, LW(12), 0)
        GOTO(1)
    END_IF
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    SWITCH(LW(9))
        CASE_EQUAL(EXIT_FLEE)
            /* (off to the right in hops) */
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_EXIT, 0)
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, -3)
            LOOP(4)
                ADD(LW(0), 60)
                CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
                CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 14, -1)
            END_LOOP
            CALL_CHILD(pm64boss_gone)
        CASE_EQUAL(EXIT_TRANSFORM)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_EXIT, STRING("SFX_BTL_CHARGE1"))
            CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.5f), 0, 0, 0, 0, 0, 0, 0)
            CALL_CHILD(pm64boss_gone)
        CASE_DEFAULT
            /* (beaten: it falls and is gone) */
            WAIT_FRAMES(20)
            CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            CALL_CHILD(pm64boss_gone)
    END_SWITCH
    WAIT_FRAMES(10)
    /* (its companions come in with it, side by side: a wave's twins, PM64 toy_tank.c's Left/Right formations) */
    SET_VALUE(LW(12), -1)
    IF_INT_NOT_EQUAL(LW(10), 0)
        BEGIN_SIBLING_EVENT_GET_ID(LW(12))
            SET_VALUE(LW(8), LW(3))
            SET_VALUE(LW(9), 1)
            CALL_CHILD(pm64boss_summon)
        END_SIBLING_EVENT
    END_IF
    CALL_CHILD(pm64boss_enter)
    WAIT_FRAMES(20)
    /* (and its companions, one after another, all in before this ends: a RETURN deletes its siblings with it) */
    IF_INT_NOT_EQUAL(LW(12), -1)
        LABEL(2)
        IS_EVENT_RUNNING(LW(12), LW(0))
        IF_INT_NOT_EQUAL(LW(0), 0)
            WAIT_FRAMES(1)
            GOTO(2)
        END_IF
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 0)
    /* (the fallen one deleted last: see pm64boss_gone) */
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
    RETURN
EVT_END()

/* after a hit: a clone vanishes; its clones vanish; a toppled tower falls apart; a piece breaks off */
static EVT_BEGIN(pm64boss_afterhit)
    CALL(evt_pm64boss_vanish, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
        RETURN
    END_IF
    LABEL(1)
    CALL(evt_pm64boss_nextclonegone, BTL_UNIT_SELF, LW(3))
    IF_INT_NOT_EQUAL(LW(3), -1)
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(btlevtcmd_KillUnit, LW(3), 0)
        GOTO(1)
    END_IF
    /* (PM64 fake_bowser.c BOSS_CMD_TOPPLE_HIT: each falls off in 20 frames, a shake as the top one lands) */
    CALL(evt_pm64boss_toppling, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        LABEL(2)
        CALL(evt_pm64boss_towerfall, LW(3), LW(5), LW(6), LW(7))
        IF_INT_NOT_EQUAL(LW(3), -1)
            CALL(btlevtcmd_JumpSetting, LW(3), 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, LW(3), LW(5), LW(6), LW(7), PM64_FRAMES(20), -1)
            CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
            GOTO(2)
        END_IF
        CALL(evt_btl_camera_shake_h, 0, 4, 0, 16, 0)
    END_IF
    /* (the tower tips: the top one's "We're going over!!", fake_bowser.c EVS_KoopaBros_HandleEvent: Wait 30, the
       tower unstable, ActorSpeak MSG_CH1_0109, Wait 20) */
    CALL(evt_pm64boss_towerline, BTL_UNIT_SELF, 0, LW(3), LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        WAIT_FRAMES(PM64_FRAMES(30))
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, LW(3))
        WAIT_FRAMES(PM64_FRAMES(20))
    END_IF
    /* (a lone brother jumped on flips onto his back: common_koopa_bros.inc.c EVENT_FLIP_TRIGGER, Hurt and a flip
       over its 22 FlipPosOffsets frames, then IdleToppled) */
    CALL(evt_pm64boss_flip, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -4)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_FLIP, STRING("SFX_ENM_NOKO_SHELL1"))
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(1.0f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(22), -1)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    END_IF
    /* (its Star Rod barrier: an attack bounces off (final_bowser_1.c EVENT_ZERO_DAMAGE -> EVS_NoDamageHit in Mock;
       the first time Bowser's taunt MSG_CH8_0085 and the hint); a Star Power special breaks it (final_bowser_2.c
       EVENT_PEACH_BEAM: sparkles and a shimmer burst, Hurt, the first time MSG_CH8_00A7)) */
    CALL(evt_pm64boss_barrierhit, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    IF_INT_EQUAL(LW(0), 2)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_BARRI_BARRIER_BREAK1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
        ADD(LW(6), 50)
        CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(5), LW(6), LW(7), 80, 80, 80, 100, 0, 0, 0, 0)
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(5), LW(6), LW(7), FLOAT(2.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_btl_camera_shake_h, 0, 3, 0, 12, 0)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -4)
        WAIT_FRAMES(PM64_FRAMES(20))
        IF_INT_NOT_EQUAL(LW(1), 0)
            CALL(evt_btl_camera_set_mode, 0, 7)
            CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
            CALL(evt_btl_camera_set_zoom, 0, 100)
            WAIT_FRAMES(PM64_FRAMES(30))
            CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(1), 0, BTL_UNIT_SELF)
            CALL(evt_btl_camera_set_mode, 0, 0)
        END_IF
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    END_IF
    IF_INT_EQUAL(LW(0), 1)
        CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_ENM_BARRI_BARRIER1"), EVT_NULLPTR, 0, EVT_NULLPTR)
        CALL(evt_pm64boss_mockrow, BTL_UNIT_SELF, LW(5))
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(5))
        WAIT_FRAMES(PM64_FRAMES(30))
        IF_INT_NOT_EQUAL(LW(1), 0)
            CALL(evt_btl_camera_set_mode, 0, 7)
            CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
            CALL(evt_btl_camera_set_zoom, 0, 100)
            WAIT_FRAMES(PM64_FRAMES(30))
            CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(1), 0, BTL_UNIT_SELF)
            CALL(evt_btl_camera_set_mode, 0, 0)
        END_IF
        IF_INT_NOT_EQUAL(LW(2), 0)
            CALL(evt_msg_print, 0, LW(2), 0, 0)
        END_IF
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    END_IF
    CALL(evt_pm64boss_shed, BTL_UNIT_SELF, LW(13), LW(5), LW(6), LW(7))
    IF_INT_NOT_EQUAL(LW(13), 0)
        CALL(btlevtcmd_SpawnUnit, LW(3), LW(13), 0)
        IF_INT_NOT_EQUAL(LW(3), -1)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_SetPos, LW(3), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpSetting, LW(3), 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, LW(3), LW(5), LW(6), LW(7), 20, -1)
            CALL(btlevtcmd_SetHomePos, LW(3), LW(5), LW(6), LW(7))
        END_IF
    END_IF
    RETURN
EVT_END()
/* the fallen unit flees into the one that takes its place (Tubba's Heart back into Tubba Blubba): LW(13) its
 * setup, LW(14) the fallen one's walk row, LW(15) the new one's */
static EVT_BEGIN(pm64boss_handoff)
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
    CALL(evt_btl_camera_set_zoom, 0, 150)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, 13) /* (PM64: Dizzy) */
    WAIT_FRAMES(40)
    /* (PM64 tubbas_heart.c EVS_FleeFromBattle: ActorSpeak MSG_CH3_00C6, Dizzy) */
    CALL(evt_pm64boss_line, BTL_UNIT_SELF, -1, LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, BTL_UNIT_SELF)
    END_IF
    /* the new one walks in from the right */
    CALL(btlevtcmd_SpawnUnit, LW(3), LW(13), 0)
    IF_INT_EQUAL(LW(3), -1)
        /* (no slot left for it: the fallen one is gone all the same, so the fight can still end) */
        CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
        CALL(evt_btl_camera_set_mode, 0, 0)
        RETURN
    END_IF
    CALL(btlevtcmd_SetPos, LW(3), 300, 0, 0)
    CALL(evt_pm64boss_row, LW(3), LW(15))
    CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(3.0f))
    CALL(btlevtcmd_MovePosition, LW(3), 150, 0, 0, 0, -1, 0)
    CALL(evt_pm64boss_row, LW(3), -1)
    /* the fallen one hops into it and is gone */
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(14))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    LOOP(3)
        ADD(LW(0), 20)
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 14, -1)
    END_LOOP
    CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, 150, 30, 0, 18, -1)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BTL_RECOVERY1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(evt_eff, 0, STRING("kemuri_test"), 4, 150, 30, 0, FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
    /* (gone but not deleted until the end: pm64boss_gone. Before, its deletion cut this off here: Tubba stood at
       150 with his home at 300) */
    CALL_CHILD(pm64boss_gone)
    WAIT_FRAMES(20)
    /* and it takes its place */
    CALL(btlevtcmd_SetHomePos, LW(3), 90, 0, 0)
    CALL(evt_pm64boss_row, LW(3), LW(15))
    CALL(btlevtcmd_SetMoveSpeed, LW(3), FLOAT(3.0f))
    CALL(btlevtcmd_MovePosition, LW(3), 90, 0, 0, 0, -1, 0)
    CALL(evt_pm64boss_row, LW(3), -1)
    CALL(evt_pm64boss_entered, LW(3))
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(btlevtcmd_KillUnit, BTL_UNIT_SELF, 0)
    RETURN
EVT_END()

/* a move's end, however it ends: the stage lightens if it darkened, a companion it left at the target goes home
 * (or is used up), and its charge, hazard and swell go (pm64boss_attack's LABEL 99; or its damage event, when a
 * counter or superguard has deleted the attack: TTYD's own enemies leave that to the damage event too) */
static EVT_BEGIN(pm64boss_finish)
    CALL(evt_pm64boss_lighten, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL(evt_batstage_return_stg_dark_base, 30, 0)
        CALL(evt_batstage_return_aud_dark_base, 30, 0)
    END_IF
    CALL(evt_pm64boss_stray, BTL_UNIT_SELF, LW(0), LW(1))
    IF_INT_NOT_EQUAL(LW(0), -1)
        IF_INT_EQUAL(LW(1), 1)
            /* (an inline event, not a sibling: this script returns at once, and a RETURN deletes its siblings
               with it; the inline one is its own and jumps home while the damage event plays the hurt) */
            BEGIN_INLINE_EVENT
                CALL(btlevtcmd_GetHomePos, LW(0), LW(1), LW(2), LW(3))
                CALL(btlevtcmd_JumpSetting, LW(0), 20, FLOAT(0.0f), FLOAT(0.7f))
                CALL(btlevtcmd_JumpPosition, LW(0), LW(1), LW(2), LW(3), 20, -1)
            END_INLINE_EVENT
        ELSE
            CALL(btlevtcmd_GetPos, LW(0), LW(1), LW(2), LW(3))
            CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(1), LW(2), LW(3), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            CALL(btlevtcmd_KillUnit, LW(0), 0)
        END_IF
    END_IF
    CALL(evt_pm64boss_done, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(pm64boss_damage)
    /* hit mid-move (a counter, a superguard: the attack event is gone): its move's end here */
    CALL(evt_pm64boss_midmove, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(pm64boss_finish)
        CALL(evt_btl_camera_set_mode, 0, 0)
    END_IF
    CALL(evt_pm64boss_fell, BTL_UNIT_SELF)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    SET_VALUE(LW(10), -2)
    SET_VALUE(LW(11), 1)
    CALL_CHILD(btldefaultevt_Damage)
    CALL_CHILD(pm64boss_afterhit)
    CALL(evt_pm64boss_fallen, BTL_UNIT_SELF, LW(13), LW(14), LW(15))
    IF_INT_NOT_EQUAL(LW(13), 0)
        /* (fleeing in from the right with nothing more: Tubba's Heart's own hand-off; the rest by exit/enter) */
        CALL(evt_pm64boss_exitinfo, BTL_UNIT_SELF, LW(13), LW(9), LW(5), LW(10), LW(0), LW(1), LW(2))
        SET_VALUE(LW(0), 0)
        IF_INT_EQUAL(LW(9), EXIT_FLEE)
            IF_INT_EQUAL(LW(5), ENTER_RIGHT)
                IF_INT_EQUAL(LW(10), 0)
                    SET_VALUE(LW(0), 1)
                END_IF
            END_IF
        END_IF
        IF_INT_EQUAL(LW(0), 1)
            CALL_CHILD(pm64boss_handoff)
        ELSE
            CALL_CHILD(pm64boss_handoff2)
        END_IF
    END_IF
    RETURN
EVT_END()

static EVT_BEGIN(pm64boss_wait)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    CALL(btlevtcmd_AnimeChangePoseFromTable, BTL_UNIT_SELF, 1)
    RETURN
EVT_END()

/* --- a move's look: PM64's effects for it (BossMove.fx, pm64_boss.h FX_*) -----------------------------------------
 * Played from the game's own PM64 effect library (src/effect/n64, the effects TTYD carries over from PM64, the same
 * one the PM64 partners' moves use: pm64_partner_pc.c pm64Fx/pm64Beam), with PM64's arguments where the two agree
 * (counts and lifetimes in PM64 frames, doubled: PM64_FRAMES). Each move's script runs its timeline in a sibling
 * event (pm64boss_fxrun) as its wind-up starts and again as it lets go, a frame at a time, and a burst on the target
 * with each hit (evt_pm64boss_fxhit). Nothing here touches the damage: looks only. */
enum { FXP_WIND, FXP_GO, FXP_HIT, FXP_POST, FXP_HEAL };

static const char* const kFxName[FX_COUNT] = {
    "none",   "fire_breath", "spit_fire", "fire_stream", "petit", "ice_bolt", "icy_breath",
    "spit_bits", "hurricane", "sky_bolt", "ground_bolts", "bulb_bolt", "rod_bolt", "rod_wave",
    "spell", "staff_bolt", "bomb", "debris", "block", "shell_flare", "leech", "mallet"};

/* its mouth (a hand, a staff's tip): `fwd` of its width in front of it, `up` of its height up */
static void fxAt(const BattleUnit* u, float fwd, float up, float out[3]) {
    const float face = u->faceDirection < 0 ? -1.0f : 1.0f;
    out[0] = u->position.x + face * (float)u->width * fwd;
    out[1] = u->position.y + (float)u->height * up;
    out[2] = u->position.z + 5.0f;
}

/* a lightning bolt from one point to another (PM64 EFFECT_LIGHTNING_BOLT: TTYD's PokopiThunderN64, the same
 * effect pm64Beam draws Watt's bolts with), `frames` PM64 frames; ice: in white and ice blue (crystal_king.c
 * EVS_Attack_IceBolt's bolt colours) */
static void fxBolt(const float a[3], const float b[3], int frames, float scale, int ice) {
    EffectEntry* e = effPokopiThunderN64Entry(2, PM64_FRAMES(frames), a[0], a[1], a[2], b[0], b[1], b[2], scale);
    if (ice && e != NULL && e->userdata != NULL) {
        EffPokopiThunderN64Work* w = (EffPokopiThunderN64Work*)e->userdata;
        w->r = 120;
        w->g = 200;
        w->b = 255;
    }
}

/* a breath stream (PM64 EFFECT_FIRE_BREATH's puffs, 50 a frame apart, each 24 PM64 frames) from a mouth to a point,
 * growing to `scale`, in two colours (the puff's and its glow: crystal_king.c's icy breath white and ice blue,
 * huff_n_puff.c's cloud white) */
static const int kIceBreath[6] = {220, 240, 255, 110, 180, 255};
static const int kWindBreath[6] = {255, 255, 255, 170, 180, 200};
static void fxBreath(float x, float y, float z, float tx, float ty, float tz, float scale, const int col[6]) {
    EffectEntry* e = effBreathFireN64Entry(2, 50, 1, PM64_FRAMES(24), x, y, z, tx, ty, tz);
    if (e != NULL && e->userdata != NULL) {
        EffBreathFireN64Work* w = (EffBreathFireN64Work*)e->userdata;
        int k;
        for (k = 0; k < 6; k++) {
            w->color[k] = col[k];
        }
        w->scale = w->initialScale = 0.04f;
        w->targetScale = scale;
        w->scaleRate = 0.025f;
    }
}

/* PM64 EFFECT_EMBERS where a fire lands (lava_piranha.c: 50 x 50, 40 of them, 55 frames) */
static void fxEmbers(float x, float z, float size) {
    effFireSparkN64Entry(0, 40, PM64_FRAMES(55), x, 0.0f, z, 50.0f * size, 50.0f * size, size, 1.0f, 1.0f);
}

/* the clones of the unit's kind standing (the Crystal King's: they breathe with him) */
static const BattleUnit* fxClone(const BattleUnit* self, int b, int n) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        const int k = bossOfUnit(u);
        if (alive(u) && u != self && u->alliance == 1 && k >= 0 && kBosses[k].illusory && cloneKind(b, k) && n-- == 0) {
            return u;
        }
    }
    return NULL;
}

/* the unit its move's bolt comes from: its unit1 standing (General Guy's Light Bulb), else itself */
static const BattleUnit* fxSource(const BattleUnit* self, const BossMove* m) {
    int i;
    for (i = 0; m->unit1 > 0 && i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        if (alive(u) && u != self && u->alliance == 1 && bossOfUnit(u) == m->unit1 - 1) {
            return u;
        }
    }
    return self;
}

/* each of the player's side standing, in turn (-1 ends) */
static const BattleUnit* fxFoe(int n) {
    int i;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* u = unitAt(i);
        if (u != NULL && u->alliance == 0 && u->currentHp > 0 && !(u->attributeFlags & UNIT_ATTR_INACTIVE) && n-- == 0) {
            return u;
        }
    }
    return NULL;
}

/* the frames from letting go to the hit (a shot's flight, a breath reaching the target) */
static int fxTravel(int fx, int dflt) {
    switch (fx) {
        case FX_FIRE_BREATH: return PM64_FRAMES(12); /* (final_bowser_1.c: Wait 10, then the damage) */
        case FX_SPIT_FIRE: return PM64_FRAMES(20);   /* (EFFECT_63 type 1: 20 frames to the target) */
        case FX_FIRE_STREAM: return PM64_FRAMES(16);
        case FX_PETIT: return PM64_FRAMES(20);
        case FX_ICE_BOLT: return PM64_FRAMES(7);     /* (EVS_Attack_IceBolt: the bolt, Wait 7) */
        case FX_ICY_BREATH: return PM64_FRAMES(20);
        case FX_HURRICANE: return PM64_FRAMES(40);
        case FX_SKY_BOLT:
        case FX_STAFF_BOLT:
        case FX_ROD_BOLT:
        case FX_BULB_BOLT: return PM64_FRAMES(4);
        case FX_GROUND_BOLTS: return PM64_FRAMES(30);
        case FX_ROD_WAVE: return PM64_FRAMES(10);
        case FX_SPELL: return PM64_FRAMES(15);       /* (EFFECT_SHAPE_SPELL: 15 frames to the target) */
        case FX_DEBRIS: return PM64_FRAMES(30);
        case FX_BLOCK: return PM64_FRAMES(20);       /* (kammy_koopa.c: the block falls in 20 frames) */
        default: return dflt;
    }
}

/* one frame of a move's timeline (phase FXP_*, `f` TTYD frames in): 1 while there's more to come */
static int bossFxStep(const BattleUnit* unit, int fx, int phase, const BattleUnit* target, int f) {
    const int b = bossOfUnit(unit);
    const BossMove* m = b >= 0 && rtOf(unit)->move >= 0 ? &kBosses[b].moves[rtOf(unit)->move] : NULL;
    float a[3], t[3];
    int i;
    if (unit == NULL || b < 0) {
        return 0;
    }
    if (target != NULL) {
        t[0] = target->position.x;
        t[1] = target->position.y + (float)target->height * 0.5f;
        t[2] = target->position.z + 5.0f;
    } else {
        t[0] = -60.0f;
        t[1] = 20.0f;
        t[2] = 5.0f;
    }
    if (phase == FXP_HEAL) {
        /* PM64 EFFECT_RECOVER (and EFFECT_SPARKLES) over a boss that heals (final_jr_troopa.c EVS_Move_HealSelf,
           crystal_king.c EVS_Move_Recover, final_bowser_2.c EVS_Recover, huff_n_puff.c EVS_Move_HealOrSlam) */
        fxAt(unit, 0.0f, 1.0f, a);
        effRecoverHeartN64Entry(0, 3, a[0], a[1], a[2]);
        return 0;
    }
    switch (fx) {
        case FX_FIRE_BREATH:
            /* final_bowser_1.c EVS_Attack_FireBreath: EFFECT_FIRE_BREATH from his mouth to 40 past the target, 20
               up: 50 puffs a frame apart, each 24 frames (type 2; 0 under the Star Rod's power) */
            if (phase == FXP_GO && (f == 0 || f == PM64_FRAMES(25))) {
                /* (two waves of 50, one a frame here: PM64's 50 a frame at its 30 frames a second, for the
                   breath's 70-frame loop) */
                const float face = unit->faceDirection < 0 ? -1.0f : 1.0f;
                EffectEntry* e;
                fxAt(unit, 0.45f, 0.6f, a);
                e = effBreathFireN64Entry(rtOf(unit)->barrier ? 0 : 2, 50, 1, PM64_FRAMES(24), a[0], a[1], a[2],
                                          t[0] + face * 40.0f, 20.0f, t[2]);
                if (e != NULL && e->userdata != NULL && !rtOf(unit)->barrier) {
                    /* (SetBowserFireBreathScales: each puff grows from 0.04 to 1.875 at 0.05 a PM64 frame; the
                       chain's puffs take it on) */
                    EffBreathFireN64Work* w = (EffBreathFireN64Work*)e->userdata;
                    w->scale = w->initialScale = 0.04f;
                    w->targetScale = 1.875f;
                    w->scaleRate = 0.025f;
                }
            }
            return phase == FXP_GO && f < PM64_FRAMES(25);
        case FX_SPIT_FIRE:
        case FX_FIRE_STREAM:
            /* lava_piranha.c EVS_Attack_SpitFire / EVS_Attack_FireBreath: EFFECT_63 (TTYD's PacknBfireN64, the
               Piranha boss's fire) type 1 a ball over 20 frames, type 0 a stream over 16 that lasts 40; EFFECT_EMBERS
               where it lands */
            if (phase == FXP_GO && f == 0) {
                fxAt(unit, 0.4f, 0.55f, a);
                if (fx == FX_SPIT_FIRE) {
                    effPacknBfireN64Entry(1, PM64_FRAMES(20), PM64_FRAMES(20), a[0], a[1], a[2], t[0], t[1], t[2], 1.0f);
                } else {
                    effPacknBfireN64Entry(0, PM64_FRAMES(16), PM64_FRAMES(40), a[0], a[1], a[2], t[0], t[1], t[2], 1.0f);
                }
            } else if (phase == FXP_HIT && target != NULL) {
                fxEmbers(t[0], t[2], 1.0f);
            }
            return 0;
        case FX_PETIT:
            /* lava_bud.c EVS_Attack_SpitPetit: EFFECT_FLAME (small, blue) on the Bud as it spits; the Petit dives
               in flame (petit_piranha.c EVS_TakeTurn, EFFECT_FLAME red): here a small ball of the Piranha's fire,
               and embers where it lands */
            if (phase == FXP_WIND && f == 0) {
                fxAt(unit, 0.2f, 0.8f, a);
                effFlameN64Entry(1, a[0], a[1], a[2], 0.3f);
            } else if (phase == FXP_GO && f == 0) {
                fxAt(unit, 0.4f, 0.55f, a);
                effPacknBfireN64Entry(1, PM64_FRAMES(20), PM64_FRAMES(20), a[0], a[1], a[2], t[0], t[1], t[2], 0.6f);
            } else if (phase == FXP_HIT && target != NULL) {
                effFlameN64Entry(0, t[0], t[1], t[2], 0.3f);
                fxEmbers(t[0], t[2], 0.6f);
            }
            return 0;
        case FX_ICE_BOLT:
            /* crystal_king.c EVS_Attack_IceBolt: EFFECT_COLD_BREATH and EFFECT_SPARKLES at his mouth as he inhales,
               then EFFECT_LIGHTNING_BOLT in white and ice blue to the target (20 frames), EFFECT_MISC_PARTICLES on it */
            if (phase == FXP_WIND && (f == 0 || f == PM64_FRAMES(10))) {
                fxAt(unit, 0.35f, 0.6f, a);
                effSnowDustN64Entry(1, 12, PM64_FRAMES(30), a[0], a[1], a[2], 20.0f, 20.0f, 1.0f);
            } else if (phase == FXP_GO && f == 0) {
                fxAt(unit, 0.35f, 0.6f, a);
                fxBolt(a, t, 20, 1.0f, 1);
            } else if (phase == FXP_HIT && target != NULL) {
                effIceN64Entry(0, t[0], t[1], t[2]);
            }
            return phase == FXP_WIND && f < PM64_FRAMES(10);
        case FX_ICY_BREATH:
            /* crystal_king.c EVS_Attack_IcyBreath / EVS_Attack_CloneBreath: EFFECT_SPARKLES as he inhales, then
               EFFECT_HUFF_PUFF_BREATH (white, 60 frames) from him and from each clone: PM64's huff_puff_breath
               isn't in the TTYD library, so its breath stream (FIRE_BREATH's) in white and ice blue, two waves */
            if (f != 0 && !(phase == FXP_GO && f == PM64_FRAMES(25))) {
                return phase == FXP_GO && f < PM64_FRAMES(25);
            }
            for (i = -1; i < 2; i++) {
                const BattleUnit* u = i < 0 ? unit : fxClone(unit, b, i);
                if (u == NULL) {
                    continue;
                }
                fxAt(u, 0.35f, 0.6f, a);
                if (phase == FXP_WIND) {
                    effSnowDustN64Entry(1, 12, PM64_FRAMES(30), a[0], a[1], a[2], 24.0f, 24.0f, 1.0f);
                } else if (phase == FXP_GO) {
                    const float face = u->faceDirection < 0 ? -1.0f : 1.0f;
                    fxBreath(a[0], a[1], a[2], t[0] + face * 40.0f, t[1], t[2], 1.4f, kIceBreath);
                }
            }
            if (phase == FXP_HIT && target != NULL) {
                effSnowDustN64Entry(1, 16, PM64_FRAMES(30), t[0], t[1], t[2], 30.0f, 30.0f, 1.0f);
            }
            return phase == FXP_GO && f < PM64_FRAMES(25);
        case FX_SPIT_BITS:
            /* crystal_king.c EVS_SpitSingleBit: EFFECT_MISC_PARTICLES as each Bit shatters on the target */
            if (phase == FXP_HIT && target != NULL) {
                effIceN64Entry(0, t[0], t[1], t[2]);
            }
            return 0;
        case FX_HURRICANE:
            /* huff_n_puff.c EVS_Move_HurricaneBreath: EFFECT_HUFF_PUFF_BREATH from his mouth for 200 frames as he
               blows, the target spun round in the wind (the Whirlwind command): the breath stream (FIRE_BREATH's,
               PM64's huff_puff_breath not being in the TTYD library) in cloud white, waves through the blow, and
               the PM64 whirlwind on the target (pm64Fx's 9) */
            if (phase == FXP_GO && f % PM64_FRAMES(25) == 0 && f < PM64_FRAMES(100)) {
                const float face = unit->faceDirection < 0 ? -1.0f : 1.0f;
                fxAt(unit, 0.45f, 0.45f, a);
                fxBreath(a[0], a[1], a[2], t[0] + face * 60.0f, t[1], t[2], 2.0f, kWindBreath);
            }
            if (phase == FXP_GO && f == PM64_FRAMES(15) && target != NULL) {
                EffectEntry* e = effWhirlwindN64Entry(0, NULL, target->sizeMultiplier, PM64_FRAMES(60));
                if (e != NULL && e->userdata != NULL) {
                    ((EffWhirlwindN64Work*)e->userdata)->position.x = target->position.x;
                    ((EffWhirlwindN64Work*)e->userdata)->position.y = target->position.y;
                    ((EffWhirlwindN64Work*)e->userdata)->position.z = target->position.z + 5.0f;
                }
            }
            return phase == FXP_GO && f < PM64_FRAMES(100);
        case FX_SPELL:
        case FX_STAFF_BOLT:
        case FX_SKY_BOLT:
            /* final_jr_troopa.c EVS_Attack_MagicSpell / EVS_Attack_LightningBolt: EFFECT_GATHER_ENERGY_PINK on the
               staff 30 frames, then EFFECT_SHAPE_SPELL at the target (15 frames) and EFFECT_BLAST on it, or
               EFFECT_LIGHTNING_BOLT from the sky onto it (18 frames); huff_n_puff.c EVS_Attack_DirectLightning the
               same bolt (scale 1.5, 20 frames) */
            if (phase == FXP_WIND && f == 0 && fx != FX_SKY_BOLT) {
                fxAt(unit, 0.45f, 0.9f, a);
                effChargeEntry(1, PM64_FRAMES(30), a[0], a[1], a[2], 1.0f);
            } else if (phase == FXP_GO && f == 0 && fx == FX_SPELL) {
                fxAt(unit, 0.45f, 0.9f, a);
                effMagic1N64Entry(0, PM64_FRAMES(15), a[0], a[1], a[2], t[0], t[1], t[2]);
            } else if (phase == FXP_GO && f == 0) {
                a[0] = t[0];
                a[1] = 260.0f;
                a[2] = t[2];
                fxBolt(a, t, fx == FX_SKY_BOLT ? 20 : 18, fx == FX_SKY_BOLT ? 1.5f : 1.0f, 0);
            } else if (phase == FXP_HIT && target != NULL) {
                if (fx == FX_SPELL) {
                    effExplosionN64Entry(0, PM64_FRAMES(30), t[0], t[1], t[2], 1.0f);
                } else {
                    effShockN64Entry(3, target->position.x, target->position.y, t[2], (float)target->width,
                                     (float)target->height);
                }
            }
            return 0;
        case FX_GROUND_BOLTS:
            /* huff_n_puff.c EVS_Attack_GroundLightning: ten EFFECT_LIGHTNING_BOLTs onto the ground across the
               player's side, a few frames apart (scale 3 to 4, 30 frames each) */
            if (phase == FXP_GO && f % PM64_FRAMES(3) == 0 && f / PM64_FRAMES(3) < 10) {
                static const signed char kAt[10] = {0, -60, 50, -110, 20, -30, 90, -90, 40, -10};
                const int k = f / PM64_FRAMES(3);
                a[0] = t[0] + (float)kAt[k];
                a[1] = 280.0f;
                a[2] = t[2] + (float)((k * 7) % 30) - 15.0f;
                t[0] = a[0];
                t[1] = 0.0f;
                t[2] = a[2];
                fxBolt(a, t, 30, 2.0f, 0);
                effHokoriN64Entry(2, PM64_FRAMES(15), t[0], 0.0f, t[2]);
            } else if (phase == FXP_HIT && target != NULL) {
                effShockN64Entry(3, target->position.x, target->position.y, t[2], (float)target->width,
                                 (float)target->height);
            }
            return phase == FXP_GO && f < PM64_FRAMES(3) * 10;
        case FX_BULB_BOLT: {
            /* general_guy.c EVS_Attack_ShootLightning: EFFECT_SNAKING_STATIC on the bulb 40 frames, then
               EFFECT_LIGHTNING_BOLT from it at Mario and then at the partner (18 frames each) */
            const BattleUnit* s = m != NULL ? fxSource(unit, m) : unit;
            fxAt(s, 0.0f, 0.5f, a);
            if (phase == FXP_WIND && f == 0) {
                effThunderflareN64Entry(0, PM64_FRAMES(40), a[0], a[1], a[2], 1.0f);
            } else if (phase == FXP_HIT && target != NULL) {
                fxBolt(a, t, 18, 1.0f, 0);
                effShockN64Entry(3, target->position.x, target->position.y, t[2], (float)target->width,
                                 (float)target->height);
            }
            return 0;
        }
        case FX_ROD_BOLT:
        case FX_ROD_WAVE:
            /* final_bowser_2.c EVS_Attack_LightningBlast / EVS_UseDrainingShockwave: EFFECT_LIGHT_RAYS and
               EFFECT_BULB_GLOW on the Star Rod's tip (135 up at his scale) as it charges, EFFECT_ENERGY_IN_OUT
               round him; then fx_lightning_bolt from the rod to the target, or EFFECT_ENERGY_SHOCKWAVE from him
               over everyone (60 frames) */
            fxAt(unit, -0.15f, 0.95f, a); /* (the rod, raised in his back hand) */
            if (phase == FXP_WIND && f == 0) {
                effRadiationN64Entry(0, a[0], a[1], a[2], 1.5f);
                effChargeEntry(1, PM64_FRAMES(50), a[0], a[1], a[2], 1.5f);
            } else if (phase == FXP_GO && f == 0 && fx == FX_ROD_BOLT) {
                fxBolt(a, t, 11, 1.5f, 0);
            } else if (phase == FXP_GO && f == 0) {
                fxAt(unit, 0.0f, 0.5f, a);
                effAkariSflushN64Entry(0, PM64_FRAMES(30), a[0], a[1], a[2], 4.0f);
            } else if (phase == FXP_HIT && target != NULL) {
                effShockN64Entry(3, target->position.x, target->position.y, t[2], (float)target->width,
                                 (float)target->height);
            }
            return 0;
        case FX_BOMB:
            /* general_guy.c EVS_Attack_ThrowBomb: the bomb's blast on the target (general_guy_bomb.c) */
            if (phase == FXP_HIT && target != NULL) {
                effBombN64Entry(0, t[0], t[1], t[2]);
            }
            return 0;
        case FX_DEBRIS:
        case FX_BLOCK:
            /* tutankoopa.c EVS_Attack_DropDebris: EFFECT_CHOMP_DROP (dust shaken from the ceiling), then the rocks
               onto Mario and his partner; kammy_koopa.c: EFFECT_GATHER_ENERGY_PINK on her rod 20 frames, the
               block falls onto the target in 20 */
            if (phase == FXP_WIND && f == 0 && fx == FX_BLOCK) {
                fxAt(unit, 0.4f, 0.8f, a);
                effChargeEntry(1, PM64_FRAMES(20), a[0], a[1], a[2], 1.0f);
            } else if (phase == FXP_WIND && f == 0) {
                for (i = 0; i < 3; i++) {
                    effHokoriN64Entry(2, PM64_FRAMES(15), t[0] + (float)(i * 50 - 50), 200.0f, t[2]);
                }
            } else if (phase == FXP_GO && f == 0) {
                for (i = 0; fx == FX_DEBRIS ? fxFoe(i) != NULL : i < 1; i++) {
                    const BattleUnit* u = fx == FX_DEBRIS ? fxFoe(i) : target;
                    if (u != NULL) {
                        effGarekiN64Entry(0, u->position.x, 220.0f, u->position.z + 5.0f, u->position.y);
                    }
                }
            } else if (phase == FXP_HIT && target != NULL) {
                effHokoriN64Entry(2, PM64_FRAMES(15), target->position.x, target->position.y, t[2]);
            }
            return 0;
        case FX_LEECH:
            /* forest_fuzzy.inc.c EVS_Attack_Leech: EFFECT_RECOVER on the Fuzzy as it drains the target */
            if (phase == FXP_HIT) {
                fxAt(unit, 0.0f, 1.0f, a);
                effRecoverHeartN64Entry(0, 1, a[0], a[1], a[2]);
            }
            return 0;
        case FX_MALLET:
            /* spy_guy.inc.c EVS_Attack_Mallet: EFFECT_FIREWORK (scale 1) where the mallet lands */
            if (phase == FXP_HIT && target != NULL) {
                effFireworksN64Entry(0, PM64_FRAMES(10), t[0], t[1], t[2], 0.0f, 0.0f, 0.0f, 1.0f);
            }
            return 0;
        case FX_SHELL_FLARE:
            /* common_koopa_bros.inc.c HandleCommand: EFFECT_LENS_FLARE 30 frames as he points, back from his spin */
            if (phase == FXP_POST) {
                fxAt(unit, 0.3f, 0.8f, a);
                effLensflareN64Entry(0, PM64_FRAMES(30), a[0], a[1], a[2], 1.0f);
            }
            return 0;
        default:
            return 0;
    }
}

/* tests: $TTYD_PM64_NOFX=1 plays the moves without their looks (the before captures of the pm64_bossfx_* scenarios) */
static int fxOff(void) {
    static int sOff = -1;
    if (sOff < 0) {
        const char* v = getenv("TTYD_PM64_NOFX");
        sOff = v != NULL && v[0] == '1';
    }
    return sOff;
}

/* the look of the move the unit is playing (FX_NONE: none, or none asked for) */
static int moveFx(const BattleUnit* unit) {
    const int b = bossOfUnit(unit);
    return b >= 0 && rtOf(unit)->move >= 0 && !fxOff() ? kBosses[b].moves[rtOf(unit)->move].fx : FX_NONE;
}

/* its fx's first frame: the line tests look for, and a capture a little into it (the effect at its height) */
static void bossFxLog(const BattleUnit* unit, int fx, int phase) {
    static const char* const kPhase[] = {"wind-up", "release", "hit", "post", "heal"};
    const int b = bossOfUnit(unit);
    if (b < 0 || fx <= FX_NONE || fx >= FX_COUNT || !bossLog()) {
        return;
    }
    /* (the phases each has a look in: bits 1 wind-up, 2 release, 4 hit, 8 post) */
    static const unsigned char kPhases[FX_COUNT] = {0, 2, 6, 6, 7, 7, 7, 4, 2, 6, 6, 5, 7, 7, 7, 7, 4, 7, 7, 8, 4, 4};
    /* (into the release: as the breath is at its fullest, the ball halfway, the bolt at its brightest) */
    static const unsigned char kAt[FX_COUNT] = {14, 40, 20, 24, 20, 8, 40, 14, 60, 6, 30, 14, 6, 20, 14, 6, 14,
                                                30, 20, 30, 14, 14};
    if (!(kPhases[fx] & (1 << phase))) {
        return;
    }
    printf("[pm64] fx: %s move %d: %s (%s) starts\n", kBosses[b].english, rtOf(unit)->move + 1, kFxName[fx],
           kPhase[phase]);
    fflush(stdout);
    /* (a capture a little into it; a look that's all in the hit, as it lands) */
    if (phase != FXP_HIT || kPhases[fx] == 4) {
        char key[24];
        snprintf(key, sizeof(key), "fx_%s", kFxName[fx]);
        bossKey(unit, key, phase == FXP_GO ? kAt[fx] : phase == FXP_HIT ? 6 : 12);
    }
}

/* in: the unit, the phase (FXP_*), the target, the frame, its fx (-1 the first frame: its move's); out: its fx,
 * 1 while its timeline has more to come (pm64boss_fxrun) */
USER_FUNC(evt_pm64boss_fx) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int phase = evtGetValue(event, args[1]);
    const int id = evtGetValue(event, args[2]);
    const BattleUnit* target = id >= 0 ? ARG_UNIT(2) : NULL;
    const int f = evtGetValue(event, args[3]);
    int fx = evtGetValue(event, args[4]);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (f == 0) {
        fx = moveFx(unit);
        evtSetValue(event, args[4], fx);
        bossFxLog(unit, fx, phase);
    }
    evtSetValue(event, args[5], fx > FX_NONE && unit != NULL && bossFxStep(unit, fx, phase, target, f));
    return EVT_RETURN_DONE;
}

/* in: the unit, the frames from letting go to the hit by default; out: its move's (its shot's flight) */
USER_FUNC(evt_pm64boss_fxtravel) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    evtSetValue(event, args[2],
                fxTravel(moveFx(unit),
                         evtGetValue(event, args[1])));
    return EVT_RETURN_DONE;
}

/* in: the unit, the target: its move's burst as a hit reaches the target (a bolt from the bulb, embers, ice) */
USER_FUNC(evt_pm64boss_fxhit) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const BattleUnit* target = ARG_UNIT(1);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0 && target != NULL && !fxOff()) {
        const BossMove* m = &kBosses[b].moves[rtOf(unit)->move];
        if (m->fx > FX_NONE) {
            bossFxLog(unit, m->fx, FXP_HIT);
            bossFxStep(unit, m->fx, FXP_HIT, target, 0);
        } else if (m->kind == MV_SLAM || m->kind == MV_POUND) {
            /* a slam's landing: PM64 EFFECT_LANDING_DUST (tubba_blubba.c EVS_Attack_SlamFist, final_bowser_2.c
               EVS_Attack_BodySlam; with the engine's own puff and shake) */
            effHokoriN64Entry(2, PM64_FRAMES(15), target->position.x, 0.0f, target->position.z + 5.0f);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: its move's look once the hit has landed (the Koopa Bros.' lens flare as one points) */
USER_FUNC(evt_pm64boss_fxpost) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (b >= 0 && rtOf(unit)->move >= 0) {
        const int fx = moveFx(unit);
        if (fx == FX_SHELL_FLARE) {
            bossFxLog(unit, fx, FXP_POST);
            bossFxStep(unit, fx, FXP_POST, NULL, 0);
        }
    }
    return EVT_RETURN_DONE;
}

/* in: the unit: a heal's look (PM64 EFFECT_RECOVER over it) */
USER_FUNC(evt_pm64boss_healfx) {
    EvtWord* args = event->args;
    BattleUnit* unit = ARG_UNIT(0);
    (void)isFirstCall;
    if (unit != NULL && bossOfUnit(unit) >= 0 && !fxOff()) {
        if (bossLog()) {
            printf("[pm64] fx: %s heals: recover (heal) starts\n", kBosses[bossOfUnit(unit)].english);
            fflush(stdout);
        }
        bossFxStep(unit, FX_NONE, FXP_HEAL, NULL, 0);
    }
    return EVT_RETURN_DONE;
}

/* a move's timeline from its phase LW(6) on (LW(3) the target), a frame at a time, in a sibling event of its own */
static EVT_BEGIN(pm64boss_fxrun)
    SET_VALUE(LW(0), 0)
    SET_VALUE(LW(7), -1)
    LABEL(0)
    CALL(evt_pm64boss_fx, BTL_UNIT_SELF, LW(6), LW(3), LW(0), LW(7), LW(1))
    IF_INT_EQUAL(LW(1), 1)
        WAIT_FRAMES(1)
        ADD(LW(0), 1)
        GOTO(0)
    END_IF
    RETURN
EVT_END()

/* the hit on LW(3), LW(4) with weapon LW(12): avoided, or the damage (LW(13): the sound) */
static EVT_BEGIN(pm64boss_hit)
    CALL(evt_pm64boss_fxhit, BTL_UNIT_SELF, LW(3))
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
    CALL(evt_pm64boss_hitlog, BTL_UNIT_SELF, LW(3), LW(5))
    SWITCH(LW(5))
        CASE_EQUAL(1)
            IF_INT_NOT_EQUAL(LW(13), 0)
                CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_NAME, LW(13))
            END_IF
            IF_INT_EQUAL(LW(10), MV_SLAM)
                CALL(evt_btl_camera_shake_h, 0, 6, 0, 24, 0)
                CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
                CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            ELSE
                CALL(evt_btl_camera_shake_h, 0, 2, 1, 10, 0)
            END_IF
            CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), 256, LW(5))
        CASE_EQUAL(3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        CASE_EQUAL(6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        CASE_EQUAL(2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
    END_SWITCH
    RETURN
EVT_END()
/* one hit or more on LW(3), LW(4) (the move's hits; the partner's weapon on the partner) */
static EVT_BEGIN(pm64boss_hits)
    CALL(evt_pm64boss_weapon, BTL_UNIT_SELF, LW(3), LW(12))
    LABEL(0)
    CALL_CHILD(pm64boss_hit)
    CALL(evt_pm64boss_morehits, BTL_UNIT_SELF, LW(3), LW(0))
    IF_INT_EQUAL(LW(0), 1)
        WAIT_FRAMES(12)
        GOTO(0)
    END_IF
    CALL(evt_pm64boss_post, BTL_UNIT_SELF)
    RETURN
EVT_END()
/* a hit per companion: each flies at the target, then comes back (a gang) or is gone (Spit Bits: PM64
 * crystal_king.c EVS_Attack_SpitBits, one hit per Bit he has) */
static EVT_BEGIN(pm64boss_mates)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, LW(12))
    CALL(evt_pm64boss_windup, BTL_UNIT_SELF, 20, LW(5))
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(6), FXP_WIND)
        CALL_CHILD(pm64boss_fxrun)
    END_SIBLING_EVENT
    WAIT_FRAMES(LW(5))
    CALL(evt_pm64boss_release, BTL_UNIT_SELF)
    LABEL(1)
    CALL(evt_pm64boss_nextmate, BTL_UNIT_SELF, LW(6))
    IF_INT_NOT_EQUAL(LW(6), -1)
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_GO, 0)
        CALL(evt_pm64boss_snd, LW(6), BS_JUMP, 0)
        CALL(btlevtcmd_JumpSetting, LW(6), 20, FLOAT(0.0f), FLOAT(0.7f))
        /* (it's out at the target: a counter or superguard that cuts the move short sends it back,
           pm64boss_finish) */
        CALL(evt_pm64boss_mateout, BTL_UNIT_SELF, LW(6))
        CALL(btlevtcmd_JumpPosition, LW(6), LW(0), LW(1), LW(2), 20, -1)
        CALL(evt_pm64boss_matehits, BTL_UNIT_SELF)
        CALL_CHILD(pm64boss_hits)
        CALL(evt_pm64boss_mateback, BTL_UNIT_SELF, LW(0))
        IF_INT_EQUAL(LW(0), 1)
            CALL(btlevtcmd_GetHomePos, LW(6), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpSetting, LW(6), 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, LW(6), LW(0), LW(1), LW(2), 20, -1)
        ELSE
            CALL(btlevtcmd_GetPos, LW(6), LW(0), LW(1), LW(2))
            CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), LW(1), LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
            CALL(btlevtcmd_KillUnit, LW(6), 0)
        END_IF
        CALL(evt_pm64boss_mateout, BTL_UNIT_SELF, -1)
        /* (the target is down: the rest stay home, as TTYD's multi-hit enemies stop) */
        CALL(evt_pm64boss_down, LW(3), LW(0))
        IF_INT_EQUAL(LW(0), 0)
            GOTO(1)
        END_IF
    END_IF
    WAIT_FRAMES(20)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

/* back home: walking, or by hops (LW(8)) */
static EVT_BEGIN(pm64boss_home)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(14))
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    IF_INT_NOT_EQUAL(LW(8), 0)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_JUMP, 0)
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, -1)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_LAND, 0)
    ELSE
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -2, 0)
    END_IF
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    RETURN
EVT_END()

/* in: the unit; out: a slam's rows up, down and landing (the move's row LW(11) where it has none) */
USER_FUNC(evt_pm64boss_slamrows) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    (void)isFirstCall;
    if (b >= 0 && kBosses[b].rise > 0) {
        evtSetValue(event, args[1], kBosses[b].rise);
        evtSetValue(event, args[2], kBosses[b].fall);
        evtSetValue(event, args[3], kBosses[b].land);
    }
    return EVT_RETURN_DONE;
}

/* in: the unit, 1 it has gathered its strength (its idle the angry row) or 0 it has spent it */
USER_FUNC(evt_pm64boss_charged) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = bossOfUnit(unit);
    (void)isFirstCall;
    if (unit != NULL) {
        unit->work[UW_CHARGED] = evtGetValue(event, args[1]);
        /* (charged, it may be dangerous to touch: Huff N. Puff electrified, huff_n_puff.c EVS_Move_Charge*
           ACTOR_EVENT_FLAG_ELECTRIFIED) */
        if (b >= 0) {
            const BossRt* rt = rtOf(unit);
            armHazard(unit, b,
                      unit->work[UW_CHARGED] != 0 && rt->move >= 0 ? kBosses[b].moves[rt->move].hazard : 0);
        }
    }
    return EVT_RETURN_DONE;
}

/* the swarm of darkness that's out (drawn with its unit, pm64_enemy_pc.c) */
static struct {
    const BattleUnit* unit;
    unsigned start;
    float from[3], to[3];
} sSwarm;

int portPm64BossFx(const BattleUnit* unit, int* glow, float from[3], float to[3], int* age) {
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    /* (its glow: 1 charged (pink, pulsing), 2 a lit bulb (warm yellow: PM64 EFFECT_BULB_GLOW), 3 the Star Rod's
       power (cycling colours: final_bowser_2.c UpdateEnchantedBowserColors)) */
    *glow = b < 0                                                ? 0
            : rtOf(unit)->barrier                                ? 3
            : kBosses[b].lamp                                    ? 2
            : kBosses[b].angry > 0 && unit->work[UW_CHARGED] != 0 ? 1
                                                                 : 0;
    if (unit == NULL || unit != sSwarm.unit) {
        return 0;
    }
    /* (its age in PM64 frames: the swarm plays at PM64's speed, as its waits) */
    *age = (int)(gp->retraceCount - sSwarm.start) / PM64_FRAMES(1);
    if (*age > 200) {
        sSwarm.unit = NULL;
        return 0;
    }
    memcpy(from, sSwarm.from, sizeof(sSwarm.from));
    memcpy(to, sSwarm.to, sizeof(sSwarm.to));
    return 1;
}

/* in: the unit, its position, the target's: its swarm sets off */
USER_FUNC(evt_pm64boss_swarmfx) {
    EvtWord* args = event->args;
    int i;
    (void)isFirstCall;
    sSwarm.unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    sSwarm.start = gp->retraceCount;
    for (i = 0; i < 3; i++) {
        sSwarm.from[i] = (float)evtGetValue(event, args[1 + i]);
        sSwarm.to[i] = (float)evtGetValue(event, args[4 + i]);
    }
    bossKey(sSwarm.unit, "swarm", 12);
    bossKey(sSwarm.unit, "squeeze", 60);
    bossKey(sSwarm.unit, "squeeze", 110);
    return EVT_RETURN_DONE;
}

/* in: the unit; out: -1 once it's gone (or never quakes). While it runs, the ground shakes under its steps (PM64
 * tubba_blubba.c EVS_PlayFootstepQuaking: ShakeCam and a rumble every step of its Run) */
USER_FUNC(evt_pm64boss_footstep) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    const Pm64BossSounds* snd = b >= 0 ? kBosses[b].sounds : NULL;
    (void)isFirstCall;
    if (b < 0 || !(kBosses[b].quake || kBosses[b].douse || (snd != NULL && snd->step[0] != NULL)) ||
        unit->work[UW_GONE] != 0 || unit->currentHp <= 0) {
        evtSetValue(event, args[1], -1);
        return EVT_RETURN_DONE;
    }
    evtSetValue(event, args[1], 0);
    if (snd != NULL && snd->step[0] != NULL) {
        /* its own footsteps (PM64 actors.yaml walkSound, one each stepDelay walked; tubba_blubba.c's
           SOUND_HEAVY_NPC_STEP_C, final_bowser_1.c EVS_PlayFootstepSounds' _B and _C), with a ROM only: TTYD's
           boss engine walks them silently */
        BossRt* rt = rtOf(unit);
        const float dx = unit->position.x - rt->stepAt[0], dz = unit->position.z - rt->stepAt[1];
        const float d = sqrtf(dx * dx + dz * dz);
        rt->stepAt[0] = unit->position.x;
        rt->stepAt[1] = unit->position.z;
        if (unit->work[UW_ANIM] == kBosses[b].walk + 1 && d > 0.0f && d < 40.0f &&
            unit->position.y <= unit->homePosition.y + 1.0f) {
            rt->walked += d;
            if (rt->walked >= (float)(snd->stepDist > 0 ? snd->stepDist : 30)) {
                rt->walked = 0.0f;
                bossSnd(unit, snd->step[rt->foot != 0 && snd->step[1] != NULL ? 1 : 0], NULL);
                rt->foot = (signed char)!rt->foot;
            }
        }
    }
    if (kBosses[b].quake && unit->work[UW_ANIM] == kBosses[b].walk + 1 && (gp->retraceCount & 3) == 0) {
        btl_camera_shake_h(0, 1.5f, 0.0f, 4, 0);
    }
    if (kBosses[b].douse > 0 && (kBosses[b].hazard & HZ_FIRE) && unit->parts != NULL) {
        /* frozen, its fire goes out and it is stuck `douse` turns; thawed, it burns again (PM64 lava_bud.c
           EVS_Death, second phase: put out (FlameEffect removed), AVAR_Common_StunTurnsLeft 2; here a freeze) */
        BossRt* rt = rtOf(unit);
        if (unit->freezeTurns > 0 && !rt->doused) {
            rt->doused = 1;
            armHazard(unit, b, rt->armed);
            if (unit->freezeTurns < kBosses[b].douse) {
                unit->freezeTurns = kBosses[b].douse;
            }
        } else if (unit->freezeTurns <= 0 && rt->doused) {
            rt->doused = 0;
            armHazard(unit, b, rt->armed);
        }
    }
    return EVT_RETURN_DONE;
}

static EVT_BEGIN(pm64boss_footsteps)
    LABEL(0)
    WAIT_FRAMES(1)
    CALL(evt_pm64boss_footstep, BTL_UNIT_SELF, LW(0))
    IF_INT_NOT_EQUAL(LW(0), -1)
        GOTO(0)
    END_IF
    RETURN
EVT_END()

/* in: the unit, its move's kind (or -1: it flees); out: the message tag of its line now (0 none). A boss's line
 * before a move is spoken the first time it plays it. */
USER_FUNC(evt_pm64boss_line) {
    EvtWord* args = event->args;
    BattleUnit* unit = BattleGetUnitPtr(_battleWorkPointer, BattleTransID(event, evtGetValue(event, args[0])));
    const int b = unit != NULL ? bossOfUnit(unit) : -1;
    const int kind = evtGetValue(event, args[1]);
    int m;
    (void)isFirstCall;
    evtSetValue(event, args[2], 0);
    if (b < 0) {
        return EVT_RETURN_DONE;
    }
    if (kind < 0) {
        if (kBosses[b].fleeMsg != 0) {
            evtSetValue(event, args[2], (EvtWord)(intptr_t)sLineTag[b][1]);
        }
        return EVT_RETURN_DONE;
    }
    m = kBosses[b].lineMove - 1;
    if (m >= 0 && m < BOSS_MOVES && kBosses[b].lineMsg != 0 && kBosses[b].moves[m].kind == kind &&
        rtOf(unit)->uses[m] == 1 && sayOnce(b, 0)) {
        evtSetValue(event, args[2], (EvtWord)(intptr_t)sLineTag[b][0]);
    }
    return EVT_RETURN_DONE;
}

/* the Dark Swarm (PM64 tubbas_heart.c EVS_Attack_DarkSwarm): Enraged where it stands, energy gathers on it for
 * 60 frames (GATHER_ENERGY_PINK), TalkAngry and a hop as its glow goes out, and the swarm of dark hearts flies
 * over the target (LW(3), LW(4); EFFECT_TUBBA_HEART_ATTACK): it squeezes for 145 frames, then the hit */
static EVT_BEGIN(pm64boss_swarm)
    WAIT_FRAMES(20)
    CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_SWARM0, STRING("SFX_ENM_MAHO_ATTACK1"))
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    ADD(LW(1), 16)
    CALL(evt_eff, STRING(""), STRING("charge"), 0, LW(0), LW(1), LW(2), FLOAT(1.0f), PM64_FRAMES(45), 0, 0, 0, 0, 0, 0)
    WAIT_FRAMES(PM64_FRAMES(60))
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    BEGIN_SIBLING_EVENT
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.8f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 18, -1)
    END_SIBLING_EVENT
    CALL(evt_pm64boss_charged, BTL_UNIT_SELF, 0)
    CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_SWARM1, STRING("SFX_ENM_MAHO_ATTACK1"))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    ADD(LW(1), 5)
    CALL(evt_pm64boss_swarmfx, BTL_UNIT_SELF, LW(5), LW(6), LW(7), LW(0), LW(1), LW(2))
    WAIT_FRAMES(PM64_FRAMES(30))
    CALL(evt_btl_camera_set_homing_unit, 0, LW(3), LW(3))
    CALL(evt_btl_camera_set_zoom, 0, 300)
    WAIT_FRAMES(PM64_FRAMES(56))
    CALL(evt_pm64boss_snd_at, BTL_UNIT_SELF, BS_SWARM2, STRING("SFX_BTL_DAMAGED_PLIABLE1"), LW(3))
    WAIT_FRAMES(PM64_FRAMES(37))
    CALL(evt_pm64boss_snd_at, BTL_UNIT_SELF, BS_SWARM2, STRING("SFX_BTL_DAMAGED_PLIABLE1"), LW(3))
    WAIT_FRAMES(PM64_FRAMES(22))
    CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_SWARM3, 0)
    CALL_CHILD(pm64boss_hits)
    WAIT_FRAMES(30)
    RETURN
EVT_END()

/* one target: a jump onto it, a rush into it, a slam onto it, or a shot from where it stands (LW(10) the kind,
 * LW(11) its row) */
static EVT_BEGIN(pm64boss_single)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 250)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclare, BTL_UNIT_SELF, LW(3), LW(4))
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, LW(12))
    IF_INT_EQUAL(LW(10), MV_SWARM)
        CALL_CHILD(pm64boss_swarm)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
        CALL(evt_btl_camera_set_mode, 0, 0)
        RETURN
    END_IF
    IF_INT_EQUAL(LW(10), MV_SHOT)
        /* (its wind-up row, then its release row: a two-pose move's) */
        CALL(evt_pm64boss_windup, BTL_UNIT_SELF, 30, LW(5))
        BEGIN_SIBLING_EVENT
            SET_VALUE(LW(6), FXP_WIND)
            CALL_CHILD(pm64boss_fxrun)
        END_SIBLING_EVENT
        WAIT_FRAMES(LW(5))
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_GO, STRING("SFX_ENM_MAHO_ATTACK1"))
        CALL(evt_pm64boss_release, BTL_UNIT_SELF)
        /* (its look as it lets go: the breath, the bolt, the ball of fire; then its flight to the target) */
        BEGIN_SIBLING_EVENT
            SET_VALUE(LW(6), FXP_GO)
            CALL_CHILD(pm64boss_fxrun)
        END_SIBLING_EVENT
        CALL(evt_pm64boss_fxtravel, BTL_UNIT_SELF, 15, LW(5))
        WAIT_FRAMES(LW(5))
        CALL_CHILD(pm64boss_hits)
        WAIT_FRAMES(30)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
        CALL(evt_btl_camera_set_mode, 0, 0)
        RETURN
    END_IF
    /* up to the target */
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(14))
    CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 70)
    IF_INT_NOT_EQUAL(LW(8), 0)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
        LOOP(0)
            SUBTRACT(LW(5), 30)
            IF_INT_LESS_OR_EQUAL(LW(5), LW(0))
                BREAK_LOOP
            END_IF
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_JUMP, 0)
            CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(5), 0, LW(2), 14, -1)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_LAND, 0)
        END_LOOP
    ELSE
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 1)
    END_IF
    IF_INT_EQUAL(LW(10), MV_POUND)
        /* (PM64 tubba_blubba.c EVS_Attack_SlamFist: TenseArms 8 frames, RaiseArms 20, SlamArms 3, the blow) */
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
        WAIT_FRAMES(PM64_FRAMES(8))
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_GO, STRING("SFX_ENM_WAN_JUMP1"))
        ADD(LW(11), 1)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
        WAIT_FRAMES(PM64_FRAMES(20))
        ADD(LW(11), 1)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
        WAIT_FRAMES(PM64_FRAMES(3))
        CALL(btlevtcmd_GetPos, LW(3), LW(0), LW(1), LW(2))
        CALL(evt_eff, 0, STRING("kemuri_test"), 4, LW(0), 0, LW(2), FLOAT(1.0f), 0, 0, 0, 0, 0, 0, 0)
        CALL(evt_btl_camera_shake_h, 0, 5, 0, 15, 0)
        SET_VALUE(LW(10), MV_SLAM) /* (the hit's big shake) */
        CALL_CHILD(pm64boss_hits)
        WAIT_FRAMES(30)
        CALL(evt_btl_camera_set_mode, 0, 0)
        CALL_CHILD(pm64boss_home)
        RETURN
    END_IF
    IF_INT_EQUAL(LW(10), MV_RUSH)
        CALL(evt_pm64boss_windup, BTL_UNIT_SELF, 12, LW(5))
        WAIT_FRAMES(LW(5))
        CALL(evt_pm64boss_release, BTL_UNIT_SELF)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_GO, 0)
        CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(10.0f))
        CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 0, -1, 1)
        CALL_CHILD(pm64boss_hits)
    ELSE
        /* a jump (or a slam: a crouch, then high up and down hard) onto it */
        IF_INT_EQUAL(LW(10), MV_SLAM)
            /* (PM64 tubba_blubba.c EVS_Attack_BodySlam: Lean 8 frames, Jump, Fall after 12, Land) */
            SET_VALUE(LW(5), LW(11))
            SET_VALUE(LW(6), LW(11))
            SET_VALUE(LW(7), LW(11))
            CALL(evt_pm64boss_slamrows, BTL_UNIT_SELF, LW(5), LW(6), LW(7))
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(15))
            WAIT_FRAMES(PM64_FRAMES(8))
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_JUMP, STRING("SFX_ENM_WAN_JUMP1"))
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(5))
            BEGIN_SIBLING_EVENT
                WAIT_FRAMES(PM64_FRAMES(12))
                CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(6))
            END_SIBLING_EVENT
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.5f))
            /* (the leap outlasts the Fall's 12 PM64 frames: the Fall row shows on the way down) */
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), PM64_FRAMES(12) + 12, -1)
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(7))
        ELSE
            /* a jump: the squeeze first (PM64 tubbas_heart.c EVS_Attack_Leap: Squeeze 5 frames, then Leap) */
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(15))
            WAIT_FRAMES(PM64_FRAMES(5))
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_JUMP, 0)
            CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
            /* (a two-pose jump: its release row on the way down, as Jr. Troopa's Fall mid-Leap) */
            BEGIN_SIBLING_EVENT
                WAIT_FRAMES(15)
                CALL(evt_pm64boss_release, BTL_UNIT_SELF)
            END_SIBLING_EVENT
            CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 30, -1)
            CALL(evt_pm64boss_release, BTL_UNIT_SELF)
        END_IF
        CALL_CHILD(pm64boss_hits)
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        ADD(LW(0), 50)
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), 0, LW(2), 25, -1)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_LAND, 0)
    END_IF
    WAIT_FRAMES(20)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL_CHILD(pm64boss_home)
    CALL(evt_pm64boss_fxpost, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* everyone: a blow (fire, wind, a blizzard...) from where it stands, or a quake */
static EVT_BEGIN(pm64boss_all)
    CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
    CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    IF_INT_EQUAL(LW(3), -1)
        RETURN
    END_IF
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
    CALL(btlevtcmd_WaitGuardMove)
    CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, LW(12))
    CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(6), FXP_WIND)
        CALL_CHILD(pm64boss_fxrun)
    END_SIBLING_EVENT
    IF_INT_EQUAL(LW(10), MV_QUAKE)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(15))
        CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(0.7f))
        CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 45, -1)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
        CALL(evt_btl_camera_shake_h, 0, 6, 0, 30, 0)
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_LAND, STRING("SFX_ENM_WAN_LANDING1"))
    ELSE
        CALL(evt_pm64boss_windup, BTL_UNIT_SELF, 40, LW(5))
        WAIT_FRAMES(LW(5))
        CALL(evt_pm64boss_release, BTL_UNIT_SELF)
        IF_INT_NOT_EQUAL(LW(13), 0)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_NAME, LW(13))
        END_IF
        CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_GO, 0)
        SET_VALUE(LW(13), 0)
    END_IF
    /* (its look as it lets go: the bolts, the shockwave, the rocks; then the time they take to reach everyone) */
    BEGIN_SIBLING_EVENT
        SET_VALUE(LW(6), FXP_GO)
        CALL_CHILD(pm64boss_fxrun)
    END_SIBLING_EVENT
    CALL(evt_pm64boss_fxtravel, BTL_UNIT_SELF, 0, LW(5))
    IF_INT_GREATER(LW(5), 0)
        /* (a look that reaches the player's side, the bolts, the rocks, the wave: the whole stage in view) */
        CALL(evt_btl_camera_set_mode, 0, 0)
        WAIT_FRAMES(LW(5))
    END_IF
    SET_VALUE(LW(6), 0)
    LABEL(0)
    CALL(evt_pm64boss_fxhit, BTL_UNIT_SELF, LW(3))
    CALL(evt_pm64boss_weapon, BTL_UNIT_SELF, LW(3), LW(12))
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    IF_INT_EQUAL(LW(5), 1)
        IF_INT_EQUAL(LW(6), 0)
            CALL(btlevtcmd_ResultACDefence, LW(3), LW(12))
            SET_VALUE(LW(6), 1)
        END_IF
        CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
    ELSE
        CALL(btlevtcmd_StartAvoid, LW(3), 38)
    END_IF
    CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
    IF_INT_NOT_EQUAL(LW(3), -1)
        GOTO(0)
    END_IF
    WAIT_MS(500)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

/* a heal of LW(9) HP, a charge (its hits LW(9) stronger next turn: TTYD's Charge), or a warning (it gathers its
 * strength, swirling, and its next turn is the move it warned of) */
static EVT_BEGIN(pm64boss_self)
    CALL(evt_btl_camera_set_mode, 0, 8)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_SELF)
    CALL(evt_btl_camera_set_zoom, 0, 200)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
    WAIT_FRAMES(30)
    SWITCH(LW(10))
        CASE_EQUAL(MV_HEAL)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_HEAL, STRING("SFX_BTL_RECOVERY1"))
            CALL(evt_pm64boss_healfx, BTL_UNIT_SELF)
            CALL(btlevtcmd_RecoverHp, BTL_UNIT_SELF, 1, LW(9))
        CASE_EQUAL(MV_CHARGE)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_POWER, STRING("SFX_BTL_CHARGE1"))
            CALL(btlevtcmd_OnOffStatus, BTL_UNIT_SELF, STATUS_CHARGE, 1, LW(9), 1)
        CASE_EQUAL(MV_SUMMON)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_SUMMON, 0)
            SET_VALUE(LW(8), BTL_UNIT_SELF)
            SET_VALUE(LW(9), 0)
            CALL_CHILD(pm64boss_summon)
        CASE_EQUAL(MV_CLONES)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_CLONES0, 0)
            CALL_CHILD(pm64boss_clones)
        CASE_EQUAL(MV_SWALLOW)
            CALL_CHILD(pm64boss_swallow)
        CASE_EQUAL(MV_BARRIER)
            /* (PM64 final_bowser_1.c EVS_StarRodCast: Brandish, the rod gathers light 65 frames, light rays 30,
               enchanted, star shimmer and the power-up 75 frames, RearUpLaugh (the move's post) and a shockwave) */
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_CAST0, STRING("SFX_BTL_CHARGE1"))
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            ADD(LW(1), 80)
            CALL(evt_eff, STRING(""), STRING("charge"), 0, LW(0), LW(1), LW(2), FLOAT(2.5f), PM64_FRAMES(65), 0, 0,
                 0, 0, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(65))
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_CAST1, STRING("SFX_ENM_BARRI_BARRIER1"))
            WAIT_FRAMES(PM64_FRAMES(30))
            CALL(evt_pm64boss_barrier, BTL_UNIT_SELF)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_BARRIER0, 0)
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            ADD(LW(1), 50)
            CALL(evt_eff, STRING(""), STRING("stardust"), 2, LW(0), LW(1), LW(2), 80, 80, 80, 100, 0, 0, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(45))
            CALL(evt_pm64boss_post, BTL_UNIT_SELF)
            CALL(evt_btl_camera_shake_h, 0, 4, 0, 20, 0)
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_BARRIER1, STRING("SFX_BTL_CHARGE1"))
        CASE_DEFAULT
            /* (PM64 tubbas_heart.c EVS_Move_Charge: energy gathers for 60 frames, a hop, charged) */
            CALL(evt_pm64boss_snd, BTL_UNIT_SELF, BS_POWER, STRING("SFX_BTL_CHARGE1"))
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            ADD(LW(1), 15)
            CALL(evt_eff, STRING(""), STRING("charge"), 0, LW(0), LW(1), LW(2), FLOAT(2.0f), PM64_FRAMES(60), 0, 0,
                 0, 0, 0, 0)
            WAIT_FRAMES(PM64_FRAMES(60))
            CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
            CALL(btlevtcmd_JumpSetting, BTL_UNIT_SELF, 20, FLOAT(0.0f), FLOAT(1.0f))
            CALL(btlevtcmd_JumpPosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 15, -1)
            CALL(evt_pm64boss_charged, BTL_UNIT_SELF, 1)
    END_SWITCH
    CALL(evt_pm64boss_post, BTL_UNIT_SELF) /* (Huff N. Puff's BragBody after the inhale) */
    WAIT_FRAMES(40)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    CALL(evt_btl_camera_set_mode, 0, 0)
    RETURN
EVT_END()

static EVT_BEGIN(pm64boss_attack)
    CALL(btlevtcmd_AddUnitWork, BTL_UNIT_SELF, UW_TURN, 1)
    CALL(evt_pm64boss_pick, BTL_UNIT_SELF, LW(10), LW(11), LW(12), LW(13), LW(14), LW(15), LW(9), LW(8))
    IF_INT_EQUAL(LW(10), -1)
        GOTO(99)
    END_IF
    IF_INT_EQUAL(LW(10), MV_REST)
        CALL_CHILD(pm64boss_rest)
        GOTO(99)
    END_IF
    /* its line at the turn's start (its first turn, low HP), in its talk row */
    CALL(evt_pm64boss_turnmsg, BTL_UNIT_SELF, LW(0), LW(1))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_btl_camera_set_mode, 0, 7)
        CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
        CALL(evt_btl_camera_set_zoom, 0, 150)
        WAIT_FRAMES(30)
        IF_INT_NOT_EQUAL(LW(1), -1)
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(1))
        END_IF
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, BTL_UNIT_SELF)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
        CALL(evt_btl_camera_set_mode, 0, 0)
    END_IF
    /* its move's own line, the first time (from off the field: a plain box) */
    CALL(evt_pm64boss_movemsg, BTL_UNIT_SELF, LW(0), LW(1))
    IF_INT_NOT_EQUAL(LW(0), 0)
        IF_INT_EQUAL(LW(1), 0)
            CALL(evt_btl_camera_set_mode, 0, 7)
            CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
            CALL(evt_btl_camera_set_zoom, 0, 150)
            WAIT_FRAMES(40)
            CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, BTL_UNIT_SELF)
            CALL(evt_btl_camera_set_mode, 0, 0)
        ELSE
            CALL(evt_msg_print, 0, LW(0), 0, 0)
        END_IF
    END_IF
    /* a tower brother with others standing: the tower first */
    CALL_CHILD(pm64boss_build)
    /* its line before the move, the first time (PM64 tubbas_heart.c EVS_TakeTurn: before its first Charge, the
       camera on it, Wait 40, ActorSpeak MSG_CH3_00C5 in TalkAngry) */
    CALL(evt_pm64boss_line, BTL_UNIT_SELF, LW(10), LW(0))
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(evt_btl_camera_set_mode, 0, 7)
        CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, BTL_UNIT_NONE)
        CALL(evt_btl_camera_set_zoom, 0, 150)
        WAIT_FRAMES(40)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, LW(11))
        CALL(evt_msg_print, MSG_PRINT_BATTLE_UNIT, LW(0), 0, BTL_UNIT_SELF)
        CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
        CALL(evt_btl_camera_set_mode, 0, 0)
    END_IF
    /* the stage darkens for lightning (PM64 SetBackgroundAlpha 0 -> 200), and lightens after (LABEL 99) */
    CALL(evt_pm64boss_darken, BTL_UNIT_SELF, LW(0))
    IF_INT_GREATER(LW(0), 0)
        CALL(evt_batstage_set_stg_dark, 200, LW(0), 0)
        CALL(evt_batstage_set_aud_dark, 200, LW(0), 0)
        WAIT_FRAMES(LW(0))
    END_IF
    IF_INT_GREATER_OR_EQUAL(LW(10), MV_HEAL)
        CALL_CHILD(pm64boss_self)
        GOTO(99)
    END_IF
    IF_INT_GREATER_OR_EQUAL(LW(10), MV_ALL)
        CALL_CHILD(pm64boss_all)
        GOTO(99)
    END_IF
    CALL(btlevtcmd_check_battleflag, LW(0), 2)
    IF_INT_NOT_EQUAL(LW(0), 0)
        CALL(btlevtcmd_GetFirstAttackTarget, LW(3), LW(4))
    ELSE
        CALL(btlevtcmd_GetEnemyBelong, BTL_UNIT_SELF, LW(0))
        CALL(btlevtcmd_SamplingEnemy, BTL_UNIT_SELF, LW(0), LW(12))
        CALL(btlevtcmd_ChoiceSamplingEnemy, LW(12), LW(3), LW(4))
    END_IF
    IF_INT_EQUAL(LW(3), -1)
        CALL(btlevtcmd_CheckToken, BTL_UNIT_SELF, TOKEN_FLAG_CONFUSED_ACTION, LW(0))
        IF_INT_NOT_EQUAL(LW(0), 0)
            /* (the move's end first: the stage it darkened, its charge and hazard) */
            CALL_CHILD(pm64boss_finish)
            CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
            CALL_CHILD(subsetevt_confuse_flustered)
            RETURN
        END_IF
        GOTO(99)
    END_IF
    CALL(evt_pm64boss_mateshits, BTL_UNIT_SELF, LW(0))
    IF_INT_EQUAL(LW(0), 1)
        CALL_CHILD(pm64boss_mates)
        GOTO(99)
    END_IF
    CALL_CHILD(pm64boss_single)
    LABEL(99)
    CALL_CHILD(pm64boss_finish)
    CALL(evt_pm64boss_row, BTL_UNIT_SELF, -1)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static EVT_BEGIN(pm64boss_init)
    CALL(btlevtcmd_SetEventWait, BTL_UNIT_SELF, PTR(pm64boss_wait))
    CALL(btlevtcmd_SetEventAttack, BTL_UNIT_SELF, PTR(pm64boss_attack))
    CALL(btlevtcmd_SetEventDamage, BTL_UNIT_SELF, PTR(pm64boss_damage))
    CALL(btlevtcmd_SetEventConfusion, BTL_UNIT_SELF, PTR(pm64boss_attack))
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_TURN, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_CHARGE, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_ANIM, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_FORCE, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_CHARGED, 0)
    CALL(btlevtcmd_SetUnitWork, BTL_UNIT_SELF, UW_GONE, 0)
    CALL(evt_pm64boss_reset, BTL_UNIT_SELF)
    SPAWN_EVENT(pm64boss_footsteps)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

static void buildKinds(void) {
    static int sBuilt;
    int b, i;
    if (sBuilt) {
        return;
    }
    sBuilt = 1;
    bossTables();
    for (b = 0; b < BOSSES; b++) {
        const Pm64Boss* p = &kBosses[b];
        BattleUnitKind* k = &sKinds[b];
        BattleUnitKindPart* part = &sParts[b];
        /* its hit box and cursors follow its drawn size: PM64's actor size (its units are its sprite's pixels) at
           its PM64 scale and PM64's size beside Mario (portPm64Size, as pm64_enemy_pc.c draws it); a head on a vine
           stands the vine's height up */
        const float f = p->scale * portPm64Size();
        const float vine = (float)p->vine * f, head = (float)p->height * f;
        const float w = (float)p->width * f, h = head + vine;
        snprintf(sTag[b][0], sizeof(sTag[b][0]), "pc_un_pm64b%d", b);
        snprintf(sTag[b][1], sizeof(sTag[b][1]), "pc_hlp_pm64b%d", b);
        for (i = 0; i < 5; i++) {
            sDefence[b].defenses[i] = (s8)p->defense;
        }
        memset(part, 0, sizeof(*part));
        part->partNum = 1;
        part->partName = sTag[b][0];
        part->modelName = "c_koburon";
        part->hitBaseOffset.y = vine + head * 0.5f;
        part->hitCursorBaseOffset.y = h;
        part->shadowSize = (s16)w;
        part->hitHeight = (s16)h;
        part->baseAlpha = 255;
        part->defense = &sDefence[b];
        /* engine 2: elements it shrugs off (2: immune), what touching it does */
        memset(&sDefenceAttr[b], 0, sizeof(sDefenceAttr[b]));
        sDefenceAttr[b].defenseAttrs[ELEMENT_FIRE] = (p->immune & IM_FIRE) ? 2 : 0;
        sDefenceAttr[b].defenseAttrs[ELEMENT_ICE] = (p->immune & IM_ICE) ? 2 : 0;
        sDefenceAttr[b].defenseAttrs[ELEMENT_EXPLOSION] = (p->immune & IM_BLAST) ? 2 : 0;
        sDefenceAttr[b].defenseAttrs[ELEMENT_ELECTRIC] = (p->immune & IM_SHOCK) ? 2 : 0;
        part->defenseAttr = &sDefenceAttr[b];
        part->attributes = PART_ATTR_MAIN | PART_ATTR_GUARD_COVER;
        part->counterAttributes = (PartsCounterAttribute_Flags)(((p->hazard & HZ_FIRE) ? COUNTER_ATTR_FIERY : 0) |
                                                                ((p->hazard & HZ_SPIKE) ? COUNTER_ATTR_TOP_SPIKY : 0) |
                                                                ((p->hazard & HZ_SHOCK) ? COUNTER_ATTR_ELECTRIC : 0) |
                                                                ((p->hazard & HZ_ICE) ? COUNTER_ATTR_ICY : 0));
        part->poseTable = sPoses;
        memset(k, 0, sizeof(*k));
        k->unitType = (BattleUnitType)p->unitType;
        k->name = sTag[b][0];
        k->maxHp = (s16)(p->hp * 5 / 2); /* post-game: x 2.5 (a story enemy's: set where it's met) */
        k->dangerHP = (s16)(k->maxHp / 4);
        k->perilHP = (s16)(k->maxHp / 10);
        k->level = 50;
        k->bonusExp = 20;
        k->baseCoin = 30;
        k->pbSoftCap = 9999;
        k->width = (s16)(w + 0.5f);
        k->height = (s16)(h + 0.5f);
        k->hitOffset[0] = 6;
        k->hitOffset[1] = (s16)(h + 0.5f);
        k->centerOffset.y = vine + head * 0.5f;
        k->talkTogeBaseOffset.y = h;
        k->heldItemBaseOffset.x = -w * 0.5f;
        k->heldItemBaseOffset.y = vine + head * 0.5f;
        k->burnFlameOffset.y = vine + head * 0.6f;
        k->bintaHitOffset.y = vine + head * 0.6f;
        k->kissHitOffset.y = vine + head * 0.6f;
        k->cutBaseOffset.y = vine + head * 0.5f;
        k->cutWidth = w;
        k->cutHeight = h;
        k->itemStealParam = 30;
        k->damageSfxName = "SFX_BTL_DAMAGED_PLIABLE1";
        if (p->sounds != NULL && p->sounds->hurt != NULL) {
            /* its own hurt sound, TTYD's thud the stand-in (battle_damage.c plays it by name: port/pm64_sfx.h) */
            static char sHurt[BOSS_UNITS_MAX][64];
            snprintf(sHurt[b], sizeof(sHurt[b]), "%s|SFX_BTL_DAMAGED_PLIABLE1", p->sounds->hurt);
            k->damageSfxName = sHurt[b];
        }
        k->fireDamageSfxName = "SFX_BTL_DAMAGE_FIRE1";
        k->iceDamageSfxName = "SFX_BTL_DAMAGE_ICE1";
        k->explosionDamageSfxName = "SFX_BTL_DAMAGE_BIRIBIRI1";
        k->statusVulnerability = p->home == 0 ? &sRegist : p->width >= 36 ? &sRegistFoeBig : &sRegistFoe;
        k->numParts = 1;
        k->parts = part;
        k->initEventCode = pm64boss_init;
        k->dataTable = sData;
        /* a flyer: hammers can't reach it, quakes pass under it; a clone: not counted for the win, no numbers */
        k->attributeFlags = (p->fly > 0 ? UNIT_ATTR_FLOATING : 0) |
                            (p->illusory ? UNIT_ATTR_IGNORE_FOR_CLEAR | UNIT_ATTR_NO_DAMAGE_NUMBER : 0);
        for (i = 0; i < BOSS_MOVES && !moveEnd(&p->moves[i]); i++) {
            weaponFor(&sWeapons[b][i], &p->moves[i], 0);
            weaponFor(&sWeaponsP[b][i], &p->moves[i], 1);
            snprintf(sMoveTag[b][i], sizeof(sMoveTag[b][i]), "pc_pm64b%d_m%d", b, i);
        }
        memset(&sSpawn[b], 0, sizeof(sSpawn[b]));
        sSpawn[b].unitKindParams = k;
        sSpawn[b].alliance = 1;
        sSpawn[b].attackPhase = BATTLE_PHASE_ENEMY;
        sSpawn[b].position.x = 300.0f;
    }
}

/* a boss's line from the ROM (its own words, in the game's language) as a speech bubble's message: PM64's new
 * page (a blank line in the plain text) a new bubble */
static void speechText(unsigned id, const char* tag) {
    char raw[640], out[768];
    size_t o = 0;
    int i, n;
    if (id == 0 || (n = portPm64MessageText(id, portPm64GameLanguage(), raw, sizeof(raw))) <= 0) {
        return;
    }
    while (n > 0 && raw[n - 1] == '\n') {
        n--;
    }
    for (i = 0; i < n && o + 8 < sizeof(out); i++) {
        if (raw[i] == '\n' && i + 1 < n && raw[i + 1] == '\n') {
            memcpy(out + o, "<p>", 3);
            o += 3;
            while (i + 1 < n && raw[i + 1] == '\n') {
                i++;
            }
        } else {
            out[o++] = raw[i];
        }
    }
    memcpy(out + o, "<k>", 4);
    portModsSetMessage(tag, out);
}

/* a unit's Tattle Log entry, and its name and Tattle from the ROM */
static void bossTexts(int b) {
    TattleEntry* tattle = battleGetUnitMonosiriPtr(kBosses[b].unitType);
    char name[64], text[1024];
    tattle->enemyTag = sTag[b][0];
    tattle->tattleBtlTag = sTag[b][1];
    tattle->modelName = "c_koburon";
    tattle->poseName = "S_1";
    if (portPm64MessageText(PM64_MSG(0x29, kBosses[b].nameMsg), portPm64GameLanguage(), name, sizeof(name)) <= 0) {
        snprintf(name, sizeof(name), "%s", kBosses[b].english);
    }
    if (kBosses[b].tattleMsg < 0 ||
        portPm64MessageText(PM64_MSG(0x1C, kBosses[b].tattleMsg), portPm64GameLanguage(), text, sizeof(text)) <= 0) {
        /* (Paper Mario never fights Kammy, so its ROM has no Tattle of her) */
        snprintf(text, sizeof(text), portTrGame("That's %s, from Paper Mario.<p>Max HP is %d.<k>"), kBosses[b].english,
                 sKinds[b].maxHp);
    }
    {
        /* the Tattle's numbers as they are here (post-game, or scaled to the chapter) */
        int i, attack = 0;
        for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
            if (kBosses[b].moves[i].weight > 0 && kBosses[b].moves[i].kind < MV_HEAL) {
                attack = (int)sWeapons[b][i].hpDamageWork[0];
                break;
            }
        }
        portPm64TattleNumbers(text, sizeof(text), sKinds[b].maxHp, attack);
    }
    portBattlexVariantTexts(kBosses[b].unitType, name, text);
    snprintf(sLineTag[b][0], sizeof(sLineTag[b][0]), "pc_pm64b%d_line", b);
    snprintf(sLineTag[b][1], sizeof(sLineTag[b][1]), "pc_pm64b%d_flee", b);
    speechText(kBosses[b].lineMsg, sLineTag[b][0]);
    speechText(kBosses[b].fleeMsg, sLineTag[b][1]);
    snprintf(sLineTag[b][2], sizeof(sLineTag[b][2]), "pc_pm64b%d_start", b);
    snprintf(sLineTag[b][3], sizeof(sLineTag[b][3]), "pc_pm64b%d_low", b);
    speechText(kBosses[b].startMsg, sLineTag[b][2]);
    speechText(kBosses[b].lowMsg, sLineTag[b][3]);
    {
        static const char* const kLine[] = {"tip", "build", "bounce", "break"};
        const unsigned msg[] = {kBosses[b].tipMsg, kBosses[b].buildMsg, kBosses[b].bounceMsg, kBosses[b].breakMsg};
        int i;
        for (i = 0; i < 4; i++) {
            snprintf(sLineTag[b][LINE_TIP + i], sizeof(sLineTag[b][LINE_TIP + i]), "pc_pm64b%d_%s", b, kLine[i]);
            speechText(msg[i], sLineTag[b][LINE_TIP + i]);
        }
    }
    if (kBosses[b].bounceMsg != 0 || kBosses[b].barrierPal > 0) {
        /* the barrier's hint: Paper Mario's says to use the Star Beam (or Peach Beam), which isn't here */
        char hint[256];
        snprintf(hint, sizeof(hint), "%s<k>",
                 portTrGame("Ordinary attacks can't get through that barrier. A Star Power special will break it!"));
        portModsSetMessage(kHintTag, hint);
    }
    {
        int i;
        for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
            speechText(kBosses[b].moves[i].msg, sMoveTag[b][i]);
        }
    }
}

/* --- the fight --------------------------------------------------------------------------------------------- */

static NpcSetupBattleInfo sInfo;
static NpcBattleSelect sSelect[2];
static BattleGroupSetup sGroup;
static BattleUnitSetup sUnits[1 + BOSS_WITH];
static PointDropData sNoDrops[1];

/* in: the stand-in NPC's name, the boss, the area battle table entry whose stage it's fought on */
/* unit u's size beside Mario, drawn and PM64's (logged once a fight for each of its units): its idle row's height
 * in pixels at its PM64 scale, a vine's height added; drawn: half a unit a pixel x portPm64Size (pm64_enemy_pc.c)
 * beside TTYD Mario's 21.8 units (pm64_partner_pc.c) */
static void logSize(int u) {
    const float mario = portPm64MarioHeightPx(), size = portPm64Size();
    const float px =
        (portPm64NpcSpriteHeight(kBosses[u].sprite, kBosses[u].idle) + (float)kBosses[u].vine) * kBosses[u].scale;
    if (mario > 0.0f) {
        printf("[pm64] sizes: %s %.0f px, %.2f of Mario (PM64 %.2f)\n", kBosses[u].english, px,
               0.5f * px * size / 21.8f, px / mario);
        fflush(stdout);
    }
}

/* 1 the area battle table has entry `stage` (its terminator, a NULL name, not passed) with a fight on a stage */
static int stageOk(const NpcSetupBattleInfo* tbl, int stage) {
    int i;
    if (tbl == NULL || stage < 0) {
        return 0;
    }
    for (i = 0; i <= stage; i++) {
        if (tbl[i].name == NULL) {
            return 0;
        }
    }
    return tbl[stage].select != NULL && tbl[stage].select[0].weight != 0 && tbl[stage].select[0].stageData != NULL;
}

int portPm64FightHasEarly(int fight) {
    const int b = portPm64FightBoss(fight);
    bossTables();
    return b >= 0 && b < FIGHTS && kBosses[b].early1 > 0;
}

/* The fight's numbers as they are now (scaled or the post-game's), every unit in it: its own units (sMain) and
 * their companions, summons and pieces. "[pm64] boss stats: <unit>: <HP> HP, defence <d>; hits <move power>, ..."
 * (the hit as weaponFor set it: bossPower; a heal as healed; the Koopa Bros.' Spin by the tower's height). */
static void statsLog(int want) {
    int u, m;
    for (u = 0; u < BOSSES; u++) {
        char line[256];
        int at, i, in = sMain[u];
        for (m = 0; !in && m < BOSSES; m++) {
            in = sMain[m] && mateKind(m, u);
        }
        if (!in) {
            continue;
        }
        at = snprintf(line, sizeof(line), "[pm64] boss stats: %s (%s, Mario level %d against %d, %d%%): %d HP, defence %d;",
                      kBosses[u].english, sMain[u] ? "boss" : "companion",
                      pouchGetPtr() != NULL ? (int)pouchGetPtr()->level : 0, want, sScale > 0 ? sScale : 0,
                      sKinds[u].maxHp, ownDefence(u));
        for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[u].moves[i]) && at < (int)sizeof(line) - 16; i++) {
            const BossMove* mv = &kBosses[u].moves[i];
            if (mv->kind < MV_HEAL) {
                at += snprintf(line + at, sizeof(line) - (size_t)at, " move %d hits %d", i + 1,
                               (int)sWeapons[u][i].hpDamageWork[0]);
                if (mv->partner > 0) {
                    at += snprintf(line + at, sizeof(line) - (size_t)at, " (partner %d)",
                                   (int)sWeaponsP[u][i].hpDamageWork[0]);
                }
                at += snprintf(line + at, sizeof(line) - (size_t)at, ",");
            } else if (mv->kind == MV_HEAL) {
                at += snprintf(line + at, sizeof(line) - (size_t)at, " move %d heals %d,", i + 1,
                               sScale > 0 ? scaled(mv->power * 2 / 5, 1) : mv->power);
            }
        }
        printf("%s\n", line);
    }
    fflush(stdout);
}

USER_FUNC(evt_pm64boss_battle) {
    EvtWord* args = event->args;
    NpcEntry* npc = npcNameToPtr_NoAssert((const char*)evtGetValue(event, args[0]));
    const int b = evtGetValue(event, args[1]);
    const int stage = evtGetValue(event, args[2]);
    const int flags = evtGetValue(event, args[3]); /* NPC_BATTLE_* added (a fight from the boss list: LOSS_OK) */
    char area[4];
    AreaData* ad;
    const NpcSetupBattleInfo* tbl;
    int u, n, lead, count, guard = 0;
    (void)isFirstCall;
    memcpy(area, gp->currentMapName, 3);
    area[3] = '\0';
    ad = areaDataPtr(area);
    tbl = ad != NULL ? (const NpcSetupBattleInfo*)ad->btlSetupTbl : NULL;
    /* (the tables first: FIGHTS is 0 until then. The rematch's name, "Koopa Bros. (rematch)", is modes_aurora.c's
       own, so nothing had built them when it was the first PM64 fight of a launch: no battle set on the NPC, and
       seq_battle.c's stage was NULL) */
    bossTables();
    /* out (args[4]): 1 the fight is set, 0 refused (the caller starts no battle: a battle with no stage reads
       battleInfo.stageData NULL in seq_battle.c) */
    evtSetValue(event, args[4], 0);
    if (npc == NULL || b < 0 || b >= FIGHTS || !stageOk(tbl, stage)) {
        printf("[pm64] boss %d: no fight set (%s, stage %d: %s): refused\n", b, area, stage,
               npc == NULL ? "no NPC" : tbl == NULL ? "no battle table" : "no such stage");
        fflush(stdout);
        return EVT_RETURN_DONE;
    }
    buildKinds();
    /* its earliest version asked for (PM64_BOSS_FLAG_EARLY: the story meeting, a refight's "first fight"): that one
       leads, against PM64's level 1 (Jr. Troopa's prologue fight). Before, the story meeting at level 1 fought his
       last fight (60 HP, 8-9 a hit, scaled to 5 a hit): no way through. */
    const int early = (flags & PM64_BOSS_FLAG_EARLY) && kBosses[b].early1 > 0 && kBosses[b].early1 <= BOSSES;
    /* scaled to Mario's level (PM64_BOSS_FLAG_SCALE): every unit of its file, before its Tattle's numbers */
    sScale = 0;
    const int want = early ? 1 : b < (int)sizeof(kPm64Level) ? kPm64Level[b] : 27;
    if ((flags & PM64_BOSS_FLAG_SCALE) && pouchGetPtr() != NULL) {
        const int level = pouchGetPtr()->level < want ? pouchGetPtr()->level : want;
        sScale = 50 + 50 * (level > 0 ? level : 1) / want;
    }
    sDefCap = 99;
    if (sScale > 0) {
        const PouchData* pouch = pouchGetPtr();
        const int gear = pouch->jumpLevel > pouch->hammerLevel ? pouch->jumpLevel : pouch->hammerLevel;
        sDefCap = gear > 1 ? (gear < 3 ? gear : 3) - 1 : 0; /* (1 normal, 2 Super, 3 Ultra: their hit, less one) */
    }
    /* (the kinds are built once: each fight sets its file's HP and hits again, scaled or the post-game's) */
    for (u = 0; u < BOSSES; u++) {
        int i;
        if (sSetOf[u] != sSetOf[b] || kBosses[u].home != 0) {
            continue;
        }
        sKinds[u].maxHp = (s16)(sScale > 0 ? scaled(kBosses[u].hp, 1) : kBosses[u].hp * 5 / 2);
        sKinds[u].dangerHP = (s16)(sKinds[u].maxHp / 4);
        sKinds[u].perilHP = (s16)(sKinds[u].maxHp / 10);
        for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[u].moves[i]); i++) {
            weaponFor(&sWeapons[u][i], &kBosses[u].moves[i], 0);
            weaponFor(&sWeaponsP[u][i], &kBosses[u].moves[i], 1);
        }
    }
    /* every unit of the fight (its lead and the ones that take a fallen one's place): its Tattle Log entry,
       name and Tattle */
    lead = early ? kBosses[b].early1 - 1 : kBosses[b].lead1 > 0 ? kBosses[b].lead1 - 1 : b;
    {
        /* (the boss named: the lead, or its version's first unit; a lead that is a companion's (Tubba's Heart) is
           the fight's start, the boss its own entry) */
        const int named = early ? lead : b;
        if (sScale > 0) {
            printf("[pm64] boss scaled: Mario level %d against %d: %d%% of PM64's own (%s: %d HP of %d, defence %d of "
                   "%d)\n",
                   (int)pouchGetPtr()->level, want, sScale, kBosses[named].english, sKinds[named].maxHp,
                   kBosses[named].hp, ownDefence(named), kBosses[named].defense);
            fflush(stdout);
        }
        printf("[pm64] boss version: %s's %s fight\n", kBosses[b].english,
               early ? "first" : kBosses[b].early1 > 0 ? "last" : "only");
        fflush(stdout);
    }
    /* (every unit of its file: the ones it brings in, summons, clones, sheds) */
    for (u = 0; u < BOSSES; u++) {
        if (sSetOf[u] == sSetOf[b]) {
            bossTexts(u);
            setDefence(u, ownDefence(u));
        }
    }
    /* the fight's own units: its lead and those that take its place, in turn */
    memset(sMain, 0, sizeof(sMain));
    for (u = lead; u >= 0 && u < BOSSES && guard < 8; u = kBosses[u].spawn1 - 1, guard++) {
        sMain[u] = 1;
    }
    statsLog(want);
    memset(&sTower, 0, sizeof(sTower));
    memset(sRt, 0, sizeof(sRt));
    memset(sSaid, 0, sizeof(sSaid));
    /* (the last fight's swarm: its unit pointer may be a new fight's unit now) */
    memset(&sSwarm, 0, sizeof(sSwarm));
    sMateDone = 0;
    memset(sUnits, 0, sizeof(sUnits));
    sUnits[0].unitKindParams = &sKinds[lead];
    sUnits[0].alliance = 1;
    sUnits[0].attackPhase = BATTLE_PHASE_ENEMY;
    for (n = 0; n < BOSS_WITH && kBosses[lead].with1[n] > 0 && kBosses[lead].with1[n] <= BOSSES; n++) {
    }
    /* (alone in the middle; one or two beside it: it a little forward; more: further forward) */
    sUnits[0].position.x = n == 0 ? 90.0f : n <= 2 ? 60.0f : 40.0f;
    sUnits[0].position.y = (float)kBosses[lead].fly;
    sLeadHome[0] = sUnits[0].position.x;
    sLeadHome[1] = sUnits[0].position.y;
    sLeadHome[2] = sUnits[0].position.z;
    count = 1;
    for (u = 0; u < n; u++) {
        const int w = kBosses[lead].with1[u] - 1;
        float pos[3];
        int j, same = 0;
        slotPos(&sUnits[0].position, w, u, n, pos);
        for (j = 0; j < u; j++) {
            same += kBosses[lead].with1[j] - 1 == w;
        }
        if (same > 0 && (kBosses[w].at[0] != 0 || kBosses[w].at[1] != 0 || kBosses[w].at[2] != 0)) {
            pos[0] += 45.0f * (float)same; /* (look-alikes with one place: side by side) */
        }
        sUnits[count] = sUnits[0];
        sUnits[count].unitKindParams = &sKinds[w];
        sUnits[count].position.x = pos[0];
        sUnits[count].position.y = pos[1];
        sUnits[count].position.z = pos[2];
        sUnits[count].unitWork[0] = u + 1; /* (its place: MV_SUMMON brings back the ones gone) */
        count++;
    }
    memset(&sGroup, 0, sizeof(sGroup));
    sGroup.numEnemies = count;
    sGroup.enemyData = sUnits;
    sGroup.noItemWeight = 1;
    sGroup.hpDropTable = sNoDrops;
    sGroup.fpDropTable = sNoDrops;
    memset(sSelect, 0, sizeof(sSelect));
    sSelect[0].weight = 1;
    sSelect[0].groupSetup = &sGroup;
    sSelect[0].stageData = tbl[stage].select[0].stageData;
    sInfo = tbl[stage];
    sInfo.name = sTag[b][0];
    sInfo.fixedItem = 0;
    sInfo.swFlag = -1;
    sInfo.altSelect = NULL;
    sInfo.select = sSelect;
    sInfo.bgmName = PM64_BOSS_TRACK; /* (TTYD's boss theme; a name the game's list hasn't was silence) */
    sInfo.battleFlags |= flags & ~(PM64_BOSS_FLAG_SCALE | PM64_BOSS_FLAG_EARLY);
    npcSetupBattleInfo(npc, &sInfo);
    evtSetValue(event, args[4], 1);
    for (u = 0; u < BOSSES; u++) {
        if (sSetOf[u] == sSetOf[b]) {
            logSize(u);
        }
    }
    /* its own Paper Mario battle theme from the ROM over that track ("pm64_boss_music", pm64_music.h) */
    portPm64MusicBossSong(kBosses[b].song > 0 ? kBosses[b].song : -1);
    printf("[pm64] boss: %s (%d HP; first %s) on %s's stage %d\n", kBosses[b].english, sKinds[early ? lead : b].maxHp,
           kBosses[lead].english, area, stage);
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* --- the story's Paper Mario 64 enemies with no cousin here --------------------------------------------------------
 * (port/include/port/pm64_foes.h): in the areas they fit, a picked ordinary fight (Mixed: two in five, All: every
 * one) has one of them, beside its enemies (a fight of one or two) or in place of its last one. The field NPC stays
 * the area's own; the newcomer's numbers are PM64's scaled to the chapter, its level and rewards the enemy's it
 * stands beside. */

static const struct {
    char area[4];
    signed char foes[3]; /* pm64_bosses/story_foes.c's units (from 0), -1 none */
} kFoeAreas[] = {
    {"win", {0, 1, -1}}, /* Boggly Woods: Forest Fuzzies, Hyper Goombas */
    {"gra", {2, 1, -1}},  /* Twilight Trail: Hyper Paragoombas and Hyper Goombas */
    {"jin", {8, 9, -1}},  /* Creepy Steeple: Clubbas, guards of a haunted castle, and Duplighosts */
    {"muj", {5, 10, -1}}, /* Keelhaul Key's jungle: Spy Guys in their camouflage, Spear Guys (Jade Jungle's) */
    {"dou", {6, -1, -1}}, /* Pirate's Grotto: Pyro Guys */
    {"eki", {3, 4, 5}},   /* Riverside Station's machinery: Shy Guys, Groove Guys, Spy Guys */
    {"bom", {8, -1, -1}}, /* Fahr Outpost: Clubbas */
    {"aji", {3, 7, -1}},  /* the X-Naut Fortress: Shy Guys and Medi Guys */
    {"las", {9, -1, -1}}, /* the Palace of Shadow: Duplighosts (Bowser's Castle's) */
};
#define FOE_AREAS (int)(sizeof(kFoeAreas) / sizeof(kFoeAreas[0]))

#define FOE_SCRIPTED                                                                                               \
    (NPC_BATTLE_NO_ESCAPE | NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_PLAYER_ENTRY | NPC_BATTLE_NO_JUMP |                \
     NPC_BATTLE_NO_HAMMER | NPC_BATTLE_NO_PARTY_CHANGE)
#define FOE_POOL 32
#define FOE_UNITS 5

static struct {
    const NpcEntry* npc;
    BattleGroupSetup group;
    BattleUnitSetup units[FOE_UNITS];
} sFoePool[FOE_POOL];
static int sFoeNext;

static unsigned foeHash(unsigned h, const char* s, int n) {
    while (s != NULL && n-- > 0 && *s != '\0') {
        h = (h ^ (unsigned char)*s++) * 16777619u;
    }
    return h;
}

/* the PM64 enemy b met in chapter: its HP and each move's power scaled */
static void foeScale(int b, int chapter) {
    int i;
    sKinds[b].maxHp = (s16)portPm64FoeScale(kBosses[b].hp, kBosses[b].home, chapter, 0);
    sKinds[b].dangerHP = (s16)(sKinds[b].maxHp / 3);
    sKinds[b].perilHP = (s16)(sKinds[b].maxHp / 5);
    for (i = 0; i < BOSS_MOVES && !moveEnd(&kBosses[b].moves[i]); i++) {
        sWeapons[b][i].hpDamageWork[0] = portPm64FoeScale(kBosses[b].moves[i].power, kBosses[b].home, chapter, 1);
    }
}

static const float kFoeSlotX[FOE_UNITS][FOE_UNITS] = {
    {90.0f}, {70.0f, 110.0f}, {50.0f, 90.0f, 130.0f}, {30.0f, 70.0f, 110.0f, 150.0f},
    {10.0f, 50.0f, 90.0f, 130.0f, 170.0f},
};

static void foeSetupPost(NpcEntry* npc, NpcSetupBattleInfo* setup) {
    NpcBattleInfo* info;
    const BattleGroupSetup* g;
    const BattleUnitKind* beside;
    unsigned h;
    int a, i, n, foe, chapter, slot = -1;
    if (npc == NULL || setup == NULL || setup->name == NULL || gp == NULL || !portPm64FoesActive() ||
        (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished) || portVersusOn() ||
        (chapter = portPm64FoesChapter(gp->currentMapName)) < 0 || (setup->battleFlags & FOE_SCRIPTED) != 0) {
        return;
    }
    /* a field enemy only: a townsfolk NPC (evt_npc_setup's battleInfoId 0) holds its area's btlSetupTbl[0]
       fight too, one it never starts; it would take a pool slot a live enemy's fight still points at. Field
       enemies come from the map's NPC database (NPC_FLAG_RECORD_DEFEAT, set before this), or have NPC_FLAG_ENEMY
       or a dead event from their setup (the enemies' own init scripts set NPC_FLAG_ENEMY later). */
    if ((npc->flags & (NPC_FLAG_ENEMY | NPC_FLAG_RECORD_DEFEAT)) == 0 && npc->deadEvent == NULL) {
        return;
    }
    for (a = 0; a < FOE_AREAS; a++) {
        if (strncmp(gp->currentMapName, kFoeAreas[a].area, 3) == 0) {
            break;
        }
    }
    info = &npc->battleInfo;
    g = info->groupSetup;
    if (a == FOE_AREAS || g == NULL || g->numEnemies <= 0 || g->numEnemies > FOE_UNITS || g->enemyData == NULL) {
        return;
    }
    for (i = 0; i < g->numEnemies; i++) {
        const BattleUnitSetup* u = &g->enemyData[i];
        if (u->unitKindParams == NULL || u->alliance != 1 || u->unitKindParams->unitType >= UNIT_BONETAIL ||
            bossOfParams(u->unitKindParams) >= 0) {
            return; /* (a boss's, a scripted fight's or our own units) */
        }
    }
    /* which fights, and which of the area's enemies: the room and the NPC (the same every game) */
    h = foeHash(foeHash(0x811C9DC5u ^ 0x64u, gp->currentMapName, 16), npc->description, 32);
    if (portPm64FoesMode() < 2 && h % 5u >= 2u) {
        return;
    }
    for (n = 0; n < 3 && kFoeAreas[a].foes[n] >= 0; n++) {
    }
    bossTables();
    if (sFoeBase < 0) {
        return;
    }
    foe = sFoeBase + kFoeAreas[a].foes[(h / 5u) % (unsigned)n];
    for (i = 0; i < FOE_POOL; i++) {
        if (sFoePool[i].npc == npc) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = sFoeNext;
        sFoeNext = (sFoeNext + 1) % FOE_POOL;
    }
    buildKinds();
    foeScale(foe, chapter);
    bossTexts(foe);
    sFoePool[slot].npc = npc;
    sFoePool[slot].group = *g;
    n = g->numEnemies;
    memcpy(sFoePool[slot].units, g->enemyData, sizeof(BattleUnitSetup) * (size_t)n);
    beside = g->enemyData[n - 1].unitKindParams;
    if (n < 3) {
        sFoePool[slot].units[n] = g->enemyData[n - 1];
        n++;
    }
    sFoePool[slot].units[n - 1].unitKindParams = &sKinds[foe];
    sFoePool[slot].units[n - 1].itemDropTable = NULL;
    for (i = 0; i < n; i++) {
        sFoePool[slot].units[i].position.x = kFoeSlotX[n - 1][i];
    }
    /* its rewards the enemy's beside it */
    sKinds[foe].level = beside->level;
    sKinds[foe].bonusExp = beside->bonusExp;
    sKinds[foe].baseCoin = beside->baseCoin;
    sFoePool[slot].group.numEnemies = n;
    sFoePool[slot].group.enemyData = sFoePool[slot].units;
    info->groupSetup = &sFoePool[slot].group;
    {
        const char* log = getenv("TTYD_PM64_LOG"); /* (tests) */
        if (log != NULL && log[0] != '\0' && log[0] != '0') {
            printf("[pm64] story: %s %s joins (%d enemies), HP %d, attack %d\n", gp->currentMapName,
                   kBosses[foe].english, n, sKinds[foe].maxHp, (int)sWeapons[foe][0].hpDamageWork[0]);
            fflush(stdout);
            logSize(foe);
        }
    }
}

/* BattleCalculateDamage(...), after: what the hit on a boss unit is, for bossDamagePre (BattleDamageDirect) right
 * after it: a Star Power special (its weapon's baseSpCost: the Crystal Star moves) breaks a barrier; a weapon that
 * flips shelled foes (jumps, quakes: SPECIAL_FLIPS_SHELLED) flips a lone Koopa brother (PM64's flip trigger) */
static struct {
    const BattleUnit* target;
    signed char special, flip;
} sCalc;

static void calcPost(s32* result, BattleUnit* attacker, BattleUnit* target, BattleUnitPart* part, BattleWeapon* weapon,
                     u32* resultFlags, u32 flags) {
    (void)result, (void)part, (void)resultFlags, (void)flags;
    sCalc.target = NULL;
    if (attacker == NULL || target == NULL || weapon == NULL || attacker->alliance != 0 || bossOfUnit(target) < 0) {
        return;
    }
    sCalc.target = target;
    sCalc.special = weapon->baseSpCost > 0;
    sCalc.flip = (weapon->special & SPECIAL_FLIPS_SHELLED) != 0;
}

/* a hit on a unit that flees when low (Tubba's Heart, PM64 tubbas_heart.c EVS_HandleEvent: at 5 HP of 50 or less
 * EVS_FleeFromBattle): the hit that takes it to a tenth of its HP leaves it 1 HP or more and marks it fleeing
 * (no KO, so no death and no battle's end), and once fleeing it takes no more */
static int bossDamagePre(s32* unitId, BattleUnit** target, BattleUnitPart** part, s32* damage, s32* fpDamage,
                         u32* result, u32* pattern, s32* flag) {
    BattleUnit* t = *target;
    const Pm64Boss* p;
    BossRt* rt;
    int b, at, give = 0;
    (void)unitId, (void)part, (void)fpDamage, (void)result, (void)pattern, (void)flag;
    if (t != NULL && *damage > 0 && bossOfUnit(t) < 0 && _battleWorkPointer != NULL && bossLog()) {
        /* (tests: a boss's blow lands) */
        const BattleUnit* by = BattleGetUnitPtr(_battleWorkPointer, _battleWorkPointer->activeUnitId);
        if (bossOfUnit(by) >= 0) {
            /* (the hit as it lands, after defence and badges: the scaling's check, pm64_boss_story_*) */
            printf("[pm64] boss: %s hits kind %d for %d\n", kBosses[bossOfUnit(by)].english, (int)t->currentKind,
                   (int)*damage);
            fflush(stdout);
        }
        bossKey(by, "hit", 1);
        bossKey(by, "afterhit", 20);
    }
    if (t == NULL || (b = bossOfUnit(t)) < 0) {
        return 0;
    }
    p = &kBosses[b];
    rt = rtOf(t);
    if ((p->home != 0 || p->moves[0].kind == MV_REST) && bossLog() && getenv("TTYD_PM64_HOLD") != NULL &&
        getenv("TTYD_PM64_HOLD")[0] == '1') {
        /* (tests, $TTYD_PM64_HOLD=1: a story enemy, or a unit that never acts (General Guy's Light Bulb), takes
           no damage, so the move that needs it is played: the pm64_bossfx_foe_* and _general_guy scenarios) */
        *damage = 0;
        return 0;
    }
    if (p->illusory) {
        /* a clone: the hit goes through it and it vanishes (PM64 crystal_king.c, ACTOR_EVENT_FLAG_ILLUSORY) */
        *damage = 0;
        rt->vanish = 1;
        return 0;
    }
    if (p->tower && towerPlace(t) >= 0) {
        /* the tower takes no damage: a hit tips it, a second the same round topples it (PM64 fake_bowser.c
           EVS_KoopaBros_HandleEvent, EVS_Broadcast_ToppleHit) */
        *damage = 0;
        if (!sTower.tipped) {
            sTower.tipped = 1;
            sTower.sayOver = 1;
        } else {
            sTower.falling = 1;
        }
        sCalc.target = NULL;
        return 0;
    }
    if (sCalc.target == t) {
        /* (what BattleCalculateDamage saw of this hit: calcPost) */
        if (rt->barrier) {
            if (sCalc.special) {
                /* a Star Power special breaks the barrier (PM64's Star Beam / Peach Beam: final_bowser_2.c
                   EVENT_PEACH_BEAM); this hit does no damage, as the beam didn't */
                rt->barrier = 0;
                rt->broke = 1;
                rt->brokeTurn = (short)t->work[UW_TURN];
                setDefence(b, ownDefence(b));
                *damage = 0;
                printf("[pm64] boss: %s's barrier breaks\n", p->english);
                fflush(stdout);
            } else {
                rt->bounced = 1;
            }
        } else if (sCalc.flip && p->tower && towerPlace(t) < 0 && rt->toppled == 0) {
            /* a lone brother jumped on flips onto his back: 2 turns at defence 0 (common_koopa_bros.inc.c
               EVENT_FLIP_TRIGGER: AVAR_Koopa_ToppleTurns 2, ToppledDefense) */
            rt->flip = 1;
            rt->toppled = 2;
            setDefence(b, 0);
        }
        sCalc.target = NULL;
    }
    if (*damage <= 0) {
        return 0;
    }
    rt->shed = p->shed1 > 0;
    if (p->spawn1 <= 0) {
        return 0;
    }
    if (t->work[UW_GONE] != 0) {
        *damage = 0;
        return 0;
    }
    /* gives way at its phaseAt share (0: a tenth, Tubba's Heart; -1: as it would fall) */
    at = p->phaseAt == 0 ? 10 : p->phaseAt;
    give = at < 0 ? *damage >= t->currentHp : (t->currentHp - *damage) * 100 <= at * t->maxHp;
    if (p->leaveHits > 0 && _battleWorkPointer != NULL) {
        /* it leaves once it's been hit in leaveHits turns (General Guy's Shy Squad: 2) */
        if (rt->lastHitTurn != _battleWorkPointer->turnCount + 1) {
            rt->lastHitTurn = (short)(_battleWorkPointer->turnCount + 1);
            rt->hitTurns++;
        }
        give |= rt->hitTurns >= p->leaveHits;
    }
    if (give && p->exit == EXIT_BEATEN) {
        /* a wave: the next comes in only once the last of this one falls (its twin may still stand) */
        int i;
        for (i = 0; i < BATTLE_UNIT_MAX; i++) {
            const BattleUnit* u = unitAt(i);
            const int k = bossOfUnit(u);
            if (alive(u) && u != t && u->alliance == 1 && k >= 0 && kBosses[k].spawn1 == p->spawn1 &&
                u->work[UW_GONE] == 0) {
                return 0;
            }
        }
    }
    if (give) {
        if (*damage >= t->currentHp) {
            *damage = t->currentHp - 1;
        }
        t->work[UW_GONE] = 1;
        printf("[pm64] boss: %s flees at %d HP\n", p->english, t->currentHp - *damage);
        fflush(stdout);
    }
    return 0;
}

/* $TTYD_PM64_CURVE=1 (tests, the docs' table): the level scaling's numbers for every fight (and Jr. Troopa's first)
 * at Mario's levels 1, 5, 10, 20 and 30, as evt_pm64boss_battle would set them: "[pm64] curve: <fight> level L
 * (N%): <unit> <HP> HP, hits <a>/<b>...; ..." (each unit of the fight: its lead and those taking its place, their
 * companions; a hit as weaponFor sets it, before badges, defence and the Koopa Bros.' tower height) */
static void curveLog(void) {
    static const int kLevels[] = {1, 5, 10, 20, 30};
    const int savedScale = sScale;
    int b, v, l, u, i;
    bossTables();
    for (b = 0; b < FIGHTS; b++) {
        for (v = 0; v < 2; v++) {
            const int lead = v ? kBosses[b].early1 - 1 : kBosses[b].lead1 > 0 ? kBosses[b].lead1 - 1 : b;
            const int want = v ? 1 : b < (int)sizeof(kPm64Level) ? kPm64Level[b] : 27;
            signed char main[BOSS_UNITS_MAX];
            int guard = 0;
            if (v && kBosses[b].early1 <= 0) {
                continue;
            }
            memset(main, 0, sizeof(main));
            for (u = lead; u >= 0 && u < BOSSES && guard < 8; u = kBosses[u].spawn1 - 1, guard++) {
                main[u] = 1;
            }
            for (l = 0; l < 5; l++) {
                const int level = kLevels[l] < want ? kLevels[l] : want;
                char line[512];
                int at;
                sScale = 50 + 50 * level / want;
                at = snprintf(line, sizeof(line), "[pm64] curve: %s%s level %d (%d%%):", kBosses[b].english,
                              v ? " (first fight)" : "", kLevels[l], sScale);
                for (u = 0; u < BOSSES && at < (int)sizeof(line) - 48; u++) {
                    int m, in = main[u];
                    for (m = 0; !in && m < BOSSES; m++) {
                        in = main[m] && mateKind(m, u);
                    }
                    if (!in || kBosses[u].home != 0) {
                        continue;
                    }
                    at += snprintf(line + at, sizeof(line) - (size_t)at, " %s %d HP, hits", kBosses[u].english,
                                   scaled(kBosses[u].hp, 1));
                    for (i = 0, m = 0; i < BOSS_MOVES && !moveEnd(&kBosses[u].moves[i]) && at < (int)sizeof(line) - 8;
                         i++) {
                        if (kBosses[u].moves[i].kind < MV_HEAL && kBosses[u].moves[i].power > 0) {
                            at += snprintf(line + at, sizeof(line) - (size_t)at, "%s%d", m++ ? "/" : " ",
                                           bossPower(kBosses[u].moves[i].power));
                        }
                    }
                    at += snprintf(line + at, sizeof(line) - (size_t)at, "%s;", m ? "" : " none");
                }
                printf("%s\n", line);
            }
        }
    }
    fflush(stdout);
    sScale = savedScale;
}

void portPm64FoeUnitsInit(void) {
    {
        const char* curve = getenv("TTYD_PM64_CURVE");
        if (curve != NULL && curve[0] == '1') {
            curveLog();
        }
    }
    portModHostAddHook(0, kSlot_BattleDamageDirect, (void*)bossDamagePre);
    portModHostAddHook(1, kSlot_BattleCalculateDamage, (void*)calcPost);
    portModHostAddHook(1, kSlot_npcSetupBattleInfo, (void*)foeSetupPost);
}
