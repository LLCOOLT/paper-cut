#ifndef PORT_PRACTICE_MORE_H
#define PORT_PRACTICE_MORE_H

/* More of the Practice tab's powers (port/src/common/practice_more_pc.c):
 * a badge given and worn at once, in battle too (GitHub #13); each partner's
 * HP, max HP and rank (#3); a warp to any room, with a safe spawn (#4).
 * Every change marks the run (portPracticeTaint); none online or in a
 * replay (portPracticeInGame). Controls: port/settings/features/practice_more.cpp. */

#ifdef __cplusplus
extern "C" {
#endif

/* #13: gives the badge and wears it now (in a battle, Mario's or the
 * partner's counts follow at once; the BP isn't checked). FALSE: no room. */
int portPracticeGiveBadgeWorn(int id);

/* #3: a partner's (1 Goombella ... 7 Ms. Mowz, the pouch's ids) */
enum { PORT_PARTY_HP, PORT_PARTY_MAX_HP, PORT_PARTY_RANK, PORT_PARTY_FIELDS };
/* -1: not in the party */
int portPracticeParty(int partner, int field);
/* sets it (in a battle, the partner on the field too); max HP stays until
 * the next rank-up or HP Plus P change works it out again */
void portPracticeSetParty(int partner, int field, int value);
/* the partner's name in the game's own text (UTF-8) */
const char* portPracticePartyName(int partner, char* out, int size);

/* #4: the rooms (the area maps' list): how many, and each one's map id and
 * area name ("" where the game has none) */
int portRoomWarpCount(void);
const char* portRoomWarpMap(int i);
const char* portRoomWarpArea(int i);
/* the room's ways in (its exits' names), for a spawn other than the safe one */
int portRoomWarpEntrances(const char* map, const char** out, int max);
/* warps there (entrance NULL: the room's own start, safe), in the field
 * once no scene or menu is up; FALSE: refused (not in the field, online, a
 * replay, a mode running: portPracticeModeRefusal) */
int portRoomWarpRequest(const char* map, const char* entrance);
/* each game frame (practice_more_pc.c is called from portRulesFrame) */
void portRoomWarpFrame(void);

#ifdef __cplusplus
}
#endif

#endif
