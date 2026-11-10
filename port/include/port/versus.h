#ifndef PORT_VERSUS_H
#define PORT_VERSUS_H

/* Versus battles (port/src/aurora/versus_aurora.c, docs/VERSUS.md): Mario
 * and his partners against Luigi and his, on the Glitz Pit's stage or a chapter's, two
 * players on one screen. Each side is set up in the Start menu's Versus page
 * (port/src/aurora/versus_setup_aurora.c): a level split between HP, FP and
 * BP, up to three partners, the boots and hammer, badges up to the BP and a
 * few items.
 *
 * In the fight Luigi's side is the enemy side (alliance 1) made of player
 * units: a second Mario unit drawn as Luigi and partner units. Mario's side
 * acts in the player phase, Luigi's in the enemy phase, each through the
 * game's own command menu; while Luigi's side acts the game's "Mario", "the
 * partner" and the pouch are Luigi's (the side, portVersusSide), and the
 * second controller is read as the first. The defender guards and
 * Superguards every hit with the normal timing, on their own controller.
 *
 * Nothing here runs unless a versus battle was started: every hook returns
 * at once otherwise, so story battles and online play are untouched.
 *
 * Online (an online session's Start menu, docs/VERSUS.md): the session's
 * games run the set-up page and the fight in lockstep, the guest as Luigi's
 * player; the set-up is the host's, sent with the session's start
 * (portVersusSetupText, portVersusSetupForSession), so both games begin from
 * the same one. Luigi's side can also be the computer (PortVersusSide's
 * controller and skill). */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_VERSUS_PARTNERS 3 /* each side's partners to swap between */
#define PORT_VERSUS_BADGES 32  /* each side's badges */
#define PORT_VERSUS_ITEMS 6    /* each side's items */
#define PORT_VERSUS_STAGES 8   /* the stages a fight can be on (the Glitz Pit first) */

/* who plays a side: a player (on this screen or online), or the computer
 * (Luigi's side only) */
enum { PORT_VERSUS_LOCAL, PORT_VERSUS_ONLINE, PORT_VERSUS_COMPUTER };
/* the computer's skill */
enum { PORT_VERSUS_EASY, PORT_VERSUS_NORMAL, PORT_VERSUS_HARD };

typedef struct PortVersusSide {
    int level;                            /* 1..30: (level - 1) level-ups split below */
    int hpUps, fpUps, bpUps;              /* +5 HP, +5 FP, +3 BP each */
    int gear;                             /* boots and hammer: 1 normal, 2 super, 3 ultra */
    int partners[PORT_VERSUS_PARTNERS];   /* PartyId (1 Goombella .. 7 Ms. Mowz), 0 none; the first comes out */
    int partnerRank;                      /* 0 normal, 1 super, 2 ultra */
    short badges[PORT_VERSUS_BADGES];     /* ItemType, 0 none */
    short items[PORT_VERSUS_ITEMS];       /* ItemType, 0 none */
    int controller;                       /* PORT_VERSUS_LOCAL or (Luigi's side) PORT_VERSUS_COMPUTER */
    int skill;                            /* the computer's: PORT_VERSUS_EASY .. PORT_VERSUS_HARD */
} PortVersusSide;

typedef struct PortVersusRules {
    int stage;      /* 0 the Glitz Pit .. PORT_VERSUS_STAGES - 1 (versus_aurora.c's kStages) */
    int turnTimer;  /* seconds for a command, 0 off */
    int luckBadges; /* Close Call, Pretty Lucky, Lucky Day, Lucky Start allowed */
    int showstopperKo; /* Showstopper knocks out by chance (else heavy damage: portVersusShowstopperSoft) */
} PortVersusRules;

typedef struct PortVersusSetup {
    PortVersusSide side[2]; /* 0 Mario's, 1 Luigi's */
    PortVersusRules rules;
} PortVersusSetup;

/* The set-up as it is now (kept for the next fight; read from and written to
 * <user>/versus.txt). */
PortVersusSetup* portVersusSetup(void);
void portVersusSetupDefaults(PortVersusSetup* s);
void portVersusSave(void);
/* The set-up as versus.txt's text (the host's, for an online session), and
 * a set-up from such a text for this run: an online session's, the same in
 * every game of it (hold: not saved to this player's versus.txt meanwhile,
 * the guest's and a watcher's). */
int portVersusSetupText(char* out, unsigned size);
void portVersusSetupForSession(const char* text, unsigned len, int hold);
/* Checks one side's set-up; NULL when it is fine, else why not (English). */
const char* portVersusCheck(const PortVersusSetup* s, int side);
/* Badges and items allowed (the fair-play rules), a badge's BP, the BP a
 * side has and uses. */
int portVersusBadgeAllowed(int item, const PortVersusRules* rules);
int portVersusBadgeFlagged(int item);
int portVersusItemAllowed(int item);
int portVersusBp(const PortVersusSide* side);
int portVersusBpUsed(const PortVersusSide* side);
int portVersusMaxHp(const PortVersusSide* side);
int portVersusMaxFp(const PortVersusSide* side);
/* A stage's name, in the game's text (the place's own) */
const char* portVersusStageName(int stage);

/* Starts the fight with the set-up (from the field with a file in play); 0
 * and why when it can't now. */
int portVersusStart(const char** why);
/* portVersusStart's checks alone: 1 when it would start now (nothing
 * started), else 0 and why. */
int portVersusCanStart(const char** why);
/* A start asked for but not yet begun (still in the field) taken back: 1 if
 * there was one, 0 when none or the fight already began. */
int portVersusUnask(void);
/* Online: the host's fight isn't on after all. This game's is taken back
 * however far it got: an ask not yet begun dropped (1); one on its way to
 * the ring, or set up there, ended with no winner and back to the field it
 * left (2: the ring left, or its battle run from as it opens). 0: none. */
int portVersusAbort(void);
/* The endless ladder: Mario's side as set up against a computer Luigi, fight
 * after fight until Mario falls, each rung's Luigi stronger (from the field
 * with a file in play; 0 and why when it can't now). The rung being fought
 * (0: none); *best the most rungs won (kept in <user>/versus_ladder.txt),
 * *last the last ladder's (-1: none yet). */
int portVersusLadderStart(const char** why);
int portVersusLadder(int* best, int* last);
/* A versus battle is set up, running or just over (the hooks act). */
int portVersusOn(void);
/* A versus fight asked for (from the ask on, before the frame hook takes it),
 * set up, running or ending (save states refused meanwhile). */
int portVersusBusy(void);
/* In a versus battle: the side acting now (0 Mario's, 1 Luigi's). */
int portVersusSide(void);
/* unit (a BattleUnit*) is one of the versus sides' player units: Mario,
 * Luigi or a partner; outside versus, Mario's side alone (alliance 0). */
int portVersusPlayerUnit(const void* unit);
/* The defender's guard pad record (a BattleWorkPad*) for a hit on unit, or
 * NULL for the game's own (second_player_pc.c's guard pad). *handled is 1 in
 * versus. */
void* portVersusGuardPad(const void* unit, int* handled);
/* makeKey: the second controller read as the first while Luigi's side acts.
 * 1 while a versus battle runs (co-op's own routing stays out of it). */
int portVersusRoute(void);
/* Luigi's player has a controller of their own on this computer: the keyboard a seat swap took to port 2, a
 * test's scripted one, or port 2's controller once it is a player (joined by Start, a seat given) or took Luigi's
 * side by its own A on the set-up page. Never a controller only for lying in port 2 (the seats rule,
 * docs/PLAYERS.md): its turns would be read from a pad nobody picked. */
int portVersusSecondPad(void);
/* The last fight's winner (0 Mario's side, 1 Luigi's; -1 none yet); the
 * seconds left for the command being chosen (-1: no timer or no command). */
int portVersusWinner(void);
int portVersusTimeLeft(void);
/* Who plays a side (the set-up's controller, for this fight). */
void portVersusSetController(int side, int controller);
/* battle_ac.c: for a unit (a BattleUnit*) of the computer's side, whether its
 * action command now is done right by itself (its roll came up: 1, or 0);
 * -1 for any other unit. */
int portVersusAutoCommand(const void* unit);
/* sac_suki.c: a versus fight with the Showstopper rule off, where it does
 * heavy damage instead of its knock-out (1; else 0) */
int portVersusShowstopperSoft(void);
/* battle_damage.c: a Stop's turns from attacker (a BattleUnit*). In a versus
 * fight Luigi's side acts in the enemy phase and statuses tick at the turn's
 * end, so its stops get one turn more, to last through Mario's next turn as
 * Mario's last through Luigi's; time itself everywhere else. */
int portVersusStopTime(const void* attacker, int time);

/* The computer's side (port/src/aurora/versus_cpu_aurora.c), for
 * versus_aurora.c: a fight's start (its skill; its random numbers from the
 * same start), a command's start; each frame of its command, 1 once it has
 * chosen (the menu then makes the attack); a guard's result for a hit on its
 * unit (as BattleActionCommandCheckDefence's); an action command's roll at
 * its start, and whether it came up. */
struct BattleWork;
struct BattleUnit;
struct BattleWeapon;
void portVersusCpuReset(int skill);
void portVersusCpuCommandBegins(void);
int portVersusCpuCommand(struct BattleWork* wp, struct BattleUnit* unit);
/* Tests ($TTYD_AUTO_BATTLE, port/src/aurora/story_test_aurora.c): the same
 * choice, at its hardest, for Mario's side in an ordinary battle (any enemy
 * a target; no Star Powers); items among the choices only with items set */
int portVersusCpuCommandAuto(struct BattleWork* wp, struct BattleUnit* unit, int items);
/* the tests' damage boost on enemies (TTYD_GOD_POWER), which its choices count on; Run Away chosen */
void portVersusCpuAutoBoost(int boost);
void portVersusCpuAutoReset(void); /* a new battle: what its hits did forgotten */
int portVersusCpuEscape(struct BattleWork* wp, struct BattleUnit* unit);
int portVersusCpuDefend(struct BattleWork* wp, struct BattleUnit* unit); /* (tests: Tactics, Defend) */
int portVersusCpuLectureEnd(struct BattleWork* wp, struct BattleUnit* unit); /* a lesson's "End Lesson" */
int portVersusCpuGuard(struct BattleUnit* unit, struct BattleWeapon* weapon);
void portVersusCpuActionCommand(void);
int portVersusCpuActionGood(void);
/* While its side acts, each frame: what it does on the first controller now
 * in a Star Power's game (buttons and directions pressed this frame, the
 * stick; all 0 otherwise) */
typedef struct PortVersusCpuPad {
    unsigned press; /* PAD_BUTTON_* */
    unsigned dirs;  /* DEMO_STICK_* */
    int stickX, stickY;
} PortVersusCpuPad;
void portVersusCpuSacPad(PortVersusCpuPad* pad);

/* The Start menu's set-up page (pause_forms.h's PortPausePage). */
extern const struct PortPausePage gPortVersusSetupPage;

/* mods.cpp: the versus maps (a stage's each, with a room's files of its area), registered once. */
void portVersusRegisterMaps(void);

#ifdef __cplusplus
}
#endif

#endif
