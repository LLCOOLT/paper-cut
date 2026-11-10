/* Paper Mario 64: Lava Piranha, her two Lava Buds, and the fiery second phase (pm64_boss.h).
 * PM64 src/battle/area/kzn2/actor/lava_piranha.c and lava_bud.c (petit_piranha.c for
 * the Petit Piranhas the fiery Buds spit out). PM64 fights her as one boss on a vine and two Buds on vines of
 * their own; each phase is a unit here, the second taking the first's place (spawn1), the Buds beside her (with1).
 * Planted: none of them walks or hops; every move is from where they stand. All four shrug off fire (IM_FIRE,
 * fire 99); the fiery ones burn whoever jumps on them (HZ_FIRE). Her first fall: she sinks and comes back on
 * fire in a puff (.phaseAt = -1, .exit = EXIT_TRANSFORM, .enter = ENTER_PLACE), and her withered Buds are
 * replaced by fiery ones (.revive = 1). */
#include "pm64_boss.h"

/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds), from kzn2/actor/lava_piranha.c: EVS_Hit_Inner
   SOUND_LAVA_PIRANHA_WITHER as she's hit; EVS_Death, first phase SOUND_LAVA_PIRANHA_DEFEAT as she sinks, second
   phase SOUND_LAVA_PIRANHA_COLLAPSE; and lava_bud.c: EVS_Hit and EVS_Death SOUND_LAVA_BUD_WITHER, EVS_Move_SummonPetit
   SOUND_LAVA_BUD_SPIT_PETIT as the fiery one spits out its Petit Piranha */
static const Pm64BossSounds kPiranhaSfx = {.hurt = "SOUND_LAVA_PIRANHA_WITHER", .exit = "SOUND_LAVA_PIRANHA_DEFEAT"};
static const Pm64BossSounds kFieryPiranhaSfx = {.hurt = "SOUND_LAVA_PIRANHA_WITHER",
                                                .death = "SOUND_LAVA_PIRANHA_COLLAPSE"};
static const Pm64BossSounds kBudSfx = {.hurt = "SOUND_LAVA_BUD_WITHER", .death = "SOUND_LAVA_BUD_WITHER"};
static const Pm64BossSounds kFieryBudSfx = {.hurt = "SOUND_LAVA_BUD_WITHER", .death = "SOUND_LAVA_BUD_WITHER",
                                            .power = "SOUND_LAVA_BUD_SPIT_PETIT"};

/* each on its vine (PM64 draws the vine's VineSegmentA/B sprites along a skeleton, the head at its top, its parts
   hanging from there): LavaPiranha (120) rows 1 and 2, its head ForceHomePos 61 up (lava_piranha.c EVS_Init);
   LavaBud (122) rows 0 and 1, 57 up (ACTOR_BUD_1). Without the vine the head sank under the floor. */
#define PIRANHA_VINE .vineA = 2, .vineB = 3, .vine = 61
#define BUD_VINE .vineA = 1, .vineB = 2, .vine = 57

static const Pm64Boss kUnits[] = {
    /* 1: Lava Piranha (lava_piranha.c, ActorBlueprint: 40 HP, defence 0 (fire 99), size 50x50). Every turn the
       Spit Fire (EVS_TakeTurn, first phase -> EVS_Attack_SpitFire: VINE_ANIM_BOSS_ATTACK 14 frames, SpitFireIn
       10, the inhale 45, SpitFireOut, a ball of fire at Mario 19 frames, embers, DMG_SPIT_FIRE 5 fire damage).
       Her first fall isn't the end (EVS_Death, first phase: the fakeout win jingle, she sinks into the lava
       and rises again on fire, full HP, the Buds too): here she takes her fiery self's place (spawn1 = 2).
       LavaPiranha (120): 3 Idle, 4 TongueWag, 5 SpitFireIn, 6 SpitFireOut, 7 FireBreathIn, 8 FireBreathOut,
       9 Hurt, 10 Sleep, 13 PlayDead, 14 Talk (DefaultAnims: Idle; EVS_Init UnkAnim2: Hurt). */
    {"Lava Piranha", 0x3A, 0x3C, 120, -1, 1.0f, UNIT_UNUSED_MARILYN3, 40, 0, 50, 50, 3, 3, 3, 9,
     /* (EVS_Attack_SpitFire: SOUND_LAVA_PIRANHA_INHALE, then SOUND_LAVA_PIRANHA_SPIT_FIRE) */
     {{MV_SHOT, 5, 5, 1, 0, "SFX_BTL_DAMAGE_FIRE1", .release = 6, .windup = 55, .element = EL_FIRE,
       .wind = "SOUND_LAVA_PIRANHA_INHALE", .go = "SOUND_LAVA_PIRANHA_SPIT_FIRE", .fx = FX_SPIT_FIRE}, {-1}},
     0, 2, 0, 0, {3, 3}, .phaseAt = -1, .exit = EXIT_TRANSFORM, .immune = IM_FIRE, .sounds = &kPiranhaSfx, PIRANHA_VINE,
     .song = 0x0D /* SONG_LAVA_PIRANHA_BATTLE */},
    /* 2: Lava Piranha on fire, her second phase (lava_piranha.c EVS_Death's first-phase branch: SetActorType
       ACTOR_TYPE_LAVA_PIRANHA_PHASE_2, EVS_Ignite, full HP; Tattle 0x3D, "you'll take damage if you step on
       'em"). Every turn the Fire Breath (EVS_TakeTurn, PIRANHA_STATE_FIERY -> EVS_Attack_FireBreath:
       VINE_ANIM_BOSS_ATTACK 14 frames, Talk 10, the inhale 45, FireBreathOut, a stream of fire 15 frames,
       DMG_FIRE_BREATH 7 fire damage, 20 frames, FireBreathIn 19, TongueWag). Her idle TongueWag (FieryAnims);
       her hurt row Sleep (EVS_Death sets AVAR_Common_UnkAnim2 = ANIM_LavaPiranha_Sleep, which EVS_Hit plays).
       Her Buds rise on fire with her (with1 = the fiery Buds). */
    {"Lava Piranha", 0x3A, 0x3D, 120, -1, 1.0f, UNIT_UNUSED_REPORTER, 40, 0, 50, 50, 4, 4, 4, 10,
     /* (EVS_Attack_FireBreath: SOUND_LAVA_PIRANHA_INHALE, then SOUND_LAVA_PIRANHA_FIRE_BREATH) */
     {{MV_SHOT, 7, 7, 1, 0, "SFX_BTL_DAMAGE_FIRE1", .release = 8, .windup = 55, .post = 7, .element = EL_FIRE,
       .wind = "SOUND_LAVA_PIRANHA_INHALE", .go = "SOUND_LAVA_PIRANHA_FIRE_BREATH", .fx = FX_FIRE_STREAM},
      {-1}},
     0, 0, 0, 0, {4, 4}, .enter = ENTER_PLACE, .revive = 1, .immune = IM_FIRE, .hazard = HZ_FIRE,
     .sounds = &kFieryPiranhaSfx, PIRANHA_VINE},
    /* 3: Lava Bud (lava_bud.c, ActorBlueprint: 8 HP, defence 0 (fire 99), size 30x30), two beside her. Every
       turn the Petit spit (EVS_TakeTurn, first phase -> EVS_Attack_SpitPetit: VINE_ANIM_BUD_ATTACK, SpitWindup
       after 20 frames for 39, SpitRelease 5; a Petit Piranha arcs out and dives onto Mario, DMG_PETIT_SPIT 4
       fire damage). Beaten in the first phase it withers (EVS_Death: PIRANHA_STATE_DEAD, no more turns).
       LavaBud (122): 3 Idle, 4 IdleFiery, 5 SpitWindup, 6 SpitRelease, 7 SpitWindupFiery, 8 SpitReleaseFiery,
       9 Hurt, 11 HurtFiery, 13 PlayDead, 15 Dead. */
    {"Lava Bud", 0x3B, 0x3E, 122, -1, 1.0f, UNIT_UNUSED_HOTDOG_MASTER, 8, 0, 30, 30, 3, 3, 3, 9,
     /* (EVS_Attack_SpitPetit: SOUND_PETIT_PIRANHA_DIVE as the Petit dives) */
     {{MV_SHOT, 4, 5, 1, 0, "SFX_BTL_DAMAGE_FIRE1", .release = 6, .windup = 39, .element = EL_FIRE,
       .go = "SOUND_PETIT_PIRANHA_DIVE", .fx = FX_PETIT}, {-1}},
     .immune = IM_FIRE, .sounds = &kBudSfx, BUD_VINE},
    /* 4: Lava Bud on fire, the second phase (lava_bud.c, ACTOR_TYPE_LAVA_BUD_PHASE_2; Tattle 0x3F, "They blow
       out Petit Piranhas"). Its turns (EVS_TakeTurn, second phase, PIRANHA_STATE_FIERY): EVS_Move_SummonPetit
       (VINE_ANIM_BUD_ATTACK, SpitWindupFiery after 20 frames for 34, SpitReleaseFiery 10; a Petit Piranha,
       1 HP, flies out beside it; AVAR_SummonDelayTurns 1 so its next turn it rests), then the Petit's turn
       (petit_piranha.c EVS_TakeTurn: Strain 20 frames, Curl, then it Dives onto Mario in a burst of flame,
       DMG_IMPACT 6 fire damage, and is gone), and again. Here: the summon a warning, the Petit's dive its
       follow-up (next1). Frozen, its fire goes out and it's stuck 2 turns (lava_bud.c EVS_Death, second phase:
       put out, AVAR_Common_StunTurnsLeft 2: .douse); it rises in place (.enter = ENTER_PLACE). */
    {"Lava Bud", 0x3B, 0x3F, 122, -1, 1.0f, UNIT_UNUSED_ZESS_T, 8, 0, 30, 30, 4, 4, 4, 11,
     {{MV_WARN, 0, 7, 1, 0, NULL, 2},
      /* (petit_piranha.c's dive: SOUND_PETIT_PIRANHA_DIVE, lava_bud.c EVS_Attack_SpitPetit) */
      {MV_SHOT, 6, 7, 0, 3, "SFX_BTL_DAMAGE_FIRE1", .release = 8, .windup = 20, .element = EL_FIRE,
       .go = "SOUND_PETIT_PIRANHA_DIVE", .fx = FX_PETIT}, {-1}},
     .enter = ENTER_PLACE, .immune = IM_FIRE, .hazard = HZ_FIRE, .douse = 2, .sounds = &kFieryBudSfx, BUD_VINE},
};

const Pm64BossSet kPm64Boss_lava_piranha = {"lava_piranha", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
