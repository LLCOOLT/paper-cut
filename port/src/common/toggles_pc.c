/* Console-accurate mode's game side (port/include/port/toggles.h): the
 * switch itself. What it puts back and locks is the settings page's
 * (port/settings/features/console_accurate.cpp); the game's own code asks
 * this before any Paper Cut change of its own that isn't a setting. */
#include "port/toggles.h"
#include "port/settings.h"

int portConsoleAccurate(void) {
    static int* sOn;
    if (sOn == NULL) {
        sOn = portSettingsDeclare("console_accurate", 0, 0, 1);
    }
    return *sOn != 0;
}
