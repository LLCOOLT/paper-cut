#ifndef PORT_MASTER_QUEST_H
#define PORT_MASTER_QUEST_H

/* Master Quest (docs/MASTER_QUEST.md): the story's fights remixed by a set of
 * data, off until turned on ("master_quest", Modes tab).
 *
 * A set is text, a line each (the curated set is built in; the challenge
 * editor makes more, kept in <user>/master_quest_sets.txt and shared as
 * PCMQ1: codes):
 *
 *   name <the set's name>
 *   fight <area> <lead> = <unit> <unit> ...      (1-5 units)
 *   enemy <area|*> <unit> <change> <change> ...
 *
 * fight: in that area (the map name's first three letters: hei, gon, jon...),
 * an ordinary field fight whose first enemy is <lead> becomes one of its
 * area's fight lines for that lead, picked the same each time for a room, an
 * enemy and the remix seed. The units are the game's own, as the area's own
 * fights have them (their item tables, their heights); a unit the area has
 * no fight with is left out of the line.
 *
 * enemy: changes to that enemy, from the game's own moves and statuses:
 *   hp+N hp-N     its HP, after the difficulty's
 *   hp*P          its HP times P percent (before hp+N)
 *   atk+N atk-N   each of its attacks' base damage (attacks with a fixed power)
 *   hit=<status>:<chance>:<turns>[:<strength>]   (up to two) its attacks on
 *                 Mario's side may also give this status (chance in percent):
 *                 sleep stop dizzy poison confuse burn freeze tiny
 *                 attack_down defense_down slow
 *   start=<status>:<turns>[:<strength>]   (up to two) it starts the fight with
 *                 this status: dodgy huge attack_up defense_up charge
 *                 invisible fast payback hold_fast hp_regen electric
 *   element=fire|ice|explosion|electric   its attacks' element
 *   pierce        its attacks pierce defense
 * "*" as the area: every area (an area's own line wins).
 *
 * Bosses, scripted fights (no escape, losing allowed, no Jump or Hammer...),
 * tutorials and the boss rush's and Glitz Pit modes' fights stay as they
 * are; in the Glitz Pit's story matches (area tou) only "enemy" lines apply.
 *
 * Online co-op: the host's settings say whether it is on and which set; the
 * set's text goes with the host's setup (port/settings/features/online.cpp),
 * so both games play the same fights. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_MQ_UNITS 5
#define PORT_MQ_FIGHTS 640
#define PORT_MQ_ENEMIES 320
#define PORT_MQ_TEXT_MAX (64 * 1024)

/* A status: type -1 none; else StatusEffectType (battle/battle_status_effect.h). */
typedef struct PortMqStatus {
    signed char type;
    signed char chance; /* hit: percent */
    signed char turns;
    signed char strength;
} PortMqStatus;

/* One fight line. Unit types are BattleUnitType (battle/battle_unit.h). */
typedef struct PortMqFight {
    char area[4];
    short lead;
    short count;
    short units[PORT_MQ_UNITS];
} PortMqFight;

/* One enemy line: the changes to one kind of enemy. The same fields as an
 * enemy variant's battle changes, so a variant made elsewhere (the enemy
 * maker) can be put in a set as one of these. */
typedef struct PortMqEnemy {
    char area[4]; /* "" every area */
    short unit;
    short hpAdd;
    short hpPercent;   /* 100 unchanged */
    signed char atkAdd;
    signed char element; /* -1 unchanged, else BattleWeaponElement */
    unsigned char pierce;
    PortMqStatus hit[2];
    PortMqStatus start[2];
} PortMqEnemy;

typedef struct PortMqSet {
    char name[48];
    int fightCount;
    int enemyCount;
    PortMqFight fights[PORT_MQ_FIGHTS];
    PortMqEnemy enemies[PORT_MQ_ENEMIES];
} PortMqSet;

/* --- the settings ---------------------------------------------------------- */

/* "master_quest" (0/1) and "master_quest_set" (0 the curated set, else a
 * custom set's hash, portMasterQuestHash). */
void portMasterQuestDeclare(void);
int portMasterQuestOn(void);

/* --- sets ----------------------------------------------------------------- */

/* The curated set's text (built in). */
const char* portMasterQuestCuratedText(void);
/* Reads a set; 0 and the first error ("line 12: ...") when it isn't one.
 * Lines it can't use are errors, so a set made here is checked as it's typed. */
int portMasterQuestParse(const char* text, PortMqSet* out, char* err, size_t errSize);
/* A set's id for the settings and online: 24 bits, never 0. */
unsigned portMasterQuestHash(const char* text);
/* A set as a code (PCMQ1:<base64url of the text>) and back; malloc'd, NULL
 * when it isn't one. */
char* portMasterQuestCode(const char* text);
char* portMasterQuestFromCode(const char* code);

/* The custom sets (<user>/master_quest_sets.txt, a code a line). */
int portMasterQuestCustomCount(void);
const char* portMasterQuestCustomText(int i); /* NULL past the last */
const char* portMasterQuestCustomName(int i);
/* Adds or (same name) replaces a set; 0 and why when it isn't one. */
int portMasterQuestCustomSave(const char* text, char* err, size_t errSize);
void portMasterQuestCustomRemove(int i);

/* The set in play now (the curated set, the custom one the setting names, or
 * online the host's); NULL when Master Quest is off. Game side. */
const PortMqSet* portMasterQuestActive(void);
/* The text of the set the settings name ("" for the curated set); the host
 * sends it with the online setup. */
const char* portMasterQuestSelectedText(void);
/* Online guest: the host's set for this run (text "" or NULL: the curated one). */
void portMasterQuestSetForRun(const char* text, size_t len);

/* Names: the units a set can use ("GOOMBA", "SPIKY_GOOMBA"...) and statuses. */
int portMasterQuestUnitCount(void);
const char* portMasterQuestUnitNameAt(int i, int* type);
const char* portMasterQuestUnitName(int type); /* NULL: not one a set can use */
int portMasterQuestUnitByName(const char* name); /* 0 when unknown */

/* --- hook points for scripts (the Lua mods API) ---------------------------- */

/* Called while Master Quest or a script's hooks are on; any field may be NULL.
 * Scripts are off in online play unless both players run the same ones. */
typedef struct PortMasterQuestHooks {
    void* user;
    /* A field fight about to be set up in area/map: the game's enemies (or
     * the set's, already picked) in units, count of them. Change them in
     * place (up to PORT_MQ_UNITS; units only from the area, as above) and
     * return the new count, or return count unchanged. */
    int (*formation)(void* user, const char* area, const char* map, short* units, int count);
    /* An enemy entering a fight, with the set's changes for it (or none, all
     * unchanged): change them in place. */
    void (*enemy)(void* user, const char* area, int unitType, PortMqEnemy* changes);
} PortMasterQuestHooks;
/* NULL removes them. */
void portMasterQuestSetHooks(const PortMasterQuestHooks* hooks);
const PortMasterQuestHooks* portMasterQuestHooks(void);

/* The game side's hooks (master_quest_aurora.c), once from portPlayModesInit. */
void portMasterQuestInit(void);
/* The custom list changed on disk (the settings page): read it again. */
void portMasterQuestReload(void);

#ifdef __cplusplus
}
#endif

#endif
