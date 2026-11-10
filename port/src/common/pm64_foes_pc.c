/* Paper Mario 64 enemies in the story's fights (port/include/port/pm64_foes.h, docs/PM64_EXTRAS.md).
 *
 * The scaling rule. Each PM64 enemy has a home chapter (where PM64 first puts it). Its HP and attack are
 * PM64's, times the ratio of a typical TTYD enemy's in the chapter it's met in to a typical PM64 enemy's in
 * its home chapter, rounded, at least 1:
 *
 *   HP     = round(PM64 HP     x kTtydHp[chapter]  / kPm64Hp[home])
 *   Attack = round(PM64 attack x kTtydAtk[chapter] / kPm64Atk[home])
 *            (at most round(1.3 x kTtydAtk[chapter]))
 *
 * (the tables below; a Goomba in Petal Meadows stays 2 HP, 1 attack, a Dry Bones in the Palace of Shadow is
 * 10 HP, 4 attack). The attack is what each of its hits does: the cousin's own move damage moved to it
 * (never below 1). Defence, statuses, coins, Star Points and items stay the TTYD cousin's, so its moves,
 * its weak spots and the area's rewards are TTYD's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_foes.h"
#include "port/enemy_variant.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "mario/mariost.h"

/* typical HP and attack of an ordinary enemy, chapter 0 (prologue) to 8 */
static const float kTtydHp[9] = {2, 3, 4, 5, 6, 7, 8, 9, 12};
static const float kTtydAtk[9] = {1.0f, 1.2f, 1.8f, 2.4f, 3.0f, 3.4f, 3.8f, 4.4f, 5.2f};
static const float kPm64Hp[9] = {3, 3, 4, 6, 7, 9, 8, 11, 10};
static const float kPm64Atk[9] = {1, 1, 2, 2, 3, 3, 3, 4, 5};

/* PM64's attack and home chapter, by the PM64 enemy's English name (pm64_enemy_pc.c's table) */
static const struct {
    const char* name;
    int attack, home;
} kPm64[] = {
    {"Goomba", 1, 1},          {"Spiked Goomba", 2, 1},    {"Paragoomba", 1, 1},      {"Koopa Troopa", 1, 1},
    {"Paratroopa", 1, 1},      {"Fuzzy", 1, 1},            {"Bob-omb", 1, 1},         {"Bullet Bill", 2, 1},
    {"Bill Blaster", 0, 1},    {"Monty Mole", 2, 2},       {"Cleft", 2, 2},           {"Pokey", 2, 2},
    {"Bandit", 2, 2},          {"Pokey Mummy", 2, 2},      {"Buzzy Beetle", 2, 2},    {"Swooper", 3, 2},
    {"Stone Chomp", 3, 2},     {"Forest Fuzzy", 1, 3},     {"Hyper Goomba", 1, 3},    {"Hyper Paragoomba", 1, 3},
    {"Hyper Cleft", 3, 3},     {"Shy Guy", 2, 4},          {"Jungle Fuzzy", 2, 5},    {"Putrid Piranha", 3, 5},
    {"Lava Bubble", 4, 5},     {"Spike Top", 3, 5},        {"Lakitu", 2, 6},          {"Spiny", 3, 6},
    {"Crazee Dayzee", 2, 6},   {"Amazy Dayzee", 4, 6},     {"Ruff Puff", 4, 6},       {"Frost Piranha", 4, 7},
    {"Ember", 4, 8},           {"Dry Bones", 4, 8},        {"Bony Beetle", 3, 8},     {"Magikoopa", 3, 8},
    {"Koopatrol", 4, 8},       {"Hammer Bro", 4, 8},       {"Gloomba", 2, 5},         {"Paragloomba", 2, 5},
    {"Spiked Gloomba", 3, 5},  {"Dark Koopa", 3, 5},       {"Dark Paratroopa", 3, 5},
};

/* the TTYD cousin's usual hit, so the PM64 enemy's attack can stand in for it */
static int hostAttack(int kind) {
    switch (kind) {
    case UNIT_GOOMBA: case UNIT_PARAGOOMBA: case UNIT_FUZZY: case UNIT_HYPER_GOOMBA: case UNIT_HYPER_PARAGOOMBA:
    case UNIT_GOOMBA_GLITZVILLE: case UNIT_LAKITU: case UNIT_BILL_BLASTER:
        return 1;
    case UNIT_SPIKY_GOOMBA: case UNIT_KOOPA_TROOPA: case UNIT_PARATROOPA: case UNIT_BOB_OMB: case UNIT_CLEFT:
    case UNIT_BANDIT: case UNIT_CRAZEE_DAYZEE: case UNIT_SPIKE_TOP: case UNIT_BUZZY_BEETLE:
        return 2;
    case UNIT_POKEY: case UNIT_POISON_POKEY: case UNIT_SWOOPER: case UNIT_GREEN_FUZZY: case UNIT_FLOWER_FUZZY:
    case UNIT_PUTRID_PIRANHA: case UNIT_SPINY: case UNIT_GLOOMBA: case UNIT_PARAGLOOMBA: case UNIT_HYPER_CLEFT:
    case UNIT_BIG_BANDIT:
        return 3;
    case UNIT_BULLET_BILL: case UNIT_LAVA_BUBBLE: case UNIT_RUFF_PUFF: case UNIT_MAGIKOOPA: case UNIT_KOOPATROL:
    case UNIT_HAMMER_BRO: case UNIT_SPIKY_GLOOMBA: case UNIT_DARK_KOOPA: case UNIT_DARK_PARATROOPA:
        return 4;
    case UNIT_FROST_PIRANHA: case UNIT_EMBER: case UNIT_DRY_BONES: case UNIT_RED_SPIKY_BUZZY: case UNIT_RED_CHOMP:
        return 5;
    case UNIT_AMAZY_DAYZEE:
        return 20;
    default:
        return 2;
    }
}

static int pm64Index(int i, int* attack, int* home) {
    const Pm64EnemyInfo* e = portPm64EnemyInfo(i);
    size_t k;
    *attack = 1;
    *home = 1;
    if (e == NULL) {
        return 0;
    }
    for (k = 0; k < sizeof(kPm64) / sizeof(kPm64[0]); k++) {
        if (strcmp(kPm64[k].name, e->english) == 0) {
            *attack = kPm64[k].attack;
            *home = kPm64[k].home;
            return 1;
        }
    }
    return 1;
}

static int scaled(float v, float to, float from) {
    const int r = (int)(v * to / from + 0.5f);
    return r < 1 ? 1 : r;
}

/* an attack scaled to the chapter, at most 1.3 times the chapter's usual (a foe strong for its own chapter, as
 * Clubba's 3 in chapter 3, would otherwise come out far above the enemies beside it: 7 in chapter 7) */
static int attackScaled(float v, int chapter, int home) {
    const int r = scaled(v, kTtydAtk[chapter], kPm64Atk[home]);
    const int cap = (int)(kTtydAtk[chapter] * 1.3f + 0.5f);
    return r > cap ? cap : r;
}

int portPm64FoeHp(int i, int chapter) {
    const Pm64EnemyInfo* e = portPm64EnemyInfo(i);
    int atk, home;
    if (e == NULL || !pm64Index(i, &atk, &home)) {
        return 1;
    }
    chapter = chapter < 0 ? 0 : chapter > 8 ? 8 : chapter;
    return scaled((float)e->hp, kTtydHp[chapter], kPm64Hp[home]);
}

int portPm64FoeScale(int value, int home, int chapter, int attack) {
    chapter = chapter < 0 ? 0 : chapter > 8 ? 8 : chapter;
    home = home < 0 ? 0 : home > 8 ? 8 : home;
    if (value <= 0) {
        return 0;
    }
    return attack ? attackScaled((float)value, chapter, home)
                  : scaled((float)value, kTtydHp[chapter], kPm64Hp[home]);
}

int portPm64FoeAttack(int i, int chapter) {
    int atk, home;
    if (!pm64Index(i, &atk, &home)) {
        return 1;
    }
    chapter = chapter < 0 ? 0 : chapter > 8 ? 8 : chapter;
    return atk <= 0 ? 0 : attackScaled((float)atk, chapter, home);
}

/* --- the setting --- */

static int* sMode;

int portPm64FoesMode(void) {
    if (sMode == NULL) {
        sMode = portSettingsDeclare("pm64_story_enemies", 0, 0, 2);
    }
    return sMode != NULL ? *sMode : 0;
}

int portPm64FoesActive(void) {
    if (portPm64FoesMode() == 0 || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    /* online: only when every player has a ROM linked (the partners' session token says so) */
    return !portStateSyncLinked() || portPm64OnlineToken()[0] != '\0';
}

int portPm64FoesChapter(const char* map) {
    static const struct {
        const char area[4];
        int chapter;
    } kAreas[] = {
        {"tik", 0}, {"hei", 1}, {"nok", 1}, {"gon", 1}, {"win", 2}, {"mri", 2}, {"tou", 3}, {"usu", 4},
        {"gra", 4}, {"jin", 4}, {"muj", 5}, {"dou", 5}, {"hom", 6}, {"eki", 6}, {"rsh", 6}, {"pik", 6},
        {"bom", 7}, {"moo", 7}, {"aji", 7}, {"las", 8},
    };
    size_t k;
    if (map == NULL || strlen(map) < 4 || map[3] != '_' || map[4] == 'm') {
        return -1; /* (the play modes' own maps: "<area>_m0" ...) */
    }
    for (k = 0; k < sizeof(kAreas) / sizeof(kAreas[0]); k++) {
        if (strncmp(map, kAreas[k].area, 3) == 0) {
            return kAreas[k].chapter;
        }
    }
    return -1;
}

/* --- which fights --- */

#define SCRIPTED                                                                                                   \
    (NPC_BATTLE_NO_ESCAPE | NPC_BATTLE_LOSS_OK | NPC_BATTLE_NO_PLAYER_ENTRY | NPC_BATTLE_NO_JUMP |                \
     NPC_BATTLE_NO_HAMMER | NPC_BATTLE_NO_PARTY_CHANGE)

static unsigned hashStr(unsigned h, const char* s, int n) {
    while (s != NULL && n-- > 0 && *s != '\0') {
        h = (h ^ (unsigned char)*s++) * 16777619u;
    }
    return h;
}

/* the PM64 cousin of a TTYD kind in the story, -1 none: the same character only (the Pit's and Master Quest's
 * stand-ins that fight as another kind, portPm64EnemyStandIn, never take a story enemy's place) */
static int cousinOf(int kind) {
    int i;
    for (i = 0; i < portPm64EnemyCount(); i++) {
        if (portPm64EnemyInfo(i)->host == kind && !portPm64EnemyStandIn(i)) {
            return i;
        }
    }
    return -1;
}

/* the NPC's fight is ordinary (not scripted, every enemy a plain one) and picked.
 * Only a field enemy (NPC_FLAG_ENEMY, which the enemies' own init scripts set): every townsfolk NPC set up with
 * battleInfoId 0 (evt_npc_setup) holds its area's btlSetupTbl[0] fight too, one it never starts, so a Toad and
 * the Whacka on Keelhaul Key wandered as that fight's Ember. */
static int pickedFight(NpcEntry* npc) {
    const NpcBattleInfo* info;
    const BattleGroupSetup* g;
    int i, any = 0;
    if (npc == NULL || gp == NULL || !portPm64FoesActive() || portPm64FoesChapter(gp->currentMapName) < 0) {
        return 0;
    }
    info = &npc->battleInfo;
    g = info->groupSetup;
    if (g == NULL || g->numEnemies <= 0 || g->enemyData == NULL || (info->battleFlags & SCRIPTED) != 0) {
        return 0;
    }
    for (i = 0; i < g->numEnemies; i++) {
        const BattleUnitSetup* u = &g->enemyData[i];
        if (u->unitKindParams == NULL || u->alliance != 1) {
            return 0;
        }
        any |= cousinOf((int)u->unitKindParams->unitType) >= 0;
    }
    if (!any) {
        return 0;
    }
    if (portPm64FoesMode() >= 2) {
        return 1;
    }
    /* mixed: two fights in five, the same ones every time */
    return hashStr(hashStr(2166136261u, gp->currentMapName, 16), npc->description, 32) % 5u < 2u;
}

static int picked(NpcEntry* npc) {
    return npc != NULL && (npc->flags & NPC_FLAG_ENEMY) != 0 && pickedFight(npc);
}

int portPm64FoeOfNpc(NpcEntry* npc) {
    const BattleGroupSetup* g;
    if (!picked(npc)) {
        return -1;
    }
    g = npc->battleInfo.groupSetup;
    return cousinOf((int)g->enemyData[0].unitKindParams->unitType);
}

/* the field: an NPC's pose drawn as its cousin (the NPCs looked up once a frame) */
#define FIELD_MAX 64
static unsigned sFieldFrame = ~0u;
static int sFieldCount;
static struct {
    int poseId, enemy;
    NpcEntry* npc;
} sField[FIELD_MAX];

/* (tests, TTYD_PM64_LOG) each NPC of the room once: the PM64 enemy it wanders as, or a townsfolk NPC whose
   unused fight has a cousin, kept as itself */
static int fieldLogOn(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* log = getenv("TTYD_PM64_LOG");
        sOn = log != NULL && log[0] != '\0' && log[0] != '0';
    }
    return sOn;
}

static void fieldLog(NpcEntry* npc, int slot, int enemy) {
    static char sMap[16];
    static unsigned char sSaid[FIELD_MAX * 4];
    const char* model = npc->tribe != NULL && npc->tribe->modelName != NULL ? npc->tribe->modelName : "?";
    if (!fieldLogOn() || slot < 0 || slot >= (int)sizeof(sSaid)) {
        return;
    }
    if (strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        snprintf(sMap, sizeof(sMap), "%s", gp->currentMapName);
        memset(sSaid, 0, sizeof(sSaid));
    }
    if (sSaid[slot]) {
        return;
    }
    sSaid[slot] = 1;
    if (enemy >= 0) {
        printf("[pm64] field: %s %s as %s\n", sMap, model, portPm64EnemyInfo(enemy)->english);
    } else {
        printf("[pm64] field: %s %s kept (not a field enemy)\n", sMap, model);
    }
    fflush(stdout);
}

int portPm64FoeFieldPose(int poseId, int* enemy, const char** animName) {
    int i;
    if (gp == NULL || gp->inBattle || poseId < 0) {
        return 0;
    }
    if (sFieldFrame != gp->retraceCount) {
        NpcWork* w = npcGetWorkPtr();
        sFieldFrame = gp->retraceCount;
        sFieldCount = 0;
        for (i = 0; w != NULL && w->entries != NULL && i < w->total && sFieldCount < FIELD_MAX; i++) {
            NpcEntry* npc = &w->entries[i];
            int e;
            if ((npc->flags & NPC_FLAG_USED) == 0 || npc->poseId < 0) {
                continue;
            }
            if ((e = portPm64FoeOfNpc(npc)) < 0) {
                if ((npc->flags & NPC_FLAG_ENEMY) == 0 && fieldLogOn() && pickedFight(npc)) {
                    fieldLog(npc, i, -1);
                }
                continue;
            }
            fieldLog(npc, i, e);
            sField[sFieldCount].poseId = npc->poseId;
            sField[sFieldCount].enemy = e;
            sField[sFieldCount].npc = npc;
            sFieldCount++;
        }
    }
    for (i = 0; i < sFieldCount; i++) {
        if (sField[i].poseId == poseId) {
            *enemy = sField[i].enemy;
            *animName = sField[i].npc->currentAnim;
            return 1;
        }
    }
    return 0;
}

/* the ROM's Tattle with its "Max HP: N" and "Attack Power: N" as they are here */
void portPm64TattleNumbers(char* t, size_t size, int hp, int attack) {
    static const char* const kKeys[2] = {"Max HP: ", "Attack Power: "};
    int k;
    for (k = 0; k < 2; k++) {
        char* at = strstr(t, kKeys[k]);
        char tail[PORT_VARIANT_TATTLE_MAX];
        char* end;
        if (at == NULL) {
            continue;
        }
        at += strlen(kKeys[k]);
        strtol(at, &end, 10);
        if (end == at) {
            continue;
        }
        snprintf(tail, sizeof(tail), "%s", end);
        snprintf(at, size - (size_t)(at - t), "%d%s", k == 0 ? hp : attack, tail);
    }
}

int portPm64FoesArm(BattleWork* wp) {
    NpcEntry* npc = NULL;
    const NpcBattleInfo* info = wp != NULL && wp->fbatInfo != NULL ? wp->fbatInfo->setup : NULL;
    const BattleGroupSetup* g = info != NULL ? info->groupSetup : NULL;
    PortEnemyVariant list[PORT_VARIANT_ARMED_MAX];
    const PortEnemyVariant* armed;
    int n, i, k, chapter, added = 0;
    {
        /* the field NPC whose battle this is (the field battle data's npc isn't set yet here): the one whose
           battle info it is; in the field's list: gp->inBattle is already set, so npcGetWorkPtr is the battle's) */
        NpcWork* w = portNpcFieldWork();
        for (i = 0; info != NULL && w != NULL && w->entries != NULL && i < w->total; i++) {
            if ((w->entries[i].flags & NPC_FLAG_USED) != 0 && &w->entries[i].battleInfo == info) {
                npc = &w->entries[i];
                break;
            }
        }
    }
    if (g == NULL || npc == NULL || !picked(npc)) {
        const char* log = getenv("TTYD_PM64_LOG"); /* (tests: why a story fight wasn't armed) */
        if (log != NULL && log[0] != '\0' && log[0] != '0') {
            printf("[pm64] story: not armed (group %d, npc %d, picked %d)\n", g != NULL, npc != NULL,
                   npc != NULL && picked(npc));
            fflush(stdout);
        }
        return 0;
    }
    chapter = portPm64FoesChapter(gp->currentMapName);
    /* alongside what another mode armed (Master Quest's variant= lines win for their kinds) */
    n = portEnemyVariantArmed(&armed);
    n = n > PORT_VARIANT_ARMED_MAX ? PORT_VARIANT_ARMED_MAX : n;
    memcpy(list, armed, sizeof(PortEnemyVariant) * (size_t)n);
    for (i = 0; i < g->numEnemies && n < PORT_VARIANT_ARMED_MAX; i++) {
        const int kind = (int)g->enemyData[i].unitKindParams->unitType;
        const int e = cousinOf(kind);
        int dup = 0, hp, atk;
        for (k = 0; k < n; k++) {
            dup |= list[k].baseKind == kind;
        }
        if (dup || e < 0 || !portPm64EnemyVariant(e, &list[n])) {
            continue;
        }
        hp = portPm64FoeHp(e, chapter);
        atk = portPm64FoeAttack(e, chapter);
        list[n].hpPercent = 100;
        list[n].hpAdd = hp - portPm64EnemyInfo(e)->hp;
        list[n].attack = atk - hostAttack(kind);
        if (hostAttack(kind) + list[n].attack < 1) {
            list[n].attack = 1 - hostAttack(kind);
        }
        portPm64TattleNumbers(list[n].tattle, sizeof(list[n].tattle), hp, atk);
        {
            const char* log = getenv("TTYD_PM64_LOG"); /* (tests) */
            if (log != NULL && log[0] != '\0' && log[0] != '0') {
                printf("[pm64] story: %s %s for kind %d, HP %d, attack %d\n", gp->currentMapName,
                       portPm64EnemyInfo(e)->english, kind, hp, atk);
            }
        }
        n++;
        added++;
    }
    if (added > 0) {
        portEnemyVariantArm(list, n);
        fflush(stdout);
    }
    return added > 0;
}
