/* The controller light's view of the game (port/settings/features/
 * controller_light.cpp): Mario's HP, while a game is under way. */
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

int portLightMarioHp(int* current, int* maximum) {
    const PouchData* pouch = pouchGetPtr();
    if (gp == NULL || pouch == NULL || pouch->maximumHP <= 0) {
        return 0;
    }
    *current = pouch->currentHP;
    *maximum = pouch->maximumHP;
    return 1;
}
