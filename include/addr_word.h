// AddrWord: a pointer-sized integer (u32) for address arithmetic, and ADDR_WORD to relocate a pointer word in place.
#pragma once

#include <dolphin/types.h>

/* A memory address held as an integer, for address arithmetic: relocating
 * the pointer words of a loaded file between offsets and addresses, heap
 * bounds and alignment, ARAM transfer addresses. The size of a pointer
 * (u32). */
#ifdef TARGET_PC
#include <stdint.h>
typedef uintptr_t AddrWord; /* PC port: addresses are pointer-sized */
#else
typedef u32 AddrWord;
#endif

/* A pointer word of loaded data seen as an address, so it can be relocated
 * in place (offset <-> address). */
#define ADDR_WORD(f) (*(AddrWord*)&(f))
