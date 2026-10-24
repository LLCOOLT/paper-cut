/* Host unit test for the bespoke big-endian loaders (no disc): fonts
 * (portFontSwap), camera roads (portCamRoadDecode), card saves
 * (slot and metadata conversion, cardmgr glue) and the U8 archive reader (src/sdk/arc.c
 * built for PC, big-endian FST read through accessors). Each format is built
 * synthetically in GameCube byte order from the layouts the game code reads. */
#include "port/bedecode.h"
#include "port/camroad_pc.h"
#include "port/cardsave_pc.h"
#include "port/font_pc.h"
#include "cam_road.h"
#include "manager/cardmgr.h"
#include "manager/fontmgr.h"
#include "sdk/arc.h"

BOOL ARCConvertEntrynumToPath(ARCHandle* handle, s32 entrynum, char* path, u32 maxlen);

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sFailures, sChecks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        sChecks++;                                                                \
        if (!(c)) {                                                               \
            sFailures++;                                                          \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

/* src/sdk/arc.c calls OSPanic on a bad magic. */
static int sPanics;
void OSPanic(const char* file, int line, const char* msg, ...) {
    (void)file;
    (void)line;
    (void)msg;
    sPanics++;
}

static void putF32(uint8_t* p, float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    portPutBE32(p, u);
}

static void putBE64(uint8_t* p, uint64_t v) {
    portPutBE32(p, (uint32_t)(v >> 32));
    portPutBE32(p + 4, (uint32_t)v);
}

/* ------------------------------------------------------------------ font */
static void testFont(void) {
    enum { INF = 0x20, GLY = 0x40, SHEET = 0x60, MAP = 0x80, WID = 0x90, F_END = 0xA0 };
    uint8_t* f = (uint8_t*)calloc(1, F_END);
    BinaryFontHeader* h = (BinaryFontHeader*)f;
    BinaryFontInfo* inf = (BinaryFontInfo*)(f + INF);
    BinaryFontGlyph* gly = (BinaryFontGlyph*)(f + GLY);
    BinaryFontMap* map = (BinaryFontMap*)(f + MAP);
    BinaryFontWidth* wid = (BinaryFontWidth*)(f + WID);
    int i;

    memcpy(f, "FONTbfn1", 8);
    portPutBE32(f + 8, F_END);
    portPutBE32(f + 12, 4);
    memcpy(f + INF, "INF1", 4);
    portPutBE32(f + INF + 4, GLY - INF);
    portPutBE16(f + INF + 0x8, 1);      /* encoding */
    portPutBE16(f + INF + 0xA, 0x1A);   /* ascent */
    portPutBE16(f + INF + 0xC, 0x6);    /* descent */
    portPutBE16(f + INF + 0xE, 0x18);   /* width */
    portPutBE16(f + INF + 0x10, 0x20);  /* leading */
    portPutBE16(f + INF + 0x12, 0x123); /* replace */
    memcpy(f + GLY, "GLY1", 4);
    portPutBE32(f + GLY + 4, MAP - GLY);
    portPutBE16(f + GLY + 0x8, 0x10);
    portPutBE16(f + GLY + 0xA, 0x2FF);
    portPutBE16(f + GLY + 0xC, 24);
    portPutBE16(f + GLY + 0xE, 26);
    portPutBE32(f + GLY + 0x10, 0x20);
    portPutBE16(f + GLY + 0x14, 0);     /* GX_TF_I4 */
    portPutBE16(f + GLY + 0x16, 21);
    portPutBE16(f + GLY + 0x18, 19);
    portPutBE16(f + GLY + 0x1A, 512);
    portPutBE16(f + GLY + 0x1C, 1024);
    for (i = 0; i < 0x20; i++) {
        f[SHEET + i] = (uint8_t)(0xA0 + i); /* texels: must stay as they are */
    }
    memcpy(f + MAP, "MAP1", 4);
    portPutBE32(f + MAP + 4, WID - MAP);
    portPutBE16(f + MAP + 0x8, 2);      /* TABLE_CHARACTER_MAP */
    portPutBE16(f + MAP + 0xA, 0x8140);
    portPutBE16(f + MAP + 0xC, 0x8141);
    portPutBE16(f + MAP + 0xE, 2);
    /* MAP1 is 0x10 bytes here: the table lives in the next test below */
    memcpy(f + WID, "WID1", 4);
    portPutBE32(f + WID + 4, F_END - WID);
    portPutBE16(f + WID + 0x8, 0x20);
    portPutBE16(f + WID + 0xA, 0x21);
    f[WID + 0xC] = 1;
    f[WID + 0xD] = 7;
    f[WID + 0xE] = 2;
    f[WID + 0xF] = 9;

    CHECK(portFontSwap(f, F_END));
    CHECK(h->magic == 'FONT' && h->version == 'bfn1' && h->size == F_END && h->blockcount == 4);
    CHECK(inf->magic == 'INF1' && inf->size == GLY - INF);
    CHECK(inf->encoding == 1 && inf->ascent == 0x1A && inf->descent == 6 && inf->width == 0x18);
    CHECK(inf->leading == 0x20 && inf->replace == 0x123);
    CHECK(gly->magic == 'GLY1' && gly->firstcode == 0x10 && gly->lastcode == 0x2FF);
    CHECK(gly->cellwidth == 24 && gly->cellheight == 26 && gly->sheetsize == 0x20);
    CHECK(gly->sheetrows == 21 && gly->sheetcolumns == 19 && gly->sheetwidth == 512 && gly->sheetheight == 1024);
    CHECK(f[SHEET] == 0xA0 && f[SHEET + 0x1F] == 0xBF);
    CHECK(map->magic == 'MAP1' && map->mapping == 2 && map->firstchar == 0x8140 && map->lastchar == 0x8141);
    CHECK(wid->magic == 'WID1' && wid->firstcode == 0x20 && wid->lastcode == 0x21);
    CHECK(f[WID + 0xC] == 1 && f[WID + 0xD] == 7 && f[WID + 0xE] == 2 && f[WID + 0xF] == 9);
    free(f);

    /* MAP1 table entries are u16 runs; a malformed chain is rejected */
    {
        uint8_t g[0x40] = {0};
        BinaryFontMap* m = (BinaryFontMap*)(g + 0x20);
        memcpy(g, "FONTbfn1", 8);
        portPutBE32(g + 8, sizeof(g));
        portPutBE32(g + 12, 1);
        memcpy(g + 0x20, "MAP1", 4);
        portPutBE32(g + 0x24, 0x20);
        portPutBE16(g + 0x28, 3); /* MAP_CHARACTER_MAP */
        portPutBE16(g + 0x2E, 2);
        portPutBE16(g + 0x30, 0x8145);
        portPutBE16(g + 0x32, 0x0102);
        portPutBE16(g + 0x34, 0x889F);
        portPutBE16(g + 0x36, 0x0304);
        CHECK(portFontSwap(g, sizeof(g)));
        CHECK(m->mapping == 3 && m->count == 2);
        CHECK(m->table[0] == 0x8145 && m->table[1] == 0x0102 && m->table[2] == 0x889F && m->table[3] == 0x0304);

        memset(g, 0, sizeof(g));
        memcpy(g, "FONTbfn1", 8);
        portPutBE32(g + 12, 1);
        memcpy(g + 0x20, "INF1", 4);
        portPutBE32(g + 0x24, 0x1000); /* runs past the file */
        CHECK(!portFontSwap(g, sizeof(g)));
    }
}

/* -------------------------------------------------------------- cam road */
static int sCamErrors;
static void camError(struct PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    (void)msg;
    sCamErrors++;
}

enum {
    CR_E8 = 0x110, CR_CURVES = 0x120, CR_F0 = 0x290, CR_F4 = 0x2F8, CR_F8 = 0x300, CR_FC = 0x318,
    CR_100 = 0x324, CR_104 = 0x32C, CR_108 = 0x394, CR_END = 0x3A0
};

static uint8_t* buildCamRoad(void) {
    uint8_t* b = (uint8_t*)calloc(1, CR_END);
    static const uint32_t offs[9] = {CR_E8, CR_CURVES, CR_F0, CR_F4, CR_F8, CR_FC, CR_100, CR_104, CR_108};
    int i;
    memcpy(b, "camroad header pad", 18);
    memcpy(b + 0x44, "1.01", 5);
    portPutBE32(b + 0xC8, 2);
    {
        /* f_f0Num .. f_108Num: the decoder skips tables whose count is 0 */
        static const uint32_t nums[7] = {1, 1, 2, 1, 2, 1, 1};
        for (i = 0; i < 7; i++) {
            portPutBE32(b + 0xCC + 4 * i, nums[i]);
        }
    }
    for (i = 0; i < 9; i++) {
        portPutBE32(b + 0xE8 + 4 * i, offs[i]);
    }
    putF32(b + CR_E8, 25.0f);
    putF32(b + CR_E8 + 4, -1.5f);
    putF32(b + CR_E8 + 8, 300.0f);
    for (i = 0; i < 2; i++) {
        uint8_t* c = b + CR_CURVES + i * 0xB8;
        strcpy((char*)c, i ? "curve_b" : "curve_a");
        portPutBE32(c + 0x20, 1);              /* f_20 */
        putF32(c + 0x24, 0.5f);                /* f_24 */
        portPutBE32(c + 0x28, (uint32_t)i);    /* off */
        portPutBE32(c + 0x40, 3);              /* idxA */
        putF32(c + 0x8C, 42.0f);               /* height */
        portPutBE32(c + 0xA8, (uint32_t)i);    /* grpOfs */
        portPutBE32(c + 0xB4, 0xFFFFFFFFu);    /* f_b4 */
    }
    strcpy((char*)b + CR_F0, "group0");
    portPutBE32(b + CR_F0 + 0x58, 1);          /* vtxOfs */
    portPutBE32(b + CR_F0 + 0x60, 0);          /* stripOfs */
    portPutBE32(b + CR_F0 + 0x64, 2);          /* stripNum */
    portPutBE32(b + CR_F4, 0);                 /* strip idxOfs */
    portPutBE32(b + CR_F4 + 4, 2);             /* strip num */
    for (i = 0; i < 6; i++) {
        putF32(b + CR_F8 + 4 * i, (float)i + 0.25f);
    }
    putF32(b + CR_FC, 7.0f);
    putF32(b + CR_FC + 4, 8.0f);
    putF32(b + CR_FC + 8, 9.0f);
    portPutBE32(b + CR_100, 0);
    portPutBE32(b + CR_100 + 4, 1);
    portPutBE32(b + CR_104 + 0x64, 4);
    memcpy(b + CR_108, "\x12\x34\x56\x78", 4);
    return b;
}

static void testCamRoad(void) {
    uint8_t* b = buildCamRoad();
    CamRoadMap* m;
    CamRoadMap* again;
    uint8_t snapshot[CR_END];
    int32_t s;

    portCamRoadErrorHook = camError;
    m = portCamRoadDecode(b, CR_END, "camroad");
    CHECK(m != NULL && sCamErrors == 0);
    if (!m) {
        free(b);
        return;
    }
    CHECK((void*)m != (void*)b);
    CHECK(strcmp(m->version, "1.01") == 0);
    CHECK(memcmp(m->pad0, "camroad header pad", 18) == 0);
    CHECK(m->curveNum == 2);
    CHECK((uint8_t*)m->f_e8 == b + CR_E8 && (uint8_t*)m->curves == b + CR_CURVES);
    CHECK((uint8_t*)m->f_f0 == b + CR_F0 && (uint8_t*)m->f_f4 == b + CR_F4 && (uint8_t*)m->f_f8 == b + CR_F8);
    CHECK((uint8_t*)m->f_fc == b + CR_FC && (uint8_t*)m->f_100 == b + CR_100);
    CHECK((uint8_t*)m->f_104 == b + CR_104 && (uint8_t*)m->f_108 == b + CR_108);
    CHECK(m->f_e8->x == 25.0f && m->f_e8->y == -1.5f && m->f_e8->z == 300.0f);
    CHECK(strcmp(m->curves[0].name, "curve_a") == 0 && strcmp(m->curves[1].name, "curve_b") == 0);
    CHECK(m->curves[0].off == 0 && m->curves[1].off == 1);
    {
        uint8_t* c1 = (uint8_t*)&m->curves[1];
        float fl;
        uint32_t u;
        memcpy(&u, c1 + 0x20, 4);
        CHECK(u == 1);
        memcpy(&fl, c1 + 0x24, 4);
        CHECK(fl == 0.5f);
        memcpy(&s, c1 + 0x40, 4);
        CHECK(s == 3);
        memcpy(&fl, c1 + 0x8C, 4);
        CHECK(fl == 42.0f);
        memcpy(&s, c1 + 0xA8, 4);
        CHECK(s == 1);
        memcpy(&s, c1 + 0xB4, 4);
        CHECK(s == -1);
    }
    CHECK(strcmp((char*)m->f_f0, "group0") == 0); /* group pad kept as bytes */
    memcpy(&s, (uint8_t*)m->f_f0 + 0x58, 4);
    CHECK(s == 1);
    memcpy(&s, (uint8_t*)m->f_f0 + 0x64, 4);
    CHECK(s == 2);
    CHECK(((int32_t*)m->f_f4)[1] == 2);
    CHECK(((Vec*)m->f_f8)[1].x == 3.25f && ((Vec*)m->f_f8)[1].z == 5.25f);
    CHECK(((Vec*)m->f_fc)[0].y == 8.0f);
    CHECK(((int32_t*)m->f_100)[1] == 1);
    memcpy(&s, (uint8_t*)m->f_104 + 0x64, 4);
    CHECK(s == 4);
    CHECK(memcmp(m->f_108, "\x12\x34\x56\x78", 4) == 0); /* unused table: untouched */

    /* set up again from the same (archive-resident) blob: no second swap */
    memcpy(snapshot, b, CR_END);
    again = portCamRoadDecode(b, CR_END, "camroad");
    CHECK(again == m && memcmp(snapshot, b, CR_END) == 0);
    CHECK(m->f_e8->x == 25.0f);

    /* fresh file at the same address: decoded again into the same slot */
    free(b);
    b = buildCamRoad();
    portPutBE32(b + 0xC8, 1);
    m = portCamRoadDecode(b, CR_END, "camroad");
    CHECK(m != NULL && m->curveNum == 1 && (uint8_t*)m->curves == b + CR_CURVES);

    /* bad table offset */
    {
        uint8_t* bad = buildCamRoad();
        portPutBE32(bad + 0xF0, CR_END + 0x100);
        sCamErrors = 0;
        CHECK(portCamRoadDecode(bad, CR_END, "bad") == NULL && sCamErrors == 1);
        free(bad);
    }
    portCamRoadErrorHook = NULL;
    free(b);
}

/* ----------------------------------------------------------- card saves */
static uint32_t byteSum(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint32_t s = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        s += b[i];
    }
    return s;
}

static void nativeCheckSum(CardSaveSlot* s) { /* cardmgr.c check_sum */
    strcpy(s->version, "009");
    s->size = sizeof(s->data);
    s->checksum1 = byteSum(&s->data, sizeof(s->data));
    s->checksum2 = ~s->checksum1;
}

static int nativeDataCheck(const CardSaveSlot* s) { /* cardmgr.c dataCheck */
    return s->size == (int32_t)sizeof(s->data) && strcmp(s->version, "009") == 0 && s->checksum2 == ~s->checksum1 &&
           s->checksum1 == byteSum(&s->data, sizeof(s->data));
}

/* GameCube slot offsets that differ between versions (cardmgr.h) */
#ifdef VERSION_G8ME01
#define T_PARLOR_SCORE 0xD4
#define T_PARLOR_PAD 0x254
#define T_INFORMANT 0x2230
#define T_TAIL 0x225B
#else
#define T_PARLOR_SCORE 0x74
#define T_PARLOR_PAD 0x134
#define T_INFORMANT 0x2110
#define T_TAIL 0x213B
#endif

static void fillSlot(CardSaveSlot* s) {
    memset(s, 0, sizeof(*s));
    s->data.flags = 0x0104;
    s->data.pad_2[0] = 0x55;
    s->data.global.flags = 0x12345678;
    s->data.global.retraceLocalTime = 0x0102030405060708LL;
    s->data.global.countdown.flags = 0xBEEF;
    s->data.global.countdown.x = 1.25f;
    s->data.global.mapFadeOutType = 3;
    strcpy(s->data.global.currentMapName, "gor_01");
    s->data.global.mapAlloc = (void*)s; /* runtime pointer: not saved */
    s->data.global.mGSWF[3] = 0x80000001u;
    s->data.global.mGSW[0x7FF] = 0xAB;
    s->data.global.lastSlot = 2;
    s->data.global.savePlayerPos.x = -12.5f;
    s->data.global.savePlayerPos.y = 10.0f;
    s->data.global.savePlayerPos.z = 300.75f;
    s->data.global.saveTime = 0x00000123456789ABLL;
    s->data.global.saveCount = 77;
    s->data.global.stickX[1] = -100;
    s->data.global.prevRumbleStatus[3] = 9;
    s->data.global.mLotteryInfo.flags = 0x0008;
    s->data.global.mLotteryInfo.buyTime = 0x1122334455667788LL;
    s->data.global.mLotteryInfo.number = 1234;
    s->data.global.mLotteryInfo.days[3] = 7;
    s->data.pouch.partyData[2].maximumHP = 25;
    s->data.pouch.coins = 999;
    s->data.pouch.audienceCount = 123.5f;
    s->data.pouch.jumpLevel = -2;
    s->data.pouch.badges[199] = 0x0123;
    s->data.pouch.mEmailReadFlags[3] = 0xDEADBEEF;
    strcpy(s->data.pouch.yoshiName, "Yoshi");
    strcpy(s->data.deadInfo[63].mapName, "tik_01");
    s->data.deadInfo[63].mask = 0x00F0F0F0;
    s->data.badgeShop.bargainBadges[4] = 250;
    s->data.badgeShop.restockFlags = 1;
    s->data.parlor.recordScore[3][5] = 0xCAFEF00D;
    s->data.parlor.pad_134 = 5;
    s->data.informant.luigiRead[12] = 1;
    s->data.pad_tail[4] = 0x77;
    nativeCheckSum(s);
}

static void testCardSave(void) {
    CardSaveSlot* a = (CardSaveSlot*)calloc(1, sizeof(CardSaveSlot));
    CardSaveSlot* b = (CardSaveSlot*)calloc(1, sizeof(CardSaveSlot));
    uint8_t* img = (uint8_t*)calloc(1, PORT_CARD_SLOT_SIZE);
    const uint8_t* gw;

    fillSlot(a);
    CHECK(nativeDataCheck(a));
    CHECK(portCardSlotToImage(a, img));
    gw = img + 0x8;
    /* GameCube offsets (cardmgr.h / mariost.h comments) */
    CHECK(portBE16(img) == 0x0104 && img[2] == 0x55);
    CHECK(portBE32(gw + 0x0) == 0x12345678u);
    CHECK(portBE64(gw + 0x20) == 0x0102030405060708ULL);
    CHECK(portBE16(gw + 0x68) == 0xBEEF && portBEF32(gw + 0x68 + 0x20) == 1.25f);
    CHECK(portBE16(gw + 0xA8) == 0x0008);
    CHECK(portBE64(gw + 0xA8 + 0x18) == 0x1122334455667788ULL && portBE16(gw + 0xA8 + 0x20) == 1234);
    CHECK(portBE16(gw + 0xA8 + 0x28) == 7);
    CHECK(portBE32(gw + 0xF8) == 3);
    CHECK(strcmp((const char*)gw + 0x12C, "gor_01") == 0);
    CHECK(portBE32(gw + 0x160) == 0); /* mapAlloc */
    CHECK(portBE32(gw + 0x178 + 3 * 4) == 0x80000001u);
    CHECK(gw[0x578 + 0x7FF] == 0xAB);
    CHECK(portBE32(gw + 0x11B8) == 2);
    CHECK(portBEF32(gw + 0x11D4) == -12.5f && portBEF32(gw + 0x11DC) == 300.75f);
    CHECK(portBE64(gw + 0x11E8) == 0x00000123456789ABULL && portBE32(gw + 0x11F0) == 77);
    CHECK((int8_t)gw[0x13B8 + 1] == -100 && gw[0x13D4 + 3] == 9);
    CHECK(portBE16(img + 0x13E0 + 2 * 0xE + 2) == 25);
    CHECK(portBE16(img + 0x13E0 + 0x78) == 999 && portBEF32(img + 0x13E0 + 0x84) == 123.5f);
    CHECK((int8_t)img[0x13E0 + 0x98] == -2 && portBE16(img + 0x13E0 + 0x1FA + 199 * 2) == 0x0123);
    CHECK(portBE32(img + 0x13E0 + 0x590 + 12) == 0xDEADBEEFu);
    CHECK(strcmp((const char*)img + 0x13E0 + 0x5C0, "Yoshi") == 0);
    CHECK(strcmp((const char*)img + 0x19B4 + 63 * 0x14, "tik_01") == 0);
    CHECK(portBE32(img + 0x19B4 + 63 * 0x14 + 0x10) == 0x00F0F0F0u);
    CHECK(portBE32(img + 0x1EB4 + 0xF8 + 16) == 250 && portBE16(img + 0x1EB4 + 0x114) == 1);
    CHECK(portBE32(img + 0x1FD8 + T_PARLOR_SCORE + (3 * 6 + 5) * 4) == 0xCAFEF00Du);
    CHECK(portBE32(img + 0x1FD8 + T_PARLOR_PAD) == 5);
    CHECK(img[T_INFORMANT + 0x1A + 12] == 1 && img[T_TAIL + 4] == 0x77);
    CHECK(memcmp(img + 0x3FF0, "009", 4) == 0 && portBE32(img + 0x3FF4) == PORT_CARD_GC_DATA_SIZE);
    CHECK(portBE32(img + 0x3FF8) == byteSum(img, PORT_CARD_GC_DATA_SIZE) &&
          portBE32(img + 0x3FFC) == ~portBE32(img + 0x3FF8));

    /* back to native: everything but runtime pointers survives */
    CHECK(portCardSlotFromImage(img, b));
    CHECK(nativeDataCheck(b));
    a->data.global.mapAlloc = NULL;
    nativeCheckSum(a);
    CHECK(memcmp(&a->data, &b->data, sizeof(a->data)) == 0);
    CHECK(b->data.global.mLotteryInfo.buyTime == 0x1122334455667788LL && b->data.global.mLotteryInfo.number == 1234 &&
          b->data.global.mLotteryInfo.days[3] == 7);

    /* a corrupted card image stays rejected by the game's dataCheck */
    img[0x100] ^= 0x40;
    CHECK(!portCardSlotFromImage(img, b));
    CHECK(!nativeDataCheck(b));
    img[0x100] ^= 0x40;

    /* an empty (never written) slot: invalid both ways */
    memset(img, 0, PORT_CARD_SLOT_SIZE);
    CHECK(!portCardSlotFromImage(img, b) && !nativeDataCheck(b));

    /* metadata: same layout, u16 options and checksums swap */
    {
        CardMetadata* m = (CardMetadata*)calloc(1, sizeof(CardMetadata));
        CardMetadata* m2 = (CardMetadata*)calloc(1, sizeof(CardMetadata));
        strcpy(m->data.fileName, "ttyd");
        m->data.bannerTex[5] = 0x3C;
        m->data.rumbleEnabled = 1;
        m->data.soundMode = 0x0203;
        m->data.lastSlot = 3;
        m->checksum1 = byteSum(&m->data, sizeof(m->data));
        m->checksum2 = ~m->checksum1;
        portCardMetaToImage(m, img);
        CHECK(portBE16(img + 0x1E40) == 1 && portBE16(img + 0x1E42) == 0x0203 && portBE16(img + 0x1E44) == 3);
        CHECK(portBE32(img + 0x1FF8) == byteSum(img, 0x1E46)); /* GC checksum holds on the image */
        CHECK(img[0x40 + 5] == 0x3C && strcmp((const char*)img, "ttyd") == 0);
        portCardMetaFromImage(img, m2);
        CHECK(memcmp(m, m2, sizeof(*m)) == 0);
        free(m);
        free(m2);
    }

    /* cardmgr glue: write slot 2 of the backup set, read it back */
    {
        int32_t off = 0x2000 + 2 * 0x4000 + 4 * 0x4000;
        uint8_t* card = (uint8_t*)calloc(1, 0x22000);
        void* buf;
        fillSlot(a);
        buf = portCardWriteImage(a, 0x4000, off);
        CHECK(buf != (void*)a);
        memcpy(card + off, buf, 0x4000);
        CHECK(portBE32(card + off + 0x3FF4) == PORT_CARD_GC_DATA_SIZE);
        memset(b, 0, sizeof(*b));
        buf = portCardReadBegin(b, 0x4000, off);
        CHECK(buf != (void*)b);
        memcpy(buf, card + off, 0x4000);
        portCardReadEnd(0);
        CHECK(nativeDataCheck(b) && b->data.global.saveCount == 77 && b->data.pouch.coins == 999);
        /* failed read: destination untouched */
        memset(b, 0, sizeof(*b));
        buf = portCardReadBegin(b, 0x4000, off);
        memcpy(buf, card + off, 0x4000);
        portCardReadEnd(-3);
        CHECK(b->data.pouch.coins == 0);
        free(card);
    }
    free(a);
    free(b);
    free(img);
}

/* ------------------------------------------------------------- ARC (U8) */
static void testArc(void) {
    /* FST: 0 root (dir, next 4), 1 file "foo", 2 dir "sub" (parent 0, next 4), 3 file "bar" */
    enum { FST = 0x20, NENT = 4, STR = FST + NENT * 12, DATA = 0x80, SIZE = 0xA0 };
    static const char strings[] = "\0foo\0sub\0bar";
    uint8_t* a = (uint8_t*)calloc(1, SIZE);
    ARCHandle h;
    ARCFileInfo fi;
    char path[32];

    portPutBE32(a + 0x0, 0x55AA382D);
    portPutBE32(a + 0x4, FST);
    portPutBE32(a + 0x8, NENT * 12 + sizeof(strings));
    portPutBE32(a + 0xC, DATA);
    portPutBE32(a + FST + 0, 0x01000000);
    portPutBE32(a + FST + 4, 0);
    portPutBE32(a + FST + 8, NENT);
    portPutBE32(a + FST + 12, 0x00000001);
    portPutBE32(a + FST + 16, DATA);
    portPutBE32(a + FST + 20, 4);
    portPutBE32(a + FST + 24, 0x01000005);
    portPutBE32(a + FST + 28, 0);
    portPutBE32(a + FST + 32, NENT);
    portPutBE32(a + FST + 36, 0x00000009);
    portPutBE32(a + FST + 40, DATA + 0x10);
    portPutBE32(a + FST + 44, 8);
    memcpy(a + STR, strings, sizeof(strings));
    memcpy(a + DATA, "FOO!", 4);
    memcpy(a + DATA + 0x10, "barbarba", 8);

    sPanics = 0;
    CHECK(ARCInitHandle(a, &h) && sPanics == 0);
    CHECK(h.entryNum == NENT && h.FSTLength == NENT * 12 + sizeof(strings));
    CHECK((uint8_t*)h.FSTStart == a + FST && (uint8_t*)h.fileStart == a + DATA);
    CHECK(h.FSTStringStart == (char*)a + STR);
    CHECK(ARCOpen(&h, "foo", &fi) && fi.startOffset == DATA && ARCGetLength(&fi) == 4);
    CHECK(memcmp(ARCGetStartAddrInMem(&fi), "FOO!", 4) == 0);
    CHECK(ARCOpen(&h, "/sub/bar", &fi) && ARCGetLength(&fi) == 8);
    CHECK(memcmp(ARCGetStartAddrInMem(&fi), "barbarba", 8) == 0);
    CHECK(!ARCOpen(&h, "sub", &fi));     /* a directory */
    CHECK(!ARCOpen(&h, "nope", &fi));
    CHECK(ARCConvertPathToEntrynum(&h, "sub/../foo") == 1);
    CHECK(ARCConvertEntrynumToPath(&h, 2, path, sizeof(path)) && strcmp(path, "/sub/") == 0); /* dirs: files keep no parent */
    CHECK(portBE32(a + FST + 8) == NENT); /* the table itself stays big-endian */
    portPutBE32(a, 0x12345678);
    ARCInitHandle(a, &h);
    CHECK(sPanics == 1);
    free(a);
}

int main(void) {
    testFont();
    testCamRoad();
    testCardSave();
    testArc();
    printf("loaders_test: %d checks, %d failures\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
