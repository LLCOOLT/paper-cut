/* Each PC executable is built for one game version (TTYD_VERSION): the disc it
 * runs from must be that version's, since JP and US data and structs differ. */
#include <stdio.h>
#include <string.h>
#include "port/platform.h"

BOOL portCheckGameId(const char* id, const char* source) {
    if (id == NULL || memcmp(id, PORT_GAME_ID, 6) != 0) {
        fprintf(stderr, "%s is %.6s, but this build is for %s (Paper Mario: The Thousand-Year Door, %s).\n", source,
                id ? id : "not a GameCube disc", PORT_GAME_ID,
                strcmp(PORT_GAME_ID, "G8ME01") == 0 ? "US" : "Japan");
        return FALSE;
    }
    return TRUE;
}
