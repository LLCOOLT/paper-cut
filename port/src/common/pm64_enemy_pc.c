/* Paper Mario 64 enemies (port/include/port/pm64_enemy.h): the table, their
 * texts from the linked ROM, the variants that make them, and their battle
 * draw as PM64 sprites. Sprite indices and animation numbers as the PM64
 * decomp names the sprite archive; HP as PM64 has them, attack and defence
 * our own fitting numbers on top of each host's. */
#include "port/file_pc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include "port/file_write.h"
#include "port/anim_pc.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/pm64_foes.h"
#include "port/pm64_media.h"
#include "port/pm64_spin.h"
#include "port/costume.h"
#include "port/enemy_variant.h"
#include "port/statesync.h"
#include "port/paper.h"
#include "port/settings.h"
#include "port/toggles.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/animdrv.h"
#include "driver/camdrv.h"
#include "driver/mapdrv.h"
#include "driver/npcdrv.h"
#include "driver/seqdrv.h"
#include "mario/mariost.h"

/* english, name and Tattle messages, sprite, palette, idle, walk, hurt, host, hp, attack, defense */
static const Pm64EnemyInfo kEnemies[] = {
    {"Goomba", 0x00, 0x00, 37, 0, 1, 2, 5, UNIT_GOOMBA, 2, 0, 0},
    {"Spiked Goomba", 0x01, 0x01, 38, 0, 1, 2, 6, UNIT_SPIKY_GOOMBA, 2, 0, 0},
    {"Paragoomba", 0x02, 0x02, 39, 0, 1, 3, 6, UNIT_PARAGOOMBA, 2, 0, 0},
    {"Koopa Troopa", 0x07, 0x07, 40, 1, 1, 3, 11, UNIT_KOOPA_TROOPA, 4, 0, 0},
    {"Paratroopa", 0x08, 0x08, 41, 1, 1, 2, 7, UNIT_PARATROOPA, 4, 0, 0},
    {"Fuzzy", 0x09, 0x09, 42, 0, 1, 2, 8, UNIT_FUZZY, 3, 0, 0},
    {"Bob-omb", 0x0A, 0x0A, 43, 0, 2, 4, 14, UNIT_BOB_OMB, 3, 0, 0},
    {"Bullet Bill", 0x0B, 0x0B, 44, 0, 1, 1, 5, UNIT_BULLET_BILL, 2, 0, 0},
    {"Bill Blaster", 0x0C, 0x0C, 45, 0, 1, 1, 3, UNIT_BILL_BLASTER, 5, 0, 0},
    {"Monty Mole", 0x14, 0x14, 46, 0, 1, 2, 5, UNIT_BIG_BANDIT, 3, 0, 0},
    {"Cleft", 0x15, 0x15, 47, 0, 2, 6, 8, UNIT_CLEFT, 2, 0, 0},
    {"Pokey", 0x16, 0x16, 48, 0, 4, 8, 12, UNIT_POKEY, 4, 0, 0},
    {"Bandit", 0x17, 0x17, 49, 0, 1, 5, 9, UNIT_BANDIT, 5, 0, 0},
    {"Pokey Mummy", 0x18, 0x18, 48, 1, 4, 8, 12, UNIT_POISON_POKEY, 4, 0, 0},
    {"Buzzy Beetle", 0x19, 0x19, 50, 0, 1, 3, 7, UNIT_BUZZY_BEETLE, 3, 0, 0},
    {"Swooper", 0x1A, 0x1A, 51, 0, 2, 6, 21, UNIT_SWOOPER, 4, 0, 0},
    {"Stone Chomp", 0x1E, 0x1E, 52, 0, 1, 1, 6, UNIT_RED_CHOMP, 4, 0, 0},
    {"Forest Fuzzy", 0x20, 0x20, 42, 1, 1, 2, 8, UNIT_GREEN_FUZZY, 6, 0, 0},
    {"Hyper Goomba", 0x21, 0x21, 37, 2, 1, 2, 5, UNIT_HYPER_GOOMBA, 7, 0, 0},
    {"Hyper Paragoomba", 0x22, 0x22, 39, 2, 1, 3, 6, UNIT_HYPER_PARAGOOMBA, 7, 0, 0},
    {"Hyper Cleft", 0x23, 0x23, 47, 1, 2, 6, 8, UNIT_HYPER_CLEFT, 4, 0, 0},
    {"Shy Guy", 0x27, 0x29, 58, 0, 1, 2, 12, UNIT_GOOMBA_GLITZVILLE, 7, 1, 0},
    {"Jungle Fuzzy", 0x35, 0x37, 42, 3, 1, 2, 8, UNIT_FLOWER_FUZZY, 7, 0, 0},
    {"Putrid Piranha", 0x37, 0x39, 53, 0, 1, 2, 14, UNIT_PUTRID_PIRANHA, 12, 0, 0},
    {"Lava Bubble", 0x38, 0x3A, 69, 0, 1, 2, 7, UNIT_LAVA_BUBBLE, 9, 0, 0},
    {"Spike Top", 0x39, 0x3B, 76, 0, 3, 6, 19, UNIT_SPIKE_TOP, 4, 0, 0},
    {"Lakitu", 0x3D, 0x42, 72, 0, 1, 2, 7, UNIT_LAKITU, 12, 0, 0},
    {"Spiny", 0x3E, 0x43, 73, 0, 1, 3, 9, UNIT_SPINY, 5, 0, 0},
    {"Crazee Dayzee", 0x40, 0x45, 71, 0, 1, 2, 8, UNIT_CRAZEE_DAYZEE, 8, 0, 0},
    {"Amazy Dayzee", 0x41, 0x46, 71, 1, 1, 2, 8, UNIT_AMAZY_DAYZEE, 20, 0, 0},
    {"Ruff Puff", 0x44, 0x49, 124, 0, 1, 3, 6, UNIT_RUFF_PUFF, 10, 0, 0},
    {"Frost Piranha", 0x4A, 0x50, 53, 1, 1, 2, 14, UNIT_FROST_PIRANHA, 10, 0, 0},
    {"Ember", 0x59, 0x60, 69, 1, 1, 2, 7, UNIT_EMBER, 8, 0, 0},
    {"Dry Bones", 0x5A, 0x61, 82, 0, 2, 3, 7, UNIT_DRY_BONES, 8, 0, 0},
    {"Bony Beetle", 0x5B, 0x62, 84, 0, 4, 12, 24, UNIT_RED_SPIKY_BUZZY, 8, 0, 0},
    {"Magikoopa", 0x5C, 0x63, 85, 0, 1, 1, 4, UNIT_MAGIKOOPA, 11, 0, 0},
    {"Koopatrol", 0x5D, 0x67, 88, 0, 1, 3, 11, UNIT_KOOPATROL, 8, 0, 0},
    {"Hammer Bro", 0x5E, 0x68, 89, 0, 2, 4, 14, UNIT_HAMMER_BRO, 10, 0, 0},
    {"Gloomba", 0x6B, 0x79, 37, 4, 1, 2, 5, UNIT_GLOOMBA, 7, 0, 0},
    {"Paragloomba", 0x6C, 0x7A, 39, 3, 1, 3, 6, UNIT_PARAGLOOMBA, 7, 0, 0},
    {"Spiked Gloomba", 0x6D, 0x7B, 38, 1, 1, 2, 6, UNIT_SPIKY_GLOOMBA, 7, 0, 0},
    {"Dark Koopa", 0x6E, 0x7C, 40, 0, 1, 3, 11, UNIT_DARK_KOOPA, 8, 0, 0},
    {"Dark Paratroopa", 0x6F, 0x7D, 41, 0, 1, 2, 7, UNIT_DARK_PARATROOPA, 8, 0, 0},
};
#define ENEMIES (int)(sizeof(kEnemies) / sizeof(kEnemies[0]))

int portPm64EnemyCount(void) {
    return ENEMIES;
}

const Pm64EnemyInfo* portPm64EnemyInfo(int i) {
    return i >= 0 && i < ENEMIES ? &kEnemies[i] : NULL;
}

int portPm64EnemyStandIn(int i) {
    return i >= 0 && i < ENEMIES && (kEnemies[i].host == UNIT_BIG_BANDIT || kEnemies[i].host == UNIT_GOOMBA_GLITZVILLE);
}

const char* portPm64EnemyName(int i, char* out, int size) {
    if (size <= 0) {
        return out;
    }
    out[0] = '\0';
    if (i < 0 || i >= ENEMIES) {
        return out;
    }
    if (portPm64MessageText(PM64_MSG(0x29, kEnemies[i].nameMsg), 0, out, (size_t)size) <= 0) {
        snprintf(out, (size_t)size, "%s", kEnemies[i].english);
    }
    return out;
}

const char* portPm64EnemyTattle(int i, char* out, int size) {
    if (size <= 0) {
        return out;
    }
    out[0] = '\0';
    if (i >= 0 && i < ENEMIES) {
        portPm64MessageText(PM64_MSG(0x1C, kEnemies[i].tattleMsg), 0, out, (size_t)size);
    }
    return out;
}

int portPm64EnemyVariant(int i, PortEnemyVariant* out) {
    const Pm64EnemyInfo* e = portPm64EnemyInfo(i);
    if (e == NULL || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    portEnemyVariantInit(out, e->host);
    out->pm64 = i + 1;
    out->attack = e->attack;
    out->defense = e->defense;
    portPm64EnemyName(i, out->name, (int)sizeof(out->name));
    portPm64EnemyTattle(i, out->tattle, (int)sizeof(out->tattle));
    return 1;
}

/* --- seen on this PC --- */

static unsigned char sSeen[ENEMIES];
static int sSeenRead;

static void seenPath(char* out, size_t size) {
    snprintf(out, size, "%s/pm64_tattles.txt", portSettingsUserPath());
}

static void seenRead(void) {
    char path[1024], line[64];
    FILE* f;
    if (sSeenRead) {
        return;
    }
    sSeenRead = 1;
    seenPath(path, sizeof(path));
    if ((f = portFopen(path, "r")) == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        int i;
        for (i = 0; i < ENEMIES; i++) {
            const size_t n = strlen(kEnemies[i].english);
            if (strncmp(line, kEnemies[i].english, n) == 0 && (line[n] == '\n' || line[n] == '\r' || line[n] == 0)) {
                sSeen[i] = 1;
            }
        }
    }
    fclose(f);
}

static void seenMark(int i) {
    char path[1024];
    FILE* f;
    int k;
    seenRead();
    if (i < 0 || i >= ENEMIES || sSeen[i]) {
        return;
    }
    sSeen[i] = 1;
    seenPath(path, sizeof(path));
    if ((f = portFileWriteOpen(path, "w")) == NULL) {
        return;
    }
    fprintf(f, "# Paper Mario 64 enemies met in battle (the Tattle Log's Paper Mario 64 page)\n");
    for (k = 0; k < ENEMIES; k++) {
        if (sSeen[k]) {
            fprintf(f, "%s\n", kEnemies[k].english);
        }
    }
    portFileWriteClose(f, path);
}

int portPm64EnemySeen(int i) {
    seenRead();
    return i >= 0 && i < ENEMIES && sSeen[i];
}

/* --- the PM64 bosses tattled (each its own Tattle Log entry: pm64_bosses_aurora.c), by key, in
   <user>/pm64_boss_tattles.txt (a line each) --- */
#define BOSS_KEYS 96
static char sBossKey[BOSS_KEYS][48];
static int sBossKeys = -1;

static void bossKeysPath(char* out, size_t size) {
    snprintf(out, size, "%s/pm64_boss_tattles.txt", portSettingsUserPath());
}

static void bossKeysRead(void) {
    char path[1024], line[64];
    FILE* f;
    if (sBossKeys >= 0) {
        return;
    }
    sBossKeys = 0;
    bossKeysPath(path, sizeof(path));
    if ((f = portFopen(path, "r")) == NULL) {
        return;
    }
    while (sBossKeys < BOSS_KEYS && fgets(line, sizeof(line), f) != NULL) {
        size_t n = strcspn(line, "\r\n");
        line[n] = '\0';
        if (n > 0 && line[0] != '#') {
            snprintf(sBossKey[sBossKeys++], sizeof(sBossKey[0]), "%s", line);
        }
    }
    fclose(f);
}

int portPm64BossTattledKey(const char* key) {
    int i;
    bossKeysRead();
    for (i = 0; key != NULL && i < sBossKeys; i++) {
        if (strcmp(sBossKey[i], key) == 0) {
            return 1;
        }
    }
    return 0;
}

int portPm64BossTattleMarkKey(const char* key) {
    char path[1024];
    FILE* f;
    int i;
    if (key == NULL || key[0] == '\0' || portPm64BossTattledKey(key) || sBossKeys >= BOSS_KEYS) {
        return 0;
    }
    snprintf(sBossKey[sBossKeys++], sizeof(sBossKey[0]), "%s", key);
    bossKeysPath(path, sizeof(path));
    if ((f = portFileWriteOpen(path, "w")) == NULL) {
        return 1;
    }
    fprintf(f, "# Paper Mario 64 bosses tattled (each one's own Tattle Log entry)\n");
    for (i = 0; i < sBossKeys; i++) {
        fprintf(f, "%s\n", sBossKey[i]);
    }
    portFileWriteClose(f, path);
    return 1;
}

int portPm64EnemyHp(int pm64) {
    if (pm64 < 1 || pm64 > ENEMIES || portPm64Linked() == PM64_NONE) {
        return -1;
    }
    seenMark(pm64 - 1);
    return kEnemies[pm64 - 1].hp;
}

/* --- in the Pit and Master Quest --- */

int portPm64EnemiesActive(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_enemies", 0, 0, 1);
    }
    return *sOn != 0 && portPm64Linked() != PM64_NONE && !portStateSyncLinked();
}

void portPm64EnemiesArm(const int* kinds, int count, unsigned seed) {
    PortEnemyVariant list[PORT_VARIANT_ARMED_MAX];
    int n = 0, i, k;
    for (i = 0; i < count && n < PORT_VARIANT_ARMED_MAX; i++) {
        int dup = 0, pick = -1, options = 0;
        for (k = 0; k < n; k++) {
            dup |= list[k].baseKind == kinds[i];
        }
        if (dup) {
            continue;
        }
        for (k = 0; k < ENEMIES; k++) {
            options += kEnemies[k].host == kinds[i];
        }
        seed = seed * 1103515245u + 12345u;
        /* each kind with a counterpart: one battle in two */
        if (options == 0 || ((seed >> 16) & 1) == 0) {
            continue;
        }
        seed = seed * 1103515245u + 12345u;
        options = (int)((seed >> 16) % (unsigned)options);
        for (k = 0; k < ENEMIES; k++) {
            if (kEnemies[k].host == kinds[i] && options-- == 0) {
                pick = k;
                break;
            }
        }
        if (pick >= 0 && portPm64EnemyVariant(pick, &list[n])) {
            const char* log = getenv("TTYD_PM64_LOG"); /* (tests) */
            if (log != NULL && log[0] != '\0' && log[0] != '0') {
                printf("[pm64] enemies: %s for kind %d\n", kEnemies[pick].english, kinds[i]);
                fflush(stdout);
            }
            n++;
        }
    }
    if (n > 0) {
        portEnemyVariantArm(list, n);
    }
}

/* --- the draw --- */

/* (one play per pose and row: a sprite drawn in several rows at once, Huff N. Puff's body, face and arms or a Lava
   Piranha's vine and head, plays each row on; one met again after the pose played another starts over) */
#define PLAYS 32
static struct {
    int pose1;
    int sprite, anim;
    unsigned gen, frame;
    Pm64Anim a;
} sPlay[PLAYS];

static Pm64Anim* playOf(int poseId, int sprite, int anim) {
    int i, j, free = -1, oldest = 0;
    for (i = 0; i < PLAYS; i++) {
        if (sPlay[i].pose1 == poseId + 1 && sPlay[i].sprite == sprite && sPlay[i].anim == anim) {
            break;
        }
        if (sPlay[i].pose1 == 0 && free < 0) {
            free = i;
        }
        if (sPlay[i].frame < sPlay[oldest].frame) {
            oldest = i;
        }
    }
    if (i == PLAYS) {
        i = free >= 0 ? free : oldest;
        sPlay[i].pose1 = poseId + 1;
        sPlay[i].sprite = -1;
    } else {
        for (j = 0; j < PLAYS; j++) {
            /* (rows drawn together this frame, layers, don't count) */
            if (j != i && sPlay[j].pose1 == poseId + 1 && (int)(sPlay[j].frame - sPlay[i].frame) > 0 &&
                sPlay[j].frame != gp->retraceCount) {
                sPlay[i].sprite = -1; /* (the pose played another row since: this one starts over) */
                break;
            }
        }
    }
    if (sPlay[i].sprite != sprite || sPlay[i].anim != anim || sPlay[i].gen != portPm64Generation()) {
        sPlay[i].sprite = sprite;
        sPlay[i].anim = anim;
        sPlay[i].gen = portPm64Generation();
        sPlay[i].frame = gp->retraceCount;
        /* (a negative sprite: the player's sprite -1 - sprite, port/include/port/pm64_media.h) */
        portPm64AnimStart(&sPlay[i].a, sprite >= 0 ? portPm64NpcSprite(sprite) : portPm64PlayerSprite(-1 - sprite),
                          anim);
    } else if (sPlay[i].frame != gp->retraceCount) {
        const unsigned d = gp->retraceCount - sPlay[i].frame;
        portPm64AnimStep(&sPlay[i].a, (float)(d > 4 ? 4 : d));
        sPlay[i].frame = gp->retraceCount;
    }
    return &sPlay[i].a;
}

/* the PM64 enemy a battle pose belongs to (its index), and whether it's the
 * unit's first drawn part (the others draw nothing); -1 none */
static int enemyOfPose(int poseId, int* main, const BattleUnit** unitOut) {
    BattleWork* wp = _battleWorkPointer;
    int i;
    if (wp == NULL || gp == NULL || !gp->inBattle) {
        return -1;
    }
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        const BattleUnit* unit = BattleGetUnitPtr(wp, i);
        const PortEnemyVariant* v;
        const BattleUnitPart* part;
        int first = 1;
        if (unit == NULL || unit->alliance != 1 || (v = portEnemyVariantOf(unit)) == NULL || v->pm64 <= 0 ||
            v->pm64 > ENEMIES) {
            continue;
        }
        for (part = unit->parts; part != NULL; part = part->nextPart) {
            if (part->poseId < 0) {
                continue;
            }
            if (part->poseId == poseId) {
                *main = first;
                *unitOut = unit;
                return v->pm64 - 1;
            }
            first = 0;
        }
    }
    return -1;
}

/* Every battle state of a PM64 enemy and the animation it plays in each, by the NPC sprite archive index; idle,
 * walk and hurt are kEnemies' own. Animation numbers as the PM64 decomp's sprite list names them
 * (tools/splat_ext/npc_sprite_names.yaml), each state's pick as PM64's own actors play it
 * (src/battle/common/actor/<name>.inc.c: the status anims tables, STATUS_KEY_STOP and PARALYZE on Still, SLEEP,
 * DIZZY, the BurnHurt and BurnStill pair, the FlippedAnims or ToppledAnims tables). A sprite with no frames lying
 * on its back has UP in the flipped states: deliberately its upright row (one whose host never flips, or flips
 * only once grounded as another enemy: kGrounded). tools/port/pm64_enemy_anims.py check fails on a state with no
 * row (CI), and on a host that flips with only UP there. */
enum {
    ST_IDLE, /* (kEnemies) */
    ST_WALK, /* (kEnemies) */
    ST_HURT, /* (kEnemies) */
    ST_RUN,
    ST_JUMP,
    ST_ATTACK,
    ST_TALK,
    ST_SLEEP,
    ST_STILL, /* Stop, frozen */
    ST_DIZZY, /* dizzy, confused */
    ST_BURNT,
    ST_BURN_HURT, /* a fire or explosion hit, or any hit while burning */
    ST_DEAD,
    ST_FLIP, /* on its back */
    ST_FLIP_HURT,
    ST_FLIP_SLEEP,
    ST_FLIP_STILL,
    ST_FLIP_DIZZY,
    ST_FLIP_STRUGGLE, /* on its back, trying to get up (the turns it lies there) */
    ST_COUNT
};
#define ROW_FIRST ST_RUN
#define ROWS (ST_COUNT - ROW_FIRST)
#define UP (-2) /* (the upright state's row: no frames on its back) */
static const struct {
    short sprite;
    /* run, jump, attack, talk, sleep, still, dizzy, burnt, burn hurt, dead,
       flipped: idle, hurt, sleep, still, dizzy, struggle */
    signed char row[ROWS];
} kRows[] = {
    {37, {3, 4, 4, 1, 8, 0, 7, 10, 9, 6, UP, UP, UP, UP, UP, UP}},             /* Goomba: Midair, Dead */
    {38, {3, 4, 4, 1, 10, 0, 9, 8, 7, 6, UP, UP, UP, UP, UP, UP}},             /* Spiked Goomba: BurnEyesClosed/Open */
    {39, {4, 1, 5, 1, 11, 0, 12, 14, 13, 6, UP, UP, UP, UP, UP, UP}},          /* Paragoomba: Dive */
    {40, {4, 1, 6, 1, 17, 0, 19, 13, 12, 11, 15, 15, 18, 16, 20, 15}},         /* Koopa Troopa: ShellSpin, Stunned, Topple* */
    {41, {3, 1, 12, 1, 18, 0, 19, 9, 8, 7, UP, UP, UP, UP, UP, UP}},           /* Paratroopa: ShellDive */
    {42, {3, 5, 4, 11, 14, 0, 15, 18, 16, 8, UP, UP, UP, UP, UP, UP}},         /* Fuzzy: Leech, Stunned */
    {43, {6, 2, 3, 2, 24, 0, 28, 17, 16, 14, UP, UP, UP, UP, UP, UP}},         /* Bob-omb: lit as it attacks */
    {44, {1, 1, 1, 1, 1, 0, 1, 7, 7, 5, UP, UP, UP, UP, UP, UP}},              /* Bullet Bill: no sleep or dizzy */
    {45, {1, 1, 2, 1, 1, 0, 1, 5, 5, 4, UP, UP, UP, UP, UP, UP}},              /* Bill Blaster: Fire, Collapse */
    {46, {3, 1, 4, 1, 20, 0, 20, 11, 10, 5, UP, UP, UP, UP, UP, UP}},          /* Monty Mole: dizzy on Sleep, as PM64 */
    {47, {7, 2, 17, 2, 12, 0, 14, 2, 8, 8, 5, 9, 13, 1, 15, 3}},               /* Cleft: Charge, Topple*, ToppleKick */
    {48, {8, 4, 17, 4, 37, 0, 33, 25, 24, 12, UP, UP, UP, UP, UP, UP}},        /* Pokey (four high): Throw */
    {49, {7, 1, 13, 1, 16, 0, 17, 11, 10, 9, UP, UP, UP, UP, UP, UP}},         /* Bandit: Tackle */
    {50, {4, 1, 5, 1, 9, 0, 10, 11, 11, 7, 2, 25, 27, 26, 28, 28}},            /* Buzzy Beetle: ShellSpin, Topple* */
    {51, {6, 6, 6, 2, 19, 1, 20, 24, 23, 21, UP, UP, UP, UP, UP, UP}},         /* Swooper: Fly */
    {52, {1, 1, 4, 1, 10, 0, 11, 8, 7, 6, UP, UP, UP, UP, UP, UP}},            /* Stone Chomp: Bite */
    {53, {3, 1, 6, 4, 0, 0, 15, 17, 16, 14, UP, UP, UP, UP, UP, UP}},          /* Piranhas: Bite; asleep Still, as PM64 */
    {58, {3, 9, 5, 1, 18, 0, 19, 14, 13, 12, UP, UP, UP, UP, UP, UP}},         /* Shy Guy: Tackle; asleep Slouch, as PM64 */
    {69, {3, 1, 3, 1, 11, 0, 10, 9, 8, 7, UP, UP, UP, UP, UP, UP}},            /* Lava Bubble, Ember: FlyFast */
    {71, {3, 1, 7, 1, 12, 0, 11, 10, 9, 8, UP, UP, UP, UP, UP, UP}},           /* Dayzees: SingFast */
    {72, {3, 1, 19, 22, 8, 0, 9, 6, 5, 7, UP, UP, UP, UP, UP, UP}},            /* Lakitu: LiftSpiny, Talk */
    {73, {4, 15, 8, 1, 16, 0, 18, 12, 11, 9, 2, 10, 17, 21, 19, 20}},          /* Spiny: ShellSpin, *Topple, Struggle */
    {76, {8, 3, 10, 3, 27, 0, 30, 23, 22, 19, 5, 21, 29, 2, 32, 21}},          /* Spike Top: ShellSpin, *Topple */
    {82, {4, 2, 6, 2, 14, 0, 13, 12, 11, 8, UP, UP, UP, UP, UP, UP}},          /* Dry Bones: ThrowBone, Collapse */
    {84, {14, 4, 22, 4, 42, 0, 38, 31, 30, 24, 6, 26, 44, 2, 40, 28}},         /* Bony Beetle: ShellSpin, *Toppled */
    {85, {1, 1, 3, 2, 8, 0, 7, 6, 5, 4, UP, UP, UP, UP, UP, UP}},              /* Magikoopa: CastSpell, Shout */
    {88, {4, 1, 6, 1, 17, 0, 19, 13, 12, 11, 16, 15, 18, 16, 20, 15}},         /* Koopatrol: ShellSpin, Topple* */
    {89, {7, 2, 12, 10, 17, 0, 18, 16, 15, 14, UP, UP, UP, UP, UP, UP}},       /* Hammer Bro: Throw, Talk */
    {124, {4, 1, 2, 12, 9, 0, 11, 8, 7, 6, UP, UP, UP, UP, UP, UP}},           /* Ruff Puff: Angry, Talk */
};

/* a winged enemy brought down (its host becomes another kind: btlevtcmd_ChangeKind) plays on as its grounded
 * cousin, as PM64's Paratroopa becomes a Koopa Troopa (paratroopa.inc.c SetActorType) */
static const struct {
    const char *winged, *grounded;
} kGrounded[] = {
    {"Paragoomba", "Goomba"},
    {"Hyper Paragoomba", "Hyper Goomba"},
    {"Paragloomba", "Gloomba"},
    {"Paratroopa", "Koopa Troopa"},
    {"Dark Paratroopa", "Dark Koopa"},
};

static int enemyNamed(const char* english) {
    int i;
    for (i = 0; i < ENEMIES; i++) {
        if (strcmp(kEnemies[i].english, english) == 0) {
            return i;
        }
    }
    return -1;
}

/* enemy idx as its unit is now: its grounded cousin once its host became that kind */
static int enemyAsNow(int idx, const BattleUnit* unit) {
    size_t k;
    if (unit == NULL || (int)unit->currentKind == (int)kEnemies[idx].host) {
        return idx;
    }
    for (k = 0; k < sizeof(kGrounded) / sizeof(kGrounded[0]); k++) {
        if (strcmp(kEnemies[idx].english, kGrounded[k].winged) == 0) {
            const int g = enemyNamed(kGrounded[k].grounded);
            return g >= 0 && (int)kEnemies[g].host == (int)unit->currentKind ? g : idx;
        }
    }
    return idx;
}

static int rowOf(const Pm64EnemyInfo* e, int st) {
    size_t k;
    int r;
    if (st == ST_WALK) {
        return e->walk;
    }
    if (st == ST_HURT) {
        return e->hurt;
    }
    if (st < ROW_FIRST || st >= ST_COUNT) {
        return e->idle;
    }
    for (k = 0; k < sizeof(kRows) / sizeof(kRows[0]); k++) {
        if (kRows[k].sprite == e->sprite) {
            r = kRows[k].row[st - ROW_FIRST];
            if (r == UP) {
                /* (no frames on its back: as it stands) */
                return rowOf(e, st == ST_FLIP_HURT || st == ST_FLIP_STRUGGLE ? ST_HURT
                                : st == ST_FLIP_SLEEP                          ? ST_SLEEP
                                : st == ST_FLIP_STILL                          ? ST_STILL
                                : st == ST_FLIP_DIZZY                          ? ST_DIZZY
                                                                               : ST_IDLE);
            }
            return r >= 0 ? r : e->idle;
        }
    }
    return e->idle; /* (a sprite with no row: the check fails on it) */
}

/* Script-set poses (by name, not in the part's pose table): what TTYD's letter before the number means there
 * ("NKT_S_3", "KRN_C_1", "A_1A"). A letter not here is neutral: the unit's statuses decide. The check reads this
 * table: every letter a host's scripts use is here or listed neutral. */
static const struct {
    char letter;
    signed char st; /* -1: neutral */
} kPoseLetter[] = {
    {'W', ST_WALK}, {'R', ST_RUN},       {'J', ST_JUMP},   {'F', ST_JUMP},  {'D', ST_HURT},
    {'Q', ST_BURN_HURT}, {'A', ST_ATTACK}, {'T', ST_TALK}, {'N', ST_SLEEP}, {'Y', ST_STILL},
    {'K', ST_DIZZY}, {'C', ST_DEAD},    {'Z', ST_DEAD},   {'S', -1},       {'X', -1},
    {'I', -1},       {'B', -1},         {'E', -1},        {'H', -1},       {'O', -1},
    {'P', -1},       {'G', -1},
};

/* a pose name's state by its letter (kPoseLetter); -1 neutral */
static int letterState(const char* pose) {
    const char* u = pose != NULL ? strrchr(pose, '_') : NULL; /* (the letter before the number) */
    const char c = u != NULL && u > pose ? u[-1] : pose != NULL ? pose[0] : 'S';
    size_t k;
    for (k = 0; k < sizeof(kPoseLetter) / sizeof(kPoseLetter[0]); k++) {
        if (kPoseLetter[k].letter == c) {
            return kPoseLetter[k].st;
        }
    }
    return -1;
}

/* which pose types' entries in the part's pose table name pose (has[t]: BattlePoseType t); 0 if none do.
 * A pose table has no end marker: the game's own lookup (BtlUnit_GetPoseNameFromType) stops at the first entry of
 * the asked type's fallback (BTL_POSE_NORMAL, BTL_POSE_DAMAGE, BTL_POSE_STAY_DEFAULT), so the table is only as long
 * as the furthest of those three; what lies past it is the next data in the unit's file (a Goomba's event table,
 * then its defence: a garbage name read there crashed the first battle). The scan stops where the game's would. */
static int poseTypesOf(const BattleUnitPart* part, const char* pose, unsigned char has[BTL_POSE_MAX]) {
    const PoseTableEntry* t = part != NULL ? part->poseTable : NULL;
    int n, any = 0, fallbacks = 0;
    memset(has, 0, BTL_POSE_MAX);
    for (n = 0; t != NULL && pose != NULL && n < 128 && fallbacks != 7 && t->poseType < BTL_POSE_MAX; n++, t++) {
        fallbacks |= t->poseType == BTL_POSE_NORMAL ? 1 : t->poseType == BTL_POSE_DAMAGE ? 2
                   : t->poseType == BTL_POSE_STAY_DEFAULT ? 4 : 0;
        if (t->poseName != NULL && t->poseType >= 0 && strcmp(t->poseName, pose) == 0) {
            has[t->poseType] = 1;
            any = 1;
        }
    }
    return any;
}

/* The state a PM64 enemy's battle unit (NULL: in the field) is in as its host plays pose: what the pose stands
 * for in the host's pose table (the type the game asked for: damage, attack, walk ...; one entry's name may stand
 * for several, and a standing one's for none of these), else its letter (kPoseLetter); a standing pose takes the
 * unit's statuses (stopped, frozen, asleep, dizzy, confused, burnt), as the game picks its status pose
 * (_GetStatusPoseType); and all of it on its back while the unit is flipped (STATUS_FLAG_FLIPPED: the host's
 * flipped pose table plays then, "NKT_S_2"). */
static int stateOf(const BattleUnit* unit, const BattleUnitPart* part, const char* pose) {
    unsigned char m[BTL_POSE_MAX];
    const int any = poseTypesOf(part, pose, m);
    const int flipped = unit != NULL && (unit->statusFlags & STATUS_FLAG_FLIPPED) != 0;
    const int burnt = unit != NULL && unit->burnTurns > 0;
    int st = -1, neutral = 0;
    if (any) {
        int t, hurt = m[BTL_POSE_DAMAGE];
        neutral = m[BTL_POSE_NORMAL] || m[BTL_POSE_STAY_DEFAULT];
        for (t = 32; t < BTL_POSE_DAMAGE; t++) {
            hurt |= m[t];
        }
        if (m[STATUS_INSTANT_KILL]) {
            st = ST_DEAD;
        } else if (unit != NULL && ((m[STATUS_SLEEP] && unit->sleepTurns > 0) ||
                                    ((m[STATUS_STOP] || m[STATUS_FREEZE]) && (unit->stopTurns > 0 || unit->freezeTurns > 0)) ||
                                    ((m[STATUS_DIZZY] || m[STATUS_CONFUSE]) && (unit->dizzyTurns > 0 || unit->confusionTurns > 0)))) {
            st = -1; /* (the status pose the game picked for the status it has: a Bullet Bill frozen in "KIL_D_1") */
        } else if (neutral) {
            /* (a name standing for the plain stance too, or for a whole table's worth: a flipped Cleft's
               "SIN_D_2", a charging Koopatrol's shell "A_1"; its letter says which) */
            st = letterState(pose);
        } else if (m[29] || m[30]) {
            st = ST_BURN_HURT; /* (fire and explosion hits: the part drawn black) */
        } else if (hurt) {
            st = ST_HURT;
        } else if (m[50] || m[51]) {
            st = ST_ATTACK;
        } else if (m[BTL_POSE_RUN] || m[41]) {
            st = ST_RUN;
        } else if (m[BTL_POSE_WALK] || m[47]) {
            st = ST_WALK;
        } else if (m[BTL_POSE_TALK]) {
            st = ST_TALK;
        }
    } else {
        st = letterState(pose);
        if (st < 0 && flipped && part != NULL) {
            st = ST_FLIP_STRUGGLE; /* (a script's own pose while it lies there: the wake-up struggle, "NKT_S_3") */
        }
    }
    if (st < 0) {
        /* standing: as its statuses have it (_GetStatusPoseType's order) */
        if (unit != NULL && (unit->stopTurns > 0 || unit->freezeTurns > 0)) {
            st = ST_STILL;
        } else if (unit != NULL && unit->sleepTurns > 0) {
            st = ST_SLEEP;
        } else if (unit != NULL && (unit->dizzyTurns > 0 || unit->confusionTurns > 0)) {
            st = ST_DIZZY;
        } else if (any && !neutral && m[STATUS_SLEEP]) {
            st = ST_SLEEP; /* (a status pose the unit's turns don't show: as the table has it) */
        } else if (any && !neutral && (m[STATUS_STOP] || m[STATUS_FREEZE])) {
            st = ST_STILL;
        } else if (any && !neutral && (m[STATUS_DIZZY] || m[STATUS_CONFUSE])) {
            st = ST_DIZZY;
        } else {
            st = burnt ? ST_BURNT : ST_IDLE;
        }
    }
    if (burnt && st == ST_HURT) {
        st = ST_BURN_HURT;
    }
    if (flipped) {
        switch (st) {
        case ST_IDLE:
        case ST_BURNT:
        case ST_TALK:
            st = ST_FLIP;
            break;
        case ST_HURT:
        case ST_BURN_HURT:
            st = ST_FLIP_HURT;
            break;
        case ST_SLEEP:
            st = ST_FLIP_SLEEP;
            break;
        case ST_STILL:
            st = ST_FLIP_STILL;
            break;
        case ST_DIZZY:
            st = ST_FLIP_DIZZY;
            break;
        default:
            break;
        }
    }
    return st;
}

/* the animation enemy e plays for its host's pose (see stateOf); a battle's changes logged with $TTYD_PM64_LOG
 * (tests: "[pm64] anim: Koopa Troopa flipped (NKT_S_2): row 15") */
static int animOf(const Pm64EnemyInfo* e, const char* pose, const BattleUnit* unit, const BattleUnitPart* part) {
    static const char* const kStateName[ST_COUNT] = {
        "idle",       "walk",    "hurt",          "run",          "jump",           "attack",     "talk",
        "asleep",     "still",   "dizzy",         "burnt",        "burn hurt",      "knocked out", "flipped",
        "flipped hurt", "flipped asleep", "flipped still", "flipped dizzy", "flipped struggle"};
    static struct {
        const BattleUnit* unit;
        int st;
    } sLast[8];
    static int sLog = -1;
    const int st = stateOf(unit, part, pose);
    const int row = rowOf(e, st);
    if (sLog < 0) {
        const char* v = getenv("TTYD_PM64_LOG");
        sLog = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    if (sLog && unit != NULL) {
        int i, slot = -1;
        for (i = 0; i < 8 && slot < 0; i++) {
            slot = sLast[i].unit == unit ? i : slot;
        }
        for (i = 0; i < 8 && slot < 0; i++) {
            slot = sLast[i].unit == NULL ? i : slot;
        }
        slot = slot < 0 ? (int)(gp->retraceCount % 8u) : slot;
        if (sLast[slot].unit != unit || sLast[slot].st != st) {
            sLast[slot].unit = unit;
            sLast[slot].st = st;
            printf("[pm64] anim: %s %s (%s): row %d\n", e->english, kStateName[st], pose != NULL ? pose : "-", row);
            fflush(stdout);
        }
    }
    return row;
}

/* a draw in another colour (sTintOn: the swarm's dark hearts), or added on top (sGlowOn: a charged glow) */
static int sTintOn, sGlowOn, sGlowNext;
static GXColor sTint;

/* the pose's own colour and fade, as its sprite is drawn */
static GXColor poseTint(const AnimationPose* pose) {
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    if (pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255);
    }
    return tint;
}

/* Cool Mode: the rows a pose draws as one sprite this frame (a boss's body and the face and arms over it), each
 * with its matrix; the glasses go on once, on the face (coolFlush). The vine's segments, the glow drawn again on
 * top and the swarm's hearts aren't in it. */
#define COOL_ROWS 3
typedef struct CoolSet {
    int n;
    const Pm64Anim* a[COOL_ROWS];
    Mtx m[COOL_ROWS];
    GXColor tint;
} CoolSet;

/* each pose's face, kept frame to frame (port/include/port/pm64.h portPm64AnimFace) */
static struct {
    int pose1;
    unsigned frame;
    Pm64Face f;
} sFace[PLAYS];

static Pm64Face* faceOf(int poseId) {
    int i, oldest = 0;
    for (i = 0; i < PLAYS; i++) {
        if (sFace[i].pose1 == poseId + 1) {
            break;
        }
        if (sFace[i].pose1 == 0 || (sFace[oldest].pose1 != 0 && sFace[i].frame < sFace[oldest].frame)) {
            oldest = i;
        }
    }
    if (i == PLAYS || sFace[i].frame + 30u < gp->retraceCount) {
        /* (a pose met again after a while, or one seen first: a new face) */
        i = i == PLAYS ? oldest : i;
        memset(&sFace[i], 0, sizeof(sFace[i]));
        sFace[i].pose1 = poseId + 1;
        sFace[i].f.part = -1;
    }
    sFace[i].frame = gp->retraceCount;
    return &sFace[i].f;
}

/* where the glasses go on an enemy whose eyes no frame has shown yet (asleep from the start, eyes shut, an
 * eyeless shell): high on the middle of its frame */
static const float kCoolSeed[3] = {0.42f, 0.72f, 0.45f};

static void coolFlush(int poseId, CoolSet* s) {
    float box[4], at[3];
    int k = 0;
    if (s->n <= 0 || !portFunOn(PORT_FUN_COOL)) {
        return;
    }
    if (portPm64AnimFace(s->a, s->n, faceOf(poseId), 1, kCoolSeed, box, at, &k) && k >= 0 && k < s->n) {
        portFunCoolSprite((MtxPtr)s->m[k], box[0], box[1], box[2], box[3], at, &s->tint);
    }
}

static void drawParts(const Pm64Anim* a, Mtx m, const AnimationPose* pose, int palette) {
    CameraEntry* cam = camGetCurPtr();
    Mtx mv, nrm;
    Vec at = {0.0f, 0.0f, 0.0f};
    GXColor tint;
    int i;
    if (cam == NULL || a->sprite == NULL) {
        return;
    }
    tint = poseTint(pose);
    if (sTintOn || sGlowOn) {
        tint = sTint;
    }
    PSMTXMultVec(m, &at, &at);
    mapSetMaterialLight(pose->materialLightFlag | ANIM_MATERIAL_LIGHT_NO_VTX_COLOR, at);
    PSMTXConcat(cam->view, m, mv);
    GXLoadPosMtxImm(mv, GX_PNMTX0);
    /* lit as a sprite facing the camera, however it is turned: PM64 doesn't light its sprites by their facing (an
       edge-on frame of a paper turn or of the Spin Dash went near black, the light at right angles to it; the
       spin's own shading, spinParts, is PM64's) */
    PSMTXIdentity(nrm);
    GXLoadNrmMtxImm(nrm, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_NRM, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumIndStages(0);
    GXSetNumTevStages(2);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevKColor(GX_KCOLOR0, tint);
    GXSetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
    GXSetTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K0_A);
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_KONST, GX_CA_ZERO);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetAlphaCompare(GX_GREATER, 8, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZCompLoc(GX_FALSE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, tint.a == 0xFF && !sGlowOn ? GX_TRUE : GX_FALSE);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, sGlowOn ? GX_BL_ONE : GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetCullMode(GX_CULL_NONE);
    for (i = 0; i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        GXTexObj tex;
        const void* data;
        int w, h, tw, th;
        float x0, x1, y0, y1, z, u, v;
        const int pal = p->palette >= 0 ? p->palette : palette;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) ||
            (data = portPm64RasterTexture(a->sprite, p->raster, pal, &tw, &th)) == NULL) {
            continue;
        }
        GXInitTexObj(&tex, (void*)data, (u16)tw, (u16)th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        GXLoadTexObj(&tex, GX_TEXMAP0);
        x0 = -(float)(w / 2) * p->sx;
        x1 = (float)(w - w / 2) * p->sx;
        y0 = 0.0f;
        y1 = (float)h * p->sy;
        z = p->z * 0.05f + (float)i * 0.02f;
        u = (float)w / (float)tw;
        v = (float)h / (float)th;
        {
            /* the quad about the part's point, turned by the part's own rotation as PM64's spr_appendGfx_component
               turns it (its matrix T * Ry * Rz * Rx * S: x first, then z, then y): Mario's hammer swung down
               (MarioB1's Swing, the hammer at 140 degrees), an enemy's turned arm or shell; half units */
            const float cx[4] = {x0, x1, x1, x0}, cy[4] = {y1, y1, y0, y0};
            const float su[4] = {0.0f, u, u, 0.0f}, sv[4] = {0.0f, 0.0f, v, v};
            const float ax = 0.017453292f * p->rx, ay = 0.017453292f * p->ry, az = 0.017453292f * p->rz;
            const float sa = sinf(ax), ca = cosf(ax), sb = sinf(ay), cb = cosf(ay), sc = sinf(az), cc = cosf(az);
            int k;
            GXBegin(GX_QUADS, GX_VTXFMT0, 4);
            for (k = 0; k < 4; k++) {
                float px = cx[k], py = cy[k], pz = 0.0f, t;
                t = py * ca - pz * sa; /* about x */
                pz = py * sa + pz * ca;
                py = t;
                t = px * cc - py * sc; /* about z */
                py = px * sc + py * cc;
                px = t;
                t = px * cb + pz * sb; /* about y */
                pz = -px * sb + pz * cb;
                px = t;
                GXPosition3f32((p->x + px) * 0.5f, (p->y + py) * 0.5f, pz * 0.5f + z);
                GXNormal3f32(0.0f, 0.0f, 1.0f);
                GXTexCoord2f32(su[k], sv[k]);
            }
            GXEnd();
        }
    }
}

/* Mario's PM64 look spinning (the Spin Dash, pm64_spin.h): his paper's facing now and a frame of PM64's before */
static struct {
    int on;
    float now, before;
} sSpinLook;

/* src/77480.c appendGfx_player_spin: the spinning sprite shaded as it turns edge on (255, less 60 for each whole
 * 25 degrees it is turned from facing left or right, at least 100), then its afterimage, a quarter seen (alpha
 * 64), turned on by his facing a frame before (phys_get_spin_history 1) */
static void spinParts(const Pm64Anim* a, Mtx m, const AnimationPose* pose, int palette) {
    const GXColor base = poseTint(pose);
    const float f = fmodf(fmodf(sSpinLook.now, 360.0f) + 360.0f, 360.0f);
    const float off = f > 90.0f && f <= 180.0f ? 180.0f - f : f > 180.0f && f <= 270.0f ? f - 180.0f
                      : f > 270.0f                ? 360.0f - f
                                                  : f;
    int tint = 255 - (int)(off / 25.0f) * 60;
    Mtx g, t;
    if (tint < 100) {
        tint = 100;
    }
    sTintOn = 1;
    sTint = base;
    sTint.r = (u8)(base.r * tint / 255);
    sTint.g = (u8)(base.g * tint / 255);
    sTint.b = (u8)(base.b * tint / 255);
    drawParts(a, m, pose, palette);
    PSMTXRotRad(t, 'y', 0.017453292f * sSpinLook.before);
    PSMTXConcat(m, t, g);
    sTint.a = (u8)(base.a * 64 / 255);
    drawParts(a, g, pose, palette);
    sTintOn = 0;
}

/* pose poseId drawn as PM64 sprite sprite playing anim (main: this pose draws it; the others of its
 * unit draw nothing); 1 if it was. cool: the row noted for Cool Mode's glasses (NULL: not a face's row) */
static int drawOnPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int main, int sprite,
                      int palette, int anim, CoolSet* cool) {
    AnimationPose* pose = animPoseGetAnimPosePtr(poseId);
    Mtx m, t;
    float turn, flip;
    if (pose == NULL) {
        return 0;
    }
    /* the host isn't drawn (a pose drawn with disableDraw still draws its shapes, in the last material set:
       another model's textures on it); its paper turn steps once a frame (portAnimPoseStepTurn) */
    if (xluMode != 1) {
        return 1;
    }
    turn = portAnimPoseStepTurn(poseId, rotY, &flip);
    if (!main || pose->disableDraw || portPaperShadowPass()) {
        return 1;
    }
    PSMTXCopy(mtx, m);
    if (turn != 0.0f) {
        PSMTXRotRad(t, 'y', 0.017453292f * turn);
        PSMTXConcat(m, t, m);
    }
    if (flip < 0.0f) {
        PSMTXScale(t, 1.0f, 1.0f, -1.0f);
        PSMTXConcat(m, t, m);
    }
    if (scale != 1.0f) {
        PSMTXScale(t, scale, scale, scale);
        PSMTXConcat(m, t, m);
    }
    /* PM64's enemies face left, as TTYD's enemies on the right do: as they are */
    if (sSpinLook.on) {
        spinParts(playOf(poseId, sprite, anim), m, pose, palette);
    } else {
        drawParts(playOf(poseId, sprite, anim), m, pose, palette);
    }
    if (cool != NULL && cool->n < COOL_ROWS) {
        cool->a[cool->n] = playOf(poseId, sprite, anim);
        PSMTXCopy(m, cool->m[cool->n]);
        if (cool->n == 0) {
            cool->tint = poseTint(pose);
        }
        cool->n++;
    }
    if (sGlowNext) {
        /* a charged boss glows (PM64's EnableActorGlow): its sprite again, added on top, pulsing */
        const float s = 0.5f + 0.5f * sinf((float)(gp->retraceCount % 60) * (6.2831853f / 60.0f));
        sTint.r = 0xFF;
        sTint.g = 0x60;
        sTint.b = 0x90;
        sTint.a = (u8)(40.0f + 110.0f * s);
        if (sGlowNext == 2) {
            /* a lit bulb (General Guy's Light Bulb, PM64 EFFECT_BULB_GLOW): warm yellow, a slow flicker */
            sTint.r = 0xFF;
            sTint.g = 0xF0;
            sTint.b = 0x80;
            sTint.a = (u8)(150.0f + 50.0f * s);
        } else if (sGlowNext == 3) {
            /* the Star Rod's power on Bowser (PM64 UpdateEnchantedBowserColors): colours cycling */
            const float h = (float)(gp->retraceCount % 120u) * (6.2831853f / 120.0f);
            sTint.r = (u8)(160.0f + 95.0f * sinf(h));
            sTint.g = (u8)(160.0f + 95.0f * sinf(h + 2.0943951f));
            sTint.b = (u8)(160.0f + 95.0f * sinf(h + 4.1887902f));
            sTint.a = (u8)(30.0f + 50.0f * s); /* (a shimmer over his own colours, never a wash) */
        }
        sGlowOn = 1;
        drawParts(playOf(poseId, sprite, anim), m, pose, palette);
        sGlowOn = 0;
    }
    return 1;
}

/* The Dark Swarm (PM64 tubbas_heart.c EVS_Attack_DarkSwarm, EFFECT_TUBBA_HEART_ATTACK): the Heart's own sprite,
 * small, dark and many: they fly from it to the target, swarm about it while it's squeezed, and scatter. */
#define SWARM_HEARTS 18
static void drawSwarm(int poseId, int sprite, int palette, int anim, const float from[3], const float to[3], int age) {
    const AnimationPose* pose = animPoseGetAnimPosePtr(poseId);
    const Pm64Anim* a = playOf(poseId, sprite, anim);
    int i;
    if (pose == NULL || age < 0 || age > 190) {
        return;
    }
    sTintOn = 1;
    for (i = 0; i < SWARM_HEARTS; i++) {
        const float k = (float)i / (float)SWARM_HEARTS;
        const float spin = (float)age * 0.09f + k * 6.2831853f;
        float r = 26.0f - 8.0f * k, x, y, z, in = 1.0f, alpha = 1.0f, size = 0.32f + 0.12f * (float)(i % 3) / 2.0f;
        Mtx m, t;
        if (age < 40) {
            /* on their way, one after another */
            in = (float)(age - (int)(k * 12.0f)) / 28.0f;
            in = in < 0.0f ? 0.0f : in > 1.0f ? 1.0f : in;
        } else if (age > 150) {
            /* scattering */
            r += (float)(age - 150) * 4.0f;
            alpha = 1.0f - (float)(age - 150) / 40.0f;
        }
        if (in <= 0.0f || alpha <= 0.0f) {
            continue;
        }
        x = to[0] + cosf(spin) * r;
        y = to[1] + 8.0f + k * 28.0f + sinf(spin * 2.0f + k * 9.0f) * 4.0f;
        z = to[2] + sinf(spin) * r * 0.6f;
        x = from[0] + (x - from[0]) * in;
        y = from[1] + 16.0f + (y - from[1] - 16.0f) * in + sinf(in * 3.1415927f) * 30.0f;
        z = from[2] + (z - from[2]) * in;
        sTint.r = 0x40;
        sTint.g = 0x18;
        sTint.b = 0x50;
        sTint.a = (u8)(230.0f * alpha);
        PSMTXTrans(m, x, y, z);
        PSMTXScale(t, size, size, size);
        PSMTXConcat(m, t, m);
        drawParts(a, m, pose, palette);
    }
    sTintOn = 0;
}

const char* portPm64PoseAnimName(int poseId, const char* fallback) {
    AnimationPose* pose = poseId >= 0 ? animPoseGetAnimPosePtr(poseId) : NULL;
    AnimationPoseData* data = poseId >= 0 ? animPoseGetAnimBaseDataPtr(poseId) : NULL;
    if (pose != NULL && data != NULL && pose->animId >= 0 && pose->animId < data->animCount) {
        return data->anims[pose->animId].name;
    }
    return fallback;
}

/* PM64's size beside Mario, as the partners have it (portPm64Size: each stands beside TTYD Mario as beside PM64's);
 * Mario's own PM64 look is drawn at it too (TTYD Mario's height), so everything keeps PM64's proportions to him
 * and to the TTYD characters around them alike */
static float pm64Factor(void) {
    return portPm64Size();
}

/* dst: src moved v up its own y axis (before drawOnPose's turn and scale) */
static void liftMtx(float (*src)[4], float (*dst)[4], float v) {
    int i;
    PSMTXCopy(src, dst);
    for (i = 0; i < 3; i++) {
        dst[i][3] += src[i][1] * v;
    }
}

/* row's parts' extent up the sprite (pixels from its feet): where its lowest starts, how tall; 0 none */
static int rowExtent(int poseId, int sprite, int row, float* bottom, float* height) {
    const Pm64Anim* a = playOf(poseId, sprite, row);
    float lo = 1e9f, hi = -1e9f;
    int i;
    for (i = 0; a != NULL && i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        int w, h;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) || h <= 0) {
            continue;
        }
        lo = p->y < lo ? p->y : lo;
        hi = p->y + (float)h * p->sy > hi ? p->y + (float)h * p->sy : hi;
    }
    if (hi <= lo) {
        return 0;
    }
    *bottom = lo;
    *height = hi - lo;
    return 1;
}

int portPm64EnemyDrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    const BattleUnit* unit = NULL;
    const BattleUnitPart* part;
    const Pm64EnemyInfo* e;
    const char* poseName = NULL;
    int main = 0, idx;
    if (poseId < 0 || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    if (gp != NULL && !gp->inBattle) {
        /* a field enemy wandering as its PM64 cousin (the story's, port/include/port/pm64_foes.h) */
        if (!portPm64FoeFieldPose(poseId, &idx, &poseName) || idx < 0 || idx >= ENEMIES) {
            return 0;
        }
        CoolSet cool = {0};
        int r;
        e = &kEnemies[idx];
        r = drawOnPose(poseId, mtx, xluMode, rotY, scale * pm64Factor(), 1, e->sprite, e->palette,
                       animOf(e, poseName, NULL, NULL), &cool);
        coolFlush(poseId, &cool);
        return r;
    }
    if ((idx = enemyOfPose(poseId, &main, &unit)) < 0) {
        /* a Paper Mario 64 boss (its own unit: port/include/port/pm64_foes.h) */
        BattleWork* wp = _battleWorkPointer;
        int i, sprite, palette, anim, glow = 0, age = 0, r;
        float size, from[3], to[3];
        for (i = 0; wp != NULL && i < BATTLE_UNIT_MAX; i++) {
            const BattleUnit* u = BattleGetUnitPtr(wp, i);
            if (u == NULL || u->parts == NULL || u->parts->poseId != poseId ||
                !portPm64BossDraw(u, &sprite, &palette, &anim, &size)) {
                continue;
            }
            const int swarm = portPm64BossFx(u, &glow, from, to, &age);
            int rowA, rowB;
            float vine, head[3][4];
            /* PM64's size beside Mario, as the partners (portPm64Size) */
            size *= pm64Factor();
            PSMTXCopy(mtx, head);
            if (portPm64BossVine(u, &rowA, &rowB, &vine)) {
                /* a head on a vine (Lava Piranha, the Lava Buds): the vine's segments stacked up from the floor,
                   alternating, and the head at its top (its parts hang from there) */
                float at = 0.0f, bottom, h;
                int k;
                for (k = 0; k < 24 && at < vine; k++) {
                    const int row = (k & 1) ? rowB : rowA;
                    float m[3][4];
                    if (!rowExtent(poseId, sprite, row, &bottom, &h) || h < 1.0f) {
                        break;
                    }
                    liftMtx(mtx, m, (at - bottom) * 0.5f * scale * size);
                    m[2][3] -= 0.2f; /* (behind the head) */
                    drawOnPose(poseId, m, xluMode, rotY, scale * size, 1, sprite, palette, row, NULL);
                    at += h;
                }
                liftMtx(mtx, head, vine * 0.5f * scale * size);
            }
            sGlowNext = glow;
            CoolSet cool = {0};
            r = drawOnPose(poseId, head, xluMode, rotY, scale * size, 1, sprite, palette, anim, &cool);
            {
                /* a sprite in parts (Huff N. Puff's face and arms over his body), each a hair nearer */
                int n, layer;
                for (n = 0; n < 2; n++) {
                    if (portPm64BossLayer(u, anim, n, &layer)) {
                        float m[3][4];
                        PSMTXCopy(head, m);
                        m[2][3] += 0.5f * (float)(n + 1);
                        drawOnPose(poseId, m, xluMode, rotY, scale * size, 1, sprite, palette, layer, &cool);
                    }
                }
            }
            sGlowNext = 0;
            coolFlush(poseId, &cool); /* (once, on the face: on top of the parts drawn over it) */
            if (swarm && xluMode == 1 && !portPaperShadowPass()) {
                drawSwarm(poseId, sprite, palette, anim, from, to, age);
            }
            return r;
        }
        return 0;
    }
    e = &kEnemies[idx];
    for (part = unit->parts; part != NULL; part = part->nextPart) {
        if (part->poseId == poseId) {
            /* (the pose it plays: a status-picked one leaves poseName as the last set by hand) */
            poseName = portPm64PoseAnimName(poseId, part->poseName);
        }
    }
    /* PM64's size beside Mario; the TTYD host's own design size (its base scale, in the unit's matrix) taken back
       out, so the cousin is its own size (a status's size change stays) */
    if (unit->baseScale.y > 0.05f) {
        scale /= unit->baseScale.y;
    }
    {
        CoolSet cool = {0};
        const BattleUnitPart* own = NULL;
        int r;
        for (part = unit->parts; part != NULL; part = part->nextPart) {
            own = part->poseId == poseId ? part : own;
        }
        e = &kEnemies[enemyAsNow(idx, unit)]; /* (a winged one brought down: its grounded cousin) */
        r = drawOnPose(poseId, mtx, xluMode, rotY, scale * pm64Factor(), main, e->sprite, e->palette,
                       animOf(e, poseName, unit, own), &cool);
        coolFlush(poseId, &cool);
        return r;
    }
}

/* --- the Tattle card's picture (src/window/win_log.c's capture; port/include/port/pm64_enemy.h) --- */

/* a PM64 enemy's or boss's unit of kind `kind` in this battle: the one being tattled (the move's target), else the
 * first of its kind; its sprite, palette and idle row, standing as itself as TTYD's cards show theirs (*enemy: the
 * PM64 enemy's index, -1 a boss). NULL: none (a TTYD enemy of that kind, or not in a battle). */
static const BattleUnit* cardUnit(int kind, int* sprite, int* palette, int* row, int* enemy) {
    BattleWork* wp = _battleWorkPointer;
    const BattleUnit* pick[BATTLE_UNIT_MAX + 1];
    int n = 0, i;
    if (wp == NULL || gp == NULL || !gp->inBattle) {
        return NULL;
    }
    if (wp->weaponTargets.numTargets > 0 && wp->weaponTargets.currentTarget >= 0 &&
        wp->weaponTargets.currentTarget < wp->weaponTargets.numTargets) {
        const int t = wp->weaponTargets.targetIndices[(int)wp->weaponTargets.currentTarget];
        const BattleUnit* tu;
        if (t >= 0 && t < (int)(sizeof(wp->weaponTargets.targets) / sizeof(wp->weaponTargets.targets[0])) &&
            (tu = BattleGetUnitPtr(wp, wp->weaponTargets.targets[t].unitIdx)) != NULL && (int)tu->currentKind == kind) {
            pick[n++] = tu; /* (the one tattled says alone: a TTYD enemy may stand beside a PM64 one of its kind) */
        }
    }
    if (n == 0) {
        for (i = 0; i < BATTLE_UNIT_MAX; i++) {
            pick[n++] = BattleGetUnitPtr(wp, i);
        }
    }
    for (i = 0; i < n; i++) {
        const BattleUnit* u = pick[i];
        const PortEnemyVariant* v;
        if (u == NULL || (int)u->currentKind != kind) {
            continue;
        }
        if ((v = portEnemyVariantOf(u)) != NULL && v->pm64 > 0 && v->pm64 <= ENEMIES) {
            const Pm64EnemyInfo* e = &kEnemies[v->pm64 - 1];
            *sprite = e->sprite;
            *palette = e->palette;
            *row = e->idle; /* (its picture: standing, as TTYD's cards show their enemies) */
            *enemy = v->pm64 - 1;
            return u;
        }
        if (portPm64BossCardRow(u, sprite, palette, row)) {
            *enemy = -1;
            return u;
        }
    }
    return NULL;
}

/* a row's parts' box as drawParts draws them (x0, x1, y0, y1); 0: nothing drawn */
static int animBox(const Pm64Anim* a, float box[4]) {
    int i, any = 0;
    for (i = 0; a->sprite != NULL && i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        float x0, x1, y0, y1, t;
        int w, h;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) || w <= 0 || h <= 0) {
            continue;
        }
        x0 = (p->x - (float)(w / 2) * p->sx) * 0.5f;
        x1 = (p->x + (float)(w - w / 2) * p->sx) * 0.5f;
        y0 = p->y * 0.5f;
        y1 = (p->y + (float)h * p->sy) * 0.5f;
        if (x0 > x1) {
            t = x0, x0 = x1, x1 = t; /* (a part drawn mirrored) */
        }
        if (y0 > y1) {
            t = y0, y0 = y1, y1 = t;
        }
        if (!any) {
            box[0] = x0, box[1] = x1, box[2] = y0, box[3] = y1;
            any = 1;
        } else {
            box[0] = x0 < box[0] ? x0 : box[0];
            box[1] = x1 > box[1] ? x1 : box[1];
            box[2] = y0 < box[2] ? y0 : box[2];
            box[3] = y1 > box[3] ? y1 : box[3];
        }
    }
    return any;
}

int portPm64TattleCardIs(int kind) {
    int sprite, palette, row, enemy;
    return portPm64Linked() != PM64_NONE && cardUnit(kind, &sprite, &palette, &row, &enemy) != NULL &&
           portPm64NpcSprite(sprite) != NULL;
}

#define CARD_ROWS 28
int portPm64TattleCapture(int kind, int cardPose, unsigned tw, unsigned th) {
    static Pm64Anim sCard[CARD_ROWS]; /* (the rows drawn: a vine's segments, the head, its parts over it) */
    struct {
        float lift, z;
    } draw[CARD_ROWS];
    const BattleUnit* u;
    AnimationPose* pose;
    CameraEntry* cam;
    int sprite, palette, row, enemy, n = 0, i, rowA, rowB, layer, any = 0;
    float vine = 0.0f, box[4] = {0.0f, 0.0f, 0.0f, 0.0f}, b[4], cx, cy, hx, hy, dist;
    Mtx44 pm;
    Mtx view, keep, m;
    if (portPm64Linked() == PM64_NONE || cardPose < 0 || tw == 0 || th == 0 ||
        (u = cardUnit(kind, &sprite, &palette, &row, &enemy)) == NULL || portPm64NpcSprite(sprite) == NULL ||
        (pose = animPoseGetAnimPosePtr(cardPose)) == NULL || (cam = camGetCurPtr()) == NULL) {
        return 0;
    }
    if (enemy < 0 && portPm64BossVine(u, &rowA, &rowB, &vine)) {
        /* a head on a vine (Lava Piranha, the Lava Buds): the vine's segments stacked under it, as on the stage */
        float at = 0.0f;
        int k;
        for (k = 0; k < 24 && at < vine && n < CARD_ROWS - 3; k++) {
            portPm64AnimStart(&sCard[n], portPm64NpcSprite(sprite), (k & 1) ? rowB : rowA);
            if (!animBox(&sCard[n], b) || b[3] - b[2] < 0.5f) {
                break;
            }
            draw[n].lift = at * 0.5f - b[2];
            draw[n].z = -0.2f;
            n++;
            at += (b[3] - b[2]) * 2.0f;
        }
    } else {
        vine = 0.0f;
    }
    portPm64AnimStart(&sCard[n], portPm64NpcSprite(sprite), row);
    draw[n].lift = vine * 0.5f;
    draw[n].z = 0.0f;
    n++;
    for (i = 0; enemy < 0 && i < 2; i++) {
        /* a sprite in parts (Huff N. Puff's face and arms over his body) */
        if (portPm64BossLayer(u, row, i, &layer)) {
            portPm64AnimStart(&sCard[n], portPm64NpcSprite(sprite), layer);
            draw[n].lift = vine * 0.5f;
            draw[n].z = 0.5f * (float)(i + 1);
            n++;
        }
    }
    for (i = 0; i < n; i++) {
        if (animBox(&sCard[i], b)) {
            b[2] += draw[i].lift;
            b[3] += draw[i].lift;
            if (!any) {
                memcpy(box, b, sizeof(box));
                any = 1;
            } else {
                box[0] = b[0] < box[0] ? b[0] : box[0];
                box[1] = b[1] > box[1] ? b[1] : box[1];
                box[2] = b[2] < box[2] ? b[2] : box[2];
                box[3] = b[3] > box[3] ? b[3] : box[3];
            }
        }
    }
    if (!any || box[1] - box[0] < 0.5f || box[3] - box[2] < 0.5f) {
        return 0;
    }
    /* framed as the card frames a model by its box (capture): centred, the larger way filling the picture as far
       as TTYD's own enemies do */
    cx = (box[0] + box[1]) * 0.5f;
    cy = (box[2] + box[3]) * 0.5f;
    hx = (box[1] - box[0]) * 0.5f;
    hy = (box[3] - box[2]) * 0.5f;
    if (2.0f * hx / (float)tw > 2.0f * hy / (float)th) {
        dist = hx / tanf(0.21816615760326385f);
    } else {
        dist = (608.0f * hy / 480.0f) / tanf(0.21816615760326385f);
    }
    C_MTXPerspective(pm, 25.0f, 1.2666667f, 1.0f, 1000.0f + dist);
    GXSetProjection(pm, GX_PERSPECTIVE);
    C_MTXLookAt(view, &(Vec){cx, cy, dist}, &(Vec){0.0f, 1.0f, 0.0f}, &(Vec){cx, cy, 0.0f});
    PSMTXCopy(cam->view, keep);
    PSMTXCopy(view, cam->view);
    {
        CoolSet cool = {0};
        for (i = 0; i < n; i++) {
            PSMTXTrans(m, 0.0f, draw[i].lift, draw[i].z);
            drawParts(&sCard[i], m, pose, palette);
            if (draw[i].z >= 0.0f && cool.n < COOL_ROWS) {
                /* (the head and its parts, not the vine) */
                cool.a[cool.n] = &sCard[i];
                PSMTXCopy(m, cool.m[cool.n]);
                cool.n++;
            }
        }
        if (cool.n > 0 && portFunOn(PORT_FUN_COOL)) {
            Pm64Face face = {0};
            float fb[4], at[3];
            int k = 0;
            face.part = -1;
            cool.tint = poseTint(pose);
            if (portPm64AnimFace(cool.a, cool.n, &face, 1, kCoolSeed, fb, at, &k) && k >= 0 && k < cool.n) {
                portFunCoolSprite((MtxPtr)cool.m[k], fb[0], fb[1], fb[2], fb[3], at, &cool.tint);
            }
        }
    }
    PSMTXCopy(keep, cam->view);
    {
        static int sLog = -1;
        if (sLog < 0) {
            const char* log = getenv("TTYD_PM64_LOG");
            sLog = log != NULL && log[0] == '1';
        }
        if (sLog) {
            printf("[pm64] tattle card: kind %d drawn as sprite %d row %d (%s, %d rows, %.0f x %.0f: x %.1f to %.1f, "
                   "y %.1f to %.1f, vine %.0f)\n",
                   kind, sprite, row, enemy >= 0 ? kEnemies[enemy].english : "a boss", n, box[1] - box[0],
                   box[3] - box[2], box[0], box[1], box[2], box[3], vine);
            for (i = 0; i < n; i++) {
                if (animBox(&sCard[i], b)) {
                    printf("[pm64] tattle card:   row %d: x %.1f to %.1f, y %.1f to %.1f, lift %.1f, %d parts\n",
                           sCard[i].anim, b[0], b[1], b[2], b[3], draw[i].lift, sCard[i].count);
                }
            }
            fflush(stdout);
        }
    }
    return 1;
}

/* --- the other PM64 sprites standing in for the game's models (port/include/port/pm64_media.h) --- */

int gPortPm64RowfPose = -1;
Pm64FieldLook gPortPm64FieldLooks[PM64_FIELD_LOOKS] = {
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
};

int portPm64TexOpaqueBounds(const void* tex, int texW, int w, int h, int* x0, int* y0, int* x1, int* y1) {
    const unsigned char* t = (const unsigned char*)tex;
    int x, y, found = 0;
    *x0 = w;
    *y0 = h;
    *x1 = *y1 = -1;
    if (t == NULL || texW <= 0) {
        return 0;
    }
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            /* (GX RGBA8: 4x4 tiles, each its 16 AR pairs then its 16 GB pairs) */
            const size_t tile = ((size_t)(y / 4) * (size_t)(texW / 4) + (size_t)(x / 4)) * 64;
            if (t[tile + (size_t)((y % 4) * 4 + x % 4) * 2] >= 128) {
                *x0 = x < *x0 ? x : *x0;
                *x1 = x > *x1 ? x : *x1;
                *y0 = y < *y0 ? y : *y0;
                *y1 = y > *y1 ? y : *y1;
                found = 1;
            }
        }
    }
    return found;
}

/* each field boss's head as last drawn: the top centre of its main row's visible pixels, in the world */
static Vec sFieldHead[PM64_FIELD_LOOKS];
static int sFieldHeadPose[PM64_FIELD_LOOKS] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; /* its pose */

/* row's visible pixels as it plays now (sprite pixels: x about its middle, y up from its feet): 0 none */
static int rowOpaque(int poseId, const Pm64FieldLook* l, int row, float* lo, float* hi, float* bottom, float* top) {
    const Pm64Anim* a = playOf(poseId, l->sprite, row);
    int k, any = 0;
    for (k = 0; a != NULL && k < a->count; k++) {
        const Pm64Part* p = &a->part[k];
        const void* data;
        int w, h, tw, th, x0, y0, x1, y1;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) ||
            (data = portPm64RasterTexture(a->sprite, p->raster, p->palette >= 0 ? p->palette : l->palette, &tw,
                                          &th)) == NULL ||
            !portPm64TexOpaqueBounds(data, tw, w, h, &x0, &y0, &x1, &y1)) {
            continue;
        }
        if (!any) {
            *lo = *bottom = 1e9f;
            *hi = *top = -1e9f;
            any = 1;
        }
        *top = fmaxf(*top, p->y + (float)(h - y0) * p->sy);
        *bottom = fminf(*bottom, p->y + (float)(h - 1 - y1) * p->sy);
        *lo = fminf(*lo, p->x + ((float)x0 - (float)(w / 2)) * p->sx);
        *hi = fmaxf(*hi, p->x + ((float)(x1 + 1) - (float)(w / 2)) * p->sx);
    }
    return any;
}

/* Each field boss's lift (sprite pixels) so no visible pixel of it, its parts drawn over it included, sits in the
 * floor (once some did: Huff N. Puff's cloud sank into the ground), and its width: measured once per pose and row
 * (the row's first frame seen) */
static struct {
    int pose, anim;
    float lift, lo, hi, top;
} sFieldFit[PM64_FIELD_LOOKS];

static float fieldFit(int i, int poseId, const Pm64FieldLook* l) {
    float lo, hi, bottom, top, low = 0.0f, wlo = 0.0f, whi = 0.0f, wtop = 0.0f;
    int n, any = 0;
    if (i < 0 || i >= PM64_FIELD_LOOKS) {
        return 0.0f;
    }
    if (sFieldFit[i].pose == poseId + 1 && sFieldFit[i].anim == l->anim) {
        return sFieldFit[i].lift;
    }
    for (n = -1; n < 2; n++) {
        const int row = n < 0 ? l->anim : l->layer[n];
        if (row < 0 || !rowOpaque(poseId, l, row, &lo, &hi, &bottom, &top)) {
            continue;
        }
        low = !any || bottom < low ? bottom : low;
        wlo = !any || lo < wlo ? lo : wlo;
        whi = !any || hi > whi ? hi : whi;
        wtop = !any || top > wtop ? top : wtop;
        any = 1;
    }
    if (!any) {
        return 0.0f; /* (not read yet: again next draw) */
    }
    sFieldFit[i].pose = poseId + 1;
    sFieldFit[i].anim = l->anim;
    sFieldFit[i].lift = l->vineA < 0 && low < 0.0f ? -low : 0.0f; /* (a vine's head hangs from its vine) */
    sFieldFit[i].lo = wlo;
    sFieldFit[i].hi = whi;
    sFieldFit[i].top = wtop;
    return sFieldFit[i].lift;
}

int portPm64FieldLookBounds(int i, float* halfWidth, float* bottom, float* top) {
    const Pm64FieldLook* l;
    float size;
    if (i < 0 || i >= PM64_FIELD_LOOKS || (l = &gPortPm64FieldLooks[i])->pose < 0 ||
        sFieldFit[i].pose != l->pose + 1) {
        return 0;
    }
    /* (as drawFieldBoss draws it: the NPC's scale 2, half a unit a pixel) */
    size = 2.0f * l->scale * pm64Factor();
    *halfWidth = (sFieldFit[i].hi - sFieldFit[i].lo) * 0.25f * size;
    *bottom = l->hover + (l->vineA >= 0 ? l->vine * 0.5f * size : 0.0f);
    *top = *bottom + (sFieldFit[i].top + sFieldFit[i].lift) * 0.5f * size;
    return 1;
}

static void fieldHeadNote(int i, int poseId, float (*head)[4], float size, const Pm64FieldLook* l) {
    const Pm64Anim* a = playOf(poseId, l->sprite, l->anim);
    float top = -1e9f, lo = 1e9f, hi = -1e9f;
    int k;
    Vec at;
    for (k = 0; a != NULL && k < a->count; k++) {
        const Pm64Part* p = &a->part[k];
        const void* data;
        int w, h, tw, th, x0, y0, x1, y1;
        float t;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) ||
            (data = portPm64RasterTexture(a->sprite, p->raster, p->palette >= 0 ? p->palette : l->palette, &tw,
                                          &th)) == NULL ||
            !portPm64TexOpaqueBounds(data, tw, w, h, &x0, &y0, &x1, &y1)) {
            continue;
        }
        /* (drawParts: the raster's top row at p->y + h * sy, its left column at p->x - w / 2 * sx) */
        t = p->y + (float)(h - y0) * p->sy;
        top = t > top ? t : top;
        lo = fminf(lo, p->x + ((float)x0 - (float)(w / 2)) * p->sx);
        hi = fmaxf(hi, p->x + ((float)(x1 + 1) - (float)(w / 2)) * p->sx);
    }
    if (top < -1e8f) {
        return;
    }
    at.x = (lo + hi) * 0.25f * size;
    at.y = top * 0.5f * size;
    at.z = 0.0f;
    PSMTXMultVec(head, &at, &sFieldHead[i]);
    sFieldHeadPose[i] = poseId;
}

int portPm64FieldLookHead(int i, float out[3]) {
    if (i < 0 || i >= PM64_FIELD_LOOKS || gPortPm64FieldLooks[i].pose < 0 ||
        sFieldHeadPose[i] != gPortPm64FieldLooks[i].pose) {
        return 0;
    }
    out[0] = sFieldHead[i].x;
    out[1] = sFieldHead[i].y;
    out[2] = sFieldHead[i].z;
    return 1;
}

/* a Paper Mario 64 boss standing in the field (port/include/port/pm64_media.h): as the battle's draw, a vine's
 * segments under its head and its parts over it */
static int drawFieldBoss(const Pm64FieldLook* l, int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    float head[3][4];
    int n, r;
    const float size = scale * l->scale * pm64Factor();
    float base[3][4];
    CoolSet cool = {0};
    {
        /* off the floor: its own float (Huff N. Puff's cloud, as high as in its battle), and no visible pixel
           under its feet */
        const int i = l >= gPortPm64FieldLooks && l < gPortPm64FieldLooks + PM64_FIELD_LOOKS
                          ? (int)(l - gPortPm64FieldLooks)
                          : -1;
        const float lift = l->hover + fieldFit(i, poseId, l) * 0.5f * size;
        if (lift > 0.0f) {
            liftMtx(mtx, base, lift);
            mtx = base;
        }
    }
    PSMTXCopy(mtx, head);
    if (l->vineA >= 0) {
        float at = 0.0f, bottom, h;
        int k;
        for (k = 0; k < 24 && at < l->vine; k++) {
            const int row = (k & 1) && l->vineB >= 0 ? l->vineB : l->vineA;
            float m[3][4];
            if (!rowExtent(poseId, l->sprite, row, &bottom, &h) || h < 1.0f) {
                break;
            }
            liftMtx(mtx, m, (at - bottom) * 0.5f * size);
            m[2][3] -= 0.2f;
            drawOnPose(poseId, m, xluMode, rotY, size, 1, l->sprite, l->palette, row, NULL);
            at += h;
        }
        liftMtx(mtx, head, l->vine * 0.5f * size);
    }
    r = drawOnPose(poseId, head, xluMode, rotY, size, 1, l->sprite, l->palette, l->anim, &cool);
    if (xluMode == 1 && !portPaperShadowPass() && l >= gPortPm64FieldLooks &&
        l < gPortPm64FieldLooks + PM64_FIELD_LOOKS) {
        fieldHeadNote((int)(l - gPortPm64FieldLooks), poseId, head, size, l);
    }
    for (n = 0; n < 2; n++) {
        if (l->layer[n] >= 0) {
            float m[3][4];
            PSMTXCopy(head, m);
            m[2][3] += 0.5f * (float)(n + 1);
            drawOnPose(poseId, m, xluMode, rotY, size, 1, l->sprite, l->palette, l->layer[n], &cool);
        }
    }
    coolFlush(poseId, &cool);
    return r;
}

/* tests (TTYD_PM64_LOG): each animation of TTYD Mario's the PM64 look plays, as the player sprite and its
 * animation it is drawn with (or TTYD's model, a paper form: sprite -1), his height as drawn and the frame it
 * started: "[pm64] mario look: M_H_1 (field) as player sprite 5 anim 16, 21.8 units tall (TTYD Mario 21.8, draw
 * scale 1.00) at frame 431" (pm64_mario_look_*; the review GIFs line PM64's own frames up by it). Each of his
 * poses apart (in battle the thrown hammer is a part of its own, drawn beside him). */
static void marioLookLog(int poseId, const char* name, int battle, int player, int anim, float scale) {
    static int sLog = -1, sLogs;
    static struct {
        int pose1, player, anim, battle;
        const char* name;
    } sLast[4];
    const unsigned frame = gp != NULL ? (unsigned)gp->retraceCount : 0u;
    float px;
    int i;
    if (sLog < 0) {
        const char* v = getenv("TTYD_PM64_LOG");
        sLog = v != NULL && v[0] != '\0' && v[0] != '0';
    }
    if (!sLog || sLogs >= 400) {
        return;
    }
    for (i = 0; i < 3 && sLast[i].pose1 != 0 && sLast[i].pose1 != poseId + 1; i++) {
    }
    if (sLast[i].pose1 == poseId + 1 && name == sLast[i].name && player == sLast[i].player &&
        anim == sLast[i].anim && battle == sLast[i].battle) {
        return;
    }
    sLogs++;
    sLast[i].pose1 = poseId + 1;
    sLast[i].name = name;
    sLast[i].player = player;
    sLast[i].anim = anim;
    sLast[i].battle = battle;
    if (player < 0) {
        printf("[pm64] mario look: %s (%s) as TTYD's model (a paper form) at frame %u\n", name != NULL ? name : "-",
               battle ? "battle" : "field", frame);
    } else {
        /* (his height: Mario1's idle in pixels, half a unit each, at PM64's size beside Mario; the pose's own
           draw scale over it, the menus' and the battle's, as for TTYD's model) */
        px = portPm64MarioHeightPx();
        printf("[pm64] mario look: %s (%s) as player sprite %d anim %d, %.1f units tall (TTYD Mario 21.8, draw scale "
               "%.2f) at frame %u\n",
               name != NULL ? name : "-", battle ? "battle" : "field", player, anim, 0.5f * px * pm64Factor(), scale,
               frame);
    }
    fflush(stdout);
}

/* how many frames pose poseId has played TTYD's animation name (from 0, its first frame drawn): where PM64's own
 * steps of a hammer move are (portPm64MarioLookAnim) */
#define LOOK_TIMES 8
static int lookFrames(int poseId, const char* name) {
    static struct {
        int pose1;
        char name[16];
        unsigned start, last;
    } sTimes[LOOK_TIMES];
    int i, use = 0;
    unsigned oldest = ~0u;
    if (name == NULL || gp == NULL) {
        return 0;
    }
    for (i = 0; i < LOOK_TIMES; i++) {
        if (sTimes[i].pose1 == poseId + 1) {
            use = i;
            break;
        }
        if (sTimes[i].last < oldest) {
            oldest = sTimes[i].last;
            use = i;
        }
    }
    if (sTimes[use].pose1 != poseId + 1 || strncmp(sTimes[use].name, name, sizeof(sTimes[use].name)) != 0 ||
        gp->retraceCount - sTimes[use].last > 2) {
        /* (another animation, or the pose not drawn for a while: started over) */
        sTimes[use].pose1 = poseId + 1;
        snprintf(sTimes[use].name, sizeof(sTimes[use].name), "%s", name);
        sTimes[use].start = gp->retraceCount;
    }
    sTimes[use].last = gp->retraceCount;
    return (int)(gp->retraceCount - sTimes[use].start);
}

/* Hammer Throw's hammer with Mario's PM64 look (battle_mario.c HammerNageru_object): a part of Mario's own of its
 * own (3), TTYD's hammer by itself (M_H_4 / 8 / 12 by the hammer's level) thrown and turned about its middle, 12
 * units up (HammerRotate): drawn as PM64's thrown hammer, MarioB2's Hammer<L> (its middle at its point), there. 1
 * if it was. */
static int drawThrownHammer(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    BattleWork* wp = _battleWorkPointer;
    const BattleUnit* unit;
    const BattleUnitPart* part;
    const AnimationPose* pose;
    const AnimationPoseData* data;
    const char* name;
    int level, anim, player;
    Mtx m, t;
    if (wp == NULL || gp == NULL || !gp->inBattle || (unit = BattleGetMarioPtr(wp)) == NULL ||
        unit->parts == NULL || portPm64PlayerSprite(PM64_PLAYER_MARIOB2) == NULL) {
        return 0;
    }
    for (part = unit->parts->nextPart; part != NULL && part->poseId != poseId; part = part->nextPart) {
    }
    if (part == NULL || (pose = animPoseGetAnimPosePtr(poseId)) == NULL ||
        (data = animPoseGetAnimBaseDataPtr(poseId)) == NULL || pose->animId < 0 || pose->animId >= data->animCount) {
        return 0;
    }
    name = data->anims[pose->animId].name;
    level = strcmp(name, "M_H_4") == 0 ? 1 : strcmp(name, "M_H_8") == 0 ? 2 : strcmp(name, "M_H_12") == 0 ? 3 : 0;
    if (level == 0) {
        return 0;
    }
    anim = portPm64MarioLookAnim(name, 1, 0, &player);
    marioLookLog(poseId, name, 1, player, anim, scale);
    PSMTXTrans(t, 0.0f, 12.0f, 0.0f);
    PSMTXConcat(mtx, t, m);
    return drawOnPose(poseId, m, xluMode, rotY, scale * pm64Factor(), 1, -1 - player, -1, anim, NULL);
}

int portPm64MediaDrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    AnimationPose* pose;
    AnimationPoseData* data;
    const char* name = NULL;
    int anim, mario, i, sprite;
    if (poseId < 0) {
        return 0;
    }
    for (i = 0; gp != NULL && !gp->inBattle && i < PM64_FIELD_LOOKS; i++) {
        if (gPortPm64FieldLooks[i].pose == poseId) {
            /* (one that can't be drawn as itself now, the ROM unlinked or its look not read: nothing, never the
               Toad underneath, until it is hidden or gone at the next frame's start) */
            if (portPm64Linked() != PM64_NONE && gPortPm64FieldLooks[i].sprite >= 0) {
                drawFieldBoss(&gPortPm64FieldLooks[i], poseId, mtx, xluMode, rotY, scale);
            }
            return 1;
        }
    }
    if (poseId == gPortPm64RowfPose && portPm64Linked() == PM64_NONE) {
        return 1; /* (Rowf as the ROM is unlinked: nothing, until he is hidden or gone) */
    }
    if (portPm64Linked() == PM64_NONE) {
        return 0;
    }
    mario = poseId != gPortPm64RowfPose && portPm64MarioLookOn() && portCostumeMarioPose(poseId);
    if (poseId != gPortPm64RowfPose && !mario) {
        return portPm64MarioLookOn() ? drawThrownHammer(poseId, mtx, xluMode, rotY, scale) : 0;
    }
    pose = animPoseGetAnimPosePtr(poseId);
    if (pose == NULL || (data = animPoseGetAnimBaseDataPtr(poseId)) == NULL) {
        return 0;
    }
    if (pose->animId >= 0 && pose->animId < data->animCount) {
        name = data->anims[pose->animId].name;
    }
    if (mario) {
        /* Mario's Paper Mario 64 look: the player's sprites (Mario1 front; the hammer's swings MarioW1's, MarioB1's,
           MarioB2's: portPm64MarioLookAnim), at PM64's size beside Mario (pm64Factor) as its partners, enemies and
           bosses are drawn: TTYD Mario's own height, and PM64's proportions to all of them. A paper form (the plane,
           boat, tube, paper-thin side, the Spring Jump's accordion, a pipe, a paper fold in battle: marioPaperOn,
           pose->paperId) is TTYD's model folded, which PM64 has no frames of: drawn as TTYD's (not his PM64 idle
           standing where a plane or boat should be); the Spin Hammer's spin (p_kaiten_h) stays his PM64 spin. */
        const int battle = gp->inBattle != 0;
        int player;
        if (portPm64PlayerSprite(PM64_PLAYER_MARIO1) == NULL) {
            return 0;
        }
        anim = portPm64MarioLookAnim(name, battle, lookFrames(poseId, name), &player);
        if (!battle && name != NULL && portSpinDashLookAnim() >= 0 && strcmp(name, portSpinDashPoseName()) == 0) {
            /* the Spin Dash: PM64's own spin from its start (src/world/action/spin.c), shaded, with its afterimage */
            anim = portSpinDashLookAnim();
            player = PM64_PLAYER_MARIO1;
            sSpinLook.on = portSpinDashLookFacing(&sSpinLook.now, &sSpinLook.before);
        }
        if (pose->paperId != -1 && player == PM64_PLAYER_MARIO1) {
            sSpinLook.on = 0;
            marioLookLog(poseId, name, battle, -1, -1, scale);
            return 0;
        }
        marioLookLog(poseId, name, battle, player, anim, scale);
        sprite = -1 - player;
        scale *= pm64Factor();
    } else {
        /* Rowf (the NPC sprite archive's 166): Idle 1, Walk 2, Talk 4; PM64's size beside Mario, as the rest */
        anim = name != NULL && name[0] == 'T' ? 4 : name != NULL && (name[0] == 'W' || name[0] == 'R') ? 2 : 1;
        sprite = PM64_SPR_ROWF;
        scale *= pm64Factor();
    }
    {
        CoolSet cool = {0};
        const int r = drawOnPose(poseId, mtx, xluMode, rotY, scale, 1, sprite, -1, anim, &cool);
        sSpinLook.on = 0;
        coolFlush(poseId, &cool);
        return r;
    }
}

/* --- the check: a Paper Mario 64 look never shows the host model underneath (pm64_media.h) ---
 * Each pose drawn as Paper Mario 64 (a partner, an enemy, a field boss, Rowf, Mario's look, a ghost's partner) is
 * noted with the frame, the map and the battle-or-field stretch it was drawn in; a host model drawn for a pose
 * that was Paper Mario 64 within the last 2 seconds of the same stretch, or for one of Paper Cut's own PM64 NPCs
 * (pc_pm64_*, which have no look of their own), is logged: "[pm64] host model shown: ...". The playtest's "the
 * 64 bosses briefly turn into Toads before the fight loads" (pm64_host_model scenarios). */
#define HOST_POSES 1024
#define HOST_WINDOW 120
typedef struct HostNote {
    unsigned at, stretch; /* retraceCount, sStretch (0: never) */
    char map[16];
} HostNote;
static HostNote sHostNote[HOST_POSES];
static unsigned sStretch;
static int sStretchBattle = -1, sHostLogs;

static void hostStretch(void) {
    if ((int)gp->inBattle != sStretchBattle) { /* (a battle's start or end: its poses all made again) */
        sStretchBattle = gp->inBattle;
        sStretch++;
    }
}

void portPm64NoteDrawn(int poseId) {
    HostNote* n;
    if (poseId < 0 || poseId >= HOST_POSES || gp == NULL) {
        return;
    }
    hostStretch();
    n = &sHostNote[poseId];
    n->at = gp->retraceCount;
    n->stretch = sStretch;
    snprintf(n->map, sizeof(n->map), "%s", gp->currentMapName);
}

void portPm64NoteReleased(int poseId) {
    if (poseId >= 0 && poseId < HOST_POSES) {
        sHostNote[poseId].stretch = 0;
    }
}

/* the field NPC whose pose this is */
static const NpcEntry* fieldNpcOfPose(int poseId) {
    NpcWork* wp = gp->inBattle ? NULL : npcGetWorkPtr();
    int i;
    for (i = 0; wp != NULL && wp->entries != NULL && i < wp->total; i++) {
        const NpcEntry* e = &wp->entries[i];
        if ((e->flags & NPC_FLAG_USED) && e->poseId == poseId) {
            return e;
        }
    }
    return NULL;
}

void portPm64HostDrawCheck(int poseId, int xluMode) {
    const NpcEntry* npc;
    const HostNote* n;
    const char* why = NULL;
    AnimationPoseData* data;
    if (poseId < 0 || gp == NULL || xluMode != 1 || portPaperShadowPass()) {
        return;
    }
    hostStretch();
    if (portPm64MarioLookOn() && portCostumeMarioPose(poseId) && animPoseGetAnimPosePtr(poseId) != NULL &&
        animPoseGetAnimPosePtr(poseId)->paperId != -1) {
        return; /* (Mario's look in a paper form: TTYD's model on purpose, portPm64MediaDrawPose) */
    }
    npc = fieldNpcOfPose(poseId);
    n = poseId < HOST_POSES ? &sHostNote[poseId] : NULL;
    if (npc != NULL && strncmp(npc->description, "pc_pm64_", 8) == 0) {
        why = "a Paper Mario 64 NPC of Paper Cut's own";
    } else if (n != NULL && n->stretch == sStretch && gp->retraceCount - n->at < HOST_WINDOW &&
               strcmp(n->map, gp->currentMapName) == 0) {
        why = "drawn as Paper Mario 64 just before";
    }
    if (why == NULL || sHostLogs >= 24) {
        return;
    }
    sHostLogs++;
    data = animPoseGetAnimBaseDataPtr(poseId);
    printf("[pm64] host model shown: pose %d (%s%s%s), %s, in %s (seq %d%s%s, in battle %d, %u frames after its "
           "Paper Mario 64 draw)\n",
           poseId, data != NULL ? data->fileName : "?", npc != NULL ? ", NPC " : "", npc != NULL ? npc->description : "",
           why, gp->currentMapName, (int)seqGetSeq(), seqCheckSeq() ? " -> " : "",
           seqCheckSeq() ? (seqGetNextSeq() == SEQ_BATTLE ? "battle" : "another") : "", (int)gp->inBattle,
           n != NULL && n->stretch != 0 ? gp->retraceCount - n->at : 0u);
    fflush(stdout);
}
