#ifndef PORT_STORY_TEST_H
#define PORT_STORY_TEST_H

/* Test-only hooks for playing the story unattended (tools/port/story_sweep.py;
 * docs/PC_PORT.md's environment table): each is off unless its variable is
 * set, and none is ever on in an online session or a replay.
 *
 *   TTYD_EVENT=<row>          start a new game at that row of the game's own
 *                             story table (src/event.c's event_dt, counted
 *                             from 0 over every stage), as its scenario
 *                             select does (seq_e3.c): the story just before
 *                             it, every set-up function up to it, its
 *                             partners, its map and entrance
 *   TTYD_AUTO_BATTLE=1        Mario's and the partner's commands chosen by
 *                             the versus computer (hard; no Star Powers),
 *                             their action commands and guards automatic, A
 *                             once a battle is ending; no
 *                             items (TTYD_AUTO_BATTLE_ITEMS=1: items too; with
 *                             no TTYD_GOD, items when a knock-out is near);
 *                             "[story] STUCK" after TTYD_AUTO_BATTLE_TURNS
 *                             commands in one battle (default 60), then Run
 *                             Away, and exit 6 (also after
 *                             TTYD_AUTO_BATTLE_FRAMES frames, default 3600,
 *                             with no new command: a move that never ends);
 *                             Bill Blasters targeted first;
 *                             TTYD_FOE_HP=<n>: every enemy's HP (and its
 *                             most) n from the battle's first command (a
 *                             foe that outlasts a first hit)
 *   TTYD_GOD=1               Mario's side takes no damage in battle, and
 *                             its HP and FP are kept full in the field;
 *                             TTYD_GOD_POWER=<k>: Mario's side's hits on the
 *                             enemies do k times the damage, at least k
 *                             (whatever their defence)
 *   TTYD_GLITZ_RULE=<id>      every Glitz Pit match booked with that
 *                             condition (battle_actrecord.h; 0 or "none":
 *                             no condition, always kept):
 *                             port/src/common/rules_pc.c
 *   TTYD_AUTO_TEXT=1          A pressed whenever a message waits for it;
 *                             choices answered by TTYD_AUTO_CHOICE (below);
 *                             with no control and nothing to read, A now and
 *                             then (a list to pick from, a screen that waits);
 *                             TTYD_AUTO_TEXT=2: rarely with control too (a
 *                             title card that waits with Mario's keys on)
 *   TTYD_AUTO_TEXT_DELAY=<n>  a slow reader: each message page held n frames
 *                             before A (a long scene at a player's pace, its
 *                             scripts' clocks running as long as theirs)
 *   TTYD_AUTO_CHOICE=<rules>  "<tag>:<n>,...,*:<n>": the n-th option (0 the
 *                             first, -1 the last) of the choice whose text,
 *                             or the message before it, has that tag;
 *                             default the first
 *   TTYD_STORY_WATCH=<s>      every story change logged with its frame;
 *                             "[story] STUCK" and the running scripts when
 *                             it hasn't changed in s seconds of game time
 *                             (exit 6 with TTYD_STORY_UNTIL)
 *   TTYD_STORY_UNTIL=<v>      exit 0 once the story reaches v (or the map is
 *                             TTYD_STORY_UNTIL_MAP, or with
 *                             TTYD_STORY_UNTIL_CONTROL=<n> Mario has had
 *                             control n frames on TTYD_EVENT's map: a scene
 *                             that leaves the story as it was is over),
 *                             TTYD_STORY_SETTLE frames
 *                             later (default 300), with a picture
 *                             (TTYD_SHOT_DIR); exit 7 at a Game Over
 *   TTYD_STORY_KICK=<sym>[,...]  when the story hasn't moved and Mario has
 *                             had control for TTYD_STORY_KICK_AFTER frames
 *                             (default 240), start the next of these scripts
 *                             as the game would: the case (A in front, a
 *                             touch) or NPC talk or map object that runs it,
 *                             else on its own. A symbol is the executable's
 *                             name (rel_<area>__<name>); "*" before it: a
 *                             pointer to the script (a static's export);
 *                             "case:<hit>": that hit object's case (a door);
 *                             "talk:<sym>": only as an NPC's talk;
 *                             "walk:<hit>" or "walk:<x>:<z>" ("_" keeps
 *                             Mario's own): Mario walked there
 */

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* $TTYD_STORY_ROW=<row> with the $TTYD_MAP warp (port/src/common/trace_pc.c;
 * tools/port/revisit_sweep.py): the new game set up as TTYD_EVENT's row
 * would set it up, on the warp's map instead; 0 when there is no such row */
int portStoryTestRowSetUp(int row);

/* $TTYD_EVENT's warp, run every frame from portTraceFrame (the title too) */
void portStoryTestEventFrame(void);

/* src/sdk/DEMOPad.c, after the input script: the text advance's presses */
void portStoryTestPad(PADStatus* pads);
/* port/mods/mods.cpp (headless: main_headless.c), at start: the hooks set up
 * (only when a variable is set) */
void portStoryTestInit(void);
/* Builds without code mods (headless): the hooks the mods' host runs on
 * Aurora, called straight from the game's functions instead (the pre hooks
 * before the body, the frame's after marioStMain), so a run plays out the
 * same on both backends; each does nothing unless its test is on. */
void portStoryTestCommandInitPre(void** wp);
void portStoryTestCommandInputPre(void** work);
void portStoryTestDamagePre(s32* unitId, void** target, void** part, s32* damage, s32* fpDamage,
                            u32* result, u32* pattern, s32* flag);
void portStoryTestFramePost(void);
/* Aurora (an input script's "key NAME [FRAMES]", input_pc.c): a keyboard key (SDL's name: Escape, Return,
 * Backspace, A...) down or up, as the window's own event: the menus and pages see it as a key a player
 * pressed. 0 when SDL doesn't know the name. */
int portStoryTestKey(const char* name, int down);

#ifdef __cplusplus
}
#endif

#endif
