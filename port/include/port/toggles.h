#ifndef PORT_TOGGLES_H
#define PORT_TOGGLES_H

/* Small options, each its own declared setting (port/include/port/settings.h),
 * off by default so the game plays as the original until one is turned on.
 * Online, every one that changes play comes from the host's settings (online
 * sends its settings.txt), or is off there: see each.
 *
 *  - Level sync ("level_sync", port/src/common/level_sync_pc.c)
 *  - Console-accurate mode ("console_accurate", port/src/common/toggles_pc.c;
 *    the lock itself: port/settings/features/console_accurate.cpp)
 *  - Fun toggles ("fun_*", port/src/common/fun_pc.c)
 *  - Page-turn transitions ("page_turn", port/src/common/page_turn_pc.c)
 *  - Battery saver ("battery_saver", port/src/aurora/battery_aurora.c)
 *  - Chaos mode ("chaos_mode", port/src/common/chaos_pc.c)
 *  - Partner wheel ("partner_wheel", port/settings/features/partner_wheel.cpp) */

#include <dolphin/mtx.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Console-accurate mode ------------------------------------------------
 * One switch: the game as the GameCube plays it. Every setting marked
 * "changes play" (and text speed, faster field text and menu speed) is put
 * back to the original and locked while it is on; turned off, they come back
 * as they were. The speedrun glitches are the game's own and Paper Cut leaves
 * them in, so they are there with it on. Races and leaderboards ask
 * portConsoleAccurate(). Online, it is the host's. */
int portConsoleAccurate(void);

/* --- Level sync -----------------------------------------------------------
 * "level_sync": 0 off, 1 in the post-game and on New Game+ files, 2 always.
 * An enemy below Mario's level is brought up to it: more HP (a tenth more per
 * level below, at most 4x), harder hits (1 more per 6 levels, at most 6), and
 * Star Points as for an enemy of his level. A New Game+ file is one whose
 * Mario is 8 or more levels above the chapter's usual level. Online: the
 * host's setting. */
/* battle_unit.c BtlUnit_Entry, an enemy (alliance 1): its HP and its level
 * (the Star Points it gives) after the difficulty. */
int portLevelSyncHp(int hp, int kindLevel);
int portLevelSyncLevel(int level);
/* battle_damage.c, an enemy's hit on Mario or a partner, after the difficulty. */
int portLevelSyncDamage(int damage, int kindLevel);

/* --- Fun toggles (the Modes tab) -------------------------------------------- */
enum {
    PORT_FUN_BIG_HEADS,    /* "fun_big_heads": heads drawn larger (draw only) */
    PORT_FUN_MOON_GRAVITY, /* "fun_moon_gravity": Mario jumps as on the Moon, everywhere (changes play) */
    PORT_FUN_PAPER_THIN,   /* "fun_paper_thin": everyone a sliver of paper, seen from any side (draw only) */
    PORT_FUN_LUIGI_VOICE,  /* "fun_luigi_voice": Mario's steps, jumps and landings in Luigi's voice */
    PORT_FUN_SPIN,         /* "fun_spin": everyone turns round and round (draw only) */
    PORT_FUN_COUNT
};
void portFunDeclare(void);
/* A fun toggle is on (and console-accurate mode isn't). */
int portFunOn(int which);
/* animdrv.c _animPoseDrawMtx, the model's matrix before its groups: the
 * matrix to draw with (out, or in when nothing changes). file: the model's. */
MtxPtr portFunPoseMtx(const char* file, MtxPtr in, Mtx out);
/* animdrv.c dispProc, after a group's matrix and its world positions: 1 and
 * the scale when the group is a head (big heads), and the point it grows
 * about in the group's coordinates when the head table (fun_heads.inc) has
 * one (pivot is left as it is otherwise: the caller's rotation centre).
 * data: AnimationPoseData. */
int portFunHeadScale(const void* data, int groupId, float* scale, float pivot[3]);
/* Chaos mode (chaos_pc.c): a fun toggle on for its effect (on 1), or back as
 * the setting has it (0). */
void portFunChaosSet(int which, int on);

/* --- Page-turn transitions --------------------------------------------------
 * "page_turn": 0 the fade, 1 a pop-up book's page, 2 a page turned, 3 folded
 * like paper: doors, pipes and plain room changes turn the old room away as
 * the game's own page animations (OFF_d_meku_right, OFF_d_meku, OFF_d_ori,
 * on every disc) instead of fading to black. Online: the host's. */
/* seq_mapchange.c, the fade leaving a room: the type to use instead. */
int portPageTurnLeave(int type);
/* seq_game.c, the fade arriving: the type to use instead. */
int portPageTurnArrive(int type);
/* fadedrv.c fadeEntry, a page flip's animation file: ours while we turn. */
const char* portPageTurnFile(const char* file);

/* --- Battery saver ------------------------------------------------------------
 * "battery_saver": 0 off, 1 on battery, 2 always. While it saves: the game
 * drawn at 30 frames a second (it still runs at 60: every other frame isn't
 * drawn), the internal resolution halved and no in-between frames. Not in
 * online play or replays (their games must draw every frame alike). */
/* main.c, before a frame's drawing: 1 to skip drawing this one. */
int portBatterySkipDraw(void);
/* main.c, the skipped frame's end in place of DEMODoneRender: the frame's
 * pacing and everything the port does once a frame, without presenting. */
void portBatteryFrameUndrawn(void);
/* dispdrv.c dispDraw: this frame isn't drawn (its draw list is let go). */
int portBatteryUndrawn(void);
/* Game frames since the last drawn one (1, or 2 while skipping): draw-time
 * steps that assume one a frame (a paper character's turn) take that many. */
int portDrawFrames(void);
/* settings_aurora.c portSettingsApply: the frame rate and internal resolution to apply. */
const char* portBatteryFramerate(const char* framerate);
int portBatteryResolution(int resolution);
/* Saving now (for the settings page). */
int portBatterySaving(void);

/* --- Chaos mode -------------------------------------------------------------
 * "chaos_mode": a random effect every 30 seconds (giant or tiny Mario,
 * flipped controls, the camera upside down, coin rain, moon gravity, big
 * heads, a Bob-omb's blast; in battle the Crowd Control commands). Offline
 * only: off online, while recording or playing a replay, and with
 * console-accurate mode. */
/* src/system.c-side frame (a marioStMain hook, playmodes): the timer and the effects. */
void portChaosFrame(void);
/* DEMOPad.c, after replays and pings: the flipped controls. */
struct PADStatus;
void portChaosPad(struct PADStatus* pads);
/* The effect running now and its name (English), or NULL; ms left. */
const char* portChaosEffect(int* msLeft);
/* Seconds until the next effect (the settings page), -1 when off. */
int portChaosNext(void);

#ifdef __cplusplus
}
#endif

#endif
