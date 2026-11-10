#ifndef PORT_PM64_FOES_H
#define PORT_PM64_FOES_H

#include <stddef.h>

/* Paper Mario 64 enemies in the story's fights (docs/PM64_EXTRAS.md, "In the story";
 * port/src/common/pm64_foes_pc.c). Setting "pm64_story_enemies": 0 off, 1 mixed (some fights), 2 all (every
 * fitting fight). A field enemy whose fight is picked wanders as the PM64 cousin of its fight's first enemy
 * (drawn from the linked ROM, TTYD's own field AI), and in its battle every enemy with a PM64 cousin is that
 * cousin: its sprite and animations, PM64's HP and attack scaled to TTYD's curve by chapter, the ROM's name
 * and Tattle, the cousin's own moves (most of TTYD's enemies are PM64's, same attacks). Story-scripted
 * fights, bosses, the Pit and the play modes' own fights are left alone; rewards are the TTYD enemy's.
 *
 * Which fights: a hash of the room's name and the field NPC's name, so every game (online, a replay) picks
 * the same. Online it follows the host's setting, and only when every player has a ROM linked (the PM64
 * partners' session token). */

#ifdef __cplusplus
extern "C" {
#endif

/* The setting (0 off, 1 mixed, 2 all) and whether it applies now (a ROM linked; online: the session's
 * players all have one). */
int portPm64FoesMode(void);
int portPm64FoesActive(void);
/* The chapter (0 prologue .. 8) the story's area map belongs to, -1 none (the Pit, the play modes' maps). */
int portPm64FoesChapter(const char* map);
/* PM64 enemy index (port/include/port/pm64_enemy.h) the field NPC wanders as; -1 none. */
struct NpcEntry;
int portPm64FoeOfNpc(struct NpcEntry* npc);
/* The field draw: the NPC pose poseId drawn as its PM64 cousin; 1 if it was (pm64_enemy_pc.c). */
int portPm64FoeFieldPose(int poseId, int* enemy, const char** animName);
/* Btl_UnitSetup, before (pm64_media_aurora.c): this fight's PM64 cousins armed when its field NPC was picked;
 * 1 if any were. */
struct BattleWork;
int portPm64FoesArm(struct BattleWork* wp);
/* The scaled HP and attack of PM64 enemy i in chapter c (the rule: docs/PM64_EXTRAS.md). */
int portPm64FoeHp(int i, int chapter);
int portPm64FoeAttack(int i, int chapter);
/* The rule for any PM64 number: value (HP, or an attack when attack is 1) of an enemy whose PM64 home chapter is
 * home, met in chapter. */
int portPm64FoeScale(int value, int home, int chapter, int attack);
/* A PM64 Tattle's "Max HP: N" and "Attack Power: N" rewritten in place to hp and attack. */
void portPm64TattleNumbers(char* text, size_t size, int hp, int attack);

/* Paper Mario 64's bosses as refights (port/src/aurora/pm64_bosses_aurora.c): setting "pm64_bosses" (with a
 * ROM linked, not online or in a replay); how many there are and each one's English name; and the draw:
 * a boss unit's sprite, palette, row and size (0 if the unit isn't one; headless: always 0). */
struct BattleUnit;
int portPm64BossesOn(void);
/* They can be fought now, the setting aside (a ROM linked, not online or in a replay): the Paper Mario 64 boss list
 * and the boss rush's "Paper Mario 64 bosses only" (modes_aurora.c) */
int portPm64BossesReady(void);
/* evt_pm64boss_battle's flags (with NPC_BATTLE_*): this fight's HP and hits scaled to Mario's level, never past
 * PM64's own ("pm64_boss_scale": a fight from the boss list, the board or Boss refights; not the boss rush) */
#define PM64_BOSS_FLAG_SCALE 0x40000000
/* ... and its EARLIEST version (portPm64FightHasEarly: Jr. Troopa's first fight), scaled against PM64's level 1: the
 * story meeting's, and a refight's "first fight" */
#define PM64_BOSS_FLAG_EARLY 0x08000000
/* fight (0..PM64_BOSS_FIGHTS-1) has an earlier version than the refights' default (its last): 1 Jr. Troopa */
int portPm64FightHasEarly(int fight);
int portPm64BossCount(void);
const char* portPm64BossEnglish(int boss);
int portPm64BossDraw(const struct BattleUnit* unit, int* sprite, int* palette, int* anim, float* scale);
/* Its Tattle card's picture (pm64_enemy_pc.c): its sprite, palette and idle row, standing as itself whatever it is
 * doing now (not toppled from a tower, mid-move or hurt); 0 if the unit isn't one (headless: always 0). */
int portPm64BossCardRow(const struct BattleUnit* unit, int* sprite, int* palette, int* row);
/* A boss unit's effects drawn with it: *glow 1 while it has gathered its strength (PM64's EnableActorGlow); and 1
 * while its swarm of darkness is out (PM64's EFFECT_TUBBA_HEART_ATTACK): from where it set off, the target's
 * position, how many frames since (headless: always 0). */
int portPm64BossFx(const struct BattleUnit* unit, int* glow, float from[3], float to[3], int* age);
/* a sprite in parts: the n-th part (0, 1) drawn over row; 0 none */
int portPm64BossLayer(const struct BattleUnit* unit, int row, int n, int* anim);
/* a head on a vine (Lava Piranha): the vine's two segment rows and its height in PM64 units (the head is drawn
 * that far up); 0 none (headless: always 0) */
int portPm64BossVine(const struct BattleUnit* unit, int* rowA, int* rowB, float* height);
/* The story's PM64 enemies with no cousin here (Forest Fuzzies and Hyper Goombas in Boggly Woods, Shy Guys...):
 * units of the boss engine's, put into the fights picked in the areas they fit (Aurora: its hook registered). */
void portPm64FoeUnitsInit(void);

#ifdef __cplusplus
}
#endif

#endif
