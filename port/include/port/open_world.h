#ifndef PORT_OPEN_WORLD_H
#define PORT_OPEN_WORLD_H

/* Paper Cut's own open world (docs/RANDOMIZER.md, "Paper Cut's open world";
 * port/src/common/open_world_pc.c). A file is an open-world file when its
 * GSW block from PORT_OPEN_GSW holds the stamp: the seed, what is shuffled
 * and each chapter's own story. Everything here does nothing for any other
 * file, so the game is the original one unless a file was started as one.
 *
 * The game code calls these from small TARGET_PC blocks:
 * - swdrv.c: GSW 0 (the story) read and written through the current
 *   chapter's own story (portOpenStoryGet/Set);
 * - mario_pouch.c: a partner's item joins the partner, the Super Boots twice
 *   are the Ultra Boots, the Castle Keys open any of the castle's doors
 *   (the Steeple Keys, the palace tower's likewise); the Star Power maximum
 *   follows the stars Mario has;
 * - evt_pouch.c: a scene that sends the party away brings back who was there;
 * - evtmgr_cmd.c: a partner's own joining scene gives what was placed there;
 * - seq_load.c: a new file started while the randomizer is on is stamped. */

#ifdef __cplusplus
extern "C" {
#endif

/* GSW 1920..2047, unused by the game (it names GSWs up to 1474) and by the
 * TTYD AP mod (1696, 1792..1805) */
enum {
    PORT_OPEN_GSW = 1920,
    PORT_OPEN_MAGIC = PORT_OPEN_GSW,      /* "PCOW" */
    PORT_OPEN_VERSION = PORT_OPEN_GSW + 4, /* the stage the file was made with */
    PORT_OPEN_POOLS = PORT_OPEN_GSW + 5,   /* ttyd_rando::Pool bits */
    PORT_OPEN_STICKY = PORT_OPEN_GSW + 6,  /* the chapter last played (title cards, intermissions) */
    PORT_OPEN_GOAL = PORT_OPEN_GSW + 7,    /* the Crystal Stars that are the goal (0: all of the stage's) */
    PORT_OPEN_SEED = PORT_OPEN_GSW + 8,    /* the seed text, NUL-terminated */
    PORT_OPEN_SEED_MAX = 48,
    PORT_OPEN_TRACKS = PORT_OPEN_GSW + 56, /* u16 per chapter 0..9: its story (0: not begun) */
    PORT_OPEN_AWAY = PORT_OPEN_GSW + 76,   /* partners a scene sent away (bit per party id) */
    PORT_OPEN_FLAGS = PORT_OPEN_GSW + 77,  /* PORT_OPEN_FLAG_* */
    PORT_OPEN_FLAG_GOAL = 1,               /* the goal's notice was shown */
    PORT_OPEN_STAGE = 2,                   /* this build's stage (PORT_OPEN_VERSION): chapters 1 to 5 */
};

/* This file is an open-world file. */
int portOpenOn(void);
/* its seed's text ("" if it isn't one) */
const char* portOpenFileSeed(void);
/* its goal: the Crystal Stars it asks for (0: all of its stage's, or not one) */
int portOpenFileGoal(void);
/* GSW 0 as the current chapter sees it (raw: the game's own value) */
int portOpenStoryGet(int raw);
/* GSW 0 written: 1 if it went to the current chapter's own story */
int portOpenStorySet(int value);
/* the chapter whose story the current map reads (0: the game's own) */
int portOpenTrack(void);
/* An item the pouch is asked about or for, as this file has it: a Castle Key
 * for any Castle Key. */
unsigned portOpenItemAlias(unsigned item);
/* pouchGetItem: 1 if this item was given otherwise (a partner joined) */
int portOpenGetItem(int item);
/* evt_pouch.c: a script sends a partner away (left) or brings one back
 * (join); join returns 1 to leave it out (not found yet in this file) */
int portOpenPartyLeft(int partner);
int portOpenPartyJoin(int partner);
/* mario_pouch.c, pouchGetStarStone: the Star Power maximum by the stars
 * Mario has; the goal reached */
void portOpenStarStone(void);
/* evtmgr_cmd.c, CALL_CHILD: the script to run instead */
struct EventEntry;
void* portOpenChildScript(struct EventEntry* entry, void* script);
/* seq_load.c, a new file: stamped if the randomizer is armed for one */
void portOpenNewFile(void);
/* a test or a scripted run: stamp the file in play with this seed */
void portOpenStamp(const char* seed, unsigned pools, int goal);
/* $TTYD_OPEN_SELFTEST: the items and stories checked at once (PASS/FAIL lines) */
void portOpenSelfTest(void);

/* The C++ side (port/archipelago/open_world.cpp, Aurora; stubs in port/src/headless/platform/headless.c) */
/* the item a partner's own place holds: 0x1000 + party id for a partner, an
 * item id, or 0 to leave it */
int portOpenPartnerReward(int partner);
/* the area's linked: its script words written and its partner scene known */
void portOpenAreaLinked(const char* area);
/* the new file's stamp: 1 if the randomizer is armed for it (seed, pools and goal out) */
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools, int* goal);
/* a notice's text in the player's language (portTr) */
const char* portOpenText(const char* english);
/* msgSearch: the open world's own messages (the partners' items), or NULL */
const char* portOpenMsg(const char* tag);
/* the area's partner scene (all_party_lecture), from portOpenAreaLinked */
void portOpenSetLecture(void* script);

/* the item ids a partner's item takes in this file (the game's unused key items) */
int portOpenPartnerToken(int partner);

#ifdef __cplusplus
}
#endif

#endif
