/* The partner wheel's game side (port/settings/features/partner_wheel.cpp):
 * who can be chosen now. The choice itself goes into the controller's input
 * as the second screen's partner tap (port/include/port/companion.h), and
 * the field takes it where the Start menu could open (win_main.c,
 * portQuickSwapPoll), as it takes any partner switch. */
#include "port/gameplus.h"
#include "port/pm64.h"
#include "port/player_menu.h"
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
        /* (Ms. Mowz's slot held by Watt alone: only he is there, below) */
        if ((flags & POUCH_PARTY_FLAG_JOINED) && !(flags & POUCH_PARTY_FLAG_AWAY) &&
            !portPm64SlotStandIn(gPortPartnerMenuOrder[i])) {
            ids[n++] = gPortPartnerMenuOrder[i];
        }
    }
    /* the Paper Mario 64 partners met, after them (port/include/port/pm64.h) */
    for (i = 1; i < PM64_PARTNER_COUNT && n < max; i++) {
        if (portPm64PartnerAvailable(i)) {
            ids[n++] = PM64_WHEEL_BASE + i;
        }
    }
    if (portPm64PartnerOn(*current) > 0) {
        *current = PM64_WHEEL_BASE + portPm64PartnerOn(*current);
    }
    return n;
}

/* the field could take a switch now (no scene, no menu) */
int portPartnerWheelFieldFree(void) {
    const int guest = portPlayerMenuWheelField(); /* (an online co-op guest playing the partner: the host's field) */
    if (guest >= 0) {
        return guest;
    }
    return gp != NULL && seqGetSeq() == SEQ_GAME && !gp->inBattle && !marioCheckMenuDisable();
}

const char* portPartnerWheelWhyNot(void) {
    const MarioWork* mario = marioGetPtr();
    if (gp != NULL && gp->inBattle) {
        return "Quick swap: not in battle.";
    }
    if (gp == NULL || seqGetSeq() != SEQ_GAME || mario == NULL) {
        return "Quick swap: only in the field.";
    }
    if (mario->characterId != 0) {
        return "Quick swap: only Mario has partners.";
    }
    if (!portPartnerWheelFieldFree()) {
        return "Quick swap: not right now.";
    }
    return "Quick swap: no partner to swap to.";
}
