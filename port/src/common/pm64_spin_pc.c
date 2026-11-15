/* Paper Mario 64's Spin Dash in the field (port/include/port/pm64_spin.h, GitHub #51, docs/PM64_EXTRAS.md).
 *
 * Paper Mario's spin, re-made from what its decomp shows of it (pmret/papermario; read, never copied):
 * - who may start it (src/7E9D0.c check_input_spin): from standing, walking or running only, on the ground,
 *   not while a scene moves Mario, not with Slow Go worn, not while already spinning;
 * - its state (include/common_structs.h PlayerSpinState): a time at full speed, then slowing to a run
 *   (initialSpinTime, fullSpeedSpinTime, speedScale, frictionScale), a hit wall ending it (hitWallTime), its
 *   own sound (spinSoundID: include/enums.h SOUND_SPIN, SOUND_SPEEDY_SPIN, SOUND_SPIN_ATTACK,
 *   SOUND_SPEEDY_SPIN_ATTACK), which any other action stops (src/7E9D0.c set_action_state);
 * - its badges (src/encounter.c update_encounters_neutral and update_encounters_pre_battle,
 *   src/battle/btl_states_actions.c): with Spin Attack or Dizzy Attack worn, a spinning Mario touching an
 *   enemy starts the battle as his (no enemy first strike), Spin Attack beating the weaker ones outright and
 *   Dizzy Attack leaving the first enemy dizzy for 3 turns (EVS_ApplyDizzyAttack); a plain spin is a plain
 *   touch; Speedy Spin makes it faster and longer.
 * - the spin itself (src/world/action/spin.c action_update_spin), at Paper Mario's 30 frames a second, here
 *   at this game's 60 (each of its frames two of ours): 25 frames spinning (initialSpinTime), the first 16 at
 *   twice the run's speed (fullSpeedSpinTime 15, speedScale 2.0), then slowing by half the run's speed a frame
 *   (frictionScale 0.5) to a tenth of it; Speedy Spin 30 frames, 21 at 2.7 times the run, slowing by 0.9.
 *   His sprite turns 40 degrees a frame (spinRate) from the second frame on, one way as he goes right across the
 *   screen and the other as he goes left, and once the spin's time is up (or after 10 frames
 *   against a wall: hitWallTime) turns on until it faces left or right again, the way he went, and stops.
 *   He plays ANIM_Mario1_Spin meanwhile (ANIM_Mario1_SpeedySpin with Speedy Spin, ANIM_Mario1_SpinAttack
 *   with Spin Attack: the Spin Attack's over the Speedy Spin's), from its start, and is drawn shaded as he
 *   turns edge on, with an afterimage (src/77480.c appendGfx_player_spin, phys_get_spin_history).
 * Here, in TTYD's look, he holds a pose of his own of the same kind meanwhile (SPIN_POSE); in Mario's Paper Mario
 * 64 look (pm64_player_pc.c), PM64's own spin animations (portSpinDashLookAnim) and its shading and afterimage
 * (pm64_enemy_pc.c).
 * In this game it is the game's own machinery: the dash is marioSetForceMove's forced run (mot_dash's
 * footsteps, floors and falls), moved as a run is (marioMoveMain: walls, slopes, moving floors, a road exit's
 * push; a script's walk goes straight through them, and a spin through a map's edge or an exit's wall fell off
 * the map), its way the stick's as it starts and fixed after, as Paper Mario's
 * (no stick: where he stands), Mario's paper turning round and round meanwhile (his own facing,
 * MARIO_DISP_FLAG_DIR_LOCK as the Gale Force spin keeps it), the dust the game's (effKemuriEntry), the sound the ROM's (the "SOUND_X|SFX_Y" form of
 * port/include/port/pm64_sfx.h: the game's spin hammer whoosh without the setting). Any other move (a jump,
 * the hammer, the partner's, a paper form, a fall, a scene, an exit, the Start menu) ends it at once: still on
 * his feet, he walks or runs on as the stick asks (or stands); in another move's, at no more than a run's speed;
 * a script's own walk (an exit's walk-out) takes him as it would from a run. Spin Attack's first strike is the game's own, the spin hammer's (FBAT_HIT_SPIN_HAMMER: the battle's
 * spin hammer opening, its banner, Bump Attack and First Attack as with the hammer); Bump Attack already
 * beats the weaker enemies outright, as Spin Attack does in Paper Mario.
 *
 * Everything that changes play reads the game's state, its settings (online, the host's) and the players'
 * input; a co-op guest never spins the host's Mario (its own Luigi it does: brothers_pc.c). */
#include "port/pm64_spin.h"

#include "port/bindings.h"
#include "port/brothers.h"
#include "port/players.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/settings.h"
#include "port/statesync.h"
#include "port/toggles.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/notes.h"
#endif
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "eff/eff_kemuri.h"
#include "eff/n64/eff_whirlwind_n64.h"
#include "driver/effdrv.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"
#include "mot/mot_walk.h"
#include "party.h"
#include "pmario_sound.h"
#include "system.h"
#include "win/win_main.h"
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define COOLDOWN 12       /* frames after a spin before the next (Paper Mario: the spin's own end) */
#define WALL_FRAMES 20    /* frames held up (a wall) before the spin turns to its end (hitWallTime 10, at 30) */
#define DUST_EVERY 4      /* frames between its puffs of dust */
#define RUN_KEEP 1.0f     /* a move out of a spin keeps at most the run's speed (times this) */
#define DIZZY_TURNS 3     /* src/encounter.c: dizzyAttack.duration 3 */
#define DIZZY_WAIT 300    /* frames a Dizzy Attack's touch waits for its battle before it's forgotten */

/* Mario's pose while he spins, in TTYD's look (the PM64 look plays PM64's own: portSpinDashLookAnim) */
#define SPIN_POSE "M_J_1B"
#define SPIN_LONGEST 40 /* frames past the shape's a spin may take turning to its end (360 / its turn, and some) */

static struct {
    int on;          /* frames into the spin (0: none) */
    int ending;      /* its time was cut short (a wall): turning to its end */
    int still;       /* no stick as it started: it spins where he stands */
    void* ribbon;    /* the badges' ribbons (their effect, its work) */
    void* ribbonWork;
    float sign;      /* the way his paper turns */
    float facing[3]; /* his paper's facing, now and the 2 frames before (the PM64 look's afterimage) */
    int lookAnim;    /* the PM64 look's Mario1 animation */
    int poseLocked;  /* this set MARIO_DISP_FLAG_POSE_LOCK */
    const char* asked; /* the pose his moves asked for meanwhile (the last): his as the spin ends */
    PortSpinShape shape;
    float dir;       /* where it goes */
    float run;       /* the run's speed as it started */
    float lastX, lastZ;
    int held;        /* frames held up */
    int locked;      /* this set MARIO_DISP_FLAG_DIR_LOCK */
    int cool;        /* frames before the next */
    int bound;       /* the binding held last frame (its press is the edge) */
    int bound2;      /* the same, Luigi's */
    float speed;     /* the speed it gave him last */
    int mine;        /* the forced run is the spin's (no script's walk took over) */
    int starting;    /* (start's own marioSetForceMove) */
    int taken;       /* a forced move set since (a script's walk, an exit's walk-out): Mario is its */
    char map[16];    /* the map it started on */
    float fromX, fromZ; /* (the log) */
} sSpin;

static struct {
    int pending;      /* the next battle starts with the front enemy dizzy */
    unsigned at;      /* (gp->retraceCount when its touch came) */
} sDizzy;

static int* sSetting;

void marioGetStick(f32* dir, f32* mag); /* (mario_sbr.c: the stick's way and push, Mario's own reading) */

static int settingOn(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("pm64_spin", 0, 0, 1);
    }
    return sSetting != NULL && *sSetting != 0;
}

int portSpinDashOn(void) {
    if (!settingOn() || portConsoleAccurate() || portPm64Linked() == PM64_NONE) {
        return 0;
    }
    /* online: every game of the session has the ROM (the hello's pm64), as the badges and the partners */
    return !portStateSyncLinked() || portPm64PartnersActive();
}

static int worn(int badge) {
    return portPm64BadgeWorn(badge) > 0;
}

/* src/world/action/spin.c action_update_spin's numbers, at 30 frames a second, as this game's 60: its times
   doubled, its slowing and its turn a frame halved */
void portSpinDashShape(PortSpinShape* out) {
    if (worn(PORT_PM64_BADGE_SPEEDY_SPIN)) {
        out->frames = 2 * (30 - 1); /* initialSpinTime 30 (its last frame already turns to the end) */
        out->full = 2 * (20 + 1);   /* fullSpeedSpinTime 20 (curStateTime 0 .. 20 at full speed) */
        out->scale = 2.7f;          /* speedScale */
        out->friction = 0.9f / 2.0f; /* frictionScale */
    } else {
        out->frames = 2 * (25 - 1);
        out->full = 2 * (15 + 1);
        out->scale = 2.0f;
        out->friction = 0.5f / 2.0f;
    }
    out->rate = 40.0f / 2.0f; /* spinRate (both) */
}

float portSpinDashSpeed(const PortSpinShape* s, int t, float run) {
    float k;
    if (t < s->full) {
        return run * s->scale;
    }
    /* speedScale - (curStateTime - fullSpeedSpinTime - 1) * frictionScale, at least 0.1 of the run */
    k = s->scale - (float)(t - s->full) * s->friction;
    return run * (k < 0.1f ? 0.1f : k);
}

float portSpinDashTurnSign(float dir, float camYaw) {
    /* (spin.c: spinRate as targetYaw less the camera's yaw is at most 180, the right of the screen; else
       -spinRate. This game's paper faces right at 180 and left at 0, as PM64's sprite does) */
    return revise360(dir - camYaw) <= 180.0f ? 1.0f : -1.0f;
}

int portSpinDashTurn(const PortSpinShape* s, int t, int ending, float sign, float* facing) {
    const float was = revise360(*facing);
    float now;
    if (t < 2) {
        return 0; /* (spin.c: turning from curStateTime 2) */
    }
    now = was + sign * s->rate;
    if (!ending && t < s->frames) {
        *facing = revise360(now);
        return 0;
    }
    /* (SUBSTATE_SPIN_1: on until it faces the way it went, 180 turning one way, 0 the other) */
    if (sign > 0.0f && was < 180.0f && now >= 180.0f) {
        *facing = 180.0f;
        return 1;
    }
    if (sign < 0.0f && now <= 0.0f && was < 90.0f) {
        *facing = 0.0f;
        return 1;
    }
    *facing = revise360(now);
    return 0;
}

/* the game's L as each player's Spin Dash: its press, or (player 1's, while the paper notes are on, whose button it
   is too: port/include/port/notes.h) a tap of it, let go within L_TAP frames, never at a note nor with the notes'
   quick wheel or a note open (a hold of it there is the quick wheel's) */
#define L_TAP 20
static struct {
    int down;   /* held last frame */
    int frames; /* frames held */
    int spent;  /* this hold spins no more (it did, or it's the notes') */
} sL[2];

static int notesHaveL(int player) {
#ifdef TTYD_BACKEND_AURORA
    return player == 0 && portNotesOn();
#else
    (void)player;
    return 0;
#endif
}

static int notesUseL(int player) {
#ifdef TTYD_BACKEND_AURORA
    return player == 0 && portNoteNear();
#else
    (void)player;
    return 0;
#endif
}

/* the port whose touch Spin Dash button is held (-1 none): that player's binding as well */
static int sTouchHeld = -1;

void portSpinDashTouchHeld(int port) {
    sTouchHeld = port;
}

int portSpinDashPressed(int player, int l) {
    int held = 0, press, fromL = 0;
    int* last = player == 0 ? &sSpin.bound : &sSpin.bound2;
    const int p = player == 0 ? 0 : 1;
#ifdef TTYD_BACKEND_AURORA
    held = portBindingsHeldOn(PORT_BIND_SPIN_DASH, player);
#endif
    held = held || (sTouchHeld >= 0 && sTouchHeld == player);
    press = held && !*last;
    *last = held;
    if (l) {
        if (!sL[p].down) {
            sL[p].frames = 0;
            sL[p].spent = notesUseL(player);
            if (!sL[p].spent && !notesHaveL(player)) {
                fromL = 1; /* (no notes: on its press) */
                sL[p].spent = 1;
            }
        } else {
            sL[p].frames++;
            if (notesUseL(player)) {
                sL[p].spent = 1;
            }
        }
    } else if (sL[p].down) {
        fromL = !sL[p].spent && sL[p].frames < L_TAP && !notesUseL(player);
    }
    sL[p].down = l != 0;
    return press || fromL;
}

void portSpinDashStartFx(float x, float y, float z) {
    const int speedy = worn(PORT_PM64_BADGE_SPEEDY_SPIN), attack = worn(PORT_PM64_BADGE_SPIN_ATTACK);
    Vec pos;
    pos.x = x;
    pos.y = y;
    pos.z = z;
    psndSFXOn_3D(speedy && attack ? "SOUND_SPEEDY_SPIN_ATTACK|SFX_MARIO_HAMMER_KAITEN2"
                 : attack         ? "SOUND_SPIN_ATTACK|SFX_MARIO_HAMMER_KAITEN2"
                 : speedy         ? "SOUND_SPEEDY_SPIN|SFX_MARIO_HAMMER_KAITEN1"
                                  : "SOUND_SPIN|SFX_MARIO_HAMMER_KAITEN1",
                 &pos);
    effKemuriEntry(0, x, y, z, 0.0f);
}

void portSpinDashDust(float x, float y, float z, float dir) {
    effKemuriEntry(0, x, y, z, dir);
}

/* The badges' ribbons round the spinner (spin.c: fx_effect_46, src/effects/effect_46.c): the game's own copy of
   that effect, eff_whirlwind_n64 (its four bands, white, red and blue). Spin Attack's red (type 2), else Dizzy
   Attack's blue (4), else Speedy Spin's white (0); one more turning the other way; none without them. Its end
   (set_action_state): ten frames left to fade. */
void* portSpinDashRibbonStart(void* target, float sign) {
    int type;
    EffectEntry* fx;
    if (worn(PORT_PM64_BADGE_SPIN_ATTACK)) {
        type = 2;
    } else if (worn(PORT_PM64_BADGE_DIZZY_ATTACK)) {
        type = 4;
    } else if (worn(PORT_PM64_BADGE_SPEEDY_SPIN)) {
        type = 0;
    } else {
        return NULL;
    }
    fx = effWhirlwindN64Entry(type + (sign < 0.0f ? 1 : 0), (MarioWork*)target, 1.0f, 1000);
    return fx;
}

/* (the entry still the ribbons it was: the effect pool's slots outlive their effects) */
static EffWhirlwindN64Work* ribbonWork(void* fx, void* work) {
    EffectEntry* e = (EffectEntry*)fx;
    if (e == NULL || !e->flags || e->type == NULL || strcmp(e->type, "WhirlwindN64") != 0 ||
        (work != NULL && e->userdata != work)) {
        return NULL;
    }
    return (EffWhirlwindN64Work*)e->userdata;
}

void portSpinDashRibbonFollow(void* fx, void* work, float x, float y, float z) {
    EffWhirlwindN64Work* w = ribbonWork(fx, work);
    if (w != NULL) {
        w->position.x = x;
        w->position.y = y;
        w->position.z = z;
    }
}

void* portSpinDashRibbonWork(void* fx) {
    return ribbonWork(fx, NULL);
}

void portSpinDashRibbonEnd(void* fx, void* work) {
    EffWhirlwindN64Work* w = ribbonWork(fx, work);
    if (w != NULL && w->life > 10) {
        w->life = 10;
    }
}

/* --- Mario's */

static int logOn(void) {
    return 1; /* (a line as a spin starts and ends: rare, and what the tests read) */
}

/* standing, walking or running on the ground, his keys and controls his, nothing open */
static int canStart(MarioWork* mario) {
    const WindowWork* win = winGetPtr();
    if (mario->characterId != 0 || gp->inBattle || gp->fbatData == NULL || gp->fbatData->mode != 1 ||
        gp->mapEventId || marioStGetSystemLevel() != 0) {
        return 0;
    }
    if (mario->currMotionId != MARIO_MOTION_STAY && mario->currMotionId != MARIO_MOTION_WALK &&
        mario->currMotionId != MARIO_MOTION_DASH) {
        return 0;
    }
    if (mario->hitobjStandOn == NULL || (mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) ||
        mario->keyOffCount > 0 || !marioChkKey() || !marioChkCtrl() ||
        marioKeyOffChk() || marioCtrlOffChk() || (win != NULL && win->menuState != 0)) {
        return 0;
    }
    /* (a script's walk; a paper form; carrying Bobbery; Slow Go, as check_input_spin's ABILITY_SLOW_GO) */
    if ((mario->flags & (MARIO_FLAG_FORCED_MOVEMENT | MARIO_FLAG_PAPER_MODE | MARIO_FLAG_TUBE_MODE |
                         MARIO_FLAG_BG_MODE | MARIO_FLAG_IS_CARRYING_BOBBERY)) ||
        pouchEquipCheckBadge(ITEM_SLOW_GO)) {
        return 0;
    }
    return 1;
}

/* the spin over (why: its end, a wall, or what cut it short): Mario handed back as a run or a walk would be */
static void stop(MarioWork* mario, const char* why) {
    const float dx = mario->position.x - sSpin.fromX, dz = mario->position.z - sSpin.fromZ;
    const char* then = "";
    if (sSpin.on == 0) {
        return;
    }
    if (sSpin.mine) {
        if (mario->flags & MARIO_FLAG_FORCED_MOVEMENT) {
            mario->flags &= ~MARIO_FLAG_FORCED_MOVEMENT;
            mario->forceMoveTimer = 0;
        }
        if ((mario->currMotionId == MARIO_MOTION_WALK || mario->currMotionId == MARIO_MOTION_DASH) &&
            !(mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING))) {
            /* still on his feet in the spin's run, however it ended (its end, a wall, a scene, the Start menu, the
               setting, the forced run cleared by another): straight back to the walk or the run the stick asks
               for, as Paper Mario's spin ends (and as the game's own forced run ends: marioForceMoveMain); the
               walk and the run set their speed only as they start, so the spin's speed (up to twice the run's,
               down to a tenth of it) would last until the stick was let go. The stick read afresh (a forced
               run's controlStickSensitivity is its speed), the run past mot_dash's radius; with the keys off
               (an exit, a scene) it is let go a frame on, and he stops as a run does */
            const int x = mario->wStickDir1, y = mario->wStickDir2;
            float angle, mag;
            marioGetStick(&angle, &mag);
            mario->baseSpeed = 0.0f;
            if (mag > 0.0f && x * x + y * y > 0xBD1) {
                mario->baseSpeed = marioGetDashSpd();
                then = ", then runs";
            } else if (mag > 0.0f) {
                mario->baseSpeed = marioGetWalkSpd();
                then = ", then walks";
            } else {
                then = ", then stands";
            }
        } else if (mario->baseSpeed > sSpin.run * RUN_KEEP) {
            /* (a jump, the hammer, the partner's, a fall: their own motion, at no more than a run's speed) */
            mario->baseSpeed = sSpin.run * RUN_KEEP;
        }
    }
    if (sSpin.poseLocked) {
        sSpin.poseLocked = 0;
        mario->dispFlags &= ~MARIO_DISP_FLAG_POSE_LOCK;
        /* the pose his moves asked for meanwhile (the run's, a jump's or the hammer's that ended it), at once:
           no frame of the spin's pose after it */
        if (sSpin.asked != NULL) {
            marioChgPose(sSpin.asked);
        }
        sSpin.asked = NULL;
    }
    if (sSpin.locked) {
        sSpin.locked = 0;
        if (mario->currMotionId != MARIO_MOTION_DOKAN) { /* (a pipe's walk keeps the lock it set itself) */
            mario->dispFlags &= ~MARIO_DISP_FLAG_DIR_LOCK;
            /* his paper back to facing left or right, the nearer: the game's turn from there */
            mario->displayDirection = revise360(mario->displayDirection);
            mario->displayDirection = mario->displayDirection > 90.0f && mario->displayDirection <= 270.0f ? 180.0f : 0.0f;
        }
    }
    if (logOn()) {
        printf("[spin] Mario's spin ends (%s): %.0f units in %d frames, at (%.0f, %.0f, %.0f)%s\n", why,
               sqrtf(dx * dx + dz * dz), sSpin.on, mario->position.x, mario->position.y, mario->position.z, then);
        fflush(stdout);
    }
    sSpin.on = 0;
    sSpin.ending = 0;
    sSpin.mine = 0;
    sSpin.taken = 0;
    sSpin.cool = COOLDOWN;
    portSpinDashRibbonEnd(sSpin.ribbon, sSpin.ribbonWork);
    sSpin.ribbon = sSpin.ribbonWork = NULL;
}

static void start(MarioWork* mario) {
    portSpinDashShape(&sSpin.shape);
    sSpin.run = marioGetDashSpd();
    if (sSpin.run <= 0.0f) {
        sSpin.run = mario->baseDashSpeed > 0.0f ? mario->baseDashSpeed : 2.25f;
    }
    /* spin.c: the way his paper turns by the way he was going (targetYaw before the spin's), then the stick's
       way as it starts, fixed (player_input_to_move_vector: no stick, the way he was going); no stick, no
       speed at all (inputMagnitude 0: speedModifier 0): a spin where he stands */
    sSpin.sign = portSpinDashTurnSign(mario->moveDirection, mario->targetYaw);
    sSpin.still = !(mario->controlStickSensitivity > 0.0f);
    sSpin.dir = sSpin.still ? mario->moveDirection : mario->controlStickAngle;
    sSpin.on = 1;
    sSpin.ending = 0;
    sSpin.held = 0;
    /* (spin.c: SpinAttack over SpeedySpin; Dizzy Attack alone the plain Spin) */
    sSpin.lookAnim = worn(PORT_PM64_BADGE_SPIN_ATTACK)   ? PM64_MARIO1_SPIN_ATTACK
                     : worn(PORT_PM64_BADGE_SPEEDY_SPIN) ? PM64_MARIO1_SPEEDY_SPIN
                                                         : PM64_MARIO1_SPIN;
    sSpin.lastX = sSpin.fromX = mario->position.x;
    sSpin.lastZ = sSpin.fromZ = mario->position.z;
    /* (the forced run outlasts the spin by a little: the spin's own count ends it, stop) */
    sSpin.speed = portSpinDashSpeed(&sSpin.shape, 0, sSpin.run);
    sSpin.starting = 1;
    marioSetForceMove(sSpin.dir, sSpin.shape.frames + SPIN_LONGEST + 2, sSpin.speed);
    sSpin.starting = 0;
    sSpin.taken = 0;
    snprintf(sSpin.map, sizeof(sSpin.map), "%s", gp->currentMapName);
    if (sSpin.still) {
        /* (the forced run's motion, its pose held, at no speed: he stays where he is) */
        sSpin.run = sSpin.speed = 0.0f;
        mario->baseSpeed = 0.0f;
    }
    sSpin.mine = 1;
    if (!(mario->dispFlags & MARIO_DISP_FLAG_DIR_LOCK)) {
        mario->dispFlags |= MARIO_DISP_FLAG_DIR_LOCK;
        sSpin.locked = 1;
    }
    sSpin.facing[0] = sSpin.facing[1] = sSpin.facing[2] = revise360(mario->displayDirection);
    /* his spin's pose from its first frame, held (the dash's motion would set its run's each frame) */
    if (!(mario->dispFlags & MARIO_DISP_FLAG_POSE_LOCK)) {
        sSpin.asked = mario->animName;
        marioChgPose(SPIN_POSE);
        mario->dispFlags |= MARIO_DISP_FLAG_POSE_LOCK;
        sSpin.poseLocked = 1;
    }
    portSpinDashStartFx(mario->position.x, mario->position.y, mario->position.z);
    sSpin.ribbon = portSpinDashRibbonStart(mario, sSpin.sign);
    sSpin.ribbonWork = portSpinDashRibbonWork(sSpin.ribbon);
    if (logOn()) {
        printf("[spin] Mario spins at (%.0f, %.0f, %.0f) toward %.0f: %d frames, %.2f a frame (the run's %.2f)%s%s%s%s\n",
               mario->position.x, mario->position.y, mario->position.z, sSpin.dir, sSpin.shape.frames,
               sSpin.run * sSpin.shape.scale, sSpin.run, worn(PORT_PM64_BADGE_SPEEDY_SPIN) ? ", Speedy Spin" : "",
               worn(PORT_PM64_BADGE_SPIN_ATTACK) ? ", Spin Attack" : "",
               worn(PORT_PM64_BADGE_DIZZY_ATTACK) ? ", Dizzy Attack" : "", sSpin.still ? ", in place" : "");
        fflush(stdout);
    }
}

/* the spin under way: 0 once it's over */
static int under(MarioWork* mario) {
    const char* why = NULL;
    float moved, want;
    if (sSpin.taken) {
        /* a script's walk (marioSetForceMove: an exit's walk-out, an event's) took him over: its, not the spin's,
           to end (told by the call itself: a wall, a slope or a floor can change the speed he goes at) */
        sSpin.mine = 0;
        stop(mario, "a script's walk");
        return 0;
    }
    if (strncmp(sSpin.map, gp->currentMapName, sizeof(sSpin.map)) != 0) {
        /* (another map: Mario is the new one's, his pose its; what the spin set is let go, nothing more) */
        sSpin.asked = NULL;
        why = "a map change";
    } else if (!(mario->flags & MARIO_FLAG_FORCED_MOVEMENT) || mario->currMotionId != MARIO_MOTION_DASH) {
        why = "another move";
    } else if (gp->inBattle || gp->fbatData == NULL || gp->fbatData->mode != 1 || gp->mapEventId ||
               marioStGetSystemLevel() != 0 || mario->keyOffCount > 0 || marioKeyOffChk() || marioCtrlOffChk()) {
        why = "a scene or a battle";
    } else if (mario->flags & (MARIO_FLAG_IS_JUMPING | MARIO_FLAG_IS_FALLING)) {
        why = "off the ground";
    } else if (!portSpinDashOn()) {
        why = "the setting off";
    }
    if (why == NULL) {
        moved = hypotf(mario->position.x - sSpin.lastX, mario->position.z - sSpin.lastZ);
        want = portSpinDashSpeed(&sSpin.shape, sSpin.on - 1, sSpin.run);
        sSpin.held = moved < 0.25f * want ? sSpin.held + 1 : 0;
        if (sSpin.held >= WALL_FRAMES) {
            sSpin.ending = 1; /* (a wall: it turns to its end now) */
        }
        if (sSpin.on > sSpin.shape.frames + SPIN_LONGEST) {
            why = "its end"; /* (never: it turns to its end well before) */
        }
    }
    if (why != NULL) {
        stop(mario, why);
        return 0;
    }
    sSpin.lastX = mario->position.x;
    sSpin.lastZ = mario->position.z;
    /* (its way fixed, as spin.c's targetYaw: the stick no longer steers it) */
    mario->baseSpeed = sSpin.speed = portSpinDashSpeed(&sSpin.shape, sSpin.on, sSpin.run);
    mario->moveDirection = mario->directionView = sSpin.dir;
    /* his paper spinning at Paper Mario's turn, then on to face the way he went: there the spin ends */
    {
        float facing = mario->displayDirection;
        const int done = portSpinDashTurn(&sSpin.shape, sSpin.on, sSpin.ending, sSpin.sign, &facing);
        if (sSpin.locked) {
            mario->displayDirection = mario->displayDirectionTarget = facing;
        }
        sSpin.facing[2] = sSpin.facing[1];
        sSpin.facing[1] = sSpin.facing[0];
        sSpin.facing[0] = facing;
        if (done) {
            stop(mario, sSpin.ending ? "a wall" : "its end");
            return 0;
        }
    }
    if (sSpin.on % DUST_EVERY == 0) {
        portSpinDashDust(mario->position.x, mario->position.y, mario->position.z, -sSpin.dir);
    }
    sSpin.on++;
    return 1;
}

void portSpinDashFrame(void) {
    MarioWork* mario = marioGetPtr();
    int l, pressed;
    if (gp == NULL || mario == NULL) {
        return;
    }
    if (sDizzy.pending && !gp->inBattle && gp->fbatData != NULL && gp->fbatData->mode == 1 &&
        gp->retraceCount - sDizzy.at > DIZZY_WAIT) {
        sDizzy.pending = 0; /* (its touch started no battle) */
    }
    /* (read every frame, so a binding held through a spin is no press after it) */
    l = (keyGetButton(0) & PAD_TRIGGER_L) != 0;
    pressed = portSpinDashPressed(0, l);
    if (sSpin.on > 0 && under(mario)) {
        return;
    }
    if (sSpin.cool > 0) {
        sSpin.cool--;
    }
    if (!pressed || sSpin.cool > 0 || !portSpinDashOn() || portStateSyncCoopGuest() || !canStart(mario)) {
        return;
    }
    start(mario);
}

int portSpinDashMoving(void) {
    const MarioWork* mario = marioGetPtr();
    /* (never a script's walk set since, which must go its way, through walls as ever) */
    return sSpin.on > 0 && sSpin.mine && !sSpin.taken && mario != NULL &&
           (mario->flags & MARIO_FLAG_FORCED_MOVEMENT) && mario->currMotionId == MARIO_MOTION_DASH;
}

void portSpinDashForceMoveSet(void) {
    if (sSpin.on > 0 && !sSpin.starting) {
        sSpin.taken = 1;
    }
}

void portSpinDashPoseAsked(const char* name) {
    if (sSpin.poseLocked && name != NULL) {
        sSpin.asked = name;
    }
}

const char* portSpinDashPoseName(void) {
    return SPIN_POSE;
}

int portSpinDashLookAnim(void) {
    return sSpin.on > 0 ? sSpin.lookAnim : -1;
}

int portSpinDashLookFacing(float* now, float* before) {
    if (sSpin.on <= 0 || !sSpin.locked) {
        return 0;
    }
    *now = sSpin.facing[0];
    *before = sSpin.facing[2];
    return 1;
}

/* --- the touch: Spin Attack's first strike, Dizzy Attack's dizzy */

NpcEntry* portSpinDashHit(NpcEntry* found, void* kindp) {
    u32* kind = (u32*)kindp;
    const PartyEntry* luigi = NULL;
    MarioWork* mario = marioGetPtr();
    int attack, dizzy, byLuigi;
    if (found == NULL || kind == NULL || mario == NULL || gp->fbatData == NULL || gp->fbatData->mode != 1 ||
        !(found->flags & NPC_FLAG_ENEMY) || (*kind != FBAT_HIT_CONTACT && *kind != FBAT_HIT_ENEMY_FIRST) ||
        !portSpinDashOn()) {
        return found;
    }
    byLuigi = portCoopTouchedBy(found);
    if (byLuigi) {
        /* (another player's character touched it: a spin only if it's Luigi's, spinning, at it) */
        luigi = portBrothersSpinning();
        if (luigi == NULL || hypotf(luigi->position.x - found->position.x, luigi->position.z - found->position.z) >
                                 0.5f * ((luigi->width > 0.0f ? luigi->width : 24.0f) + found->width) + 8.0f) {
            return found;
        }
    } else if (sSpin.on == 0) {
        return found;
    }
    attack = worn(PORT_PM64_BADGE_SPIN_ATTACK);
    dizzy = worn(PORT_PM64_BADGE_DIZZY_ATTACK);
    if (!attack && !dizzy) {
        return found; /* (a plain spin is a plain touch, as in Paper Mario) */
    }
    if (attack) {
        /* the spin hammer's first strike (no hammer yet: a stomp's) */
        *kind = pouchGetPtr()->hammerLevel > 0 ? FBAT_HIT_SPIN_HAMMER : FBAT_HIT_JUMP;
        if (byLuigi) {
            portCoopTouchForget(); /* (Luigi's spin counts as Mario's, as his hammer does) */
        }
    } else {
        *kind = FBAT_HIT_CONTACT; /* (Dizzy Attack alone: Mario's touch, never the enemy's first strike) */
    }
    fbatSetAttackAnnounce((s32)*kind);
    if (dizzy) {
        sDizzy.pending = 1;
        sDizzy.at = gp->retraceCount;
    }
    printf("[spin] %s's spin hits '%s': %s%s\n", byLuigi ? "Luigi" : "Mario", found->description,
           attack ? "Spin Attack, a first strike" : "a touch", dizzy ? ", Dizzy Attack's dizzy" : "");
    fflush(stdout);
    return found;
}

void portSpinDashBattleStart(BattleWork* wp) {
    BattleUnit* best = NULL;
    int i;
    if (!sDizzy.pending || wp == NULL) {
        return;
    }
    sDizzy.pending = 0;
    for (i = 0; i < BATTLE_UNIT_MAX; i++) {
        BattleUnit* u = BattleGetUnitPtr(wp, i);
        if (u == NULL || u->alliance != 1 || u->currentHp <= 0 ||
            (u->attributeFlags & UNIT_ATTR_NOT_SAMPLED_AS_TARGET)) {
            continue;
        }
        if (best == NULL || u->homePosition.x < best->homePosition.x) {
            best = u; /* (the front: nearest Mario's side) */
        }
    }
    if (best == NULL) {
        return;
    }
    if (best->kindParams != NULL && best->kindParams->statusVulnerability != NULL &&
        best->kindParams->statusVulnerability->dizzy == 0) {
        printf("[spin] Dizzy Attack: the front enemy (unit %d) can't be made dizzy\n", best->unitId);
        fflush(stdout);
        return;
    }
    BtlUnit_SetStatus(best, STATUS_DIZZY, DIZZY_TURNS, 1);
    printf("[spin] Dizzy Attack: the front enemy (unit %d) dizzy for %d turns\n", best->unitId, DIZZY_TURNS);
    fflush(stdout);
}
