#ifndef PORT_MODERUN_H
#define PORT_MODERUN_H

/* Test hooks for the modes' full runs (tests/port/testpass/moderun_*.json, group "moderun"), Aurora only, each
 * off unless its variable is set:
 *
 *   TTYD_MODERUN=1            every mode run (the ones Leave covers: port/include/port/mode_leave.h) checked:
 *                             the pouch, the partners and the story kept from before it began (a snapshot 30
 *                             frames old, so a lent loadout put in on the start's own frame isn't taken for the
 *                             file's), then once it is over, compared field group by field group:
 *                               "[moderun] <mode> started (run N)"
 *                               "[moderun] <mode> over after F frames, S s"
 *                               "[moderun] control back after <mode>: F frames, no menu, lending 0"
 *                               "[moderun] check after <mode>: stats same, partners same, badges same, items
 *                                same, key items same, coins A -> B, story same (V)" (or "changed (...)")
 *                             "[moderun] control not back after <mode>" when 3600 frames pass without it
 *   TTYD_MODERUN_EXIT=<n>     exit 0 n game frames after the check of the TTYD_MODERUN_ENDS-th (default 1)
 *                             mode run (a run that ends when its mode does, not at a fixed frame count)
 *   TTYD_MODERUN_LEAVE=<n>    n frames into each run, Leave it as the Settings tab's button does
 *                             (portModeLeave): "[moderun] leave asked (in the field|in a battle)"
 *   TTYD_MODERUN_LEAVE_RUN=<k> only the k-th run is left (default every run)
 */

#ifdef __cplusplus
extern "C" {
#endif

/* each frame, after the modes' own frame (port/src/aurora/modes_aurora.c) */
void portModeRunFrame(void);

/* --- the modes env alone can't drive (port/src/aurora/moderun_battles_aurora.c), each off unless set:
 *
 *   TTYD_MODERUN_PIT_RULE=<k>    every endless Glitz Pit match under condition k (modes_aurora.c's
 *                                kConditions index, 0-13; the roll still made): "[moderun] Endless Glitz Pit
 *                                round N: condition k (rolled R)". 5, "no items", is kept by the auto-battler;
 *                                10, "appeal 3 times", never is (the run lost after its first fight)
 *   TTYD_MODERUN_PIT_ROUNDS=<n>  once n rounds of the endless Glitz Pit are won (the next match drawn), Leave
 *                                as the Settings tab's button does: "[moderun] Endless Glitz Pit: n rounds won"
 *   TTYD_MODERUN_DOJO=<dummy>,<attacks>,<partner>,<every move>  the dojo asked for (portDojoStart) 700 game
 *                                frames after the $TTYD_MAP warp settled, as $TTYD_MODE_TEST's modes are
 *                                ($TTYD_DOJO's start, at the first field frame, can come before the warp)
 *   TTYD_MODERUN_PITCOOP=<n>     the Pit of 100 Trials in co-op asked for the same way; once floor n + 1 is
 *                                reached and Mario is free on it, Leave; while it is set the Pit's Mover
 *                                never comes (gameplus_pc.c portEvtPitMover: his floor has no enemy and its
 *                                pipe opens only by paying him, which the test's script can't tell apart)
 *   TTYD_MODERUN_RAID_GUARD=1    in the raid's fury (phase 3), a guard counts as a Superguard (the
 *                                auto-battler's automatic guards are never Superguards: phase 3 never ends)
 *   (with TTYD_MODERUN) the raid, which isn't one of Leave's modes, gets the same started / over / control
 *                                back / check / done lines as "Raid boss", and TTYD_MODERUN_EXIT
 */

/* modes_aurora.c pitNextMatch: the condition for the match of round step (rolled: the game's own roll) */
int portModeRunPitMatch(int step, int rolled, int count);
/* dojo_aurora.c's frame */
void portModeRunDojoFrame(void);
/* pit_coop_aurora.c's frame */
void portModeRunPitCoopFrame(void);
/* raid_aurora.c checkDamagePre: 1 when this defence result counts as a Superguard (the guard test hook) */
int portModeRunRaidGuard(int defenceResult);
/* raid_aurora.c's frame: active, a raid on */
void portModeRunRaidFrame(int active);

/* --- the Paper Tower climbed by itself (port/src/aurora/moderun_climb_aurora.c; tests/port/testpass/
 * moderun_tower.json), off unless its variable is set:
 *
 *   TTYD_TOWER_AUTO=1         each walking floor of a climb, Mario walked from the way in to the way out by the
 *                             controller, as a player would: R held (Paper mode) through a paper gate and across a
 *                             wind wall; under a tube gate R held and the stick spun until he rolls up (Tube
 *                             mode), rolled under it, then B to stand; stuck: a jump, then along the wall (as the
 *                             input scripts' walk_to). The battle floors are the auto-battler's
 *                             (TTYD_AUTO_BATTLE). Mario needs the Paper and Tube curses (TTYD_ITEMS=2,3 on a
 *                             TTYD_STORY=405 file; a finished file has them). Its lines:
 *                               "[tower] auto: floor N: <map>, out by <hit>, gates <kinds>"
 *                               "[tower] auto: floor N gate G (paper|tube|wind) through (paper mode|tube
 *                                mode|walking)"
 *                               "[tower] auto: floor N crossed in F frames"
 *                               "[tower] auto: floor N not crossed in F frames (...)", then exit 6
 *   TTYD_TOWER_AUTO=nogates   the same with every floor's gates taken off at the climb's start: the fallback
 *                             for when the gates can't be driven, a separate scenario (moderun_tower_nogates)
 *   TTYD_TOWER_AUTO_MAX=<n>   frames one floor may take before that exit (default 5400)
 *   TTYD_TOWER_AUTO_START=<n> without TTYD_TOWER_TEST, the climb itself asked for (portTowerStart, as Modes'
 *                             row) once Mario has been free in the field n frames (default 700): "[tower]
 *                             auto: climb asked on <map>" (or "... start refused: <why>"); the plan the
 *                             climb's seed's, floor 1 untouched (TTYD_TOWER_TEST puts its room there, gates off)
 */
typedef struct PortTowerAutoFloor {
    int floor;        /* 1-30 (0: no climb) */
    int battle;       /* a battle floor */
    int reverse;      /* in by the east way ("e_bero"), out by the west */
    int gatesLive;    /* the floor's gates in force (its room's ways read) */
    int gateCount;
    int gateKind[3];  /* PORT_TOWER_PAPER, _TUBE or _WIND (port/include/port/postgame.h) */
    float gateAt[3];  /* where on the way across, 0-1 */
    const char* map;  /* the floor's map (the host's name) */
} PortTowerAutoFloor;
/* the climb's floor now (port/src/aurora/paper_tower_aurora.c); 0: no climb */
int portTowerAutoFloor(PortTowerAutoFloor* out);
/* TTYD_TOWER_AUTO=nogates: the climb's start takes every floor's gates off */
int portTowerAutoGatesOff(void);
/* from portTowerRegisterMaps: the climber's hook, when TTYD_TOWER_AUTO is set */
void portTowerAutoInit(void);

/* --- the party modes' and versus's full runs (port/src/aurora/moderun_party_aurora.c; tests/port/testpass/
 * moderun_quiz.json, moderun_cards.json, moderun_bingo.json, moderun_versus*.json), off unless set:
 *
 * The quiz, the Tattle-card duels and Bingo lend nothing, so Leave doesn't cover them: with TTYD_MODERUN=1
 * their runs are watched alike, with the same lines ("Quiz", "Tattle cards", "Bingo" for <mode>):
 *   "[moderun] <mode> started (party run N; before it: level L, coins C, story S)"
 *   "[moderun] <mode> over after F frames[: player P wins, N points | : wins <opponent> a -> b ...]"
 *   "[moderun] control back after <mode>: F frames, no menu" (60 frames of control, no party game up)
 *   "[moderun] check after <mode>: ..." (as moderun_aurora.c's, against the pouch just before it)
 *   "[moderun] done: N party run(s) checked", TTYD_MODERUN_EXIT frames after the check (exit 0)
 *   "[moderun] control not back after <mode> ..." after 3600 frames (exit 6 with TTYD_MODERUN_EXIT)
 * and, once the $TTYD_MAP warp has settled:
 *   TTYD_MODERUN_TATTLES=all|<n>  every Tattle Log enemy tattled (or the first n): a card deck
 *                             ("[moderun] tattles: N set")
 *   TTYD_MODERUN_DUEL=<n>     a duel against Westside card player n (0 rookie, 1 regular, 2 champion), as
 *                             talking to them asks ("[moderun] duel refused: ..." when not)
 *   TTYD_MODERUN_BINGO=<seed> a new Bingo card, as the page's New card; once a line is done ("[moderun] Bingo:
 *                             N line(s), ..."), TTYD_MODERUN_BINGO_CLEAR frames later (default 120) the card
 *                             cleared, as the page's Clear: the run's end
 *   TTYD_MODERUN_VERSUS_CPU=1 a versus fight's Mario side chosen by the versus computer too (the story's auto
 *                             battle stands aside in versus; its action-command assist and end-of-battle A
 *                             still apply)
 */
/* each frame, from party_aurora.c's frameHook, before the party games' frames */
void portModeRunPartyFrame(void);
/* versus_aurora.c's commandInputPre: Mario's side is the computer's too ($TTYD_MODERUN_VERSUS_CPU) */
int portModeRunVersusCpu(void);

#ifdef __cplusplus
}
#endif

#endif
