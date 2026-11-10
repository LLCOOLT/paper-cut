/* Paper Mario 64: General Guy and his troops (pm64_boss.h).
 *
 * PM64's fight (src/battle/area/omo2/area.c Formation_01: General Guy in his Toy Tank, its Light Bulb above): the
 * General stays shut in the tank while his troops come in waves (toy_tank.c EVS_ManageTroopWaves): the Shy Squad
 * (general_guy.c EVS_Init: SummonEnemy ShySquadFormation), then two Stilt Guys (EVS_BeginWave_StiltGuys ->
 * EVS_SummonStiltGuys), then two Shy Stacks (EVS_BeginWave_ShyStacks), and once they're beaten the tank phase
 * (EVS_BeginWave_Tank: MSG_CH4_0071, the tank becomes targetable and the General takes his turns). Here each wave
 * hands over to the next (spawn1) as it falls (.phaseAt = -1): the Shy Squad runs off (or once it's been hit on
 * two turns, .leaveHits), the Stilt Guys and the Shy Stacks come in pairs (a twin: .with1 itself, .revive = 2)
 * and the next wave waits for the second of a pair (.exit = EXIT_BEATEN); the last is the General in his tank,
 * whose fall ends the fight (toy_tank.c EVS_Death). The Light Bulb above the tank (light_bulb.c; area.c
 * Formation_01 BTL_POS_AIR_D) is its own target from the tank phase on (EVS_BeginWave_Tank clears its
 * ACTOR_PART_FLAG_NO_TARGET): it comes in with the General (his with1) and his Lightning plays only while it
 * stands (EVS_TakeTurn: ActorExists ACTOR_BULB). Not here: the tank itself (a model). */
#include "pm64_boss.h"

/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds). src/battle/actors.yaml: GeneralGuy, ToyTank, ShySquad,
   StiltGuy, ShyStack and LightBulb all hurtSound SOUND_ACTOR_HURT; StiltGuy walkSound SOUND_STILT_GUY_STEP, ShyStack
   SOUND_ACTOR_STEP_A, _B every 10 (stepDelay). The rest from each script, cited below. */
/* (toy_tank.c EVS_Death: SOUND_TOY_TANK_FALL_APART, SOUND_TOY_TANK_EXPLODE as the tank goes) */
static const Pm64BossSounds kGeneralSfx = {.hurt = "SOUND_ACTOR_HURT", .death = "SOUND_TOY_TANK_EXPLODE"};
/* (shy_squad.c EVS_Attack_Swarm: SOUND_LARGE_CROWD_WALK as the squad runs; EVS_Flee: SOUND_SHY_GUY_SCREAMS3 as it
   panics; EVS_Death: SOUND_ACTOR_DEATH) */
static const Pm64BossSounds kSquadSfx = {.step = {"SOUND_LARGE_CROWD_WALK", NULL}, .stepDist = 90,
                                         .hurt = "SOUND_ACTOR_HURT", .death = "SOUND_ACTOR_DEATH",
                                         .exit = "SOUND_SHY_GUY_SCREAMS3"};
/* (stilt_guy.c EVS_Attack_Leap: SOUND_ACTOR_HOP up, SOUND_STILT_GUY_STEP as he lands; EVS_FallDown:
   SOUND_STILTS_FALL) */
static const Pm64BossSounds kStiltSfx = {.step = {"SOUND_STILT_GUY_STEP", "SOUND_STILT_GUY_STEP"},
                                         .jump = "SOUND_ACTOR_HOP", .land = "SOUND_STILT_GUY_STEP",
                                         .hurt = "SOUND_ACTOR_HURT", .death = "SOUND_STILTS_FALL"};
/* (shy_stack.c EVS_SpinSmashDeath: SOUND_ACTOR_DEATH) */
static const Pm64BossSounds kStackSfx = {.step = {"SOUND_ACTOR_STEP_A", "SOUND_ACTOR_STEP_B"}, .stepDist = 10,
                                         .hurt = "SOUND_ACTOR_HURT", .death = "SOUND_ACTOR_DEATH"};
/* (light_bulb.c EVS_Death: SOUND_TANK_BULB_SHATTER) */
static const Pm64BossSounds kBulbSfx = {.hurt = "SOUND_ACTOR_HURT", .death = "SOUND_TANK_BULB_SHATTER"};

static const Pm64Boss kUnits[] = {
    /* 1: General Guy in his Toy Tank (PM64 src/battle/area/omo2/actor/general_guy.c with toy_tank.c): the tank's
       30 HP and defence 2 are what's hit (toy_tank.c ActorBlueprint maxHP 30, DefenseTable ELEMENT_NORMAL 2; the
       General's own 10 HP never matter: EVS_Death sets them to 1 as he tumbles out). Each turn (EVS_TakeTurn), while
       the Light Bulb is up and the partner isn't KO'd, RandInt(100) < 60 the Lightning (EVS_Attack_ShootLightning:
       the hatch opens, Lift, Point 20 frames, the room darkens, static on the bulb for 40 frames, a bolt at Mario,
       5 damage, then one at the partner, 2), else the Bomb Toss (EVS_Attack_ThrowBomb: the hatch opens, Lift with
       the bomb 35 frames, Whistle as he lights and tosses it, it flies 15 frames, a blast, 4 damage). His line as
       the tank phase begins (toy_tank.c EVS_BeginWave_Tank: ActorSpeak MSG_CH4_0071): his first turn's line.
       Here: the Bomb Toss Lift (11) for its 35 frames, Whistle (10) as it flies (.release), a blast (EL_BLAST);
       the Lightning Point (3) for its 20 frames, shock, the stage darkened first (FadeBackgroundDarken, Wait 20:
       .darken 20), the partner hit for 2 (.partner). The Lightning only while the Light Bulb (unit 5) stands
       and the partner isn't KO'd (.cond C_UNIT_PARTNER), else the Bomb Toss (.else1): EVS_TakeTurn's own
       order. The Bulb comes in beside him as the tank phase begins (.with1, .revive 2).
       GeneralGuy (117): 2 Idle, 3 Point, 5 Hurt, 10 Whistle, 11 Lift, 12 Talk. */
    {"General Guy", 0x2F, 0x31, 117, -1, 1.0f, UNIT_UNUSED_BELDAM3, 30, 2, 30, 30, 2, 2, 2, 5,
     /* (general_guy.c EVS_Attack_ThrowBomb: SOUND_TOY_TANK_OPEN_HATCH, SOUND_TOSS as the bomb flies,
        SOUND_BOMB_BLAST; EVS_Attack_ShootLightning: SOUND_TOY_TANK_OPEN_HATCH, SOUND_ELECTRIC_BUZZ on the bulb,
        SOUND_TANK_BULB_FIRE with each bolt) */
     {{MV_SHOT, 4, 11, 40, 0, "SOUND_BOMB_BLAST|SFX_ENM_BOMB_ATTACK2", .release = 10, .windup = 35,
       .element = EL_BLAST, .wind = "SOUND_TOY_TANK_OPEN_HATCH", .go = "SOUND_TOSS", .fx = FX_BOMB},
      {MV_ALL, 5, 3, 60, 0, "SOUND_TANK_BULB_FIRE|SFX_BTL_THUNDERS_ATTACK1", .windup = 20, .element = EL_SHOCK,
       .partner = 2, .darken = 20, .cond = C_UNIT_PARTNER, .unit1 = 5, .else1 = 1, .wind = "SOUND_ELECTRIC_BUZZ", .fx = FX_BULB_BOLT},
      {-1}},
     2, 0, 0, 0, {5}, 0, 0, 0, 0, 0, 0, 0, 0, /* (his fight starts with the Shy Squad, unit 2) */
     .startMsg = PM64_MSG(0x0F, 0x71), .talkRow = 12, .revive = 2, .sounds = &kGeneralSfx,
     .song = 0x0C /* SONG_GENERAL_GUY_BATTLE */},
    /* 2: the Shy Squad (PM64 src/battle/area/omo2_1/actor/shy_squad.c): 15 HP (one Shy Guy each), defence 0. Its
       turn (EVS_TakeTurn -> EVS_Attack_Swarm): the squad runs up to Mario (Run, then Dash at speed 6) and piles on,
       1 damage from each Shy Guy still standing (DMG_SWARM, up to 15 hits 10 frames apart), then dashes home. On
       its arrival it cheers MSG_CH4_0065 (EVS_HandlePhase, PHASE_PLAYER_BEGIN: Excited), here before its first
       rush; once it has been attacked on two turns it panics and runs (EVS_HandlePhase, PHASE_ENEMY_BEGIN ->
       EVS_Flee: Panic, MSG_CH4_0066), and the Stilt Guys take its place.
       Here: 1 damage a hit, a hit for each Shy Guy left (15 at full HP, fewer as it loses them: .hits 15,
       BY_HP_HITS); its cheer its first turn's line; it runs off once hit on two turns (.leaveHits) or beaten.
       ShySquadGuy (109): 1 Idle, 3 Run, 4 Dash, 10 Jump, 13 Hurt. */
    {"Shy Squad", 0x30, 0x32, 109, -1, 1.0f, UNIT_UNUSED_MINI_BZZAP, 15, 0, 60, 30, 1, 3, 10, 13,
     {{MV_RUSH, 1, 4, 1, 0, NULL, .hits = 15, .byHp = BY_HP_HITS}, {-1}},
     0, 3, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, PM64_MSG(0x0F, 0x66), .phaseAt = -1, .exit = EXIT_FLEE,
     .leaveHits = 2, .startMsg = PM64_MSG(0x0F, 0x65), .sounds = &kSquadSfx},
    /* 3: the Stilt Guys (PM64 src/battle/area/omo2_2/actor/stilt_guy.c; two of them, toy_tank.c
       Left/RightStiltGuyFormation): 7 HP each, defence 0; a pair (.with1 itself, .revive = 2). Each turn (EVS_TakeTurn ->
       EVS_Attack_Leap): Run up to 50 short of Mario at speed 4, Fall (a crouch) 8 frames, Jump onto him over 15
       frames, 4 damage (DMG_LEAP), and back. Their reply to the General's order (EVS_SummonStiltGuys: ActorSpeak
       MSG_CH4_006D from the second one), here before the pair's first Leap (once between them). Both beaten,
       the Shy Stacks come on. StiltGuy (111): 1 Idle, 3 Run, 4 Fall, 5 Jump, 6 Hurt. */
    {"Stilt Guy", 0x32, 0x34, 111, -1, 1.0f, UNIT_UNUSED_FIGHTER, 7, 0, 30, 75, 1, 3, 4, 6,
     {{MV_JUMP, 4, 5, 1, 0, NULL}, {-1}},
     0, 4, 0, 0, {3}, 0, 0, 0, 0, 0, 1, PM64_MSG(0x0F, 0x6D), 0, .phaseAt = -1, .exit = EXIT_BEATEN,
     .revive = 2, .sounds = &kStiltSfx},
    /* 4: the Shy Stacks (PM64 src/battle/area/omo2_3/actor/shy_stack.c; two of them, toy_tank.c
       Left/RightShyStackFormation): 10 HP each, defence 0; a pair. Each turn (EVS_TakeTurn): the
       stack draws its slingshots (4xDraw 20 frames), fires (4xFire), a rock from each Shy Guy left in the stack, one
       hit of 4 damage with all four (DMG_SHOOT_FOUR; 3, 2, 1 as they're knocked off: BY_HP_POWER). Their reply to
       the General (EVS_BeginWave_ShyStacks: ActorSpeak MSG_CH4_0070 from the second one), here before the pair's
       first volley. Both beaten, the tank phase begins: the General himself (unit 1). ShyStackGuy (113): rows by
       stack height, 4x to 1x in a row (.hpRows 4, .hpStep 1): 4 4xIdle, 8 4xHurt, 27 4xDraw, 31 4xFire. */
    {"Shy Stack", 0x31, 0x33, 113, -1, 1.0f, UNIT_UNUSED_MASTER, 10, 0, 30, 75, 4, 4, 4, 8,
     /* (shy_stack.c EVS_TakeTurn: SOUND_SLINGSHOT_DRAW, SOUND_SLINGSHOT_FIRE) */
     {{MV_SHOT, 4, 27, 1, 0, NULL, .release = 31, .windup = 20, .byHp = BY_HP_POWER, .wind = "SOUND_SLINGSHOT_DRAW",
       .go = "SOUND_SLINGSHOT_FIRE"}, {-1}},
     0, 1, 0, 0, {4}, 0, 0, 0, 0, 0, 1, PM64_MSG(0x0F, 0x70), 0, .phaseAt = -1, .exit = EXIT_BEATEN,
     .revive = 2, .hpRows = 4, .hpStep = 1, .sounds = &kStackSfx},
    /* 5: the Light Bulb (PM64 src/battle/area/omo2/actor/light_bulb.c): 8 HP, defence 1 (DefenseTable
       ELEMENT_NORMAL 1), size 30 x 30, in the air above the tank (EVS_Init SetActorPos 116, 70, 0; the tank at 85:
       31 behind it, 70 up; BTL_POS_AIR_D, so a hammer can't reach it), no Tattle (ACTOR_FLAG_NO_TATTLE) and no
       turns (EVS_TakeTurn: Return). Its name MSG_EnemyName_Bulb (0x29 0x7A). Broken (EVS_Death: the shatter,
       RemoveActor), the General's Lightning is over. It comes in with the General, a puff in place (.enter);
       PM64 draws it as a stage model (MODEL_kyu1/kyu3 of omo_07) under EFFECT_BULB_GLOW: here the round bomb of
       GeneralGuyBomb (118: 0 Still), Gray palette (1), glowing as a lit bulb (.lamp). */
    {"Light Bulb", 0x7A, -1, 118, 1, 1.0f, UNIT_UNUSED_ANTI_GUY, 8, 1, 30, 30, 0, 0, 0, 0,
     {{MV_REST, 0, 0, 1}}, 0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, 0, .at = {31, 70, 0}, .fly = 70,
     .enter = ENTER_PLACE, .lamp = 1, .sounds = &kBulbSfx},
};

const Pm64BossSet kPm64Boss_general_guy = {"general_guy", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
