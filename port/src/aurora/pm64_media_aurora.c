/* Paper Mario 64 enemies in the Pit and Master Quest (port/include/port/pm64_enemy.h):
 * Btl_UnitSetup, before, with nothing else armed, PM64 counterparts armed for
 * the formation's kinds. Offline only, with a linked ROM and "pm64_enemies".
 * And in the story's fights (port/include/port/pm64_foes.h, "pm64_story_enemies"): the picked field
 * enemies' fights, first. */
#include "port/pm64_enemy.h"
#include "port/pm64_media.h"
#include "port/pm64_foes.h"
#include "port/modes.h"
#include "port/versus.h"
#include "port/enemy_variant.h"
#include "port/hook_list.h"
#include "port/master_quest.h"
#include "port/mod_host.h"
#include "port/replay.h"
#include "battle/battle.h"
#include "battle/battle_database_common.h"
#include "battle/battle_unit.h"
#include "driver/npcdrv.h"
#include "mario/mariost.h"
#include <string.h>

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static int unitSetupPre(BOOL* result, BattleWork** wpp) {
    BattleWork* wp = *wpp;
    const NpcBattleInfo* info = wp != NULL && wp->fbatInfo != NULL ? wp->fbatInfo->setup : NULL;
    const BattleGroupSetup* g = info != NULL ? info->groupSetup : NULL;
    const PortEnemyVariant* armed;
    int kinds[PORT_VARIANT_ARMED_MAX], n = 0, i;
    (void)result;
    /* the story's fights (not the play modes' own fights or versus battles) */
    if (g != NULL && gp != NULL && portPm64FoesActive() &&
        (portModeStatus()->mode == PORT_MODE_NONE || portModeStatus()->finished) && !portVersusOn() &&
        portPm64FoesArm(wp)) {
        return 0;
    }
    if (g == NULL || gp == NULL || !portPm64EnemiesActive() || portReplayActive() ||
        portEnemyVariantArmed(&armed) > 0) {
        return 0;
    }
    /* the Pit of 100 Trials, or a Master Quest file */
    if (strncmp(gp->currentMapName, "jon", 3) != 0 && !portMasterQuestOn()) {
        return 0;
    }
    for (i = 0; i < g->numEnemies && n < PORT_VARIANT_ARMED_MAX; i++) {
        if (g->enemyData[i].unitKindParams != NULL) {
            kinds[n++] = (int)g->enemyData[i].unitKindParams->unitType;
        }
    }
    portPm64EnemiesArm(kinds, n, gp->retraceCount * 2654435761u);
    return 0;
}

void portPm64MediaInit(void) {
    portModHostAddHook(0, kSlot_Btl_UnitSetup, (void*)unitSetupPre);
    portPm64FoeUnitsInit(); /* the story's PM64 enemies with no cousin here (port/include/port/pm64_foes.h) */
    portPm64BadgesInit(); /* Paper Mario 64's badges and Rowf (port/include/port/pm64_media.h) */
    portPm64BoardInit();  /* the boss list's board beside him */
    portPm64StoryInit();  /* Paper Mario 64 bosses standing in Rogueport in the story */
    portPm64TouchesInit(); /* the Star Spirits' cameo */
}
