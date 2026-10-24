#pragma once

#include <dolphin/types.h>

/* One script word: scripts store values and pointers (strings, other
 * scripts, data tables) in their words and variables. */
#ifdef TARGET_PC
#include <stdint.h>
typedef intptr_t EvtWord; /* PC port: script words hold pointers */
#else
typedef s32 EvtWord;
#endif
