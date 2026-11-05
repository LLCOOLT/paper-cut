/* Widescreen: port/include/port/widescreen.h. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <aurora/aurora.h>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "mario/mariost.h"
#include "port/settings.h"
#include "port/view.h"
#include "port/widescreen.h"
#include <stdio.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

/* Off by default everywhere: much of the game was built for 4:3 only (the
 * title diorama, the opening story, indoor rooms end at the 4:3 edges), so
 * 16:9 shows past those sets. It is the player's choice. */
#define WIDESCREEN_DEFAULT 0

/* The game's frame is 608x480 with square pixels (camdrv's 3D aspect,
 * 1.2666666); the widened frame is the chosen aspect (16:9 by default). */
#define GAME_ASPECT (608.0f / 480.0f)
#define WIDE_ASPECT (16.0f / 9.0f)

static int sEnabled = -1;
static int* sAspectChoice; /* settings: aspect_ratio (PORT_ASPECT_*) */
static int* sAspectMax;    /* settings: aspect_max, 0 none or the widest (16:9, 21:9) */
static int* sSceneLimits;  /* settings: aspect_scene_limits, bars where a scene's set ends */
static float sAspect;      /* the frame's now; 0: the game's 4:3 */
extern float gPortWidescreenEdgeScale; /* include/wide_x.h */

int portWidescreenEnabled(void) {
    if (sEnabled < 0) {
        const char* e = getenv("TTYD_WIDESCREEN");
        sEnabled = WIDESCREEN_DEFAULT;
#if defined(__APPLE__)
        {
            /* iOS: the switch in the Settings app (port/ios/Settings.bundle);
             * macOS: `defaults write <bundle id> widescreen -bool true` */
            Boolean valid = false;
            Boolean on = CFPreferencesGetAppBooleanValue(CFSTR("widescreen"), kCFPreferencesCurrentApplication, &valid);
            if (valid) {
                sEnabled = on ? 1 : 0;
            }
        }
#endif
        if (e != NULL) {
            sEnabled = e[0] == '1';
        }
    }
    return sEnabled;
}

static void declare(void) {
    if (sAspectChoice == NULL) {
        sAspectChoice = portSettingsDeclare("aspect_ratio", PORT_ASPECT_SWITCH, 0, PORT_ASPECT_WINDOW);
        sAspectMax = portSettingsDeclare("aspect_max", 0, 0, 2);
        sSceneLimits = portSettingsDeclare("aspect_scene_limits", 1, 0, 1);
    }
}

/* The scenes whose sets end before the widened frame's edges (the
 * widescreen survey of 0.3.0): the widest aspect each shows
 * before black void past its walls or the set's own ends comes into view,
 * measured at its entrances. Wider than that, the frame stops there and the
 * rest is bars (side_bars blurs them). Rooms not listed are not limited.
 * Where a room's view is in kAngleLimits (below), its bars follow the camera
 * instead and this is not used. */
static const struct {
    const char* map;
    float widest;
} kRoomLimits[] = {
    {"eki_01", 4.0f / 3.0f}, /* Riverside Station: the wall at the right ends at 4:3 */
    {"aji_01", 4.0f / 3.0f}, /* X-Naut Fortress: void past the left wall */
    {"las_00", 4.0f / 3.0f}, /* Palace of Shadow: void past the left wall */
    {"gon_00", 1.40f},       /* Hooktail Castle: void past the left wall */
    {"kpa_00", 1.40f},       /* Bowser's castle set: its backdrop and rocks end */
    {"dou_00", 1.47f},       /* Pirate's Grotto: void past the left wall */
    {"rsh_00_a", 1.60f},     /* Excess Express: void past the right wall */
    {"mri_01", 1.87f},       /* the Great Tree: void past the left at 21:9 */
    {"nok_00", 1.90f},       /* Petalburg: the path and the hills end at 21:9 */
    {"gor_01", 1.90f},       /* Rogueport: the sky past the last building at 21:9 */
    {"rsh_02_a", 2.00f},     /* Excess Express: void past the right at 21:9 */
};
/* battles: the theatre's stage fills 16:9; past it its side walls show */
#define BATTLE_LIMIT (16.0f / 9.0f)

static int angleView(void);

/* The widest aspect the scene now shows (0: no limit). */
static float sceneLimit(void) {
    size_t i;
    if (gp == NULL) {
        return 0.0f;
    }
    if (gp->inBattle) {
        return BATTLE_LIMIT;
    }
    if (angleView() >= 0) {
        return 0.0f; /* bars per camera angle instead (below) */
    }
    for (i = 0; i < sizeof(kRoomLimits) / sizeof(kRoomLimits[0]); i++) {
        if (strcmp(gp->currentMapName, kRoomLimits[i].map) == 0) {
            return kRoomLimits[i].widest;
        }
    }
    return 0.0f;
}

/* The aspect the settings ask for now: 0 for the game's own 4:3. */
static float wanted(void) {
    static const float kMax[] = {0.0f, 16.0f / 9.0f, 21.0f / 9.0f};
    float aspect = 0.0f;
    declare();
    switch (*sAspectChoice) {
    case PORT_ASPECT_SWITCH:
        aspect = portWidescreenEnabled() ? WIDE_ASPECT : 0.0f;
        break;
    case PORT_ASPECT_16_9:
        aspect = 16.0f / 9.0f;
        break;
    case PORT_ASPECT_16_10:
        aspect = 16.0f / 10.0f;
        break;
    case PORT_ASPECT_21_9:
        aspect = 21.0f / 9.0f;
        break;
    case PORT_ASPECT_32_9:
        aspect = 32.0f / 9.0f;
        break;
    case PORT_ASPECT_WINDOW: {
        const AuroraWindowSize size = aurora_get_window_size();
        if (size.native_fb_width > 0 && size.native_fb_height > 0) {
            aspect = (float)size.native_fb_width / (float)size.native_fb_height;
        }
        break;
    }
    default: /* PORT_ASPECT_4_3 */
        break;
    }
    if (*sAspectMax > 0 && aspect > kMax[*sAspectMax]) {
        aspect = kMax[*sAspectMax]; /* the rest: bars (blurred with side_bars) */
    }
    if (*sSceneLimits) {
        const float limit = sceneLimit();
        if (limit > 0.0f && aspect > limit) {
            aspect = limit; /* the scene ends there: bars past it, as aspect_max's */
        }
    }
    /* a window no wider than 4:3 shows the game's own frame */
    return aspect > 4.0f / 3.0f + 0.01f ? aspect : 0.0f;
}

static void apply(void) {
    if (sAspect > 0.0f) {
        VILockAspectRatio((int)lroundf(sAspect * 1000.0f), 1000);
        /* a hair past the edge: s16 quad coordinates round toward 0 */
        gPortWidescreenEdgeScale = sAspect / GAME_ASPECT + 0.005f;
    } else {
        VIUnlockAspectRatio();
        gPortWidescreenEdgeScale = 1.0f;
    }
}

/* The settings or the window's shape changed what the frame should be. */
void portWidescreenFrame(void) {
    const float aspect = wanted();
    if (fabsf(aspect - sAspect) > 0.001f) {
        sAspect = aspect;
        apply();
    }
}

float portWidescreenAspect(void) {
    return sAspect > 0.0f ? sAspect : 4.0f / 3.0f;
}

int portWidescreenWide(void) {
    return sAspect > 0.0f;
}

void portWidescreenInit(void) {
    portWidescreenEnabled();
    portWidescreenFrame();
}

/* The settings page (settings_aurora.c): on or off while running. */
void portWidescreenSet(int on) {
    portWidescreenEnabled();
    if (sEnabled != (on != 0)) {
        sEnabled = on != 0;
        portWidescreenFrame();
    }
}

/* The cameras mirror mode flips (port/include/port/view.h): the world's
 * (the background pictures, the 3D and its effects and images, the fades,
 * whose wipes close on Mario where he is seen), not the offscreen and shadow
 * passes or the 2D one (the HUD, menus and message windows). */
static int mirrored(int cameraId) {
    switch (cameraId) {
    case CAMERA_BACKGROUND:
    case CAMERA_3D:
    case CAMERA_3D_EFFECTA:
    case CAMERA_3D_IMAGE:
    case CAMERA_3D_EFFECTB:
    case CAMERA_FADE:
    case CAMERA_FADE2:
        return portViewMirrored();
    default:
        return 0;
    }
}

/* ---- Bars per camera angle (port/include/port/widescreen.h) ----
 * In the rooms of kAngleLimits the void past a wall shows only while the
 * camera is near that end of the room; there the widest aspect with no void
 * falls in a line with the camera's x (measured walking through each room at
 * 21:9, its target's x against the widest void-free middle of the picture).
 * Black bars inside the frame cover what is past that, sliding in and out as
 * the camera moves, so the frame keeps its shape and the HUD its place. A
 * camera height of the room that wasn't measured (its at.z elsewhere) keeps
 * the room's limit of kRoomLimits. */

static const struct {
    const char* map;
    float z;            /* the camera target's z in this view (within 12) */
    float la, lb, lmin; /* the left end: widest = la + lb * x, never below lmin (lb 0: none) */
    float ra, rb, rmin; /* the right end, the same */
} kAngleLimits[] = {
    {"eki_01", -13.0f, 4.078f, 0.00969f, 1.97f, 4.176f, -0.01021f, 1.23f},
    {"aji_01", -7.7f, 9.319f, 0.00930f, 1.27f, -1.633f, -0.01180f, 2.16f},
    {"las_00", -19.6f, 3.138f, 0.00808f, 1.32f, 3.333f, -0.00832f, 1.39f},
    {"gon_00", -13.0f, 2.161f, 0.01023f, 1.46f, 2.472f, -0.01379f, 1.45f},
    {"dou_00", -36.1f, 3.010f, 0.01000f, 1.61f, 0.0f, 0.0f, 0.0f},
    {"rsh_00_a", -11.8f, 2.843f, 0.00995f, 1.43f, 2.172f, -0.00878f, 1.19f},
    {"mri_01", 7.0f, 3.810f, 0.00611f, 2.08f, 6.191f, -0.01315f, 1.98f},
    {"rsh_02_a", -183.0f, 0.0f, 0.0f, 0.0f, 2.00f, 0.0f, 2.00f},
};
#define ANGLE_MARGIN 0.06f /* a little inside the void's edge */

/* The view now is one of kAngleLimits' (its entry, or -1). */
static int angleView(void) {
    const CameraEntry* c;
    size_t i;
    if (gp == NULL || gp->inBattle) {
        return -1;
    }
    c = camGetPtr(CAMERA_3D);
    for (i = 0; i < sizeof(kAngleLimits) / sizeof(kAngleLimits[0]); i++) {
        if (strcmp(gp->currentMapName, kAngleLimits[i].map) == 0 && fabsf(c->cameraAt.z - kAngleLimits[i].z) < 12.0f) {
            return (int)i;
        }
    }
    return -1;
}

/* the widest aspect the view shows (0: no limit) */
static float angleLimit(int i) {
    const float x = camGetPtr(CAMERA_3D)->cameraAt.x;
    float w = 100.0f;
    if (kAngleLimits[i].lmin > 0.0f) {
        const float l = kAngleLimits[i].la + kAngleLimits[i].lb * x;
        w = l > kAngleLimits[i].lmin ? l : kAngleLimits[i].lmin;
    }
    if (kAngleLimits[i].rmin > 0.0f) {
        const float r = kAngleLimits[i].ra + kAngleLimits[i].rb * x;
        const float rr = r > kAngleLimits[i].rmin ? r : kAngleLimits[i].rmin;
        w = rr < w ? rr : w;
    }
    return w - ANGLE_MARGIN;
}

static float sMatte = 100.0f; /* the bars' inner edges now (as the edge scale; 100: none) */
static int sWideLog = -1;

static int mattesWanted(void) {
    declare();
    return sAspect > 0.0f && *sSceneLimits && !portViewFreeActive();
}

static void drawMattes(CameraId camId, void* param);

/* Battles: the theatre's proscenium (the wooden frame's outer edges, at
 * x +-BATTLE_FRAME_X, z BATTLE_FRAME_Z) ends a little inside 16:9 at the
 * battle's own view, and past it are the hall's grey walls and floor; the
 * curtain's rise shows them most. The widest aspect inside it, from the 3D
 * camera now (it pans and zooms with the moves: zoomed in, no bars). The z is
 * a guess (the stage's front); the x was measured at the battles' starting
 * view (eye 0 110 750), where it is exact. */
#define BATTLE_FRAME_X 213.5f
#define BATTLE_FRAME_Z 200.0f
static float battleLimit(void) {
    const CameraEntry* c = camGetPtr(CAMERA_3D);
    float w = 100.0f;
    int side;
    for (side = -1; side <= 1; side += 2) {
        Vec p = {side * BATTLE_FRAME_X, 60.0f, BATTLE_FRAME_Z}, v;
        float cx, cw;
        MTXMultVec(c->view, &p, &v);
        cx = c->projection[0][0] * v.x + c->projection[0][1] * v.y + c->projection[0][2] * v.z + c->projection[0][3];
        cw = c->projection[3][0] * v.x + c->projection[3][1] * v.y + c->projection[3][2] * v.z + c->projection[3][3];
        if (cw > 1.0f) {
            const float x = side * cx / cw; /* how far out this side's edge is (clip x, 1: 4:3) */
            const float a = (x - 0.01f) * GAME_ASPECT;
            w = a < w ? a : w;
        }
    }
    return w;
}

/* each field frame (mapDisp): the bars follow the camera */
void portWideMapDrawn(void) {
    const float edge = gPortWidescreenEdgeScale;
    float target = edge;
    int view;
    if (sWideLog < 0) {
        sWideLog = getenv("TTYD_WIDE_LOG") != NULL;
    }
    view = mattesWanted() ? angleView() : -1;
    if (sWideLog && gp != NULL) {
        const CameraEntry* c = camGetPtr(CAMERA_3D);
        printf("[wide] cam %s at %.1f %.1f %.1f eye %.1f %.1f %.1f view %d bars %.3f\n", gp->currentMapName,
               c->cameraAt.x, c->cameraAt.y, c->cameraAt.z, c->cameraPos.x, c->cameraPos.y, c->cameraPos.z, view,
               sMatte > 50.0f ? 0.0f : sMatte * GAME_ASPECT);
        {
            /* the view's own eye (-R^T t) and forward, and fovY: what battle cameras set */
            const float(*v)[4] = c->view;
            const float ex = -(v[0][0] * v[0][3] + v[1][0] * v[1][3] + v[2][0] * v[2][3]);
            const float ey = -(v[0][1] * v[0][3] + v[1][1] * v[1][3] + v[2][1] * v[2][3]);
            const float ez = -(v[0][2] * v[0][3] + v[1][2] * v[1][3] + v[2][2] * v[2][3]);
            printf("[wide] view eye %.1f %.1f %.1f fwd %.3f %.3f %.3f fov %.2f p00 %.3f\n", ex, ey, ez, -v[2][0],
                   -v[2][1], -v[2][2], c->fovY, c->projection[0][0]);
        }
    }
    {
        const float limit = view >= 0 ? angleLimit(view) : gp != NULL && gp->inBattle && mattesWanted() ? battleLimit() : 0.0f;
        float e;
        if (limit <= 0.0f) {
            sMatte = 100.0f;
            return;
        }
        e = limit / GAME_ASPECT;
        if (e < 4.0f / 3.0f / GAME_ASPECT) {
            e = 4.0f / 3.0f / GAME_ASPECT; /* never into the game's 4:3 picture */
        }
        if (e < edge - 0.005f) {
            target = e;
        }
    }
    if (sMatte > edge) {
        sMatte = edge; /* they slide in from the frame's edges */
    }
    /* in fast (the void shows as the camera moves), out gently */
    sMatte += (target - sMatte) * (target < sMatte ? 0.5f : 0.15f);
    if (target >= edge && sMatte > edge - 0.002f) {
        sMatte = 100.0f;
    } else {
        dispEntry(CAMERA_2D, 100000.0f, 3, drawMattes, NULL); /* the first of the 2D camera's */
    }
}

/* the bars: the 2D camera's first draw (under the HUD, over the world) */
static void drawMattes(CameraId camId, void* param) {
    CameraEntry* cam = camGetPtr(CAMERA_2D);
    const float edge = gPortWidescreenEdgeScale;
    const GXColor black = {0, 0, 0, 255};
    float inner, outer;
    int side;
    (void)camId;
    (void)param;
    if (sMatte >= edge - 0.002f || !mattesWanted()) {
        return;
    }
    inner = 304.0f * sMatte;
    outer = 304.0f * edge + 8.0f;
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, black);
    GXSetNumChans(0);
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    GXSetTevColor(GX_TEVREG0, black);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXLoadPosMtxImm(cam->view, 0);
    GXSetCurrentMtx(0);
    GXSetCullMode(GX_CULL_NONE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    for (side = -1; side <= 1; side += 2) {
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(side * inner, 260.0f, 0.0f);
        GXPosition3f32(side * outer, 260.0f, 0.0f);
        GXPosition3f32(side * outer, -260.0f, 0.0f);
        GXPosition3f32(side * inner, -260.0f, 0.0f);
        GXEnd();
    }
}

static f32 sScaleX = 1.0f; /* GXSetProjectionScaleX's now */

void portWidescreenCamera(int cameraId) {
    f32 scale = 1.0f;
    if (sAspect > 0.0f) {
        switch (cameraId) {
        case CAMERA_BACKGROUND: /* full-screen background pictures */
        case CAMERA_FADE:       /* full-screen fades */
        case CAMERA_FADE2:
            break;
        default:
            scale = GAME_ASPECT / sAspect;
            break;
        }
    }
    if (mirrored(cameraId)) {
        scale = -scale;
    }
    if (scale != 1.0f || sScaleX != 1.0f) {
        GXSetProjectionScaleX(scale);
        sScaleX = scale;
    }
}

float portWidescreenScaleX(void) {
    return sScaleX;
}

void portWidescreenCameraDone(void) {
    if (sScaleX < 0.0f) { /* what draws after the cameras is not mirrored */
        GXSetProjectionScaleX(-sScaleX);
        sScaleX = -sScaleX;
    }
}
