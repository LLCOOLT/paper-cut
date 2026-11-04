/* Discord status's view of the game (port/settings/features/discord.cpp): the
 * map Mario is on and whether a battle runs. */
#include "mario/mariost.h"

/* 1 with the map's name (up to size - 1 characters; "" before the first map)
 * and whether a battle runs; 0 before the game has started */
int portDiscordGame(char* map, int size, int* inBattle) {
    int i;
    if (gp == NULL || map == NULL || size <= 0) {
        return 0;
    }
    for (i = 0; i < size - 1 && i < (int)sizeof(gp->currentMapName) && gp->currentMapName[i] != '\0'; i++) {
        map[i] = gp->currentMapName[i];
    }
    map[i] = '\0';
    *inBattle = gp->inBattle != 0;
    return 1;
}
