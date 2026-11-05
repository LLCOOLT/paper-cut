/* The post-game places' and modes' game side (port/include/port/postgame.h):
 * their maps added to the host's (before the mods are read, from
 * modes_aurora.c's portModesRegisterMaps) and one hook on marioStMain for
 * their frames. Nothing happens in them until one is started or turned on. */
#include "port/postgame.h"
#include "port/achievements.h"
#include "port/mod_host.h"
#include "port/hook_list.h"

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void frameHook(void) {
    portTrophyRoomFrame();
    portTowerFrame();
    portAchievementsModesFrame(); /* the modes' achievements (achievements_modes_aurora.c) */
}

void portPostgameInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portTrophyRoomRegisterMap();
    portTowerRegisterMaps();
    portDailyShopInit();
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
}
