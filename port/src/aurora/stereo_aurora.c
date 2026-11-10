/* Stereoscopic 3D (Graphics > Stereoscopic 3D; docs/STEREOSCOPIC_3D.md): the game's side.
 *
 * Each frame the cameras are drawn twice into the same frame (camdrv.c camDraw, and interp_aurora.c's in-between
 * frames), first for the left eye, then for the right:
 * - the left eye is a redraw that changes nothing (interp_aurora.c's portInterpPureBegin: as an in-between frame,
 *   with the per-draw counters stepped as the game's own draw is about to step them, then put back), so the game's
 *   own draw, the right eye, runs on exactly the state it would without stereo, and both show the same moment;
 * - after the left eye the frame is kept and cleared (stereo.cpp), and at the frame's end the two are put
 *   together (portStereoCompose, vi_aurora.c), under the overlays drawn after it (the settings page and such).
 * Each 3D camera (perspective) is moved for the eye along its own x axis, half the eye separation, and its
 * projection skewed so that the plane at the screen distance stays where it was (an off-axis frustum: no
 * keystone, and that plane at screen depth). The 2D cameras (HUD, menus, text, fades) are not moved: identical in
 * both eyes, at screen depth. The render-to-texture cameras (offscreen, shadow) are not moved either: what they
 * make is the same for both eyes. A flat background picture is moved as far as the far distance.
 * Draw-only: the game's state is as without it (online and replays keep each player's own: stereo_ keys). */
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <math.h>
#include <string.h>
#include "driver/camdrv.h"
#include "port/platform.h"
#include "port/settings.h"
#include "port/stereo.h"
#include "port/widescreen.h"

/* the eyes' separation at 100% depth, as a share of the screen distance */
#define MAX_SEPARATION 0.03f

static int* sMode;
static int* sDepth;
static int* sScreen;

static int sEye = -1;     /* the eye being drawn, -1 none */
static int sPureOwn;      /* the left eye's redraw begun here (the game's frame; not an in-between one) */
static float sSeparation; /* half the eyes' separation, in the 3D camera's view units */
static float sDistance;   /* the screen's distance, the same */
static float sP00;        /* the 3D camera's x scale (its projection's [0][0]) */
static unsigned sSaved;   /* the cameras moved for this eye (bits by id) */
static Mtx sView[CAMERA_MAX];
static Mtx44 sProj[CAMERA_MAX];

static void declare(void) {
    if (sMode == NULL) {
        sMode = portSettingsDeclare("stereo_3d", 0, 0, PORT_STEREO_MODES - 1);
        sDepth = portSettingsDeclare("stereo_depth", 50, 0, 100);
        sScreen = portSettingsDeclare("stereo_screen", 100, 50, 200);
    }
}

int portStereoMode(void) {
    declare();
    return *sMode > 0 && *sMode < PORT_STEREO_MODES ? *sMode : PORT_STEREO_OFF;
}

int portStereoEyes(void) {
    if (portStereoMode() == PORT_STEREO_OFF || !portInterpListRedrawable()) {
        return 1; /* (a frame whose draw makes extdrv's textures: the game's own draw only, shown to both eyes) */
    }
    return 2;
}

/* The eyes' separation and the screen's distance, from the 3D camera: the screen at the distance of what the camera
 * looks at (Mario in the field, the stage in battle) times the setting. */
static void measure(void) {
    CameraEntry* cam = camGetPtr(CAMERA_3D);
    float distance = 0.0f;
    if (cam != NULL) {
        Vec at;
        PSMTXMultVec(cam->view, &cam->cameraAt, &at);
        distance = -at.z;
        sP00 = cam->projType == GX_PERSPECTIVE ? cam->projection[0][0] : 0.0f;
    }
    if (!(distance > 1.0f && distance < 100000.0f)) {
        distance = 500.0f; /* (no target in front of the camera) */
    }
    if (!(sP00 > 0.01f && sP00 < 100.0f)) {
        sP00 = 2.5f;
    }
    sDistance = distance * (float)*sScreen / 100.0f;
    sSeparation = sDistance * MAX_SEPARATION * (float)*sDepth / 100.0f;
}

void portStereoEyeBegin(int eye, int eyes) {
    if (eyes < 2) {
        return;
    }
    if (eye == 0) {
        measure();
        if (!portInterpInBetween()) {
            portInterpPureBegin(); /* the game's frame: the left eye a redraw that changes nothing */
            sPureOwn = 1;
        }
    } else {
        portPaperEyeBegin(); /* the paper shadows queued and drawn again */
    }
    sEye = eye;
    sSaved = 0;
}

void portStereoCamera(int cameraId) {
    CameraEntry* cam;
    float e;
    if (sEye < 0 || cameraId < 0 || cameraId >= CAMERA_MAX) {
        return;
    }
    switch (cameraId) {
    case CAMERA_BACKGROUND:
    case CAMERA_3D:
    case CAMERA_3D_EFFECTA:
    case CAMERA_3D_IMAGE:
    case CAMERA_3D_EFFECTB:
    case CAMERA_DEBUG_3D:
        break;
    default:
        return; /* render-to-texture, the HUD, fades: the same in both eyes */
    }
    cam = camGetPtr((CameraId)cameraId);
    if (cam == NULL || (sSaved & (1u << cameraId))) {
        return;
    }
    e = sEye == 0 ? -sSeparation : sSeparation;
    if (portWidescreenScaleX() < 0.0f) {
        e = -e; /* mirror mode: the picture flipped after, the eyes with it */
    }
    if (cam->projType == GX_PERSPECTIVE) {
        sSaved |= 1u << cameraId;
        PSMTXCopy(cam->view, sView[cameraId]);
        memcpy(sProj[cameraId], cam->projection, sizeof(Mtx44));
        /* the eye at x = e in view space; x at the screen's distance stays where it was */
        cam->view[0][3] -= e;
        cam->projection[0][2] -= cam->projection[0][0] * e / sDistance;
    } else if (cameraId == CAMERA_BACKGROUND) {
        /* a flat picture behind everything: as far as far goes (scaled a little, so its edge stays off screen) */
        const float shift = sP00 * e / sDistance, grow = 1.0f + fabsf(shift);
        sSaved |= 1u << cameraId;
        PSMTXCopy(cam->view, sView[cameraId]);
        memcpy(sProj[cameraId], cam->projection, sizeof(Mtx44));
        cam->projection[0][0] *= grow;
        cam->projection[0][3] = cam->projection[0][3] * grow + shift;
    }
}

void portStereoEyeEnd(int eye, int eyes) {
    int id;
    if (eyes < 2) {
        return;
    }
    for (id = 0; id < CAMERA_MAX; id++) {
        if (sSaved & (1u << id)) {
            CameraEntry* cam = camGetPtr((CameraId)id);
            PSMTXCopy(sView[id], cam->view);
            memcpy(cam->projection, sProj[id], sizeof(Mtx44));
        }
    }
    sSaved = 0;
    sEye = -1;
    if (eye == 0) {
        if (sPureOwn) {
            portInterpPureEnd();
            sPureOwn = 0;
        } else {
            portInterpPureRewind(); /* an in-between frame: as it was for the right eye */
        }
        portStereoGpuLeftDone(); /* the left eye kept, the frame cleared for the right */
    }
}

void portStereoCompose(void) {
    const int mode = portStereoMode();
    if (mode != PORT_STEREO_OFF) {
        portStereoGpuCompose(mode);
    }
}
