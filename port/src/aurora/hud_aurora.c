/* HUD scale: port/include/port/hud.h. */
#include <aurora/display.h>
#include "driver/windowdrv.h"
#include "port/hud.h"
#include "port/settings.h"
#include "wide_x.h"

/* battle/battle_seq_command.c: the menus' draws (not the target cursors,
 * which point at the units) */
void BattleCommandDisplay_ActMenuDisp(CameraId camId, void* param);
void BattleCommandDisplay_WeaponSelectMenuDisp(CameraId camId, void* param);
void BattleCommandDisplay_OperationMenuDisp(CameraId camId, void* param);
void BattleCommandDisplay_ChangePartySelectMenuDisp(CameraId camId, void* param);
void BattleCommandDisplay_MultiItemMenuDisp(CameraId camId, void* param);
void BattleCommandDisplay_WeaponMultiItemCancelCheckDisp(CameraId camId, void* param);

static int* sScale; /* settings: hud_scale, % */
static int sTransformed;

static int isBattleMenu(DispCallback cb) {
    return cb == BattleCommandDisplay_ActMenuDisp || cb == BattleCommandDisplay_WeaponSelectMenuDisp ||
           cb == BattleCommandDisplay_OperationMenuDisp || cb == BattleCommandDisplay_ChangePartySelectMenuDisp ||
           cb == BattleCommandDisplay_MultiItemMenuDisp || cb == BattleCommandDisplay_WeaponMultiItemCancelCheckDisp;
}

/* Scaled by s about (px, py) in the game's NDC (the 4:3 frame: x -1..1 over
 * its 608 units, y -1..1 over 480, up). */
static void about(float s, float px, float py) {
    GXSetProjectionTransform(s, s, (1.0f - s) * px, (1.0f - s) * py);
    sTransformed = 1;
}

/* The middle of [lo, hi] once scaled by s, kept inside [-edge, edge]. */
static float keepInside(float s, float lo, float hi, float edge) {
    const float c = (lo + hi) * 0.5f, half = (hi - lo) * 0.5f * s;
    if (half >= edge) {
        return 0.0f;
    }
    return c < -edge + half ? -edge + half : c > edge - half ? edge - half : c;
}

/* A box (NDC) scaled by s about its middle, moved back inside the frame
 * where it would leave it. */
static void box(float s, float x0, float x1, float y0, float y1) {
    const float edge = portWidescreenEdgeScale(); /* the frame's x edge in the game's NDC */
    const float cx = keepInside(s, x0, x1, edge), cy = keepInside(s, y0, y1, 1.0f);
    GXSetProjectionTransform(s, s, cx - s * (x0 + x1) * 0.5f, cy - s * (y0 + y1) * 0.5f);
    sTransformed = 1;
}

void portHudDraw(CameraId camId, DispCallback callback, void* param) {
    float s;
    if (sScale == NULL) {
        sScale = portSettingsDeclare("hud_scale", 100, 50, 150);
    }
    s = (float)*sScale / 100.0f;
    gPortHudStatusScale = s;
    if (camId == CAMERA_2D && callback != NULL && s != 1.0f) {
        if (portHudIsStatusBar(callback)) {
            about(s, 0.0f, 1.0f); /* the top of the screen */
            return;
        }
        if (portHudIsMessage(callback) && param != NULL) {
            /* the window's middle (its x, y are the top left in the 2D
             * camera's units, centred; its "height" is the width) */
            const WindowEntry* w = param;
            box(s, w->x / 304.0f, (w->x + w->height) / 304.0f, (w->y - w->width) / 240.0f, w->y / 240.0f);
            return;
        }
        if (isBattleMenu(callback)) {
            about(s, -1.0f, 0.0f); /* the left edge, where the lists are */
            return;
        }
    }
    if (sTransformed) {
        GXSetProjectionTransform(1.0f, 1.0f, 0.0f, 0.0f);
        sTransformed = 0;
    }
}
