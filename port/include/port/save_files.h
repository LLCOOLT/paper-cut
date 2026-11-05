#ifndef PORT_SAVE_FILES_H
#define PORT_SAVE_FILES_H
/* Unlimited save files (port/settings/features/save_files.cpp): every save
 * the game writes is also kept in a library of named files, beside the
 * memory card's four, each with a thumbnail of where it was saved; any of
 * them goes back into one of the card's files to be played. The card stays
 * the GameCube's (Dolphin's) .gci. What a slot image says about its save
 * (port/src/common/save_info_pc.c), from the GameCube image itself: */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortSaveInfo {
    int valid;          /* a save (not empty, not broken) */
    char name[64];      /* the file's player name, UTF-8 */
    char map[16];       /* the map it was saved on ("gor_01") */
    int story;          /* GSW 0 */
    int chapter;        /* 0 the prologue, 1-8, 9 after the end */
    int level;          /* Mario's level */
    int stars;          /* Crystal Stars (star powers) obtained */
    long long seconds;  /* play time */
    long long saveTime; /* OSTime of the save, in seconds since 2000-01-01 */
    int saveCount;
} PortSaveInfo;

/* Reads a slot's GameCube image (PORT_CARD_SLOT_SIZE bytes); returns info->valid. */
int portSaveImageInfo(const uint8_t* image, PortSaveInfo* info);

#ifdef __cplusplus
}
#endif
#endif
