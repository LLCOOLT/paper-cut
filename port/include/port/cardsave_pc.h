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

/* Paper Cut's own block in each slot: text ("key=value" lines, NUL-ended) in
 * the part of the 0x4000-byte slot the game never fills nor checksums
 * (PORT_CARD_EXTRA_AT..+PORT_CARD_EXTRA_SIZE, after the GameCube data), so the
 * .gci stays the GameCube's and Dolphin's: they ignore it (a save written
 * there has none). It goes wherever the card file goes (online's setup
 * exchange, save transfer, backups): the save file's id and its own settings
 * (port/settings/features/save_files.cpp). Kept per slot here as the card is
 * read and written; slot 0-3. */
#define PORT_CARD_EXTRA_AT 0x3800u
#define PORT_CARD_EXTRA_SIZE 0x7F0u
#define PORT_CARD_EXTRA_MAGIC "pcx1\n"
const char* portCardSlotExtra(int slot); /* "" when none */
void portCardSlotExtraSet(int slot, const char* text);
/* The block from a slot image (NULL when it has none). */
const char* portCardImageExtra(const uint8_t* image);
/* cardmgr's calls: a save into slot (cardWrite), a copy (cardCopy), an erase,
 * a slot loaded into the game (cardCopy2Main) and a slot put in place
 * without the card (portCardLoadSlotImage: quick resume, warps, whose load
 * keeps the settings as they are). */
void portCardExtraSave(int slot);
void portCardExtraCopy(int from, int to);
void portCardExtraErase(int slot);
void portCardExtraLoaded(int slot);
void portCardExtraKeepNext(int slot);
/* The save files' side (save_files.cpp), each NULL until set: the block for
 * a save (old: the slot's block so far) or a copy into out; a slot's block as
 * its save is loaded; a main slot's image just before it is written. */
extern void (*gPortCardExtraForSave)(int slot, const char* old, int copy, char* out, int cap);
extern void (*gPortCardExtraApply)(int slot, const char* text);
extern void (*gPortCardSlotWritten)(int slot, const uint8_t* image);

#ifdef __cplusplus
}
#endif
#endif
