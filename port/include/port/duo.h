#ifndef PORT_DUO_H
#define PORT_DUO_H

/* Duo moves (port/src/common/duo_pc.c, docs/DUO_MOVES.md; the setting
 * Multiplayer > Duo moves, port/settings/features/duo_moves.cpp): Mario and
 * the partner spend both their moves of the turn on one team attack, built
 * from the game's own animations, with both players' action commands at
 * once. One per partner, from the start; with a second hero (Brothers mode's
 * Luigi, portDuoSetSecondHero) the Bros. moves, both halves Mario's own
 * animations. Their FP comes from the shared pool, as every move's.
 *
 * Where they are: a duo move whose Mario half is a jump is at the end of
 * Mario's Jump list, one whose half is a hammer swing at the end of his
 * Hammer list, and every one of the partner's at the end of the partner's
 * own moves; whoever picks it, the other's move this turn goes with it (it
 * is greyed out when the other has already moved or can't).
 *
 * Who presses what: with two players (co-op with a second controller, or
 * online) Mario's command is player 1's and the partner's player 2's, both
 * on A. With one player (the setting's "Always", co-op without a second
 * controller, and each side of a versus fight) the one player does both: A
 * for Mario, B for the partner, as in the Mario & Luigi games.
 *
 * Everything that changes play reads the game's input and the battle only,
 * so online lockstep carries it; the command's display keeps the game's
 * random numbers as they were (the font takes them).
 *
 * The API below is the move table and its runner, for other modes that pair
 * two heroes (Brothers mode: Mario and Luigi) to reuse: a mode can list its
 * own moves (portDuoRegister) and say who plays each half
 * (portDuoSetPlayers). */

#ifdef __cplusplus
extern "C" {
#endif

struct BattleWork;
struct BattleUnit;
struct BattleWeapon;
struct BattleCommand;

/* the action command both players play at once */
enum {
    PORT_DUO_AC_TIMED = 0, /* a press as each one's attack lands (a mark runs to a line) */
    PORT_DUO_AC_MASH = 1,  /* both mash for a moment, then both attack */
};

/* the hero's half */
enum {
    PORT_DUO_HERO_STOMP = 0,  /* a jump onto the enemy (in the Jump list) */
    PORT_DUO_HERO_HAMMER = 1, /* a hammer swing in front of it (in the Hammer list) */
};

/* the partner's half: how its animations are put together */
enum {
    PORT_DUO_PARTNER_LEAP = 0,  /* runs to a take-off spot and leaps onto the enemy */
    PORT_DUO_PARTNER_DASH = 1,  /* winds up near the enemy, then darts into it */
    PORT_DUO_PARTNER_BLAST = 2, /* walks up to it, glows and blows up (Bobbery) */
};

/* who the second half is (PortDuoMove.second) */
enum {
    PORT_DUO_SECOND_PARTNER = 0, /* the active partner (partnerKind says which) */
    PORT_DUO_SECOND_HERO = 1,    /* a second Mario-kind unit on the same side: Luigi in Brothers mode
                                    (portDuoSetSecondHero); its half is a PORT_DUO_HERO_* in .partner */
};

/* who plays the second half (portDuoSetPlayers) */
enum {
    PORT_DUO_PLAYERS_AUTO = 0, /* from the setting, co-op, online and versus (the default) */
    PORT_DUO_PLAYERS_ONE = 1,  /* one player: A for the hero, B for the partner */
    PORT_DUO_PLAYERS_TWO = 2,  /* the hero's on controller 1, the partner's on controller 2 */
};

#define PORT_DUO_MAX_MOVES 32

/* One duo move. The poses are the partner's own animation names (its
 * unit_party_*.c scripts'); NULL leaves that step's pose as it is. */
typedef struct PortDuoMove {
    const char* key;       /* its message tags: pc_duo_name_<key>, pc_duo_desc_<key> */
    int second;            /* PORT_DUO_SECOND_*: the partner or a second hero */
    int partnerKind;       /* BattleUnitType (UNIT_GOOMBELLA .. UNIT_MS_MOWZ); a second hero: unused */
    int minRank;           /* the partner's rank it needs: 0 Normal, 1 Super, 2 Ultra */
    int fp;                /* its FP, from the shared pool */
    int ac;                /* PORT_DUO_AC_* */
    int partnerOffset;     /* timed: frames from the hero's mark to the partner's (- first) */
    int hero;              /* PORT_DUO_HERO_* */
    int partner;           /* PORT_DUO_PARTNER_*; a second hero: its PORT_DUO_HERO_* */
    int heroPower[3];      /* the hero's hit by boots or hammer level (1..3) */
    int partnerPower[3];   /* the partner's hit by its rank; a second hero's by its boots or hammer level */
    int goodBonus;         /* + to a half whose command was good */
    int perfectBonus;      /* + more when it was just on time */
    int duoBonus;          /* + to the partner's hit when both were good */
    const char* poseWait;  /* the partner while the command runs */
    const char* poseAir;   /* in the leap or dart */
    const char* poseHit;   /* as it hits */
    const char* sfxHit;    /* the partner's hit's sound (or NULL) */
    int element;           /* the partner's hit's BattleWeaponElement */
    int status;            /* the partner's hit's extra on a good command: 0 none, 1 burn, 2 dizzy, 3 sleep */
    int airFrames;         /* frames of the partner's leap or dart */
    int floats;            /* a floating partner (Flurrie): no float while it leaps */
} PortDuoMove;

/* The built-in table and the ones added: count, and move i (NULL past the end). */
int portDuoMoveCount(void);
const PortDuoMove* portDuoMoveAt(int i);
/* Adds a mode's own move (Brothers mode): copied; its key's messages are the
 * mode's to give (portDuoMsg asks the registered resolver). Returns its index or -1. */
int portDuoRegister(const PortDuoMove* move);
void portDuoSetMsgResolver(const char* (*resolve)(const char* tag));
/* Who plays the halves from now on (PORT_DUO_PLAYERS_*). */
void portDuoSetPlayers(int players);
/* A mode with a second hero on the hero's side (Brothers mode: Luigi): the
 * battle unit id of the other hero than the one the game calls Mario now
 * (BattleGetMarioPtr; Luigi's id while Mario's is the game's Mario, Mario's
 * while Luigi acts), or -1 when there is none this battle. NULL: no second
 * hero (the built-in Bros. moves, PORT_DUO_SECOND_HERO, are not listed). The
 * second hero's half is played by player 2 with two players (on A), or by B. */
void portDuoSetSecondHero(int (*otherHeroUnitId)(void));
/* With two players: the controller (0 or 1) a half's unit (a BattleUnit*) is
 * played from, or -1 for the default (the hero's half 0, the second half 1).
 * Brothers mode: Luigi 1 and Mario 0 whichever of them picked the move. A
 * mode that routes the second controller to the first for its own turns
 * (portBrothersRoute) leaves the controllers as they are while
 * portDuoActive(), as co-op's routing does. */
void portDuoSetControllerFor(int (*controllerOf)(const void* unit));

/* Duo moves are on in this battle: the setting, and two players or "Always". */
int portDuoEnabled(void);
/* The move the hero (or the partner) can choose now is i: listed, and with
 * *usable 0 when it's greyed out (the other has moved, can't act). */
int portDuoListed(int i, int* usable);

/* battle_seq_command.c: appends the duo moves of the list `type` (0 Jump,
 * 1 Hammer, 6 the partner's moves) after the list's own; and greys out the
 * ones the other hero can't join now (reason PORT_DUO_REASON). */
void portDuoAddWeapons(struct BattleCommand* cmd, int type, int* num);
void portDuoCheckWeapons(struct BattleCommand* cmd, int count);
#define PORT_DUO_REASON 0x51 /* a greyed-out duo move's reason (its message: pc_duo_busy) */
/* battle_seq.c, Mario's turn starting: 1 when his Jump or Hammer list has a
 * duo move, so the list opens instead of the single move (bit 0 Jump, bit 1 Hammer). */
int portDuoListsMario(struct BattleWork* wp);
/* msgdrv.c msgSearch: the duo moves' own messages (pc_duo_*), or NULL. */
const char* portDuoMsg(const char* tag);

/* A duo move's script runs (its action command, the attack): the co-op
 * routing leaves controller 1 as player 1's while it does
 * (second_player_pc.c), faster battles stay at 1x (battle_speed_pc.c). */
int portDuoActive(void);
int portDuoCommandRunning(void);

#ifdef __cplusplus
}
#endif

#endif
