#ifndef PORT_VERSUS_H
#define PORT_VERSUS_H

/* Versus battles (port/src/aurora/versus_aurora.c, docs/VERSUS.md): Mario
 * and his partners against Luigi and his, on the Glitz Pit's stage, two
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
 * Hooks for later stages: portVersusSetController (who plays a side: a
 * player here, the online guest or the computer later). */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_VERSUS_PARTNERS 3 /* each side's partners to swap between */
#define PORT_VERSUS_BADGES 32  /* each side's badges */
#define PORT_VERSUS_ITEMS 6    /* each side's items */

/* who plays a side (stage 1: only players on this screen) */
enum { PORT_VERSUS_LOCAL, PORT_VERSUS_ONLINE, PORT_VERSUS_COMPUTER };

typedef struct PortVersusSide {
    int level;                            /* 1..30: (level - 1) level-ups split below */
    int hpUps, fpUps, bpUps;              /* +5 HP, +5 FP, +3 BP each */
    int gear;                             /* boots and hammer: 1 normal, 2 super, 3 ultra */
    int partners[PORT_VERSUS_PARTNERS];   /* PartyId (1 Goombella .. 7 Ms. Mowz), 0 none; the first comes out */
    int partnerRank;                      /* 0 normal, 1 super, 2 ultra */
    short badges[PORT_VERSUS_BADGES];     /* ItemType, 0 none */
    short items[PORT_VERSUS_ITEMS];       /* ItemType, 0 none */
    int controller;                       /* PORT_VERSUS_LOCAL (stage 1) */
} PortVersusSide;

typedef struct PortVersusRules {
    int stage;      /* 0: the Glitz Pit */
    int turnTimer;  /* seconds for a command, 0 off */
    int luckBadges; /* Close Call, Pretty Lucky, Lucky Day, Lucky Start allowed */
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

/* Starts the fight with the set-up (from the field with a file in play); 0
 * and why when it can't now. */
int portVersusStart(const char** why);
/* A versus battle is set up, running or just over (the hooks act). */
int portVersusOn(void);
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
/* The last fight's winner (0 Mario's side, 1 Luigi's; -1 none yet); the
 * seconds left for the command being chosen (-1: no timer or no command). */
int portVersusWinner(void);
int portVersusTimeLeft(void);
/* Who plays a side (later stages: online, computer). */
void portVersusSetController(int side, int controller);

/* The Start menu's set-up page (pause_forms.h's PortPausePage). */
extern const struct PortPausePage gPortVersusSetupPage;

/* mods.cpp: the versus map (with the Glitz Pit's files), registered once. */
void portVersusRegisterMaps(void);

#ifdef __cplusplus
}
#endif

#endif
