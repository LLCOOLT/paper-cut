// system.c: angle and distance maths, interpolation, sorting, pad input (keyGet*), rumble and frame-time helpers.
#pragma once

#include <dolphin/mtx.h>
#include "version.h"
const char* getMarioStDvdRoot(void);
f32 reviseAngle(f32 angle);
f32 distABf(f32 x1, f32 y1, f32 x2, f32 y2);
f32 compAngle(f32 angle1, f32 angle2);
f32 angleABf(f32 x1, f32 y1, f32 x2, f32 y2);
/* An index into a table of 4-byte entries as the GameCube's 32-bit address arithmetic reads it: a NaN
 * distance makes angleABf's index INT_MIN, which wraps there to the table's own start (entry 0); with
 * 64-bit addresses it read 8 GB before the table and crashed (a sound effect at a NaN position, psndMain,
 * Great Tree mri_08). Valid indices are unchanged. */
#ifdef TARGET_PC
#define WRAP32_INDEX4(i) ((i) & 0x3FFFFFFF)
#else
#define WRAP32_INDEX4(i) (i)
#endif
/* An easing curve: a number (0-13), or the address of a curve function f32 (s32 step, s32 steps, f32 start,
 * f32 end), as gor_00's ship departures and muj_12's crystal star pass it. */
#ifdef TARGET_PC
#include <stdint.h>
typedef intptr_t IntplMode; /* PC port: wide enough for a function's address */
#else
typedef s32 IntplMode;
#endif
f32 intplGetValue(IntplMode mode, f32 start, f32 end, s32 currStep, s32 steps);
void qqsort(void* base, u32 n, u32 size, s32 (*compare)(const void* a, const void* b));
void makeKey(void);
u32 keyGetButton(u32 chan);
u32 keyGetButtonTrg(u32 chan);
u32 keyGetButtonRep(u32 chan);
u32 keyGetDir(u32 chan);
u32 keyGetDirTrg(u32 chan);
u32 keyGetDirRep(u32 chan);
s8 keyGetStickX(u32 chan);
s8 keyGetStickY(u32 chan);
s8 keyGetSubStickX(u32 chan);
s8 keyGetSubStickY(u32 chan);
void padRumbleOff(u32 chan);
void padRumbleOn(u32 chan);
void padRumbleHardOff(u32 chan);
u8 padGetRumbleStatus(u32 chan);
void sincosf(f32* ret_sin, f32* ret_cos, f32 degree);
void movePos(float* ret_sin, float* ret_cos, f32 dist, f32 degree);
s32 irand(s32 scalar);
u16 sysGetToken(void);
void sysWaitDrawSync(void);
void sysDummyDraw(Mtx mtx);
f32 getV60FPS(f32 a1, s64 a2, s64 a3);
f32 sysFrame2SecFloat(f32 frame);
s32 sysMsec2Frame(s32 msec);
f32 sysMsec2FrameFloat(f32 msec);
void mtxGetRotationElement(Mtx src, Mtx dst, s8 firstAxis, s8 secondAxis);
// PAL and the EU demo only (the functions exist only in the European builds; declared for every build so unused
// PAL-only code reads normally)
void sysDebugReport(const char* file, s32 line, const char* fmt, ...);                           // OSReport "[file:line] message"
BOOL __assert2(const char* filename, s32 line, const char* assertion, const char* message, ...); // panics with the message
s32 LZ77Decode(u8* input, u8* output);
void* memcpy_as4(void* dest, const void* src, u32 count);

inline Vec initializeVec(Vec* input);

extern const char* build_date_ptr;
extern const char* build_time_ptr;
