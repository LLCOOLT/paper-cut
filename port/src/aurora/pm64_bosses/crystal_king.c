/* Paper Mario 64: the Crystal King, his Crystal Bits and his clones (pm64_boss.h).
 *
 * Drawn at PM64's own size (no SetActorScale in crystal_king.c or crystal_bit.c: 1.0).
 * PM64 src/battle/area/pra2/actor/crystal_king.c (the King and his clones), crystal_bit.c (the Bits), area.c
 * (Formation_01: the King at 70,0,5 with the cube, sphere and prism Bits floating around him).
 *
 * His turns (EVS_TakeTurn, AVAR_NextMove):
 *   - First a Recover check (EVS_Move_Recover): below 25% of his HP always, below 40% 30 in 100, below 55% 10
 *     in 100; at most twice in the fight (AVAR_HealCount < 2); 20 HP. It doesn't move the pattern on.
 *   - Above 35 HP: Spit Bits while he has Bits, then Summon Bits, then Spit Bits... (AVAL_Move_First ->
 *     SummonBits -> AttackWithBits -> AfterBitsAttack -> SummonBits ...). If the Bits are gone when he would
 *     spit them, the Ice Bolt instead (the Icy Breath if Mario is frozen).
 *   - At 35 HP or less (AfterBitsAttack, HP not > 35): the Ice Bolt once (AVAL_Flag_UsedIceBolt), then Make
 *     Clones, then the Clone Breath every turn while a clone is left (AfterClonesAttack); once Mario hits the
 *     real one (EVS_OnHit removes both clones) he makes them again.
 *
 * Here, in the engine's terms: phase 1 (above half: 35 of 70, as PM64's) Spit Bits while he has Bits
 * (.cond = C_MATES; .hits = BOSS_HITS_MATES: one hit per Bit, each used up), its follow-up Summon Bits (the Bits
 * that are gone come back to their places, .with1 and their .at); with no Bits left to spit, the Ice Bolt (or
 * the Icy Breath, Mario frozen: .cond = C_NOT_FROZEN, .else1). A phase-1 follow-up after he's crossed half is
 * dropped for a phase-2 move (AfterBitsAttack looks at his HP). Phase 2: the Ice Bolt once (.uses), then Make
 * Clones (two illusory copies, unit 5; a hit on the real one makes them vanish), then the Clone Breath while a
 * clone stands (.cond = C_CLONES), else Make Clones again. The Recover first every turn by his HP share
 * (.pre, .chance), twice at most (.uses). Spits and breaths: Inhale (19) the wind-up, ExhaleLoop (28) the
 * release, PostExhale (21) once the hit lands (.post).
 * CrystalKing (126): 0 Still, 9 Idle, 10 CubeIdle, 11 SphereIdle, 12 PrismIdle, 14 Walk, 19 Inhale, 21
 * PostExhale, 25 Hurt, 28 ExhaleLoop, 29 IdleArmsUp, 30 IdleArmsForward, 31 ReachUp. */
#include "pm64_boss.h"

/* the Recover's odds by his HP share (EVS_TakeTurn: below 25% always, below 40% 30 in 100, below 55% 10) */
static const signed char kRecover[] = {25, 100, 40, 30, 55, 10, 0};
/* his Paper Mario sounds (pm64_boss.h Pm64BossSounds), from crystal_king.c: EVS_Move_SummonBits
   SOUND_SUMMON_CRYSTAL_BITS; EVS_Move_MakeClones SOUND_SUMMON_CRYSTAL_CLONES and SOUND_SPOOKY_LEVITATE as they glide
   off; EVS_Move_Recover SOUND_RECOVER_HEART. The Bits' from crystal_bit.c EVS_HandleEvent (EVENT_DEATH:
   SOUND_FALL_QUICK as one drops) */
static const Pm64BossSounds kKingSfx = {.summon = "SOUND_SUMMON_CRYSTAL_BITS",
                                        .clones = {"SOUND_SUMMON_CRYSTAL_CLONES", "SOUND_SPOOKY_LEVITATE"},
                                        .heal = "SOUND_RECOVER_HEART"};
static const Pm64BossSounds kBitSfx = {.death = "SOUND_FALL_QUICK"};
/* (his breaths: SOUND_RECOVER_HEART as the sparkles gather, SOUND_HEAVY_WIND_LOOP as he blows: EVS_Attack_IcyBreath,
   EVS_Attack_CloneBreath) */
#define CK_BREATH .wind = "SOUND_RECOVER_HEART", .go = "SOUND_HEAVY_WIND_LOOP"
/* (EVS_Attack_IceBolt: SOUND_RECOVER_HEART, then SOUND_CRYSTAL_KING_ICE_BOLT) */
#define CK_BOLT .wind = "SOUND_RECOVER_HEART", .go = "SOUND_CRYSTAL_KING_ICE_BOLT"

static const Pm64Boss kUnits[] = {
    /* 1: the Crystal King (crystal_king.c): 70 HP, defence 2 (fire 0, ice immune), 56x56.
       1 Spit Bits (EVS_Attack_SpitBits): ExhaleLoop 20, the Bits sucked in along a curve over 30 frames
       (EVS_InhaleBit), Inhale 30, then each Bit spat at Mario: ExhaleLoop 4, PostExhale 6 (EVS_SpitSingleBit,
       4 damage each, ice, no contact); the Bits are gone. Then (next1) Summon Bits.
       2 Summon Bits (EVS_Move_SummonBits): IdleArmsForward 20, ReachUp, the prism Bit, 10 frames, the sphere,
       24, the cube, 130 (EVS_SummonBit: each rises from him to its place). Then (next1) Spit Bits.
       3 Ice Bolt (EVS_Attack_IceBolt): Inhale 10 + sparkles 30, ExhaleLoop, the bolt (EFFECT_LIGHTNING_BOLT in
       white and ice blue) 7 + 3 frames, PostExhale, 6 damage, ice, may freeze for 2 turns. At half HP or less;
       then (next1) Make Clones.
       4 Make Clones (EVS_Move_MakeClones): two copies at his HP, Still then Idle, SOUND_SUMMON_CRYSTAL_CLONES,
       the three glide to new places over 60 frames, 150 more. Then (next1) Clone Breath.
       5 Clone Breath (EVS_Attack_CloneBreath): all three Inhale 10 + sparkles 30, ExhaleLoop, the three
       breaths 60 frames, PostExhale, 8 damage, ice (one hit, from the King). Then (next1) Make Clones (PM64:
       the breath again while a clone is left). */
    {"Crystal King", 0x57, 0x5D, 126, -1, 1.0f, UNIT_UNUSED_MECHA_KAME, 70, 2, 40, 60, 9, 14, 19, 25,
     {/* 1 Spit Bits */
      {MV_SHOT, 4, 19, 1, 1, NULL, 2, .release = 28, .windup = 50, .post = 21, .element = EL_ICE,
       .hits = BOSS_HITS_MATES, .cond = C_MATES, .else1 = 3,
       /* (EVS_Attack_SpitBits: SOUND_CRYSTAL_KING_INHALE; EVS_SpitSingleBit: SOUND_LIGHT_THROW each Bit) */
       .wind = "SOUND_CRYSTAL_KING_INHALE", .go = "SOUND_LIGHT_THROW", .fx = FX_SPIT_BITS},
      /* 2 Summon Bits (phase 1: weight 0, a follow-up only) */
      {MV_SUMMON, 0, 31, 0, 1, NULL, 1},
      /* 3 Ice Bolt, with no Bits to spit (AttackWithBits); then Summon Bits */
      {MV_SHOT, 6, 19, 0, 3, NULL, 2, .release = 28, .windup = 40, .post = 21, .element = EL_ICE,
       .status = ST_FREEZE, .statusTurns = 2, .cond = C_NOT_FROZEN, .else1 = 7, CK_BOLT, .fx = FX_ICE_BOLT},
      /* 4 Ice Bolt, phase 2's first (AfterBitsAttack, AVAL_Flag_UsedIceBolt: once); then Make Clones */
      {MV_SHOT, 6, 19, 1, 2, NULL, 5, .release = 28, .windup = 40, .post = 21, .element = EL_ICE,
       .status = ST_FREEZE, .statusTurns = 2, .uses = 1, .cond = C_NOT_FROZEN, .else1 = 7, CK_BOLT, .fx = FX_ICE_BOLT},
      /* 5 Make Clones (with none standing); then the Clone Breath */
      {MV_CLONES, 0, 9, 1, 2, NULL, 6, .unit1 = 5, .count = 2, .cond = C_NO_CLONES},
      /* 6 Clone Breath, while a clone stands (AfterClonesAttack), again; else Make Clones */
      {MV_SHOT, 8, 19, 1, 2, NULL, 6, .release = 28, .windup = 40, .post = 21, .element = EL_ICE,
       .cond = C_CLONES, .else1 = 5, CK_BREATH, .fx = FX_ICY_BREATH},
      /* 7 Icy Breath (Mario frozen, in the Ice Bolt's place): 8 damage, ice; then Summon Bits (phase 1) */
      {MV_SHOT, 8, 19, 0, 3, NULL, 2, .release = 28, .windup = 40, .post = 21, .element = EL_ICE, CK_BREATH, .fx = FX_ICY_BREATH},
      /* 8 Recover: 20 HP of PM64's 70 (50 of the post-game 175), IdleArmsUp */
      {MV_HEAL, 50, 29, 0, 0, NULL, .pre = 1, .chance = kRecover, .uses = 2}},
     0, 0, 0, 0, {2, 3, 4}, .immune = IM_ICE, .sounds = &kKingSfx, .song = 0x0F /* SONG_CRYSTAL_KING_BATTLE */},
    /* 2-4: the Crystal Bits (crystal_bit.c): 1 HP, defence 0 (ice immune), they fly, never attack (EVS_TakeTurn
       empty), 18x15. Falling (EVENT_DEATH): down to the ground in 15 frames, CubeInert/SphereInert/PrismInert,
       three bounces 30/20/10 apart, fade out in 20 frames. When the King falls, or is hit while asleep, dizzy,
       stopped or shrunk during his Bits turns, the Bits fall with him (EVS_Death, EVS_OnHit).
       Their places from the King's (area.c KingPos 70,0,5; CrystalBitPos1..3): cube -60,35,-10; sphere
       42,52,-10; prism -28,85,-15 (.at), floating (.fly); summoned back, each appears in its place in a
       puff (.enter). */
    {"Crystal Bit", 0x58, 0x5F, 126, -1, 1.0f, UNIT_UNUSED_ZESS_T, 1, 0, 16, 16, 10, 10, 10, 10, {{-1}},
     .at = {-60, 35, -10}, .enter = ENTER_PLACE, .immune = IM_ICE, .fly = 35, .sounds = &kBitSfx},
    {"Crystal Bit", 0x58, 0x5F, 126, -1, 1.0f, UNIT_UNUSED_MASTER, 1, 0, 16, 16, 11, 11, 11, 11, {{-1}},
     .at = {42, 52, -10}, .enter = ENTER_PLACE, .immune = IM_ICE, .fly = 52, .sounds = &kBitSfx},
    {"Crystal Bit", 0x58, 0x5F, 126, -1, 1.0f, UNIT_UNUSED_REPORTER, 1, 0, 16, 16, 12, 12, 12, 12, {{-1}},
     .at = {-28, 85, -15}, .enter = ENTER_PLACE, .immune = IM_ICE, .fly = 85, .sounds = &kBitSfx},
    /* 5: a clone (crystal_king.c N(clone), EVS_Init_Clone: no turns, no events): his HP and defence, his
       status and buffs (CopyStatusEffects, CopyBuffs), no shadow, illusory; while clones are up the three pulse
       between 128 and 254 alpha (EVS_Idle, 20 frames). Its own Tattle (0x5E). */
    {"Crystal King", 0x57, 0x5E, 126, -1, 1.0f, UNIT_UNUSED_HOTDOG_MASTER, 70, 2, 40, 60, 9, 14, 19, 25, {{-1}},
     .immune = IM_ICE, .illusory = 1},
};

const Pm64BossSet kPm64Boss_crystal_king = {"crystal_king", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
