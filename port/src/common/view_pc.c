/* The free camera and mirror mode: port/include/port/view.h. */
#include "port/view.h"
#include "port/online.h"
#include "port/platform.h"
#include "port/replay.h"
#include "port/settings.h"
#include "port/widescreen.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/camdrv.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mariost.h"
#include <dolphin/mtx.h>
#include <dolphin/os.h>
#include <math.h>

#define DEG(d) ((d) * 0.017453292f)

/* The free camera's reach: to either side, and with the stick up (in) and
 * down (out and higher). */
#define FREE_YAW DEG(35.0f)
#define FREE_PITCH_IN DEG(-6.0f)
#define FREE_PITCH_OUT DEG(12.0f)
#define FREE_ZOOM_IN 0.6f
#define FREE_ZOOM_OUT 1.45f
/* how fast it follows the stick and eases back (per second) */
#define FREE_EASE 7.0f
/* the C-stick's dead zone and full tilt */
#define STICK_DEAD 12
#define STICK_FULL 72

static int* sFreeCam;
static int* sMirror;
static int* sMouseCam;    /* mouse_camera: the mouse turns the free camera */
static int* sMouseSpeed;  /* mouse_camera_speed, 1-10 */
static int* sMouseInvert; /* mouse_camera_invert: up and down turned around */

/* the mouse's part of the free camera, as the stick's axes (-1..1): it stays
 * where the mouse leaves it (mouse-look), and eases back with the stick's
 * when Mario hasn't the controls */
static float sMouseX, sMouseY;

/* the local controller's C-stick, as read */
static s8 sStickX, sStickY;

/* the free camera now: yaw and pitch (radians), zoom (the eye's distance
 * from Mario, times) */
static float sYaw, sPitch, sZoom = 1.0f;
static OSTime sLast;

/* photo mode's camera, when it's on (it sets the view outright: no easing),
 * and where Mario is in its picture (0-1 from the top left) */
static struct {
    int active, hideHud;
    float yaw, pitch, zoom;
    float focusX, focusY;
} sPhoto = {0, 0, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f};

/* the cameras it turns: the 3D one and the two effect cameras that follow it */
static const CameraId kCams[] = {CAMERA_3D, CAMERA_3D_EFFECTA, CAMERA_3D_EFFECTB};
#define CAM_COUNT (int)(sizeof(kCams) / sizeof(kCams[0]))
static struct {
    int active;
    Mtx view[CAM_COUNT];
    Vec pos[CAM_COUNT], at[CAM_COUNT];
} sSaved;

static void declare(void) {
    if (sFreeCam == NULL) {
        sFreeCam = portSettingsDeclare("free_camera", 0, 0, 1);
        sMirror = portSettingsDeclare("mirror_mode", 0, 0, 1);
        sMouseCam = portSettingsDeclare("mouse_camera", 1, 0, 1);
        sMouseSpeed = portSettingsDeclare("mouse_camera_speed", 5, 1, 10);
        sMouseInvert = portSettingsDeclare("mouse_camera_invert", 0, 0, 1);
    }
}

int portViewMirrored(void) {
    declare();
    return *sMirror != 0;
}

/* A map in the field, not a battle. */
static int inField(void) {
    return gp != NULL && !gp->inBattle && seqGetSeq() == SEQ_GAME;
}

/* A battle on screen (photo mode's other place). */
static int inBattleScene(void) {
    return gp != NULL && gp->inBattle && seqGetSeq() == SEQ_BATTLE && _battleWorkPointer != NULL;
}

/* Where photo mode works: the field, or a battle. */
static int photoPlace(void) {
    return inField() || inBattleScene();
}

/* photo mode's subject: Mario's middle (in a battle, his battle self's) */
static int photoSubject(Vec* out) {
    if (inBattleScene()) {
        BattleUnit* unit = BattleGetMarioPtr(_battleWorkPointer);
        if (unit == NULL) {
            return 0;
        }
        *out = unit->position;
    } else {
        if (marioGetPtr() == NULL) {
            return 0;
        }
        *out = marioGetPtr()->position;
    }
    out->y += 25.0f;
    return 1;
}

/* Mario walks where the stick says: no menu, scene or message holds him. */
static int fieldControl(void) {
    return inField() && marioStGetSystemLevel() == 0 && marioChkKey() && marioChkCtrl();
}

static float clamp1(float v) {
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

int portViewMouseLook(void) {
    declare();
    return *sFreeCam && *sMouseCam && !sPhoto.active && fieldControl();
}

void portViewMouse(float dx, float dy) {
    /* at 5, about 400 pixels from the middle to the free camera's reach */
    const float k = (float)*sMouseSpeed * 0.0005f;
    if (!portViewMouseLook()) {
        return;
    }
    sMouseX = clamp1(sMouseX + dx * k);
    /* up: in and lower, as the stick up (inverted: as the stick down) */
    sMouseY = clamp1(sMouseY + (*sMouseInvert ? dy : -dy) * k);
}

void portViewMouseCentre(void) {
    sMouseX = sMouseY = 0.0f;
}

/* A stick's axis as -1..1, past its dead zone. */
static float axis(s8 v) {
    float f;
    if (v > -STICK_DEAD && v < STICK_DEAD) {
        return 0.0f;
    }
    f = (float)(v > 0 ? v - STICK_DEAD : v + STICK_DEAD) / (float)(STICK_FULL - STICK_DEAD);
    return f > 1.0f ? 1.0f : f < -1.0f ? -1.0f : f;
}

static void mirrorPad(PADStatus* pad) {
    const u16 lr = pad->button & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
    pad->stickX = pad->stickX == -128 ? 127 : (s8)-pad->stickX;
    pad->button &= ~(PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
    if (lr & PAD_BUTTON_LEFT) {
        pad->button |= PAD_BUTTON_RIGHT;
    }
    if (lr & PAD_BUTTON_RIGHT) {
        pad->button |= PAD_BUTTON_LEFT;
    }
}

void portViewPad(PADStatus* pad) {
    int i;
    declare();
    sStickX = pad[0].err == PAD_ERR_NONE ? pad[0].substickX : 0;
    sStickY = pad[0].err == PAD_ERR_NONE ? pad[0].substickY : 0;
    /* the game's own state decides (the same on both sides of an online
     * game); the controls are turned around before the game sees them */
    if (*sMirror && fieldControl()) {
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            if (pad[i].err == PAD_ERR_NONE) {
                mirrorPad(&pad[i]);
            }
        }
    }
}

/* v toward to, by k (0..1) */
static float ease(float v, float to, float k) {
    return v + (to - v) * k;
}

/* p moved around the pivot by rot, its distance from it times zoom */
static Vec around(const Vec* p, const Vec* pivot, Mtx rot, float zoom) {
    Vec d = {p->x - pivot->x, p->y - pivot->y, p->z - pivot->z};
    MTXMultVecSR(rot, &d, &d);
    d.x = pivot->x + d.x * zoom;
    d.y = pivot->y + d.y * zoom;
    d.z = pivot->z + d.z * zoom;
    return d;
}

static void viewBegin(void);

/* photo mode: Mario's middle through the 3D camera (as it is drawn), for its depth of field */
static void photoFocus(void) {
    CameraEntry* cam = camGetPtr(CAMERA_3D);
    Vec pivot, v;
    float t, x, y;
    if (cam == NULL || cam->projType != GX_PERSPECTIVE || !photoSubject(&pivot)) {
        return;
    }
    MTXMultVec(cam->view, &pivot, &v);
    if (v.z > -1.0f) {
        return; /* (behind the eye) */
    }
    t = tanf(DEG(cam->fovY) * 0.5f);
    x = v.x / (-v.z * t * cam->aspect);
    y = v.y / (-v.z * t);
    if (portWidescreenWide()) {
        x *= cam->aspect / portWidescreenAspect(); /* the projection narrowed to show more */
    }
    if (*sMirror) {
        x = -x;
    }
    x = 0.5f + 0.5f * x;
    y = 0.5f - 0.5f * y;
    sPhoto.focusX = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    sPhoto.focusY = y < 0.0f ? 0.0f : y > 1.0f ? 1.0f : y;
}

void portViewBegin(void) {
    viewBegin();
    if (sPhoto.active && photoPlace()) {
        photoFocus();
    }
}

static void viewBegin(void) {
    /* (a draw: read without moving the lockstep clock, which moves 1 us a read) */
    const OSTime now = (OSTime)portGameClockPeek();
    float dt, k, x, y;
    float toYaw = 0.0f, toPitch = 0.0f, toZoom = 1.0f;
    CameraEntry* cam3d;
    Vec pivot, d, right;
    Mtx yaw, pitch, rot;
    int i;
    declare();
    sSaved.active = FALSE;
    dt = sLast != 0 ? (float)OSTicksToMicroseconds(now - sLast) / 1000000.0f : 0.0f;
    sLast = now;
    if (dt < 0.0f || dt > 0.1f) {
        dt = 0.1f;
    }
    if (sPhoto.active && photoPlace()) {
        sYaw = sPhoto.yaw;
        sPitch = sPhoto.pitch;
        sZoom = sPhoto.zoom;
    } else if (!*sFreeCam || !inField()) {
        sYaw = sPitch = 0.0f; /* battles, scenes between maps: the game's own at once */
        sZoom = 1.0f;
        sMouseX = sMouseY = 0.0f;
        return;
    }
    if (sPhoto.active) {
        /* (set outright above) */
    } else if (fieldControl()) {
        if (!*sMouseCam) {
            sMouseX = sMouseY = 0.0f;
        }
        x = clamp1(axis(sStickX) + sMouseX);
        y = clamp1(axis(sStickY) + sMouseY);
        if (*sMirror) {
            x = -x; /* the picture's right */
        }
        toYaw = x * FREE_YAW;
        toPitch = y > 0.0f ? y * FREE_PITCH_IN : -y * FREE_PITCH_OUT;
        toZoom = y > 0.0f ? 1.0f + y * (FREE_ZOOM_IN - 1.0f) : 1.0f - y * (FREE_ZOOM_OUT - 1.0f);
    } else {
        sMouseX = sMouseY = 0.0f; /* a scene or a menu: back to the game's camera, as the stick's */
    }
    if (!sPhoto.active) {
        k = 1.0f - expf(-dt * FREE_EASE);
        sYaw = ease(sYaw, toYaw, k);
        sPitch = ease(sPitch, toPitch, k);
        sZoom = ease(sZoom, toZoom, k);
    }
    if (fabsf(sYaw) < 0.0005f && fabsf(sPitch) < 0.0005f && fabsf(sZoom - 1.0f) < 0.0005f) {
        return;
    }
    cam3d = camGetPtr(CAMERA_3D);
    if (cam3d->flags & CAM_FLAG_HIDDEN || cam3d->projType != GX_PERSPECTIVE) {
        return;
    }

    /* around Mario's middle: the yaw about the world's up, the pitch about
     * the camera's right (in a battle, round the point the battle camera
     * looks at: the stage, Mario is off to one side) */
    if (inBattleScene()) {
        pivot = cam3d->cameraAt;
    } else if (!photoSubject(&pivot)) {
        return;
    }
    d.x = cam3d->cameraPos.x - pivot.x;
    d.z = cam3d->cameraPos.z - pivot.z;
    right.x = d.z;
    right.y = 0.0f;
    right.z = -d.x;
    MTXRotRad(yaw, 'y', sYaw);
    if (right.x * right.x + right.z * right.z > 1.0f) {
        PSMTXRotAxisRad(pitch, &right, -sPitch); /* positive: the eye higher */
        MTXConcat(yaw, pitch, rot);
    } else {
        MTXCopy(yaw, rot); /* looking straight down: no pitch to give */
    }

    for (i = 0; i < CAM_COUNT; i++) {
        CameraEntry* cam = camGetPtr(kCams[i]);
        Mtx look, inv, moved;
        Vec pos = around(&cam->cameraPos, &pivot, rot, sZoom);
        Vec at = around(&cam->cameraAt, &pivot, rot, 1.0f);
        MTXCopy(cam->view, sSaved.view[i]);
        sSaved.pos[i] = cam->cameraPos;
        sSaved.at[i] = cam->cameraAt;
        /* the game's view (its roll, offset and zoom on Mario kept) looking
         * from the moved eye: view * lookAt^-1 * lookAt' */
        MTXLookAt(look, &cam->cameraPos, &cam->cameraUp, &cam->cameraAt);
        if (!MTXInverse(look, inv)) {
            continue;
        }
        MTXLookAt(moved, &pos, &cam->cameraUp, &at);
        MTXConcat(cam->view, inv, cam->view);
        MTXConcat(cam->view, moved, cam->view);
        cam->cameraPos = pos;
        cam->cameraAt = at;
    }
    sSaved.active = TRUE;
}

int portViewFreeActive(void) {
    return fabsf(sYaw) >= 0.0005f || fabsf(sPitch) >= 0.0005f || fabsf(sZoom - 1.0f) >= 0.0005f;
}

void portViewEnd(void) {
    int i;
    if (!sSaved.active) {
        return;
    }
    for (i = 0; i < CAM_COUNT; i++) {
        CameraEntry* cam = camGetPtr(kCams[i]);
        MTXCopy(sSaved.view[i], cam->view);
        cam->cameraPos = sSaved.pos[i];
        cam->cameraAt = sSaved.at[i];
    }
    sSaved.active = FALSE;
}

/* --- photo mode --- */

int portPhotoAvailable(void) {
    return photoPlace() && !portOnlineActive() && !portReplayActive() && (inBattleScene() || marioGetPtr() != NULL);
}

void portPhotoSet(int active, float yaw, float pitch, float zoom, int hideHud) {
    sPhoto.active = active;
    sPhoto.yaw = yaw;
    sPhoto.pitch = pitch;
    sPhoto.zoom = zoom;
    sPhoto.hideHud = active && hideHud;
}

/* the Start menu's Photo mode row: it closes the menu (the menu draws over a
 * picture of the field, not the field), photo mode once Mario has the controls */
static int sPhotoAsked;

void portPhotoRequest(void) {
    sPhotoAsked = 1;
}

int portPhotoTakeRequest(void) {
    if (sPhotoAsked && fieldControl()) {
        sPhotoAsked = 0;
        return 1;
    }
    return 0;
}

int portPhotoHideHud(void) {
    return sPhoto.hideHud;
}

void portPhotoFocus(float* x, float* y) {
    *x = sPhoto.focusX;
    *y = sPhoto.focusY;
}
