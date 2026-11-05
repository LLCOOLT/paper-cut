#ifndef PORT_PRACTICE_H
#define PORT_PRACTICE_H

/* Practice and speedrun tools (the settings page's "Practice" tab,
 * port/settings/features/practice.cpp) and local achievements
 * ("Achievements" tab, port/settings/features/achievements.cpp). Every one
 * is off until a setting turns it on; none changes how the game plays.
 *
 * - The timer (port/src/common/practice_pc.c): game time counted in the
 *   game's own frames (1/60 s, 1/50 s in 50 Hz), without the frames spent
 *   loading (a map change, a battle's set-up) when load removal is on, and
 *   real time beside it; splits when the story (GSW 0) reaches a category's
 *   next value. Shown in a corner; also sent to LiveSplit's "LiveSplit
 *   Server" component (TCP, 127.0.0.1:16834) as its commands.
 * - Frame advance: the game held between two frames, stepped one at a time.
 * - RNG: the game's rand() state shown and set, or set again at each room.
 * - Achievements (port/src/common/achievements_pc.c): read from the save's
 *   flags, kept per save file in the user folder (Achievements/<game id>/). */

#ifdef __cplusplus
extern "C" {
#endif

/* A name of the decomp's for a value (include/evt/evt_story.h; generated:
 * port/src/common/story_names_gen.c, tools/port/gen_story_names.py). */
typedef struct PortNamedValue {
    int value;
    const char* name;
    const char* note; /* the story's English event name, or the flag's comment */
} PortNamedValue;

extern const PortNamedValue portStoryNames[]; /* StoryProgress: GSW 0's values */
extern const int portStoryNamesCount;
extern const PortNamedValue portGswNames[]; /* GswId */
extern const int portGswNamesCount;
extern const PortNamedValue portGswfNames[]; /* GswfId */
extern const int portGswfNamesCount;
/* The story value's row, or NULL. */
const PortNamedValue* portStoryName(int value);

/* Once per game frame (trace_pc.c). */
void portPracticeFrame(void);

/* The timer. */
enum { PORT_TIMER_IDLE, PORT_TIMER_RUNNING, PORT_TIMER_DONE };
int portTimerState(void);
double portTimerGameSeconds(void); /* load-free game time (or every frame, without load removal) */
double portTimerRealSeconds(void);
int portTimerLoading(void); /* this frame is a load */
void portTimerStart(void);
void portTimerSplit(void);
void portTimerReset(void);
/* The category's splits: their names, and the game time each was made at (<0: not yet). */
int portSplitCount(void);
const char* portSplitName(int i);
double portSplitTime(int i);
int portSplitNext(void); /* the next split's index (portSplitCount when done) */
double portSplitRealTime(int i); /* the real time the split was made at (<0: not yet) */
int portSplitCategory(void);     /* the category the splits are of ("split_category"'s value) */
/* An attempt ended (finished 1, or reset 0): kept in LiveSplit's splits format,
 * <user>/splits/<category>.lss, with the personal best, the best segments and
 * the attempts (port/src/common/splits_lss_pc.c). */
void portSplitsSaveRun(int finished);
/* That file's path for the category now, and the personal best's game time (<0 none). */
const char* portSplitsFilePath(void);
double portSplitsBest(void);
/* The categories ("split_category"): the last is the user folder's splits.txt. */
int portSplitCategoryCount(void);
const char* portSplitCategoryName(int i);
/* Reads the category again (the setting changed, or splits.txt was edited). */
void portSplitsReload(void);
/* LiveSplit: "Off", "Connecting...", "Connected" or why not. */
const char* portLiveSplitStatus(void);
/* "h:mm:ss.cc" (or "m:ss.cc" under an hour). */
void portTimerFormat(double seconds, char* out, int size);
/* The connection (port/src/common/livesplit_pc.c): kept up once a frame while
 * enabled; a command ("split") queued while it's down goes when it's up. */
void portLiveSplitTick(int enabled, int port);
void portLiveSplitSend(const char* command);
/* The host's monotonic clock, seconds. */
double portHostSeconds(void);
/* Declares the practice settings (both the game side and the page call it). */
void portPracticeDeclare(void);
/* The user folder (Aurora's; headless: $TTYD_USER_DIR or "."). */
const char* portPracticeUserDir(void);
/* Makes <user folder>/<sub>/<game id> if it's missing (the per-release
 * records: Achievements, AreaMaps, Stickers). */
void portPracticeGameDir(const char* sub);
/* Each frame, from portPracticeFrame (achievements_pc.c). */
void portAchievementsFrame(void);
/* A save file was loaded (the file select, a save warp): gp->lastSlot and the
 * player's name say which; its achievements are read. */
void portPracticeFileLoaded(void);
void portAchievementsFileLoaded(void);

/* Frame advance (vi_aurora.c holds the game while portFrameAdvanceWait says so). */
void portFrameAdvanceHold(int hold);
int portFrameAdvanceHeld(void);
void portFrameAdvanceStep(void);
/* Between frames: nonzero while the game should stay held (a step lets one frame through). */
int portFrameAdvanceWait(void);

/* The game's rand() state, and set it (now, and at each room if fixed). */
unsigned int portPracticeRand(void);
void portPracticeSetRand(unsigned int state);

/* The practice tools changed the game (flags, items, stats): achievements
 * aren't given until a save file is loaded again. */
void portPracticeTaint(void);
int portPracticeTainted(void);

/* Achievements. */
int portAchievementCount(void);
const char* portAchievementName(int i);
const char* portAchievementNote(int i);
/* Progress toward it now (from the game's memory): *goal the target; 0 with no game. */
int portAchievementProgress(int i, int* goal);
/* Earned on this save file: the date it was ("YYYY-MM-DD"), or NULL. */
const char* portAchievementEarned(int i);
/* The file achievements are kept for now ("" before a save is loaded). */
const char* portAchievementFile(void);
/* The latest earned, for a toast: its index and how long ago (seconds); -1 if none in the last few seconds. */
int portAchievementToast(double* age);

#ifdef __cplusplus
}
#endif

#endif
