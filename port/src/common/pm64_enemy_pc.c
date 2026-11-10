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

/* More of each sprite's animations, by the NPC sprite archive index (-1: none, the nearest kept): run,
 * jump, hit while burning, burnt, dizzy, asleep, attacking, in its shell, flipped over. By the animations'
 * names in the PM64 decomp's sprite list (tools/splat_ext/npc_sprite_names.yaml: Run, Midair, BurnHurt,
 * BurnStill, Dizzy, Sleep, the attack's own (Dive, ShellSpin, Leech, Tackle, ThrowBone, CastSpell, Throw ...),
 * ShellStill, ToppleStruggle). */
enum { ROW_RUN, ROW_JUMP, ROW_BURN_HIT, ROW_BURNT, ROW_DIZZY, ROW_SLEEP, ROW_ATTACK, ROW_SHELL, ROW_FLIPPED, ROWS };
static const struct {
    short sprite;
    signed char row[ROWS];
} kRows[] = {
    {37, {3, 4, 9, 10, 7, 8, 4, -1, -1}},       /* Goomba (and Hyper Goomba, Gloomba) */
    {38, {3, 4, -1, -1, 9, 10, 4, -1, -1}},     /* Spiked Goomba */
    {39, {4, -1, 13, 14, 12, 11, 5, -1, -1}},   /* Paragoomba: Dive */
    {40, {4, -1, 12, 13, -1, 17, 6, 5, 15}},    /* Koopa Troopa, Dark Koopa: ShellSpin */
    {41, {3, -1, 8, 9, -1, 18, 12, 10, -1}},    /* Paratroopa: ShellDive */
    {42, {3, 5, 16, 18, -1, 14, 4, -1, -1}},    /* Fuzzy (Forest, Jungle): Leech */
    {43, {6, -1, 16, 17, 28, 24, 3, -1, -1}},   /* Bob-omb: lit as it attacks */
    {46, {3, -1, 10, 11, -1, 20, 4, -1, -1}},   /* Monty Mole: ThrowAttack */
    {49, {7, -1, 10, 11, 17, 16, 13, -1, -1}},  /* Bandit: Tackle */
    {50, {4, -1, 11, -1, 10, 9, 5, 6, 28}},     /* Buzzy Beetle */
    {51, {-1, -1, 23, 24, 20, 19, 6, -1, -1}},  /* Swooper: Fly */
    {52, {-1, -1, 7, 8, 11, 10, 4, -1, -1}},    /* Stone Chomp: Bite */
    {53, {3, -1, 16, 17, 15, -1, 6, -1, -1}},   /* Putrid and Frost Piranha: Bite */
    {71, {3, -1, 9, 10, 11, 12, 7, -1, -1}},    /* Dayzees: SingFast */
    {72, {-1, -1, 5, 6, 9, 8, 11, -1, -1}},     /* Lakitu: a Spiny held up */
    {73, {4, -1, 11, 12, 18, 16, 8, 5, 2}},     /* Spiny */
    {76, {8, -1, 22, 23, 30, 27, 10, 15, 5}},   /* Spike Top */
    {82, {4, -1, 11, 12, 13, 14, 6, -1, -1}},   /* Dry Bones: ThrowBone */
    {84, {14, -1, 30, 31, 38, 42, 22, 20, 6}},  /* Bony Beetle */
    {85, {-1, -1, 5, 6, 7, 8, 3, -1, -1}},      /* Magikoopa: CastSpell */
    {88, {4, -1, 12, 13, 19, 17, 6, 5, 15}},    /* Koopatrol */
    {89, {7, -1, 15, 16, 18, 17, 12, 22, -1}},  /* Hammer Bro: Throw */
    {124, {4, -1, 7, 8, 11, 9, 2, -1, -1}},     /* Ruff Puff: Angry */
};

static int rowOf(const Pm64EnemyInfo* e, int which, int otherwise) {
    size_t k;
    for (k = 0; k < sizeof(kRows) / sizeof(kRows[0]); k++) {
        if (kRows[k].sprite == e->sprite) {
            return kRows[k].row[which] >= 0 ? kRows[k].row[which] : otherwise;
        }
    }
    return otherwise;
}

/* the host's pose name ("S_1", "W_1", "D_1", "NKT_K_2" ...: TTYD's letters: S stay, W walk, R run, J jump,
 * F fall, D hurt, A attack, K in the shell, Y flipped, Q dizzy, N asleep, X down) and the unit's statuses (NULL
 * in the field) to an animation */
static int animOf(const Pm64EnemyInfo* e, const char* pose, const BattleUnit* unit) {
    const char* u = pose != NULL ? strrchr(pose, '_') : NULL; /* (the letter before the number) */
    const char c = u != NULL && u > pose ? u[-1] : pose != NULL ? pose[0] : 'S';
    const int burnt = unit != NULL && unit->burnTurns > 0;
    switch (c) {
    case 'W':
        return e->walk;
    case 'R':
        return rowOf(e, ROW_RUN, e->walk);
    case 'J':
    case 'F':
        return rowOf(e, ROW_JUMP, e->idle);
    case 'D':
    case 'X':
        return burnt ? rowOf(e, ROW_BURN_HIT, e->hurt) : e->hurt;
    case 'A':
    case 'I':
        return rowOf(e, ROW_ATTACK, e->idle);
    case 'K':
        return rowOf(e, ROW_SHELL, rowOf(e, ROW_ATTACK, e->idle));
    case 'Y':
        return rowOf(e, ROW_FLIPPED, e->hurt);
    case 'Q':
        return rowOf(e, ROW_DIZZY, e->idle);
    case 'N':
        return rowOf(e, ROW_SLEEP, e->idle);
    default:
        if (unit != NULL && unit->sleepTurns > 0) {
            return rowOf(e, ROW_SLEEP, e->idle);
        }
        if (unit != NULL && unit->dizzyTurns > 0) {
            return rowOf(e, ROW_DIZZY, e->idle);
        }
        return burnt ? rowOf(e, ROW_BURNT, e->idle) : e->idle;
    }
}

/* a draw in another colour (sTintOn: the swarm's dark hearts), or added on top (sGlowOn: a charged glow) */
static int sTintOn, sGlowOn, sGlowNext;
static GXColor sTint;

/* Cool Mode: the glasses on the eyes the frame's rasters show (the surest part's; none if no part shows
 * any), over that part's box as drawParts draws it */
static void coolParts(const Pm64Anim* a, Mtx m, GXColor tint) {
    float best = 0.0f, at[3] = {0.0f, 0.0f, 0.0f}, bx0 = 0.0f, bx1 = 0.0f, by0 = 0.0f, by1 = 0.0f;
    int i;
    for (i = 0; i < a->count; i++) {
        const Pm64Part* p = &a->part[i];
        float ex, ey, ew, score;
        int w, h;
        if (p->raster < 0 || !portPm64SpriteRaster(a->sprite, p->raster, &w, &h, NULL) || w <= 0 || h <= 0 ||
            !portPm64SpriteEyes(a->sprite, p->raster, &ex, &ey, &ew, &score) || score <= best) {
            continue;
        }
        best = score;
        bx0 = (p->x - (float)(w / 2) * p->sx) * 0.5f;
        bx1 = (p->x + (float)(w - w / 2) * p->sx) * 0.5f;
        by0 = p->y * 0.5f;
        by1 = (p->y + (float)h * p->sy) * 0.5f;
        at[0] = ex;
        at[1] = 1.0f - ey;
        at[2] = ew;
    }
    if (best > 0.0f) {
        portFunCoolSprite(m, bx0, bx1, by0, by1, at, &tint);
    }
}

static void drawParts(const Pm64Anim* a, Mtx m, const AnimationPose* pose, int palette) {
    CameraEntry* cam = camGetCurPtr();
    Mtx mv, nrm;
    Vec at = {0.0f, 0.0f, 0.0f};
    GXColor tint = {0xFF, 0xFF, 0xFF, 0xFF};
    int i;
    if (cam == NULL || a->sprite == NULL) {
        return;
    }
    if (pose->materialFlag & ANIM_MATERIAL_FLAG_EVT_COLOR) {
        tint = pose->materialColor;
    }
    if (pose->materialColor2.r != 0xFF) {
        tint.a = (u8)(tint.a * pose->materialColor2.r / 255);
    }
    if (sTintOn || sGlowOn) {
        tint = sTint;
    }
    PSMTXMultVec(m, &at, &at);
    mapSetMaterialLight(pose->materialLightFlag | ANIM_MATERIAL_LIGHT_NO_VTX_COLOR, at);
    PSMTXConcat(cam->view, m, mv);
    GXLoadPosMtxImm(mv, GX_PNMTX0);
    PSMTXInvXpose(mv, nrm);
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
        x0 = (p->x - (float)(w / 2) * p->sx) * 0.5f;
        x1 = (p->x + (float)(w - w / 2) * p->sx) * 0.5f;
        y0 = p->y * 0.5f;
        y1 = (p->y + (float)h * p->sy) * 0.5f;
        z = p->z * 0.05f + (float)i * 0.02f;
        u = (float)w / (float)tw;
        v = (float)h / (float)th;
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(x0, y1, z);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXTexCoord2f32(0.0f, 0.0f);
        GXPosition3f32(x1, y1, z);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXTexCoord2f32(u, 0.0f);
        GXPosition3f32(x1, y0, z);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXTexCoord2f32(u, v);
        GXPosition3f32(x0, y0, z);
        GXNormal3f32(0.0f, 0.0f, 1.0f);
        GXTexCoord2f32(0.0f, v);
        GXEnd();
    }
    if (portFunOn(PORT_FUN_COOL)) {
        coolParts(a, m, tint);
    }
}

/* pose poseId drawn as PM64 sprite sprite playing anim (main: this pose draws it; the others of its
 * unit draw nothing); 1 if it was */
static int drawOnPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale, int main, int sprite,
                      int palette, int anim) {
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
    drawParts(playOf(poseId, sprite, anim), m, pose, palette);
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
 * with Mario's own PM64 look on, he is PM64's size and they stay at theirs */
static float pm64Factor(void) {
    return portPm64MarioLookOn() ? 1.0f : portPm64Size();
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
        e = &kEnemies[idx];
        return drawOnPose(poseId, mtx, xluMode, rotY, scale * pm64Factor(), 1, e->sprite, e->palette,
                          animOf(e, poseName, NULL));
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
                    drawOnPose(poseId, m, xluMode, rotY, scale * size, 1, sprite, palette, row);
                    at += h;
                }
                liftMtx(mtx, head, vine * 0.5f * scale * size);
            }
            sGlowNext = glow;
            r = drawOnPose(poseId, head, xluMode, rotY, scale * size, 1, sprite, palette, anim);
            {
                /* a sprite in parts (Huff N. Puff's face and arms over his body), each a hair nearer */
                int n, layer;
                for (n = 0; n < 2; n++) {
                    if (portPm64BossLayer(u, anim, n, &layer)) {
                        float m[3][4];
                        PSMTXCopy(head, m);
                        m[2][3] += 0.5f * (float)(n + 1);
                        drawOnPose(poseId, m, xluMode, rotY, scale * size, 1, sprite, palette, layer);
                    }
                }
            }
            sGlowNext = 0;
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
    return drawOnPose(poseId, mtx, xluMode, rotY, scale * pm64Factor(), main, e->sprite, e->palette,
                      animOf(e, poseName, unit));
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
    for (i = 0; i < n; i++) {
        PSMTXTrans(m, 0.0f, draw[i].lift, draw[i].z);
        drawParts(&sCard[i], m, pose, palette);
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
            drawOnPose(poseId, m, xluMode, rotY, size, 1, l->sprite, l->palette, row);
            at += h;
        }
        liftMtx(mtx, head, l->vine * 0.5f * size);
    }
    r = drawOnPose(poseId, head, xluMode, rotY, size, 1, l->sprite, l->palette, l->anim);
    if (xluMode == 1 && !portPaperShadowPass() && l >= gPortPm64FieldLooks &&
        l < gPortPm64FieldLooks + PM64_FIELD_LOOKS) {
        fieldHeadNote((int)(l - gPortPm64FieldLooks), poseId, head, size, l);
    }
    for (n = 0; n < 2; n++) {
        if (l->layer[n] >= 0) {
            float m[3][4];
            PSMTXCopy(head, m);
            m[2][3] += 0.5f * (float)(n + 1);
            drawOnPose(poseId, m, xluMode, rotY, size, 1, l->sprite, l->palette, l->layer[n]);
        }
    }
    return r;
}

int portPm64MediaDrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale) {
    AnimationPose* pose;
    AnimationPoseData* data;
    const char* name = NULL;
    int anim, mario, i;
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
        return 0;
    }
    pose = animPoseGetAnimPosePtr(poseId);
    if (pose == NULL || (data = animPoseGetAnimBaseDataPtr(poseId)) == NULL) {
        return 0;
    }
    if (pose->animId >= 0 && pose->animId < data->animCount) {
        name = data->anims[pose->animId].name;
    }
    if (mario) {
        /* Mario's Paper Mario 64 look: the player's sprite Mario1 (front), at the NPCs' scale: Paper Mario's own
           sizes beside its partners and enemies, and about TTYD Mario's height (half again as big, he stood a
           head taller than the TTYD Mario he stands for) */
        if (portPm64PlayerSprite(0) == NULL) {
            return 0;
        }
        return drawOnPose(poseId, mtx, xluMode, rotY, scale, 1, -1, -1, portPm64MarioAnim(name));
    }
    /* Rowf (the NPC sprite archive's 166): Idle 1, Walk 2, Talk 4 */
    anim = name != NULL && name[0] == 'T' ? 4 : name != NULL && (name[0] == 'W' || name[0] == 'R') ? 2 : 1;
    return drawOnPose(poseId, mtx, xluMode, rotY, scale, 1, PM64_SPR_ROWF, -1, anim);
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
