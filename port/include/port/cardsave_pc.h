#ifndef PORT_CARDSAVE_PC_H
#define PORT_CARDSAVE_PC_H
/* Memory card saves on PC (src/manager/cardmgr.c). See docs/PC_PORT.md,
 * "Card saves".
 *
 * Policy: the card file ("mariost_save_file", written by Aurora's CARD as a
 * Dolphin-compatible .gci) keeps the GameCube byte image: big-endian scalars,
 * the GameCube GlobalWork layout (0x13D8 bytes, 32-bit pointer slots written
 * as 0) and GameCube checksums (byte sums over the GameCube image). Saves can
 * be moved between Dolphin/GameCube and the PC port in both directions.
 *
 * The game keeps working on native CardSaveSlot/CardMetadata structs. cardmgr's
 * memcard_read/memcard_write go through a staging buffer:
 *   write: portCardWriteImage converts the native region to the card image;
 *   read:  portCardReadBegin hands out the staging buffer, portCardReadEnd
 *          (from readCallback) converts it into the caller's native buffer.
 * A slot whose GameCube checksums/size do not verify is converted with
 * checksums that fail the game's dataCheck, so corrupt saves stay rejected;
 * a valid one gets native checksums (byte sum over the native data) so
 * dataCheck accepts it unchanged.
 *
 * Regions are identified by card offset: [0, 0x2000) metadata, then 0x4000-byte
 * slots (4 main + 4 backup). Anything else is copied raw. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_CARD_META_SIZE 0x2000u
#define PORT_CARD_SLOT_SIZE 0x4000u
/* sizeof(CardSaveSlot.data) on GameCube */
#ifdef VERSION_G8ME01
#define PORT_CARD_GC_DATA_SIZE 0x2260u
#else
#define PORT_CARD_GC_DATA_SIZE 0x2140u
#endif

/* Native slot (CardSaveSlot) <-> card image (PORT_CARD_SLOT_SIZE bytes).
 * Return nonzero when the source checksums verified. */
int portCardSlotToImage(const void* nativeSlot, uint8_t* image);
int portCardSlotFromImage(const uint8_t* image, void* nativeSlot);
/* Native CardMetadata <-> card image (PORT_CARD_META_SIZE bytes). The layout
 * is the same; only the u16 options and the checksums swap. */
void portCardMetaToImage(const void* nativeMeta, uint8_t* image);
void portCardMetaFromImage(const uint8_t* image, void* nativeMeta);

/* cardmgr glue: returns the buffer to pass to CARDWriteAsync. */
void* portCardWriteImage(const void* native, int32_t length, int32_t offset);
/* Called before each of the game's card writes, when set (the save backups:
 * port/settings/features/saves.cpp). */
extern void (*gPortCardWillWrite)(void);
/* cardmgr glue: returns the buffer to pass to CARDReadAsync; the conversion
 * into 'native' happens in portCardReadEnd (call it from the read callback
 * with the CARD result). */
void* portCardReadBegin(void* native, int32_t length, int32_t offset);
void portCardReadEnd(int32_t result);

#ifdef __cplusplus
}
#endif
#endif
