#ifndef PORT_PM64_ENEMY_H
#define PORT_PM64_ENEMY_H

/* Paper Mario 64 enemies (docs/PM64_EXTRAS.md; port/src/common/pm64_enemy_pc.c):
 * each one of the game's enemies (its host: the TTYD enemy whose moves and
 * behaviour it fights with) drawn as the PM64 enemy's sprite from the linked
 * ROM, with PM64's HP and our own fitting attack and defence, and its name
 * and Tattle from the ROM's messages. They are enemy variants
 * (port/include/port/enemy_variant.h: PortEnemyVariant.pm64), so they work
 * wherever variants do: the Enemy maker, the Pit and Master Quest (setting
 * "pm64_enemies"), and as variant codes. Nothing changes without a linked
 * ROM, and they're off online. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pm64EnemyInfo {
    const char* english; /* its name in English, for without the ROM's text */
    int nameMsg;         /* index in the ROM's enemy names (0x29) */
    int tattleMsg;       /* ... and in its Tattles (0x1C) */
    int sprite;          /* NPC sprite archive index */
    int palette;         /* -1: its own */
    int idle, walk, hurt; /* animations */
    int host;            /* BattleUnitType it fights as */
    int hp;              /* PM64's */
    int attack;          /* added to each hit of the host's */
    int defense;         /* taken off each hit on it */
} Pm64EnemyInfo;

int portPm64EnemyCount(void);
/* enemy i (0 .. count-1), NULL past the end */
const Pm64EnemyInfo* portPm64EnemyInfo(int i);
/* 1: enemy i is a stand-in that fights as another character (Monty Mole as the Big Bandit, Shy Guy as the Glitz
 * Pit Goomba): the Pit's and Master Quest's, never a story enemy's look (tools/port/pm64_swap_check.py) */
int portPm64EnemyStandIn(int i);
/* Its name and Tattle from the ROM (the English name / "" without one). */
const char* portPm64EnemyName(int i, char* out, int size);
const char* portPm64EnemyTattle(int i, char* out, int size);
/* A variant that makes enemy i's host enemy i; 0 without a linked ROM. */
struct PortEnemyVariant;
int portPm64EnemyVariant(int i, struct PortEnemyVariant* out);
/* The HP a variant of PM64 enemy pm64 (PortEnemyVariant.pm64, 1-based) starts
 * from in place of its host's; -1 none. */
int portPm64EnemyHp(int pm64);
/* "pm64_enemies" is on, a ROM is linked and no online session runs. */
int portPm64EnemiesActive(void);
/* The Pit and Master Quest (Btl_UnitSetup, before; nothing else armed): PM64
 * counterparts armed for this formation's kinds, each kind by chance. kinds:
 * the formation's BattleUnitTypes. */
void portPm64EnemiesArm(const int* kinds, int count, unsigned seed);
/* Seen in battle on this PC (the Tattle Log's PM64 page): kept in
 * <user>/pm64_tattles.txt. */
int portPm64EnemySeen(int i);
/* luigi_pc.c's draw: a battle pose of a PM64 enemy drawn as its sprite; 1 if it was. */
int portPm64EnemyDrawPose(int poseId, float (*mtx)[4], int xluMode, float rotY, float scale);
/* The Tattle card's picture (src/window/win_log.c's capture, into its tw x th picture): when the unit kind being
 * tattled is a PM64 enemy or boss in this battle (the move's target first), its sprite from the ROM standing as
 * itself (its idle row, a vine's segments, its parts over it), centred and framed as the card frames TTYD's models,
 * in place of the host's model (a boss's unit stands on a stand-in model, Whacka's c_koburon); 1 if drawn. */
int portPm64TattleCapture(int kind, int cardPose, unsigned tw, unsigned th);
/* The card is of a PM64 enemy or boss (as above): its lines under the picture are its name alone, not the TTYD
 * Tattle Log's number and place (its host's, or a stand-in unit type's: "188. Jr. Troopa", "Hooktail Castle"). */
int portPm64TattleCardIs(int kind);
/* The animation a pose plays now ("S_1", "D_1" ...), or fallback. (A battle part's poseName is only the last pose
 * set by hand: a status-picked pose, as after a hit, plays without it.) */
const char* portPm64PoseAnimName(int poseId, const char* fallback);
/* Once at start (Aurora): the battle hook. */
void portPm64MediaInit(void);

#ifdef __cplusplus
}
#endif

#endif
