/* Paper Mario 64: Tubba Blubba and his Heart (pm64_boss.h). */
#include "pm64_boss.h"

/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds): Tubba's from src/battle/actors.yaml TubbaBlubba (walkSound
   SOUND_HEAVY_NPC_STEP_C) and arn/actor/tubba_blubba.c (EVS_Attack_BodySlam's SetActorSounds ACTOR_SOUND_JUMP
   SOUND_LARGE_ACTOR_JUMP); the Heart's from actors.yaml TubbaHeart (jumpSound SOUND_TUBBA_HEART_JUMP: every hop) and
   arn/actor/tubbas_heart.c (EVS_Move_Charge SOUND_TUBBAS_HEART_CHARGE; EVS_Attack_DarkSwarm SOUND_SPELL_CAST1 as it
   gathers, SOUND_TUBBAS_HEART_SWARM_ATTACK as the swarm flies, SOUND_SQUEEZE on Mario twice, and
   SOUND_TUBBAS_HEART_SWARM_DISPERSE) */
static const Pm64BossSounds kTubbaSfx = {.step = {"SOUND_HEAVY_NPC_STEP_C", "SOUND_HEAVY_NPC_STEP_C"},
                                         .jump = "SOUND_LARGE_ACTOR_JUMP"};
static const Pm64BossSounds kHeartSfx = {.jump = "SOUND_TUBBA_HEART_JUMP",
                                         .power = "SOUND_TUBBAS_HEART_CHARGE",
                                         .swarm = {"SOUND_SPELL_CAST1", "SOUND_TUBBAS_HEART_SWARM_ATTACK",
                                                   "SOUND_SQUEEZE", "SOUND_TUBBAS_HEART_SWARM_DISPERSE"}};

static const Pm64Boss kUnits[] = {
    /* Tubba Blubba, reunited with his Heart (PM64 src/battle/area/arn/actor/tubba_blubba.c): 10 HP, defence 0;
       each turn RandInt(100) < 60 the Slam Fist (EVS_Attack_SlamFist: runs up, TenseArms 8 frames, RaiseArms 20,
       SlamArms, 4 damage, dust, a shockwave and a hard shake), else the Body Slam (EVS_Attack_BodySlam: runs up,
       Lean 8 frames, Jump, Fall after 12, Land, 6 damage, a shockwave and smoke). His fight starts with his Heart
       (below). BattleTubba (106): 1 Idle, 6 Run, 10 Lean, 11 Jump, 12 Fall, 13 Land, 20 Hurt, 21-23 the arms. */
    {"Tubba Blubba", 0x25, 0x27, 106, -1, 1.0f, UNIT_UNUSED_VIVIAN2, 10, 0, 64, 76, 1, 6, 10, 20,
     /* (EVS_Attack_SlamFist: SOUND_HEAVY_NPC_SWIPE as the arms come down, SOUND_HEAVY_NPC_LANDING with the blow;
        EVS_Attack_BodySlam: SOUND_HEAVY_NPC_LANDING as he lands) */
     {{MV_POUND, 4, 21, 60, 0, "SOUND_HEAVY_NPC_LANDING|SFX_ENM_WAN_LANDING1", .go = "SOUND_HEAVY_NPC_SWIPE"},
      {MV_SLAM, 6, 11, 40, 0, "SOUND_HEAVY_NPC_LANDING|SFX_ENM_WAN_LANDING1"}, {-1}},
     2, 0, 0, 0, {0}, 0, 11, 12, 13, 1, /* (its fight starts with its Heart, unit 2 here) */
     .sounds = &kTubbaSfx, .song = 0x0B /* SONG_TUBBA_BLUBBA_BATTLE */},
    /* 2: Tubba's Heart (PM64 src/battle/area/arn/actor/tubbas_heart.c): 50 HP, defence 0, gets about by hops.
       Its turns go Charge, Dark Swarm, Charge, Dark Swarm, then a Leap after every Swarm (EVS_TakeTurn's
       AVAR_NextMove and AVAR_SwarmCount): the Charge (EVS_Move_Charge: TalkAngry, energy gathers for 60 frames,
       a hop; it stays charged, TalkAngry its idle), the Dark Swarm (EVS_Attack_DarkSwarm: Enraged, gathers for
       60 frames, a hop, the swarm over Mario, 12 damage), the Leap (EVS_Attack_Leap: hops up to Mario, Squeeze
       5 frames, Leap onto him, 6 damage, bounces back). At 5 HP or less it flees (EVS_HandleEvent ->
       EVS_FleeFromBattle): here into Tubba Blubba, who fights on. TubbasHeart (107): 1 Idle, 3 Run, 11
       TalkAngry, 12 Enraged, 13 Dizzy, 14 Hurt, 21 Leap, 22 Squeeze. Its lines: MSG_CH3_00C5 before its first
       Charge (EVS_TakeTurn), MSG_CH3_00C6 as it flees (EVS_FleeFromBattle). */
    {"Tubba's Heart", 0x26, 0x26, 107, -1, 1.0f, UNIT_UNUSED_TUYONARL, 50, 0, 32, 32, 1, 3, 22, 14,
     {{MV_WARN, 0, 11, 1, 0, NULL, 2}, {MV_SWARM, 12, 12, 0, 3, NULL, 1, 3, 2}, {MV_JUMP, 6, 21, 0, 3, NULL, 1},
      {-1}},
     0, 1, 1, 0, {0}, 11, 0, 0, 0, 0, 1, PM64_MSG(0x0E, 0xC5), PM64_MSG(0x0E, 0xC6), .sounds = &kHeartSfx},
};

const Pm64BossSet kPm64Boss_tubba = {"tubba", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
