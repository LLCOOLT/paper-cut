/* Host unit test for the big-endian decoder and the TPL path (no disc).
 * Builds a synthetic big-endian TPL in memory (3 descriptors: 2 textures, 1
 * palette, one shared texture header and one shared palette header), decodes
 * it with portTplDecode and checks the native graph field by field; then
 * exercises bounds checks and the remaining descriptor features. */
#include "port/bedecode.h"
#include "port/tpl_pc.h"
#include "sdk/texPalette.h"

#include <setjmp.h>
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

/* ---------------------------------------------------- BE TPL serializer */
enum {
    OFF_DESC = 0x0C, OFF_TEXA = 0x24, OFF_TEXB = 0x48, OFF_CLUT = 0x6C,
    OFF_DATA_A = 0x80, OFF_DATA_B = 0xA0, OFF_DATA_C = 0xC0, TPL_SIZE = 0xE0
};

static void putTexHeader(uint8_t* p, uint16_t h, uint16_t w, uint32_t fmt, uint32_t data, uint32_t wrapS,
                         uint32_t wrapT, uint32_t minF, uint32_t magF, float bias, uint8_t edge, uint8_t minLod,
                         uint8_t maxLod) {
    union { float f; uint32_t u; } b;
    b.f = bias;
    portPutBE16(p + 0x00, h);
    portPutBE16(p + 0x02, w);
    portPutBE32(p + 0x04, fmt);
    portPutBE32(p + 0x08, data);
    portPutBE32(p + 0x0C, wrapS);
    portPutBE32(p + 0x10, wrapT);
    portPutBE32(p + 0x14, minF);
    portPutBE32(p + 0x18, magF);
    portPutBE32(p + 0x1C, b.u);
    p[0x20] = edge;
    p[0x21] = minLod;
    p[0x22] = maxLod;
    p[0x23] = 0; /* unpacked: 0 on disc */
}

static uint8_t* buildTpl(void) {
    uint8_t* t = (uint8_t*)malloc(TPL_SIZE) /* 16-aligned */;
    int i;
    memset(t, 0, TPL_SIZE);
    portPutBE32(t + 0x0, PORT_TPL_VERSION);
    portPutBE32(t + 0x4, 3);
    portPutBE32(t + 0x8, OFF_DESC);
    /* desc0 {texA, clut}  desc1 {texB, clut}  desc2 {texA, none} */
    portPutBE32(t + OFF_DESC + 0x00, OFF_TEXA);
    portPutBE32(t + OFF_DESC + 0x04, OFF_CLUT);
    portPutBE32(t + OFF_DESC + 0x08, OFF_TEXB);
    portPutBE32(t + OFF_DESC + 0x0C, OFF_CLUT);
    portPutBE32(t + OFF_DESC + 0x10, OFF_TEXA);
    portPutBE32(t + OFF_DESC + 0x14, 0);
    putTexHeader(t + OFF_TEXA, 4, 8, 9 /* GX_TF_C8 */, OFF_DATA_A, 0, 1, 1, 1, -0.5f, 1, 0, 0);
    putTexHeader(t + OFF_TEXB, 16, 2, 5 /* GX_TF_RGB5A3 */, OFF_DATA_B, 2, 2, 5, 1, 1.25f, 0, 1, 3);
    portPutBE16(t + OFF_CLUT + 0, 16);
    t[OFF_CLUT + 2] = 0;
    t[OFF_CLUT + 3] = 0xAB;
    portPutBE32(t + OFF_CLUT + 4, 2 /* GX_TL_RGB5A3 */);
    portPutBE32(t + OFF_CLUT + 8, OFF_DATA_C);
    for (i = OFF_DATA_A; i < TPL_SIZE; i++) {
        t[i] = (uint8_t)i;
    }
    return t;
}

static void testTpl(void) {
    uint8_t* blob = buildTpl();
    uint8_t payload[TPL_SIZE - OFF_DATA_A];
    TEXPalette* pal;
    TEXDescriptor* d;
    uint32_t live0 = portTplLiveCount();

    memcpy(payload, blob + OFF_DATA_A, sizeof(payload));
    pal = portTplDecode(blob, TPL_SIZE, "synthetic.tpl");
    CHECK(pal == (TEXPalette*)blob); /* native root in place */
    CHECK(pal->versionNumber == PORT_TPL_VERSION);
    CHECK(pal->numDescriptors == 3);
    d = pal->descriptorArray;
    CHECK(d != NULL);
    CHECK((uint8_t*)d < blob || (uint8_t*)d >= blob + TPL_SIZE); /* side allocation */

    /* texture A */
    CHECK(d[0].textureHeader != NULL);
    CHECK(d[0].textureHeader->height == 4);
    CHECK(d[0].textureHeader->width == 8);
    CHECK(d[0].textureHeader->format == 9);
    CHECK((uint8_t*)d[0].textureHeader->data == blob + OFF_DATA_A);
    CHECK(d[0].textureHeader->wrapS == 0);
    CHECK(d[0].textureHeader->wrapT == 1);
    CHECK(d[0].textureHeader->minFilter == 1);
    CHECK(d[0].textureHeader->magFilter == 1);
    CHECK(d[0].textureHeader->LODBias == -0.5f);
    CHECK(d[0].textureHeader->edgeLODEnable == 1);
    CHECK(d[0].textureHeader->minLOD == 0);
    CHECK(d[0].textureHeader->maxLOD == 0);
    CHECK(d[0].textureHeader->unpacked == 1);
    /* texture B */
    CHECK(d[1].textureHeader->height == 16);
    CHECK(d[1].textureHeader->width == 2);
    CHECK(d[1].textureHeader->format == 5);
    CHECK((uint8_t*)d[1].textureHeader->data == blob + OFF_DATA_B);
    CHECK(d[1].textureHeader->wrapS == 2);
    CHECK(d[1].textureHeader->wrapT == 2);
    CHECK(d[1].textureHeader->minFilter == 5);
    CHECK(d[1].textureHeader->magFilter == 1);
    CHECK(d[1].textureHeader->LODBias == 1.25f);
    CHECK(d[1].textureHeader->edgeLODEnable == 0);
    CHECK(d[1].textureHeader->minLOD == 1);
    CHECK(d[1].textureHeader->maxLOD == 3);
    CHECK(d[1].textureHeader->unpacked == 1);
    /* palette, shared by desc0 and desc1 */
    CHECK(d[0].CLUTHeader != NULL);
    CHECK(d[0].CLUTHeader == d[1].CLUTHeader);
    CHECK(d[0].CLUTHeader->numEntries == 16);
    CHECK(d[0].CLUTHeader->unpacked == 1);
    CHECK(d[0].CLUTHeader->pad8 == 0xAB);
    CHECK(d[0].CLUTHeader->format == 2);
    CHECK((uint8_t*)d[0].CLUTHeader->data == blob + OFF_DATA_C);
    /* shared texture header stays shared; NULL stays NULL */
    CHECK(d[2].textureHeader == d[0].textureHeader);
    CHECK(d[2].CLUTHeader == NULL);
    /* payloads untouched (still big-endian for Aurora) */
    CHECK(memcmp(blob + OFF_DATA_A, payload, sizeof(payload)) == 0);
    CHECK(portTplLiveCount() == live0 + 1);

    /* idempotent: a second unpack (GC callers do this) changes nothing */
    CHECK(portTplDecode(blob, TPL_SIZE, "synthetic.tpl") == pal);
    CHECK(pal->descriptorArray == d);
    CHECK(portTplLiveCount() == live0 + 1);

    /* a fresh TPL at the same address replaces the old graph */
    {
        uint8_t* again = buildTpl();
        memcpy(blob, again, TPL_SIZE);
        free(again);
        pal = portTplDecode(blob, TPL_SIZE, "synthetic.tpl");
        CHECK(pal && pal->numDescriptors == 3 && pal->descriptorArray[1].textureHeader->maxLOD == 3);
        CHECK(portTplLiveCount() == live0 + 1);
    }
    portTplRelease(blob);
    CHECK(portTplLiveCount() == live0);
    free(blob);
}

/* ------------------------------------------------------- error handling */
static jmp_buf sJmp;
static char sLastError[256];
static void onError(PortDecodeCtx* ctx, const char* msg) {
    (void)ctx;
    snprintf(sLastError, sizeof(sLastError), "%s", msg);
    longjmp(sJmp, 1);
}

extern const PortType portBE_tpl_TEXPalette;

static int decodeFails(uint8_t* blob, uint32_t size) {
    PortDecodeCtx ctx;
    int failed;
    portDecodeBegin(&ctx, blob, size, "bad.tpl");
    ctx.onError = onError;
    sLastError[0] = 0;
    if (setjmp(sJmp) == 0) {
        portDecode(&ctx, &portBE_tpl_TEXPalette, 0);
        failed = 0;
    } else {
        failed = 1;
    }
    portDecodeEnd(&ctx);
    portDecodeArenaFree(&ctx.arena);
    return failed;
}

static void testBounds(void) {
    uint8_t* t = buildTpl();
    CHECK(!decodeFails(t, TPL_SIZE));
    CHECK(decodeFails(t, 0x8)); /* header itself truncated */
    portPutBE32(t + OFF_DESC + 0x08, TPL_SIZE - 4); /* texB header runs off the end */
    CHECK(decodeFails(t, TPL_SIZE));
    CHECK(strstr(sLastError, "TEXHeader") != NULL);
    free(t);

    t = buildTpl();
    portPutBE32(t + 0x4, 0x10000000); /* absurd descriptor count */
    CHECK(decodeFails(t, TPL_SIZE));
    free(t);

    t = buildTpl();
    portPutBE32(t + OFF_TEXA + 0x08, 0xFFFFFFF0); /* texel payload out of range */
    CHECK(decodeFails(t, TPL_SIZE));
    free(t);

    /* portTplDecode: bad version, and texel data inside the in-place root */
    portTplErrorHook = onError;
    t = buildTpl();
    portPutBE32(t, 0x0020AF31);
    if (setjmp(sJmp) == 0) {
        portTplDecode(t, TPL_SIZE, "bad.tpl");
        CHECK(0);
    } else {
        CHECK(strstr(sLastError, "version") != NULL);
    }
    free(t);
    t = buildTpl();
    portPutBE32(t + OFF_TEXB + 0x08, 0x4);
    if (setjmp(sJmp) == 0) {
        portTplDecode(t, TPL_SIZE, "bad.tpl");
        CHECK(0);
    } else {
        CHECK(strstr(sLastError, "in-place") != NULL);
    }
    free(t);
    portTplErrorHook = NULL;
}

/* -------------------------------- other descriptor features (hand tables) */
/* Hand-written only to unit-test the runtime; the game's tables are generated. */
typedef struct Inner { uint16_t a; int8_t b; float c; } Inner;
typedef struct Node {
    uint32_t id;
    Inner in[2];
    const char* name;
    struct Node* next;
    struct Node* list; /* terminated by id == 0xFFFFFFFF */
    int16_t s[3];
    void* runtime;
    uint32_t tag;
} Node;
/* disc layout: id@0 in[2]@4 (each 8: a@0 b@2 c@4) name@0x14 next@0x18 list@0x1C s[3]@0x20 runtime@0x28 tag@0x2C = 0x30 */
extern const PortType tInner, tNode;
static const PortField fInner[] = {
    { "a", 0, offsetof(Inner, a), PORT_FK_U16, 0, 0, 0, 0, 0, 0, 0, 2, 0, NULL, NULL },
    { "b", 2, offsetof(Inner, b), PORT_FK_S8, 0, 0, 0, 0, 0, 0, 0, 1, 0, NULL, NULL },
    { "c", 4, offsetof(Inner, c), PORT_FK_F32, 0, 0, 0, 0, 0, 0, 0, 4, 0, NULL, NULL },
};
const PortType tInner = { "Inner", 8, sizeof(Inner), _Alignof(Inner), 3, fInner };
static const PortField fNode[] = {
    { "id", 0, offsetof(Node, id), PORT_FK_U32, 0, 0, 0, 0, 0, 0, 0, 4, 0, NULL, NULL },
    { "in", 4, offsetof(Node, in), PORT_FK_EMBED, PORT_CNT_ONE, 0, 1, 2, 0, 8, sizeof(Inner), sizeof(Inner), 0, &tInner, NULL },
    { "name", 0x14, offsetof(Node, name), PORT_FK_PTR_STRING, 0, 0, 0, 0, 0, 0, 0, sizeof(void*), 0, NULL, NULL },
    { "next", 0x18, offsetof(Node, next), PORT_FK_PTR_STRUCT, PORT_CNT_ONE, 0, 0, 0, 0, 0, 0, sizeof(void*), 0, &tNode, NULL },
    { "list", 0x1C, offsetof(Node, list), PORT_FK_PTR_STRUCT, PORT_CNT_UNTIL, 4, 0, 0, 0, 0, 0, sizeof(void*), 0xFFFFFFFF, &tNode, NULL },
    { "s", 0x20, offsetof(Node, s), PORT_FK_S16, 0, 0, 1, 3, 0, 2, 2, 2, 0, NULL, NULL },
    { "runtime", 0x28, offsetof(Node, runtime), PORT_FK_PTR_NULL, 0, 0, 0, 0, 0, 0, 0, sizeof(void*), 0, NULL, NULL },
    { "tag", 0x2C, offsetof(Node, tag), PORT_FK_CONST, 0, 0, 0, 0, 0, 0, 0, 4, 0x1234, NULL, NULL },
};
const PortType tNode = { "Node", 0x30, sizeof(Node), _Alignof(Node), 8, fNode };

static void putNode(uint8_t* b, uint32_t o, uint32_t id, uint32_t name, uint32_t next, uint32_t list) {
    portPutBE32(b + o, id);
    portPutBE16(b + o + 4, (uint16_t)(id + 1));
    b[o + 6] = 0xFE; /* -2 */
    portPutBE32(b + o + 8, 0x3F800000); /* 1.0f */
    portPutBE16(b + o + 0xC, (uint16_t)(id + 2));
    b[o + 0xE] = 3;
    portPutBE32(b + o + 0x10, 0x40000000); /* 2.0f */
    portPutBE32(b + o + 0x14, name);
    portPutBE32(b + o + 0x18, next);
    portPutBE32(b + o + 0x1C, list);
    portPutBE16(b + o + 0x20, 0xFFFF);
    portPutBE16(b + o + 0x22, 7);
    portPutBE16(b + o + 0x24, 0x8000);
    portPutBE32(b + o + 0x28, 0xDEADBEEF);
}

static void testFeatures(void) {
    uint8_t b[0x200];
    PortDecodeCtx ctx;
    Node* n;
    memset(b, 0, sizeof(b));
    /* node0 @0x10 -> next node1 @0x40 -> next node0 (cycle); node0.list @0x70: 2 nodes + terminator */
    putNode(b, 0x10, 10, 0x180, 0x40, 0x70);
    putNode(b, 0x40, 20, 0, 0x10, 0);
    putNode(b, 0x70, 30, 0x180, 0, 0);
    putNode(b, 0xA0, 40, 0, 0x40, 0);
    putNode(b, 0xD0, 0xFFFFFFFF, 0, 0, 0);
    memcpy(b + 0x180, "hello", 6);

    portDecodeBegin(&ctx, b, sizeof(b), "nodes");
    n = (Node*)portDecode(&ctx, &tNode, 0x10);
    portDecodeEnd(&ctx);
    CHECK(n && n->id == 10);
    CHECK(n->in[0].a == 11 && n->in[0].b == -2 && n->in[0].c == 1.0f);
    CHECK(n->in[1].a == 12 && n->in[1].b == 3 && n->in[1].c == 2.0f);
    CHECK(n->name && strcmp(n->name, "hello") == 0 && (const uint8_t*)n->name == b + 0x180);
    CHECK(n->next && n->next->id == 20 && n->next->next == n); /* cycle preserved */
    CHECK(n->next->name == NULL && n->next->list == NULL);
    CHECK(n->list && n->list[0].id == 30 && n->list[1].id == 40 && n->list[2].id == 0xFFFFFFFF);
    CHECK(n->list[1].next == n->next); /* element pointing at an already decoded node */
    CHECK(n->s[0] == -1 && n->s[1] == 7 && n->s[2] == -32768);
    CHECK(n->runtime == NULL);
    CHECK(n->tag == 0x1234);
    portDecodeArenaFree(&ctx.arena);

    /* unterminated string */
    memset(b + 0x180, 'x', sizeof(b) - 0x180);
    portDecodeBegin(&ctx, b, sizeof(b), "nodes");
    ctx.onError = onError;
    if (setjmp(sJmp) == 0) {
        portDecode(&ctx, &tNode, 0x10);
        CHECK(0);
    } else {
        CHECK(strstr(sLastError, "not terminated") != NULL);
    }
    portDecodeEnd(&ctx);
    portDecodeArenaFree(&ctx.arena);
}

int main(void) {
    testTpl();
    testBounds();
    testFeatures();
    printf("bedecode_test: %d checks, %d failures\n", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
