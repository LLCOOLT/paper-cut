/* Enemy variants (port/include/port/enemy_variant.h): their codes, and the
 * battle side, which changes each enemy of an armed variant's kind as it
 * enters. The armed list and the units' marks are battle state: this file's
 * data is in the save states (CMakeLists.txt's snapshot list), so a rewind
 * or an online resync in a battle keeps them. */
#include "port/enemy_variant.h"
#include "port/battlex.h"
#include "port/code64.h"
#include "port/pm64_enemy.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "battle/battle_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char kPrefix[] = "PCFOE1:";

static const char* const kMoveNames[PORT_VARIANT_MOVE_COUNT] = {
    "None",
    "Burn (like a Lava Bubble's flame)",
    "Freeze (like a Frost Piranha's breath)",
    "Poison (like a Poison Pokey's needles)",
    "Electrify (like a Dark Puff's charge)",
    "Sleep (like a Crazee Dayzee's song)",
    "Dizzy",
    "Confuse",
    "Shrink (like a Magikoopa's spell)",
    "Soften (defence down)",
    "Immobilize",
    "Drain (heals by the damage done)",
};

void portEnemyVariantInit(PortEnemyVariant* v, int baseKind) {
    memset(v, 0, sizeof(*v));
    v->baseKind = baseKind;
    v->hpPercent = 100;
    v->color = 0xFFFFFFFFu;
    v->sizePercent = 100;
}

const char* portEnemyVariantMoveName(int move) {
    return move >= 0 && move < PORT_VARIANT_MOVE_COUNT ? kMoveNames[move] : kMoveNames[0];
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* --- codes --- */

/* text with ';' '=' '%' escaped as %XX */
static int putText(char* out, int at, int size, const char* key, const char* text) {
    int n = snprintf(out + at, (size_t)(size - at), ";%s=", key);
    if (n < 0 || at + n >= size) {
        return -1;
    }
    at += n;
    for (; *text; text++) {
        const unsigned char c = (unsigned char)*text;
        if (c == ';' || c == '=' || c == '%' || c < 0x20) {
            if (at + 3 >= size) {
                return -1;
            }
            snprintf(out + at, 4, "%%%02X", c);
            at += 3;
        } else {
            if (at + 1 >= size) {
                return -1;
            }
            out[at++] = (char)c;
        }
    }
    out[at] = 0;
    return at;
}

/* a name or tattle from a code: as text only. '<' and '>' (the game's message tags: a shared code's
   "<wait 9999>" or "<o>" would run where the name is shown) and control characters are left out */
static void getText(const char* in, int n, char* out, int size) {
    int i, len = 0;
    for (i = 0; i < n && len + 1 < size; i++) {
        char c;
        if (in[i] == '%' && i + 2 < n) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            c = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            c = in[i];
        }
        if (c == '<' || c == '>' || ((unsigned char)c < 0x20 && c != '\n' && c != '\0')) {
            continue;
        }
        out[len++] = c;
    }
    out[len] = 0;
}

int portEnemyVariantCode(const PortEnemyVariant* v, char* out, int size) {
    char body[1024];
    int at;
    const int pre = (int)sizeof(kPrefix) - 1;
    at = snprintf(body, sizeof(body), "k=%d;h=%d;p=%d;a=%d;d=%d;c=%08X;s=%d;m=%d;x=%d", v->baseKind, v->hpPercent,
                  v->hpAdd, v->attack, v->defense, v->color, v->sizePercent, v->move, v->coins);
    if (at < 0 || at >= (int)sizeof(body)) {
        return 0;
    }
    if (v->pm64 > 0) { /* a Paper Mario 64 enemy (port/include/port/pm64_enemy.h) */
        const int n = snprintf(body + at, sizeof(body) - (size_t)at, ";g=%d", v->pm64);
        if (n < 0 || at + n >= (int)sizeof(body)) {
            return 0;
        }
        at += n;
    }
    if (v->name[0] != 0 && (at = putText(body, at, (int)sizeof(body), "n", v->name)) < 0) {
        return 0;
    }
    if (v->tattle[0] != 0 && (at = putText(body, at, (int)sizeof(body), "t", v->tattle)) < 0) {
        return 0;
    }
    if (size <= pre) {
        return 0;
    }
    memcpy(out, kPrefix, (size_t)pre);
    at = portCode64Encode((const unsigned char*)body, at, out + pre, size - pre);
    return at < 0 ? 0 : pre + at;
}

int portEnemyVariantParse(const char* code, PortEnemyVariant* out) {
    unsigned char body[1024];
    const char* start = code != NULL ? strstr(code, kPrefix) : NULL;
    const char* p;
    int n, haveKind = 0;
    if (start == NULL || (n = portCode64Decode(start + sizeof(kPrefix) - 1, body, (int)sizeof(body))) <= 0) {
        return 0;
    }
    portEnemyVariantInit(out, 0);
    p = (const char*)body;
    while (*p) {
        const char* end = strchr(p, ';');
        const char* eq = strchr(p, '=');
        const int len = end != NULL ? (int)(end - p) : (int)strlen(p);
        if (eq == NULL || eq - p >= len || eq - p != 1) {
            return 0;
        }
        {
            const char key = p[0];
            const char* val = eq + 1;
            const int vlen = len - 2;
            char num[16];
            snprintf(num, sizeof(num), "%.*s", vlen < 15 ? vlen : 15, val);
            switch (key) {
                case 'k': /* (an enemy's: never Mario's or a partner's) */
                    out->baseKind = atoi(num);
                    haveKind = out->baseKind >= 1 && out->baseKind < UNIT_MARIO;
                    break;
                case 'h': out->hpPercent = clampi(atoi(num), 10, 1000); break;
                case 'p': out->hpAdd = clampi(atoi(num), -99, 999); break;
                case 'a': out->attack = clampi(atoi(num), -9, 20); break;
                case 'd': out->defense = clampi(atoi(num), -9, 20); break;
                case 'c': out->color = (unsigned)strtoul(num, NULL, 16); break;
                case 's': out->sizePercent = clampi(atoi(num), 25, 300); break;
                case 'm': out->move = clampi(atoi(num), 0, PORT_VARIANT_MOVE_COUNT - 1); break;
                case 'x': out->coins = clampi(atoi(num), 0, 99); break;
                case 'n': getText(val, vlen, out->name, (int)sizeof(out->name)); break;
                case 't': getText(val, vlen, out->tattle, (int)sizeof(out->tattle)); break;
                case 'g': out->pm64 = clampi(atoi(num), 0, 255); break;
                default: break; /* a key a later version adds */
            }
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return haveKind;
}

/* --- in battle --- */

static PortEnemyVariant sArmed[PORT_VARIANT_ARMED_MAX];
static int sArmedCount;
static signed char sUnitVariant[BATTLE_UNIT_MAX]; /* by unitId: armed index + 1 */
static int sSpawnedCoins;
static int sTextsOn;

void portEnemyVariantArm(const PortEnemyVariant* list, int count) {
    count = clampi(count, 0, PORT_VARIANT_ARMED_MAX);
    if (count > 0) {
        memcpy(sArmed, list, sizeof(PortEnemyVariant) * (size_t)count);
    }
    sArmedCount = count;
    memset(sUnitVariant, 0, sizeof(sUnitVariant));
    sSpawnedCoins = 0;
}

int portEnemyVariantArmed(const PortEnemyVariant** list) {
    *list = sArmed;
    return sArmedCount;
}

void portEnemyVariantSpawn(BattleUnit* unit) {
    const PortEnemyVariant* v = NULL;
    BattleUnitPart* part;
    int i, hp;
    if (unit == NULL || sArmedCount == 0 || unit->unitId < 0 || unit->unitId >= BATTLE_UNIT_MAX ||
        unit->alliance == 0) {
        return;
    }
    for (i = 0; i < sArmedCount; i++) {
        if ((int)unit->currentKind == sArmed[i].baseKind) {
            v = &sArmed[i];
            break;
        }
    }
    if (v == NULL) {
        return;
    }
    sUnitVariant[unit->unitId] = (signed char)(i + 1);
    hp = v->pm64 > 0 && portPm64EnemyHp(v->pm64) > 0 ? portPm64EnemyHp(v->pm64) : unit->maxHp; /* (PM64's own) */
    hp = hp * v->hpPercent / 100 + v->hpAdd;
    hp = clampi(hp, 1, 9999);
    unit->maxHp = unit->baseMaxHp = unit->currentHp = (s16)hp;
    if (v->color != 0xFFFFFFFFu) {
        for (part = unit->parts; part != NULL; part = part->nextPart) {
            part->color.r = (u8)(v->color >> 24);
            part->color.g = (u8)(v->color >> 16);
            part->color.b = (u8)(v->color >> 8);
            part->color.a = (u8)v->color;
        }
    }
    if (v->sizePercent != 100) {
        const f32 s = (f32)v->sizePercent / 100.0f;
        unit->baseScale.x *= s;
        unit->baseScale.y *= s;
        unit->baseScale.z *= s;
        unit->width = (s16)(unit->width * s);
        unit->height = (s16)(unit->height * s);
    }
    sSpawnedCoins += v->coins;
    if (!sTextsOn && (v->name[0] != 0 || v->tattle[0] != 0)) {
        sTextsOn = 1;
    }
    portBattlexVariantTexts(v->baseKind, v->name, v->tattle);
}

const PortEnemyVariant* portEnemyVariantOf(const BattleUnit* unit) {
    int i;
    if (unit == NULL || unit->unitId < 0 || unit->unitId >= BATTLE_UNIT_MAX) {
        return NULL;
    }
    i = sUnitVariant[unit->unitId];
    return i > 0 && i <= sArmedCount ? &sArmed[i - 1] : NULL;
}

int portEnemyVariantDamage(const BattleUnit* attacker, const BattleUnit* target, int damage) {
    const PortEnemyVariant* a = portEnemyVariantOf(attacker);
    const PortEnemyVariant* t = portEnemyVariantOf(target);
    if (a == NULL && t == NULL) {
        return damage;
    }
    if (a != NULL && damage > 0) {
        damage += a->attack;
    }
    if (t != NULL) {
        damage -= t->defense;
    }
    if (damage < 0) {
        damage = 0;
    }
    if (a != NULL && a->move == PORT_VARIANT_MOVE_DRAIN && damage > 0) {
        BattleUnit* self = (BattleUnit*)attacker;
        self->currentHp = (s16)(self->currentHp + damage > self->maxHp ? self->maxHp : self->currentHp + damage);
    }
    return damage;
}

BattleWeapon* portEnemyVariantWeapon(const BattleUnit* attacker, BattleWeapon* weapon) {
    static BattleWeapon sCopies[4];
    static int sNext;
    const PortEnemyVariant* a = portEnemyVariantOf(attacker);
    BattleWeapon* w;
    if (a == NULL || weapon == NULL || a->move == PORT_VARIANT_MOVE_NONE || a->move == PORT_VARIANT_MOVE_DRAIN ||
        weapon->hpDamageFn == NULL) {
        return weapon;
    }
    w = &sCopies[sNext];
    sNext = (sNext + 1) & 3;
    *w = *weapon;
    switch (a->move) {
        case PORT_VARIANT_MOVE_BURN: w->burnChance = 60; w->burnTime = 3; break;
        case PORT_VARIANT_MOVE_FREEZE: w->freezeChance = 40; w->freezeTime = 2; break;
        case PORT_VARIANT_MOVE_POISON: w->poisonChance = 70; w->poisonTime = 3; w->poisonStrength = 1; break;
        case PORT_VARIANT_MOVE_ELECTRIC: w->electricChance = 50; w->electricTime = 3; break;
        case PORT_VARIANT_MOVE_SLEEP: w->sleepChance = 50; w->sleepTime = 2; break;
        case PORT_VARIANT_MOVE_DIZZY: w->dizzyChance = 50; w->dizzyTime = 2; break;
        case PORT_VARIANT_MOVE_CONFUSE: w->confuseChance = 50; w->confuseTime = 2; break;
        case PORT_VARIANT_MOVE_TINY:
            w->sizeChangeChance = 60; w->sizeChangeTime = 3; w->sizeChangeStrength = -2;
            break;
        case PORT_VARIANT_MOVE_SOFT:
            w->defChangeChance = 60; w->defChangeTime = 3; w->defChangeStrength = -2;
            break;
        case PORT_VARIANT_MOVE_STOP: w->stopChance = 40; w->stopTime = 2; break;
        default: break;
    }
    return w;
}

int portEnemyVariantCoins(void) {
    return sArmedCount > 0 ? sSpawnedCoins : 0;
}

void portEnemyVariantBattleEnd(void) {
    int i;
    if (sTextsOn) {
        for (i = 0; i < sArmedCount; i++) {
            portBattlexVariantTexts(sArmed[i].baseKind, NULL, NULL);
        }
        sTextsOn = 0;
    }
    sArmedCount = 0;
    memset(sUnitVariant, 0, sizeof(sUnitVariant));
    sSpawnedCoins = 0;
}
