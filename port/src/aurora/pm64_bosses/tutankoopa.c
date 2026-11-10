/* Paper Mario 64: Tutankoopa and his Chomp (pm64_boss.h). */
#include "pm64_boss.h"

/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds): Tutankoopa's steps from src/battle/actors.yaml Tutankoopa
   (walkSound SOUND_ACTOR_STEP_A, _B; isk_part_2/actor/tutankoopa.c EVS_GetBackUp), his summon from
   EVS_Move_SummonChomp (SOUND_CHAIN_CHOMP_BITE as the Chomp comes out); the Chomp's from chain_chomp.c
   (EVS_Chomp_HopToPos: SOUND_CHAIN_CHOMP_THUD each time it lands) and tutankoopa.c EVS_Tutankoopa_Death
   (SOUND_ACTOR_DEATH on the Chomp) */
static const Pm64BossSounds kTutankoopaSfx = {.step = {"SOUND_ACTOR_STEP_A", "SOUND_ACTOR_STEP_B"},
                                              .summon = "SOUND_CHAIN_CHOMP_BITE"};
static const Pm64BossSounds kChompSfx = {.land = "SOUND_CHAIN_CHOMP_THUD", .death = "SOUND_ACTOR_DEATH"};

static const Pm64Boss kUnits[] = {
    /* Tutankoopa (PM64 src/battle/area/isk_part_2/actor/tutankoopa.c): 30 HP, defence 0, size 56 x 45; three
       Buzzy Beetle shells beside him (AVAR_ShellsLeft 3). EVS_TakeTurn: with no Chomp out he summons one
       (EVS_Move_SummonChomp, see the Chomp below); otherwise AVAR_NextMove (AVAL_Move_Toss first, EVS_Init)
       alternates the Throw Shell and the Drop Debris, and once the shells are gone it is the Drop Debris every
       turn.
       - Throw Shell (EVS_Attack_ThrowShell): runs to the next shell, Idle 10 frames, Lift 7, the shell up over
         his head 8, then Toss: it flies at Mario in 30 frames (SetGoalToTarget, FlyPartTo), DMG_THROW_SHELL 3
         damage, no contact; then runs home. Its next turn the Drop Debris (move 2).
       - Drop Debris (EVS_Attack_DropDebris): the first time, ActorSpeak MSG_CH2_00E5 in Shout; then Shout with
         the hieroglyphs 30 frames (SOUND_TUTANKOOPA_MAGIC, EFFECT_HIEROGLYPHS), the ceiling's dust
         (EFFECT_CHOMP_DROP), and some 70 frames later rocks fall on Mario and his partner (EVS_DropDebris_Players:
         DMG_DROP_DEBRIS_PLAYER and _PARTNER, 2 each, no contact). Its next turn the Throw Shell (move 1), or, once
         the three shells are gone (after the third Drop Debris), the Drop Debris for good (move 3).
       - Summon Chomp (EVS_Move_SummonChomp): whenever no Chomp is out, ahead of the pattern (.pre, .cond =
         C_NO_MATES): Shout, MSG_CH2_00E6 the first time (.msg), the Chomp comes in (.unit1, .count 1).
       Here: the Throw Shell three times at most (.uses, AVAR_ShellsLeft 3), Lift (4) through its 7 + 8 frames,
       Toss (5) as it flies (.release); the Drop Debris on the partner too, for 2 (.partner).
       Tutankoopa (103): 1 Idle, 2 Run, 3 Shout, 4 Lift, 5 Toss, 6 Hurt, 7 Fall, 8 Levitate, 10 Dizzy, 11 Talk,
       22 RockStill, 23 RockShatter. */
    {"Tutankoopa", 0x1C, 0x1C, 103, -1, 1.0f, UNIT_UNUSED_MARILYN2, 30, 0, 56, 45, 1, 2, 4, 6,
     /* (sounds: EVS_Attack_ThrowShell SOUND_TOSS as the shell flies; EVS_Attack_DropDebris SOUND_TUTANKOOPA_MAGIC
        as he shouts, EVS_DropDebris_Players SOUND_TUTANKOOPA_DEBRIS_FALL as the rocks come down) */
     {{MV_SHOT, 3, 4, 1, 0, "SFX_ENM_NOKO_SHELL1", 2, .release = 5, .windup = 15, .uses = 3, .else1 = 3,
       .go = "SOUND_TOSS"},
      {MV_ALL, 2, 3, 0, 3, "SOUND_TUTANKOOPA_DEBRIS_FALL|", 1, 3, 3, .partner = 2, .wind = "SOUND_TUTANKOOPA_MAGIC", .fx = FX_DEBRIS},
      {MV_ALL, 2, 3, 0, 3, "SOUND_TUTANKOOPA_DEBRIS_FALL|", 3, .partner = 2, .wind = "SOUND_TUTANKOOPA_MAGIC", .fx = FX_DEBRIS},
      {MV_SUMMON, 0, 3, 0, 0, NULL, .unit1 = 2, .count = 1, .cond = C_NO_MATES, .pre = 1,
       .msg = PM64_MSG(0x0D, 0xE6)},
      {-1}},
     0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 2, PM64_MSG(0x0D, 0xE5), 0, .sounds = &kTutankoopaSfx,
     .song = 0x0A /* SONG_TUTANKOOPA_BATTLE */},
    /* 2: the Chomp (PM64 src/battle/area/isk_part_2/actor/chain_chomp.c): 4 HP, defence 3 (fire, ice and magic
       99), size 56 x 40; it gets about by hops (EVS_Chomp_HopHome, EVS_Chomp_HopToPos). Tutankoopa brings it
       in through the gate (tutankoopa.c EVS_Move_SummonChomp: ActorSpeak MSG_CH2_00E6, the gate opens over
       60 frames, the Chomp hops out in SlowBite; MSG_CH2_00E7 the times after); here he summons it whenever none
       is out (his move 4), and it comes in from the back. Its turn (EVS_TakeTurn): a hop 45 forward (15 frames), SlowBite 15 frames, then a leap onto
       Mario (JumpToGoal 5 frames) and the bite, DMG_CHOMP_BITE 3 damage; it hops back home in Bite.
       ChainChomp (104): 1 Idle, 2 Chain, 3 QuickBite, 4 Bite, 5 SlowBite, 6 Hurt, 8 Dizzy. */
    {"Chomp", 0x1D, 0x1D, 104, -1, 1.0f, UNIT_UNUSED_REPORTER, 4, 3, 56, 40, 1, 5, 5, 6,
     {{MV_JUMP, 3, 4, 1, 0, NULL}, {-1}},
     0, 0, 1, 0, {0}, 0, .sounds = &kChompSfx},
};

const Pm64BossSet kPm64Boss_tutankoopa = {"tutankoopa", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
