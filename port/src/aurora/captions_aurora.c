/* Captions for sound cues (port/include/port/captions.h): where a cue was
 * heard, from the 3D camera, for the page's table (port/settings/features/
 * accessibility.cpp). The projection is worked out here from the camera's own
 * matrices, as getScreenPoint does, but without touching GX's state: a sound
 * may start in the middle of a frame's drawing. Reads only. */
#include <dolphin/mtx.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/camdrv.h"
#include "port/captions.h"
#include "port/companion_menu.h"
#include "port/view.h"
#include "wide_x.h"

/* pos through the 3D camera: on screen, or out of sight to one side */
static int whereGame(const float* pos);

/* mirror mode: the sides as the flipped picture shows them */
static int where(const float* pos) {
    const int at = whereGame(pos);
    if (portViewMirrored()) {
        return at == PORT_CAPTION_OFF_LEFT ? PORT_CAPTION_OFF_RIGHT : at == PORT_CAPTION_OFF_RIGHT ? PORT_CAPTION_OFF_LEFT : at;
    }
    return at;
}

static int whereGame(const float* pos) {
    CameraEntry* camera = camGetPtr(CAMERA_3D);
    float v[3], clip[4];
    int i;
    if (camera == NULL) {
        return PORT_CAPTION_NO_POS;
    }
    for (i = 0; i < 3; i++) {
        v[i] = camera->view[i][0] * pos[0] + camera->view[i][1] * pos[1] + camera->view[i][2] * pos[2] +
               camera->view[i][3];
    }
    for (i = 0; i < 4; i++) {
        clip[i] = camera->projection[i][0] * v[0] + camera->projection[i][1] * v[1] +
                  camera->projection[i][2] * v[2] + camera->projection[i][3];
    }
    if (camera->projType != GX_PERSPECTIVE) {
        clip[3] = 1.0f;
    }
    if (clip[3] <= 0.001f) {
        /* behind the camera: its side in the camera's own x */
        return v[0] < 0.0f ? PORT_CAPTION_OFF_LEFT : v[0] > 0.0f ? PORT_CAPTION_OFF_RIGHT : PORT_CAPTION_OFF_SCREEN;
    }
    {
        /* the 608 x 480 frame's units, x right and y up from its centre; wider in widescreen */
        const float x = clip[0] / clip[3] * 304.0f, y = clip[1] / clip[3] * 240.0f;
        const float edge = 304.0f * portWidescreenEdgeScale();
        if (x < -edge) {
            return PORT_CAPTION_OFF_LEFT;
        }
        if (x > edge) {
            return PORT_CAPTION_OFF_RIGHT;
        }
        if (y < -240.0f || y > 240.0f) {
            return PORT_CAPTION_OFF_SCREEN;
        }
    }
    return PORT_CAPTION_ON_SCREEN;
}

/* the DualSense's speaker (port/settings/features/dualsense_speaker.cpp) hears
 * every cue too */
void (*gPortSfxHeard)(const char* name);

void portCaptionSfx(const char* name, const float* pos) {
    int offScreenOnly = 0, at, entry;
    if (gPortSfxHeard != NULL && name != NULL) {
        gPortSfxHeard(name);
    }
    if (gPortCompanionMenu || name == NULL) {
        return;
    }
    entry = portCaptionLookup(name, &offScreenOnly);
    if (entry < 0) {
        return;
    }
    at = pos != NULL ? where(pos) : PORT_CAPTION_NO_POS;
    if (offScreenOnly && at != PORT_CAPTION_OFF_LEFT && at != PORT_CAPTION_OFF_RIGHT && at != PORT_CAPTION_OFF_SCREEN) {
        return; /* a door or switch the player can see */
    }
    portCaptionPush(entry, at);
}

/* a positioned sound's pan (pmario_sound.c calc3D), for the tests: $TTYD_CAPTION_LOG
 * logs each new value (a held sound's same pan once) */
void portCaptionPan(int pan) {
    static int sLog = -1, sLast = -1;
    if (sLog < 0) {
        sLog = getenv("TTYD_CAPTION_LOG") != NULL;
    }
    if (sLog && pan != sLast) {
        sLast = pan;
        printf("[caption] pan %d\n", pan);
        fflush(stdout);
    }
}
