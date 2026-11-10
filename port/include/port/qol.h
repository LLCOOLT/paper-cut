#ifndef PORT_QOL_H
#define PORT_QOL_H

/* The remake's comfort and rule changes (port/src/common/qol_pc.c), each a
 * declared setting, off by default (Enhancements tab, port/settings/features/
 * qol.cpp); the "Remake rules" preset turns them on together
 * (port/settings/features/remake_rules.inc). The rule changes are marked
 * "changes play": Console-accurate mode locks them and online uses the
 * host's. The game code calls these from small TARGET_PC blocks. */

#ifdef __cplusplus
extern "C" {
#endif

/* Declares every setting here (safe to call again). */
void portQolDeclare(void);
/* A setting's value by key (declared on first use). */
int portQol(const char* key);

/* --- Coins up to 9,999 ("coin_cap"; Money Money up to 99 a battle) --- */
/* mario_pouch.c, statuswindow.c: the most coins Mario can carry (999, or 9,999). */
int portCoinCap(void);
/* npcdrv.c, a battle's coins to drop on the field: with the setting, up to 99,
 * and those over what the field's 32 item slots can hold paid at once. */
int portBattleCoinDrop(int coins);

/* --- Use a healing item at once when the bag is full ("full_bag_use") --- */
/* itemdrv.c, the full bag's list: the item chosen to go (the new one, or one
 * of the bag's) is eaten instead when it heals Mario or the partner; TRUE when
 * it was (the caller then doesn't throw it). */
int portFullBagUse(int itemId);

/* --- A level-up heals every partner ("levelup_heal_party") --- */
/* battle_seq_end.c, after the level-up's own heal of Mario and the partner out. */
void portLevelUpHealParty(void);

/* --- Retry in the Bowser intermission's pause ("bowser_retry") --- */
/* win_main.c, Bowser's pause screen each frame (buttons: this frame's
 * presses): Y loads the save state taken as the stage began (Retry on Game
 * Over's, gameplus_pc.c). TRUE when it asked to (the pause then closes). */
int portBowserRetry(int buttons);
/* gameplus_pc.c portRetryPoint: a room Bowser plays in keeps the retry state. */
int portBowserRetryRoom(void);

/* --- Left and Right scroll item and badge lists ("list_lr_scroll") --- */
/* win_item.c / win_badge.c: how many rows a Left or Right press moves the
 * cursor (0: off, the game's own). */
int portListPageRows(void);

/* --- Event commands: CALL(portEvtQolOn, PORT_QOL_EVT_*, LW(n)): LW(n) = 1 when the
 * change applies now, else 0 --- */
#define PORT_QOL_EVT_SHOP 1       /* evt_shop.c keep/unkeep: back to the list without the question ("shop_plus") */
#define PORT_QOL_EVT_SELL_AGAIN 2 /* evt_shop.c sell: the same, while the bag has something to sell */
#define PORT_QOL_EVT_MAIL_QUIET 3 /* evt_sub.c mail_evt: a notice instead of the scene ("mail_no_stop"; once
                                   * the mail tutorial was seen and a mail read); the notice is shown here */
#define PORT_QOL_EVT_PARTNER_HINT 4 /* the partner hint's event: the partner can speak now */
#define PORT_QOL_EVT_SELL_STORED 5 /* evt_shop.c sell: storage has an item to sell ("shop_plus") */
struct EventEntry;
int portEvtQolOn(struct EventEntry* event, int isFirstCall);

/* --- Shop improvements ("shop_plus") --- */
/* evt_shop.c name_disp: the line under the item's name (game encoding): how
 * many of it Mario holds (and has stored), and his shop points. */
void portShopLine(int itemId, int points, char* out, int size);

/* --- The partner hint ("partner_hint"; the Partner hint key or button,
 * port/settings/features/bindings.cpp) --- */
/* The hint control was pressed: the partner says where to go next, in a
 * speech window, once Mario is free in the field. */
void portPartnerHintPress(void);
/* Every field frame (portGameplusFrame): starts the hint's event when asked
 * and Mario is free. */
void portQolFrame(void);
/* seqdrv.c seqSetSeq, a map change (Skip the interludes on a replay): the map
 * and entrance, changed to Mario's side when an interlude seen before begins
 * (its flags and story value set); an interlude's end is recorded. */
void portInterludeMapChange(const char** map, const char** bero);
/* seq_mapchange.c _unload, after the managers' re-init: 1 when that map change
 * was a skipped interlude. The partner out is sent back (the interlude's scripts
 * do, so Mario's side puts Goombella out and heals); the caller then clears the
 * area's words (LSW/LSWF) as the trip through another area would have. */
int portInterludeUnload(void);
struct EventEntry;
/* An event command (CALL(portEvtPaceTrim, LW(n))): LW(n) = 1 when the Glitz Pit
 * and chapter 6 pace trims are on ("pace_trims"; not online): tou_04.c's guard
 * escorts to the ring after the first match; the Excess Express trips (gameplus_pc.c). */
int portEvtPaceTrim(struct EventEntry* event, int isFirstCall);
int portPaceTrims(void);

/* --- Hints for coins ("coin_hints"; the Journal's row, port/settings/features/qol.cpp) --- */
int portCoinHintPrice(void);
/* Pays the price for a hint: where a Star Piece or Shine Sprite is still
 * hidden (this area first, then areas Mario found something in), written to
 * out in the game's encoding. 1 bought; 0 too few coins; -1 none left; -2 not
 * in the field. */
int portCoinHintBuy(char* out, int size);

/* --- The Jukebox's rules ("jukebox_unlock": 0 the tracks heard on this
 * file, 1 also a share of the list per Shine Sprite found, 2 every track) --- */
/* gameplus2_pc.c portJukeboxKnown: track id is open by the rule. */
int portJukeboxUnlocked(int id);

/* --- The Notes page (the Start menu, port/settings/features/qol.cpp) --- */
/* The Trouble taken now (1-30), or 0 (none, or no game in the field). */
int portNotesTroubleTaken(void);
/* Trouble id solved (1) or not (0); -1 with no game. */
int portNotesTroubleSolved(int id);
/* Trouble id's client and what it asks, in the game's own words and
 * encoding (its global text; control tags taken out, lines kept). */
void portNotesTroubleText(int id, char* client, int clientSize, char* contents, int contentsSize);
/* Merluvlee's readings (evt_uranaisi.c records each one told: its message
 * label; gameplus2_pc.c keeps the last PORT_FORTUNE_COUNT beside the save). */
#define PORT_FORTUNE_COUNT 6
void portFortuneRecord(const char* label);
int portFortuneGet(int i, char* label, int size);
/* gameplus2_pc.c: the shop stock the restock notice last told of on this file
 * (0 none yet; -1 no game); set: record value. */
int portFileStock(int set, int value);
/* Tattle prompts ("talk_prompts"): gameplus2_pc.c keeps the field Tattles
 * heard on this file (an NPC's tattle message tag; party_kuribo.c marks one
 * heard): 1 heard (or no game), 0 new. portTattlePrompt (mario_balloon.c):
 * the "!" shows, Goombella out and an NPC with a Tattle not heard yet near. */
#define PORT_TATTLE_MAX 256
int portTattleHeard(const char* tag, int mark);
/* The field Tattle's message for an NPC (party_kuribo.c): the game's, the
 * NPC's name as its tag (Paper Cut's own NPCs have theirs under their names,
 * npc_tattles_aurora.c), else Goombella's line for one she can't place, not
 * the red "No messages" */
const char* portTattleTag(const char* name);
/* A module's own Tattle for an NPC of its own, by the moment (pm64_story_aurora.c: a story boss's while it frees a
 * partner): its tag, or NULL for the NPC's name's as above */
extern const char* (*gPortTattleTagHook)(const char* name);
int portTattlePrompt(void);
/* The Glitz Pit roster screen: tou.c gives its ranking's reader while its REL
 * is linked (NULL when unlinked); portTouRoster reads place i (0 the champion's
 * seat): the fighter's name and title (the game's text, read from the ring's
 * message file: the room's own may not have them) and whether it is Mario.
 * 0 past the end, or away from Glitzville. The reader gives message tags. */
typedef int (*PortTouRosterFn)(int i, const char** name, const char** title, int* isPlayer);
void portTouRosterRegister(PortTouRosterFn fn);
int portTouRoster(int i, const char** name, const char** title, int* isPlayer);
/* Reading i (0 the newest) in her own words, read from the disc's Rogueport
 * Sewers text (tik.txt) in the game's language and encoding, control tags
 * out; 0 when there is none. */
int portNotesFortune(int i, char* out, int size);

/* --- The recipe book (the Start menu, port/settings/features/qol.cpp) --- */
/* How many dishes the recipe log lists. */
int portRecipeCount(void);
/* Dish index's name (game encoding) and, when it was not cooked yet, a hint:
 * one ingredient of a way to make it. 1 cooked, 0 not yet, -1 none/no game. */
int portRecipeAt(int index, char* name, int nameSize, char* hint, int hintSize);

/* --- The save-file screen's details ("save_file_details"; seq_load.c) --- */
/* The "File N" label's colour for slot (0xRRGGBB), dimmer when not chosen. */
unsigned portSaveSlotColour(int slot, int selected);
/* A slot's line: its partner and the date it was saved (game encoding);
 * returns TRUE when the file's story is finished (the completion medal). */
struct GlobalWork;
int portSaveFileLine(const struct GlobalWork* global, int story, char* out, int size);

/* --- The keyboard in the game's name entry (a file's name, Yoshi's) --- */
/* nameent.c, each frame of the latin grid: what the desktop keyboard typed
 * (port/include/port/menu_typing.h) put in name (8 letters, space-padded) at
 * *nameLen, which moves; returns Enter and Esc as Start and B to OR into the
 * frame's presses. Always on: it is the keyboard, not a rule. */
unsigned portNameEntTyped(char* name, int* nameLen);

#ifdef __cplusplus
}
#endif

#endif
