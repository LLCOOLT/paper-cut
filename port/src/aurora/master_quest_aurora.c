/* Master Quest's battle side (port/include/port/master_quest.h,
 * docs/MASTER_QUEST.md):
 *
 * - fights: npcSetupBattleInfo, after the game picked a field enemy's fight,
 *   gives it the set's formation for its area and first enemy: the units
 *   made from the area's own fights' entries for them (their item tables,
 *   heights and work), placed as the game places 1-5 enemies, the held items
 *   rolled again as the game rolls them. Each NPC's formation is kept in a
 *   pool by its entry, as long as its battle info points at it.
 * - enemies: as each enemy enters a fight (BtlUnit_Entry) its changes are
 *   found (the set's, then a script's hook) and kept by its unit id; its HP
 *   changes at once, its starting statuses at the fight's opening
 *   (btlseqFirstAct), and its attacks on Mario's side go through
 *   BattleCheckDamage as a changed copy of the weapon (the game's own
 *   weapon data is never written).
 *
 * Every choice comes from the settings, the area's data and the game's own
 * random numbers, so online co-op's two games make the same ones. */
#include "port/master_quest.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/modes.h"
#include "port/settings.h"
#include "port/versus.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/mapdata.h"
#include "driver/npcdrv.h"
#include "mario/mariost.h"
#include "system.h"

#include <stdio.h>
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

#define SCRIPTED                                                                                                   \
    (NPC_BATTLE_NO_ESCAPE | NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_PLAYER_ENTRY | NPC_BATTLE_NO_JUMP |                \
     NPC_BATTLE_NO_HAMMER | NPC_BATTLE_NO_PARTY_CHANGE)

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

static void areaNow(char area[4]) {
    memcpy(area, gp->currentMapName, 3);
    area[3] = '\0';
}

/* the play modes that stage their own fights, and versus battles */
static int ownFightsNow(void) {
    return (portModeStatus()->mode != PORT_MODE_NONE && !portModeStatus()->finished) || portVersusOn();
}

/* every enemy of the group is one a set can use (no boss, tutorial or part) */
static int ordinaryGroup(const BattleGroupSetup* g) {
    int i;
    if (g == NULL || g->numEnemies <= 0 || g->enemyData == NULL) {
        return 0;
    }
    for (i = 0; i < g->numEnemies; i++) {
        const BattleUnitSetup* u = &g->enemyData[i];
        if (u->unitKindParams == NULL || u->alliance != 1 ||
            portMasterQuestUnitName(u->unitKindParams->unitType) == NULL) {
            return 0;
        }
    }
    return 1;
}

/* --- the area's units -------------------------------------------------------- */

#define TEMPLATES 128 /* every kind of enemy an area's fights have (the Pit's are the most) */
static char sTplArea[4];
static const void* sTplTable;
static BattleUnitSetup sTpl[TEMPLATES];
static int sTplCount;

static void addTemplates(const NpcBattleSelect* sel) {
    int i;
    for (; sel != NULL && sel->weight > 0; sel++) {
        const BattleGroupSetup* g = sel->groupSetup;
        if (g == NULL || g->enemyData == NULL) {
            continue;
        }
        for (i = 0; i < g->numEnemies; i++) {
            const BattleUnitSetup* u = &g->enemyData[i];
            int k, known = 0;
            if (u->unitKindParams == NULL || u->alliance != 1) {
                continue;
            }
            for (k = 0; k < sTplCount && !known; k++) {
                known = sTpl[k].unitKindParams->unitType == u->unitKindParams->unitType;
            }
            if (!known && sTplCount < TEMPLATES) {
                sTpl[sTplCount++] = *u;
            }
        }
    }
}

/* the area's entries for each kind of enemy its fights have (its REL is linked) */
static void scanArea(const char* area) {
    const AreaData* a = areaDataPtr(area);
    const NpcSetupBattleInfo* table = a != NULL ? (const NpcSetupBattleInfo*)a->btlSetupTbl : NULL;
    int i;
    if (strcmp(area, sTplArea) == 0 && table == sTplTable) {
        return;
    }
    snprintf(sTplArea, sizeof(sTplArea), "%s", area);
    sTplTable = table;
    sTplCount = 0;
    for (i = 0; table != NULL && table[i].name != NULL && i < 512; i++) {
        addTemplates(table[i].select);
        addTemplates(table[i].altSelect);
    }
}

static const BattleUnitSetup* templateOf(int type) {
    int i;
    for (i = 0; i < sTplCount; i++) {
        if ((int)sTpl[i].unitKindParams->unitType == type) {
            return &sTpl[i];
        }
    }
    return NULL;
}

/* --- fights ------------------------------------------------------------------- */

typedef struct Formation {
    const NpcEntry* npc;
    BattleGroupSetup group;
    BattleUnitSetup units[PORT_MQ_UNITS];
} Formation;

#define POOL 96
static Formation sPool[POOL];
static int sPoolNext;

static Formation* poolFor(const NpcEntry* npc) {
    int i;
    for (i = 0; i < POOL; i++) {
        if (sPool[i].npc == npc) {
            return &sPool[i];
        }
    }
    /* a slot no NPC's battle info points at now (else the oldest) */
    for (i = 0; i < POOL; i++) {
        Formation* f = &sPool[(sPoolNext + i) % POOL];
        if (f->npc == NULL || f->npc->battleInfo.groupSetup != &f->group) {
            sPoolNext = (sPoolNext + i + 1) % POOL;
            return f;
        }
    }
    sPoolNext = (sPoolNext + 1) % POOL;
    return &sPool[sPoolNext];
}

/* the game's places for 1-5 enemies (x), and the depth that goes with each */
static const float kSlotX[PORT_MQ_UNITS][PORT_MQ_UNITS] = {
    {90.0f},
    {70.0f, 110.0f},
    {50.0f, 90.0f, 130.0f},
    {30.0f, 70.0f, 110.0f, 150.0f},
    {10.0f, 50.0f, 90.0f, 130.0f, 170.0f},
};

static float depthAt(float x) {
    return x < 20.0f ? -20.0f : x < 60.0f ? -10.0f : x < 100.0f ? 0.0f : x < 140.0f ? 10.0f : 20.0f;
}

/* the set's line for this fight: one of its area's lines for the lead */
static int setFormation(const PortMqSet* set, const char* area, int lead, u32 h, short* units) {
    int i, matches = 0, pick;
    for (i = 0; i < set->fightCount; i++) {
        matches += strcmp(set->fights[i].area, area) == 0 && set->fights[i].lead == lead;
    }
    if (matches == 0) {
        return 0;
    }
    pick = (int)(h % (u32)matches);
    for (i = 0; i < set->fightCount; i++) {
        const PortMqFight* f = &set->fights[i];
        if (strcmp(f->area, area) == 0 && f->lead == lead && pick-- == 0) {
            memcpy(units, f->units, sizeof(short) * (size_t)f->count);
            return f->count;
        }
    }
    return 0;
}

/* npcSetupBattleInfo(npc, setup), after: the fight it picked, remixed */
static void setupPost(NpcEntry* npc, NpcSetupBattleInfo* setup) {
    const PortMqSet* set = portMasterQuestActive();
    const PortMasterQuestHooks* hooks = portMasterQuestHooks();
    NpcBattleInfo* info;
    BattleGroupSetup* g;
    Formation* f;
    char area[4];
    short units[PORT_MQ_UNITS];
    int count = 0, i, n, changed = 0, groupIndex = 0, lead;
    const NpcBattleSelect* sel;
    u32 h;
    if ((set == NULL && (hooks == NULL || hooks->formation == NULL)) || npc == NULL || setup == NULL ||
        setup->name == NULL || gp == NULL || ownFightsNow()) {
        return;
    }
    info = &npc->battleInfo;
    g = info->groupSetup;
    if ((setup->battleFlags & SCRIPTED) != 0 || !ordinaryGroup(g)) {
        return;
    }
    areaNow(area);
    if (strcmp(area, "tou") == 0) {
        return; /* the Glitz Pit's matches keep their teams */
    }
    scanArea(area);
    lead = g->enemyData[0].unitKindParams->unitType;
    for (sel = setup->select; sel != NULL && sel->weight > 0; sel++, groupIndex++) {
        if (sel->groupSetup == g) {
            break;
        }
    }
    h = hashStr(hashStr(hashStr(hashInt(2166136261u, (u32)(portSettingsFind("remix_seed") != NULL
                                                                ? *portSettingsFind("remix_seed")
                                                                : 1)),
                                        gp->currentMapName),
                                npc->description),
                        setup->name);
    h = hashInt(h, (u32)groupIndex);
    if (set != NULL) {
        short wanted[PORT_MQ_UNITS];
        const int m = setFormation(set, area, g->enemyData[0].unitKindParams->unitType, h, wanted);
        for (i = 0; i < m; i++) {
            if (templateOf(wanted[i]) != NULL) {
                units[count++] = wanted[i];
            }
        }
        changed = count > 0;
    }
    if (!changed) {
        for (i = 0; i < g->numEnemies && i < PORT_MQ_UNITS; i++) {
            units[count++] = (short)g->enemyData[i].unitKindParams->unitType;
        }
    }
    if (hooks != NULL && hooks->formation != NULL) {
        short before[PORT_MQ_UNITS];
        int kept = 0;
        memcpy(before, units, sizeof(units));
        n = hooks->formation(hooks->user, area, gp->currentMapName, units, count);
        n = n < 0 ? 0 : n > PORT_MQ_UNITS ? PORT_MQ_UNITS : n;
        for (i = 0; i < n; i++) {
            if (portMasterQuestUnitName(units[i]) != NULL && templateOf(units[i]) != NULL) {
                units[kept++] = units[i];
            }
        }
        if (kept > 0 && (kept != count || memcmp(before, units, sizeof(short) * (size_t)kept) != 0)) {
            count = kept;
            changed = 1;
        } else {
            memcpy(units, before, sizeof(units));
        }
    }
    if (!changed) {
        return;
    }
    f = poolFor(npc);
    f->npc = npc;
    f->group = *g;
    f->group.numEnemies = count;
    f->group.enemyData = f->units;
    for (i = 0; i < count; i++) {
        const float x = kSlotX[count - 1][i];
        f->units[i] = *templateOf(units[i]);
        f->units[i].position.x = x;
        f->units[i].position.z = depthAt(x);
    }
    info->groupSetup = &f->group;
    /* the held items, rolled as npcSetupBattleInfo rolls them */
    memset(info->heldItems, 0, sizeof(info->heldItems));
    memset(info->itemDropLists, 0, sizeof(info->itemDropLists));
    for (i = 0; i < count; i++) {
        ItemDropData* table = f->units[i].itemDropTable;
        ItemDropData* drop;
        s32 total = 0;
        if (table == NULL) {
            continue;
        }
        for (drop = table; drop->holdWeight > 0 || drop->type; drop++) {
            total += drop->holdWeight;
        }
        if (total <= 0) {
            continue;
        }
        total = irand(total);
        for (drop = table; drop->holdWeight > 0 || drop->type; drop++) {
            if ((total -= drop->holdWeight) < 0) {
                info->heldItems[i] = drop->type;
                info->itemDropLists[i] = table;
                break;
            }
        }
    }
    npc->baseCoinCount = f->units[0].unitKindParams->baseCoin;
    {
        /* the fight it was: its first enemy's kind (the NPC's own name is the
         * game's Japanese, in Shift-JIS on every release) */
        const char* was = portMasterQuestUnitName(lead);
        if (was != NULL) {
            printf("[master quest] %s %s:", gp->currentMapName, was);
        } else {
            printf("[master quest] %s unit %d:", gp->currentMapName, lead);
        }
    }
    for (i = 0; i < count; i++) {
        printf(" %s", portMasterQuestUnitName(units[i]));
    }
    printf("\n");
    fflush(stdout);
}

/* --- enemies -------------------------------------------------------------------- */

static int sBattleOn;      /* this fight takes enemy changes */
static int sOpened;        /* the starting statuses were given */
static char sBattleArea[4];
static PortMqEnemy sUnit[BATTLE_UNIT_MAX];
static u8 sUnitHas[BATTLE_UNIT_MAX];

static void noChanges(PortMqEnemy* e) {
    memset(e, 0, sizeof(*e));
    e->hpPercent = 100;
    e->element = -1;
    e->hit[0].type = e->hit[1].type = -1;
    e->start[0].type = e->start[1].type = -1;
}

static int anyChange(const PortMqEnemy* e) {
    return e->hpAdd != 0 || e->hpPercent != 100 || e->atkAdd != 0 || e->element >= 0 || e->pierce || e->variant ||
           e->hit[0].type >= 0 || e->hit[1].type >= 0 || e->start[0].type >= 0 || e->start[1].type >= 0;
}

/* the set's changes for an enemy in this area (an area's own line before "*") */
static int changesFor(const PortMqSet* set, int type, PortMqEnemy* out) {
    int i;
    const PortMqEnemy* any = NULL;
    noChanges(out);
    for (i = 0; set != NULL && i < set->enemyCount; i++) {
        const PortMqEnemy* e = &set->enemies[i];
        if (e->unit != type) {
            continue;
        }
        if (strcmp(e->area, sBattleArea) == 0) {
            *out = *e;
            return 1;
        }
        if (e->area[0] == '\0' && any == NULL) {
            any = e;
        }
    }
    if (any != NULL) {
        *out = *any;
        return 1;
    }
    return 0;
}

/* the variant= lines for this fight's enemies armed (port/include/port/enemy_variant.h), before they enter;
 * nothing armed (another mode's left alone) when it has none */
static void armVariants(const PortMqSet* set, const BattleGroupSetup* g) {
    PortEnemyVariant list[PORT_VARIANT_ARMED_MAX];
    int count = 0, i, k;
    for (i = 0; set != NULL && i < g->numEnemies && count < PORT_VARIANT_ARMED_MAX; i++) {
        const int type = g->enemyData[i].unitKindParams->unitType;
        PortMqEnemy e;
        int dup = 0;
        for (k = 0; k < count; k++) {
            dup |= list[k].baseKind == type;
        }
        if (!dup && changesFor(set, type, &e) && e.variant > 0 && e.variant <= set->variantCount) {
            list[count++] = set->variants[e.variant - 1];
        }
    }
    if (count > 0) {
        /* alongside what another mode armed for this fight's other kinds (the story's Paper Mario 64 enemies,
           port/include/port/pm64_foes.h, whichever hook runs first): this set's lines win for their kinds */
        const PortEnemyVariant* armed;
        const int had = portEnemyVariantArmed(&armed);
        for (i = 0; i < had && count < PORT_VARIANT_ARMED_MAX; i++) {
            int dup = 0;
            for (k = 0; k < count; k++) {
                dup |= list[k].baseKind == armed[i].baseKind;
            }
            if (!dup) {
                list[count++] = armed[i];
            }
        }
        portEnemyVariantArm(list, count);
    }
}

/* Btl_UnitSetup(wp), before: whether this fight takes changes */
static int unitSetupPre(BOOL* result, BattleWork** wpp) {
    BattleWork* wp = *wpp;
    const NpcBattleInfo* info = wp != NULL && wp->fbatInfo != NULL ? wp->fbatInfo->setup : NULL;
    const PortMasterQuestHooks* hooks = portMasterQuestHooks();
    (void)result;
    sBattleOn = 0;
    sOpened = 0;
    memset(sUnitHas, 0, sizeof(sUnitHas));
    if (info == NULL || gp == NULL || ownFightsNow() ||
        (portMasterQuestActive() == NULL && (hooks == NULL || hooks->enemy == NULL))) {
        return 0;
    }
    areaNow(sBattleArea);
    if (!ordinaryGroup(info->groupSetup)) {
        return 0;
    }
    /* scripted fights stay, but for the Glitz Pit's matches (losing one is allowed) */
    if ((info->battleFlags & (strcmp(sBattleArea, "tou") == 0 ? (SCRIPTED & ~NPC_BATTLE_LOSS_OK) : SCRIPTED)) != 0) {
        return 0;
    }
    sBattleOn = 1;
    armVariants(portMasterQuestActive(), info->groupSetup);
    return 0;
}

/* BtlUnit_Entry(setup), after: an enemy's changes found, its HP changed */
static void entryPost(BattleUnit** result, BattleUnitSetup* setup) {
    BattleUnit* unit = *result;
    const PortMasterQuestHooks* hooks = portMasterQuestHooks();
    PortMqEnemy e;
    int has, hp;
    (void)setup;
    if (!sBattleOn || unit == NULL || unit->alliance != 1 || unit->unitId < 0 || unit->unitId >= BATTLE_UNIT_MAX) {
        return;
    }
    has = changesFor(portMasterQuestActive(), unit->initialKind, &e);
    if (hooks != NULL && hooks->enemy != NULL && portMasterQuestUnitName(unit->initialKind) != NULL) {
        hooks->enemy(hooks->user, sBattleArea, unit->initialKind, &e);
        has = 1;
    }
    sUnitHas[unit->unitId] = (u8)(has && anyChange(&e));
    if (!sUnitHas[unit->unitId]) {
        return;
    }
    sUnit[unit->unitId] = e;
    hp = unit->baseMaxHp;
    if (e.hpPercent != 100) {
        hp = (hp * e.hpPercent + 50) / 100;
    }
    hp += e.hpAdd;
    hp = hp < 1 ? 1 : hp > 999 ? 999 : hp;
    if (hp != unit->baseMaxHp) {
        unit->baseMaxHp = (s16)hp;
        unit->maxHp = (s16)hp;
        unit->currentHp = (s16)hp;
    }
}

/* btlseqFirstAct(wp), before, once a fight: the enemies' starting statuses */
static int firstActPre(BattleWork** wpp) {
    BattleWork* wp = *wpp;
    int i, k;
    if (!sBattleOn || sOpened || wp == NULL) {
        return 0;
    }
    sOpened = 1;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* unit = wp->units[i];
        if (unit == NULL || unit->alliance != 1 || unit->unitId < 0 || unit->unitId >= BATTLE_UNIT_MAX ||
            !sUnitHas[unit->unitId]) {
            continue;
        }
        for (k = 0; k < 2; k++) {
            const PortMqStatus* s = &sUnit[unit->unitId].start[k];
            if (s->type >= 0) {
                BtlUnit_SetStatus(unit, (StatusEffectType)s->type, s->turns, s->strength);
            }
        }
    }
    return 0;
}

/* a hit status into the weapon's fields (the larger chance wins) */
static void hitStatus(BattleWeapon* w, const PortMqStatus* s) {
    u8* chance = NULL;
    s8* time = NULL;
    s8* strength = NULL;
    s8 sign = 1;
    switch (s->type) {
        case STATUS_SLEEP: chance = &w->sleepChance; time = &w->sleepTime; break;
        case STATUS_STOP: chance = &w->stopChance; time = &w->stopTime; break;
        case STATUS_DIZZY: chance = &w->dizzyChance; time = &w->dizzyTime; break;
        case STATUS_POISON: chance = &w->poisonChance; time = &w->poisonTime; strength = &w->poisonStrength; break;
        case STATUS_CONFUSE: chance = &w->confuseChance; time = &w->confuseTime; break;
        case STATUS_BURN: chance = &w->burnChance; time = &w->burnTime; break;
        case STATUS_FREEZE: chance = &w->freezeChance; time = &w->freezeTime; break;
        case STATUS_TINY:
            chance = &w->sizeChangeChance; time = &w->sizeChangeTime; strength = &w->sizeChangeStrength; sign = -1;
            break;
        case STATUS_ATTACK_DOWN:
            chance = &w->atkChangeChance; time = &w->atkChangeTime; strength = &w->atkChangeStrength; sign = -1;
            break;
        case STATUS_DEFENSE_DOWN:
            chance = &w->defChangeChance; time = &w->defChangeTime; strength = &w->defChangeStrength; sign = -1;
            break;
        case STATUS_SLOW: chance = &w->slowChance; time = &w->slowTime; break;
        default: return;
    }
    if (*chance >= (u8)s->chance) {
        return;
    }
    *chance = (u8)s->chance;
    *time = s->turns;
    if (strength != NULL) {
        *strength = (s8)(sign * (s->strength > 0 ? s->strength : 1));
    }
}

#define COPIES 8
static BattleWeapon sCopies[COPIES];
static int sCopyNext;

/* BattleCheckDamage(attacker, target, part, weapon, flags), before: an enemy's
 * attack on Mario's side with its changes, as a copy of the weapon */
static int checkDamagePre(s32* result, BattleUnit** attacker, BattleUnit** target, BattleUnitPart** part,
                          BattleWeapon** weapon, s32* flags) {
    const PortMqEnemy* e;
    BattleWeapon* w;
    int k;
    (void)result;
    (void)part;
    (void)flags;
    if (!sBattleOn || *attacker == NULL || *target == NULL || *weapon == NULL || (*attacker)->alliance != 1 ||
        (*target)->alliance != 0 || (*attacker)->unitId < 0 || (*attacker)->unitId >= BATTLE_UNIT_MAX ||
        !sUnitHas[(*attacker)->unitId]) {
        return 0;
    }
    e = &sUnit[(*attacker)->unitId];
    if (e->atkAdd == 0 && e->element < 0 && !e->pierce && e->hit[0].type < 0 && e->hit[1].type < 0) {
        return 0;
    }
    w = &sCopies[sCopyNext];
    sCopyNext = (sCopyNext + 1) % COPIES;
    *w = **weapon;
    if (w->hpDamageFn != NULL) {
        if (e->atkAdd != 0 && w->hpDamageFn == weaponGetPowerDefault && (s32)w->hpDamageWork[0] > 0) {
            const s32 power = (s32)w->hpDamageWork[0] + e->atkAdd;
            w->hpDamageWork[0] = (u32)(power < 1 ? 1 : power);
        }
        if (e->element >= 0) {
            w->element = (u8)e->element;
        }
        if (e->pierce) {
            w->special |= SPECIAL_PIERCES_DEFENSE;
        }
        for (k = 0; k < 2; k++) {
            if (e->hit[k].type >= 0) {
                hitStatus(w, &e->hit[k]);
            }
        }
    }
    *weapon = w;
    return 0;
}

void portMasterQuestInit(void) {
    static int sDone PORT_HOST_DATA; /* (the hooks: not game state) */
    if (sDone) {
        return;
    }
    sDone = 1;
    portMasterQuestDeclare();
    portModHostAddHook(1, kSlot_npcSetupBattleInfo, (void*)setupPost);
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portModHostAddHook(1, kSlot_BtlUnit_Entry, (void*)entryPost);
    portModHostAddHook(0, kSlot_btlseqFirstAct, (void*)firstActPre);
    portModHostAddHook(0, kSlot_BattleCheckDamage, (void*)checkDamagePre);
}
