#pragma once

#include <dolphin/types.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>

/* Maya effect animation key (read by effCalcMayaAnimMatrix through GQR5: s16 values, 6 fraction bits) */
typedef struct MayaHermitChannel {
    s16 value;      // 0x0
    s16 inTangent;  // 0x2
    s16 outTangent; // 0x4
    u8 pad_06;   // 0x6, never accessed
    u8 step;        // 0x7, nonzero: hold value, no interpolation
} MayaHermitChannel;

/* 22 channels: translate xyz, rotate xyz, scale xyz, pivot xyz, translate2 xyz, pivot2 xyz, translate3 xyz, alpha */
typedef struct MayaHermitKey {
    s16 time;                  // 0x0
    MayaHermitChannel ch[22];  // 0x2
} MayaHermitKey;

/* An animated part: its keys, and the triangle fans drawn with it (effDrawMayaPoly) */
typedef struct MayaPoly {
    s32 keyCount;         // 0x0
    MayaHermitKey* keys;  // 0x4
    s32 count;            // 0x8, triangle fans
    s16** strips;         // 0xC
} MayaPoly;

typedef struct MayaKeyList {
    s32 count;       // 0x0
    MayaPoly* polys; // 0x4
} MayaKeyList;

typedef struct MayaAnimData {
    u8 pad0[4];          // 0x0
    f32 endTime;         // 0x4
    MayaKeyList* lists;  // 0x8
} MayaAnimData;

typedef struct MayaJoint {
    Mtx mtx;    // 0x0
    f32 alpha;  // 0x30
} MayaJoint;

/* effMayaAnimAlloc: the header (0x18) and one joint per animated part */
typedef struct MayaAnim {
    u16 flags;            // 0x0, 1 playing, 2 ended
    f32 time;             // 0x4
    OSTime startTime;     // 0x8
    MayaAnimData* data;   // 0x10
    MayaJoint joints[1];  // 0x18
} MayaAnim;
