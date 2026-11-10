/* Paper Mario 64's bosses (port/src/aurora/pm64_bosses_aurora.c, the engine): the types a boss file fills in.
 * Each file in this folder is one fight's units (or the story's foes), registered in registry.h.
 * Links between units (lead1, spawn1, with1) count from 1 within the file. */
#pragma once

#include "port/pm64.h"
#include "battle/battle_unit.h"

/* PM64 runs its battles at 30 frames a second, TTYD at 60: a frame count from a PM64 script (a Wait, an
 * animation hold, a jump's duration) is PM64_FRAMES(n) here, so it plays at PM64's own speed. A speed per
 * frame (SetMoveSpeed, a JumpSetting) is PM64's halved. */
#define PM64_FRAMES(n) ((n) * 2)

/* one target: a jump onto it, a rush into it, a shot at it, a slam (a big leap down onto it); everyone: a blow,
 * a quake; itself: a heal, a charge, a warning (it gathers its strength: its next turn is the move named) */
/* MV_POUND: walks up, then its row, row + 1 and row + 2 (wind-up, arms up, the blow); MV_SWARM: from where it
 * stands, a swarm of darkness over the target */
/* (engine 2): MV_SUMMON brings in its companions that are gone (its with1,
 * or `count` of `unit1`); MV_CLONES makes `count` illusory copies of `unit1` (they vanish when hit, and when it
 * is hit); MV_SWALLOW draws its companions in and heals `power` HP for each; MV_REST does nothing (a turn it
 * waits). Kinds below MV_HEAL are attacks. */
/* MV_BARRIER raises its barrier (Bowser's Star Rod): its attacks hurt nothing until a Star Power special hits it */
enum {
    MV_JUMP, MV_RUSH, MV_SHOT, MV_SLAM, MV_POUND, MV_SWARM, MV_ALL, MV_QUAKE,
    MV_HEAL, MV_CHARGE, MV_WARN, MV_SUMMON, MV_CLONES, MV_SWALLOW, MV_REST, MV_BARRIER
};

/* a move's element (TTYD's: what it is, what puts a fiery unit out, what an immune unit shrugs off) */
enum { EL_NORMAL, EL_FIRE, EL_ICE, EL_BLAST, EL_SHOCK };
/* a status a move gives the target (statusTurns turns, `statusChance` % (0: 100)) */
enum { ST_NONE, ST_FREEZE, ST_BURN, ST_DIZZY, ST_SLEEP, ST_STOP, ST_POISON, ST_SHRINK, ST_CONFUSE };
/* when a move may be played (else its else1 is, or a move by weight) */
enum {
    C_ANY,
    C_MATES,    /* one of its companions (with1, or unit1) is standing */
    C_NO_MATES, /* none is */
    C_CLONES,   /* one of its clones is standing */
    C_NO_CLONES,
    C_FROZEN,   /* Mario is frozen */
    C_HURT,     /* below its max HP */
    C_UNIT,     /* unit1 is standing (General Guy's Light Bulb) */
    C_NO_UNIT,
    C_NOT_FROZEN, /* Mario isn't frozen (Crystal King's Ice Bolt; frozen, its else1 the Icy Breath) */
    C_UNIT_PARTNER /* unit1 is standing and the partner isn't KO'd (General Guy's Lightning: general_guy.c
                      EVS_TakeTurn, ActorExists ACTOR_BULB and the partner not STATUS_FLAG_KO) */
};
/* a move's look (BossMove.fx): PM64's own effect for it, played from the game's PM64 effect library (src/effect/n64:
 * the effects TTYD carries over from PM64; the engine's bossFx), timed by the move: its wind-up (`windup`), the
 * moment it lets go (its travel or breath before the hit), and each hit. The PM64 effect each stands for is named
 * at its case in pm64_bosses_aurora.c bossFxStep. */
enum {
    FX_NONE,
    FX_FIRE_BREATH,  /* EFFECT_FIRE_BREATH from the mouth over the target (Bowser's Fire Breath) */
    FX_SPIT_FIRE,    /* EFFECT_63 type 1, a ball of fire, and EFFECT_EMBERS where it lands (Lava Piranha) */
    FX_FIRE_STREAM,  /* EFFECT_63 type 0, a stream of fire, and EFFECT_EMBERS (fiery Lava Piranha) */
    FX_PETIT,        /* EFFECT_FLAME on the Bud as it spits, the Petit's flame onto the target (Lava Buds) */
    FX_ICE_BOLT,     /* EFFECT_COLD_BREATH, then EFFECT_LIGHTNING_BOLT in white and ice blue (Crystal King) */
    FX_ICY_BREATH,   /* EFFECT_SPARKLES, then EFFECT_HUFF_PUFF_BREATH, from him and each clone (Crystal King) */
    FX_SPIT_BITS,    /* EFFECT_MISC_PARTICLES (ice glints) where each Bit breaks on the target */
    FX_HURRICANE,    /* EFFECT_HUFF_PUFF_BREATH and the target spun round (Huff N. Puff) */
    FX_SKY_BOLT,     /* EFFECT_LIGHTNING_BOLT from the sky onto the target (Jr. Troopa, Huff's direct lightning) */
    FX_GROUND_BOLTS, /* EFFECT_LIGHTNING_BOLT, ten onto the ground across the player's side (Huff's ground lightning) */
    FX_BULB_BOLT,    /* EFFECT_SNAKING_STATIC on the Light Bulb, then a bolt from it at each target (General Guy) */
    FX_ROD_BOLT,     /* EFFECT_LIGHT_RAYS and EFFECT_ENERGY_IN_OUT on the Star Rod, a bolt from it (Bowser) */
    FX_ROD_WAVE,     /* EFFECT_LIGHT_RAYS on the Star Rod, then EFFECT_ENERGY_SHOCKWAVE over everyone (Bowser) */
    FX_SPELL,        /* EFFECT_GATHER_ENERGY_PINK on the staff, then EFFECT_SHAPE_SPELL at the target (Jr. Troopa) */
    FX_STAFF_BOLT,   /* EFFECT_GATHER_ENERGY_PINK on the staff, then FX_SKY_BOLT's bolt (Jr. Troopa's Lightning) */
    FX_BOMB,         /* the bomb's blast on the target (General Guy's Bomb Toss) */
    FX_DEBRIS,       /* EFFECT_CHOMP_DROP dust from the ceiling, then rocks onto each target (Tutankoopa) */
    FX_BLOCK,        /* EFFECT_GATHER_ENERGY_PINK on the rod, then the block's crash on the target (Kammy) */
    FX_SHELL_FLARE,  /* EFFECT_LENS_FLARE on the spinner as it lands back and points (the Koopa Bros.) */
    FX_LEECH,        /* EFFECT_RECOVER on it as it drains the target (the Forest Fuzzy's Leech) */
    FX_MALLET,       /* EFFECT_FIREWORK, the burst of a blow, on the target (the Spy Guy's Mallet) */
    FX_COUNT
};

#define BOSS_HITS_MATES (-1) /* hits: once per companion standing (each flies at Mario and is used up: Spit Bits) */
/* byHp bits: its power drops with its HP (power x HP share, at least 1); its hits do (hits x share, at least 1) */
#define BY_HP_POWER 1
#define BY_HP_HITS 2

typedef struct {
    signed char kind, power, row, weight;
    signed char phase; /* 0 any time, 1 above half HP, 2 at half or below, 3 only as a warning's follow-up */
    const char* sfx;
    signed char next1;  /* the move (index + 1) its next turn plays; 0: picked by weight */
    signed char after1; /* once this move has been played afterN times: the next turn's move instead */
    signed char afterN;
    /* --- engine 2 (all optional; name them: .element = EL_ICE) --- */
    signed char element;      /* EL_*: the move's element */
    signed char status, statusTurns, statusChance; /* ST_*: given to the target */
    signed char release;      /* two poses: `row` the wind-up, this the row it lets go in (0: one pose) */
    unsigned char windup;     /* PM64 frames of the wind-up (0: the engine's own) */
    signed char hits;         /* hits on the target (0: 1); BOSS_HITS_MATES */
    signed char gang;         /* with BOSS_HITS_MATES: its companions of its own kind join in and come back (Tuff
                                 Puffs); they don't act again this round */
    signed char unit1;        /* MV_SUMMON / MV_CLONES: the unit (0 for MV_SUMMON: its with1); C_UNIT: the unit */
    signed char count;        /* MV_SUMMON / MV_CLONES: how many at most (0: 1, or MV_SUMMON's with1 all) */
    signed char uses;         /* at most this many times a fight (0: no limit) */
    signed char cond;         /* C_*: when it may be played */
    signed char else1;        /* the move (index + 1) played when its condition fails */
    signed char pre;          /* looked at first every turn, ahead of the pattern (which it doesn't move on) */
    signed char partner;      /* it hits the partner too, for this power (0: Mario alone) */
    signed char swell;        /* % bigger while it plays it (Huff N. Puff's inhale: 25) */
    signed char byHp;         /* BY_HP_* bits */
    const signed char* chance; /* {HP share %, chance %, ...0}: ascending; below the first share it fits, played
                                  that often in 100 (no table: always). Crystal King's Recover: {25,100, 40,30, 55,10, 0} */
    unsigned msg;             /* a line before it the first time (PM64_MSG) */
    signed char offstage;     /* that line from off the field (a plain box, no speaker) */
    signed char cooldown;     /* after it plays, this many of its turns pass before it may again (Bowser's Body
                                 Slam: 1, "2 turns apart") */
    signed char perMate;      /* + this much power per companion standing (Huff N. Puff's Group Slam: 1) */
    signed char mateHits;     /* with BOSS_HITS_MATES: hits from each companion (0: 1) */
    signed char post;         /* the row once the hit has landed (0: none; PostExhale, ExitShell) */
    signed char darken;       /* the stage darkens over this many PM64 frames first (lightning; back after) */
    signed char hazard;       /* with MV_WARN: HZ_* bits it has while it holds its strength (Huff charged:
                                 HZ_SHOCK); gone with its next attack */
    signed char flat;         /* its power isn't doubled while its barrier is up (PM64 final_bowser_2.c
                                 DMG_BOOSTED_SHOCKWAVE 6 = DMG_SHOCKWAVE) */
    /* --- its Paper Mario sounds: `sfx` (the hit) may be "SOUND_X|SFX_Y" (PM64's
       sound with the TTYD stand-in, port/pm64_sfx.h) or "SOUND_X|" (PM64's alone: nothing without a ROM) --- */
    const char* wind;         /* PM64's sound as the wind-up starts (ROM only: there was none) */
    const char* go;           /* PM64's sound as it lets go (a shot's, in place of the engine's own stand-in) */
    signed char fx;           /* FX_*: its look (PM64's effect for it; FX_NONE: the engine's own puffs alone) */
    signed char lead;         /* only while Mario's HP share beats its own by this many points, then by `chance`
                                 (PM64 final_bowser_1.c EVS_Recover: 25; 0: whatever Mario's HP) */
} BossMove;

/* a unit's own Paper Mario sounds (the decomp's SOUND_ names, port/include/port/pm64_sound_ids.h; cited per sound
 * in each file). The engine plays each where it played a TTYD sound before, with that sound as the stand-in (no
 * ROM, or "pm64_sfx" off: as before); where it played none, PM64's alone (with a ROM only). NULL: as before. */
typedef struct {
    const char* step[2]; /* footsteps while it walks, left and right (actors.yaml walkSound) */
    unsigned char stepDist; /* walked between steps (0: 30, actors.yaml stepDelay) */
    const char* jump;    /* it takes off (actors.yaml jumpSound) */
    const char* land;    /* it lands from a leap, a hop or a slam */
    const char* hurt;    /* it's hit (actors.yaml hurtSound, or its EVS_Hit's), in place of TTYD's thud */
    const char* death;   /* it falls for good */
    const char* power;   /* MV_CHARGE / MV_WARN: it gathers its strength */
    const char* heal;    /* MV_HEAL, and MV_SWALLOW's heal */
    const char* summon;  /* MV_SUMMON */
    const char* clones[2]; /* MV_CLONES: as it starts, and as each copy glides off */
    const char* inhale;  /* MV_SWALLOW: it draws its companions in */
    const char* cast[2]; /* MV_BARRIER: the rod gathers light, then lets it go */
    const char* barrier[2]; /* MV_BARRIER: the barrier goes up, then the shockwave */
    const char* exit;    /* it gives way to the unit that takes its place (a transform, a flight) */
    const char* swarm[4]; /* MV_SWARM: it gathers itself, the swarm sets off, a squeeze, it disperses */
    const char* flip;    /* a tower brother flips onto his back */
} Pm64BossSounds;

#define BOSS_MOVES 8
#define BOSS_WITH 6 /* companions beside a unit */

/* how a unit gives way to its spawn1 (exit), and how a unit comes in (enter) */
enum { EXIT_FLEE, EXIT_BEATEN, EXIT_TRANSFORM };
enum { ENTER_RIGHT, ENTER_LEFT, ENTER_PLACE, ENTER_DROP };
/* immune / hazard bits */
#define IM_FIRE 1
#define IM_ICE 2
#define IM_BLAST 4
#define IM_SHOCK 8
#define HZ_FIRE 1  /* burns who touches it (a jump on it): TTYD's fiery counter */
#define HZ_SPIKE 2 /* spiky on top */
#define HZ_SHOCK 4
#define HZ_ICE 8

typedef struct {
    const char* english;
    int nameMsg, tattleMsg; /* the ROM's enemy names (0x29) and Tattles (0x1C) */
    int sprite, palette;
    float scale;            /* drawn size (1: PM64's NPC scale as the enemies) */
    int unitType;
    int hp, defense;        /* PM64's */
    int width, height;
    signed char idle, walk, jump, hurt; /* jump: the row before a jump or slam (a squeeze, a lean) */
    BossMove moves[BOSS_MOVES];
    signed char lead1;  /* the unit (index + 1) its fight starts with, when not itself */
    signed char spawn1; /* the unit (index + 1) that takes its place when it falls */
    signed char hops;   /* it gets about by hops, not by walking */
    signed char home;   /* a story enemy's PM64 home chapter (its numbers scaled to where it's met); 0: a boss */
    signed char with1[BOSS_WITH]; /* the units (index + 1) that fight beside it from the start */
    signed char angry;    /* its idle row while it has gathered its strength (-1 none, 0: its idle) */
    signed char rise, fall, land; /* a slam's rows up, down and landing (0: the move's row) */
    signed char quake;    /* the ground shakes under its steps while it runs (PM64's EVS_PlayFootstepQuaking) */
    signed char lineMove; /* the move (index + 1) it speaks its line before, the first time (PM64's ActorSpeak) */
    unsigned lineMsg;     /* that line (the ROM's message id; 0 none) */
    unsigned fleeMsg;     /* its line as it flees (0 none) */
    /* --- engine 2 (all optional; name them: .fly = 40) --- */
    short at[3];          /* as a companion: where it stands from its lead's home (x back, y up, z); 0 0 0: in line */
    signed char phaseAt;  /* its spawn1 takes over at this HP share % (0: 10; -1: as it would fall) */
    signed char exit;     /* EXIT_*: how it gives way to its spawn1 */
    signed char enter;    /* ENTER_*: how it comes in (as a spawn1, a summon, a wave) */
    signed char revive;   /* as a spawn1: 1 its with1 replace the old unit's companions; 2 its with1 join them */
    signed char leaveHits; /* it gives way after it's been hit in this many turns (Shy Squad: 2) */
    signed char immune;   /* IM_* bits */
    signed char hazard;   /* HZ_* bits */
    signed char douse;    /* with HZ_FIRE: frozen, its fire goes out and it's stuck this many turns (2) */
    signed char illusory; /* a clone: hits go through it and it vanishes; not counted for the win */
    signed char tower;    /* a Koopa Bros. tower brother (see the guide) */
    signed char fly;      /* it hovers this high (hammers can't reach it) */
    signed char shrink;   /* % smaller at no HP (drawn size follows its HP: Huff N. Puff) */
    signed char wobble;   /* % its idle size wobbles */
    signed char shed1;    /* a hit sheds this unit (index + 1; Tuff Puffs off Huff N. Puff) */
    signed char shedMax;  /* at most this many of it out (0: 4) */
    signed char hpRows, hpStep; /* its rows follow its HP: hpRows stages, each lost adds hpStep (Shy Stack) */
    signed char tipRow, downRow, crouchRow; /* a tower brother's rows: tipping, toppled, under another */
    unsigned startMsg;    /* its line at its first turn (PM64 EVS_HandlePhase PHASE_PLAYER_BEGIN, the first time) */
    signed char lowAt;    /* its line the first turn below this HP share % ... */
    unsigned lowMsg;      /* ... (Jr. Troopa: MSG_CH8_0060 under 15 of 60) */
    signed char talkRow;  /* its row while it speaks a line (0: the move's row, or its idle) */
    const signed char* order;  /* a fixed turn order: moves (index + 1), 0-ended, round and round */
    signed char chargedPal;    /* its palette + 1 while it holds its strength (Huff N. Puff's "Charged": 2) */
    const signed char* layers; /* a sprite in parts: triples {row, part, part} (-1 none), -1-ended (Huff:
                                  body, face, arms); a row not listed is drawn alone */
    /* --- engine 2: the Light Bulb, the Koopa Bros.' lines, the Star Rod barrier --- */
    signed char lamp;     /* it glows as a lit bulb (General Guy's Light Bulb, PM64 EFFECT_BULB_GLOW) */
    unsigned tipMsg;      /* a tower brother's: the top one's line as the tower tips (once per tipping) */
    unsigned buildMsg;    /* ... and as the tower first stands (once a fight) */
    signed char barrierPal; /* its palette + 1 while its barrier is up (Bowser's StarRod palette: 2) */
    signed char mockRow;  /* its row as an attack bounces off its barrier (PM64 EVS_NoDamageHit: Mock) */
    unsigned bounceMsg;   /* its line the first time an attack bounces off its barrier */
    unsigned breakMsg;    /* its line the first time its barrier breaks */
    /* --- its Paper Mario sounds and music --- */
    const Pm64BossSounds* sounds; /* NULL: the engine's TTYD sounds */
    unsigned char song;   /* a fight's first unit: PM64's battle song for it (the decomp's SONG_ id, include/enums.h),
                             from the linked ROM ("pm64_boss_music"); 0: the game's */
    /* --- a head on a vine (Lava Piranha, the Lava Buds: PM64 draws the vine from VineSegmentA/B sprites along
       a skeleton and the head at its top; the head rows' parts hang from there, below the sprite's feet) --- */
    signed char vineA, vineB; /* the vine's two segment rows, + 1 (0: no vine) */
    unsigned char vine;       /* the vine's height in PM64 units (lava_piranha.c VINE_0: 9 bones x 10) */
    /* --- a fight's versions (a fight's first unit only) --- */
    signed char early1;       /* the unit (index + 1) its EARLIEST PM64 fight starts with, fought at PM64's level 1
                                 for the scaling: the story meeting's, and a refight's "first fight" (Jr. Troopa's
                                 prologue fight); 0: the one version (every other boss: PM64 fights it once, or its
                                 story meeting comes when the last version fits) */
} Pm64Boss;

/* one file's units: the first `fights` are fights (the boss rush's and refights' list, in registry order), the
 * rest the units they bring in */
typedef struct {
    const char* id;
    const Pm64Boss* units;
    int count;
    int fights;
} Pm64BossSet;

#define PM64_BOSS(id) extern const Pm64BossSet kPm64Boss_##id;
#include "registry.h"
#undef PM64_BOSS
