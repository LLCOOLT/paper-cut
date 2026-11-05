/* The 0.4.0 modes watched for the achievements' counters
 * (port/include/port/achievements.h): each game frame, from what each mode
 * already tells its page (its status, its last result, its best), so the
 * modes' own files needn't change. Photo mode counts its pictures itself
 * (shot_aurora.c). Reads only; draw-only. */
#include "port/achievements.h"
#include "port/luigi.h"
#include "port/modes.h"
#include "port/online.h"
#include "port/open_world.h"
#include "port/playmodes.h"
#include "port/postgame.h"
#include "port/settings.h"
#include "port/versus.h"
#include "driver/seqdrv.h"
#include "driver/swdrv.h"
#include "evt/evt_story.h"
#include "mario/mario_pouch.h"
#include "mario/mariost.h"

/* the story values at which each chapter is over (the next one's start) */
static const int kChapterEnds[] = {
    STORY_STG2_00_BOGGLY_WOODS, STORY_STG3_00_WELCOME_TO_GLITZVILLE, STORY_STG4_00_WELCOME_TO_STAGE_4,
    STORY_STG5_00_LOGBOOK,      STORY_STG6_00_START,                 STORY_STG7_00_START,
    STORY_STG8_00_START,        STORY_STG8_END_AFTER_THE_BATTLE,
};

static int popcount7(int bits) {
    int n = 0, i;
    for (i = 0; i < 7; i++) {
        n += (bits >> i) & 1;
    }
    return n;
}

void portAchievementsModesFrame(void) {
    static int sTowerResults, sChapterResults, sGhostDeltas, sDailyBought;
    static int sVersusArmed, sPrevStory = -1;
    static unsigned sTick;
    const SequenceType seq = seqGetSeq();
    int floor = 0, cleared = 0, n;
    unsigned long long ms = 0;
    float delta = 0.0f;
    if (gp == NULL || pouchGetPtr() == NULL || (seq != SEQ_GAME && seq != SEQ_BATTLE)) {
        sPrevStory = -1;
        return;
    }
    if (++sTick % 10 != 0) {
        return;
    }
    /* the Paper Tower: the floor reached (an unhelped climb), the top, the time */
    if (portTowerActive() && !portTowerHelped()) {
        portAchievementCounterMax("tower_floor", portTowerFloor());
    }
    n = portTowerResult(&floor, &ms, &cleared);
    if (n != sTowerResults) {
        if (sTowerResults != 0 || n == 1) {
            if (!portTowerHelped()) {
                portAchievementCounterMax("tower_floor", floor);
                if (cleared) {
                    portAchievementCounterMax("tower_cleared", 1);
                    if (ms < 30ull * 60ull * 1000ull) {
                        portAchievementCounterMax("tower_fast", 1);
                    }
                }
            }
        }
        sTowerResults = n;
    }
    /* the trophy room */
    if (portTrophyRoomHere()) {
        portAchievementCounterMax("trophy_visit", 1);
        if (portTrophyRoomFull()) {
            portAchievementCounterMax("trophy_full", 1);
        }
    }
    /* daily specials bought (counted this run, added as they come) */
    n = portDailySpecialsBought();
    if (n > sDailyBought) {
        portAchievementCounterAdd("daily_bought", n - sDailyBought);
    }
    sDailyBought = n;
    /* versus: a fight set up, then its winner known */
    if (portVersusOn() && portVersusWinner() < 0) {
        sVersusArmed = 1;
    } else if (sVersusArmed && portVersusWinner() >= 0) {
        sVersusArmed = 0;
        portAchievementCounterAdd("versus_battles", 1);
    }
    /* the open-world randomizer: the file, and its Crystal Stars */
    if (portOpenOn()) {
        portAchievementCounterMax("rando_file", 1);
        portAchievementCounterMax("rando_stars", popcount7((pouchGetPtr()->starPowersObtained >> 1) & 0x7F));
    }
    /* ghost races: racing, and a room 10 seconds ahead */
    if (portGhostRacing()) {
        portAchievementCounterMax("ghost_race", 1);
        n = portGhostDelta(&delta);
        if (n != sGhostDeltas) {
            sGhostDeltas = n;
            if (delta <= -10.0f) {
                portAchievementCounterMax("ghost_ahead", 1);
            }
        }
    }
    /* the Pit in co-op: the floor now, and the best kept */
    if (portPitCoopActive()) {
        portAchievementCounterMax("pitcoop_floor", portPitCoopFloor());
    }
    /* boss rush, refights, the endless Glitz Pit */
    {
        const PortModeStatus* st = portModeStatus();
        if (st->mode == PORT_MODE_BOSS_RUSH && st->finished == 1) {
            portAchievementCounterMax("bossrush_won", 1);
        } else if (st->mode == PORT_MODE_REFIGHT && st->finished == 1) {
            portAchievementCounterMax("refight_won", 1);
        } else if (st->mode == PORT_MODE_ENDLESS_PIT) {
            portAchievementCounterMax("glitz_rounds", st->step - 1);
        }
    }
    /* chapter select: a replay finished */
    n = portChapterLastResult(&floor, &ms);
    if (n != sChapterResults) {
        if (sChapterResults != 0 || n == 1) {
            if (floor > 0) {
                portAchievementCounterMax("chapter_replay", 1);
            }
        }
        sChapterResults = n;
    }
    /* online co-op */
    if (portOnlineActive()) {
        portAchievementCounterMax("online_coop", 1);
    }
    /* a chapter finished: on a hardcore file, with the enemies remixed, as Luigi */
    {
        const int story = swByteGet(GSW_STORY_PROGRESS);
        size_t i;
        if (sPrevStory >= 0 && story > sPrevStory && seq == SEQ_GAME) {
            for (i = 0; i < sizeof(kChapterEnds) / sizeof(kChapterEnds[0]); i++) {
                if (sPrevStory < kChapterEnds[i] && story >= kChapterEnds[i]) {
                    const int* remix = portSettingsFind("remix_enemies");
                    if (portHardcoreActive() && i == 0) {
                        portAchievementCounterMax("hardcore_ch1", 1);
                    }
                    if (remix != NULL && *remix) {
                        portAchievementCounterMax("remix_chapter", 1);
                    }
                    if (portLuigiOn()) {
                        portAchievementCounterMax("luigi_chapter", 1);
                    }
                }
            }
        }
        sPrevStory = story;
    }
}
