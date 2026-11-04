#ifndef PORT_BATTLE_SPEED_H
#define PORT_BATTLE_SPEED_H

/* Faster battles (settings: battle_speed, Off / 1.5x / 2x;
 * port/src/common/battle_speed_pc.c): a battle's frames come faster, as
 * fast-forward's do (the frame pacing and the game clock, speed_aurora.c),
 * except while the player is the one acting: an action command set up or
 * running, a Stylish move's check, the command menu open, a Star Special
 * under way, and a moment after each. A guard or Superguard can't be told
 * in advance, so while faster their windows (and the mashing check) are as
 * many frames wider, the same time as at 1x. Off in online co-op. */

#ifdef __cplusplus
extern "C" {
#endif

struct BattleWork;
/* Each battle frame (BattleActionCommandManager): the speed from what the battle is doing. */
void portBattleSpeedUpdate(struct BattleWork* wp);
/* A Stylish move's check ran this frame (BattleAcrobatMain). */
void portBattleSpeedMark(void);
/* The speed this frame, percent: 100 outside battles, when off, or while the player acts. */
int portBattleSpeedPct(void);
/* In a battle with the setting on (and not online): vsync stays off for it. */
int portBattleSpeedOn(void);
/* A guard window of frames at 1x, as many frames at this speed (at most 30, the pad history's). */
int portBattleSpeedFrames(int frames);

#ifdef __cplusplus
}
#endif

#endif
