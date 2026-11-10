/* Bowser's fire breath while he is the Play as pick (port/include/port/fire_breath.h,
 * docs/PLAY_AS.md "Fire breath").
 *
 * The field: his hammer swing (mot_hammer) is drawn and heard as a breath of
 * fire. Its motion runs exactly as Mario's, so every hammer check the game
 * makes (the swing's hit sphere on enemies, hitEstimate's block, switch and
 * wall test, hitobjHammer for the map objects and scripts, the remake's
 * backswing) is the hammer's; only the pose (his disc's KPA_A_1A..D, fitted
 * to the swing's 20 frames: inhale, lunge, breathe, close), the flame (the
 * game's gonbaba_breath of type 7, which his own stages give him, koopa.c)
 * and the sounds (his roar and the flame's, as koopa.c plays them) are his.
 *
 * Battle: "Fire Breath" in his Hammer list and his first strike from the
 * field's breath: one script, fire_breath_event, after the shape of Mario's
 * hammer (marioAttackEvent_NormalHammer_Core: the hold-left command, the
 * Stylish, the walk back) with the breath of Bowser's own fight
 * (unit_boss_koopa.c fire_attack_event: inhale, roar, flame, every target in
 * turn). The pose is his (the stand-in plays portFireBreathPose's name while
 * Mario's own pose stands underneath), so it is draw only; the damage,
 * elements and statuses are the game's own rules on the weapons below.
 *
 * Everything that changes play reads the game's state and input only; the
 * stand-in's pose and the sounds are draw and sound only. */
#include "port/fire_breath.h"

#include "battle/battle.h"
#include "battle/battle_ac.h"
#include "battle/battle_camera.h"
#include "battle/battle_damage.h"
#include "battle/battle_event_cmd.h"
#include "battle/battle_event_default.h"
#include "battle/battle_mario.h"
#include "battle/battle_seq_command.h"
#include "battle/battle_sub.h"
#include "battle/battle_unit.h"
#include "battle/battle_weapon_power.h"
#include "data/item_data.h"
#include "driver/camdrv.h"
#include "driver/effdrv.h"
#include "driver/msgdrv.h"
#include "eff/eff_gonbaba_breath.h"
#include "evt/evt_audience.h"
#include "evt/evt_cmd.h"
#include "evt/evt_snd.h"
#include "manager/evtmgr.h"
#include "manager/evtmgr_cmd.h"
#include "mario/mario.h"
#include "mario/mario_pouch.h"
#include "mario/mario_sbr.h"
#include "pmario_sound.h"
#include "port/luigi.h"
#include "port/versus.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/tr.h"
#endif
#include "system.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* battle_mario.c: Merlee's charm before an attack */
extern EvtWord marioAttackEvent_MajinaiPowerUpCheck[];

int portFireBreathOn(void) {
    return portPlayAs() == PORT_PLAY_AS_BOWSER && !portVersusOn();
}

/* ------------------------------------------------------------- the field */

/* Faces the breath (yaw 0 left, 180 right) and lets it out as one jet over
 * `frames`: the effect lets a puff out every 8 frames (his own, type 7) or 4
 * (the dragons'), made for a breath held for seconds, so within the field's
 * swing or the battle move only two or three would show. `fast`: a third
 * faster (the field's short breath reaches its length in time). Draw only. */
static void aimBreath(EffectEntry* e, f32 yaw, int frames, int fast) {
    EffGonbabaBreathWork* w;
    int i;
    if (e == NULL) {
        return;
    }
    w = (EffGonbabaBreathWork*)e->userdata;
    w->yaw = yaw;
    if (w->child != NULL) {
        ((EffGonbabaBreathWork*)w->child->userdata)->yaw = yaw;
    }
    for (i = 1; i < e->count; i++) {
        w[i].delay = (i - 1) * frames / e->count;
        if (fast) {
            w[i].velX *= 1.35f;
        }
    }
}

/* The flame by the hammer's rank, as his own stages step his breath up
 * (koopa.c: his normal flame, then at his top level the dragons' breath of
 * type 0, bigger): the wooden hammer his own orange flame (type 7), the Super
 * Hammer Bonetail's blue flame (type 3, the dragons' fuller stream), the Ultra
 * Hammer Gloomtail's white-hot Mega Breath (type 2), half again as big. Draw
 * only. */
static int flameType(f32* size) {
    switch (pouchGetHammerLv()) {
        case 3:
            *size *= 1.5f;
            return 2;
        case 2:
            *size *= 1.2f;
            return 3;
        default:
            return 7;
    }
}

/* the frame of the swing the flame starts on (the hammer's head comes down on
 * frame 10, where the swing's hit lands) and the one its sound ends on */
#define FIELD_FLAME_AT 8
#define FIELD_SOUND_OFF 16
/* the flame at his mouth (Bowser is drawn at 0.7 of his size: his stages'
 * 40 ahead and 26 up, koopa.c kpaFireEntry2, scaled, then fitted to his
 * KPA_A_1C mouth in play); the size his stages give the Super level's flame
 * (1.0 at his full size: a short jet needs it to read) */
#define FIELD_AHEAD 28.0f
#define FIELD_UP 24.0f
#define FIELD_SIZE 1.0f
/* frames at full strength (then it fades over 32, eff_gonbaba_breath.c) */
#define FIELD_LIFE (32 + 8)
/* toward the camera when the swing strikes something solid (a big block is
 * about 50 deep, his snout in it to the middle) */
#define FIELD_CLEAR 30.0f

static s32 sFieldSfx = -1;

void portFireBreathFieldStart(void) {
    portFireBreathFieldStop();
}

void portFireBreathFieldFrame(int t) {
    MarioWork* mario = marioGetPtr();
    if (!portFireBreathOn()) {
        return;
    }
    if (t == FIELD_FLAME_AT) {
        const int right = mario->displayDirectionTarget >= 90.0f && mario->displayDirectionTarget <= 270.0f;
        const f32 off = mario->dispFlags & MARIO_DISP_FLAG_REAR_POSE ? -6.0f : 0.0f;
        f32 x = mario->position.x, z = mario->position.z;
        f32 size = FIELD_SIZE;
        const int type = flameType(&size);
        EffectEntry* e;
        movePos(&x, &z, FIELD_AHEAD, toMovedir2(mario->displayDirectionTarget, off));
        if (mario->hitobjHammerNew != NULL) {
            /* breathing on a block, a switch or a wall right before him: his mouth is in it, and so was
             * the flame (drawn inside the block, unseen); out in front of it, toward the camera */
            const f32 yaw = camGetPtr(CAMERA_3D)->viewYaw * 0.017453292f;
            x -= FIELD_CLEAR * sinf(yaw);
            z += FIELD_CLEAR * cosf(yaw);
        }
        e = effGonbabaBreathEntry(type, FIELD_LIFE, x, mario->position.y + FIELD_UP, z, size);
        aimBreath(e, right ? 180.0f : 0.0f, 16, 1);
        printf("[firebreath] field: flame %d (hammer %d)%s\n", type, (int)pouchGetHammerLv(),
               mario->hitobjHammerNew != NULL ? " in front of what it strikes" : "");
        fflush(stdout);
        portFireBreathFieldStop();
        psndSFXOn_3D("SFX_VOICE_KOOPA_FIRE1", &mario->position);
        sFieldSfx = psndSFXOn_3D("SFX_KUPPA_FIRE_BREATH1", &mario->position);
    } else if (t >= FIELD_SOUND_OFF) {
        portFireBreathFieldStop();
    }
}

void portFireBreathFieldStop(void) {
    if (sFieldSfx != -1) {
        psndSFXOff(sFieldSfx);
        sFieldSfx = -1;
    }
}

/* ---------------------------------------------------------- the stand-in */

/* the pose the battle script asks for ("" none) */
static char sBattlePose[16];

/* his disc's breath, fitted to the field swing's 21 frames (0..20): the
 * inhale (6 frames), the lunge (8, at twice its speed), the breath (its
 * 12-frame loop, from where the flame comes) and closing his mouth (6, at
 * twice its speed) */
static const char* fieldPose(int t, float* frame) {
    t = t < 0 ? 0 : t > 20 ? 20 : t;
    if (t < 6) {
        *frame = (float)t;
        return "KPA_A_1A";
    }
    if (t < 10) {
        *frame = (float)(t - 6) * 2.0f;
        return "KPA_A_1B";
    }
    if (t < 18) {
        *frame = (float)(t - 10);
        return "KPA_A_1C";
    }
    *frame = (float)(t - 18) * 2.0f;
    return "KPA_A_1D";
}

static const char* battleSwingPose(const char* mario, float* frame);
static void swingReset(void);
static void standReset(void);
static void throwReset(void);

const char* portFireBreathPose(int where, const char* mario, float* frame) {
    *frame = -1.0f;
    if (!portFireBreathOn()) {
        return NULL;
    }
    if (where == PORT_LUIGI_FIELD) {
        MarioWork* mw = marioGetPtr();
        if (mw == NULL || mw->currMotionId != MARIO_MOTION_HAMMER) {
            portFireBreathFieldStop(); /* (the swing was cut short: its sound with it) */
            return NULL;
        }
        return fieldPose((int)mw->multiTimer, frame);
    }
    if (where == PORT_LUIGI_BATTLE) {
        if (sBattlePose[0] != '\0') {
            return sBattlePose;
        }
        return battleSwingPose(mario, frame);
    }
    return NULL;
}

/* ----------------------------------------------------------------- text */

static const char* tr(const char* english) {
#ifdef TTYD_BACKEND_AURORA
    return portTrGame(english);
#else
    return english; /* the headless build has no translations */
#endif
}

const char* portFireBreathMsg(const char* tag) {
    if (tag == NULL || strncmp(tag, "pc_fire_breath", 14) != 0) {
        return NULL;
    }
    if (strcmp(tag, "pc_fire_breath") == 0) {
        return tr("Fire Breath");
    }
    if (strcmp(tag, "pc_fire_breath_desc") == 0) {
        return tr("Breathe fire on all ground\nenemies, and maybe burn them.");
    }
    return NULL;
}

/* --------------------------------------------------------------- battle */

static BattleWeapon sWeapon;
static BattleWeapon sFirstWeapon;

#define WEAPON_SLOTS ((int)(sizeof(((BattleCommand*)0)->weapon) / sizeof(((BattleCommand*)0)->weapon[0])))

int portFireBreathInHammerList(void) {
    BattleWork* wp = _battleWorkPointer;
    if (wp == NULL || !portFireBreathOn() || pouchGetPtr()->hammerLevel < 1) {
        return 0;
    }
    /* (not in the hammer's lesson: its menu is the lesson's own) */
    return !((wp->flags & (BATTLE_FLAG_LECTURE_HAMMER | BATTLE_FLAG_LECTURE_MENU)) && wp->lectureUnitId != -1);
}

void portFireBreathAddWeapon(BattleCommand* cmd, int* num) {
    BattleCommandWeapon* e;
    if (cmd == NULL || num == NULL || *num >= WEAPON_SLOTS || !portFireBreathInHammerList()) {
        return;
    }
    e = &cmd->weapon[*num];
    e->index = -1;
    e->itemId = 0;
    e->weapon = &sWeapon;
    e->enabled = 0;
    e->disableReason = 0;
    e->icon = itemDataTable[ITEM_FIRE_FLOWER].icon_id; /* (the game's fire: the Fire Flower) */
    e->name = msgSearch(sWeapon.name);
    (*num)++;
}

BattleWeapon* portFireBreathFirstAttack(void) {
    return portFireBreathOn() ? &sFirstWeapon : NULL;
}

void portFireBreathBattleStart(void) {
    portFireBreathFieldStop();
    sBattlePose[0] = '\0';
    swingReset();
    standReset();
    throwReset();
    portPlayAsThrownBattleStart(); /* (Peach's thrown parasol: last battle's pose id is gone) */
}

/* fbPose(name or 0): the stand-in's pose from now (0: Mario's again) */
static USER_FUNC(fbPose) {
    const char* name = (const char*)evtGetValue(event, event->args[0]);
    (void)isFirstCall;
    snprintf(sBattlePose, sizeof(sBattlePose), "%s", name != NULL ? name : "");
    return EVT_RETURN_DONE;
}

/* the battle flame: ahead of and up from his feet, its size, at his scale
 * (0.7), as the boss's (60 ahead, 22.5 up, 3.5 big, unit_boss_koopa.c) */
#define BATTLE_AHEAD 42.0f
#define BATTLE_UP 16.0f
#define BATTLE_SIZE 2.45f
/* (it fades over 32 more frames: out as he closes his mouth, KPA_A_1D) */
#define BATTLE_LIFE 45

/* the flame at u's mouth (mg its status size; drawn dx along x from where u
 * is), the way it faces, lasting `life` frames at full strength and let out
 * over `frames`; its type */
static int battleFlame(BattleUnit* u, f32 mg, f32 dx, int life, int frames) {
    EffectEntry* e;
    f32 dir, size = BATTLE_SIZE * mg;
    const int type = flameType(&size); /* (the hammer's rank: the field's colours) */
    dir = u->faceDirection < 0 ? -1.0f : 1.0f;
    e = effGonbabaBreathEntry(type, life, u->position.x + dx + dir * BATTLE_AHEAD * mg, u->position.y + BATTLE_UP * mg,
                              u->position.z - 5.0f, size);
    aimBreath(e, dir > 0.0f ? 180.0f : 0.0f, frames, 0);
    return type;
}

/* fbFlame(unit, status size): the flame at the unit's mouth, the way it faces */
static USER_FUNC(fbFlame) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = BattleGetUnitPtr(wp, BattleTransID(event, (s32)evtGetValue(event, event->args[0])));
    int type;
    (void)isFirstCall;
    if (u == NULL) {
        return EVT_RETURN_DONE;
    }
    type = battleFlame(u, evtGetFloat(event, event->args[1]), 0.0f, BATTLE_LIFE, 24);
    printf("[firebreath] battle: flame %d (hammer %d)\n", type, (int)pouchGetHammerLv());
    fflush(stdout);
    return EVT_RETURN_DONE;
}

/* ------------------------------------------- his battle hammer, as fire */

/* Mario's battle hammer poses (battle_mario.c): the wind-up and the raise
 * while the command is held (marioAttackEvent_NormalHammer_Core's LW(0),
 * LW(1) by hammer rank; marioAttackEvent_KaitenHammer's whirl, LW(0)), and
 * the swing that strikes (NormalHammer_Core's LW(2), Piercing Blow's M_H_13..15;
 * KaitenHammer's LW(1)) */
static const char* const kSwingInhale[] = {"M_H_1A", "M_H_1B", "M_H_5A", "M_H_5B", "M_H_9A",
                                           "M_H_9B", "M_H_2A", "M_H_6A", "M_H_10A"};
static const char* const kSwingBreathe[] = {"M_H_1C", "M_H_5C", "M_H_9C", "M_H_13", "M_H_14",
                                            "M_H_15", "M_H_2B", "M_H_6B", "M_H_10B"};

/* the Quake Hammers swing these too (into the ground: his stomp, not a breath) */
extern EvtWord marioAttackEvent_JishinHammer[];
extern EvtWord marioAttackEvent_UltraJishinHammer[];

/* the swing's flame: its life (under 32 it is already fading from the start,
 * eff_gonbaba_breath.c: so 2 frames at full strength, then 32 fading, a
 * shorter jet than Fire Breath's for a move that strikes one) and over how
 * many frames it is let out */
#define SWING_LIFE 34
#define SWING_SPREAD 10
/* his lunge (KPA_A_1B at twice its speed: 4 frames), the breath's 12-frame
 * loop until it closes, his mouth closing (KPA_A_1D at twice its speed) */
#define SWING_LUNGE 4
#define SWING_CLOSE (SWING_LUNGE + 30)

static struct {
    int phase; /* 0 none, 1 inhale, 2 breath */
    int t;     /* frames in the breath */
    int flame; /* its flame is out */
    s32 sfx;   /* the flame's sound */
} sSwing = {0, 0, 0, -1};

static int inList(const char* name, const char* const* list, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (!strcmp(name, list[i])) {
            return 1;
        }
    }
    return 0;
}

static void swingSoundOff(void) {
    if (sSwing.sfx != -1) {
        psndSFXOff(sSwing.sfx);
        sSwing.sfx = -1;
    }
}

/* a battle begins: no swing going on (its sound went with the last battle's) */
static void swingReset(void) {
    sSwing.phase = 0;
    sSwing.sfx = -1;
}

int portFireBreathMuteSfx(int id) {
    static int ids[3];
    static int ready;
    int i;
    if (sSwing.phase != 2 || !portFireBreathOn()) {
        return 0;
    }
    if (!ready) {
        ids[0] = searchPSSFXList("SFX_MARIO_HAMMER_HIT_GROUND1");
        ids[1] = searchPSSFXList("SFX_MARIO_HAMMER_HIT_GROUND2");
        ids[2] = searchPSSFXList("SFX_MARIO_HAMMER_HIT_GROUND3");
        ready = 1;
    }
    for (i = 0; i < 3; i++) {
        if (ids[i] != 0 && (id & 0x1FFF) == (ids[i] & 0x1FFF)) {
            return 1;
        }
    }
    return 0;
}

/* his swing is a hammer move of the swinging kind (not a Quake Hammer) */
static int swingIsBreath(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleWeapon* w = wp != NULL ? BattleGetSelectWeapon(wp) : NULL;
    if (w == NULL) {
        return 1; /* (a first strike: the hammer's own) */
    }
    return w->attackEvtScript != marioAttackEvent_JishinHammer && w->attackEvtScript != marioAttackEvent_UltraJishinHammer;
}

/* ------------------------------ his thrown hammer and quakes, as his own */

/* battle_mario.c: Hammer Throw's, and Fire Drive's (the Quake Hammers' are above) */
extern EvtWord marioAttackEvent_HammerNageru[];
extern EvtWord marioAttackEvent_FireNaguri[];

/* the attack script Mario's unit runs now (NULL: none) */
static const EvtWord* runningMove(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = wp != NULL ? BattleGetMarioPtr(wp) : NULL;
    if (u == NULL || u->attackEventId == 0 || !evtCheckID(u->attackEventId)) {
        return NULL;
    }
    return u->attackEventCode;
}

/* a move that brings the hammer down on the ground (the Quake Hammers, Fire Drive): his stomp */
static int isStomp(const EvtWord* code) {
    return code != NULL && (code == marioAttackEvent_JishinHammer || code == marioAttackEvent_UltraJishinHammer ||
                            code == marioAttackEvent_FireNaguri);
}

/* Mario's poses in them (battle_mario.c): the wind-up, the raise held while the command runs, and the
 * swing into the ground (the Quake Hammers' M_H_13..15 by rank, Fire Drive's M_H_17C..19C) */
static const char* const kStompCrouch[] = {"M_H_1A", "M_H_5A", "M_H_9A", "M_H_17A", "M_H_18A", "M_H_19A"};
static const char* const kStompUp[] = {"M_H_1B", "M_H_5B", "M_H_9B", "M_H_17B", "M_H_18B", "M_H_19B"};
static const char* const kStompDown[] = {"M_H_13", "M_H_14", "M_H_15", "M_H_17C", "M_H_18C", "M_H_19C"};
/* Hammer Throw's throw (M_H_3, 7, 11 by rank) */
static const char* const kThrow[] = {"M_H_3", "M_H_7", "M_H_11"};

/* how high he leaps (drawn: his unit stays on the ground, so the quake's timing and hits are the
 * hammer's), over how many frames up, and down onto the ground as the hammer would strike it */
#define STOMP_HIGH 42.0f
#define STOMP_RISE 18
#define STOMP_FALL 8
/* his hip drop held after he lands, then he stands */
#define STOMP_SQUAT 24

static struct {
    int phase; /* 0 none, 1 the leap, 2 up there, 3 the drop */
    int t;
    f32 lift; /* drawn this high (y) */
    f32 from; /* where the drop began */
} sStomp;

static struct {
    int on; /* his throw's spit is going on */
    int t;
} sThrow;

#define COUNT_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* A ground move's stomp: he leaps up as Mario winds up (KPA_J_1A), hangs there while the command runs
 * (KPA_J_1B, a slow bob), and drops onto the ground in his hip drop (KPA_A_2D, the one his own stages'
 * ground pound uses, evt_koopa_hip_attack) as the hammer would come down, landing as the quake starts.
 * Draw and sound only. */
static const char* stompPose(const char* mario, float* frame) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = wp != NULL ? BattleGetMarioPtr(wp) : NULL;
    f32 f;
    if (inList(mario, kStompCrouch, COUNT_OF(kStompCrouch))) {
        if (sStomp.phase != 1) {
            sStomp.phase = 1;
            sStomp.t = 0;
            if (u != NULL) {
                psndSFXOn_3D("SFX_KUPPA_JUMP1", &u->position);
            }
            printf("[firebreath] quake: leap (%s)\n", mario);
            fflush(stdout);
        } else {
            sStomp.t++;
        }
        f = sStomp.t >= STOMP_RISE ? 1.0f : (f32)sStomp.t / STOMP_RISE;
        sStomp.lift = STOMP_HIGH * (1.0f - (1.0f - f) * (1.0f - f));
        return "KPA_J_1A";
    }
    if (inList(mario, kStompUp, COUNT_OF(kStompUp))) {
        if (sStomp.phase != 2) {
            sStomp.phase = 2;
            sStomp.t = 0;
        } else {
            sStomp.t++;
        }
        /* (up from wherever the leap got to, then a slow bob) */
        if (sStomp.lift < STOMP_HIGH - 3.0f) {
            sStomp.lift += 3.0f;
        } else {
            sStomp.lift = STOMP_HIGH + 2.0f * sinf((f32)sStomp.t * 0.15f);
        }
        return "KPA_J_1B";
    }
    if (inList(mario, kStompDown, COUNT_OF(kStompDown))) {
        if (sStomp.phase != 3) {
            sStomp.phase = 3;
            sStomp.t = 0;
            sStomp.from = sStomp.lift;
            printf("[firebreath] quake: stomp (%s, from %d up)\n", mario, (int)sStomp.from);
            fflush(stdout);
        } else {
            sStomp.t++;
        }
        f = sStomp.t >= STOMP_FALL ? 1.0f : (f32)sStomp.t / STOMP_FALL;
        sStomp.lift = sStomp.from * (1.0f - f * f);
        if (sStomp.t == STOMP_FALL && u != NULL) {
            psndSFXOn_3D("SFX_KUPPA_LANDING1", &u->position);
        }
        return sStomp.t < STOMP_FALL + STOMP_SQUAT ? "KPA_A_2D" : "KPA_S_1";
    }
    sStomp.phase = 0;
    sStomp.lift = 0.0f;
    (void)frame;
    return NULL;
}

/* Hammer Throw's throw: he spits a fireball (his lunge, a breath, his mouth closing) where Mario lets the
 * hammer go; the fireball is the thrown hammer's (portFireBreathThrown). Draw and sound only. */
static const char* throwPose(const char* mario, float* frame) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = wp != NULL ? BattleGetMarioPtr(wp) : NULL;
    if (!inList(mario, kThrow, COUNT_OF(kThrow))) {
        sThrow.on = 0;
        return NULL;
    }
    if (!sThrow.on) {
        sThrow.on = 1;
        sThrow.t = 0;
        if (u != NULL) {
            psndSFXOn_3D("SFX_VOICE_KOOPA_FIRE1", &u->position);
        }
        printf("[firebreath] hammer throw: spit (%s)\n", mario);
        fflush(stdout);
    } else {
        sThrow.t++;
    }
    if (sThrow.t < SWING_LUNGE) {
        *frame = (float)sThrow.t * 2.0f;
        return "KPA_A_1B";
    }
    if (sThrow.t < SWING_LUNGE + 12) {
        *frame = (float)(sThrow.t - SWING_LUNGE);
        return "KPA_A_1C";
    }
    if (sThrow.t < SWING_LUNGE + 30) {
        *frame = (float)((sThrow.t - SWING_LUNGE - 12) * 2 > 6 ? 6 : (sThrow.t - SWING_LUNGE - 12) * 2);
        return "KPA_A_1D";
    }
    return "KPA_S_1";
}

f32 portFireBreathLift(void) {
    return portFireBreathOn() && sStomp.phase != 0 && isStomp(runningMove()) ? sStomp.lift : 0.0f;
}

/* The thrown fireball: the breath's own flame (the hammer's rank's colours, flameType), its origin
 * moved each frame onto the thrown hammer, its stream turned back along the way it flies so it trails
 * behind like a comet. Draw only: the hammer part flies, hits and is hidden as the badge's script has it. */
#define BALL_SIZE 1.5f
/* each puff's frames (the breath's 64 would make a long stream), and its speed back along the trail */
#define BALL_PUFF_LIFE 26
#define BALL_TRAIL 0.6f

static struct {
    EffectEntry* e;
    void* work; /* its userdata when made (it is still ours while this matches) */
    f32 x;
    int type;
} sBall;

/* a battle begins: no stomp, spit or fireball going on (the last battle's effects are gone) */
static void throwReset(void) {
    sStomp.phase = 0;
    sStomp.lift = 0.0f;
    sThrow.on = 0;
    sBall.e = NULL;
}

static int ballValid(void) {
    return sBall.e != NULL && (sBall.e->flags & EFFECT_FLAG_IN_USE) && sBall.e->userdata == sBall.work &&
           sBall.e->type != NULL && strcmp(sBall.e->type, "GonbabaBreath") == 0;
}

static void ballMove(EffectEntry* e, f32 x, f32 y, f32 z, f32 yaw) {
    EffGonbabaBreathWork* w = (EffGonbabaBreathWork*)e->userdata;
    int i;
    w->position.x = x;
    w->position.y = y;
    w->position.z = z;
    w->yaw = yaw;
    for (i = 1; i < e->count; i++) {
        /* (a puff just made again: short-lived and slow, so the trail stays near the ball) */
        if (w[i].life > BALL_PUFF_LIFE) {
            w[i].life = BALL_PUFF_LIFE;
            w[i].velX *= BALL_TRAIL;
        }
    }
    if (w->child != NULL && (w->child->flags & EFFECT_FLAG_IN_USE)) {
        EffGonbabaBreathWork* c = (EffGonbabaBreathWork*)w->child->userdata;
        c->position.x = x;
        c->position.y = y;
        c->position.z = z;
        c->yaw = yaw;
    }
}

void portFireBreathThrown(int shown, f32 x, f32 y, f32 z) {
    if (!portFireBreathOn() || runningMove() != marioAttackEvent_HammerNageru) {
        shown = 0;
    }
    if (!ballValid()) {
        sBall.e = NULL;
    }
    if (!shown) {
        if (sBall.e != NULL) {
            /* (it struck, or missed and fell: it burns out where it is) */
            effSoftDelete(sBall.e);
            printf("[firebreath] hammer throw: fireball out at %d %d\n", (int)x, (int)y);
            fflush(stdout);
            sBall.e = NULL;
        }
        return;
    }
    if (sBall.e == NULL) {
        f32 size = BALL_SIZE;
        EffGonbabaBreathWork* w;
        int i;
        sBall.type = flameType(&size);
        sBall.e = effGonbabaBreathEntry(sBall.type, 0, x, y, z, size);
        if (sBall.e == NULL) {
            return;
        }
        sBall.work = sBall.e->userdata;
        sBall.x = x;
        w = (EffGonbabaBreathWork*)sBall.e->userdata;
        for (i = 1; i < sBall.e->count; i++) {
            w[i].delay = (i - 1) * 3 / 2 % BALL_PUFF_LIFE; /* (a steady stream from the start) */
        }
        printf("[firebreath] hammer throw: fireball %d (hammer %d)\n", sBall.type, (int)pouchGetHammerLv());
        fflush(stdout);
    }
    {
        /* the trail goes back the way it flies (yaw 0: the stream to the left, as the breath faces) */
        static f32 yaw;
        if (x > sBall.x + 0.25f) {
            yaw = 0.0f;
        } else if (x < sBall.x - 0.25f) {
            yaw = 180.0f;
        }
        sBall.x = x;
        ballMove(sBall.e, x, y, z, yaw);
    }
}

/* Called once a frame for Mario's battle pose (luigi_pc.c): Bowser's pose in
 * place of the hammer's, the flame as the swing begins. Draw and sound only. */
static const char* battleSwingPose(const char* mario, float* frame) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u;
    const EvtWord* move;
    if (mario == NULL || wp == NULL) {
        sSwing.phase = 0;
        sStomp.phase = 0;
        sThrow.on = 0;
        return NULL;
    }
    move = runningMove();
    if (isStomp(move)) {
        return stompPose(mario, frame);
    }
    sStomp.phase = 0;
    sStomp.lift = 0.0f;
    if (move == marioAttackEvent_HammerNageru) {
        const char* spit = throwPose(mario, frame);
        if (spit != NULL) {
            return spit;
        }
    } else {
        sThrow.on = 0;
    }
    if (inList(mario, kSwingInhale, (int)(sizeof(kSwingInhale) / sizeof(kSwingInhale[0])))) {
        if (sSwing.phase != 1) {
            sSwing.phase = swingIsBreath() ? 1 : -1;
        }
        return sSwing.phase == 1 ? "KPA_A_1A" : NULL; /* (on at Mario's rate, as Fire Breath's) */
    }
    if (!inList(mario, kSwingBreathe, (int)(sizeof(kSwingBreathe) / sizeof(kSwingBreathe[0])))) {
        if (sSwing.phase == 2) {
            swingSoundOff();
        }
        sSwing.phase = 0;
        return NULL;
    }
    if (sSwing.phase == 0) {
        sSwing.phase = swingIsBreath() ? 1 : -1; /* (a first strike's swing comes with no wind-up of its own) */
    }
    if (sSwing.phase < 0) {
        return NULL;
    }
    if (sSwing.phase == 1) {
        sSwing.phase = 2;
        sSwing.t = 0;
        sSwing.flame = 0;
    } else {
        sSwing.t++;
    }
    if (sSwing.t >= SWING_LUNGE && !sSwing.flame && (u = BattleGetMarioPtr(wp)) != NULL) {
        /* (from his mouth where he is drawn: stood off, portFireBreathStandOff) */
        const int type = battleFlame(u, u->sizeMultiplier, portFireBreathStandOff(), SWING_LIFE, SWING_SPREAD);
        sSwing.flame = 1;
        printf("[firebreath] battle hammer %s: flame %d (hammer %d)\n", mario, type, (int)pouchGetHammerLv());
        fflush(stdout);
        swingSoundOff();
        psndSFXOn_3D("SFX_VOICE_KOOPA_FIRE1", &u->position);
        sSwing.sfx = psndSFXOn_3D("SFX_BOSS_KOOPA_FIRE_BREATH1", &u->position);
    }
    if (sSwing.t < SWING_LUNGE) {
        *frame = (float)sSwing.t * 2.0f;
        return "KPA_A_1B";
    }
    if (sSwing.t < SWING_CLOSE) {
        *frame = (float)((sSwing.t - SWING_LUNGE) % 12);
        return "KPA_A_1C";
    }
    swingSoundOff();
    *frame = (float)((sSwing.t - SWING_CLOSE) * 2 > 6 ? 6 : (sSwing.t - SWING_CLOSE) * 2);
    return "KPA_A_1D";
}

/* ------------------------------------------ his stand-off for the breath */

/* battle_mario.c: the plain Hammer's (its script, marioAttackEvent_NormalHammer, is Head Rattle's too) */
extern BattleWeapon marioDefaultWeapon_Hammer;

/* How far a breath stands from the one it strikes: Fire Breath's 80 from its
 * hit spot (fire_breath_event), where Mario's hammer walks up to 35
 * (marioAttackEvent_NormalHammer_Core) or 30 (the whirl,
 * marioAttackEvent_KaitenHammer), his snout in the enemy. */
#define STAND_BREATH 80.0f
#define STAND_HAMMER 35.0f
#define STAND_WHIRL 30.0f

/* the hammer move scripts he breathes on (not the Quake Hammers', the thrown
 * hammer's or Fire Drive's, nor Fire Breath's own, which stands back itself);
 * how much further back the breath stands (0: none) */
static f32 standOffFor(const EvtWord* code) {
    if (code == NULL) {
        return 0.0f;
    }
    if (code == marioWeapon_KaitenHammer.attackEvtScript || code == marioWeapon_UltraHammer.attackEvtScript ||
        code == marioDefaultWeapon_FirstAttackKaitenHammer.attackEvtScript) {
        return STAND_BREATH - STAND_WHIRL;
    }
    if (code == marioDefaultWeapon_Hammer.attackEvtScript || code == badgeWeapon_GatsunHammer.attackEvtScript ||
        code == badgeWeapon_DokanHammer.attackEvtScript || code == badgeWeapon_IceNaguri.attackEvtScript ||
        code == badgeWeapon_TsuranukiNaguri.attackEvtScript || code == badgeWeapon_ConfuseHammer.attackEvtScript) {
        return STAND_BREATH - STAND_HAMMER;
    }
    return 0.0f;
}

static struct {
    s32 id;     /* the attack event it is for (0 none) */
    int have;   /* his walk up has begun */
    Vec stale;  /* his move target as the move began (his last move's) */
    Vec before; /* where he stood the frame before */
    Vec from;   /* where the walk up began */
    Vec spot;   /* the hammer's spot, before the one struck */
    f32 off;    /* how much further back he is drawn there */
    f32 shift;  /* the drawn shift now (x) */
} sStand;

/* a battle begins: no stand-off (its event ids start over) */
static void standReset(void) {
    sStand.id = 0;
    sStand.shift = 0.0f;
}

static f32 distXZ(const Vec* a, const Vec* b) {
    const f32 dx = a->x - b->x, dz = a->z - b->z;
    return sqrtf(dx * dx + dz * dz);
}

f32 portFireBreathStandOff(void) {
    BattleWork* wp = _battleWorkPointer;
    BattleUnit* u = wp != NULL && portFireBreathOn() ? BattleGetMarioPtr(wp) : NULL;
    f32 off, f, span;
    const Vec* to;
    if (u == NULL || u->attackEventId == 0 || !evtCheckID(u->attackEventId) ||
        (off = standOffFor(u->attackEventCode)) <= 0.0f) {
        sStand.id = 0;
        sStand.shift = 0.0f;
        return 0.0f;
    }
    if (sStand.id != u->attackEventId) {
        sStand.id = u->attackEventId;
        sStand.have = 0;
        sStand.stale = u->moveTargetPos;
        sStand.before = u->position;
    }
    if (!sStand.have) {
        if (distXZ(&u->moveTargetPos, &sStand.stale) < 1.0f) {
            sStand.before = u->position;
            sStand.shift = 0.0f;
            return 0.0f;
        }
        /* his walk up to the hammer's spot began (MovePosition set its target) */
        sStand.have = 1;
        sStand.from = sStand.before;
        sStand.spot = u->moveTargetPos;
        sStand.off = off;
        printf("[firebreath] battle hammer stands off %d further (the breath's %d)\n", (int)off, (int)STAND_BREATH);
        fflush(stdout);
    }
    /* eased in over the walk up and out over the walk back (to wherever he goes from the spot): so far
     * along the way from the walk's other end, of the whole */
    to = distXZ(&u->moveTargetPos, &sStand.spot) < 1.0f ? &sStand.from : &u->moveTargetPos;
    span = distXZ(&sStand.spot, to);
    f = span > 1.0f ? distXZ(&u->position, to) / span : 1.0f;
    f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    /* back the way he came (home is behind him) */
    sStand.shift = (sStand.from.x < sStand.spot.x ? -1.0f : 1.0f) * sStand.off * f;
    return sStand.shift;
}

/* Fire Breath, and the first strike (battle flag 2, its one target): LW(12)
 * the weapon, LW(3)/LW(4) the target, LW(7)/LW(8) the first one, LW(9) the
 * command's result, LW(10) 1 on a first strike, LW(15) the flame's sound. */
static EVT_BEGIN(fire_breath_event)
    SET_VALUE(LW(10), 0)
    CALL(btlevtcmd_check_battleflag, LW(0), 2)
    IF_INT_NOT_EQUAL(LW(0), 0)
        // A first strike: he comes in from his home spot's left at the one struck.
        SET_VALUE(LW(10), 1)
        SET_VALUE(LW(12), PTR(&sFirstWeapon))
        CALL(btlevtcmd_GetFirstAttackTarget, LW(3), LW(4))
        IF_INT_EQUAL(LW(3), -1)
            GOTO(99)
        END_IF
        CALL(evt_btl_camera_set_mode, 0, 7)
        CALL(evt_btl_camera_set_homing_unit, 0, LW(3), BTL_UNIT_NONE)
        CALL(evt_btl_camera_set_zoom, 0, 300)
        CALL(evt_btl_camera_set_moveSpeedLv, 0, 2)
        CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        CALL(btlevtcmd_FaceDirectionSub, BTL_UNIT_SELF, LW(0), 250)
        CALL(btlevtcmd_SetPos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
        WAIT_FRAMES(30)
    ELSE
        CALL(btlevtcmd_CommandGetWeaponAddress, BTL_UNIT_SELF, LW(12))
        CALL(btlevtcmd_GetSelectEnemy, LW(3), LW(4))
        IF_INT_EQUAL(LW(3), -1)
            GOTO(99)
        END_IF
        CALL(btlevtcmd_AttackDeclareAll, BTL_UNIT_SELF)
        CALL(btlevtcmd_WaitGuardMove)
        CALL(btlevtcmd_PayWeaponCost, BTL_UNIT_SELF, LW(12))
        CALL(btlevtcmd_RunDataEventChild, BTL_UNIT_SELF, 7)
        CALL_CHILD(marioAttackEvent_MajinaiPowerUpCheck)
        CALL(evt_btl_camera_set_mode, 0, 11)
        CALL(evt_btl_camera_set_homing_unitparts, 0, BTL_UNIT_SELF, 1, LW(3), LW(4))
        CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
        CALL(evt_btl_camera_set_zoom, 0, 300)
    END_IF
    SET_VALUE(LW(7), LW(3))
    SET_VALUE(LW(8), LW(4))
    // The hammer's command: hold the stick left while he draws breath, let go as the last light comes on.
    CALL(btlevtcmd_GetWeaponActionLv, LW(12), LW(0))
    CALL(btlevtcmd_AcSetDifficulty, BTL_UNIT_SELF, LW(0))
    CALL(btlevtcmd_AcSetParamAll, 4, 30, 0, 0, 0, EVT_NULLPTR, EVT_NULLPTR, EVT_NULLPTR)
    CALL(btlevtcmd_AcSetFlag, 0)
    CALL(btlevtcmd_SetupAC, BTL_UNIT_SELF, 3, 1, 0)
    // He runs up to a breath's length from the front target.
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_RUN)
    CALL(btlevtcmd_GetHitPos, LW(3), LW(4), LW(0), LW(1), LW(2))
    CALL(btlevtcmd_GetPos, BTL_UNIT_SELF, EVT_NULLPTR, LW(1), EVT_NULLPTR)
    CALL(btlevtcmd_FaceDirectionAdd, LW(3), LW(0), 80)
    ADD(LW(2), 5)
    IF_INT_EQUAL(LW(10), 1)
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(5.0f))
    ELSE
        CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(4.0f))
    END_IF
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(0), LW(2), 1, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    CALL(btlevtcmd_CalculateFaceDirection, BTL_UNIT_SELF, -1, LW(3), LW(4), 16, LW(15))
    CALL(btlevtcmd_ChangeFaceDirection, BTL_UNIT_SELF, LW(15))
    // He draws in his breath while the command runs.
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("M_S_1"))
    CALL(fbPose, STRING("KPA_A_1A"))
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(evt_btl_camera_set_zoom, 0, 350)
    WAIT_FRAMES(20)
    CALL(btlevtcmd_StartAC, 0)
    CALL(btlevtcmd_ResultAC)
    CALL(btlevtcmd_GetResultAC, LW(9))
    // He lunges with a roar and breathes a stream of fire.
    CALL(fbPose, STRING("KPA_A_1B"))
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_VOICE_KOOPA_FIRE1"), EVT_NULLPTR, 0, EVT_NULLPTR)
    CALL(btlevtcmd_snd_se, BTL_UNIT_SELF, STRING("SFX_BOSS_KOOPA_FIRE_BREATH1"), EVT_NULLPTR, 0, LW(15))
    WAIT_FRAMES(8)
    CALL(btlevtcmd_GetStatusMg, BTL_UNIT_SELF, LW(5))
    CALL(fbFlame, BTL_UNIT_SELF, LW(5))
    CALL(fbPose, STRING("KPA_A_1C"))
    CALL(evt_btl_camera_set_mode, 0, 7)
    CALL(evt_btl_camera_set_homing_unit, 0, BTL_UNIT_SELF, LW(3))
    CALL(evt_btl_camera_set_zoom, 0, 250)
    WAIT_FRAMES(8)
    // The flame reaches each target in turn: more damage after a good command; the ones it misses dodge.
    LABEL(10)
    CALL(btlevtcmd_PreCheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(6))
    IF_INT_EQUAL(LW(6), 1)
        IF_BITS_SET(LW(9), 2)
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT | DAMAGE_FLAG_AC_SUCCESS, LW(5))
        ELSE
            CALL(btlevtcmd_CheckDamage, BTL_UNIT_SELF, LW(3), LW(4), LW(12), DAMAGE_FLAG_LAST_HIT, LW(5))
        END_IF
    ELSE
        IF_INT_EQUAL(LW(6), 3)
            CALL(btlevtcmd_StartAvoid, LW(3), 38)
        END_IF
        IF_INT_EQUAL(LW(6), 6)
            CALL(btlevtcmd_StartAvoid, LW(3), 39)
        END_IF
        IF_INT_EQUAL(LW(6), 2)
            CALL(btlevtcmd_StartAvoid, LW(3), 40)
        END_IF
    END_IF
    IF_INT_EQUAL(LW(10), 0)
        CALL(btlevtcmd_GetSelectNextEnemy, LW(3), LW(4))
        IF_INT_NOT_EQUAL(LW(3), -1)
            WAIT_FRAMES(5)
            GOTO(10)
        END_IF
    END_IF
    // The command's rating over the first target, as the hammer's.
    IF_BITS_SET(LW(9), 2)
        CALL(btlevtcmd_GetResultPrizeLv, LW(7), 0, LW(6))
        CALL(btlevtcmd_GetHitPos, LW(7), LW(8), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_ACSuccessEffect, LW(6), LW(0), LW(1), LW(2))
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), LW(6))
    ELSE
        CALL(btlevtcmd_AudienceDeclareACResult, LW(12), -1)
    END_IF
    CALL(btlevtcmd_StopAC)
    CALL(btlevtcmd_WeaponAftereffect, LW(12))
    // A Stylish window while the fire still pours: a good press and he spins and pumps his fist.
    CALL(btlevtcmd_ACRStart, BTL_UNIT_SELF, 0, 15, 15, 0)
    CALL(btlevtcmd_ACRGetResult, LW(6), LW(13))
    WAIT_FRAMES(20)
    CALL(fbPose, STRING("KPA_A_1D"))
    CALL(evt_snd_sfxoff, LW(15))
    WAIT_FRAMES(10)
    CALL(fbPose, 0)
    IF_INT_GREATER_OR_EQUAL(LW(6), 2)
        CALL(evt_btl_camera_set_mode, 0, 0)
        CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
        LOOP(20)
            CALL(btlevtcmd_AddRotate, BTL_UNIT_SELF, 0, 36, 0)
            WAIT_FRAMES(1)
        END_LOOP
        CALL(btlevtcmd_SetRotate, BTL_UNIT_SELF, 0, 0, 0)
        CALL(btlevtcmd_snd_voice, BTL_UNIT_SELF, 1)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("M_V_2"))
        CALL(btlevtcmd_AudienceDeclareAcrobatResult, LW(12), 1, 0, 0, 0)
        WAIT_MS(500)
    ELSE
        CALL(evt_audience_acrobat_notry)
        CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("M_S_1"))
        WAIT_FRAMES(20)
    END_IF
    // He walks back to his home spot.
    CALL(evt_audience_ap_recovery)
    CALL(btlevtcmd_InviteApInfoReport)
    CALL(evt_btl_camera_set_mode, 0, 0)
    CALL(evt_btl_camera_set_moveSpeedLv, 0, 1)
    CALL(btlevtcmd_AnimeChangePose, BTL_UNIT_SELF, 1, STRING("M_W_1"))
    CALL(btlevtcmd_SetMoveSpeed, BTL_UNIT_SELF, FLOAT(6.0f))
    CALL(btlevtcmd_AnimeChangePoseType, BTL_UNIT_SELF, 1, BTL_POSE_WALK)
    CALL(btlevtcmd_GetHomePos, BTL_UNIT_SELF, LW(0), LW(1), LW(2))
    CALL(btlevtcmd_MovePosition, BTL_UNIT_SELF, LW(0), LW(1), LW(2), 0, -1, 0)
    LABEL(99)
    CALL(fbPose, 0)
    CALL(btlevtcmd_ResetFaceDirection, BTL_UNIT_SELF)
    CALL_CHILD(btldefaultevt_SuitoruBadgeEffect)
    CALL(btlevtcmd_StartWaitEvent, BTL_UNIT_SELF)
    RETURN
EVT_END()

/* Fire Breath (his Hammer list): every ground enemy, fire, the hammer's
 * hold-left command and its power by the hammer's rank (failed, succeeded:
 * 1/2, 2/3, 3/4 against Mario's single-target 1/2, 2/4, 3/6), a chance to
 * burn. Cost and help as the game's hammer moves: 4 FP, the hammer's own
 * command help. */
static BattleWeapon sWeapon = {
    .name = "pc_fire_breath",
    .description = "pc_fire_breath_desc",
    .baseAccuracy = 100,
    .baseFpCost = 4,
    .reserved_14 = 1.0f,
    .stylishCommandMultiplier = 1,
    .reserved_19 = 5,
    .bingoSlotChance = 100,
    .reserved_1B = 50,
    .hpDamageFn = weaponGetPowerFromMarioHammerLv,
    .hpDamageWork = {1, 2, 2, 3, 3, 4, 0, 0},
    .targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE |
                        TARGET_CLASS_NO_SELF | TARGET_CLASS_MULTIPLE,
    .targetPropertyFlags = TARGET_PROP_HAMMERLIKE | TARGET_PROP_START_OPPOSING_SIDE,
    .element = ELEMENT_FIRE,
    .acLevel = 3,
    .reserved_6F = 2,
    .acHelpMsg = "msg_ac_hammer",
    .special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_UNGUARDABLE,
    .counterResistFlags = COUNTER_RESIST_ALL,
    .targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_UNUSED_2000 | TARGET_WEIGHT_RANDOM,
    .burnChance = 30,
    .burnTime = 2,
    .attackEvtScript = fire_breath_event,
    .stageEffectRates = {0, 0, 0, 100, 0, 10, 5, 5, 5, 5, 0, 0},
};

/* His first strike after the field's breath hit an enemy: that enemy alone,
 * the hammer first strike's power (marioDefaultWeapon_Hammer's), fire, and it
 * burns (the game has no first strike of its own that burns; Burn is its
 * status). */
static BattleWeapon sFirstWeapon = {
    .name = "pc_fire_breath",
    .description = "pc_fire_breath_desc",
    .baseAccuracy = 100,
    .reserved_14 = 1.0f,
    .stylishCommandMultiplier = 1,
    .reserved_19 = 5,
    .bingoSlotChance = 100,
    .reserved_1B = 50,
    .hpDamageFn = weaponGetPowerFromMarioHammerLv,
    .hpDamageWork = {1, 2, 2, 4, 3, 6, 0, 0},
    .targetClassFlags = TARGET_CLASS_NO_TREE_OR_SWITCH | TARGET_CLASS_NO_SYSTEM_UNIT | TARGET_CLASS_NO_OWN_ALLIANCE |
                        TARGET_CLASS_NO_SELF | TARGET_CLASS_SINGLE,
    .targetPropertyFlags = TARGET_PROP_HAMMERLIKE | TARGET_PROP_START_OPPOSING_SIDE,
    .element = ELEMENT_FIRE,
    .acLevel = 3,
    .reserved_6F = 2,
    .special = SPECIAL_BADGE_BUFFABLE | SPECIAL_STATUS_BUFFABLE | SPECIAL_CHARGE_BUFFABLE | SPECIAL_UNGUARDABLE,
    .counterResistFlags = COUNTER_RESIST_ALL,
    .targetWeightFlags = TARGET_WEIGHT_PREFER_FRONT | TARGET_WEIGHT_UNUSED_2000 | TARGET_WEIGHT_RANDOM,
    .burnChance = 100,
    .burnTime = 2,
    .attackEvtScript = fire_breath_event,
    .stageEffectRates = {0, 0, 0, 100, 0, 10, 5, 5, 5, 5, 0, 0},
};
