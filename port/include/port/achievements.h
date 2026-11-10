#ifndef PORT_ACHIEVEMENTS_H
#define PORT_ACHIEVEMENTS_H

/* The achievements' counters (port/src/common/achievements_pc.c; the list
 * and the page: port/include/port/practice.h). A mode counts what it does
 * into a named counter, kept per save file in the achievements' record
 * beside the memory cards; an achievement of kind K_COUNTER is earned when
 * its counter reaches its goal, with the usual pop-up.
 *
 * To add an achievement for a mode: a row in achievements_pc.c's kList
 * ({id, name, note, K_COUNTER, goal, "counter"}) and, where the mode does
 * the thing, portAchievementCounterAdd("counter", 1) (a count) or
 * portAchievementCounterMax("counter", value) (a best: a floor, a round).
 * Or watch the mode's own calls from port/src/aurora/achievements_modes_aurora.c,
 * as the 0.4.0 modes are, so the mode's file needn't change.
 *
 * Counted only while achievements are on, with a save file loaded and the
 * practice tools not used since (as achievements are given). Draw-only: the
 * game's state isn't touched. Plain C, any file. */

#ifdef __cplusplus
extern "C" {
#endif

void portAchievementCounterAdd(const char* key, int n);
void portAchievementCounterMax(const char* key, int value);
/* Sets bit (0-30) of a counter used as a set (each partner, boss, emote seen). */
void portAchievementCounterBit(const char* key, int bit);
/* The counter's value on this file (0 if never counted). */
int portAchievementCounter(const char* key);

/* 0.5.0's battle feats, from the battle's own record (achievements_pc.c):
 * a battle over (battlex_pc.c, result 1 won), and a Special move chosen
 * (battle_seq_command.c, the weapon from superActionTable). */
struct BattleWork;
void portAchievementsBattleOver(struct BattleWork* wp, int result);
void portAchievementsSpecialUsed(const void* weapon);

/* Online co-op: a guest (no save file of its own) keeps its achievements against its copy of the host's
 * adventure (coop_save_pc.c's <host>_<room>), with the same checks on the mirrored game and its toasts on its
 * own screen; when the copy is kept as file slot (0-3: coop_copy_pc.c, the launcher), the record becomes that
 * file's (with Souvenir). On one computer players 2-4 play player 1's file, so theirs count on it. */
void portAchievementsCoopCopyKept(int slot);

/* Aurora builds: the modes watched for their counters, each game frame
 * (port/src/aurora/achievements_modes_aurora.c, from postgame_aurora.c). */
void portAchievementsModesFrame(void);

#ifdef __cplusplus
}
#endif

#endif
