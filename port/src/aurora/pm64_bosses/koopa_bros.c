/* Paper Mario 64: the Koopa Bros. (pm64_boss.h).
 *
 * PM64 src/battle/area/trd_part_2: Formation_00 (area.c) is the four Ninjakoopas (koopa_bros_green.c,
 * koopa_bros_yellow.c, koopa_bros_black.c, koopa_bros_red.c, all common_koopa_bros.inc.c with their own anims and
 * status chances) and fake_bowser.c, which runs the whole fight: first the Bowser costume (map models of trd_00,
 * not here: this fight starts where EVS_KoopaBrosEnter leaves it), then EVS_KoopaBros_TakeTurn for all four.
 * Each Ninjakoopa: 5 HP, defence 1 (DefaultDefense), defence 0 while toppled (ToppledDefense), size 38 x 42,
 * flippable (ACTOR_EVENT_FLAG_FLIPABLE). They stand front to back Green (x 20), Yellow (60), Black (100), Red (140)
 * (EVS_KoopaBrosEnter's ForceHomePos), so unit 1 here is Green and the rest come in behind it.
 *
 * Their turn, all four together (fake_bowser.c EVS_KoopaBros_TakeTurn):
 *  1. A tipping tower steadies (Wait 30, BOSS_CMD_STABLE: the top one Idle, the rest IdleCrouch).
 *  2. Toppled ones try to get up (BOSS_CMD_TRY_GET_UP, common_koopa_bros.inc.c HandleCommand): a toppled one
 *     stays down its first turn (sweat 20 frames) and gets up the second (sweat 12 frames, a hop of 8 frames
 *     turning 4 x 22 degrees, back to Idle and defence 1); it fights again from the turn after.
 *  3. The tower: with no tower, every standing one builds one (EVS_TryFormingTower / EVS_BuildTowerWithKoopa:
 *     the front one points (PointForward), they run (Run), Leap, Midair, Land on each other; the first time the
 *     top one says MSG_CH1_010A "Have a taste of this!"); with a tower, standing ones join it
 *     (EVS_TryJoiningTower). One standing alone attacks alone (AFLAG_Boss_DoingSoloAttack).
 *  4. The Spin Attack (BOSS_CMD_SPIN_ATTACK), unless the tower is down: the first time the top one says
 *     MSG_CH1_0108 "The Koopa Bros. special: super spinning attack!". The top one TopEnterShell 10 frames,
 *     ShellSpin 60 frames (SOUND_KOOPA_BROS_TOWER_SPIN_1..3 by the tower's height), shoots off at speed 13..15
 *     and hits Mario for damage = the tower's height (2..4), spins back up onto the tower in 40 frames,
 *     TopExitShell, PointForward with a lens flare 30 frames; then each one below spins out (EnterShell 10,
 *     ShellSpin 60, speed 14..16) and back into its place (no damage), ExitShell, IdleCrouch.
 *     The Solo Attack (BOSS_CMD_SOLO_ATTACK): runs up, EnterShell 10, ShellSpin 30 (SOUND_KOOPA_BROS_SPINUP),
 *     speed 16 into Mario for 1 damage, jumps back, TopExitShell, PointForward, runs home.
 *
 * Hits (fake_bowser.c EVS_KoopaBros_HandleEvent): the tower takes no damage itself. One hit makes it tip
 * (IdleTipping; the top one says MSG_CH1_0109 "We're going over!!" once per tipping); a second hit the same
 * round, by the other of Mario and his partner, or one shock or blast hit, topples it (EVS_Broadcast_ToppleHit,
 * BOSS_CMD_TOPPLE_HIT: each falls off in 20 frames HurtStill, a shake as the top one lands, then IdleToppled for
 * 2 turns at defence 0 with its own HP bar and target). A Ninjakoopa standing on its own flips when jumped on
 * (common_koopa_bros.inc.c EVS_HandleEvent EVENT_FLIP_TRIGGER: Hurt, a flip over FlipPosOffsets 22 frames,
 * IdleToppled, toppled 2 turns at defence 0).
 *
 * KoopaBros (101): palettes 0 Black, 1 Red, 2 Yellow, 3 Green; rows 3 Run, 4 Idle, 5 IdleCrouch, 7 ShellSpin,
 * 8 TopEnterShell, 9 TopExitShell, 10 HurtStill, 11 Hurt, 12 IdleToppled, 13 IdleTipping, 16 Leap, 17 Midair,
 * 18 Land, 20 Talk, 24 Dizzy, 27 PointForward, 31 EnterShell, 32 ExitShell, 34 DizzyToppled.
 *
 * Here each Ninjakoopa is its own unit (.tower = 1) and the engine runs the tower
 * (pm64_bosses_aurora.c): with two or more standing, the first to act builds it (the others leap on, he on top)
 * and spins for its height (2..4; alone, the Solo Attack's 1); the ones below wait. The tower takes no damage:
 * a hit tips it (IdleTipping 13 on top, IdleCrouch 5 below), it steadies on the top one's next turn, a second
 * hit topples it: each falls off, IdleToppled (12) 2 turns at defence 0, then gets up with a hop. The Spin
 * Attack: ShellSpin (7) through its 60 frames, then off at Mario; TopExitShell (9) once it lands (.post).
 * A brother standing on his own (not in the tower, not on his back) flips onto his back when jumped on (a jump or
 * a quake: TTYD's shell-flipping weapons) and lies there 2 turns at defence 0, as a toppled one. One back on his
 * feet beside a standing tower leaps onto it (EVS_TryJoiningTower) and the tower spins for its new height; it
 * spins once a round, whoever is on top. Lines (KB_TOWER): MSG_CH1_0109 by the top one as the tower tips (once
 * per tipping), MSG_CH1_010A by the top one the first time it stands, pointing (PointForward 27: .talkRow). */
#include "pm64_boss.h"

/* the Spin Attack: PM64 BOSS_CMD_SOLO_ATTACK / BOSS_CMD_SPIN_ATTACK (common_koopa_bros.inc.c HandleCommand) */
/* (its sounds: SOUND_KOOPA_BROS_SPINUP as one alone spins up, BOSS_CMD_SOLO_ATTACK; the tower's spin-up by its
   height, SOUND_KOOPA_BROS_TOWER_SPIN_1..3, is the engine's; SOUND_KOOPA_LAUNCH_SHELL as he shoots off) */
#define KB_SPIN                                                                                                    \
    {MV_RUSH, 1, 7, 1, 0, "SFX_ENM_NOKO_SHELL1", .windup = 60, .post = 9, .wind = "SOUND_KOOPA_BROS_SPINUP",       \
     .go = "SOUND_KOOPA_LAUNCH_SHELL", .fx = FX_SHELL_FLARE}
/* the tower brothers' rows (KoopaBros 101): IdleTipping, IdleToppled, IdleCrouch, PointForward; their lines
 * (fake_bowser.c): MSG_CH1_0109 "We're going over!!", MSG_CH1_010A "Have a taste of this!" */
#define KB_TOWER .tower = 1, .tipRow = 13, .downRow = 12, .crouchRow = 5, .talkRow = 27,                            \
                 .tipMsg = PM64_MSG(0x0C, 0x109), .buildMsg = PM64_MSG(0x0C, 0x10A), .sounds = &kBrosSfx
/* their Paper Mario sounds (pm64_boss.h Pm64BossSounds): src/battle/actors.yaml Green/Yellow/Black/RedNinjakoopa
   (walkSound SOUND_ACTOR_STEP_A, _B; jumpSound SOUND_ACTOR_JUMP, also common_koopa_bros.inc.c HandleCommand's
   hop up) and fake_bowser.c EVS_BuildTowerWithKoopa (SOUND_KOOPA_BROS_LAND as each lands on the tower) */
static const Pm64BossSounds kBrosSfx = {.step = {"SOUND_ACTOR_STEP_A", "SOUND_ACTOR_STEP_B"},
                                        .jump = "SOUND_ACTOR_JUMP", .land = "SOUND_KOOPA_BROS_LAND"};

static const Pm64Boss kUnits[] = {
    /* 1: Green Ninjakoopa (koopa_bros_green.c; front, PM64's ACTOR_ENEMY0 and the tower's top when all stand):
       sleep 70, dizzy 45, paralyze 75. Speaks MSG_CH1_0108 before the first Spin Attack. */
    {"Koopa Bros.", 0x11, 0x11, 101, 3, 1.0f, UNIT_UNUSED_BELDAM2, 5, 1, 38, 42, 4, 3, 16, 11, {KB_SPIN, {-1}},
     0, 0, 0, 0, {2, 3, 4}, 0, 0, 0, 0, 0, 1, PM64_MSG(0x0C, 0x108), 0, KB_TOWER,
     .song = 0x08 /* SONG_KOOPA_BROS_BATTLE */},
    /* 2: Yellow Ninjakoopa (koopa_bros_yellow.c; ACTOR_ENEMY1): sleep 60, dizzy 75, paralyze 75 */
    {"Yellow Ninjakoopa", 0x10, 0x10, 101, 2, 1.0f, UNIT_UNUSED_ZESS_T, 5, 1, 38, 42, 4, 3, 16, 11, {KB_SPIN, {-1}},
     KB_TOWER},
    /* 3: Black Ninjakoopa (koopa_bros_black.c; ACTOR_ENEMY2): sleep 40, dizzy 30, paralyze 60 */
    {"Black Ninjakoopa", 0x0F, 0x0F, 101, 0, 1.0f, UNIT_UNUSED_REPORTER, 5, 1, 38, 42, 4, 3, 16, 11, {KB_SPIN, {-1}},
     KB_TOWER},
    /* 4: Red Ninjakoopa (koopa_bros_red.c; ACTOR_ENEMY3, at the back; the Bros.' leader): sleep 30, dizzy 60,
       paralyze 60 */
    {"Red Ninjakoopa", 0x0E, 0x0E, 101, 1, 1.0f, UNIT_UNUSED_HOTDOG_MASTER, 5, 1, 38, 42, 4, 3, 16, 11,
     {KB_SPIN, {-1}}, KB_TOWER},
};

const Pm64BossSet kPm64Boss_koopa_bros = {"koopa_bros", kUnits, (int)(sizeof(kUnits) / sizeof(kUnits[0])), 1};
