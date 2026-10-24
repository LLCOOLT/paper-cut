/* ARQ completion callbacks, run later as on hardware.
 *
 * On GameCube an ARQ transfer completes in a DMA interrupt some time after
 * ARQPostRequest returns, and the game relies on that: aramMgrToAramAsync
 * links its entry into the list the callback searches only after posting.
 * Both PC backends (port/src/headless/sdk/ar_pc.c, Aurora's AR.cpp) copy at
 * once and call the callback before returning, so the game's calls are routed
 * here (port_prefix.h renames ARQPostRequest): the backend completes the copy,
 * and the game's callback runs at the next portARQPoll, which runs at each
 * retrace and in the game's empty wait loops (PORT_SPIN, include/pc_call.h). */
#undef ARQPostRequest
#include <stdint.h>
#include <dolphin/types.h>
#include <dolphin/arq.h> /* addresses as uintptr_t: Aurora's arq.h has no ARQAddr */
#include <stdio.h>
#include <stdlib.h>
#include "port/platform.h"

#define PENDING_MAX 256

typedef struct Pending {
    ARQRequest* request;
    ARQCallback callback; /* the game's */
    BOOL done;
} Pending;

static Pending sPending[PENDING_MAX];
static u32 sHead, sCount;

static void onComplete(uintptr_t request) {
    u32 i;
    for (i = 0; i < sCount; i++) {
        Pending* p = &sPending[(sHead + i) % PENDING_MAX];
        if ((uintptr_t)p->request == request && !p->done) {
            p->done = TRUE;
            return;
        }
    }
}

void portARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, uintptr_t source, uintptr_t dest,
                        u32 length, ARQCallback callback) {
    Pending* p;
    if (callback == NULL) {
        ARQPostRequest(request, owner, type, priority, source, dest, length, NULL);
        return;
    }
    if (sCount == PENDING_MAX) {
        portARQPoll();
        if (sCount == PENDING_MAX) {
            fprintf(stderr, "portARQPostRequest: %u transfers pending\n", (unsigned)sCount);
            abort();
        }
    }
    p = &sPending[(sHead + sCount++) % PENDING_MAX];
    p->request = request;
    p->callback = callback;
    p->done = FALSE;
    ARQPostRequest(request, owner, type, priority, source, dest, length, onComplete);
}

void portARQPoll(void) {
    /* completed transfers, in posting order; a callback may post more */
    while (sCount > 0 && sPending[sHead].done) {
        Pending p = sPending[sHead];
        sHead = (sHead + 1) % PENDING_MAX;
        sCount--;
        p.request->callback = p.callback;
        p.callback((uintptr_t)p.request);
    }
}
