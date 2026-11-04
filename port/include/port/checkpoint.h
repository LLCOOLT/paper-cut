/* Test checkpoints (port/src/common/checkpoint_pc.c): a scripted run
 * ($TTYD_INPUT, input_pc.c) saves the game to a file at each room change or
 * at a `checkpoint NAME` step, and a later run starts from one of them
 * ($TTYD_CHECKPOINT_LOAD) instead of from the start of the chapter.
 *
 * A checkpoint is a real save of the game (the image a Save Block writes,
 * portCardSnapshot), not a save state, so it still loads after a rebuild:
 * a fix to the code is tested from the room where the run failed. Beside
 * the save it holds Super Luigi's mode, chapter and the inn's return game,
 * the room and entrance being entered, the clock and random state, and the
 * test driver's place in its script. */
#ifndef PORT_CHECKPOINT_H
#define PORT_CHECKPOINT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Super Luigi's part (superluigi_aurora.c; none in a build without it) */
typedef struct PortSlCheckpoint {
    int mode;    /* PORT_SL_* (0: not in Super Luigi) */
    int chapter;
    int returnSlot;
    int haveReturn; /* the inn's chapters: Mario's game to go back to */
    uint8_t* returnImage; /* PORT_CARD_SLOT_SIZE bytes (the caller's buffer) */
    char keptMap[16];     /* where Mario left the inn from */
    float keptPos[3];
} PortSlCheckpoint;

void portSlCheckpointGet(PortSlCheckpoint* out);
/* the save image loaded as file SLOT, as a continue does, with Super Luigi's
 * state put back; the caller then picks the room (a map change) */
void portSlCheckpointResume(const PortSlCheckpoint* ck, const uint8_t* image, int slot);

/* Saves the game as it is now to <$TTYD_CHECKPOINT_DIR>/<name>.ckpt.
 * map/bero: the room being entered (a room change: the save is made before
 * the old room is left, and a resume enters the new one by that entrance,
 * as the run did); NULL: here and now (a resume loads it as a Save Block's
 * save loads: Luigi where he stood, the room coming up fresh). driver: the
 * test driver's lines (key=value, '\n' after each), kept for the resume.
 * Returns 1 if it was written. */
int portCheckpointSave(const char* name, const char* map, const char* bero, const char* driver);

/* $TTYD_CHECKPOINT_LOAD: the file read at start; NULL if none (or bad).
 * Returns the driver's lines; *inPlace: 1 if it was a here-and-now save. */
const char* portCheckpointPending(int* inPlace);
/* Once the title (or the field) is up: the checkpoint loaded and its room
 * entered. Returns 1 the frame it's done (the caller then puts its driver
 * state back), 0 while it waits. Called once a frame, before seqMain. */
int portCheckpointResumeFrame(void);
/* a driver line's value: "step=12" -> "12"; NULL if the key isn't there */
const char* portCheckpointValue(const char* lines, const char* key, char* out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
