// SfxNameWord: a sound-name parameter that holds either a name pointer or an id (SFX_NAME_WORD, SFX_NAME_IS_ID).
#pragma once

#include <dolphin/types.h>

/* A sound name parameter takes a name or an id. Names are addresses in main
 * memory (0x80000000 and up); a word that is non-negative as an s32 is an id
 * and is used as the index directly. SfxNameWord is that name-or-id word.
 *
 * SFX_NAME_WORD(x) passes an id, or a name literal through the word: for a
 * literal it is the same pointer, but the compiler then treats the address as
 * a loop-invariant word and loads it once (an @ temp), which is the shape
 * some callers were compiled with. */
#ifdef TARGET_PC
/* PC port: names are host pointers, normally 2 GiB or higher on 64-bit
 * hosts; ids, with their flag bits (up to 0x6000000 for environment sounds),
 * stay below that. So, as on GameCube, a word below 0x80000000 is an id;
 * but when some host memory lies under 2 GiB (Windows with bottom-up or
 * high-entropy ASLR off: port/include/port/low_address.h), a word there that
 * points at text is a name. */
#include <stdint.h>
#include "port/low_address.h"
typedef uintptr_t SfxNameWord;
#define SFX_NAME_WORD(lit) ((const char*)(SfxNameWord)(lit))
#define SFX_NAME_ID(name) ((s32)(SfxNameWord)(name))
#define SFX_NAME_IS_ID(name) (!portWordIsHostText((SfxNameWord)(name), 0x7FFFFFFFu))
#else
typedef u32 SfxNameWord;
#define SFX_NAME_WORD(lit) ((const char*)(SfxNameWord)(lit))
/* the id a name parameter holds, when SFX_NAME_IS_ID (a direct s32 cast: the
 * SfxNameWord round trip changes register allocation in psndENVSearch) */
#define SFX_NAME_ID(name) ((s32)(name))
#define SFX_NAME_IS_ID(name) (SFX_NAME_ID(name) >= 0)

#endif