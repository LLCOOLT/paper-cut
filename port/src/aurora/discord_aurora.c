/* Discord status's view of the game (port/settings/features/discord.cpp): the
 * map Mario is on, whether a battle runs, and how far the story is. */
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
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

/* 1 with the story's chapter (0 the prologue, 1-8, 9 after the final battle)
 * and Mario's level; 0 when no file is being played */
int portDiscordStory(int* chapter, int* level) {
    static const int kStarts[] = {STORY_STG1_00_PETAL_MEADOWS, STORY_STG2_00_BOGGLY_WOODS,
                                  STORY_STG3_00_WELCOME_TO_GLITZVILLE, STORY_STG4_00_WELCOME_TO_STAGE_4,
                                  STORY_STG5_00_LOGBOOK, STORY_STG6_00_START, STORY_STG7_00_START,
                                  STORY_STG8_00_START, STORY_STG9_00_EPILOGUE};
    PouchData* pouch;
    int story, i;
    if (gp == NULL || chapter == NULL || level == NULL || (seqGetSeq() != SEQ_GAME && seqGetSeq() != SEQ_BATTLE)) {
        return 0;
    }
    story = swByteGet(GSW_STORY_PROGRESS);
    *chapter = 0;
    for (i = 0; i < (int)(sizeof(kStarts) / sizeof(kStarts[0])); i++) {
        if (story >= kStarts[i]) {
            *chapter = i + 1;
        }
    }
    pouch = pouchGetPtr();
    *level = pouch != NULL ? pouch->level : 0;
    return 1;
}
