/* Co-op pings (port/include/port/ping.h): a paper arrow over a spot or an
 * enemy, from either player, carried in the controller's input.
 *
 * The sending side (portPingPad, the game thread's pad read, before online
 * co-op's exchange): a ping this game's player asked for (a click, a tap, the
 * Ping binding let go after aiming) is resolved to a spot in the world here,
 * from this game's own view, and written into the player's pad. From then on
 * it is input like any other: the same in both games of a session, in a
 * replay, in a watcher's batches.
 *
 * The receiving side (portPingTakePad, after the replay's): every pad's ping
 * is read and taken off it, before the game's own code sees the pad. So what
 * the game gets is the same with or without pings, and both games start the
 * same arrow on the same frame. The arrows themselves are this file's own
 * (never the game's state), drawn in the 2D layer without the game's font. */
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/npcdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mariost.h"
#include "party.h"
#include "port/online.h"
#include "port/ping.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/view.h"
#include "wide_x.h"
#ifdef TTYD_BACKEND_AURORA
#include "port/bindings.h"
#endif

#define PING_BIT 0x0080          /* PADStatus.button: no GameCube button */
#define PING_EXT_MASK 0xF0000000u /* extButton's top four bits: Aurora's buttons stop at 0x08000000 */
#define PING_STEP 4.0f           /* the spot's x and z, in steps of 4 units: 10 bits each, +-2048 */
#define PING_FRAMES 300          /* an arrow stays 5 seconds */
#define PING_SNAP 56.0f          /* an enemy this near the spot gets the arrow (and it follows) */
#define PING_QUICK 160.0f        /* a quick press: the enemy this near the character, else its spot */
#define AIM_SPEED 7.0f           /* the cursor's units a frame, the C-stick all the way */
#define PLAYERS 2

int portPingsOn(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("coop_pings", 1, 0, 1);
    }
    return sOn != NULL && *sOn;
}

/* --- Where the players are --- */

static int inField(void) {
    return gp != NULL && !gp->inBattle && marioGetPtr() != NULL;
}

/* the character a player plays: Mario (0), the partner (1; Mario without one) */
static Vec character(int player) {
    MarioWork* mario = marioGetPtr();
    if (player == 1) {
        PartyEntry* party = partyGetPtr(marioGetPartyId());
        if (party != NULL) {
            return party->position;
        }
    }
    return mario->position;
}

static int enemyHere(const NpcEntry* e) {
    return (e->flags & (NPC_FLAG_USED | NPC_FLAG_ACTIVE)) == (NPC_FLAG_USED | NPC_FLAG_ACTIVE) &&
           (e->flags & NPC_FLAG_ENEMY) && !(e->flags & (NPC_FLAG_HIDDEN | NPC_FLAG_NO_DISPLAY));
}

/* the nearest enemy within reach of (x, z) on the ground, or -1 */
static int nearestEnemy(float x, float z, float reach) {
    NpcWork* w = npcGetWorkPtr();
    int i, best = -1;
    float bestD = reach * reach;
    for (i = 0; w != NULL && w->entries != NULL && i < w->total; i++) {
        const NpcEntry* e = &w->entries[i];
        float dx, dz, d;
        if (!enemyHere(e)) {
            continue;
        }
        dx = e->position.x - x;
        dz = e->position.z - z;
        d = dx * dx + dz * dz;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

/* --- Sending: this game's own pings --- */

static struct {
    int clicked;     /* a click or tap waiting for the next pad read */
    float nx, ny;    /* where, 0-1 across the picture */
} sClick;

static struct {
    int held;        /* the Ping binding held at the last read */
    int frames;      /* how long */
    int moved;       /* the cursor was moved */
    Vec cursor;      /* where it is (this game's only: drawn for this player alone) */
} sAim[PLAYERS];

void portPingClick(float nx, float ny) {
    sClick.clicked = 1;
    sClick.nx = nx;
    sClick.ny = ny;
}

/* the picture's point (nx, ny) on the ground at height h, through the 3D camera */
static int unproject(float nx, float ny, float h, Vec* out) {
    CameraEntry* camera = camGetPtr(CAMERA_3D);
    float xf, yf, xn, yn, dc[3], dw[3], eye[3], s;
    int i;
    if (camera == NULL || camera->projType != GX_PERSPECTIVE || camera->projection[0][0] == 0.0f ||
        camera->projection[1][1] == 0.0f) {
        return 0;
    }
    /* the 608 x 480 frame's units (wider in widescreen), as the menus' taps (menu_tap_pc.c) */
    xf = (nx - 0.5f) * 608.0f * portWidescreenEdgeScale();
    yf = (0.5f - ny) * 480.0f;
    if (portViewMirrored()) {
        xf = -xf;
    }
    xn = xf / 304.0f;
    yn = yf / 240.0f;
    /* the camera's own space: a point at z = -1 (clip w = 1) */
    dc[0] = (xn + camera->projection[0][2] - camera->projection[0][3]) / camera->projection[0][0];
    dc[1] = (yn + camera->projection[1][2] - camera->projection[1][3]) / camera->projection[1][1];
    dc[2] = -1.0f;
    /* to the world: the view's rotation transposed; the eye at -R^T t */
    for (i = 0; i < 3; i++) {
        dw[i] = camera->view[0][i] * dc[0] + camera->view[1][i] * dc[1] + camera->view[2][i] * dc[2];
        eye[i] = -(camera->view[0][i] * camera->view[0][3] + camera->view[1][i] * camera->view[1][3] +
                   camera->view[2][i] * camera->view[2][3]);
    }
    if (dw[1] > -0.0001f) {
        return 0; /* looking up from there: the ground isn't under the point */
    }
    s = (h - eye[1]) / dw[1];
    if (s < 0.0f) {
        return 0;
    }
    out->x = eye[0] + dw[0] * s;
    out->y = h;
    out->z = eye[2] + dw[2] * s;
    return 1;
}

static int quantize(float v) {
    int q = (int)floorf(v / PING_STEP + 0.5f);
    return q < -512 ? -512 : q > 511 ? 511 : q;
}

static void encode(PADStatus* pad, float x, float z) {
    const unsigned ux = (unsigned)quantize(x) & 0x3FFu, uz = (unsigned)quantize(z) & 0x3FFu;
    pad->button |= PING_BIT;
    pad->analogA = (u8)(ux & 0xFF);
    pad->analogB = (u8)(uz & 0xFF);
#ifdef TTYD_BACKEND_AURORA
    pad->extButton = (pad->extButton & ~PING_EXT_MASK) | ((ux >> 8) << 28) | ((uz >> 8) << 30);
#endif
}

/* the player this game's input plays: online, the host's 1 or the guest's 2 */
static int localPlayer(int port) {
    if (portOnlineActive()) {
        return portOnlineIsHost() ? 0 : 1;
    }
    return port;
}

#ifdef TTYD_BACKEND_AURORA
int portBindingsHeldOn(int action, int port); /* bindings.cpp */
#endif

void portPingPad(PADStatus* pads) {
    int port;
    const int ports = portOnlineActive() ? 1 : PLAYERS; /* online: this game's one player, pad 0 */
    const int clicked = sClick.clicked;
    sClick.clicked = 0;
    if (!portPingsOn() || !inField() || portReplayPlaying() || portOnlineWatching()) {
        memset(sAim, 0, sizeof(sAim));
        return;
    }
    for (port = 0; port < ports; port++) {
        const int player = localPlayer(port);
        PADStatus* pad = &pads[port];
        int held = 0, send = 0;
        Vec spot;
        /* the second screen's tap rides in pad 0's analogA (0xC0 and up): a ping waits a frame */
        const int busy = port == 0 && (pad->analogA & 0xC0) == 0xC0;
#ifdef TTYD_BACKEND_AURORA
        held = portBindingsHeldOn(PORT_BIND_PING, port);
#endif
        if (held) {
            float sx = pad->substickX / 127.0f, sz = pad->substickY / 127.0f;
            if (!sAim[port].held) {
                sAim[port].held = 1;
                sAim[port].frames = 0;
                sAim[port].moved = 0;
                sAim[port].cursor = character(player);
            }
            sAim[port].frames++;
            if (sx * sx + sz * sz > 0.04f) {
                sAim[port].moved = 1;
                sAim[port].cursor.x += sx * AIM_SPEED;
                sAim[port].cursor.z -= sz * AIM_SPEED; /* the stick up: away from the camera */
            }
        } else if (sAim[port].held && !busy) {
            sAim[port].held = 0;
            spot = sAim[port].cursor;
            if (!sAim[port].moved) {
                /* a quick press: the nearest enemy, else where the character stands */
                const Vec at = character(player);
                const int e = nearestEnemy(at.x, at.z, PING_QUICK);
                spot = e >= 0 ? npcGetWorkPtr()->entries[e].position : at;
            }
            send = 1;
        }
        if (!send && port == 0 && clicked && !busy) {
            const Vec at = character(player);
            send = unproject(sClick.nx, sClick.ny, at.y, &spot);
        }
        if (send) {
            encode(pad, spot.x, spot.z);
            printf("[ping] player %d sent a ping at %.0f, %.0f\n", player + 1, spot.x, spot.z);
            fflush(stdout);
        }
    }
}

/* --- Receiving: everyone's, as the game reads its pads --- */

typedef struct Arrow {
    int live;
    int age;
    int npc;    /* the enemy it follows (-1 a spot) */
    Vec at;     /* the spot on the ground, or the enemy's feet */
    float top;  /* the arrow's tip above the spot */
} Arrow;

static Arrow sArrows[PLAYERS]; /* one each: a new ping replaces the player's last */
static char sMap[16];

void portPingTakePad(PADStatus* pads) {
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        PADStatus* pad = &pads[i];
        unsigned ux, uz;
        int qx, qz;
        if (!(pad->button & PING_BIT)) {
            continue;
        }
        ux = pad->analogA;
        uz = pad->analogB;
#ifdef TTYD_BACKEND_AURORA
        ux |= ((pad->extButton >> 28) & 3u) << 8;
        uz |= ((pad->extButton >> 30) & 3u) << 8;
        pad->extButton &= ~PING_EXT_MASK;
#endif
        pad->button &= (u16)~PING_BIT;
        pad->analogA = 0;
        pad->analogB = 0;
        if (i >= PLAYERS || !inField()) {
            continue;
        }
        qx = (int)(ux ^ 0x200u) - 0x200;
        qz = (int)(uz ^ 0x200u) - 0x200;
        {
            Arrow* a = &sArrows[i];
            const Vec who = character(i);
            memset(a, 0, sizeof(*a));
            a->live = 1;
            a->at.x = qx * PING_STEP;
            a->at.y = who.y;
            a->at.z = qz * PING_STEP;
            a->npc = nearestEnemy(a->at.x, a->at.z, PING_SNAP);
            a->top = 12.0f;
            printf("[ping] player %d: %s at %.0f, %.0f\n", i + 1, a->npc >= 0 ? "an enemy" : "a spot", a->at.x,
                   a->at.z);
            fflush(stdout);
        }
    }
}

/* --- Drawing (the 2D layer) --- */

static const GXColor kColor[PLAYERS] = {{0xF0, 0x50, 0x40, 0xFF}, {0x40, 0xA8, 0xF0, 0xFF}}; /* 1P red, 2P blue */

static void flatStart(void) {
    Mtx m;
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    PSMTXCopy(camGetPtr(CAMERA_2D)->view, m);
    GXLoadPosMtxImm(m, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
}

static void color(GXColor c, float alpha) {
    c.a = (u8)(c.a * (alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha));
    GXSetTevColor(GX_TEVREG0, c);
}

/* a polygon (fan) of n points around (x, y), scaled by s */
static void fan(float x, float y, float s, const float (*p)[2], int n) {
    int i;
    GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, (u16)n);
    for (i = 0; i < n; i++) {
        GXPosition3f32(x + p[i][0] * s, y + p[i][1] * s, 0.0f);
    }
}

/* the arrow, its tip at (x, y) pointing down: a head and a shaft, as a cut-out */
static void arrowShape(float x, float y, float s, float grow) {
    const float head[3][2] = {{0.0f, -grow}, {-15.0f - grow, 17.0f + grow * 0.5f}, {15.0f + grow, 17.0f + grow * 0.5f}};
    const float shaft[4][2] = {
        {-5.5f - grow, 16.0f}, {5.5f + grow, 16.0f}, {5.5f + grow, 38.0f + grow}, {-5.5f - grow, 38.0f + grow}};
    fan(x, y, s, head, 3);
    fan(x, y, s, shaft, 4);
}

static void arrowDraw(float x, float y, float s, GXColor c, float alpha) {
    const float crease[3][2] = {{0.0f, 0.0f}, {15.0f, 17.0f}, {0.0f, 17.0f}};
    color((GXColor){0x20, 0x18, 0x10, 0xFF}, alpha * 0.35f); /* its shadow, as paper casts one */
    arrowShape(x + 3.0f * s, y - 3.0f * s, s, 2.5f);
    color(c, alpha); /* the edge, the player's colour */
    arrowShape(x, y, s, 2.5f);
    color((GXColor){0xFF, 0xFC, 0xF0, 0xFF}, alpha); /* the paper */
    arrowShape(x, y, s, 0.0f);
    color((GXColor){0xD8, 0xD0, 0xC0, 0xFF}, alpha); /* the fold down its middle */
    fan(x, y, s, crease, 3);
}

/* a ring on the ground around a world spot (through the 3D camera) */
static void ringDraw(const Vec* at, float radius, GXColor c, float alpha) {
    enum { N = 20 };
    Vec in[N + 1], out[N + 1];
    int i;
    for (i = 0; i <= N; i++) {
        const float t = (float)i * 6.2831853f / N;
        Vec p = *at, q;
        p.x += cosf(t) * radius;
        p.z += sinf(t) * radius;
        getScreenPoint(&p, &q);
        out[i] = q;
        p = *at;
        p.x += cosf(t) * radius * 0.72f;
        p.z += sinf(t) * radius * 0.72f;
        getScreenPoint(&p, &q);
        in[i] = q;
    }
    color(c, alpha);
    GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)(2 * (N + 1)));
    for (i = 0; i <= N; i++) {
        const float m = portViewMirrored() ? -1.0f : 1.0f;
        GXPosition3f32(out[i].x * m, out[i].y, 0.0f);
        GXPosition3f32(in[i].x * m, in[i].y, 0.0f);
    }
}

static void pingDisp(CameraId camId, void* param) {
    int i;
    (void)camId;
    (void)param;
    flatStart();
    for (i = 0; i < PLAYERS; i++) {
        const Arrow* a = &sArrows[i];
        Vec tip, screen;
        float grow, alpha, bob;
        if (!a->live) {
            continue;
        }
        /* it springs up out of the ground, bobs, and fades at the end */
        grow = a->age < 10 ? sinf((float)a->age / 10.0f * 2.0f) * 1.15f : 1.0f;
        alpha = a->age > PING_FRAMES - 30 ? (float)(PING_FRAMES - a->age) / 30.0f : 1.0f;
        bob = sinf((float)a->age * 0.12f) * 4.0f;
        ringDraw(&a->at, a->npc >= 0 ? 26.0f : 18.0f, kColor[i], alpha * 0.8f);
        tip = a->at;
        tip.y += a->top + bob;
        getScreenPoint(&tip, &screen);
        if (portViewMirrored()) {
            screen.x = -screen.x;
        }
        arrowDraw(screen.x, screen.y, grow * 1.1f, kColor[i], alpha);
    }
    for (i = 0; i < PLAYERS; i++) {
        /* this player's cursor while aiming (theirs alone) */
        if (sAim[i].held && sAim[i].frames > 6) {
            const int player = localPlayer(i);
            ringDraw(&sAim[i].cursor, 16.0f, kColor[player], 0.9f);
            ringDraw(&sAim[i].cursor, 6.0f, (GXColor){0xFF, 0xFF, 0xFF, 0xFF}, 0.9f);
        }
    }
}

void portPingFrame(void) {
    int i, any = 0;
    if (gp == NULL) {
        return;
    }
    if (strncmp(sMap, gp->currentMapName, sizeof(sMap)) != 0) {
        /* another room: the old one's arrows gone */
        memcpy(sMap, gp->currentMapName, sizeof(sMap));
        memset(sArrows, 0, sizeof(sArrows));
        memset(sAim, 0, sizeof(sAim));
    }
    for (i = 0; i < PLAYERS; i++) {
        Arrow* a = &sArrows[i];
        if (!a->live) {
            continue;
        }
        if (++a->age >= PING_FRAMES) {
            a->live = 0;
            continue;
        }
        if (a->npc >= 0) {
            NpcWork* w = npcGetWorkPtr();
            const NpcEntry* e = w != NULL && a->npc < w->total ? &w->entries[a->npc] : NULL;
            if (e == NULL || !enemyHere(e)) {
                a->npc = -1; /* defeated or gone: the arrow stays where it was */
            } else {
                a->at = e->position;
                a->top = e->height + 14.0f;
            }
        }
        any = 1;
    }
    for (i = 0; i < PLAYERS; i++) {
        any |= sAim[i].held && sAim[i].frames > 6;
    }
    if (any && portPingsOn() && inField()) {
        dispEntry(CAMERA_2D, 901.0f, 1, pingDisp, NULL);
    }
}
