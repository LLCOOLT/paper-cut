#ifndef PORT_CROWD_H
#define PORT_CROWD_H

/* Crowd Control in plain chat: viewers' commands in a Twitch channel's chat
 * drive the battle audience (port/settings/features/crowd_control.cpp reads
 * the chat; port/src/common/crowd_pc.c does what they ask, in the battle's
 * own frame). Off by default ("crowd_control"). */

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PORT_CROWD_GIFT,  /* someone in the stands throws a good item */
    PORT_CROWD_ROCK,  /* ... or a rock or a can */
    PORT_CROWD_STAR,  /* a little Star Power */
    PORT_CROWD_DRAIN, /* a little Star Power taken away */
    PORT_CROWD_BINGO, /* the Bingo cards filled: the spin comes at the next turn */
    PORT_CROWD_CHEER, /* the crowd cheers and jumps */
    PORT_CROWD_LEAVE, /* a few of the crowd leave */
    PORT_CROWD_JOIN,  /* a few more come */
    PORT_CROWD_COUNT
};

/* A command, from any thread; dropped if one is already waiting. */
void portCrowdPush(int command);
/* The same from the game's own frame (chaos mode, port/src/common/chaos_pc.c): online too, as
 * every game of a session pushes it in the same frame (a chat's commands, portCrowdPush, are
 * one game's alone, so off online). */
void portCrowdPushGame(int command);
/* Frames of battles with an audience so far: it changes while one is on. */
unsigned portCrowdBattleFrames(void);
/* battle_audience.c, each battle frame: the waiting command, if any. */
void portCrowdBattleFrame(void);

#ifdef __cplusplus
}
#endif

#endif
