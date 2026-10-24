/* MusyX disc data swapper: builds big-endian .proj/.pool/.sdir/song/.stm
 * images with the GameCube layouts, converts them and reads them back
 * through the native MusyX structs, as libs/musyx does. */
#include <dolphin/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <musyx/synthdata.h>
#include <musyx/seq.h>
#include <musyx/stream.h>
#include "port/musyx_swap_pc.h"

static int sFailures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            sFailures++;                                                                           \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void be16(u8* p, u32 off, u16 v) {
    p[off] = (u8)(v >> 8);
    p[off + 1] = (u8)v;
}

static void be32(u8* p, u32 off, u32 v) {
    p[off] = (u8)(v >> 24);
    p[off + 1] = (u8)(v >> 16);
    p[off + 2] = (u8)(v >> 8);
    p[off + 3] = (u8)v;
}

static u16 ne16(const u8* p, u32 off) {
    u16 v;
    memcpy(&v, p + off, 2);
    return v;
}

/* ---- .proj ---- */
static void testProj(void) {
    static u8 prj[0x400];
    GROUP_DATA* g;
    u16* list;
    PAGE* page;
    MIDISETUP* ms;
    FX_DATA* fx;

    memset(prj, 0xEE, sizeof(prj));
    /* group 0 @0: song group id 7 */
    be32(prj, 0x00, 0x100);   /* nextOff */
    be16(prj, 0x04, 7);       /* id */
    be16(prj, 0x06, 0);       /* type: song */
    be32(prj, 0x08, 0x40);    /* macroOff */
    be32(prj, 0x0C, 0x50);    /* sampleOff */
    be32(prj, 0x10, 0x60);    /* curveOff (shared with keymaps) */
    be32(prj, 0x14, 0x60);    /* keymapOff */
    be32(prj, 0x18, 0x68);    /* layerOff */
    be32(prj, 0x1C, 0x70);    /* normpage */
    be32(prj, 0x20, 0x80);    /* drumpage */
    be32(prj, 0x24, 0x90);    /* midiSetup */
    /* macro list: single 0x0102, range 0x8010..0x0020, end */
    be16(prj, 0x40, 0x0102);
    be16(prj, 0x42, 0x8010);
    be16(prj, 0x44, 0x0020);
    be16(prj, 0x46, 0xFFFF);
    be16(prj, 0x50, 0x0201);
    be16(prj, 0x52, 0xFFFF);
    be16(prj, 0x60, 0x0303);
    be16(prj, 0x62, 0xFFFF);
    be16(prj, 0x68, 0xFFFF);
    /* pages: macro 0x0405 index 0, then terminator index 0xFF */
    be16(prj, 0x70, 0x0405);
    prj[0x72] = 9;
    prj[0x73] = 4;
    prj[0x74] = 0;
    be16(prj, 0x76, 0x0506);
    prj[0x7A] = 0xFF;
    be16(prj, 0x80, 0x0607);
    prj[0x84] = 0xFF;
    /* midi setup: song 0x0011, then end */
    be16(prj, 0x90, 0x0011);
    be16(prj, 0x92, 0);
    prj[0x94] = 33; /* channel[0].program */
    be16(prj, 0x90 + 0x54, 0xFFFF);
    /* group 1 @0x100: fx group id 9 */
    be32(prj, 0x100, 0x180);
    be16(prj, 0x104, 9);
    be16(prj, 0x106, 1);
    be32(prj, 0x108, 0x40); /* shares the macro list of group 0 */
    be32(prj, 0x10C, 0x52);
    be32(prj, 0x110, 0x52);
    be32(prj, 0x114, 0x52);
    be32(prj, 0x118, 0x52);
    be32(prj, 0x11C, 0x140); /* FX table */
    be16(prj, 0x140, 2);
    be16(prj, 0x142, 0);
    be16(prj, 0x144, 0x0A0B);
    be16(prj, 0x146, 0x0C0D);
    prj[0x148] = 3;
    be16(prj, 0x14E, 0x0E0F);
    be16(prj, 0x150, 0x1011);
    /* terminator */
    be32(prj, 0x180, 0xFFFFFFFF);

    portMusyxSwapProj(prj, sizeof(prj));

    g = (GROUP_DATA*)prj;
    CHECK(g->nextOff == 0x100 && g->id == 7 && g->type == 0, "group0 header");
    CHECK(g->macroOff == 0x40 && g->sampleOff == 0x50 && g->layerOff == 0x68, "group0 offsets");
    list = (u16*)(prj + g->macroOff);
    CHECK(list[0] == 0x0102 && list[1] == 0x8010 && list[2] == 0x0020 && list[3] == 0xFFFF,
          "macro list %04x %04x %04x", list[0], list[1], list[2]);
    CHECK(ne16(prj, 0x50) == 0x0201 && ne16(prj, 0x60) == 0x0303, "sample/curve lists (shared once)");
    page = (PAGE*)(prj + g->data.song.normpageOff);
    CHECK(page[0].macro == 0x0405 && page[0].prio == 9 && page[1].macro == 0x0506 && page[1].index == 0xFF,
          "norm pages %04x %04x", page[0].macro, page[1].macro);
    page = (PAGE*)(prj + g->data.song.drumpageOff);
    CHECK(page[0].macro == 0x0607, "drum page");
    ms = (MIDISETUP*)(prj + g->data.song.midiSetupOff);
    CHECK(ms[0].songId == 0x11 && ms[0].channel[0].program == 33 && ms[1].songId == 0xFFFF, "midi setup");

    g = (GROUP_DATA*)(prj + g->nextOff);
    CHECK(g->id == 9 && g->type == 1 && g->nextOff == 0x180, "group1 header");
    fx = (FX_DATA*)(prj + g->data.fx.tableOff);
    CHECK(fx->num == 2 && fx->fx[0].id == 0x0A0B && fx->fx[0].macro == 0x0C0D && fx->fx[0].maxVoices == 3 &&
              fx->fx[1].id == 0x0E0F && fx->fx[1].macro == 0x1011,
          "fx table");
    g = (GROUP_DATA*)(prj + g->nextOff);
    CHECK(g->nextOff == 0xFFFFFFFF, "terminator");
    CHECK(prj[0x1C0] == 0xEE, "untouched bytes");
}

/* ---- .pool ---- */
static void testPool(void) {
    static u8 pool[0x200];
    POOL_DATA* pd;
    MEM_DATA* m;
    MSTEP* st;

    memset(pool, 0, sizeof(pool));
    be32(pool, 0x0, 0x10);  /* macros */
    be32(pool, 0x4, 0x40);  /* curves */
    be32(pool, 0x8, 0x60);  /* keymaps */
    be32(pool, 0xC, 0x90);  /* layers */
    /* macro 0x0005: two steps */
    be32(pool, 0x10, 0x18);
    be16(pool, 0x14, 5);
    be32(pool, 0x18, 0x11223344);
    be32(pool, 0x1C, 0x55667788);
    be32(pool, 0x20, 0x00000003);
    be32(pool, 0x24, 0x80000000);
    be32(pool, 0x28, 0xFFFFFFFF);
    /* curve 0x0001: bytes 1..8 (must stay as stored) */
    be32(pool, 0x40, 0x10);
    be16(pool, 0x44, 1);
    memcpy(pool + 0x48, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
    be32(pool, 0x50, 0xFFFFFFFF);
    /* keymap 0x4002: two KEYMAP entries */
    be32(pool, 0x60, 0x18);
    be16(pool, 0x64, 0x4002);
    be16(pool, 0x68, 0x0123);
    pool[0x6A] = 0xFE;
    pool[0x6B] = 64;
    be16(pool, 0x6C, (u16)-3);
    be16(pool, 0x70, 0x0456);
    be32(pool, 0x78, 0xFFFFFFFF);
    /* layer 0x8003: num 1 */
    be32(pool, 0x90, 0x18);
    be16(pool, 0x94, 0x8003);
    be32(pool, 0x98, 1);
    be16(pool, 0x9C, 0x0789);
    pool[0x9E] = 10;
    pool[0x9F] = 20;
    be16(pool, 0xA2, (u16)-7);
    be32(pool, 0xA8, 0xFFFFFFFF);

    portMusyxSwapPool(pool, sizeof(pool));

    pd = (POOL_DATA*)pool;
    CHECK(pd->macroOff == 0x10 && pd->curveOff == 0x40 && pd->keymapOff == 0x60 && pd->layerOff == 0x90,
          "pool header");
    m = (MEM_DATA*)(pool + pd->macroOff);
    CHECK(m->nextOff == 0x18 && m->id == 5, "macro header");
    st = (MSTEP*)&m->data.cmd;
    CHECK(st[0].para[0] == 0x11223344 && st[0].para[1] == 0x55667788 && st[1].para[0] == 3 &&
              st[1].para[1] == 0x80000000,
          "macro steps %08x", st[0].para[0]);
    CHECK(((MEM_DATA*)((u8*)m + m->nextOff))->nextOff == 0xFFFFFFFF, "macro end");
    m = (MEM_DATA*)(pool + pd->curveOff);
    CHECK(m->id == 1 && memcmp(m->data.tab, "\x01\x02\x03\x04\x05\x06\x07\x08", 8) == 0, "curve raw");
    m = (MEM_DATA*)(pool + pd->keymapOff);
    CHECK(m->id == 0x4002 && m->data.map[0].id == 0x0123 && m->data.map[0].transpose == -2 &&
              m->data.map[0].panning == 64 && m->data.map[0].prioOffset == -3 && m->data.map[1].id == 0x0456,
          "keymap");
    m = (MEM_DATA*)(pool + pd->layerOff);
    CHECK(m->id == 0x8003 && m->data.layer.num == 1 && m->data.layer.entry[0].id == 0x0789 &&
              m->data.layer.entry[0].keyLow == 10 && m->data.layer.entry[0].prioOffset == -7,
          "layer");
}

/* ---- .sdir ---- */
static void testSdir(void) {
    static u8 sd[0x100];
    u8* native;
    SDIR_DATA* e;
    SNDADPCMinfo* info;
    DSPADPCMplusInfo* plus;
    u32 size, i;

    memset(sd, 0, sizeof(sd));
    /* entry 0: ADPCM (type 0) with info at 0x80 */
    be16(sd, 0x00, 0x0010);
    be16(sd, 0x02, 0);
    be32(sd, 0x04, 0x1000);
    be32(sd, 0x0C, 0x3C007D00);       /* info: base note 60, 32000 Hz */
    be32(sd, 0x10, 0x00000064);       /* length 100, compType 0 */
    be32(sd, 0x14, 14);
    be32(sd, 0x18, 86);
    be32(sd, 0x1C, 0x80);
    /* entry 1: ADPCM+ (type 1, 28 samples -> 2 blocks) with info at 0xA8 */
    be16(sd, 0x20, 0x0011);
    be32(sd, 0x24, 0x2000);
    be32(sd, 0x30, 0x0100001C);
    be32(sd, 0x3C, 0xA8);
    /* entry 2: PCM16 (type 2), no info */
    be16(sd, 0x40, 0x0012);
    be32(sd, 0x50, 0x02000010);
    /* terminator, then the info records */
    be16(sd, 0x60, 0xFFFF);
    /* info @0x80: numCoef 8, PS 0x12, loopPS 0x34, loopY0 -2, loopY1 3, coef i*100-700 */
    be16(sd, 0x80, 8);
    sd[0x82] = 0x12;
    sd[0x83] = 0x34;
    be16(sd, 0x84, (u16)-2);
    be16(sd, 0x86, 3);
    for (i = 0; i < 16; i++) {
        be16(sd, 0x88 + i * 2, (u16)(s16)(i * 100 - 700));
    }
    /* plus info @0xA8 + 2 blocks */
    be16(sd, 0xA8, 8);
    for (i = 0; i < 16; i++) {
        be16(sd, 0xB0 + i * 2, (u16)(s16)(i - 8));
    }
    be16(sd, 0xD0, (u16)-100);
    be16(sd, 0xD2, 200);
    sd[0xD4] = 0x45;
    be16(sd, 0xD6, 300);
    be16(sd, 0xD8, (u16)-400);
    sd[0xDA] = 0x67;

    size = portMusyxSdirNativeSize(sd, sizeof(sd));
    native = malloc(size);
    portMusyxSdirConvert(native, sd, sizeof(sd));
    e = (SDIR_DATA*)native;
    CHECK(e[0].id == 0x10 && e[0].offset == 0x1000 && e[0].header.info == 0x3C007D00 &&
              e[0].header.length == 100 && e[0].header.loopOffset == 14 && e[0].header.loopLength == 86,
          "sdir entry 0");
    CHECK(e[1].id == 0x11 && e[1].offset == 0x2000 && (e[1].header.length >> 24) == 1, "sdir entry 1");
    CHECK(e[2].id == 0x12 && e[2].extraData == 0, "sdir entry 2");
    CHECK(e[3].id == 0xFFFF, "sdir terminator");
    /* dataGetSample: extraData is relative to the directory start */
    info = (SNDADPCMinfo*)(native + e[0].extraData);
    CHECK(info->numCoef == 8 && info->initialPS == 0x12 && info->loopPS == 0x34 && info->loopY0 == -2 &&
              info->loopY1 == 3 && info->coefTab[0][0] == -700 && info->coefTab[7][1] == 800,
          "adpcm info %d %d", info->coefTab[0][0], info->coefTab[7][1]);
    plus = (DSPADPCMplusInfo*)(native + e[1].extraData);
    CHECK(plus->coefTab[0][0] == -8 && plus->coefTab[7][1] == 7, "adpcm+ coefs");
    CHECK(plus->blk[0].Y0 == -100 && plus->blk[0].Y1 == 200 && plus->blk[0].PS == 0x45, "adpcm+ blk0");
    CHECK(plus->blk[1].Y0 == 300 && plus->blk[1].Y1 == -400 && plus->blk[1].PS == 0x67, "adpcm+ blk1");
    free(native);
}

/* ---- song (ARR) and .slib ---- */
static void testSlib(void) {
    static u8 lib[0x400];
    u8* a = lib + 0x20;
    ARR* arr;
    u32* tt;
    TENTRY* te;
    SEQ_PATTERN* pat;
    NOTE_DATA* nd;
    MTRACK_DATA* mt;
    u32 i;

    memset(lib, 0, sizeof(lib));
    be32(lib, 0, 0x300);           /* chunk size */
    be32(lib, 0x300, 0xFFFFFFFF);  /* end */
    /* ARR header */
    be32(a, 0x00, 0x60);           /* tTab */
    be32(a, 0x04, 0x160);          /* pTab */
    be32(a, 0x08, 0x170);          /* tmTab */
    be32(a, 0x0C, 0x1B0);          /* mTrack */
    be32(a, 0x10, 0x40000078);     /* info */
    be32(a, 0x14, 0x1234);         /* loopPoint[0] */
    be32(a, 0x54, 0);              /* tsTab */
    /* tTab: tracks 0 and 1 share the TENTRY list at 0x1D0 */
    be32(a, 0x60, 0x1D0);
    be32(a, 0x64, 0x1D0);
    /* track: pattern 1, loop entry (0xFFFE, target 0), end */
    be32(a, 0x1D0, 0x10);
    a[0x1D4] = 0xFF;
    a[0x1D5] = 100;
    be16(a, 0x1D8, 1);
    a[0x1DA] = (u8)-12;
    a[0x1DB] = 5;
    be32(a, 0x1DC, 0x200);
    be16(a, 0x1E4, 0xFFFE);
    be16(a, 0x1E6, 0);
    be16(a, 0x1F0, 0xFFFF);
    /* pTab: pattern 0 unused (0), pattern 1 @0x220 */
    be32(a, 0x160, 0);
    be32(a, 0x164, 0x220);
    /* pattern: headerLen, pitchBend, modulation; notes from +0xC */
    be32(a, 0x220, 0x0C);
    be32(a, 0x224, 0);
    be32(a, 0x228, 0);
    be16(a, 0x22C, 0x0102); /* note: time, key 60, vel 100, length 0x0304 */
    a[0x22E] = 60;
    a[0x22F] = 100;
    be16(a, 0x230, 0x0304);
    be16(a, 0x232, 0x0506); /* controller (4 bytes) */
    a[0x234] = 0x87;
    a[0x235] = 0x01;
    be16(a, 0x236, 0x0708); /* padding: key|vel == 0 (4 bytes) */
    be16(a, 0x23A, 0x090A); /* note */
    a[0x23C] = 61;
    a[0x23D] = 1;
    be16(a, 0x23E, 0x0B0C);
    be16(a, 0x240, 0x0D0E); /* end */
    a[0x242] = 0xFF;
    a[0x243] = 0xFF;
    /* tempo track */
    be32(a, 0x1B0, 0);
    be32(a, 0x1B4, 120);
    be32(a, 0x1B8, 0xFFFFFFFF);

    portMusyxSwapSlib(lib, sizeof(lib));

    CHECK(*(u32*)lib == 0x300 && *(u32*)(lib + 0x300) == 0xFFFFFFFF, "chunk sizes");
    arr = (ARR*)a;
    CHECK(arr->tTab == 0x60 && arr->pTab == 0x160 && arr->mTrack == 0x1B0 && arr->info == 0x40000078 &&
              arr->loopPoint[0] == 0x1234,
          "arr header");
    tt = (u32*)ARR_GET(arr, arr->tTab);
    CHECK(tt[0] == 0x1D0 && tt[1] == 0x1D0 && tt[2] == 0, "track table");
    te = (TENTRY*)ARR_GET(arr, tt[0]);
    CHECK(te[0].time == 0x10 && te[0].prgChange == 0xFF && te[0].velocity == 100 && te[0].pattern == 1 &&
              te[0].transpose == -12 && te[0].velocityAdd == 5,
          "tentry 0 (shared list swapped once)");
    CHECK(te[1].time == 0x200 && te[1].pattern == 0xFFFE && *(u16*)&te[1].transpose == 0, "loop entry");
    CHECK(te[2].pattern == 0xFFFF, "track end");
    pat = (SEQ_PATTERN*)ARR_GET(arr, ((u32*)ARR_GET(arr, arr->pTab))[te[0].pattern]);
    CHECK(pat->headerLen == 0x0C && pat->pitchBend == 0, "pattern header");
    nd = (NOTE_DATA*)&pat->noteData;
    CHECK(nd->time == 0x0102 && nd->key == 60 && nd->length == 0x0304, "note 0");
    nd++;
    CHECK(nd->time == 0x0506 && nd->key == 0x87, "controller");
    nd = (NOTE_DATA*)((u8*)nd + 4);
    CHECK(nd->time == 0x0708, "padding");
    nd = (NOTE_DATA*)((u8*)nd + 4);
    CHECK(nd->time == 0x090A && nd->key == 61 && nd->length == 0x0B0C, "note 1");
    nd++;
    CHECK(nd->time == 0x0D0E && nd->key == 0xFF, "pattern end");
    mt = (MTRACK_DATA*)ARR_GET(arr, arr->mTrack);
    CHECK(mt[0].time == 0 && mt[0].bpm == 120 && mt[1].time == 0xFFFFFFFF, "tempo track");
    (void)i;
}

/* A looping track ends at its 0xFFFE entry: no 0xFFFF follows, and the next
 * bytes (here the pattern table) must not be swapped as track entries. */
static void testArrLoopTrack(void) {
    static u8 a[0x200];
    ARR* arr;
    u32* pt;
    TENTRY* te;
    SEQ_PATTERN* pat;
    NOTE_DATA* nd;

    memset(a, 0, sizeof(a));
    be32(a, 0x00, 0x60);  /* tTab */
    be32(a, 0x04, 0x178); /* pTab */
    be32(a, 0x08, 0x1B0); /* tmTab */
    be32(a, 0x10, 0x78);  /* info */
    be32(a, 0x60, 0x160); /* track 0 */
    /* track 0: pattern 2 at time 0, then loop to entry 0 at 0x300 */
    a[0x164] = 0xFF;
    a[0x165] = 0xFF;
    be16(a, 0x168, 2);
    be32(a, 0x16C, 0x300);
    be16(a, 0x174, 0xFFFE);
    be16(a, 0x176, 0);
    /* pTab right after the track: patterns 0 and 1 unused, 2 @0x190 */
    be32(a, 0x178, 0);
    be32(a, 0x17C, 0);
    be32(a, 0x180, 0x190);
    be32(a, 0x190, 0x0C);
    be16(a, 0x19C, 0x0010); /* note: time 0x10, key 60, vel 90, length 0x120 */
    a[0x19E] = 60;
    a[0x19F] = 90;
    be16(a, 0x1A0, 0x0120);
    be16(a, 0x1A2, 0x0300); /* end */
    a[0x1A4] = 0xFF;
    a[0x1A5] = 0xFF;
    memset(a + 0x1B0, 0xFF, 64); /* tmTab: track 0 on channel 3 */
    a[0x1B0] = 3;

    portMusyxSwapArr(a, sizeof(a));

    arr = (ARR*)a;
    te = (TENTRY*)ARR_GET(arr, ((u32*)ARR_GET(arr, arr->tTab))[0]);
    CHECK(te[0].pattern == 2 && te[0].time == 0, "loop track entry 0");
    CHECK(te[1].pattern == 0xFFFE && te[1].time == 0x300 && *(u16*)&te[1].transpose == 0,
          "loop track loop entry");
    pt = (u32*)ARR_GET(arr, arr->pTab);
    CHECK(pt[0] == 0 && pt[1] == 0 && pt[2] == 0x190, "pattern table after a loop track (%x)", pt[2]);
    if (pt[2] != 0x190) {
        return;
    }
    pat = (SEQ_PATTERN*)ARR_GET(arr, pt[2]);
    nd = (NOTE_DATA*)&pat->noteData;
    CHECK(pat->headerLen == 0x0C && nd[0].time == 0x10 && nd[0].length == 0x120 && nd[1].time == 0x300,
          "pattern after a loop track");
    CHECK(a[0x1B0] == 3 && a[0x1B1] == 0xFF && a[0x1B3] == 0xFF, "MIDI channel table untouched");
}

/* ---- .stm header ---- */
static void testStreamHeader(void) {
    static u8 h[0x100];
    struct {
        u8 type;
        u16 freq;
        u32 count, loopEnd, loopStart, size0, size1, loopStart0, loopStart1;
        u16 initHist0, initHist1, loopHist0, loopHist1;
    } * hdr;
    s16* coef;
    memset(h, 0, sizeof(h));
    h[0] = 2;
    be16(h, 2, 32000);
    be32(h, 4, 2);
    be32(h, 8, 0x12345);
    be32(h, 0xC, 0xFFFFFFFF);
    be32(h, 0x10, 0x8000);
    be32(h, 0x1C, 0x40);
    be16(h, 0x20, 0x1234);
    be16(h, 0x26, 0xFEDC);
    be16(h, 0x5C, (u16)-5);
    be16(h, 0xBC + 30, 77);
    portMusyxSwapStreamHeader(h);
    hdr = (void*)h;
    CHECK(hdr->type == 2 && hdr->freq == 32000 && hdr->count == 2 && hdr->loopEnd == 0x12345 &&
              hdr->loopStart == 0xFFFFFFFF && hdr->size0 == 0x8000 && hdr->loopStart1 == 0x40 &&
              hdr->initHist0 == 0x1234 && hdr->loopHist1 == 0xFEDC,
          "stream header %u %u %u %x %x %x %x %x %x", hdr->type, hdr->freq, hdr->count, hdr->loopEnd,
          hdr->loopStart, hdr->size0, hdr->loopStart1, hdr->initHist0, hdr->loopHist1);
    coef = (s16*)(h + 0x5C);
    CHECK(coef[0] == -5, "coef L");
    coef = (s16*)(h + 0xBC);
    CHECK(coef[15] == 77, "coef R");
}

int main(void) {
    testProj();
    testPool();
    testSdir();
    testSlib();
    testArrLoopTrack();
    testStreamHeader();
    if (sFailures == 0) {
        printf("musyx_swap_test: OK\n");
    }
    return sFailures != 0;
}
