/* Master Quest's sets (port/include/port/master_quest.h): the settings, the
 * text format, codes, the custom list and which set is in play. The battle
 * side is port/src/aurora/master_quest_aurora.c. */
#include "port/file_pc.h"
#include "port/file_write.h"
#include "port/master_quest.h"
#include "port/code64.h"
#include "port/practice.h"
#include "port/settings.h"
#include "battle/battle_database_common.h"
#include "battle/battle_status_effect.h"
#include "battle/battle_unit.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- the settings ---------------------------------------------------------- */

static int* sOn;
static int* sSet;

void portMasterQuestDeclare(void) {
    if (sOn != NULL) {
        return;
    }
    sOn = portSettingsDeclare("master_quest", 0, 0, 1);
    sSet = portSettingsDeclare("master_quest_set", 0, 0, 0xFFFFFF);
}

int portMasterQuestOn(void) {
    portMasterQuestDeclare();
    return *sOn != 0;
}

/* --- names ------------------------------------------------------------------ */

typedef struct Name {
    const char* name;
    int value;
} Name;

/* the field enemies of the story's areas and the Pit (no bosses, their parts,
 * clones, tutorials or partners) */
static const Name kUnits[] = {
    {"AMAZY_DAYZEE", UNIT_AMAZY_DAYZEE},
    {"ARANTULA", UNIT_ARANTULA},
    {"BADGE_BANDIT", UNIT_BADGE_BANDIT},
    {"BALD_CLEFT", UNIT_BALD_CLEFT},
    {"BANDIT", UNIT_BANDIT},
    {"BIG_BANDIT", UNIT_BIG_BANDIT},
    {"BILL_BLASTER", UNIT_BILL_BLASTER},
    {"BOB_OMB", UNIT_BOB_OMB},
    {"BOB_ULK", UNIT_BOB_ULK},
    {"BOMBSHELL_BILL", UNIT_BOMBSHELL_BILL},
    {"BOMBSHELL_BILL_BLASTER", UNIT_BOMBSHELL_BILL_BLASTER},
    {"BOO", UNIT_BOO},
    {"BOOMERANG_BRO", UNIT_BOOMERANG_BRO},
    {"BRISTLE", UNIT_BRISTLE},
    {"BULKY_BOB_OMB", UNIT_BULKY_BOB_OMB},
    {"BULLET_BILL", UNIT_BULLET_BILL},
    {"BUZZY_BEETLE", UNIT_BUZZY_BEETLE},
    {"CHAIN_CHOMP", UNIT_CHAIN_CHOMP},
    {"CLEFT", UNIT_CLEFT},
    {"CRAZEE_DAYZEE", UNIT_CRAZEE_DAYZEE},
    {"DARK_BONES", UNIT_DARK_BONES},
    {"DARK_BOO", UNIT_DARK_BOO},
    {"DARK_BRISTLE", UNIT_DARK_BRISTLE},
    {"DARK_CRAW", UNIT_DARK_CRAW},
    {"DARK_KOOPA", UNIT_DARK_KOOPA},
    {"DARK_KOOPATROL", UNIT_DARK_KOOPATROL},
    {"DARK_LAKITU", UNIT_DARK_LAKITU},
    {"DARK_PARATROOPA", UNIT_DARK_PARATROOPA},
    {"DARK_PUFF", UNIT_DARK_PUFF},
    {"DARK_WIZZERD", UNIT_DARK_WIZZERD},
    {"DRY_BONES", UNIT_DRY_BONES},
    {"DULL_BONES", UNIT_DULL_BONES},
    {"ELITE_WIZZERD", UNIT_ELITE_WIZZERD},
    {"ELITE_X_NAUT", UNIT_ELITE_X_NAUT},
    {"EMBER", UNIT_EMBER},
    {"FIRE_BRO", UNIT_FIRE_BRO},
    {"FLOWER_FUZZY", UNIT_FLOWER_FUZZY},
    {"FROST_PIRANHA", UNIT_FROST_PIRANHA},
    {"FUZZY", UNIT_FUZZY},
    {"GLOOMBA", UNIT_GLOOMBA},
    {"GOOMBA", UNIT_GOOMBA},
    {"GOOMBA_GLITZVILLE", UNIT_GOOMBA_GLITZVILLE},
    {"GREEN_FUZZY", UNIT_GREEN_FUZZY},
    {"GREEN_MAGIKOOPA", UNIT_GREEN_MAGIKOOPA},
    {"HAMMER_BRO", UNIT_HAMMER_BRO},
    {"HYPER_BALD_CLEFT", UNIT_HYPER_BALD_CLEFT},
    {"HYPER_CLEFT", UNIT_HYPER_CLEFT},
    {"HYPER_GOOMBA", UNIT_HYPER_GOOMBA},
    {"HYPER_PARAGOOMBA", UNIT_HYPER_PARAGOOMBA},
    {"HYPER_SPIKY_GOOMBA", UNIT_HYPER_SPIKY_GOOMBA},
    {"ICE_PUFF", UNIT_ICE_PUFF},
    {"IRON_CLEFT_GREEN", UNIT_IRON_CLEFT_GREEN},
    {"IRON_CLEFT_RED", UNIT_IRON_CLEFT_RED},
    {"KOOPATROL", UNIT_KOOPATROL},
    {"KOOPA_TROOPA", UNIT_KOOPA_TROOPA},
    {"KP_KOOPA", UNIT_KP_KOOPA},
    {"KP_PARATROOPA", UNIT_KP_PARATROOPA},
    {"LAKITU", UNIT_LAKITU},
    {"LAVA_BUBBLE", UNIT_LAVA_BUBBLE},
    {"MAGIKOOPA", UNIT_MAGIKOOPA},
    {"MOON_CLEFT", UNIT_MOON_CLEFT},
    {"PALE_PIRANHA", UNIT_PALE_PIRANHA},
    {"PARABUZZY", UNIT_PARABUZZY},
    {"PARAGLOOMBA", UNIT_PARAGLOOMBA},
    {"PARAGOOMBA", UNIT_PARAGOOMBA},
    {"PARATROOPA", UNIT_PARATROOPA},
    {"PHANTOM_EMBER", UNIT_PHANTOM_EMBER},
    {"PIDER", UNIT_PIDER},
    {"PIRANHA_PLANT", UNIT_PIRANHA_PLANT},
    {"POISON_POKEY", UNIT_POISON_POKEY},
    {"POISON_PUFF", UNIT_POISON_PUFF},
    {"POKEY", UNIT_POKEY},
    {"PUTRID_PIRANHA", UNIT_PUTRID_PIRANHA},
    {"RED_BONES", UNIT_RED_BONES},
    {"RED_CHOMP", UNIT_RED_CHOMP},
    {"RED_MAGIKOOPA", UNIT_RED_MAGIKOOPA},
    {"RED_SPIKY_BUZZY", UNIT_RED_SPIKY_BUZZY},
    {"RUFF_PUFF", UNIT_RUFF_PUFF},
    {"SHADY_KOOPA", UNIT_SHADY_KOOPA},
    {"SHADY_PARATROOPA", UNIT_SHADY_PARATROOPA},
    {"SKY_BLUE_SPINY", UNIT_SKY_BLUE_SPINY},
    {"SPANIA", UNIT_SPANIA},
    {"SPIKE_TOP", UNIT_SPIKE_TOP},
    {"SPIKY_GLOOMBA", UNIT_SPIKY_GLOOMBA},
    {"SPIKY_GOOMBA", UNIT_SPIKY_GOOMBA},
    {"SPIKY_PARABUZZY", UNIT_SPIKY_PARABUZZY},
    {"SPINIA", UNIT_SPINIA},
    {"SPINY", UNIT_SPINY},
    {"SPUNIA", UNIT_SPUNIA},
    {"SWAMPIRE", UNIT_SWAMPIRE},
    {"SWOOPER", UNIT_SWOOPER},
    {"SWOOPULA", UNIT_SWOOPULA},
    {"WHITE_MAGIKOOPA", UNIT_WHITE_MAGIKOOPA},
    {"WIZZERD", UNIT_WIZZERD},
    {"X_NAUT", UNIT_X_NAUT},
    {"X_NAUT_PHD", UNIT_X_NAUT_PHD},
    {"X_YUX", UNIT_X_YUX},
    {"YUX", UNIT_YUX},
    {"Z_YUX", UNIT_Z_YUX},
};
#define UNIT_NAMES ((int)(sizeof(kUnits) / sizeof(kUnits[0])))

/* statuses a hit can give Mario's side */
static const Name kHitStatus[] = {
    {"sleep", STATUS_SLEEP},     {"stop", STATUS_STOP},           {"dizzy", STATUS_DIZZY},
    {"poison", STATUS_POISON},   {"confuse", STATUS_CONFUSE},     {"burn", STATUS_BURN},
    {"freeze", STATUS_FREEZE},   {"tiny", STATUS_TINY},           {"attack_down", STATUS_ATTACK_DOWN},
    {"defense_down", STATUS_DEFENSE_DOWN}, {"slow", STATUS_SLOW},
};

/* statuses an enemy can start a fight with */
static const Name kStartStatus[] = {
    {"dodgy", STATUS_DODGY},         {"huge", STATUS_HUGE},       {"attack_up", STATUS_ATTACK_UP},
    {"defense_up", STATUS_DEFENSE_UP}, {"charge", STATUS_CHARGE}, {"invisible", STATUS_INVISIBLE},
    {"fast", STATUS_FAST},           {"payback", STATUS_PAYBACK}, {"hold_fast", STATUS_HOLD_FAST},
    {"hp_regen", STATUS_HP_REGEN},   {"electric", STATUS_ELECTRIC},
};

static const Name kElements[] = {
    {"normal", ELEMENT_NORMAL},       {"fire", ELEMENT_FIRE},         {"ice", ELEMENT_ICE},
    {"explosion", ELEMENT_EXPLOSION}, {"electric", ELEMENT_ELECTRIC},
};

static int nameIndex(const Name* names, int count, const char* s) {
    int i;
    for (i = 0; i < count; i++) {
        const char* a = names[i].name;
        const char* b = s;
        while (*a != '\0' && *b != '\0' && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return i;
        }
    }
    return -1;
}

int portMasterQuestUnitCount(void) {
    return UNIT_NAMES;
}

const char* portMasterQuestUnitNameAt(int i, int* type) {
    if (i < 0 || i >= UNIT_NAMES) {
        return NULL;
    }
    if (type != NULL) {
        *type = kUnits[i].value;
    }
    return kUnits[i].name;
}

const char* portMasterQuestUnitName(int type) {
    int i;
    for (i = 0; i < UNIT_NAMES; i++) {
        if (kUnits[i].value == type) {
            return kUnits[i].name;
        }
    }
    return NULL;
}

int portMasterQuestUnitByName(const char* name) {
    const int i = nameIndex(kUnits, UNIT_NAMES, name);
    return i < 0 ? 0 : kUnits[i].value;
}

/* --- parsing ---------------------------------------------------------------- */

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void fail(char* err, size_t size, int line, const char* fmt, ...) {
    char what[160];
    va_list ap;
    if (err == NULL || size == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(what, sizeof(what), fmt, ap);
    va_end(ap);
    snprintf(err, size, "line %d: %s", line, what);
}

/* the next word of *p (spaces apart), NUL-terminated in place; NULL at the end */
static char* word(char** p) {
    char* s = *p;
    char* start;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return NULL;
    }
    start = s;
    while (*s != '\0' && *s != ' ' && *s != '\t') {
        s++;
    }
    if (*s != '\0') {
        *s++ = '\0';
    }
    *p = s;
    return start;
}

static int validArea(const char* s) {
    int i;
    if (strlen(s) != 3) {
        return 0;
    }
    for (i = 0; i < 3; i++) {
        if (!islower((unsigned char)s[i]) && !isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static int number(const char* s, int* out) {
    char* end;
    long v;
    if (s == NULL || *s == '\0') {
        return 0;
    }
    v = strtol(s, &end, 10);
    if (*end != '\0' || v < -9999 || v > 9999) {
        return 0;
    }
    *out = (int)v;
    return 1;
}

/* "<status>:<a>:<b>[:<c>]" with n numbers needed (and one more optional) */
static int statusSpec(char* s, const Name* names, int count, int needed, signed char* type, int* nums) {
    char* parts[5];
    int n = 0, i, at;
    char* p = s;
    parts[n++] = p;
    while (*p != '\0' && n < 5) {
        if (*p == ':') {
            *p = '\0';
            parts[n++] = p + 1;
        }
        p++;
    }
    if (n < needed + 1 || n > needed + 2) {
        return 0;
    }
    at = nameIndex(names, count, parts[0]);
    if (at < 0) {
        return 0;
    }
    *type = (signed char)names[at].value;
    nums[needed] = 1; /* the default strength */
    for (i = 1; i < n; i++) {
        if (!number(parts[i], &nums[i - 1]) || nums[i - 1] < 0 || nums[i - 1] > 100) {
            return 0;
        }
    }
    return 1;
}

static void enemyDefaults(PortMqEnemy* e) {
    memset(e, 0, sizeof(*e));
    e->hpPercent = 100;
    e->element = -1;
    e->hit[0].type = e->hit[1].type = -1;
    e->start[0].type = e->start[1].type = -1;
}

static int parseEnemy(char* rest, int line, PortMqSet* set, char* err, size_t errSize) {
    PortMqEnemy* e;
    char* area = word(&rest);
    char* unit = word(&rest);
    char* change;
    int hits = 0, starts = 0, v;
    if (area == NULL || unit == NULL) {
        fail(err, errSize, line, "enemy <area|*> <unit> <changes...>");
        return 0;
    }
    if (strcmp(area, "*") != 0 && !validArea(area)) {
        fail(err, errSize, line, "\"%s\" isn't an area (three letters, as hei or jon) or *", area);
        return 0;
    }
    if (portMasterQuestUnitByName(unit) == 0) {
        fail(err, errSize, line, "\"%s\" isn't an enemy a set can use", unit);
        return 0;
    }
    if (set->enemyCount >= PORT_MQ_ENEMIES) {
        fail(err, errSize, line, "more than %d enemy lines", PORT_MQ_ENEMIES);
        return 0;
    }
    e = &set->enemies[set->enemyCount];
    enemyDefaults(e);
    snprintf(e->area, sizeof(e->area), "%s", strcmp(area, "*") == 0 ? "" : area);
    e->unit = (short)portMasterQuestUnitByName(unit);
    while ((change = word(&rest)) != NULL) {
        if (strncmp(change, "hp+", 3) == 0 || strncmp(change, "hp-", 3) == 0) {
            if (!number(change + 2, &v) || v < -99 || v > 99) {
                fail(err, errSize, line, "\"%s\": hp+N or hp-N, N up to 99", change);
                return 0;
            }
            e->hpAdd = (short)v;
        } else if (strncmp(change, "hp*", 3) == 0) {
            if (!number(change + 3, &v) || v < 10 || v > 500) {
                fail(err, errSize, line, "\"%s\": hp*P, P a percent from 10 to 500", change);
                return 0;
            }
            e->hpPercent = (short)v;
        } else if (strncmp(change, "atk+", 4) == 0 || strncmp(change, "atk-", 4) == 0) {
            if (!number(change + 3, &v) || v < -9 || v > 9) {
                fail(err, errSize, line, "\"%s\": atk+N or atk-N, N up to 9", change);
                return 0;
            }
            e->atkAdd = (signed char)v;
        } else if (strncmp(change, "hit=", 4) == 0) {
            int nums[3];
            PortMqStatus* s;
            if (hits == 2) {
                fail(err, errSize, line, "two hit= at most");
                return 0;
            }
            s = &e->hit[hits++];
            if (!statusSpec(change + 4, kHitStatus, COUNT(kHitStatus), 2, &s->type, nums)) {
                fail(err, errSize, line,
                     "hit=<status>:<chance>:<turns>[:<strength>]; the statuses: sleep stop dizzy poison confuse "
                     "burn freeze tiny attack_down defense_down slow");
                return 0;
            }
            s->chance = (signed char)nums[0];
            s->turns = (signed char)(nums[1] < 1 ? 1 : nums[1] > 9 ? 9 : nums[1]);
            s->strength = (signed char)(nums[2] > 9 ? 9 : nums[2]);
        } else if (strncmp(change, "start=", 6) == 0) {
            int nums[2];
            PortMqStatus* s;
            if (starts == 2) {
                fail(err, errSize, line, "two start= at most");
                return 0;
            }
            s = &e->start[starts++];
            if (!statusSpec(change + 6, kStartStatus, COUNT(kStartStatus), 1, &s->type, nums)) {
                fail(err, errSize, line,
                     "start=<status>:<turns>[:<strength>]; the statuses: dodgy huge attack_up defense_up charge "
                     "invisible fast payback hold_fast hp_regen electric");
                return 0;
            }
            s->chance = 100;
            s->turns = (signed char)(nums[0] < 1 ? 1 : nums[0] > 9 ? 9 : nums[0]);
            s->strength = (signed char)(nums[1] > 9 ? 9 : nums[1]);
        } else if (strncmp(change, "element=", 8) == 0) {
            const int at = nameIndex(kElements, COUNT(kElements), change + 8);
            if (at < 0) {
                fail(err, errSize, line, "element=normal|fire|ice|explosion|electric");
                return 0;
            }
            e->element = (signed char)kElements[at].value;
        } else if (strcmp(change, "pierce") == 0) {
            e->pierce = 1;
        } else if (strncmp(change, "variant=", 8) == 0) {
            /* the shared enemy-variant format (port/include/port/enemy_variant.h) */
            PortEnemyVariant* v;
            if (set->variantCount >= PORT_MQ_VARIANTS) {
                fail(err, errSize, line, "more than %d variant= lines", PORT_MQ_VARIANTS);
                return 0;
            }
            v = &set->variants[set->variantCount];
            if (!portEnemyVariantParse(change + 8, v)) {
                fail(err, errSize, line, "\"%.24s...\": variant=PCFOE1:<the Enemy maker's code>", change);
                return 0;
            }
            v->baseKind = e->unit;
            e->variant = (short)(++set->variantCount);
        } else {
            fail(err, errSize, line, "\"%s\" isn't a change (hp+N hp*P atk+N hit= start= element= pierce variant=)",
                 change);
            return 0;
        }
    }
    set->enemyCount++;
    return 1;
}

static int parseFight(char* rest, int line, PortMqSet* set, char* err, size_t errSize) {
    PortMqFight* f;
    char* area = word(&rest);
    char* lead = word(&rest);
    char* eq = word(&rest);
    char* unit;
    if (area == NULL || lead == NULL || eq == NULL || strcmp(eq, "=") != 0) {
        fail(err, errSize, line, "fight <area> <lead> = <unit> <unit> ...");
        return 0;
    }
    if (!validArea(area)) {
        fail(err, errSize, line, "\"%s\" isn't an area (three letters, as hei or jon)", area);
        return 0;
    }
    if (portMasterQuestUnitByName(lead) == 0) {
        fail(err, errSize, line, "\"%s\" isn't an enemy a set can use", lead);
        return 0;
    }
    if (set->fightCount >= PORT_MQ_FIGHTS) {
        fail(err, errSize, line, "more than %d fight lines", PORT_MQ_FIGHTS);
        return 0;
    }
    f = &set->fights[set->fightCount];
    memset(f, 0, sizeof(*f));
    snprintf(f->area, sizeof(f->area), "%s", area);
    f->lead = (short)portMasterQuestUnitByName(lead);
    while ((unit = word(&rest)) != NULL) {
        const int type = portMasterQuestUnitByName(unit);
        if (type == 0) {
            fail(err, errSize, line, "\"%s\" isn't an enemy a set can use", unit);
            return 0;
        }
        if (f->count == PORT_MQ_UNITS) {
            fail(err, errSize, line, "%d enemies at most", PORT_MQ_UNITS);
            return 0;
        }
        f->units[f->count++] = (short)type;
    }
    if (f->count == 0) {
        fail(err, errSize, line, "no enemies after =");
        return 0;
    }
    set->fightCount++;
    return 1;
}

int portMasterQuestParse(const char* text, PortMqSet* out, char* err, size_t errSize) {
    char buf[512];
    int line = 0;
    const char* p = text != NULL ? text : "";
    if (err != NULL && errSize > 0) {
        err[0] = '\0';
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "Master Quest");
    while (*p != '\0') {
        const char* end = strchr(p, '\n');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        char* s;
        char* kind;
        line++;
        if (len >= sizeof(buf)) {
            fail(err, errSize, line, "too long");
            return 0;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        p += len + (end != NULL ? 1 : 0);
        while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == ' ' || buf[len - 1] == '\t')) {
            buf[--len] = '\0';
        }
        s = buf;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (*s == '\0' || *s == '#') {
            continue;
        }
        kind = word(&s);
        if (strcmp(kind, "name") == 0) {
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            snprintf(out->name, sizeof(out->name), "%s", s);
        } else if (strcmp(kind, "fight") == 0) {
            if (!parseFight(s, line, out, err, errSize)) {
                return 0;
            }
        } else if (strcmp(kind, "enemy") == 0) {
            if (!parseEnemy(s, line, out, err, errSize)) {
                return 0;
            }
        } else {
            fail(err, errSize, line, "\"%s\": a line is name, fight or enemy (or # a note)", kind);
            return 0;
        }
    }
    return 1;
}

unsigned portMasterQuestHash(const char* text) {
    unsigned h = 2166136261u;
    const char* p = text != NULL ? text : "";
    while (*p != '\0') {
        if (*p != '\r') {
            h = (h ^ (unsigned char)*p) * 16777619u;
        }
        p++;
    }
    h = (h ^ (h >> 24)) & 0xFFFFFFu;
    return h != 0 ? h : 1;
}

/* --- codes ------------------------------------------------------------------- */

#define CODE_PREFIX "PCMQ1:"

char* portMasterQuestCode(const char* text) {
    const size_t len = strlen(text);
    const size_t size = sizeof(CODE_PREFIX) + (len * 4) / 3 + 4;
    char* out = (char*)malloc(size);
    if (out == NULL) {
        return NULL;
    }
    strcpy(out, CODE_PREFIX);
    portCode64Encode((const unsigned char*)text, (int)len, out + strlen(CODE_PREFIX), (int)(size - strlen(CODE_PREFIX)));
    return out;
}

char* portMasterQuestFromCode(const char* code) {
    const char* start = strstr(code, CODE_PREFIX);
    char* out;
    int at;
    if (start == NULL) {
        return NULL;
    }
    start += strlen(CODE_PREFIX);
    out = (char*)malloc(strlen(start) + 1);
    if (out == NULL) {
        return NULL;
    }
    at = portCode64Decode(start, (unsigned char*)out, (int)strlen(start) + 1);
    if (at <= 0 || at >= PORT_MQ_TEXT_MAX) {
        free(out);
        return NULL;
    }
    return out;
}

/* --- the custom list (the settings page's thread) ----------------------------- */

#define CUSTOM_MAX 32
static char* sCustomText[CUSTOM_MAX];
static char sCustomName[CUSTOM_MAX][48];
static int sCustomCount;
static int sCustomRead;
static int sCustomVersion; /* __atomic: the game side reads the file again when it changes */
static int sFileLock;      /* __atomic: the file written (the page's thread) or read (the game's), one at a time */

static void fileLock(void) {
    while (__atomic_test_and_set(&sFileLock, __ATOMIC_ACQUIRE)) {
    }
}

static void fileUnlock(void) {
    __atomic_clear(&sFileLock, __ATOMIC_RELEASE);
}

static void listPath(char* out, size_t size) {
    snprintf(out, size, "%s/master_quest_sets.txt", portPracticeUserDir()); /* the user folder (any build) */
}

/* the file's codes, each decoded; fn(text, user) for each, until it returns 0 */
static void eachInFile(int (*fn)(char* text, void* user), void* user) {
    char path[1024];
    FILE* f;
    char* line;
    listPath(path, sizeof(path));
    fileLock();
    f = portFopen(path, "rb");
    if (f == NULL) {
        fileUnlock();
        return;
    }
    line = (char*)malloc(PORT_MQ_TEXT_MAX * 2);
    if (line == NULL) {
        fclose(f);
        fileUnlock();
        return;
    }
    while (fgets(line, PORT_MQ_TEXT_MAX * 2, f) != NULL) {
        char* text = line[0] != '#' ? portMasterQuestFromCode(line) : NULL;
        if (text != NULL && !fn(text, user)) {
            break;
        }
    }
    free(line);
    fclose(f);
    fileUnlock();
}

static PortMqSet sScratch; /* the page's parses (one thread) */

static int addRead(char* text, void* user) {
    (void)user;
    if (sCustomCount < CUSTOM_MAX && portMasterQuestParse(text, &sScratch, NULL, 0)) {
        sCustomText[sCustomCount] = text;
        snprintf(sCustomName[sCustomCount], sizeof(sCustomName[0]), "%s", sScratch.name);
        sCustomCount++;
    } else {
        free(text);
    }
    return 1;
}

static void readList(void) {
    if (sCustomRead) {
        return;
    }
    sCustomRead = 1;
    eachInFile(addRead, NULL);
}

static void writeList(void) {
    char path[1024];
    FILE* f;
    int i;
    listPath(path, sizeof(path));
    fileLock();
    f = portFileWriteOpen(path, "wb");
    if (f == NULL) {
        fileUnlock();
        return;
    }
    fputs("# Paper Cut: Master Quest sets (Challenges > Master Quest sets), a code a line\n", f);
    for (i = 0; i < sCustomCount; i++) {
        char* code = portMasterQuestCode(sCustomText[i]);
        if (code != NULL) {
            fputs(code, f);
            fputc('\n', f);
            free(code);
        }
    }
    portFileWriteClose(f, path);
    fileUnlock();
    __atomic_add_fetch(&sCustomVersion, 1, __ATOMIC_SEQ_CST);
}

int portMasterQuestCustomCount(void) {
    readList();
    return sCustomCount;
}

const char* portMasterQuestCustomText(int i) {
    readList();
    return i >= 0 && i < sCustomCount ? sCustomText[i] : NULL;
}

const char* portMasterQuestCustomName(int i) {
    readList();
    return i >= 0 && i < sCustomCount ? sCustomName[i] : NULL;
}

int portMasterQuestCustomSave(const char* text, char* err, size_t errSize) {
    int i;
    char* copy;
    readList();
    if (strlen(text) >= PORT_MQ_TEXT_MAX) {
        snprintf(err, errSize, "longer than %d characters", PORT_MQ_TEXT_MAX);
        return 0;
    }
    if (!portMasterQuestParse(text, &sScratch, err, errSize)) {
        return 0;
    }
    copy = (char*)malloc(strlen(text) + 1);
    if (copy == NULL) {
        return 0;
    }
    strcpy(copy, text);
    for (i = 0; i < sCustomCount; i++) {
        if (strcmp(sCustomName[i], sScratch.name) == 0) {
            free(sCustomText[i]);
            sCustomText[i] = copy;
            writeList();
            return 1;
        }
    }
    if (sCustomCount == CUSTOM_MAX) {
        free(copy);
        snprintf(err, errSize, "%d sets at most: remove one first", CUSTOM_MAX);
        return 0;
    }
    sCustomText[sCustomCount] = copy;
    snprintf(sCustomName[sCustomCount], sizeof(sCustomName[0]), "%s", sScratch.name);
    sCustomCount++;
    writeList();
    return 1;
}

void portMasterQuestCustomRemove(int i) {
    readList();
    if (i < 0 || i >= sCustomCount) {
        return;
    }
    free(sCustomText[i]);
    memmove(&sCustomText[i], &sCustomText[i + 1], sizeof(sCustomText[0]) * (size_t)(sCustomCount - i - 1));
    memmove(&sCustomName[i], &sCustomName[i + 1], sizeof(sCustomName[0]) * (size_t)(sCustomCount - i - 1));
    sCustomCount--;
    writeList();
}

void portMasterQuestReload(void) {
    __atomic_add_fetch(&sCustomVersion, 1, __ATOMIC_SEQ_CST);
}

/* the selected set's text, read from the file under its lock into a buffer of this call's own: never the
   page's list (sCustomText), which the settings page's thread may be saving or removing from meanwhile */
static char sSelectedText[PORT_MQ_TEXT_MAX];

static int findSelected(char* text, void* user) {
    const int found = portMasterQuestHash(text) == *(const unsigned*)user;
    if (found) {
        snprintf(sSelectedText, sizeof(sSelectedText), "%s", text);
    }
    free(text);
    return !found;
}

const char* portMasterQuestSelectedText(void) {
    unsigned want;
    portMasterQuestDeclare();
    if (*sSet == 0) {
        return "";
    }
    want = (unsigned)*sSet;
    sSelectedText[0] = '\0';
    eachInFile(findSelected, &want);
    return sSelectedText;
}

/* --- the set in play (the game's thread) ---------------------------------------- */

static const char kCurated[] =
#include "master_quest_curated.inc"
    ;

const char* portMasterQuestCuratedText(void) {
    return kCurated;
}

static PortMqSet sCurated, sCustom;
/* the host's set for the run: put in place on the online thread (the lobby), read on the game's: parsed
 * into the buffer not in use, then sRunState (__atomic) names it, so the game never reads half a set */
static PortMqSet sRunSet[2];
static int sCuratedState; /* 0 not read, 1 good, -1 bad */
static unsigned sCustomHash;
static int sCustomSeen = -1;
static int sCustomGood;
static int sRunState; /* __atomic: 0 none, 1 the host's set (in sRunSet[bit 4]), 2 the host's curated set */

static const PortMqSet* curated(void) {
    if (sCuratedState == 0) {
        char err[200];
        sCuratedState = portMasterQuestParse(kCurated, &sCurated, err, sizeof(err)) ? 1 : -1;
        if (sCuratedState < 0) {
            fprintf(stderr, "[master quest] the curated set: %s\n", err);
        }
    }
    return sCuratedState > 0 ? &sCurated : NULL;
}

typedef struct Find {
    unsigned hash;
    int found;
} Find;

static int findCustom(char* text, void* user) {
    Find* f = (Find*)user;
    if (portMasterQuestHash(text) == f->hash) {
        f->found = portMasterQuestParse(text, &sCustom, NULL, 0);
        free(text);
        return 0;
    }
    free(text);
    return 1;
}

void portMasterQuestSetForRun(const char* text, size_t len) {
    char* copy;
    /* the buffer the game isn't reading */
    const int spare = (__atomic_load_n(&sRunState, __ATOMIC_ACQUIRE) & 16) ? 0 : 1;
    if (text == NULL || len == 0) {
        __atomic_store_n(&sRunState, 2, __ATOMIC_RELEASE);
        return;
    }
    copy = (char*)malloc(len + 1);
    if (copy == NULL) {
        __atomic_store_n(&sRunState, 2, __ATOMIC_RELEASE);
        return;
    }
    memcpy(copy, text, len);
    copy[len] = '\0';
    __atomic_store_n(&sRunState, portMasterQuestParse(copy, &sRunSet[spare], NULL, 0) ? 1 | (spare << 4) : 2,
                     __ATOMIC_RELEASE);
    free(copy);
}

void portMasterQuestRunEnd(void) {
    __atomic_store_n(&sRunState, 0, __ATOMIC_RELEASE);
}

const PortMqSet* portMasterQuestActive(void) {
    const int run = __atomic_load_n(&sRunState, __ATOMIC_ACQUIRE);
    int version;
    portMasterQuestDeclare();
    if (!*sOn) {
        return NULL;
    }
    if ((run & 15) == 1) {
        return &sRunSet[run >> 4];
    }
    if ((run & 15) == 2 || *sSet == 0) {
        return curated();
    }
    version = __atomic_load_n(&sCustomVersion, __ATOMIC_SEQ_CST);
    if ((unsigned)*sSet != sCustomHash || sCustomSeen != version) {
        Find f;
        f.hash = (unsigned)*sSet;
        f.found = 0;
        eachInFile(findCustom, &f);
        sCustomHash = f.hash;
        sCustomSeen = version;
        sCustomGood = f.found;
        if (!f.found) {
            fprintf(stderr, "[master quest] set %06X isn't in master_quest_sets.txt: the curated set\n", f.hash);
        }
    }
    return sCustomGood ? &sCustom : curated();
}

/* --- hooks for scripts ------------------------------------------------------------ */

static PortMasterQuestHooks sHooks;
static int sHooksSet;

void portMasterQuestSetHooks(const PortMasterQuestHooks* hooks) {
    if (hooks == NULL) {
        memset(&sHooks, 0, sizeof(sHooks));
        sHooksSet = 0;
    } else {
        sHooks = *hooks;
        sHooksSet = 1;
    }
}

const PortMasterQuestHooks* portMasterQuestHooks(void) {
    return sHooksSet ? &sHooks : NULL;
}
