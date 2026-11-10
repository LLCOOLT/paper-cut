/* Small Paper Mario 64 touches on Aurora (port/include/port/pm64_media.h), draw-only, from the linked ROM:
 * - while Mario does a Special move, one of the seven Star Spirits looks on from the top of the screen,
 *   over the audience, playing its battle sprite's idle; each Special has its own Spirit (setting
 *   "pm64_star_spirits", on);
 * Nothing shows without a ROM. */
#include "port/achievements.h"
#include "port/hook_list.h"
#include "port/mod_host.h"
#include "port/pm64.h"
#include "port/pm64_media.h"
#include "port/pm64_sfx.h"
#include "port/settings.h"
#include "battle/battle.h"
#include "battle/battle_unit.h"
#include "driver/camdrv.h"
#include "driver/dispdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include <stdlib.h>
#include "mario/mariost.h"
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

extern BattleWeapon marioWeapon_Genki0, marioWeapon_Genki1, marioWeapon_BakuGame, marioWeapon_Scissor,
    marioWeapon_ZubaStar, marioWeapon_Deka, marioWeapon_Muki, marioWeapon_Suki;

/* the NPC sprite archive's battle Star Spirits: Eldstar 24, Mamar, Skolar, Muskular, Misstar, Klevar, Kalmar 30 */
enum { ELDSTAR = 24, MAMAR, SKOLAR, MUSKULAR, MISSTAR, KLEVAR, KALMAR };

static int spiritOf(const BattleWeapon* w) {
    if (w == &marioWeapon_Genki0) {
        return ELDSTAR; /* Sweet Treat: Eldstar's Refresh */
    }
    if (w == &marioWeapon_Genki1) {
        return MISSTAR; /* Sweet Feast: Misstar's Smooch */
    }
    if (w == &marioWeapon_BakuGame) {
        return SKOLAR; /* Star Storm */
    }
    if (w == &marioWeapon_Scissor) {
        return MAMAR;
    }
    if (w == &marioWeapon_ZubaStar) {
        return KALMAR;
    }
    if (w == &marioWeapon_Deka) {
        return KLEVAR;
    }
    if (w == &marioWeapon_Muki) {
        return MUSKULAR;
    }
    if (w == &marioWeapon_Suki) {
        return KLEVAR;
    }
    return w != NULL && w->baseSpCost > 0 ? ELDSTAR : -1;
}

static Pm64Anim sAnim;
static int sSpirit = -1, sFade;
static unsigned sGen, sFrame;

static void spiritDisp(CameraId camId, void* param) {
    const unsigned a = (unsigned)(sFade * 255 / 20);
    (void)camId;
    (void)param;
    if (sSpirit < 0 || sAnim.sprite == NULL) {
        return;
    }
    /* (the 2D camera's centre is the screen's; y up) */
    portPm64AnimDraw2D(&sAnim, -1, 215.0f, 95.0f, 1.25f, 0xFFFFFF00u | (a > 255 ? 255 : a));
}

static void frameHook(void) {
    static int* sOn;
    BattleWork* wp = _battleWorkPointer;
    const BattleUnit* unit;
    int want = -1;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("pm64_star_spirits", 1, 0, 1);
    }
    if (*sOn && gp != NULL && gp->inBattle && wp != NULL && portPm64Linked() != PM64_NONE) {
        const s32 act = BattleGetSeq(wp, SEQ_ACT);
        unit = BattleGetUnitPtr(wp, wp->activeUnitId);
        if (unit != NULL && unit->currentKind == UNIT_MARIO && act >= 0x6000006 && act <= 0x6000007 /* (its attack running) */) {
            want = spiritOf(wp->weaponTargets.weapon);
        }
    }
    if (want >= 0 && (want != sSpirit || sGen != portPm64Generation())) {
        sSpirit = want;
        sGen = portPm64Generation();
        sFrame = gp->retraceCount;
        portPm64AnimStart(&sAnim, portPm64NpcSprite(want), 1); /* Idle */
    }
    if (want >= 0) {
        if (sFade == 0) {
            portPm64SfxStarSpirit(); /* as it appears (port/include/port/pm64_sfx.h) */
            portAchievementCounterBit("pm64_spirits", want - ELDSTAR); /* (port/include/port/achievements.h) */
        }
        sFade = sFade < 20 ? sFade + 1 : 20;
    } else if (sFade > 0) {
        sFade--;
    }
    if (sSpirit < 0 || sFade == 0 || gp == NULL) {
        sSpirit = sFade == 0 ? -1 : sSpirit;
        return;
    }
    if (sFrame != gp->retraceCount) {
        const unsigned d = gp->retraceCount - sFrame;
        portPm64AnimStep(&sAnim, (float)(d > 4 ? 4 : d));
        sFrame = gp->retraceCount;
    }
    dispEntry(CAMERA_2D, 450.0f, 1, spiritDisp, NULL);
}

void portPm64TouchesInit(void) {
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
