#ifndef PORT_REPLAY_H
#define PORT_REPLAY_H

/* Replays (docs/REPLAYS.md): a run's controller input recorded to a small
 * file and played back exactly.
 *
 * The game plays out the same from the same start and the same input (the
 * lockstep online co-op uses, port/include/port/online.h): a fresh start of
 * the program, the game clock virtual from a base the file keeps, the same
 * settings that change play, the same memory card and start save. So a
 * recording, and a playback, each begin with the program starting again
 * (Restart now), and run from the first pad read to the last.
 *
 * - The pad read's side (port/src/common/replay_pc.c): each frame's input of
 *   both players kept, run-length (recording), or put in place of the
 *   controllers' (playback); a fingerprint of the game's state every
 *   PORT_REPLAY_CHECK_EVERY frames kept, or compared.
 * - The page and the file (port/settings/features/replay.cpp, Aurora): the
 *   Practice tab's Replays section, the restart, the settings, card and save
 *   kept or put in place, <user>/replays/<name> <date>.pcreplay. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_REPLAY_PAD_BYTES 16 /* one controller's state for a frame, as online's */
#define PORT_REPLAY_PADS 2       /* both players */
#define PORT_REPLAY_CHECK_EVERY 600

/* a run of frames with the same input */
typedef struct PortReplayRun {
    unsigned frames;
    unsigned char pads[PORT_REPLAY_PADS][PORT_REPLAY_PAD_BYTES];
} PortReplayRun;

/* the game's fingerprint (portOnlineFingerprint) at a frame */
typedef struct PortReplayCheck {
    unsigned frame;
    unsigned long long hash;
} PortReplayCheck;

/* Recording: from the next pad read; ended with portReplayRecordEnd (the
 * input kept until the next begin). */
void portReplayRecordBegin(void);
void portReplayRecordEnd(void);
int portReplayRecording(void);
/* what was recorded: the runs, the checks and the frames */
const PortReplayRun* portReplayRuns(size_t* count);
const PortReplayCheck* portReplayChecks(size_t* count);

/* Playback: these runs and checks (copied) from the next pad read. */
void portReplayPlayBegin(const PortReplayRun* runs, size_t runCount, const PortReplayCheck* checks,
                         size_t checkCount);
int portReplayPlaying(void);
/* the input played to its end (the game now gets nothing pressed) */
int portReplayFinished(void);
/* the player pressed B to stop (playback): the page's side ends it */
int portReplayStopAsked(void);
/* the first frame whose fingerprint differed, or -1: in step so far */
long portReplayOutOfStep(void);

/* Recording or playing: what would change this game alone (save states,
 * warps, the Practice tab's edits, fast travel, menu taps) is off. */
int portReplayActive(void);
/* pad reads since the recording or playback began, and (playback) its length */
unsigned portReplayFrame(void);
unsigned portReplayLength(void);

/* The pad read (src/sdk/DEMOPad.c), after online co-op's. */
struct PADStatus;
void portReplayApplyPad(struct PADStatus* pads);

/* Aurora (replay.cpp): at start-up (main_aurora.c), a recording or playback
 * the restart asked for begun; a recording saved before a restart. */
void portReplayStartup(void);
void portReplaySaveIfRecording(void);
/* The game started again recording (as the Replays section's button), named
 * name, from quick resume's save (fromSave) or the title; 0 when it can't
 * here (phones, online co-op, a replay under way). */
int portReplayRestartRecording(const char* name, int fromSave);
/* restart.cpp: the environment for a replay's restart put in place */
void portReplayRestartEnv(void);
/* playback's fast-forward: how many times faster the frames go (vi_aurora.c) */
int portReplaySpeed(void);

#ifdef __cplusplus
}
#endif

#endif
