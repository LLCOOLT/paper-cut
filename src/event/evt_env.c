// Script commands for environment effects (envdrv).
#include "evt/evt_env.h"
#include "manager/evtmgr_cmd.h"
#include "driver/envdrv.h"
#include <dolphin/os.h>

/** s32 type, s32 msec (0 = forever) */
USER_FUNC(evt_env_blur_on) {
    EvtWord* args = event->args;
    s32 type, msec;
    s64 now;

    type = evtGetValue(event, args[0]);
    msec = evtGetValue(event, args[1]);
    now = event->lifetime;
    if (isFirstCall) {
        envBlurOn(type, msec);
#ifdef TARGET_PC
        /* PC port: the start time in one 8-byte word. The GameCube splits it over two 4-byte words, high word
         * first, and reads them back as one u64; on PC that read gave only the high word (0), so the wait was
         * measured from the event's start and a blur from a script running longer than msec ended at once. */
        event->userdata[0] = now;
#else
        event->userdata[0] = ((u64)now >> 32) & 0xFFFFFFFF;
        event->userdata[1] = now & 0xFFFFFFFF;
#endif
    }
#ifdef TARGET_PC
    if (msec && OSTicksToMilliseconds(now - event->userdata[0]) >= msec) {
#else
    if (msec && OSTicksToMilliseconds(now - *(u64*)event->userdata) >= msec) {
#endif
        envBlurOff();
        return EVT_RETURN_DONE;
    }
    return EVT_RETURN_BLOCK;
}
