/* Gameplay+ battle feedback pop-ups ("battle_popups", port/include/port/gameplus.h):
 * "GUARD!" / "SUPERGUARD!" over Mario or the partner, and a resisted status's
 * icon bouncing off the unit with "Resisted", drawn with the game's own font
 * and status-icon textures for about a second. */
#include "port/gameplus.h"
#include "battle/battle.h"
#include "battle/battle_disp.h"
#include "battle/battle_unit.h"
#include "driver/dispdrv.h"
#include "manager/fontmgr.h"
#include <string.h>

#define POPUP_MAX 8
#define POPUP_FRAMES 60

typedef struct Popup {
    const char* text;
    s32 icon;  /* a status icon's texture (battle_status_icon.c's table), or -1 */
    Vec world; /* where it starts, in the battle's world */
    s32 age;   /* frames shown */
    GXColor color;
} Popup;

static Popup sPopups[POPUP_MAX];

s32 portStatusIconId(s32 statusType); /* battle_status_icon.c */

static void add(const char* text, s32 icon, const Vec* at, GXColor color) {
    s32 i, oldest = 0;
    for (i = 0; i < POPUP_MAX; i++) {
        if (sPopups[i].text == NULL) {
            oldest = i;
            break;
        }
        if (sPopups[i].age > sPopups[oldest].age) {
            oldest = i;
        }
    }
    sPopups[oldest].text = text;
    sPopups[oldest].icon = icon;
    sPopups[oldest].world = *at;
    sPopups[oldest].age = 0;
    sPopups[oldest].color = color;
}

void portPopupGuard(BattleUnit* unit, BattleUnitPart* part, int super) {
    Vec at;
    if (!portGameplus("battle_popups") || unit == NULL) {
        return;
    }
    BtlUnit_GetHitPos(unit, part, &at.x, &at.y, &at.z);
    at.y += 75.0f; /* above the game's own "Nice" / "Great" */
    add(super ? "SUPERGUARD!" : "GUARD!", -1, &at,
        super ? (GXColor){0xFF, 0xD8, 0x30, 0xFF} : (GXColor){0x90, 0xE0, 0xFF, 0xFF});
}

void portPopupResisted(BattleUnit* unit, int statusType) {
    Vec at;
    if (!portGameplus("battle_popups") || unit == NULL) {
        return;
    }
    BtlUnit_GetPos(unit, &at.x, &at.y, &at.z);
    at.x += unit->statusIconOffset[0] * unit->sizeMultiplier;
    at.y += unit->statusIconOffset[1] * unit->sizeMultiplier;
    add("Resisted", portStatusIconId(statusType), &at, (GXColor){0xFF, 0xFF, 0xFF, 0xFF});
}

static void disp(CameraId camId, void* param) {
    s32 i;
    (void)camId;
    (void)param;
    for (i = 0; i < POPUP_MAX; i++) {
        Popup* p = &sPopups[i];
        Vec screen;
        f32 rise, w;
        u8 alpha;
        if (p->text == NULL) {
            continue;
        }
        btlGetScreenPoint(&p->world, &screen);
        /* a hop up, a small bounce, then a fade */
        rise = p->age < 12 ? p->age * 2.5f : 30.0f + (p->age < 20 ? (20 - p->age) * 0.6f : 0.0f);
        alpha = p->age < POPUP_FRAMES - 15 ? 0xFF : (u8)(0xFF * (POPUP_FRAMES - p->age) / 15);
        if (p->icon >= 0) {
            Mtx m, s;
            const f32 tilt = p->age < 24 ? (24 - p->age) * 1.5f : 0.0f; /* knocked off at an angle */
            PSMTXScale(s, 0.8f, 0.8f, 1.0f);
            PSMTXRotRad(m, 'z', 0.017453292f * tilt);
            PSMTXConcat(m, s, m);
            m[0][3] = screen.x + 13.0f + tilt;
            m[1][3] = screen.y + 19.0f + rise;
            m[2][3] = screen.z;
            btlDispTexPlane2(m, (u32)p->icon, (GXColor){0xFF, 0xFF, 0xFF, alpha}, 0);
            screen.y -= 18.0f; /* the word under the icon */
        }
        FontDrawStart_alpha(alpha);
        FontDrawEdge();
        FontDrawColor((GXColor){p->color.r, p->color.g, p->color.b, alpha});
        FontDrawScale(0.8f);
        w = FontGetMessageWidth(p->text) * 0.8f;
        FontDrawString(screen.x - w * 0.5f, screen.y + rise + 12.0f, p->text);
    }
}

void portPopupFrame(void) {
    s32 i, any = 0;
    for (i = 0; i < POPUP_MAX; i++) {
        if (sPopups[i].text != NULL && ++sPopups[i].age >= POPUP_FRAMES) {
            sPopups[i].text = NULL;
        }
        any |= sPopups[i].text != NULL;
    }
    if (any) {
        dispEntry(CAMERA_2D, 902.0f, 1, disp, NULL);
    }
}

void portPopupClear(void) {
    memset(sPopups, 0, sizeof(sPopups));
}
