/* Gameplay options (port/include/port/gameplay.h): the original game's
 * behavior unless the Aurora backend's settings change them. */
#include "port/gameplay.h"

int gPortSkipIntro;
int gPortTextSpeed = 100;
float gPortRunSpeed = 1.0f;
int gPortIntroFrames; /* frames of the boot logos since the speed was last updated */

void portIntroFrame(void) {
    gPortIntroFrames++;
}

int portTextMs(int ms) {
    return gPortTextSpeed == 100 ? ms : (int)((long long)ms * gPortTextSpeed / 100);
}
