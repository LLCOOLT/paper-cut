#ifndef PORT_ENEMY_VARIANT_H
#define PORT_ENEMY_VARIANT_H

/* Enemy variants (port/src/common/enemy_variant_pc.c): one of the game's
 * enemies with changed stats, a colour, an extra effect on its attacks taken
 * from another enemy's attack, and a name and Tattle of its own. Made with
 * the Enemy maker (port/settings/features/battlex.cpp), shared as codes, and
 * used by challenges, battle puzzles and Master Quest's formations.
 *
 * The format is plain data on purpose: a mode can fill a PortEnemyVariant in
 * C (a table of them, say) or read one from a code, then arm it for the next
 * battle. Armed variants apply to every enemy of their base kind in that
 * battle, as it enters; the battle's end disarms them.
 *
 *   PCFOE1:<base64url of "key=value;key=value...">
 *
 * keys (any may be left out; a missing one is the default in brackets):
 *   k  base kind, the BattleUnitType number (UNIT_GOOMBA = 1 ...)  [required]
 *   h  max HP in percent of the enemy's own (10-1000)              [100]
 *   p  HP added after the percentage (-99..999)                    [0]
 *   a  attack: added to the damage of each of its hits (-9..20)    [0]
 *   d  defence: taken off each hit on it (-9..20; 0 or less ok)    [0]
 *   c  colour tint as RRGGBBAA hex (FFFFFFFF: none)                [FFFFFFFF]
 *   s  size in percent (25-300)                                    [100]
 *   m  a move borrowed for its attacks (PortVariantMove)           [0]
 *   x  coins added when it is beaten (0-99)                        [0]
 *   n  its name (UTF-8, percent-escaped ';' '=' '%')               [the game's]
 *   t  its Tattle (likewise)                                       [the game's]
 *
 * Online: variants change play, so only arm them from data both sides have
 * (a mode's compiled-in tables, a setting the host sends); the Enemy maker's
 * own list is not sent. */

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_VARIANT_NAME_MAX 32
#define PORT_VARIANT_TATTLE_MAX 400
#define PORT_VARIANT_ARMED_MAX 8

/* The moves a variant's attacks can borrow: the status an existing enemy's
 * attack gives, with a chance and turns like that attack's
 * (enemy_variant_pc.c kMoves). */
typedef enum PortVariantMove {
    PORT_VARIANT_MOVE_NONE,
    PORT_VARIANT_MOVE_BURN,     /* like a Lava Bubble's flame: burned */
    PORT_VARIANT_MOVE_FREEZE,   /* like a Frost Piranha's breath: frozen */
    PORT_VARIANT_MOVE_POISON,   /* like a Poison Pokey's needles: poisoned */
    PORT_VARIANT_MOVE_ELECTRIC, /* like a Dark Puff's charged body: electrified */
    PORT_VARIANT_MOVE_SLEEP,    /* like a Crazee Dayzee's song: asleep */
    PORT_VARIANT_MOVE_DIZZY,    /* a dizzying hit */
    PORT_VARIANT_MOVE_CONFUSE,  /* a confusing hit */
    PORT_VARIANT_MOVE_TINY,     /* like a Magikoopa's spell: shrunk */
    PORT_VARIANT_MOVE_SOFT,     /* a softening hit: defence down */
    PORT_VARIANT_MOVE_STOP,     /* an immobilizing hit */
    PORT_VARIANT_MOVE_DRAIN,    /* its hits heal it by the damage they do */
    PORT_VARIANT_MOVE_COUNT
} PortVariantMove;

typedef struct PortEnemyVariant {
    int baseKind;     /* BattleUnitType */
    int hpPercent;    /* 100: as the game has it */
    int hpAdd;
    int attack;       /* added to each hit's damage */
    int defense;      /* taken off each hit on it */
    unsigned color;   /* 0xRRGGBBAA; 0xFFFFFFFF none */
    int sizePercent;  /* 100: as the game has it */
    int move;         /* PortVariantMove */
    int coins;        /* added when beaten */
    char name[PORT_VARIANT_NAME_MAX];     /* "" the game's */
    char tattle[PORT_VARIANT_TATTLE_MAX]; /* "" the game's */
} PortEnemyVariant;

/* A variant with nothing changed for baseKind. */
void portEnemyVariantInit(PortEnemyVariant* v, int baseKind);
/* A code read (spaces and line breaks ignored; text around "PCFOE1:" too);
 * 0: not a variant's code. Values out of range are clamped. */
int portEnemyVariantParse(const char* code, PortEnemyVariant* out);
/* The variant's code into out (size bytes); its length, 0 if it didn't fit. */
int portEnemyVariantCode(const PortEnemyVariant* v, char* out, int size);
/* The English name of a borrowed move. */
const char* portEnemyVariantMoveName(int move);

/* --- in battle (safe to call from any mode, on the game's frame) --- */

/* Arms up to PORT_VARIANT_ARMED_MAX variants for the next battle (count 0
 * disarms); kept until that battle ends. A copy is taken. */
void portEnemyVariantArm(const PortEnemyVariant* list, int count);
/* The variants armed now, and how many. */
int portEnemyVariantArmed(const PortEnemyVariant** list);

/* The game's hooks (TARGET_PC blocks in the battle code): */
struct BattleUnit;
struct BattleWeapon;
/* battle.c Btl_UnitSetup, each enemy as it enters with its HP set. */
void portEnemyVariantSpawn(struct BattleUnit* unit);
/* The variant an enemy unit is (NULL: none). */
const PortEnemyVariant* portEnemyVariantOf(const struct BattleUnit* unit);
/* battle_damage.c BattleCalculateDamage, before the "no damage" check: the
 * damage with the attacker's attack and the target's defence. */
int portEnemyVariantDamage(const struct BattleUnit* attacker, const struct BattleUnit* target, int damage);
/* battle_damage.c BattleCheckDamage: the weapon with the attacker's borrowed
 * move's status in it (a copy; the weapon itself when none). */
struct BattleWeapon* portEnemyVariantWeapon(const struct BattleUnit* attacker, struct BattleWeapon* weapon);
/* The coins the variants beaten this battle add (battle_seq_end.c). */
int portEnemyVariantCoins(void);
/* The battle ended: disarmed, the name and Tattle texts put back. */
void portEnemyVariantBattleEnd(void);

#ifdef __cplusplus
}
#endif

#endif
