/* The partner wheel's game side (port/settings/features/partner_wheel.cpp):
 * who can be chosen now. The choice itself goes into the controller's input
 * as the second screen's partner tap (port/include/port/companion.h), and
 * the field takes it where the Start menu could open (win_main.c,
 * portQuickSwapPoll), as it takes any partner switch. */
#include "port/gameplus.h"
#include "driver/seqdrv.h"
#include "mario/mario.h"
#include "mario/mario_party.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

int portPartnerWheelList(int* ids, int max, int* current) {
    PouchData* pouch;
    MarioWork* mario;
    int i, n = 0;
    *current = 0;
    if (gp == NULL || seqGetSeq() != SEQ_GAME || gp->inBattle || (mario = marioGetPtr()) == NULL ||
        mario->characterId != 0 || (pouch = pouchGetPtr()) == NULL) {
        return 0;
    }
    *current = marioGetParty();
    for (i = 0; i < PORT_PARTNER_MENU_COUNT && n < max; i++) {
        const unsigned flags = pouch->partyData[gPortPartnerMenuOrder[i]].flags;
        if ((flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY)) {
            ids[n++] = gPortPartnerMenuOrder[i];
        }
    }
    return n;
}

/* the field could take a switch now (no scene, no menu) */
int portPartnerWheelFieldFree(void) {
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !marioCheckMenuDisable();
}
