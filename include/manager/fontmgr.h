// fontmgr.c: the font manager: the binary font (BFN) file structures and text drawing.
#pragma once

#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/types.h>

#ifndef PORT_ENUM_BASE
#define PORT_ENUM_BASE(type)
#endif
typedef enum BinaryFontEncoding PORT_ENUM_BASE(int) {
    SINGLE_BYTE_ENCODING = 0,
    DOUBLE_BYTE_ENCODING = 1,
    MULTI_BYTE_ENCODING = 2
} BinaryFontEncoding;

typedef enum BinaryFontMapping PORT_ENUM_BASE(int) {
    LINEAR_CHARACTER_MAP = 0,
    SJIS_CHARACTER_MAP = 1,
    TABLE_CHARACTER_MAP = 2,
    MAP_CHARACTER_MAP = 3
} BinaryFontMapping;

typedef struct BinaryFontBlock {
    u32 magic; // 0x0, FourCC
    u32 size;  // 0x4, full size of this block
} BinaryFontBlock;

// BFN = Binary FoNt
typedef struct BinaryFontHeader {
    u32 magic;      // 0x0, "FONT"
    u32 version;    // 0x4, "bfn1"
    u32 size;       // 0x8, full size of the file
    u32 blockcount; // 0xC, doesn't include header
    u8 align[16];   // 0x10-0x1F
} BinaryFontHeader;

// INF1 section
typedef struct BinaryFontInfo {
    u32 magic;    // 0x0, "INF1"
    u32 size;     // 0x4, full size of this block
    u16 encoding; // 0x8, see BinaryFontEncoding
    u16 ascent;   // 0xA, how far above baseline
    u16 descent;  // 0xC, how far below baseline
    u16 width;    // 0xE, character width
    u16 leading;  // 0x10, "line height"
    u16 replace;  // 0x12, replacement code for unmapped characters
    u8 align[12]; // 0x14-0x1F
} BinaryFontInfo;

// GLY1 section
typedef struct BinaryFontGlyph {
    u32 magic;        // 0x0, "GLY1"
    u32 size;         // 0x4, full size of this block
    u16 firstcode;    // 0x8, first code in this glyph block (inclusive)
    u16 lastcode;     // 0xA, last code in this glyph block (inclusive)
    u16 cellwidth;    // 0xC
    u16 cellheight;   // 0xE
    u32 sheetsize;    // 0x10, includes padding
    u16 format;       // 0x14, GXTexFmt
    u16 sheetrows;    // 0x16
    u16 sheetcolumns; // 0x18
    u16 sheetwidth;   // 0x1A
    u16 sheetheight;  // 0x1C
    u8 align[2];      // 0x1E-0x1F
} BinaryFontGlyph;

typedef struct BinaryFontMap {
    u32 magic;     // 0x0, "MAP1"
    u32 size;      // 0x4, full size of this block
    u16 mapping;   // 0x8, see BinaryFontMapping
    u16 firstchar; // 0xA, first character in this block (inclusive)
    u16 lastchar;  // 0xC, last character in this block (inclusive)
    u16 count;     // 0xE, number of mapping entries
    u16 table[];   // 0x10, unknown size
} BinaryFontMap;

typedef struct BinaryFontWidth {
    u32 magic;     // 0x0, "WID1"
    u32 size;      // 0x4, full size of this block
    u16 firstcode; // 0x8, first code for this block (inclusive)
    u16 lastcode;  // 0xA, last code for this block (inclusive)
} BinaryFontWidth;

void fontmgrInit(void);
void fontmgrTexSetup(void);
void FontDrawStart(void);
void FontDrawStart_alpha(u8 alpha);
u16 FontGetMessageWidthLine(const char* msg, u16* lines);
void FontDrawMessage(s32 x, s32 y, const char* msg);
u16 FontGetMessageWidth(const char* message);
void FontDrawStringMtx(Mtx mtx, const char* message);
void FontDrawStringShake(f32 x, f32 y, const char* msg); // each character moved by a random 0-2 times the font scale
void FontDrawColor(GXColor color);

u16 hankakuSearch(s32 c);
u16 kanjiSearch(u32 code);
u16 kanjiGetWidth(u16 code);

void FontDrawEdge(void);
void FontDrawEdgeOff(void);
void FontDrawMessageMtx(Mtx mtx, const char* msg);

GXColor HSV2RGB(GXColor hsv);
void FontDrawColorIDX(s32 idx);
void FontDrawRainbowColor(void);
void FontDrawRainbowColorOff(void);
void FontDrawNoise(void);
void FontDrawNoiseOff(void);
void FontDrawColor_(void);
GXColor FontGetDrawColor(void);
void FontDrawScale(f32 scale);
void FontDrawScaleVec(Vec scale);
void FontDrawCode(f32 x, f32 y, u16 code);
void FontDrawCodeMtx(Mtx mtx, u16 code);
void FontDrawString(f32 x, f32 y, const char* msg);
void FontDrawStringPitch(f32 x, f32 y, f32 pitch, const char* msg);
void FontDrawStringVecPitch(Vec pos, f32 pitch, const s8* msg);
void JUTFont_Free(void);
void JUTFontSetup(u16 font);
void FontDrawStringCenterMtx(Mtx mtx, const s8* msg);

#ifdef TTYD_PORT_SOURCE
/* Paper Cut's own code (port/src): whatever it draws, the game's random numbers are put back after
 * (port/include/port/port_text.h) */
#include "port/port_text.h"
#define FontDrawMessage(...) PORT_TEXT_CALL(FontDrawMessage(__VA_ARGS__))
#define FontDrawMessageMtx(...) PORT_TEXT_CALL(FontDrawMessageMtx(__VA_ARGS__))
#define FontDrawStringMtx(...) PORT_TEXT_CALL(FontDrawStringMtx(__VA_ARGS__))
#define FontDrawStringShake(...) PORT_TEXT_CALL(FontDrawStringShake(__VA_ARGS__))
#define FontDrawCode(...) PORT_TEXT_CALL(FontDrawCode(__VA_ARGS__))
#define FontDrawCodeMtx(...) PORT_TEXT_CALL(FontDrawCodeMtx(__VA_ARGS__))
#define FontDrawString(...) PORT_TEXT_CALL(FontDrawString(__VA_ARGS__))
#define FontDrawStringPitch(...) PORT_TEXT_CALL(FontDrawStringPitch(__VA_ARGS__))
#define FontDrawStringVecPitch(...) PORT_TEXT_CALL(FontDrawStringVecPitch(__VA_ARGS__))
#define FontDrawStringCenterMtx(...) PORT_TEXT_CALL(FontDrawStringCenterMtx(__VA_ARGS__))
#endif
