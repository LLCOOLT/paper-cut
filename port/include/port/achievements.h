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
/* The counter's value on this file (0 if never counted). */
int portAchievementCounter(const char* key);

/* Aurora builds: the modes watched for their counters, each game frame
 * (port/src/aurora/achievements_modes_aurora.c, from postgame_aurora.c). */
void portAchievementsModesFrame(void);

#ifdef __cplusplus
}
#endif

#endif
