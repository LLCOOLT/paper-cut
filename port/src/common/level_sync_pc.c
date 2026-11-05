/* Level sync (port/include/port/toggles.h): an enemy below Mario's level is
 * brought up to it, in the post-game and on New Game+ files (or always), so
 * a high-level Mario still has fights worth the name. Everything here is a
 * function of the game's own state and the setting, which online takes from
 * the host: both games scale the same enemies the same way. */
#include "port/toggles.h"
#include "port/settings.h"
#include "port/versus.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"
#include <stdio.h>

#define HP_STEP 10      /* % more HP per level below Mario */
#define HP_MAX 400      /* at most this % */
#define ATTACK_LEVELS 6 /* 1 more damage per this many levels below */
#define ATTACK_MAX 6
#define NG_PLUS_AHEAD 8 /* levels above the chapter's usual: a New Game+ file */

static int setting(void) {
    static int* sMode;
    if (sMode == NULL) {
        sMode = portSettingsDeclare("level_sync", 0, 0, 2);
    }
    return *sMode;
}

/* the level a Mario usually has by this point of the story (a little above the
 * chapter's enemies, as most players have him) */
static int usualLevel(int story) {
    static const struct {
        int story, level;
    } kChapters[] = {
        {STORY_STG8_00_START, 27}, {STORY_STG7_00_START, 24},          {STORY_STG6_00_START, 21},
        {STORY_STG5_00_LOGBOOK, 18}, {STORY_STG4_00_WELCOME_TO_STAGE_4, 15}, {STORY_STG3_00_WELCOME_TO_GLITZVILLE, 12},
        {STORY_STG2_00_BOGGLY_WOODS, 9}, {STORY_STG1_00_PETAL_MEADOWS, 5},
    };
    size_t i;
    for (i = 0; i < sizeof(kChapters) / sizeof(kChapters[0]); i++) {
        if (story >= kChapters[i].story) {
            return kChapters[i].level;
        }
    }
    return 3; /* the prologue */
}

/* Mario's level when level sync applies now, else 0 */
static int syncLevel(void) {
    const int mode = setting();
    PouchData* pouch;
    int story;
    if (mode == 0 || portConsoleAccurate() || portVersusOn() || gp == NULL || (pouch = pouchGetPtr()) == NULL) {
        return 0;
    }
    story = swByteGet(GSW_STORY_PROGRESS);
    if (mode == 2 || story >= STORY_STG9_02_RETURN_TO_ROGUEPORT || pouch->level >= usualLevel(story) + NG_PLUS_AHEAD) {
        return pouch->level;
    }
    return 0;
}

int portLevelSyncHp(int hp, int kindLevel) {
    const int level = syncLevel();
    int percent, r;
    if (level <= kindLevel || kindLevel <= 0 || hp <= 0) {
        return hp;
    }
    percent = 100 + HP_STEP * (level - kindLevel);
    percent = percent > HP_MAX ? HP_MAX : percent;
    r = (hp * percent + 50) / 100;
    printf("[level_sync] enemy level %d to %d: HP %d -> %d\n", kindLevel, level, hp, r);
    fflush(stdout);
    return r;
}

int portLevelSyncLevel(int level) {
    const int mario = syncLevel();
    /* two above Mario: Star Points as for an even fight (BtlUnit_GetExp gives
     * none for an enemy at or below his level) */
    return mario > 0 && level > 0 && level < mario + 2 ? mario + 2 : level;
}

int portLevelSyncDamage(int damage, int kindLevel) {
    const int level = syncLevel();
    int more;
    if (level <= kindLevel || kindLevel <= 0 || damage <= 0) {
        return damage;
    }
    more = (level - kindLevel) / ATTACK_LEVELS;
    return damage + (more > ATTACK_MAX ? ATTACK_MAX : more);
}
