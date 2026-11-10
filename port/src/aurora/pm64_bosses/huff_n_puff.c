/* Paper Mario 64: Huff N. Puff (pm64_boss.h). */
#include "pm64_boss.h"

/* his twelve turns (EVS_TakeTurn's AVAR_NextMove, move numbers below): HealOrSlam, Breath, HealOrSlam, Charge
   (ground), Ground Lightning, HealOrSlam, Breath, Charge (direct), Direct Lightning, HealOrSlam, Charge (ground),
   Ground Lightning; HealOrSlam is the inhale (7) with Tuff Puffs out, else (.else1) the Body Slam (1) */
static const signed char kTurns[] = {7, 2, 7, 3, 4, 7, 2, 6, 5, 7, 3, 4, 0};
/* every row is three in a row: the body, then its face and arms over it (PRT_MAIN, PRT_FACE, PRT_ARMS) */
static const signed char kParts[] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
                                     21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
                                     40, 44, 45, 46, 47, 48, 49, -1};
/* the Group Slam: every turn from 5 HP of 60 (AVAL_Move_Desperation) */
static const signed char kLastStand[] = {9, 100, 0};
/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds), from flo2/actor/huff_n_puff.c: src/battle/actors.yaml
   HuffNPuff flySound SOUND_HUFF_N_PUFF_FLY as he floats over; EVS_Attack_BodySlam SOUND_LARGE_ACTOR_JUMP;
   EVS_Attack_GroupSlam SOUND_HEAVY_NPC_LANDING; EVS_Move_HealOrSlam SOUND_HUFF_N_PUFF_INHALE as he draws the Tuff
   Puffs in, SOUND_RECOVER_HEART; EVS_Move_ChargeForGround / _ForDirect SOUND_CHARGE_LIGHTNING. The Tuff Puffs':
   EVS_Attack_TuffPuffSwarm SOUND_RUFF_PUFF_FLY_A as each flies in */
static const Pm64BossSounds kHuffSfx = {.step = {"SOUND_HUFF_N_PUFF_FLY", NULL}, .stepDist = 80,
                                        .jump = "SOUND_LARGE_ACTOR_JUMP", .land = "SOUND_HEAVY_NPC_LANDING",
                                        .power = "SOUND_CHARGE_LIGHTNING", .heal = "SOUND_RECOVER_HEART",
                                        .inhale = "SOUND_HUFF_N_PUFF_INHALE"};
static const Pm64BossSounds kTuffSfx = {.jump = "SOUND_RUFF_PUFF_FLY_A"};

static const Pm64Boss kUnits[] = {
    /* Huff N. Puff (PM64 src/battle/area/flo2/actor/huff_n_puff.c): 60 HP, defence 0 (shock 99: immune), a
       flyer (ACTOR_FLAG_FLYING, home { 80, 80, 0 } in flo2 area.c), actor size 125 x 95. EVS_TakeTurn plays a
       fixed rotation through AVAR_NextMove, twelve turns long:
         Basic_1 HealOrSlam, Breath_1 HurricaneBreath, Basic_2 HealOrSlam, PreCharge_1 ChargeForGround,
         Charged_1 GroundLightning, Basic_3 HealOrSlam, Breath_2 HurricaneBreath, PreCharge_2 ChargeForDirect,
         Charged_2 DirectLightning, Basic_4 HealOrSlam, PreCharge_3 ChargeForGround, Charged_3 GroundLightning,
       then Basic_1 again. Here the twelve turns are kTurns (.order).
       - EVS_Move_HealOrSlam: with Tuff Puffs out he inhales them all (InhaleBody, SOUND_HUFF_N_PUFF_INHALE,
         each sucked in along a path, 40 frames), BragBody, heals 1 HP per small one and 2 per large one, and
         grows back by each one's max HP (scale capped at 100%); with none (or 50% when he has healed before
         and the heal would be 2 or less) EVS_Attack_BodySlam: flies over Mario (FlyToGoal 40 frames), Wait 10,
         JumpBody, JumpToGoal 20 frames onto him, 5 damage (DMG_SLAM), a bounce back (JumpWithBounce 20),
         IdleBody, flies home. A miss: four ever-smaller bounces away in IdleSadBody.
       - EVS_Move_HurricaneBreath: InhaleBody 90 frames while he swells 25%, HoldBreathBody 60 frames, BlowBody
         with SOUND_HEAVY_WIND_LOOP, a 200-frame Whirlwind mash command as he shrinks back 25% and Mario spins;
         7 damage unmashed down to 2 fully mashed (DMG_WIND_6..DMG_WIND_1), no contact. Here: 5 (DMG_WIND_4,
         the middle of the meter) as a shot from where he floats.
       - EVS_Move_ChargeForGround / EVS_Move_ChargeForDirect: IdleChargedBody (his idle while charged, with a
         jitter: EVS_UpdateChargedJitter), electrified to the touch (ACTOR_EVENT_FLAG_ELECTRIFIED), the snaking
         static on him, SOUND_CHARGE_LIGHTNING, a 60-frame shake, Wait 60 + 30.
       - EVS_Attack_GroundLightning: the sky darkens over 60 frames (SetBackgroundAlpha 0 -> 200), Wait 20,
         UnleashBody, SOUND_HUFF_N_PUFF_LIGHTNING_BLAST, ten bolts strike the ground, a hard shake; 12 damage
         (DMG_GROUND_LIGHTNING: DAMAGE_TYPE_QUAKE | IGNORE_DEFENSE | NO_CONTACT).
       - EVS_Attack_DirectLightning: flies over Mario (FlyToGoal 40 frames), the sky darkens over 40, Wait 30,
         UnleashBody, SOUND_HUFF_N_PUFF_FIRE_LIGHTNING, a bolt down onto him: 10 damage (DMG_DIRECT_LIGHTNING:
         DAMAGE_TYPE_SHOCK | IGNORE_DEFENSE | NO_CONTACT), flies home.
       - EVS_Attack_GroupSlam (AVAL_Move_Desperation, from his first turn at 5 HP or less while not charged):
         MSG_CH6_00D5 the first time (EVS_Dialogue_Concerned), InhaleBody 8 frames, HoldBreathBody and two
         20-frame jiggles (SOUND_HUFF_N_PUFF_JIGGLE), then he and every Tuff Puff slam down on Mario: 5 damage
         + 1 per Tuff Puff (DAMAGE_TYPE_QUAKE | NO_CONTACT).
       - EVS_TrySpawningRuffPuff (each hit, EVS_HuffNPuff_Hit): Tuff Puffs break off him, one per point of
         damage (up to 4; up to 8 on a Nice/Super hit, half of them large), into ten places round him; he
         shrinks 1% per point of damage (down to 50%). EVS_Idle: a squash-and-stretch wobble, stronger as a
         combo lands (EVS_IncreaseWobble).
       - The Tuff Puff swarm (EVS_Attack_TuffPuffSwarm, before his move whenever Tuff Puffs are out; the first
         time MSG_CH6_00CF / 00D0): they surround Mario and squeeze, 1 damage each a beat (2, unblockable, while
         the Stop Leech mash runs) until he shakes them off. Tuff Puffs (tuff_puff.c): 1 HP, defence 0, shock
         immune, no turn of their own (EVS_TakeTurn does nothing); TuffPuff (75): 2 IdleSmall, 3 IdleLarge,
         6/7 FlyFast, 9/11 Grin, 12/13 Hurt, 18/19 Shock, 20/21 Talk.
       His line MSG_CH6_00D1 ("Now you'll witness the full power of my storm!") before his first Charge
       (EVS_TakeTurn -> EVS_Dialogue_FullPower).
       Here: he floats 80 up (.fly), shock can't hurt him (.immune), drawn in three parts (.layers), smaller as
       his HP goes down to half (.shrink 50) and wobbling as he idles (.wobble); each hit breaks a Tuff Puff off
       him (.shed1, up to 6 out); charged he's his "Charged" palette (.chargedPal) and electrified to touch (the
       Charges' .hazard = HZ_SHOCK); both lightnings darken the stage first (.darken); the Hurricane Breath swells
       him 25% (.swell) through the inhale (90 frames; the hold's 60 more: .windup 150).
       HuffNPuff (123): 1 IdleBody, 4 IdleSadBody, 7 HurtBody, 11 JumpBody, 14 UnleashBody, 17 IdleChargedBody,
       20 BraceBody, 23 BragBody, 26 InhaleBody, 29 HoldBreathBody, 32 BlowBody, 35 PanicBody, 38 FrownBody,
       44 TalkBody. Every row is three parts in a row: Body, Face, Arms (row, row + 1, row + 2), drawn together
       (PRT_MAIN, PRT_FACE, PRT_ARMS). Palette 1 is "Charged". */
    {"Huff N. Puff", 0x45, 0x4A, 123, -1, 1.0f, UNIT_UNUSED_MECHA_KURI, 60, 0, 80, 72, 1, 1, 11, 7,
     {/* 1: Body Slam (EVS_Attack_BodySlam via EVS_Move_HealOrSlam): 5 damage, JumpBody */
      {MV_SLAM, 5, 11, 1, 0, "SOUND_HEAVY_NPC_LANDING|SFX_ENM_WAN_LANDING1"},
      /* 2: Hurricane Breath (EVS_Move_HurricaneBreath): InhaleBody, BlowBody, 5 damage (2..7 by the mash) */
      /* (SOUND_HUFF_N_PUFF_INHALE, then SOUND_HEAVY_WIND_LOOP as he blows) */
      {MV_SHOT, 5, 26, 0, 0, "SFX_BTL_CLAUD_BREATH1", .release = 32, .windup = 150, .swell = 25,
       .wind = "SOUND_HUFF_N_PUFF_INHALE", .go = "SOUND_HEAVY_WIND_LOOP", .fx = FX_HURRICANE},
      /* 3: Charge (EVS_Move_ChargeForGround): IdleChargedBody, electrified */
      {MV_WARN, 0, 17, 0, 0, NULL, .hazard = HZ_SHOCK},
      /* 4: Ground Lightning (EVS_Attack_GroundLightning): the sky darkens 60 frames, Wait 20, UnleashBody,
         12 damage */
      {MV_ALL, 12, 17, 0, 0, "SOUND_HUFF_N_PUFF_LIGHTNING_BLAST|SFX_ITEM_ICE_THUNDER_FALL1", .release = 14,
       .windup = 20, .darken = 60, .fx = FX_GROUND_BOLTS},
      /* 5: Direct Lightning (EVS_Attack_DirectLightning): the sky darkens 40 frames, Wait 30, UnleashBody, a
         bolt onto Mario, 10 shock damage */
      {MV_SHOT, 10, 17, 0, 0, "SFX_BTL_THUNDERS_ATTACK1", .release = 14, .windup = 30, .element = EL_SHOCK,
       .darken = 40, .go = "SOUND_HUFF_N_PUFF_FIRE_LIGHTNING", .fx = FX_SKY_BOLT},
      /* 6: Charge (EVS_Move_ChargeForDirect): as 3 */
      {MV_WARN, 0, 17, 0, 0, NULL, .hazard = HZ_SHOCK},
      /* 7: the inhale of HealOrSlam: the Tuff Puffs drawn in, 2 HP each here (1 of PM64's 60, x 2.5),
         BragBody after; none out: the Body Slam */
      {MV_SWALLOW, 2, 26, 0, 0, NULL, .cond = C_MATES, .else1 = 1, .post = 23},
      /* 8: the Group Slam (EVS_Attack_GroupSlam, from 5 HP: .pre, kLastStand): MSG_CH6_00D5 the first time,
         5 damage + 1 per Tuff Puff (.perMate) */
      {MV_SLAM, 5, 11, 0, 0, "SOUND_HEAVY_NPC_LANDING|SFX_ENM_WAN_LANDING1", .pre = 1, .chance = kLastStand,
       .perMate = 1,
       .msg = PM64_MSG(0x11, 0xD5)}},
     0, 0, 0, 0, {0}, 17, 0, 0, 0, 0, 3, PM64_MSG(0x11, 0xD1), 0, .immune = IM_SHOCK, .fly = 80, .shrink = 50,
     .wobble = 4, .shed1 = 2, .shedMax = 6, .chargedPal = 2, .order = kTurns, .layers = kParts, .sounds = &kHuffSfx,
     .song = 0x0E /* SONG_HUFF_N_PUFF_BATTLE */},
    /* 2: a Tuff Puff (PM64 src/battle/area/flo2/actor/tuff_puff.c): 1 HP, defence 0 (shock 99), a flyer, actor
       size 24 x 32; breaks off Huff N. Puff as he is hit (huff_n_puff.c EVS_TrySpawningRuffPuff) and has no turn
       of its own (EVS_TakeTurn does nothing: no moves here); it squeezes Mario in Huff's swarm and is inhaled to
       heal him (EVS_Attack_TuffPuffSwarm, EVS_Move_HealOrSlam). TuffPuff (75): 2 IdleSmall, 6 FlyFastSmall,
       12 HurtSmall (the large ones 3, 7, 13). Huff's hits break them off him (his .shed1).
       Here they have a turn: the swarm (EVS_Attack_TuffPuffSwarm), the first one to act taking every Tuff Puff
       at Mario, a squeeze of 1 each (.hits = BOSS_HITS_MATES, .gang), MSG_CH6_00CF the first
       time; the rest sit that round out. They fly (.fly 40) and shrug off shock. */
    {"Tuff Puff", 0x46, 0x4B, 75, -1, 1.0f, UNIT_UNUSED_MINI_BZZAP, 1, 0, 20, 24, 2, 6, 2, 12,
     /* (EVS_TuffPuff_SqueezePlayer: SOUND_SQUEEZE as they squeeze) */
     {{MV_RUSH, 1, 6, 1, 0, "SOUND_SQUEEZE|", .hits = BOSS_HITS_MATES, .gang = 1,
       .msg = PM64_MSG(0x11, 0xCF)}, {-1}},
     0, 0, 0, 0, {0}, 0, 0, 0, 0, 0, 0, 0, 0, .immune = IM_SHOCK, .fly = 40, .sounds = &kTuffSfx},
};

const Pm64BossSet kPm64Boss_huff_n_puff = {"huff_n_puff", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
