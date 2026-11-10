/* The play modes' game side (port/include/port/playmodes.h): one hook on
 * marioStMain runs each mode's frame. Nothing happens in them until a mode
 * is started or turned on. */
#include "port/playmodes.h"
#include "port/party.h"
#include "port/master_quest.h"
#include "port/battlex.h"
#include "port/pm64.h"
#include "port/pm64_enemy.h"
#include "port/mod_host.h"
#include "port/hook_list.h"

#define PORT_HOOK(index, name) static const int kSlot_##name = index;
PORT_HOOK_LIST
#undef PORT_HOOK

static void frameHook(void) {
    portChaptersFrame();
    portRemixFrame();
    portTricksFrame();
    portGhostFrame();
    portPitCoopFrame();
}

void portPlayModesInit(void) {
    static int sDone;
    if (sDone) {
        return;
    }
    sDone = 1;
    portModHostAddHook(1, kSlot_marioStMain, (void*)frameHook);
    portRemixInit();
    portPartyInit(); /* bingo, the quiz party, Tattle cards, the daily seed (party.h) */
    portMasterQuestInit(); /* after the remix: it remixes the remixed fight */
    portBattlexInit(); /* battle grades, the badge set and merchant, puzzles (port/include/port/battlex.h) */
    portPm64Init(); /* the Paper Mario 64 partners: the corner, their moves (port/include/port/pm64.h) */
    portPm64MediaInit(); /* Paper Mario 64 enemies, badges and touches (port/include/port/pm64_enemy.h) */
}
