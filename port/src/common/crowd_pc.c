/* Crowd Control's commands done in the battle (port/include/port/crowd.h):
 * through the audience's own functions, as the crowd does on its own. The
 * chat thread leaves one command at a time; the battle frame takes it. */
#include "port/crowd.h"
#include "port/online.h"
#include "port/replay.h"
#include "battle/battle.h"
#include "battle/battle_audience.h"
#include "battle/battle_break_slot.h"
#include "mario/mario_pouch.h"
#include "system.h"
#include <stdio.h>

static int sWaiting = -1; /* the command waiting, -1 none (__atomic: the chat thread sets it) */
static unsigned sBattleFrames;

void portCrowdPush(int command) {
    int none = -1;
    if (command >= 0 && command < PORT_CROWD_COUNT && !portOnlineActive() && !portReplayActive()) { /* online: one game's chat alone */
        __atomic_compare_exchange_n(&sWaiting, &none, command, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
}

void portCrowdPushGame(int command) {
    int none = -1;
    if (command >= 0 && command < PORT_CROWD_COUNT && !portReplayActive()) {
        __atomic_compare_exchange_n(&sWaiting, &none, command, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
}

unsigned portCrowdBattleFrames(void) {
    return __atomic_load_n(&sBattleFrames, __ATOMIC_RELAXED);
}

/* Someone in the stands holds up item to throw (BattleAudienceItemOn, as the
 * crowd's own random throws pick a seat). */
static void crowdThrow(u16 item) {
    s32 tries;
    for (tries = 0; tries < 200; tries++) {
        if (BattleAudienceItemOn(irand(200), item, 100) == TRUE) {
            return;
        }
    }
}

static void crowdStarPower(s32 delta) {
    PouchData* pouch = pouchGetPtr();
    s32 sp = pouch->currentSP + delta;
    if (sp > pouch->maximumSP) {
        sp = pouch->maximumSP;
    }
    if (sp < 0) {
        sp = 0;
    }
    pouch->currentSP = (s16)sp;
}

/* the Bingo slot between spins: no spin running, its message done */
static int slotIdle(void) {
    BreakSlotWork* slot = BattleBreakSlotGetPtr();
    return slot->phase == 0 && slot->evt == NULL;
}

/* the command waits for a later frame (unless a new one came meanwhile) */
static void requeue(int command) {
    int none = -1;
    __atomic_compare_exchange_n(&sWaiting, &none, command, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

void portCrowdBattleFrame(void) {
    static const u16 kGifts[] = {ITEM_MUSHROOM, ITEM_HONEY_SYRUP, ITEM_FIRE_FLOWER};
    BattleAudience* audience = BattleAudienceBaseGetPtr();
    int command;

    __atomic_add_fetch(&sBattleFrames, 1, __ATOMIC_RELAXED);
    /* scripted moments (a boss's entrance, Hooktail's...) and the wide stages: not now */
    if (audience->flags & AUDIENCE_FLAG_FROZEN || _battleWorkPointer->flags & BATTLE_FLAG_WIDE_STAGE) {
        return;
    }
    command = __atomic_exchange_n(&sWaiting, -1, __ATOMIC_SEQ_CST);
    if (command == PORT_CROWD_BINGO && !slotIdle()) {
        requeue(command); /* (the Bingo below waits for the slot) */
        return;
    }
    if (command >= 0) {
        printf("[crowd] command %d\n", command);
        fflush(stdout);
    }
    switch (command) {
        case PORT_CROWD_GIFT:
            crowdThrow(kGifts[irand(3)]);
            break;
        case PORT_CROWD_ROCK:
            crowdThrow(irand(2) ? ITEM_AUDIENCE_ROCK : ITEM_AUDIENCE_CAN);
            break;
        case PORT_CROWD_STAR:
            crowdStarPower(25);
            break;
        case PORT_CROWD_DRAIN:
            crowdStarPower(-25);
            break;
        case PORT_CROWD_BINGO:
            /* every card turned (BattleBreakSlot_PointInc, as Stylish moves do); the
             * battle starts the spin between turns (BattleBreakSlot_CheckReaction).
             * Only while the slot is idle (above): a card turned during a spin
             * (phases 1-8) or its message would stall the reels. */
            while (pouchGetPtr()->starPowersObtained != 0 && BattleBreakSlotGetPtr()->reelNo < 3) {
                BattleBreakSlot_PointInc();
            }
            break;
        case PORT_CROWD_CHEER:
            BattleAudienceJoySACLecture(); /* a cheer and the crowd jumping */
            break;
        case PORT_CROWD_LEAVE:
            BattleAudienceAddAudienceNum(-5);
            break;
        case PORT_CROWD_JOIN:
            BattleAudienceAddAudienceNum(5);
            break;
        default:
            break;
    }
}
