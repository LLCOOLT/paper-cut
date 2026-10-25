#pragma once

#include <dolphin/types.h>
#include "manager/evtmgr.h"

/* evt_win.c: the shop windows (evt_unitwin_*); a shop's own events get the work */
typedef struct UnitWinEntry {
    const char* mapObj; // 0x0
    const char* hitName; // 0x4
    void* evt;          // 0x8
} UnitWinEntry;

typedef struct UnitWinWork {
    u16 flags;             // 0x0
    u16 pad2;              // 0x2
    UnitWinEntry* entries; // 0x4
    s32* goods;            // 0x8
    u16 itemFlags[6];      // 0xC
    s32 goodsMax;          // 0x18
    EventEntry* evt;       // 0x1C
    EventEntry* childEvt;  // 0x20
    s32 goodsIdx;          // 0x24
    s32 timer;             // 0x28
    s32 coin;              // 0x2C
    s32 coinDisp;          // 0x30
    f32 cursor;            // 0x34
    u8 pad38[4];           // 0x38
    f32 cursorX;           // 0x3C
    f32 cursorY;           // 0x40
    f32 scroll;            // 0x44
    u8 pad48[4];           // 0x48
    s32 field_0x4C;        // 0x4C
    s32 field_0x50;        // 0x50
    u16 flags2;            // 0x54
    u8 pad56[2];           // 0x56
    s32 count;             // 0x58
    s32 field_0x5C;        // 0x5C
    s32 winId[3];          // 0x60
    u8 pad6C[0xE0 - 0x6C]; // 0x6C
    s32 selected;          // 0xE0
    s32 bought;            // 0xE4
} UnitWinWork;
