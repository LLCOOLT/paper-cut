// Program entry: initialises the game and runs the main loop (game update, draw, frame timing).
#include "main.h"

#include "mario/mariost.h"
#include "sdk/DEMOInit.h"
#ifdef TARGET_PC
#include "port/toggles.h"
#endif

/**
 * Program entry: initialises the game (marioStInit),
 * then loops forever running one game update (marioStMain) and one draw (marioStDisp) per frame,
 * recording the update and render times in OS ticks in gp->deltaGame and gp->deltaRender.
 */
int main(void) {
    OSTick base_tick;

    marioStInit();
    marioStMain();
    while (1) {
        base_tick = OSGetTick();
#ifdef TARGET_PC
        if (portBatterySkipDraw()) {
            /* battery saver: this frame runs, its cameras and draw list as ever, but
             * nothing is drawn or shown (port/toggles.h) */
            marioStDisp();
            marioStMain();
            gp->deltaGame = OSGetTick() - base_tick;
            portBatteryFrameUndrawn();
            gp->deltaRender = OSGetTick() - base_tick;
            continue;
        }
#endif
        DEMOBeforeRender();
        marioStDisp();
        marioStMain();
        gp->deltaGame = OSGetTick() - base_tick;
        DEMODoneRender();
        gp->deltaRender = OSGetTick() - base_tick;
    }

    return 0;
}
