#ifndef PORT_MUSYX_SWAP_PC_H
#define PORT_MUSYX_SWAP_PC_H

/* Big-endian MusyX data from the disc, made native on load (PC only).
 * port/src/common/musyx_swap.c. Every function tolerates truncated or
 * malformed input (it stops at the end of the buffer) and swaps each field
 * once even when structures are shared. */
#include <dolphin/types.h>

/* .proj: group chain, ID lists, song pages, MIDI setups, FX tables. */
void portMusyxSwapProj(void* data, u32 len);
/* .pool: macro steps (u32 pairs), keymaps, layers; curves stay as stored
 * (u8 tables and little-endian ADSR records). */
void portMusyxSwapPool(void* data, u32 len);
/* .sdir: native SDIR_DATA entries have a pointer field, so the directory is
 * rebuilt: native entries, then a copy of the original file with its ADPCM
 * info records swapped; extraData offsets are moved accordingly. */
u32 portMusyxSdirNativeSize(const void* data, u32 len);
void portMusyxSdirConvert(void* dst, const void* src, u32 len);
/* .slib: chunk sizes and each chunk's song (ARR at chunk + 0x20). */
void portMusyxSwapSlib(void* data, u32 len);
/* One MusyX song (ARR) of `len` bytes. */
void portMusyxSwapArr(void* arr, u32 len);
/* The game's .stm stream header (struct StreamHeader + ADPCM coefficients
 * at 0x5C/0xBC), 0x100 bytes. */
void portMusyxSwapStreamHeader(void* header);

#endif
