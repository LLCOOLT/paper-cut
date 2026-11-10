/* Co-op's camera and how far players 2 to 4 may roam (port/include/port/coop_camera.h; Multiplayer >
 * Camera, port/settings/features/coop_camera.cpp; docs/PLAYERS.md).
 *
 * The couch (one screen): the field camera's target moves toward the middle of the characters this
 * computer's players walk (at most PAN_MAX from Mario) and the camera pulls back by up to ZOOM_MAX, on
 * a spring so it eases in and out. The room's camera road still places the camera from that target
 * (it projects the target on the room's own curve and keeps it inside the curve's ends, and the floor
 * that picks the curve stays Mario's), so the camera never leaves the room's limits; the pull-back
 * fades out where the road holds the camera off the target (a room's end). A scene's or a script's
 * own camera (a preset, the host's camera online, a frozen target) is left as it is.
 *
 * Online, on a guest's screen: the target (and the floor that picks the curve) is the guest's own
 * character, as the host's game moves it, so the guest sees the room where it walks; its camera's yaw
 * goes to the host with its controller, so its stick walks as its picture shows.
 *
 * Everything here reads the game's state and the setting (each player's own: only the picture and
 * the yaw their own stick turns by), never the window's shape, except the picture's edge for the
 * soft edge (as the old leash did); replays and the headless build use the reach from Mario alone. */
#include "driver/camdrv.h"
#include "mario/mario.h"
#include "mario/mario_sbr.h"
#include "mario/mariost.h"
#include "party.h"
#include "system.h"
#include "wide_x.h"
#include "port/brothers.h"
#include "port/coop_camera.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/statesync.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PAN_MAX 150.0f     /* units the target may move from Mario toward the group */
#define ZOOM_MAX 0.25f     /* the camera's distance: at most a quarter more */
#define FIT_SHARE 0.7f     /* of the picture's half the group may fill before the camera pulls back (the soft edge holds at about 0.87: fully pulled back by then) */
#define SPRING_K 0.006f    /* the camera's spring (per field frame): settles in about a second and a half */
#define REACH 420.0f       /* units from Mario: the edge where no picture is drawn */
#define EDGE_SIDE 40.0f    /* the picture's margins for the soft edge, in its 608x480 units */
#define EDGE_TOP 170.0f    /* (feet: the body above them stays in) */
#define EDGE_BOTTOM 210.0f
#define TAN_HALF_FOV 0.22169f /* tan(12.5 degrees): the field camera's 25 degree field of view */
#define FALL_DEPTH 500.0f     /* units below where it last stood: fallen */
#define AIR_FRAMES 240        /* in the air this long: fallen (or stuck in the air) */

static int* sSetting;

static void declare(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("coop_camera", 0, 0, 1);
    }
}

int portCoopCameraFit(void) {
    declare();
    return *sSetting == 0;
}

/* --- the camera --- */

enum { MODE_NONE, MODE_COUCH, MODE_GUEST };

typedef struct {
    float x, v;
} Spring;

static struct {
    Spring off[3]; /* the target's offset from Mario's */
    Spring zoom;
    int mode;
    char map[16];
    unsigned logClock;
} sCam;

static void spring(Spring* s, float want) {
    const float c = 2.0f * sqrtf(SPRING_K); /* critically damped: no overshoot */
    s->v += SPRING_K * (want - s->x) - c * s->v;
    s->x += s->v;
}

static void springsReset(void) {
    memset(sCam.off, 0, sizeof(sCam.off));
    memset(&sCam.zoom, 0, sizeof(sCam.zoom));
}

/* the couch: where the target should go (from Mario) and how far to pull back, for the group */
static void couchWant(float (*pts)[3], int n, float* wantX, float* wantZ, float* zoomWant) {
    const MarioWork* mario = marioGetPtr();
    const CameraEntry* cam = camGetPtr(CAMERA_3D);
    float fx = cam->cameraAt.x - cam->cameraPos.x, fz = cam->cameraAt.z - cam->cameraPos.z;
    float flat = sqrtf(fx * fx + fz * fz);
    float umin = 0.0f, umax = 0.0f, wmin = 0.0f, wmax = 0.0f;
    float uc, wc, len, hu, need;
    float rx, rz;
    int i;
    if (flat < 1.0f) {
        fx = 0.0f, fz = -1.0f, flat = 1.0f;
    }
    fx /= flat, fz /= flat;
    rx = -fz, rz = fx; /* the picture's sideways, on the ground */
    /* the middle of everyone (Mario at 0), across and along the ground as the camera looks */
    for (i = 0; i < n; i++) {
        const float dx = pts[i][0] - mario->position.x, dz = pts[i][2] - mario->position.z;
        const float u = dx * rx + dz * rz, w = dx * fx + dz * fz;
        umin = u < umin ? u : umin, umax = u > umax ? u : umax;
        wmin = w < wmin ? w : wmin, wmax = w > wmax ? w : wmax;
    }
    uc = 0.5f * (umin + umax);
    wc = 0.5f * (wmin + wmax);
    len = sqrtf(uc * uc + wc * wc);
    if (len > PAN_MAX) {
        uc *= PAN_MAX / len, wc *= PAN_MAX / len;
    }
    *wantX = rx * uc + fx * wc;
    *wantZ = rz * uc + fz * wc;
    /* how much of the picture (the 4:3 one: widescreen only adds room) everyone takes now, feet and heads, seen
       from the camera as it is (panned as far as the road let it); as if not pulled back yet, the pull-back
       that fits them */
    {
        float ux, uy, uz, vx, vy, vz, gx = cam->cameraAt.x - cam->cameraPos.x, gy = cam->cameraAt.y - cam->cameraPos.y,
                                      gz = cam->cameraAt.z - cam->cameraPos.z;
        const float g = sqrtf(gx * gx + gy * gy + gz * gz);
        int k;
        need = 0.0f;
        if (g > 1.0f) {
            gx /= g, gy /= g, gz /= g;
            ux = -gz, uy = 0.0f, uz = gx; /* sideways (on the ground) */
            hu = sqrtf(ux * ux + uz * uz);
            ux /= hu, uz /= hu;
            vx = uy * gz - uz * gy, vy = uz * gx - ux * gz, vz = ux * gy - uy * gx; /* up the picture */
            for (i = -1; i < n; i++) {
                const float* p = i < 0 ? &mario->position.x : pts[i];
                for (k = 0; k < 2; k++) {
                    const float dx = p[0] - cam->cameraPos.x, dy = p[1] + (k ? 44.0f : 0.0f) - cam->cameraPos.y,
                                dz = p[2] - cam->cameraPos.z;
                    const float depth = dx * gx + dy * gy + dz * gz;
                    if (depth > 1.0f) {
                        const float sx = fabsf(dx * ux + dz * uz) / depth, sy = fabsf(dx * vx + dy * vy + dz * vz) / depth;
                        need = fmaxf(need, fmaxf(sx / (TAN_HALF_FOV * (608.0f / 480.0f) * FIT_SHARE),
                                                 sy / (TAN_HALF_FOV * FIT_SHARE)));
                    }
                }
            }
            need *= 1.0f + sCam.zoom.x;
        }
    }
    *zoomWant = need - 1.0f < 0.0f ? 0.0f : need - 1.0f > ZOOM_MAX ? ZOOM_MAX : need - 1.0f;
}

/* a guest's own character falling (a pit, deep water, out of the room until the host's game brings it back):
 * the camera goes down with it GUEST_DROP at most, as Mario's stays above a pit, never under the room's floor
 * where nothing but the backdrop shows (the multiplayer walkthrough: the sewers black on the guest's screen
 * while its partner fell, for seconds). Where it last stood: after a few frames level, or anything higher. */
#define GUEST_DROP 200.0f
#define GUEST_LEVEL_FRAMES 8

static struct {
    float floorY, lastY;
    int level, seen;
} sGuestFall;

static float guestFloorY(float y) {
    const float dy = y - sGuestFall.lastY;
    if (!sGuestFall.seen) {
        sGuestFall.seen = 1;
        sGuestFall.floorY = sGuestFall.lastY = y;
        sGuestFall.level = 0;
        return y;
    }
    sGuestFall.level = dy > -2.5f && dy < 2.5f ? sGuestFall.level + 1 : 0;
    sGuestFall.lastY = y;
    if (sGuestFall.level >= GUEST_LEVEL_FRAMES || y > sGuestFall.floorY) {
        sGuestFall.floorY = y; /* (standing or walking on a floor, or up: a jump, stairs) */
    }
    return y < sGuestFall.floorY - GUEST_DROP ? sGuestFall.floorY - GUEST_DROP : y;
}

void portCoopCamera(float* x, float* y, float* z, int followY) {
    float want[3] = {0.0f, 0.0f, 0.0f}, zoomWant = 0.0f;
    int mode = MODE_NONE, k;
    if (gp == NULL || gp->inBattle) {
        return;
    }
    if (strncmp(sCam.map, gp->currentMapName, sizeof(sCam.map)) != 0) {
        snprintf(sCam.map, sizeof(sCam.map), "%s", gp->currentMapName);
        springsReset(); /* a new room: the camera starts on Mario, as the road does */
        sGuestFall.seen = 0;
    }
    if (portCoopCameraFit() && portStateSyncCoopGuest()) {
        float ox, oy, oz;
        const MarioWork* mario = marioGetPtr();
        mode = MODE_GUEST;
        if (portStateSyncCoopOwnPose(&ox, &oy, &oz) && oy == oy) {
            oy = guestFloorY(oy);
            want[0] = ox - mario->position.x;
            want[1] = oy - mario->position.y;
            want[2] = oz - mario->position.z;
            if (want[0] * want[0] + want[2] * want[2] > 1500.0f * 1500.0f) {
                want[0] = want[1] = want[2] = 0.0f; /* (a pose from another place: none) */
            }
        } else {
            sGuestFall.seen = 0;
        }
    } else if (portCoopCameraFit()) {
        float pts[4][3];
        const int n = portCoopFramePoints(pts, 4);
        mode = MODE_COUCH;
        if (n > 0) {
            couchWant(pts, n, &want[0], &want[2], &zoomWant);
        }
    }
    for (k = 0; k < 3; k++) {
        spring(&sCam.off[k], want[k]);
    }
    spring(&sCam.zoom, zoomWant);
    if (sCam.zoom.x < 0.0f) {
        sCam.zoom.x = 0.0f, sCam.zoom.v = 0.0f;
    }
    sCam.mode = mode;
    if (++sCam.logClock % 240 == 0 && (fabsf(sCam.off[0].x) + fabsf(sCam.off[2].x) > 2.0f || sCam.zoom.x > 0.005f)) {
        /* (tests: the scenarios coop_camera_* and online_camera_guest look for these) */
        printf("[coopcam] %s: target moved (%.0f, %.0f, %.0f) from Mario's, pulled back %d%%\n",
               mode == MODE_GUEST ? "this guest's own character" : "fitting everyone", sCam.off[0].x, sCam.off[1].x,
               sCam.off[2].x, (int)(sCam.zoom.x * 100.0f + 0.5f));
        fflush(stdout);
    }
    *x += sCam.off[0].x;
    *z += sCam.off[2].x;
    if (followY) {
        *y += sCam.off[1].x;
    }
    if (mode != MODE_COUCH && mode != MODE_GUEST) {
        portBrothersCamera(x, z); /* Follow Mario: as before (Brothers mode keeps Luigi in view) */
    }
}

void portCoopCameraProbe(float* x, float* y, float* z) {
    if (sCam.mode == MODE_GUEST && gp != NULL && !gp->inBattle) {
        /* the guest's character's floor picks the curve (eased as the target) */
        *x += sCam.off[0].x;
        *y += sCam.off[1].x;
        *z += sCam.off[2].x;
    }
}

void portCoopCameraZoom(float* posX, float* posY, float* posZ, float atX, float atY, float atZ, float targetX,
                        float targetZ) {
    float dx = *posX - atX, dy = *posY - atY, dz = *posZ - atZ, flat, held, fade, f;
    if (sCam.zoom.x < 0.001f || gp == NULL || gp->inBattle) { /* (off the couch the spring eases it out) */
        return;
    }
    flat = sqrtf(dx * dx + dz * dz);
    if (flat < 1.0f) {
        return;
    }
    /* how far the road holds the camera off the target sideways (a room's end): no pull-back there */
    held = fabsf((atX - targetX) * (dz / flat) - (atZ - targetZ) * (dx / flat));
    fade = held <= 40.0f ? 1.0f : held >= 140.0f ? 0.0f : 1.0f - (held - 40.0f) / 100.0f;
    fade = fade * fade * (3.0f - 2.0f * fade);
    f = 1.0f + sCam.zoom.x * fade;
    *posX = atX + dx * f;
    *posY = atY + dy * f;
    *posZ = atZ + dz * f;
}

/* --- how far they roam --- */

int portCoopRoams(int player) {
    /* (an online guest's character, in the host's game or walked in the guest's own: the whole room) */
    return player >= 1 && player <= 3 &&
           (portStateSyncCoopPadHere(player + 1) || portStateSyncCoopLocalSeat() == player + 1);
}

float portCoopEdgeExcess(float x, float y, float z) {
    const MarioWork* mario = marioGetPtr();
    float excess = hypotf(x - mario->position.x, z - mario->position.z) - REACH;
#ifdef TTYD_BACKEND_AURORA
    if (!portReplayActive()) {
        /* (a replay's picture may be another shape than the recording's: the reach alone, as headless) */
        Vec pos = {x, y, z}, screen;
        const float side = 304.0f * portWidescreenEdgeScale() - EDGE_SIDE;
        getScreenPoint(&pos, &screen);
        excess = fmaxf(excess, fabsf(screen.x) - side);
        excess = fmaxf(excess, screen.y - EDGE_TOP);
        excess = fmaxf(excess, -EDGE_BOTTOM - screen.y);
    }
#else
    (void)y;
#endif
    return excess;
}

static struct {
    float groundY;
    int air;
    int seen;
} sFall[4];

int portCoopFallen(int slot, const PartyEntry* party) {
    if (slot < 0 || slot >= 4 || party == NULL) {
        return 0;
    }
    if (party->flags & PARTY_FLAG_FORCE_FALL) {
        return 1; /* deep water, a pit */
    }
    if (!sFall[slot].seen) {
        sFall[slot].seen = 1;
        sFall[slot].groundY = party->position.y;
        sFall[slot].air = 0;
    }
    if (!(party->flags & (PARTY_FLAG_IS_JUMPING | PARTY_FLAG_IS_FALLING))) {
        sFall[slot].groundY = party->position.y;
        sFall[slot].air = 0;
        return 0;
    }
    return ++sFall[slot].air > AIR_FRAMES || party->position.y < sFall[slot].groundY - FALL_DEPTH ||
           party->position.y != party->position.y; /* (NaN: never a place) */
}

void portCoopFallReset(int slot) {
    if (slot >= 0 && slot < 4) {
        sFall[slot].seen = 0;
    }
}

/* --- online: each guest's yaw --- */

static struct {
    uint16_t wire;
} sSeat[5];

uint16_t portCoopCameraYawWire(void) {
    float yaw;
    if (!portStateSyncCoopGuest() || gp == NULL || gp->inBattle) {
        return 0;
    }
    yaw = revise360(camGetPtr(CAMERA_3D)->viewYaw);
    return (uint16_t)(1 + (int)(yaw * (65000.0f / 360.0f)));
}

void portCoopCameraSeatYaw(int seat, uint16_t wire) {
    if (seat >= 2 && seat <= 4) {
        sSeat[seat].wire = wire;
    }
}

float portCoopStickYaw(int player, float fallback) {
    const int seat = player + 1;
    if (portStateSyncCoopLocalSeat() == seat) {
        /* (a co-op guest walking its own character here: its own camera's yaw, as the host takes it) */
        const uint16_t own = portCoopCameraYawWire();
        return own != 0 ? (float)(own - 1) * (360.0f / 65000.0f) : fallback;
    }
    if (!portCoopRoams(player) || sSeat[seat].wire == 0) {
        return fallback; /* (a guest that sends none: an older release, or its game not in the field) */
    }
    return (float)(sSeat[seat].wire - 1) * (360.0f / 65000.0f);
}
