/* Luigi's higher jump (the setting luigi_high_jump, on by default; docs/PLAYABLE_LUIGI.md, "His jump"):
 * while Luigi is the Play as pick, his A-button jump in the field goes about 11 % higher and hangs a few
 * frames longer at the top, as in his own games. Mario, Peach and Bowser jump as before, and so does
 * everything else: battles (their jumps are attacks on fixed timing), the jump off an enemy or a switch,
 * blue jump stands, scripted jumps, Yoshi, the Moon's gravity and the pre-battle slow motion.
 *
 * Three touches to Mario's own jump (src/motion/mot_jump.c, src/mario/mario_motion.c):
 *  - take-off (marioPreJump, after marioSetJumpPara): the rise's curve times RISE, so it reaches RISE
 *    times as high on the same frames;
 *  - the last of the rise (marioJump): while he rises slower than APEX_SPEED, the curve's slowing is
 *    APEX_GRAVITY of Mario's, so he eases into the top;
 *  - the top (marioJump, after marioSetFallPara): the fall starts at FALL_START of Mario's pull, so he
 *    hangs before dropping; the fall then speeds up as Mario's does and lands as his.
 * Landing, stomping, the hammer and the Super Boots' spin jump (A after the top, the same frames after
 * it) are Mario's, and so is the Ultra Boots' Spring Jump, which still goes far higher (its low hops
 * at a short charge stay Mario's too: scaled, they would reach a 50-unit ledge, which nothing else does).
 *
 * tools/port/luigi_jump_sweep.py simulates the same curve (it reads these constants) and checks every
 * field map for a ledge, block or gap that only the higher jump reaches. Online it is one of the
 * session's field rules (port/include/port/online_local.h); replays keep it (replay.cpp). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port/gameplus.h"
#include "port/luigi.h"
#include "mario/mario.h"
#include "mario/mariost.h"

#define RISE 1.06f
#define APEX_SPEED 2.0f
#define APEX_GRAVITY 0.6f
#define FALL_START 0.5f

/* Where Luigi takes off with Mario's jump: the few places the sweep found where the higher one reaches
 * what Mario's can't and it matters (an early item, a skipped way round, a pit with no way out). Each
 * box holds every take-off from which it reaches there (tools/port/luigi_jump_sweep.py --zones-for);
 * the sweep reads this table, so a re-run proves the place closed. x, z (min, max), then y (min, max). */
static const struct {
    const char map[8];
    float x0, z0, x1, z1, y0, y1;
} kMarioJumpHere[] = {
    {"gor_02", -481.0f, -71.0f, -317.0f, 38.0f, -5.0f, 5.0f}, /* onto a house's roof by the west door, and */
    {"gor_02", -124.0f, -364.0f, -16.0f, -138.0f, 67.0f, 78.0f}, /* onto the east side's roofs (their Star */
    {"gor_02", 206.0f, -338.0f, 401.0f, 146.0f, 128.0f, 139.0f}, /* Piece, Shine Sprite and Ultra Stone) */
    {"jin_00", -695.0f, 39.0f, -495.0f, 214.0f, 146.0f, 157.0f}, /* the Steeple's upper ledge (its key) */
    {"muj_03", -132.0f, -107.0f, 57.0f, 107.0f, 114.0f, 125.0f}, /* onto a brick, up to the badge block */
    {"dou_03", -11.0f, -397.0f, 238.0f, -193.0f, 260.0f, 270.0f}, /* down into a pit with no way out */
    {"las_00", -414.0f, 132.0f, -205.0f, 344.0f, 238.0f, 249.0f}, /* down into a pit with no way out */
};

/* this jump is Luigi's higher one (from its take-off to its top) */
static int sHigh;

/* tests: TTYD_JUMP_LOG=1 logs each A-button jump's top (Mario's too), for comparing the two */
static int sLogFrame;
static float sLogY;

static int logOn(void) {
    static int sOn = -1;
    if (sOn < 0) {
        const char* v = getenv("TTYD_JUMP_LOG");
        sOn = v != NULL && v[0] == '1';
    }
    return sOn;
}

static int marioJumpHere(const MarioWork* mario) {
    size_t i;
    for (i = 0; i < sizeof(kMarioJumpHere) / sizeof(kMarioJumpHere[0]); i++) {
        if (strcmp(gp->currentMapName, kMarioJumpHere[i].map) == 0 && mario->position.x >= kMarioJumpHere[i].x0 &&
            mario->position.x <= kMarioJumpHere[i].x1 && mario->position.z >= kMarioJumpHere[i].z0 &&
            mario->position.z <= kMarioJumpHere[i].z1 && mario->position.y >= kMarioJumpHere[i].y0 &&
            mario->position.y <= kMarioJumpHere[i].y1) {
            return 1;
        }
    }
    return 0;
}

int portLuigiHighJump(void) {
    return portLuigiOn() && portGameplus("luigi_high_jump") != 0;
}

void portLuigiJumpStart(void) {
    MarioWork* mario = marioGetPtr();
    sHigh = 0;
    if (mario != NULL && logOn()) {
        sLogFrame = (int)gp->retraceCount;
        sLogY = mario->position.y;
    }
    if (mario == NULL || mario->characterId != 0 || mario->currMotionId != MARIO_MOTION_JUMP ||
        (mario->trigFlags & MARIO_TRIG_FLAG_SET_JUMP_PARAMS) || (mario->flags & MARIO_FLAG_PRE_BATTLE_PHYSICS) ||
        mario->playerGravity != 1.0f || !portLuigiHighJump() || marioJumpHere(mario)) {
        return;
    }
    sHigh = 1;
    mario->jumpVelocityY *= RISE;
    mario->jumpAccelerationY *= RISE;
    mario->jumpJerkY *= RISE;
    mario->jumpSnapY *= RISE;
}

void portLuigiJumpRise(float prev) {
    MarioWork* mario = marioGetPtr();
    float v;
    if (!sHigh || mario->currMotionId != MARIO_MOTION_JUMP) {
        return;
    }
    v = mario->jumpVelocityY;
    if (v > 0.0f && v < APEX_SPEED) {
        mario->jumpVelocityY = prev + (v - prev) * APEX_GRAVITY;
    }
}

void portLuigiJumpTop(void) {
    MarioWork* mario = marioGetPtr();
    if (logOn() && mario->currMotionId == MARIO_MOTION_JUMP) {
        /* (the frame's step down isn't taken yet: he is at the top) */
        printf("[jump] %s: the top %.2f over the take-off at %.0f %.0f %.0f, %d frames after it",
               sHigh ? "Luigi's higher jump" : "a jump", mario->position.y - sLogY, mario->position.x, sLogY,
               mario->position.z, (int)gp->retraceCount - sLogFrame);
        puts("");
        fflush(stdout);
    }
    if (!sHigh) {
        return;
    }
    sHigh = 0;
    if (mario->currMotionId == MARIO_MOTION_JUMP) {
        mario->jumpAccelerationY *= FALL_START;
    }
}
