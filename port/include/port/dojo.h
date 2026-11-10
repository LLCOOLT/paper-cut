#ifndef PORT_DOJO_H
#define PORT_DOJO_H

/* The action-command dojo and timing assist by command type (docs/DOJO.md).
 *
 * The game side (port/src/common/dojo_pc.c): each action command, guard,
 * Superguard and Stylish move is judged as the game judges it, and how far
 * the press was from its window is told in frames ("3 frames early",
 * "Perfect", "2 frames late") in a window of the game's own over the battle,
 * with the window drawn as a bar of frames. The results are kept per command
 * type (attempts, hits, the average offset) in <user>/dojo.txt, the device's
 * own (not in the save). Timing assist reads only its settings
 * ("ac_assist_<type>", 0-3, with "ac_attack_assist" / "ac_guard_assist" at
 * 3, "By command type"), so online the host's settings are what plays; the
 * dojo's results only suggest them.
 *
 * The dojo itself (port/src/aurora/dojo_aurora.c): a practice battle on the
 * Glitz Pit's ring against one of the Glitz Pit's fighters as a dummy that
 * never falls, with Mario's HP, FP and Star Power filled every turn; the
 * dummy attacks every turn, when Mario or the partner chooses Defend, or
 * never. Started from the Modes tab (port/settings/features/dojo.cpp), offline
 * only; Run Away leaves it. Mario is lent back as he was afterwards. */

#ifdef __cplusplus
extern "C" {
#endif

/* The command types, as the stats and the assist keep them. */
enum {
    PORT_DOJO_JUMP,
    PORT_DOJO_HAMMER,
    PORT_DOJO_SPECIAL, /* Mario's Star Powers and other commands */
    PORT_DOJO_GOOMBELLA,
    PORT_DOJO_KOOPS,
    PORT_DOJO_FLURRIE,
    PORT_DOJO_YOSHI,
    PORT_DOJO_VIVIAN,
    PORT_DOJO_BOBBERY,
    PORT_DOJO_MOWZ,
    PORT_DOJO_GUARD,
    PORT_DOJO_SUPERGUARD,
    PORT_DOJO_STYLISH,
    PORT_DOJO_TYPES
};

/* A type's key ("jump", ...; its setting is "ac_assist_<key>") and English name. */
const char* portDojoTypeKey(int type);
const char* portDojoTypeName(int type);
/* Its group: 0 an attack (ac_attack_assist), 1 a guard (ac_guard_assist). */
int portDojoTypeGroup(int type);

/* Kept results (the device's, all dojo sessions); session: since the dojo
 * last started. offsetSum / measured: the average press, in frames outside
 * the window (below 0 early, 0 in time); measured: the presses it is from. */
typedef struct PortDojoStats {
    int attempts, hits, measured, early, late;
    float offsetSum;
} PortDojoStats;
void portDojoGetStats(int type, int session, PortDojoStats* out);
void portDojoResetStats(void);
/* The assist level (0-3) the results suggest for a type: -1 with too few attempts yet. */
int portDojoSuggest(int type);
/* Sets each type's assist to what the results suggest (types without enough
 * attempts as they are) and both groups to "By command type"; how many changed. */
int portDojoApplySuggestions(void);

/* Declares the settings (ac_assist_*, dojo_feedback); safe to call again. */
void portDojoDeclare(void);

/* --- the game's calls (battle_ac.c, battle_acrobat.c, battle_event_cmd.c) --- */
struct BattleWork;
struct BattleUnit;
struct BattleWeapon;
struct BattleWorkPad;
/* A battle starts (BattleActionCommandManagerInit). */
void portDojoBattleStart(void);
/* Each battle frame, from the action-command manager after the pads are read. */
void portDojoBattleFrame(struct BattleWork* wp);
/* BattleActionCommandResult, around the command's result callback. */
void portDojoBeforeResult(struct BattleWork* wp);
void portDojoAfterResult(struct BattleWork* wp);
/* BattleActionCommandCheckDefence, judged (before its pad record is cleared). */
void portDojoGuardJudged(struct BattleUnit* unit, struct BattleWeapon* weapon, int normal, int superguard, int mash,
                         const struct BattleWorkPad* pad, int result);
/* BattleActionCommandSetDifficulty with "ac_attack_assist" 3: the difficulty for unit's command. */
int portDojoAssistAttack(struct BattleUnit* unit, int difficulty);
/* The guard windows (frames) after Accessibility's guard assist (either kind). */
void portDojoAssistGuard(int* normal, int* superguard);
/* BattleAcrobatStart: a Stylish check armed (its window widened by the assist);
 * each battle frame (portDojoBattleFrame calls it): decided since, and how. */
void portDojoStylishStart(struct BattleWork* wp, int* windowStart, int* windowEnd, int endFrame);
void portDojoStylishFrame(struct BattleWork* wp);

/* --- the dojo battle (dojo_aurora.c sets it; the game side reads it) --- */
void portDojoSetRunning(int on);
int portDojoRunning(void);
/* The dummy's unit kind (dojo_aurora.c's copy of the fighter's): it never
 * falls, and attacks only as portDojoAttacks says. */
void portDojoSetDummyKind(const void* kind);
/* The dojo started: the results "since" start again. */
void portDojoSessionStart(void);
/* The judging and its window are wanted now (the dojo, or "dojo_feedback" in any battle). */
int portDojoWanted(void);

/* The dojo's set-up (the Modes tab) */
enum { PORT_DOJO_DUMMY_EVERY_TURN, PORT_DOJO_DUMMY_ON_DEFEND, PORT_DOJO_DUMMY_NEVER };
typedef struct PortDojoSetup {
    int dummy;   /* portDojoDummyName's index */
    int attacks; /* PORT_DOJO_DUMMY_* */
    int partner; /* the first partner out: 0 none, 1-7 the game's party ids */
    int everyMove; /* lend every partner (at Ultra rank), the jump and hammer badges and all Star Powers */
} PortDojoSetup;
/* The dummies: the Glitz Pit's fighters (their names in the game's text when
 * a game runs, as UTF-8; English otherwise). */
int portDojoDummyCount(void);
const char* portDojoDummyName(int i);
/* A partner's name (the game's party id 1-7) as the game says it (UTF-8),
 * English when no game ran yet; a command type's the same, or NULL (then
 * portDojoTypeName's English, translated by the page). */
const char* portDojoPartnerName(int party);
const char* portDojoTypeLabel(int type);
/* Starts it from the field on the next frame; 0 and why when it can't now. */
int portDojoStart(const PortDojoSetup* setup, const char** why);
void portDojoStop(void);
/* The dojo is on: from its start to Mario's pouch put back (portDojoRunning is its battles'). */
int portDojoActive(void);
/* Its map and hooks (modes_aurora.c's portModesRegisterMaps calls it). */
void portDojoRegisterMaps(void);
/* The set-up of the dojo running, while it runs (the dummy's attacks can change meanwhile). */
void portDojoSetAttacks(int attacks);
int portDojoAttacks(void);

#ifdef __cplusplus
}
#endif

#endif
