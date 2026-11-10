/* Paper Mario 64: the story's Paper Mario 64 enemies with no cousin (pm64_boss.h). */
#include "pm64_boss.h"

/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds): steps, jumps and hurts from src/battle/actors.yaml (each
   enemy's walkSound, jumpSound, hurtSound), the rest from src/battle/common/actor/<enemy>.inc.c, cited per sound */
#define FOE_STEPS .step = {"SOUND_ACTOR_STEP_A", "SOUND_ACTOR_STEP_B"}, .hurt = "SOUND_ACTOR_HURT"
static const Pm64BossSounds kFuzzySfx = {.jump = "SOUND_FUZZY_HOP_A", .hurt = "SOUND_ACTOR_HURT"};
/* (hyper_goomba.inc.c EVS_Move_Charge: SOUND_POWER_UP) */
static const Pm64BossSounds kHyperGoombaSfx = {.step = {"SOUND_GOOMBA_STEP", "SOUND_GOOMBA_STEP"},
                                               .jump = "SOUND_ACTOR_JUMP", .hurt = "SOUND_ACTOR_HURT",
                                               .power = "SOUND_POWER_UP"};
/* (hyper_paragoomba.inc.c EVS_Attack_SkyDive: SOUND_PARAGOOMBA_DIVE; EVS_Attack_ChargedDive: SOUND_POWER_UP) */
static const Pm64BossSounds kHyperParagoombaSfx = {.step = {"SOUND_PARAGOOMBA_FLY", "SOUND_PARAGOOMBA_FLY"},
                                                   .jump = "SOUND_PARAGOOMBA_DIVE", .hurt = "SOUND_ACTOR_HURT",
                                                   .power = "SOUND_POWER_UP"};
/* (shy_guy_base.inc.c, pyro_guy.inc.c EVS_Attack_Tackle / EVS_TakeTurn: SetActorSounds ACTOR_SOUND_JUMP
   SOUND_ACTOR_HOP) */
static const Pm64BossSounds kShyGuySfx = {FOE_STEPS, .jump = "SOUND_ACTOR_HOP"};
static const Pm64BossSounds kGuySfx = {FOE_STEPS};
/* (medi_guy.inc.c EVS_Move_HealOne: SOUND_RECOVER_HEART) */
static const Pm64BossSounds kMediGuySfx = {.hurt = "SOUND_ACTOR_HURT", .heal = "SOUND_RECOVER_HEART"};
static const Pm64BossSounds kClubbaSfx = {.step = {"SOUND_CLUBBA_STEP", "SOUND_CLUBBA_STEP"},
                                          .hurt = "SOUND_ACTOR_HURT"};
/* (duplighost.inc.c EVS_Attack_FlyingTackle: SetActorSounds ACTOR_SOUND_JUMP SOUND_DUPLIGHOST_LEAP) */
static const Pm64BossSounds kDuplighostSfx = {.step = {"SOUND_DUPLIGHOST_STEP", "SOUND_DUPLIGHOST_STEP"},
                                              .jump = "SOUND_DUPLIGHOST_LEAP", .hurt = "SOUND_ACTOR_HURT"};

static const Pm64Boss kUnits[] = {
    /* --- the story's Paper Mario 64 enemies that have no Thousand-Year Door cousin (port/include/port/pm64_foes.h):
       PM64's HP and attack, scaled by chapter as the cousins are; rows 1 idle, 2 walk, the rest from the sheets */
    /* (rows by the decomp's sprite animation names; moves from each actor's battle script,
       src/battle/common/actor/<enemy>.inc.c, cited per enemy) */
    /* 0: forest_fuzzy.inc.c: 6 HP; EVS_Attack_Leech (DMG_LEECH 1, through defence). (EVS_Move_Clone, 30% with one
       Fuzzy about, isn't here: a fight's new units are the formation's) */
    /* 0 */ {"Forest Fuzzy", 0x20, 0x20, 42, 1, 1.0f, UNIT_UNUSED_WANAWANA, 6, 0, 24, 26, 1, 3, 5, 8,
              /* (forest_fuzzy.inc.c EVS_Attack_Leech: SOUND_LEECH) */
              {{MV_JUMP, 1, 4, 1, 0, "SOUND_LEECH|", .fx = FX_LEECH}, {-1}}, 0, 0, 1, 3, .sounds = &kFuzzySfx},
    /* 1: hyper_goomba.inc.c: 7 HP; EVS_TakeTurn: RandInt(1000) < 500 (750 on its first turn) EVS_Move_Charge, its
       next turn the charged Headbonk (DMG_CHARGED 8), else EVS_Attack_Headbonk (DMG_HEADBONK 1) */
    /* 1 */ {"Hyper Goomba", 0x21, 0x21, 37, 2, 1.0f, UNIT_UNUSED_MINARAI_KAMEC, 7, 0, 24, 24, 1, 2, 11, 5,
              {{MV_JUMP, 1, 4, 1, 0, NULL}, {MV_WARN, 0, 11, 1, 0, NULL, 3}, {MV_JUMP, 8, 4, 0, 3, NULL, 0}, {-1}}, 0, 0, 0, 3,
              .sounds = &kHyperGoombaSfx},
    /* 2: hyper_paragoomba.inc.c: 7 HP; as the Hyper Goomba, from the air: EVS_Attack_SkyDive (DMG_SKY_DIVE 1) or,
       charged, EVS_Attack_ChargedDive (DMG_CHARGED_DIVE 8) */
    /* 2 */ {"Hyper Paragoomba", 0x22, 0x22, 39, 2, 1.0f, UNIT_UNUSED_UFO, 7, 0, 26, 30, 1, 3, 1, 6,
              {{MV_JUMP, 1, 5, 1, 0, NULL}, {MV_WARN, 0, 1, 1, 0, NULL, 3}, {MV_JUMP, 8, 5, 0, 3, NULL, 0}, {-1}}, 0, 0, 0, 3,
              .sounds = &kHyperParagoombaSfx},
    /* 3: shy_guy_base.inc.c: 7 HP; EVS_TakeTurn RandInt(1): EVS_Attack_Tackle (DMG_TACKLE 2) or EVS_Attack_Vault
       (DMG_VAULT 3), even odds */
    /* 3 */ {"Shy Guy", 0x27, 0x29, 58, 0, 1.0f, UNIT_UNUSED_SHY_GUY, 7, 0, 24, 30, 1, 2, 9, 12,
              {{MV_RUSH, 2, 5, 1, 0, NULL}, {MV_JUMP, 3, 10, 1, 0, NULL}, {-1}}, 0, 0, 0, 4, .sounds = &kShyGuySfx},
    /* 4: groove_guy.inc.c: 7 HP; EVS_Attack_Tackle (DMG_TACKLE 2); EVS_Attack_SleepySpin makes Mario dizzy (no
       damage; here a weak spin at everyone until the engine has a status move); its summons aren't here */
    /* 4 */ {"Groove Guy", 0x28, 0x2A, 59, 0, 1.0f, UNIT_UNUSED_GROOVE_GUY, 7, 0, 24, 30, 1, 3, 5, 8,
              /* (groove_guy.inc.c EVS_Attack_SleepySpin: SOUND_GROOVE_GUY_TWIRL, then SOUND_GROOVE_GUY_SPIN_DOWN) */
              {{MV_RUSH, 2, 4, 3, 0, NULL},
               {MV_ALL, 1, 10, 1, 0, "SOUND_GROOVE_GUY_SPIN_DOWN|", .wind = "SOUND_GROOVE_GUY_TWIRL"}, {-1}},
              0, 0, 0, 4, .sounds = &kGuySfx},
    /* 5: spy_guy.inc.c: 7 HP; EVS_Attack_Slingshot (DMG_SHOOT 2, no contact) or EVS_Attack_Mallet (DMG_MALLET 2) by
       the weapon it holds */
    /* 5 */ {"Spy Guy", 0x29, 0x2B, 62, 0, 1.0f, UNIT_UNUSED_SPY_GUY, 7, 0, 24, 30, 2, 4, 23, 11,
              /* (spy_guy.inc.c EVS_Attack_Slingshot: SOUND_SLINGSHOT_DRAW, SOUND_SLINGSHOT_FIRE; EVS_Attack_Mallet:
                 SOUND_MALLET_SWING) */
              {{MV_SHOT, 2, 22, 2, 0, NULL, .wind = "SOUND_SLINGSHOT_DRAW", .go = "SOUND_SLINGSHOT_FIRE"},
               {MV_RUSH, 2, 7, 1, 0, NULL, .go = "SOUND_MALLET_SWING", .fx = FX_MALLET}, {-1}}, 0, 0, 0, 4, .sounds = &kGuySfx},
    /* 6: pyro_guy.inc.c: 7 HP; EVS_TakeTurn: a fire tackle (DMG_TACKLE 4, DAMAGE_TYPE_FIRE) */
    /* 6 */ {"Pyro Guy", 0x2A, 0x2C, 61, 0, 1.0f, UNIT_UNUSED_PYRO_GUY, 7, 0, 24, 30, 1, 2, 1, 6,
              {{MV_RUSH, 4, 5, 1, 0, "SFX_BTL_DAMAGE_FIRE1"}, {-1}}, 0, 0, 0, 5, .sounds = &kShyGuySfx},
    /* 7: medi_guy.inc.c: 7 HP; EVS_TakeTurn: someone hurt, RandInt(1000) < 600 EVS_Move_HealOne, else
       EVS_Attack_Swoop (DMG_SWOOP 1). (Here it heals itself, once below half) */
    /* 7 */ {"Medi Guy", 0x2C, 0x2E, 63, 0, 1.0f, UNIT_UNUSED_BZZAP, 7, 0, 24, 30, 1, 2, 1, 6,
              /* (medi_guy.inc.c EVS_Attack_Swoop: SOUND_GATHER_SMALL, then SOUND_TOSS) */
              {{MV_RUSH, 1, 3, 2, 0, NULL, .wind = "SOUND_GATHER_SMALL", .go = "SOUND_TOSS"},
               {MV_HEAL, 5, 5, 3, 2, NULL}, {-1}}, 0, 0, 0, 4, .sounds = &kMediGuySfx},
    /* 8: clubba.inc.c: 8 HP; EVS_TakeTurn: a club swing (DMG_SWING 3, no contact) */
    /* 8 */ {"Clubba", 0x24, 0x24, 56, 0, 1.0f, UNIT_UNUSED_PENNINGTON, 8, 0, 40, 44, 2, 3, 2, 12,
              /* (clubba.inc.c EVS_TakeTurn: SOUND_CLUBBA_SWING) */
              {{MV_RUSH, 3, 18, 1, 0, "SFX_ENM_WAN_LANDING1", .go = "SOUND_CLUBBA_SWING"}, {-1}}, 0, 0, 0, 3,
              .sounds = &kClubbaSfx},
    /* 9: duplighost.inc.c (PM64 chapter 7: sam, pra3, kpa3): 15 HP, 0 defence, size 36x36; EVS_TakeTurn: 60%
       EVS_CopyPartner (it turns into Mario's partner: not here, a fight's units are the formation's), else
       EVS_Attack_FlyingTackle: Crouch 20 frames, Leap onto the target (JumpToGoal 12), DMG_FLYING_LEAP 4, Land.
       Rows (sprite 77 Duplighost): 2 Idle, 4 Run (EVS_ReturnHome), 7 Crouch, 8 Leap, 10 Hurt */
    /* 9 */ {"Duplighost", 0x4C, 0x52, 77, 0, 1.0f, UNIT_UNUSED_MECHA_KURI, 15, 0, 36, 36, 2, 4, 7, 10,
              {{MV_JUMP, 4, 8, 1, 0, NULL}, {-1}}, 0, 0, 0, 7, .sounds = &kDuplighostSfx},
    /* 10: spear_guy.inc.c (PM64 chapter 5, jan): 7 HP, 0 defence, size 24x24; EVS_TakeTurn (generation 0):
       EVS_Attack_SpearStab: Run up (speed 6), IdleFwd 5 frames, StabWindup 15, StabThrust, DMG_STAB 3 (no
       contact). (EVS_Move_SummonBackup, 10-30% by how many summon, isn't here; EVS_Attack_ThrowSpear is the
       summoned ones'.) Rows (sprite 65 SpearGuy): 4 IdleFwd, 6 Run, 26 StabWindup, 27 StabThrust, 8 HurtFwd */
    /* 10 */ {"Spear Guy", 0x36, 0x38, 65, 0, 1.0f, UNIT_UNUSED_MECHA_KAME, 7, 0, 24, 30, 4, 6, 26, 8,
               /* (spear_guy.inc.c EVS_Attack_SpearStab: SOUND_SPEAR_STAB) */
               {{MV_RUSH, 3, 27, 1, 0, NULL, .go = "SOUND_SPEAR_STAB"}, {-1}}, 0, 0, 0, 5, .sounds = &kGuySfx},
};

const Pm64BossSet kPm64Boss_story_foes = {"story_foes", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 0};
