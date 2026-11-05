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
 *   are the Ultra Boots, the Castle Keys open any of the castle's doors;
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
    PORT_OPEN_SEED = PORT_OPEN_GSW + 8,    /* the seed text, NUL-terminated */
    PORT_OPEN_SEED_MAX = 48,
    PORT_OPEN_TRACKS = PORT_OPEN_GSW + 56, /* u16 per chapter 0..9: its story (0: not begun) */
};

/* This file is an open-world file. */
int portOpenOn(void);
/* its seed's text ("" if it isn't one) */
const char* portOpenFileSeed(void);
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
/* evtmgr_cmd.c, CALL_CHILD: the script to run instead */
struct EventEntry;
void* portOpenChildScript(struct EventEntry* entry, void* script);
/* seq_load.c, a new file: stamped if the randomizer is armed for one */
void portOpenNewFile(void);
/* a test or a scripted run: stamp the file in play with this seed */
void portOpenStamp(const char* seed, unsigned pools);
/* $TTYD_OPEN_SELFTEST: the items and stories checked at once (PASS/FAIL lines) */
void portOpenSelfTest(void);

/* The C++ side (port/archipelago/open_world.cpp, Aurora; stubs in port/src/headless/platform/headless.c) */
/* the item a partner's own place holds: 0x1000 + party id for a partner, an
 * item id, or 0 to leave it */
int portOpenPartnerReward(int partner);
/* the area's linked: its script words written and its partner scene known */
void portOpenAreaLinked(const char* area);
/* the new file's stamp: 1 if the randomizer is armed for it (seed and pools out) */
int portOpenArmNewFile(char* seed, int seedMax, unsigned* pools);
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
