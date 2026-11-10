/* Peach's parasol (port/include/port/luigi.h, docs/PLAY_AS.md). Her hammer plane shows the parasol
 * cards made from the disc (art/recipes/peach_parasol.recipe, put on her models by
 * luigi_model_pc.c's portPlayAsHammerCards): closed in every swing, as her animations pick Mario's
 * hammer drawings, and open over her head while she floats.
 *
 * The float: with Peach as the pick, holding A as she comes down from a jump opens the parasol, and
 * she sinks slowly, drifting no faster than she walks, for at most FLOAT_FRAMES after the jump's
 * peak; letting go of A (or the time running out) closes it and she falls as Mario does. It never
 * lifts her. Everything it reads is the game's own state and input (MarioWork), so it is the same on
 * every run of the same input, online and in replays; with anyone else picked it does nothing.
 *
 * The Ultra Boots' Spring Jump wins over it, so the same input gives Peach what it gives Mario: a jump
 * in place with A held never opens it (the Spring Jump arms as she comes down), and on any other jump
 * the stick turned a third of a round with A held (SPIN_TURN, the start of the Spring Jump's spin) closes it
 * for the rest of that jump, so she drops as Mario does and lands into the Spring Jump. The Spin Jump
 * (A pressed again after the top) is untouched: its press starts the spin's own motion. */
#include "port/luigi.h"

#include "driver/animdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include "mario/mario_motion.h"
#include "mario/mario_pouch.h"
#include "mot/mot_jabara.h"
#include "version.h"

#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLOAT_FRAMES 105  /* the most it lasts after the peak: 1.75 s */
#define FLOAT_SINK 1.0f   /* units a frame down while open (a fall reaches 18) */
#define FLOAT_BRAKE 0.25f /* opened while falling faster: the share of the way to FLOAT_SINK a frame */
#define FLOAT_DRIFT 0.75f /* the most she moves sideways, in her walking speeds */

/* Where she doesn't open it: the few places a float reaches what Mario's jump can't and it matters (a pit
 * with no way out, a key early, a way round skipped). Each box holds every take-off (the jump's start,
 * lastGroundPos) from which a float gets there, as a sweep of the maps' floors found them (the jump's arc,
 * the float's sink and drift, letting go of A at any frame); x, z (min, max), then y (min, max). */
static const struct {
    const char map[8];
    float x0, z0, x1, z1, y0, y1;
} kNoFloat[] = {
    {"aji_13", -143.0f, -127.0f, 227.0f, 318.0f, 195.0f, 215.0f}, /* down onto the Card Key's floor */
    {"dou_03", -45.0f, -404.0f, 245.0f, -186.0f, 260.0f, 270.0f}, /* down into a pit with no way out */
    {"gor_03", 376.0f, -177.0f, 691.0f, 137.0f, 192.0f, 209.0f},  /* across onto a ledge (western Rogueport) */
    {"las_00", -425.0f, 105.0f, -180.0f, 355.0f, 238.0f, 249.0f}, /* down into a pit with no way out */
    {"las_24", -395.0f, -199.0f, 8.0f, 195.0f, 94.0f, 105.0f},    /* down past the way round */
    {"mri_00", 13.0f, -345.0f, 363.0f, 5.0f, 156.0f, 167.0f},     /* down past the way round */
    {"muj_10", -19.0f, -170.0f, 198.0f, 28.0f, 12.0f, 30.0f},     /* across onto the far deck */
};

static int noFloatHere(const MarioWork* m) {
    size_t i;
    for (i = 0; i < sizeof(kNoFloat) / sizeof(kNoFloat[0]); i++) {
        if (strcmp(gp->currentMapName, kNoFloat[i].map) == 0 && m->lastGroundPos.x >= kNoFloat[i].x0 &&
            m->lastGroundPos.x <= kNoFloat[i].x1 && m->lastGroundPos.z >= kNoFloat[i].z0 &&
            m->lastGroundPos.z <= kNoFloat[i].z1 && m->lastGroundPos.y >= kNoFloat[i].y0 &&
            m->lastGroundPos.y <= kNoFloat[i].y1) {
            return 1;
        }
    }
    return 0;
}

/* The Spring Jump's spin started mid-air (the stick turned one way with A held and the Ultra Boots):
 * read once a falling frame (portParasolFall), from the stick in MarioWork. A frame that doesn't follow
 * the last one read (a new jump, a loaded state) starts it over. */
#define SPIN_TURN 120.0f /* degrees turned one way: a third of a round */
#define SPIN_MAG 30.0f   /* the stick pushed at least this far (its gate reaches about 70) */
static struct {
    int air;     /* the airTimer last read */
    int sign;    /* the way it turns: 1, -1, 0 none yet */
    int idle;    /* frames the stick has been near the middle */
    float angle; /* the stick's angle last read */
    float turned;
    int spun;    /* the spin is on: no float for the rest of this jump */
} sSpin = {-1, 0, 0, -1000.0f, 0.0f, 0};

static void spinRead(const MarioWork* m) {
    const float x = (float)m->wStickDir1, y = (float)m->wStickDir2;
    float a, d;
    if (m->airTimer != sSpin.air + 1 && m->airTimer != sSpin.air) {
        sSpin.sign = 0;
        sSpin.idle = 0;
        sSpin.turned = 0.0f;
        sSpin.spun = 0;
        sSpin.angle = -1000.0f;
        sSpin.air = -1;
    }
    if (m->airTimer == sSpin.air) {
        return; /* (read already this frame) */
    }
    sSpin.air = m->airTimer;
    if (!(m->buttonHold & PAD_BUTTON_A) || !marioPriCheckJabara()) {
        sSpin.sign = 0;
        sSpin.turned = 0.0f;
        return;
    }
    if (x * x + y * y < SPIN_MAG * SPIN_MAG) {
        if (++sSpin.idle > 10) {
            sSpin.sign = 0;
            sSpin.turned = 0.0f;
            sSpin.angle = -1000.0f; /* (let go: a new spin starts from wherever it is pushed next) */
        }
        return;
    }
    sSpin.idle = 0;
    a = atan2f(y, x) * (180.0f / 3.14159265f);
    if (sSpin.angle > -999.0f) {
        d = a - sSpin.angle;
        d = d > 180.0f ? d - 360.0f : d < -180.0f ? d + 360.0f : d;
        if (d != 0.0f) {
            const int s = d > 0.0f ? 1 : -1;
            if (d >= 90.0f || d <= -90.0f || (sSpin.sign != 0 && s != sSpin.sign)) {
                sSpin.turned = 0.0f; /* (a jolt or a turn back: not a spin) */
            }
            sSpin.sign = s;
            sSpin.turned += d > 0.0f ? d : -d;
            if (sSpin.turned >= SPIN_TURN) {
                sSpin.spun = 1;
            }
        }
    }
    sSpin.angle = a;
}

static int floating(const MarioWork* m) {
    if (portPlayAs() != PORT_PLAY_AS_PEACH || m == NULL || m->characterId != 0) {
        return 0;
    }
    if (m->currMotionId != MARIO_MOTION_FALL || !(m->flags & MARIO_FLAG_IS_FALLING) ||
        !(m->flags & MARIO_FLAG_HAS_INPUT_JUMP) || m->jumpPeakAirTime <= 0) {
        return 0; /* only coming down from her own jump (not off a ledge, a spring, a hit) */
    }
    if (m->flags & (MARIO_FLAG_PAPER_MODE | MARIO_FLAG_IS_8_BIT | MARIO_FLAG_TUBE_MODE |
                    MARIO_FLAG_IS_CARRYING_BOBBERY | MARIO_FLAG_FORCED_MOVEMENT | MARIO_FLAG_PRE_BATTLE_PHYSICS)) {
        return 0;
    }
    if (!(m->buttonHold & PAD_BUTTON_A) || m->airTimer - m->jumpPeakAirTime > FLOAT_FRAMES) {
        return 0;
    }
    if (noFloatHere(m)) {
        return 0;
    }
    /* a jump in place with the Ultra Boots is the Spring Jump's (hold A as she comes down): it wins */
#if VERSION >= VERSION_US
    if (marioPriCheckJabara() && m->jumpStartSpeed == 0.0f && !(m->trigFlags & MARIO_TRIG_FLAG_JABARA_MOVE_CHK)) {
        return 0;
    }
#else
    /* (JP keeps no take-off speed nor stick-moved flag: in place is still over the spot she left, lastGroundPos
     * set at take-off; floating there kept her up through the spin a Spring Jump is begun with) */
    if (marioPriCheckJabara() && (m->position.x - m->lastGroundPos.x) * (m->position.x - m->lastGroundPos.x) +
                                         (m->position.z - m->lastGroundPos.z) * (m->position.z - m->lastGroundPos.z) <
                                     1.0f) {
        return 0;
    }
#endif
    /* the Spring Jump's spin started mid-air (any jump): it wins too, for the rest of the jump */
    if (sSpin.spun && sSpin.air >= 0 && m->airTimer - sSpin.air >= 0 && m->airTimer - sSpin.air <= 1) {
        return 0;
    }
    return 1;
}

int portParasolFloating(void) {
    return floating(marioGetPtr());
}

/* TTYD_PARASOL_TRACE=1: a line a falling frame while Peach is the pick (tests) */
static int trace(void) {
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("TTYD_PARASOL_TRACE");
        on = e != NULL && e[0] == '1';
    }
    return on;
}

float portParasolFall(float spd) {
    MarioWork* m = marioGetPtr();
    if (portPlayAs() == PORT_PLAY_AS_PEACH) {
        spinRead(m);
    }
    if (trace() && portPlayAs() == PORT_PLAY_AS_PEACH) {
        printf("[parasol] air %d peak %d mot %d A %d y %.2f spd %.2f base %.2f float %d stick %d %d spin %.0f %d\n",
               m->airTimer,
               m->jumpPeakAirTime, m->currMotionId, (m->buttonHold & PAD_BUTTON_A) != 0, m->position.y, spd,
               m->baseSpeed, floating(m), m->wStickDir1, m->wStickDir2, sSpin.turned, sSpin.spun);
        fflush(stdout);
    }
    if (!floating(m)) {
        return spd;
    }
    {
        /* the speed before marioGetFallSpd's step: opened while falling faster, it brakes from there */
        const float before = spd - m->jumpAccelerationY;
        if (before < -FLOAT_SINK) {
            spd = before + (-FLOAT_SINK - before) * FLOAT_BRAKE;
        } else if (spd < -FLOAT_SINK) {
            spd = -FLOAT_SINK;
        } else if (spd > 0.0f) {
            spd = 0.0f;
        }
    }
    /* the fall's curve starts again each frame from this speed, so letting go falls as from the peak */
    marioSetFallPara();
    m->jumpVelocityY = spd;
    if (m->baseSpeed > m->baseWalkSpeed * FLOAT_DRIFT) {
        m->baseSpeed = m->baseWalkSpeed * FLOAT_DRIFT;
    }
    return spd;
}

float portParasolDrift(float dist) {
    MarioWork* m = marioGetPtr();
    float most;
    if (!floating(m)) {
        return dist;
    }
    most = m->baseWalkSpeed * m->playerGravity * FLOAT_DRIFT;
    return dist > most ? most : dist;
}

/* The float's own pose (tools/port/peach_motions.py M_J_1P, from behind M_J_1PR, in her packs): both
 * hands on the handle over her head, the hammer plane straight up from the near one with the open
 * parasol's slot. Played in place of Mario's fall only while she floats with the parasol on her models
 * (else that slot is still Mario's hammer drawing). */
#define FLOAT_ANIM "M_J_1P"
#define FLOAT_ANIM_BACK "M_J_1PR"

const char* portParasolAnim(int where, int back) {
    if (where != PORT_LUIGI_FIELD || !portPlayAsParasol() || !portParasolFloating()) {
        return NULL;
    }
    return back ? FLOAT_ANIM_BACK : FLOAT_ANIM;
}

/* The open parasol: its texture slots after the twelve closed (luigi_pack.PARASOL_TEXTURES), one per
 * hammer; the pose shows the first (the wooden Hammer's), this picks the one of the hammer she has. */
#define OPEN_SLOT 12

void portParasolPose(int pose, int back, int where, const char* mario) {
    AnimationPoseData* data;
    AnimationPose* p;
    const AnimTableEntry* cur;
    int g, tier;
    (void)mario;
    if (portParasolAnim(where, back) == NULL || pose < 0) {
        return;
    }
    cur = animPoseGetCurrentAnim(pose);
    if (cur == NULL || strcmp(cur->name, portParasolAnim(where, back)) != 0) {
        return; /* (not the float's pose: a model without it) */
    }
    tier = pouchGetHammerLv();
    tier = tier < 1 ? 1 : tier > 3 ? 3 : tier;
    data = animPoseGetAnimBaseDataPtr(pose);
    p = animPoseGetAnimPosePtr(pose);
    if (data == NULL || p == NULL) {
        return;
    }
    for (g = 0; g < data->groupCount; g++) {
        const AnimPoseGroup* gr = &data->groups[g];
        if (strcmp(gr->name, "Hammer_pc") == 0 && gr->shapeId >= 0 && gr->shapeId < data->shapeCount) {
            const AnimPoseShape* sh = &data->shapes[gr->shapeId];
            if (sh->shapeDrawStart >= 0 && sh->shapeDrawStart < data->drawCount) {
                const int slot = data->draws[sh->shapeDrawStart].texDataIds[0];
                if (slot >= 0 && slot < data->texDataCount &&
                    data->texData[slot].bindId + OPEN_SLOT + 2 < data->texBindCount) {
                    p->texAnimEntries[slot].texBindIdx = (u8)(OPEN_SLOT + tier - 1);
                }
            }
            return;
        }
    }
}
