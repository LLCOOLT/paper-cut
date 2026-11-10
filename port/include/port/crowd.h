#ifndef PORT_CROWD_H
#define PORT_CROWD_H

/* Crowd Control in plain chat: viewers' commands in a Twitch channel's chat
 * drive the battle audience (port/settings/features/crowd_control.cpp reads
 * the chat; port/src/common/crowd_pc.c does what they ask, in the battle's
 * own frame). Off by default ("crowd_control"). */

#include <stddef.h>

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

/* Idle players in the audience (port/src/common/crowd_seats_pc.c, docs/PLAYERS.md): in co-op a
 * player with no character in the battle has a seat in the crowd, emotes and a few throws.
 * "audience_players" (on); battle_audience.c calls Reset (BattleAudience_Init), Frame
 * (BattleAudience_Main) and Disp (BattleAudience_Disp). */
int portCrowdSeatsOn(void);
void portCrowdSeatsReset(void);
void portCrowdSeatsFrame(void);
void portCrowdSeatsDisp(void);
/* statesync_battle_pc.c: a shared battle's B_CROWD message (from: the sender's battle place) */
void portCrowdSeatsNet(int from, const void* data, size_t n);
/* statesync_battle_pc.c for it: send to player 1's game (toA) or to every other game of the shared
 * battle (0 when none runs); this game decides the shared battle; the battle's id */
int portStateSyncCrowdSend(int toA, const void* data, size_t n);
int portStateSyncBattleIsA(void);
unsigned portStateSyncBattleId(void);

#ifdef __cplusplus
}
#endif

#endif
