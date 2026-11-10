#ifndef PORT_SAVE_CONVERT_H
#define PORT_SAVE_CONVERT_H
/* A save file (.gci, the GameCube card image: port/include/port/cardsave_pc.h)
 * of one release in another's layout (port/src/common/save_convert_pc.c), for
 * the settings page's "Saves" tab. US and PAL saves are the same but for the
 * game code; JP's Pianta Parlor records are four rows, not eight (0x120 bytes
 * less), and its slots say version "009", not "010". */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The release a .gci is from ('J', 'E' or 'P', its game code's last letter),
 * or 0 when it isn't a save of this game (that includes any size but the
 * game's own, 0x40 + 0x2000 + 8 * 0x4000 bytes: 17 blocks). */
char portSaveRelease(const uint8_t* gci, size_t size);

/* Writes gci as a save of release to ('J', 'E' or 'P') into out (size bytes,
 * not gci itself). Slots that don't verify stay unreadable. Returns the
 * number of slots converted, -1 when gci isn't a save of this game. */
int portSaveConvert(const uint8_t* gci, size_t size, char to, uint8_t* out);

/* The .gci's name for a release: "01-G8MJ-mariost_save_file.gci". */
void portSaveFileName(char release, char* out, size_t cap);

/* A copy for Dolphin or a GameCube: the GameCube game reads Paper Cut's own
 * items (the badge set's, the badge maker's, Zess T.'s new dishes: ids past
 * its item table, port/include/port/battlex.h) past the end of its table. In
 * gci (size bytes, changed in place) every slot that verifies has them sold
 * at the price a shop pays, the coins added (up to 999); a worn one taken off
 * and the HP, FP and BP worked out again as the game does. Returns how many
 * were sold (coins, when not NULL: what they paid), -1 when gci isn't a save
 * of this game. */
int portSaveDolphinSafe(uint8_t* gci, size_t size, int* coins);
/* 1 when a slot of gci holds one of Paper Cut's own items. */
int portSaveHasPortItems(const uint8_t* gci, size_t size);

#ifdef __cplusplus
}
#endif
#endif
