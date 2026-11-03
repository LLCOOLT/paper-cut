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

/* The offsets that scale a box (NDC) by s about its middle and move it back
 * inside the frame where it would leave it. */
static void boxOffsets(float s, float x0, float x1, float y0, float y1, float* tx, float* ty) {
    const float edge = portWidescreenEdgeScale(); /* the frame's x edge in the game's NDC */
    *tx = keepInside(s, x0, x1, edge) - s * (x0 + x1) * 0.5f;
    *ty = keepInside(s, y0, y1, 1.0f) - s * (y0 + y1) * 0.5f;
}

static void box(float s, float x0, float x1, float y0, float y1) {
    float tx, ty;
    boxOffsets(s, x0, x1, y0, y1, &tx, &ty);
    GXSetProjectionTransform(s, s, tx, ty);
    sTransformed = 1;
}

static float scaleNow(void) {
    if (sScale == NULL) {
        sScale = portSettingsDeclare("hud_scale", 100, 50, 150);
    }
    return (float)*sScale / 100.0f;
}

/* The battle menus' transform: the command ring about the left edge (it sits
 * over the unit, on the left); a sub-menu's list (its bubble from about x
 * -135, the rows' bar to 255, y -95 to 135 in the 2D units) about its own
 * middle, kept inside the frame, since about the left edge it would leave the
 * right one past ~110 %. */
static void battleMenuXf(int list, float s, float* tx, float* ty) {
    if (list == 0) {
        *tx = (1.0f - s) * -1.0f;
        *ty = 0.0f;
    } else {
        boxOffsets(s, -135.0f / 304.0f, 255.0f / 304.0f, -95.0f / 240.0f, 135.0f / 240.0f, tx, ty);
    }
}

void portHudBattleMenuPoint(int list, float* gx, float* gy) {
    const float s = scaleNow();
    float tx, ty;
    if (s == 1.0f) {
        return;
    }
    battleMenuXf(list, s, &tx, &ty);
    *gx = (*gx / 304.0f - tx) / s * 304.0f;
    *gy = (*gy / 240.0f - ty) / s * 240.0f;
}

static int sBattleMenu; /* a battle menu's draw is under way, with this transform */
static float sBattleTx, sBattleTy;

void portHudBattleHelpWin(int begin, float x, float y, float w, float h) {
    const float s = scaleNow();
    if (!sBattleMenu || s == 1.0f) {
        return;
    }
    if (begin) {
        box(s, x / 304.0f, (x + w) / 304.0f, (y - h) / 240.0f, y / 240.0f);
    } else {
        GXSetProjectionTransform(s, s, sBattleTx, sBattleTy);
    }
}

void portHudDraw(CameraId camId, DispCallback callback, void* param) {
    float s, tx, ty;
    s = scaleNow();
    gPortHudStatusScale = s;
    sBattleMenu = 0;
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
            battleMenuXf(callback != BattleCommandDisplay_ActMenuDisp, s, &tx, &ty);
            GXSetProjectionTransform(s, s, tx, ty);
            sTransformed = 1;
            sBattleMenu = 1;
            sBattleTx = tx;
            sBattleTy = ty;
            return;
        }
    }
    if (sTransformed) {
        GXSetProjectionTransform(1.0f, 1.0f, 0.0f, 0.0f);
        sTransformed = 0;
    }
}
