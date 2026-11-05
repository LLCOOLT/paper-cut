/* What a save slot's GameCube image says about its save, for the save files'
 * library (port/include/port/save_files.h): decoded as the game would read
 * it, then the file select's own fields (seq_load.c) read from it. */
#include "port/save_files.h"
#include "port/cardsave_pc.h"
#include "port/companion.h"
#include "manager/cardmgr.h"
#include "evt/evt_story.h"
#include <dolphin/os.h>
#include <stdio.h>
#include <string.h>

/* GSW 0 at the end of each chapter (as port/src/aurora/chapters_aurora.c) */
static const s32 kEnd[] = {
    STORY_STG0_END_TO_STAGE_1,      STORY_STG1_END_TO_STAGE_2,       STORY_STG2_END_TO_GLITZVILLE,
    STORY_STG3_END_TO_STAGE_4,      STORY_STG4_END_OFF_TO_THE_KEYS,  STORY_STG5_END_THE_TRAIN_LEAVES,
    STORY_STG6_END_ULTRA_HAMMER,    STORY_STG7_END_TO_THE_LAST_DUNGEON, STORY_STG8_END_AFTER_THE_BATTLE,
};

int portSaveImageInfo(const uint8_t* image, PortSaveInfo* info) {
    static CardSaveSlot slot;
    char name[sizeof(slot.data.global.playerName) + 1];
    size_t i;
    memset(info, 0, sizeof(*info));
    if (!portCardSlotFromImage(image, &slot) || (slot.data.flags & (CARD_SAVE_FLAG_EMPTY | CARD_SAVE_FLAG_CORRUPT))) {
        return 0;
    }
    info->valid = 1;
    memcpy(name, slot.data.global.playerName, sizeof(slot.data.global.playerName));
    name[sizeof(name) - 1] = '\0';
    portCompanionUtf8(name, info->name, (int)sizeof(info->name), 0);
    memcpy(info->map, slot.data.global.currentMapName, sizeof(info->map) - 1);
    info->map[sizeof(info->map) - 1] = '\0';
    info->story = slot.data.global.gsw0;
    info->chapter = 9;
    for (i = 0; i < sizeof(kEnd) / sizeof(kEnd[0]); i++) {
        if (info->story < kEnd[i]) {
            info->chapter = (int)i;
            break;
        }
    }
    info->level = slot.data.pouch.level;
    info->stars = 0;
    for (i = 0; i < 16; i++) {
        info->stars += (slot.data.pouch.starPowersObtained >> i) & 1;
    }
    info->seconds = (long long)OSTicksToSeconds(slot.data.global.retraceLocalTime);
    info->saveTime = (long long)OSTicksToSeconds(slot.data.global.saveTime);
    info->saveCount = slot.data.global.saveCount;
    return 1;
}
