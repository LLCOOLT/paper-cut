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
 *   (the Steeple Keys, the palace's likewise); the Star Power maximum
 *   follows the stars Mario has;
 * - evt_pouch.c: a scene that sends the party away brings back who was there;
 * - evtmgr_cmd.c: a partner's own joining scene gives what was placed there;
 * - seq_load.c: a new file started while the randomizer is on is stamped;
 * - gor_04.c: the station's attendants say whose way in the room is (the
 *   train's, chapter 6, or the blimp's, chapter 3: portEvtOpenRoomChapter);
 * - gor_01.c: the Ultra Hammer's chest stays whatever hammer Mario has
 *   (portEvtOpenWorld);
 * - gon_06.c, usu_01.c, dou_07.c: a black chest gives what was put there,
 *   shown as an item (portEvtOpenChest);
 * - evt_msg.c: a joining scene that gave something else leaves out the
 *   partner's lines (portOpenMuteLine);
 * - tik_05.c: the Thousand-Year Door's scene shows the stars Mario has
 *   (portOpenStarShown), the pedestal's the star of the chapter done
 *   (portEvtOpenStretchStar). */

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
    PORT_OPEN_ORDER = PORT_OPEN_GSW + 78,  /* the chapters in the order they were done (8 bytes, 0: none yet) */
    PORT_OPEN_LOGIC = PORT_OPEN_GSW + 86,  /* the logic's version the file's placements were made with */
    PORT_OPEN_FLAG_GOAL = 1,               /* the goal's notice was shown */
    PORT_OPEN_STAGE = 3,                   /* this build's stage (PORT_OPEN_VERSION): chapters 1 to 8 */
    /* this build's logic (PORT_OPEN_LOGIC, ttyd_rando::Options::pcLogic): 0 before the
     * one-way trips (a file from then keeps its placements), 1 with them */
    PORT_OPEN_LOGIC_VERSION = 1,
};

/* This file is an open-world file. */
int portOpenOn(void);
/* its seed's text ("" if it isn't one) */
const char* portOpenFileSeed(void);
/* its goal: the Crystal Stars it asks for (0: all of its stage's, or not one) */
int portOpenFileGoal(void);
/* GSW 0 as the current chapter sees it (raw: the game's own value) */
int portOpenStoryGet(int raw);
/* GSW 0 as portOpenStoryGet reads it, changing nothing (no room noted, no
 * Rogueport catch-up, no chapter last played): swStoryPeek, for readers
 * outside the game's own logic (menus, Discord, achievements) */
int portOpenStoryPeek(int raw);
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
/* $TTYD_OPEN_SEED for a scripted run, or NULL (also online and in replays,
 * where the randomizer's settings are the session's) */
const char* portOpenTestSeed(void);
/* a test or a scripted run: stamp the file in play with this seed */
void portOpenStamp(const char* seed, unsigned pools, int goal);
/* $TTYD_OPEN_SELFTEST: the items and stories checked at once (PASS/FAIL lines) */
void portOpenSelfTest(void);
/* Script functions (USER_FUNC): the room reads this chapter's story for the
 * rest of the visit (args[0]: the chapter); 1 into args[0] if this file is an
 * open-world file */
int portEvtOpenRoomChapter(struct EventEntry* event, int isFirstCall);
int portEvtOpenWorld(struct EventEntry* event, int isFirstCall);
/* The one-way trips (Flavio's ship, the train's first ride, the cannon to
 * the moon) with no way back until their chapter's scene ends: args[0] the
 * trip (0 the ship, 1 the train, 2 the moon), 1 into args[1] if Mario may
 * take it. In an open-world file he may only once it can be finished from
 * what he holds and what lies on the way (the seed's logic counts on the
 * same: rando_gen.cpp's pcTripCanEnd); in any other file always. */
int portEvtOpenTripReady(struct EventEntry* event, int isFirstCall);
/* for the C++ side's portOpenTripCanEnd: what Mario holds (a key item's count
 * in the pouch, as this file counts it; a partner in the party; the Crystal
 * Stars) */
int portOpenHeldCount(int item);
int portOpenPartnerIn(int partner);
int portOpenStarsIn(void);
/* a place's flag (kTtydApLocations' gsw: type 0 a GSW at least value, 1 a GSWF) */
int portOpenPlaceFound(int type, int offset, int value);
/* the file's stamp: its seed, what is shuffled, its goal and its logic's version */
void portOpenStampRead(char* seed, int seedMax, unsigned* pools, int* goal, int* logic);

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
/* msgSearch: the open world's own messages (the partners' items, the trips'
 * refusals), or NULL */
const char* portOpenMsg(const char* tag);
/* the one-way trip (portEvtOpenTripReady's) can be finished from what Mario
 * holds and the places found, on this file's seed (1 if it isn't one):
 * port/archipelago/open_world_trips.cpp, on every backend */
int portOpenTripCanEnd(int trip);
/* the area's partner scene (all_party_lecture), from portOpenAreaLinked */
void portOpenSetLecture(void* script);

/* the item ids a partner's item takes in this file (the game's unused key items) */
int portOpenPartnerToken(int partner);

/* tik_05, the Thousand-Year Door's scene: Crystal Star 1..7 is shown (1
 * unless this is an open-world file and Mario hasn't got it) */
int portOpenStarShown(int star);
/* tik_05, the pedestal's scene after the n-th chapter done (Rogueport's
 * stretch n): the Crystal Star of that chapter. Script function: args[0]
 * holds n - 1 and gets that star's number - 1 (unchanged in any other file,
 * and for a chapter with no star) */
int portEvtOpenStretchStar(struct EventEntry* event, int isFirstCall);

/* A black chest (gon_06, usu_01, dou_07): the item the seed put in the chest
 * whose own item is this curse, when it is anything else; 0 leaves the chest
 * as the game has it (its curse and the Paper Mage's lesson). The C++ side's
 * lookup and the script function (args[0] the curse, args[1] out). */
int portOpenChestReward(int curse);
int portEvtOpenChest(struct EventEntry* event, int isFirstCall);

/* evt_msg.c: 1 to skip a line the party speaks (speaker "party", or
 * evt_msg_print_party) in a joining scene that gave something other than
 * its partner: the partner out would say the joining partner's words */
int portOpenMuteLine(struct EventEntry* event, const char* speaker);

#ifdef __cplusplus
}
#endif

#endif
