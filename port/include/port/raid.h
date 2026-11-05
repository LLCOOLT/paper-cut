#ifndef PORT_RAID_H
#define PORT_RAID_H

/* The raid boss (port/src/aurora/raid_aurora.c, docs/RAID.md): one of the
 * game's bosses, scaled up for a full party (two to four players: online,
 * local controllers, Brothers mode's Luigi), in four phases that borrow
 * other bosses' attacks and need everyone's Superguards and the duo moves.
 * One boss a week, the same for everyone: the week (Monday to Sunday, UTC)
 * names the boss and the bosses whose attacks its phases borrow, so it needs
 * no network. The relay keeps each week's clears (port/relay/src/index.js,
 * /raid/<WEEK>), and says which boss its week has.
 *
 * Lockstep: everything that changes play comes from the game's own state
 * (the file, the party, the players present as played) and the week, which
 * online is read from the game clock (the host's start: the same in every
 * game of a session), offline from the date; its timers count frames. Draw
 * and board code only read. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_RAID_PHASES 4
#define PORT_RAID_NAMES 4

/* the bosses of the rotation, and the attacks phases can borrow */
int portRaidBossCount(void);
const char* portRaidBossName(int boss);  /* English */
int portRaidBorrowCount(void);
const char* portRaidBorrowName(int borrow); /* English: "Gloomtail's poison breath" */

/* The week now: weeks since the Monday 1969-12-29, UTC (online and in
 * replays from the game clock, so the host's). */
int portRaidWeek(void);
/* That week's Monday, "YYYY-MM-DD". */
void portRaidWeekText(int week, char* out, int size);
/* A week's raid: its boss, and the two attacks its phases borrow. */
int portRaidOfWeek(int week, int* borrowA, int* borrowB);
/* The raid HP of a week's boss for a party of players (2 to 4). */
int portRaidHp(int boss, int players);

/* Starts this week's raid once Mario is free in the field (the Start menu's
 * forms, in the game's frame: the same in every game online); 0 and why when
 * it can't now. */
int portRaidStart(const char** why);
/* The same from the settings page's thread (not online: one player's button). */
void portRaidAsk(void);
/* A raid runs (its map, its fight). */
int portRaidActive(void);

typedef struct PortRaidStatus {
    int active;          /* a raid runs */
    int inFight;         /* its battle is on */
    int week, boss, borrowA, borrowB, players;
    int phase;           /* 1 to PORT_RAID_PHASES */
    int hp, maxHp;       /* the boss's */
    int line;            /* the HP its phase ends at (0 the last) */
    int gateOpen;        /* the phase's condition is met (the shell broken, the fury stopped) */
    int shellHits;       /* phase 2 without duo moves: hits on the shell so far, of shellNeeded */
    int shellNeeded;
    int guards, guardsNeeded; /* phase 3: the heroes who Superguarded, of those standing */
    unsigned frames;     /* the fight's, counted */
    int turns;
} PortRaidStatus;
const PortRaidStatus* portRaidStatus(void);

/* The last raid that ended this run: 1 won, 2 lost or fled (0: none yet); its
 * week, boss, frames, turns and players; and the names of its players
 * (online: the session's; else ""), for the board. */
typedef struct PortRaidResult {
    int result;
    int week, boss, players, turns;
    int phase; /* the phase it ended in */
    unsigned frames;
    unsigned serial; /* counts up with each result */
    char names[PORT_RAID_NAMES][32];
} PortRaidResult;
const PortRaidResult* portRaidLast(void);

/* Once at start (modes_aurora.c's portModesRegisterMaps): the maps and hooks. */
void portRaidInit(void);

#ifdef __cplusplus
}
#endif

#endif
