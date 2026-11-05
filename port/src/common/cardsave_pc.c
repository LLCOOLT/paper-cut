/* Memory card save conversion (GameCube card image <-> native structs).
 * See port/include/port/cardsave_pc.h for the policy. */
#include "port/cardsave_pc.h"
#include "port/bedecode.h"
#include "manager/cardmgr.h"

#include <stddef.h>
#include <string.h>

extern const PortType portBE_cardsave_GlobalWork;
extern const PortType portBE_cardsave_PouchData;
extern const PortType portBE_cardsave_NpcDeadInfo;
extern const PortType portBE_cardsave_BadgeShopWork;

/* GameCube offsets inside CardSaveSlot (include/manager/cardmgr.h comments). */
enum {
    GC_FLAGS = 0x0,
    GC_PAD2 = 0x2,
    GC_GLOBAL = 0x8,
    GC_POUCH = 0x13E0,
    GC_DEAD = 0x19B4,
    GC_BADGE = 0x1EB4,
    GC_PARLOR = 0x1FD8,
#ifdef VERSION_G8ME01
    GC_INFORMANT = 0x2230,     /* the US parlor work is 0x120 bytes bigger */
    GC_TAIL = 0x225B,
#else
    GC_INFORMANT = 0x2110,
    GC_TAIL = 0x213B,
#endif
    GC_VERSION = 0x3FF0,
    GC_SIZE = 0x3FF4,
    GC_CK1 = 0x3FF8,
    GC_CK2 = 0x3FFC,
    GC_GW_SIZE = 0x13D8,
    GC_GW_SAVEPOS = 0x11D4,    /* GlobalWork.savePlayerPos (Vec, not in the spec) */
    GC_POUCH_SIZE = 0x5D4,
    GC_DEAD_SIZE = 0x14,
    GC_BADGE_SIZE = 0x124,
    GC_PARLOR_SIZE = GC_INFORMANT - GC_PARLOR,
    GC_INFORMANT_SIZE = 0x2B,
    GC_META_OPT = 0x1E40,      /* CardMetadata.data.rumbleEnabled/soundMode/lastSlot */
    GC_META_CK1 = 0x1FF8,
    GC_META_CK2 = 0x1FFC,
    GC_META_DATA_SIZE = 0x1E46
};

_Static_assert(sizeof(CardSaveSlot) == PORT_CARD_SLOT_SIZE, "CardSaveSlot must stay 0x4000 bytes");
_Static_assert(offsetof(CardSaveSlot, version) == GC_VERSION, "CardSaveSlot trailer offset");
_Static_assert(sizeof(CardMetadata) == PORT_CARD_META_SIZE, "CardMetadata size");
_Static_assert(offsetof(CardMetadata, data.rumbleEnabled) == GC_META_OPT, "CardMetadata layout");
_Static_assert(offsetof(CardMetadata, checksum1) == GC_META_CK1, "CardMetadata layout");
_Static_assert(sizeof(((CardSaveSlot*)0)->data.parlor) == GC_PARLOR_SIZE, "PiantaParlorWork size");
_Static_assert(sizeof(((CardSaveSlot*)0)->data.informant) == GC_INFORMANT_SIZE, "InformantUnknown size");
_Static_assert(sizeof(lottery_info) == 0x40, "lottery_info size");

static uint32_t byteSum(const uint8_t* p, uint32_t n) {
    uint32_t s = 0, i;
    for (i = 0; i < n; i++) {
        s += p[i];
    }
    return s;
}

static int nativeSlotValid(const CardSaveSlot* s) {
    return s->size == (int32_t)sizeof(s->data) && s->checksum2 == ~s->checksum1 &&
           s->checksum1 == byteSum((const uint8_t*)&s->data, sizeof(s->data));
}

static void noPanic(PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    (void)msg; /* ctx->failed is set: the slot is converted as invalid */
}

int portCardSlotToImage(const void* nativeSlot, uint8_t* image) {
    const CardSaveSlot* s = (const CardSaveSlot*)nativeSlot;
    GlobalWork gw;
    int ok = nativeSlotValid(s), i;
    uint32_t ck;

    memset(image, 0, PORT_CARD_SLOT_SIZE);
    portPutBE16(image + GC_FLAGS, s->data.flags);
    memcpy(image + GC_PAD2, s->data.pad_2, sizeof(s->data.pad_2));

    gw = s->data.global;
    ok &= portEncode(&portBE_cardsave_GlobalWork, &gw, image + GC_GLOBAL, GC_GW_SIZE);
    {
        uint32_t v;
        memcpy(&v, &s->data.global.savePlayerPos.x, 4);
        portPutBE32(image + GC_GLOBAL + GC_GW_SAVEPOS + 0, v);
        memcpy(&v, &s->data.global.savePlayerPos.y, 4);
        portPutBE32(image + GC_GLOBAL + GC_GW_SAVEPOS + 4, v);
        memcpy(&v, &s->data.global.savePlayerPos.z, 4);
        portPutBE32(image + GC_GLOBAL + GC_GW_SAVEPOS + 8, v);
    }
    ok &= portEncode(&portBE_cardsave_PouchData, &s->data.pouch, image + GC_POUCH, GC_POUCH_SIZE);
    for (i = 0; i < 64; i++) {
        ok &= portEncode(&portBE_cardsave_NpcDeadInfo, &s->data.deadInfo[i], image + GC_DEAD + i * GC_DEAD_SIZE,
                         GC_DEAD_SIZE);
    }
    ok &= portEncode(&portBE_cardsave_BadgeShopWork, &s->data.badgeShop, image + GC_BADGE, GC_BADGE_SIZE);
    for (i = 0; i < GC_PARLOR_SIZE / 4; i++) {
        uint32_t v;
        memcpy(&v, (const uint8_t*)&s->data.parlor + 4 * i, 4);
        portPutBE32(image + GC_PARLOR + 4 * i, v);
    }
    memcpy(image + GC_INFORMANT, &s->data.informant, GC_INFORMANT_SIZE);
    memcpy(image + GC_TAIL, s->data.pad_tail, sizeof(s->data.pad_tail));

    memcpy(image + GC_VERSION, s->version, sizeof(s->version));
    portPutBE32(image + GC_SIZE, s->size == (int32_t)sizeof(s->data) ? PORT_CARD_GC_DATA_SIZE : (uint32_t)s->size);
    if (ok) {
        ck = byteSum(image, PORT_CARD_GC_DATA_SIZE);
        portPutBE32(image + GC_CK1, ck);
        portPutBE32(image + GC_CK2, ~ck);
    } else {
        portPutBE32(image + GC_CK1, 0); /* stays invalid (ck2 != ~ck1) */
        portPutBE32(image + GC_CK2, 0);
    }
    return ok;
}

int portCardSlotFromImage(const uint8_t* image, void* nativeSlot) {
    CardSaveSlot* s = (CardSaveSlot*)nativeSlot;
    PortDecodeCtx ctx;
    uint32_t ck1 = portBE32(image + GC_CK1), ck2 = portBE32(image + GC_CK2), gcSize = portBE32(image + GC_SIZE);
    int ok = gcSize == PORT_CARD_GC_DATA_SIZE && ck2 == ~ck1 && ck1 == byteSum(image, PORT_CARD_GC_DATA_SIZE);
    int i;

    memset(s, 0, sizeof(*s));
    s->data.flags = portBE16(image + GC_FLAGS);
    memcpy(s->data.pad_2, image + GC_PAD2, sizeof(s->data.pad_2));

    portDecodeBegin(&ctx, image, PORT_CARD_SLOT_SIZE, "card save");
    ctx.onError = noPanic;
    portDecodeInto(&ctx, &portBE_cardsave_GlobalWork, GC_GLOBAL, &s->data.global);
    portDecodeInto(&ctx, &portBE_cardsave_PouchData, GC_POUCH, &s->data.pouch);
    for (i = 0; i < 64; i++) {
        portDecodeInto(&ctx, &portBE_cardsave_NpcDeadInfo, GC_DEAD + i * GC_DEAD_SIZE, &s->data.deadInfo[i]);
    }
    portDecodeInto(&ctx, &portBE_cardsave_BadgeShopWork, GC_BADGE, &s->data.badgeShop);
    portDecodeEnd(&ctx);
    portDecodeArenaFree(&ctx.arena); /* flat types: nothing was allocated */
    if (ctx.failed) {
        ok = 0;
    }
    s->data.global.savePlayerPos.x = portBEF32(image + GC_GLOBAL + GC_GW_SAVEPOS + 0);
    s->data.global.savePlayerPos.y = portBEF32(image + GC_GLOBAL + GC_GW_SAVEPOS + 4);
    s->data.global.savePlayerPos.z = portBEF32(image + GC_GLOBAL + GC_GW_SAVEPOS + 8);
    for (i = 0; i < GC_PARLOR_SIZE / 4; i++) {
        uint32_t v = portBE32(image + GC_PARLOR + 4 * i);
        memcpy((uint8_t*)&s->data.parlor + 4 * i, &v, 4);
    }
    memcpy(&s->data.informant, image + GC_INFORMANT, GC_INFORMANT_SIZE);
    memcpy(s->data.pad_tail, image + GC_TAIL, sizeof(s->data.pad_tail));

    memcpy(s->version, image + GC_VERSION, sizeof(s->version));
    s->size = gcSize == PORT_CARD_GC_DATA_SIZE ? (int32_t)sizeof(s->data) : (int32_t)gcSize;
    if (ok) {
        s->checksum1 = byteSum((const uint8_t*)&s->data, sizeof(s->data));
        s->checksum2 = ~s->checksum1;
    } else {
        s->checksum1 = 0; /* fails dataCheck */
        s->checksum2 = 0;
    }
    return ok;
}

void portCardMetaToImage(const void* nativeMeta, uint8_t* image) {
    const uint8_t* m = (const uint8_t*)nativeMeta;
    int i;
    memcpy(image, m, PORT_CARD_META_SIZE);
    for (i = 0; i < 3; i++) {
        uint16_t v;
        memcpy(&v, m + GC_META_OPT + 2 * i, 2);
        portPutBE16(image + GC_META_OPT + 2 * i, v);
    }
    for (i = 0; i < 2; i++) {
        uint32_t v;
        memcpy(&v, m + GC_META_CK1 + 4 * i, 4);
        portPutBE32(image + GC_META_CK1 + 4 * i, v);
    }
}

void portCardMetaFromImage(const uint8_t* image, void* nativeMeta) {
    uint8_t* m = (uint8_t*)nativeMeta;
    int i;
    memcpy(m, image, PORT_CARD_META_SIZE);
    for (i = 0; i < 3; i++) {
        uint16_t v = portBE16(image + GC_META_OPT + 2 * i);
        memcpy(m + GC_META_OPT + 2 * i, &v, 2);
    }
    for (i = 0; i < 2; i++) {
        uint32_t v = portBE32(image + GC_META_CK1 + 4 * i);
        memcpy(m + GC_META_CK1 + 4 * i, &v, 4);
    }
}

/* ------------------------------------------------------------ cardmgr glue */
/* One CARD operation is in flight at a time (cardmgr's busy flag), so one
 * staging buffer per direction is enough. 32-byte aligned for CARD DMA. */
static uint8_t sWriteImage[PORT_CARD_SLOT_SIZE] __attribute__((aligned(32)));
static uint8_t sReadImage[PORT_CARD_SLOT_SIZE] __attribute__((aligned(32)));
static void* sReadDst;
static int32_t sReadLen, sReadOff;

static int isSlot(int32_t length, int32_t offset) {
    return length == (int32_t)PORT_CARD_SLOT_SIZE && offset >= (int32_t)PORT_CARD_META_SIZE &&
           (offset - (int32_t)PORT_CARD_META_SIZE) % (int32_t)PORT_CARD_SLOT_SIZE == 0;
}

static int isMeta(int32_t length, int32_t offset) {
    return offset == 0 && length == (int32_t)PORT_CARD_META_SIZE;
}

void (*gPortCardWillWrite)(void);

/* ------------------------------------------------- Paper Cut's slot block */
_Static_assert(PORT_CARD_EXTRA_AT >= PORT_CARD_GC_DATA_SIZE && PORT_CARD_EXTRA_AT + PORT_CARD_EXTRA_SIZE <= GC_VERSION,
               "the block sits between the GameCube data and the slot's trailer");
static char sExtra[4][PORT_CARD_EXTRA_SIZE];
static int sExtraKeep[4];
void (*gPortCardExtraForSave)(int slot, const char* old, int copy, char* out, int cap);
void (*gPortCardExtraApply)(int slot, const char* text);
void (*gPortCardSlotWritten)(int slot, const uint8_t* image);

static int slotIndex(int32_t offset) {
    return (int)(((offset - (int32_t)PORT_CARD_META_SIZE) / (int32_t)PORT_CARD_SLOT_SIZE) & 7);
}

const char* portCardSlotExtra(int slot) {
    return slot >= 0 && slot < 4 ? sExtra[slot] : "";
}

void portCardSlotExtraSet(int slot, const char* text) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    if (text == NULL) {
        text = "";
    }
    strncpy(sExtra[slot], text, PORT_CARD_EXTRA_SIZE - 1);
    sExtra[slot][PORT_CARD_EXTRA_SIZE - 1] = '\0';
}

const char* portCardImageExtra(const uint8_t* image) {
    static char text[PORT_CARD_EXTRA_SIZE];
    const char* at = (const char*)image + PORT_CARD_EXTRA_AT;
    if (memcmp(at, PORT_CARD_EXTRA_MAGIC, sizeof(PORT_CARD_EXTRA_MAGIC) - 1) != 0) {
        return NULL;
    }
    memcpy(text, at, PORT_CARD_EXTRA_SIZE - 1);
    text[PORT_CARD_EXTRA_SIZE - 1] = '\0';
    return text;
}

void portCardExtraSave(int slot) {
    char out[PORT_CARD_EXTRA_SIZE];
    if (slot < 0 || slot >= 4 || gPortCardExtraForSave == NULL) {
        return;
    }
    out[0] = '\0';
    gPortCardExtraForSave(slot, sExtra[slot], 0, out, (int)sizeof(out));
    portCardSlotExtraSet(slot, out);
}

void portCardExtraCopy(int from, int to) {
    char out[PORT_CARD_EXTRA_SIZE];
    if (from < 0 || from >= 4 || to < 0 || to >= 4) {
        return;
    }
    if (gPortCardExtraForSave == NULL) {
        portCardSlotExtraSet(to, sExtra[from]);
        return;
    }
    out[0] = '\0';
    gPortCardExtraForSave(to, sExtra[from], 1, out, (int)sizeof(out));
    portCardSlotExtraSet(to, out);
}

void portCardExtraErase(int slot) {
    portCardSlotExtraSet(slot, "");
}

void portCardExtraKeepNext(int slot) {
    if (slot >= 0 && slot < 4) {
        sExtraKeep[slot] = 1;
    }
}

void portCardExtraLoaded(int slot) {
    if (slot < 0 || slot >= 4) {
        return;
    }
    if (sExtraKeep[slot]) {
        sExtraKeep[slot] = 0;
        return;
    }
    if (gPortCardExtraApply != NULL) {
        gPortCardExtraApply(slot, sExtra[slot]);
    }
}

/* a slot image's block, from what is kept for its slot (main and backup) */
static void putExtra(int index, uint8_t* image) {
    const char* text = sExtra[index & 3];
    const size_t n = strlen(text), magic = sizeof(PORT_CARD_EXTRA_MAGIC) - 1;
    uint8_t* at = image + PORT_CARD_EXTRA_AT;
    memset(at, 0, PORT_CARD_EXTRA_SIZE);
    if (n >= magic && memcmp(text, PORT_CARD_EXTRA_MAGIC, magic) == 0) {
        memcpy(at, text, n < PORT_CARD_EXTRA_SIZE - 1 ? n : PORT_CARD_EXTRA_SIZE - 1);
    }
}

/* a slot just read: its block kept (the main copy's; the backup's when the
 * main has none) */
static void takeExtra(int index, const uint8_t* image) {
    const char* text = portCardImageExtra(image);
    if (index < 4) {
        portCardSlotExtraSet(index, text);
    } else if (sExtra[index & 3][0] == '\0' && text != NULL) {
        portCardSlotExtraSet(index & 3, text);
    }
}

void* portCardWriteImage(const void* native, int32_t length, int32_t offset) {
    if (gPortCardWillWrite != NULL) {
        gPortCardWillWrite();
    }
    if (isSlot(length, offset)) {
        const int index = slotIndex(offset);
        portCardSlotToImage(native, sWriteImage);
        putExtra(index, sWriteImage);
        if (index < 4 && gPortCardSlotWritten != NULL) {
            gPortCardSlotWritten(index, sWriteImage);
        }
    } else if (isMeta(length, offset)) {
        portCardMetaToImage(native, sWriteImage);
    } else if (length > 0 && length <= (int32_t)sizeof(sWriteImage)) {
        memcpy(sWriteImage, native, (size_t)length); /* not a known region: raw */
    } else {
        return (void*)native;
    }
    return sWriteImage;
}

void* portCardReadBegin(void* native, int32_t length, int32_t offset) {
    if (length <= 0 || length > (int32_t)sizeof(sReadImage)) {
        sReadDst = NULL;
        return native;
    }
    sReadDst = native;
    sReadLen = length;
    sReadOff = offset;
    return sReadImage;
}

void portCardReadEnd(int32_t result) {
    void* dst = sReadDst;
    sReadDst = NULL;
    if (!dst || result < 0) {
        return;
    }
    if (isSlot(sReadLen, sReadOff)) {
        portCardSlotFromImage(sReadImage, dst);
        takeExtra(slotIndex(sReadOff), sReadImage);
    } else if (isMeta(sReadLen, sReadOff)) {
        portCardMetaFromImage(sReadImage, dst);
    } else {
        memcpy(dst, sReadImage, (size_t)sReadLen);
    }
}
