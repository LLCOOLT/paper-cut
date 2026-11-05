#ifndef PORT_POSTGAME_H
#define PORT_POSTGAME_H

/* Post-game places and modes (docs/MODES.md), each off until started:
 *
 * - The trophy room (port/src/aurora/trophy_room_aurora.c): a room under
 *   Rogueport with the file's Crystal Stars on their pedestals, the bosses
 *   it has beaten as paper cut-outs, and the achievements' plaques.
 * - The Paper Tower (port/src/aurora/paper_tower_aurora.c): a climb of 30
 *   floors made of the game's own rooms, remixed with Paper-mode gaps,
 *   Tube-mode gaps and wind walls, a battle every fifth floor.
 * - Daily shop specials (port/src/aurora/daily_shop_aurora.c): a few shops
 *   stock one rare item at a discount each day (port/include/port/date_seed.h).
 *
 * Each one moves Mario or changes what a shop sells, so none runs during
 * online co-op or a replay. The settings page (port/settings/features/
 * postgame.cpp) and the Start menu's forms ask through these calls, which
 * are safe from their threads; the game side runs on the game's frame. */

#ifdef __cplusplus
extern "C" {
#endif

/* --- the trophy room ------------------------------------------------------- */

/* Visits it at the next field frame; 0 and why when it can't now. */
int portTrophyRoomVisit(const char** why);
/* Mario is in it (the field), and stands on its pedestal (the plaques show). */
int portTrophyRoomHere(void);
int portTrophyRoomOnPedestal(void);
/* Mario is in it with every Crystal Star and every cut-out set out (an achievement's). */
int portTrophyRoomFull(void);

/* --- the Paper Tower ------------------------------------------------------- */

#define PORT_TOWER_FLOORS 30

enum { PORT_TOWER_WALK, PORT_TOWER_PAPER, PORT_TOWER_TUBE, PORT_TOWER_WIND, PORT_TOWER_MIXED, PORT_TOWER_BATTLE };

/* The file has finished the game. */
int portTowerUnlocked(void);
/* A climb from the first floor ("paper_tower_seed": 0 the day's, as the
 * daily specials'); 0 and why when it can't now. */
int portTowerStart(const char** why);
/* Ends it: Mario goes back to the room he started from. */
void portTowerStop(void);
int portTowerActive(void);
int portTowerFloor(void);      /* 1-30 */
int portTowerChallenge(void);  /* this floor's PORT_TOWER_* */
unsigned portTowerSeed(void);  /* the climb's */
unsigned long long portTowerElapsed(void);
/* Past this floor without clearing it (a room that won't let Mario through):
 * the climb counts as helped (no best, no achievements). 0 and why. */
int portTowerSkip(const char** why);
int portTowerHelped(void);
/* The last climb's end (the floor reached, ms, cleared); a count that changes as each ends. */
int portTowerResult(int* floor, unsigned long long* ms, int* cleared);
/* The highest floor and the best time to the top (0: none), unhelped climbs only. */
int portTowerBest(int* floor, unsigned long long* ms);

/* --- daily shop specials --------------------------------------------------- */

/* Today's special in shop i (the shops' list, port order): its item, price
 * and the usual price; 0 when the setting is off or there's none. */
int portDailyShopCount(void);
const char* portDailyShopName(int i); /* English: "Rogueport's item shop" */
int portDailyShopSpecial(int i, int* item, int* price, int* usual);
/* The shop whose room Mario is in, with a special out today (-1: none). */
int portDailyShopHere(void);
/* An item bought at a special's price (for achievements): how many, ever. */
int portDailySpecialsBought(void);

/* --- the game side (port/src/aurora, from modes_aurora.c) ------------------ */

void portPostgameInit(void);   /* the maps and the hooks, once */
void portTrophyRoomFrame(void);
void portTrophyRoomRegisterMap(void);
void portTowerFrame(void);
void portTowerRegisterMaps(void);
void portDailyShopInit(void);

#ifdef __cplusplus
}
#endif

#endif
