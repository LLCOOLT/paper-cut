/* Paper Mario 64: Jr. Troopa (pm64_boss.h).
 *
 * The refight is his last and fullest one, PM64 src/battle/area/kmr_part_3/actor/final_jr_troopa.c (60 HP,
 * defence 2, Tattle 0x1C 0x6B): one actor with three forms that EVS_TakeTurn switches at the start of his turn,
 * below 65% of his HP into the winged, spiked form (EVS_Transform_Flying) and below 35% into the mage
 * (EVS_Transform_Mage). His earlier fights (jr_troopa.c, egg_, para_, spiked_para_, mage_jr_troopa.c) are each
 * one of these forms on its own, so the last fight covers them all. His FIRST fight (kmr_part_3 jr_troopa.c: 5 HP, a
 * Leap for 1) is unit 4, the story meeting's and a refight's "first fight" (.early1).
 *
 * Here each form is a unit and the next takes its place in a puff where it stood (spawn1, .exit = EXIT_TRANSFORM,
 * .enter = ENTER_PLACE) as the hit that would fell it lands (.phaseAt = -1). PM64's 60 HP split by the
 * thresholds: 21 HP as himself (60 -> 39), 18 winged (39 -> 21), 21 as the mage: 60 in all.
 * Rows by the decomp's animation names (anims.py): JrTroopa (32), SpikedParaJrTroopa (34), MageJrTroopa (35). */
#include "pm64_boss.h"

/* his Paper Mario sounds (pm64_boss.h Pm64BossSounds), from src/battle/actors.yaml JrTroopa_1..6 (walkSound
   SOUND_SMALL_NPC_STEP, hurtSound SOUND_ACTOR_HURT) and final_jr_troopa.c: EVS_Attack_Leap's SetActorSounds
   ACTOR_SOUND_JUMP SOUND_JR_TOOPA_JUMP (the decomp's spelling); EVS_TransformFX's SOUND_JR_TROOPA_TRANSFORM as each
   form gives way; EVS_Attack_SpikeDive's SOUND_PARAGOOMBA_DIVE as he dives; EVS_Move_HealSelf's
   SOUND_RECOVER_HEART; EVS_JrTroopa_Death's SOUND_ACTOR_DEATH */
static const Pm64BossSounds kJrSfx = {.step = {"SOUND_SMALL_NPC_STEP", "SOUND_SMALL_NPC_STEP"},
                                      .jump = "SOUND_JR_TOOPA_JUMP", .hurt = "SOUND_ACTOR_HURT",
                                      .exit = "SOUND_JR_TROOPA_TRANSFORM"};
static const Pm64BossSounds kJrFlyingSfx = {.jump = "SOUND_PARAGOOMBA_DIVE", .hurt = "SOUND_ACTOR_HURT",
                                            .exit = "SOUND_JR_TROOPA_TRANSFORM"};
static const Pm64BossSounds kJrMageSfx = {.step = {"SOUND_SMALL_NPC_STEP", "SOUND_SMALL_NPC_STEP"},
                                          .hurt = "SOUND_ACTOR_HURT", .heal = "SOUND_RECOVER_HEART",
                                          .death = "SOUND_ACTOR_DEATH"};

/* his first fight's (kmr_part_3 jr_troopa.c): SOUND_JR_TOOPA_JUMP (EVS_TakeTurn's SetActorSounds), SOUND_ACTOR_DEATH
   (EVS_JrTroopa_Death) */
static const Pm64BossSounds kJrFirstSfx = {.step = {"SOUND_SMALL_NPC_STEP", "SOUND_SMALL_NPC_STEP"},
                                           .jump = "SOUND_JR_TOOPA_JUMP", .hurt = "SOUND_ACTOR_HURT",
                                           .death = "SOUND_ACTOR_DEATH"};
/* his powered-up Leap: at 1 HP of 5 (below 21%), always */
static const signed char kJrPoweredUp[] = {21, 100, 0};

static const Pm64Boss kUnits[] = {
    /* 1: Jr. Troopa himself (final_jr_troopa.c PRT_BASE, BaseAnims). JrTroopa (32): 3 Idle, 9 Run, 14 Jump,
       15 Midair, 16 Fall, 17 Hurt.
       His one move as himself, the Leap (EVS_Attack_Leap, DMG_LEAP 8): runs to 50 in front of Mario at speed 6,
       Jump 2 frames, Midair, Fall after 8, JumpToGoal 15 frames onto him, 8 damage, then three hops back
       (10, 8, 6 frames), Idle 8 frames and runs home at speed 7.
       His opening line, MSG_CH8_005D "Here it comes, Mario!! This is the final fight!!!" (EVS_HandlePhase,
       PHASE_PLAYER_BEGIN, the first time, in PointTalk 26): his first turn's line (.startMsg, .talkRow).
       As he gives way, MSG_CH8_005E "Darn it!!!!! ... Ultimate Power!!!" (EVS_Transform_Flying's ActorSpeak in
       TiredTalk, then EVS_TransformFX): his fleeMsg, spoken by every exit, then the puff.
       The Leap: Jump (14) the crouch, Midair (15) on the way up, Fall (16) on the way down (.release). */
    {"Jr. Troopa", 0x13, 0x6B, 32, -1, 1.0f, UNIT_UNUSED_CRUMP2, 21, 2, 24, 32, 3, 9, 14, 17,
     {{MV_JUMP, 8, 15, 1, 0, NULL, .release = 16}, {-1}},
     0, 2, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, PM64_MSG(0x13, 0x5E), .phaseAt = -1, .exit = EXIT_TRANSFORM,
     .startMsg = PM64_MSG(0x13, 0x5D), .talkRow = 26, .sounds = &kJrSfx,
     .song = 0x04 /* SONG_JR_TROOPA_BATTLE */, .early1 = 4 /* his first fight (below) */},
    /* 2: winged and spiked (final_jr_troopa.c PRT_FLYING, FlyingAnims; EVS_Transform_Flying: flies up to home
       + (20, 60), ACTOR_FLAG_FLYING, a spiky top). SpikedParaJrTroopa (34): 3 Idle, 5 EggIdle, 7 FlyFast,
       8 Dive, 9 Hurt, 17 EnterEgg, 18 ExitEgg.
       EVS_TakeTurn: RandInt(100) < 50 the Swoop, else the Spike Dive.
       The Swoop (EVS_Attack_Swoop, DMG_SWOOP 8): FlyFast to 50 in front of Mario at speed 6, Wait 10, a hop up
       (10 frames), Dive into him at speed 8, 8 damage, on past him and FlyFast home.
       The Spike Dive (EVS_Attack_SpikeDive, DMG_SPIKE_DIVE 9): Wait 20, EnterEgg 10 frames, EggIdle 10, then
       an arc down onto Mario spinning shell-first (JumpToGoal 20 frames), 9 damage, ExitEgg and FlyFast home.
       As he gives way, MSG_CH8_005F "Shoooot!!!!!!!!!!! ... How about this!!!???" (EVS_Transform_Mage, Talk).
       Here: he hovers 60 up (.fly), spiked on top (.hazard = HZ_SPIKE: a jump on him hurts); the Swoop a rush,
       FlyFast (7) the 10-frame wait, Dive (8) the charge; the Spike Dive a jump, EnterEgg (17) the crouch,
       EggIdle (5) through the arc, ExitEgg (18) once it lands (.post). */
    {"Jr. Troopa", 0x13, 0x6B, 34, -1, 1.0f, UNIT_UNUSED_RED_SPIKY_PARABUZZY, 18, 2, 32, 38, 3, 7, 17, 9,
     /* (EVS_Attack_Swoop: SOUND_PARAGOOMBA_PREDIVE as he hops up, SOUND_PARAGOOMBA_DIVE as he dives) */
     {{MV_RUSH, 8, 7, 50, 0, NULL, .release = 8, .windup = 10, .wind = "SOUND_PARAGOOMBA_PREDIVE",
       .go = "SOUND_PARAGOOMBA_DIVE"},
      {MV_JUMP, 9, 5, 50, 0, NULL, .post = 18}, {-1}},
     0, 3, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, PM64_MSG(0x13, 0x5F), .phaseAt = -1, .exit = EXIT_TRANSFORM,
     .enter = ENTER_PLACE, .hazard = HZ_SPIKE, .fly = 60, .sounds = &kJrFlyingSfx},
    /* 3: the mage (final_jr_troopa.c PRT_MAGE, MageAnims; EVS_Transform_Mage: falls back to the ground).
       MageJrTroopa (35): 2 Idle, 4 Walk, 6 RaiseStaff, 7 SwingStaff, 9 Hurt.
       EVS_TakeTurn: RandInt(100) < 60 the Magic Spell, < 80 the Lightning Bolt, else Heal Self (when hurt and
       with heals left, AVAR_HealsLeft 5; otherwise the Magic Spell).
       The Magic Spell (EVS_Attack_MagicSpell, DMG_SPELL 8): RaiseStaff 8 frames, energy gathers for 30,
       SwingStaff, the spell flies at Mario for 15 frames, 8 magic damage, no contact.
       The Lightning Bolt (EVS_Attack_LightningBolt, DMG_LIGHTNING 9 (US; JP 5)): RaiseStaff 8, gathers 30,
       SwingStaff, the sky darkens over 60 frames, a bolt onto Mario 14 frames later, 9 shock damage.
       Heal Self (EVS_Move_HealSelf): Wait 30, RaiseStaff 8, gathers 30, sparkles, heals 17% of his max HP
       (US; JP 16%): 10 of PM64's 60, here 25 (x 2.5, the post-game HP).
       His low-HP line, MSG_CH8_0060 "Aaaargh!!! You've still got some skills!!" (EVS_TakeTurn, the first turn
       under 15 HP of 60): his low-HP line, under 15 of the mage's 21 (.lowAt 71), in Talk (14).
       Here: both spells RaiseStaff (6) through the 8 + 30 frames, SwingStaff (7) as they fly (.release,
       .windup 38); the bolt shock (EL_SHOCK) with the stage darkening over 60 frames first (.darken); Heal Self at
       most 5 times (.uses), only when he's hurt. */
    {"Jr. Troopa", 0x13, 0x6B, 35, -1, 1.0f, UNIT_UNUSED_ZESS_T, 21, 2, 24, 32, 2, 4, 6, 9,
     /* (EVS_Attack_MagicSpell: SOUND_SPELL_CAST1 as he raises the staff, SOUND_SPELL_CAST2 as the spell flies;
        EVS_Attack_LightningBolt: SOUND_SPELL_CAST1, SOUND_BIG_POWER_UP, SOUND_LIGHTNING_STRIKE as it strikes) */
     {{MV_SHOT, 8, 6, 60, 0, NULL, .release = 7, .windup = 38, .wind = "SOUND_SPELL_CAST1",
       .go = "SOUND_SPELL_CAST2", .fx = FX_SPELL},
      {MV_SHOT, 9, 6, 20, 0, "SOUND_LIGHTNING_STRIKE|SFX_BTL_THUNDERS_ATTACK1", .release = 7, .windup = 38,
       .element = EL_SHOCK, .darken = 60, .wind = "SOUND_SPELL_CAST1", .go = "SOUND_BIG_POWER_UP", .fx = FX_STAFF_BOLT},
      {MV_HEAL, 25, 6, 20, 0, NULL, .uses = 5}, {-1}},
     0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, 0, .enter = ENTER_PLACE, .lowAt = 71, .lowMsg = PM64_MSG(0x13, 0x60),
     .talkRow = 14, .sounds = &kJrMageSfx},
    /* 4: his FIRST fight, the prologue's (PM64 src/battle/area/kmr_part_3/actor/jr_troopa.c, ACTOR_TYPE_JR_TROOPA_1):
       plain Jr. Troopa, 5 HP, defence 0, no Tattle (actors.yaml JrTroopa_1: MSG_NONE), no transforms. The story
       meeting in Rogueport fights this one (unit 1's .early1), and refights may ask for it ("first fight").
       EVS_TakeTurn: every turn the Leap (runs to 50 in front of Mario at speed 6, a crouch of 6 frames, Jump,
       Midair after 3, Fall after 5 more, JumpToGoal 15 frames onto him, AVAR_AttackDamage: DMG_INITIAL 1; then
       three hops back and home). When he has lost 3 HP (EVS_TakeTurn, once) MSG_CH0_00B1; at 1 HP or less
       (EVS_HandlePhase, once) MSG_CH0_00B3, ChargeArmsUp and the pink gather, and from then on DMG_FULL_POWER 2.
       Here: move 1 the Leap for 1; move 2 the powered-up Leap for 2, looked at first (.pre) below 21% of his HP
       (1 HP of 5: .chance), his power-up line before it the first time (.msg); the 3-HP line his low line (.lowAt
       41: 2 HP of 5). His death (EVS_JrTroopa_Death): SOUND_ACTOR_DEATH. */
    {"Jr. Troopa", 0x13, -1, 32, -1, 1.0f, UNIT_UNUSED_CRUMP2, 5, 0, 24, 32, 3, 9, 14, 17,
     {{MV_JUMP, 1, 15, 1, 0, NULL, .release = 16},
      {MV_JUMP, 2, 15, 0, 0, NULL, .release = 16, .pre = 1, .chance = kJrPoweredUp, .msg = PM64_MSG(0x0B, 0xB3)},
      {-1}},
     0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, 0, .lowAt = 41, .lowMsg = PM64_MSG(0x0B, 0xB1), .talkRow = 26,
     .sounds = &kJrFirstSfx, .song = 0x04 /* SONG_JR_TROOPA_BATTLE */},
};

const Pm64BossSet kPm64Boss_jr_troopa = {"jr_troopa", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
