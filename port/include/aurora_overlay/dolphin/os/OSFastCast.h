/* Aurora build: replaces Aurora's <dolphin/os/OSFastCast.h>, whose float to
 * integer casts are plain C casts (undefined out of range). The GameCube's
 * quantized stores (psq_st) saturate to the integer range and truncate, as
 * the 2004 SDK's PC version (libs/dolsdk2004) does; the game relies on it
 * (n64 effects, animdrv, hitdrv). */
#ifndef _DOLPHIN_OSFASTCAST_H_
#define _DOLPHIN_OSFASTCAST_H_

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void OSInitFastCast(void) {}

static inline s16 __OSf32tos16(f32 inF) {
    if (inF >= 32767.0f) return 32767;
    if (inF <= -32768.0f) return -32768;
    return (s16)inF;
}
static inline void OSf32tos16(f32* f, s16* out) { *out = __OSf32tos16(*f); }

static inline u8 __OSf32tou8(f32 inF) {
    if (inF >= 255.0f) return 255;
    if (inF <= 0.0f) return 0;
    return (u8)inF;
}
static inline void OSf32tou8(f32* f, u8* out) { *out = __OSf32tou8(*f); }

static inline s8 __OSf32tos8(f32 inF) {
    if (inF >= 127.0f) return 127;
    if (inF <= -128.0f) return -128;
    return (s8)inF;
}
static inline void OSf32tos8(f32* f, s8* out) { *out = __OSf32tos8(*f); }

static inline u16 __OSf32tou16(f32 inF) {
    if (inF >= 65535.0f) return 65535;
    if (inF <= 0.0f) return 0;
    return (u16)inF;
}
static inline void OSf32tou16(f32* f, u16* out) { *out = __OSf32tou16(*f); }

static inline f32 __OSs8tof32(const s8* arg) { return (f32)*arg; }
static inline void OSs8tof32(const s8* in, f32* out) { *out = __OSs8tof32(in); }

/* psq_l's dequantization by GQR5's load scale (a signed 6-bit power of two),
 * the register as the game last set it (hwaccess.h HW_SET_GQR5). */
extern u32 portGQR5;
static inline f32 __OSs16tof32(const s16* in) {
    union {
        u32 u;
        f32 f;
    } scale;
    s32 n = (s32)((portGQR5 >> 24) & 0x3F);
    if (n == 0) {
        return (f32)*in;
    }
    if (n & 0x20) {
        n -= 0x40;
    }
    scale.u = (u32)(127 - n) << 23; /* 2^-n, exact */
    return (f32)*in * scale.f;
}
static inline void OSs16tof32(const s16* in, f32* out) { *out = __OSs16tof32(in); }

static inline f32 __OSu8tof32(const u8* arg) { return (f32)*arg; }
static inline void OSu8tof32(const u8* in, f32* out) { *out = __OSu8tof32(in); }

static inline f32 __OSu16tof32(const u16* arg) { return (f32)*arg; }
static inline void OSu16tof32(const u16* in, f32* out) { *out = __OSu16tof32(in); }

#ifdef __cplusplus
}
#endif

#endif
