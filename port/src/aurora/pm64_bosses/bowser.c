/* Paper Mario 64: Kammy Koopa and Bowser (pm64_boss.h). PM64's last battle is Bowser
 * in two parts (kpa2 final_bowser_1, then final_bowser_2) with Kammy Koopa's fight against Peach and Twink in
 * between (kkj kammy_koopa); here they are two fights, Kammy's first, then Bowser alone (PM64 never puts Kammy in
 * Bowser's battle: kpa2/area.c's formations hold Bowser only). */
#include "pm64_boss.h"

/* Bowser's Recover: 75 times in 100 (EVS_Recover's odds), only while Mario's HP share beats his by 25 (.lead) */
static const signed char kRecover[] = {101, 75, 0};
/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds). Kammy's from kkj/actor/kammy_koopa.c (EVS_Death:
   SOUND_ACTOR_COLLAPSE). Bowser's from kpa2/actor/final_bowser_1.c and final_bowser_2.c: EVS_PlayFootstepSounds
   SOUND_HEAVY_NPC_STEP_B, _C; src/battle/actors.yaml FinalBowser_1/2 jumpSound SOUND_LARGE_ACTOR_JUMP;
   EVS_StarRodCast SOUND_BOWSER_STAR_ROD_GATHER, _USE, _ENCHANT, _SHOCKWAVE in turn; EVS_Recover
   SOUND_BOWSER_CAST_RECOVER; final_bowser_2.c EVS_Death SOUND_BOWSER_COLLAPSE. His moves' (below): EVS_Attack_FireBreath
   SOUND_BOWSER_FIRE_BREATH_LOOP, EVS_Attack_ClawSwipe SOUND_HEAVY_NPC_SWIPE, EVS_Attack_BodySlam
   SOUND_HEAVY_NPC_LANDING, EVS_UseDrainingShockwave SOUND_BOWSER_STAR_ROD_USE and _DRAIN_WAVE,
   EVS_Attack_LightningBlast SOUND_BOWSER_CHARGE_LIGHTNING and SOUND_BOWSER_LIGHTNING */
static const Pm64BossSounds kKammySfx = {.death = "SOUND_ACTOR_COLLAPSE"};
static const Pm64BossSounds kBowserSfx = {.step = {"SOUND_HEAVY_NPC_STEP_B", "SOUND_HEAVY_NPC_STEP_C"},
                                          .jump = "SOUND_LARGE_ACTOR_JUMP", .heal = "SOUND_BOWSER_CAST_RECOVER",
                                          .cast = {"SOUND_BOWSER_STAR_ROD_GATHER", "SOUND_BOWSER_STAR_ROD_USE"},
                                          .barrier = {"SOUND_BOWSER_STAR_ROD_ENCHANT",
                                                      "SOUND_BOWSER_STAR_ROD_SHOCKWAVE"},
                                          .death = "SOUND_BOWSER_COLLAPSE"};

static const Pm64Boss kUnits[] = {
    /* Kammy Koopa (PM64 src/battle/area/kkj/actor/kammy_koopa.c): 10 HP, defence 0, flying (BTL_POS_AIR_C, her
       EVS_Idle bobs her 3 up and down). One move, every turn: the Block Drop (EVS_TakeTurn: FlyRodTalk, a pink
       gather of energy 20 frames, a Hammer1Block grows on her rod over 60 frames and rises off screen, FlyRodCast
       10 frames, then it falls on the target in 20 frames: 4 damage, magic, no contact, a hard shake). Her first
       line (EVS_HandlePhase, the first player turn: MSG_CH8_0093) is spoken here before her first Block Drop.
       Here: she flies (.fly 40); FlyRodTalk (7) through the 20 + 60 frames, FlyRodCast (8) as the block falls.
       BattleKammy (96): 5 FlyIdle, 7 FlyRodTalk, 8 FlyRodCast, 13 FlyHurt. */
    {"Kammy Koopa", 0x62, -1, 96, -1, 1.0f, UNIT_UNUSED_OKORL, 10, 0, 28, 36, 5, 5, 5, 13,
     /* (EVS_TakeTurn: SOUND_KAMMY_SUMMON_MAGIC as the energy gathers, SOUND_KAMMY_LOWER_OBJECT as the block comes
        down) */
     {{MV_SHOT, 4, 7, 1, 0, NULL, .release = 8, .windup = 80, .wind = "SOUND_KAMMY_SUMMON_MAGIC",
       .go = "SOUND_KAMMY_LOWER_OBJECT", .fx = FX_BLOCK}, {-1}},
     0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 1, PM64_MSG(0x13, 0x93), 0, .fly = 40, .sounds = &kKammySfx,
     .song = 0x03 /* SONG_SPECIAL_BATTLE (PM64's boss battle theme; kkj names none of its own) */},
    /* Bowser with the Star Rod (PM64 src/battle/area/kpa2/actor/final_bowser_1.c and final_bowser_2.c, the same
       moves; drawn at SetActorScale 1.5): 99 HP, defence 2. Phase 1 here (above half) is final_bowser_1, phase 2
       (half or below) final_bowser_2 (which starts with the Star Rod's power on, and at under 50% says
       MSG_CH8_00A8). His turn (EVS_TakeTurn_Inner): on turn 2 always, then RandInt(100) < 75 once 4 turns have
       passed since the last, while its power is off: the Star Rod (EVS_StarRodCast: Brandish, the rod gathers
       light 65 frames, light rays 30, then ACTOR_EVENT_FLAG_STAR_ROD_ENCHANTED: defence 99, the radial star
       aura, every attack doubled (the DMG_BOOSTED_*), RearUpLaugh and a shockwave; it holds until the Star Beam
       or Peach Beam). Otherwise EVS_UseAttack: RandInt(80) < 20 the Body Slam (2 turns apart), RandInt(60) < 20
       the Claw Swipe (2 turns apart), else the Fire Breath; and after turn 3, from 3 turns after the last one,
       EVS_UseAttackOrShockwave: the Draining Shockwave 2 times in 3, else the Lightning Blast (about 1 turn in
       4 then). The weights below are those odds folded together; the moves' spacing their .cooldown (the turns
       that pass before one may come again). BattleBowser (128): 2 Idle, 3 Walk, 13 Hurt, 15 Brandish,
       17 PreJump, 18 Jump, 19 Land, 21 Swipe, 24 FireBreathLoop. */
    {"Bowser", 0x61, 0x6E, 128, -1, 1.5f, UNIT_UNUSED_YOWARL, 99, 2, 80, 88, 2, 3, 17, 13,
     {/* the Star Rod (EVS_Move_UseStarRod -> EVS_StarRodCast; MSG_CH8_0084 before it the first time): Brandish
         (15), then RearUpLaugh (8, .post). The barrier: defence 99 (BoostedDefense), his attacks doubled
         (DMG_BOOSTED_*), recoloured (the StarRod palette 1, .barrierPal) until the Star Beam or Peach Beam
         (EVENT_PEACH_BEAM): here until a Star Power special hits him (no Star Beam in TTYD). Looked at first
         (.pre): on his second turn always (final_bowser_1.c, turn 2), then while it's down by the turns since
         it broke (AVAR_TurnsSinceStarBeam: 1-2 never, 3 15 in 100, 4+ 75 in 100; the engine's MV_BARRIER) */
      {MV_BARRIER, 0, 15, 0, 0, NULL, .pre = 1, .post = 8},
      /* the Fire Breath (EVS_Attack_FireBreath: PreFireBreath 35 frames, FireBreathStill 10, the breath over Mario
         70 frames in FireBreathLoop, PostFireBreath 15; 10 damage, fire) */
      {MV_SHOT, 10, 24, 40, 0, "SFX_BOSS_KOOPA_FIRE_BREATH1", .windup = 45, .element = EL_FIRE,
       .go = "SOUND_BOWSER_FIRE_BREATH_LOOP", .fx = FX_FIRE_BREATH},
      /* the Claw Swipe (EVS_Attack_ClawSwipe: walks up, RearUpMock 25 frames, Swipe 3; 8 damage and poison for 3
         turns) */
      {MV_RUSH, 8, 21, 20, 0, NULL, .windup = 25, .status = ST_POISON, .statusTurns = 3, .cooldown = 1,
       .go = "SOUND_HEAVY_NPC_SWIPE"},
      /* the Body Slam (EVS_Attack_BodySlam: walks to Mario + 60 at speed 3, Idle 15, PreJump 3, Jump, onto him in
         15 frames, Land; 8 damage, crush; it can knock one of Mario's commands away, then hops back twice) */
      {MV_SLAM, 8, 18, 20, 0, "SOUND_HEAVY_NPC_LANDING|SFX_BOSS_KOOPA_HIP_ATTACK1", .cooldown = 1},
      /* the Draining Shockwave (EVS_UseDrainingShockwave: Brandish 10 frames, light rays 25, a wave over everyone:
         6 damage to Mario, 3 to the partner, magic, and every one of Mario's buffs gone (RemovePlayerBuffs); the
         same under the Star Rod: DMG_BOOSTED_SHOCKWAVE 6, .flat) */
      {MV_ALL, 6, 15, 10, 0, "SOUND_BOWSER_STAR_ROD_DRAIN_WAVE|SFX_ENM_KAMEKU_MAGIC1", .windup = 35, .partner = 3,
       .cooldown = 2, .flat = 1, .wind = "SOUND_BOWSER_STAR_ROD_USE", .fx = FX_ROD_WAVE},
      /* the Lightning Blast (EVS_Attack_LightningBlast: Brandish, energy 50 frames, light rays 30, a bolt onto
         Mario: 10 damage, shock, no contact) */
      {MV_SHOT, 10, 15, 5, 0, NULL, .windup = 80, .element = EL_SHOCK, .cooldown = 2,
       .wind = "SOUND_BOWSER_CHARGE_LIGHTNING", .go = "SOUND_BOWSER_LIGHTNING", .fx = FX_ROD_BOLT},
      /* the Recover (EVS_Recover: Brandish, energy 30 frames, heals 31% of his max HP (30; 75 here, x 2.5),
         3 times a fight, when Mario's HP% beats his by 25, 75% of the time), ahead of his other moves (.pre,
         .chance, .lead). It was below half his HP whatever Mario's: a Mario losing the fight still met all three
         (87 HP back of 95 at level 25), and no level-25 party without god mode could win */
      {MV_HEAL, 75, 15, 0, 0, NULL, .pre = 1, .chance = kRecover, .uses = 3, .lead = 25}},
     0, 0, 0, 0, {0}, 0, 18, 18, 19, 0, 1, PM64_MSG(0x13, 0x84), 0,
     /* under the Star Rod: the StarRod palette; an attack that bounces off, Mock (7) for 30 frames
        (EVS_NoDamageHit), the first time "That weak attack doesn't affect me anymore!" (MSG_CH8_0085, his taunt
        as the first part's attacks fail) and the engine's hint (a Star Power special breaks it); broken, the first
        time "My Star Rod power's gone!" (MSG_CH8_00A7, final_bowser_2.c EVENT_PEACH_BEAM) */
     .barrierPal = 2, .mockRow = 7, .bounceMsg = PM64_MSG(0x13, 0x85), .breakMsg = PM64_MSG(0x13, 0xA7),
     .sounds = &kBowserSfx, .song = 0x05 /* SONG_FINAL_BOWSER_BATTLE */},
};

const Pm64BossSet kPm64Boss_bowser = {"bowser", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 2};
