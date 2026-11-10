/* Ray-traced lighting (Graphics > Ray-traced lighting; port/settings/features/rt_lighting.cpp draws it,
 * screen-space: rays marched against the scene's depth on screen): over the 3D scene, under the HUD, as
 * bloom (camdrv.c camDraw before the 2D camera, and interp_aurora.c's in-between frames). Here the game's side:
 * the 3D camera's projection and viewport loaded again (so Aurora's view of them is the 3D camera's, whatever
 * camera drew last) and the scene's main light for the contact shadows, the paper shadows' lamp blend at the
 * camera's target, turned into view space. Off by default; draw-only. */
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include "driver/camdrv.h"
#include "port/paper.h"
#include "port/settings.h"
#include "port/stereo.h"

/* rt_lighting.cpp: level 1 On, 2 High; light: toward the light in view space (unit), w its strength */
void portRtLightingDraw(int level, const float* light);
int portRtLightingAvailable(void);

void portRtLightingApply(void) {
    static int* sLevel;
    CameraEntry* cam;
    float l[3];
    Vec toLight;
    float light[4];
    if (sLevel == NULL) {
        sLevel = portSettingsDeclare("rt_lighting", 0, 0, 2);
    }
    if (*sLevel <= 0 || !portRtLightingAvailable()) {
        return;
    }
    {
        static int sTold; /* each level, once */
        if (sTold != *sLevel) {
            sTold = *sLevel;
            printf("[rt] lighting level %d\n", *sLevel);
        }
    }
    if (portStereoMode() != PORT_STEREO_OFF) {
        return; /* stereoscopic 3D: twice the cost, and its screen-locked noise would sit on the screen's plane */
    }
    cam = camGetPtr(CAMERA_3D);
    if (cam == NULL || (cam->flags & CAM_FLAG_HIDDEN) || cam->projType != GX_PERSPECTIVE) {
        return;
    }
    GXSetViewport(cam->viewport[0], cam->viewport[1], cam->viewport[2], cam->viewport[3], cam->viewport[4],
                  cam->viewport[5]);
    GXSetProjection(cam->projection, GX_PERSPECTIVE);
    portPaperSceneLight(cam->cameraAt.x, cam->cameraAt.y, cam->cameraAt.z, l);
    toLight.x = -l[0];
    toLight.y = -l[1];
    toLight.z = -l[2];
    PSMTXMultVecSR(cam->view, &toLight, &toLight);
    VECNormalize(&toLight, &toLight);
    light[0] = toLight.x;
    light[1] = toLight.y;
    light[2] = toLight.z;
    light[3] = 1.0f;
    portRtLightingDraw(*sLevel, light);
}
