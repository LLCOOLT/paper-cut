// swdrv.c: the saved work driver: global (swByteGet, swGet...) and area-local (_swGet...) saved words and flags.
#pragma once

#include <dolphin/types.h>

void swInit(void);
void swReInit(void);

// Local Saved Work
s32 _swByteGet(s32 index);
void _swByteSet(s32 index, u8 value);
// Local Saved Work Flags
void _swClear(s32 index);
BOOL _swGet(s32 index);
void _swSet(s32 index);
// Global Saved Work
s32 swByteGet(s32 index);
void swByteSet(s32 index, s32 value);
#ifdef TARGET_PC
// PC port: GSW(0), the story, as swByteGet(0) reads it but with no side effects (an open-world file's
// Rogueport catch-up and room bookkeeping): for per-machine readers (menus, Discord, achievements), which
// must not change game state in online lockstep
s32 swStoryPeek(void);
#endif
// Global Saved Work Flags
void swClear(s32 index);
BOOL swGet(s32 index);
void swSet(s32 index);
