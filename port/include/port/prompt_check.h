/* "!" prompts over Mario (src/mario/mario_balloon.c, port/src/common/prompt_check_pc.c).
 *
 * The prompts Paper Cut adds to the game's own ("talk_prompts": every A spot; "remake_pipe_a": every pipe) come
 * from the case events on the floor or wall Mario is at. A case is only a name: the thing it acts on (a pipe, a
 * block, a model) can be hidden, switched off or taken away while the case stays (a setting changed in the room,
 * a story point that hides a pipe, a room's own scripts). So each frame such a prompt shows only while what it
 * acts on is there now. */
#ifndef PORT_PROMPT_CHECK_H
#define PORT_PROMPT_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

struct HitEntry;
struct CaseEntry;

/* why the thing the case on hit acts on isn't there to act on now (a short reason), or NULL when it is */
const char* portPromptMissing(struct HitEntry* hit, const struct CaseEntry* ce);

/* A "!" means A does that thing now. So a prompt Paper Cut adds for the case on hit shows only while an A press
 * this frame would fire it, by casedrv.c's own test: the case's hit where its condition looks (in front:
 * hitobjFront; pushed: hitobjPush; stood on: hitobjStandOn, pipes too), case events open (no script or window
 * holding Mario, not a plane or in Vivian's shadow) and Mario standing, walking or running. Why A wouldn't (a short
 * reason), or NULL when it would. (An NPC A would talk to takes the prompt over: its talk balloon.) */
const char* portPromptCaseByA(struct HitEntry* hit, const struct CaseEntry* ce);

/* what put up a prompt (marioBalloonMain's checks). There is no Tattle "!": Goombella's field Tattle (X) names
 * only the NPC A talks to (fbatNpcTalkCheck), whose talk balloon is up already, and anywhere else a "!" would
 * promise an A press that does nothing. */
enum {
    PORT_PROMPT_NONE,
    PORT_PROMPT_FRONT, /* A in front (the game's flag, or talk_prompts) */
    PORT_PROMPT_PUSH,  /* pushing or standing on + A */
    PORT_PROMPT_PIPE,  /* a pipe under Mario (remake_pipe_a) */
    PORT_PROMPT_CHEST, /* an unopened chest */
    PORT_PROMPT_NPC,   /* the NPC A talks to (fbatNpcTalkCheck: the game's own talk test, the one A runs) */
};

/* each frame, from marioBalloonMain: the prompt shown or not, what put it up and its hit. With $TTYD_PROMPT_LOG
 * each prompt that comes up is logged ("[prompt] shown ..."), with whether its target is there. */
void portPromptFrame(int shown, int kind, struct HitEntry* hit);

/* $TTYD_PROMPT_AUDIT: each room Mario is free in, every case spot a prompt could show at is listed and Mario is
 * put on each floor one ("[prompt] spot ..."); the travel sweep ($TTYD_TRAVEL_SWEEP) waits while it runs. */
int portPromptAuditBusy(void);
void portPromptAuditFrame(void); /* each frame (trace_pc.c) */

#ifdef __cplusplus
}
#endif

#endif
