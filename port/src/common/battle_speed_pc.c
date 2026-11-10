/* Faster battles (port/include/port/battle_speed.h). The decision is made
 * once per battle frame from the battle's own state, so it is the same for
 * the same run (the guard windows it widens are game logic); the backend's
 * frame pacing then follows it (speed_aurora.c; headless has no pacing).
 *
 * The player acts while:
 * - an action command is set up or running (actionCommands.maincb: from
 *   BattleActionCommandSetup until its main callback finishes);
 * - a Stylish move's window is being checked (BattleAcrobatMain);
 * - the command menu is open (BATTLE_FLAG_COMMAND_INPUT), so the cursor and
 *   the menus keep their pace;
 * - a Star Special (a move costing Star Power) is under way: its minigames
 *   read the pad themselves.
 * 1x then, and for HOLD frames after, so back-to-back commands (a Power
 * Bounce's, a Stylish after a jump) don't flicker between speeds.
 *
 * $TTYD_BATTLE_SPEED_LOG (tests): each change of speed and each guard's
 * window logged. */
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "driver/seqdrv.h"
#include "port/battle_speed.h"
#include "port/settings.h"
#include "port/statesync.h"
#include <dolphin/vi.h>
#include <stdio.h>
#include <stdlib.h>

#define HOLD 20 /* frames at 1x after the player last acted */

static const int kPct[3] = {100, 150, 200};
static int* sSetting;
static int sMarked;
static int sCalm; /* frames since the player last acted */
static int sPct = 100;
static int sLog = -1;

static int setting(void) {
    if (sSetting == NULL) {
        sSetting = portSettingsDeclare("battle_speed", 0, 0, 2);
    }
    return *sSetting;
}

static int logging(void) {
    if (sLog < 0) {
        sLog = getenv("TTYD_BATTLE_SPEED_LOG") != NULL;
    }
    return sLog;
}

void portBattleSpeedMark(void) {
    sMarked = 1;
}

void portBattleSpeedUpdate(BattleWork* wp) {
    const BattleWeapon* w = wp->weaponTargets.weapon;
    const int acting = wp->actionCommands.maincb != NULL || sMarked || (wp->flags & BATTLE_FLAG_COMMAND_INPUT) ||
                       ((wp->flags & BATTLE_FLAG_ACT_DECIDED) && w != NULL && w->baseSpCost > 0);
    const int s = setting();
    int pct;
    sMarked = 0;
    sCalm = acting ? 0 : sCalm < HOLD ? sCalm + 1 : HOLD;
    pct = s > 0 && s < 3 && sCalm >= HOLD && !portStateSyncSharedBattle() ? kPct[s] : 100;
    if (pct != sPct && logging()) {
        printf("[battlespeed] retrace %u: %d%%%s\n", (unsigned)VIGetRetraceCount(), pct,
               pct == 100 ? (acting ? " (the player acts)" : "") : "");
        fflush(stdout);
    }
    sPct = pct;
}

int portBattleSpeedPct(void) {
    return seqGetSeq() == SEQ_BATTLE ? sPct : 100;
}

int portBattleSpeedOn(void) {
    return seqGetSeq() == SEQ_BATTLE && setting() > 0 && !portStateSyncSharedBattle();
}

int portBattleSpeedFrames(int frames) {
    const int pct = portBattleSpeedPct();
    int n;
    if (pct <= 100) {
        return frames;
    }
    n = (frames * pct + 50) / 100;
    if (n > 30) {
        n = 30;
    }
    if (logging()) {
        printf("[battlespeed] retrace %u: a guard window of %d frames is %d at %d%%\n", (unsigned)VIGetRetraceCount(),
               frames, n, pct);
        fflush(stdout);
    }
    return n;
}
